#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Version Functions */
gc_int32 ga_version_check(gc_int32 major, gc_int32 minor, gc_int32 rev) {
	gc_int32 res = (major == GA_VERSION_MAJOR) ? major - GA_VERSION_MAJOR :
	               (minor == GA_VERSION_MINOR) ? minor - GA_VERSION_MINOR :
	               rev - GA_VERSION_REV;
	return res < 0 ? -1 :
	       res > 0 ?  1 : 0;
}

/* Format Functions */
gc_int32 ga_format_sampleSize(ga_Format *format) {
	return (format->bitsPerSample >> 3) * format->numChannels;
}

gc_float32 ga_format_toSeconds(ga_Format *format, gc_int32 samples) {
	return samples / (gc_float32)format->sampleRate;
}

gc_int32 ga_format_toSamples(ga_Format *format, gc_float32 seconds) {
	return seconds * format->sampleRate;
}

/* Device Functions */
ga_Device* ga_device_open(ga_DeviceType type,
                          gc_int32 num_buffers,
                          gc_int32 num_samples,
			  ga_Format *format) {
	// todo allow overriding with an environment variable
	// todo if type==ga_DeviceType_Default try multiple devicetypes if the first returns an error
	if (type == ga_DeviceType_Default) {
#if defined(ENABLE_OSS)
		type = ga_DeviceType_OSS;
#elif defined(ENABLE_XAUDIO2)
		type = ga_DeviceType_XAudio2;
#elif defined(ENABLE_PULSEAUDIO)
		type = ga_DeviceType_PulseAudio;
#elif defined(ENABLE_OPENAL)
		type = ga_DeviceType_OpenAL;    // generic (multiplatform) drivers go last
#else
		type = ga_DeviceType_Unknown;
#endif
	}

	ga_Device *ret = gcX_ops->allocFunc(sizeof(ga_Device));
	ret->dev_type = type;
	ret->num_buffers = num_buffers;
	ret->num_samples = num_samples;
	ret->format = *format;

	switch (type) {
#ifdef ENABLE_OSS
		case ga_DeviceType_OSS: ret->procs = gaX_deviceprocs_OSS; break;
#endif
#ifdef ENABLE_XAUDIO2
		case ga_DeviceType_XAudio2: ret->procs = gaX_deviceprocs_XAudio2; break;
#endif
#ifdef ENABLE_PULSEAUDIO
		case ga_DeviceType_PulseAudio: ret->procs = gaX_deviceprocs_PulseAudio; break;
#endif
#ifdef ENABLE_OPENAL
		case ga_DeviceType_OpenAL: ret->procs = gaX_deviceprocs_OpenAL; break;
#endif
		default: goto fail;
	}

	if (ret->procs.open(ret) != GC_SUCCESS) goto fail;
	return ret;

fail:
	gcX_ops->freeFunc(ret);
	return NULL;
}

gc_result ga_device_close(ga_Device *device) {
	return device->procs.close ? device->procs.close(device) : GC_ERROR_GENERIC;
}

gc_int32 ga_device_check(ga_Device *device) {
	return device->procs.check ? device->procs.check(device) : GC_ERROR_GENERIC;
}

gc_result ga_device_queue(ga_Device *device, void *buffer) {
	return device->procs.queue ? device->procs.queue(device, buffer) : GC_ERROR_GENERIC;
}

/* Data Source Structure */
void ga_data_source_init(ga_DataSource *dataSrc) {
	dataSrc->refCount = 1;
	dataSrc->readFunc = 0;
	dataSrc->seekFunc = 0;
	dataSrc->tellFunc = 0;
	dataSrc->closeFunc = 0;
	dataSrc->flags = 0;
	dataSrc->refMutex = gc_mutex_create();
}

gc_int32 ga_data_source_read(ga_DataSource *dataSrc, void *dst, gc_int32 size, gc_int32 count) {
	tDataSourceFunc_Read func = dataSrc->readFunc;
	char* context = (char*)dataSrc + sizeof(ga_DataSource);
	assert(func);
	return func(context, dst, size, count);
}

gc_result ga_data_source_seek(ga_DataSource *dataSrc, gc_int32 offset, gc_int32 origin) {
	tDataSourceFunc_Seek func = dataSrc->seekFunc;
	char* context = (char*)dataSrc + sizeof(ga_DataSource);
	if (func) return func(context, offset, origin);
	else return GC_ERROR_GENERIC;
}

