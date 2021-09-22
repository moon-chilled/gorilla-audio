#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

#if GAU_SUPPORT_VORBIS == 0
GaSampleSource *gau_sample_source_create_vorbis(GaDataSource *data) { return NULL; }
#elif GAU_SUPPORT_VORBIS == 1

#define OV_EXCLUDE_STATIC_CALLBACKS

#include <limits.h>

#include "vorbis/vorbisfile.h"

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
	bool end_of_samples;
	OggVorbis_File ogg_file;
	vorbis_info *ogg_info;
	GaMutex ogg_mutex;
};

static usz ss_read(GaSampleSourceContext *ctx, void *odst, usz num_frames,
                   GaCbOnSeek onseek, void *seek_ctx) {
	usz samples_left = num_frames;
	long samples_read;
	usz total_samples = 0;
	f32 *dst = odst;
	do {
		f32 **samples;
		with_mutex(ctx->ogg_mutex)
			samples_read = ov_read_float(&ctx->ogg_file, &samples, min(samples_left, INT_MAX), NULL);
		if (samples_read <= 0) ctx->end_of_samples = true;
		if (samples_read > 0) {
			samples_left -= samples_read;
			total_samples += samples_read;

			for (u32 i = 0; i < samples_read; ++i) {
				for (s32 channel = 0; channel < ctx->ogg_info->channels; ++channel) {
					*dst++ = samples[channel][i];
				}
			}
		}
	} while (samples_read > 0 && samples_left);
	return total_samples;
}

static bool ss_end(GaSampleSourceContext *ctx) {
	return ctx->end_of_samples;
}
static ga_result ss_seek(GaSampleSourceContext *ctx, usz frame_offset) {
	int res;
	with_mutex(ctx->ogg_mutex) {
		res = ov_pcm_seek(&ctx->ogg_file, frame_offset);
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
		/* TODO: Decide whether to support total frames for OGG files */
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
GaSampleSource *gau_sample_source_create_vorbis(GaDataSource *data) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;

	ctx->data_src = data;
	ctx->end_of_samples = false;
	if (!ga_isok(ga_mutex_create(&ctx->ogg_mutex))) goto fail;

	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.ready = NULL,
		.tell = ss_tell,
		.close = ss_close,
		.context = ctx,
		.threadsafe = true,
	};
	bool seekable = ga_data_source_flags(data) & GaDataAccessFlag_Seekable;
	if (seekable) m.seek = ss_seek;

	ov_callbacks ogg_callbacks = {
		.read_func = ogg_read,
		.tell_func = ogg_tell,
		.close_func = ogg_close,

	};

	if (seekable) ogg_callbacks.seek_func = ogg_seek;

	/* 0 means "open" */
	if (ov_open_callbacks(&ctx->data_src, &ctx->ogg_file, 0, 0, ogg_callbacks) != 0) goto fail;
	ctx->ogg_info = ov_info(&ctx->ogg_file, -1);
	if (seekable) ov_pcm_seek(&ctx->ogg_file, 0); /* Seek fixes some poorly-formatted OGGs. */
	bool is_valid_ogg = ctx->ogg_info->channels <= 2;
	if (!is_valid_ogg) {
		ov_clear(&ctx->ogg_file);
		goto fail;
	}
	m.format.sample_fmt = GaSampleFormat_F32;
	m.format.num_channels = ctx->ogg_info->channels;
	m.format.frame_rate = ctx->ogg_info->rate;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;

	ga_data_source_acquire(data);
	return ret;

fail:
	ga_free(ctx);
	return NULL;
}

#elif GAU_SUPPORT_VORBIS == 2

//#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "stb_vorbis.c"

struct GaSampleSourceContext {
	GaDataSource *data_src;
	bool end_of_samples;
	stb_vorbis *vorb;
	GaMutex mutex;
};

static usz ss_read(GaSampleSourceContext *ctx, void *dst, usz num_frames, GaCbOnSeek onseek, void *seek_ctx) {
	int ret = stb_vorbis_get_samples_float_interleaved(ctx->vorb, ctx->vorb->channels, dst, num_frames * ctx->vorb->channels);
	if (ret < 0) return 0;
	return ret;
}

static bool ss_end(GaSampleSourceContext *ctx) {
	return ctx->vorb->eof;
}

static ga_result ss_seek(GaSampleSourceContext *ctx, usz frame_offset) {
	return stb_vorbis_seek(ctx->vorb, frame_offset/**ctx->vorb->channels*/) ? GA_OK : GA_ERR_GENERIC;
}

static ga_result ss_tell(GaSampleSourceContext *ctx, usz *frames, usz *total_frames) {
	ga_result ret = GA_OK;
	if (total_frames) {
		with_mutex(ctx->mutex) {
			if (!(*total_frames = stb_vorbis_stream_length_in_samples(ctx->vorb)))
				ret = GA_ERR_MIS_UNSUP;
		}
	}
	if (!ga_isok(ret)) return ret;
	if (frames) {
		with_mutex(ctx->mutex) {
			if (!ctx->vorb->current_loc_valid) ret = GA_ERR_INTERNAL;
			*frames = ctx->vorb->current_loc;
		}
	}
	return ret;
}

static void ss_close(GaSampleSourceContext *ctx) {
	stb_vorbis_close(ctx->vorb);
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->mutex);
	ga_free(ctx);
}

GaSampleSource *gau_sample_source_create_vorbis(GaDataSource *data) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;

	ctx->data_src = data;
	ctx->end_of_samples = false;
	if (!ga_isok(ga_mutex_create(&ctx->mutex))) goto fail;
	if (!(ctx->vorb = stb_vorbis_open_data_source(data, NULL/*err*/, NULL/*alloc*/))) goto fail;

	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.ready = NULL,
		.tell = ss_tell,
		.close = ss_close,
		.context = ctx,
		.format = {.num_channels = ctx->vorb->channels, .frame_rate = ctx->vorb->sample_rate, .sample_fmt = GaSampleFormat_F32},
		.threadsafe = true,
	};
	bool seekable = ga_data_source_flags(data) & GaDataAccessFlag_Seekable;
	if (seekable) m.seek = ss_seek;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;

	ga_data_source_acquire(data);
	return ret;
fail:
	return NULL;
}

#endif //GAU_SUPPORT_VORBIS
