#include "gorilla/ga.h"
#include "gorilla/gau.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

/* High-Level Manager */
struct gau_Manager {
	GauThreadPolicy threadPolicy;
	gc_Thread* mixThread;
	gc_Thread* streamThread;
	ga_Device* device;
	ga_Mixer* mixer;
	ga_StreamManager* streamMgr;
	gc_int32 sample_size;
	gc_int16* mixBuffer;
	ga_Format format;
	gc_int32 killThreads;
};

static gc_int32 gauX_mixThreadFunc(void *context) {
	gau_Manager* ctx = (gau_Manager*)context;
	while (!ctx->killThreads) {
		gc_int32 numToQueue = ga_device_check(ctx->device);
		while (numToQueue--) {
			ga_mixer_mix(ctx->mixer, ctx->mixBuffer);
			ga_device_queue(ctx->device, ctx->mixBuffer);
		}
		gc_thread_sleep(5);
	}
	return 0;
}
static gc_int32 gauX_streamThreadFunc(void *context) {
	gau_Manager* ctx = (gau_Manager*)context;
	while (!ctx->killThreads) {
		ga_stream_manager_buffer(ctx->streamMgr);
		gc_thread_sleep(50);
	}
	return 0;
}
gau_Manager *gau_manager_create(void) {
	return gau_manager_create_custom(NULL, GauThreadPolicy_Single, NULL, NULL);
}
gau_Manager *gau_manager_create_custom(GaDeviceType *dev_type,
                                       GauThreadPolicy thread_policy,
                                       gc_uint32 *num_buffers,
				       gc_uint32 *num_samples) {
	gau_Manager* ret = memset(gcX_ops->allocFunc(sizeof(gau_Manager)), 0, sizeof(gau_Manager));

	assert(thread_policy == GauThreadPolicy_Single
	       || thread_policy == GauThreadPolicy_Multi);
	if (!num_buffers) num_buffers = &(gc_uint32){4};
	if (!num_samples) num_samples = &(gc_uint32){512};
	assert(*num_buffers >= 2);
	assert(*num_samples >= 128);

	/* Open device */
	memset(&ret->format, 0, sizeof(ga_Format));
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
		ret->mixThread = gc_thread_create(gauX_mixThreadFunc, ret, GC_THREAD_PRIORITY_HIGHEST, 64 * 1024);
		ret->streamThread = gc_thread_create(gauX_streamThreadFunc, ret, GC_THREAD_PRIORITY_HIGHEST, 64 * 1024);
		gc_thread_run(ret->mixThread);
		gc_thread_run(ret->streamThread);
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
void gau_manager_update(gau_Manager *mgr) {
	if (mgr->threadPolicy == GauThreadPolicy_Single) {
		gc_int16* buf = mgr->mixBuffer;
		ga_Mixer* mixer = mgr->mixer;
		ga_Device* dev = mgr->device;
		gc_int32 numToQueue = ga_device_check(dev);
		while (numToQueue--) {
			ga_mixer_mix(mixer, buf);
			ga_device_queue(dev, buf);
		}
		ga_stream_manager_buffer(mgr->streamMgr);
	}
	ga_mixer_dispatch(mgr->mixer);
}
ga_Mixer *gau_manager_mixer(gau_Manager *mgr) {
	return mgr->mixer;
}
ga_StreamManager *gau_manager_stream_manager(gau_Manager* mgr) {
	return mgr->streamMgr;
}
ga_Device *gau_manager_device(gau_Manager *mgr) {
  return mgr->device;
}
void gau_manager_destroy(gau_Manager *mgr) {
	if (mgr->threadPolicy == GauThreadPolicy_Multi) {
		mgr->killThreads = 1;
		gc_thread_join(mgr->streamThread);
		gc_thread_join(mgr->mixThread);
		gc_thread_destroy(mgr->streamThread);
		gc_thread_destroy(mgr->mixThread);
	}

	/* Clean up mixer and stream manager */
	ga_stream_manager_destroy(mgr->streamMgr);
	ga_mixer_destroy(mgr->mixer);
	gcX_ops->freeFunc(mgr->mixBuffer);
	ga_device_close(mgr->device);
	gcX_ops->freeFunc(mgr);
}

/* On-Finish Callbacks */
void gau_on_finish_destroy(ga_Handle *handle, void *ctx) {
	ga_handle_destroy(handle);
}

/* File-Based Data Source */
typedef struct {
	FILE* f;
	gc_Mutex* fileMutex;
} gau_DataSourceFileContext;

typedef struct {
	ga_DataSource dataSrc;
	gau_DataSourceFileContext context;
} gau_DataSourceFile;

static gc_size gauX_data_source_file_read(void *context, void *dst, gc_size size, gc_size count) {
	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)context;
	gc_size ret;
	gc_mutex_lock(ctx->fileMutex);
	ret = fread(dst, size, count, ctx->f);
	gc_mutex_unlock(ctx->fileMutex);
	return ret;
}
static gc_result gauX_data_source_file_seek(void *context, gc_ssize offset, GaSeekOrigin whence) {
	int fwhence;
	switch (whence) {
		case GaSeekOrigin_Set: fwhence = SEEK_SET; break;
		case GaSeekOrigin_Cur: fwhence = SEEK_CUR; break;
		case GaSeekOrigin_End: fwhence = SEEK_END; break;
		default: return GC_ERROR_GENERIC;
	}

	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	gc_mutex_lock(ctx->fileMutex);
	gc_result ret = fseek(ctx->f, offset, fwhence) == -1 ? GC_ERROR_GENERIC : GC_SUCCESS;
	gc_mutex_unlock(ctx->fileMutex);

	return ret;
}
static gc_size gauX_data_source_file_tell(void *context) {
	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)context;
	gc_mutex_lock(ctx->fileMutex);
	gc_size ret = ftell(ctx->f);
	gc_mutex_unlock(ctx->fileMutex);
	return ret;
}
static void gauX_data_source_file_close(void *context) {
	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	fclose(ctx->f);
	gc_mutex_destroy(ctx->fileMutex);
}
static ga_DataSource *gauX_data_source_create_fp(FILE *fp) {
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
	ret->context.fileMutex = gc_mutex_create();
	return (ga_DataSource*)ret;
}

