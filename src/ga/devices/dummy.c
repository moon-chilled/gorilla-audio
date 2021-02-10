#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

static ga_result gaX_open(GaDevice *dev) { return GA_OK; }

static ga_result gaX_close(GaDevice *dev) { return GA_OK; }

static u32 gaX_check(GaDevice *dev) { return dev->num_buffers; }

static ga_result gaX_queue(GaDevice *dev, void *buf) { return GA_OK; }

GaXDeviceProcs gaX_deviceprocs_dummy = { .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
