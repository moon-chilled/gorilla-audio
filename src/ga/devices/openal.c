#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include "al.h"
#include "alc.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

struct GaXDeviceImpl {
	struct ALCdevice *dev;
	struct ALCcontext *context;
	u32 *hw_buffers;
	u32 hw_source;
	u32 next_buffer;
	u32 empty_buffers;
};


const ga_result openal_to_ga(ALuint error) {
	switch (error) {
		case AL_NO_ERROR: puts("OpenAL error - None"); return GA_ERR_INTERNAL;
		case AL_INVALID_NAME: puts("OpenAL error - Invalid name."); return GA_ERR_INTERNAL;
		case AL_INVALID_ENUM: puts("OpenAL error - Invalid enum."); return GA_ERR_INTERNAL;
		case AL_INVALID_VALUE: puts("OpenAL error - Invalid value."); return GA_ERR_INTERNAL;
		case AL_INVALID_OPERATION: puts("OpenAL error - Invalid op."); return GA_ERR_INTERNAL;
		case AL_OUT_OF_MEMORY: puts("OpenAL error - Out of memory."); return GA_ERR_SYS_MEM;
		default: puts("OpenAL error - Unknown error."); return GA_ERR_SYS_LIB;
	}
}

static s32 AUDIO_ERROR = 0;

#define CHECK_AL_ERROR(dowhat) do { \
	if ((AUDIO_ERROR = alGetError()) != AL_NO_ERROR) { \
		ret = openal_to_ga(AUDIO_ERROR); \
		dowhat; \
	} \
} while (0)

static ga_result gaX_open(GaDevice *dev) {
	ga_result ret;
	dev->impl = ga_alloc(sizeof(GaXDeviceImpl));
	if (!dev->impl) return GA_ERR_SYS_MEM;
	memset(dev->impl, 0, sizeof(*dev->impl));

	dev->impl->next_buffer = 0;
	dev->impl->empty_buffers = dev->num_buffers;

	dev->impl->dev = alcOpenDevice(NULL);
	if (!dev->impl->dev) goto cleanup;

	dev->impl->context = alcCreateContext(dev->impl->dev, 0);
	if (!dev->impl->context) goto cleanup;

	ALCboolean ctxRet = alcMakeContextCurrent(dev->impl->context);
	if (ctxRet == ALC_FALSE) goto cleanup;

	alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
	CHECK_AL_ERROR(goto cleanup);

	dev->impl->hw_buffers = ga_alloc(sizeof(u32) * dev->num_buffers);
	alGenBuffers(dev->num_buffers, dev->impl->hw_buffers);
	CHECK_AL_ERROR(goto cleanup);

	alGenSources(1, &dev->impl->hw_source);
	CHECK_AL_ERROR(alDeleteBuffers(dev->num_buffers, dev->impl->hw_buffers); goto cleanup);

	return GA_OK;

cleanup:
	if (dev->impl->hw_buffers) ga_free(dev->impl->hw_buffers);
	if (dev->impl->context) alcDestroyContext(dev->impl->context);
	if (dev->impl->dev) alcCloseDevice(dev->impl->dev);
	ga_free(dev->impl);
	return ret;
}

static ga_result gaX_close(GaDevice *dev) {
	alDeleteSources(1, &dev->impl->hw_source);
	alDeleteBuffers(dev->num_buffers, dev->impl->hw_buffers);
	alcDestroyContext(dev->impl->context);
	alcCloseDevice(dev->impl->dev);
	ga_free(dev->impl->hw_buffers);
	ga_free(dev->impl);
	return GA_OK;
}

static s32 gaX_check(GaDevice *dev) {
	ga_result ret;
	s32 whichBuf = 0;
	s32 numProcessed = 0;
	alGetSourcei(dev->impl->hw_source, AL_BUFFERS_PROCESSED, &numProcessed);
	CHECK_AL_ERROR(;);
	while (numProcessed--) {
		whichBuf = (dev->impl->next_buffer + dev->impl->empty_buffers++) % dev->num_buffers;
		alSourceUnqueueBuffers(dev->impl->hw_source, 1, &dev->impl->hw_buffers[whichBuf]);
		CHECK_AL_ERROR(;);
	}
	return dev->impl->empty_buffers;
}

static ga_result gaX_queue(GaDevice *dev, void *in_buffer) {
	ga_result ret = GA_OK;
	s32 formatOal;
	ALint state;
	s32 bps = dev->format.bits_per_sample;

	if (dev->format.num_channels == 1)
		formatOal = (s32)(bps == 16 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8);
	else
		formatOal = (s32)(bps == 16 ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8);

	alBufferData(dev->impl->hw_buffers[dev->impl->next_buffer], formatOal, in_buffer,
			(ALsizei)dev->num_samples * ga_format_sample_size(&dev->format), dev->format.sample_rate);
	CHECK_AL_ERROR(goto done);

	alSourceQueueBuffers(dev->impl->hw_source, 1, &dev->impl->hw_buffers[dev->impl->next_buffer]);
	CHECK_AL_ERROR(goto done);

	dev->impl->next_buffer = (dev->impl->next_buffer + 1) % dev->num_buffers;
	--dev->impl->empty_buffers;
	alGetSourcei(dev->impl->hw_source, AL_SOURCE_STATE, &state);
	CHECK_AL_ERROR(goto done);

	if (state != AL_PLAYING) {
		/* NOTE: calling this, even as a 'noop', can cause a clicking sound. */
		alSourcePlay(dev->impl->hw_source);
	}

	CHECK_AL_ERROR(goto done);

done:
	return ret;
}

GaXDeviceProcs gaX_deviceprocs_OpenAL = { gaX_open, gaX_check, gaX_queue, gaX_close };
