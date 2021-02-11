#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

static ga_result gaX_open(GaDevice *dev) {
	int fd = open("/dev/dsp", O_WRONLY/*|O_NONBLOCK*/); //todo configurable
	if (fd < 0) return GA_ERR_SYS_IO;

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
	switch (dev->format.sample_fmt) {
#ifdef AFMT_U8
		case GaSampleFormat_U8:  fmt = AFMT_U8;     break;
#endif
#ifdef AFMT_S16_NE
		case GaSampleFormat_S16: fmt = AFMT_S16_NE; break;
#endif
#ifdef AFMT_S32_NE
		case GaSampleFormat_S32: fmt = AFMT_S32_NE; break;
#endif
		default: goto cleanup;
	}
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &(int){fmt}) == -1) goto cleanup;

	audio_buf_info info;
	if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &info) == -1) goto cleanup;
	dev->num_buffers = info.fragstotal;
	dev->num_samples = info.fragsize / ga_format_sample_size(&dev->format);
	if (dev->num_buffers < 2) goto cleanup;

	dev->impl = (void*)(usz)fd;

	return GA_OK;

cleanup:
	close(fd);
	return GA_ERR_SYS_LIB;
}

static ga_result gaX_close(GaDevice *dev) {
	return close((int)(usz)dev->impl) ? GA_ERR_SYS_IO : GA_OK;
}

static u32 gaX_check(GaDevice *dev) {
	int fd = (int)(usz)dev->impl;
	audio_buf_info i;
	if (ioctl(fd, SNDCTL_DSP_GETOSPACE, &i) == -1) return 0;

	return i.fragments < 0 ? 0 : i.fragments;
}

static ga_result gaX_queue(GaDevice *dev, void *buf) {
	ssz sz = dev->num_samples * ga_format_sample_size(&dev->format);
	ssz written = write((int)(usz)dev->impl, buf, sz);
	return sz==written ? GA_OK : GA_ERR_SYS_IO;
}

GaXDeviceProcs gaX_deviceprocs_OSS = { .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
