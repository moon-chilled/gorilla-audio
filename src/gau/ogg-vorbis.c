/* OGG Sample Source */
#include "vorbis/vorbisfile.h"

#include "gorilla/ga.h"
#include "gorilla/gau.h"

typedef struct {
	ga_DataSource *data_src;
} gau_OggDataSourceCallbackData;

static size_t gauX_sample_source_ogg_callback_read(void *ptr, size_t size, size_t nmemb, void *datasource) {
	ga_DataSource *ds = ((gau_OggDataSourceCallbackData*)datasource)->data_src;
	return ga_data_source_read(ds, ptr, size, nmemb);
}

static int gauX_sample_source_ogg_callback_seek(void *datasource, ogg_int64_t offset, int whence) {
	// concession for 32-bit platforms
	if (offset > GC_SSIZE_MAX) return -1;

	gc_ssize off = offset;

	ga_DataSource *ds = ((gau_OggDataSourceCallbackData*)datasource)->data_src;
	gc_result res;
	switch (whence) {
		case SEEK_SET: res = ga_data_source_seek(ds, off, GaSeekOrigin_Set); break;
		case SEEK_CUR: res = ga_data_source_seek(ds, off, GaSeekOrigin_Cur); break;
		case SEEK_END: res = ga_data_source_seek(ds, off, GaSeekOrigin_End); break;
		default: res = GC_ERROR_GENERIC; break;
	}

	return res==GC_SUCCESS ? 0 : -1;
}

static long gauX_sample_source_ogg_callback_tell(void *datasource) {
	return ga_data_source_tell(((gau_OggDataSourceCallbackData*)datasource)->data_src);
}

static int gauX_sample_source_ogg_callback_close(void *datasource) {
	// should this ga_data_source_release(((gau_OggDataSourceCallbackData*)datasource)->data_src)?
	return 1;
}

typedef struct {
	ga_DataSource *data_src;
	gc_int32 end_of_samples;
	OggVorbis_File ogg_file;
	vorbis_info *ogg_info;
	gau_OggDataSourceCallbackData ogg_callback_data;
	gc_Mutex *ogg_mutex;
} gau_SampleSourceOggContext;

typedef struct gau_SampleSourceOgg {
	ga_SampleSource sample_src;
	gau_SampleSourceOggContext context;
} gau_SampleSourceOgg;

