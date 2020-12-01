#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

/* Version Functions */
gc_bool ga_version_compatible(gc_int32 major, gc_int32 minor, gc_int32 rev) {
	return major == GA_VERSION_MAJOR && minor <= GA_VERSION_MINOR;
}

/* Format Functions */
gc_uint32 ga_format_sample_size(ga_Format *format) {
	return (format->bits_per_sample >> 3) * format->num_channels;
}

gc_float32 ga_format_to_seconds(ga_Format *format, gc_size samples) {
	return samples / (gc_float32)format->sample_rate;
}

gc_int32 ga_format_to_samples(ga_Format *format, gc_float32 seconds) {
	return seconds * format->sample_rate;
}

/* Device Functions */
ga_Device* ga_device_open(GaDeviceType *type,
                          gc_uint32 *num_buffers,
                          gc_uint32 *num_samples,
			  ga_Format *format) {
	if (!type) type = &(GaDeviceType){GaDeviceType_Default};
	if (!num_buffers) num_buffers = &(gc_uint32){4};
       	if (!num_samples) num_samples = &(gc_uint32){512};
	if (!format) format = &(ga_Format){.bits_per_sample=16, .num_channels=2, .sample_rate=44100};

	// todo allow overriding with an environment variable
	if (*type == GaDeviceType_Default) {
#define try(t) *type = t; if ((ret = ga_device_open(type, num_buffers, num_samples, format))) return ret
		ga_Device *ret;
#if defined(ENABLE_OSS)
		try(GaDeviceType_OSS);
#endif
#if defined(ENABLE_XAUDIO2)
		try(GaDeviceType_XAudio2);
#endif
#if defined(ENABLE_PULSEAUDIO)
		try(GaDeviceType_PulseAudio);
#endif
#if defined(ENABLE_ALSA)
		try(GaDeviceType_ALSA);      // put pulse before alsa, as pulseaudio has an alsa implementation
#endif
#if defined(ENABLE_OPENAL)
		try(GaDeviceType_OpenAL);    // generic (multiplatform) drivers go last
#endif
#undef try
		*type = GaDeviceType_Unknown;
		return NULL;
	}

	ga_Device *ret = gcX_ops->allocFunc(sizeof(ga_Device));
	ret->dev_type = *type;
	ret->num_buffers = *num_buffers;
	ret->num_samples = *num_samples;
	ret->format = *format;

	switch (*type) {
		case GaDeviceType_Dummy: ret->procs = gaX_deviceprocs_dummy; break;
#ifdef ENABLE_OSS
		case GaDeviceType_OSS: ret->procs = gaX_deviceprocs_OSS; break;
#endif
#ifdef ENABLE_XAUDIO2
		case GaDeviceType_XAudio2: ret->procs = gaX_deviceprocs_XAudio2; break;
#endif
#ifdef ENABLE_PULSEAUDIO
		case GaDeviceType_PulseAudio: ret->procs = gaX_deviceprocs_PulseAudio; break;
#endif
#ifdef ENABLE_ALSA
		case GaDeviceType_ALSA: ret->procs = gaX_deviceprocs_ALSA; break;
#endif
#ifdef ENABLE_OPENAL
		case GaDeviceType_OpenAL: ret->procs = gaX_deviceprocs_OpenAL; break;
#endif
		default: goto fail;
	}

	if (ret->procs.open(ret) != GC_SUCCESS) goto fail;
	*type = ret->dev_type;
	*num_buffers = ret->num_buffers;
	*num_samples = ret->num_samples;
	*format = ret->format;
	return ret;

fail:
	*type = GaDeviceType_Unknown;
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
	dataSrc->read = NULL;
	dataSrc->seek = NULL;
	dataSrc->tell = NULL;
	dataSrc->close = NULL;
	dataSrc->flags = 0;
}

