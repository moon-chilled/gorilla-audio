#include <stdio.h>
#include <string.h>

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

GaSampleSource *ga_contrib_sample_source_create_mp3(GaDataSource *data_src);

const char *devicetypename(GaDeviceType type) {
	switch (type) {
		case GaDeviceType_Dummy: return "dummy";
		case GaDeviceType_WAV: return "wav";
		case GaDeviceType_OSS: return "oss";
		case GaDeviceType_XAudio2: return "xaudio2";
		case GaDeviceType_PulseAudio: return "pulse";
		case GaDeviceType_ALSA: return "alsa";
		case GaDeviceType_OpenAL: return "al";
		default: return "???";
	}
}

void printtime(int n) {
	if (n < 0) printf("???");
	else printf("%d:%02d", n/60, n%60);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s <audio-file>\n", argv[0]);
		return 1;
	}

	ga_initialize_systemops(NULL);
	GauManager *mgr = check(gau_manager_create_custom(&(GaDeviceType){GaDeviceType_Default}, GauThreadPolicy_Multi, NULL, NULL), "Unable to create audio device");
	//GauManager *mgr = check(gau_manager_create_custom(&(GaDeviceType){GaDeviceType_Dummy}, GauThreadPolicy_Multi, NULL, NULL), "Unable to create audio device");
	GaMixer *mixer = check(gau_manager_mixer(mgr), "Unable to get mixer from manager");
	GaStreamManager *smgr = gau_manager_stream_manager(mgr);

	GaHandle *handle;
	{
		ga_usize l = strlen(argv[1]);
		if (l >= 3 && !strcmp(argv[1] + l - 3, "mp3")) {
			GaDataSource *ds = gau_data_source_create_file(argv[1]);
			check(ds, "Could not open file '%s'.", argv[1]);
			GaSampleSource *ss = ga_contrib_sample_source_create_mp3(ds);
			check(ss, "Could not load file '%s'.", argv[1]);
			handle = ga_handle_create(mixer, ss);
			ga_data_source_release(ds);
			ga_sample_source_release(ss);
		} else {
			handle = gau_create_handle_buffered_file(mixer, smgr, argv[1], GauAudioType_Autodetect, NULL, NULL, NULL);
		}
	}
	check(handle, ":(");
	//ga_handle_setParamf(handle, GA_HANDLE_PARAM_PAN, 0);

	ga_handle_play(handle);

	GaDevice *dev = gau_manager_device(mgr);
	GaFormat hfmt;
	ga_handle_format(handle, &hfmt);
	printf("gaplay [%iHz %ich -> %s (%iHz %ich)] %s\n", hfmt.sample_rate, hfmt.num_channels, devicetypename(dev->dev_type), dev->format.sample_rate, dev->format.num_channels, argv[1]);

	ga_usize cur, dur;
	assert(ga_isok(ga_handle_tell(handle, GaTellParam_Current, &cur)));
	cur = ga_format_to_seconds(&dev->format, cur);
	if (ga_isok(ga_handle_tell(handle, GaTellParam_Total, &dur))) {
		dur = ga_format_to_seconds(&hfmt, dur);
	} else {
		dur = -1;
	}

	while (ga_handle_playing(handle)) {
		gau_manager_update(mgr);
		assert(ga_isok(ga_handle_tell(handle, GaTellParam_Current, &cur)));
		cur = ga_format_to_seconds(&dev->format, cur);
		printtime(cur);
		printf(" / ");
		printtime(dur);
		printf(" (%.0f%%)\r", 100*cur/(float)dur);
		fflush(stdout);
		ga_thread_sleep(1000);
	}

	putchar('\n');

	ga_handle_destroy(handle);
	gau_manager_destroy(mgr);
	ga_shutdown_systemops();
}
