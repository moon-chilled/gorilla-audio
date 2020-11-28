#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <pulse/simple.h>
#include <pulse/error.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

struct gaX_DeviceImpl {
	pa_simple *interface;
};

static gc_result gaX_open(ga_Device *dev) {
	dev->impl = gcX_ops->allocFunc(sizeof(gaX_DeviceImpl));
	if (!dev->impl) return GC_ERROR_GENERIC;

	pa_sample_spec spec;

	switch (dev->format.bits_per_sample) {
		case  8: spec.format = PA_SAMPLE_U8;    break;
		case 16: spec.format = PA_SAMPLE_S16LE; break;
		case 24: spec.format = PA_SAMPLE_S24LE; break;
		case 32: spec.format = PA_SAMPLE_S32LE; break;
		default: goto cleanup;
	}

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

	return GC_SUCCESS;

cleanup:
	if (dev->impl) gcX_ops->freeFunc(dev->impl);
	return GC_ERROR_GENERIC;
}

static gc_result gaX_close(ga_Device *dev) {
	pa_simple_free(dev->impl->interface);
	gcX_ops->freeFunc(dev->impl);
	return GC_SUCCESS;
}

static gc_int32 gaX_check(ga_Device *dev) {
	return dev->num_buffers; //TODO is this right?
}

static gc_result gaX_queue(ga_Device *dev, void *buf) {
	int res = pa_simple_write(dev->impl->interface, buf, dev->num_samples * ga_format_sampleSize(&dev->format), NULL);
	return res<0 ? GC_SUCCESS : GC_ERROR_GENERIC;
}

gaX_DeviceProcs gaX_deviceprocs_PulseAudio = { gaX_open, gaX_check, gaX_queue, gaX_close };
