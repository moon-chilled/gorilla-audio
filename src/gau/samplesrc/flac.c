#include <string.h>

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

#if GAU_SUPPORT_FLAC == 0
GaSampleSource *gau_sample_source_create_flac(GaDataSource *data) { return NULL; }
#elif GAU_SUPPORT_FLAC == 1

#undef true
#undef false //grumble, grumble

#include "FLAC/stream_decoder.h"
#include "FLAC/metadata.h"

struct GaSampleSourceContext {
	GaDataSource *data_src;
	usz datalen; //iff ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable
	char *buffer;
	u32 bufmax; // one past the index of the highest valid (unread) sample
	u32 bufoff; // the index of the first valid sample
	u32 bufcap; // the number of samples that can ever be stored in buffer

	GaFormat fmt;

	usz frame_off;

	bool seeking;

	FLAC__StreamDecoder *flac;
	u32 flacbps;
	GaMutex mutex;
};

static FLAC__StreamDecoderReadStatus flac_read(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *context) {
	GaSampleSourceContext *ctx = context;
	// I think _CONTINUE makes more sense here, but this is what the official docs' examples do
	if (!*bytes) return FLAC__STREAM_DECODER_READ_STATUS_ABORT;

	// todo ga_data_source_read should be able to return an error
	usz nread = ga_data_source_read(ctx->data_src, buffer, 1, *bytes);
	if (nread != *bytes) return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
	else return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

static FLAC__StreamDecoderSeekStatus flac_seek(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *context) {
	GaSampleSourceContext *ctx = context;
	if (ga_isok(ga_data_source_seek(ctx->data_src, absolute_byte_offset, GaSeekOrigin_Set)))
		return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
	else
		return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
}

static FLAC__StreamDecoderTellStatus flac_tell(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *context) {
	GaSampleSourceContext *ctx = context;
	*absolute_byte_offset = ga_data_source_tell(ctx->data_src);
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__bool flac_eof(const FLAC__StreamDecoder *sd, void *context) {
	GaSampleSourceContext *ctx = context;
	return ga_data_source_eof(ctx->data_src);
}

static FLAC__StreamDecoderLengthStatus flac_length(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *context) {
	GaSampleSourceContext *ctx = context;

	if (ctx->datalen) {
		*stream_length = ctx->datalen;
		return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
	} else {
		return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;
	}
}

static void flac_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *context) {
	assert(0);
	//GaSampleSourceContext *ctx = context;
	//ctx->errored = true;
}

static void flac_metadata(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *context) {
	GaSampleSourceContext *ctx = context;

	if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;

	ctx->fmt.frame_rate = metadata->data.stream_info.sample_rate;
	ctx->fmt.num_channels = metadata->data.stream_info.channels;
	ctx->flacbps = metadata->data.stream_info.bits_per_sample;

	switch (ctx->flacbps) {
		case 16: ctx->fmt.sample_fmt = GaSampleFormat_S16; break;
		case 24: ctx->fmt.sample_fmt = GaSampleFormat_S32; break;
		default: assert(0);
	}

	ctx->bufcap = metadata->data.stream_info.max_blocksize * ctx->fmt.num_channels;
	ctx->buffer = ga_alloc(ctx->bufcap * ctx->fmt.sample_fmt);
}


static FLAC__StreamDecoderWriteStatus flac_write(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *context) {
	GaSampleSourceContext *ctx = context;
	// lock not required; decode will only be called by a thread that's already acquired a mutex

	if (ctx->seeking) return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;

	assert (!ctx->bufoff);
	assert (!ctx->bufmax);
	assert (frame->header.channels == ctx->fmt.num_channels);

	assert (ctx->bufcap >= frame->header.blocksize * ctx->fmt.num_channels + ctx->bufoff);

	if (ctx->flacbps == 16) {
		s16 *p = (s16*)ctx->buffer;
		for (usz i = 0; i < frame->header.blocksize; i++) {
			for (usz j = 0; j < ctx->fmt.num_channels; j++) {
				*p++ = buffer[j][i]; //not great data access :/ oh well
			}
		}
		ctx->bufmax += p - (s16*)ctx->buffer;
	} else if (ctx->flacbps == 24) {
		s32 *p = (s32*)ctx->buffer;
		for (usz i = 0; i < frame->header.blocksize; i++) {
			for (usz j = 0; j < ctx->fmt.num_channels; j++) {
				*p++ = buffer[j][i] << 8; //not great data access :/ oh well
			}
		}
		ctx->bufmax += p - (s32*)ctx->buffer;
	} else {
		return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	}

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static ga_bool ss_end(GaSampleSourceContext *ctx) {
	FLAC__StreamDecoderState res;
	with_mutex(ctx->mutex) res = FLAC__stream_decoder_get_state(ctx->flac);
	return res == FLAC__STREAM_DECODER_END_OF_STREAM
	    || res == FLAC__STREAM_DECODER_ABORTED;
}

static usz ss_read(GaSampleSourceContext *ctx, void *dst, usz num_frames, GaCbOnSeek onseek, void *seek_ctx) {
	usz frames_read = 0;
	usz samples_left = num_frames*ctx->fmt.num_channels;
	char *sdst = dst;

	ga_mutex_lock(ctx->mutex);
	while (frames_read < num_frames) {
		while (ctx->bufoff+1 >= ctx->bufmax) {
			if (!FLAC__stream_decoder_process_single(ctx->flac)
			    || FLAC__stream_decoder_get_state(ctx->flac) == FLAC__STREAM_DECODER_END_OF_STREAM
			    || FLAC__stream_decoder_get_state(ctx->flac) == FLAC__STREAM_DECODER_ABORTED) {
				ga_mutex_unlock(ctx->mutex);
				return frames_read;
			}
		}

		usz samples_read = min(ctx->bufmax - ctx->bufoff, samples_left);
		memcpy(sdst, ctx->buffer + ctx->bufoff * ctx->fmt.sample_fmt, samples_read * ctx->fmt.sample_fmt);
		sdst += samples_read * ctx->fmt.sample_fmt;
		frames_read += samples_read/ctx->fmt.num_channels;
		ctx->frame_off += samples_read/ctx->fmt.num_channels;
		ctx->bufoff += samples_read;
		samples_left -= samples_read;

		if (ctx->bufoff+1 >= ctx->bufmax) {
			ctx->bufoff = ctx->bufmax = 0;
		}
	}
	ga_mutex_unlock(ctx->mutex);

	return frames_read;
}

static ga_result ss_seek(GaSampleSourceContext *ctx, usz frame_offset) {
	FLAC__bool res;
	with_mutex(ctx->mutex) {
		ctx->seeking = true;
		res = FLAC__stream_decoder_seek_absolute(ctx->flac, frame_offset);
		if (res) {
			ctx->frame_off = frame_offset;
			ctx->bufmax = ctx->bufoff = 0;
		}
		ctx->seeking = false;
	}
	return res ? GA_OK : GA_ERR_GENERIC;
}
static ga_result ss_tell(GaSampleSourceContext *ctx, usz *cur, usz *total) {
	ga_mutex_lock(ctx->mutex);
	if (total) *total = FLAC__stream_decoder_get_total_samples(ctx->flac);
	if (cur) *cur = ctx->frame_off;
	ga_mutex_unlock(ctx->mutex);
	return GA_OK;
}
static void ss_close(GaSampleSourceContext *ctx) {
	FLAC__stream_decoder_finish(ctx->flac);
	FLAC__stream_decoder_delete(ctx->flac);
	ga_free(ctx->buffer);
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->mutex);
	ga_free(ctx);
}

GaSampleSource *gau_sample_source_create_flac(GaDataSource *data_src) {
	bool seekable = ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable;

	usz datalen = 0;
	if (seekable) {
		if (!ga_isok(ga_data_source_seek(data_src, 0, GaSeekOrigin_End))) return NULL;
		datalen = ga_data_source_tell(data_src);
		if (!ga_isok(ga_data_source_seek(data_src, 0, GaSeekOrigin_Set))) return NULL;
	}

	GaSampleSourceContext *ctx = ga_zalloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;
	if (!ga_isok(ga_mutex_create(&ctx->mutex))) goto fail;
	ctx->data_src = data_src;
	ctx->datalen = datalen;

	ctx->flac = FLAC__stream_decoder_new();

	FLAC__StreamDecoderInitStatus status = FLAC__stream_decoder_init_stream(ctx->flac, flac_read, seekable ? flac_seek : NULL, flac_tell, seekable ? flac_length : NULL, flac_eof, flac_write, flac_metadata, flac_error, ctx);
	if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) goto fail;
	if (!FLAC__stream_decoder_process_until_end_of_metadata(ctx->flac)) goto fail;

	if (ctx->flacbps != 16
	    && ctx->flacbps != 24
	    && ctx->flacbps != 32) goto fail;

	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.ready = NULL,
		.tell = ss_tell,
		.close = ss_close,
		.context = ctx,
		.format = ctx->fmt,
		.threadsafe = true,
	};
	if (seekable) m.seek = ss_seek;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;
	ga_data_source_acquire(data_src);
	return ret;

