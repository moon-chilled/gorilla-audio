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
	gc_int32 sampleSize;
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
	return gau_manager_create_custom(&(GaDeviceType){GaDeviceType_Default}, GauThreadPolicy_Single, &(gc_int32){4}, &(gc_int32){512});
}
gau_Manager *gau_manager_create_custom(GaDeviceType *dev_type,
                                       GauThreadPolicy thread_policy,
                                       gc_int32 *num_buffers,
				       gc_int32 *num_samples) {
	gau_Manager* ret = memset(gcX_ops->allocFunc(sizeof(gau_Manager)), 0, sizeof(gau_Manager));

	assert(thread_policy == GauThreadPolicy_Single
	       || thread_policy == GauThreadPolicy_Multi);
	assert(!num_buffers || *num_buffers >= 2);
	assert(!num_samples || *num_samples > 128);

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
	ret->sampleSize = ga_format_sampleSize(&ret->format);
	ret->mixBuffer = (gc_int16*)gcX_ops->allocFunc(ret->mixer->numSamples * ret->sampleSize);
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
void gau_manager_update(gau_Manager* in_mgr)
{
  if(in_mgr->threadPolicy == GauThreadPolicy_Single)
  {
    gc_int16* buf = in_mgr->mixBuffer;
    ga_Mixer* mixer = in_mgr->mixer;
    ga_Device* dev = in_mgr->device;
    gc_int32 numToQueue = ga_device_check(dev);
    while(numToQueue--)
    {
      ga_mixer_mix(mixer, buf);
      ga_device_queue(dev, buf);
    }
    ga_stream_manager_buffer(in_mgr->streamMgr);
  }
  ga_mixer_dispatch(in_mgr->mixer);
}
ga_Mixer* gau_manager_mixer(gau_Manager* in_mgr)
{
  return in_mgr->mixer;
}
ga_StreamManager* gau_manager_streamManager(gau_Manager* in_mgr)
{
  return in_mgr->streamMgr;
}
ga_Device* gau_manager_device(gau_Manager* in_mgr)
{
  return in_mgr->device;
}
void gau_manager_destroy(gau_Manager* in_mgr)
{
  if(in_mgr->threadPolicy == GauThreadPolicy_Multi)
  {
    in_mgr->killThreads = 1;
    gc_thread_join(in_mgr->streamThread);
    gc_thread_join(in_mgr->mixThread);
    gc_thread_destroy(in_mgr->streamThread);
    gc_thread_destroy(in_mgr->mixThread);
  }

  /* Clean up mixer and stream manager */
  ga_stream_manager_destroy(in_mgr->streamMgr);
  ga_mixer_destroy(in_mgr->mixer);
  gcX_ops->freeFunc(in_mgr->mixBuffer);
  ga_device_close(in_mgr->device);
  gcX_ops->freeFunc(in_mgr);
}

/* On-Finish Callbacks */
void gau_on_finish_destroy(ga_Handle* in_finishedHandle, void* in_context)
{
  ga_handle_destroy(in_finishedHandle);
}

/* File-Based Data Source */
typedef struct gau_DataSourceFileContext {
	FILE* f;
	gc_Mutex* fileMutex;
} gau_DataSourceFileContext;

typedef struct gau_DataSourceFile {
	ga_DataSource dataSrc;
	gau_DataSourceFileContext context;
} gau_DataSourceFile;

static gc_size gauX_data_source_file_read(void* in_context, void* in_dst, gc_size in_size, gc_size in_count) {
	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)in_context;
	gc_size ret;
	gc_mutex_lock(ctx->fileMutex);
	ret = fread(in_dst, in_size, in_count, ctx->f);
	gc_mutex_unlock(ctx->fileMutex);
	return ret;
}
static gc_result gauX_data_source_file_seek(void *in_context, gc_ssize offset, GaSeekOrigin whence) {
	int fwhence;
	switch(whence) {
		case GaSeekOrigin_Set: fwhence = SEEK_SET; break;
		case GaSeekOrigin_Cur: fwhence = SEEK_CUR; break;
		case GaSeekOrigin_End: fwhence = SEEK_END; break;
		default: return GC_ERROR_GENERIC;
	}

	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)in_context;
	gc_mutex_lock(ctx->fileMutex);
	gc_result ret = fseek(ctx->f, offset, fwhence) == -1 ? GC_ERROR_GENERIC : GC_SUCCESS;
	gc_mutex_unlock(ctx->fileMutex);

	return ret;
}
static gc_size gauX_data_source_file_tell(void* in_context) {
	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)in_context;
	gc_mutex_lock(ctx->fileMutex);
	gc_size ret = ftell(ctx->f);
	gc_mutex_unlock(ctx->fileMutex);
	return ret;
}
static void gauX_data_source_file_close(void* in_context) {
	gau_DataSourceFileContext* ctx = (gau_DataSourceFileContext*)in_context;
	fclose(ctx->f);
	gc_mutex_destroy(ctx->fileMutex);
}
static ga_DataSource *gauX_data_source_create_fp(FILE *fp) {
	if (!fp) return NULL;

	rewind(fp);

	gau_DataSourceFile* ret = gcX_ops->allocFunc(sizeof(gau_DataSourceFile));
	ga_data_source_init(&ret->dataSrc);
	ret->dataSrc.flags = GaDataAccessFlag_Seekable | GaDataAccessFlag_Threadsafe;
	ret->dataSrc.readFunc = &gauX_data_source_file_read;
	ret->dataSrc.seekFunc = &gauX_data_source_file_seek;
	ret->dataSrc.tellFunc = &gauX_data_source_file_tell;
	ret->dataSrc.closeFunc = &gauX_data_source_file_close;
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
	ret->dataSrc.readFunc = &gauX_data_source_memory_read;
	ret->dataSrc.seekFunc = &gauX_data_source_memory_seek;
	ret->dataSrc.tellFunc = &gauX_data_source_memory_tell;
	ret->dataSrc.closeFunc = &gauX_data_source_memory_close;
	ga_memory_acquire(memory);
	ret->context.memory = memory;
	ret->context.pos = 0;
	ret->context.mutex = gc_mutex_create();
	return (ga_DataSource*)ret;
}

