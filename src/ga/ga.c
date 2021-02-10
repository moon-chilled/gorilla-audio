#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

/* Version Functions */
bool ga_version_compatible(s32 major, s32 minor, s32 rev) {
	return major == GA_VERSION_MAJOR
	   // until 1.0, minor version changes are breaking
	   && (major != 0 || minor == GA_VERSION_MINOR)
	   && (minor <= GA_VERSION_MINOR);
}

/* Format Functions */
u32 ga_format_sample_size(GaFormat *format) {
	return (format->bits_per_sample >> 3) * format->num_channels;
}

f32 ga_format_to_seconds(GaFormat *format, usz samples) {
	return samples / (f32)format->sample_rate;
}

s32 ga_format_to_samples(GaFormat *format, f32 seconds) {
	return seconds * format->sample_rate;
}

/* Device Functions */
GaDevice *ga_device_open(GaDeviceType *type,
                          u32 *pnum_buffers,
                          u32 *pnum_samples,
			  GaFormat *pformat) {
	type = type ? type : &(GaDeviceType){GaDeviceType_Default};

	// todo allow overriding with an environment variable
	if (*type == GaDeviceType_Default) {
#define try(t) *type = t; if ((ret = ga_device_open(type, pnum_buffers, pnum_samples, pformat))) return ret
		GaDevice *ret;
#if defined(ENABLE_OSS)
		try(GaDeviceType_OSS);
#endif
#if defined(ENABLE_XAUDIO2)
		try(GaDeviceType_XAudio2);
#endif
#if defined(ENABLE_ARCAN)
		try(GaDeviceType_Arcan);
#endif
#if defined(ENABLE_SNDIO)
		try(GaDeviceType_Sndio);
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

	u32 num_buffers = pnum_buffers ? *pnum_buffers : 4;
	u32 num_samples = pnum_samples ? *pnum_samples : 512;
	GaFormat format = pformat ? *pformat : (GaFormat){.bits_per_sample=16, .num_channels=2, .sample_rate=48000};

	GaDevice *ret = ga_alloc(sizeof(GaDevice));
	ret->dev_type = *type;
	ret->num_buffers = num_buffers;
	ret->num_samples = num_samples;
	ret->format = format;

	switch (*type) {
		case GaDeviceType_Dummy: ret->procs = gaX_deviceprocs_dummy; break;
		case GaDeviceType_WAV: ret->procs = gaX_deviceprocs_WAV; break;
#ifdef ENABLE_OSS
		case GaDeviceType_OSS: ret->procs = gaX_deviceprocs_OSS; break;
#endif
#ifdef ENABLE_XAUDIO2
		case GaDeviceType_XAudio2: ret->procs = gaX_deviceprocs_XAudio2; break;
#endif
#ifdef ENABLE_ARCAN
		case GaDeviceType_Arcan: ret->procs = gaX_deviceprocs_Arcan; break;
#endif
#ifdef ENABLE_SNDIO
		case GaDeviceType_Sndio: ret->procs = gaX_deviceprocs_sndio; break;
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
	if (pnum_buffers) *pnum_buffers = ret->num_buffers;
	if (pnum_samples) *pnum_samples = ret->num_samples;
	if (pformat) *pformat = ret->format;
	return ret;

fail:
	*type = GaDeviceType_Unknown;
	ga_free(ret);
	return NULL;
}

ga_result ga_device_close(GaDevice *device) {
	ga_result ret = device->procs.close ? device->procs.close(device) : GA_OK;
	free(device);
	return ret;
}

u32 ga_device_check(GaDevice *device) {
	return device->procs.check(device);
}

ga_result ga_device_queue(GaDevice *device, void *buffer) {
	return device->procs.queue(device, buffer);
}

GaDataSource *ga_data_source_create(const GaDataSourceCreationMinutiae *m) {
	if (!m->read || !m->tell || !m->eof) return NULL;
	GaDataSource *ret = ga_alloc(sizeof(GaDataSource));
	if (!ret) return NULL;
	ret->read = m->read;
	ret->seek = m->seek;
	ret->tell = m->tell;
	ret->eof = m->eof;
	ret->close = m->close;
	ret->context = m->context;
	ret->flags = (m->seek ? GaDataAccessFlag_Seekable : 0)
	           | (m->threadsafe ? GaDataAccessFlag_Threadsafe : 0);
	ret->refCount = rc_new();
	return ret;
}

usz ga_data_source_read(GaDataSource *src, void *dst, usz size, usz count) {
	return src->read(src->context, dst, size, count);
}

ga_result ga_data_source_seek(GaDataSource *src, ssz offset, GaSeekOrigin whence) {
	if (src->seek && (src->flags & GaDataAccessFlag_Seekable)) return src->seek(src->context, offset, whence);
	else return GA_ERR_MIS_UNSUP;
}

bool ga_data_source_eof(GaDataSource *src) {
	return src->eof(src->context);
}

usz ga_data_source_tell(GaDataSource *src) {
	return src->tell(src->context);
}

GaDataAccessFlags ga_data_source_flags(GaDataSource *src) {
	return src->flags;
}

void gaX_data_source_destroy(GaDataSource *src) {
	assert(src->refCount.rc == 0);
	if (src->close) src->close(src->context);
	ga_free(src);
}

void ga_data_source_acquire(GaDataSource *dataSrc) {
	incref(&dataSrc->refCount);
}

void ga_data_source_release(GaDataSource *dataSrc) {
	if (decref(&dataSrc->refCount)) gaX_data_source_destroy(dataSrc);
}

/* Sample Source Structure */
GaSampleSource *ga_sample_source_create(const GaSampleSourceCreationMinutiae *m) {
	if (!m->read || !m->tell || !m->end) return NULL;
	GaSampleSource *ret = ga_alloc(sizeof(GaSampleSource));
	if (!ret) return NULL;
	ret->refCount = rc_new();
	ret->read = m->read;
	ret->end = m->end;
	ret->ready = m->ready;
	ret->seek = m->seek;
	ret->tell = m->tell;
	ret->close = m->close;
	ret->context = m->context;
	ret->format = m->format;
	ret->flags = (m->seek ? GaDataAccessFlag_Seekable : 0)
	           | (m->threadsafe ? GaDataAccessFlag_Threadsafe : 0);
	return ret;
}

usz ga_sample_source_read(GaSampleSource *src, void *dst, usz num_samples, GaCbOnSeek onseek, void *seek_ctx) {
	return src->read(src->context, dst, num_samples, onseek, seek_ctx);
}
bool ga_sample_source_end(GaSampleSource *src) {
	return src->end(src->context);
}
bool ga_sample_source_ready(GaSampleSource *src, usz num_samples) {
	return src->ready ? src->ready(src->context, num_samples) : true;
}
ga_result ga_sample_source_seek(GaSampleSource *src, usz sampleOffset) {
	return src->seek && (src->flags & GaDataAccessFlag_Seekable) ? src->seek(src->context, sampleOffset) : GA_ERR_MIS_UNSUP;
}
ga_result ga_sample_source_tell(GaSampleSource *src, usz *samples, usz *totalSamples) {
	return src->tell ? src->tell(src->context, samples, totalSamples) : GA_ERR_MIS_UNSUP;
}
static void gaX_sample_source_destroy(GaSampleSource *src) {
	if (src->close) src->close(src->context);
	ga_free(src);
}

GaDataAccessFlags ga_sample_source_flags(GaSampleSource *src) {
	return src->flags;
}

void ga_sample_source_format(GaSampleSource *src, GaFormat *format) {
	*format = src->format;
}

void ga_sample_source_acquire(GaSampleSource *src) {
	incref(&src->refCount);
}

void ga_sample_source_release(GaSampleSource *src) {
	if (decref(&src->refCount)) gaX_sample_source_destroy(src);
}

/* Memory Functions */
static GaMemory *gaX_memory_create(void *data, usz size, bool copy) {
	GaMemory *ret = ga_alloc(sizeof(GaMemory));
	ret->size = size;
	if (data) {
		if (copy) ret->data = memcpy(ga_alloc(size), data, size);
		else ret->data = data;
	} else {
		ret->data = ga_alloc(size);
	}
	ret->refCount = rc_new();
	return ret;
}

GaMemory *ga_memory_create(void *data, usz size) {
	return gaX_memory_create(data, size, true);
}

GaMemory *ga_memory_create_data_source(GaDataSource *src) {
	enum { BUFSZ = 4096 };
	char *data;
	usz len;

	if (ga_data_source_flags(src) & GaDataAccessFlag_Seekable) {
		usz where = ga_data_source_tell(src);
		if (ga_data_source_seek(src, 0, GaSeekOrigin_End) != GA_OK)
			return NULL; //forward
		usz tlen = ga_data_source_tell(src);
		if (where > tlen || ga_data_source_seek(src, where, GaSeekOrigin_Set) != GA_OK)
			return NULL; //forward
		len = tlen - where;
		data = ga_alloc(len);
		if (!data) return NULL; //GA_ERR_MEMORY
		usz read = ga_data_source_read(src, data, 1, len);
		if (read != len) {
			ga_free(data);
			return NULL;
		}
	} else {
		len = 0;
		usz bytes_read = 0;
		data = NULL;
		do {
			data = ga_realloc(data, len + BUFSZ);
			bytes_read = ga_data_source_read(src, data + len, 1, BUFSZ);
			if (bytes_read < BUFSZ)
				data = ga_realloc(data, len + bytes_read);
			len += bytes_read;
		} while (bytes_read > 0);
	}

	GaMemory *ret = gaX_memory_create(data, len, false);
	if (!ret) ga_free(data);
	return ret;
}

usz ga_memory_size(GaMemory *mem) {
	return mem->size;
}

void *ga_memory_data(GaMemory *mem) {
	return mem->data;
}

static void gaX_memory_destroy(GaMemory *mem) {
	ga_free(mem->data);
	ga_free(mem);
}

void ga_memory_acquire(GaMemory *mem) {
	incref(&mem->refCount);
}

void ga_memory_release(GaMemory *mem) {
	if (decref(&mem->refCount)) gaX_memory_destroy(mem);
}


/* Sound Functions */
GaSound *ga_sound_create(GaMemory *memory, GaFormat *format) {
	if (ga_memory_size(memory) % ga_format_sample_size(format)) return NULL;

	GaSound *ret = ga_alloc(sizeof(GaSound));
	if (!ret) return NULL;

	ret->num_samples = ga_memory_size(memory) / ga_format_sample_size(format);
	ret->format = *format;
	ga_memory_acquire(memory);
	ret->memory = memory;
	ret->refCount = rc_new();

	return ret;
}

GaSound *ga_sound_create_sample_source(GaSampleSource *src) {
	GaFormat format;
	ga_sample_source_format(src, &format);
	u32 sample_size = ga_format_sample_size(&format);
	usz total_samples;
	ga_result told = ga_sample_source_tell(src, NULL, &total_samples);

	/* Known total samples*/
	if (ga_isok(told)) {
		char *data;
		GaMemory *memory;
		usz data_size = sample_size * total_samples;
		data = ga_alloc(data_size);
		ga_sample_source_read(src, data, total_samples, 0, 0);
		memory = gaX_memory_create(data, data_size, 0);
		if (memory) {
			GaSound *ret = ga_sound_create(memory, &format);
			if (!ret) ga_memory_release(memory);
			return ret;
		} else {
			ga_free(data);
			return NULL;
		}
	/* Unknown total samples */
	} else {
		s32 BUFFER_SAMPLES = format.sample_rate * 2;
		char *data = 0;
		GaMemory *memory;
		total_samples = 0;
		while (!ga_sample_source_end(src)) {
			s32 num_samples_read;
			data = ga_realloc(data, (total_samples + BUFFER_SAMPLES) * sample_size);
			num_samples_read = ga_sample_source_read(src, data + (total_samples * sample_size), BUFFER_SAMPLES, 0, 0);
			if (num_samples_read < BUFFER_SAMPLES) {
				data = ga_realloc(data, (total_samples + num_samples_read) * sample_size);
			}
			total_samples += num_samples_read;
		}
		memory = gaX_memory_create(data, total_samples * sample_size, 0);
		if (memory) {
			GaSound *ret = ga_sound_create(memory, &format);
			if (!ret) ga_memory_release(memory);
			return ret;
		} else {
			ga_free(data);
			return NULL;
		}
	}
}

void *ga_sound_data(GaSound *sound) {
	return ga_memory_data(sound->memory);
}

usz ga_sound_size(GaSound *sound) {
	return ga_memory_size(sound->memory);
}

usz ga_sound_num_samples(GaSound *sound) {
	return ga_memory_size(sound->memory) / ga_format_sample_size(&sound->format);
}

void ga_sound_format(GaSound *sound, GaFormat *format) {
	*format = sound->format;
}

static void gaX_sound_destroy(GaSound *sound) {
	ga_memory_release(sound->memory);
	ga_free(sound);
}

void ga_sound_acquire(GaSound *sound) {
	incref(&sound->refCount);
}

void ga_sound_release(GaSound *sound) {
	if (decref(&sound->refCount)) gaX_sound_destroy(sound);
}

/* Handle Functions */
static void gaX_handle_init(GaHandle *handle, GaMixer *mixer) {
	handle->state = GaHandleState_Initial;
	handle->mixer = mixer;
	handle->callback = NULL;
	handle->context = NULL;
	handle->pitch = 1;
	handle->gain = handle->last_gain = 1;
	handle->pan = handle->last_pan = 0;
	ga_mutex_create(&handle->mutex); //todo errhandle
}

GaHandle *ga_handle_create(GaMixer *mixer, GaSampleSource *src) {
	GaHandle *h = ga_alloc(sizeof(GaHandle));
	ga_sample_source_acquire(src);
	h->sample_src = src;
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

static ga_result gaX_handle_cleanup(GaHandle *handle) {
	/* May only be called from the dispatch thread */
	ga_sample_source_release(handle->sample_src);
	ga_mutex_destroy(handle->mutex);
	ga_free(handle);
	return GA_OK;
}

ga_result ga_handle_play(GaHandle *handle) {
	ga_mutex_lock(handle->mutex);
	if (handle->state >= GaHandleState_Finished) {
		ga_mutex_unlock(handle->mutex);
		return GA_ERR_MIS_UNSUP;
	}
	handle->state = GaHandleState_Playing;
	ga_mutex_unlock(handle->mutex);
	return GA_OK;
}

ga_result ga_handle_stop(GaHandle *handle) {
	ga_mutex_lock(handle->mutex);
	if (handle->state >= GaHandleState_Finished) {
		ga_mutex_unlock(handle->mutex);
		return GA_ERR_MIS_UNSUP;
	}
	handle->state = GaHandleState_Stopped;
	ga_mutex_unlock(handle->mutex);
	return GA_OK;
}

bool ga_handle_playing(GaHandle *handle) {
	return handle->state == GaHandleState_Playing;
}
bool ga_handle_stopped(GaHandle *handle) {
	return handle->state == GaHandleState_Stopped;
}
bool ga_handle_finished(GaHandle *handle) {
	return handle->state >= GaHandleState_Finished;
}
bool ga_handle_destroyed(GaHandle *handle) {
	return handle->state >= GaHandleState_Destroyed;
}

ga_result ga_handle_set_callback(GaHandle *handle, GaCbHandleFinish callback, void *context) {
	/* Does not need mutex because it can only be called from the dispatch thread */
	handle->callback = callback;
	handle->context = context;
	return GA_OK;
}

ga_result ga_handle_set_paramf(GaHandle *handle, GaHandleParam param, f32 value) {
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
		default: return GA_ERR_MIS_PARAM;
	}
}

ga_result ga_handle_get_paramf(GaHandle *handle, GaHandleParam param, f32 *value) {
	switch (param) {
		case GaHandleParam_Gain:  *value = handle->gain;  return GA_OK;
		case GaHandleParam_Pan:   *value = handle->pan;   return GA_OK;
		case GaHandleParam_Pitch: *value = handle->pitch; return GA_OK;
		default: return GA_ERR_MIS_PARAM;
	}
}

ga_result ga_handle_set_parami(GaHandle *handle, GaHandleParam param, s32 value) {
	/*
	   switch(param)
	   {
	   case GaHandleParam_?:
	   ga_mutex_lock(handle->mutex);
	   ga_mutex_unlock(handle->mutex);
	   return GA_OK;
	   }
	   */
	return GA_ERR_MIS_PARAM;
}

ga_result ga_handle_get_parami(GaHandle *handle, GaHandleParam param, s32 *value) {
	/*
	   switch(param)
	   {
	   case GaHandleParam_?: *value = ?; return GA_OK;
	   }
	   */
	return GA_ERR_MIS_PARAM;
}

ga_result ga_handle_seek(GaHandle *handle, usz sample_offset) {
	ga_sample_source_seek(handle->sample_src, sample_offset);
	return GA_OK;
}

ga_result ga_handle_tell(GaHandle *handle, GaTellParam param, usz *out) {
	if (!out) return GA_ERR_MIS_PARAM;
	if (param == GaTellParam_Current) return ga_sample_source_tell(handle->sample_src, out, NULL);
	else if (param == GaTellParam_Total) return ga_sample_source_tell(handle->sample_src, NULL, out);
	else return GA_ERR_MIS_PARAM;
}

bool ga_handle_ready(GaHandle *handle, usz num_samples) {
	return ga_sample_source_ready(handle->sample_src, num_samples);
}

void ga_handle_format(GaHandle *handle, GaFormat *format) {
	ga_sample_source_format(handle->sample_src, format);
}

/* Mixer Functions */
GaMixer *ga_mixer_create(GaFormat *format, u32 num_samples) {
	GaMixer *ret = ga_alloc(sizeof(GaMixer));
	if (!ret) return NULL;
	if (!ga_isok(ga_mutex_create(&ret->dispatch_mutex))) goto fail;
	if (!ga_isok(ga_mutex_create(&ret->mix_mutex))) goto fail;
	ga_list_head(&ret->dispatch_list);
	ga_list_head(&ret->mix_list);
	ret->num_samples = num_samples;
	ret->format = *format;
	ret->mix_format.bits_per_sample = 32;
	ret->mix_format.num_channels = format->num_channels;
	ret->mix_format.sample_rate = format->sample_rate;
	ret->mix_buffer = ga_alloc(num_samples * ga_format_sample_size(&ret->mix_format));
	ret->suspended = false;
	return ret;

fail:
	ga_mutex_destroy(ret->dispatch_mutex);
	ga_mutex_destroy(ret->mix_mutex);
	ga_free(ret);
	return NULL;
}

ga_result ga_mixer_suspend(GaMixer *m) {
	return atomic_exchange(&m->suspended, true) ? GA_ERR_MIS_UNSUP : GA_OK;
}

ga_result ga_mixer_unsuspend(GaMixer *m) {
	return atomic_exchange(&m->suspended, false) ? GA_OK : GA_ERR_MIS_UNSUP;
}

void ga_mixer_format(GaMixer *mixer, GaFormat *fmt) {
	*fmt = mixer->format;
}

u32 ga_mixer_num_samples(GaMixer *mixer) {
	return mixer->num_samples;
}

static void gaX_mixer_mix_buffer(GaMixer *mixer,
                          void *src_buffer, s32 src_samples, GaFormat *src_fmt,
                          s32 *dst, s32 dst_samples, GaFormat *dst_fmt,
			  f32 gain, f32 last_gain, f32 pan, f32 last_pan, f32 pitch) {
	u32 mixer_channels = dst_fmt->num_channels;
	s32 src_channels = src_fmt->num_channels;
	f32 sample_scale = src_fmt->sample_rate / (f32)dst_fmt->sample_rate * pitch; //todo interpolate?
	f32 fj = 0.0;
	f32 srcSamplesRead = 0.0;
	u32 sample_size = ga_format_sample_size(src_fmt);
	pan = clamp((pan + 1) / 2, 0, 1);
	last_pan = clamp((last_pan + 1) / 2, 0, 1);
	f32 cur_pan = last_pan, cur_gain = last_gain;
	f32 d_pan = (pan - last_pan) / dst_samples;
	f32 d_gain = (gain - last_gain) / dst_samples;

	/* TODO: Support 8-bit/16-bit mono/stereo mixer format */
	switch (src_fmt->bits_per_sample) {
		case 16: {
			s32 src_bytes = src_samples * sample_size;
			const s16 *src = src_buffer;
			for (u32 i = 0, j = 0; i < dst_samples * mixer_channels && src_bytes >= 2 * src_channels; i += mixer_channels) {
				f32 lmul = cur_gain * (cur_pan < 0.5 ? 1 : (1 - cur_pan) * 2);
				f32 rmul = cur_gain * (cur_pan > 0.5 ? 1 : cur_pan * 2);
				cur_pan += d_pan;
				cur_gain += d_gain;
				dst[i] += (s32)((s32)src[j] * lmul);
				dst[i + 1] += (s32)((s32)src[j + ((src_channels == 1) ? 0 : 1)] * rmul);

				fj += sample_scale * src_channels;
				srcSamplesRead += sample_scale * src_channels;
				u32 jP/*j'*/ = (u32)fj & (src_channels == 1 ? ~0u : ~1u);
				src_bytes -= (jP - j) * 2;
				j = jP;
			}

			break;
		}
	}
}

static void gaX_mixer_mix_handle(GaMixer *mixer, GaHandle *handle, usz num_samples) {
	GaSampleSource *ss = handle->sample_src;
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
	u32 srcSampleSize = ga_format_sample_size(&handleFormat);
	f32 oldPitch = handle->pitch;
	f32 dstToSrc = handleFormat.sample_rate / (f32)mixer->format.sample_rate * oldPitch;
	usz requested = num_samples * dstToSrc;
	requested = requested / dstToSrc < num_samples ? requested + 1 : requested;

	if (requested <= 0 || !ga_sample_source_ready(ss, requested)) return;

	ga_mutex_lock(handle->mutex);
	f32 gain = handle->gain;
	f32 last_gain = handle->last_gain;
	handle->last_gain = gain;
	f32 pan = handle->pan;
	f32 last_pan = handle->last_pan;
	handle->last_pan = pan;
	f32 pitch = handle->pitch;
	ga_mutex_unlock(handle->mutex);

	/* We avoided a mutex lock by using pitch to check if buffer has enough dst samples */
	/* If it has changed since then, we re-test to make sure we still have enough samples */
	if (oldPitch != pitch) {
		dstToSrc = handleFormat.sample_rate / (f32)mixer->format.sample_rate * pitch;
		requested = (s32)(num_samples * dstToSrc);
		requested = requested / dstToSrc < num_samples ? requested + 1 : requested;
		if (requested <= 0 || !ga_sample_source_ready(ss, requested)) return;
	}

	/* TODO: To optimize, we can refactor the _read() interface to be _mix(), avoiding this malloc/copy */
	void *src = ga_alloc(requested * srcSampleSize);
	s32 numRead = 0;
	numRead = ga_sample_source_read(ss, src, requested, NULL, NULL);
	gaX_mixer_mix_buffer(mixer,
	                     src, numRead, &handleFormat,
	                     mixer->mix_buffer, num_samples, &mixer->format,
	                     gain, last_gain, pan, last_pan, pitch);
	ga_free(src);
}

ga_result ga_mixer_mix(GaMixer *m, void *buffer) {
	usz end = m->num_samples * m->format.num_channels;

	if (m->suspended) {
		memset(buffer, 0, end * m->format.bits_per_sample);
		return GA_OK;
	}

	GaLink *link;
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
	//todo this is wrong
	switch (m->format.bits_per_sample) {
		case 8:
			for (usz i = 0; i < end; ++i) {
				s32 sample = m->mix_buffer[i];
				((s8*)buffer)[i] = sample > -128 ? (sample < 127 ? sample : 127) : -128;
			}
			break;
		case 16:
			for (usz i = 0; i < end; ++i) {
			        s32 sample = m->mix_buffer[i];
			        ((s16*)buffer)[i] = sample > -32768 ? (sample < 32767 ? sample : 32767) : -32768;
			}
			break;
		case 32:
			memcpy(buffer, m->mix_buffer, end * 4);
			break;
	}

	return GA_OK;
}

ga_result ga_mixer_dispatch(GaMixer *m) {
	for (GaLink *next, *link = m->dispatch_list.next; (next = link->next), (link != &m->dispatch_list); link = next) {
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

	ga_free(m->mix_buffer);
	ga_free(m);
	return GA_OK;
}
