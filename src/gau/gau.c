#include "gorilla/ga.h"
#include "gorilla/gau.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

/* High-Level Manager */
struct GauManager {
	GauThreadPolicy threadPolicy;
	GaThread* mixThread;
	GaThread* streamThread;
	GaDevice* device;
	GaMixer* mixer;
	GaStreamManager* streamMgr;
	gc_int32 sample_size;
	gc_int16* mixBuffer;
	GaFormat format;
	gc_int32 killThreads;
};

static gc_int32 gauX_mixThreadFunc(void *context) {
	GauManager* ctx = (GauManager*)context;
	while (!ctx->killThreads) {
		gc_int32 numToQueue = ga_device_check(ctx->device);
		while (numToQueue--) {
			ga_mixer_mix(ctx->mixer, ctx->mixBuffer);
			ga_device_queue(ctx->device, ctx->mixBuffer);
		}
		ga_thread_sleep(5);
	}
	return 0;
}
static gc_int32 gauX_streamThreadFunc(void *context) {
	GauManager* ctx = (GauManager*)context;
	while (!ctx->killThreads) {
		ga_stream_manager_buffer(ctx->streamMgr);
		ga_thread_sleep(50);
	}
	return 0;
}
GauManager *gau_manager_create(void) {
	return gau_manager_create_custom(NULL, GauThreadPolicy_Single, NULL, NULL);
}
GauManager *gau_manager_create_custom(GaDeviceType *dev_type,
                                       GauThreadPolicy thread_policy,
                                       gc_uint32 *num_buffers,
				       gc_uint32 *num_samples) {
	GauManager* ret = memset(gcX_ops->allocFunc(sizeof(GauManager)), 0, sizeof(GauManager));

	assert(thread_policy == GauThreadPolicy_Single
	       || thread_policy == GauThreadPolicy_Multi);
	if (!num_buffers) num_buffers = &(gc_uint32){4};
	if (!num_samples) num_samples = &(gc_uint32){512};
	assert(*num_buffers >= 2);
	assert(*num_samples >= 128);

	/* Open device */
	memset(&ret->format, 0, sizeof(GaFormat));
	ret->format.bits_per_sample = 16;
	ret->format.num_channels = 2;
	ret->format.sample_rate = 44100;
	ret->device = ga_device_open(dev_type, num_buffers, num_samples, &ret->format);
	if (!ret->device) goto fail;

	/* Initialize mixer */
	ret->mixer = ga_mixer_create(&ret->format, *num_samples);
	if (!ret->mixer) goto fail;
	ret->streamMgr = ga_stream_manager_create();
	if (!ret->streamMgr) goto fail;
	ret->sample_size = ga_format_sample_size(&ret->format);
	ret->mixBuffer = (gc_int16*)gcX_ops->allocFunc(ret->mixer->num_samples * ret->sample_size);
	if (!ret->mixBuffer) goto fail;

	/* Create and run mixer and stream threads */
	ret->threadPolicy = thread_policy;
	ret->killThreads = 0;
	if(ret->threadPolicy == GauThreadPolicy_Multi) {
		ret->mixThread = ga_thread_create(gauX_mixThreadFunc, ret, GaThreadPriority_Highest, 64 * 1024);
		ret->streamThread = ga_thread_create(gauX_streamThreadFunc, ret, GaThreadPriority_Highest, 64 * 1024);
		ga_thread_run(ret->mixThread);
		ga_thread_run(ret->streamThread);
	} else {
		ret->mixThread = 0;
		ret->streamThread = 0;
	}

	return ret;
fail:
	if (ret) {
		if (ret->device) ga_device_close(ret->device);
		if (ret->mixer) ga_mixer_destroy(ret->mixer);
		if (ret->streamMgr) ga_stream_manager_destroy(ret->streamMgr);
		if (ret->mixBuffer) gcX_ops->freeFunc(ret->mixBuffer);
		gcX_ops->freeFunc(ret);
	}
	return NULL;
}
void gau_manager_update(GauManager *mgr) {
	if (mgr->threadPolicy == GauThreadPolicy_Single) {
		gc_int16* buf = mgr->mixBuffer;
		GaMixer* mixer = mgr->mixer;
		GaDevice* dev = mgr->device;
		gc_int32 numToQueue = ga_device_check(dev);
		while (numToQueue--) {
			ga_mixer_mix(mixer, buf);
			ga_device_queue(dev, buf);
		}
		ga_stream_manager_buffer(mgr->streamMgr);
	}
	ga_mixer_dispatch(mgr->mixer);
}
GaMixer *gau_manager_mixer(GauManager *mgr) {
	return mgr->mixer;
}
GaStreamManager *gau_manager_stream_manager(GauManager* mgr) {
	return mgr->streamMgr;
}
GaDevice *gau_manager_device(GauManager *mgr) {
  return mgr->device;
}
void gau_manager_destroy(GauManager *mgr) {
	if (mgr->threadPolicy == GauThreadPolicy_Multi) {
		mgr->killThreads = 1;
		ga_thread_join(mgr->streamThread);
		ga_thread_join(mgr->mixThread);
		ga_thread_destroy(mgr->streamThread);
		ga_thread_destroy(mgr->mixThread);
	}

	/* Clean up mixer and stream manager */
	ga_stream_manager_destroy(mgr->streamMgr);
	ga_mixer_destroy(mgr->mixer);
	gcX_ops->freeFunc(mgr->mixBuffer);
	ga_device_close(mgr->device);
	gcX_ops->freeFunc(mgr);
}

