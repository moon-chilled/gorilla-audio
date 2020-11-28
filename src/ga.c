#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

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
	if (type == ga_DeviceType_Default) {
#define try(t) if ((ret = ga_device_open(t, num_buffers, num_samples, format))) return ret
		ga_Device *ret;
#if defined(ENABLE_OSS)
		try(ga_DeviceType_OSS);
#endif
#if defined(ENABLE_XAUDIO2)
		try(ga_DeviceType_XAudio2);
#endif
#if defined(ENABLE_PULSEAUDIO)
		try(ga_DeviceType_PulseAudio);
#endif
#if defined(ENABLE_ALSA)
		try(ga_DeviceType_ALSA);      // put pulse before alsa, as pulseaudio has an alsa implementation
#endif
#if defined(ENABLE_OPENAL)
		try(ga_DeviceType_OpenAL);    // generic (multiplatform) drivers go last
#endif
#undef try
		return NULL;
	}

	ga_Device *ret = gcX_ops->allocFunc(sizeof(ga_Device));
	ret->dev_type = type;
	ret->num_buffers = num_buffers;
	ret->num_samples = num_samples;
	ret->format = *format;

	switch (type) {
		case ga_DeviceType_Dummy: ret->procs = gaX_deviceprocs_dummy; break;
#ifdef ENABLE_OSS
		case ga_DeviceType_OSS: ret->procs = gaX_deviceprocs_OSS; break;
#endif
#ifdef ENABLE_XAUDIO2
		case ga_DeviceType_XAudio2: ret->procs = gaX_deviceprocs_XAudio2; break;
#endif
#ifdef ENABLE_PULSEAUDIO
		case ga_DeviceType_PulseAudio: ret->procs = gaX_deviceprocs_PulseAudio; break;
#endif
#ifdef ENABLE_ALSA
		case ga_DeviceType_ALSA: ret->procs = gaX_deviceprocs_ALSA; break;
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
	gc_result ret = device->procs.close ? device->procs.close(device) : GC_ERROR_GENERIC;
	free(device);
	return ret;
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
	char *context = (char*)dataSrc + sizeof(ga_DataSource);
	assert(dataSrc->refCount == 0);
	if(func)
		func(context);
	gcX_ops->freeFunc(dataSrc);
}

void ga_data_source_acquire(ga_DataSource *dataSrc) {
	atomic_fetch_add(&dataSrc->refCount, 1);
}

void ga_data_source_release(ga_DataSource *dataSrc) {
	gc_int32 rc = atomic_fetch_sub(&dataSrc->refCount, 1);
	assert(rc > 0);
	// if there *was* one reference, before we removed it
	if (rc == 1) gaX_data_source_destroy(dataSrc);
}

/* Sample Source Structure */
void ga_sample_source_init(ga_SampleSource *sampleSrc) {
	sampleSrc->refCount = 1;
	sampleSrc->readFunc = NULL;
	sampleSrc->endFunc = NULL;
	sampleSrc->readyFunc = NULL;
	sampleSrc->seekFunc = NULL;
	sampleSrc->tellFunc = NULL;
	sampleSrc->closeFunc = NULL;
	sampleSrc->flags = 0;
}

gc_int32 ga_sample_source_read(ga_SampleSource *sampleSrc, void *dst, gc_int32 numSamples, tOnSeekFunc onSeekFunc, void *seekContext) {
	return sampleSrc->readFunc(sampleSrc, dst, numSamples, onSeekFunc, seekContext);
}

gc_int32 ga_sample_source_end(ga_SampleSource *sampleSrc) {
	return sampleSrc->endFunc(sampleSrc);
}

gc_result ga_sample_source_seek(ga_SampleSource *sampleSrc, gc_int32 sampleOffset) {
	return sampleSrc->seekFunc ? sampleSrc->seekFunc(sampleSrc, sampleOffset) : GC_ERROR_GENERIC;
}

gc_int32 ga_sample_source_tell(ga_SampleSource *sampleSrc, gc_int32 *totalSamples) {
	if (!sampleSrc->tellFunc) {
		*totalSamples = -1;
		return -1;
	}

	return sampleSrc->tellFunc(sampleSrc, totalSamples);
}

gc_int32 ga_sample_source_flags(ga_SampleSource *sampleSrc) {
	return sampleSrc->flags;
}

