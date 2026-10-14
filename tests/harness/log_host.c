/* Harness logging: same LOGx API as src/platform/log.c, to stderr. */
#include "platform/log.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

void sf_log_init(void) { setvbuf(stderr, NULL, _IOLBF, 0); }
void sf_log_shutdown(void) {}
void sf_log_flush(void) { fflush(stderr); }

void sf_log_write(const char *level, const char *file, int line, const char *fmt, ...)
{
    const char *base = strrchr(file, '/');
    va_list ap;
    fprintf(stderr, "[%8u][%s] %s:%d: ", (unsigned)(sf_plat_time_us() / 1000), level,
            base ? base + 1 : file, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
