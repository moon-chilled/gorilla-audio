#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

/* Loop Sample Source */
struct GaSampleSourceContext {
	GauSampleSourceLoop *loop_src;
	GaSampleSource *inner_src;
	atomic_ssz trigger_sample; //upon reaching this sample [negative → last]
	atomic_usz target_sample;  //skip to this sample
	atomic_bool loop_enable;
	u32 sample_size;
	atomic_u32 loop_count;
};

struct GauSampleSourceLoop {
	GaSampleSource *sample_src;
	GaSampleSourceContext *ctx;
};

static usz read(GaSampleSourceContext *ctx, void *dst, usz num_samples, GaCbOnSeek onseek, void *seek_ctx) {
	usz pos, total;
	usz total_read = 0;
	GaSampleSource *ss = ctx->inner_src;

	if (!ctx->loop_enable) return ga_sample_source_read(ss, dst, num_samples, 0, 0);

	if (!ga_sample_source_tell(ss, &pos, NULL)) return 0;

	usz trigger_sample;
	if (ctx->trigger_sample < 0) {
		if (!ga_sample_source_tell(ss, NULL, &trigger_sample)) return 0;
	} else {
		trigger_sample = (usz)ctx->trigger_sample;
	}
	usz target_sample = ctx->target_sample;

	if (pos > trigger_sample) return ga_sample_source_read(ss, dst, num_samples, 0, 0);
	u32 sample_size = ctx->sample_size;
	while (num_samples) {
		usz avail = trigger_sample - pos;
		bool do_seek = avail <= num_samples;
		usz to_read = do_seek ? avail : num_samples;
		usz num_read = ga_sample_source_read(ss, dst,  to_read, 0, 0);
		total_read += num_read;
		num_samples -= num_read;
		dst = (char*)dst + num_read * sample_size;
		if (do_seek && to_read == num_read) {
			ga_sample_source_seek(ss, target_sample);
			ctx->loop_count++;
			if (onseek)
				onseek(total_read, target_sample - trigger_sample, seek_ctx);
		}
		ga_sample_source_tell(ss, &pos, &total); //todo check
	}
	return total_read;
}
static bool end(GaSampleSourceContext *ctx) {
	return ga_sample_source_end(ctx->inner_src);
}
static bool ready(GaSampleSourceContext *ctx, usz num_samples) {
	return ga_sample_source_ready(ctx->inner_src, num_samples);
}
static ga_result seek(GaSampleSourceContext *ctx, usz sample_sffset) {
	return ga_sample_source_seek(ctx->inner_src, sample_sffset);
}
static ga_result tell(GaSampleSourceContext *ctx, usz *samples, usz *total_samples) {
	return ga_sample_source_tell(ctx->inner_src, samples, total_samples);
}
static void close(GaSampleSourceContext *ctx) {
	ga_sample_source_release(ctx->inner_src);
	ga_free(ctx->loop_src);
	ga_free(ctx);
}
void gau_sample_source_loop_set(GauSampleSourceLoop *src, ssz trigger_sample, usz target_sample, bool loop_enable) {
	src->ctx->target_sample = target_sample;
	src->ctx->trigger_sample = trigger_sample;
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

	ctx->trigger_sample = -1;
	ctx->target_sample = 0;
	ctx->loop_enable = true;
	ctx->loop_count = 0;
	ctx->inner_src = src;
	ctx->sample_size = ga_format_sample_size(&m.format);

	ret->sample_src = ga_sample_source_create(&m);
	if (!ret->sample_src) {
		ga_free(ctx);
		ga_free(ret);
		return NULL;
	}

	ga_sample_source_acquire(src);
	return ret;
}