/* Stream Sample Source */
typedef struct gau_SampleSourceStreamContext {
	ga_BufferedStream* stream;
} gau_SampleSourceStreamContext;

typedef struct gau_SampleSourceStream {
	ga_SampleSource sampleSrc;
	gau_SampleSourceStreamContext context;
} gau_SampleSourceStream;

gc_size gauX_sample_source_stream_read(void* in_context, void* in_dst, gc_size in_numSamples,
                                        tOnSeekFunc in_onSeekFunc, void* in_seekContext) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)in_context)->context;
	return ga_stream_read(ctx->stream, in_dst, in_numSamples);
}
gc_bool gauX_sample_source_stream_end(void* in_context) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)in_context)->context;
	return ga_stream_end(ctx->stream);
}
gc_bool gauX_sample_source_stream_ready(void* in_context, gc_size in_numSamples) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)in_context)->context;
	return ga_stream_ready(ctx->stream, in_numSamples);
}
gc_result gauX_sample_source_stream_seek(void* in_context, gc_size in_sampleOffset) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)in_context)->context;
	return ga_stream_seek(ctx->stream, in_sampleOffset);
}
gc_result gauX_sample_source_stream_tell(void* in_context, gc_size *samples, gc_size *totalSamples) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)in_context)->context;
	return ga_stream_tell(ctx->stream, samples, totalSamples);
}
void gauX_sample_source_stream_close(void* in_context) {
	gau_SampleSourceStreamContext* ctx = &((gau_SampleSourceStream*)in_context)->context;
	ga_stream_release(ctx->stream);
}
ga_SampleSource* gau_sample_source_create_stream(ga_StreamManager* in_mgr, ga_SampleSource* in_sampleSrc, gc_size in_bufferSamples) {
	gau_SampleSourceStream* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceStream));
	gau_SampleSourceStreamContext* ctx = &ret->context;
	gc_int32 sampleSize;
	ga_BufferedStream* stream;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sample_source_format(in_sampleSrc, &ret->sampleSrc.format);
	sampleSize = ga_format_sampleSize(&ret->sampleSrc.format);
	stream = ga_stream_create(in_mgr, in_sampleSrc, in_bufferSamples * sampleSize);
	if (stream) {
		ctx->stream = stream;
		ret->sampleSrc.flags = ga_stream_flags(stream);
		ret->sampleSrc.flags |= GaDataAccessFlag_Threadsafe;
		ret->sampleSrc.readFunc = &gauX_sample_source_stream_read;
		ret->sampleSrc.endFunc = &gauX_sample_source_stream_end;
		ret->sampleSrc.readyFunc = &gauX_sample_source_stream_ready;
		if (ret->sampleSrc.flags & GaDataAccessFlag_Seekable) {
			ret->sampleSrc.seekFunc = &gauX_sample_source_stream_seek;
			ret->sampleSrc.tellFunc = &gauX_sample_source_stream_tell;
		}
		ret->sampleSrc.closeFunc = &gauX_sample_source_stream_close;
	} else {
		gcX_ops->freeFunc(ret);
		ret = NULL;
	}
	return (ga_SampleSource*)ret;
}

