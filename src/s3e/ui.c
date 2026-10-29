/* The port's own UI in the game: the CONTROLS help, reached from the pause
 * dialog and from Settings, and the controller scale fixed at Large.
 *
 * Controller scale: Settings_Controller_Scale toggles the profile's
 * ControllerScale (0x4a67c540); the HUD reads it through 0x4a67c364 and
 * scales the fight controls 1.32x when it is Large. The Vita's buttons
 * press fixed screen points measured at Large (input.c), so the getter
 * always answers Large and the Settings row becomes CONTROLS: its label
 * (0x4a3d97ac) and its click (0x4a67c540) are replaced.
 *
 * Pause: PauseFightDialog (vtable 0x4a8c4330) builds home, music, sound
 * and play in slot 0x26c (0x4a390c60) as image buttons; ours adds a
 * CONTROLS icon made the same way (0x4a161010) from embedded images,
 * which opens the help over the pause dialog.
 *
 * The help is the game's SimpleDialog with controls_help.png on its
 * message label; blank lines in the message give the dialog its height.
 * Its title label (dlg+0x178, brown) is hidden under controls_title.png,
 * gold like the game's own titles.
 *
 * Images (src/assets, scripts/make_ui_art.py) are linked into the eboot
 * (ui_assets.S) and opened as memory files under textures/sfport/.
 */
#include "s3e/game.h"
#include "loader/s3e_loader.h"
#include "platform/platform.h"

#include <stdlib.h>
#include <string.h>

/* --- embedded images ------------------------------------------------------ */

extern const uint8_t sf_asset_controls_help[], sf_asset_controls_help_end[];
extern const uint8_t sf_asset_controls_title[], sf_asset_controls_title_end[];
extern const uint8_t sf_asset_pause_controls[], sf_asset_pause_controls_end[];
extern const uint8_t sf_asset_pause_selected[], sf_asset_pause_selected_end[];
extern const uint8_t sf_asset_watermark[], sf_asset_watermark_end[];

#define ASSET_DIR "textures/sfport/"

static const struct { const char *name; const uint8_t *begin, *end; } s_assets[] = {
    { "controls_help.png", sf_asset_controls_help, sf_asset_controls_help_end },
    { "controls_title.png", sf_asset_controls_title, sf_asset_controls_title_end },
    { "pause_controls.png", sf_asset_pause_controls, sf_asset_pause_controls_end },
    { "pause_selected.png", sf_asset_pause_selected, sf_asset_pause_selected_end },
    { "watermark.png", sf_asset_watermark, sf_asset_watermark_end },
};

/* Any path ending in textures/sfport/<name>, whatever drive and asset set
 * the game puts in front. */
const uint8_t *sf_ui_asset(const char *path, uint32_t *size)
{
    const char *p = path ? strstr(path, ASSET_DIR) : NULL;
    size_t i;

    if (!p)
        return NULL;
    p += strlen(ASSET_DIR);
    for (i = 0; i < sizeof(s_assets) / sizeof(s_assets[0]); i++)
        if (!strcmp(p, s_assets[i].name)) {
            *size = (uint32_t)(s_assets[i].end - s_assets[i].begin);
            return s_assets[i].begin;
        }
    return NULL;
}

/* --- the help dialog --------------------------------------------------------- */

#define HELP_LINES  9        /* blank message lines: the dialog's height */
#define HELP_SCALE  0.6f     /* controls_help.png in the dialog */

typedef void (*add_child_z_fn)(void *node, void *child, int z);

/* The title label (0x4a31a604) is a child of the dialog at z 2; its origin
 * is the centre of its text, as the sprite's is. */
static void gold_title(uint8_t *dlg)
{
    void *title = *(void **)(dlg + 0x178);
    void *pic = sf_game_sprite(ASSET_DIR "controls_title.png");

    if (!title || !pic) {
        LOGE("ui: controls title not replaced (label %p, sprite %p)", title, pic);
        return;
    }
    ((setxy_fn)VT(pic, NODE_SET_POS))(pic, ((getf_fn)VT(title, NODE_GET_X))(title),
                                      ((getf_fn)VT(title, NODE_GET_Y))(title));
    ((add_child_z_fn)VT(dlg, NODE_ADD_CHILD_Z))(dlg, pic, 2);
    ((setb_fn)VT(title, NODE_SET_VISIBLE))(title, 0);
}

void sf_ui_show_controls(void)
{
    char text[HELP_LINES + 1];
    void *dlg, *label, *pic;

    memset(text, '\n', HELP_LINES);
    text[HELP_LINES] = 0;
    dlg = sf_game_dialog("CONTROLS", text, "ok", "");
    if (!dlg) {
        LOGE("ui: controls dialog not created");
        return;
    }
    /* A dialog with +0x130 set raises the fight's pause flag (0x4a8f404c)
     * when shown and clears it when closed (0x4a51d524, 0x4a51b180), so
     * closing this one over the pause dialog would resume the fight under
     * it. The flag is already up there; this dialog leaves it alone. */
    *((uint8_t *)dlg + 0x130) = 0;
    gold_title(dlg);
    label = *(void **)((uint8_t *)dlg + 0x1bc);   /* SimpleDialog's message */
    pic = sf_game_sprite(ASSET_DIR "controls_help.png");
    if (!label || !pic) {
        LOGE("ui: controls picture not created (label %p, sprite %p)", label, pic);
        return;
    }
    /* The label's origin is the centre of its text. */
    ((setf_fn)VT(pic, NODE_SET_SCALE))(pic, HELP_SCALE);
    ((setxy_fn)VT(pic, NODE_SET_POS))(pic, 0.0f, 0.0f);
    ((add_child_fn)VT(label, NODE_ADD_CHILD))(label, pic);
}

