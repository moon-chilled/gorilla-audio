/** OSS Device Implementation.
 *
 *  \file ga_oss.h
 */

#ifndef _GORILLA_GA_OSS_H
#define _GORILLA_GA_OSS_H

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

#include "gorilla/ga_internal.h"

typedef struct ga_DeviceImpl_OSS
{
  GA_DEVICE_HEADER
  int fd;
} ga_DeviceImpl_OSS;

ga_DeviceImpl_OSS* gaX_device_open_OSS(gc_int32 in_numBuffers,
                                       gc_int32 in_numSamples,
                                       ga_Format* in_format);
gc_int32 gaX_device_check_OSS(ga_DeviceImpl_OSS* in_device);
gc_result gaX_device_queue_OSS(ga_DeviceImpl_OSS* in_device,
                               void* in_buffer);
gc_result gaX_device_close_OSS(ga_DeviceImpl_OSS* in_device);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GORILLA_GA_OSS_H */
