#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include <assert.h>

/* Tell Jumps */
typedef struct {
	gc_int32 pos;
	gc_int32 delta;
} gau_TellJumpData;

typedef struct {
	gc_Link link;
	gau_TellJumpData data;
} gau_TellJumpLink;

void gauX_tell_jump_push(gc_Link *head, gc_int32 pos, gc_int32 delta) {
	gau_TellJumpLink* link = gcX_ops->allocFunc(sizeof(gau_TellJumpLink));
	link->link.data = &link->data;
	link->data.pos = pos;
	link->data.delta = delta;
	gc_list_link(head->prev, (gc_Link*)link, &link->data);
}

gc_int32 gauX_tell_jump_peek(gc_Link *head, gc_int32 *pos, gc_int32 *delta) {
	gau_TellJumpLink* link;
	if (head->next == head) return 0;
	link = (gau_TellJumpLink*)head->next;
	*pos = link->data.pos;
	*delta = link->data.delta;
	return 1;
}

gc_int32 gauX_tell_jump_pop(gc_Link *head) {
	gau_TellJumpLink *link;
	if (head->next == head) return 0;
	link = (gau_TellJumpLink*)head->next;
	gc_list_unlink((gc_Link*)link);
	gcX_ops->freeFunc(link);
	return 1;
}

gc_int32 gauX_tell_jump_process(gc_Link *head, gc_int32 advance) {
	gc_int32 ret = 0;
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
	_Atomic gc_int32 refCount;
	gc_Mutex* produceMutex;
	ga_BufferedStream* stream;
} gaX_StreamLink;

gaX_StreamLink *gaX_stream_link_create() {
	gaX_StreamLink* ret = (gaX_StreamLink*)gcX_ops->allocFunc(sizeof(gaX_StreamLink));
	ret->refCount = 1;
	ret->produceMutex = gc_mutex_create();
	ret->stream = 0;
	return ret;
}

gc_int32 gaX_stream_link_produce(gaX_StreamLink *streamLink) {
	gc_int32 ret = 1;
	gc_mutex_lock(streamLink->produceMutex);
	if (streamLink->stream) {
		/* Mutexing this entire section guarantees that ga_stream_destroy()
		   cannot occur during production */
		ga_stream_produce(streamLink->stream);
		ret = 0;
	}
	gc_mutex_unlock(streamLink->produceMutex);
	return ret;
}

void gaX_stream_link_kill(gaX_StreamLink *streamLink) {
	gc_mutex_lock(streamLink->produceMutex);
	streamLink->stream = 0;
	gc_mutex_unlock(streamLink->produceMutex);
}

void gaX_stream_link_destroy(gaX_StreamLink* streamLink) {
	gc_mutex_destroy(streamLink->produceMutex);
	gcX_ops->freeFunc(streamLink);
}

void gaX_stream_link_acquire(gaX_StreamLink *streamLink) {
	atomic_fetch_add(&streamLink->refCount, 1);
}
void gaX_stream_link_release(gaX_StreamLink *streamLink) {
	gc_int32 rc = atomic_fetch_sub(&streamLink->refCount, 1);
	assert(rc > 0);
	if (rc == 1) gaX_stream_link_destroy(streamLink);
}

/* Stream Manager */
ga_StreamManager *ga_stream_manager_create(void) {
	ga_StreamManager* ret = gcX_ops->allocFunc(sizeof(ga_StreamManager));
	ret->streamListMutex = gc_mutex_create();
	gc_list_head(&ret->streamList);
	return ret;
}
gaX_StreamLink *gaX_stream_manager_add(ga_StreamManager *mgr, ga_BufferedStream *stream) {
	gaX_StreamLink* streamLink = gaX_stream_link_create(stream);
	gaX_stream_link_acquire(streamLink); /* The new client adds its own refcount */
	/* It's safe to add() while iterating in stream() because of implicit fault tolerance */
	/* That is, all possible outcomes are valid, despite the race condition */
	/* This is true because we are guaranteed to stream() on the same thread that calls remove() */
	gc_mutex_lock(mgr->streamListMutex);
	streamLink->stream = stream;
	gc_list_link(&mgr->streamList, (gc_Link*)streamLink, streamLink);
	gc_mutex_unlock(mgr->streamListMutex);
	return streamLink;
}
void ga_stream_manager_buffer(ga_StreamManager *mgr) {
	gc_Link *link = mgr->streamList.next;
	while (link != &mgr->streamList) {
		gc_int32 streamDead;
		gaX_StreamLink* streamLink;
		streamLink = (gaX_StreamLink*)link->data;
		link = link->next;
		streamDead = gaX_stream_link_produce(streamLink);
		if (streamDead) {
			gc_mutex_lock(mgr->streamListMutex);
			gc_list_unlink((gc_Link*)streamLink);
			gc_mutex_unlock(mgr->streamListMutex);
			gaX_stream_link_release(streamLink);
		}
	}
}
void ga_stream_manager_destroy(ga_StreamManager *mgr) {
	gc_Link *link;
	link = mgr->streamList.next;
	while (link != &mgr->streamList) {
		gaX_StreamLink* oldLink = (gaX_StreamLink*)link;
		link = link->next;
		gaX_stream_link_release(oldLink);
	}
	gc_mutex_destroy(mgr->streamListMutex);
	gcX_ops->freeFunc(mgr);
}

