#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <pulse/simple.h>
#include <pulse/error.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

struct GaXDeviceImpl {
	pa_simple *interface;
};

static ga_result gaX_open(GaDevice *dev) {
	pa_sample_spec spec;

	switch (dev->format.bits_per_sample) {
		case  8: spec.format = PA_SAMPLE_U8;    break;
		case 16: spec.format = PA_SAMPLE_S16LE; break;
		case 24: spec.format = PA_SAMPLE_S24LE; break;
		case 32: spec.format = PA_SAMPLE_S32LE; break;
		default: return GA_ERR_MIS_PARAM;
	}

	dev->impl = ga_alloc(sizeof(GaXDeviceImpl));
	if (!dev->impl) return GA_ERR_SYS_MEM;

	spec.channels = dev->format.num_channels;
	spec.rate = dev->format.sample_rate;

	dev->impl->interface = pa_simple_new(NULL,              // default server
	                                     "Gorilla Audio",   // application name
	                                     PA_STREAM_PLAYBACK,// direction
	                                     NULL,              // default device
	                                     "Gorilla Audio",   // stream description
	                                     &spec,             // sample fmt
	                                     NULL,              // default channelmap
	                                     NULL,              // default buffering attributes
	                                     NULL);             // if there was an error, we don't care which one

	if (!dev->impl->interface) goto cleanup;

	return GA_OK;

cleanup:
	ga_free(dev->impl);
	return GA_ERR_SYS_LIB;
}

static ga_result gaX_close(GaDevice *dev) {
	pa_simple_free(dev->impl->interface);
	ga_free(dev->impl);
	return GA_OK;
}

static s32 gaX_check(GaDevice *dev) {
	return dev->num_buffers; //TODO is this right?
}

static ga_result gaX_queue(GaDevice *dev, void *buf) {
	int res = pa_simple_write(dev->impl->interface, buf, dev->num_samples * ga_format_sample_size(&dev->format), NULL);
	return res<0 ? GA_OK : GA_ERR_SYS_LIB;
}

GaXDeviceProcs gaX_deviceprocs_PulseAudio = { gaX_open, gaX_check, gaX_queue, gaX_close };