void ga_sample_source_format(ga_SampleSource *sampleSrc, ga_Format *format) {
	*format = sampleSrc->format;
}

void gaX_sample_source_destroy(ga_SampleSource *sampleSrc) {
	if (sampleSrc->closeFunc) sampleSrc->closeFunc(sampleSrc);
	gcX_ops->freeFunc(sampleSrc);
}

void ga_sample_source_acquire(ga_SampleSource *sampleSrc) {
	atomic_fetch_add(&sampleSrc->refCount, 1);
}

void ga_sample_source_release(ga_SampleSource *sampleSrc) {
	gc_int32 rc = atomic_fetch_sub(&sampleSrc->refCount, 1);
	assert(rc > 0);
	if (rc == 1) gaX_sample_source_destroy(sampleSrc);
}

gc_bool ga_sample_source_ready(ga_SampleSource *sampleSrc, gc_int32 numSamples) {
	return sampleSrc->readyFunc ? sampleSrc->readyFunc(sampleSrc, numSamples) : gc_true;
}

/* Memory Functions */
static ga_Memory *gaX_memory_create(void *data, gc_size size, gc_bool copy) {
	ga_Memory *ret = gcX_ops->allocFunc(sizeof(ga_Memory));
	ret->size = size;
	if (data) {
		if (copy) ret->data = memcpy(gcX_ops->allocFunc(size), data, size);
		else ret->data = data;
	} else {
		ret->data = gcX_ops->allocFunc(size);
	}
	ret->refCount = 1;
	return ret;
}

ga_Memory *ga_memory_create(void *data, gc_size size) {
	return gaX_memory_create(data, size, gc_true);
}

ga_Memory *ga_memory_create_data_source(ga_DataSource *dataSource) {
	ga_Memory* ret = 0;
	gc_int32 BUFFER_BYTES = 4096;
	char* data = 0;
	gc_int32 totalBytes = 0;
	gc_int32 numBytesRead = 0;
	do {
		data = gcX_ops->reallocFunc(data, totalBytes + BUFFER_BYTES);
		numBytesRead = ga_data_source_read(dataSource, data + totalBytes, 1, BUFFER_BYTES);
		if (numBytesRead < BUFFER_BYTES)
			data = gcX_ops->reallocFunc(data, totalBytes + numBytesRead);
		totalBytes += numBytesRead;
	} while (numBytesRead > 0);
	ret = gaX_memory_create(data, totalBytes, gc_false);
	if (!ret) gcX_ops->freeFunc(data);
	return ret;
}

gc_size ga_memory_size(ga_Memory *mem) {
	return mem->size;
}

void *ga_memory_data(ga_Memory *mem) {
	return mem->data;
}

static void gaX_memory_destroy(ga_Memory *mem) {
	gcX_ops->freeFunc(mem->data);
	gcX_ops->freeFunc(mem);
}

void ga_memory_acquire(ga_Memory *mem) {
	atomic_fetch_add(&mem->refCount, 1);
}

void ga_memory_release(ga_Memory *mem) {
	gc_int32 rc = atomic_fetch_sub(&mem->refCount, 1);
	assert(rc > 0);
	if (rc == 1) gaX_memory_destroy(mem);
}


/* Sound Functions */
ga_Sound *ga_sound_create(ga_Memory *memory, ga_Format *format) {
	ga_Sound *ret = gcX_ops->allocFunc(sizeof(ga_Sound));
	assert(ga_memory_size(memory) % ga_format_sampleSize(format) == 0);
	ret->numSamples = ga_memory_size(memory) / ga_format_sampleSize(format);
	ret->format = *format;
	ga_memory_acquire(memory);
	ret->memory = memory;
	ret->refCount = 1;
	return (ga_Sound*)ret;
}

