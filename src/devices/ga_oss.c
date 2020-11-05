#include "gorilla/ga.h"

#include "gorilla/devices/ga_oss.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

ga_DeviceImpl_OSS* gaX_device_open_OSS(gc_int32 numBuffers, gc_int32 numSamples, ga_Format* format) {
	ga_DeviceImpl_OSS* ret = (ga_DeviceImpl_OSS*)gcX_ops->allocFunc(sizeof(ga_DeviceImpl_OSS));
	if (!ret) return NULL;

	ret->numBuffers = numBuffers;
	ret->numSamples = numSamples;
	memcpy(&ret->format, format, sizeof(ga_Format));

	ret->fd = open("/dev/dsp", O_WRONLY, O_NONBLOCK); //todo configurable
	if (ret->fd < 0) goto cleanup;

	// If we request a mode and it's not supported by the hardware, this tells
	// the mixer to fake it in software.
	// We don't do error-checks intentionally; per the freebsd header:
	// > It's[sic] _error return must be ignored_ since normally this call will return erno=EINVAL.
	// Note: this must be the first ioctl performed on the device.
	ioctl(ret->fd, SNDCTL_DSP_COOKEDMODE, &(int){1});

	// latency requests
	// range is [0, 10]; lower is less latent
	// 5 is the default
	// I chose 3 arbitrarily, and it should probably be changed.  It happens to be the default used by byuu's ruby
	if (ioctl(ret->fd, SNDCTL_DSP_POLICY, &(int){3}) == -1) goto cleanup;

	if (ioctl(ret->fd, SNDCTL_DSP_SPEED, &(int){ret->format.sampleRate}) == -1) goto cleanup;
	if (ioctl(ret->fd, SNDCTL_DSP_CHANNELS, &(int){ret->format.numChannels}) == -1) goto cleanup;
	int fmt;
	switch (ret->format.bitsPerSample) {
		case 16: fmt = AFMT_S16_LE; break;
		case 24: fmt = AFMT_S24_LE; break;
		case 32: fmt = AFMT_S32_LE; break;
		default: goto cleanup;
	}
	if (ioctl(ret->fd, SNDCTL_DSP_SETFMT, &(int){fmt}) == -1) goto cleanup;

	return ret;

cleanup:
	if (ret->fd >= 0) close(ret->fd);
	gcX_ops->freeFunc(ret);
	return NULL;
}

gc_result gaX_device_close_OSS(ga_DeviceImpl_OSS* in_device) {
	gc_result ret = close(in_device->fd) ? GC_ERROR_GENERIC : GC_SUCCESS;
	gcX_ops->freeFunc(in_device);
	return ret;
}

gc_int32 gaX_device_check_OSS(ga_DeviceImpl_OSS* in_device) {
	return in_device->numBuffers; //TODO is this right?
}

gc_result gaX_device_queue_OSS(ga_DeviceImpl_OSS* in_device, void* in_buffer) {
	gc_ssize sz = in_device->numSamples * ga_format_sampleSize(&in_device->format);
	gc_ssize written = write(in_device->fd, in_buffer, sz);
	return sz==written ? GC_SUCCESS : GC_ERROR_GENERIC;
}
