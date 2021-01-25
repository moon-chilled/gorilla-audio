#include <string.h>

#include "gorilla/gau.h"
#include "gorilla/ga_internal.h"

/* Memory-Based Data Source */
typedef struct {
	GaMemory *memory;
	usz pos;
	GaMutex mutex;
} GauDataSourceMemoryContext;

typedef struct {
	GaDataSource dataSrc;
	GauDataSourceMemoryContext context;
} GauDataSourceMemory;

usz gauX_data_source_memory_read(void *context, void *dst, usz size, usz count) {
	GauDataSourceMemoryContext *ctx = (GauDataSourceMemoryContext*)context;
	usz ret = 0;
	usz dataSize = ga_memory_size(ctx->memory);
	usz toRead = size * count;
	usz remaining;

	ga_mutex_lock(ctx->mutex);
	remaining = dataSize - ctx->pos;
	toRead = toRead < remaining ? toRead : remaining;
	toRead = toRead - (toRead % size);
	if (toRead) {
		memcpy(dst, (char*)ga_memory_data(ctx->memory) + ctx->pos, toRead);
		ctx->pos += toRead;
		ret = toRead / size;
	}
	ga_mutex_unlock(ctx->mutex);
	return ret;
}
ga_result gauX_data_source_memory_seek(void *context, ssz offset, GaSeekOrigin whence) {
	GauDataSourceMemoryContext *ctx = (GauDataSourceMemoryContext*)context;
	usz data_size = ga_memory_size(ctx->memory);
	ssz pos;
	ga_mutex_lock(ctx->mutex);
	switch (whence) {
		case GaSeekOrigin_Set: pos = offset; break;
		case GaSeekOrigin_Cur: pos = ctx->pos + offset; break;
		case GaSeekOrigin_End: pos = data_size - offset; break;
		default: goto fail;
	}
	if (pos < 0 || (usz)pos > data_size) goto fail;
	ctx->pos = pos;
	ga_mutex_unlock(ctx->mutex);
	return GA_OK;
fail:
	ga_mutex_unlock(ctx->mutex);
	return GA_ERR_GENERIC;
}
usz gauX_data_source_memory_tell(void *context) {
	GauDataSourceMemoryContext *ctx = (GauDataSourceMemoryContext*)context;
	ga_mutex_lock(ctx->mutex);
	usz ret = ctx->pos;
	ga_mutex_unlock(ctx->mutex);
	return ret;
}
void gauX_data_source_memory_close(void *context) {
	GauDataSourceMemoryContext *ctx = (GauDataSourceMemoryContext*)context;
	ga_memory_release(ctx->memory);
	ga_mutex_destroy(ctx->mutex);
}
GaDataSource *gau_data_source_create_memory(GaMemory *memory) {
	GauDataSourceMemory *ret = ga_alloc(sizeof(GauDataSourceMemory));
	if (!ret) return NULL;
	if (!ga_isok(ga_mutex_create(&ret->context.mutex))) {
		ga_free(ret);
		return NULL;
	}
	ga_data_source_init(&ret->dataSrc);
	ret->dataSrc.flags = GaDataAccessFlag_Seekable | GaDataAccessFlag_Threadsafe;
	ret->dataSrc.read = &gauX_data_source_memory_read;
	ret->dataSrc.seek = &gauX_data_source_memory_seek;
	ret->dataSrc.tell = &gauX_data_source_memory_tell;
	ret->dataSrc.close = &gauX_data_source_memory_close;
	ga_memory_acquire(memory);
	ret->context.memory = memory;
	ret->context.pos = 0;
	return (GaDataSource*)ret;
}
