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
	GaLink link;
	gau_TellJumpData data;
} gau_TellJumpLink;

void gauX_tell_jump_push(GaLink *head, gc_ssize pos, gc_size delta) {
	gau_TellJumpLink* link = gcX_ops->allocFunc(sizeof(gau_TellJumpLink));
	link->link.data = &link->data;
	link->data.pos = pos;
	link->data.delta = delta;
	ga_list_link(head->prev, (GaLink*)link, &link->data);
}

gc_bool gauX_tell_jump_peek(GaLink *head, gc_ssize *pos, gc_size *delta) {
	gau_TellJumpLink* link;
	if (head->next == head) return gc_false;
	link = (gau_TellJumpLink*)head->next;
	*pos = link->data.pos;
	*delta = link->data.delta;
	return gc_true;
}

gc_bool gauX_tell_jump_pop(GaLink *head) {
	gau_TellJumpLink *link;
	if (head->next == head) return gc_false;
	link = (gau_TellJumpLink*)head->next;
	ga_list_unlink((GaLink*)link);
	gcX_ops->freeFunc(link);
	return gc_true;
}

gc_size gauX_tell_jump_process(GaLink *head, gc_size advance) {
	gc_size ret = 0;
	gau_TellJumpLink* link = (gau_TellJumpLink*)head->next;
	while ((GaLink*)link != head) {
		gau_TellJumpLink* oldLink = link;
		link = (gau_TellJumpLink*)link->link.next;
		oldLink->data.pos -= advance;
		if (oldLink->data.pos <= 0) {
			ret += oldLink->data.delta;
			ga_list_unlink((GaLink*)oldLink);
		}
	}

	return ret;
}

void gauX_tell_jump_clear(GaLink *head) {
	while (gauX_tell_jump_pop(head));
}


/* Stream Link */
typedef struct {
	GaLink link;
	gc_atomic_uint32 refCount;
	GaMutex* produce_mutex;
	GaBufferedStream* stream;
} gaX_StreamLink;

gaX_StreamLink *gaX_stream_link_create(void) {
	gaX_StreamLink* ret = (gaX_StreamLink*)gcX_ops->allocFunc(sizeof(gaX_StreamLink));
	ret->refCount = 1;
	ret->produce_mutex = ga_mutex_create();
	ret->stream = NULL;
	return ret;
}

// returns true if the stream is dead
gc_bool gaX_stream_link_produce(gaX_StreamLink *stream_link) {
	gc_bool ret = gc_true;
	ga_mutex_lock(stream_link->produce_mutex);
	if (stream_link->stream) {
		/* Mutexing this entire section guarantees that ga_stream_destroy()
		   cannot occur during production */
		ga_stream_produce(stream_link->stream);
		ret = gc_false;
	}
	ga_mutex_unlock(stream_link->produce_mutex);
	return ret;
}

void gaX_stream_link_kill(gaX_StreamLink *stream_link) {
	ga_mutex_lock(stream_link->produce_mutex);
	stream_link->stream = 0;
	ga_mutex_unlock(stream_link->produce_mutex);
}

void gaX_stream_link_destroy(gaX_StreamLink* stream_link) {
	ga_mutex_destroy(stream_link->produce_mutex);
	gcX_ops->freeFunc(stream_link);
}

void gaX_stream_link_acquire(gaX_StreamLink *stream_link) {
	atomic_fetch_add(&stream_link->refCount, 1);
}
void gaX_stream_link_release(gaX_StreamLink *stream_link) {
	if (gcX_decref(&stream_link->refCount)) gaX_stream_link_destroy(stream_link);
}

