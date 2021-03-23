#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <alsa/asoundlib.h>

// TODO: consider using SND_PCM_ACCESS_MMAP_INTERLEAVED?
// it would require some restructuring elsewhere.  Probably not worth it,
// especially since most people aren't using straight alsa anymore

struct GaXDeviceImpl {
	snd_pcm_t *interface;
};

static ga_result gaX_open(GaDevice *dev) {
#define acheck(expr) do { if ((expr) < 0) { res = GA_ERR_SYS_LIB; goto cleanup; } } while (0)
	ga_result res = GA_OK;
	dev->impl = ga_alloc(sizeof(GaXDeviceImpl));
	if (!dev->impl) return GA_ERR_SYS_MEM;

	if (snd_pcm_open(&dev->impl->interface, "default", SND_PCM_STREAM_PLAYBACK, (dev->class == GaDeviceClass_PushAsync) * SND_PCM_NONBLOCK) < 0) {
		ga_free(dev->impl);
		return GA_ERR_SYS_LIB;
	}

	switch (dev->class) {
		case GaDeviceClass_PushAsync:
		case GaDeviceClass_PushSync: break;
		case GaDeviceClass_Callback:
			dev->class = GaDeviceClass_PushSync;
	}

	snd_pcm_hw_params_t *params = NULL;
#define alloca malloc
        snd_pcm_hw_params_alloca(&params);
#undef alloca
	if (!params) goto cleanup;
        acheck(snd_pcm_hw_params_any(dev->impl->interface, params));

        acheck(snd_pcm_hw_params_set_access(dev->impl->interface, params, SND_PCM_ACCESS_RW_INTERLEAVED));

	snd_pcm_format_t fmt;
	switch (dev->format.sample_fmt) {
		case GaSampleFormat_U8: fmt = SND_PCM_FORMAT_U8; break;
		case GaSampleFormat_S16: fmt = SND_PCM_FORMAT_S16; break;
		case GaSampleFormat_S32: fmt = SND_PCM_FORMAT_S32; break;
		case GaSampleFormat_F32: fmt = SND_PCM_FORMAT_FLOAT; break;
		default: res = GA_ERR_MIS_PARAM; goto cleanup;
	}
        acheck(snd_pcm_hw_params_set_format(dev->impl->interface, params, fmt));

        acheck(snd_pcm_hw_params_set_channels(dev->impl->interface, params, dev->format.num_channels));
        acheck(snd_pcm_hw_params_set_buffer_size(dev->impl->interface, params, dev->num_frames * ga_format_frame_size(&dev->format)));
	// this can transparently change the frame rate from under the user
	// TODO: should we let them pass an option to error if they can't get exactly the desired frame rate?
	acheck(snd_pcm_hw_params_set_rate_near(dev->impl->interface, params, &dev->format.frame_rate, NULL));
	acheck(snd_pcm_hw_params(dev->impl->interface, params));

	free(params);
	//todo latency

	return GA_OK;

cleanup:
	free(params);
	snd_pcm_drain(dev->impl->interface);
	snd_pcm_close(dev->impl->interface);
	ga_free(dev->impl);
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
	snd_pcm_sframes_t avail = snd_pcm_avail(dev->impl->interface);
	if (avail < 0) return GA_ERR_GENERIC; //negative is a 'code', but it's not specified what values it can take
	*num_frames = avail / dev->num_frames;
	return GA_OK;
}

static ga_result gaX_queue(GaDevice *dev, void *buf) {
	snd_pcm_sframes_t written = snd_pcm_writei(dev->impl->interface, buf, dev->num_frames);
	// TODO: handle the below (particularly run)
	if (written == -EBADFD) return GA_ERR_INTERNAL; // PCM is not in the right state (SND_PCM_STATE_PREPARED or SND_PCM_STATE_RUNNING) 
	if (written == -EPIPE) return GA_ERR_SYS_RUN; // underrun
	if (written == -ESTRPIPE) return GA_ERR_GENERIC; // a suspend event occurred (stream is suspended and waiting for an application recovery)

	if (written != dev->num_frames) return GA_ERR_GENERIC; // underrun/signal
	return GA_OK;
}

GaXDeviceProcs gaX_deviceprocs_ALSA = { .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