gc_int32 ga_data_source_tell(ga_DataSource *dataSrc) {
	tDataSourceFunc_Tell func = dataSrc->tellFunc;
	char* context = (char*)dataSrc + sizeof(ga_DataSource);
	if (func) return func(context);
	return -1;
}

gc_int32 ga_data_source_flags(ga_DataSource *dataSrc) {
	return dataSrc->flags;
}

void gaX_data_source_destroy(ga_DataSource *dataSrc) {
	tDataSourceFunc_Close func = dataSrc->closeFunc;
	char* context = (char*)dataSrc + sizeof(ga_DataSource);
	assert(dataSrc->refCount == 0);
	if(func)
		func(context);
	gc_mutex_destroy(dataSrc->refMutex);
	gcX_ops->freeFunc(dataSrc);
}

void ga_data_source_acquire(ga_DataSource *dataSrc) {
	// TODO atomics???
	gc_mutex_lock(dataSrc->refMutex);
	++dataSrc->refCount;
	gc_mutex_unlock(dataSrc->refMutex);
}

void ga_data_source_release(ga_DataSource *dataSrc) {
	gc_int32 refCount;
	assert(dataSrc->refCount > 0);
	gc_mutex_lock(dataSrc->refMutex);
	--dataSrc->refCount;
	refCount = dataSrc->refCount;
	gc_mutex_unlock(dataSrc->refMutex);
	if (refCount == 0) gaX_data_source_destroy(dataSrc);
}

/* Sample Source Structure */
void ga_sample_source_init(ga_SampleSource *sampleSrc) {
	sampleSrc->refCount = 1;
	sampleSrc->readFunc = 0;
	sampleSrc->endFunc = 0;
	sampleSrc->readyFunc = 0;
	sampleSrc->seekFunc = 0;
	sampleSrc->tellFunc = 0;
	sampleSrc->closeFunc = 0;
	sampleSrc->flags = 0;
	sampleSrc->refMutex = gc_mutex_create();
}

gc_int32 ga_sample_source_read(ga_SampleSource* sampleSrc, void* dst, gc_int32 numSamples,
                               tOnSeekFunc onSeekFunc, void* seekContext) {
	tSampleSourceFunc_Read func = sampleSrc->readFunc;
	assert(func);
	return func(sampleSrc, dst, numSamples, onSeekFunc, seekContext);
}

gc_int32 ga_sample_source_end(ga_SampleSource* in_sampleSrc) {
	tSampleSourceFunc_End func = in_sampleSrc->endFunc;
	assert(func);
	return func(in_sampleSrc);
}

gc_result ga_sample_source_seek(ga_SampleSource* in_sampleSrc, gc_int32 in_sampleOffset)
{
  tSampleSourceFunc_Seek func = in_sampleSrc->seekFunc;
  if(func)
    return func(in_sampleSrc, in_sampleOffset);
  return GC_ERROR_GENERIC;
}
gc_int32 ga_sample_source_tell(ga_SampleSource* in_sampleSrc, gc_int32* out_totalSamples)
{
  tSampleSourceFunc_Tell func = in_sampleSrc->tellFunc;
  char* context = (char*)in_sampleSrc + sizeof(ga_SampleSource);
  if(func)
    return func(in_sampleSrc, out_totalSamples);
  *out_totalSamples = -1;
  return -1;
}
gc_int32 ga_sample_source_flags(ga_SampleSource* in_sampleSrc)
{
  return in_sampleSrc->flags;
}
void ga_sample_source_format(ga_SampleSource* in_sampleSrc, ga_Format* out_format)
{
  memcpy(out_format, &in_sampleSrc->format, sizeof(ga_Format));
}
void gaX_sample_source_destroy(ga_SampleSource* in_sampleSrc)
{
  tSampleSourceFunc_Close func = in_sampleSrc->closeFunc;
  if(func)
    func(in_sampleSrc);
  gc_mutex_destroy(in_sampleSrc->refMutex);
  gcX_ops->freeFunc(in_sampleSrc);
}
void ga_sample_source_acquire(ga_SampleSource* in_sampleSrc)
{
  gc_mutex_lock(in_sampleSrc->refMutex);
  ++in_sampleSrc->refCount;
  gc_mutex_unlock(in_sampleSrc->refMutex);
}
void ga_sample_source_release(ga_SampleSource* in_sampleSrc)
{
  gc_int32 refCount;
  assert(in_sampleSrc->refCount > 0);
  gc_mutex_lock(in_sampleSrc->refMutex);
  --in_sampleSrc->refCount;
  refCount = in_sampleSrc->refCount;
  gc_mutex_unlock(in_sampleSrc->refMutex);
  if(refCount == 0)
    gaX_sample_source_destroy(in_sampleSrc);
}
gc_int32 ga_sample_source_ready(ga_SampleSource* in_sampleSrc, gc_int32 in_numSamples)
{
  tSampleSourceFunc_Ready func = in_sampleSrc->readyFunc;
  if(func)
    return func(in_sampleSrc, in_numSamples);
  return 1;
}

