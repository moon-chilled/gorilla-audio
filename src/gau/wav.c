#include <stdio.h>
#include <string.h>

#include "gorilla/ga.h"
#include "gorilla/gau.h"

// what was this for?  Alternate audio backend, perhaps?
#if 0
FILE *gauX_openWavFile(const char *fn, size_t *out_dataSizeOff) {
	FILE *f = fopen(fn, "wb");
	gc_int32 size = 0;
	gc_uint16 val16 = 0;
	gc_uint32 val32 = 0;
	fwrite("RIFF", 1, 4, f);
	fwrite(&size, 4, 1, f); /* file size */
	fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f);
	size = 16;
	fwrite(&size, 4, 1, f); /* format chunk size */
	val16 = 0x0001;
	fwrite(&val16, 2, 1, f);
	val16 = 2;
	fwrite(&val16, 2, 1, f);
	val32 = 44100;
	fwrite(&val32, 4, 1, f);
	val32 = 4 * 44100;
	fwrite(&val32, 4, 1, f);
	val16 = 2;
	fwrite(&val16, 2, 1, f);
	val16 = 16;
	fwrite(&val16, 2, 1, f);
	fwrite("data", 1, 4, f);
	*out_dataSizeOff = ftell(f);
	size = 0;
	fwrite(&size, 4, 1, f); /* data size */
	return f;
}
void gauX_closeWavFile(FILE *fp, long dataSizeOff) {
  size_t totalSize = ftell(fp);
  size_t size = totalSize - 8;
  fseek(fp, 4, SEEK_SET);
  fwrite(&size, 4, 1, fp);
  fseek(fp, dataSizeOff, SEEK_SET);
  size = size - (dataSizeOff + 4);
  fwrite(&size, 4, 1, fp);
  fclose(fp);
}
#endif

/* WAV Sample Source */
typedef struct {
	gc_int32 file_size;
	gc_int16 fmt_tag, channels, block_align, bits_per_sample;
	gc_int32 fmt_size, sample_rate, bytes_per_sec;
	gc_int32 data_offset, data_size;
} GaWavData;

void gauX_data_source_advance(ga_DataSource *data_src, gc_int32 delta) {
	if(ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable) {
		ga_data_source_seek(data_src, delta, GaSeekOrigin_Cur);
	} else {
		char buffer[256];
		while(delta > 0) {
			gc_int32 advance = delta > 256 ? 256 : delta;
			gc_int32 bytesAdvanced = ga_data_source_read(data_src, &buffer[0], 1, advance);
			delta -= bytesAdvanced;
		}
	}
}


static gc_result gauX_sample_source_wav_load_header(ga_DataSource *data_src, GaWavData *wav_data) {
	if(!data_src)
		return GC_ERROR_GENERIC;

	/* TODO: Make this work with non-blocking reads? Need to get this data... */
	gc_int32 data_offset = 0;
	char id[5];
	id[4] = 0;
	ga_data_source_read(data_src, &id[0], sizeof(char), 4); /* 'RIFF' */
	data_offset += 4;
	if (strcmp(id, "RIFF")) return GC_ERROR_GENERIC;

	ga_data_source_read(data_src, &wav_data->file_size, sizeof(gc_int32), 1);
	ga_data_source_read(data_src, &id[0], sizeof(char), 4); /* 'WAVE' */
	data_offset += 8;
	if (strcmp(id, "WAVE")) return GC_ERROR_GENERIC;
	gc_int32 dataFound = 0;
	gc_int32 hdrFound = 0;
	do {
		gc_int32 chunkSize = 0;
		ga_data_source_read(data_src, &id[0], sizeof(char), 4);
		ga_data_source_read(data_src, &chunkSize, sizeof(gc_int32), 1);
		data_offset += 8;
		/* 'fmt ' */
		if (!hdrFound && !strcmp(id, "fmt ")) {
			wav_data->fmt_size = chunkSize;
			ga_data_source_read(data_src, &wav_data->fmt_tag, sizeof(gc_int16), 1);
			ga_data_source_read(data_src, &wav_data->channels, sizeof(gc_int16), 1);
			ga_data_source_read(data_src, &wav_data->sample_rate, sizeof(gc_int32), 1);
			ga_data_source_read(data_src, &wav_data->bytes_per_sec, sizeof(gc_int32), 1);
			ga_data_source_read(data_src, &wav_data->block_align, sizeof(gc_int16), 1);
			ga_data_source_read(data_src, &wav_data->bits_per_sample, sizeof(gc_int16), 1);
			gauX_data_source_advance(data_src, chunkSize - 16);
			hdrFound = 1;
		/* 'data' */
		} else if (!dataFound && !strcmp(id, "data")) {
			wav_data->data_size = chunkSize;
			wav_data->data_offset = data_offset;
			dataFound = 1;
		} else {
			gauX_data_source_advance(data_src, chunkSize);
		}
		data_offset += chunkSize;
	} while (!(hdrFound && dataFound)); /* TODO: Need End-Of-Data support in Data Sources */
	if (hdrFound && dataFound) return GC_SUCCESS;
	else return GC_ERROR_GENERIC;
}