gc_size ga_data_source_read(ga_DataSource *src, void *dst, gc_size size, gc_size count) {
	char* context = (char*)src + sizeof(ga_DataSource); //todo this probably doesn't work well with padding
	return src->read(context, dst, size, count);
}

gc_result ga_data_source_seek(ga_DataSource *src, gc_ssize offset, GaSeekOrigin whence) {
	char* context = (char*)src + sizeof(ga_DataSource);
	if (src->seek && (src->flags & GaDataAccessFlag_Seekable)) return src->seek(context, offset, whence);
	else return GC_ERROR_GENERIC;
}

gc_size ga_data_source_tell(ga_DataSource *dataSrc) {
	char* context = (char*)dataSrc + sizeof(ga_DataSource);
	return dataSrc->tell(context);
}

GaDataAccessFlags ga_data_source_flags(ga_DataSource *dataSrc) {
	return dataSrc->flags;
}

void gaX_data_source_destroy(ga_DataSource *dataSrc) {
	GaCbDataSource_Close func = dataSrc->close;
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
	if (gcX_decref(&dataSrc->refCount)) gaX_data_source_destroy(dataSrc);
}

/* Sample Source Structure */
void ga_sample_source_init(ga_SampleSource *src) {
	src->refCount = 1;
	src->read = NULL;
	src->end = NULL;
	src->ready = NULL;
	src->seek = NULL;
	src->tell = NULL;
	src->close = NULL;
	src->flags = 0;
}

gc_size ga_sample_source_read(ga_SampleSource *src, void *dst, gc_size num_samples, GaCbOnSeek onseek, void *seek_ctx) {
	return src->read(src, dst, num_samples, onseek, seek_ctx);
}

gc_bool ga_sample_source_end(ga_SampleSource *src) {
	return src->end(src);
}

gc_result ga_sample_source_seek(ga_SampleSource *src, gc_size sampleOffset) {
	return src->seek ? src->seek(src, sampleOffset) : GC_ERROR_GENERIC;
}

gc_result ga_sample_source_tell(ga_SampleSource *src, gc_size *samples, gc_size *totalSamples) {
	if (!src->tell) return GC_ERROR_GENERIC;

	return src->tell(src, samples, totalSamples);
}

GaDataAccessFlags ga_sample_source_flags(ga_SampleSource *src) {
	return src->flags;
}

void ga_sample_source_format(ga_SampleSource *src, ga_Format *format) {
	*format = src->format;
}

void gaX_sample_source_destroy(ga_SampleSource *src) {
	if (src->close) src->close(src);
	gcX_ops->freeFunc(src);
}

void ga_sample_source_acquire(ga_SampleSource *src) {
	atomic_fetch_add(&src->refCount, 1);
}

void ga_sample_source_release(ga_SampleSource *src) {
	if (gcX_decref(&src->refCount)) gaX_sample_source_destroy(src);
}