ga_DataSource *gau_data_source_create_file(const char *fname) {
	return gauX_data_source_create_fp(fopen(fname, "rb"));
}


/* Memory-Based Data Source */
typedef struct {
	ga_Memory* memory;
	gc_size pos;
	gc_Mutex* mutex;
} GauDataSourceMemoryContext;

typedef struct {
	ga_DataSource dataSrc;
	GauDataSourceMemoryContext context;
} GauDataSourceMemory;

gc_size gauX_data_source_memory_read(void *context, void *dst, gc_size size, gc_size count) {
	GauDataSourceMemoryContext *ctx = (GauDataSourceMemoryContext*)context;
	gc_size ret = 0;
	gc_size dataSize = ga_memory_size(ctx->memory);
	gc_size toRead = size * count;
	gc_size remaining;

	gc_mutex_lock(ctx->mutex);
	remaining = dataSize - ctx->pos;
	toRead = toRead < remaining ? toRead : remaining;
	toRead = toRead - (toRead % size);
	if (toRead) {
		memcpy(dst, (char*)ga_memory_data(ctx->memory) + ctx->pos, toRead);
		ctx->pos += toRead;
		ret = toRead / size;
	}
	gc_mutex_unlock(ctx->mutex);
	return ret;
}
gc_result gauX_data_source_memory_seek(void *context, gc_ssize offset, GaSeekOrigin whence) {
	GauDataSourceMemoryContext* ctx = (GauDataSourceMemoryContext*)context;
	gc_size data_size = ga_memory_size(ctx->memory);
	gc_ssize pos;
	gc_mutex_lock(ctx->mutex);
	switch (whence) {
		case GaSeekOrigin_Set: pos = offset; break;
		case GaSeekOrigin_Cur: pos = ctx->pos + offset; break;
		case GaSeekOrigin_End: pos = data_size - offset; break;
		default: goto fail;
	}
	if (pos < 0 || (gc_size)pos > data_size) goto fail;
	ctx->pos = pos;
	gc_mutex_unlock(ctx->mutex);
	return GC_SUCCESS;
fail:
	gc_mutex_unlock(ctx->mutex);
	return GC_ERROR_GENERIC;
}
gc_size gauX_data_source_memory_tell(void *context) {
	GauDataSourceMemoryContext* ctx = (GauDataSourceMemoryContext*)context;
	gc_mutex_lock(ctx->mutex);
	gc_size ret = ctx->pos;
	gc_mutex_unlock(ctx->mutex);
	return ret;
}
void gauX_data_source_memory_close(void *context) {
	GauDataSourceMemoryContext* ctx = (GauDataSourceMemoryContext*)context;
	ga_memory_release(ctx->memory);
	gc_mutex_destroy(ctx->mutex);
}
ga_DataSource *gau_data_source_create_memory(ga_Memory *memory) {
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
	ret->context.mutex = gc_mutex_create();
	return (ga_DataSource*)ret;
}