/* Memory Functions */
static ga_Memory* gaX_memory_create(void* in_data, gc_int32 in_size, gc_int32 in_copy)
{
  ga_Memory* ret = gcX_ops->allocFunc(sizeof(ga_Memory));
  ret->size = in_size;
  if(in_copy)
  {
    ret->data = gcX_ops->allocFunc(in_size);
    memcpy(ret->data, in_data, in_size);
  }
  else
    ret->data = in_data;
  ret->refMutex = gc_mutex_create();
  ret->refCount = 1;
  return (ga_Memory*)ret;
}
ga_Memory* ga_memory_create(void* in_data, gc_int32 in_size)
{
  return gaX_memory_create(in_data, in_size, 1);
}
ga_Memory* ga_memory_create_data_source(ga_DataSource* in_dataSource)
{
  ga_Memory* ret = 0;
  gc_int32 BUFFER_BYTES = 4096;
  char* data = 0;
  gc_int32 totalBytes = 0;
  gc_int32 numBytesRead = 0;
  do
  {
    data = gcX_ops->reallocFunc(data, totalBytes + BUFFER_BYTES);
    numBytesRead = ga_data_source_read(in_dataSource, data + totalBytes, 1, BUFFER_BYTES);
    if(numBytesRead < BUFFER_BYTES)
      data = gcX_ops->reallocFunc(data, totalBytes + numBytesRead);
    totalBytes += numBytesRead;
  } while(numBytesRead > 0);
  ret = gaX_memory_create(data, totalBytes, 0);
  if(!ret)
    gcX_ops->freeFunc(data);
  return ret;
}
gc_int32 ga_memory_size(ga_Memory* in_mem)
{
  return in_mem->size;
}
void* ga_memory_data(ga_Memory* in_mem)
{
  return in_mem->data;
}
static void gaX_memory_destroy(ga_Memory* in_mem)
{
  gcX_ops->freeFunc(in_mem->data);
  gcX_ops->freeFunc(in_mem);
}
void ga_memory_acquire(ga_Memory* in_mem)
{
  gc_mutex_lock(in_mem->refMutex);
  ++in_mem->refCount;
  gc_mutex_unlock(in_mem->refMutex);
}
void ga_memory_release(ga_Memory* in_mem)
{
  gc_int32 refCount;
  assert(in_mem->refCount > 0);
  gc_mutex_lock(in_mem->refMutex);
  --in_mem->refCount;
  refCount = in_mem->refCount;
  gc_mutex_unlock(in_mem->refMutex);
  if(refCount == 0)
    gaX_memory_destroy(in_mem);
}


