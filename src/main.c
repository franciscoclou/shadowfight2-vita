/* Shadow Fight 2 -- PS Vita loader.
 *
 * Phase 1 placeholder: brings up logging, checks the game data is where the
 * loader will look for it, and exits. Phase 3 replaces the body with the
 * XE3U loader (src/loader/) and the s3e/EGL/GLES shims.
 */

#include "platform/log.h"

#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#define SF_DATA_DIR "ux0:data/shadowfight2"
#define SF_S3E_PATH SF_DATA_DIR "/ShadowFight2.s3e.raw"

/* Room for the game: app_android.icf asks for MemSize = DispAreaQ + 90 MB. */
int _newlib_heap_size_user = 256 * 1024 * 1024;

int main(void)
{
    SceIoStat st;

    sf_log_init();

    if (sceIoGetstat(SF_S3E_PATH, &st) < 0)
        LOGE("%s missing -- run ./scripts/prepare_game.sh and deploy option (C)",
             SF_S3E_PATH);
    else
        LOGI("found %s (%u bytes)", SF_S3E_PATH, (unsigned)st.st_size);

    LOGI("loader not implemented yet (Phase 3); exiting");
    sf_log_shutdown();
    sceKernelExitProcess(0);
    return 0;
}
