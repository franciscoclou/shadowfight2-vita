/* s3ePointer, s3eKeyboard, s3eAccelerometer.
 *
 * The game only imports the callback-registration side of s3ePointer, so
 * touches are delivered as events: each s3eDeviceYield samples the
 * platform and calls the registered callbacks for every change.
 *
 * s3ePointerCallback: 0 BUTTON_EVENT, 1 MOTION_EVENT, 2 TOUCH_EVENT,
 * 3 TOUCH_MOTION_EVENT. Event structs as in s3ePointer.h.
 *
 * The game's fight controls are on-screen only, so physical controls are
 * turned into virtual touches (ids from VPAD_ID) on those controls: the
 * left stick / d-pad drags the joystick, face buttons hold the attack
 * buttons. Positions are read from <data>/controls.txt, which is written
 * with the defaults on first run so they can be tuned without a rebuild.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

typedef struct { int32_t button, pressed, x, y; } s3ePointerEvent;
typedef struct { int32_t x, y; } s3ePointerMotionEvent;
typedef struct { uint32_t id, pressed; int32_t x, y; } s3ePointerTouchEvent;
typedef struct { uint32_t id; int32_t x, y; } s3ePointerTouchMotionEvent;
typedef struct { int32_t key, pressed; } s3eKeyboardEvent;

static struct { s3eCallback fn; void *ud; } s_ptr_cb[4], s_key_cb[2];
static sf_input s_prev;
static uint8_t s_keys[256];

static void fire(s3eCallback fn, void *ud, void *ev)
{
    if (fn)
        fn(ev, ud);
}

static int find_touch(const sf_input *in, int id)
{
    int i;
    for (i = 0; i < in->touch_count; i++)
        if (in->touch[i].id == id)
            return i;
    return -1;
}

/* Vita buttons -> s3eKey codes (s3eKeyboard.h): L is Back 210 (Android
 * back, closes dialogs). The others drive virtual touches instead. */
static const struct { uint32_t btn; int key; } s_keymap[] = {
    { SF_BTN_L, 210 },
};

/* --- virtual touches ------------------------------------------------------ */

#define VPAD_ID 200
#define CONTROLS_VERSION 4
#define CONTROLS_COMPATIBLE 2   /* older files had punch and kick swapped */

/* punch, kick and the joystick are measured from the tutorial dojo's HUD
 * on the harness; ranged, magic and pause are estimates (those controls
 * only appear later). */
enum { V_PUNCH, V_KICK, V_RANGED, V_MAGIC, V_PAUSE, V_NBUTTONS };
static struct { const char *name; uint32_t btn; int x, y; } s_vbtn[V_NBUTTONS] = {
    { "punch",  SF_BTN_SQUARE,   883, 385 },
    { "kick",   SF_BTN_CROSS,    800, 467 },
    { "ranged", SF_BTN_TRIANGLE, 905, 280 },
    { "magic",  SF_BTN_CIRCLE,   790, 300 },
    { "pause",  SF_BTN_START,    480,  40 },
};
static int s_joy_x = 137, s_joy_y = 405, s_joy_r = 65, s_deadzone = 40;
static int s_touch_controls;   /* draw the on-screen fight controls */
static int s_cheats = 1;       /* cheats panel and unlimited energy (patches.c) */

static void ensure_controls(void);

int sf_touch_controls_shown(void)
{
    ensure_controls();
    return s_touch_controls;
}

int sf_option_cheats(void)
{
    ensure_controls();
    return s_cheats;
}

static void controls_path(char *path, size_t n)
{
    snprintf(path, n, "%s/controls.txt", sf_data_dir);
}

