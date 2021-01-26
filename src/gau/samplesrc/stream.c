#include "gorilla/gau.h"
#include "gorilla/ga_u_internal.h"

static usz read(GaSampleSourceContext *context, void *dst, usz num_samples,
                GaCbOnSeek onseek, void *seek_ctx) {
	return ga_stream_read((GaBufferedStream*)context, dst, num_samples);
}
static bool end(GaSampleSourceContext *context) {
	return ga_stream_end((GaBufferedStream*)context);
}
static bool ready(GaSampleSourceContext *context, usz num_samples) {
	return ga_stream_ready((GaBufferedStream*)context, num_samples);
}
static ga_result seek(GaSampleSourceContext *context, usz sample_offset) {
	return ga_stream_seek((GaBufferedStream*)context, sample_offset);
}
static ga_result tell(GaSampleSourceContext *context, usz *samples, usz *totalSamples) {
	return ga_stream_tell((GaBufferedStream*)context, samples, totalSamples);
}
static void close(GaSampleSourceContext *context) {
	ga_stream_release((GaBufferedStream*)context);
}

GaSampleSource *gau_sample_source_create_stream(GaStreamManager *mgr, GaSampleSource *sample_src, usz buffer_samples) {
	GaSampleSourceCreationMinutiae m = {
		.read = read,
		.end = end,
		.ready = ready,
		.tell = tell,
		.close = close,
		.threadsafe = true,
	};
	ga_sample_source_format(sample_src, &m.format);

	GaBufferedStream *stream = ga_stream_create(mgr, sample_src, buffer_samples * ga_format_sample_size(&m.format));
	if (!stream) return NULL;
	if (ga_stream_flags(stream) & GaDataAccessFlag_Seekable) m.seek = seek;

	m.context = (GaSampleSourceContext*)stream;

	GaSampleSource *ret = ga_sample_source_create(&m);
	if (!ret) ga_stream_release(stream);
	return ret;
}
