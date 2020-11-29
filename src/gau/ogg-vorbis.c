/* OGG Sample Source */
#include "vorbis/vorbisfile.h"

#include "gorilla/ga.h"
#include "gorilla/gau.h"

typedef struct {
	ga_DataSource* dataSrc;
} gau_OggDataSourceCallbackData;

static size_t gauX_sample_source_ogg_callback_read(void *ptr, size_t size, size_t nmemb, void *datasource) {
	ga_DataSource* ds = ((gau_OggDataSourceCallbackData*)datasource)->dataSrc;
	return ga_data_source_read(ds, ptr, size, nmemb);
}

static gc_result gauX_sample_source_ogg_callback_seek(void *datasource, ogg_int64_t offset, int whence) {
	ga_DataSource* ds = ((gau_OggDataSourceCallbackData*)datasource)->dataSrc;
	switch (whence) {
		case SEEK_SET: return ga_data_source_seek(ds, (gc_int32)offset, GA_SEEK_ORIGIN_SET);
		case SEEK_CUR: return ga_data_source_seek(ds, (gc_int32)offset, GA_SEEK_ORIGIN_CUR);
		case SEEK_END: return ga_data_source_seek(ds, (gc_int32)offset, GA_SEEK_ORIGIN_END);
		default: return GC_ERROR_GENERIC;
	}
}

static long gauX_sample_source_ogg_callback_tell(void *datasource) {
	return ga_data_source_tell(((gau_OggDataSourceCallbackData*)datasource)->dataSrc);
}

static int gauX_sample_source_ogg_callback_close(void *datasource) {
	// should this ga_data_source_release(((gau_OggDataSourceCallbackData*)datasource)->dataSrc)?
	return 1;
}

typedef struct {
	ga_DataSource* dataSrc;
	gc_int32 endOfSamples;
	OggVorbis_File oggFile;
	vorbis_info* oggInfo;
	gau_OggDataSourceCallbackData oggCallbackData;
	gc_Mutex* oggMutex;
} gau_SampleSourceOggContext;

typedef struct gau_SampleSourceOgg {
	ga_SampleSource sampleSrc;
	gau_SampleSourceOggContext context;
} gau_SampleSourceOgg;

