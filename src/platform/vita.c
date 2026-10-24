/* PS Vita implementation of platform.h. */
#include "platform/platform.h"
#include "platform/log.h"

#include <kubridge.h>
#include <psp2/audioout.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/openpsid.h>
#include <psp2/message_dialog.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <vitaGL.h>

#include <stdio.h>
#include <string.h>

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX 0x0C20D050
#endif

char sf_data_dir[256] = "ux0:data/shadowfight2";

static int s_gl_inited;

static SceUID alloc_at(const char *name, SceKernelMemBlockType type, uintptr_t addr, uint32_t size)
{
    SceKernelAllocMemBlockKernelOpt opt;
    memset(&opt, 0, sizeof(opt));
    opt.size = sizeof(opt);
    opt.attr = 0x1;                 /* has fixed virtual base */
    opt.field_C = (SceUInt32)addr;
    return kuKernelAllocMemBlock(name, type, size, &opt);
}

uintptr_t sf_plat_map_image(uintptr_t addr, uint32_t text_size, uint32_t data_size)
{
    SceUID t = alloc_at("sf_text", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, addr, text_size);
    SceUID d = alloc_at("sf_data", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, addr + text_size, data_size);
    if (t < 0 || d < 0) {
        LOGE("kuKernelAllocMemBlock failed: text=%#x data=%#x", t, d);
        return 0;
    }
    return addr;
}

void sf_plat_write_text(void *dst, const void *src, uint32_t len)
{
    kuKernelCpuUnrestrictedMemcpy(dst, src, len);
}

void sf_plat_flush(void *addr, uint32_t len)
{
    kuKernelFlushCaches(addr, len);
}

uint64_t sf_plat_time_us(void)
{
    return sceKernelGetProcessTimeWide();
}

void sf_plat_sleep_us(uint32_t us)
{
    sceKernelDelayThread(us);
}

/* The OpenPSID is unique per console and survives reinstalls, like the
 * Android ID the game gets on a phone. */
const char *sf_plat_device_id(void)
{
    static char id[33];
    SceKernelOpenPsId psid;
    int i;

    if (!id[0]) {
        memset(&psid, 0, sizeof(psid));
        sceKernelGetOpenPsId(&psid);
        for (i = 0; i < 16; i++)
            snprintf(id + i * 2, 3, "%02x", (unsigned char)psid.id[i]);
    }
    return id;
}

/* sceAudioOutOutput blocks until the previous block has played, so this
 * thread runs at exactly the output rate. */
#define AUDIO_GRAIN 1024

static sf_audio_fill s_audio_fill;
static int s_audio_rate;

static int audio_thread(SceSize args, void *argp)
{
    static int16_t buf[2][AUDIO_GRAIN * 2];
    int port, cur = 0;

    port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, AUDIO_GRAIN, s_audio_rate,
                               SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        LOGE("sceAudioOutOpenPort failed: %#x", port);
        return 0;
    }
    for (;;) {
        s_audio_fill(buf[cur], AUDIO_GRAIN);
        sceAudioOutOutput(port, buf[cur]);
        cur ^= 1;
    }
    return 0;
}

void sf_plat_audio_start(int rate, sf_audio_fill fill)
{
    SceUID th;
    s_audio_fill = fill;
    s_audio_rate = rate;
    th = sceKernelCreateThread("sf_audio", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0)
        LOGE("cannot start the audio thread: %#x", th);
}

void sf_plat_gl_init(int width, int height)
{
    /* Small legacy pool (the game is GLES2, not immediate mode); the RAM
     * threshold leaves the rest of main memory to the game's heap. */
    vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_ENABLE, SHARK_ENABLE);
    vglInitExtended(0x100000, width, height, 0x1000000, SCE_GXM_MULTISAMPLE_NONE);
    s_gl_inited = 1;
}

/* Show msg in a system dialog and wait for the user to dismiss it. The
 * dialog draws over a GXM display, so vitaGL comes up first if the game
 * has not started it yet. Only the main thread calls this, before the game
 * thread runs. */
void sf_vita_show_error(const char *msg)
{
    SceMsgDialogUserMessageParam user;
    SceMsgDialogParam param;

    if (!s_gl_inited) {
        vglInitExtended(0, 960, 544, 0x100000, SCE_GXM_MULTISAMPLE_NONE);
        s_gl_inited = 1;
    }
    memset(&user, 0, sizeof(user));
    user.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    user.msg = (const SceChar8 *)msg;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &user;
    if (sceMsgDialogInit(&param) < 0)
        return;
    while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_TRUE);
    }
    sceMsgDialogTerm();
}

void sf_plat_swap(void)
{
    vglSwapBuffers(GL_FALSE);
}

void *sf_plat_gl_proc(const char *name)
{
    return vglGetProcAddress(name);
}

void sf_plat_input(sf_input *out)
{
    static int inited;
    static const struct { uint32_t sce, sf; } map[] = {
        { SCE_CTRL_UP, SF_BTN_UP }, { SCE_CTRL_DOWN, SF_BTN_DOWN },
        { SCE_CTRL_LEFT, SF_BTN_LEFT }, { SCE_CTRL_RIGHT, SF_BTN_RIGHT },
        { SCE_CTRL_CROSS, SF_BTN_CROSS }, { SCE_CTRL_CIRCLE, SF_BTN_CIRCLE },
        { SCE_CTRL_SQUARE, SF_BTN_SQUARE }, { SCE_CTRL_TRIANGLE, SF_BTN_TRIANGLE },
        { SCE_CTRL_LTRIGGER, SF_BTN_L }, { SCE_CTRL_RTRIGGER, SF_BTN_R },
        { SCE_CTRL_START, SF_BTN_START }, { SCE_CTRL_SELECT, SF_BTN_SELECT },
    };
    SceCtrlData pad;
    SceTouchData touch;
    unsigned i;

    if (!inited) {
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
        inited = 1;
    }
    memset(out, 0, sizeof(*out));
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0)
        for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            if (pad.buttons & map[i].sce)
                out->buttons |= map[i].sf;

    /* The front panel reports 1920x1088; the surface is 960x544. */
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0) {
        for (i = 0; i < touch.reportNum && i < SF_MAX_TOUCH; i++) {
            out->touch[i].id = touch.report[i].id;
            out->touch[i].x = touch.report[i].x / 2;
            out->touch[i].y = touch.report[i].y / 2;
        }
        out->touch_count = (int)i;
    }
}
