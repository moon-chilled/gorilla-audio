#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

/* Loop Sample Source */
struct GaSampleSourceContext {
	GauSampleSourceLoop *loop_src;
	GaSampleSource *inner_src;
	atomic_ssz trigger_frame; // upon reaching this frame [negative → last]...
	atomic_usz target_frame;  // ...skip to this frame
	atomic_bool loop_enable;
	u32 frame_size;
	atomic_u32 loop_count;
};

struct GauSampleSourceLoop {
	GaSampleSource *sample_src;
	GaSampleSourceContext *ctx;
};

static usz read(GaSampleSourceContext *ctx, void *dst, usz num_frames, GaCbOnSeek onseek, void *seek_ctx) {
	usz pos, total;
	usz total_read = 0;
	GaSampleSource *ss = ctx->inner_src;

	if (!ctx->loop_enable) return ga_sample_source_read(ss, dst, num_frames, 0, 0);

	if (!ga_sample_source_tell(ss, &pos, NULL)) return 0;

	usz trigger_frame;
	if (ctx->trigger_frame < 0) {
		if (!ga_sample_source_tell(ss, NULL, &trigger_frame)) return 0;
	} else {
		trigger_frame = (usz)ctx->trigger_frame;
	}
	usz target_frame = ctx->target_frame;

	if (pos > trigger_frame) return ga_sample_source_read(ss, dst, num_frames, 0, 0);
	u32 frame_size = ctx->frame_size;
	while (num_frames) {
		usz avail = trigger_frame - pos;
		bool do_seek = avail <= num_frames;
		usz to_read = do_seek ? avail : num_frames;
		usz num_read = ga_sample_source_read(ss, dst,  to_read, 0, 0);
		total_read += num_read;
		num_frames -= num_read;
		dst = (char*)dst + num_read * frame_size;
		if (do_seek && to_read == num_read) {
			ga_sample_source_seek(ss, target_frame);
			ctx->loop_count++;
			if (onseek)
				onseek(total_read, target_frame - trigger_frame, seek_ctx);
		}
		ga_sample_source_tell(ss, &pos, &total); //todo check
	}
	return total_read;
}
static bool end(GaSampleSourceContext *ctx) {
	return ga_sample_source_end(ctx->inner_src);
}
static bool ready(GaSampleSourceContext *ctx, usz num_frames) {
	return ga_sample_source_ready(ctx->inner_src, num_frames);
}
//todo what to do when frame_offset is outside of [target_frame,trigger_frame)
static ga_result seek(GaSampleSourceContext *ctx, usz frame_offset) {
	return ga_sample_source_seek(ctx->inner_src, frame_offset);
}
static ga_result tell(GaSampleSourceContext *ctx, usz *frames, usz *total_frame) {
	return ga_sample_source_tell(ctx->inner_src, frames, total_frame);
}
static void close(GaSampleSourceContext *ctx) {
	ga_sample_source_release(ctx->inner_src);
	ga_free(ctx->loop_src);
	ga_free(ctx);
}
void gau_sample_source_loop_set(GauSampleSourceLoop *src, ssz trigger_frame, usz target_frame, bool loop_enable) {
	src->ctx->target_frame = target_frame;
	src->ctx->trigger_frame = trigger_frame;
	src->ctx->loop_enable = loop_enable;
	src->ctx->loop_count = 0;
}
u32 gau_sample_source_loop_count(GauSampleSourceLoop *src) {
	return src->ctx->loop_count;
}
void gau_sample_source_loop_disable(GauSampleSourceLoop *src) {
	gau_sample_source_loop_set(src, -1, 0, false);
}
void gau_sample_source_loop_enable(GauSampleSourceLoop *src) {
	gau_sample_source_loop_set(src, -1, 0, true);
}

GaSampleSource *gau_sample_source_loop_sample_source(GauSampleSourceLoop *s) {
	return s->sample_src;
}

GauSampleSourceLoop *gau_sample_source_create_loop(GaSampleSource *src) {
	if (!(ga_sample_source_flags(src) & GaDataAccessFlag_Seekable)) return NULL;

	GauSampleSourceLoop *ret = ga_alloc(sizeof(GauSampleSourceLoop));
	if (!ret) return NULL;
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) {
		ga_free(ret);
		return NULL;
	}

	GaSampleSourceCreationMinutiae m = {
		.read = read,
		.end = end,
		.ready = ready,
		.seek = seek,
		.tell = tell,
		.close = close,
		.context = ctx,
		.threadsafe = true,
	};
	ga_sample_source_format(src, &m.format);

	ctx->trigger_frame = -1;
	ctx->target_frame = 0;
	ctx->loop_enable = true;
	ctx->loop_count = 0;
	ctx->inner_src = src;
	ctx->frame_size = ga_format_frame_size(&m.format);

	ret->sample_src = ga_sample_source_create(&m);
	if (!ret->sample_src) {
		ga_free(ctx);
		ga_free(ret);
		return NULL;
	}

	ga_sample_source_acquire(src);
	return ret;
}