/* Loop Sample Source */
typedef struct gau_SampleSourceLoopContext {
  ga_SampleSource* innerSrc;
  gc_int32 triggerSample;
  gc_int32 targetSample;
  gc_Mutex* loopMutex;
  gc_int32 sampleSize;
  volatile gc_int32 loopCount;
} gau_SampleSourceLoopContext;

struct gau_SampleSourceLoop {
  ga_SampleSource sampleSrc;
  gau_SampleSourceLoopContext context;
};

gc_size gauX_sample_source_loop_read(void* in_context, void* in_dst, gc_size in_numSamples,
		tOnSeekFunc in_onSeekFunc, void* in_seekContext) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)in_context)->context;
	gc_int32 numRead = 0;
	gc_int32 triggerSample, targetSample;
	gc_size pos, total;
	gc_int32 sampleSize;
	gc_int32 totalRead = 0;
	ga_SampleSource* ss = ctx->innerSrc;
	gc_mutex_lock(ctx->loopMutex);
	triggerSample = ctx->triggerSample;
	targetSample = ctx->targetSample;
	gc_mutex_unlock(ctx->loopMutex);
	ga_sample_source_tell(ss, &pos, &total); //todo check retval
	if((targetSample < 0 && triggerSample <= 0))
		return ga_sample_source_read(ss, in_dst, in_numSamples, 0, 0);
	if(triggerSample <= 0)
		triggerSample = total;
	if(pos > triggerSample)
		return ga_sample_source_read(ss, in_dst, in_numSamples, 0, 0);
	sampleSize = ctx->sampleSize;
	while(in_numSamples)
	{
		gc_int32 avail = triggerSample - pos;
		gc_bool doSeek = avail <= in_numSamples;
		gc_int32 toRead = doSeek ? avail : in_numSamples;
		numRead = ga_sample_source_read(ss, in_dst,  toRead, 0, 0);
		totalRead += numRead;
		in_numSamples -= numRead;
		in_dst = (char*)in_dst + numRead * sampleSize;
		if(doSeek && toRead == numRead)
		{
			ga_sample_source_seek(ss, targetSample);
			++ctx->loopCount;
			if(in_onSeekFunc)
				in_onSeekFunc(totalRead, targetSample - triggerSample, in_seekContext);
		}
		ga_sample_source_tell(ss, &pos, &total); //todo check
	}
	return totalRead;
}
gc_bool gauX_sample_source_loop_end(void* in_context) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)in_context)->context;
	return ga_sample_source_end(ctx->innerSrc);
}
gc_bool gauX_sample_source_loop_ready(void* in_context, gc_size in_numSamples) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)in_context)->context;
	return ga_sample_source_ready(ctx->innerSrc, in_numSamples);
}
gc_result gauX_sample_source_loop_seek(void* in_context, gc_size in_sampleOffset) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)in_context)->context;
	return ga_sample_source_seek(ctx->innerSrc, in_sampleOffset);
}
gc_result gauX_sample_source_loop_tell(void* in_context, gc_size *samples, gc_size *totalSamples) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)in_context)->context;
	return ga_sample_source_tell(ctx->innerSrc, samples, totalSamples);
}
void gauX_sample_source_loop_close(void* in_context) {
	gau_SampleSourceLoopContext* ctx = &((gau_SampleSourceLoop*)in_context)->context;
	ga_sample_source_release(ctx->innerSrc);
	gc_mutex_destroy(ctx->loopMutex);
}
void gau_sample_source_loop_set(gau_SampleSourceLoop* in_sampleSrc, gc_int32 in_triggerSample, gc_int32 in_targetSample) {
	gau_SampleSourceLoopContext* ctx = &in_sampleSrc->context;
	gc_mutex_lock(ctx->loopMutex);
	ctx->targetSample = in_targetSample;
	ctx->triggerSample = in_triggerSample;
	ctx->loopCount = 0;
	gc_mutex_unlock(ctx->loopMutex);
}
gc_int32 gau_sample_source_loop_count(gau_SampleSourceLoop* in_sampleSrc) {
	gau_SampleSourceLoopContext* ctx = &in_sampleSrc->context;
	return ctx->loopCount;
}
void gau_sample_source_loop_clear(gau_SampleSourceLoop* in_sampleSrc) {
	gau_sample_source_loop_set(in_sampleSrc, -1, -1);
}
gau_SampleSourceLoop* gau_sample_source_create_loop(ga_SampleSource* in_sampleSrc) {
	gau_SampleSourceLoop* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceLoop));
	gau_SampleSourceLoopContext* ctx = &ret->context;
	gc_int32 sampleSize;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sample_source_acquire(in_sampleSrc);
	ga_sample_source_format(in_sampleSrc, &ret->sampleSrc.format);
	sampleSize = ga_format_sampleSize(&ret->sampleSrc.format);
	ctx->triggerSample = -1;
	ctx->targetSample = -1;
	ctx->loopCount = 0;
	ctx->loopMutex = gc_mutex_create();
	ctx->innerSrc = in_sampleSrc;
	ctx->sampleSize = sampleSize;
	ret->sampleSrc.flags = ga_sample_source_flags(in_sampleSrc);
	ret->sampleSrc.flags |= GaDataAccessFlag_Threadsafe;
	assert(ret->sampleSrc.flags & GaDataAccessFlag_Seekable);
	ret->sampleSrc.readFunc = &gauX_sample_source_loop_read;
	ret->sampleSrc.endFunc = &gauX_sample_source_loop_end;
	ret->sampleSrc.readyFunc = &gauX_sample_source_loop_ready;
	ret->sampleSrc.seekFunc = &gauX_sample_source_loop_seek;
	ret->sampleSrc.tellFunc = &gauX_sample_source_loop_tell;
	ret->sampleSrc.closeFunc = &gauX_sample_source_loop_close;
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