/* Stream Sample Source */
typedef struct {
	ga_BufferedStream* stream;
} gau_SampleSourceStreamContext;

typedef struct {
	ga_SampleSource sampleSrc;
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
gc_result gauX_sample_source_stream_seek(void *context, gc_size sample_offset) {
	gau_SampleSourceStreamContext *ctx = &((gau_SampleSourceStream*)context)->context;
	return ga_stream_seek(ctx->stream, sample_offset);
}
gc_result gauX_sample_source_stream_tell(void *context, gc_size *samples, gc_size *totalSamples) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)context)->context;
	return ga_stream_tell(ctx->stream, samples, totalSamples);
}
void gauX_sample_source_stream_close(void *context) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)context)->context;
	ga_stream_release(ctx->stream);
}
ga_SampleSource* gau_sample_source_create_stream(ga_StreamManager *mgr, ga_SampleSource* sample_src, gc_size buffer_samples) {
	gau_SampleSourceStream* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceStream));
	gau_SampleSourceStreamContext* ctx = &ret->context;
	ga_BufferedStream* stream;
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
	return (ga_SampleSource*)ret;
}

/* Loop Sample Source */
typedef struct gau_SampleSourceLoopContext {
	ga_SampleSource* innerSrc;
	gc_int32 triggerSample;
	gc_int32 targetSample;
	gc_Mutex* loopMutex;
	gc_int32 sample_size;
	volatile gc_int32 loopCount;
} gau_SampleSourceLoopContext;

struct gau_SampleSourceLoop {
  ga_SampleSource sampleSrc;
  gau_SampleSourceLoopContext context;
};

gc_size gauX_sample_source_loop_read(void *context, void *dst, gc_size num_samples,
		GaCbOnSeek onseek, void *seek_ctx) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)context)->context;
	gc_int32 numRead = 0;
	gc_int32 triggerSample, targetSample;
	gc_size pos, total;
	gc_uint32 sample_size;
	gc_int32 totalRead = 0;
	ga_SampleSource* ss = ctx->innerSrc;
	gc_mutex_lock(ctx->loopMutex);
	triggerSample = ctx->triggerSample;
	targetSample = ctx->targetSample;
	gc_mutex_unlock(ctx->loopMutex);
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
	gau_SampleSourceLoopContext *ctx = &((gau_SampleSourceLoop*)context)->context;
	return ga_sample_source_end(ctx->innerSrc);
}
gc_bool gauX_sample_source_loop_ready(void *context, gc_size num_samples) {
	gau_SampleSourceLoopContext *ctx = &((gau_SampleSourceLoop*)context)->context;
	return ga_sample_source_ready(ctx->innerSrc, num_samples);
}
gc_result gauX_sample_source_loop_seek(void *context, gc_size sample_sffset) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)context)->context;
	return ga_sample_source_seek(ctx->innerSrc, sample_sffset);
}
gc_result gauX_sample_source_loop_tell(void *context, gc_size *samples, gc_size *total_samples) {
	gau_SampleSourceLoopContext *ctx = &((gau_SampleSourceLoop*)context)->context;
	return ga_sample_source_tell(ctx->innerSrc, samples, total_samples);
}
void gauX_sample_source_loop_close(void *context) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)context)->context;
	ga_sample_source_release(ctx->innerSrc);
	gc_mutex_destroy(ctx->loopMutex);
}
void gau_sample_source_loop_set(gau_SampleSourceLoop *src, gc_int32 trigger_sample, gc_int32 target_sample) {
	gau_SampleSourceLoopContext* ctx = &src->context;
	gc_mutex_lock(ctx->loopMutex);
	ctx->targetSample = target_sample;
	ctx->triggerSample = trigger_sample;
	ctx->loopCount = 0;
	gc_mutex_unlock(ctx->loopMutex);
}
gc_int32 gau_sample_source_loop_count(gau_SampleSourceLoop* src) {
	return src->context.loopCount;
}
void gau_sample_source_loop_clear(gau_SampleSourceLoop* src) {
	gau_sample_source_loop_set(src, -1, -1);
}
gau_SampleSourceLoop* gau_sample_source_create_loop(ga_SampleSource *src) {
	gau_SampleSourceLoop* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceLoop));
	gau_SampleSourceLoopContext* ctx = &ret->context;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sample_source_acquire(src);
	ga_sample_source_format(src, &ret->sampleSrc.format);
	gc_uint32 sample_size = ga_format_sample_size(&ret->sampleSrc.format);
	ctx->triggerSample = -1;
	ctx->targetSample = -1;
	ctx->loopCount = 0;
	ctx->loopMutex = gc_mutex_create();
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
	ga_Sound* sound;
	gc_uint32 sample_size;
	gc_size num_samples;
	gc_Mutex* posMutex;
	gc_atomic_size pos; /* Volatile, but shouldn't need a mutex around use */
} gau_SampleSourceSoundContext;

