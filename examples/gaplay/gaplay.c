#include <stdio.h>

#include <gorilla/ga.h>
#include <gorilla/gau.h>
#include <gorilla/ga_internal.h> //sneaky, sneaky!  TODO: define proper getters here

#define check(ptr, ...) ({ \
	__typeof__(ptr) _ptr = ptr; \
	if (!(_ptr)) { \
		fprintf(stderr, __VA_ARGS__); \
		fputc('\n', stderr); \
		return 1; \
	} \
	_ptr; \
})

const char *devicetypename(GaDeviceType type) {
	switch (type) {
		case GaDeviceType_OSS: return "oss";
		case GaDeviceType_XAudio2: return "xaudio2";
		case GaDeviceType_PulseAudio: return "pulse";
		case GaDeviceType_ALSA: return "alsa";
		case GaDeviceType_OpenAL: return "al";
		default: return "???";
	}
}

void printtime(int n) {
	printf("%d:%02d", n/60, n%60);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s <audio-file>\n", argv[0]);
		return 1;
	}

	ga_initialize_systemops(NULL);
	GauManager *mgr = check(gau_manager_create_custom(&(GaDeviceType){GaDeviceType_Default}, GauThreadPolicy_Multi, NULL, NULL), "Unable to create audio device");
	GaMixer *mixer = check(gau_manager_mixer(mgr), "Unable to get mixer from manager");
	GaStreamManager *smgr = gau_manager_stream_manager(mgr);

	//GaHandle *handle = gau_create_handle_buffered_file(mixer, smgr, argv[1], GauAudioType_Wav, NULL, NULL, NULL);
	GaHandle *handle = gau_create_handle_buffered_file(mixer, smgr, argv[1], GauAudioType_Ogg, NULL, NULL, NULL);
	check(handle, "Could not load file '%s'.", argv[1]);
	//ga_handle_setParamf(handle, GA_HANDLE_PARAM_PAN, 0);

	ga_handle_play(handle);

	GaDevice *dev = gau_manager_device(mgr);
	GaFormat hfmt;
	ga_handle_format(handle, &hfmt);
	printf("gaplay [%s %i -> %iHz %i -> %ich] %s\n", devicetypename(dev->dev_type), hfmt.sample_rate, dev->format.sample_rate, hfmt.num_channels, dev->format.num_channels, argv[1]);

	int dur = ga_format_to_seconds(&dev->format, ga_handle_tell(handle, GaTellParam_Total));

	while (ga_handle_playing(handle)) {
		gau_manager_update(mgr);
		int cur = ga_format_to_seconds(&dev->format, ga_handle_tell(handle, GaTellParam_Current));
		printtime(cur);
		printf(" / ");
		printtime(dur);
		printf(" (%.0f%%)\r", 100*cur/(float)dur);
		fflush(stdout);
		ga_thread_sleep(1000);
		break;
	}

	putchar('\n');

	ga_handle_destroy(handle);
	gau_manager_destroy(mgr);
	ga_shutdown_systemops();
}