static void save_controls(void)
{
    char path[300];
    FILE *f;
    int i;

    controls_path(path, sizeof(path));
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "# Shadow Fight 2 controls: screen points (960x544) the Vita\n"
               "# buttons press. Delete this file to restore the defaults.\n"
               "version = %d\n"
               "# 1 shows the on-screen fight controls, 0 hides them (the magic\n"
               "# and ranged meters stay). SELECT toggles it at any time.\n"
               "touch_controls = %d\n"
               "# 1 turns the cheats on at the next launch: unlimited energy, and a\n"
               "# button in the shop to a panel with coins, gems and a progress reset.\n"
               "cheats = %d\n"
               "# joystick = centre x, centre y, drag radius, stick deadzone (0-127)\n"
               "joystick = %d %d %d %d\n",
            CONTROLS_VERSION, s_touch_controls, s_cheats, s_joy_x, s_joy_y, s_joy_r, s_deadzone);
    for (i = 0; i < V_NBUTTONS; i++)
        fprintf(f, "%s = %d %d\n", s_vbtn[i].name, s_vbtn[i].x, s_vbtn[i].y);
    fclose(f);
}

/* Reads controls.txt. A missing file, or one with the version 1 layout, is
 * replaced with the defaults; a compatible older one keeps its values and
 * is rewritten with the options added since. */
static void load_controls(void)
{
    char path[300], line[128], name[32];
    FILE *f;
    int i, x, y, version = 0;

    controls_path(path, sizeof(path));
    if ((f = fopen(path, "r"))) {
        while (fgets(line, sizeof(line), f))
            if (sscanf(line, " version = %d", &x) == 1)
                version = x;
        rewind(f);
    }
    if (!f || version < CONTROLS_COMPATIBLE) {
        if (f)
            fclose(f);
        save_controls();
        LOGI("controls: wrote defaults to %s", path);
        return;
    }
    while (fgets(line, sizeof(line), f)) {
        int r, dz;
        if (line[0] == '#')
            continue;
        if (sscanf(line, " touch_controls = %d", &x) == 1) {
            s_touch_controls = x != 0;
            continue;
        }
        /* Before version 4 cheats were off unless asked for. */
        if (sscanf(line, " cheats = %d", &x) == 1) {
            if (version >= 4)
                s_cheats = x != 0;
            continue;
        }
        if (sscanf(line, " joystick = %d %d %d %d", &x, &y, &r, &dz) == 4) {
            s_joy_x = x; s_joy_y = y; s_joy_r = r; s_deadzone = dz;
            continue;
        }
        if (sscanf(line, " %31[a-z_] = %d %d", name, &x, &y) == 3)
            for (i = 0; i < V_NBUTTONS; i++)
                if (!strcmp(name, s_vbtn[i].name)) {
                    s_vbtn[i].x = x;
                    s_vbtn[i].y = y;
                }
    }
    fclose(f);
    if (version < CONTROLS_VERSION)
        save_controls();
    LOGI("controls: loaded %s (touch controls %s%s)", path, s_touch_controls ? "shown" : "hidden",
         s_cheats ? ", cheats" : "");
}

/* Loaded on first use: s3eFileOpen asks whether to hide the controls'
 * textures before the first input poll. */
static void ensure_controls(void)
{
    static int loaded;
    if (!loaded) {
        loaded = 1;
        load_controls();
    }
}

static void add_touch(sf_input *in, int id, int x, int y)
{
    if (in->touch_count < SF_MAX_TOUCH) {
        in->touch[in->touch_count].id = id;
        in->touch[in->touch_count].x = x;
        in->touch[in->touch_count].y = y;
        in->touch_count++;
    }
}

/* The joystick touch goes down at the centre and is then dragged, so the
 * game sees a press followed by motion, as from a finger. */