typedef struct gau_SampleSourceSound {
	ga_SampleSource sampleSrc;
	gau_SampleSourceSoundContext context;
} gau_SampleSourceSound;

gc_size gauX_sample_source_sound_read(void *context, void *dst, gc_size num_samples,
                                       GaCbOnSeek onseek, void *seek_ctx) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	ga_Sound* snd = ctx->sound;
	char* src;
	gc_size pos, avail, num_read;
	gc_mutex_lock(ctx->posMutex);
	pos = ctx->pos;
	avail = ctx->num_samples - pos;
	num_read = min(avail, num_samples);
	ctx->pos += num_read;
	gc_mutex_unlock(ctx->posMutex);
	src = (char*)ga_sound_data(snd) + pos * ctx->sample_size;
	memcpy(dst, src, num_read * ctx->sample_size);
	return num_read;
}
gc_bool gauX_sample_source_sound_end(void *context) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	return ctx->pos >= ctx->num_samples;
}
gc_result gauX_sample_source_sound_seek(void *context, gc_size sample_offset) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	if(sample_offset > ctx->num_samples)
		return GC_ERROR_GENERIC;
	gc_mutex_lock(ctx->posMutex);
	ctx->pos = sample_offset;
	gc_mutex_unlock(ctx->posMutex);
	return GC_SUCCESS;
}
gc_result gauX_sample_source_sound_tell(void *context, gc_size *pos, gc_size *total) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	if (pos) *pos = ctx->pos;
	if (total) *total = ctx->num_samples;
	return GC_SUCCESS;
}
void gauX_sample_source_sound_close(void *context) {
	gau_SampleSourceSoundContext *ctx = &((gau_SampleSourceSound*)context)->context;
	ga_sound_release(ctx->sound);
	gc_mutex_destroy(ctx->posMutex);
}
ga_SampleSource *gau_sample_source_create_sound(ga_Sound *sound) {
	gau_SampleSourceSound* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceSound));
	gau_SampleSourceSoundContext* ctx = &ret->context;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sound_acquire(sound);
	ga_sound_format(sound, &ret->sampleSrc.format);
	ctx->posMutex = gc_mutex_create();
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
	return (ga_SampleSource*)ret;
}

ga_Memory *gau_load_memory_file(const char *fname) {
	ga_DataSource *datasrc = gau_data_source_create_file(fname);
	if (!datasrc) return NULL;
	ga_Memory *ret = ga_memory_create_data_source(datasrc);
	ga_data_source_release(datasrc);
	return ret;
}