gc_size gauX_sample_source_sound_read(void* context, void* in_dst, gc_size num_samples,
                                       tOnSeekFunc in_onSeekFunc, void* in_seekContext) {
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
	memcpy(in_dst, src, num_read * ctx->sample_size);
	return num_read;
}
gc_bool gauX_sample_source_sound_end(void* context) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	return ctx->pos >= ctx->num_samples;
}
gc_result gauX_sample_source_sound_seek(void* context, gc_size sample_offset) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	if(sample_offset > ctx->num_samples)
		return GC_ERROR_GENERIC;
	gc_mutex_lock(ctx->posMutex);
	ctx->pos = sample_offset;
	gc_mutex_unlock(ctx->posMutex);
	return GC_SUCCESS;
}
gc_result gauX_sample_source_sound_tell(void* context, gc_size *pos, gc_size *total) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	if (pos) *pos = ctx->pos;
	if (total) *total = ctx->num_samples;
	return GC_SUCCESS;
}
void gauX_sample_source_sound_close(void* context) {
	gau_SampleSourceSoundContext* ctx = &((gau_SampleSourceSound*)context)->context;
	ga_sound_release(ctx->sound);
	gc_mutex_destroy(ctx->posMutex);
}
ga_SampleSource* gau_sample_source_create_sound(ga_Sound* in_sound) {
	gau_SampleSourceSound* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceSound));
	gau_SampleSourceSoundContext* ctx = &ret->context;
	ga_sample_source_init(&ret->sampleSrc);
	ga_sound_acquire(in_sound);
	ga_sound_format(in_sound, &ret->sampleSrc.format);
	ctx->posMutex = gc_mutex_create();
	ctx->sound = in_sound;
	ctx->sample_size = ga_format_sampleSize(&ret->sampleSrc.format);
	ctx->num_samples = ga_sound_numSamples(in_sound);
	ctx->pos = 0;
	ret->sampleSrc.flags = GaDataAccessFlag_Seekable | GaDataAccessFlag_Threadsafe;
	ret->sampleSrc.readFunc = &gauX_sample_source_sound_read;
	ret->sampleSrc.endFunc = &gauX_sample_source_sound_end;
	ret->sampleSrc.seekFunc = &gauX_sample_source_sound_seek;
	ret->sampleSrc.tellFunc = &gauX_sample_source_sound_tell;
	ret->sampleSrc.closeFunc = &gauX_sample_source_sound_close;
	return (ga_SampleSource*)ret;
}

