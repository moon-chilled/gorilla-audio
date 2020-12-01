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
gc_uint32 ga_format_sample_size(GaFormat *format) {
	return (format->bits_per_sample >> 3) * format->num_channels;
}

gc_float32 ga_format_to_seconds(GaFormat *format, gc_size samples) {
	return samples / (gc_float32)format->sample_rate;
}

gc_int32 ga_format_to_samples(GaFormat *format, gc_float32 seconds) {
	return seconds * format->sample_rate;
}

/* Device Functions */
GaDevice* ga_device_open(GaDeviceType *type,
                          gc_uint32 *num_buffers,
                          gc_uint32 *num_samples,
			  GaFormat *format) {
	if (!type) type = &(GaDeviceType){GaDeviceType_Default};
	if (!num_buffers) num_buffers = &(gc_uint32){4};
       	if (!num_samples) num_samples = &(gc_uint32){512};
	if (!format) format = &(GaFormat){.bits_per_sample=16, .num_channels=2, .sample_rate=44100};

	// todo allow overriding with an environment variable
	if (*type == GaDeviceType_Default) {
#define try(t) *type = t; if ((ret = ga_device_open(type, num_buffers, num_samples, format))) return ret
		GaDevice *ret;
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

	GaDevice *ret = gcX_ops->allocFunc(sizeof(GaDevice));
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

	if (ret->procs.open(ret) != GA_OK) goto fail;
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

ga_result ga_device_close(GaDevice *device) {
	ga_result ret = device->procs.close ? device->procs.close(device) : GA_ERR_GENERIC;
	free(device);
	return ret;
}

gc_int32 ga_device_check(GaDevice *device) {
	return device->procs.check ? device->procs.check(device) : GA_ERR_GENERIC;
}

ga_result ga_device_queue(GaDevice *device, void *buffer) {
	return device->procs.queue ? device->procs.queue(device, buffer) : GA_ERR_GENERIC;
}

/* Data Source Structure */
void ga_data_source_init(GaDataSource *dataSrc) {
	dataSrc->refCount = 1;
	dataSrc->read = NULL;
	dataSrc->seek = NULL;
	dataSrc->tell = NULL;
	dataSrc->close = NULL;
	dataSrc->flags = 0;
}

gc_size ga_data_source_read(GaDataSource *src, void *dst, gc_size size, gc_size count) {
	char* context = (char*)src + sizeof(GaDataSource); //todo this probably doesn't work well with padding
	return src->read(context, dst, size, count);
}

ga_result ga_data_source_seek(GaDataSource *src, gc_ssize offset, GaSeekOrigin whence) {
	char* context = (char*)src + sizeof(GaDataSource);
	if (src->seek && (src->flags & GaDataAccessFlag_Seekable)) return src->seek(context, offset, whence);
	else return GA_ERR_GENERIC;
}

gc_size ga_data_source_tell(GaDataSource *dataSrc) {
	char* context = (char*)dataSrc + sizeof(GaDataSource);
	return dataSrc->tell(context);
}

GaDataAccessFlags ga_data_source_flags(GaDataSource *dataSrc) {
	return dataSrc->flags;
}

void gaX_data_source_destroy(GaDataSource *dataSrc) {
	GaCbDataSource_Close func = dataSrc->close;
	char *context = (char*)dataSrc + sizeof(GaDataSource);
	assert(dataSrc->refCount == 0);
	if(func)
		func(context);
	gcX_ops->freeFunc(dataSrc);
}

void ga_data_source_acquire(GaDataSource *dataSrc) {
	atomic_fetch_add(&dataSrc->refCount, 1);
}

void ga_data_source_release(GaDataSource *dataSrc) {
	if (gcX_decref(&dataSrc->refCount)) gaX_data_source_destroy(dataSrc);
}

/* Sample Source Structure */
void ga_sample_source_init(GaSampleSource *src) {
	src->refCount = 1;
	src->read = NULL;
	src->end = NULL;
	src->ready = NULL;
	src->seek = NULL;
	src->tell = NULL;
	src->close = NULL;
	src->flags = 0;
}

gc_size ga_sample_source_read(GaSampleSource *src, void *dst, gc_size num_samples, GaCbOnSeek onseek, void *seek_ctx) {
	return src->read(src, dst, num_samples, onseek, seek_ctx);
}

gc_bool ga_sample_source_end(GaSampleSource *src) {
	return src->end(src);
}

ga_result ga_sample_source_seek(GaSampleSource *src, gc_size sampleOffset) {
	return src->seek ? src->seek(src, sampleOffset) : GA_ERR_GENERIC;
}

ga_result ga_sample_source_tell(GaSampleSource *src, gc_size *samples, gc_size *totalSamples) {
	if (!src->tell) return GA_ERR_GENERIC;

	return src->tell(src, samples, totalSamples);
}

GaDataAccessFlags ga_sample_source_flags(GaSampleSource *src) {
	return src->flags;
}

void ga_sample_source_format(GaSampleSource *src, GaFormat *format) {
	*format = src->format;
}

void gaX_sample_source_destroy(GaSampleSource *src) {
	if (src->close) src->close(src);
	gcX_ops->freeFunc(src);
}

void ga_sample_source_acquire(GaSampleSource *src) {
	atomic_fetch_add(&src->refCount, 1);
}

void ga_sample_source_release(GaSampleSource *src) {
	if (gcX_decref(&src->refCount)) gaX_sample_source_destroy(src);
}

gc_bool ga_sample_source_ready(GaSampleSource *src, gc_size num_samples) {
	return src->ready(src, num_samples);
}

/* Memory Functions */
static GaMemory *gaX_memory_create(void *data, gc_size size, gc_bool copy) {
	GaMemory *ret = gcX_ops->allocFunc(sizeof(GaMemory));
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

GaMemory *ga_memory_create(void *data, gc_size size) {
	return gaX_memory_create(data, size, gc_true);
}

GaMemory *ga_memory_create_data_source(GaDataSource *src) {
	enum { BUFSZ = 4096 };
	char *data;
	gc_size len;

	if (ga_data_source_flags(src) & GaDataAccessFlag_Seekable) {
		gc_size where = ga_data_source_tell(src);
		if (ga_data_source_seek(src, 0, GaSeekOrigin_End) != GA_OK)
			return NULL; //GC_ERR_INTERNAL
		gc_size tlen = ga_data_source_tell(src);
		if (where > tlen || ga_data_source_seek(src, where, GaSeekOrigin_Set) != GA_OK)
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

	GaMemory *ret = gaX_memory_create(data, len, gc_false);
	if (!ret) gcX_ops->freeFunc(data);
	return ret;
}

gc_size ga_memory_size(GaMemory *mem) {
	return mem->size;
}

void *ga_memory_data(GaMemory *mem) {
	return mem->data;
}

static void gaX_memory_destroy(GaMemory *mem) {
	gcX_ops->freeFunc(mem->data);
	gcX_ops->freeFunc(mem);
}

void ga_memory_acquire(GaMemory *mem) {
	atomic_fetch_add(&mem->refCount, 1);
}

void ga_memory_release(GaMemory *mem) {
	if (gcX_decref(&mem->refCount)) gaX_memory_destroy(mem);
}


/* Sound Functions */
GaSound *ga_sound_create(GaMemory *memory, GaFormat *format) {
	GaSound *ret = gcX_ops->allocFunc(sizeof(GaSound));
	assert(ga_memory_size(memory) % ga_format_sample_size(format) == 0);
	ret->num_samples = ga_memory_size(memory) / ga_format_sample_size(format);
	ret->format = *format;
	ga_memory_acquire(memory);
	ret->memory = memory;
	ret->refCount = 1;
	return (GaSound*)ret;
}

GaSound *ga_sound_create_sample_source(GaSampleSource *src) {
	GaSound* ret = 0;
	GaFormat format;
	gc_size total_samples;
	gc_uint32 sample_size;
	ga_sample_source_format(src, &format);
	sample_size = ga_format_sample_size(&format);
	ga_result told = ga_sample_source_tell(src, NULL, &total_samples);

	/* Known total samples*/
	if (told == GA_OK) {
		char* data;
		GaMemory* memory;
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
		GaMemory* memory;
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

void *ga_sound_data(GaSound *sound) {
	return ga_memory_data(sound->memory);
}

gc_size ga_sound_size(GaSound *sound) {
	return ga_memory_size(sound->memory);
}

gc_size ga_sound_num_samples(GaSound *sound) {
	return ga_memory_size(sound->memory) / ga_format_sample_size(&sound->format);
}

void ga_sound_format(GaSound *sound, GaFormat *format) {
	*format = sound->format;
}

static void gaX_sound_destroy(GaSound *sound) {
	ga_memory_release(sound->memory);
	gcX_ops->freeFunc(sound);
}

void ga_sound_acquire(GaSound *sound) {
	atomic_fetch_add(&sound->refCount, 1);
}

void ga_sound_release(GaSound *sound) {
	if (gcX_decref(&sound->refCount)) gaX_sound_destroy(sound);
}

/* Handle Functions */
void gaX_handle_init(GaHandle *handle, GaMixer *mixer) {
	handle->state = GaHandleState_Initial;
	handle->mixer = mixer;
	handle->callback = 0;
	handle->context = 0;
	handle->gain = 1.0;
	handle->pitch = 1.0;
	handle->pan = 0.0;
	handle->mutex = ga_mutex_create();
}

GaHandle *ga_handle_create(GaMixer *mixer, GaSampleSource *src) {
	GaHandle* h = gcX_ops->allocFunc(sizeof(GaHandle));
	ga_sample_source_acquire(src);
	h->sample_src = src;
	h->finished = 0;
	gaX_handle_init(h, mixer);

	ga_mutex_lock(mixer->mix_mutex);
	ga_list_link(&mixer->mix_list, &h->mix_link, h);
	ga_mutex_unlock(mixer->mix_mutex);

	ga_mutex_lock(mixer->dispatch_mutex);
	ga_list_link(&mixer->dispatch_list, &h->dispatch_link, h);
	ga_mutex_unlock(mixer->dispatch_mutex);

	return h;
}

ga_result ga_handle_destroy(GaHandle *handle) {
	/* Sets the destroyed state. Will be cleaned up once all threads ACK. */
	ga_mutex_lock(handle->mutex);
	handle->state = GaHandleState_Destroyed;
	ga_mutex_unlock(handle->mutex);
	return GA_OK;
}

ga_result gaX_handle_cleanup(GaHandle *handle) {
	/* May only be called from the dispatch thread */
	ga_sample_source_release(handle->sample_src);
	ga_mutex_destroy(handle->mutex);
	gcX_ops->freeFunc(handle);
	return GA_OK;
}

ga_result ga_handle_play(GaHandle *handle) {
	ga_mutex_lock(handle->mutex);
	if (handle->state >= GaHandleState_Finished) {
		ga_mutex_unlock(handle->mutex);
		return GA_ERR_GENERIC;
	}
	handle->state = GaHandleState_Playing;
	ga_mutex_unlock(handle->mutex);
	return GA_OK;
}

ga_result ga_handle_stop(GaHandle *handle) {
	ga_mutex_lock(handle->mutex);
	if (handle->state >= GaHandleState_Finished) {
		ga_mutex_unlock(handle->mutex);
		return GA_ERR_GENERIC;
	}
	handle->state = GaHandleState_Stopped;
	ga_mutex_unlock(handle->mutex);
	return GA_OK;
}

gc_bool ga_handle_playing(GaHandle *handle) {
	return handle->state == GaHandleState_Playing;
}
gc_bool ga_handle_stopped(GaHandle *handle) {
	return handle->state == GaHandleState_Stopped;
}
gc_bool ga_handle_finished(GaHandle *handle) {
	return handle->state >= GaHandleState_Finished;
}
gc_bool ga_handle_destroyed(GaHandle *handle) {
	return handle->state >= GaHandleState_Destroyed;
}

ga_result ga_handle_setCallback(GaHandle *handle, ga_FinishCallback callback, void *context) {
	/* Does not need mutex because it can only be called from the dispatch thread */
	handle->callback = callback;
	handle->context = context;
	return GA_OK;
}

ga_result ga_handle_setParamf(GaHandle *handle, GaHandleParam param, gc_float32 value) {
	switch (param) {
		case GaHandleParam_Gain:
			ga_mutex_lock(handle->mutex);
			handle->gain = value;
			ga_mutex_unlock(handle->mutex);
			return GA_OK;
		case GaHandleParam_Pan:
			ga_mutex_lock(handle->mutex);
			handle->pan = value;
			ga_mutex_unlock(handle->mutex);
			return GA_OK;
		case GaHandleParam_Pitch:
			ga_mutex_lock(handle->mutex);
			handle->pitch = value;
			ga_mutex_unlock(handle->mutex);
			return GA_OK;
		default: return GA_ERR_GENERIC;
	}
}

ga_result ga_handle_getParamf(GaHandle *handle, GaHandleParam param, gc_float32 *value) {
	switch (param) {
		case GaHandleParam_Gain:  *value = handle->gain;  return GA_OK;
		case GaHandleParam_Pan:   *value = handle->pan;   return GA_OK;
		case GaHandleParam_Pitch: *value = handle->pitch; return GA_OK;
		default: return GA_ERR_GENERIC;
	}
}

ga_result ga_handle_setParami(GaHandle *handle, GaHandleParam param, gc_int32 value) {
	/*
	   switch(param)
	   {
	   case GaHandleParam_?:
	   ga_mutex_lock(handle->mutex);
	   ga_mutex_unlock(handle->mutex);
	   return GA_OK;
	   }
	   */
	return GA_ERR_GENERIC;
}

ga_result ga_handle_getParami(GaHandle *handle, GaHandleParam param, gc_int32 *value) {
	/*
	   switch(param)
	   {
	   case GaHandleParam_?: *value = ?; return GA_OK;
	   }
	   */
	return GA_ERR_GENERIC;
}

ga_result ga_handle_seek(GaHandle *handle, gc_int32 sampleOffset) {
	ga_sample_source_seek(handle->sample_src, sampleOffset);
	return GA_OK;
}

gc_int32 ga_handle_tell(GaHandle *handle, GaTellParam param) {
	ga_result res;
	gc_size ret;
	if (param == GaTellParam_Current) res = ga_sample_source_tell(handle->sample_src, &ret, NULL);
	else if (param == GaTellParam_Total) res = ga_sample_source_tell(handle->sample_src, NULL, &ret);
	else return -1;
	if (res != GA_OK) return -1;
	return ret;
}

gc_bool ga_handle_ready(GaHandle *handle, gc_int32 num_samples) {
	return ga_sample_source_ready(handle->sample_src, num_samples);
}

void ga_handle_format(GaHandle *handle, GaFormat *format) {
	ga_sample_source_format(handle->sample_src, format);
}

/* Mixer Functions */
GaMixer *ga_mixer_create(GaFormat *format, gc_int32 num_samples) {
	GaMixer *ret = gcX_ops->allocFunc(sizeof(GaMixer));
	ga_list_head(&ret->dispatch_list);
	ga_list_head(&ret->mix_list);
	ret->num_samples = num_samples;
	ret->format = *format;
	ret->mix_format.bits_per_sample = 32;
	ret->mix_format.num_channels = format->num_channels;
	ret->mix_format.sample_rate = format->sample_rate;
	ret->mix_buffer = gcX_ops->allocFunc(num_samples * ga_format_sample_size(&ret->mix_format));
	ret->dispatch_mutex = ga_mutex_create();
	ret->mix_mutex = ga_mutex_create();
	return ret;
}

GaFormat *ga_mixer_format(GaMixer *mixer) {
	return &mixer->format;
}

gc_int32 ga_mixer_num_samples(GaMixer *mixer) {
	return mixer->num_samples;
}

void gaX_mixer_mix_buffer(GaMixer *mixer,
                          void *srcBuffer, gc_int32 srcSamples, GaFormat *srcFmt,
                          gc_int32 *dst, gc_int32 dstSamples, GaFormat *dstFmt,
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

void gaX_mixer_mix_handle(GaMixer *mixer, GaHandle *handle, gc_size num_samples) {
	GaSampleSource* ss = handle->sample_src;
	if (ga_sample_source_end(ss)) {
		/* Stream is finished! */
		ga_mutex_lock(handle->mutex);
		if (handle->state < GaHandleState_Finished)
			handle->state = GaHandleState_Finished;
		ga_mutex_unlock(handle->mutex);
		return;
	}
	if (handle->state != GaHandleState_Playing) return;
	GaFormat handleFormat;
	ga_sample_source_format(ss, &handleFormat);
	/* Check if we have enough samples to stream a full buffer */
	gc_uint32 srcSampleSize = ga_format_sample_size(&handleFormat);
	gc_float32 oldPitch = handle->pitch;
	gc_float32 dstToSrc = handleFormat.sample_rate / (gc_float32)mixer->format.sample_rate * oldPitch;
	gc_size requested = num_samples * dstToSrc;
	requested = requested / dstToSrc < num_samples ? requested + 1 : requested;

	if (requested <= 0 || !ga_sample_source_ready(ss, requested)) return;

	ga_mutex_lock(handle->mutex);
	gc_float32 gain = handle->gain;
	gc_float32 pan = handle->pan;
	gc_float32 pitch = handle->pitch;
	ga_mutex_unlock(handle->mutex);

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

ga_result ga_mixer_mix(GaMixer *m, void *buffer) {
	GaLink* link;
	gc_size end = m->num_samples * m->format.num_channels;
	GaFormat* fmt = &m->format;
	memset(m->mix_buffer, 0, m->num_samples * ga_format_sample_size(&m->mix_format));

	link = m->mix_list.next;
	while (link != &m->mix_list) {
		GaHandle *h = (GaHandle*)link->data;
		GaLink *old_link = link;
		link = link->next;
		gaX_mixer_mix_handle(m, h, m->num_samples);
		if (ga_handle_finished(h)) {
			ga_mutex_lock(m->mix_mutex);
			ga_list_unlink(old_link);
			ga_mutex_unlock(m->mix_mutex);
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

	return GA_OK;
}

ga_result ga_mixer_dispatch(GaMixer *m) {
	for (GaLink *link = m->dispatch_list.next; link != &m->dispatch_list; link = link->next) {
		GaHandle *handle = link->data;

		/* Remove finished handles and call callbacks */
		if (ga_handle_destroyed(handle)) {
			if (!handle->mix_link.next) {
				/* NOTES ABOUT THREADING POLICY WITH REGARD TO LINKED LISTS: */
				/* Only a single thread may iterate through any list */
				/* The thread that unlinks must be the only thread that iterates through the list */
				/* A single auxiliary thread may link(), but must mutex-lock to avoid link/unlink collisions */
				ga_mutex_lock(m->dispatch_mutex);
				ga_list_unlink(&handle->dispatch_link);
				ga_mutex_unlock(m->dispatch_mutex);
				gaX_handle_cleanup(handle);
			}
		} else if (handle->callback && ga_handle_finished(handle)) {
			handle->callback(handle, handle->context);
			handle->callback = NULL;
			handle->context = NULL;
		}
	}

	return GA_OK;
}

ga_result ga_mixer_destroy(GaMixer *m) {
	/* NOTE: Mixer/handles must no longer be in use on any thread when destroy is called */
	for (GaLink *link = m->dispatch_list.next; link != &m->dispatch_list;) {
		GaHandle *h = (GaHandle*)link->data;
		link = link->next;
		gaX_handle_cleanup(h);
	}


	ga_mutex_destroy(m->dispatch_mutex);
	ga_mutex_destroy(m->mix_mutex);

	gcX_ops->freeFunc(m->mix_buffer);
	gcX_ops->freeFunc(m);
	return GA_OK;
}
