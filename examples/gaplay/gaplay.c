#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <stdlib.h>

#include <gorilla/ga.h>
#include <gorilla/gau.h>

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
#define clamp(x, lo, hi) min(max(lo, x), hi)

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

const char *sampleformatname(GaSampleFormat fmt) {
	switch (fmt) {
		case GaSampleFormat_U8: return "u8";
		case GaSampleFormat_S16: return "s16";
		case GaSampleFormat_S32: return "s32";
		case GaSampleFormat_F32: return "f32";
	}
	return "???";
}
const char *devicetypename(GaDeviceType type) {
	switch (type) {
		case GaDeviceType_Dummy: return "dummy";
		case GaDeviceType_WAV: return "wav";
		case GaDeviceType_OSS: return "oss";
		case GaDeviceType_XAudio2: return "xaudio2";
		case GaDeviceType_Arcan: return "arcan";
		case GaDeviceType_PulseAudio: return "pulse";
		case GaDeviceType_Sndio: return "sndio";;
		case GaDeviceType_ALSA: return "alsa";
		case GaDeviceType_OpenAL: return "al";
	}
	return "???";
}
const char *deviceclassname(GaDeviceClass class) {
	switch (class) {
		case GaDeviceClass_AsyncPush: return "async";
		case GaDeviceClass_SyncPipe: return "sync";
		case GaDeviceClass_SyncShared: return "absinthe";
		case GaDeviceClass_Callback: return "call me sometime";
	}
	return "???";
}

void done(int status) {
	struct termios term;
	tcgetattr(0, &term);
	term.c_lflag |= ICANON | ECHO;
	tcsetattr(0, TCSANOW, &term);
	exit(status);
}

void printmins(int n) {
	if (n < 0) printf("???");
	else printf("%d:%02d", n/60, n%60);
}

void printtime(int cur, int dur) {
	printmins(cur);
	printf(" / ");
	printmins(dur);
	printf(" (%.0f%%)\e[K\r", 100*cur/(float)dur);
	fflush(stdout);
}
bool kbhit() {
	return poll(&(struct pollfd){.fd = 0, .events = POLLIN}, 1, 100) == 1;
}

char getch() {
	char ret;
	read(0, &ret, 1);
	return ret;
}

const char *lc2s(GaLogCategory c) {
	switch (c) {
		case GaLogTrace: return "trace";
		case GaLogInfo: return "info";
		case GaLogWarn: return "warn";
		case GaLogErr: return "err";
	}
	return "???";
}

static void log_to_file(void *ctx, GaLogCategory category, const char *file, const char *function, int line, const char *msg) {
	if (category <= GaLogTrace) return;
	fprintf(ctx, "%s: %s:%s:%d: %s\n", lc2s(category), file, function, line, msg);
}

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Usage: %s <audio-file>\n", argv[0]);
		return 1;
	}

	ga_register_logger(log_to_file, stderr);
	ga_initialize_systemops(NULL);
	GaDeviceType dev_type = GaDeviceType_Default;
	GauManager *mgr = check(gau_manager_create(), "Unable to create audio device");


	GaHandle *handle;
	{
		ga_usize l = strlen(argv[1]);
		if (l >= 3 && !strcmp(argv[1] + l - 3, "mp3")) {
			GaDataSource *ds = gau_data_source_create_file(argv[1]);
			check(ds, "Could not open file '%s'.", argv[1]);
			GaSampleSource *ss = ga_contrib_sample_source_create_mp3(ds);
			check(ss, "Could not load file '%s'.", argv[1]);
			ga_data_source_release(ds);
			handle = gau_create_handle_buffered_samples(mgr, ss);
			check(handle, "oops...");
			ga_sample_source_release(ss);
		} else {
			handle = gau_create_handle_buffered_file(mgr, argv[1]);
		}
	}
	check(handle, ":(");
	//ga_handle_set_paramf(handle, GaHandleParam_Gain, 0);

	ga_handle_play(handle);

	GaDevice *dev = gau_manager_device(mgr);
	GaFormat hfmt, dfmt;
	ga_handle_format(handle, &hfmt);
	ga_device_format(dev, &dfmt);
	printf("gaplay [%s %iHz %ich -> %s (%s %iHz %ich), %s] %s\n", sampleformatname(hfmt.sample_fmt), hfmt.frame_rate, hfmt.num_channels, devicetypename(dev_type), sampleformatname(dfmt.sample_fmt), dfmt.frame_rate, dfmt.num_channels, deviceclassname(ga_device_class(dev)), argv[1]);

	ga_usize cur, dur, sdur;
	assert(ga_isok(ga_handle_tell(handle, GaTellParam_Current, &cur)));
	cur = ga_format_to_seconds(&dfmt, cur);
	if (ga_isok(ga_handle_tell(handle, GaTellParam_Total, &sdur))) {
		dur = ga_format_to_seconds(&hfmt, sdur);
	} else {
		dur = sdur = -1;
	}

	{
		struct termios term;
		tcgetattr(0, &term);
		term.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(0, TCSANOW, &term);
	}

	printtime(cur, dur);

	while (!ga_handle_finished(handle)) {
		gau_manager_update(mgr);
		assert(ga_isok(ga_handle_tell(handle, GaTellParam_Current, &cur)));
		cur = ga_format_to_seconds(&hfmt, cur);

		if (kbhit()) {
			switch (getch()) {
				case ' ':
					(ga_handle_playing(handle) ? ga_handle_stop : ga_handle_play)(handle);
					break;
				case 'q': goto done;

				// escape, maybe arrow key
				case '\x1b':
					if (!kbhit() || getch() != '[' || !kbhit()) break;

				       	int delta;
					switch (getch()) {
						case 'A': delta =  60; break;
						case 'B': delta = -60; break;
						case 'C': delta =   5; break;
						case 'D': delta =  -5; break;
						default: goto cont;
					}

					int frame = clamp(ga_format_to_frames(&hfmt, cur + delta), 0, sdur - 1);
					ga_handle_seek(handle, frame);

					assert(ga_isok(ga_handle_tell(handle, GaTellParam_Current, &cur)));
					cur = ga_format_to_seconds(&hfmt, cur);
					break;
			}
		}

cont:
		printtime(cur, dur);
	}

done:
	putchar('\n');

	ga_handle_destroy(handle);
	gau_manager_destroy(mgr);
	ga_shutdown_systemops();

	done(0);
}
