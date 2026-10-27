/* Content packs: the .dz archives the game downloads after the first act
 * (Hermit's zone and every later one, boss models, raids, clans). The port
 * is offline, so scripts/fetch_packs.sh fetches them on the PC and they are
 * copied next to ShadowFight2.s3e.raw as <NAME>.dz.
 *
 * The game knows a pack is installed from ram://assets/packs.xml, one
 * <Pack Name Url Version Attach Hash/> per pack; ?PackAssert(ZONE_2) in the
 * quests only looks the name up there. At boot (link 0x4a69ece0) it attaches
 * each Attach="1" pack from the unprefixed path <Name>.dz, which on Android
 * resolves ram:// then rom://, so files in the data directory are found and
 * the game's own pack removal (which deletes from ram) cannot touch them.
 *
 * The file is guarded by assets/packs.xml.hash: the MD5 of
 * "assets/packs.xml" + contents + device ID, as hex, written twice with a
 * newline between (0x4a1f6858; the ID is the salt set at 0x4a69df90). It
 * depends on this console's ID, so it is written here at boot rather than
 * shipped.
 *
 * The boot attacher also compares each pack's MD5 with the Hash attribute,
 * reading the whole file a KB at a time: ~320 MB on every launch. Patch
 * "pack check" (patches.c) makes it compare the path instead, so Hash holds
 * "<Name>.dz".
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Every pack the quests check with ?PackAssert; fetch_packs.sh downloads
 * the same list. */
static const char *const s_names[] = {
    "ZONE_2", "ZONE_3", "ZONE_4", "ZONE_5", "ZONE_6", "ZONE_7", "ZONE_7_2", "ZONE_7_3",
    "ZONE_IM", "ZONE_RAID", "MODELS", "CLANS",
};

/* --- MD5 (RFC 1321) ------------------------------------------------------- */

typedef struct {
    uint32_t h[4];
    uint64_t len;
    uint8_t buf[64];
} md5_ctx;

#define ROL(x, c) (((x) << (c)) | ((x) >> (32 - (c))))

static void md5_block(md5_ctx *c, const uint8_t *p)
{
    static const uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const uint8_t r[16] = { 7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21 };
    uint32_t w[16], a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    int i;

    for (i = 0; i < 16; i++)
        w[i] = p[i * 4] | p[i * 4 + 1] << 8 | p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
    for (i = 0; i < 64; i++) {
        uint32_t f, t;
        int g;
        if (i < 16)      { f = (b & cc) | (~b & d);  g = i; }
        else if (i < 32) { f = (d & b) | (~d & cc);  g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ cc ^ d;           g = (3 * i + 5) & 15; }
        else             { f = cc ^ (b | ~d);        g = (7 * i) & 15; }
        t = d;
        d = cc;
        cc = b;
        b += ROL(a + f + k[i] + w[g], r[(i >> 4) * 4 + (i & 3)]);
        a = t;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
}

static void md5_init(md5_ctx *c)
{
    c->h[0] = 0x67452301;
    c->h[1] = 0xefcdab89;
    c->h[2] = 0x98badcfe;
    c->h[3] = 0x10325476;
    c->len = 0;
}

static void md5_update(md5_ctx *c, const void *data, size_t n)
{
    const uint8_t *p = data;
    while (n--) {
        c->buf[c->len++ & 63] = *p++;
        if (!(c->len & 63))
            md5_block(c, c->buf);
    }
}

static void md5_hex(md5_ctx *c, char out[33])
{
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80;
    int i;

    md5_update(c, &pad, 1);
    pad = 0;
    while ((c->len & 63) != 56)
        md5_update(c, &pad, 1);
    for (i = 0; i < 8; i++) {
        uint8_t b = (uint8_t)(bits >> (i * 8));
        md5_update(c, &b, 1);
    }
    for (i = 0; i < 16; i++)
        sprintf(out + i * 2, "%02x", (unsigned)(c->h[i / 4] >> (i % 4 * 8)) & 0xff);
}

/* --- packs.xml ------------------------------------------------------------ */

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf)
        buf[n] = 0;
    fclose(f);
    return buf;
}

static int write_file(const char *path, const char *data)
{
    FILE *f = fopen(path, "wb");
    size_t n = strlen(data);
    int ok;

    if (!f)
        return 0;
    ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

void sf_packs_register(void)
{
    char xml[2048], path[300], hash_path[310], hash[33], hash_file[70], *old;
    struct stat st;
    size_t len;
    md5_ctx c;
    unsigned i, found = 0;

    /* The game writes this same header and "files" entry on a fresh
     * install; tabs and newlines included, it is byte for byte the same. */
    len = (size_t)snprintf(xml, sizeof(xml),
                           "<?xml version=\"1.0\"?>\n<Packs>\n"
                           "\t<Pack Name=\"files\" Url=\"rom://assets/files.dz\" Version=\"1.9.38\" />\n");
    for (i = 0; i < sizeof(s_names) / sizeof(s_names[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s.dz", sf_data_dir, s_names[i]);
        if (stat(path, &st) != 0)
            continue;
        len += (size_t)snprintf(xml + len, sizeof(xml) - len,
                                "\t<Pack Name=\"%s\" Url=\"%s.dz\" Version=\"1.9.38\" "
                                "Attach=\"1\" Hash=\"%s.dz\" />\n",
                                s_names[i], s_names[i], s_names[i]);
        found++;
    }
    snprintf(xml + len, sizeof(xml) - len, "</Packs>\n");
    if (!found) {
        LOGI("packs: none in %s; zones after the first act will ask for a download", sf_data_dir);
        return;
    }

    snprintf(path, sizeof(path), "%s/ram/assets/packs.xml", sf_data_dir);
    snprintf(hash_path, sizeof(hash_path), "%s.hash", path);
    md5_init(&c);
    md5_update(&c, "assets/packs.xml", strlen("assets/packs.xml"));
    md5_update(&c, xml, strlen(xml));
    md5_update(&c, sf_plat_device_id(), strlen(sf_plat_device_id()));
    md5_hex(&c, hash);
    snprintf(hash_file, sizeof(hash_file), "%s\n%s", hash, hash);

    old = read_file(path);
    if (old && !strcmp(old, xml)) {
        free(old);
        old = read_file(hash_path);
        if (old && !strcmp(old, hash_file)) {
            free(old);
            LOGI("packs: %u installed, packs.xml up to date", found);
            return;
        }
    }
    free(old);

    snprintf(hash_path, sizeof(hash_path), "%s/ram/assets", sf_data_dir);
    mkdir(hash_path, 0777);
    snprintf(hash_path, sizeof(hash_path), "%s.hash", path);
    if (write_file(path, xml) && write_file(hash_path, hash_file))
        LOGI("packs: %u installed, packs.xml written", found);
    else
        LOGE("packs: cannot write %s", path);
}
