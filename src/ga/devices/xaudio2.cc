#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

//#include "gorilla/devices/ga_xaudio2.h"

#define INITGUID
#include <windows.h>
#include <AudioClient.h>
#include <xaudio2.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>


extern "C" {
struct GaXDeviceImpl {
	struct IXAudio2 *xa;
	struct IXAudio2MasteringVoice *master;
	struct IXAudio2SourceVoice *source;
	ga_uint32 frame_size;
	ga_uint32 next_buffer;
	void** buffers;
};

static ga_result gaX_open(GaDevice *dev) {
#define x2check(expr) do { if (FAILED(expr)) { ret = GA_ERR_SYS_LIB; goto cleanup; } } while (0)
	ga_result ret;
	HRESULT result;
	WAVEFORMATEX fmt;
	dev->impl = (GaXDeviceImpl*)ga_alloc(sizeof(GaXDeviceImpl));
	if (!dev->impl) return GA_ERR_SYS_MEM;
	dev->impl->frame_size = ga_format_frame_size(&dev->format);
	dev->impl->next_buffer = 0;
	dev->impl->xa = 0;
	dev->impl->master = 0;

	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	result = XAudio2Create(&dev->impl->xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
	if (FAILED(result)) goto cleanup;

	result = dev->impl->xa->CreateMasteringVoice(&dev->impl->master, 2, 48000, 0, 0, 0);
	if (FAILED(result)) goto cleanup;

	fmt.cbSize = sizeof(WAVEFORMATEX);
	ZeroMemory(&fmt, sizeof(WAVEFORMATEX));
	fmt.cbSize = sizeof(WAVEFORMATEX);
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 2;
	fmt.wBitsPerSample = 16;
	fmt.nSamplesPerSec = 48000;
	fmt.nBlockAlign = fmt.nChannels * (fmt.wBitsPerSample / 8);
	fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

	x2check(dev->impl->xa->CreateSourceVoice(&dev->impl->source, &fmt, XAUDIO2_VOICE_NOPITCH, XAUDIO2_DEFAULT_FREQ_RATIO, 0, 0, 0));

	x2check(dev->impl->xa->StartEngine());

	x2check(dev->impl->source->Start(0, XAUDIO2_COMMIT_NOW));

	dev->impl->buffers = (void**)ga_zalloc(dev->num_buffers * sizeof(void*));
	if (!dev->impl->buffers) {
		ret = GA_ERR_SYS_MEM;
		goto cleanup;
	}
	for (usz i = 0; i < dev->num_buffers; ++i)
		dev->impl->buffers[i] = ga_alloc(dev->num_frames * dev->impl->frame_size);

	return GA_OK;

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
	if (dev->impl && dev->impl->buffers) {
		for (usz i = 0; i < dev->num_buffers; i++) ga_free(dev->impl->buffers[i]);
		ga_free(dev->impl->buffers);
	}
	
	ga_free(dev->impl);
	return ret;
}

static ga_result gaX_close(GaDevice *dev) {
	ga_sint32 i;
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
		ga_free(dev->impl->buffers[i]);
	ga_free(dev->impl->buffers);
	ga_free(dev->impl);
	return GA_OK;
}

static u32 gaX_check(GaDevice *dev) {
	XAUDIO2_VOICE_STATE state = { 0 };
	dev->impl->source->GetState(&state);
	return dev->num_buffers - state.BuffersQueued;
}

static ga_result gaX_queue(GaDevice *dev, void *in_buffer) {
	XAUDIO2_BUFFER buf;
	void *data;
	ZeroMemory(&buf, sizeof(XAUDIO2_BUFFER));
	buf.AudioBytes = dev->num_frames * dev->impl->frame_size;
	data = dev->impl->buffers[dev->impl->next_buffer++];
	dev->impl->next_buffer %= dev->num_buffers;
	memcpy(data, in_buffer, buf.AudioBytes);
	buf.pAudioData = (const BYTE*)data;
	dev->impl->source->SubmitSourceBuffer(&buf, 0);
	return GA_OK;
}
GaXDeviceProcs gaX_deviceprocs_XAudio2 = { .open = gaX_open, .check = gaX_check, .queue = gaX_queue, .close = gaX_close };
} // extern "C"