/* Stream */
ga_BufferedStream *ga_stream_create(ga_StreamManager *mgr, ga_SampleSource *sampleSrc, gc_int32 bufferSize) {
	ga_BufferedStream* ret = gcX_ops->allocFunc(sizeof(ga_BufferedStream));
	ret->refCount = 1;
	ga_sample_source_acquire(sampleSrc);
	ga_sample_source_format(sampleSrc, &ret->format);
	gc_list_head(&ret->tellJumps);
	ret->innerSrc = sampleSrc;
	ret->nextSample = 0;
	ret->seek = 0;
	ret->tell = 0;
	ret->end = 0;
	ret->bufferSize = bufferSize;
	ret->flags = ga_sample_source_flags(sampleSrc);
	assert(ret->flags & GA_FLAG_THREADSAFE);
	ret->produceMutex = gc_mutex_create();
	ret->seekMutex = gc_mutex_create();
	ret->readMutex = gc_mutex_create();
	ret->buffer = gc_buffer_create(bufferSize);
	ret->streamLink = (gc_Link*)gaX_stream_manager_add(mgr, ret);
	return ret;
}
void gaX_stream_onSeek(gc_int32 sample, gc_int32 delta, void *seekContext) {
	ga_BufferedStream* s = (ga_BufferedStream*)seekContext;
	gc_int32 samplesAvail, sampleSize;
	ga_Format fmt;
	ga_sample_source_format(s->innerSrc, &fmt);
	sampleSize = ga_format_sampleSize(&fmt);
	gc_mutex_lock(s->readMutex);
	gc_mutex_lock(s->seekMutex);
	samplesAvail = gc_buffer_bytesAvail(s->buffer) / sampleSize;
	gauX_tell_jump_push(&s->tellJumps, samplesAvail + sample, delta);
	gc_mutex_unlock(s->seekMutex);
	gc_mutex_unlock(s->readMutex);
}
gc_int32 gaX_read_samples_into_stream(ga_BufferedStream *stream,
                                      gc_CircBuffer *b,
                                      gc_int32 samples,
				      ga_SampleSource *sampleSrc) {
	void *dataA;
	void *dataB;
	gc_uint32 sizeA = 0;
	gc_uint32 sizeB = 0;
	gc_int32 numBuffers;
	gc_int32 numWritten = 0;
	ga_Format fmt;
	gc_int32 sampleSize;
	ga_sample_source_format(sampleSrc, &fmt);
	sampleSize = ga_format_sampleSize(&fmt);
	numBuffers = gc_buffer_getFree(b, samples * sampleSize, &dataA, &sizeA, &dataB, &sizeB);
	if (numBuffers >= 1) {
		numWritten = ga_sample_source_read(sampleSrc, dataA, sizeA / sampleSize, &gaX_stream_onSeek, stream);
		if (numBuffers == 2 && numWritten == sizeA)
			numWritten += ga_sample_source_read(sampleSrc, dataB, sizeB / sampleSize, &gaX_stream_onSeek, stream);
	}
	gc_buffer_produce(b, numWritten * sampleSize);
	return numWritten;
}
void ga_stream_produce(ga_BufferedStream *s) {
	gc_CircBuffer* b = s->buffer;
	gc_int32 sampleSize = ga_format_sampleSize(&s->format);
	gc_int32 bytesFree = gc_buffer_bytesFree(b);
	if (s->seek >= 0) {
		gc_mutex_lock(s->readMutex);
		gc_mutex_lock(s->seekMutex);

		/* Check again now that we're mutexed */
		if (s->seek >= 0) {
			gc_int32 samplePos = s->seek;
			s->tell = samplePos;
			s->seek = -1;
			s->nextSample = samplePos;
			ga_sample_source_seek(s->innerSrc, samplePos);
			gc_buffer_consume(s->buffer, gc_buffer_bytesAvail(s->buffer)); /* Clear buffer */
			gauX_tell_jump_clear(&s->tellJumps); /* Clear tell-jump list */
		}
		gc_mutex_unlock(s->seekMutex);
		gc_mutex_unlock(s->readMutex);
	}

	while (bytesFree) {
		gc_int32 samplesWritten = 0;
		gc_int32 bytesWritten = 0;
		gc_int32 bytesToWrite = bytesFree;
		samplesWritten = gaX_read_samples_into_stream(s, b, bytesToWrite / sampleSize, s->innerSrc);
		bytesWritten = samplesWritten * sampleSize;
		bytesFree -= bytesWritten;
		s->nextSample += samplesWritten;
		if (bytesWritten < bytesToWrite && ga_sample_source_end(s->innerSrc)) {
			s->end = 1;
			break;
		}
	}
}
gc_int32 ga_stream_read(ga_BufferedStream *s, void *dst, gc_int32 numSamples) {
	gc_CircBuffer* b = s->buffer;
	gc_int32 delta;

	/* Read the samples */
	gc_int32 samplesConsumed = 0;
	gc_mutex_lock(s->readMutex);

	void *dataA;
	void *dataB;
	gc_uint32 sizeA, sizeB;
	gc_int32 totalBytes = 0;
	gc_int32 sampleSize = ga_format_sampleSize(&s->format);
	gc_int32 dstBytes = numSamples * sampleSize;
	gc_int32 avail = gc_buffer_bytesAvail(b);
	dstBytes = dstBytes > avail ? avail : dstBytes;
	if (gc_buffer_getAvail(b, dstBytes, &dataA, &sizeA, &dataB, &sizeB) >= 1) {
		gc_int32 bytesToRead = dstBytes < (gc_int32)sizeA ? dstBytes : (gc_int32)sizeA;
		memcpy(dst, dataA, bytesToRead);
		totalBytes += bytesToRead;
		if (dstBytes > 0 && dataB) {
			gc_int32 dstBytesLeft = dstBytes - bytesToRead;
			bytesToRead = dstBytesLeft < (gc_int32)sizeB ? dstBytesLeft : (gc_int32)sizeB;
			memcpy((char*)dst + totalBytes, dataB, bytesToRead);
			totalBytes += bytesToRead;
		}
	}
	samplesConsumed = totalBytes / sampleSize;
	gc_buffer_consume(b, totalBytes);

	/* Update the tell pos */
	gc_mutex_lock(s->seekMutex);
	s->tell += samplesConsumed;
	delta = gauX_tell_jump_process(&s->tellJumps, samplesConsumed);
	s->tell += delta;
	gc_mutex_unlock(s->seekMutex);
	gc_mutex_unlock(s->readMutex);
	return samplesConsumed;
}
gc_int32 ga_stream_ready(ga_BufferedStream *s, gc_int32 numSamples) {
	gc_int32 avail = gc_buffer_bytesAvail(s->buffer);
	return s->end || (avail >= numSamples * ga_format_sampleSize(&s->format) && avail > s->bufferSize / 2.0f);
}
gc_int32 ga_stream_end(ga_BufferedStream *s) {
	gc_CircBuffer* b = s->buffer;
	gc_int32 bytesAvail = gc_buffer_bytesAvail(b);
	return s->end && bytesAvail == 0;
}
gc_result ga_stream_seek(ga_BufferedStream *s, gc_int32 sampleOffset) {
	gc_mutex_lock(s->seekMutex);
	s->seek = sampleOffset;
	gc_mutex_unlock(s->seekMutex);
	return GC_SUCCESS;
}
gc_int32 ga_stream_tell(ga_BufferedStream *s, gc_int32 *totalSamples) {
	gc_int32 ret = -1;
	ga_sample_source_tell(s->innerSrc, totalSamples);
	gc_mutex_lock(s->seekMutex);
	ret = s->seek >= 0 ? s->seek : s->tell;
	gc_mutex_unlock(s->seekMutex);
	return ret;
}
gc_int32 ga_stream_flags(ga_BufferedStream *s) {
	return s->flags;
}
void gaX_stream_destroy(ga_BufferedStream *s) {
	gaX_stream_link_kill((gaX_StreamLink*)s->streamLink); /* This must be done first, so that the stream remains valid until it killed */
	gaX_stream_link_release((gaX_StreamLink*)s->streamLink);
	gc_mutex_destroy(s->produceMutex);
	gc_mutex_destroy(s->seekMutex);
	gc_mutex_destroy(s->readMutex);
	gc_buffer_destroy(s->buffer);
	gauX_tell_jump_clear(&s->tellJumps);
	ga_sample_source_release(s->innerSrc);
	gcX_ops->freeFunc(s);
}

void ga_stream_acquire(ga_BufferedStream *stream) {
	atomic_fetch_add(&stream->refCount, 1);
}
void ga_stream_release(ga_BufferedStream *stream) {
	gc_int32 rc = atomic_fetch_sub(&stream->refCount, 1);
	assert(rc > 0);
	if (rc == 1) gaX_stream_destroy(stream);
}
