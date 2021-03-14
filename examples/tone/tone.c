#include "gorilla/ga.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

int main(int argc, char** argv) {
	GaFormat fmt = {
		.sample_fmt = GaSampleFormat_S16,
		.num_channels = 2,
		.frame_rate = 48000,
	};
	GaDevice *dev;
	ga_uint32 num_frames = 2048;
	ga_float32 pan = 1.0f;
	ga_float32 t = 0.0f;

	/* Initialize library + device */
	dev = ga_device_open(&(GaDeviceType){GaDeviceType_Default}, NULL, &num_frames, &fmt);
	if (!dev) goto fail;
	if (fmt.sample_fmt != GaSampleFormat_S16) goto fail;
	if (fmt.num_channels != 2) goto fail;

	/* Infinite mix loop */
	while (1) {
		ga_uint32 num_to_queue = ga_device_check(dev);
		while (num_to_queue--) {
			ga_sint16 *buf = ga_device_get_buffer(dev);
			if (!buf) continue;
			for (ga_uint32 i = 0; i < num_frames * 2; i += 2) {
				ga_float32 f = sin(t);
				ga_sint16 sample = f * (32767 + (f < 0));

				pan = sin(t / 300) / 2.0f + 0.5f;
				buf[i] = sample * pan;
				buf[i + 1] = sample * (1.0f - pan);
				t = t + 0.03f;
				if (t > 300 * M_PI) t -= 600*M_PI;
			}
			ga_device_queue(dev, buf);
		}
	}

	/* Clean up device + library */
	ga_device_close(dev);

	return 0;

fail:
	ga_device_close(dev);
	return 1;
}
