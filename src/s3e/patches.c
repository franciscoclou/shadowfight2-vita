/* Patches to the game's code, applied after loading and before the entry
 * point runs. Each patch checks that the word it replaces is still the
 * expected original instruction, so a different game build is left alone
 * instead of being corrupted.
 */
#include "s3e/game.h"
#include "loader/s3e_loader.h"
#include "platform/platform.h"

typedef struct {
    const char *name;
    uint32_t link;       /* link-time address of the instruction */
    uint32_t orig, patched;
} sf_patch;

/* Always applied: fixes for running offline on the Vita.
 *
 * pack check: the boot pack attacher (0x4a69ece0) MD5s every attached pack
 * through 1 KB freads (0x4a1fa6fc) to compare with its Hash attribute,
 * ~320 MB on every launch. Calling the string copy (0x4a004084, same
 * (out, in) shape) instead compares the pack's path, which packs.c writes
 * as the Hash. */
static const sf_patch s_always[] = {
    { "pack check", 0x4a69f028, 0xebed6db3 /* bl 0x4a1fa6fc */, 0xebe59415 /* bl 0x4a004084 */ },
};

/* remove_online (controls.txt, the default): quests.c edits the quest
 * scripts, which are RSA-signed (.sha). FUN_4a6a3af4(path, data, len)
 * verifies a signature for the protected loader (0x4a6a4134), which
 * refuses the file when it returns 0; it now returns 1. The .hash check
 * after it stays, and quests.c answers it. */
static const sf_patch s_remove_online[] = {
    { "quest signatures", 0x4a6a3af4, 0xe92d4ff0 /* push {r4-r11, lr} */, 0xe3a00001 /* mov r0, #1 */ },
    { "quest signatures", 0x4a6a3af8, 0xe1a04000 /* mov r4, r0 */, 0xe12fff1e /* bx lr */ },
    /* The game already has a build without the online parts of these
     * screens: Market/Amazon (internalSettings.xml, byte +3 of the Market
     * settings, getter 0x4a67b4b0). Flipping that flag would also switch
     * billing and logins, so only these screens' own checks answer "yes":
     *   settings: music, sound and credits only, as on Amazon (no Support,
     *     Google Play, Facebook or Game Center; the Facebook row in Advanced
     *     goes too). The two checks that pick the bottom buttons
     *     (0x4a3e2618, 0x4a3dfdac: mask 3 = Advanced and Back, Amazon 2 =
     *     Back only) are left alone, so Advanced stays.
     *   top bar: no "+" (to the real-money tab), and the bar sized without it.
     *   shop: no "Free" tab (videos, offer walls), its page and its badge. */
    { "settings as Amazon", 0x4a3dba14, 0xeb0a7ea5 /* bl 0x4a67b4b0 */, 0xe3a00001 /* mov r0, #1 */ },
    { "settings as Amazon", 0x4a3dd3b4, 0xeb0a783d, 0xe3a00001 },
    { "settings as Amazon", 0x4a3df3fc, 0xeb0a702b, 0xe3a00001 },
    { "settings as Amazon", 0x4a3e1dcc, 0xeb0a65b7, 0xe3a00001 },
    { "settings as Amazon", 0x4a3e1ff4, 0xeb0a652d, 0xe3a00001 },
    { "settings as Amazon", 0x4a3e230c, 0xeb0a6467, 0xe3a00001 },
    { "settings as Amazon", 0x4a3e2384, 0xeb0a6449, 0xe3a00001 },
    { "top bar without +", 0x4a5042d0, 0xeb05dc76, 0xe3a00001 },
    { "top bar without +", 0x4a5044a0, 0xeb05dc02, 0xe3a00001 },
    { "shop without Free", 0x4a653838, 0xeb009f1c, 0xe3a00001 },
    { "shop without Free", 0x4a654bf0, 0xeb009a2e, 0xe3a00001 },
    { "shop without Free", 0x4a65d728, 0xeb007760, 0xe3a00001 },
    /* The map's raid toggle (and its arrow, 0x4a4c98fc) shows when the
     * profile's RaidToggleIsVisible (+0x28c, getter 0x4a285ac4) is set,
     * which the raid intro does. Raids need Nekki's servers. */
    { "no raid toggle", 0x4a285ac4, 0xe5d0028c /* ldrb r0, [r0, #0x28c] */, 0xe3a00000 /* mov r0, #0 */ },
};

/* cheats (controls.txt "cheats = 1", the default).
 *
 * shop cheats: the shop's constructor (0x4a65fc28) copies the debug flag
 * (accessor 0x4a1c805c) into its own byte +0x134, and only builds its
 * "Hanger" button to the cheats panel (0x4a656444) when that is set. The
 * flag itself stays off: it is what draws the fps and timing text and the
 * other screens' debug buttons. cheats.c restyles the panel.
 *
 * unlimited energy: the user's flag +0x2a8, set when the Unlimited_Energy
 * item is bought (0x4a285fa4), read through 0x4a285f9c. */
static const sf_patch s_cheats[] = {
    { "shop cheats", 0x4a65fd58, 0xe5d02000 /* ldrb r2, [r0] */, 0xe3a02001 /* mov r2, #1 */ },
    { "unlimited energy", 0x4a285f9c, 0xe5d002a8 /* ldrb r0, [r0, #0x2a8] */, 0xe3a00001 /* mov r0, #1 */ },
};

static void apply(const sf_s3e_module *mod, const sf_patch *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        uint32_t *at = (uint32_t *)(mod->base + (p[i].link - mod->hdr.base));
        if (*at != p[i].orig) {
            LOGW("patch %s: %#x holds %#x, not %#x; skipped", p[i].name,
                 (unsigned)p[i].link, (unsigned)*at, (unsigned)p[i].orig);
            continue;
        }
        sf_plat_write_text(at, &p[i].patched, sizeof(uint32_t));
        sf_plat_flush(at, sizeof(uint32_t));
        LOGI("patch %s applied at %#x", p[i].name, (unsigned)p[i].link);
    }
}

void sf_apply_patches(const sf_s3e_module *mod)
{
    sf_game_init(mod);
    apply(mod, s_always, sizeof(s_always) / sizeof(s_always[0]));
    sf_ui_install(mod);
    sf_packs_install_unlocks();
    if (sf_option_remove_online())
        apply(mod, s_remove_online, sizeof(s_remove_online) / sizeof(s_remove_online[0]));
    if (sf_option_cheats()) {
        apply(mod, s_cheats, sizeof(s_cheats) / sizeof(s_cheats[0]));
        sf_cheats_install(mod);
    }
}
