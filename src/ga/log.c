#include <stdio.h>
#include <stdarg.h>

#include "gorilla/ga.h"

static void log_to_file(void *ctx, GaLogCategory category, const char *file, const char *function, int line, const char *msg) {
	fprintf(ctx, "%s: %s:%s:%d: %s\n", category == GaLogInfo ? "info" : category == GaLogWarn ? "warn" : "<?>", file, function, line, msg);
}
static void null_logger(void *ctx, GaLogCategory category, const char *file, const char *function, int line, const char *msg) {}

static GaCbLogger logger = null_logger;
static void *logger_ctx;

void ga_register_logger(GaCbLogger llogger, void *ctx) {
	logger = llogger;
	logger_ctx = ctx;
}

ga_result ga_open_logfile(const char *fname) {
	FILE *fp = fopen(fname, "a");
	if (!fp) return GA_ERR_SYS_IO;;
	logger = log_to_file;
	logger_ctx = fp;
	return GA_OK;
}

void ga_do_log(GaLogCategory category, const char *file, const char *function, int line, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	char buf[1024];
	vsnprintf(buf, sizeof(buf), fmt, ap);
	logger(logger_ctx, category, file, function, line, buf);

	va_end(ap);
}
