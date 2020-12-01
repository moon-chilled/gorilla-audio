#include "gorilla/ga.h"

#include "gorilla/devices/ga_xaudio2.h"

#define INITGUID
#include <windows.h>
#include <xaudio2.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>


struct ga_DeviceImpl {
	struct IXAudio2* xa;
	struct IXAudio2MasteringVoice* master;
	struct IXAudio2SourceVoice* source;
	gc_uint32 sample_size;
	gc_uint32 nextBuffer;
	void** buffers;
};

extern "C" {
static gc_result gaX_open(ga_Device *dev) {
	HRESULT result;
	WAVEFORMATEX fmt;
	gc_int32 i;
	dev->impl = gcX_ops->allocFunc(sizeof(gaX_DeviceImpl));
	dev->impl->sample_size = ga_format_sample_size(&dev->format);
	dev->impl->nextBuffer = 0;
	dev->impl->xa = 0;
	dev->impl->master = 0;

	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	result = XAudio2Create(&dev->impl->xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(result)) goto cleanup;

	result = dev->impl->xa->CreateMasteringVoice(&dev->impl->master, 2, 44100, 0, 0, 0);
	if (FAILED(result)) goto cleanup;

	fmt.cbSize = sizeof(WAVEFORMATEX);
	ZeroMemory(&fmt, sizeof(WAVEFORMATEX));
	fmt.cbSize = sizeof(WAVEFORMATEX);
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 2;
	fmt.wBitsPerSample = 16;
	fmt.nSamplesPerSec = 44100;
	fmt.nBlockAlign = fmt.nChannels * (fmt.wBitsPerSample / 8);
	fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

	result = dev->impl->xa->CreateSourceVoice(&dev->impl->source, &fmt, XAUDIO2_VOICE_NOPITCH, XAUDIO2_DEFAULT_FREQ_RATIO, 0, 0, 0);
	if (FAILED(result)) goto cleanup;

	result = dev->impl->xa->StartEngine();
	if (FAILED(result)) goto cleanup;

	result = dev->impl->source->Start(0, XAUDIO2_COMMIT_NOW);
	if (FAILED(result)) goto cleanup;

	dev->impl->buffers = (void**)gcX_ops->allocFunc(dev->num_buffers * sizeof(void*));
	for(i = 0; i < dev->num_buffers; ++i)
		dev->impl->buffers[i] = gcX_ops->allocFunc(dev->num_samples * dev->impl->sample_size);

	return GC_SUCCESS;

cleanup:
	if (dev->impl->source) {
		dev->impl->source->Stop(0, XAUDIO2_COMMIT_NOW);
		dev->impl->source->FlushSourceBuffers();
		dev->impl->source->DestroyVoice();
	}
	if (dev->impl->xa) dev->impl->xa->StopEngine();
	if (dev->impl->master) dev->impl->master->DestroyVoice();
	if (dev->impl->xa) dev->impl->xa->Release();
	CoUninitialize();
	gcX_ops->freeFunc(dev->impl);
	return GC_ERROR_GENERIC;
}

static gc_result gaX_close(ga_Device *dev) {
	gc_int32 i;
	if(dev->impl->source) {
		dev->impl->source->Stop(0, XAUDIO2_COMMIT_NOW);
		dev->impl->source->FlushSourceBuffers();
		dev->impl->source->DestroyVoice();
	}
	if(dev->impl->xa)
		dev->impl->xa->StopEngine();
	if(dev->impl->master)
		dev->impl->master->DestroyVoice();
	if(dev->impl->xa)
		dev->impl->xa->Release();
	CoUninitialize();

	for(i = 0; i < dev->num_buffers; ++i)
		gcX_ops->freeFunc(dev->impl->buffers[i]);
	gcX_ops->freeFunc(in_device->buffers);
	gcX_ops->freeFunc(in_device);
	return GC_SUCCESS;
}

static gc_int32 gaX_check(ga_Device *dev) {
	gc_int32 ret = 0;
	XAUDIO2_VOICE_STATE state = { 0 };
	dev->impl->source->GetState(&state);
	return dev->num_buffers - state.BuffersQueued;
}

static gc_result gaX_queue(ga_Device *dev, void *in_buffer) {
	XAUDIO2_BUFFER buf;
	void* data;
	ZeroMemory(&buf, sizeof(XAUDIO2_BUFFER));
	buf.AudioBytes = dev->num_samples * dev->impl->sample_size;
	data = dev->impl->buffers[in_device->nextBuffer++];
	dev->impl->nextBuffer %= dev->num_buffers;
	memcpy(data, in_buffer, buf.AudioBytes);
	buf.pAudioData = (const BYTE*)data;
	dev->impl->source->SubmitSourceBuffer(&buf, 0);
	return GC_SUCCESS;
}
gaX_DeviceProcs gaX_deviceprocs_XAudio2 = { gaX_open, gaX_check, gaX_queue, gaX_close };
} // extern "C"
