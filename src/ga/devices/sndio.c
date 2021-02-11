#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <sndio.h>

struct GaXDeviceImpl {
	struct sio_hdl *hdl;
};

static ga_result gaX_open(GaDevice *dev) {
	if (dev->format.sample_fmt < 0) return GA_ERR_MIS_UNSUP;

	dev->impl = ga_alloc(sizeof(GaXDeviceImpl));
	if (!dev->impl) return GA_ERR_SYS_MEM;
	dev->impl->hdl = sio_open(SIO_DEVANY, SIO_PLAY, 0/*sync*/);
	if (!dev->impl->hdl) goto fail;

	struct sio_par par;
       	sio_initpar(&par);
	par.bits = dev->format.sample_fmt << 3;
	par.bps = dev->format.sample_fmt;
	par.sig = dev->format.sample_fmt != GaSampleFormat_U8;
	par.le = ENDIAN(1, 0);
	par.pchan = dev->format.num_channels;
	par.rate = dev->format.sample_rate;
	par.appbufsz = dev->num_buffers * dev->num_samples;
	par.round = dev->num_samples;
	par.xrun = SIO_SYNC;
	if (!sio_setpar(dev->impl->hdl, &par)) goto fail;
	if (par.le != ENDIAN(1, 0)
	    || par.bps != par.bits >> 3
	    || par.sig != (dev->format.sample_fmt != GaSampleFormat_U8)) goto fail;
	switch (par.bits) {
		case  8: dev->format.sample_fmt = GaSampleFormat_U8;
		case 16: dev->format.sample_fmt = GaSampleFormat_S16;
		case 32: dev->format.sample_fmt = GaSampleFormat_S32;
		default: goto fail;
	}
	dev->format.num_channels = par.pchan;
	dev->format.sample_rate = par.rate;
	dev->num_samples = par.round;
	dev->num_buffers = par.appbufsz / par.round;
	if (!sio_start(dev->impl->hdl)) goto fail;

	return GA_OK;

fail:
	if (dev->impl->hdl) sio_close(dev->impl->hdl);
	ga_free(dev->impl);
	return GA_ERR_SYS_LIB;
}

static ga_result gaX_close(GaDevice *dev) {
	ga_result ret = sio_stop(dev->impl->hdl) ? GA_OK : GA_ERR_SYS_LIB;
	sio_close(dev->impl->hdl);
	ga_free(dev->impl);
	return ret;
}

static u32 gaX_check(GaDevice *dev) {
	return dev->num_buffers; //TODO
}

static ga_result gaX_queue(GaDevice *dev, void *buf) {
	if (sio_write(dev->impl->hdl, buf, ga_format_sample_size(&dev->format) * dev->num_samples) != ga_format_sample_size(&dev->format) * dev->num_samples) return GA_ERR_SYS_LIB;
	return GA_OK;
}

GaXDeviceProcs gaX_deviceprocs_sndio = { .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
