#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

static gc_result gaX_open(ga_Device *dev) {
	int fd = open("/dev/dsp", O_WRONLY, O_NONBLOCK); //todo configurable
	if (fd < 0) goto cleanup;

	// If we request a mode and it's not supported by the hardware, this tells
	// the mixer to fake it in software.
	// We don't do error-checks intentionally; per the freebsd header:
	// > It's[sic] _error return must be ignored_ since normally this call will return erno=EINVAL.
	// Note: this must be the first ioctl performed on the device.
#ifdef SNDCTL_DSP_COOKEDMODE
	ioctl(fd, SNDCTL_DSP_COOKEDMODE, &(int){1});
#endif

	// latency requests
	// range is [0, 10]; lower is less latent
	// 5 is the default
	// I chose 3 arbitrarily, and it should probably be changed.  It happens to be the default used by byuu's ruby
#ifdef SNDCTL_DSP_POLICY
	if (ioctl(fd, SNDCTL_DSP_POLICY, &(int){3}) == -1) goto cleanup;
#endif

	if (ioctl(fd, SNDCTL_DSP_SPEED, &(int){dev->format.sample_rate}) == -1) goto cleanup;
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &(int){dev->format.num_channels}) == -1) goto cleanup;
	int fmt;
	switch (dev->format.bits_per_sample) {
		// 8-bit being unsigned is consistent with other frameworks
		// ga_openal.c uses AL_FORMAT_*8, which is apparently (http://forum.lwjgl.org/index.php?topic=4058.0) unsigned
		// xaudio2 uses unsigned for 8-bit audio (https://docs.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-createsourcevoice),
		//  though ga_xaudio2.cc currently doesn't take advantage of this (hardcodes 16-bit)
#ifdef AFMT_U8
		case  8: fmt = AFMT_U8;     break;
#endif
		// TODO should these be _NE (native endian) instead?  Investigate else, preferrably test on be hw
#ifdef AFMT_S16_LE
		case 16: fmt = AFMT_S16_LE; break;
#endif
#ifdef AFMT_S24_LE
		case 24: fmt = AFMT_S24_LE; break;
#endif
#ifdef AFMT_S32_LE
		case 32: fmt = AFMT_S32_LE; break;
#endif
		default: goto cleanup;
	}
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &(int){fmt}) == -1) goto cleanup;
	dev->impl = (void*)(gc_size)fd;

	return GC_SUCCESS;

cleanup:
	if (fd >= 0) close(fd);
	return GC_ERROR_GENERIC;
}

static gc_result gaX_close(ga_Device *dev) {
	return close((int)(gc_size)dev->impl) ? GC_ERROR_GENERIC : GC_SUCCESS;
}

static gc_int32 gaX_check(ga_Device *dev) {
	return dev->num_buffers; //TODO is this right?
}

static gc_result gaX_queue(ga_Device *dev, void *buf) {
	gc_ssize sz = dev->num_samples * ga_format_sample_size(&dev->format);
	gc_ssize written = write((int)(gc_size)dev->impl, buf, sz);
	return sz==written ? GC_SUCCESS : GC_ERROR_GENERIC;
}

gaX_DeviceProcs gaX_deviceprocs_OSS = { gaX_open, gaX_check, gaX_queue, gaX_close };
