/* Where the time goes inside our shims: shader compile/link, texture
 * uploads and file I/O, plus frame pacing. Every 10 s with activity it logs
 * one summary line, and a frame slower than SLOW_FRAME_MS gets its own line
 * with what it spent its time on (at most one per second), so long loads
 * and hitches can be read straight off debug.log.
 *
 * Categories are added to from the game's loader threads too, hence the
 * atomics.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

#define REPORT_US     (10 * 1000000ull)
#define SLOW_FRAME_MS 100

static const char *const s_names[SF_PROF_N] = {
    "shader", "link", "texture", "fopen", "fread", "malloc", "inflate", "yield", "write",
    "wait",
};

static uint64_t s_us[SF_PROF_N], s_calls[SF_PROF_N], s_bytes;
static uint64_t s_frame_us[SF_PROF_N];   /* per-frame snapshot base */

void sf_prof_add(int cat, uint64_t us, uint32_t bytes)
{
    __atomic_fetch_add(&s_us[cat], us, __ATOMIC_RELAXED);
    __atomic_fetch_add(&s_calls[cat], 1, __ATOMIC_RELAXED);
    if (bytes)
        __atomic_fetch_add(&s_bytes, bytes, __ATOMIC_RELAXED);
}

static uint64_t get(uint64_t *v)
{
    return __atomic_load_n(v, __ATOMIC_RELAXED);
}

void sf_prof_frame(void)
{
    static uint64_t last_frame, window_start, last_slow_log, frames, slow, worst;
    static uint64_t base_us[SF_PROF_N], base_calls[SF_PROF_N], base_bytes;
    uint64_t now = sf_plat_time_us(), dt;
    int i;

    if (!last_frame) {
        last_frame = window_start = now;
        for (i = 0; i < SF_PROF_N; i++)
            s_frame_us[i] = get(&s_us[i]);
        return;
    }
    dt = now - last_frame;
    last_frame = now;
    frames++;
    if (dt > worst)
        worst = dt;
    if (dt > SLOW_FRAME_MS * 1000ull) {
        slow++;
        if (now - last_slow_log > 1000000) {
            char line[512];
            int n = snprintf(line, sizeof(line), "slow frame: %u ms (", (unsigned)(dt / 1000)), any = 0;
            for (i = 0; i < SF_PROF_N && n < (int)sizeof(line); i++) {
                unsigned ms = (unsigned)((get(&s_us[i]) - s_frame_us[i]) / 1000);
                if (ms)
                    n += snprintf(line + n, sizeof(line) - (size_t)n, "%s%s %u ms", any++ ? ", " : "",
                                  s_names[i], ms);
            }
            LOGI("%s%s)", line, any ? "" : "nothing measured");
            last_slow_log = now;
        }
    }
    for (i = 0; i < SF_PROF_N; i++)
        s_frame_us[i] = get(&s_us[i]);

    if (now - window_start >= REPORT_US) {
        char line[512];
        uint64_t span = now - window_start;
        int n, active = 0;
        n = snprintf(line, sizeof(line), "prof %us: %u frames (%u fps), %u over %d ms, worst %u ms",
                     (unsigned)(span / 1000000), (unsigned)frames,
                     (unsigned)(frames * 1000000 / span), (unsigned)slow, SLOW_FRAME_MS,
                     (unsigned)(worst / 1000));
        for (i = 0; i < SF_PROF_N && n < (int)sizeof(line); i++) {
            uint64_t c = get(&s_calls[i]) - base_calls[i], us = get(&s_us[i]) - base_us[i];
            if (!c)
                continue;
            active = 1;
            n += snprintf(line + n, sizeof(line) - (size_t)n, " | %s %ux %u ms", s_names[i],
                          (unsigned)c, (unsigned)(us / 1000));
            base_calls[i] += c;
            base_us[i] += us;
        }
        if (n < (int)sizeof(line) && get(&s_bytes) != base_bytes) {
            snprintf(line + n, sizeof(line) - (size_t)n, " | read %u KB",
                     (unsigned)((get(&s_bytes) - base_bytes) / 1024));
            base_bytes = get(&s_bytes);
        }
        /* GPU memory left: a texture upload that does not fit fails
         * silently and shows stale memory (gl.c logs each failure). */
        {
            uint32_t vram, ram;
            sf_plat_gl_mem_free(&vram, &ram);
            n = (int)strlen(line);
            if (n < (int)sizeof(line) && (vram || ram))
                snprintf(line + n, sizeof(line) - (size_t)n, " | gpu free vram %u MB ram %u MB",
                         (unsigned)(vram >> 10), (unsigned)(ram >> 10));
        }
        /* Steady gameplay with nothing loading is not worth a line unless
         * frames were slow. */
        if (active || slow)
            LOGI("%s", line);
        window_start = now;
        frames = slow = worst = 0;
    }
}
