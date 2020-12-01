#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include <assert.h>

/* Tell Jumps */
typedef struct {
	gc_ssize pos;
	gc_size delta;
} gau_TellJumpData;

typedef struct {
	gc_Link link;
	gau_TellJumpData data;
} gau_TellJumpLink;

void gauX_tell_jump_push(gc_Link *head, gc_ssize pos, gc_size delta) {
	gau_TellJumpLink* link = gcX_ops->allocFunc(sizeof(gau_TellJumpLink));
	link->link.data = &link->data;
	link->data.pos = pos;
	link->data.delta = delta;
	gc_list_link(head->prev, (gc_Link*)link, &link->data);
}

gc_bool gauX_tell_jump_peek(gc_Link *head, gc_ssize *pos, gc_size *delta) {
	gau_TellJumpLink* link;
	if (head->next == head) return gc_false;
	link = (gau_TellJumpLink*)head->next;
	*pos = link->data.pos;
	*delta = link->data.delta;
	return gc_true;
}

gc_bool gauX_tell_jump_pop(gc_Link *head) {
	gau_TellJumpLink *link;
	if (head->next == head) return gc_false;
	link = (gau_TellJumpLink*)head->next;
	gc_list_unlink((gc_Link*)link);
	gcX_ops->freeFunc(link);
	return gc_true;
}

gc_size gauX_tell_jump_process(gc_Link *head, gc_size advance) {
	gc_size ret = 0;
	gau_TellJumpLink* link = (gau_TellJumpLink*)head->next;
	while ((gc_Link*)link != head) {
		gau_TellJumpLink* oldLink = link;
		link = (gau_TellJumpLink*)link->link.next;
		oldLink->data.pos -= advance;
		if (oldLink->data.pos <= 0) {
			ret += oldLink->data.delta;
			gc_list_unlink((gc_Link*)oldLink);
		}
	}

	return ret;
}

void gauX_tell_jump_clear(gc_Link *head) {
	while (gauX_tell_jump_pop(head));
}


/* Stream Link */
typedef struct {
	gc_Link link;
	gc_atomic_uint32 refCount;
	gc_Mutex* produce_mutex;
	ga_BufferedStream* stream;
} gaX_StreamLink;

gaX_StreamLink *gaX_stream_link_create() {
	gaX_StreamLink* ret = (gaX_StreamLink*)gcX_ops->allocFunc(sizeof(gaX_StreamLink));
	ret->refCount = 1;
	ret->produce_mutex = gc_mutex_create();
	ret->stream = 0;
	return ret;
}

// returns true if the stream is dead
gc_bool gaX_stream_link_produce(gaX_StreamLink *stream_link) {
	gc_bool ret = gc_true;
	gc_mutex_lock(stream_link->produce_mutex);
	if (stream_link->stream) {
		/* Mutexing this entire section guarantees that ga_stream_destroy()
		   cannot occur during production */
		ga_stream_produce(stream_link->stream);
		ret = gc_false;
	}
	gc_mutex_unlock(stream_link->produce_mutex);
	return ret;
}

void gaX_stream_link_kill(gaX_StreamLink *stream_link) {
	gc_mutex_lock(stream_link->produce_mutex);
	stream_link->stream = 0;
	gc_mutex_unlock(stream_link->produce_mutex);
}

void gaX_stream_link_destroy(gaX_StreamLink* stream_link) {
	gc_mutex_destroy(stream_link->produce_mutex);
	gcX_ops->freeFunc(stream_link);
}

void gaX_stream_link_acquire(gaX_StreamLink *stream_link) {
	atomic_fetch_add(&stream_link->refCount, 1);
}
void gaX_stream_link_release(gaX_StreamLink *stream_link) {
	if (gcX_decref(&stream_link->refCount)) gaX_stream_link_destroy(stream_link);
}

/* Stream Manager */
ga_StreamManager *ga_stream_manager_create(void) {
	ga_StreamManager* ret = gcX_ops->allocFunc(sizeof(ga_StreamManager));
	ret->mutex = gc_mutex_create();
	gc_list_head(&ret->stream_list);
	return ret;
}
gaX_StreamLink *gaX_stream_manager_add(ga_StreamManager *mgr, ga_BufferedStream *stream) {
	gaX_StreamLink* stream_link = gaX_stream_link_create(stream);
	gaX_stream_link_acquire(stream_link); /* The new client adds its own refcount */
	/* It's safe to add() while iterating in stream() because of implicit fault tolerance */
	/* That is, all possible outcomes are valid, despite the race condition */
	/* This is true because we are guaranteed to stream() on the same thread that calls remove() */
	gc_mutex_lock(mgr->mutex);
	stream_link->stream = stream;
	gc_list_link(&mgr->stream_list, (gc_Link*)stream_link, stream_link);
	gc_mutex_unlock(mgr->mutex);
	return stream_link;
}
void ga_stream_manager_buffer(ga_StreamManager *mgr) {
	gc_Link *link = mgr->stream_list.next;
	while (link != &mgr->stream_list) {
		gaX_StreamLink* stream_link;
		stream_link = (gaX_StreamLink*)link->data;
		link = link->next;
		gc_bool streamDead = gaX_stream_link_produce(stream_link);
		if (streamDead) {
			gc_mutex_lock(mgr->mutex);
			gc_list_unlink((gc_Link*)stream_link);
			gc_mutex_unlock(mgr->mutex);
			gaX_stream_link_release(stream_link);
		}
	}
}
void ga_stream_manager_destroy(ga_StreamManager *mgr) {
	gc_Link *link;
	link = mgr->stream_list.next;
	while (link != &mgr->stream_list) {
		gaX_StreamLink* oldLink = (gaX_StreamLink*)link;
		link = link->next;
		gaX_stream_link_release(oldLink);
	}
	gc_mutex_destroy(mgr->mutex);
	gcX_ops->freeFunc(mgr);
}

