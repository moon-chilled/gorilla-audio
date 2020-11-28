#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include "al.h"
#include "alc.h"

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

struct gaX_DeviceImpl {
	struct ALCdevice *dev;
	struct ALCcontext *context;
	gc_uint32 *hw_buffers;
	gc_uint32 hw_source;
	gc_uint32 next_buffer;
	gc_uint32 empty_buffers;
};


const char *gaX_openAlErrorToString(ALuint error) {
	switch (error) {
		case AL_NO_ERROR: return "OpenAL error - None";
		case AL_INVALID_NAME: return "OpenAL error - Invalid name.";
		case AL_INVALID_ENUM: return "OpenAL error - Invalid enum.";
		case AL_INVALID_VALUE: return "OpenAL error - Invalid value.";
		case AL_INVALID_OPERATION: return "OpenAL error - Invalid op.";
		case AL_OUT_OF_MEMORY: return "OpenAL error - Out of memory.";
		default: return "OpenAL error - Unknown error.";
	}
}

static gc_int32 AUDIO_ERROR = 0;

#define CHECK_AL_ERROR(dowhat) do { \
	if ((AUDIO_ERROR = alGetError()) != AL_NO_ERROR) { \
		puts(gaX_openAlErrorToString(AUDIO_ERROR)); \
		dowhat; \
	} \
} while (0)

static gc_result gaX_open(ga_Device *dev) {
	dev->impl = gcX_ops->allocFunc(sizeof(gaX_DeviceImpl));
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

	dev->impl->hw_buffers = gcX_ops->allocFunc(sizeof(gc_uint32) * dev->num_buffers);
	alGenBuffers(dev->num_buffers, dev->impl->hw_buffers);
	CHECK_AL_ERROR(goto cleanup);

	alGenSources(1, &dev->impl->hw_source);
	CHECK_AL_ERROR(alDeleteBuffers(dev->num_buffers, dev->impl->hw_buffers); goto cleanup);

	return GC_SUCCESS;

cleanup:
	if (dev->impl->hw_buffers) gcX_ops->freeFunc(dev->impl->hw_buffers);
	if (dev->impl->context) alcDestroyContext(dev->impl->context);
	if (dev->impl->dev) alcCloseDevice(dev->impl->dev);
	gcX_ops->freeFunc(dev->impl);
	return GC_ERROR_GENERIC;
}

static gc_result gaX_close(ga_Device *dev) {
	alDeleteSources(1, &dev->impl->hw_source);
	alDeleteBuffers(dev->num_buffers, dev->impl->hw_buffers);
	alcDestroyContext(dev->impl->context);
	alcCloseDevice(dev->impl->dev);
	gcX_ops->freeFunc(dev->impl->hw_buffers);
	gcX_ops->freeFunc(dev->impl);
	return GC_SUCCESS;
}

static gc_int32 gaX_check(ga_Device *dev) {
	gc_int32 whichBuf = 0;
	gc_int32 numProcessed = 0;
	alGetSourcei(dev->impl->hw_source, AL_BUFFERS_PROCESSED, &numProcessed);
	CHECK_AL_ERROR(;);
	while (numProcessed--) {
		whichBuf = (dev->impl->next_buffer + dev->impl->empty_buffers++) % dev->num_buffers;
		alSourceUnqueueBuffers(dev->impl->hw_source, 1, &dev->impl->hw_buffers[whichBuf]);
		CHECK_AL_ERROR(;);
	}
	return dev->impl->empty_buffers;
}

static gc_result gaX_queue(ga_Device *dev, void* in_buffer) {
	gc_int32 formatOal;
	ALint state;
	gc_int32 bps = dev->format.bits_per_sample;

	if (dev->format.num_channels == 1)
		formatOal = (gc_int32)(bps == 16 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8);
	else
		formatOal = (gc_int32)(bps == 16 ? AL_FORMAT_STEREO16 : AL_FORMAT_STEREO8);

	alBufferData(dev->impl->hw_buffers[dev->impl->next_buffer], formatOal, in_buffer,
			(ALsizei)dev->num_samples * ga_format_sampleSize(&dev->format), dev->format.sample_rate);
	CHECK_AL_ERROR(return GC_ERROR_GENERIC);

	alSourceQueueBuffers(dev->impl->hw_source, 1, &dev->impl->hw_buffers[dev->impl->next_buffer]);
	CHECK_AL_ERROR(return GC_ERROR_GENERIC);

	dev->impl->next_buffer = (dev->impl->next_buffer + 1) % dev->num_buffers;
	--dev->impl->empty_buffers;
	alGetSourcei(dev->impl->hw_source, AL_SOURCE_STATE, &state);
	CHECK_AL_ERROR(return GC_ERROR_GENERIC);

	if (state != AL_PLAYING) {
		/* NOTE: calling this, even as a 'noop', can cause a clicking sound. */
		alSourcePlay(dev->impl->hw_source);
	}

	CHECK_AL_ERROR(return GC_ERROR_GENERIC);
	return GC_SUCCESS;
}

gaX_DeviceProcs gaX_deviceprocs_OpenAL = { gaX_open, gaX_check, gaX_queue, gaX_close };