/* On-Finish Callbacks */
void gau_on_finish_destroy(GaHandle *handle, void *ctx) {
	ga_handle_destroy(handle);
}

/* File-Based Data Source */
typedef struct {
	FILE* f;
	GaMutex* fileMutex;
} gau_DataSourceFileContext;

typedef struct {
	GaDataSource dataSrc;
	gau_DataSourceFileContext context;
} gau_DataSourceFile;

static gc_size gauX_data_source_file_read(void *context, void *dst, gc_size size, gc_size count) {
	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)context;
	gc_size ret;
	ga_mutex_lock(ctx->fileMutex);
	ret = fread(dst, size, count, ctx->f);
	ga_mutex_unlock(ctx->fileMutex);
	return ret;
}
static ga_result gauX_data_source_file_seek(void *context, gc_ssize offset, GaSeekOrigin whence) {
	int fwhence;
	switch (whence) {
		case GaSeekOrigin_Set: fwhence = SEEK_SET; break;
		case GaSeekOrigin_Cur: fwhence = SEEK_CUR; break;
		case GaSeekOrigin_End: fwhence = SEEK_END; break;
		default: return GA_ERR_GENERIC;
	}

	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	ga_mutex_lock(ctx->fileMutex);
	ga_result ret = fseek(ctx->f, offset, fwhence) == -1 ? GA_ERR_GENERIC : GA_OK;
	ga_mutex_unlock(ctx->fileMutex);

	return ret;
}
static gc_size gauX_data_source_file_tell(void *context) {
	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)context;
	ga_mutex_lock(ctx->fileMutex);
	gc_size ret = ftell(ctx->f);
	ga_mutex_unlock(ctx->fileMutex);
	return ret;
}
static void gauX_data_source_file_close(void *context) {
	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	fclose(ctx->f);
	ga_mutex_destroy(ctx->fileMutex);
}
static GaDataSource *gauX_data_source_create_fp(FILE *fp) {
	if (!fp) return NULL;

	rewind(fp);

	gau_DataSourceFile* ret = gcX_ops->allocFunc(sizeof(gau_DataSourceFile));
	ga_data_source_init(&ret->dataSrc);
	ret->dataSrc.flags = GaDataAccessFlag_Seekable | GaDataAccessFlag_Threadsafe;
	ret->dataSrc.read = &gauX_data_source_file_read;
	ret->dataSrc.seek = &gauX_data_source_file_seek;
	ret->dataSrc.tell = &gauX_data_source_file_tell;
	ret->dataSrc.close = &gauX_data_source_file_close;
	ret->context.f = fp;
	ret->context.fileMutex = ga_mutex_create();
	return (GaDataSource*)ret;
}