/* Sound Functions */
ga_Sound* ga_sound_create(ga_Memory* in_memory, ga_Format* in_format)
{
  ga_Sound* ret = gcX_ops->allocFunc(sizeof(ga_Sound));
  ret->numSamples = ga_memory_size(in_memory) / ga_format_sampleSize(in_format);
  memcpy(&ret->format, in_format, sizeof(ga_Format));
  ga_memory_acquire(in_memory);
  ret->memory = in_memory;
  ret->refMutex = gc_mutex_create();
  ret->refCount = 1;
  return (ga_Sound*)ret;
}
ga_Sound* ga_sound_create_sample_source(ga_SampleSource* in_sampleSrc)
{
  ga_Sound* ret = 0;
  ga_Format format;
  gc_int32 dataSize;
  gc_int32 totalSamples;
  ga_SampleSource* sampleSrc = in_sampleSrc;
  gc_int32 sampleSize;
  ga_sample_source_format(sampleSrc, &format);
  sampleSize = ga_format_sampleSize(&format);
  ga_sample_source_tell(sampleSrc, &totalSamples);
  if(totalSamples > 0)
  {
    /* Known total samples*/
    char* data;
    ga_Memory* memory;
    dataSize = sampleSize * totalSamples;
    data = gcX_ops->allocFunc(dataSize);
    ga_sample_source_read(sampleSrc, data, totalSamples, 0, 0);
    memory = gaX_memory_create(data, dataSize, 0);
    if(memory)
    {
      ret = ga_sound_create(memory, &format);
      if(!ret)
        ga_memory_release(memory);
    }
    else
      gcX_ops->freeFunc(data);
  }
  else
  {
    /* Unknown total samples */
    gc_int32 BUFFER_SAMPLES = format.sampleRate * 2;
    char* data = 0;
    ga_Memory* memory;
    totalSamples = 0;
    while(!ga_sample_source_end(sampleSrc))
    {
      gc_int32 numSamplesRead;
      data = gcX_ops->reallocFunc(data, (totalSamples + BUFFER_SAMPLES) * sampleSize);
      numSamplesRead = ga_sample_source_read(sampleSrc, data + (totalSamples * sampleSize), BUFFER_SAMPLES, 0, 0);
      if(numSamplesRead < BUFFER_SAMPLES)
      {
        data = gcX_ops->reallocFunc(data, (totalSamples + numSamplesRead) * sampleSize);
      }
      totalSamples += numSamplesRead;
    }
    memory = gaX_memory_create(data, totalSamples * sampleSize, 0);
    if(memory)
    {
      ret = ga_sound_create(memory, &format);
      if(!ret)
        ga_memory_release(memory);
    }
    else
      gcX_ops->freeFunc(data);
  }
  return ret;
}
void* ga_sound_data(ga_Sound* in_sound)
{
  return ga_memory_data(in_sound->memory);
}
gc_int32 ga_sound_size(ga_Sound* in_sound)
{
  return ga_memory_size(in_sound->memory);
}
gc_int32 ga_sound_numSamples(ga_Sound* in_sound)
{
  return ga_memory_size(in_sound->memory) / ga_format_sampleSize(&in_sound->format);
}
void ga_sound_format(ga_Sound* in_sound, ga_Format* out_format)
{
  memcpy(out_format, &in_sound->format, sizeof(ga_Format));
}
static void gaX_sound_destroy(ga_Sound* in_sound)
{
  ga_memory_release(in_sound->memory);
  gcX_ops->freeFunc(in_sound);
}
void ga_sound_acquire(ga_Sound* in_sound)
{
  gc_mutex_lock(in_sound->refMutex);
  ++in_sound->refCount;
  gc_mutex_unlock(in_sound->refMutex);
}
void ga_sound_release(ga_Sound* in_sound)
{
  gc_int32 refCount;
  assert(in_sound->refCount > 0);
  gc_mutex_lock(in_sound->refMutex);
  --in_sound->refCount;
  refCount = in_sound->refCount;
  gc_mutex_unlock(in_sound->refMutex);
  if(refCount == 0)
    gaX_sound_destroy(in_sound);
}

/* Handle Functions */
void gaX_handle_init(ga_Handle* in_handle, ga_Mixer* in_mixer)
{
  ga_Handle* h = in_handle;
  h->state = GA_HANDLE_STATE_INITIAL;
  h->mixer = in_mixer;
  h->callback = 0;
  h->context = 0;
  h->gain = 1.0f;
  h->pitch = 1.0f;
  h->pan = 0.0f;
  h->handleMutex = gc_mutex_create();
}

