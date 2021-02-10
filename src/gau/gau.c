#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

/* High-Level Manager */
struct GauManager {
	GauThreadPolicy thread_policy;
	GaThread *mix_thread, *stream_thread;
	GaDevice *device;
	GaMixer *mixer;
	GaStreamManager *stream_mgr;
	GaFormat format;
	bool kill_threads;
};

static ga_result mix_thread(void *context) {
	GauManager *ctx = context;
	while (!ctx->kill_threads) {
		u32 numToQueue = ga_device_check(ctx->device);
		while (numToQueue--) {
			s16 *buf = ga_device_get_buffer(ctx->device);
			if (!buf) continue; //avoid churning cpu, even at the cost of underruns
			ga_mixer_mix(ctx->mixer, buf);
			ga_device_queue(ctx->device, buf);
		}
		ga_thread_sleep(5);
	}
	return GA_OK;
}
static ga_result stream_thread(void *context) {
	GauManager *ctx = context;
	while (!ctx->kill_threads) {
		ga_stream_manager_buffer(ctx->stream_mgr);
		ga_thread_sleep(50);
	}
	return GA_OK;
}
GauManager *gau_manager_create(void) {
	return gau_manager_create_custom(NULL, GauThreadPolicy_Single, NULL, NULL);
}
GauManager *gau_manager_create_custom(GaDeviceType *dev_type,
                                       GauThreadPolicy thread_policy,
                                       u32 *num_buffers,
				       u32 *num_samples) {
	GauManager *ret = memset(ga_alloc(sizeof(GauManager)), 0, sizeof(GauManager));

	assert(thread_policy == GauThreadPolicy_Single
	       || thread_policy == GauThreadPolicy_Multi);
	num_buffers = num_buffers ? num_buffers : &(u32){4};
	num_samples = num_samples ? num_samples : &(u32){512};
	assert(*num_buffers >= 2);
	assert(*num_samples >= 128);

	/* Open device */
	ret->format.bits_per_sample = 16;
	ret->format.num_channels = 2;
	ret->format.sample_rate = 48000;
	ret->device = ga_device_open(dev_type, num_buffers, num_samples, &ret->format);
	if (!ret->device) goto fail;

	/* Initialize mixer */
	ret->mixer = ga_mixer_create(&ret->format, *num_samples);
	if (!ret->mixer) goto fail;
	ret->stream_mgr = ga_stream_manager_create();
	if (!ret->stream_mgr) goto fail;

	/* Create and run mixer and stream threads */
	ret->thread_policy = thread_policy;
	ret->kill_threads = false;
	if (ret->thread_policy == GauThreadPolicy_Multi) {
		ret->mix_thread = ga_thread_create(mix_thread, ret, GaThreadPriority_Highest, 64 * 1024);
		ret->stream_thread = ga_thread_create(stream_thread, ret, GaThreadPriority_Highest, 64 * 1024);
	} else {
		ret->mix_thread = NULL;
		ret->stream_thread = NULL;
	}

	return ret;
fail:
	if (ret) {
		if (ret->device) ga_device_close(ret->device);
		if (ret->mixer) ga_mixer_destroy(ret->mixer);
		if (ret->stream_mgr) ga_stream_manager_destroy(ret->stream_mgr);
		ga_free(ret);
	}
	return NULL;
}
void gau_manager_update(GauManager *mgr) {
	ga_mixer_dispatch(mgr->mixer);
	if (mgr->thread_policy == GauThreadPolicy_Multi) return;
	GaMixer *mixer = mgr->mixer;
	GaDevice *dev = mgr->device;
	u32 numToQueue = ga_device_check(dev);
	while (numToQueue--) {
		s16 *buf = ga_device_get_buffer(dev);
		if (!buf) continue;
		ga_mixer_mix(mixer, buf);
		ga_device_queue(dev, buf);
	}
	ga_stream_manager_buffer(mgr->stream_mgr);
}