GaDataSource *gau_data_source_create_file(const char *fname) {
	return gauX_data_source_create_fp(fopen(fname, "rb"));
}


/* Memory-Based Data Source */
typedef struct {
	GaMemory* memory;
	gc_size pos;
	GaMutex* mutex;
} GauDataSourceMemoryContext;

typedef struct {
	GaDataSource dataSrc;
	GauDataSourceMemoryContext context;
} GauDataSourceMemory;

gc_size gauX_data_source_memory_read(void *context, void *dst, gc_size size, gc_size count) {
	GauDataSourceMemoryContext *ctx = (GauDataSourceMemoryContext*)context;
	gc_size ret = 0;
	gc_size dataSize = ga_memory_size(ctx->memory);
	gc_size toRead = size * count;
	gc_size remaining;

	ga_mutex_lock(ctx->mutex);
	remaining = dataSize - ctx->pos;
	toRead = toRead < remaining ? toRead : remaining;
	toRead = toRead - (toRead % size);
	if (toRead) {
		memcpy(dst, (char*)ga_memory_data(ctx->memory) + ctx->pos, toRead);
		ctx->pos += toRead;
		ret = toRead / size;
	}
	ga_mutex_unlock(ctx->mutex);
	return ret;
}
ga_result gauX_data_source_memory_seek(void *context, gc_ssize offset, GaSeekOrigin whence) {
	GauDataSourceMemoryContext* ctx = (GauDataSourceMemoryContext*)context;
	gc_size data_size = ga_memory_size(ctx->memory);
	gc_ssize pos;
	ga_mutex_lock(ctx->mutex);
	switch (whence) {
		case GaSeekOrigin_Set: pos = offset; break;
		case GaSeekOrigin_Cur: pos = ctx->pos + offset; break;
		case GaSeekOrigin_End: pos = data_size - offset; break;
		default: goto fail;
	}
	if (pos < 0 || (gc_size)pos > data_size) goto fail;
	ctx->pos = pos;
	ga_mutex_unlock(ctx->mutex);
	return GA_OK;
fail:
	ga_mutex_unlock(ctx->mutex);
	return GA_ERR_GENERIC;
}
gc_size gauX_data_source_memory_tell(void *context) {
	GauDataSourceMemoryContext* ctx = (GauDataSourceMemoryContext*)context;
	ga_mutex_lock(ctx->mutex);
	gc_size ret = ctx->pos;
	ga_mutex_unlock(ctx->mutex);
	return ret;
}
void gauX_data_source_memory_close(void *context) {
	GauDataSourceMemoryContext* ctx = (GauDataSourceMemoryContext*)context;
	ga_memory_release(ctx->memory);
	ga_mutex_destroy(ctx->mutex);
}
GaDataSource *gau_data_source_create_memory(GaMemory *memory) {
	GauDataSourceMemory *ret = gcX_ops->allocFunc(sizeof(GauDataSourceMemory));
	ga_data_source_init(&ret->dataSrc);
	ret->dataSrc.flags = GaDataAccessFlag_Seekable | GaDataAccessFlag_Threadsafe;
	ret->dataSrc.read = &gauX_data_source_memory_read;
	ret->dataSrc.seek = &gauX_data_source_memory_seek;
	ret->dataSrc.tell = &gauX_data_source_memory_tell;
	ret->dataSrc.close = &gauX_data_source_memory_close;
	ga_memory_acquire(memory);
	ret->context.memory = memory;
	ret->context.pos = 0;
	ret->context.mutex = ga_mutex_create();
	return (GaDataSource*)ret;
}

/* Stream Sample Source */
typedef struct {
	GaBufferedStream* stream;
} gau_SampleSourceStreamContext;

typedef struct {
	GaSampleSource sampleSrc;
	gau_SampleSourceStreamContext context;
} gau_SampleSourceStream;

