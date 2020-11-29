#include <stdio.h>
#include <string.h>

#include "gorilla/ga.h"
#include "gorilla/gau.h"

// what was this for?  Alternate audio backend, perhaps?
#if 0
FILE* gauX_openWavFile(const char* in_fn, size_t* out_dataSizeOff) {
	FILE* f = fopen(in_fn, "wb");
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
void gauX_closeWavFile(FILE* in_f, long in_dataSizeOff) {
  size_t totalSize = ftell(in_f);
  size_t size = totalSize - 8;
  fseek(in_f, 4, SEEK_SET);
  fwrite(&size, 4, 1, in_f);
  fseek(in_f, in_dataSizeOff, SEEK_SET);
  size = size - (in_dataSizeOff + 4);
  fwrite(&size, 4, 1, in_f);
  fclose(in_f);
}
#endif

/* WAV Sample Source */
typedef struct {
	gc_int32 fileSize;
	gc_int16 fmtTag, channels, blockAlign, bitsPerSample;
	gc_int32 fmtSize, sampleRate, bytesPerSec;
	gc_int32 dataOffset, dataSize;
} ga_WavData;

void gauX_data_source_advance(ga_DataSource* in_dataSrc, gc_int32 in_delta) {
	if(ga_data_source_flags(in_dataSrc) & GA_FLAG_SEEKABLE) {
		ga_data_source_seek(in_dataSrc, in_delta, GA_SEEK_ORIGIN_CUR);
	} else {
		char buffer[256];
		while(in_delta > 0) {
			gc_int32 advance = in_delta > 256 ? 256 : in_delta;
			gc_int32 bytesAdvanced = ga_data_source_read(in_dataSrc, &buffer[0], 1, advance);
			in_delta -= bytesAdvanced;
		}
	}
}


static gc_result gauX_sample_source_wav_load_header(ga_DataSource* in_dataSrc, ga_WavData* out_wavData) {
	/* TODO: Make this work with non-blocking reads? Need to get this data... */
	ga_WavData* wavData = out_wavData;
	gc_int32 seekable = ga_data_source_flags(in_dataSrc) & GA_FLAG_SEEKABLE ? 1 : 0;
	gc_int32 dataOffset = 0;
	char id[5];
	id[4] = 0;
	if(!in_dataSrc)
		return GC_ERROR_GENERIC;
	ga_data_source_read(in_dataSrc, &id[0], sizeof(char), 4); /* 'RIFF' */
	dataOffset += 4;
	if (strcmp(id, "RIFF")) return GC_ERROR_GENERIC;

	ga_data_source_read(in_dataSrc, &wavData->fileSize, sizeof(gc_int32), 1);
	ga_data_source_read(in_dataSrc, &id[0], sizeof(char), 4); /* 'WAVE' */
	dataOffset += 8;
	if (strcmp(id, "WAVE")) return GC_ERROR_GENERIC;
	gc_int32 dataFound = 0;
	gc_int32 hdrFound = 0;
	do {
		gc_int32 chunkSize = 0;
		ga_data_source_read(in_dataSrc, &id[0], sizeof(char), 4);
		ga_data_source_read(in_dataSrc, &chunkSize, sizeof(gc_int32), 1);
		dataOffset += 8;
		/* 'fmt ' */
		if (!hdrFound && !strcmp(id, "fmt ")) {
			wavData->fmtSize = chunkSize;
			ga_data_source_read(in_dataSrc, &wavData->fmtTag, sizeof(gc_int16), 1);
			ga_data_source_read(in_dataSrc, &wavData->channels, sizeof(gc_int16), 1);
			ga_data_source_read(in_dataSrc, &wavData->sampleRate, sizeof(gc_int32), 1);
			ga_data_source_read(in_dataSrc, &wavData->bytesPerSec, sizeof(gc_int32), 1);
			ga_data_source_read(in_dataSrc, &wavData->blockAlign, sizeof(gc_int16), 1);
			ga_data_source_read(in_dataSrc, &wavData->bitsPerSample, sizeof(gc_int16), 1);
			gauX_data_source_advance(in_dataSrc, chunkSize - 16);
			hdrFound = 1;
		/* 'data' */
		} else if (!dataFound && !strcmp(id, "data")) {
			wavData->dataSize = chunkSize;
			wavData->dataOffset = dataOffset;
			dataFound = 1;
		} else {
			gauX_data_source_advance(in_dataSrc, chunkSize);
		}
		dataOffset += chunkSize;
	} while (!(hdrFound && dataFound)); /* TODO: Need End-Of-Data support in Data Sources */
	if (hdrFound && dataFound) return GC_SUCCESS;
	else return GC_ERROR_GENERIC;
}

typedef struct gau_SampleSourceWavContext {
	ga_DataSource* dataSrc;
	ga_WavData wavHeader;
	gc_int32 sampleSize;
	gc_int32 pos;
	gc_Mutex* posMutex;
} gau_SampleSourceWavContext;

typedef struct gau_SampleSourceWav {
	ga_SampleSource sampleSrc;
	gau_SampleSourceWavContext context;
} gau_SampleSourceWav;

static gc_int32 gauX_sample_source_wav_read(void* in_context, void* in_dst, gc_int32 in_numSamples,
                                            tOnSeekFunc in_onSeekFunc, void* in_seekContext) {
	gau_SampleSourceWavContext* ctx = &((gau_SampleSourceWav*)in_context)->context;
	gc_int32 numRead = 0;
	gc_int32 totalSamples = ctx->wavHeader.dataSize / ctx->sampleSize;
	gc_mutex_lock(ctx->posMutex);
	if (ctx->pos + in_numSamples > totalSamples) in_numSamples = totalSamples - ctx->pos;
	if (in_numSamples > 0) {
		numRead = ga_data_source_read(ctx->dataSrc, in_dst, ctx->sampleSize, in_numSamples);
		ctx->pos += numRead;
	}
	gc_mutex_unlock(ctx->posMutex);
	return numRead;
}
static gc_int32 gauX_sample_source_wav_end(void* in_context) {
	gau_SampleSourceWavContext* ctx = &((gau_SampleSourceWav*)in_context)->context;
	gc_int32 totalSamples = ctx->wavHeader.dataSize / ctx->sampleSize;
	return ctx->pos == totalSamples; /* No need to mutex this use */
}
static gc_result gauX_sample_source_wav_seek(void* in_context, gc_int32 in_sampleOffset) {
	gau_SampleSourceWavContext* ctx = &((gau_SampleSourceWav*)in_context)->context;
	gc_mutex_lock(ctx->posMutex);
	gc_result ret = ga_data_source_seek(ctx->dataSrc, ctx->wavHeader.dataOffset + in_sampleOffset * ctx->sampleSize, GA_SEEK_ORIGIN_SET);
	if(ret >= 0) ctx->pos = in_sampleOffset;
	gc_mutex_unlock(ctx->posMutex);
	return ret;
}
static gc_int32 gauX_sample_source_wav_tell(void* in_context, gc_int32* out_totalSamples) {
	gau_SampleSourceWavContext* ctx = &((gau_SampleSourceWav*)in_context)->context;
	if (out_totalSamples) *out_totalSamples = ctx->wavHeader.dataSize / ctx->sampleSize;
	return ctx->pos; /* No need to mutex this use */
}
static void gauX_sample_source_wav_close(void* in_context) {
	gau_SampleSourceWavContext* ctx = &((gau_SampleSourceWav*)in_context)->context;
	ga_data_source_release(ctx->dataSrc);
	gc_mutex_destroy(ctx->posMutex);
}

ga_SampleSource* gau_sample_source_create_wav(ga_DataSource* in_dataSrc) {
	gc_result validHeader;
	gau_SampleSourceWav* ret = gcX_ops->allocFunc(sizeof(gau_SampleSourceWav));
	gau_SampleSourceWavContext* ctx = &ret->context;
	gc_int32 seekable = ga_data_source_flags(in_dataSrc) & GA_FLAG_SEEKABLE ? 1 : 0;
	ga_sample_source_init(&ret->sampleSrc);
	ret->sampleSrc.flags = GA_FLAG_THREADSAFE;
	if (seekable) ret->sampleSrc.flags |= GA_FLAG_SEEKABLE;
	ret->sampleSrc.readFunc = &gauX_sample_source_wav_read;
	ret->sampleSrc.endFunc = &gauX_sample_source_wav_end;
	if (seekable) {
		ret->sampleSrc.seekFunc = &gauX_sample_source_wav_seek;
		ret->sampleSrc.tellFunc = &gauX_sample_source_wav_tell;
	}
	ret->sampleSrc.closeFunc = &gauX_sample_source_wav_close;
	ctx->pos = 0;
	ga_data_source_acquire(in_dataSrc);
	ctx->dataSrc = in_dataSrc;
	validHeader = gauX_sample_source_wav_load_header(in_dataSrc, &ctx->wavHeader);
	if (validHeader == GC_SUCCESS) {
		ctx->posMutex = gc_mutex_create();
		ret->sampleSrc.format.numChannels = ctx->wavHeader.channels;
		ret->sampleSrc.format.bitsPerSample = ctx->wavHeader.bitsPerSample;
		ret->sampleSrc.format.sampleRate = ctx->wavHeader.sampleRate;
		ctx->sampleSize = ga_format_sampleSize(&ret->sampleSrc.format);
	} else {
		ga_data_source_release(in_dataSrc);
		gcX_ops->freeFunc(ret);
		ret = 0;
	}
	return (ga_SampleSource*)ret;
}