fail:
	if (ctx) {
		ga_mutex_destroy(ctx->mutex);
		if (ctx->flac) FLAC__stream_decoder_delete(ctx->flac);
	}
	ga_free(ctx);
	return NULL;
}

#elif GAU_SUPPORT_FLAC == 2

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DRFLAC_API static
#ifdef __GNUC__
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "dr_flac.h"
#ifdef __GNUC__
# pragma GCC diagnostic pop
#endif

struct GaSampleSourceContext {
	drflac *flac;
	GaMutex mutex;
	GaDataSource *data_src;
	bool end;
};

static void *flac_alloc(usz sz, void *data) { return ga_alloc(sz); }
static void *flac_realloc(void *p, usz sz, void *data) { return ga_realloc(p, sz); }
static void flac_free(void *p, void *data) { return ga_free(p); }

static drflac_allocation_callbacks flac_allocator = {.onMalloc = flac_alloc, .onRealloc = flac_realloc, .onFree = flac_free};

static usz flac_read(void *context, void *buf, usz l) {
	GaSampleSourceContext *ctx = context;
	return ga_data_source_read(ctx->data_src, buf, 1, l);
}
static drflac_bool32 flac_seek(void *context, int offset, drflac_seek_origin origin) {
	GaSampleSourceContext *ctx = context;
	drflac_bool32 ret;
	switch (origin) {
		case drflac_seek_origin_start: ret = ga_isok(ga_data_source_seek(ctx->data_src, offset, GaSeekOrigin_Set)); break;
		case drflac_seek_origin_current: ret = ga_isok(ga_data_source_seek(ctx->data_src, offset, GaSeekOrigin_Cur)); break;
		default: ret = 0;
	}
	if (ret && ctx->end) ctx->end = false;
	return ret;
}

