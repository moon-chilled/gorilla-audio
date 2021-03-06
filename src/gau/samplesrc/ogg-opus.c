#include "opusfile.h"
//#include "internal.h" //opus

#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

static int ogg_read(void *datasource, u8 *ptr, int size) {
	GaDataSource **ds = datasource;
	return ga_data_source_read(*ds, ptr, 1, size);
}

static int ogg_seek(void *datasource, ogg_int64_t offset, int whence) {
	// concession for 32-bit platforms
	if (offset > GA_SSIZE_MAX) return -1;

	ssz off = offset;

	GaDataSource **ds = datasource;
	ga_result res;
	switch (whence) {
		case SEEK_SET: res = ga_data_source_seek(*ds, off, GaSeekOrigin_Set); break;
		case SEEK_CUR: res = ga_data_source_seek(*ds, off, GaSeekOrigin_Cur); break;
		case SEEK_END: res = ga_data_source_seek(*ds, off, GaSeekOrigin_End); break;
		default: res = GA_ERR_MIS_PARAM; break;
	}

	return ga_isok(res) ? 0 : -1;
}

static long ogg_tell(void *datasource) {
	GaDataSource **ds = datasource;
	return ga_data_source_tell(*ds);
}

struct GaSampleSourceContext {
	GaDataSource *data_src;
	bool end_of_samples;
	GaFormat format;
	OggOpusFile *ogg_file;
	GaMutex ogg_mutex;
};

static usz ss_read(GaSampleSourceContext *ctx, void *odst, usz num_frames,
                                            GaCbOnSeek onseek, void *seek_ctx) {
	usz ret = 0;
	usz left = num_frames * ctx->format.num_channels;
	f32 *dst = odst;

	with_mutex(ctx->ogg_mutex) {
		do {
			int i = op_read_float(ctx->ogg_file, dst, left, NULL);
			if (i > 0) {
				ret += i;
				dst += i * ctx->format.num_channels;
				left -= i * ctx->format.num_channels;
			} else {
				ctx->end_of_samples = true;
				break;
			}
		} while (left);
	}

	return ret;
}

static bool ss_end(GaSampleSourceContext *ctx) {
	return ctx->end_of_samples;
}
static ga_result ss_seek(GaSampleSourceContext *ctx, usz frame_offset) {
	int res;
	with_mutex(ctx->ogg_mutex) {
		res = op_pcm_seek(ctx->ogg_file, frame_offset);
		ctx->end_of_samples = false;
	}
	switch (res) {
		case 0: return GA_OK; //op_pcm_seek returns 0 on success
		case OP_ENOSEEK: return GA_ERR_MIS_UNSUP;
		case OP_EINVAL:
		case OP_EBADLINK: return GA_ERR_MIS_PARAM;
		case OP_EREAD: return GA_ERR_SYS_IO;
		default: return GA_ERR_SYS_LIB;
	}
}
static ga_result ss_tell(GaSampleSourceContext *ctx, usz *cur, usz *ototal) {
	ga_result ret = GA_OK;
	with_mutex(ctx->ogg_mutex) {
		if (ototal) {
			s64 ctotal = op_pcm_total(ctx->ogg_file, -1);
			if (ctotal < 0) ret = GA_ERR_MIS_UNSUP;
			else *ototal = (usz)ctotal;
		}
		if (cur) *cur = op_pcm_tell(ctx->ogg_file);
	}
	return ret;
}
static void ss_close(GaSampleSourceContext *ctx) {
	op_free(ctx->ogg_file);
	ga_data_source_release(ctx->data_src);
	ga_mutex_destroy(ctx->ogg_mutex);
	ga_free(ctx);
}
GaSampleSource *gau_sample_source_create_opus(GaDataSource *data) {
	GaSampleSourceContext *ctx = ga_alloc(sizeof(GaSampleSourceContext));
	if (!ctx) return NULL;

	ctx->data_src = data;
	ctx->end_of_samples = false;
	if (!ga_isok(ga_mutex_create(&ctx->ogg_mutex))) goto fail;

	GaSampleSourceCreationMinutiae m = {
		.read = ss_read,
		.end = ss_end,
		.ready = NULL,
		.tell = ss_tell,
		.close = ss_close,
		.context = ctx,
		.threadsafe = true,
	};
	bool seekable = ga_data_source_flags(data) & GaDataAccessFlag_Seekable;
	if (seekable) m.seek = ss_seek;

	OpusFileCallbacks callbacks = {
		.read = ogg_read,
		.tell = ogg_tell,
	};

	if (seekable) callbacks.seek = ogg_seek;

	/* 0 means "open" */
	if (!(ctx->ogg_file = op_open_callbacks(&ctx->data_src, &callbacks, NULL, 0, NULL))) goto fail;
	if (seekable != op_seekable(ctx->ogg_file)) return NULL; //???

	m.format.sample_fmt = GaSampleFormat_F32;
	m.format.num_channels = op_head(ctx->ogg_file, 0)->channel_count;
	m.format.frame_rate = 48000;
	ctx->format = m.format;
	bool is_valid_ogg = m.format.num_channels <= 2;
	if (!is_valid_ogg) {
		op_free(ctx->ogg_file);
		goto fail;
	}

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) goto fail;

	ga_data_source_acquire(data);
	return ret;

fail:
	ga_free(ctx);
	return NULL;
}
