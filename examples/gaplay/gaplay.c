#include <stdio.h>

#include <gorilla/ga.h>
#include <gorilla/gau.h>
#include <gorilla/ga_internal.h> //sneaky, sneaky!  TODO: define proper getters here

#define check(ptr, ...) do { \
	if (!(ptr)) { \
		fprintf(stderr, __VA_ARGS__); \
		fputc('\n', stderr); \
		return 1; \
	} \
} while (0)

const char *devicetypename(ga_DeviceType type) {
	switch (type) {
		case ga_DeviceType_OSS: return "oss";
		case ga_DeviceType_XAudio2: return "xaudio2";
		case ga_DeviceType_PulseAudio: return "pulse";
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
	gau_Manager *mgr = gau_manager_create_custom(ga_DeviceType_Default, GAU_THREAD_POLICY_MULTI, 4, 512);
	ga_Mixer *mixer = gau_manager_mixer(mgr);
	ga_StreamManager *smgr = gau_manager_streamManager(mgr);

	ga_Handle *handle = gau_create_handle_buffered_file(mixer, smgr, argv[1], GAU_AUDIO_TYPE_OGG, NULL, NULL, NULL);
	check(handle, "Could not load file '%s'.", argv[1]);
	ga_handle_play(handle);

	ga_Device *dev = gau_manager_device(mgr);
	printf("gaplay [%s] %s\n", devicetypename(dev->dev_type), argv[1]);

	int dur = ga_format_toSeconds(&dev->format, ga_handle_tell(handle, GA_TELL_PARAM_TOTAL));

	while (1) {
		gau_manager_update(mgr);
		int cur = ga_format_toSeconds(&dev->format, ga_handle_tell(handle, GA_TELL_PARAM_CURRENT));
		printtime(cur);
		printf(" / ");
		printtime(dur);
		printf(" (%.0f%%)\r", 100*cur/(float)dur);
		fflush(stdout);
		gc_thread_sleep(1);
	}

	gau_manager_destroy(mgr);
	gc_shutdown();
}
