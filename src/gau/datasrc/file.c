#include <stdio.h>

#include "gorilla/gau.h"
#include "gorilla/ga_internal.h"

/* File-Based Data Source */
typedef struct {
	FILE *f;
	GaMutex file_mutex;
} gau_DataSourceFileContext;

typedef struct {
	GaDataSource dataSrc;
	gau_DataSourceFileContext context;
} gau_DataSourceFile;

static usz gauX_data_source_file_read(void *context, void *dst, usz size, usz count) {
	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	usz ret;
	ga_mutex_lock(ctx->file_mutex);
	ret = fread(dst, size, count, ctx->f);
	ga_mutex_unlock(ctx->file_mutex);
	return ret;
}
static ga_result gauX_data_source_file_seek(void *context, ssz offset, GaSeekOrigin whence) {
	int fwhence;
	switch (whence) {
		case GaSeekOrigin_Set: fwhence = SEEK_SET; break;
		case GaSeekOrigin_Cur: fwhence = SEEK_CUR; break;
		case GaSeekOrigin_End: fwhence = SEEK_END; break;
		default: return GA_ERR_GENERIC;
	}

	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	ga_mutex_lock(ctx->file_mutex);
	ga_result ret = fseek(ctx->f, offset, fwhence) == -1 ? GA_ERR_GENERIC : GA_OK;
	ga_mutex_unlock(ctx->file_mutex);

	return ret;
}
static usz gauX_data_source_file_tell(void *context) {
	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	ga_mutex_lock(ctx->file_mutex);
	usz ret = ftell(ctx->f);
	ga_mutex_unlock(ctx->file_mutex);
	return ret;
}
static void gauX_data_source_file_close(void *context) {
	gau_DataSourceFileContext *ctx = (gau_DataSourceFileContext*)context;
	fclose(ctx->f);
	ga_mutex_destroy(ctx->file_mutex);
}
static GaDataSource *gauX_data_source_create_fp(FILE *fp) {
	if (!fp) return NULL;

	rewind(fp);

	gau_DataSourceFile *ret = ga_alloc(sizeof(gau_DataSourceFile));
	ga_data_source_init(&ret->dataSrc);
	ret->dataSrc.flags = GaDataAccessFlag_Seekable | GaDataAccessFlag_Threadsafe;
	ret->dataSrc.read = &gauX_data_source_file_read;
	ret->dataSrc.seek = &gauX_data_source_file_seek;
	ret->dataSrc.tell = &gauX_data_source_file_tell;
	ret->dataSrc.close = &gauX_data_source_file_close;
	ret->context.f = fp;
	if (!ga_isok(ga_mutex_create(&ret->context.file_mutex))) {} //todo
	return (GaDataSource*)ret;
}

GaDataSource *gau_data_source_create_file(const char *fname) {
	return gauX_data_source_create_fp(fopen(fname, "rb"));
}