/* Stream Manager */
GaStreamManager *ga_stream_manager_create(void) {
	GaStreamManager* ret = gcX_ops->allocFunc(sizeof(GaStreamManager));
	ret->mutex = ga_mutex_create();
	ga_list_head(&ret->stream_list);
	return ret;
}
gaX_StreamLink *gaX_stream_manager_add(GaStreamManager *mgr, GaBufferedStream *stream) {
	gaX_StreamLink* stream_link = gaX_stream_link_create();
	gaX_stream_link_acquire(stream_link); /* The new client adds its own refcount */
	/* It's safe to add() while iterating in stream() because of implicit fault tolerance */
	/* That is, all possible outcomes are valid, despite the race condition */
	/* This is true because we are guaranteed to stream() on the same thread that calls remove() */
	ga_mutex_lock(mgr->mutex);
	stream_link->stream = stream;
	ga_list_link(&mgr->stream_list, (GaLink*)stream_link, stream_link);
	ga_mutex_unlock(mgr->mutex);
	return stream_link;
}
void ga_stream_manager_buffer(GaStreamManager *mgr) {
	GaLink *link = mgr->stream_list.next;
	while (link != &mgr->stream_list) {
		gaX_StreamLink* stream_link;
		stream_link = (gaX_StreamLink*)link->data;
		link = link->next;
		gc_bool streamDead = gaX_stream_link_produce(stream_link);
		if (streamDead) {
			ga_mutex_lock(mgr->mutex);
			ga_list_unlink((GaLink*)stream_link);
			ga_mutex_unlock(mgr->mutex);
			gaX_stream_link_release(stream_link);
		}
	}
}
void ga_stream_manager_destroy(GaStreamManager *mgr) {
	GaLink *link;
	link = mgr->stream_list.next;
	while (link != &mgr->stream_list) {
		gaX_StreamLink* oldLink = (gaX_StreamLink*)link;
		link = link->next;
		gaX_stream_link_release(oldLink);
	}
	ga_mutex_destroy(mgr->mutex);
	gcX_ops->freeFunc(mgr);
}

