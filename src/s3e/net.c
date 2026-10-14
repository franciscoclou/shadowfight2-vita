/* s3eSocket / s3eInet: the multiplayer (SmartFox) and online services.
 * Milestone 1 runs offline: the network reports itself unavailable and
 * socket creation fails, which the game treats as "no connection".
 */
#include "s3e/s3e_internal.h"

#include <stdio.h>

#define S3E_SOCKET_ERR_NETDOWN 1006

static void *sf_s3eSocketCreate(uint32_t type, int32_t domain)
{
    LOGI("s3eSocketCreate -> offline");
    return NULL;
}
static int32_t sf_s3eSocketClose(void *s) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSocketConnect(void *s, const void *addr, void *cb, void *ud) { return S3E_RESULT_ERROR; }
static int32_t sf_s3eSocketSend(void *s, const char *buf, uint32_t len, int32_t flags) { return -1; }
static int32_t sf_s3eSocketRecv(void *s, char *buf, uint32_t len, int32_t flags) { return -1; }
static int32_t sf_s3eSocketReadable(void *s, void *cb, void *ud) { return S3E_RESULT_ERROR; }
static int32_t sf_s3eSocketWritable(void *s, void *cb, void *ud) { return S3E_RESULT_ERROR; }
static int32_t sf_s3eSocketGetError(void) { return S3E_SOCKET_ERR_NETDOWN; }
/* 1 S3E_SOCKET_NETWORK_AVAILABLE = 0; everything else 0 too. */
static int32_t sf_s3eSocketGetInt(uint32_t prop) { return 0; }
static const char *sf_s3eSocketGetString(uint32_t prop) { return ""; }

static int32_t sf_s3eInetLookup(const char *host, void *addr, void *cb, void *ud)
{
    LOGI("s3eInetLookup(%s) -> offline", host ? host : "");
    return S3E_RESULT_ERROR;
}
static void sf_s3eInetLookupCancel(void) {}
static uint16_t sf_s3eInetHtons(uint16_t v) { return (uint16_t)(v >> 8 | v << 8); }

static int32_t sf_s3eInetAton(uint32_t *out, const char *s)
{
    unsigned a, b, c, d;
    char tail;
    if (!out || !s || sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255)
        return 0;
    *out = a | b << 8 | c << 16 | d << 24;   /* network byte order */
    return 1;
}

const sf_export sf_exports_net[] = {
    { "s3eSocketCreate", (void *)sf_s3eSocketCreate },
    { "s3eSocketClose", (void *)sf_s3eSocketClose },
    { "s3eSocketConnect", (void *)sf_s3eSocketConnect },
    { "s3eSocketSend", (void *)sf_s3eSocketSend },
    { "s3eSocketRecv", (void *)sf_s3eSocketRecv },
    { "s3eSocketReadable", (void *)sf_s3eSocketReadable },
    { "s3eSocketWritable", (void *)sf_s3eSocketWritable },
    { "s3eSocketGetError", (void *)sf_s3eSocketGetError },
    { "s3eSocketGetInt", (void *)sf_s3eSocketGetInt },
    { "s3eSocketGetString", (void *)sf_s3eSocketGetString },
    { "s3eInetLookup", (void *)sf_s3eInetLookup },
    { "s3eInetLookupCancel", (void *)sf_s3eInetLookupCancel },
    { "s3eInetHtons", (void *)sf_s3eInetHtons },
    { "s3eInetAton", (void *)sf_s3eInetAton },
    { NULL, NULL }
};
