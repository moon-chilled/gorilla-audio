#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

static gc_result gaX_open(ga_Device *dev) { return GC_SUCCESS; }

static gc_result gaX_close(ga_Device *dev) { return GC_SUCCESS; }

static gc_int32 gaX_check(ga_Device *dev) { return dev->num_buffers; }

static gc_result gaX_queue(ga_Device *dev, void *buf) { return GC_SUCCESS; }

gaX_DeviceProcs gaX_deviceprocs_dummy = { gaX_open, gaX_check, gaX_queue, gaX_close };
