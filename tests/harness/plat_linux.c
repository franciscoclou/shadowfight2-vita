/* Linux armhf implementation of platform.h, for running the loader and the
 * real game code under qemu-arm on the PC (scripts/harness.sh).
 *
 * Rendering goes to a 960x544 pbuffer on Mesa's software rasteriser via
 * EGL's surfaceless platform. Every SF_SHOT_EVERY frames the frame is
 * written to $SF_OUT/frame_NNNNN.ppm; the run stops after SF_MAX_FRAMES.
 * SF_TAPS="frame:x:y,frame:x:y" injects touches (held for 6 frames).
 */
#define _GNU_SOURCE
#include "platform/platform.h"
#include "platform/log.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

char sf_data_dir[256] = ".";
/* Reported by s3eMemoryGetInt; matches the Vita build's newlib heap. */
int _newlib_heap_size_user = 200 * 1024 * 1024;

static int s_w, s_h;
static unsigned s_frame;
static EGLDisplay s_dpy;
static EGLSurface s_surf;

uintptr_t sf_plat_map_image(uintptr_t addr, uint32_t text_size, uint32_t data_size)
{
    void *p = mmap((void *)addr, text_size + data_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return 0;
    return (uintptr_t)p;
}

void sf_plat_write_text(void *dst, const void *src, uint32_t len)
{
    memcpy(dst, src, len);
}

void sf_plat_flush(void *addr, uint32_t len)
{
    __builtin___clear_cache((char *)addr, (char *)addr + len);
}

uint64_t sf_plat_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void sf_plat_sleep_us(uint32_t us)
{
    (void)us;   /* the harness runs as fast as qemu allows */
}

void sf_plat_gl_init(int width, int height)
{
    static const EGLint cfg_attr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE
    };
    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint pb_attr[] = { EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE };
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_dpy;
    EGLConfig cfg;
    EGLint n;
    EGLContext ctx;

    s_w = width;
    s_h = height;
    get_dpy = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    s_dpy = get_dpy(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (!eglInitialize(s_dpy, NULL, NULL) || !eglChooseConfig(s_dpy, cfg_attr, &cfg, 1, &n) || !n) {
        LOGE("harness: EGL init failed (%#x)", eglGetError());
        exit(2);
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    s_surf = eglCreatePbufferSurface(s_dpy, cfg, pb_attr);
    ctx = eglCreateContext(s_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (!eglMakeCurrent(s_dpy, s_surf, s_surf, ctx)) {
        LOGE("harness: eglMakeCurrent failed (%#x)", eglGetError());
        exit(2);
    }
    LOGI("harness GL: %s", (const char *)glGetString(GL_RENDERER));
}

static void save_frame(void)
{
    const char *dir = getenv("SF_OUT") ? getenv("SF_OUT") : ".";
    char path[512];
    GLint fbo = 0;
    unsigned char *px = malloc((size_t)s_w * s_h * 4);
    FILE *f;
    int y;

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadPixels(0, 0, s_w, s_h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);

    snprintf(path, sizeof(path), "%s/frame_%05u.ppm", dir, s_frame);
    if ((f = fopen(path, "wb"))) {
        fprintf(f, "P6\n%d %d\n255\n", s_w, s_h);
        for (y = s_h - 1; y >= 0; y--) {
            int x;
            for (x = 0; x < s_w; x++)
                fwrite(px + ((size_t)y * s_w + x) * 4, 1, 3, f);
        }
        fclose(f);
        LOGI("harness: wrote %s", path);
    }
    free(px);
}

void sf_plat_swap(void)
{
    static unsigned every, max;
    if (!every) {
        every = getenv("SF_SHOT_EVERY") ? (unsigned)atoi(getenv("SF_SHOT_EVERY")) : 30;
        max = getenv("SF_MAX_FRAMES") ? (unsigned)atoi(getenv("SF_MAX_FRAMES")) : 300;
    }
    s_frame++;
    if (s_frame % every == 0 || s_frame == 1)
        save_frame();
    eglSwapBuffers(s_dpy, s_surf);
    if (s_frame >= max) {
        save_frame();
        LOGI("harness: reached %u frames, stopping", s_frame);
        sf_log_flush();
        exit(0);
    }
}

void *sf_plat_gl_proc(const char *name)
{
    return (void *)eglGetProcAddress(name);
}

void sf_plat_input(sf_input *out)
{
    const char *taps = getenv("SF_TAPS");
    memset(out, 0, sizeof(*out));
    while (taps && *taps) {
        unsigned f;
        int x, y;
        if (sscanf(taps, "%u:%d:%d", &f, &x, &y) == 3 && s_frame >= f && s_frame < f + 6) {
            out->touch[0].id = 0;
            out->touch[0].x = x;
            out->touch[0].y = y;
            out->touch_count = 1;
        }
        taps = strchr(taps, ',');
        if (taps)
            taps++;
    }
}