gc_size gauX_sample_source_stream_read(void *context, void *dst, gc_size num_samples,
                                        GaCbOnSeek onseek, void *seek_ctx) {
	gau_SampleSourceStreamContext *ctx = &((gau_SampleSourceStream*)context)->context;
	return ga_stream_read(ctx->stream, dst, num_samples);
}
gc_bool gauX_sample_source_stream_end(void *context) {
	gau_SampleSourceStreamContext *ctx = &((gau_SampleSourceStream*)context)->context;
	return ga_stream_end(ctx->stream);
}
gc_bool gauX_sample_source_stream_ready(void *context, gc_size num_samples) {
	gau_SampleSourceStreamContext *ctx = &((gau_SampleSourceStream*)context)->context;
	return ga_stream_ready(ctx->stream, num_samples);
}
ga_result gauX_sample_source_stream_seek(void *context, gc_size sample_offset) {
	gau_SampleSourceStreamContext *ctx = &((gau_SampleSourceStream*)context)->context;
	return ga_stream_seek(ctx->stream, sample_offset);
}
ga_result gauX_sample_source_stream_tell(void *context, gc_size *samples, gc_size *totalSamples) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)context)->context;
	return ga_stream_tell(ctx->stream, samples, totalSamples);
}
void gauX_sample_source_stream_close(void *context) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)context)->context;
	ga_stream_release(ctx->stream);
}
GaSampleSource* gau_sample_source_create_stream(GaStreamManager *mgr, GaSampleSource* sample_src, gc_size buffer_samples) {
	gau_SampleSourceStream* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceStream));
	gau_SampleSourceStreamContext* ctx = &ret->context;
	GaBufferedStream* stream;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sample_source_format(sample_src, &ret->sampleSrc.format);
	stream = ga_stream_create(mgr, sample_src, buffer_samples * ga_format_sample_size(&ret->sampleSrc.format));
	if (!stream) {
		gcX_ops->freeFunc(ret);
		return NULL;
	}
	ctx->stream = stream;
	ret->sampleSrc.flags = ga_stream_flags(stream);
	ret->sampleSrc.flags |= GaDataAccessFlag_Threadsafe;
	ret->sampleSrc.read = &gauX_sample_source_stream_read;
	ret->sampleSrc.end = &gauX_sample_source_stream_end;
	ret->sampleSrc.ready = &gauX_sample_source_stream_ready;
	if (ret->sampleSrc.flags & GaDataAccessFlag_Seekable) {
		ret->sampleSrc.seek = &gauX_sample_source_stream_seek;
		ret->sampleSrc.tell = &gauX_sample_source_stream_tell;
	}
	ret->sampleSrc.close = &gauX_sample_source_stream_close;
	return (GaSampleSource*)ret;
}

/* Loop Sample Source */
typedef struct GauSampleSourceLoopContext {
	GaSampleSource* innerSrc;
	gc_int32 triggerSample;
	gc_int32 targetSample;
	GaMutex* loopMutex;
	gc_int32 sample_size;
	volatile gc_int32 loopCount;
} GauSampleSourceLoopContext;

struct GauSampleSourceLoop {
  GaSampleSource sampleSrc;
  GauSampleSourceLoopContext context;
};

