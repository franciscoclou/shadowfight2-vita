/* PS Vita implementation of platform.h. */
#include "platform/platform.h"
#include "platform/log.h"

#include <kubridge.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>
#include <vitaGL.h>

#include <string.h>

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX 0x0C20D050
#endif

char sf_data_dir[256] = "ux0:data/shadowfight2";

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

void sf_plat_gl_init(int width, int height)
{
    /* Small legacy pool (the game is GLES2, not immediate mode); the RAM
     * threshold leaves the rest of main memory to the game's heap. */
    vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_ENABLE, SHARK_ENABLE);
    vglInitExtended(0x100000, width, height, 0x1000000, SCE_GXM_MULTISAMPLE_NONE);
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
