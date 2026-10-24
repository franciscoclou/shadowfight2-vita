/* s3eDevice, s3eTimer, s3eDebug, s3eConfig, s3eSurface, s3eGL and s3eExt.
 * Enum values come from Marmalade's own headers (preserved in preprocessed
 * .ii files of open-source extensions) or from decompiling
 * libs3e_android.so with REA; see docs/RE_NOTES.md.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <ctype.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- timing ------------------------------------------------------------ */

static uint64_t s_t0;

uint64_t sf_time_ms(void)
{
    return (sf_plat_time_us() - s_t0) / 1000;
}

static uint64_t sf_s3eTimerGetMs(void) { return sf_time_ms(); }
static uint64_t sf_s3eTimerGetUST(void) { return sf_time_ms(); }

static uint64_t sf_s3eTimerGetUTC(void)
{
    return (uint64_t)time(NULL) * 1000;
}

static int64_t sf_s3eTimerGetLocaltimeOffset(void)
{
    return 0;
}

/* One-shot timers, dispatched from s3eDeviceYield like Marmalade does. */
#define MAX_TIMERS 32
static struct { uint64_t due; s3eCallback fn; void *ud; } s_timers[MAX_TIMERS];

static int32_t sf_s3eTimerCancelTimer(s3eCallback fn, void *ud)
{
    int i;
    for (i = 0; i < MAX_TIMERS; i++)
        if (s_timers[i].fn == fn && s_timers[i].ud == ud)
            s_timers[i].fn = NULL;
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eTimerSetTimer(uint32_t ms, s3eCallback fn, void *ud)
{
    int i;
    if (!fn)
        return S3E_RESULT_ERROR;
    sf_s3eTimerCancelTimer(fn, ud);
    for (i = 0; i < MAX_TIMERS; i++) {
        if (!s_timers[i].fn) {
            s_timers[i].due = sf_time_ms() + ms;
            s_timers[i].fn = fn;
            s_timers[i].ud = ud;
            return S3E_RESULT_SUCCESS;
        }
    }
    LOGE("s3eTimerSetTimer: out of timers");
    return S3E_RESULT_ERROR;
}

static void run_timers(void)
{
    uint64_t now = sf_time_ms();
    int i;
    for (i = 0; i < MAX_TIMERS; i++) {
        if (s_timers[i].fn && s_timers[i].due <= now) {
            s3eCallback fn = s_timers[i].fn;
            void *ud = s_timers[i].ud;
            s_timers[i].fn = NULL;
            fn(NULL, ud);
        }
    }
}

/* --- device ------------------------------------------------------------ */

static int s_quit;
static struct { s3eCallback fn; void *ud; } s_dev_cb[32];

int sf_quit_requested(void) { return s_quit; }

/* Marmalade delivers every callback (input, timers) on the main thread.
 * The game's loader thread yields too; dispatching there would deliver
 * each touch twice, concurrently, and the second touch-end crashes in
 * cocos2d's touch dictionary (link 0x4a02f8cc). */
static int64_t sf_s3eDeviceYield(int32_t ms)
{
    uint64_t until = sf_time_ms() + (ms > 0 ? (uint32_t)ms : 0);
    if (!sf_thread_is_main()) {
        sf_plat_sleep_us(ms > 0 ? (uint32_t)ms * 1000 : 0);
        return 0;
    }
    do {
        sf_input_poll();
        run_timers();
        if (ms > 0)
            sf_plat_sleep_us(1000);
    } while (sf_time_ms() < until);
    return 0;
}

static int64_t sf_s3eDeviceYieldUntilEvent(int32_t ms)
{
    return sf_s3eDeviceYield(ms > 0 ? (ms < 16 ? ms : 16) : 0);
}

static int32_t sf_s3eDeviceCheckQuitRequest(void) { return s_quit; }
static int32_t sf_s3eDeviceRequestQuit(void) { s_quit = 1; return 0; }

static void sf_s3eDeviceExit(int32_t code)
{
    LOGI("s3eDeviceExit(%d)", (int)code);
    sf_log_flush();
    exit(code);
}

static void sf_s3eDeviceAbort(void)
{
    LOGE("s3eDeviceAbort");
    sf_log_flush();
    abort();
}

static int32_t sf_s3eDeviceRegister(uint32_t id, s3eCallback fn, void *ud)
{
    if (id < 32) {
        s_dev_cb[id].fn = fn;
        s_dev_cb[id].ud = ud;
    }
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eDeviceUnRegister(uint32_t id, s3eCallback fn)
{
    if (id < 32 && s_dev_cb[id].fn == fn)
        s_dev_cb[id].fn = NULL;
    return S3E_RESULT_SUCCESS;
}

/* s3eDeviceProperty, exact (s3eDevice.h). We present as an Android
 * device: the game's data and code paths are the Android ones. */
static int32_t sf_s3eDeviceGetInt(uint32_t prop)
{
    switch (prop) {
    case 0:  return 19;          /* S3E_DEVICE_OS = S3E_OS_ID_ANDROID */
    case 1:  return 16000;       /* S3E_DEVICE_CLASS = ANDROID_GENERIC */
    case 4:  return 1;           /* S3E_DEVICE_LANGUAGE = ENGLISH */
    case 6:  return 128 * 1024;  /* S3E_DEVICE_FREE_RAM (KB) */
    case 7:  return 512 * 1024;  /* S3E_DEVICE_TOTAL_RAM (KB) */
    case 8:  return 12;          /* S3E_DEVICE_ARCHITECTURE = ARM7 */
    case 9:  return 0x00070400;  /* S3E_DEVICE_S3E_VERSION */
    case 10: return 100;         /* S3E_DEVICE_BATTERY_LEVEL */
    case 12: return 1;           /* S3E_DEVICE_MAINS_POWER */
    case 16: return 3;           /* S3E_DEVICE_FPU = NEON */
    case 28: return 3;           /* S3E_DEVICE_NUM_CPU_CORES */
    case 29: return 512 * 1024;  /* S3E_DEVICE_MEM_TOTAL (KB) */
    case 30: return 128 * 1024;  /* S3E_DEVICE_MEM_FREE (KB) */
    case 34: return 0;           /* S3E_DEVICE_SUPPORTS_SUSPEND_RESUME */
    default:
        LOGI("s3eDeviceGetInt(%u) -> 0", (unsigned)prop);
        return 0;
    }
}

static const char *sf_s3eDeviceGetString(uint32_t prop)
{
    switch (prop) {
    case 0:  return "ANDROID";              /* S3E_DEVICE_OS */
    case 2:  return "PSVita";               /* S3E_DEVICE_ID */
    case 8:  return "ARM7";                 /* S3E_DEVICE_ARCHITECTURE */
    case 9:  return "7.4.0";                /* S3E_DEVICE_S3E_VERSION */
    case 13: return "4.4.2";                /* S3E_DEVICE_OS_VERSION */
    case 14: return "Cortex-A9";            /* S3E_DEVICE_CHIPSET */
    case 19: return "19";                   /* S3E_DEVICE_SDK_VERSION */
    case 20: return "en_US";                /* S3E_DEVICE_LOCALE */
    case 21: return "PS Vita";              /* S3E_DEVICE_NAME */
    case 25: return sf_plat_device_id();    /* S3E_DEVICE_UNIQUE_ID */
    case 31: return "UTC";                  /* S3E_DEVICE_TIMEZONE */
    default:
        LOGI("s3eDeviceGetString(%u) -> \"\"", (unsigned)prop);
        return "";
    }
}

static int32_t sf_s3eDeviceSetInt(uint32_t prop, int32_t v)
{
    LOGI("s3eDeviceSetInt(%u, %d)", (unsigned)prop, (int)v);
    return S3E_RESULT_SUCCESS;
}

/* --- debug ------------------------------------------------------------- */

static void sf_s3eDebugOutputString(const char *s) { if (s) LOGI("[game] %s", s); }
static void sf_s3eDebugTraceLine(const char *s) { if (s) LOGI("[trace] %s", s); }
static void sf_s3eDebugPrint(int x, int y, const char *s, int wrap) { (void)x; (void)y; (void)s; (void)wrap; }
static int32_t sf_s3eDebugIsDebuggerPresent(void) { return 0; }

static int32_t sf_s3eDebugErrorShow(uint32_t type, const char *s)
{
    LOGW("[error %u] %s", (unsigned)type, s ? s : "");
    return 2;   /* S3E_ERROR_SHOW_IGNORE */
}

static int32_t sf_s3eDebugAssertShow(uint32_t type, const char *s)
{
    LOGW("[assert %u] %s", (unsigned)type, s ? s : "");
    return 2;
}

static int32_t sf_s3eDebugGetInt(uint32_t prop)
{
    return prop == 0 ? 1 : 0;   /* S3E_DEBUG_TRACE_ENABLED */
}

/* --- config (.icf) ----------------------------------------------------- */

typedef struct { char sec[48], key[64], val[160]; } cfg_ent;
static cfg_ent *s_cfg;
static int s_ncfg, s_capcfg;

/* Parse ICF text: "[Section]" headers, "key=value" lines, "#" comments,
 * and "{OS=...}"/"{ID=...}" condition blocks, which we skip -- none of
 * them target the Vita, and "{}" ends a block. */
static void parse_icf(const char *text)
{
    char sec[48] = "S3E", line[400];
    int skipping = 0;
    const char *p = text;

    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char *s, *e, *eq;
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = 0;
        p = nl ? nl + 1 : NULL;

        if ((s = strchr(line, '#')))
            *s = 0;
        for (s = line; isspace((unsigned char)*s); s++) {}
        for (e = s + strlen(s); e > s && isspace((unsigned char)e[-1]); e--) {}
        *e = 0;
        if (!*s)
            continue;
        if (*s == '{') {
            skipping = strcmp(s, "{}") != 0;
            continue;
        }
        if (skipping)
            continue;
        if (*s == '[') {
            snprintf(sec, sizeof(sec), "%.*s", (int)(strcspn(s + 1, "]")), s + 1);
            continue;
        }
        if (!(eq = strchr(s, '=')))
            continue;
        *eq = 0;
        for (e = eq; e > s && isspace((unsigned char)e[-1]); e--) {}
        *e = 0;
        for (eq++; isspace((unsigned char)*eq); eq++) {}
        if (*eq == '"') {
            eq++;
            if ((e = strrchr(eq, '"')))
                *e = 0;
        }
        if (s_ncfg == s_capcfg)
            s_cfg = realloc(s_cfg, sizeof(*s_cfg) * (s_capcfg = s_capcfg ? s_capcfg * 2 : 128));
        snprintf(s_cfg[s_ncfg].sec, sizeof(s_cfg[0].sec), "%s", sec);
        snprintf(s_cfg[s_ncfg].key, sizeof(s_cfg[0].key), "%s", s);
        snprintf(s_cfg[s_ncfg].val, sizeof(s_cfg[0].val), "%s", eq);
        s_ncfg++;
    }
}

static const char *cfg_get(const char *sec, const char *key)
{
    int i;
    /* Later files (app.icf) override earlier ones (s3e.icf). */
    for (i = s_ncfg - 1; i >= 0; i--)
        if (!strcasecmp(s_cfg[i].sec, sec) && !strcasecmp(s_cfg[i].key, key))
            return s_cfg[i].val;
    return NULL;
}

static int32_t sf_s3eConfigGetString(const char *sec, const char *key, char *out)
{
    const char *v = sec && key ? cfg_get(sec, key) : NULL;
    LOGI("s3eConfigGetString([%s] %s) = %s", sec ? sec : "", key ? key : "", v ? v : "(none)");
    if (!v || !out)
        return S3E_RESULT_ERROR;
    snprintf(out, 128, "%s", v);   /* S3E_CONFIG_STRING_MAX */
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eConfigGetInt(const char *sec, const char *key, int32_t *out)
{
    const char *v = sec && key ? cfg_get(sec, key) : NULL;
    LOGI("s3eConfigGetInt([%s] %s) = %s", sec ? sec : "", key ? key : "", v ? v : "(none)");
    if (!v || !out)
        return S3E_RESULT_ERROR;
    *out = (int32_t)strtol(v, NULL, 0);
    return S3E_RESULT_SUCCESS;
}

static void load_icf_file(const char *name)
{
    char path[300];
    FILE *f;
    long n;
    char *buf;

    snprintf(path, sizeof(path), "%s/%s", sf_data_dir, name);
    if (!(f = fopen(path, "rb")))
        return;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = calloc(1, (size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) == (size_t)n)
        parse_icf(buf);
    free(buf);
    fclose(f);
    LOGI("loaded %s (%d config entries total)", path, s_ncfg);
}

void sf_device_init(const char *embedded_config)
{
    if (!s_t0)
        s_t0 = sf_plat_time_us();
    if (!embedded_config)
        return;
    s_ncfg = 0;
    parse_icf(embedded_config);
    /* prepare_game.sh installs app_android.icf as app.icf, the name
     * Marmalade's runtime reads. */
    load_icf_file("app.icf");
    load_icf_file("game.icf");
}

/* --- surface + s3eGL --------------------------------------------------- */

/* s3eSurfaceProperty: 0 WIDTH, 1 HEIGHT, 2 PITCH, 3 PIXEL_TYPE,
 * 4 DEVICE_WIDTH, 5 DEVICE_HEIGHT, 6 DEVICE_PIXEL_TYPE, 7 DEVICE_PITCH,
 * 8 BLIT_DIRECTION, 9 DISPLAY, 10 NUM_DISPLAYS, 11 DEVICE_BLIT_DIRECTION,
 * 12/13 DEVICE_WIDTH/HEIGHT_QUANTISED, 14 ORIENTATION_LOCK, 15 GL_MODE,
 * 16/17 WIDTH/HEIGHT_UNSCALED (libs3e_android.so s3eSurfaceGetInt). */
static int32_t sf_s3eSurfaceGetInt(uint32_t prop)
{
    switch (prop) {
    case 0: case 4: case 12: case 16: return SF_SCREEN_W;
    case 1: case 5: case 13: case 17: return SF_SCREEN_H;
    case 2: case 7: return SF_SCREEN_W * 2;
    case 3: case 6: return 0x300565;   /* S3E_SURFACE_PIXEL_TYPE_RGB565 */
    case 8: case 11: return 0;         /* S3E_SURFACE_BLIT_DIR_NORMAL */
    case 9: return 0;
    case 10: return 1;
    case 14: return 2;                 /* landscape lock */
    case 15: return 1;
    default:
        LOGI("s3eSurfaceGetInt(%u) -> 0", (unsigned)prop);
        return 0;
    }
}

static int32_t sf_s3eSurfaceSetInt(uint32_t prop, int32_t v)
{
    LOGI("s3eSurfaceSetInt(%u, %d)", (unsigned)prop, (int)v);
    return S3E_RESULT_SUCCESS;
}

static uint16_t s_dummy_surface[16];
static void *sf_s3eSurfacePtr(void) { return s_dummy_surface; }
static int32_t sf_s3eSurfaceSetup(void) { return S3E_RESULT_SUCCESS; }
static void sf_s3eSurfaceShow(void) {}
static int32_t sf_s3eSurfaceRegister(uint32_t id, s3eCallback fn, void *ud) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eSurfaceUnRegister(uint32_t id, s3eCallback fn) { return S3E_RESULT_SUCCESS; }

/* s3eGLProperty: 0 S3E_GL_VERSION, 1 S3E_EGL_VERSION, 2 S3E_GL_MUST_SUSPEND */
static int32_t sf_s3eGLGetInt(uint32_t prop)
{
    switch (prop) {
    case 0: return 0x200;
    case 1: return 0x140;
    case 2: return 0;
    default: return -1;
    }
}

static int s_native_window[4] = { SF_SCREEN_W, SF_SCREEN_H, 0, 0 };
static void *sf_s3eGLGetNativeWindow(void)
{
    sf_gl_init();
    return s_native_window;
}
static int32_t sf_s3eGLRegister(uint32_t id, s3eCallback fn, void *ud) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eGLUnRegister(uint32_t id, s3eCallback fn) { return S3E_RESULT_SUCCESS; }

/* --- extensions ------------------------------------------------------- */

/* Every Marmalade extension (billing, Facebook, AppsFlyer, ...) asks for its
 * function table here. Answering "not found" makes each one report itself
 * unavailable, which the game handles. */
extern const void *const sf_s3eThread_table[20];

/* s3eDeviceUniqueID (hash 0x91a0de04, one slot): Get() -> ID string. The
 * game builds its device identifier from it (link 0x4a1c8738) and, on a
 * non-desktop device, treats an empty one as a tampered save: it logs
 * "Hack error: user.xml" and never leaves the title screen. */
static const char *sf_s3eDeviceUniqueIDGet(void)
{
    return sf_plat_device_id();
}

static int32_t sf_s3eExtGetHash(uint32_t hash, void *table, uint32_t size)
{
    if (hash == 0xcc0b4a28 && table && size == 20 * sizeof(void *)) {   /* s3eThread */
        memcpy(table, sf_s3eThread_table, size);
        LOGI("s3eExtGetHash: s3eThread provided");
        return S3E_RESULT_SUCCESS;
    }
    if (hash == 0x91a0de04 && table && size == sizeof(void *)) {
        *(const void **)table = (const void *)sf_s3eDeviceUniqueIDGet;
        LOGI("s3eExtGetHash: s3eDeviceUniqueID provided (%s)", sf_plat_device_id());
        return S3E_RESULT_SUCCESS;
    }
    LOGI("s3eExtGetHash(%#x, %u bytes) -> unavailable", (unsigned)hash, (unsigned)size);
    if (table && size)
        memset(table, 0, size);
    return S3E_RESULT_ERROR;
}

const sf_export sf_exports_device[] = {
    { "s3eTimerGetMs", (void *)sf_s3eTimerGetMs },
    { "s3eTimerGetUST", (void *)sf_s3eTimerGetUST },
    { "s3eTimerGetUTC", (void *)sf_s3eTimerGetUTC },
    { "s3eTimerGetLocaltimeOffset", (void *)sf_s3eTimerGetLocaltimeOffset },
    { "s3eTimerSetTimer", (void *)sf_s3eTimerSetTimer },
    { "s3eTimerCancelTimer", (void *)sf_s3eTimerCancelTimer },
    { "s3eDeviceYield", (void *)sf_s3eDeviceYield },
    { "s3eDeviceYieldUntilEvent", (void *)sf_s3eDeviceYieldUntilEvent },
    { "s3eDeviceCheckQuitRequest", (void *)sf_s3eDeviceCheckQuitRequest },
    { "s3eDeviceRequestQuit", (void *)sf_s3eDeviceRequestQuit },
    { "s3eDeviceExit", (void *)sf_s3eDeviceExit },
    { "s3eDeviceAbort", (void *)sf_s3eDeviceAbort },
    { "s3eDeviceRegister", (void *)sf_s3eDeviceRegister },
    { "s3eDeviceUnRegister", (void *)sf_s3eDeviceUnRegister },
    { "s3eDeviceGetInt", (void *)sf_s3eDeviceGetInt },
    { "s3eDeviceGetString", (void *)sf_s3eDeviceGetString },
    { "s3eDeviceSetInt", (void *)sf_s3eDeviceSetInt },
    { "s3eDebugOutputString", (void *)sf_s3eDebugOutputString },
    { "s3eDebugTraceLine", (void *)sf_s3eDebugTraceLine },
    { "s3eDebugPrint", (void *)sf_s3eDebugPrint },
    { "s3eDebugIsDebuggerPresent", (void *)sf_s3eDebugIsDebuggerPresent },
    { "s3eDebugErrorShow", (void *)sf_s3eDebugErrorShow },
    { "s3eDebugAssertShow", (void *)sf_s3eDebugAssertShow },
    { "s3eDebugGetInt", (void *)sf_s3eDebugGetInt },
    { "s3eConfigGetString", (void *)sf_s3eConfigGetString },
    { "s3eConfigGetInt", (void *)sf_s3eConfigGetInt },
    { "s3eSurfaceGetInt", (void *)sf_s3eSurfaceGetInt },
    { "s3eSurfaceSetInt", (void *)sf_s3eSurfaceSetInt },
    { "s3eSurfacePtr", (void *)sf_s3eSurfacePtr },
    { "s3eSurfaceSetup", (void *)sf_s3eSurfaceSetup },
    { "s3eSurfaceShow", (void *)sf_s3eSurfaceShow },
    { "s3eSurfaceRegister", (void *)sf_s3eSurfaceRegister },
    { "s3eSurfaceUnRegister", (void *)sf_s3eSurfaceUnRegister },
    { "s3eGLGetInt", (void *)sf_s3eGLGetInt },
    { "s3eGLGetNativeWindow", (void *)sf_s3eGLGetNativeWindow },
    { "s3eGLRegister", (void *)sf_s3eGLRegister },
    { "s3eGLUnRegister", (void *)sf_s3eGLUnRegister },
    { "s3eExtGetHash", (void *)sf_s3eExtGetHash },
    { NULL, NULL }
};
