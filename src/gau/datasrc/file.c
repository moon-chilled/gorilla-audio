#include <stdio.h>

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

static usz read(GaDataSourceContext *ctx, void *dst, usz size, usz count) {
	return fread(dst, size, count, (FILE*)ctx);
}
static ga_result seek(GaDataSourceContext *ctx, ssz offset, GaSeekOrigin whence) {
	int fwhence;
	switch (whence) {
		case GaSeekOrigin_Set: fwhence = SEEK_SET; break;
		case GaSeekOrigin_Cur: fwhence = SEEK_CUR; break;
		case GaSeekOrigin_End: fwhence = SEEK_END; break;
		default: return GA_ERR_MIS_PARAM;
	}

	return fseek((FILE*)ctx, offset, fwhence) == -1 ? GA_ERR_SYS_IO : GA_OK;
}
static usz tell(GaDataSourceContext *ctx) {
	long ret = ftell((FILE*)ctx);
	assert(ret >= 0); //todo handle
	return ret;
}
static bool eof(GaDataSourceContext *ctx) {
	return feof((FILE*)ctx);
}
static void close(GaDataSourceContext *ctx) {
	fclose((FILE*)ctx);
}

GaDataSource *gau_data_source_create_file(const char *fname) {
	FILE *fp = fopen(fname, "rb");
	if (!fp) return NULL;

	GaDataSource *ret = ga_data_source_create(&(GaDataSourceCreationMinutiae){
		.read = read,
		.seek = seek,
		.tell = tell,
		.eof = eof,
		.close = close,
		.context = (GaDataSourceContext*)fp,
		.threadsafe = true,
	});

	if (!ret) fclose(fp);
	return ret;
}