gc_size gauX_sample_source_loop_read(void *context, void *dst, gc_size num_samples,
		GaCbOnSeek onseek, void *seek_ctx) {
	GauSampleSourceLoopContext* ctx = &((GauSampleSourceLoop*)context)->context;
	gc_int32 numRead = 0;
	gc_int32 triggerSample, targetSample;
	gc_size pos, total;
	gc_uint32 sample_size;
	gc_int32 totalRead = 0;
	GaSampleSource* ss = ctx->innerSrc;
	ga_mutex_lock(ctx->loopMutex);
	triggerSample = ctx->triggerSample;
	targetSample = ctx->targetSample;
	ga_mutex_unlock(ctx->loopMutex);
	ga_sample_source_tell(ss, &pos, &total); //todo check retval
	if ((targetSample < 0 && triggerSample <= 0)) return ga_sample_source_read(ss, dst, num_samples, 0, 0);
	if (triggerSample <= 0) triggerSample = total;
	if (pos > triggerSample) return ga_sample_source_read(ss, dst, num_samples, 0, 0);
	sample_size = ctx->sample_size;
	while (num_samples) {
		gc_int32 avail = triggerSample - pos;
		gc_bool doSeek = avail <= num_samples;
		gc_int32 toRead = doSeek ? avail : num_samples;
		numRead = ga_sample_source_read(ss, dst,  toRead, 0, 0);
		totalRead += numRead;
		num_samples -= numRead;
		dst = (char*)dst + numRead * sample_size;
		if (doSeek && toRead == numRead) {
			ga_sample_source_seek(ss, targetSample);
			++ctx->loopCount;
			if (onseek)
				onseek(totalRead, targetSample - triggerSample, seek_ctx);
		}
		ga_sample_source_tell(ss, &pos, &total); //todo check
	}
	return totalRead;
}
gc_bool gauX_sample_source_loop_end(void *context) {
	GauSampleSourceLoopContext *ctx = &((GauSampleSourceLoop*)context)->context;
	return ga_sample_source_end(ctx->innerSrc);
}
gc_bool gauX_sample_source_loop_ready(void *context, gc_size num_samples) {
	GauSampleSourceLoopContext *ctx = &((GauSampleSourceLoop*)context)->context;
	return ga_sample_source_ready(ctx->innerSrc, num_samples);
}
ga_result gauX_sample_source_loop_seek(void *context, gc_size sample_sffset) {
	GauSampleSourceLoopContext* ctx = &((GauSampleSourceLoop*)context)->context;
	return ga_sample_source_seek(ctx->innerSrc, sample_sffset);
}
ga_result gauX_sample_source_loop_tell(void *context, gc_size *samples, gc_size *total_samples) {
	GauSampleSourceLoopContext *ctx = &((GauSampleSourceLoop*)context)->context;
	return ga_sample_source_tell(ctx->innerSrc, samples, total_samples);
}
void gauX_sample_source_loop_close(void *context) {
	GauSampleSourceLoopContext* ctx = &((GauSampleSourceLoop*)context)->context;
	ga_sample_source_release(ctx->innerSrc);
	ga_mutex_destroy(ctx->loopMutex);
}
void gau_sample_source_loop_set(GauSampleSourceLoop *src, gc_int32 trigger_sample, gc_int32 target_sample) {
	GauSampleSourceLoopContext* ctx = &src->context;
	ga_mutex_lock(ctx->loopMutex);
	ctx->targetSample = target_sample;
	ctx->triggerSample = trigger_sample;
	ctx->loopCount = 0;
	ga_mutex_unlock(ctx->loopMutex);
}
gc_int32 gau_sample_source_loop_count(GauSampleSourceLoop* src) {
	return src->context.loopCount;
}
void gau_sample_source_loop_clear(GauSampleSourceLoop* src) {
	gau_sample_source_loop_set(src, -1, -1);
}
GauSampleSourceLoop* gau_sample_source_create_loop(GaSampleSource *src) {
	GauSampleSourceLoop* ret = gcX_ops->allocFunc(sizeof(GauSampleSourceLoop));
	GauSampleSourceLoopContext* ctx = &ret->context;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sample_source_acquire(src);
	ga_sample_source_format(src, &ret->sampleSrc.format);
	gc_uint32 sample_size = ga_format_sample_size(&ret->sampleSrc.format);
	ctx->triggerSample = -1;
	ctx->targetSample = -1;
	ctx->loopCount = 0;
	ctx->loopMutex = ga_mutex_create();
	ctx->innerSrc = src;
	ctx->sample_size = sample_size;
	ret->sampleSrc.flags = ga_sample_source_flags(src);
	ret->sampleSrc.flags |= GaDataAccessFlag_Threadsafe;
	assert(ret->sampleSrc.flags & GaDataAccessFlag_Seekable);
	ret->sampleSrc.read = &gauX_sample_source_loop_read;
	ret->sampleSrc.end = &gauX_sample_source_loop_end;
	ret->sampleSrc.ready = &gauX_sample_source_loop_ready;
	ret->sampleSrc.seek = &gauX_sample_source_loop_seek;
	ret->sampleSrc.tell = &gauX_sample_source_loop_tell;
	ret->sampleSrc.close = &gauX_sample_source_loop_close;
	return ret;
}

