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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SF_LOAD_ADDR  0x98000000
#define SF_GAME_STACK (4 * 1024 * 1024)

/* newlib heap backs s3eMallocBase. app_android.icf asks for
 * MemSize = DispAreaQ + 90 MB; vitaGL takes its pools from what is left.
 * This only fits in the extended memory budget that ATTRIBUTE2=12 in
 * param.sfo unlocks (CMakeLists.txt). On the default budget newlib's heap
 * block fails to allocate and every malloc returns NULL. */
#define SF_HEAP_MB 200
int _newlib_heap_size_user = SF_HEAP_MB * 1024 * 1024;

int _vshKernelSearchModuleByName(const char *name, int *unk);
void sf_vita_show_error(const char *msg);

/* Log the reason and show it on screen until the user presses a button.
 * Exiting silently looks exactly like a crash without a dump. */
static void fatal(const char *fmt, ...)
{
    char msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    LOGE("FATAL: %s", msg);
    sf_log_flush();
    sf_vita_show_error(msg);
    sceKernelExitProcess(0);
}

static void log_free_memory(const char *when)
{
    SceKernelFreeMemorySizeInfo info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    sceKernelGetFreeMemorySize(&info);
    LOGI("free memory %s: user %d KB, cdram %d KB, phycont %d KB", when,
         info.size_user / 1024, info.size_cdram / 1024, info.size_phycont / 1024);
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

/* kubridge exception handlers: log the faulting PC as a link-time address
 * too, so it can be looked up in Ghidra directly, then pass the exception
 * on to the previous handler. That is the system's, which writes the
 * psp2core dump and shows the crash error; exiting here would hide both. */
static KuKernelExceptionHandler s_prev_handler[3];

static void exception_handler(KuKernelExceptionContext *ctx)
{
    uint32_t pc = ctx->pc, lr = ctx->lr;
    uint32_t type = ctx->exceptionType;
    int in_game = pc >= sf_game.base && pc < sf_game.base + sf_game.text_size;
    LOGE("EXCEPTION type=%u pc=%#08x%s lr=%#08x sp=%#08x far=%#08x",
         (unsigned)type, (unsigned)pc, in_game ? " (game)" : "",
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
    if (type < 3 && s_prev_handler[type])
        s_prev_handler[type](ctx);
    /* No handler to chain to: returning would retry the faulting
     * instruction forever. */
    sceKernelExitProcess(0);
}

static void install_exception_handlers(void)
{
    KuKernelExceptionHandlerOpt opt;
    uint32_t type;

    memset(&opt, 0, sizeof(opt));
    opt.size = sizeof(opt);
    for (type = KU_KERNEL_EXCEPTION_TYPE_DATA_ABORT;
         type <= KU_KERNEL_EXCEPTION_TYPE_UNDEFINED_INSTRUCTION; type++)
        if (kuKernelRegisterExceptionHandler(type, exception_handler,
                                             &s_prev_handler[type], &opt) < 0)
            LOGW("kubridge has no exception handlers (type %u); "
                 "crashes will only show up as psp2core dumps", (unsigned)type);
}

int main(void)
{
    SceAppUtilInitParam init;
    SceAppUtilBootParam boot;
    SceUID thid;
    char s3e_path[300];

    sf_log_init();
    log_free_memory("at boot");

    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    memset(&init, 0, sizeof(init));
    memset(&boot, 0, sizeof(boot));
    sceAppUtilInit(&init, &boot);

    /* Probe the heap before anything else needs it: the loader's first
     * allocation is the 9.7 MB image. */
    {
        void *probe = malloc(16 * 1024 * 1024);
        if (!probe)
            fatal("Could not reserve the %d MB game heap.\n\n"
                  "Reinstall the full VPK: its param.sfo unlocks the extended memory budget.",
                  SF_HEAP_MB);
        free(probe);
    }

    if (!file_exists("ur0:data/libshacccg.suprx") && !file_exists("ur0:data/external/libshacccg.suprx"))
        fatal("ur0:data/libshacccg.suprx is missing.\n\n"
              "It is the shader compiler vitaGL needs. Extract it with ShaRKBR33D.");
    snprintf(s3e_path, sizeof(s3e_path), "%s/ShadowFight2.s3e.raw", sf_data_dir);
    if (!file_exists(s3e_path))
        fatal("%s is missing.\n\n"
              "Copy the game data (game_out/) to %s/.", s3e_path, sf_data_dir);

    /* kubridge is linked weakly, so a missing plugin shows up here as a
     * readable message instead of an import failure at boot. */
    {
        int search_unk[2];
        if (_vshKernelSearchModuleByName("kubridge", search_unk) < 0)
            fatal("kubridge.skprx is not loaded.\n\n"
                  "Add it under *KERNEL in ux0:tai/config.txt and reboot.");
    }

    install_exception_handlers();

    {
        char ram[300];
        snprintf(ram, sizeof(ram), "%s/ram", sf_data_dir);
        sceIoMkdir(ram, 0777);
    }
    sf_device_init(NULL);

    if (sf_s3e_load(&sf_game, s3e_path, SF_LOAD_ADDR,
                    sf_import_resolve, sf_import_stub) < 0)
        fatal("Could not load %s.\n\nSee %s/debug.log for the reason.", s3e_path, sf_data_dir);
    LOGI("%d imports left unresolved", sf_game.unresolved);
    log_free_memory("after mapping the game");
    sf_device_init(sf_game.config);

    thid = sceKernelCreateThread("sf_game", game_thread, 0x40, SF_GAME_STACK, 0, 0, NULL);
    if (thid < 0)
        fatal("Cannot create the game thread (%#x).", (unsigned)thid);
    sceKernelStartThread(thid, 0, NULL);
    sceKernelWaitThreadEnd(thid, NULL, NULL);
    return 0;
}
