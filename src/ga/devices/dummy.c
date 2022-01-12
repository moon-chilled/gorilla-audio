#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

static GaDeviceDescription *gaX_enumerate(u32 *num, u32 *len_bytes) {
	*num = 1;
	*len_bytes = sizeof(GaDeviceDescription);
	GaDeviceDescription *ret = ga_alloc(sizeof(GaDeviceDescription));
	*ret = (GaDeviceDescription){
		.name = "WAV outputter",
		.format = {.num_channels=2, .frame_rate=48000, .sample_fmt=GaSampleFormat_S16},
	};
	return ret;
}

static ga_result gaX_open(GaDevice *dev, const GaDeviceDescription *descr) { dev->class = GaDeviceClass_AsyncPush; return GA_OK; }

static ga_result gaX_close(GaDevice *dev) { return GA_OK; }

static ga_result gaX_check(GaDevice *dev, u32 *num_buffers) { *num_buffers = dev->num_buffers; return GA_OK; }

static ga_result gaX_queue(GaDevice *dev, void *buf) { return GA_OK; }

GaXDeviceProcs gaX_deviceprocs_dummy = { .enumerate=gaX_enumerate, .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