/* Stream */
ga_BufferedStream *ga_stream_create(ga_StreamManager *mgr, ga_SampleSource *src, gc_size buffer_size) {
	ga_BufferedStream* ret = gcX_ops->allocFunc(sizeof(ga_BufferedStream));
	ret->refCount = 1;
	ga_sample_source_acquire(src);
	ga_sample_source_format(src, &ret->format);
	gc_list_head(&ret->tell_jumps);
	ret->inner_src = src;
	ret->next_sample = 0;
	ret->seek = 0;
	ret->tell = 0;
	ret->end = 0;
	ret->buffer_size = buffer_size;
	ret->flags = ga_sample_source_flags(src);
	assert(ret->flags & GaDataAccessFlag_Threadsafe);
	ret->produce_mutex = gc_mutex_create();
	ret->seek_mutex = gc_mutex_create();
	ret->read_mutex = gc_mutex_create();
	ret->buffer = gc_buffer_create(buffer_size);
	ret->stream_link = (gc_Link*)gaX_stream_manager_add(mgr, ret);
	return ret;
}
static void gaX_stream_onSeek(gc_int32 sample, gc_int32 delta, void *seekContext) {
	ga_BufferedStream* s = (ga_BufferedStream*)seekContext;
	gc_size samplesAvail;
	ga_Format fmt;
	ga_sample_source_format(s->inner_src, &fmt);
	gc_uint32 sample_size = ga_format_sample_size(&fmt);
	gc_mutex_lock(s->read_mutex);
	gc_mutex_lock(s->seek_mutex);
	samplesAvail = gc_buffer_bytesAvail(s->buffer) / sample_size;
	gauX_tell_jump_push(&s->tell_jumps, samplesAvail + sample, delta);
	gc_mutex_unlock(s->seek_mutex);
	gc_mutex_unlock(s->read_mutex);
}
gc_int32 gaX_read_samples_into_stream(ga_BufferedStream *stream,
                                      gc_CircBuffer *b,
                                      gc_int32 samples,
				      ga_SampleSource *sampleSrc) {
	void *dataA;
	void *dataB;
	gc_size sizeA = 0;
	gc_size sizeB = 0;
	gc_size numBuffers;
	gc_size numWritten = 0;
	ga_Format fmt;
	gc_uint32 sample_size;
	ga_sample_source_format(sampleSrc, &fmt);
	sample_size = ga_format_sample_size(&fmt);
	numBuffers = gc_buffer_getFree(b, samples * sample_size, &dataA, &sizeA, &dataB, &sizeB);
	if (numBuffers >= 1) {
		numWritten = ga_sample_source_read(sampleSrc, dataA, sizeA / sample_size, &gaX_stream_onSeek, stream);
		if (numBuffers == 2 && numWritten == sizeA)
			numWritten += ga_sample_source_read(sampleSrc, dataB, sizeB / sample_size, &gaX_stream_onSeek, stream);
	}
	gc_buffer_produce(b, numWritten * sample_size);
	return numWritten;
}
void ga_stream_produce(ga_BufferedStream *s) {
	gc_CircBuffer* b = s->buffer;
	gc_uint32 sample_size = ga_format_sample_size(&s->format);
	gc_size bytes_free = gc_buffer_bytesFree(b);
	if (s->seek >= 0) {
		gc_mutex_lock(s->read_mutex);
		gc_mutex_lock(s->seek_mutex);

		/* Check again now that we're mutexed */
		if (s->seek >= 0) {
			gc_int32 samplePos = s->seek;
			s->tell = samplePos;
			s->seek = -1;
			s->next_sample = samplePos;
			ga_sample_source_seek(s->inner_src, samplePos);
			gc_buffer_consume(s->buffer, gc_buffer_bytesAvail(s->buffer)); /* Clear buffer */
			gauX_tell_jump_clear(&s->tell_jumps); /* Clear tell-jump list */
		}
		gc_mutex_unlock(s->seek_mutex);
		gc_mutex_unlock(s->read_mutex);
	}

	while (bytes_free) {
		gc_int32 samplesWritten = 0;
		gc_int32 bytesWritten = 0;
		gc_int32 bytesToWrite = bytes_free;
		samplesWritten = gaX_read_samples_into_stream(s, b, bytesToWrite / sample_size, s->inner_src);
		bytesWritten = samplesWritten * sample_size;
		bytes_free -= bytesWritten;
		s->next_sample += samplesWritten;
		if (bytesWritten < bytesToWrite && ga_sample_source_end(s->inner_src)) {
			s->end = 1;
			break;
		}
	}
}
gc_size ga_stream_read(ga_BufferedStream *s, void *dst, gc_size num_samples) {
	gc_CircBuffer* b = s->buffer;
	gc_int32 delta;

	/* Read the samples */
	gc_size samplesConsumed = 0;
	gc_mutex_lock(s->read_mutex);

	void *dataA;
	void *dataB;
	gc_size sizeA, sizeB;
	gc_size totalBytes = 0;
	gc_size sample_size = ga_format_sample_size(&s->format);
	gc_size dstBytes = num_samples * sample_size;
	gc_size avail = gc_buffer_bytesAvail(b);
	dstBytes = dstBytes > avail ? avail : dstBytes;
	if (gc_buffer_getAvail(b, dstBytes, &dataA, &sizeA, &dataB, &sizeB) >= 1) {
		gc_size bytesToRead = dstBytes < sizeA ? dstBytes : sizeA;
		memcpy(dst, dataA, bytesToRead);
		totalBytes += bytesToRead;
		if (dstBytes > 0 && dataB) {
			gc_ssize dstBytesLeft = dstBytes - bytesToRead;
			bytesToRead = dstBytesLeft < (gc_int32)sizeB ? dstBytesLeft : (gc_int32)sizeB;
			memcpy((char*)dst + totalBytes, dataB, bytesToRead);
			totalBytes += bytesToRead;
		}
	}
	samplesConsumed = totalBytes / sample_size;
	gc_buffer_consume(b, totalBytes);

	/* Update the tell pos */
	gc_mutex_lock(s->seek_mutex);
	s->tell += samplesConsumed;
	delta = gauX_tell_jump_process(&s->tell_jumps, samplesConsumed);
	s->tell += delta;
	gc_mutex_unlock(s->seek_mutex);
	gc_mutex_unlock(s->read_mutex);
	return samplesConsumed;
}
gc_bool ga_stream_ready(ga_BufferedStream *s, gc_size num_samples) {
	gc_size avail = gc_buffer_bytesAvail(s->buffer);
	return s->end || (avail >= num_samples * ga_format_sample_size(&s->format) && avail > s->buffer_size / 2.0f);
}
gc_bool ga_stream_end(ga_BufferedStream *s) {
	gc_CircBuffer* b = s->buffer;
	gc_int32 bytesAvail = gc_buffer_bytesAvail(b);
	return s->end && bytesAvail == 0;
}
gc_result ga_stream_seek(ga_BufferedStream *s, gc_size sampleOffset) {
	gc_mutex_lock(s->seek_mutex);
	s->seek = sampleOffset;
	gc_mutex_unlock(s->seek_mutex);
	return GC_SUCCESS;
}
gc_result ga_stream_tell(ga_BufferedStream *s, gc_size *samples, gc_size *totalSamples) {
	gc_result res = ga_sample_source_tell(s->inner_src, samples, totalSamples);
	if (res != GC_SUCCESS) return res;
	gc_mutex_lock(s->seek_mutex);
	if (samples) *samples = s->seek >= 0 ? s->seek : s->tell;
	gc_mutex_unlock(s->seek_mutex);
	return GC_SUCCESS;
}
gc_int32 ga_stream_flags(ga_BufferedStream *s) {
	return s->flags;
}
void gaX_stream_destroy(ga_BufferedStream *s) {
	gaX_stream_link_kill((gaX_StreamLink*)s->stream_link); /* This must be done first, so that the stream remains valid until it killed */
	gaX_stream_link_release((gaX_StreamLink*)s->stream_link);
	gc_mutex_destroy(s->produce_mutex);
	gc_mutex_destroy(s->seek_mutex);
	gc_mutex_destroy(s->read_mutex);
	gc_buffer_destroy(s->buffer);
	gauX_tell_jump_clear(&s->tell_jumps);
	ga_sample_source_release(s->inner_src);
	gcX_ops->freeFunc(s);
}

void ga_stream_acquire(ga_BufferedStream *stream) {
	atomic_fetch_add(&stream->refCount, 1);
}
void ga_stream_release(ga_BufferedStream *stream) {
	if (gcX_decref(&stream->refCount)) gaX_stream_destroy(stream);
}
