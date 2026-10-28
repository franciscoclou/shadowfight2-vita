#ifndef SF_MD5_H
#define SF_MD5_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t h[4];
    uint64_t len;
    uint8_t buf[64];
} md5_ctx;

void md5_init(md5_ctx *c);
void md5_update(md5_ctx *c, const void *data, size_t n);
void md5_hex(md5_ctx *c, char out[33]);   /* finishes c */

#endif