/* Sound Sample Source */
typedef struct gau_SampleSourceSoundContext {
	GaSound* sound;
	gc_uint32 sample_size;
	gc_size num_samples;
	GaMutex* posMutex;
	gc_atomic_size pos; /* Volatile, but shouldn't need a mutex around use */
} gau_SampleSourceSoundContext;

typedef struct gau_SampleSourceSound {
	GaSampleSource sampleSrc;
	gau_SampleSourceSoundContext context;
} gau_SampleSourceSound;

gc_size gauX_sample_source_sound_read(void *context, void *dst, gc_size num_samples,
                                       GaCbOnSeek onseek, void *seek_ctx) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	GaSound* snd = ctx->sound;
	char* src;
	gc_size pos, avail, num_read;
	ga_mutex_lock(ctx->posMutex);
	pos = ctx->pos;
	avail = ctx->num_samples - pos;
	num_read = min(avail, num_samples);
	ctx->pos += num_read;
	ga_mutex_unlock(ctx->posMutex);
	src = (char*)ga_sound_data(snd) + pos * ctx->sample_size;
	memcpy(dst, src, num_read * ctx->sample_size);
	return num_read;
}
gc_bool gauX_sample_source_sound_end(void *context) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	return ctx->pos >= ctx->num_samples;
}
ga_result gauX_sample_source_sound_seek(void *context, gc_size sample_offset) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	if(sample_offset > ctx->num_samples)
		return GA_ERR_GENERIC;
	ga_mutex_lock(ctx->posMutex);
	ctx->pos = sample_offset;
	ga_mutex_unlock(ctx->posMutex);
	return GA_OK;
}
ga_result gauX_sample_source_sound_tell(void *context, gc_size *pos, gc_size *total) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	if (pos) *pos = ctx->pos;
	if (total) *total = ctx->num_samples;
	return GA_OK;
}
void gauX_sample_source_sound_close(void *context) {
	gau_SampleSourceSoundContext *ctx = &((gau_SampleSourceSound*)context)->context;
	ga_sound_release(ctx->sound);
	ga_mutex_destroy(ctx->posMutex);
}
GaSampleSource *gau_sample_source_create_sound(GaSound *sound) {
	gau_SampleSourceSound* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceSound));
	gau_SampleSourceSoundContext* ctx = &ret->context;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sound_acquire(sound);
	ga_sound_format(sound, &ret->sampleSrc.format);
	ctx->posMutex = ga_mutex_create();
	ctx->sound = sound;
	ctx->sample_size = ga_format_sample_size(&ret->sampleSrc.format);
	ctx->num_samples = ga_sound_num_samples(sound);
	ctx->pos = 0;
	ret->sampleSrc.flags = GaDataAccessFlag_Seekable | GaDataAccessFlag_Threadsafe;
	ret->sampleSrc.read = &gauX_sample_source_sound_read;
	ret->sampleSrc.end = &gauX_sample_source_sound_end;
	ret->sampleSrc.seek = &gauX_sample_source_sound_seek;
	ret->sampleSrc.tell = &gauX_sample_source_sound_tell;
	ret->sampleSrc.close = &gauX_sample_source_sound_close;
	return (GaSampleSource*)ret;
}

GaMemory *gau_load_memory_file(const char *fname) {
	GaDataSource *datasrc = gau_data_source_create_file(fname);
	if (!datasrc) return NULL;
	GaMemory *ret = ga_memory_create_data_source(datasrc);
	ga_data_source_release(datasrc);
	return ret;
}