gc_bool ga_sample_source_ready(ga_SampleSource *src, gc_size num_samples) {
	return src->ready(src, num_samples);
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

ga_Memory *ga_memory_create_data_source(ga_DataSource *src) {
	enum { BUFSZ = 4096 };
	char *data;
	gc_size len;

	if (ga_data_source_flags(src) & GaDataAccessFlag_Seekable) {
		gc_size where = ga_data_source_tell(src);
		if (ga_data_source_seek(src, 0, GaSeekOrigin_End) != GC_SUCCESS)
			return NULL; //GC_ERR_INTERNAL
		gc_size tlen = ga_data_source_tell(src);
		if (where > tlen || ga_data_source_seek(src, where, GaSeekOrigin_Set) != GC_SUCCESS)
			return NULL; //ditto
		len = tlen - where;
		data = gcX_ops->allocFunc(len);
		if (!data) return NULL; //GC_ERR_MEMORY
		gc_size read = ga_data_source_read(src, data, 1, len);
		if (read != len) {
			gcX_ops->freeFunc(data);
			return NULL;
		}
	} else {
		len = 0;
		gc_size bytes_read = 0;
		data = NULL;
		do {
			data = gcX_ops->reallocFunc(data, len + BUFSZ);
			bytes_read = ga_data_source_read(src, data + len, 1, BUFSZ);
			if (bytes_read < BUFSZ)
				data = gcX_ops->reallocFunc(data, len + bytes_read);
			len += bytes_read;
		} while (bytes_read > 0);
	}

	ga_Memory *ret = gaX_memory_create(data, len, gc_false);
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
	if (gcX_decref(&mem->refCount)) gaX_memory_destroy(mem);
}


/* Sound Functions */
ga_Sound *ga_sound_create(ga_Memory *memory, ga_Format *format) {
	ga_Sound *ret = gcX_ops->allocFunc(sizeof(ga_Sound));
	assert(ga_memory_size(memory) % ga_format_sample_size(format) == 0);
	ret->num_samples = ga_memory_size(memory) / ga_format_sample_size(format);
	ret->format = *format;
	ga_memory_acquire(memory);
	ret->memory = memory;
	ret->refCount = 1;
	return (ga_Sound*)ret;
}

ga_Sound *ga_sound_create_sample_source(ga_SampleSource *src) {
	ga_Sound* ret = 0;
	ga_Format format;
	gc_size total_samples;
	gc_uint32 sample_size;
	ga_sample_source_format(src, &format);
	sample_size = ga_format_sample_size(&format);
	gc_result told = ga_sample_source_tell(src, NULL, &total_samples);

	/* Known total samples*/
	if (told == GC_SUCCESS) {
		char* data;
		ga_Memory* memory;
		gc_size data_size = sample_size * total_samples;
		data = gcX_ops->allocFunc(data_size);
		ga_sample_source_read(src, data, total_samples, 0, 0);
		memory = gaX_memory_create(data, data_size, 0);
		if (memory) {
			ret = ga_sound_create(memory, &format);
			if (!ret) ga_memory_release(memory);
		} else {
			gcX_ops->freeFunc(data);
		}
	/* Unknown total samples */
	} else {
		gc_int32 BUFFER_SAMPLES = format.sample_rate * 2;
		char* data = 0;
		ga_Memory* memory;
		total_samples = 0;
		while (!ga_sample_source_end(src)) {
			gc_int32 num_samples_read;
			data = gcX_ops->reallocFunc(data, (total_samples + BUFFER_SAMPLES) * sample_size);
			num_samples_read = ga_sample_source_read(src, data + (total_samples * sample_size), BUFFER_SAMPLES, 0, 0);
			if (num_samples_read < BUFFER_SAMPLES) {
				data = gcX_ops->reallocFunc(data, (total_samples + num_samples_read) * sample_size);
			}
			total_samples += num_samples_read;
		}
		memory = gaX_memory_create(data, total_samples * sample_size, 0);
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

gc_size ga_sound_num_samples(ga_Sound *sound) {
	return ga_memory_size(sound->memory) / ga_format_sample_size(&sound->format);
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
	if (gcX_decref(&sound->refCount)) gaX_sound_destroy(sound);
}

/* Handle Functions */
void gaX_handle_init(ga_Handle *handle, ga_Mixer *mixer) {
	handle->state = GA_HANDLE_STATE_INITIAL;
	handle->mixer = mixer;
	handle->callback = 0;
	handle->context = 0;
	handle->gain = 1.0;
	handle->pitch = 1.0;
	handle->pan = 0.0;
	handle->mutex = gc_mutex_create();
}

ga_Handle *ga_handle_create(ga_Mixer *mixer, ga_SampleSource *src) {
	ga_Handle* h = gcX_ops->allocFunc(sizeof(ga_Handle));
	ga_sample_source_acquire(src);
	h->sample_src = src;
	h->finished = 0;
	gaX_handle_init(h, mixer);

	gc_mutex_lock(mixer->mix_mutex);
	gc_list_link(&mixer->mix_list, &h->mix_link, h);
	gc_mutex_unlock(mixer->mix_mutex);

	gc_mutex_lock(mixer->dispatch_mutex);
	gc_list_link(&mixer->dispatch_list, &h->dispatch_link, h);
	gc_mutex_unlock(mixer->dispatch_mutex);

	return h;
}

gc_result ga_handle_destroy(ga_Handle *handle) {
	/* Sets the destroyed state. Will be cleaned up once all threads ACK. */
	gc_mutex_lock(handle->mutex);
	handle->state = GA_HANDLE_STATE_DESTROYED;
	gc_mutex_unlock(handle->mutex);
	return GC_SUCCESS;
}

gc_result gaX_handle_cleanup(ga_Handle *handle) {
	/* May only be called from the dispatch thread */
	ga_sample_source_release(handle->sample_src);
	gc_mutex_destroy(handle->mutex);
	gcX_ops->freeFunc(handle);
	return GC_SUCCESS;
}

gc_result ga_handle_play(ga_Handle *handle) {
	gc_mutex_lock(handle->mutex);
	if (handle->state >= GA_HANDLE_STATE_FINISHED) {
		gc_mutex_unlock(handle->mutex);
		return GC_ERROR_GENERIC;
	}
	handle->state = GA_HANDLE_STATE_PLAYING;
	gc_mutex_unlock(handle->mutex);
	return GC_SUCCESS;
}

gc_result ga_handle_stop(ga_Handle *handle) {
	gc_mutex_lock(handle->mutex);
	if (handle->state >= GA_HANDLE_STATE_FINISHED) {
		gc_mutex_unlock(handle->mutex);
		return GC_ERROR_GENERIC;
	}
	handle->state = GA_HANDLE_STATE_STOPPED;
	gc_mutex_unlock(handle->mutex);
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
			gc_mutex_lock(handle->mutex);
			handle->gain = value;
			gc_mutex_unlock(handle->mutex);
			return GC_SUCCESS;
		case GA_HANDLE_PARAM_PAN:
			gc_mutex_lock(handle->mutex);
			handle->pan = value;
			gc_mutex_unlock(handle->mutex);
			return GC_SUCCESS;
		case GA_HANDLE_PARAM_PITCH:
			gc_mutex_lock(handle->mutex);
			handle->pitch = value;
			gc_mutex_unlock(handle->mutex);
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
	   gc_mutex_lock(handle->mutex);
	   gc_mutex_unlock(handle->mutex);
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
	ga_sample_source_seek(handle->sample_src, sampleOffset);
	return GC_SUCCESS;
}

gc_int32 ga_handle_tell(ga_Handle *handle, gc_int32 param) {
	gc_result res;
	gc_size ret;
	if (param == GA_TELL_PARAM_CURRENT) res = ga_sample_source_tell(handle->sample_src, &ret, NULL);
	else if (param == GA_TELL_PARAM_TOTAL) res = ga_sample_source_tell(handle->sample_src, NULL, &ret);
	else return -1;
	if (res != GC_SUCCESS) return -1;
	return ret;
}

gc_bool ga_handle_ready(ga_Handle *handle, gc_int32 num_samples) {
	return ga_sample_source_ready(handle->sample_src, num_samples);
}

void ga_handle_format(ga_Handle *handle, ga_Format *format) {
	ga_sample_source_format(handle->sample_src, format);
}

/* Mixer Functions */
ga_Mixer *ga_mixer_create(ga_Format *format, gc_int32 num_samples) {
	ga_Mixer *ret = gcX_ops->allocFunc(sizeof(ga_Mixer));
	gc_list_head(&ret->dispatch_list);
	gc_list_head(&ret->mix_list);
	ret->num_samples = num_samples;
	ret->format = *format;
	ret->mix_format.bits_per_sample = 32;
	ret->mix_format.num_channels = format->num_channels;
	ret->mix_format.sample_rate = format->sample_rate;
	ret->mix_buffer = gcX_ops->allocFunc(num_samples * ga_format_sample_size(&ret->mix_format));
	ret->dispatch_mutex = gc_mutex_create();
	ret->mix_mutex = gc_mutex_create();
	return ret;
}

ga_Format *ga_mixer_format(ga_Mixer *mixer) {
	return &mixer->format;
}

gc_int32 ga_mixer_num_samples(ga_Mixer *mixer) {
	return mixer->num_samples;
}

void gaX_mixer_mix_buffer(ga_Mixer *mixer,
                          void *srcBuffer, gc_int32 srcSamples, ga_Format *srcFmt,
                          gc_int32 *dst, gc_int32 dstSamples, ga_Format *dstFmt,
			  gc_float32 gain, gc_float32 pan, gc_float32 pitch) {
	gc_int32 mixerChannels = dstFmt->num_channels;
	gc_int32 srcChannels = srcFmt->num_channels;
	gc_float32 sampleScale = srcFmt->sample_rate / (gc_float32)dstFmt->sample_rate * pitch;
	gc_float32 fj = 0.0f;
	gc_int32 j = 0;
	gc_int32 i = 0;
	gc_float32 srcSamplesRead = 0.0f;
	gc_uint32 sample_size = ga_format_sample_size(srcFmt);
	pan = (pan + 1.0f) / 2.0f;
	pan = pan > 1.0f ? 1.0f : pan;
	pan = pan < 0.0f ? 0.0f : pan;

	/* TODO: Support 8-bit/16-bit mono/stereo mixer format */
	switch (srcFmt->bits_per_sample) {
		case 16: {
			gc_int32 srcBytes = srcSamples * sample_size;
			const gc_int16 *src = srcBuffer;
			while (i < dstSamples * (gc_int32)mixerChannels && srcBytes >= 2 * srcChannels) {
				gc_int32 newJ, deltaSrcBytes;

				dst[i] += (gc_int32)((gc_int32)src[j] * gain * (1.0f - pan) * 2);
				dst[i + 1] += (gc_int32)((gc_int32)src[j + ((srcChannels == 1) ? 0 : 1)] * gain * pan * 2);

				i += mixerChannels;
				fj += sampleScale * srcChannels;
				srcSamplesRead += sampleScale * srcChannels;
				newJ = (gc_uint32)fj & (srcChannels == 1 ? ~0u : ~0x1u);
				deltaSrcBytes = (newJ - j) * 2;
				j = newJ;
				srcBytes -= deltaSrcBytes;
			}

			break;
		}
	}
}

void gaX_mixer_mix_handle(ga_Mixer *mixer, ga_Handle *handle, gc_size num_samples) {
	ga_SampleSource* ss = handle->sample_src;
	if (ga_sample_source_end(ss)) {
		/* Stream is finished! */
		gc_mutex_lock(handle->mutex);
		if (handle->state < GA_HANDLE_STATE_FINISHED)
			handle->state = GA_HANDLE_STATE_FINISHED;
		gc_mutex_unlock(handle->mutex);
		return;
	}
	if (handle->state != GA_HANDLE_STATE_PLAYING) return;
	ga_Format handleFormat;
	ga_sample_source_format(ss, &handleFormat);
	/* Check if we have enough samples to stream a full buffer */
	gc_uint32 srcSampleSize = ga_format_sample_size(&handleFormat);
	gc_float32 oldPitch = handle->pitch;
	gc_float32 dstToSrc = handleFormat.sample_rate / (gc_float32)mixer->format.sample_rate * oldPitch;
	gc_size requested = num_samples * dstToSrc;
	requested = requested / dstToSrc < num_samples ? requested + 1 : requested;

	if (requested <= 0 || !ga_sample_source_ready(ss, requested)) return;

	gc_mutex_lock(handle->mutex);
	gc_float32 gain = handle->gain;
	gc_float32 pan = handle->pan;
	gc_float32 pitch = handle->pitch;
	gc_mutex_unlock(handle->mutex);

	/* We avoided a mutex lock by using pitch to check if buffer has enough dst samples */
	/* If it has changed since then, we re-test to make sure we still have enough samples */
	if (oldPitch != pitch) {
		dstToSrc = handleFormat.sample_rate / (gc_float32)mixer->format.sample_rate * pitch;
		requested = (gc_int32)(num_samples * dstToSrc);
		requested = requested / dstToSrc < num_samples ? requested + 1 : requested;
		if (requested <= 0 || !ga_sample_source_ready(ss, requested)) return;
	}

	/* TODO: To optimize, we can refactor the _read() interface to be _mix(), avoiding this malloc/copy */
	void *src = gcX_ops->allocFunc(requested * srcSampleSize);
	gc_int32 numRead = 0;
	numRead = ga_sample_source_read(ss, src, requested, 0, 0);
	gaX_mixer_mix_buffer(mixer,
			src, numRead, &handleFormat,
			mixer->mix_buffer, num_samples, &mixer->format,
			gain, pan, pitch);
	gcX_ops->freeFunc(src);
}

gc_result ga_mixer_mix(ga_Mixer *m, void *buffer) {
	gc_Link* link;
	gc_size end = m->num_samples * m->format.num_channels;
	ga_Format* fmt = &m->format;
	memset(m->mix_buffer, 0, m->num_samples * ga_format_sample_size(&m->mix_format));

	link = m->mix_list.next;
	while (link != &m->mix_list) {
		ga_Handle *h = (ga_Handle*)link->data;
		gc_Link *old_link = link;
		link = link->next;
		gaX_mixer_mix_handle(m, h, m->num_samples);
		if (ga_handle_finished(h)) {
			gc_mutex_lock(m->mix_mutex);
			gc_list_unlink(old_link);
			gc_mutex_unlock(m->mix_mutex);
		}
	}

	/* mix_buffer will already be correct bps */
	switch (fmt->bits_per_sample) {
		case 8:
			for (gc_size i = 0; i < end; ++i) {
				gc_int32 sample = m->mix_buffer[i];
				((gc_int8*)buffer)[i] = sample > -128 ? (sample < 127 ? sample : 127) : -128;
			}
			break;
		case 16:
			for (gc_size i = 0; i < end; ++i) {
			        gc_int32 sample = m->mix_buffer[i];
			        ((gc_int16*)buffer)[i] = sample > -32768 ? (sample < 32767 ? sample : 32767) : -32768;
			}
			break;
		case 32:
			memcpy(buffer, m->mix_buffer, end * 4);
			break;
	}

	return GC_SUCCESS;
}

gc_result ga_mixer_dispatch(ga_Mixer *m) {
	for (gc_Link *link = m->dispatch_list.next; link != &m->dispatch_list; link = link->next) {
		ga_Handle *handle = link->data;

		/* Remove finished handles and call callbacks */
		if (ga_handle_destroyed(handle)) {
			if (!handle->mix_link.next) {
				/* NOTES ABOUT THREADING POLICY WITH REGARD TO LINKED LISTS: */
				/* Only a single thread may iterate through any list */
				/* The thread that unlinks must be the only thread that iterates through the list */
				/* A single auxiliary thread may link(), but must mutex-lock to avoid link/unlink collisions */
				gc_mutex_lock(m->dispatch_mutex);
				gc_list_unlink(&handle->dispatch_link);
				gc_mutex_unlock(m->dispatch_mutex);
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
	for (gc_Link *link = m->dispatch_list.next; link != &m->dispatch_list;) {
		ga_Handle *h = (ga_Handle*)link->data;
		link = link->next;
		gaX_handle_cleanup(h);
	}


	gc_mutex_destroy(m->dispatch_mutex);
	gc_mutex_destroy(m->mix_mutex);

	gcX_ops->freeFunc(m->mix_buffer);
	gcX_ops->freeFunc(m);
	return GC_SUCCESS;
}
