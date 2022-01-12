#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <alsa/asoundlib.h>

static void *gaX_get_buffer_async(GaDevice*);
static ga_result gaX_queue_async(GaDevice*,void*);
static ga_result gaX_queue_pipe(GaDevice*,void*);

struct GaXDeviceImpl {
	snd_pcm_t *interface;
	snd_pcm_uframes_t last_offset;
};

#define acheck(expr) do { if ((expr) < 0) { res = GA_ERR_SYS_LIB; ga_warn("alsa: '" #expr "' failed"); goto cleanup; } } while (0)
// you know what's cool?  GC
// I go to all this effort to make an API that's impossible to misuse, but the implementation probably has bugs...
// plus, lifo lifetimes probably fragment crappy 'malloc' heap
// :/
static GaDeviceDescription *gaX_enumerate(u32 *num, u32 *len_bytes) {
	void **hints = NULL;
	GaDeviceDescription *ret = NULL;
	char *(*tnames)[2] = NULL;

	snd_device_name_hint(-1, "pcm", &hints);
	if (!hints) goto cleanup;

	u32 n = 0;
	while (hints[n]) n++;

	tnames = ga_zalloc(n * sizeof(*tnames)); //over-allocation

	u32 l = 0;
	u32 meta_len = 0;
	for (u32 i = 0; i < n; i++) {
		char *ioid = snd_device_name_get_hint(hints[i], "IOID");
		if (!ioid || strcmp(ioid, "Output")) {
			free(ioid);
			continue;
		}
		free(ioid);
		char *n = snd_device_name_get_hint(hints[i], "NAME");
		char *d = snd_device_name_get_hint(hints[i], "DESC");
		if (n && strcmp(n, "null") && d && strcmp(d, "null")) {
			l++;
			meta_len += 1 + strlen(tnames[i][0] = n);
			tnames[i][1] = d;
		}
	}

	if (!l) goto cleanup;
	*num = l;
	ret = ga_zalloc(*len_bytes = meta_len + l * sizeof(GaDeviceDescription));
	if (!ret) goto cleanup;

	u32 default_i = 0;

	{
		char *cret = (char*)ret + l * sizeof(GaDeviceDescription);
		for (u32 i = 0, j = 0; i < n; i++) {
			if (!tnames[i][0]) continue;
			u32 l = strlen(tnames[i][0]);
			if (!(ret[j].name = gaX_strdup(tnames[i][1]))) goto cleanup;
			ret[j].private = memcpy(cret, tnames[i][0], 1+l);
			cret += 1+l;
			free(tnames[i][0]);
			free(tnames[i][1]);
			if (strstr(ret[j].private, "default")) default_i = j;
			j++;
		}
		ga_free(tnames);
		tnames = NULL;
	}

	GaDeviceDescription tmp = ret[0];
	ret[0] = ret[default_i];
	ret[default_i] = tmp;

	snd_device_name_free_hint(hints);
	return ret;

cleanup:
	if (hints) snd_device_name_free_hint(hints);
	if (ret) {
		for (u32 i = 0; i < l; i++) ga_free(ret[i].name);
		ga_free(ret);
	}
	if (tnames) {
		for (u32 i = 0; i < l; i++) free(tnames[i][0]),
		                            free(tnames[i][1]);
		ga_free(tnames);
	}
	*num = *len_bytes = 0;
	return NULL;
}

static ga_result gaX_open(GaDevice *dev, const GaDeviceDescription *descr) {
	snd_pcm_format_t fmt;
	switch (dev->format.sample_fmt) {
		case GaSampleFormat_U8:  fmt = SND_PCM_FORMAT_U8;    break;
		case GaSampleFormat_S16: fmt = SND_PCM_FORMAT_S16;   break;
		case GaSampleFormat_S32: fmt = SND_PCM_FORMAT_S32;   break;
		case GaSampleFormat_F32: fmt = SND_PCM_FORMAT_FLOAT; break;
		default: return GA_ERR_MIS_PARAM;
	}

	switch (dev->class) {
		case GaDeviceClass_AsyncPush:
		case GaDeviceClass_SyncShared: break;
		case GaDeviceClass_Callback:
		case GaDeviceClass_SyncPipe:
			dev->class = GaDeviceClass_SyncShared;
	}

	ga_result res = GA_ERR_GENERIC;
	dev->impl = ga_alloc(sizeof(GaXDeviceImpl));
	if (!dev->impl) return GA_ERR_SYS_MEM;

	if (snd_pcm_open(&dev->impl->interface, descr->private, SND_PCM_STREAM_PLAYBACK, (dev->class == GaDeviceClass_AsyncPush) * SND_PCM_NONBLOCK) < 0) {
		ga_free(dev->impl);
		return GA_ERR_SYS_LIB;
	}

	snd_pcm_hw_params_t *params = NULL;
#define alloca ga_alloc
        snd_pcm_hw_params_alloca(&params);
#undef alloca
	if (!params) goto cleanup;
        acheck(snd_pcm_hw_params_any(dev->impl->interface, params));

        acheck(snd_pcm_hw_params_set_access(dev->impl->interface, params, (dev->class == GaDeviceClass_AsyncPush) ? SND_PCM_ACCESS_MMAP_INTERLEAVED : SND_PCM_ACCESS_RW_INTERLEAVED));
        acheck(snd_pcm_hw_params_set_format(dev->impl->interface, params, fmt));

        acheck(snd_pcm_hw_params_set_channels(dev->impl->interface, params, dev->format.num_channels));
        acheck(snd_pcm_hw_params_set_buffer_size(dev->impl->interface, params, dev->num_frames * ga_format_frame_size(&dev->format)));
	acheck(snd_pcm_hw_params_set_rate_near(dev->impl->interface, params, &dev->format.frame_rate, NULL));
	acheck(snd_pcm_hw_params(dev->impl->interface, params));

	if (dev->class == GaDeviceClass_AsyncPush) {
		snd_pcm_avail(dev->impl->interface);
		const snd_pcm_channel_area_t *areas;
		snd_pcm_uframes_t frames = dev->num_frames;
		acheck(snd_pcm_mmap_begin(dev->impl->interface, &areas, &dev->impl->last_offset, &frames));
		acheck(snd_pcm_mmap_commit(dev->impl->interface, dev->impl->last_offset, frames));
		if (snd_pcm_state(dev->impl->interface) != SND_PCM_STATE_PREPARED) goto cleanup;
		acheck(snd_pcm_start(dev->impl->interface));
	}

	ga_free(params);
	//todo latency

	if (dev->class == GaDeviceClass_AsyncPush) {
		dev->procs.get_buffer = gaX_get_buffer_async;
		dev->procs.queue = gaX_queue_async;
	} else {
		dev->procs.queue = gaX_queue_pipe;
	}

	return GA_OK;

cleanup:
	ga_free(params);
	snd_pcm_drain(dev->impl->interface);
	snd_pcm_close(dev->impl->interface);
	ga_free(dev->impl);
	dev->impl = NULL;
	return res;
}

