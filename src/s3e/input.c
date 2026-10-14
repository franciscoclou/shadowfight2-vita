/* s3ePointer, s3eKeyboard, s3eAccelerometer.
 *
 * The game only imports the callback-registration side of s3ePointer, so
 * touches are delivered as events: each s3eDeviceYield samples the
 * platform and calls the registered callbacks for every change.
 *
 * s3ePointerCallback: 0 BUTTON_EVENT, 1 MOTION_EVENT, 2 TOUCH_EVENT,
 * 3 TOUCH_MOTION_EVENT. Event structs as in s3ePointer.h.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

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

/* Vita buttons -> s3eKey codes (s3eKeyboard.h): arrows 9-12, Enter 4,
 * Esc 1, Space 8, Back 210 (Android back), Menu 211. */
static const struct { uint32_t btn; int key; } s_keymap[] = {
    { SF_BTN_LEFT, 9 }, { SF_BTN_UP, 10 }, { SF_BTN_RIGHT, 11 }, { SF_BTN_DOWN, 12 },
    { SF_BTN_CROSS, 4 }, { SF_BTN_CIRCLE, 210 }, { SF_BTN_START, 211 }, { SF_BTN_SQUARE, 8 },
};

void sf_input_poll(void)
{
    sf_input in;
    int i;

    memset(&in, 0, sizeof(in));
    sf_plat_input(&in);

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
