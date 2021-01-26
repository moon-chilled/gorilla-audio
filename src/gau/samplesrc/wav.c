#include <stdio.h>
#include <string.h>

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

/* WAV Sample Source */
typedef struct {
	s32 file_size;
	s16 fmt_tag, channels, block_align, bits_per_sample;
	s32 fmt_size, sample_rate, bytes_per_sec;
	s32 data_offset, data_size;
} GaWavData;

void data_source_advance(GaDataSource *data_src, s32 delta) {
	if(ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable) {
		ga_data_source_seek(data_src, delta, GaSeekOrigin_Cur);
	} else {
		char buffer[256];
		while(delta > 0) {
			s32 advance = delta > 256 ? 256 : delta;
			s32 bytesAdvanced = ga_data_source_read(data_src, &buffer[0], 1, advance);
			delta -= bytesAdvanced;
		}
	}
}

static ga_result sample_source_wav_load_header(GaDataSource *data_src, GaWavData *wav_data) {
	if (!data_src)
		return GA_ERR_MIS_PARAM;

	/* TODO: Make this work with non-blocking reads? Need to get this data... */
	s32 data_offset = 0;
	char id[4];
	ga_data_source_read(data_src, &id[0], 1, 4); /* 'RIFF' */
	data_offset += 4;
	if (memcmp(id, "RIFF", 4)) return GA_ERR_FMT;

	ga_data_source_read(data_src, &wav_data->file_size, sizeof(s32), 1);
	ga_data_source_read(data_src, &id[0], 1, 4); /* 'WAVE' */
	data_offset += 8;
	if (memcmp(id, "WAVE", 4)) return GA_ERR_FMT;
	s32 dataFound = 0;
	s32 hdrFound = 0;
	do {
		s32 chunkSize = 0;
		ga_data_source_read(data_src, &id[0], sizeof(char), 4);
		ga_data_source_read(data_src, &chunkSize, sizeof(s32), 1);
		data_offset += 8;
		/* 'fmt ' */
		if (!hdrFound && !memcmp(id, "fmt ", 4)) {
			wav_data->fmt_size = chunkSize;
			//todo endian
			ga_data_source_read(data_src, &wav_data->fmt_tag, sizeof(s16), 1);
			ga_data_source_read(data_src, &wav_data->channels, sizeof(s16), 1);
			ga_data_source_read(data_src, &wav_data->sample_rate, sizeof(s32), 1);
			ga_data_source_read(data_src, &wav_data->bytes_per_sec, sizeof(s32), 1);
			ga_data_source_read(data_src, &wav_data->block_align, sizeof(s16), 1);
			ga_data_source_read(data_src, &wav_data->bits_per_sample, sizeof(s16), 1);
			data_source_advance(data_src, chunkSize - 16);
			hdrFound = 1;
		/* 'data' */
		} else if (!dataFound && !memcmp(id, "data", 4)) {
			wav_data->data_size = chunkSize;
			wav_data->data_offset = data_offset;
			dataFound = 1;
		} else {
			data_source_advance(data_src, chunkSize);
		}
		data_offset += chunkSize;
	} while (!(hdrFound && dataFound)); /* TODO: Need End-Of-Data support in Data Sources */
	if (hdrFound && dataFound) return GA_OK;
	else return GA_ERR_FMT;
}

struct GaSampleSourceContext {
	GaDataSource *data_src;
	GaWavData wav_header;
	u32 sample_size;
	atomic_usz pos;
	GaMutex pos_mutex;
};

static usz ss_read(GaSampleSourceContext *ctx, void *dst, usz numSamples, GaCbOnSeek onseek, void *seek_ctx) {
	usz numRead = 0;
	usz totalSamples = ctx->wav_header.data_size / ctx->sample_size;
	with_mutex(ctx->pos_mutex) {
		if (ctx->pos + numSamples > totalSamples) numSamples = totalSamples - ctx->pos;
		numRead = ga_data_source_read(ctx->data_src, dst, ctx->sample_size, numSamples);
		ctx->pos += numRead;
	}
	return numRead;
}
static bool ss_end(GaSampleSourceContext *ctx) {
	usz totalSamples = ctx->wav_header.data_size / ctx->sample_size;
	return atomic_load(&ctx->pos) == totalSamples;
}
static ga_result ss_seek(GaSampleSourceContext *ctx, usz sample_offset) {
	ga_result ret;
	with_mutex(ctx->pos_mutex) {
		ret = ga_data_source_seek(ctx->data_src, ctx->wav_header.data_offset + sample_offset * ctx->sample_size, GaSeekOrigin_Set);
		if (ret == GA_OK) ctx->pos = sample_offset;
	}
	return ret;
}
static ga_result ss_tell(GaSampleSourceContext *ctx, usz *cur, usz *total) {
	if (total) *total = ctx->wav_header.data_size / ctx->sample_size;
	if (cur) *cur = atomic_load(&ctx->pos);
	return GA_OK;
}
static void ss_close(GaSampleSourceContext *ctx) {
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->pos_mutex);
	ga_free(ctx);
}

GaSampleSource *gau_sample_source_create_wav(GaDataSource *data_src) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;
	if (!ga_isok(sample_source_wav_load_header(data_src, &ctx->wav_header))) goto fail;

	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.ready = NULL,
		.close = ss_close,
		.context = ctx,
		.format = {.num_channels = ctx->wav_header.channels, .bits_per_sample = ctx->wav_header.bits_per_sample, .sample_rate = ctx->wav_header.sample_rate},
		.threadsafe = true,
	};
	if (ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable) {
		m.seek = ss_seek;
		m.tell = ss_tell;
	}

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;

	ctx->pos = 0;
	ctx->data_src = data_src;
	ctx->sample_size = ga_format_sample_size(&m.format);
	if (!ga_isok(ga_mutex_create(&ctx->pos_mutex))) goto fail;

	ga_data_source_acquire(data_src);

	return ret;

fail:
	ga_free(ctx);
	return NULL;
}
