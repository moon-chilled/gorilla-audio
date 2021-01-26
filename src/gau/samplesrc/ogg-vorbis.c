#define OV_EXCLUDE_STATIC_CALLBACKS
#include "vorbis/vorbisfile.h"

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

static size_t ogg_read(void *ptr, size_t size, size_t nmemb, void *datasource) {
	GaDataSource **ds = datasource;
	return ga_data_source_read(*ds, ptr, size, nmemb);
}

static int ogg_seek(void *datasource, ogg_int64_t offset, int whence) {
	// concession for 32-bit platforms
	if (offset > GA_SSIZE_MAX) return -1;

	ssz off = offset;

	GaDataSource **ds = datasource;
	ga_result res;
	switch (whence) {
		case SEEK_SET: res = ga_data_source_seek(*ds, off, GaSeekOrigin_Set); break;
		case SEEK_CUR: res = ga_data_source_seek(*ds, off, GaSeekOrigin_Cur); break;
		case SEEK_END: res = ga_data_source_seek(*ds, off, GaSeekOrigin_End); break;
		default: res = GA_ERR_MIS_PARAM; break;
	}

	return ga_isok(res) ? 0 : -1;
}

static long ogg_tell(void *datasource) {
	GaDataSource **ds = datasource;
	return ga_data_source_tell(*ds);
}

static int ogg_close(void *datasource) {
	// should this ga_data_source_release() with commensurate acquire elsewhere?
	return 1;
}

struct GaSampleSourceContext {
	GaDataSource *data_src;
	u32 sample_size;
	bool end_of_samples;
	OggVorbis_File ogg_file;
	vorbis_info *ogg_info;
	GaMutex ogg_mutex;
};

static usz ss_read(GaSampleSourceContext *ctx, void *odst, usz num_samples,
                                            GaCbOnSeek onseek, void *seek_ctx) {
	s32 samples_left = num_samples;
	s32 samples_read;
	s32 channels = ctx->ogg_info->channels;
	usz total_samples = 0;
	do {
		f32 **samples;
		s32 i;
		s16 *dst;
		s32 channel;
		ga_mutex_lock(ctx->ogg_mutex);
		samples_read = ov_read_float(&ctx->ogg_file, &samples, samples_left, NULL);
		if (samples_read == 0) ctx->end_of_samples = true;
		ga_mutex_unlock(ctx->ogg_mutex);
		if (samples_read > 0) {
			samples_left -= samples_read;
			dst = (s16*)odst + total_samples * channels;
			total_samples += samples_read;
			for (i = 0; i < samples_read; ++i) {
				for (channel = 0; channel < channels; ++channel, ++dst) {
					f32 sample = samples[channel][i] * 32768.0f;
					s32 int32Sample = (s32)sample;
					s16 int16Sample;
					int32Sample = int32Sample > 32767 ? 32767 : int32Sample < -32768 ? -32768 : int32Sample;
					int16Sample = (s16)int32Sample;
					*dst = int16Sample;
				}
			}
		}
	} while (samples_read > 0 && samples_left);
	return total_samples;
}
#if 0
static usz ss_read(GaSampleSourceContext *ctx, void *odst, usz num_samples,
                                            GaCbOnSeek onseek, void *seek_ctx) {
	s32 samples_left = num_samples;
	s32 samples_read;
	s32 channels = ctx->ogg_info->channels;
	usz total_samples = 0;

	char *dst = odst;

	do {
		long bytes_read = ov_read(&ctx->ogg_file, dst, num_samples * ctx->ogg_info->channels * ctx->sample_size, 0/*little endian*/, ctx->sample_size, 1/*signed, todo not true for 8bit*/, NULL);
		if (bytes_read < 0) break;
		dst += bytes_read;

		samples_read = bytes_read / (ctx->ogg_info->channels * ctx->sample_size);
		samples_left -= samples_read;
	} while (samples_read > 0 && samples_left);
	return total_samples;
}
#endif

static bool ss_end(GaSampleSourceContext *ctx) {
	return ctx->end_of_samples;
}
static ga_result ss_seek(GaSampleSourceContext *ctx, usz sample_offset) {
	int res;
	with_mutex(ctx->ogg_mutex) {
		res = ov_pcm_seek(&ctx->ogg_file, sample_offset);
		ctx->end_of_samples = false;
	}
	switch (res) {
		case 0: return GA_OK; //ov_pcm_seek returns 0 on success
		case OV_ENOSEEK: return GA_ERR_MIS_UNSUP;
		case OV_EINVAL:
		case OV_EBADLINK: return GA_ERR_MIS_PARAM;
		case OV_EREAD: return GA_ERR_SYS_IO;
		case OV_EFAULT: //internal ov error, but our _own_ state remains consistent so not GA_ERR_INTERNAL
		default: return GA_ERR_SYS_LIB;
	}
}
static ga_result ss_tell(GaSampleSourceContext *ctx, usz *cur, usz *ototal) {
	ga_result ret = GA_OK;
	with_mutex(ctx->ogg_mutex) {
		/* TODO: Decide whether to support total samples for OGG files */
		if (ototal) {
			s64 ctotal = ov_pcm_total(&ctx->ogg_file, -1); /* Note: This isn't always valid when the stream is poorly-formatted */
			if (ctotal < 0) ret = GA_ERR_MIS_UNSUP;
			else *ototal = (usz)ctotal;
		}
		if (cur) *cur = (usz)ov_pcm_tell(&ctx->ogg_file);
	}
	return ret;
}
static void ss_close(GaSampleSourceContext *ctx) {
	ov_clear(&ctx->ogg_file);
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->ogg_mutex);
	ga_free(ctx);
}
GaSampleSource *gau_sample_source_create_ogg(GaDataSource *data) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;

	ctx->data_src = data;
	ctx->end_of_samples = false;
	if (!ga_isok(ga_mutex_create(&ctx->ogg_mutex))) goto fail;

	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.ready = NULL,
		.close = ss_close,
		.context = ctx,
		.threadsafe = true,
	};
	bool seekable = ga_data_source_flags(data) & GaDataAccessFlag_Seekable;
	if (seekable) {
		m.seek = ss_seek;
		m.tell = ss_tell;
	}

	ov_callbacks ogg_callbacks = {
		.read_func = ogg_read,
		.close_func = ogg_close,

	};

	if (seekable) {
		ogg_callbacks.seek_func = ogg_seek;
		ogg_callbacks.tell_func = ogg_tell;
	}

	/* 0 means "open" */
	if (ov_open_callbacks(&ctx->data_src, &ctx->ogg_file, 0, 0, ogg_callbacks) != 0) goto fail;
	ctx->ogg_info = ov_info(&ctx->ogg_file, -1);
	ov_pcm_seek(&ctx->ogg_file, 0); /* Seek fixes some poorly-formatted OGGs. */
	bool is_valid_ogg = ctx->ogg_info->channels <= 2;
	if (!is_valid_ogg) {
		ov_clear(&ctx->ogg_file);
		goto fail;
	}
	m.format.bits_per_sample = 2 * 8; //32 bytes/sample
	m.format.num_channels = ctx->ogg_info->channels;
	m.format.sample_rate = ctx->ogg_info->rate;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;

	ga_data_source_acquire(data);
	return ret;

fail:
	ga_free(ctx);
	return NULL;
}
