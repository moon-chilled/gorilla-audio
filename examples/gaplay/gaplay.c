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

const char *devicetypename(ga_DeviceType type) {
	switch (type) {
		case ga_DeviceType_OSS: return "oss";
		case ga_DeviceType_XAudio2: return "xaudio2";
		case ga_DeviceType_PulseAudio: return "pulse";
		case ga_DeviceType_ALSA: return "alsa";
		case ga_DeviceType_OpenAL: return "al";
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

	gc_initialize(NULL);
	//gau_Manager *mgr = gau_manager_create_custom(ga_DeviceType_ALSA, GAU_THREAD_POLICY_MULTI, 4, 512);
	//gau_Manager *mgr = gau_manager_create_custom(ga_DeviceType_PulseAudio, GAU_THREAD_POLICY_MULTI, 4, 512);
	gau_Manager *mgr = check(gau_manager_create_custom(ga_DeviceType_Default, GAU_THREAD_POLICY_MULTI, 4, 512), "Unable to create audio device");
	ga_Mixer *mixer = check(gau_manager_mixer(mgr), "Unable to get mixer from manager");
	ga_StreamManager *smgr = gau_manager_streamManager(mgr);

	ga_Handle *handle = gau_create_handle_buffered_file(mixer, smgr, argv[1], GAU_AUDIO_TYPE_OGG, NULL, NULL, NULL);
	check(handle, "Could not load file '%s'.", argv[1]);
	//ga_handle_setParamf(handle, GA_HANDLE_PARAM_PAN, 0);

	ga_handle_play(handle);

	ga_Device *dev = gau_manager_device(mgr);
	ga_Format hfmt;
	ga_handle_format(handle, &hfmt);
	printf("gaplay [%s %i -> %iHz %i -> %ich] %s\n", devicetypename(dev->dev_type), hfmt.sample_rate, dev->format.sample_rate, hfmt.num_channels, dev->format.num_channels, argv[1]);

	int dur = ga_format_toSeconds(&dev->format, ga_handle_tell(handle, GA_TELL_PARAM_TOTAL));

	while (ga_handle_playing(handle)) {
		gau_manager_update(mgr);
		int cur = ga_format_toSeconds(&dev->format, ga_handle_tell(handle, GA_TELL_PARAM_CURRENT));
		printtime(cur);
		printf(" / ");
		printtime(dur);
		printf(" (%.0f%%)\r", 100*cur/(float)dur);
		fflush(stdout);
		gc_thread_sleep(1000);
	}

	putchar('\n');

	ga_handle_destroy(handle);
	gau_manager_destroy(mgr);
	gc_shutdown();
}
