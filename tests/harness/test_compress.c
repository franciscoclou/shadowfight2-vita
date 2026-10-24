/* Unit test for src/s3e/compress.c: zlib, gzip and stored streams, with
 * good and corrupted trailers, read through an s3e-style callback.
 * Run with: make -C tests/harness test (inside shadowfight2-harness). */
#include "s3e/s3e_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <stdarg.h>
void sf_log_write(const char *l, const char *f, int n, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); fprintf(stderr, "[%s] ", l); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); }
extern const sf_export sf_exports_compress[];
typedef int32_t (*init_t)(uint32_t, s3eCallback, void *);
typedef int32_t (*read_t)(int32_t, void *, int32_t *);
typedef int32_t (*fin_t)(int32_t);
typedef struct { const uint8_t *d; uint32_t n, pos, chunk; } src_t;
typedef struct { void *buf; int32_t len; } req_t;
static int32_t cb(void *sys, void *ud)
{
    req_t *r = sys; src_t *s = ud;
    uint32_t n = s->n - s->pos; if (n > (uint32_t)r->len) n = r->len; if (n > s->chunk) n = s->chunk;
    memcpy(r->buf, s->d + s->pos, n); s->pos += n; return (int32_t)n;
}
static int run(const char *what, uint8_t *z, uint32_t zn, const uint8_t *orig, uint32_t on, uint32_t chunk)
{
    init_t init = (init_t)sf_exports_compress[0].fn; read_t rd = (read_t)sf_exports_compress[1].fn; fin_t fin = (fin_t)sf_exports_compress[2].fn;
    src_t s = { z, zn, 0, chunk }; uint8_t out[300000]; uint32_t got = 0; int32_t h = init(0, cb, &s), len;
    if (!h) { printf("%s: init failed\n", what); return 1; }
    for (;;) { len = 777; if (rd(h, out + got, &len) != 0) break; got += len; }
    fin(h);
    int ok = got == on && !memcmp(out, orig, on);
    printf("%-28s got %u/%u %s\n", what, got, on, ok ? "OK" : "FAIL");
    return !ok;
}
int main(void)
{
    static uint8_t orig[200000], z[300000]; uLongf zn; int fails = 0, i; unsigned c;
    for (i = 0; i < (int)sizeof(orig); i++) orig[i] = (uint8_t)(i * 7 ^ (i >> 5));
    /* Marmalade treats a short read as end of input (libs3e DecompRead),
     * so the callback only returns short at the real end. */
    for (c = 0xffffffffu; c; c = 0) {
        zn = sizeof(z); compress2(z, &zn, orig, sizeof(orig), 9);
        fails += run("zlib, good trailer", z, zn, orig, sizeof(orig), c);
        z[zn - 1] ^= 0xff; fails += run("zlib, bad adler32", z, zn, orig, sizeof(orig), c);
        { z_stream g; memset(&g, 0, sizeof(g)); deflateInit2(&g, 9, Z_DEFLATED, 16 + MAX_WBITS, 8, 0);
          g.next_in = orig; g.avail_in = sizeof(orig); g.next_out = z; g.avail_out = sizeof(z); deflate(&g, Z_FINISH); zn = g.total_out; deflateEnd(&g); }
        fails += run("gzip, good trailer", z, zn, orig, sizeof(orig), c);
        z[zn - 6] ^= 0xff; fails += run("gzip, bad crc32", z, zn, orig, sizeof(orig), c);
        fails += run("stored", (uint8_t *)orig + 3, 5000, orig + 3, 5000, c);
    }
    printf(fails ? "FAILURES: %d\n" : "all passed\n", fails);
    return fails != 0;
}