static void add_virtual_touches(sf_input *in)
{
    static int joy_down;
    int dx = in->lx, dy = in->ly, i;

    ensure_controls();
    /* Only in a fight: elsewhere the same screen points are menu buttons. */
    if (!sf_hud_in_fight()) {
        joy_down = 0;
        return;
    }
    if (dx * dx + dy * dy < s_deadzone * s_deadzone)
        dx = dy = 0;
    if (in->buttons & SF_BTN_LEFT)  dx = -127;
    if (in->buttons & SF_BTN_RIGHT) dx = 127;
    if (in->buttons & SF_BTN_UP)    dy = -127;
    if (in->buttons & SF_BTN_DOWN)  dy = 127;
    if (dx || dy) {
        if (joy_down)
            add_touch(in, VPAD_ID, s_joy_x + dx * s_joy_r / 127, s_joy_y + dy * s_joy_r / 127);
        else
            add_touch(in, VPAD_ID, s_joy_x, s_joy_y);
        joy_down = 1;
    } else {
        joy_down = 0;
    }
    for (i = 0; i < V_NBUTTONS; i++)
        if (in->buttons & s_vbtn[i].btn)
            add_touch(in, VPAD_ID + 1 + i, s_vbtn[i].x, s_vbtn[i].y);
}

void sf_input_poll(void)
{
    sf_input in;
    int i;

    memset(&in, 0, sizeof(in));
    sf_plat_input(&in);
    add_virtual_touches(&in);
    if ((in.buttons & SF_BTN_SELECT) && !(s_prev.buttons & SF_BTN_SELECT) && sf_hud_in_fight()) {
        s_touch_controls = !s_touch_controls;
        save_controls();
        sf_hud_apply(s_touch_controls);
        LOGI("controls: touch controls %s", s_touch_controls ? "shown" : "hidden");
    }

    /* touch releases */
    for (i = 0; i < s_prev.touch_count; i++) {
        if (find_touch(&in, s_prev.touch[i].id) < 0) {
            s3ePointerTouchEvent ev = { (uint32_t)s_prev.touch[i].id, 0,
                                        s_prev.touch[i].x, s_prev.touch[i].y };
            s3ePointerEvent bev = { 1, 0, ev.x, ev.y };
            fire(s_ptr_cb[2].fn, s_ptr_cb[2].ud, &ev);
            if (s_prev.touch[i].id == 0)
                fire(s_ptr_cb[0].fn, s_ptr_cb[0].ud, &bev);
        }
    }
    /* presses and moves */
    for (i = 0; i < in.touch_count; i++) {
        int j = find_touch(&s_prev, in.touch[i].id);
        if (j < 0) {
            s3ePointerTouchEvent ev = { (uint32_t)in.touch[i].id, 1, in.touch[i].x, in.touch[i].y };
            s3ePointerEvent bev = { 1, 1, ev.x, ev.y };
            fire(s_ptr_cb[2].fn, s_ptr_cb[2].ud, &ev);
            if (in.touch[i].id == 0)
                fire(s_ptr_cb[0].fn, s_ptr_cb[0].ud, &bev);
        } else if (s_prev.touch[j].x != in.touch[i].x || s_prev.touch[j].y != in.touch[i].y) {
            s3ePointerTouchMotionEvent ev = { (uint32_t)in.touch[i].id, in.touch[i].x, in.touch[i].y };
            s3ePointerMotionEvent mev = { ev.x, ev.y };
            fire(s_ptr_cb[3].fn, s_ptr_cb[3].ud, &ev);
            if (in.touch[i].id == 0)
                fire(s_ptr_cb[1].fn, s_ptr_cb[1].ud, &mev);
        }
    }

    for (i = 0; i < (int)(sizeof(s_keymap) / sizeof(s_keymap[0])); i++) {
        int now = (in.buttons & s_keymap[i].btn) != 0;
        int was = (s_prev.buttons & s_keymap[i].btn) != 0;
        int key = s_keymap[i].key;
        if (now != was) {
            s3eKeyboardEvent ev = { key, now };
            s_keys[key] = now ? 3 : 4;   /* S3E_KEY_STATE_PRESSED|DOWN / RELEASED */
            fire(s_key_cb[0].fn, s_key_cb[0].ud, &ev);
        } else {
            s_keys[key] = now ? 1 : 0;   /* S3E_KEY_STATE_DOWN / UP */
        }
    }
    s_prev = in;
}