/* --- Settings ------------------------------------------------------------------ */

typedef void (*settings_label_fn)(void *dlg, void *slot, void *anchor, const char *text);  /* 0x4a3dbe68 */

/* Replaces 0x4a3d97ac, which wrote "Controller Scale: Large/Small" next to
 * the row's icon (+0x238) into the label slot +0x25c. */
static void settings_label(uint8_t *dlg)
{
    ((settings_label_fn)G(0x4a3dbe68))(dlg, dlg + 0x25c, *(void **)(dlg + 0x238), "Controls");
}

/* Replaces 0x4a67c540, the scale toggle; the row's click handler
 * (0x4a3db2d4) calls it and then refreshes the label. */
static void settings_click(void)
{
    sf_ui_show_controls();
}

/* Replaces 0x4a67c364: the controller scale is always Large. */
static int controller_large(void)
{
    return 1;
}

/* --- pause dialog ---------------------------------------------------------------- */

typedef void (*image_button_fn)(void *btn, const char *normal, const char *pressed, int id, int z,
                                uint32_t f1, uint32_t f2, void *atlas, int flag);  /* 0x4a161010 */
typedef void (*set_name_fn)(void *btn, const char *name);
typedef void (*set_press_scale_fn)(void *btn, uint32_t scale);                      /* 0x4a1607dc */

#define PAUSE_X   0.0f
#define PAUSE_Y   (-260.0f)
static void pause_click(void *dlg, int *event)
{
    sf_ui_show_controls();
}

/* Slot 0x26c: the original buttons, then ours, built as 0x4a38fd9c builds
 * them but from files (no atlas). The pause icons are 72 px drawn at 2.08;
 * ours is 144 px. */
static void pause_build(uint8_t *dlg)
{
    void *btn, *body = *(void **)(dlg + 0x174);
    int z = *(int32_t *)G(0x4a8f3fb0) - 1;

    ((obj_fn)G(0x4a390c60))(dlg);
    btn = ((void *(*)(uint32_t))G(0x4a78ac9c))(0x1b8);
    ((image_button_fn)G(0x4a161010))(btn, ASSET_DIR "pause_controls.png", ASSET_DIR "pause_selected.png",
                                     4, z, sf_f32(255.0f), sf_f32(255.0f), NULL, 1);
    ((set_name_fn)VT(btn, 0x284))(btn, ASSET_DIR "pause_controls.png");
    sf_game_autorelease(btn);
    ((set_press_scale_fn)G(0x4a1607dc))(btn, sf_f32(1.3f));
    ((add_child_fn)VT(body, NODE_ADD_CHILD))(body, btn);
    if (((int (*)(void))G(0x4a196cbc))())
        ((obj_fn)G(0x4a196cd0))(btn);
    ((setf_fn)VT(btn, NODE_SET_SCALE))(btn, 1.04f);
    ((setxy_fn)VT(btn, NODE_SET_POS))(btn, PAUSE_X, PAUSE_Y);
    sf_game_listen(btn, dlg, pause_click);
    LOGI("ui: pause CONTROLS button added");
}

/* --- install ---------------------------------------------------------------------- */

void sf_ui_install(const sf_s3e_module *mod)
{
    uint32_t *slot = (uint32_t *)G(0x4a8c4330 + 0x26c);

    sf_game_hook("controller scale", 0x4a67c364, 0xe92d41f0, 0xe24dd018, (void *)controller_large);
    sf_game_hook("settings label", 0x4a3d97ac, 0xe92d47f0, 0xe24dd030, (void *)settings_label);
    sf_game_hook("settings click", 0x4a67c540, 0xe92d4070, 0xe24dd010, (void *)settings_click);

    if (*slot == (uint32_t)(uintptr_t)G(0x4a390c60))
        *slot = (uint32_t)(uintptr_t)pause_build;
    else
        LOGW("ui: pause slot holds %#x, not the builder; left alone", (unsigned)*slot);
}

/* Harness only: SF_UI_TEST=<frame> opens the help over whatever is on
 * screen, to check its layout without reaching a fight. */
static unsigned s_frames;

void sf_ui_frame(void)
{
    s_frames++;
}

void sf_ui_poll(void)
{
    sf_cheats_poll();
    static int at = -1;
    if (at < 0) {
        const char *e = getenv("SF_UI_TEST");
        at = e ? atoi(e) : 0;
    }
    if (at > 0 && s_frames >= (unsigned)at) {
        const char *f = getenv("SF_FILE_TEST"), *q = getenv("SF_QUESTS_TEST");
        const char *d = getenv("SF_DIALOG_TEST"), *t = getenv("SF_TRACE_TEST");
        at = 0;
        if (t && *t)   /* a frame trace as the pre-fight screens start (prof.c) */
            sf_prof_trace("test", atoi(t));
        else if (d && *d)   /* the game's dialog factory (0x4a51e374): 14 = settings */
            ((void (*)(int, int))G(0x4a51e374))(atoi(d), 0);
        else if (q && *q)
            sf_quests_selftest(q);
        else if (f && *f)
            sf_file_selftest(f);
        else
            sf_ui_show_controls();
    }
}