GaMixer *gau_manager_mixer(GauManager *mgr) {
	return mgr->mixer;
}
GaStreamManager *gau_manager_stream_manager(GauManager *mgr) {
	return mgr->stream_mgr;
}
GaDevice *gau_manager_device(GauManager *mgr) {
  return mgr->device;
}

void gau_manager_destroy(GauManager *mgr) {
	if (mgr->thread_policy == GauThreadPolicy_Multi) {
		mgr->kill_threads = true;
		ga_thread_join(mgr->stream_thread);
		ga_thread_join(mgr->mix_thread);
		ga_thread_destroy(mgr->stream_thread);
		ga_thread_destroy(mgr->mix_thread);
	}

	/* Clean up mixer and stream manager */
	ga_stream_manager_destroy(mgr->stream_mgr);
	ga_mixer_destroy(mgr->mixer);
	ga_device_close(mgr->device);
	ga_free(mgr);
}

/* On-Finish Callbacks */
void gau_on_finish_destroy(GaHandle *handle, void *ctx) {
	ga_handle_destroy(handle);
}


GaMemory *gau_load_memory_file(const char *fname) {
	GaDataSource *datasrc = gau_data_source_create_file(fname);
	if (!datasrc) return NULL;
	GaMemory *ret = ga_memory_create_data_source(datasrc);
	ga_data_source_release(datasrc);
	return ret;
}

static GaSampleSource *gau_sample_source_create(GaDataSource *data, GauAudioType format) {
	if (format == GauAudioType_Autodetect) {
		if (!(ga_data_source_flags(data) & GaDataAccessFlag_Seekable)) return NULL;
		char buf[4];
		if (ga_data_source_read(data, buf, 4, 1) != 1) return NULL;
		if (!ga_isok(ga_data_source_seek(data, -4, GaSeekOrigin_Cur))) return NULL;
		if (!memcmp(buf, "OggS", 4)) {
			// ogg file format is:
			// 4 'OggS'
			// 1 version, always 0
			// 1 header type; (1=continued)|(2=first)|(4=last)
			// 8 granule position
			// 4 stream serial number; ~uuid, not actually serial
			// 4 page sequence number
			// 4 crc
			// 1 number of segments
			// then 'segments' bytes of junk
			// then the actual data
			// ogg has:
			// 1 packtype
			// 6 'vorbis'
			// opus has:
			// 4 'Opus'
			if (!ga_isok(ga_data_source_seek(data, 26, GaSeekOrigin_Cur))) return NULL;
			u8 nseg;
			if (ga_data_source_read(data, &nseg, 1, 1) != 1) return NULL;
			if (!ga_isok(ga_data_source_seek(data, nseg, GaSeekOrigin_Cur))) return NULL;
			char buf[7];
			if (ga_data_source_read(data, buf, 1, 7) != 7) return NULL;
			if (!ga_isok(ga_data_source_seek(data, -7-27-nseg, GaSeekOrigin_Cur))) return NULL;

			if (!memcmp(buf, "Opus", 4)) format = GauAudioType_Opus;
			else if (!memcmp(buf+1, "vorbis", 6)) format = GauAudioType_Vorbis;
			else return NULL;
		} else if (!memcmp(buf, "RIFF", 4)) {
			format = GauAudioType_Wav;
		} else if (!memcmp(buf, "fLaC", 4)) {
			format = GauAudioType_Flac;
		} else {
			return NULL;
		}
	}

	if (format == GauAudioType_Wav) return gau_sample_source_create_wav(data);
	if (format == GauAudioType_Flac) return gau_sample_source_create_flac(data);
	if (format == GauAudioType_Opus) return gau_sample_source_create_opus(data);
	if (format == GauAudioType_Vorbis) return gau_sample_source_create_vorbis(data);
	return NULL;
}

