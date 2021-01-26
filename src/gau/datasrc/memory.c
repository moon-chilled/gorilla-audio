#include <string.h>

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

struct GaDataSourceContext {
	GaMemory *memory;
	usz pos;
	GaMutex mutex;
};

static usz read(GaDataSourceContext *ctx, void *dst, usz size, usz count) {
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
static ga_result seek(GaDataSourceContext *ctx, ssz offset, GaSeekOrigin whence) {
	ga_result ret = GA_OK;
	usz data_size = ga_memory_size(ctx->memory);
	ssz pos;
	ga_mutex_lock(ctx->mutex);
	switch (whence) {
		case GaSeekOrigin_Set: pos = offset; break;
		case GaSeekOrigin_Cur: pos = ctx->pos + offset; break;
		case GaSeekOrigin_End: pos = data_size - offset; break;
		default: ret = GA_ERR_MIS_PARAM; goto done;
	}
	if (pos < 0 || (usz)pos > data_size) {
		ret = GA_ERR_MIS_PARAM;
	       	goto done;
	}
	ctx->pos = pos;
done:
	ga_mutex_unlock(ctx->mutex);
	return ret;
}
static usz tell(GaDataSourceContext *ctx) {
	ga_mutex_lock(ctx->mutex);
	usz ret = ctx->pos;
	ga_mutex_unlock(ctx->mutex);
	return ret;
}
static void close(GaDataSourceContext *ctx) {
	ga_memory_release(ctx->memory);
	ga_mutex_destroy(ctx->mutex);
	ga_free(ctx);
}
GaDataSource *gau_data_source_create_memory(GaMemory *memory) {
	GaDataSourceContext *ctx = ga_alloc(sizeof(GaDataSourceContext));
	if (!ctx) return NULL;

	GaDataSource *ret = ga_data_source_create(&(GaDataSourceCreationMinutiae){
		.read = read,
		.seek = seek,
		.tell = tell,
		.close = close,
		.context = ctx,
		.threadsafe = true,
	});
	if (!ret) {
		ga_free(ctx);
		return NULL;
	}
	if (!ga_isok(ga_mutex_create(&ctx->mutex))) {
		ga_free(ctx);
		ga_free(ret);
		return NULL;
	}
	ga_memory_acquire(memory);
	ctx->memory = memory;
	ctx->pos = 0;
	return ret;
}
