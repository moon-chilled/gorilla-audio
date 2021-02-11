#include "gorilla/ga.h"
#include "gorilla/ga_internal.h"

#include <stdio.h>

// (c)hecked (b)uffer write
#define cbwrite(fp, data, size) do { \
	if (fwrite(data, size, 1, fp) != 1) goto cleanup; \
} while (0)
// (c)hecked (i)nteger write
#define ciwrite2(fp, number) cbwrite(fp, &(s16){ga_endian_tole2(number)}, 2)
#define ciwrite4(fp, number) cbwrite(fp, &(s32){ga_endian_tole4(number)}, 4)
static ga_result gaX_open(GaDevice *dev) {
	if (dev->format.sample_fmt < 0) return GA_ERR_MIS_UNSUP;

	FILE *fp = fopen("gorilla-out.wav", "w");;
	if (!fp) return GA_ERR_SYS_IO;

	cbwrite(fp, "RIFF", 4);
	ciwrite4(fp, 0); //36 + datasize (size of the entire file except for this and the 'RIFF' magic)
	cbwrite(fp, "WAVE", 4);

	cbwrite(fp, "fmt ", 4);
	ciwrite4(fp, 16); //subchunk size
	ciwrite2(fp, 1); //pcm
	ciwrite2(fp, dev->format.num_channels);
	ciwrite4(fp, dev->format.sample_rate);
	ciwrite4(fp, ga_format_sample_size(&dev->format) * dev->format.sample_rate);
	ciwrite2(fp, ga_format_sample_size(&dev->format));
	ciwrite2(fp, dev->format.sample_fmt << 3);

	cbwrite(fp, "data", 4);
	ciwrite4(fp, 0); //size of all the data following

	dev->impl = (GaXDeviceImpl*)fp;

	return GA_OK;

cleanup:
	if (fp) fclose(fp);
	return GA_ERR_SYS_IO;
}
#undef cbwrite
#undef ciwrite2
#undef ciwrite4

static ga_result gaX_close(GaDevice *dev) {
	bool failure = false;
	FILE *fp = (FILE*)dev->impl;
	long len = ftell(fp);
	fseek(fp, 4, SEEK_SET);
	failure |= 1 != fwrite(&(s32){ga_endian_tole4(len-8)}, 4, 1, fp);
	fseek(fp, 40, SEEK_SET);
	failure |= 1 != fwrite(&(s32){ga_endian_tole4(len-44)}, 4, 1, fp);

	failure |= fclose((FILE*)dev->impl);
	return failure ? GA_ERR_SYS_IO : GA_OK;
}

static u32 gaX_check(GaDevice *dev) {
	return 1; //TODO
}

static ga_result gaX_queue(GaDevice *dev, void *buf) {
	//todo bswap on be
	if (fwrite(buf, ga_format_sample_size(&dev->format), dev->num_samples, (FILE*)dev->impl) != dev->num_samples) return GA_ERR_SYS_IO;
	return GA_OK;
}

GaXDeviceProcs gaX_deviceprocs_WAV = { .open=gaX_open, .check=gaX_check, .queue=gaX_queue, .close=gaX_close };