static gc_int32 gauX_sample_source_ogg_read(void* in_context, void* in_dst, gc_int32 in_numSamples,
                                            tOnSeekFunc in_onSeekFunc, void* in_seekContext) {
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
	gc_int32 samplesLeft = in_numSamples;
	gc_int32 samplesRead;
	gc_int32 channels = ctx->oggInfo->channels;
	gc_int32 totalSamples = 0;
	gc_size dataSizeOff = 0;
	do {
		gc_int32 bitStream;
		gc_float32** samples;
		gc_int32 i;
		gc_int16* dst;
		gc_int32 channel;
		gc_mutex_lock(ctx->oggMutex);
		samplesRead = ov_read_float(&ctx->oggFile, &samples, samplesLeft, &bitStream);
		if (samplesRead == 0) ctx->endOfSamples = 1;
		gc_mutex_unlock(ctx->oggMutex);
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
static gc_int32 gauX_sample_source_ogg_end(void* in_context) {
	return ((gau_SampleSourceOgg*)in_context)->context.endOfSamples; /* No need for a mutex here */
}
static gc_result gauX_sample_source_ogg_seek(void* in_context, gc_int32 in_sampleOffset) {
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
	gc_mutex_lock(ctx->oggMutex);
	int res = ov_pcm_seek(&ctx->oggFile, in_sampleOffset);
	ctx->endOfSamples = 0;
	gc_mutex_unlock(ctx->oggMutex);
	return res==0 ? GC_SUCCESS : GC_ERROR_GENERIC; //ov_pcm_seek returns 0 on success
}
static gc_int32 gauX_sample_source_ogg_tell(void* in_context, gc_int32* out_totalSamples) {
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
	gc_int32 ret;
	gc_mutex_lock(ctx->oggMutex);
	/* TODO: Decide whether to support total samples for OGG files */
	if (out_totalSamples)
		*out_totalSamples = ov_pcm_total(&ctx->oggFile, -1); /* Note: This isn't always valid when the stream is poorly-formatted */
	ret = (gc_int32)ov_pcm_tell(&ctx->oggFile);
	gc_mutex_unlock(ctx->oggMutex);
	return ret;
}
static void gauX_sample_source_ogg_close(void* in_context) {
	gau_SampleSourceOggContext* ctx = &((gau_SampleSourceOgg*)in_context)->context;
	ov_clear(&ctx->oggFile);
	ga_data_source_release(ctx->dataSrc);
	gc_mutex_destroy(ctx->oggMutex);
}
ga_SampleSource* gau_sample_source_create_ogg(ga_DataSource* in_dataSrc) {
	gau_SampleSourceOgg* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceOgg));
	gau_SampleSourceOggContext* ctx = &ret->context;
	gc_int32 endian = 0; /* 0 is little endian (aka x86), 1 is big endian */
	gc_int32 bytesPerSample = 2;
	gc_int32 oggIsOpen;
	ov_callbacks oggCallbacks;
	gc_int32 seekable = ga_data_source_flags(in_dataSrc) & GA_FLAG_SEEKABLE ? 1 : 0;
	ga_sample_source_init(&ret->sampleSrc);
	ret->sampleSrc.flags = GA_FLAG_THREADSAFE;
	if (seekable) ret->sampleSrc.flags |= GA_FLAG_SEEKABLE;
	ret->sampleSrc.readFunc = &gauX_sample_source_ogg_read;
	ret->sampleSrc.endFunc = &gauX_sample_source_ogg_end;
	if (seekable) {
		ret->sampleSrc.seekFunc = &gauX_sample_source_ogg_seek;
		ret->sampleSrc.tellFunc = &gauX_sample_source_ogg_tell;
	}
	ret->sampleSrc.closeFunc = &gauX_sample_source_ogg_close;
	ga_data_source_acquire(in_dataSrc);
	ctx->dataSrc = in_dataSrc;
	ctx->endOfSamples = 0;

	/* OGG Setup */
	oggCallbacks.read_func = &gauX_sample_source_ogg_callback_read;
	if (seekable) {
		oggCallbacks.seek_func = &gauX_sample_source_ogg_callback_seek;
		oggCallbacks.tell_func = &gauX_sample_source_ogg_callback_tell;
	} else {
		oggCallbacks.seek_func = 0;
		oggCallbacks.tell_func = 0;
	}
	oggCallbacks.close_func = &gauX_sample_source_ogg_callback_close;
	ctx->oggCallbackData.dataSrc = in_dataSrc;
	oggIsOpen = ov_open_callbacks(&ctx->oggCallbackData, &ctx->oggFile, 0, 0, oggCallbacks);

	gc_bool isValidOgg = gc_false;
	/* 0 means "open" */
	if (oggIsOpen == 0) {
		ctx->oggInfo = ov_info(&ctx->oggFile, -1);
		ov_pcm_seek(&ctx->oggFile, 0); /* Seek fixes some poorly-formatted OGGs. */
		isValidOgg = ctx->oggInfo->channels <= 2;
		if (isValidOgg) {
			ret->sampleSrc.format.bitsPerSample = bytesPerSample * 8;
			ret->sampleSrc.format.numChannels = ctx->oggInfo->channels;
			ret->sampleSrc.format.sampleRate = ctx->oggInfo->rate;
		} else {
			ov_clear(&ctx->oggFile);
		}
	}
	if (!isValidOgg) {
		ga_data_source_release(in_dataSrc);
		gcX_ops->freeFunc(ret);
		return NULL;
	}
	ctx->oggMutex = gc_mutex_create();
	return (ga_SampleSource*)ret;
}