static gc_size gauX_sample_source_ogg_read(void *context, void *odst, gc_size num_samples,
                                            GaCbOnSeek onseek, void *seek_ctx) {
	gau_SampleSourceOggContext *ctx = &((gau_SampleSourceOgg*)context)->context;
	gc_int32 samples_left = num_samples;
	gc_int32 samples_read;
	gc_int32 channels = ctx->ogg_info->channels;
	gc_size total_samples = 0;
	do {
		gc_float32 **samples;
		gc_int32 i;
		gc_int16 *dst;
		gc_int32 channel;
		gc_mutex_lock(ctx->ogg_mutex);
		samples_read = ov_read_float(&ctx->ogg_file, &samples, samples_left, NULL);
		if (samples_read == 0) ctx->end_of_samples = 1;
		gc_mutex_unlock(ctx->ogg_mutex);
		if (samples_read > 0) {
			samples_left -= samples_read;
			dst = (gc_int16*)odst + total_samples * channels;
			total_samples += samples_read;
			for (i = 0; i < samples_read; ++i) {
				for (channel = 0; channel < channels; ++channel, ++dst) {
					gc_float32 sample = samples[channel][i] * 32768.0f;
					gc_int32 int32Sample = (gc_int32)sample;
					gc_int16 int16Sample;
					int32Sample = int32Sample > 32767 ? 32767 : int32Sample < -32768 ? -32768 : int32Sample;
					int16Sample = (gc_int16)int32Sample;
					*dst = int16Sample;
				}
			}
		}
	} while (samples_read > 0 && samples_left);
	return total_samples;
}
static gc_bool gauX_sample_source_ogg_end(void *context) {
	return ((gau_SampleSourceOgg*)context)->context.end_of_samples; /* No need for a mutex here */
}
static gc_result gauX_sample_source_ogg_seek(void *context, gc_size sample_offset) {
	gau_SampleSourceOggContext *ctx = &((gau_SampleSourceOgg*)context)->context;
	gc_mutex_lock(ctx->ogg_mutex);
	int res = ov_pcm_seek(&ctx->ogg_file, sample_offset);
	ctx->end_of_samples = 0;
	gc_mutex_unlock(ctx->ogg_mutex);
	return res==0 ? GC_SUCCESS : GC_ERROR_GENERIC; //ov_pcm_seek returns 0 on success
}
static gc_result gauX_sample_source_ogg_tell(void *context, gc_size *cur, gc_size *ototal) {
	gc_result ret = GC_SUCCESS;
	gau_SampleSourceOggContext *ctx = &((gau_SampleSourceOgg*)context)->context;
	gc_mutex_lock(ctx->ogg_mutex);
	/* TODO: Decide whether to support total samples for OGG files */
	if (ototal) {
		gc_int64 ctotal = ov_pcm_total(&ctx->ogg_file, -1); /* Note: This isn't always valid when the stream is poorly-formatted */
		if (ctotal < 0) ret = GC_ERROR_GENERIC;
		else *ototal = (gc_size)ctotal;
	}
	if (cur) *cur = (gc_size)ov_pcm_tell(&ctx->ogg_file);
	gc_mutex_unlock(ctx->ogg_mutex);
	return ret;
}
static void gauX_sample_source_ogg_close(void *context) {
	gau_SampleSourceOggContext *ctx = &((gau_SampleSourceOgg*)context)->context;
	ov_clear(&ctx->ogg_file);
	ga_data_source_release(ctx->data_src);
	gc_mutex_destroy(ctx->ogg_mutex);
}
ga_SampleSource *gau_sample_source_create_ogg(ga_DataSource *data) {
	gau_SampleSourceOgg *ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceOgg));
	gau_SampleSourceOggContext *ctx = &ret->context;
	gc_int32 bytes_per_sample = 2;
	ov_callbacks ogg_callbacks;
	gc_bool seekable = ga_data_source_flags(data) & GaDataAccessFlag_Seekable;
	ga_sample_source_init(&ret->sample_src);
	ret->sample_src.flags = GaDataAccessFlag_Threadsafe;
	if (seekable) ret->sample_src.flags |= GaDataAccessFlag_Seekable;
	ret->sample_src.read = &gauX_sample_source_ogg_read;
	ret->sample_src.end = &gauX_sample_source_ogg_end;
	if (seekable) {
		ret->sample_src.seek = &gauX_sample_source_ogg_seek;
		ret->sample_src.tell = &gauX_sample_source_ogg_tell;
	}
	ret->sample_src.close = &gauX_sample_source_ogg_close;
	ga_data_source_acquire(data);
	ctx->data_src = data;
	ctx->end_of_samples = 0;

	/* OGG Setup */
	ogg_callbacks.read_func = &gauX_sample_source_ogg_callback_read;
	if (seekable) {
		ogg_callbacks.seek_func = &gauX_sample_source_ogg_callback_seek;
		ogg_callbacks.tell_func = &gauX_sample_source_ogg_callback_tell;
	} else {
		ogg_callbacks.seek_func = NULL;
		ogg_callbacks.tell_func = NULL;
	}
	ogg_callbacks.close_func = &gauX_sample_source_ogg_callback_close;
	ctx->ogg_callback_data.data_src = data;

	gc_bool is_valid_ogg = gc_false;

	/* 0 means "open" */
	if (ov_open_callbacks(&ctx->ogg_callback_data, &ctx->ogg_file, 0, 0, ogg_callbacks) == 0) {
		ctx->ogg_info = ov_info(&ctx->ogg_file, -1);
		ov_pcm_seek(&ctx->ogg_file, 0); /* Seek fixes some poorly-formatted OGGs. */
		is_valid_ogg = ctx->ogg_info->channels <= 2;
		if (is_valid_ogg) {
			ret->sample_src.format.bits_per_sample = bytes_per_sample * 8;
			ret->sample_src.format.num_channels = ctx->ogg_info->channels;
			ret->sample_src.format.sample_rate = ctx->ogg_info->rate;
		} else {
			ov_clear(&ctx->ogg_file);
		}
	}
	if (!is_valid_ogg) {
		ga_data_source_release(data);
		gcX_ops->freeFunc(ret);
		return NULL;
	}
	ctx->ogg_mutex = gc_mutex_create();
	return (ga_SampleSource*)ret;
}
