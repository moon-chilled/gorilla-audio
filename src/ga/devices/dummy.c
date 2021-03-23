#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

static ga_result gaX_open(GaDevice *dev) { dev->class = GaDeviceClass_PushAsync; return GA_OK; }

static ga_result gaX_close(GaDevice *dev) { return GA_OK; }

static ga_result gaX_check(GaDevice *dev, u32 *num_buffers) { *num_buffers = dev->num_buffers; return GA_OK; }

static ga_result gaX_queue(GaDevice *dev, void *buf) { return GA_OK; }

GaXDeviceProcs gaX_deviceprocs_dummy = { .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