ga_Memory* gau_load_memory_file(const char* in_filename) {
  ga_Memory* ret;
  ga_DataSource* fileDataSrc = gau_data_source_create_file(in_filename);
  ret = ga_memory_create_data_source(fileDataSrc);
  ga_data_source_release(fileDataSrc);
  return ret;
}

ga_Sound* gau_load_sound_file(const char* in_filename, gau_AudioType in_format)
{
  ga_Sound* ret = 0;
  ga_DataSource* dataSrc = gau_data_source_create_file(in_filename);
  if(dataSrc)
  {
    ga_SampleSource* sampleSrc = 0;
    if(in_format == GAU_AUDIO_TYPE_OGG)
      sampleSrc = gau_sample_source_create_ogg(dataSrc);
    else if(in_format == GAU_AUDIO_TYPE_WAV)
      sampleSrc = gau_sample_source_create_wav(dataSrc);
    ga_data_source_release(dataSrc);
    if(sampleSrc)
    {
      ret = ga_sound_create_sample_source(sampleSrc);
      ga_sample_source_release(sampleSrc);
    }
  }
  return ret;
}
ga_Handle* gau_create_handle_sound(ga_Mixer* in_mixer, ga_Sound* in_sound,
                                   ga_FinishCallback in_callback, void* in_context,
                                   gau_SampleSourceLoop** out_loopSrc)
{
  ga_Handle* ret = 0;
  ga_SampleSource* sampleSrc = sampleSrc = gau_sample_source_create_sound(in_sound);
  if(sampleSrc)
  {
    ga_SampleSource* sampleSrc2 = sampleSrc;
    if(out_loopSrc)
    {
      gau_SampleSourceLoop* loopSampleSrc = gau_sample_source_create_loop(sampleSrc);
      gau_sample_source_loop_set(loopSampleSrc, -1, 0);
      ga_sample_source_release(sampleSrc);
      *out_loopSrc = loopSampleSrc;
      sampleSrc2 = (ga_SampleSource*)loopSampleSrc;
    }
    if(sampleSrc2)
    {
      ret = ga_handle_create(in_mixer, sampleSrc2);
      if(sampleSrc == sampleSrc2)
        ga_sample_source_release(sampleSrc2);
      ga_handle_setCallback(ret, in_callback, in_context);
    }
  }
  return ret;
}

