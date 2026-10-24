/* s3eCompression: streaming decompression with the input pulled through a
 * game callback. Contract, from libs3e_android.so (docs/RE_NOTES.md):
 *
 *   Init(type, read_cb, ud) -> handle 1..4, or 0 on failure
 *       type 0 AUTO, 1 NONE (stored), 2 ZLIB, 3 GZIP, 4 LZMA
 *       read_cb(&{buf, len}, ud) -> bytes supplied; fewer than len = end
 *   Read(handle, buf, &len) -> 0 with *len bytes produced, 1 at end/error
 *   Final(handle) -> 0
 *
 * AUTO sniffs the first bytes like the original: gzip magic, then a valid
 * zlib header, then an LZMA-alone header, else stored. LZMA is not
 * implemented yet; it is logged so its first use is obvious.
 */
#include "s3e/s3e_internal.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum { T_AUTO, T_NONE, T_ZLIB, T_GZIP, T_LZMA };
#define MAX_STREAMS 4
#define IN_SIZE (64 * 1024)

typedef struct {
    void *buf;
    int32_t len;
} sf_read_req;

typedef struct {
    int used, type, in_eof, done;
    s3eCallback read_cb;
    void *ud;
    z_stream z;
    uint8_t in[IN_SIZE];
} sf_stream;

static sf_stream *s_streams[MAX_STREAMS];

/* Top up the input buffer; returns bytes now available. */
static uint32_t fill(sf_stream *s)
{
    if (s->z.avail_in == 0 && !s->in_eof) {
        sf_read_req req = { s->in, IN_SIZE };
        int32_t n = s->read_cb(&req, s->ud);
        if (n < 0)
            n = 0;
        if (n < IN_SIZE)
            s->in_eof = 1;
        s->z.next_in = s->in;
        s->z.avail_in = (uInt)n;
    }
    return s->z.avail_in;
}

static int sniff(const uint8_t *p, uint32_t n)
{
    if (n >= 2 && p[0] == 0x1f && p[1] == 0x8b)
        return T_GZIP;
    if (n >= 2 && p[0] < 0x80 && (p[0] & 0xf) == 8 && !(p[1] & 0x20) &&
        ((p[0] << 8) | p[1]) % 31 == 0)
        return T_ZLIB;
    if (n >= 13 && p[0] <= 0xe1 && !p[9] && !p[10] && !p[11] && !p[12] &&
        (p[1] | p[2] << 8 | p[3] << 16 | (uint32_t)p[4] << 24) <= 0x900000)
        return T_LZMA;
    return T_NONE;
}

static int32_t sf_s3eCompressionDecompInit(uint32_t type, s3eCallback read_cb, void *ud)
{
    sf_stream *s;
    int h;

    if (type > T_LZMA || !read_cb)
        return 0;
    for (h = 0; h < MAX_STREAMS && s_streams[h]; h++) {}
    if (h == MAX_STREAMS) {
        LOGE("s3eCompressionDecompInit: all %d streams in use", MAX_STREAMS);
        return 0;
    }
    s = calloc(1, sizeof(*s));
    if (!s)
        return 0;
    s->read_cb = read_cb;
    s->ud = ud;
    fill(s);
    if (type == T_AUTO)
        type = (uint32_t)sniff(s->z.next_in, s->z.avail_in);
    s->type = (int)type;

    /* zlib streams are inflated raw, past their 2-byte header: the
     * game's data carries Adler-32 trailers that do not match, and
     * Marmalade's own inflater does not check them. */
    if (type == T_ZLIB && s->z.avail_in >= 2) {
        s->z.next_in += 2;
        s->z.avail_in -= 2;
    }
    if (type == T_ZLIB || type == T_GZIP) {
        if (inflateInit2(&s->z, type == T_GZIP ? 16 + MAX_WBITS : -MAX_WBITS) != Z_OK) {
            free(s);
            return 0;
        }
    } else if (type == T_LZMA) {
        LOGE("s3eCompressionDecompInit: LZMA stream, not implemented");
        free(s);
        return 0;
    }
    s_streams[h] = s;
    LOGV("s3eCompressionDecompInit -> %d (type %d)", h + 1, s->type);
    return h + 1;
}

static int32_t sf_s3eCompressionDecompRead(int32_t h, void *buf, int32_t *len)
{
    sf_stream *s = h >= 1 && h <= MAX_STREAMS ? s_streams[h - 1] : NULL;
    int32_t want, got = 0;

    if (!s || !buf || !len || *len <= 0)
        return S3E_RESULT_ERROR;
    want = *len;

    if (s->type == T_NONE) {
        while (got < want && fill(s)) {
            uint32_t n = s->z.avail_in < (uint32_t)(want - got) ? s->z.avail_in : (uint32_t)(want - got);
            memcpy((uint8_t *)buf + got, s->z.next_in, n);
            s->z.next_in += n;
            s->z.avail_in -= n;
            got += (int32_t)n;
        }
    } else {
        s->z.next_out = buf;
        s->z.avail_out = (uInt)want;
        while (!s->done && s->z.avail_out) {
            int r;
            if (!fill(s) && s->in_eof) {
                /* Input exhausted: let zlib flush what it still holds. */
                r = inflate(&s->z, Z_FINISH);
                if (r != Z_STREAM_END && r != Z_BUF_ERROR && r != Z_OK)
                    LOGW("s3eCompression %d: truncated stream (%d)", h, r);
                s->done = 1;
                break;
            }
            r = inflate(&s->z, Z_NO_FLUSH);
            if (r == Z_STREAM_END)
                s->done = 1;
            else if (r != Z_OK && r != Z_BUF_ERROR) {
                LOGW("s3eCompression %d: inflate error %d (%s)", h, r, s->z.msg ? s->z.msg : "");
                s->done = 1;
            }
        }
        got = want - (int32_t)s->z.avail_out;
    }
    *len = got;
    return got ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
}

static int32_t sf_s3eCompressionDecompFinal(int32_t h)
{
    sf_stream *s = h >= 1 && h <= MAX_STREAMS ? s_streams[h - 1] : NULL;
    if (!s)
        return S3E_RESULT_ERROR;
    if (s->type == T_ZLIB || s->type == T_GZIP)
        inflateEnd(&s->z);
    free(s);
    s_streams[h - 1] = NULL;
    return S3E_RESULT_SUCCESS;
}

const sf_export sf_exports_compress[] = {
    { "s3eCompressionDecompInit", (void *)sf_s3eCompressionDecompInit },
    { "s3eCompressionDecompRead", (void *)sf_s3eCompressionDecompRead },
    { "s3eCompressionDecompFinal", (void *)sf_s3eCompressionDecompFinal },
    { NULL, NULL }
};
