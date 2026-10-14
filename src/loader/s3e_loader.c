#include "loader/s3e_loader.h"
#include "platform/log.h"

#include "platform/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

sf_s3e_module sf_game;

static void *read_file(const char *path, uint32_t *size_out)
{
    FILE *f = fopen(path, "rb");
    long n;
    void *buf = NULL;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)n);
        if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
            free(buf);
            buf = NULL;
        }
        *size_out = (uint32_t)n;
    }
    fclose(f);
    return buf;
}

int sf_s3e_load(sf_s3e_module *mod, const char *path, uintptr_t load_addr,
                sf_resolve_fn resolve, sf_stub_fn stub)
{
    const sf_s3e_header *h;
    const uint8_t *blob, *names[512];
    uint8_t *file, *data;
    uint32_t size, p, end, nnames = 0, i;
    int32_t delta;

    memset(mod, 0, sizeof(*mod));
    file = read_file(path, &size);
    if (!file) {
        LOGE("cannot read %s", path);
        return -1;
    }
    h = (const sf_s3e_header *)file;
    if (memcmp(h->magic, "XE3U", 4) != 0 || h->ext_hdr < 8 ||
        h->code_off + h->code_file > size || h->data_off > h->code_file) {
        LOGE("%s: not a usable XE3U image", path);
        free(file);
        return -1;
    }
    mod->hdr = *h;
    blob = file + h->code_off;

    LOGI("XE3U version=%#x arch=%#x base=%#x entry=%#x code=%#x data=%#x mem=%#x",
         (unsigned)h->version, h->arch, (unsigned)h->base, (unsigned)h->entry,
         (unsigned)h->code_file, (unsigned)h->data_off, (unsigned)h->code_mem);

    /* --- map: text RX at load_addr, data RW immediately after ---------- */
    mod->text_size = ALIGN_UP(h->data_off, 0x1000);
    mod->data_size = ALIGN_UP(h->code_mem - h->data_off, 0x1000);
    if (mod->text_size != h->data_off) {
        LOGE("data_off %#x is not page aligned", (unsigned)h->data_off);
        free(file);
        return -1;
    }

    load_addr = sf_plat_map_image(load_addr, mod->text_size, mod->data_size);
    if (!load_addr) {
        LOGE("cannot map %#x + %#x bytes", (unsigned)mod->text_size, (unsigned)mod->data_size);
        free(file);
        return -1;
    }
    mod->base = load_addr;
    data = (uint8_t *)(load_addr + mod->text_size);

    sf_plat_write_text((void *)load_addr, blob, h->data_off);
    memcpy(data, blob + h->data_off, h->code_file - h->data_off);
    memset(data + (h->code_file - h->data_off), 0, mod->data_size - (h->code_file - h->data_off));

    /* --- fixups ------------------------------------------------------- */
    delta = (int32_t)(load_addr - h->base);
    for (p = h->fixup_off, end = h->fixup_off + h->fixup_size; p < end; ) {
        uint32_t kind = *(const uint32_t *)(file + p);
        uint32_t len  = *(const uint32_t *)(file + p + 4);
        const uint8_t *body = file + p + 8;

        if (len < 8)
            break;
        if (kind == 0) {
            uint16_t n = *(const uint16_t *)body;
            const uint8_t *q = body + 2;
            for (i = 0; i < n && nnames < 512; i++) {
                names[nnames++] = q;
                q += strlen((const char *)q) + 1;
            }
        } else if (kind == 1) {
            uint32_t n = *(const uint32_t *)body, bad = 0;
            const uint32_t *offs = (const uint32_t *)(body + 4);
            for (i = 0; i < n; i++) {
                if (offs[i] < h->data_off || offs[i] + 4 > h->code_mem) {
                    bad++;
                    continue;
                }
                *(uint32_t *)(load_addr + offs[i]) += (uint32_t)delta;
            }
            LOGI("applied %u base relocations (%u outside data)", (unsigned)n, (unsigned)bad);
        } else if (kind >= 2 && kind <= 4) {
            uint32_t n = *(const uint32_t *)body;
            const uint8_t *e = body + 4;
            for (i = 0; i < n; i++, e += 6) {
                uint32_t off = ((uint32_t)(e[0] | e[1] << 8) << 16) | (uint32_t)(e[2] | e[3] << 8);
                uint16_t sym = (uint16_t)(e[4] | e[5] << 8);
                const char *name = sym < nnames ? (const char *)names[sym] : "?";
                void *fn;
                if (off < h->data_off || off + 4 > h->code_mem) {
                    LOGE("import %s slot %#x outside data", name, (unsigned)off);
                    continue;
                }
                fn = resolve(name);
                if (!fn) {
                    fn = stub((int)sym, name);
                    mod->unresolved++;
                }
                *(uint32_t *)(load_addr + off) = (uint32_t)fn;
            }
            LOGI("filled %u import slots (type %u), %d unresolved so far",
                 (unsigned)n, (unsigned)kind, mod->unresolved);
        } else {
            LOGW("unknown fixup kind %u (%u bytes)", (unsigned)kind, (unsigned)len);
        }
        p += len;
    }

    sf_plat_flush((void *)load_addr, mod->text_size);
    sf_plat_flush(data, mod->data_size);

    {
        char *cfg = malloc(h->config_size + 1);
        if (cfg) {
            memcpy(cfg, file + h->config_off, h->config_size);
            cfg[h->config_size] = 0;
        }
        mod->config = cfg;
    }

    mod->entry = (void (*)(void))(load_addr + h->entry);
    free(file);
    LOGI("mapped %s at %#x (text %#x, data %#x), entry %p",
         path, (unsigned)load_addr, (unsigned)mod->text_size,
         (unsigned)mod->data_size, (void *)mod->entry);
    return 0;
}