GaSound *gau_load_sound_file(const char *fname, GauAudioType format) {
	GaSound *ret = NULL;
	GaDataSource *data = gau_data_source_create_file(fname);
	if (!data) return NULL;
	GaSampleSource *sample_src = gau_sample_source_create(data, format);
	ga_data_source_release(data);
	if (sample_src) {
		ret = ga_sound_create_sample_source(sample_src);
		ga_sample_source_release(sample_src);
	}
	return ret;
}

GaHandle *gau_create_handle_sound(GauManager *mgr, GaSound *sound,
                                   GaCbHandleFinish callback, void *context,
				   GauSampleSourceLoop **loop_src) {
	GaHandle *ret = NULL;
	GaSampleSource *src = gau_sample_source_create_sound(sound);
	if (!src) return NULL;
	GaSampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_enable(*loop_src);
		ga_sample_source_release(src);
		src2 = gau_sample_source_loop_sample_source(*loop_src);
	}
	if (src2) {
		ret = ga_handle_create(mgr->mixer, src2);
		if(src == src2)
			ga_sample_source_release(src2);
		ga_handle_set_callback(ret, callback, context);
	}
	return ret;
}

GaHandle *gau_create_handle_memory(GauManager *mgr, GaMemory *memory, GauAudioType format,
                                    GaCbHandleFinish callback, void *context,
                                    GauSampleSourceLoop **loop_src) {
	GaHandle *ret = NULL;
	GaDataSource *data = gau_data_source_create_memory(memory);
	if (!data) return NULL;
	GaSampleSource *src = gau_sample_source_create(data, format);
	if (!src) return NULL;

	GaSampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_enable(*loop_src);
		ga_sample_source_release(src);
		src2 = gau_sample_source_loop_sample_source(*loop_src);
	}
	if (src2) {
		ret = ga_handle_create(mgr->mixer, src2);
		if(src == src2)
			ga_sample_source_release(src2);
		ga_handle_set_callback(ret, callback, context);
	}
	return ret;
}

GaHandle *gau_create_handle_buffered_samples(GauManager *mgr, GaSampleSource *src,
                                             GaCbHandleFinish callback, void *context,
                                             GauSampleSourceLoop **loop_src) {
	if (!src) return NULL;

	ga_sample_source_acquire(src);

	GaHandle *ret = NULL;

	GaSampleSource *src2 = src;
	if (loop_src) {
		*loop_src = gau_sample_source_create_loop(src);
		gau_sample_source_loop_enable(*loop_src);
		ga_sample_source_release(src);
		src2 = gau_sample_source_loop_sample_source(*loop_src);
	}
	if (src2) {
		GaSampleSource *streamSampleSrc = gau_sample_source_create_stream(mgr->stream_mgr, src2, 131072);
		if(src == src2) ga_sample_source_release(src2);
		if (streamSampleSrc) {
			ret = ga_handle_create(mgr->mixer, streamSampleSrc);
			ga_sample_source_release(streamSampleSrc);
			ga_handle_set_callback(ret, callback, context);
		}
	}
	return ret;
}

GaHandle *gau_create_handle_buffered_data(GauManager *mgr,
                                           GaDataSource *data, GauAudioType format,
                                           GaCbHandleFinish callback, void *context,
                                           GauSampleSourceLoop **loop_src) {
	if (!data) return NULL;
	GaSampleSource *src = gau_sample_source_create(data, format);
	GaHandle *ret = gau_create_handle_buffered_samples(mgr, src, callback, context, loop_src);
	ga_sample_source_release(src);
	return ret;
}

GaHandle *gau_create_handle_buffered_file(GauManager *mgr,
                                          const char *filename, GauAudioType format,
                                          GaCbHandleFinish callback, void *context,
                                          GauSampleSourceLoop** loop_src) {
	GaDataSource *data = gau_data_source_create_file(filename);
	if (!data) return NULL;
	GaHandle *ret = gau_create_handle_buffered_data(mgr, data, format, callback, context, loop_src);
	ga_data_source_release(data);
	return ret;
}
