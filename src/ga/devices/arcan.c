#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <arcan_shmif.h>

struct GaXDeviceImpl {
	struct arcan_shmif_cont *acon;
	bool homemade;
};

#define GAX_ARCAN_AFMT ((GaFormat){ \
	.sample_fmt = _Generic((AUDIO_SAMPLE_TYPE)0, \
		u8 : GaSampleFormat_U8, \
		s16: GaSampleFormat_S16, \
		s32: GaSampleFormat_S32, \
		f32: GaSampleFormat_F32), \
	.frame_rate = ARCAN_SHMIF_SAMPLERATE, \
	.num_channels = ARCAN_SHMIF_ACHANNELS, })

static GaDeviceDescription *gaX_enumerate(u32 *num, u32 *len_bytes) {
	GaDeviceDescription *ret = ga_zalloc(sizeof(GaDeviceDescription));
	*ret = (GaDeviceDescription){.type=GaDeviceType_Arcan, .name="Arcan default audio device", .format=GAX_ARCAN_AFMT, .private=0};
	*num = 1; *len_bytes = sizeof(GaDeviceDescription);
	return ret;
}

static ga_result gaX_open(GaDevice *dev, const GaDeviceDescription *descr) {
	dev->format = GAX_ARCAN_AFMT;
	if (!(dev->impl = ga_alloc(sizeof(GaXDeviceImpl)))) return GA_ERR_SYS_MEM;
	dev->impl->homemade = false;
	dev->class = GaDeviceClass_AsyncPush;

	struct arcan_shmif_cont *acon = arcan_shmif_primary(SHMIF_INPUT);
	if (!acon) {
		struct arcan_shmif_cont c = arcan_shmif_open_ext(SHMIF_NOFLAGS, NULL, (struct shmif_open_ext){
			.type = SEGID_MEDIA, //just a guess; if you're a video game (or whatever), should have set that up already.  Could also reasonably be _LWA, _GAME, _APPLICATION...
			.title = "(Gorilla audio)",
			.ident = "",
		}, sizeof(struct shmif_open_ext));
		//todo is there a better way of detecting whether arcan_shmif_open_ext succeeded than c.addr?
		if (!c.addr) return GA_ERR_SYS_LIB;
		if (!(acon = ga_alloc(sizeof(struct arcan_shmif_cont)))) return GA_ERR_SYS_MEM;
		memcpy(acon, &c, sizeof(struct arcan_shmif_cont));
		dev->impl->homemade = true;
	}
	if (!arcan_shmif_lock(acon)) return GA_ERR_SYS_LIB;
	if (!arcan_shmif_resize_ext(acon, acon->w, acon->h, (struct shmif_resize_ext){
		.abuf_sz = dev->num_frames * ga_format_frame_size(dev->format),
		.abuf_cnt = dev->num_buffers,
		//.samplerate = dev->format.frame_rate, // ignored by arcan
	})) {
		arcan_shmif_unlock(acon);
		return GA_ERR_SYS_LIB;
	}
	if (!arcan_shmif_unlock(acon)) return GA_ERR_SYS_LIB;
	if (acon->abufsize != dev->num_frames * ga_format_frame_size(dev->format)
	    || dev->num_buffers != acon->abuf_cnt) return GA_ERR_SYS_LIB;;

	dev->impl->acon = acon;

	return GA_OK;
}

static ga_result gaX_close(GaDevice *dev) {
	if (dev->impl->homemade) {
		arcan_shmif_drop(dev->impl->acon);
		ga_free(dev->impl->acon);
	}
	ga_free(dev->impl);
	return GA_OK;
}

static ga_result gaX_check(GaDevice *dev, u32 *num_buffers) {
	struct arcan_shmif_cont *c = dev->impl->acon;
	*num_buffers = (c->abufsize - c->abufused) / (dev->num_frames * ga_format_frame_size(dev->format));
	return GA_OK;
}

static ga_result gaX_queue(GaDevice *dev, void *buf) {
	ga_result ret = GA_OK;

	struct arcan_shmif_cont *c = dev->impl->acon;

	if (!dev->impl->homemade) {
		struct {
			u64 magic;
			atomic_u8 resize_pending;
		} *debedebe = c->user;
		if (debedebe && debedebe->magic == 0xfeedface) {
			while (debedebe->resize_pending) ga_thread_yield(); /* :| */
		}
	}

	if (!arcan_shmif_lock(c)) return GA_ERR_SYS_LIB;
	usz desired = dev->num_frames * ga_format_frame_size(dev->format);
	usz avail = c->abufsize - c->abufused;
	if (avail < desired) goto out;
	memcpy(c->audb + c->abufused, buf, desired);
	c->abufused += desired;
	arcan_shmif_signal(c, SHMIF_SIGAUD);
out:
	arcan_shmif_unlock(c);

	return ret;
}

GaXDeviceProcs gaX_deviceprocs_Arcan = { .enumerate=gaX_enumerate, .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