ga_Sound *gau_load_sound_file(const char *fname, gau_AudioType format) {
	ga_Sound *ret = NULL;
	ga_DataSource *data = gau_data_source_create_file(fname);
	if (!data) return NULL;
	ga_SampleSource *sample_src = NULL;
	if (format == GAU_AUDIO_TYPE_OGG)
		sample_src = gau_sample_source_create_ogg(data);
	else if (format == GAU_AUDIO_TYPE_WAV)
		sample_src = gau_sample_source_create_wav(data);
	ga_data_source_release(data);
	if (sample_src) {
		ret = ga_sound_create_sample_source(sample_src);
		ga_sample_source_release(sample_src);
	}
	return ret;
}
ga_Handle* gau_create_handle_sound(ga_Mixer *mixer, ga_Sound *sound,
                                   ga_FinishCallback callback, void *context,
				   gau_SampleSourceLoop **loop_src) {
	ga_Handle *ret = NULL;
	ga_SampleSource *src = gau_sample_source_create_sound(sound);
	if (!src) return NULL;
	ga_SampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (ga_SampleSource*)*loop_src;
	}
	if (src2) {
		ret = ga_handle_create(mixer, src2);
		if(src == src2)
			ga_sample_source_release(src2);
		ga_handle_setCallback(ret, callback, context);
	}
	return ret;
}

ga_Handle* gau_create_handle_memory(ga_Mixer *mixer, ga_Memory *memory, gau_AudioType format,
                                    ga_FinishCallback callback, void *context,
                                    gau_SampleSourceLoop **loop_src) {
	ga_Handle *ret = NULL;
	ga_DataSource *data = gau_data_source_create_memory(memory);
	if (!data) return NULL;
	ga_SampleSource *src = NULL;
	if (format == GAU_AUDIO_TYPE_OGG)
		src = gau_sample_source_create_ogg(data);
	else if(format == GAU_AUDIO_TYPE_WAV)
		src = gau_sample_source_create_wav(data);
	ga_data_source_release(data);
	if (!src) return NULL;

	ga_SampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (ga_SampleSource*)*loop_src;
	}
	if (src2) {
		ret = ga_handle_create(mixer, src2);
		if(src == src2)
			ga_sample_source_release(src2);
		ga_handle_setCallback(ret, callback, context);
	}
	return ret;
}
ga_Handle *gau_create_handle_buffered_data(ga_Mixer *mixer, ga_StreamManager *streamMgr,
                                           ga_DataSource *data, gau_AudioType format,
                                           ga_FinishCallback callback, void *context,
                                           gau_SampleSourceLoop **loop_src) {
	if (!data) return NULL;
	ga_Handle* ret = NULL;

	ga_SampleSource *src = NULL;
	if (format == GAU_AUDIO_TYPE_OGG)
		src = gau_sample_source_create_ogg(data);
	else if (format == GAU_AUDIO_TYPE_WAV)
		src = gau_sample_source_create_wav(data);
	if (!src) return NULL;

	ga_SampleSource* src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (ga_SampleSource*)*loop_src;
	}
	if (src2) {
		ga_SampleSource *streamSampleSrc = gau_sample_source_create_stream(streamMgr, src2, 131072);
		if(src == src2) ga_sample_source_release(src2);
		if (streamSampleSrc) {
			ret = ga_handle_create(mixer, streamSampleSrc);
			ga_sample_source_release(streamSampleSrc);
			ga_handle_setCallback(ret, callback, context);
		}
	}
	return ret;
}
ga_Handle* gau_create_handle_buffered_file(ga_Mixer* mixer, ga_StreamManager* streamMgr,
                                           const char* filename, gau_AudioType format,
                                           ga_FinishCallback callback, void* context,
                                           gau_SampleSourceLoop** loop_src) {
	ga_DataSource *data = gau_data_source_create_file(filename);
	if (!data) return NULL;
	ga_Handle* ret = NULL;
	ga_SampleSource *src = NULL;
	if(format == GAU_AUDIO_TYPE_OGG)
		src = gau_sample_source_create_ogg(data);
	else if(format == GAU_AUDIO_TYPE_WAV)
		src = gau_sample_source_create_wav(data);
	ga_data_source_release(data);
	if (!src) return NULL;
	ga_SampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_set(*loop_src, -1, 0);
		ga_sample_source_release(src);
		src2 = (ga_SampleSource*)*loop_src;
	}
	if (src2) {
		ga_SampleSource* streamSampleSrc = gau_sample_source_create_stream(streamMgr, src2, 131072);
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
