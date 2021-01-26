#include <string.h>

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

struct GaSampleSourceContext {
	GaSound *sound;
	u32 sample_size;
	usz num_samples;
	GaMutex pos_mutex;
	atomic_usz pos;
};

static usz read(GaSampleSourceContext *ctx, void *dst, usz num_samples, GaCbOnSeek onseek, void *seek_ctx) {
	ga_mutex_lock(ctx->pos_mutex);
	usz pos = ctx->pos;
	usz avail = ctx->num_samples - pos;
	usz num_read = min(avail, num_samples);
	ctx->pos += num_read;
	ga_mutex_unlock(ctx->pos_mutex);

	char *src = (char*)ga_sound_data(ctx->sound) + pos * ctx->sample_size;
	memcpy(dst, src, num_read * ctx->sample_size);

	return num_read;
}
static bool end(GaSampleSourceContext *ctx) {
	return ctx->pos >= ctx->num_samples;
}
static ga_result seek(GaSampleSourceContext *ctx, usz sample_offset) {
	if (sample_offset > ctx->num_samples)
		return GA_ERR_MIS_PARAM;
	ga_mutex_lock(ctx->pos_mutex);
	ctx->pos = sample_offset;
	ga_mutex_unlock(ctx->pos_mutex);
	return GA_OK;
}
static ga_result tell(GaSampleSourceContext *ctx, usz *pos, usz *total) {
	if (pos) *pos = ctx->pos;
	if (total) *total = ctx->num_samples;
	return GA_OK;
}
static void close(GaSampleSourceContext *ctx) {
	ga_sound_release(ctx->sound);
	ga_mutex_destroy(ctx->pos_mutex);
	ga_free(ctx);
}

GaSampleSource *gau_sample_source_create_sound(GaSound *sound) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;

	GaSampleSourceCreationMinutiae m = {
		.read = read,
		.end = end,
		.ready = NULL,
		.seek = seek,
		.tell = tell,
		.close = close,
		.context = ctx,
		.threadsafe = true,
	};
	ga_sound_format(sound, &m.format);

	ctx->sound = sound;
	ctx->sample_size = ga_format_sample_size(&m.format);
	ctx->num_samples = ga_sound_num_samples(sound);
	ctx->pos = 0;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) {
		ga_free(ctx);
		return NULL;
	}
	ga_sound_acquire(sound);
	return ret;
}