ga_Handle* ga_handle_create(ga_Mixer* in_mixer,
                            ga_SampleSource* in_sampleSrc)
{
  ga_Handle* h = (ga_Handle*)gcX_ops->allocFunc(sizeof(ga_Handle));
  ga_sample_source_acquire(in_sampleSrc);
  h->sampleSrc = in_sampleSrc;
  h->finished = 0;
  gaX_handle_init(h, in_mixer);

  gc_mutex_lock(in_mixer->mixMutex);
  gc_list_link(&in_mixer->mixList, &h->mixLink, h);
  gc_mutex_unlock(in_mixer->mixMutex);

  gc_mutex_lock(in_mixer->dispatchMutex);
  gc_list_link(&in_mixer->dispatchList, &h->dispatchLink, h);
  gc_mutex_unlock(in_mixer->dispatchMutex);

  return h;
}
gc_result ga_handle_destroy(ga_Handle* in_handle)
{
  /* Sets the destroyed state. Will be cleaned up once all threads ACK. */
  gc_mutex_lock(in_handle->handleMutex);
  in_handle->state = GA_HANDLE_STATE_DESTROYED;
  gc_mutex_unlock(in_handle->handleMutex);
  return GC_SUCCESS;
}
gc_result gaX_handle_cleanup(ga_Handle* in_handle)
{
  /* May only be called from the dispatch thread */
  ga_Mixer* m = in_handle->mixer;
  ga_sample_source_release(in_handle->sampleSrc);
  gc_mutex_destroy(in_handle->handleMutex);
  gcX_ops->freeFunc(in_handle);
  return GC_SUCCESS;
}

gc_result ga_handle_play(ga_Handle* in_handle)
{
  gc_mutex_lock(in_handle->handleMutex);
  if(in_handle->state >= GA_HANDLE_STATE_FINISHED)
  {
    gc_mutex_unlock(in_handle->handleMutex);
    return GC_ERROR_GENERIC;
  }
  in_handle->state = GA_HANDLE_STATE_PLAYING;
  gc_mutex_unlock(in_handle->handleMutex);
  return GC_SUCCESS;
}
gc_result ga_handle_stop(ga_Handle* in_handle)
{
  gc_mutex_lock(in_handle->handleMutex);
  if(in_handle->state >= GA_HANDLE_STATE_FINISHED)
  {
    gc_mutex_unlock(in_handle->handleMutex);
    return GC_ERROR_GENERIC;
  }
  in_handle->state = GA_HANDLE_STATE_STOPPED;
  gc_mutex_unlock(in_handle->handleMutex);
  return GC_SUCCESS;
}
gc_int32 ga_handle_playing(ga_Handle* in_handle)
{
  return in_handle->state == GA_HANDLE_STATE_PLAYING ? GC_TRUE : GC_FALSE;
}
gc_int32 ga_handle_stopped(ga_Handle* in_handle)
{
  return in_handle->state == GA_HANDLE_STATE_STOPPED ? GC_TRUE : GC_FALSE;
}
gc_int32 ga_handle_finished(ga_Handle* in_handle)
{
  return in_handle->state >= GA_HANDLE_STATE_FINISHED ? GC_TRUE : GC_FALSE;
}
gc_int32 ga_handle_destroyed(ga_Handle* in_handle)
{
  return in_handle->state >= GA_HANDLE_STATE_DESTROYED ? GC_TRUE : GC_FALSE;
}
gc_result ga_handle_setCallback(ga_Handle* in_handle, ga_FinishCallback in_callback, void* in_context)
{
  /* Does not need mutex because it can only be called from the dispatch thread */
  in_handle->callback = in_callback;
  in_handle->context = in_context;
  return GC_SUCCESS;
}
gc_result ga_handle_setParamf(ga_Handle* in_handle, gc_int32 in_param,
                              gc_float32 in_value)
{
  ga_Handle* h = in_handle;
  switch(in_param)
  {
  case GA_HANDLE_PARAM_GAIN:
    gc_mutex_lock(h->handleMutex);
    h->gain = in_value;
    gc_mutex_unlock(h->handleMutex);
    return GC_SUCCESS;
  case GA_HANDLE_PARAM_PAN:
    gc_mutex_lock(h->handleMutex);
    h->pan = in_value;
    gc_mutex_unlock(h->handleMutex);
    return GC_SUCCESS;
  case GA_HANDLE_PARAM_PITCH:
    gc_mutex_lock(h->handleMutex);
    h->pitch = in_value;
    gc_mutex_unlock(h->handleMutex);
    return GC_SUCCESS;
  }
  return GC_ERROR_GENERIC;
}
gc_result ga_handle_getParamf(ga_Handle* in_handle, gc_int32 in_param,
                              gc_float32* out_value)
{
  ga_Handle* h = in_handle;
  switch(in_param)
  {
  case GA_HANDLE_PARAM_GAIN: *out_value = h->gain; return GC_SUCCESS;
  case GA_HANDLE_PARAM_PAN: *out_value = h->pan; return GC_SUCCESS;
  case GA_HANDLE_PARAM_PITCH: *out_value = h->pitch; return GC_SUCCESS;
  }
  return GC_ERROR_GENERIC;
}
gc_result ga_handle_setParami(ga_Handle* in_handle, gc_int32 in_param,
                              gc_int32 in_value)
{
  ga_Handle* h = in_handle;
  /*
  switch(in_param)
  {
  case GA_HANDLE_PARAM_?:
    gc_mutex_lock(h->handleMutex);
    gc_mutex_unlock(h->handleMutex);
    return GC_SUCCESS;
  }
  */
  return GC_ERROR_GENERIC;
}
gc_result ga_handle_getParami(ga_Handle* in_handle, gc_int32 in_param,
                              gc_int32* out_value)
{
  ga_Handle* h = in_handle;
  /*
  switch(in_param)
  {
  case GA_HANDLE_PARAM_?: *out_value = ?; return GC_SUCCESS;
  }
  */
  return GC_ERROR_GENERIC;
}
gc_result ga_handle_seek(ga_Handle* in_handle, gc_int32 in_sampleOffset)
{
  ga_sample_source_seek(in_handle->sampleSrc, in_sampleOffset);
  return GC_SUCCESS;
}
gc_int32 ga_handle_tell(ga_Handle* in_handle, gc_int32 in_param)
{
  gc_int32 total = 0;
  gc_int32 cur = ga_sample_source_tell(in_handle->sampleSrc, &total);
  if(in_param == GA_TELL_PARAM_CURRENT)
    return cur;
  else if(in_param == GA_TELL_PARAM_TOTAL)
    return total;
  return -1;
}
gc_int32 ga_handle_ready(ga_Handle* in_handle, gc_int32 in_numSamples)
{
  return ga_sample_source_ready(in_handle->sampleSrc, in_numSamples);
}
void ga_handle_format(ga_Handle* in_handle, ga_Format* out_format)
{
  ga_sample_source_format(in_handle->sampleSrc, out_format);
}