ga_Sound *ga_sound_create_sample_source(ga_SampleSource *sampleSrc) {
	ga_Sound* ret = 0;
	ga_Format format;
	gc_int32 dataSize;
	gc_int32 totalSamples;
	gc_int32 sampleSize;
	ga_sample_source_format(sampleSrc, &format);
	sampleSize = ga_format_sampleSize(&format);
	ga_sample_source_tell(sampleSrc, &totalSamples);

	if (totalSamples > 0) {
		/* Known total samples*/
		char* data;
		ga_Memory* memory;
		dataSize = sampleSize * totalSamples;
		data = gcX_ops->allocFunc(dataSize);
		ga_sample_source_read(sampleSrc, data, totalSamples, 0, 0);
		memory = gaX_memory_create(data, dataSize, 0);
		if (memory) {
			ret = ga_sound_create(memory, &format);
			if (!ret) ga_memory_release(memory);
		} else {
			gcX_ops->freeFunc(data);
		}
	} else {
		/* Unknown total samples */
		gc_int32 BUFFER_SAMPLES = format.sampleRate * 2;
		char* data = 0;
		ga_Memory* memory;
		totalSamples = 0;
		while (!ga_sample_source_end(sampleSrc)) {
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
		if (memory) {
			ret = ga_sound_create(memory, &format);
			if (!ret) ga_memory_release(memory);
		} else {
			gcX_ops->freeFunc(data);
		}
	}
	return ret;
}

void *ga_sound_data(ga_Sound *sound) {
	return ga_memory_data(sound->memory);
}

gc_size ga_sound_size(ga_Sound *sound) {
	return ga_memory_size(sound->memory);
}

gc_size ga_sound_numSamples(ga_Sound *sound) {
	return ga_memory_size(sound->memory) / ga_format_sampleSize(&sound->format);
}

void ga_sound_format(ga_Sound *sound, ga_Format *format) {
	*format = sound->format;
}

static void gaX_sound_destroy(ga_Sound *sound) {
	ga_memory_release(sound->memory);
	gcX_ops->freeFunc(sound);
}

void ga_sound_acquire(ga_Sound *sound) {
	atomic_fetch_add(&sound->refCount, 1);
}

void ga_sound_release(ga_Sound *sound) {
	gc_int32 rc = atomic_fetch_sub(&sound->refCount, 1);
	assert(rc > 0);
	if (rc == 1) gaX_sound_destroy(sound);
}

/* Handle Functions */
void gaX_handle_init(ga_Handle *handle, ga_Mixer *mixer) {
	handle->state = GA_HANDLE_STATE_INITIAL;
	handle->mixer = mixer;
	handle->callback = 0;
	handle->context = 0;
	handle->gain = 1.0f;
	handle->pitch = 1.0f;
	handle->pan = 0.0f;
	handle->handleMutex = gc_mutex_create();
}

ga_Handle *ga_handle_create(ga_Mixer *mixer, ga_SampleSource *sampleSrc) {
	ga_Handle* h = gcX_ops->allocFunc(sizeof(ga_Handle));
	ga_sample_source_acquire(sampleSrc);
	h->sampleSrc = sampleSrc;
	h->finished = 0;
	gaX_handle_init(h, mixer);

	gc_mutex_lock(mixer->mixMutex);
	gc_list_link(&mixer->mixList, &h->mixLink, h);
	gc_mutex_unlock(mixer->mixMutex);

	gc_mutex_lock(mixer->dispatchMutex);
	gc_list_link(&mixer->dispatchList, &h->dispatchLink, h);
	gc_mutex_unlock(mixer->dispatchMutex);

	return h;
}

gc_result ga_handle_destroy(ga_Handle *handle) {
	/* Sets the destroyed state. Will be cleaned up once all threads ACK. */
	gc_mutex_lock(handle->handleMutex);
	handle->state = GA_HANDLE_STATE_DESTROYED;
	gc_mutex_unlock(handle->handleMutex);
	return GC_SUCCESS;
}

gc_result gaX_handle_cleanup(ga_Handle *handle) {
	/* May only be called from the dispatch thread */
	ga_Mixer* m = handle->mixer;
	ga_sample_source_release(handle->sampleSrc);
	gc_mutex_destroy(handle->handleMutex);
	gcX_ops->freeFunc(handle);
	return GC_SUCCESS;
}

gc_result ga_handle_play(ga_Handle *handle) {
	gc_mutex_lock(handle->handleMutex);
	if (handle->state >= GA_HANDLE_STATE_FINISHED) {
		gc_mutex_unlock(handle->handleMutex);
		return GC_ERROR_GENERIC;
	}
	handle->state = GA_HANDLE_STATE_PLAYING;
	gc_mutex_unlock(handle->handleMutex);
	return GC_SUCCESS;
}

gc_result ga_handle_stop(ga_Handle *handle) {
	gc_mutex_lock(handle->handleMutex);
	if (handle->state >= GA_HANDLE_STATE_FINISHED) {
		gc_mutex_unlock(handle->handleMutex);
		return GC_ERROR_GENERIC;
	}
	handle->state = GA_HANDLE_STATE_STOPPED;
	gc_mutex_unlock(handle->handleMutex);
	return GC_SUCCESS;
}

gc_bool ga_handle_playing(ga_Handle *handle) {
	return handle->state == GA_HANDLE_STATE_PLAYING;
}
gc_bool ga_handle_stopped(ga_Handle *handle) {
	return handle->state == GA_HANDLE_STATE_STOPPED;
}
gc_bool ga_handle_finished(ga_Handle *handle) {
	return handle->state >= GA_HANDLE_STATE_FINISHED;
}
gc_bool ga_handle_destroyed(ga_Handle *handle) {
	return handle->state >= GA_HANDLE_STATE_DESTROYED;
}

gc_result ga_handle_setCallback(ga_Handle *handle, ga_FinishCallback callback, void *context) {
	/* Does not need mutex because it can only be called from the dispatch thread */
	handle->callback = callback;
	handle->context = context;
	return GC_SUCCESS;
}

gc_result ga_handle_setParamf(ga_Handle *handle, gc_int32 param, gc_float32 value) {
	switch (param) {
		case GA_HANDLE_PARAM_GAIN:
			gc_mutex_lock(handle->handleMutex);
			handle->gain = value;
			gc_mutex_unlock(handle->handleMutex);
			return GC_SUCCESS;
		case GA_HANDLE_PARAM_PAN:
			gc_mutex_lock(handle->handleMutex);
			handle->pan = value;
			gc_mutex_unlock(handle->handleMutex);
			return GC_SUCCESS;
		case GA_HANDLE_PARAM_PITCH:
			gc_mutex_lock(handle->handleMutex);
			handle->pitch = value;
			gc_mutex_unlock(handle->handleMutex);
			return GC_SUCCESS;
		default: return GC_ERROR_GENERIC;
	}
}

gc_result ga_handle_getParamf(ga_Handle *handle, gc_int32 param, gc_float32 *value) {
	switch (param) {
		case GA_HANDLE_PARAM_GAIN:  *value = handle->gain;  return GC_SUCCESS;
		case GA_HANDLE_PARAM_PAN:   *value = handle->pan;   return GC_SUCCESS;
		case GA_HANDLE_PARAM_PITCH: *value = handle->pitch; return GC_SUCCESS;
		default: return GC_ERROR_GENERIC;
	}
}

gc_result ga_handle_setParami(ga_Handle *handle, gc_int32 param, gc_int32 value) {
	/*
	   switch(param)
	   {
	   case GA_HANDLE_PARAM_?:
	   gc_mutex_lock(handle->handleMutex);
	   gc_mutex_unlock(handle->handleMutex);
	   return GC_SUCCESS;
	   }
	   */
	return GC_ERROR_GENERIC;
}

gc_result ga_handle_getParami(ga_Handle *handle, gc_int32 param, gc_int32 *value) {
	/*
	   switch(param)
	   {
	   case GA_HANDLE_PARAM_?: *value = ?; return GC_SUCCESS;
	   }
	   */
	return GC_ERROR_GENERIC;
}

gc_result ga_handle_seek(ga_Handle *handle, gc_int32 sampleOffset) {
	ga_sample_source_seek(handle->sampleSrc, sampleOffset);
	return GC_SUCCESS;
}

gc_int32 ga_handle_tell(ga_Handle *handle, gc_int32 param) {
	gc_int32 total = 0;
	gc_int32 cur = ga_sample_source_tell(handle->sampleSrc, &total);
	if (param == GA_TELL_PARAM_CURRENT) return cur;
	else if (param == GA_TELL_PARAM_TOTAL) return total;
	else return -1;
}

gc_bool ga_handle_ready(ga_Handle *handle, gc_int32 numSamples) {
	return ga_sample_source_ready(handle->sampleSrc, numSamples);
}

void ga_handle_format(ga_Handle *handle, ga_Format *format) {
	ga_sample_source_format(handle->sampleSrc, format);
}

/* Mixer Functions */
ga_Mixer *ga_mixer_create(ga_Format *format, gc_int32 numSamples) {
	ga_Mixer *ret = gcX_ops->allocFunc(sizeof(ga_Mixer));
	gc_int32 mixSampleSize;
	gc_list_head(&ret->dispatchList);
	gc_list_head(&ret->mixList);
	ret->numSamples = numSamples;
	ret->format = *format;
	ret->mixFormat.bitsPerSample = 32;
	ret->mixFormat.numChannels = format->numChannels;
	ret->mixFormat.sampleRate = format->sampleRate;
	mixSampleSize = ga_format_sampleSize(&ret->mixFormat);
	ret->mixBuffer = gcX_ops->allocFunc(numSamples * mixSampleSize);
	ret->dispatchMutex = gc_mutex_create();
	ret->mixMutex = gc_mutex_create();
	return ret;
}

ga_Format *ga_mixer_format(ga_Mixer *mixer) {
	return &mixer->format;
}

gc_int32 ga_mixer_numSamples(ga_Mixer *mixer) {
	return mixer->numSamples;
}

void gaX_mixer_mix_buffer(ga_Mixer *mixer,
                          void *srcBuffer, gc_int32 srcSamples, ga_Format *srcFmt,
                          gc_int32 *dst, gc_int32 dstSamples, ga_Format *dstFmt,
			  gc_float32 gain, gc_float32 pan, gc_float32 pitch) {
	gc_int32 mixerChannels = dstFmt->numChannels;
	gc_int32 srcChannels = srcFmt->numChannels;
	gc_float32 sampleScale = srcFmt->sampleRate / (gc_float32)dstFmt->sampleRate * pitch;
	gc_float32 fj = 0.0f;
	gc_int32 j = 0;
	gc_int32 i = 0;
	gc_float32 srcSamplesRead = 0.0f;
	gc_int32 sampleSize = ga_format_sampleSize(srcFmt);
	pan = (pan + 1.0f) / 2.0f;
	pan = pan > 1.0f ? 1.0f : pan;
	pan = pan < 0.0f ? 0.0f : pan;

	/* TODO: Support 8-bit/16-bit mono/stereo mixer format */
	switch (srcFmt->bitsPerSample) {
		case 16: {
			gc_int32 srcBytes = srcSamples * sampleSize;
			const gc_int16 *src = srcBuffer;
			while (i < dstSamples * (gc_int32)mixerChannels && srcBytes >= 2 * srcChannels) {
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

void gaX_mixer_mix_handle(ga_Mixer *mixer, ga_Handle *handle, gc_int32 numSamples) {
	ga_SampleSource* ss = handle->sampleSrc;
	if (ga_sample_source_end(ss)) {
		/* Stream is finished! */
		gc_mutex_lock(handle->handleMutex);
		if (handle->state < GA_HANDLE_STATE_FINISHED)
			handle->state = GA_HANDLE_STATE_FINISHED;
		gc_mutex_unlock(handle->handleMutex);
		return;
	}
	if (handle->state != GA_HANDLE_STATE_PLAYING) return;
	ga_Format handleFormat;
	ga_sample_source_format(ss, &handleFormat);
	/* Check if we have enough samples to stream a full buffer */
	gc_int32 srcSampleSize = ga_format_sampleSize(&handleFormat);
	gc_int32 dstSampleSize = ga_format_sampleSize(&mixer->format);
	gc_float32 oldPitch = handle->pitch;
	gc_float32 dstToSrc = handleFormat.sampleRate / (gc_float32)mixer->format.sampleRate * oldPitch;
	gc_int32 requested = (gc_int32)(numSamples * dstToSrc);
	requested = requested / dstToSrc < numSamples ? requested + 1 : requested;

	if (requested <= 0 || !ga_sample_source_ready(ss, requested)) return;
	gc_float32 gain, pan, pitch;
	gc_int32 *dstBuffer;
	gc_int32 dstSamples;

	gc_mutex_lock(handle->handleMutex);
	gain = handle->gain;
	pan = handle->pan;
	pitch = handle->pitch;
	gc_mutex_unlock(handle->handleMutex);

	/* We avoided a mutex lock by using pitch to check if buffer has enough dst samples */
	/* If it has changed since then, we re-test to make sure we still have enough samples */
	if (oldPitch != pitch) {
		dstToSrc = handleFormat.sampleRate / (gc_float32)mixer->format.sampleRate * pitch;
		requested = (gc_int32)(numSamples * dstToSrc);
		requested = requested / dstToSrc < numSamples ? requested + 1 : requested;
		if (requested <= 0 || !ga_sample_source_ready(ss, requested)) return;
	}

	dstBuffer = &mixer->mixBuffer[0];
	dstSamples = numSamples;
	/* TODO: To optimize, we can refactor the _read() interface to be _mix(), avoiding this malloc/copy */
	gc_int32 bufferSize = requested * srcSampleSize;
	void *src = gcX_ops->allocFunc(bufferSize);
	gc_int32 dstBytes = dstSamples * dstSampleSize;
	gc_int32 numRead = 0;
	numRead = ga_sample_source_read(ss, src, requested, 0, 0);
	gaX_mixer_mix_buffer(mixer,
			src, numRead, &handleFormat,
			dstBuffer, dstSamples, &mixer->format,
			gain, pan, pitch);
	gcX_ops->freeFunc(src);
}

gc_result ga_mixer_mix(ga_Mixer *m, void *buffer) {
	gc_int32 i;
	gc_Link* link;
	gc_int32 end = m->numSamples * m->format.numChannels;
	ga_Format* fmt = &m->format;
	gc_int32 mixSampleSize = ga_format_sampleSize(&m->mixFormat);
	memset(m->mixBuffer, 0, m->numSamples * mixSampleSize);

	link = m->mixList.next;
	while (link != &m->mixList) {
		ga_Handle *h = (ga_Handle*)link->data;
		gc_Link *oldLink = link;
		link = link->next;
		gaX_mixer_mix_handle(m, h, m->numSamples);
		if (ga_handle_finished(h)) {
			gc_mutex_lock(m->mixMutex);
			gc_list_unlink(oldLink);
			gc_mutex_unlock(m->mixMutex);
		}
	}

	/* mixBuffer will already be correct bps */
	switch (fmt->bitsPerSample) {
		case 8:
			for (i = 0; i < end; ++i) {
				gc_int32 sample = m->mixBuffer[i];
				((gc_int8*)buffer)[i] = sample > -128 ? (sample < 127 ? sample : 127) : -128;
			}
			break;
		case 16:
			for (i = 0; i < end; ++i) {
			        gc_int32 sample = m->mixBuffer[i];
			        ((gc_int16*)buffer)[i] = sample > -32768 ? (sample < 32767 ? sample : 32767) : -32768;
			}
			break;
		case 32:
			memcpy(buffer, m->mixBuffer, end * 4);
			break;
	}

	return GC_SUCCESS;
}

gc_result ga_mixer_dispatch(ga_Mixer *m) {
	for (gc_Link *link = m->dispatchList.next; link != &m->dispatchList; link = link->next) {
		ga_Handle *handle = link->data;

		/* Remove finished handles and call callbacks */
		if (ga_handle_destroyed(handle)) {
			if (!handle->mixLink.next) {
				/* NOTES ABOUT THREADING POLICY WITH REGARD TO LINKED LISTS: */
				/* Only a single thread may iterate through any list */
				/* The thread that unlinks must be the only thread that iterates through the list */
				/* A single auxiliary thread may link(), but must mutex-lock to avoid link/unlink collisions */
				gc_mutex_lock(m->dispatchMutex);
				gc_list_unlink(&handle->dispatchLink);
				gc_mutex_unlock(m->dispatchMutex);
				gaX_handle_cleanup(handle);
			}
		} else if (handle->callback && ga_handle_finished(handle)) {
			handle->callback(handle, handle->context);
			handle->callback = NULL;
			handle->context = NULL;
		}
	}

	return GC_SUCCESS;
}

gc_result ga_mixer_destroy(ga_Mixer *m) {
	/* NOTE: Mixer/handles must no longer be in use on any thread when destroy is called */
	for (gc_Link *link = m->dispatchList.next; link != &m->dispatchList;) {
		ga_Handle *h = (ga_Handle*)link->data;
		link = link->next;
		gaX_handle_cleanup(h);
	}


	gc_mutex_destroy(m->dispatchMutex);
	gc_mutex_destroy(m->mixMutex);

	gcX_ops->freeFunc(m->mixBuffer);
	gcX_ops->freeFunc(m);
	return GC_SUCCESS;
}
