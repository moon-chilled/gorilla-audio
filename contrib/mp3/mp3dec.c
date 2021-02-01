#include "gorilla/ga.h"

#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DRMP3_API static
#include "dr_mp3.h"


struct GaSampleSourceContext {
	drmp3 mp3;
	GaMutex mutex;
	GaDataSource *data_src;
};

void *mp3_alloc(ga_usize sz, void *data) { return ga_alloc(sz); }
void *mp3_realloc(void *p, ga_usize sz, void *data) { return ga_realloc(p, sz); }
void mp3_free(void *p, void *data) { return ga_free(p); }

static drmp3_allocation_callbacks mp3_allocator = {.onMalloc = mp3_alloc, .onRealloc = mp3_realloc, .onFree = mp3_free};

ga_usize mp3_read(void *ctx, void *buf, ga_usize l) {
	return ga_data_source_read(ctx, buf, 1, l);
}
drmp3_bool32 mp3_seek(void *ctx, int offset, drmp3_seek_origin origin) {
	switch (origin) {
		case drmp3_seek_origin_start: return ga_isok(ga_data_source_seek(ctx, offset, GaSeekOrigin_Set));
		case drmp3_seek_origin_current: return ga_isok(ga_data_source_seek(ctx, offset, GaSeekOrigin_Cur));
		default: return 0;
	}
}

static ga_usize ss_read(GaSampleSourceContext *ctx, void *dst, ga_usize num_samples, GaCbOnSeek onseek, void *seek_ctx) {
	//drmp3_uint64 frames = num_samples / ctx->mp3.channels;
	ga_mutex_lock(ctx->mutex);
	drmp3_uint64 res = drmp3_read_pcm_frames_s16(&ctx->mp3, num_samples, dst);
	ga_mutex_unlock(ctx->mutex);
	return res;// * ctx->mp3.channels;
}
static ga_bool ss_end(GaSampleSourceContext *ctx) {
	return ctx->mp3.atEnd;
}
static ga_result ss_seek(GaSampleSourceContext *ctx, ga_usize sample_offset) {
	ga_mutex_lock(ctx->mutex);
	drmp3_bool32 res = drmp3_seek_to_pcm_frame(&ctx->mp3, sample_offset/* / ctx->mp3.channels*/);
	ga_mutex_unlock(ctx->mutex);
	return res ? GA_OK : GA_ERR_GENERIC;
}
static ga_result ss_tell(GaSampleSourceContext *ctx, ga_usize *cur, ga_usize *total) {
	if (total && !ctx->mp3.onSeek) return GA_ERR_MIS_UNSUP;
	ga_result res = GA_OK;

	ga_mutex_lock(ctx->mutex);
	if (cur) *cur = ctx->mp3.currentPCMFrame;// * ctx->mp3.channels;
	if (total) {
		drmp3_uint64 tot_frames_pcm;
		if (!drmp3_get_mp3_and_pcm_frame_count(&ctx->mp3, NULL, &tot_frames_pcm)) res = GA_ERR_GENERIC;
		*total = tot_frames_pcm;// * ctx->mp3.channels;
	}
	ga_mutex_unlock(ctx->mutex);

	return res;
}
static void ss_close(GaSampleSourceContext *ctx) {
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->mutex);
	drmp3_uninit(&ctx->mp3);
	ga_free(ctx);
}


GaSampleSource *ga_contrib_sample_source_create_mp3(GaDataSource *data_src) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;
	if (!ga_isok(ga_mutex_create(&ctx->mutex))) goto fail;
	ctx->data_src = data_src;

	if (!drmp3_init(&ctx->mp3, mp3_read, (ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable) ? mp3_seek : NULL, data_src, &mp3_allocator)) goto fail;
	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.tell = ss_tell,
		.close = ss_close,
		.context = ctx,
		.format = {.num_channels = ctx->mp3.channels, .bits_per_sample = 16, .sample_rate = ctx->mp3.sampleRate},
		.threadsafe = ga_true,
	};
	if (ga_data_source_flags(data_src) & GaDataAccessFlag_Seekable) m.seek = ss_seek;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;
	ga_data_source_acquire(data_src);
	return ret;

fail:
	ga_mutex_destroy(ctx->mutex);
	ga_free(ctx);
	return NULL;
}
