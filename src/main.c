/* Shadow Fight 2 -- PS Vita loader.
 *
 * Maps the game's Marmalade XE3U image (ShadowFight2.s3e.raw, made by
 * scripts/prepare_game.sh) into memory with kubridge, points its import
 * slots at our s3e/EGL/GLES shims (src/s3e/), and runs its entry point on
 * a thread with a big stack. There is no libs3e_android.so involved: we
 * are the Marmalade runtime.
 */

#include "loader/s3e_loader.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "s3e/s3e_internal.h"

#include <kubridge.h>
#include <psp2/apputil.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>

#include <stdio.h>
#include <string.h>

#define SF_LOAD_ADDR  0x98000000
#define SF_GAME_STACK (4 * 1024 * 1024)

/* newlib heap backs s3eMallocBase. app_android.icf asks for
 * MemSize = DispAreaQ + 90 MB; vitaGL takes its pools from what is left. */
int _newlib_heap_size_user = 200 * 1024 * 1024;

int _vshKernelSearchModuleByName(const char *name, int *unk);

static void fatal(const char *msg)
{
    LOGE("FATAL: %s", msg);
    sf_log_flush();
    sceKernelDelayThread(2 * 1000 * 1000);
    sceKernelExitProcess(0);
}

static int file_exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0;
}

static int game_thread(SceSize args, void *argp)
{
    LOGI("game thread: entering s3e entry %p", (void *)sf_game.entry);
    sf_log_flush();
    sf_game.entry();
    LOGI("s3e entry returned");
    sf_log_flush();
    sceKernelExitProcess(0);
    return 0;
}

/* kubridge's abort handler: log the faulting PC as a link-time address
 * too, so it can be looked up in Ghidra directly. */
static void abort_handler(KuKernelAbortContext *ctx)
{
    uint32_t pc = ctx->pc, lr = ctx->lr;
    int in_game = pc >= sf_game.base && pc < sf_game.base + sf_game.text_size;
    LOGE("ABORT type=%u pc=%#08x%s lr=%#08x sp=%#08x far=%#08x",
         (unsigned)ctx->abortType, (unsigned)pc, in_game ? " (game)" : "",
         (unsigned)lr, (unsigned)ctx->sp, (unsigned)ctx->FAR);
    if (in_game)
        LOGE("  game link addr pc=%#08x lr=%#08x",
             (unsigned)sf_s3e_to_link(&sf_game, pc),
             (unsigned)sf_s3e_to_link(&sf_game, lr));
    LOGE("  r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x",
         (unsigned)ctx->r0, (unsigned)ctx->r1, (unsigned)ctx->r2, (unsigned)ctx->r3,
         (unsigned)ctx->r4, (unsigned)ctx->r5, (unsigned)ctx->r6, (unsigned)ctx->r7);
    {
        const uint32_t *sp = (const uint32_t *)ctx->sp;
        int i;
        for (i = 0; i < 16; i += 4)
            LOGE("  sp+%02x: %08x %08x %08x %08x", i * 4, (unsigned)sp[i],
                 (unsigned)sp[i + 1], (unsigned)sp[i + 2], (unsigned)sp[i + 3]);
    }
    sf_log_flush();
    /* Returning would retry the faulting instruction forever. */
    sceKernelExitProcess(0);
}

int main(void)
{
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    SceUID thid;
    KuKernelAbortHandlerOpt abort_opt;
    char s3e_path[300];

    sf_log_init();

    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    sceAppUtilInit(&init, &boot);

    if (!file_exists("ur0:data/libshacccg.suprx") && !file_exists("ur0:data/external/libshacccg.suprx"))
        fatal("ur0:data/libshacccg.suprx is missing (vitaGL shader compiler)");
    snprintf(s3e_path, sizeof(s3e_path), "%s/ShadowFight2.s3e.raw", sf_data_dir);
    if (!file_exists(s3e_path))
        fatal("ShadowFight2.s3e.raw missing -- run ./scripts/prepare_game.sh and deploy option (C)");

    /* kubridge is linked weakly, so a missing plugin shows up here as a
     * readable message instead of an import failure at boot. */
    {
        int search_unk[2];
        if (_vshKernelSearchModuleByName("kubridge", search_unk) < 0)
            fatal("kubridge.skprx is not loaded (add it under *KERNEL in ux0:tai/config.txt)");
    }

    memset(&abort_opt, 0, sizeof(abort_opt));
    abort_opt.size = sizeof(abort_opt);
    kuKernelRegisterAbortHandler(abort_handler, NULL, &abort_opt);

    {
        char ram[300];
        snprintf(ram, sizeof(ram), "%s/ram", sf_data_dir);
        sceIoMkdir(ram, 0777);
    }
    sf_device_init(NULL);

    if (sf_s3e_load(&sf_game, s3e_path, SF_LOAD_ADDR,
                    sf_import_resolve, sf_import_stub) < 0)
        fatal("could not load the s3e image");
    LOGI("%d imports left unresolved", sf_game.unresolved);
    sf_device_init(sf_game.config);

    thid = sceKernelCreateThread("sf_game", game_thread, 0x40, SF_GAME_STACK, 0, 0, NULL);
    if (thid < 0)
        fatal("cannot create game thread");
    sceKernelStartThread(thid, 0, NULL);
    sceKernelWaitThreadEnd(thid, NULL, NULL);
    return 0;
}
