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
#include "s3e/game.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

#define REPORT_US     (10 * 1000000ull)
#define SLOW_FRAME_MS 100

static const char *const s_names[SF_PROF_N] = {
    "shader", "link", "texture", "fopen", "fread", "malloc", "inflate", "yield", "write",
    "wait", "swap", "draw", "open.packs", "open.stat", "open.disk",
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

/* --- frame traces --------------------------------------------------------
 * sf_prof_trace() records the next frames one by one (time, swap, draw
 * calls, and every category's share) and logs them in one go afterwards,
 * so the logging does not slow the frames it measures. Started by the
 * pre-fight screens below, whose bodyguard line-up scrolls unevenly on
 * the Vita. */
#define TRACE_MAX 360

typedef struct {
    uint32_t dt;                 /* us since the previous frame */
    uint32_t us[SF_PROF_N];      /* per category, this frame */
    uint32_t calls[SF_PROF_N];
} sf_trace_frame;

static sf_trace_frame s_trace[TRACE_MAX];
static const char *s_trace_what;
static int s_trace_len, s_trace_want;
static uint64_t s_trace_calls[SF_PROF_N], s_trace_us[SF_PROF_N];

void sf_prof_trace(const char *what, int frames)
{
    int i;
    if (s_trace_want)
        return;
    s_trace_what = what;
    s_trace_len = 0;
    s_trace_want = frames < TRACE_MAX ? frames : TRACE_MAX;
    for (i = 0; i < SF_PROF_N; i++) {
        s_trace_calls[i] = get(&s_calls[i]);
        s_trace_us[i] = get(&s_us[i]);
    }
    LOGI("trace %s: started, %d frames", what, s_trace_want);
}

static void trace_dump(void)
{
    uint64_t total = 0, worst = 0;
    int f, i;
    for (f = 0; f < s_trace_len; f++) {
        total += s_trace[f].dt;
        if (s_trace[f].dt > worst)
            worst = s_trace[f].dt;
    }
    LOGI("trace %s: %d frames in %u ms (avg %u.%u ms, worst %u ms); per frame: ms [swap ms, draws] others",
         s_trace_what, s_trace_len, (unsigned)(total / 1000),
         (unsigned)(total / (s_trace_len ? s_trace_len : 1) / 1000),
         (unsigned)(total / (s_trace_len ? s_trace_len : 1) / 100 % 10), (unsigned)(worst / 1000));
    for (f = 0; f < s_trace_len; f += 4) {
        char line[600];
        int n = snprintf(line, sizeof(line), "trace %s %3d:", s_trace_what, f), k;
        for (k = f; k < f + 4 && k < s_trace_len && n < (int)sizeof(line); k++) {
            const sf_trace_frame *t = &s_trace[k];
            n += snprintf(line + n, sizeof(line) - (size_t)n, " | %u.%u [%u.%u, %u]", t->dt / 1000,
                          t->dt / 100 % 10, t->us[SF_PROF_SWAP] / 1000, t->us[SF_PROF_SWAP] / 100 % 10,
                          t->calls[SF_PROF_DRAW]);
            for (i = 0; i < SF_PROF_N && n < (int)sizeof(line); i++)
                if (i != SF_PROF_SWAP && i != SF_PROF_DRAW && i != SF_PROF_MALLOC && t->us[i] >= 500)
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " %s %u", s_names[i], t->us[i] / 1000);
        }
        LOGI("%s", line);
    }
}

static void trace_frame(uint64_t dt)
{
    sf_trace_frame *t;
    int i;
    if (!s_trace_want)
        return;
    t = &s_trace[s_trace_len++];
    t->dt = (uint32_t)dt;
    for (i = 0; i < SF_PROF_N; i++) {
        uint64_t c = get(&s_calls[i]), us = get(&s_us[i]);
        t->calls[i] = (uint32_t)(c - s_trace_calls[i]);
        t->us[i] = (uint32_t)(us - s_trace_us[i]);
        s_trace_calls[i] = c;
        s_trace_us[i] = us;
    }
    if (s_trace_len >= s_trace_want) {
        trace_dump();
        s_trace_want = 0;
    }
}

/* The pre-fight screens ask how many frames to stay up right after they
 * are built (0x4a3fe67c): EnemiesScreen, the bodyguard line-up, 276
 * (0x4a404bb0), VsScreen, a single opponent, 204 (0x4a40567c). Both
 * answers are kept, as softfp float bits; asking starts a trace a bit
 * longer than the screen. */
static uint32_t lineup_frames(void)
{
    sf_prof_trace("lineup", 320);
    return 0x438a0000;   /* 276.0f */
}

static uint32_t vs_frames(void)
{
    sf_prof_trace("vs", 240);
    return 0x434c0000;   /* 204.0f */
}

void sf_prof_install(void)
{
    sf_game_hook("lineup trace", 0x4a404bb0, 0xe3a00000, 0xe344038a, (void *)lineup_frames);
    sf_game_hook("vs trace", 0x4a40567c, 0xe3a00000, 0xe344034c, (void *)vs_frames);
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
    trace_frame(dt);
    if (dt > worst)
        worst = dt;
    if (dt > SLOW_FRAME_MS * 1000ull) {
        slow++;
        if (now - last_slow_log > 1000000) {
            char line[768];
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
        char line[768];
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