GaSound *gau_load_sound_file(const char *fname, GauAudioType format) {
	GaSound *ret = NULL;
	GaDataSource *data = gau_data_source_create_file(fname);
	if (!data) return NULL;
	GaSampleSource *sample_src = NULL;
	if (format == GauAudioType_Ogg)
		sample_src = gau_sample_source_create_ogg(data);
	else if (format == GauAudioType_Wav)
		sample_src = gau_sample_source_create_wav(data);
	ga_data_source_release(data);
	if (sample_src) {
		ret = ga_sound_create_sample_source(sample_src);
		ga_sample_source_release(sample_src);
	}
	return ret;
}
GaHandle* gau_create_handle_sound(GaMixer *mixer, GaSound *sound,
                                   ga_FinishCallback callback, void *context,
				   GauSampleSourceLoop **loop_src) {
	GaHandle *ret = NULL;
	GaSampleSource *src = gau_sample_source_create_sound(sound);
	if (!src) return NULL;
	GaSampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (GaSampleSource*)*loop_src;
	}
	if (src2) {
		ret = ga_handle_create(mixer, src2);
		if(src == src2)
			ga_sample_source_release(src2);
		ga_handle_setCallback(ret, callback, context);
	}
	return ret;
}

GaHandle* gau_create_handle_memory(GaMixer *mixer, GaMemory *memory, GauAudioType format,
                                    ga_FinishCallback callback, void *context,
                                    GauSampleSourceLoop **loop_src) {
	GaHandle *ret = NULL;
	GaDataSource *data = gau_data_source_create_memory(memory);
	if (!data) return NULL;
	GaSampleSource *src = NULL;
	if (format == GauAudioType_Ogg)
		src = gau_sample_source_create_ogg(data);
	else if(format == GauAudioType_Wav)
		src = gau_sample_source_create_wav(data);
	ga_data_source_release(data);
	if (!src) return NULL;

	GaSampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (GaSampleSource*)*loop_src;
	}
	if (src2) {
		ret = ga_handle_create(mixer, src2);
		if(src == src2)
			ga_sample_source_release(src2);
		ga_handle_setCallback(ret, callback, context);
	}
	return ret;
}
GaHandle *gau_create_handle_buffered_data(GaMixer *mixer, GaStreamManager *streamMgr,
                                           GaDataSource *data, GauAudioType format,
                                           ga_FinishCallback callback, void *context,
                                           GauSampleSourceLoop **loop_src) {
	if (!data) return NULL;
	GaHandle* ret = NULL;

	GaSampleSource *src = NULL;
	if (format == GauAudioType_Ogg)
		src = gau_sample_source_create_ogg(data);
	else if (format == GauAudioType_Wav)
		src = gau_sample_source_create_wav(data);
	if (!src) return NULL;

	GaSampleSource* src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (GaSampleSource*)*loop_src;
	}
	if (src2) {
		GaSampleSource *streamSampleSrc = gau_sample_source_create_stream(streamMgr, src2, 131072);
		if(src == src2) ga_sample_source_release(src2);
		if (streamSampleSrc) {
			ret = ga_handle_create(mixer, streamSampleSrc);
			ga_sample_source_release(streamSampleSrc);
			ga_handle_setCallback(ret, callback, context);
		}
	}
	return ret;
}
GaHandle* gau_create_handle_buffered_file(GaMixer* mixer, GaStreamManager* streamMgr,
                                           const char* filename, GauAudioType format,
                                           ga_FinishCallback callback, void* context,
                                           GauSampleSourceLoop** loop_src) {
	GaDataSource *data = gau_data_source_create_file(filename);
	if (!data) return NULL;
	GaHandle* ret = NULL;
	GaSampleSource *src = NULL;
	if(format == GauAudioType_Ogg)
		src = gau_sample_source_create_ogg(data);
	else if(format == GauAudioType_Wav)
		src = gau_sample_source_create_wav(data);
	ga_data_source_release(data);
	if (!src) return NULL;
	GaSampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (GaSampleSource*)*loop_src;
	}
	if (src2) {
		GaSampleSource* streamSampleSrc = gau_sample_source_create_stream(streamMgr, src2, 131072);
		if(src == src2)
			ga_sample_source_release(src2);
		if (streamSampleSrc) {
			ret = ga_handle_create(mixer, streamSampleSrc);
			ga_sample_source_release(streamSampleSrc);
			ga_handle_setCallback(ret, callback, context);
		}
	}
	return ret;
}
