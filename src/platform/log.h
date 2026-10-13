#ifndef SF_LOG_H
#define SF_LOG_H

/* Live debug logging over UDP.
 *
 * The Vita has no console, so every log line is sent as a UDP datagram to the
 * development PC and to the subnet broadcast address. Listen for it with:
 *
 *     ./scripts/log.sh          (or)
 *     stdbuf -oL nc -u -k -l 18194 >> debug.log 2>&1
 *
 * Lines are also appended to ux0:data/shadowfight2/debug.log on the Vita itself,
 * so a crash that happens before the network is up is still recoverable over
 * FTP.
 */

#include <stdarg.h>

void sf_log_init(void);
void sf_log_shutdown(void);
void sf_log_write(const char *level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* Flush pending output. Call before anything that might abort the process. */
void sf_log_flush(void);

#define LOGI(...) sf_log_write("I", __FILE__, __LINE__, __VA_ARGS__)
#define LOGW(...) sf_log_write("W", __FILE__, __LINE__, __VA_ARGS__)
#define LOGE(...) sf_log_write("E", __FILE__, __LINE__, __VA_ARGS__)

/* Chatty per-frame logging. Compiled out unless -DSF_VERBOSE_LOG=1. */
#if SF_VERBOSE_LOG
#define LOGV(...) sf_log_write("V", __FILE__, __LINE__, __VA_ARGS__)
#else
#define LOGV(...) ((void)0)
#endif

#endif /* SF_LOG_H */