static ga_result gaX_close(GaDevice *dev) {
	snd_pcm_drain(dev->impl->interface);
	snd_pcm_close(dev->impl->interface);
	ga_free(dev->impl);

	snd_config_update_free_global();
	// this just frees a global cache, that will be reinstated
	// transparently by alsa in the event that the library user creates and
	// then destroys multiple devices
	// but freeing it avoids false positives from valgrind/memtest
	return GA_OK;
}

static ga_result gaX_check(GaDevice *dev, u32 *num_frames) {
	//ga_info("state %d", snd_pcm_state(dev->impl->interface));
	snd_pcm_sframes_t avail = snd_pcm_avail(dev->impl->interface);
	if (avail < 0) {
		ga_warn("unable to query available buffers; code %li", avail);
		return GA_ERR_GENERIC; //negative is a 'code', but it's not specified what values it can take
	}
	*num_frames = avail / dev->num_frames;
	return GA_OK;
}

static void *gaX_get_buffer_async(GaDevice *dev) {
	// docs say of snd_pcm_mmap_begin:
	// 
	// > It is necessary to call the snd_pcm_avail_update() function
	// > directly before this call. Otherwise, this function can return a
	// > wrong count of available frames.
	// 
	// we shunt this responsibility onto the user; as documented, you must
	// call gaX_check before calling get_buffer

	const snd_pcm_channel_area_t *areas;
	snd_pcm_uframes_t frames = dev->num_frames;
	if (snd_pcm_mmap_begin(dev->impl->interface, &areas, &dev->impl->last_offset, &frames) != 0) return NULL;
	if (frames < dev->num_frames) {
		ga_warn("alsa shared memory buffer of insufficient size; this likely indicates an application bug (forgot to call ga_device_check())");
		goto fail;
	}

	if ((areas->first % 8) || (areas->step % 8)) {
		ga_err("WTF %u %u", areas->first, areas->step);
		goto fail;
	}

	if (areas->step != ga_format_frame_size(&dev->format) * 8) {
		ga_err("can't handle alsa buffer with step of '%u' bits (!= %u * 8)", areas->step, ga_format_sample_size(dev->format.sample_fmt));
	}

	if (!areas->addr) {
		ga_err("null mmap address; probably a buggy driver");
		goto fail;
	}

	return (char*)areas->addr + areas->first/8 + dev->impl->last_offset*ga_format_frame_size(&dev->format);

fail:;
	snd_pcm_sframes_t t = snd_pcm_mmap_commit(dev->impl->interface, dev->impl->last_offset, 0);
	if (t != 0) ga_warn("while decommitting empty buffer: %li", t);
	return NULL;
}
static ga_result gaX_queue_async(GaDevice *dev, void *buf) {
	snd_pcm_sframes_t written = snd_pcm_mmap_commit(dev->impl->interface, dev->impl->last_offset, dev->num_frames);

	if (written > 0 && written < dev->num_frames) {
		ga_warn("only wrote %li/%u frames", written, dev->num_frames);
		return GA_ERR_SYS_RUN;
	}
	if (written < 0) {
		ga_warn("alsa async mmap returned %li", written);
		return GA_ERR_SYS_LIB;
	}
	return GA_OK;
}

static ga_result gaX_queue_pipe(GaDevice *dev, void *buf) {
	snd_pcm_sframes_t written = snd_pcm_writei(dev->impl->interface, buf, dev->num_frames);
	// TODO: handle the below (particularly run)
	if (written == -EBADFD) return GA_ERR_INTERNAL; // PCM is not in the right state (SND_PCM_STATE_PREPARED or SND_PCM_STATE_RUNNING) 
	if (written == -EPIPE) return GA_ERR_SYS_RUN; // underrun
	if (written == -ESTRPIPE) return GA_ERR_GENERIC; // a suspend event occurred (stream is suspended and waiting for an application recovery)

	if (written != dev->num_frames) return GA_ERR_GENERIC; // underrun/signal
	return GA_OK;
}

GaXDeviceProcs gaX_deviceprocs_ALSA = { .enumerate=gaX_enumerate, .open=gaX_open, .check=gaX_check, .close=gaX_close };