static int32_t sf_s3ePointerRegister(uint32_t id, s3eCallback fn, void *ud)
{
    LOGI("s3ePointerRegister(%u)", (unsigned)id);
    if (id >= 4)
        return S3E_RESULT_ERROR;
    s_ptr_cb[id].fn = fn;
    s_ptr_cb[id].ud = ud;
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3ePointerUnRegister(uint32_t id, s3eCallback fn)
{
    if (id < 4 && s_ptr_cb[id].fn == fn)
        s_ptr_cb[id].fn = NULL;
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3ePointerUpdate(void) { return S3E_RESULT_SUCCESS; }

/* s3ePointerProperty (libs3e_android.so): 0 AVAILABLE = 1, 1 = 0,
 * 2/3 TYPE/STYLUS_TYPE = 2 (touch), 4 MULTI_TOUCH_AVAILABLE. */
static int32_t sf_s3ePointerGetInt(uint32_t prop)
{
    switch (prop) {
    case 0: return 1;
    case 1: return 0;
    case 2: case 3: return 2;
    case 4: return 1;
    default:
        LOGI("s3ePointerGetInt(%u) -> 0", (unsigned)prop);
        return 0;
    }
}

static int32_t sf_s3ePointerSetInt(uint32_t prop, int32_t v) { return S3E_RESULT_SUCCESS; }

static int32_t sf_s3eKeyboardRegister(uint32_t id, s3eCallback fn, void *ud)
{
    if (id >= 2)
        return S3E_RESULT_ERROR;
    s_key_cb[id].fn = fn;
    s_key_cb[id].ud = ud;
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eKeyboardUnRegister(uint32_t id, s3eCallback fn)
{
    if (id < 2 && s_key_cb[id].fn == fn)
        s_key_cb[id].fn = NULL;
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eKeyboardUpdate(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eKeyboardGetState(int32_t key) { return key >= 0 && key < 256 ? s_keys[key] : 0; }
static int32_t sf_s3eKeyboardSetInt(uint32_t prop, int32_t v) { return S3E_RESULT_SUCCESS; }

/* No motion sensor on a PSTV and tilt makes no sense for a fighting game:
 * report the accelerometer as unavailable. */
static int32_t sf_s3eAccelerometerStart(void) { return S3E_RESULT_ERROR; }
static int32_t sf_s3eAccelerometerStop(void) { return S3E_RESULT_SUCCESS; }
static int32_t sf_s3eAccelerometerGetX(void) { return 0; }
static int32_t sf_s3eAccelerometerGetY(void) { return 0; }
static int32_t sf_s3eAccelerometerGetZ(void) { return -1000; }

const sf_export sf_exports_input[] = {
    { "s3ePointerRegister", (void *)sf_s3ePointerRegister },
    { "s3ePointerUnRegister", (void *)sf_s3ePointerUnRegister },
    { "s3ePointerUpdate", (void *)sf_s3ePointerUpdate },
    { "s3ePointerGetInt", (void *)sf_s3ePointerGetInt },
    { "s3ePointerSetInt", (void *)sf_s3ePointerSetInt },
    { "s3eKeyboardRegister", (void *)sf_s3eKeyboardRegister },
    { "s3eKeyboardUnRegister", (void *)sf_s3eKeyboardUnRegister },
    { "s3eKeyboardUpdate", (void *)sf_s3eKeyboardUpdate },
    { "s3eKeyboardGetState", (void *)sf_s3eKeyboardGetState },
    { "s3eKeyboardSetInt", (void *)sf_s3eKeyboardSetInt },
    { "s3eAccelerometerStart", (void *)sf_s3eAccelerometerStart },
    { "s3eAccelerometerStop", (void *)sf_s3eAccelerometerStop },
    { "s3eAccelerometerGetX", (void *)sf_s3eAccelerometerGetX },
    { "s3eAccelerometerGetY", (void *)sf_s3eAccelerometerGetY },
    { "s3eAccelerometerGetZ", (void *)sf_s3eAccelerometerGetZ },
    { NULL, NULL }
};
