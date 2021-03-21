#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

static u32 autoincrement(void) {
	static atomic_u32 ret;
	return atomic_fetch_add(&ret, 1);
}

/* Version Functions */
bool ga_version_compatible(s32 major, s32 minor, s32 rev) {
	return major == GA_VERSION_MAJOR
	   // until 1.0, minor version changes are breaking
	   && (major != 0 || minor == GA_VERSION_MINOR)
	   && (minor <= GA_VERSION_MINOR);
}

/* Format Functions */
u32 ga_format_sample_size(GaSampleFormat fmt) {
	return abs(fmt);
}
u32 ga_format_frame_size(const GaFormat *format) {
	return ga_format_sample_size(format->sample_fmt) * format->num_channels;
}

f32 ga_format_to_seconds(const GaFormat *format, usz frames) {
	return frames / (f32)format->frame_rate;
}

s32 ga_format_to_frames(const GaFormat *format, f32 seconds) {
	return seconds * format->frame_rate;
}

static void *gaX_get_buffer_nozerocopy(GaDevice *dev) { return dev->buffer; }

/* Device Functions */
GaDevice *ga_device_open(GaDeviceType *type,
                          u32 *num_buffers,
                          u32 *num_frames,
                          GaFormat *format) {
	type = type ? type : &(GaDeviceType){GaDeviceType_Default};
	num_buffers = num_buffers ? num_buffers : &(u32){4};
	num_frames = num_frames ? num_frames : &(u32){512};
	format = format ? format : &(GaFormat){.sample_fmt=GaSampleFormat_S16, .num_channels=2, .frame_rate=48000};


	// todo allow overriding with an environment variable
	if (*type == GaDeviceType_Default) {
#define try(t) *type = t; if ((ret = ga_device_open(type, num_buffers, num_frames, format))) return ret
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

	GaDevice *ret = ga_zalloc(sizeof(GaDevice));
	ret->dev_type = *type;
	ret->num_buffers = *num_buffers;
	ret->num_frames = *num_frames;
	ret->format = *format;

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
	if (!ret->procs.get_buffer) {
		ret->procs.get_buffer = gaX_get_buffer_nozerocopy;
		ret->buffer = ga_alloc(ret->num_frames * ga_format_frame_size(&ret->format));
		if (!ret->buffer) {
			ret->procs.close(ret);
			goto fail;
		}
	}
	*num_buffers = ret->num_buffers;
	*num_frames = ret->num_frames;
	*format = ret->format;
	return ret;

fail:
	*type = GaDeviceType_Unknown;
	ga_free(ret);
	return NULL;
}

ga_result ga_device_close(GaDevice *device) {
	ga_result ret = device->procs.close ? device->procs.close(device) : GA_OK;
	ga_free(device->buffer);
	ga_free(device);
	return ret;
}

u32 ga_device_check(GaDevice *device) {
	return device->procs.check(device);
}

void *ga_device_get_buffer(GaDevice *device) {
	return device->procs.get_buffer(device);
}

ga_result ga_device_queue(GaDevice *device, void *buffer) {
	return device->procs.queue(device, buffer);
}

void ga_device_format(GaDevice *device, GaFormat *fmt) {
	*fmt = device->format;
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

usz ga_sample_source_read(GaSampleSource *src, void *dst, usz num_frames, GaCbOnSeek onseek, void *seek_ctx) {
	return src->read(src->context, dst, num_frames, onseek, seek_ctx);
}
bool ga_sample_source_end(GaSampleSource *src) {
	return src->end(src->context);
}
bool ga_sample_source_ready(GaSampleSource *src, usz num_frames) {
	return src->ready ? src->ready(src->context, num_frames) : true;
}
ga_result ga_sample_source_seek(GaSampleSource *src, usz sampleOffset) {
	return src->seek && (src->flags & GaDataAccessFlag_Seekable) ? src->seek(src->context, sampleOffset) : GA_ERR_MIS_UNSUP;
}
ga_result ga_sample_source_tell(GaSampleSource *src, usz *frames, usz *total_frames) {
	return src->tell ? src->tell(src->context, frames, total_frames) : GA_ERR_MIS_UNSUP;
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
	if (ga_memory_size(memory) % ga_format_frame_size(format)) return NULL;

	GaSound *ret = ga_alloc(sizeof(GaSound));
	if (!ret) return NULL;

	ret->num_frames = ga_memory_size(memory) / ga_format_frame_size(format);
	ret->format = *format;
	ga_memory_acquire(memory);
	ret->memory = memory;
	ret->refCount = rc_new();

	return ret;
}

GaSound *ga_sound_create_sample_source(GaSampleSource *src) {
	GaFormat format;
	ga_sample_source_format(src, &format);
	u32 frame_size = ga_format_frame_size(&format);
	usz total_frames;
	ga_result told = ga_sample_source_tell(src, NULL, &total_frames);

	/* Known total frames*/
	if (ga_isok(told)) {
		char *data;
		GaMemory *memory;
		usz data_size = frame_size * total_frames;
		data = ga_alloc(data_size);
		ga_sample_source_read(src, data, total_frames, 0, 0);
		memory = gaX_memory_create(data, data_size, 0);
		if (memory) {
			GaSound *ret = ga_sound_create(memory, &format);
			if (!ret) ga_memory_release(memory);
			return ret;
		} else {
			ga_free(data);
			return NULL;
		}
	/* Unknown total frames */
	} else {
		const u32 BUFFER_FRAMES = format.frame_rate * 2;
		char *data = 0;
		GaMemory *memory;
		total_frames = 0;
		while (!ga_sample_source_end(src)) {
			usz num_frames_read;
			data = ga_realloc(data, (total_frames + BUFFER_FRAMES) * frame_size);
			num_frames_read = ga_sample_source_read(src, data + (total_frames * frame_size), BUFFER_FRAMES, 0, 0);
			if (num_frames_read < BUFFER_FRAMES) {
				data = ga_realloc(data, (total_frames + num_frames_read) * frame_size);
			}
			total_frames += num_frames_read;
		}
		memory = gaX_memory_create(data, total_frames * frame_size, 0);
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

const void *ga_sound_data(GaSound *sound) {
	return ga_memory_data(sound->memory);
}

usz ga_sound_size(GaSound *sound) {
	return ga_memory_size(sound->memory);
}

usz ga_sound_num_frames(GaSound *sound) {
	return ga_memory_size(sound->memory) / ga_format_frame_size(&sound->format);
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

static void init_jukeboxstate(JukeboxState *state) {
	state->pitch = 1;
	state->gain = state->last_gain = 1;
	state->pan =  state->last_pan = 0;
}

/* Handle Functions */
GaHandle *ga_handle_create(GaMixer *mixer, GaSampleSource *src) {
	GaHandle *h = ga_alloc(sizeof(GaHandle));
	if (!h) return NULL;
	ga_sample_source_acquire(src);
	h->sample_src = src;

	h->state = GaHandleState_Initial;
	h->mixer = mixer;
	h->callback = NULL;
	h->context = NULL;
	init_jukeboxstate(&h->jukebox);

	if (!ga_isok(ga_mutex_create(&h->mutex))) {
		ga_sample_source_release(src);
		ga_free(h);
		return NULL;
	}

	h->group = &mixer->handle_group;
	ga_list_link(&mixer->handle_group.handles, &h->group_link, h);

	GaFormat fmt;
	ga_handle_format(h, &fmt);
	//todo channelnum should be min()
	if (fmt.frame_rate != mixer->format.frame_rate) assert(h->resample_state = ga_trans_resample_setup(mixer->format.frame_rate, fmt));
	else h->resample_state = NULL;

	with_mutex(mixer->mix_mutex) {
		ga_list_link(&mixer->mix_list, &h->mix_link, h);
	}

	with_mutex(mixer->dispatch_mutex) {
		ga_list_link(&mixer->dispatch_list, &h->dispatch_link, h);
	}

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
	if (handle->resample_state) ga_trans_resample_teardown(handle->resample_state);
	ga_sample_source_release(handle->sample_src);
	ga_list_unlink(&handle->group_link);
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
	ga_mutex_lock(handle->mutex);
	switch (param) {
		case GaHandleParam_Pitch: handle->jukebox.pitch = value; break;
		case GaHandleParam_Gain:  handle->jukebox.gain = value;  break;
		case GaHandleParam_Pan:   handle->jukebox.pan = value;   break;
		default:
			ga_mutex_unlock(handle->mutex);
			return GA_ERR_MIS_PARAM;
	}
	handle->jukebox_stamps[param] = autoincrement();
	ga_mutex_unlock(handle->mutex);
	return GA_OK;
}

static f32 *handle_get_paramf(GaHandle *handle, GaXHandleParam param) {
	f32 *hp, *gp;

	switch (param) {
		case GaXHandleParam_Pitch:     hp = &handle->jukebox.pitch;        gp = &handle->group->jukebox.pitch;      break;
		case GaXHandleParam_Gain:      hp = &handle->jukebox.gain;         gp = &handle->group->jukebox.gain;       break;
		case GaXHandleParam_LastGain:  hp = &handle->jukebox.last_gain;    gp = &handle->group->jukebox.last_gain;  break;
		case GaXHandleParam_Pan:       hp = &handle->jukebox.pan;          gp = &handle->group->jukebox.pan;        break;
		case GaXHandleParam_LastPan:   hp = &handle->jukebox.last_pan;     gp = &handle->group->jukebox.last_pan;   break;
		default: assert(0);
	}

	return handle->jukebox_stamps[param] > handle->group->jukebox_stamps[param] ? hp : gp;
}

ga_result ga_handle_get_paramf(GaHandle *handle, GaHandleParam param, f32 *value) {
	switch (param) {
		case GaHandleParam_Pitch:
		case GaHandleParam_Pan:
		case GaHandleParam_Gain:
			with_mutex(handle->mutex) *value = *handle_get_paramf(handle, (GaXHandleParam)param);
			return GA_OK;
		default: return GA_ERR_MIS_PARAM;
	}
}

static f32 *handle_group_get_paramf(GaHandleGroup *g, GaHandleParam param) {
	switch (param) {
		case GaHandleParam_Pitch: return &g->jukebox.pitch;
		case GaHandleParam_Gain:  return &g->jukebox.gain;
		case GaHandleParam_Pan:   return &g->jukebox.pan;
		default: assert(0);
	}
}

ga_result ga_handle_group_set_paramf(GaHandleGroup *g, GaHandleParam param, f32 value) {
	switch (param) {
		case GaHandleParam_Pitch:
		case GaHandleParam_Gain:
		case GaHandleParam_Pan:;
			f32 *f = handle_group_get_paramf(g, param);
			with_mutex(g->mutex) *f = value;
			return GA_OK;
		default: return GA_ERR_MIS_PARAM;
	}
}
ga_result ga_handle_group_get_paramf(GaHandleGroup *g, GaHandleParam param, f32 *value) {
	switch (param) {
		case GaHandleParam_Pitch:
		case GaHandleParam_Gain:
		case GaHandleParam_Pan:;
			f32 *f = handle_group_get_paramf(g, param);
			with_mutex(g->mutex) *value = *f;
			return GA_OK;
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

ga_result ga_handle_seek(GaHandle *handle, usz frame_offset) {
	return ga_sample_source_seek(handle->sample_src, frame_offset);
}

ga_result ga_handle_tell(GaHandle *handle, GaTellParam param, usz *out) {
	if (!out) return GA_ERR_MIS_PARAM;
	if (param == GaTellParam_Current) return ga_sample_source_tell(handle->sample_src, out, NULL);
	else if (param == GaTellParam_Total) return ga_sample_source_tell(handle->sample_src, NULL, out);
	else return GA_ERR_MIS_PARAM;
}

bool ga_handle_ready(GaHandle *handle, usz num_frames) {
	return ga_sample_source_ready(handle->sample_src, num_frames);
}

void ga_handle_format(GaHandle *handle, GaFormat *format) {
	ga_sample_source_format(handle->sample_src, format);
}

GaHandleGroup *ga_mixer_handle_group(GaMixer *m) {
	return &m->handle_group;
}

static ga_result gaX_handle_group_init(GaHandleGroup *g, GaMixer *m) {
	memset(g, 0, sizeof(*g));
	ga_list_head(&g->handles);
	g->mixer = m;
	init_jukeboxstate(&g->jukebox);
	return ga_mutex_create(&g->mutex);

}

GaHandleGroup *ga_handle_group_create(GaMixer *m) {
	GaHandleGroup *ret = ga_alloc(sizeof(GaHandleGroup));
	if (!ret) return NULL;

	if (!ga_isok(gaX_handle_group_init(ret, m))) {
		ga_free(ret);
	}

	return ret;
}

void ga_handle_group_add(GaHandleGroup *group, GaHandle *handle) {
	if (!group) group = &handle->mixer->handle_group;
	if (handle->group == group) return;

	with_mutex(group->mutex) {
		with_mutex(handle->group->mutex) {
			handle->group = group;
			ga_list_unlink(&handle->group_link);
		}
		ga_list_link(&group->handles, &handle->group_link, handle);
	}
}

void ga_handle_group_transfer(GaHandleGroup *group, GaHandleGroup *target) {
	if (!target) target = &group->mixer->handle_group;

	if (group == target) return;

	ga_mutex_lock(group->mutex);

	// no handles to transfer
	if (group->handles.next == &group->handles) {
		ga_mutex_unlock(group->mutex);
		return;
	}

	with_mutex(target->mutex) {
		ga_list_merge(&target->handles, &group->handles);
	}

	ga_mutex_unlock(group->mutex);
}

void ga_handle_group_disown(GaHandleGroup *group) {
	ga_handle_group_transfer(group, NULL);
}

static void gaX_handle_group_destroy(GaHandleGroup *group) {
	with_mutex(group->mutex) {
		ga_list_iterate(GaHandle, h, &group->handles) {
			ga_handle_destroy(h);
		}

		ga_list_head(&group->handles);
	}

	ga_mutex_destroy(group->mutex);
}

void ga_handle_group_destroy(GaHandleGroup *group) {
	gaX_handle_group_destroy(group);
	ga_free(group);
}

#if 0
static void gaX_mixer_reset(GaMixer *m, const GaFormat *format, u32 num_frames) {
	GaFormat fmt = *format;
	fmt.sample_fmt = GaSampleFormat_S32;

	if (num_frames != m->num_frames || ga_format_frame_size(&fmt) != ga_format_frame_size(&m->mix_format)) {
		ga_free(m->mix_buffer);
		m->mix_buffer = ga_alloc(num_frames * ga_format_frame_size(&fmt));
	}

	m->num_frames = num_frames;

	with_mutex(m->mix_mutex) {
		/*
		GaFormat fmt;
		ga_handle_format(h, &fmt);
		//todo channelnum should be min()
		if (fmt.frame_rate != mixer->format.frame_rate) assert(h->resample_state = ga_trans_resample_setup(mixer->format.frame_rate, fmt));
		else h->resample_state = NULL;
		*/
	}
}
#endif

/* Mixer Functions */
GaMixer *ga_mixer_create(const GaFormat *format, u32 num_frames) {
	GaMixer *ret = ga_alloc(sizeof(GaMixer));
	if (!ret) return NULL;
	if (!ga_isok(gaX_handle_group_init(&ret->handle_group, ret))) goto fail;
	if (!ga_isok(ga_mutex_create(&ret->dispatch_mutex))) goto fail;
	if (!ga_isok(ga_mutex_create(&ret->mix_mutex))) goto fail;
	ga_list_head(&ret->dispatch_list);
	ga_list_head(&ret->mix_list);
	ret->num_frames = num_frames;
	ret->format = *format;
	ret->mix_format.sample_fmt = GaSampleFormat_S32; //not exactly.  s32 dynamic range, but normalized to s16 magnitude
	ret->mix_format.num_channels = format->num_channels;
	ret->mix_format.frame_rate = format->frame_rate;
	ret->mix_buffer = ga_alloc(num_frames * ga_format_frame_size(&ret->mix_format));
	ret->suspended = false;
	return ret;

fail:
	ga_mutex_destroy(ret->handle_group.mutex);
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

u32 ga_mixer_num_frames(GaMixer *mixer) {
	return mixer->num_frames;
}

static void gaX_mixer_mix_buffer(GaMixer *mixer,
                          void *src_buffer, s32 src_frames, GaFormat *src_fmt,
                          s32 *dst, s32 dst_frames, GaFormat *dst_fmt,
			  f32 gain, f32 last_gain, f32 pan, f32 last_pan, f32 pitch) {
	u32 mixer_channels = dst_fmt->num_channels;
	s32 src_channels = src_fmt->num_channels;
	f32 sample_scale = 1 / pitch; //todo interpolate?
	f32 fj = 0.0;
	f32 srcSamplesRead = 0.0;
	pan = clamp((pan + 1) / 2, 0, 1);
	last_pan = clamp((last_pan + 1) / 2, 0, 1);
	f32 cur_pan = last_pan, cur_gain = last_gain;
	f32 d_pan = (pan - last_pan) / dst_frames;
	f32 d_gain = (gain - last_gain) / dst_frames;
	s32 src_bytes = src_frames * ga_format_frame_size(src_fmt);

	/* TODO: Support mono mixing format */
	switch (src_fmt->sample_fmt) {
		case GaSampleFormat_U8: {
			const u8 *src = src_buffer;
			for (u32 i = 0, j = 0; i < dst_frames * mixer_channels && src_bytes >= (s32)sizeof(u8) * src_channels; i += mixer_channels) {
				f32 lmul = cur_gain * (cur_pan < 0.5 ? 1 : (1 - cur_pan) * 2);
				f32 rmul = cur_gain * (cur_pan > 0.5 ? 1 : cur_pan * 2);
				cur_pan += d_pan;
				cur_gain += d_gain;
				dst[i] += ga_trans_s16_of_u8(src[j]) * lmul;
				dst[i + 1] += ga_trans_s16_of_u8(src[j + (src_channels > 1)]) * rmul;

				fj += sample_scale * src_channels;
				srcSamplesRead += sample_scale * src_channels;
				u32 jP/*j'*/ = (u32)fj & (src_channels == 1 ? ~0u : ~1u);
				src_bytes -= (jP - j) * sizeof(u8);
				j = jP;
			}

			break;
		}
		case GaSampleFormat_S16: {
			const s16 *src = src_buffer;
			for (u32 i = 0, j = 0; i < dst_frames * mixer_channels && src_bytes >= (s32)sizeof(s16) * src_channels; i += mixer_channels) {
				f32 lmul = cur_gain * (cur_pan < 0.5 ? 1 : (1 - cur_pan) * 2);
				f32 rmul = cur_gain * (cur_pan > 0.5 ? 1 : cur_pan * 2);
				cur_pan += d_pan;
				cur_gain += d_gain;
				dst[i] += (s32)((s32)src[j] * lmul);
				dst[i + 1] += (s32)((s32)src[j + (src_channels > 1)] * rmul);

				fj += sample_scale * src_channels;
				srcSamplesRead += sample_scale * src_channels;
				u32 jP/*j'*/ = (u32)fj & (src_channels == 1 ? ~0u : ~1u);
				src_bytes -= (jP - j) * sizeof(s16);
				j = jP;
			}

			break;
		}
		case GaSampleFormat_S32: {
			const s32 *src = src_buffer;
			for (u32 i = 0, j = 0; i < dst_frames * mixer_channels && src_bytes >= (s32)sizeof(s32) * src_channels; i += mixer_channels) {
				f32 lmul = cur_gain * (cur_pan < 0.5 ? 1 : (1 - cur_pan) * 2);
				f32 rmul = cur_gain * (cur_pan > 0.5 ? 1 : cur_pan * 2);
				cur_pan += d_pan;
				cur_gain += d_gain;
				dst[i] += ga_trans_s16_of_s32(src[j] * lmul);
				dst[i + 1] += ga_trans_s16_of_s32(src[j + (src_channels > 1)] * rmul);

				fj += sample_scale * src_channels;
				srcSamplesRead += sample_scale * src_channels;
				u32 jP/*j'*/ = (u32)fj & (src_channels == 1 ? ~0u : ~1u);
				src_bytes -= (jP - j) * sizeof(s32);
				j = jP;
			}

			break;
		}
		case GaSampleFormat_F32: {
			const f32 *src = src_buffer;
			for (u32 i = 0, j = 0; i < dst_frames * mixer_channels && src_bytes >= (s32)sizeof(f32) * src_channels; i += mixer_channels) {
				f32 lmul = cur_gain * (cur_pan < 0.5 ? 1 : (1 - cur_pan) * 2);
				f32 rmul = cur_gain * (cur_pan > 0.5 ? 1 : cur_pan * 2);
				cur_pan += d_pan;
				cur_gain += d_gain;
				dst[i] += ga_trans_s16_of_f32(clamp(src[j] * lmul, -1, 1));
				dst[i + 1] += ga_trans_s16_of_f32(clamp(src[j + (src_channels > 1)] * rmul, -1, 1));

				fj += sample_scale * src_channels;
				srcSamplesRead += sample_scale * src_channels;
				u32 jP/*j'*/ = (u32)fj & (src_channels == 1 ? ~0u : ~1u);
				src_bytes -= (jP - j) * sizeof(f32);
				j = jP;
			}

			break;
		}
	}
}

static void gaX_mixer_mix_handle(GaMixer *mixer, GaHandle *handle, usz num_frames) {
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
	GaFormat handle_format;
	ga_sample_source_format(ss, &handle_format);
	/* Check if we have enough frames to stream a full buffer */
	f32 old_pitch = *handle_get_paramf(handle, GaXHandleParam_Pitch);
	// number of frames to mix (after resampling)
	usz needed = num_frames / old_pitch;
	needed = (needed * old_pitch < num_frames) ? needed + 1 : needed;
	// number of frames to request from the handle
	usz requested = handle->resample_state ? ga_trans_resample_howmany(handle->resample_state, needed) : needed;

	if (!ga_sample_source_ready(ss, requested)) {
		ga_trace("Sample source not ready to play %zu frames; skipped!", requested);
		return;
	}

	ga_mutex_lock(handle->mutex);
	f32 gain = *handle_get_paramf(handle, GaXHandleParam_Gain);
	f32 last_gain = *handle_get_paramf(handle, GaXHandleParam_LastGain);
	*handle_get_paramf(handle, GaXHandleParam_LastGain) = gain;

	f32 pan = *handle_get_paramf(handle, GaXHandleParam_Pan);
	f32 last_pan = *handle_get_paramf(handle, GaXHandleParam_LastPan);
	*handle_get_paramf(handle, GaXHandleParam_LastPan) = pan;

	f32 pitch = *handle_get_paramf(handle, GaXHandleParam_Pitch);
	ga_mutex_unlock(handle->mutex);

	/* We avoided a mutex lock by using pitch to check if buffer has enough dst frames */
	/* If it has changed since then, we re-test to make sure we still have enough frames */
	if (old_pitch != pitch) {
		needed = num_frames / pitch;
		needed = needed * pitch < num_frames ? needed + 1 : needed;
		requested = handle->resample_state ? ga_trans_resample_howmany(handle->resample_state, needed) : needed;
		if (!ga_sample_source_ready(ss, requested)) {
			ga_trace("Sample source not ready to play %zu frames; skipped!", requested);
			return;
		}
	}

	void *dst = ga_alloc(needed * ga_format_frame_size(&handle_format));
	if (mixer->format.frame_rate != handle_format.frame_rate) {
		void *src = ga_alloc(requested * ga_format_frame_size(&handle_format));
		usz num_read = ga_sample_source_read(ss, src, requested, NULL, NULL);
		if (num_read != requested) {
			f32 r = needed / (f32)requested;
			requested = num_read;
			needed = requested * r;
		}
		ga_trans_resample_linear(handle->resample_state, dst, needed, src, requested);
		//ga_trans_resample_point(handle->resample_state, dst, needed, src, requested);
		ga_free(src);
	} else {
		usz num_read = ga_sample_source_read(ss, dst, needed, NULL, NULL);

		if (num_read != requested) {
			f32 r = needed / (f32)requested;
			requested = num_read;
			needed = requested * r;
		}
	}

	gaX_mixer_mix_buffer(mixer,
	                     dst, needed, &handle_format,
	                     mixer->mix_buffer, num_frames, &mixer->format,
	                     gain, last_gain, pan, last_pan, pitch);
	ga_free(dst);
}

ga_result ga_mixer_mix(GaMixer *m, void *buffer) {
	memset(buffer, 0, m->num_frames * ga_format_frame_size(&m->format));

	if (m->suspended) {
		return GA_OK;
	}

	ga_list_iterate(GaHandle, h, &m->mix_list) {
		gaX_mixer_mix_handle(m, h, m->num_frames);
		if (ga_handle_finished(h)) {
			with_mutex(m->mix_mutex) ga_list_unlink(link);
		}
	}

	/* mix_buffer will already be correct bps */
	switch (m->format.sample_fmt) {
		case GaSampleFormat_U8:
			for (usz i = 0; i < m->num_frames * m->format.num_channels; i++) {
				s16 sample = clamp(m->mix_buffer[i], -32768, 32767);
				((u8*)buffer)[i] = ga_trans_u8_of_s16(sample);
			}
			break;
		case GaSampleFormat_S16:
			for (usz i = 0; i < m->num_frames * m->format.num_channels; i++) {
				s16 sample = clamp(m->mix_buffer[i], -32768, 32767);
			        ((s16*)buffer)[i] = sample;
			}
			break;
		case GaSampleFormat_S32:
			for (usz i = 0; i < m->num_frames * m->format.num_channels; i++) {
				s16 sample = clamp(m->mix_buffer[i], -32768, 32767);
				((s32*)buffer)[i] = ga_trans_s32_of_s16(sample);
			}
			break;
		case GaSampleFormat_F32:
			for (usz i = 0; i < m->num_frames * m->format.num_channels; i++) {
				s16 sample = clamp(m->mix_buffer[i], -32768, 32767);
				((f32*)buffer)[i] = ga_trans_f32_of_s16(sample);
			}
			break;
		default: return GA_ERR_MIS_UNSUP;
	}

	return GA_OK;
}

ga_result ga_mixer_dispatch(GaMixer *m) {
	ga_list_iterate (GaHandle, handle, &m->dispatch_list) {
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
	gaX_handle_group_destroy(&m->handle_group);

	/* NOTE: Mixer/handles must no longer be in use on any thread when destroy is called */
	ga_list_iterate(GaHandle, h, &m->dispatch_list) {
		gaX_handle_cleanup(h);
	}


	ga_mutex_destroy(m->dispatch_mutex);
	ga_mutex_destroy(m->mix_mutex);

	ga_free(m->mix_buffer);
	ga_free(m);
	return GA_OK;
}