/* Mixer Functions */
ga_Mixer* ga_mixer_create(ga_Format* in_format, gc_int32 in_numSamples)
{
  ga_Mixer* ret = gcX_ops->allocFunc(sizeof(ga_Mixer));
  gc_int32 mixSampleSize;
  gc_list_head(&ret->dispatchList);
  gc_list_head(&ret->mixList);
  ret->numSamples = in_numSamples;
  memcpy(&ret->format, in_format, sizeof(ga_Format));
  ret->mixFormat.bitsPerSample = 32;
  ret->mixFormat.numChannels = in_format->numChannels;
  ret->mixFormat.sampleRate = in_format->sampleRate;
  mixSampleSize = ga_format_sampleSize(&ret->mixFormat);
  ret->mixBuffer = (gc_int32*)gcX_ops->allocFunc(in_numSamples * mixSampleSize);
  ret->dispatchMutex = gc_mutex_create();
  ret->mixMutex = gc_mutex_create();
  return ret;
}
ga_Format* ga_mixer_format(ga_Mixer* in_mixer)
{
  return &in_mixer->format;
}
gc_int32 ga_mixer_numSamples(ga_Mixer* in_mixer)
{
  return in_mixer->numSamples;
}
void gaX_mixer_mix_buffer(ga_Mixer* in_mixer,
                          void* in_srcBuffer, gc_int32 in_srcSamples, ga_Format* in_srcFmt,
                          gc_int32* in_dstBuffer, gc_int32 in_dstSamples, ga_Format* in_dstFmt,
                          gc_float32 in_gain, gc_float32 in_pan, gc_float32 in_pitch)
{
  ga_Format* mixFmt = in_dstFmt;
  gc_int32 mixerChannels = mixFmt->numChannels;
  gc_int32 srcChannels = in_srcFmt->numChannels;
  gc_float32 sampleScale = in_srcFmt->sampleRate / (gc_float32)mixFmt->sampleRate * in_pitch;
  gc_int32* dst = in_dstBuffer;
  gc_int32 numToFill = in_dstSamples;
  gc_float32 fj = 0.0f;
  gc_int32 j = 0;
  gc_int32 i = 0;
  gc_float32 pan = in_pan;
  gc_float32 gain = in_gain;
  gc_float32 srcSamplesRead = 0.0f;
  gc_int32 sampleSize = ga_format_sampleSize(in_srcFmt);
  pan = (pan + 1.0f) / 2.0f;
  pan = pan > 1.0f ? 1.0f : pan;
  pan = pan < 0.0f ? 0.0f : pan;

  /* TODO: Support 8-bit/16-bit mono/stereo mixer format */
  switch(in_srcFmt->bitsPerSample)
  {
  case 16:
    {
      gc_int32 srcBytes = in_srcSamples * sampleSize;
      const gc_int16* src = (const gc_int16*)in_srcBuffer;
      while(i < numToFill * (gc_int32)mixerChannels && srcBytes >= 2 * srcChannels)
      {
        gc_int32 newJ, deltaSrcBytes;
        dst[i] += (gc_int32)((gc_int32)src[j] * gain * (1.0f - pan) * 2);
        dst[i + 1] += (gc_int32)((gc_int32)src[j + ((srcChannels == 1) ? 0 : 1)] * gain * pan * 2);
        i += mixerChannels;
        fj += sampleScale * srcChannels;
        srcSamplesRead += sampleScale * srcChannels;
        newJ = (gc_uint32)fj & (srcChannels == 1 ? ~0 : ~0x1);
        deltaSrcBytes = (newJ - j) * 2;
        j = newJ;
        srcBytes -= deltaSrcBytes;
      }
      break;
    }
  }
}
void gaX_mixer_mix_handle(ga_Mixer* in_mixer, ga_Handle* in_handle, gc_int32 in_numSamples)
{
  ga_Handle* h = in_handle;

  ga_Mixer* m = in_mixer;
  ga_SampleSource* ss = h->sampleSrc;
  if(ga_sample_source_end(ss))
  {
    /* Stream is finished! */
    gc_mutex_lock(h->handleMutex);
    if(h->state < GA_HANDLE_STATE_FINISHED)
      h->state = GA_HANDLE_STATE_FINISHED;
    gc_mutex_unlock(h->handleMutex);
    return;
  }
  else
  {
    if(h->state == GA_HANDLE_STATE_PLAYING)
    {
      ga_Format handleFormat;
      ga_sample_source_format(ss, &handleFormat);
      {
        /* Check if we have enough samples to stream a full buffer */
        gc_int32 srcSampleSize = ga_format_sampleSize(&handleFormat);
        gc_int32 dstSampleSize = ga_format_sampleSize(&m->format);
        gc_float32 oldPitch = h->pitch;
        gc_float32 dstToSrc = handleFormat.sampleRate / (gc_float32)m->format.sampleRate * oldPitch;
        gc_int32 requested = (gc_int32)(in_numSamples * dstToSrc);
        requested = requested / dstToSrc < in_numSamples ? requested + 1 : requested;
        if(requested > 0 && ga_sample_source_ready(ss, requested))
        {
          gc_float32 gain, pan, pitch;
          gc_int32* dstBuffer;
          gc_int32 dstSamples;

          gc_mutex_lock(h->handleMutex);
          gain = h->gain;
          pan = h->pan;
          pitch = h->pitch;
          gc_mutex_unlock(h->handleMutex);

          /* We avoided a mutex lock by using pitch to check if buffer has enough dst samples */
          /* If it has changed since then, we re-test to make sure we still have enough samples */
          if(oldPitch != pitch)
          {
            dstToSrc = handleFormat.sampleRate / (gc_float32)m->format.sampleRate * pitch;
            requested = (gc_int32)(in_numSamples * dstToSrc);
            requested = requested / dstToSrc < in_numSamples ? requested + 1 : requested;
            if(!(requested > 0 && ga_sample_source_ready(ss, requested)))
              return;
          }

          dstBuffer = &m->mixBuffer[0];
          dstSamples = in_numSamples;
          {
            /* TODO: To optimize, we can refactor the _read() interface to be _mix(), avoiding this malloc/copy */
            gc_int32 bufferSize = requested * srcSampleSize;
            void* src = gcX_ops->allocFunc(bufferSize);
            gc_int32 dstBytes = dstSamples * dstSampleSize;
            gc_int32 numRead = 0;
            numRead = ga_sample_source_read(ss, src, requested, 0, 0);
            gaX_mixer_mix_buffer(in_mixer,
                                 src, numRead, &handleFormat,
                                 dstBuffer, dstSamples, &m->format,
                                 gain, pan, pitch);
            gcX_ops->freeFunc(src);
          }
        }
      }
    }
  }
}
gc_result ga_mixer_mix(ga_Mixer* in_mixer, void* out_buffer)
{
  gc_int32 i;
  ga_Mixer* m = in_mixer;
  gc_Link* link;
  gc_int32 end = m->numSamples * m->format.numChannels;
  ga_Format* fmt = &m->format;
  gc_int32 mixSampleSize = ga_format_sampleSize(&m->mixFormat);
  memset(&m->mixBuffer[0], 0, m->numSamples * mixSampleSize);

  link = m->mixList.next;
  while(link != &m->mixList)
  {
    ga_Handle* h = (ga_Handle*)link->data;
    gc_Link* oldLink = link;
    link = link->next;
    gaX_mixer_mix_handle(m, (ga_Handle*)h, m->numSamples);
    if(ga_handle_finished(h))
    {
      gc_mutex_lock(m->mixMutex);
      gc_list_unlink(oldLink);
      gc_mutex_unlock(m->mixMutex);
    }
  }

  switch(fmt->bitsPerSample) /* mixBuffer will already be correct bps */
  {
  case 8:
    {
      gc_int8* mix = (gc_int8*)out_buffer;
      for(i = 0; i < end; ++i)
      {
        gc_int32 sample = m->mixBuffer[i];
        mix[i] = (gc_int8)(sample > -128 ? (sample < 127 ? sample : 127) : -128);
      }
      break;
    }
  case 16:
    {
      gc_int16* mix = (gc_int16*)out_buffer;
      for(i = 0; i < end; ++i)
      {
        gc_int32 sample = m->mixBuffer[i];
        mix[i] = (gc_int16)(sample > -32768 ? (sample < 32767 ? sample : 32767) : -32768);
      }
      break;
    }
  }
  return GC_SUCCESS;
}
gc_result ga_mixer_dispatch(ga_Mixer* in_mixer)
{
  ga_Mixer* m = in_mixer;
  gc_Link* link = m->dispatchList.next;
  while(link != &m->dispatchList)
  {
    gc_Link* oldLink = link;
    ga_Handle* oldHandle = (ga_Handle*)oldLink->data;
    link = link->next;

    /* Remove finished handles and call callbacks */
    if(ga_handle_destroyed(oldHandle))
    {
      if(!oldHandle->mixLink.next)
      {
        /* NOTES ABOUT THREADING POLICY WITH REGARD TO LINKED LISTS: */
        /* Only a single thread may iterate through any list */
        /* The thread that unlinks must be the only thread that iterates through the list */
        /* A single auxiliary thread may link(), but must mutex-lock to avoid link/unlink collisions */
        gc_mutex_lock(m->dispatchMutex);
        gc_list_unlink(&oldHandle->dispatchLink);
        gc_mutex_unlock(m->dispatchMutex);
        gaX_handle_cleanup(oldHandle);
      }
    }
    else if(oldHandle->callback && ga_handle_finished(oldHandle))
    {
      oldHandle->callback(oldHandle, oldHandle->context);
      oldHandle->callback = 0;
      oldHandle->context = 0;
    }
  }
  return GC_SUCCESS;
}
gc_result ga_mixer_destroy(ga_Mixer* in_mixer)
{
  /* NOTE: Mixer/handles must no longer be in use on any thread when destroy is called */
  ga_Mixer* m = in_mixer;
  gc_Link* link;
  link = m->dispatchList.next;
  while(link != &m->dispatchList)
  {
    ga_Handle* oldHandle = (ga_Handle*)link->data;
    link = link->next;
    gaX_handle_cleanup(oldHandle);
  }

  gc_mutex_destroy(in_mixer->dispatchMutex);
  gc_mutex_destroy(in_mixer->mixMutex);

  gcX_ops->freeFunc(in_mixer->mixBuffer);
  gcX_ops->freeFunc(in_mixer);
  return GC_SUCCESS;
}
