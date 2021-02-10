#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <string.h>

#include <pulse/pulseaudio.h>
#include <pulse/error.h>

struct GaXDeviceImpl {
	pa_mainloop *mainloop;
	pa_context *context;
	pa_stream *stream;

	bool looped;
};

#define ptcheck(e) do { if (!(e)) goto fail; } while (0)
#define pacheck(e) do { if ((e) < 0) goto fail; } while (0)

static ga_result gaX_open(GaDevice *dev) {
	pa_sample_spec spec;

	switch (dev->format.bits_per_sample) {
		case  8: spec.format = PA_SAMPLE_U8;    break;
		case 16: spec.format = PA_SAMPLE_S16NE; break;
		case 24: spec.format = PA_SAMPLE_S24NE; break;
		case 32: spec.format = PA_SAMPLE_S32NE; break;
		default: return GA_ERR_MIS_PARAM;
	}

	spec.channels = dev->format.num_channels;
	spec.rate = dev->format.sample_rate;

	dev->impl = ga_alloc(sizeof(GaXDeviceImpl));
	if (!dev->impl) return GA_ERR_SYS_MEM;
	memset(dev->impl, 0, sizeof(*dev->impl));

	ptcheck(dev->impl->mainloop = pa_mainloop_new());

	ptcheck(dev->impl->context = pa_context_new(pa_mainloop_get_api(dev->impl->mainloop), "(Gorilla audio)"));

	pacheck(pa_context_connect(dev->impl->context, NULL/*default server*/, PA_CONTEXT_NOAUTOSPAWN/*libpulse but no pulsed? let's not make matters worse*/, NULL));

	for (pa_context_state_t s;;) {
		pa_mainloop_iterate(dev->impl->mainloop, 1/*block*/, NULL);
		s = pa_context_get_state(dev->impl->context);
		if (s == PA_CONTEXT_READY) break;
		else if (!PA_CONTEXT_IS_GOOD(s)) goto fail;
	}

	ptcheck(dev->impl->stream = pa_stream_new(dev->impl->context, "(Gorilla audio)", &spec, NULL/*use default channel map*/));
	

	pa_buffer_attr ba = {
		.maxlength = dev->num_samples * ga_format_sample_size(&dev->format) * dev->num_buffers,
		.tlength   = dev->num_samples * ga_format_sample_size(&dev->format) * dev->num_buffers,
		.prebuf    = -1, //should be 0?
		.minreq    = dev->num_samples * ga_format_sample_size(&dev->format),
	};

	//todo PA_STREAM_FIX_FORMAT, FIX_CHANNELS
	pacheck(pa_stream_connect_playback(dev->impl->stream, NULL/*default sink*/, &ba, PA_STREAM_ADJUST_LATENCY, NULL, NULL));

	for (pa_stream_state_t s;;) {
		pa_mainloop_iterate(dev->impl->mainloop, 1, NULL);
		s = pa_stream_get_state(dev->impl->stream);
		if (s == PA_STREAM_READY) break;
		else if (!PA_STREAM_IS_GOOD(s)) goto fail;
	}

	const pa_buffer_attr *na = pa_stream_get_buffer_attr(dev->impl->stream);
	if (!na) goto fail;
	dev->num_samples = na->minreq / ga_format_sample_size(&dev->format);
	dev->num_buffers = na->tlength / dev->num_samples;

	return GA_OK;

fail:
	if (dev->impl->stream) {
		pa_stream_disconnect(dev->impl->stream);
		pa_stream_unref(dev->impl->stream);
	}
	if (dev->impl->context) {
		pa_context_disconnect(dev->impl->context);
		pa_context_unref(dev->impl->context);
	}
	if (dev->impl->mainloop) pa_mainloop_free(dev->impl->mainloop);
	ga_free(dev->impl);
	return GA_ERR_SYS_LIB;
}

static ga_result gaX_close(GaDevice *dev) {
	pa_stream_disconnect(dev->impl->stream);
	pa_stream_unref(dev->impl->stream);
	pa_context_disconnect(dev->impl->context);
	pa_context_unref(dev->impl->context);
	pa_mainloop_free(dev->impl->mainloop);
	ga_free(dev->impl);
	return GA_OK;
}

static u32 gaX_check(GaDevice *dev) {
	pa_mainloop_iterate(dev->impl->mainloop, 0, NULL);
	dev->impl->looped = true;
	usz r = pa_stream_writable_size(dev->impl->stream);
	if (r == (usz)-1) return 0;
	return r / (dev->num_samples * ga_format_sample_size(&dev->format));
}

static ga_result gaX_queue(GaDevice *dev, void *buf) {
	if (!dev->impl->looped) pa_mainloop_iterate(dev->impl->mainloop, 0, NULL);
	dev->impl->looped = false;
	int res = pa_stream_write(dev->impl->stream, buf, dev->num_samples * ga_format_sample_size(&dev->format), NULL, 0, PA_SEEK_RELATIVE);
	return res==0 ? GA_OK : GA_ERR_SYS_LIB;
}

GaXDeviceProcs gaX_deviceprocs_PulseAudio = { gaX_open, gaX_check, gaX_queue, gaX_close };
