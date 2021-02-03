#include <string.h>

#include "FLAC/stream_decoder.h"
#include "FLAC/metadata.h"

#undef true
#undef false //grumble, grumble

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

struct GaSampleSourceContext {
	GaDataSource *data_src;
	usz datalen; //iff ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable
	s16 *buffer;
	u32 buflen, bufoff, bufcap;

	GaFormat fmt;

	usz sample_off;

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
	*stream_length = ctx->datalen;
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
	//todo experiment with returning FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED
}

static void flac_error(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *context) {
	assert(0);
	//GaSampleSourceContext *ctx = context;
	//ctx->errored = true;
}

static void flac_metadata(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *context) {
	GaSampleSourceContext *ctx = context;

	if (metadata->type != FLAC__METADATA_TYPE_STREAMINFO) return;

	ctx->fmt.sample_rate = metadata->data.stream_info.sample_rate;
	ctx->fmt.num_channels = metadata->data.stream_info.channels;
	ctx->flacbps = metadata->data.stream_info.bits_per_sample;
}


static FLAC__StreamDecoderWriteStatus flac_write(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *context) {
	GaSampleSourceContext *ctx = context;
	// lock not required; decode will only be called by a thread that's already acquired a mutex

	assert (!ctx->bufoff);
	assert (frame->header.channels == 2);
	assert (ctx->flacbps == 16);

	{
		usz c = frame->header.blocksize * ctx->fmt.num_channels + ctx->bufoff;
		if (c > ctx->bufcap) {
			ctx->buffer = ga_realloc(ctx->buffer, c * 2);
			ctx->bufcap = c;
		}
	}

	s16 *p = ctx->buffer;
	for (usz i = 0; i < frame->header.blocksize; i++) {
		for (usz j = 0; j < ctx->fmt.num_channels; j++) {
			//todo don't truncate (mixer needs to requantize on its own)
			//*p++ = buffer[j][i] << 16; //not great data access :/ oh well
			*p++ = buffer[j][i]; //not great data access :/ oh well
			printf("%d>\n", buffer[j][i]);
		}
	}
	ctx->buflen = p - ctx->buffer;
	printf("write %p..%p\n", ctx->buffer, p);

	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static usz ss_read(GaSampleSourceContext *ctx, void *dst, usz num_samples, GaCbOnSeek onseek, void *seek_ctx) {
	usz num_read = 0;
	usz num_left = num_samples;
	s16 *sdst = dst;

	ga_mutex_lock(ctx->mutex);
	while (num_read < num_samples) {
		while (ctx->buflen <= ctx->bufoff) {
			if (!FLAC__stream_decoder_process_single(ctx->flac)) {
				ga_mutex_unlock(ctx->mutex);
				return num_read;
			}
		}

		usz nread = min(ctx->buflen - ctx->bufoff, num_left);
		printf("read  %p..%p\n", ctx->buffer + ctx->bufoff, ctx->buffer + ctx->bufoff + nread);
		memcpy(sdst, ctx->buffer + ctx->bufoff, nread * 2);
		sdst += nread;
		num_read += nread;
		ctx->bufoff += nread;
		num_left -= nread;
		ctx->sample_off += nread;

		if (ctx->bufoff+1 >= ctx->buflen) {
			ctx->bufoff = ctx->buflen = 0;
		}
	}
	ga_mutex_unlock(ctx->mutex);

	printf("READ %zu/%zu\n", num_read, num_samples);
	return num_read;
}

static ga_bool ss_end(GaSampleSourceContext *ctx) {
	FLAC__StreamDecoderState res;
	with_mutex(ctx->mutex) res = FLAC__stream_decoder_get_state(ctx->flac);
	return res == FLAC__STREAM_DECODER_END_OF_STREAM;
}
static ga_result ss_seek(GaSampleSourceContext *ctx, usz sample_offset) {
	FLAC__bool res;
	with_mutex(ctx->mutex) {
		res = FLAC__stream_decoder_seek_absolute(ctx->flac, sample_offset);
		if (res) ctx->sample_off = sample_offset;
	}
	return res ? GA_OK : GA_ERR_GENERIC;
}
static ga_result ss_tell(GaSampleSourceContext *ctx, usz *cur, usz *total) {
	ga_mutex_lock(ctx->mutex);
	if (total) *total = FLAC__stream_decoder_get_total_samples(ctx->flac);
	if (cur) *cur = ctx->sample_off;
	ga_mutex_unlock(ctx->mutex);
	return GA_OK;
}
static void ss_close(GaSampleSourceContext *ctx) {
	FLAC__stream_decoder_finish(ctx->flac);
	FLAC__stream_decoder_delete(ctx->flac);
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->mutex);
	ga_free(ctx);
}

GaSampleSource *gau_sample_source_create_flac(GaDataSource *data_src) {
	bool seekable = ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable;

	usz datalen;
	if (seekable) {
		if (!ga_isok(ga_data_source_seek(data_src, 0, GaSeekOrigin_End))) return NULL;
		datalen = ga_data_source_tell(data_src);
		if (!ga_isok(ga_data_source_seek(data_src, 0, GaSeekOrigin_Set))) return NULL;
	}

	GaSampleSourceContext *ctx = memset(ga_alloc(sizeof(GaSampleSourceContext)), 0, sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;
	if (!ga_isok(ga_mutex_create(&ctx->mutex))) goto fail;
	ctx->data_src = data_src;
	ctx->datalen = datalen;
	ctx->fmt.bits_per_sample = 16;

	ctx->flac = FLAC__stream_decoder_new();

	FLAC__StreamDecoderInitStatus status = FLAC__stream_decoder_init_stream(ctx->flac, flac_read, seekable ? flac_seek : NULL, flac_tell, seekable ? flac_length : NULL, flac_eof, flac_write, flac_metadata, flac_error, ctx);
	if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) goto fail;
	if (!FLAC__stream_decoder_process_until_end_of_metadata(ctx->flac)) goto fail;

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