static usz ss_read(GaSampleSourceContext *ctx, void *dst, usz num_frames, GaCbOnSeek onseek, void *seek_ctx) {
	drflac_uint64 res;
	with_mutex(ctx->mutex) {
		res = drflac_read_pcm_frames_s16(ctx->flac, num_frames, dst);
		if (res < num_frames) ctx->end = true;
	}
	return res;
}
static bool ss_end(GaSampleSourceContext *ctx) {
	bool ret;
	with_mutex(ctx->mutex) ret = ctx->end;
	return ret;
}
static ga_result ss_seek(GaSampleSourceContext *ctx, usz frame_offset) {
	drflac_bool32 res;
	with_mutex(ctx->mutex) res = drflac_seek_to_pcm_frame(ctx->flac, frame_offset);
	return res ? GA_OK : GA_ERR_GENERIC;
}
static ga_result ss_tell(GaSampleSourceContext *ctx, usz *cur, usz *total) {
	if (total && !ctx->flac->totalPCMFrameCount) return GA_ERR_MIS_UNSUP;
	with_mutex(ctx->mutex) {
		// this is the 'index of the PCM frame the decoder is currently sitting on'.  So possibly wrong because of buffering?
		if (cur) *cur = ctx->flac->currentPCMFrame;
		if (total) *total = ctx->flac->totalPCMFrameCount;
	}
	return GA_OK;
}

static void ss_close(GaSampleSourceContext *ctx) {
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->mutex);
	drflac_close(ctx->flac);
	ga_free(ctx);
}


GaSampleSource *gau_sample_source_create_flac(GaDataSource *data_src) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;
	if (!ga_isok(ga_mutex_create(&ctx->mutex))) goto fail;
	ctx->data_src = data_src;

	if (!(ctx->flac = drflac_open(flac_read, (ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable) ? flac_seek : NULL, ctx, &flac_allocator))) goto fail;
	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.tell = ss_tell,
		.close = ss_close,
		.context = ctx,
		.format = {.num_channels = ctx->flac->channels, .sample_fmt = GaSampleFormat_S16, .frame_rate = ctx->flac->sampleRate},
		.threadsafe = true,
	};
	if (ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable) m.seek = ss_seek;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;
	ga_data_source_acquire(data_src);
	return ret;

fail:
	ga_mutex_destroy(ctx->mutex);
	ga_free(ctx);
	return NULL;
}

#endif //GAU_SUPPORT_FLAC
