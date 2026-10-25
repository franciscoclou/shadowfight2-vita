/* Unit test for src/s3e/hud.c against the game's real button atlas: the
 * plist is parsed, a fake upload of the atlas pixels is swapped for the
 * erased copy, and toggling re-uploads the right version. Run with:
 * make -C tests/harness test (inside shadowfight2-harness; needs game_out/,
 * and makes the reference images with scripts/hud_reference.py). */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char sf_data_dir[256] = "../../game_out";
static int s_shown, s_bound, s_uploads;
static uint8_t s_last_buf[512 * 512 * 4];
static const uint8_t *s_last;

void sf_log_write(const char *l, const char *f, int n, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); fprintf(stderr, "[%s] ", l); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); }
int sf_touch_controls_shown(void) { return s_shown; }
void *sf_file_load(const char *name, uint32_t *size)
{
    char path[512]; FILE *f; long n; void *buf;
    snprintf(path, sizeof(path), "%s/%s", sf_data_dir, name);
    if (!(f = fopen(path, "rb"))) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n); fread(buf, 1, (size_t)n, f); fclose(f);
    *size = (uint32_t)n; return buf;
}
uint64_t sf_plat_time_us(void) { static uint64_t t; return t += 1000; }
static void *upload_elsewhere(void *px)
{ return (void *)sf_hud_tex_image(0, 512, 512, 0x1908, 0x1401, px); }
static void get_int(unsigned e, int *v) { *v = s_bound; }
static void bind(unsigned t, unsigned tex) { s_bound = (int)tex; }
static void sub(unsigned t, int l, int x, int y, int w, int h, unsigned f, unsigned ty, const void *d)
{ s_uploads++; memcpy(s_last_buf, d, (size_t)w * h * 4); s_last = s_last_buf; }   /* GL copies */
void *sf_plat_gl_proc(const char *name)
{
    if (!strcmp(name, "glGetIntegerv")) return (void *)get_int;
    if (!strcmp(name, "glBindTexture")) return (void *)bind;
    if (!strcmp(name, "glTexSubImage2D")) return (void *)sub;
    return NULL;
}

static uint8_t *load(const char *dir, const char *name, size_t n)
{
    char path[512]; FILE *f; uint8_t *b = malloc(n);
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (!(f = fopen(path, "rb")) || fread(b, 1, n, f) != n) { fprintf(stderr, "missing %s\n", path); exit(2); }
    fclose(f); return b;
}

#define CHECK(c, msg) do { if (c) printf("ok    %s\n", msg); else { printf("FAIL  %s\n", msg); fails++; } } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *ref = getenv("SF_HUD_REF") ? getenv("SF_HUD_REF") : ".";
    size_t n = 512 * 512 * 4;
    uint8_t *orig = load(ref, "atlas.rgba", n), *hidden = load(ref, "atlas_hidden.rgba", n);
    const uint8_t *out;
    int fails = 0;
    unsigned id = 7;

    sf_hud_file_opened("assets/768/textures/buttons/fight/batchButtonsFight.png");
    s_bound = 7;
    out = sf_hud_tex_image(0, 512, 512, 0x1908, 0x1401, orig);
    CHECK(out && !memcmp(out, hidden, n), "hidden by default: upload is the erased atlas");
    CHECK(memcmp(orig, hidden, n) != 0, "erasing changes the atlas");

    s_bound = 3;
    sf_hud_apply(1);
    CHECK(s_uploads == 1 && s_last && !memcmp(s_last, orig, n), "show re-uploads the original");
    CHECK(s_bound == 3, "previous texture binding restored");
    sf_hud_apply(0);
    CHECK(s_uploads == 2 && s_last && !memcmp(s_last, hidden, n), "hide re-uploads the erased copy");

    sf_hud_tex_deleted(1, &id);
    sf_hud_apply(1);
    CHECK(s_uploads == 2, "deleted texture is no longer touched");

    s_shown = 1;
    sf_hud_file_opened("assets/768/textures/buttons/fight/batchButtonsFight.png");
    CHECK(sf_hud_tex_image(0, 512, 512, 0x1908, 0x1401, orig) == NULL, "shown: game pixels uploaded untouched");
    CHECK(sf_hud_tex_image(0, 512, 512, 0x1908, 0x1401, orig) == NULL, "other 512x512 uploads ignored");

    {
        pthread_t th;
        void *r = (void *)1;
        s_shown = 0;
        sf_hud_file_opened("assets/768/textures/buttons/fight/batchButtonsFight.png");
        pthread_create(&th, NULL, upload_elsewhere, orig);
        pthread_join(th, &r);
        CHECK(r == NULL, "upload from another thread is not taken for the atlas");
    }

    printf(fails ? "FAILURES: %d\n" : "all passed\n", fails);
    return fails != 0;
}