ga_Handle* gau_create_handle_memory(ga_Mixer* in_mixer, ga_Memory* in_memory, gau_AudioType in_format,
                                    ga_FinishCallback in_callback, void* in_context,
                                    gau_SampleSourceLoop** out_loopSrc)
{
  ga_Handle* ret = 0;
  ga_DataSource* dataSrc = gau_data_source_create_memory(in_memory);
  if(dataSrc)
  {
    ga_SampleSource* sampleSrc = 0;
    if(in_format == GAU_AUDIO_TYPE_OGG)
      sampleSrc = gau_sample_source_create_ogg(dataSrc);
    else if(in_format == GAU_AUDIO_TYPE_WAV)
      sampleSrc = gau_sample_source_create_wav(dataSrc);
    if(sampleSrc)
    {
      ga_SampleSource* sampleSrc2 = sampleSrc;
      if(out_loopSrc)
      {
        gau_SampleSourceLoop* loopSampleSrc = gau_sample_source_create_loop(sampleSrc);
        gau_sample_source_loop_set(loopSampleSrc, -1, 0);
        ga_sample_source_release(sampleSrc);
        *out_loopSrc = loopSampleSrc;
        sampleSrc2 = (ga_SampleSource*)loopSampleSrc;
      }
      if(sampleSrc2)
      {
        ret = ga_handle_create(in_mixer, sampleSrc2);
        if(sampleSrc == sampleSrc2)
          ga_sample_source_release(sampleSrc2);
        ga_handle_setCallback(ret, in_callback, in_context);
      }
    }
  }
  return ret;
}
ga_Handle* gau_create_handle_buffered_data(ga_Mixer* in_mixer, ga_StreamManager* in_streamMgr,
                                           ga_DataSource* in_dataSrc, gau_AudioType in_format,
                                           ga_FinishCallback in_callback, void* in_context,
                                           gau_SampleSourceLoop** out_loopSrc)
{
  ga_Handle* ret = 0;
  ga_DataSource* dataSrc = in_dataSrc;
  if(in_dataSrc)
  {
    ga_SampleSource* sampleSrc = 0;
    if(in_format == GAU_AUDIO_TYPE_OGG)
      sampleSrc = gau_sample_source_create_ogg(dataSrc);
    else if(in_format == GAU_AUDIO_TYPE_WAV)
      sampleSrc = gau_sample_source_create_wav(dataSrc);
    if(sampleSrc)
    {
      ga_SampleSource* sampleSrc2 = sampleSrc;
      if(out_loopSrc)
      {
        gau_SampleSourceLoop* loopSampleSrc = gau_sample_source_create_loop(sampleSrc);
        gau_sample_source_loop_set(loopSampleSrc, -1, 0);
        ga_sample_source_release(sampleSrc);
        *out_loopSrc = loopSampleSrc;
        sampleSrc2 = (ga_SampleSource*)loopSampleSrc;
      }
      if(sampleSrc2)
      {
        ga_SampleSource* streamSampleSrc = gau_sample_source_create_stream(in_streamMgr,
          sampleSrc2,
          131072);
        if(sampleSrc == sampleSrc2)
          ga_sample_source_release(sampleSrc2);
        if(streamSampleSrc)
        {
          ret = ga_handle_create(in_mixer, streamSampleSrc);
          ga_sample_source_release(streamSampleSrc);
          ga_handle_setCallback(ret, in_callback, in_context);
        }
      }
    }
  }
  return ret;
}
ga_Handle* gau_create_handle_buffered_file(ga_Mixer* in_mixer, ga_StreamManager* in_streamMgr,
                                           const char* in_filename, gau_AudioType in_format,
                                           ga_FinishCallback in_callback, void* in_context,
                                           gau_SampleSourceLoop** out_loopSrc)
{
  ga_Handle* ret = 0;
  ga_DataSource* dataSrc = gau_data_source_create_file(in_filename);
  if(dataSrc)
  {
    ga_SampleSource* sampleSrc = 0;
    if(in_format == GAU_AUDIO_TYPE_OGG)
      sampleSrc = gau_sample_source_create_ogg(dataSrc);
    else if(in_format == GAU_AUDIO_TYPE_WAV)
      sampleSrc = gau_sample_source_create_wav(dataSrc);
    ga_data_source_release(dataSrc);
    if(sampleSrc)
    {
      ga_SampleSource* sampleSrc2 = sampleSrc;
      if(out_loopSrc)
      {
        gau_SampleSourceLoop* loopSampleSrc = gau_sample_source_create_loop(sampleSrc);
        gau_sample_source_loop_set(loopSampleSrc, -1, 0);
        ga_sample_source_release(sampleSrc);
        *out_loopSrc = loopSampleSrc;
        sampleSrc2 = (ga_SampleSource*)loopSampleSrc;
      }
      if(sampleSrc2)
      {
        ga_SampleSource* streamSampleSrc = gau_sample_source_create_stream(in_streamMgr,
          sampleSrc2,
          131072);
        if(sampleSrc == sampleSrc2)
          ga_sample_source_release(sampleSrc2);
        if(streamSampleSrc)
        {
          ret = ga_handle_create(in_mixer, streamSampleSrc);
          ga_sample_source_release(streamSampleSrc);
          ga_handle_setCallback(ret, in_callback, in_context);
        }
      }
    }
  }
  return ret;
}