/* Stream */
GaBufferedStream *ga_stream_create(GaStreamManager *mgr, GaSampleSource *src, gc_size buffer_size) {
	GaBufferedStream* ret = gcX_ops->allocFunc(sizeof(GaBufferedStream));
	ret->refCount = 1;
	ga_sample_source_acquire(src);
	ga_sample_source_format(src, &ret->format);
	ga_list_head(&ret->tell_jumps);
	ret->inner_src = src;
	ret->next_sample = 0;
	ret->seek = 0;
	ret->tell = 0;
	ret->end = 0;
	ret->buffer_size = buffer_size;
	ret->flags = ga_sample_source_flags(src);
	assert(ret->flags & GaDataAccessFlag_Threadsafe);
	ret->produce_mutex = ga_mutex_create();
	ret->seek_mutex = ga_mutex_create();
	ret->read_mutex = ga_mutex_create();
	ret->buffer = ga_buffer_create(buffer_size);
	ret->stream_link = (GaLink*)gaX_stream_manager_add(mgr, ret);
	return ret;
}
static void gaX_stream_onSeek(gc_int32 sample, gc_int32 delta, void *seekContext) {
	GaBufferedStream* s = (GaBufferedStream*)seekContext;
	gc_size samplesAvail;
	GaFormat fmt;
	ga_sample_source_format(s->inner_src, &fmt);
	gc_uint32 sample_size = ga_format_sample_size(&fmt);
	ga_mutex_lock(s->read_mutex);
	ga_mutex_lock(s->seek_mutex);
	samplesAvail = ga_buffer_bytesAvail(s->buffer) / sample_size;
	gauX_tell_jump_push(&s->tell_jumps, samplesAvail + sample, delta);
	ga_mutex_unlock(s->seek_mutex);
	ga_mutex_unlock(s->read_mutex);
}
gc_int32 gaX_read_samples_into_stream(GaBufferedStream *stream,
                                      GaCircBuffer *b,
                                      gc_int32 samples,
				      GaSampleSource *sampleSrc) {
	void *dataA;
	void *dataB;
	gc_size sizeA = 0;
	gc_size sizeB = 0;
	gc_size numWritten = 0;
	GaFormat fmt;
	gc_uint32 sample_size;
	ga_sample_source_format(sampleSrc, &fmt);
	sample_size = ga_format_sample_size(&fmt);
	gc_uint8 num_buffers = ga_buffer_getFree(b, samples * sample_size, &dataA, &sizeA, &dataB, &sizeB);
	if (num_buffers >= 1) {
		numWritten = ga_sample_source_read(sampleSrc, dataA, sizeA / sample_size, &gaX_stream_onSeek, stream);
		if (num_buffers == 2 && numWritten == sizeA)
			numWritten += ga_sample_source_read(sampleSrc, dataB, sizeB / sample_size, &gaX_stream_onSeek, stream);
	}
	ga_buffer_produce(b, numWritten * sample_size);
	return numWritten;
}
void ga_stream_produce(GaBufferedStream *s) {
	GaCircBuffer* b = s->buffer;
	gc_uint32 sample_size = ga_format_sample_size(&s->format);
	gc_size bytes_free = ga_buffer_bytesFree(b);
	if (s->seek >= 0) {
		ga_mutex_lock(s->read_mutex);
		ga_mutex_lock(s->seek_mutex);

		/* Check again now that we're mutexed */
		if (s->seek >= 0) {
			gc_int32 samplePos = s->seek;
			s->tell = samplePos;
			s->seek = -1;
			s->next_sample = samplePos;
			ga_sample_source_seek(s->inner_src, samplePos);
			ga_buffer_consume(s->buffer, ga_buffer_bytesAvail(s->buffer)); /* Clear buffer */
			gauX_tell_jump_clear(&s->tell_jumps); /* Clear tell-jump list */
		}
		ga_mutex_unlock(s->seek_mutex);
		ga_mutex_unlock(s->read_mutex);
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
gc_size ga_stream_read(GaBufferedStream *s, void *dst, gc_size num_samples) {
	GaCircBuffer* b = s->buffer;
	gc_int32 delta;

	/* Read the samples */
	gc_size samplesConsumed = 0;
	ga_mutex_lock(s->read_mutex);

	void *dataA;
	void *dataB;
	gc_size sizeA, sizeB;
	gc_size totalBytes = 0;
	gc_size sample_size = ga_format_sample_size(&s->format);
	gc_size dstBytes = num_samples * sample_size;
	gc_size avail = ga_buffer_bytesAvail(b);
	dstBytes = dstBytes > avail ? avail : dstBytes;
	if (ga_buffer_getAvail(b, dstBytes, &dataA, &sizeA, &dataB, &sizeB) >= 1) {
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
	ga_buffer_consume(b, totalBytes);

	/* Update the tell pos */
	ga_mutex_lock(s->seek_mutex);
	s->tell += samplesConsumed;
	delta = gauX_tell_jump_process(&s->tell_jumps, samplesConsumed);
	s->tell += delta;
	ga_mutex_unlock(s->seek_mutex);
	ga_mutex_unlock(s->read_mutex);
	return samplesConsumed;
}
gc_bool ga_stream_ready(GaBufferedStream *s, gc_size num_samples) {
	gc_size avail = ga_buffer_bytesAvail(s->buffer);
	return s->end || (avail >= num_samples * ga_format_sample_size(&s->format) && avail > s->buffer_size / 2.0f);
}
gc_bool ga_stream_end(GaBufferedStream *s) {
	GaCircBuffer* b = s->buffer;
	gc_int32 bytesAvail = ga_buffer_bytesAvail(b);
	return s->end && bytesAvail == 0;
}
ga_result ga_stream_seek(GaBufferedStream *s, gc_size sampleOffset) {
	ga_mutex_lock(s->seek_mutex);
	s->seek = sampleOffset;
	ga_mutex_unlock(s->seek_mutex);
	return GA_OK;
}
ga_result ga_stream_tell(GaBufferedStream *s, gc_size *samples, gc_size *totalSamples) {
	ga_result res = ga_sample_source_tell(s->inner_src, samples, totalSamples);
	if (res != GA_OK) return res;
	ga_mutex_lock(s->seek_mutex);
	if (samples) *samples = s->seek >= 0 ? s->seek : s->tell;
	ga_mutex_unlock(s->seek_mutex);
	return GA_OK;
}
GaDataAccessFlags ga_stream_flags(GaBufferedStream *s) {
	return s->flags;
}
void gaX_stream_destroy(GaBufferedStream *s) {
	gaX_stream_link_kill((gaX_StreamLink*)s->stream_link); /* This must be done first, so that the stream remains valid until it killed */
	gaX_stream_link_release((gaX_StreamLink*)s->stream_link);
	ga_mutex_destroy(s->produce_mutex);
	ga_mutex_destroy(s->seek_mutex);
	ga_mutex_destroy(s->read_mutex);
	ga_buffer_destroy(s->buffer);
	gauX_tell_jump_clear(&s->tell_jumps);
	ga_sample_source_release(s->inner_src);
	gcX_ops->freeFunc(s);
}

void ga_stream_acquire(GaBufferedStream *stream) {
	atomic_fetch_add(&stream->refCount, 1);
}
void ga_stream_release(GaBufferedStream *stream) {
	if (gcX_decref(&stream->refCount)) gaX_stream_destroy(stream);
}
