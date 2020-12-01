/* OGG Sample Source */
#include "vorbis/vorbisfile.h"

#include "gorilla/ga.h"
#include "gorilla/gau.h"

typedef struct {
	ga_DataSource* data_src;
} gau_OggDataSourceCallbackData;

static size_t gauX_sample_source_ogg_callback_read(void *ptr, size_t size, size_t nmemb, void *datasource) {
	ga_DataSource* ds = ((gau_OggDataSourceCallbackData*)datasource)->data_src;
	return ga_data_source_read(ds, ptr, size, nmemb);
}

static int gauX_sample_source_ogg_callback_seek(void *datasource, ogg_int64_t offset, int whence) {
	// concessions for 32-bit platforms
	if (offset > GC_SSIZE_MAX) return -1;
	gc_ssize off = offset;

	ga_DataSource* ds = ((gau_OggDataSourceCallbackData*)datasource)->data_src;
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
	ga_DataSource* data_src;
	gc_int32 end_of_samples;
	OggVorbis_File ogg_file;
	vorbis_info* ogg_info;
	gau_OggDataSourceCallbackData ogg_callback_data;
	gc_Mutex* ogg_mutex;
} gau_SampleSourceOggContext;

typedef struct gau_SampleSourceOgg {
	ga_SampleSource sample_src;
	gau_SampleSourceOggContext context;
} gau_SampleSourceOgg;

static gc_size gauX_sample_source_ogg_read(void* in_context, void* in_dst, gc_size in_numSamples,
                                            tOnSeekFunc in_onSeekFunc, void* in_seekContext) {
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
	gc_int32 samplesLeft = in_numSamples;
	gc_int32 samplesRead;
	gc_int32 channels = ctx->ogg_info->channels;
	gc_int32 totalSamples = 0;
	do {
		gc_int32 bitStream;
		gc_float32** samples;
		gc_int32 i;
		gc_int16* dst;
		gc_int32 channel;
		gc_mutex_lock(ctx->ogg_mutex);
		samplesRead = ov_read_float(&ctx->ogg_file, &samples, samplesLeft, &bitStream);
		if (samplesRead == 0) ctx->end_of_samples = 1;
		gc_mutex_unlock(ctx->ogg_mutex);
		if (samplesRead > 0) {
			samplesLeft -= samplesRead;
			dst = (gc_int16*)(in_dst) + totalSamples * channels;
			totalSamples += samplesRead;
			for (i = 0; i < samplesRead; ++i) {
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
	} while (samplesRead > 0 && samplesLeft);
	return totalSamples;
}
static gc_bool gauX_sample_source_ogg_end(void* in_context) {
	return ((gau_SampleSourceOgg*)in_context)->context.end_of_samples; /* No need for a mutex here */
}
static gc_result gauX_sample_source_ogg_seek(void* in_context, gc_size in_sampleOffset) {
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
	gc_mutex_lock(ctx->ogg_mutex);
	int res = ov_pcm_seek(&ctx->ogg_file, in_sampleOffset);
	ctx->end_of_samples = 0;
	gc_mutex_unlock(ctx->ogg_mutex);
	return res==0 ? GC_SUCCESS : GC_ERROR_GENERIC; //ov_pcm_seek returns 0 on success
}
static gc_result gauX_sample_source_ogg_tell(void* in_context, gc_size *cur, gc_size *ototal) {
	gc_result ret = GC_SUCCESS;
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
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
static void gauX_sample_source_ogg_close(void* in_context) {
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
	ov_clear(&ctx->ogg_file);
	ga_data_source_release(ctx->data_src);
	gc_mutex_destroy(ctx->ogg_mutex);
}
ga_SampleSource* gau_sample_source_create_ogg(ga_DataSource* in_dataSrc) {
	gau_SampleSourceOgg* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceOgg));
	gau_SampleSourceOggContext* ctx = &ret->context;
	gc_int32 bytesPerSample = 2;
	gc_int32 oggIsOpen;
	ov_callbacks oggCallbacks;
	gc_bool seekable = ga_data_source_flags(in_dataSrc) & GaDataAccessFlag_Seekable;
	ga_sample_source_init(&ret->sample_src);
	ret->sample_src.flags = GaDataAccessFlag_Threadsafe;
	if (seekable) ret->sample_src.flags |= GaDataAccessFlag_Seekable;
	ret->sample_src.readFunc = &gauX_sample_source_ogg_read;
	ret->sample_src.endFunc = &gauX_sample_source_ogg_end;
	if (seekable) {
		ret->sample_src.seekFunc = &gauX_sample_source_ogg_seek;
		ret->sample_src.tellFunc = &gauX_sample_source_ogg_tell;
	}
	ret->sample_src.closeFunc = &gauX_sample_source_ogg_close;
	ga_data_source_acquire(in_dataSrc);
	ctx->data_src = in_dataSrc;
	ctx->end_of_samples = 0;

	/* OGG Setup */
	oggCallbacks.read_func = &gauX_sample_source_ogg_callback_read;
	if (seekable) {
		oggCallbacks.seek_func = &gauX_sample_source_ogg_callback_seek;
		oggCallbacks.tell_func = &gauX_sample_source_ogg_callback_tell;
	} else {
		oggCallbacks.seek_func = NULL;
		oggCallbacks.tell_func = NULL;
	}
	oggCallbacks.close_func = &gauX_sample_source_ogg_callback_close;
	ctx->ogg_callback_data.data_src = in_dataSrc;
	oggIsOpen = ov_open_callbacks(&ctx->ogg_callback_data, &ctx->ogg_file, 0, 0, oggCallbacks);

	gc_bool isValidOgg = gc_false;
	/* 0 means "open" */
	if (oggIsOpen == 0) {
		ctx->ogg_info = ov_info(&ctx->ogg_file, -1);
		ov_pcm_seek(&ctx->ogg_file, 0); /* Seek fixes some poorly-formatted OGGs. */
		isValidOgg = ctx->ogg_info->channels <= 2;
		if (isValidOgg) {
			ret->sample_src.format.bits_per_sample = bytesPerSample * 8;
			ret->sample_src.format.num_channels = ctx->ogg_info->channels;
			ret->sample_src.format.sample_rate = ctx->ogg_info->rate;
		} else {
			ov_clear(&ctx->ogg_file);
		}
	}
	if (!isValidOgg) {
		ga_data_source_release(in_dataSrc);
		gcX_ops->freeFunc(ret);
		return NULL;
	}
	ctx->ogg_mutex = gc_mutex_create();
	return (ga_SampleSource*)ret;
}
