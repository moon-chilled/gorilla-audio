#include "gorilla/ga.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

int main(int argc, char** argv) {
	GaFormat fmt;
	GaDevice *dev;
	s16 *buf;
	s32 numSamples;
	s32 sampleSize;
	s32 numToQueue;
	s32 i;
	s16 sample;
	ga_float32 pan = 1.0f;
	ga_float32 t = 0.0f;

	/* Initialize library + device */
	ga_initialize_systemops(0);
	memset(&fmt, 0, sizeof(GaFormat));
	fmt.bits_per_sample = 16;
	fmt.num_channels = 2;
	fmt.sample_rate = 44100;
	numSamples = 2048;
	sampleSize = ga_format_sampleSize(&fmt);
	dev = ga_device_open(GaDeviceType_Default, 2, 2048, &fmt);
	if(!dev)
		return 1;

	/* Infinite mix loop */
	while(1) {
		numToQueue = ga_device_check(dev);
		while (numToQueue--) {
			buf = ga_device_get_buffer(dev);
			if (!buf) continue;
			for(i = 0; i < numSamples * 2; i = i + 2) {
				sample = (s16)(sin(t) * 32768);
				sample = (sample > -32768 ? (sample < 32767 ? sample : 32767) : -32768);
				pan = (ga_float32)sin(t / 300) / 2.0f + 0.5f;
				buf[i] = (s16)(sample * pan);
				buf[i + 1] = (s16)(sample * (1.0f - pan));
				t = t + 0.03f;
				if(t > 3.14159265f)
					t -= 3.14159265f;
			}
			ga_device_queue(dev, buf);
		}
	}

	/* Clean up device + library */
	ga_device_close(dev);
	ga_shutdown_systemops();

	/* Free buffer */
	free(buf);

	return 0;
}
