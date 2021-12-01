#include "gorilla/ga.h"
#include "gorilla/gau.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

struct GaSampleSourceContext {
	float t;
};

static ga_usize ss_read(GaSampleSourceContext *ctx, void *dst, ga_usize num_frames, GaCbOnSeek onseek, void *seek_ctx) {
	float *buf = dst;
	for (ga_uint32 i = 0; i < num_frames * 2; i += 2) {
		ga_float32 f = sin(ctx->t);

		float pan = sin(ctx->t / 300) / 2.0f + 0.5f;
		buf[i] = f * pan;
		buf[i + 1] = f * (1.0f - pan);
		ctx->t = ctx->t + 0.03f;
		if (ctx->t > 300 * M_PI) ctx->t -= 600*M_PI;
	}
	return num_frames;
}
static ga_bool ss_end(GaSampleSourceContext *ctx) { return ga_false; }

int main(int argc, char** argv) {
	GaSampleSource *ss = NULL;
	GaHandle *handle = NULL;
	GauManager *mgr = gau_manager_create();
	if (!mgr) goto fail;

	GaSampleSourceContext ctx = { .t = 0, };
	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.format = {
			.sample_fmt = GaSampleFormat_F32,
			.num_channels = 2,
			.frame_rate = 48000,
		},
		.context = &ctx,
		.threadsafe = ga_true,
	};
	ss = ga_sample_source_create(&m);
	if (!ss) goto fail;
	handle = gau_create_handle_buffered_samples(mgr, ss);
	if (!handle) goto fail;

	ga_result res = ga_handle_play(handle);
	if (!ga_isok(res)) goto fail;

	while(1) ga_thread_sleep(1);

fail:
	if (handle) ga_handle_destroy(handle);
	if (ss) ga_sample_source_release(ss);
	if (mgr) gau_manager_destroy(mgr);
	return 1;
}
