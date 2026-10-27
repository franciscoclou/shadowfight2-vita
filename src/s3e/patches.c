/* Optional patches to the game's code, applied after loading and before the
 * entry point runs. Each patch checks that the word it replaces is still
 * the expected original instruction, so a different game build is left
 * alone instead of being corrupted.
 *
 * cheats (controls.txt "cheats = 1"): the AssemblySettings reader (link
 * 0x4a67ae5c) clears the debug flag (accessor 0x4a1c805c) on every boot,
 * the release build's way of hiding developer tools. Storing r11 (which
 * holds 1 there) instead of r8 (0) leaves debug mode on; the shop then
 * builds its "Hanger" button (0x4a6568d0) to the developer panel with
 * Money +1m, Bonus +1m (gems), Level +1 and Currency +1m.
 */
#include "s3e/s3e_internal.h"
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

static const sf_patch s_cheats[] = {
    { "debug flag on", 0x4a67aeac, 0xe5c08000 /* strb r8, [r0] */, 0xe5c0b000 /* strb fp, [r0] */ },
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
    apply(mod, s_always, sizeof(s_always) / sizeof(s_always[0]));
    if (sf_option_cheats())
        apply(mod, s_cheats, sizeof(s_cheats) / sizeof(s_cheats[0]));
}