typedef struct gau_SampleSourceWavContext {
	ga_DataSource *data_src;
	GaWavData wav_header;
	gc_uint32 sample_size;
	gc_atomic_size pos;
	gc_Mutex *pos_mutex;
} gau_SampleSourceWavContext;

typedef struct gau_SampleSourceWav {
	ga_SampleSource sample_src;
	gau_SampleSourceWavContext context;
} gau_SampleSourceWav;

static gc_size gauX_sample_source_wav_read(void *context, void *dst, gc_size numSamples,
                                            tOnSeekFunc onSeekFunc, void *seekContext) {
	gau_SampleSourceWavContext *ctx = &((gau_SampleSourceWav*)context)->context;
	gc_size numRead = 0;
	gc_size totalSamples = ctx->wav_header.data_size / ctx->sample_size;
	gc_mutex_lock(ctx->pos_mutex);
	if (ctx->pos + numSamples > totalSamples) numSamples = totalSamples - ctx->pos;
	numRead = ga_data_source_read(ctx->data_src, dst, ctx->sample_size, numSamples);
	ctx->pos += numRead;
	gc_mutex_unlock(ctx->pos_mutex);
	return numRead;
}
static gc_bool gauX_sample_source_wav_end(void *context) {
	gau_SampleSourceWavContext *ctx = &((gau_SampleSourceWav*)context)->context;
	gc_size totalSamples = ctx->wav_header.data_size / ctx->sample_size;
	return atomic_load(&ctx->pos) == totalSamples;
}
static gc_result gauX_sample_source_wav_seek(void *context, gc_size sample_offset) {
	gau_SampleSourceWavContext *ctx = &((gau_SampleSourceWav*)context)->context;
	gc_mutex_lock(ctx->pos_mutex);
	gc_result ret = ga_data_source_seek(ctx->data_src, ctx->wav_header.data_offset + sample_offset * ctx->sample_size, GaSeekOrigin_Set);
	if (ret == GC_SUCCESS) ctx->pos = sample_offset;
	gc_mutex_unlock(ctx->pos_mutex);
	return ret;
}
static gc_result gauX_sample_source_wav_tell(void *context, gc_size *cur, gc_size *total) {
	gau_SampleSourceWavContext *ctx = &((gau_SampleSourceWav*)context)->context;
	if (total) *total = ctx->wav_header.data_size / ctx->sample_size;
	if (cur) *cur = atomic_load(&ctx->pos);
	return GC_SUCCESS;
}
static void gauX_sample_source_wav_close(void *context) {
	gau_SampleSourceWavContext *ctx = &((gau_SampleSourceWav*)context)->context;
	ga_data_source_release(ctx->data_src);
	gc_mutex_destroy(ctx->pos_mutex);
}

ga_SampleSource *gau_sample_source_create_wav(ga_DataSource *data_src) {
	gc_result validHeader;
	gau_SampleSourceWav *ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceWav));
	gau_SampleSourceWavContext *ctx = &ret->context;
	gc_bool seekable = ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable;
	ga_sample_source_init(&ret->sample_src);
	ret->sample_src.flags = GaDataAccessFlag_Threadsafe;
	if (seekable) ret->sample_src.flags |= GaDataAccessFlag_Seekable;
	ret->sample_src.readFunc = &gauX_sample_source_wav_read;
	ret->sample_src.endFunc = &gauX_sample_source_wav_end;
	if (seekable) {
		ret->sample_src.seekFunc = &gauX_sample_source_wav_seek;
		ret->sample_src.tellFunc = &gauX_sample_source_wav_tell;
	}
	ret->sample_src.closeFunc = &gauX_sample_source_wav_close;
	ctx->pos = 0;
	ga_data_source_acquire(data_src);
	ctx->data_src = data_src;
	validHeader = gauX_sample_source_wav_load_header(data_src, &ctx->wav_header);
	if (validHeader == GC_SUCCESS) {
		ctx->pos_mutex = gc_mutex_create();
		ret->sample_src.format.num_channels = ctx->wav_header.channels;
		ret->sample_src.format.bits_per_sample = ctx->wav_header.bits_per_sample;
		ret->sample_src.format.sample_rate = ctx->wav_header.sample_rate;
		ctx->sample_size = ga_format_sampleSize(&ret->sample_src.format);
	} else {
		ga_data_source_release(data_src);
		gcX_ops->freeFunc(ret);
		ret = 0;
	}
	return (ga_SampleSource*)ret;
}
