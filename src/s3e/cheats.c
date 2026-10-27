/* The cheats panel: the shop's developer panel, made into a player feature.
 *
 * In the release build the shop builds a button to it only in debug mode
 * (patches.c "shop cheats" turns it on for the shop alone, so none of the
 * debug overlays come with it). The panel is the shop's InfoSprite (vtable
 * link 0x4a8eaf48); slot 0x290 (0x4a63a090) builds its buttons: Clear,
 * Money +1m, Bonus +1m, Level +1, Currency +1m, Reset Ads Counter, all in
 * the white skin, wired to 0x4a63a3e0.
 *
 * sf_cheats_install points that slot at build_panel(), which makes five
 * buttons with the game's own factory: coins, gems, every currency and a
 * level in the beige skin, and RESET PROGRESS in the dark one. RESET
 * PROGRESS opens the game's SimpleDialog (the clans' "Are you sure..."
 * prompt) and only its YES runs the original Clear: delete the save and
 * quit. While the panel is open the shop's item scroll carries the
 * author's watermark.
 */
#include "s3e/game.h"
#include "loader/s3e_loader.h"
#include "platform/platform.h"

#include <string.h>

enum { BTN_DARK = 0, BTN_BEIGE = 5 };
enum { ID_COINS = 1, ID_GEMS, ID_CURRENCIES, ID_LEVEL, ID_RESET };

/* --- game functions (link addresses) ------------------------------------- */

typedef void *(*new_fn)(uint32_t size);                                  /* 0x4a78ac9c */
typedef void  (*free_fn)(void *p);                                       /* 0x4a78c7cc */
typedef void *(*vec_alloc_fn)(uint32_t size);                            /* 0x4a004450 */
/* style, text, id, colour, then 127.5f and 255.0f as raw bits, font, 0 */
typedef void *(*button_fn)(int style, const char *text, int id, uint32_t colour,
                           uint32_t f1, uint32_t f2, void *font, int flag);  /* 0x4a692ca4 */
typedef void *(*dyncast_fn)(void *p, void *from, void *to, int hint);   /* 0x4a791850 */
typedef void  (*add_money_fn)(int amount, int flag);                     /* 0x4a27037c */
typedef void  (*add_gems_fn)(int amount, int a, int b, int c);           /* 0x4a2703c4 */
typedef void **(*list_fn)(void);                                         /* 0x4a6af74c */
typedef void  (*add_currency_fn)(void *profile, void *currency, int amount); /* 0x4a28a23c */
typedef uint32_t (*level_xp_fn)(void *profile);                          /* 0x4a287f24 */
typedef int   (*add_xp_fn)(void *profile, uint32_t xp);                  /* 0x4a28fe44 */
typedef void  (*add_child_z_fn)(void *node, void *child, int z);

/* --- actions --------------------------------------------------------------- */

/* The original Clear (0x4a63a448): the scene object's slot 7, then
 * 0x4a25e424, which deletes the four save files and quits. */
static void reset_confirmed(void *self, int *event)
{
    void *scene = ((get_fn)G(0x4a22c904))();
    LOGI("cheats: progress reset confirmed");
    sf_log_flush();
    ((obj_fn)VT(scene, 0x1c))(scene);
    ((obj_fn)G(0x4a25e424))(((get_fn)G(0x4a266ed4))());
}

/* The clans' confirmation: SimpleDialog with a title, a message and two
 * localised buttons. Title and message are plain text; the buttons must be
 * localisation keys, and there is no RESET key, so they say YES and NO. */
static void ask_reset(uint8_t *self)
{
    void *dlg, *simple;

    dlg = sf_game_dialog("RESET PROGRESS", "This erases all your progress and closes the game. Are you sure?",
                         "rateDlgBtn1Text" /* YES */, "dlgStoryNegative" /* NO */);
    /* DialogObject -> SimpleDialog; its first button is at +0x180. */
    simple = dlg ? ((dyncast_fn)G(0x4a791850))(dlg, G(0x4a8cc5ac), G(0x4a8b8888), 0) : NULL;
    if (!simple) {
        LOGE("cheats: confirmation dialog not created");
        return;
    }
    sf_game_listen(*(void **)((uint8_t *)simple + 0x180), self, reset_confirmed);
}

/* Button clicks, as 0x4a63a3e0 handles them. */
static void on_click(void *self, int *id)
{
    switch (*id) {
    case ID_COINS:
        ((add_money_fn)G(0x4a27037c))(1000000, 0);
        break;
    case ID_GEMS:
        ((add_gems_fn)G(0x4a2703c4))(1000000, 0, 7, 0);
        break;
    case ID_LEVEL: {
        /* The original Level +1: the XP the next level needs, through the
         * game's own XP award, which stops at the level cap. */
        void *profile = ((get_fn)G(0x4a27b924))();
        ((add_xp_fn)G(0x4a28fe44))(profile, ((level_xp_fn)G(0x4a287f24))(profile));
        ((obj_fn)G(0x4a6ac6d4))(NULL);
        break;
    }
    case ID_CURRENCIES: {
        void **list, **p;
        ((obj_fn)G(0x4a6ae7c0))(NULL);
        list = ((list_fn)G(0x4a6af74c))();
        for (p = (void **)list[0]; p < (void **)list[1]; p++)
            ((add_currency_fn)G(0x4a28a23c))(((get_fn)G(0x4a27b924))(), *p, 1000000);
        break;
    }
    case ID_RESET:
        ask_reset((uint8_t *)self);
        return;
    }
    ((obj_fn)G(0x4a6ac6a4))(NULL);   /* refresh the shop's counters */
}

/* --- panel ----------------------------------------------------------------- */

/* The factory's job (slot 0x294, 0x4a63d248) with a chosen skin: make the
 * button, hook its click to on_click, start it hidden and keep it in the
 * panel's button list (+0x210), which the panel shows and hides. */
static void *make_button(uint8_t *self, int style, const char *text, int id)
{
    void *btn = ((button_fn)G(0x4a692ca4))(style, text, id, 0xffffff80, 0x42ff0000, 0x437f0000,
                                           *(void **)(self + 0x128), 0);
    void ***list = (void ***)(self + 0x210);   /* begin, end, capacity */

    sf_game_autorelease(btn);
    sf_game_listen(btn, self, on_click);
    ((setb_fn)VT(btn, 0x1d0))(btn, 0);

    if (list[1] == list[2]) {
        size_t n = (size_t)(list[1] - list[0]), cap = n ? n * 2 : 4;
        void **grown = ((vec_alloc_fn)G(0x4a004450))((uint32_t)(cap * sizeof(void *)));
        if (n)
            memcpy(grown, list[0], n * sizeof(void *));
        if (list[0])
            ((free_fn)G(0x4a78c7cc))(list[0]);
        list[0] = grown;
        list[1] = grown + n;
        list[2] = grown + cap;
    }
    *list[1]++ = btn;
    return btn;
}

/* The author's GitHub name, centred on the shop's item scroll while the
 * panel is open. The scroll is drawn over the panel's column, so the mark
 * hangs on the shop screen itself (vtable 0x4a8ec910, design units from
 * the bottom left) above everything, and follows the panel: shown by
 * show_panel, hidden by hide_panel. It is retained here, so it outlives
 * each shop (a dying shop only detaches it). */
#define WATERMARK_X     675.0f
#define WATERMARK_Y     405.0f
#define WATERMARK_SCALE 0.75f

static void *s_mark;

/* Replaces slot 0x248 (0x4a639fc8), which shows the panel. The panel
 * hangs in the shop's right-hand column (0x4a8eb7f0), a child of the shop
 * screen. */
static void show_panel(void *self)
{
    void *column, *shop;

    ((obj_fn)G(0x4a639fc8))(self);
    column = ((node_get_fn)VT(self, NODE_GET_PARENT))(self);
    shop = column ? ((node_get_fn)VT(column, NODE_GET_PARENT))(column) : NULL;
    if (!shop)
        return;
    if (!s_mark) {
        if (!(s_mark = sf_game_sprite("textures/sfport/watermark.png"))) {
            LOGW("cheats: watermark not created");
            return;
        }
        sf_game_retain(s_mark);
        ((setf_fn)VT(s_mark, NODE_SET_SCALE))(s_mark, WATERMARK_SCALE);
        ((setxy_fn)VT(s_mark, NODE_SET_POS))(s_mark, WATERMARK_X, WATERMARK_Y);
    }
    if (((node_get_fn)VT(s_mark, NODE_GET_PARENT))(s_mark) != shop) {
        if (((node_get_fn)VT(s_mark, NODE_GET_PARENT))(s_mark))
            ((obj_fn)VT(s_mark, NODE_REMOVE_FROM_PARENT))(s_mark);
        ((add_child_z_fn)VT(shop, NODE_ADD_CHILD_Z))(shop, s_mark, 1000);
    }
    ((setb_fn)VT(s_mark, NODE_SET_VISIBLE))(s_mark, 1);
}

/* Replaces slot 0x280 (0x4a63a9f8), which hides whatever the panel shows;
 * every show calls it first. */
static void hide_panel(void *self)
{
    ((obj_fn)G(0x4a63a9f8))(self);
    if (s_mark)
        ((setb_fn)VT(s_mark, NODE_SET_VISIBLE))(s_mark, 0);
}

/* Replaces slot 0x290 (0x4a63a090): the buttons hang below the title
 * (+0x140) in a container node (+0x20c), as the original lays them out. */
static void build_panel(uint8_t *self)
{
    static const struct { int style, id; float gap; const char *text; } buttons[] = {
        { BTN_BEIGE, ID_COINS,      10.0f, "+1,000,000 COINS" },
        { BTN_BEIGE, ID_GEMS,        5.0f, "+1,000,000 GEMS" },
        { BTN_BEIGE, ID_CURRENCIES,  5.0f, "+1,000,000 CURRENCIES" },
        { BTN_BEIGE, ID_LEVEL,       5.0f, "+1 LEVEL" },
        { BTN_DARK,  ID_RESET,      24.0f, "RESET PROGRESS" },
    };
    void *title = *(void **)(self + 0x140);
    void *box = ((new_fn)G(0x4a78ac9c))(0x128);
    float y = ((getf_fn)VT(title, 0x68))(title);
    size_t i;

    ((obj_fn)G(0x4a196870))(box);
    *(void **)(self + 0x20c) = box;
    sf_game_autorelease(box);
    ((setb_fn)VT(box, 0x90))(box, 0);
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        void *btn = make_button(self, buttons[i].style, buttons[i].text, buttons[i].id);
        y -= ((getf_fn)VT(btn, 0x224))(btn) + buttons[i].gap;
        ((setf_fn)VT(btn, 0x64))(btn, y);
        y = ((getf_fn)VT(btn, 0x68))(btn);
        ((add_child_fn)VT(box, 0xc8))(box, btn);
    }
    ((add_child_fn)VT(self, 0xc8))(self, box);
}

/* --- install --------------------------------------------------------------- */

static const char s_title[] = "CHEATS";

void sf_cheats_install(const sf_s3e_module *mod)
{
    uint32_t *slot, *lit, want;

    /* The panel's vtable slot 0x290 holds a relocated code address. */
    slot = (uint32_t *)G(0x4a8eb1d8);
    if (*slot != (uint32_t)(uintptr_t)G(0x4a63a090)) {
        LOGW("cheats: panel slot holds %#x, not the builder; left alone", (unsigned)*slot);
        return;
    }
    *slot = (uint32_t)(uintptr_t)build_panel;
    slot = (uint32_t *)G(0x4a8eaf48 + 0x248);
    if (*slot == (uint32_t)(uintptr_t)G(0x4a639fc8))
        *slot = (uint32_t)(uintptr_t)show_panel;
    else
        LOGW("cheats: panel show slot holds %#x; left alone", (unsigned)*slot);
    slot = (uint32_t *)G(0x4a8eaf48 + 0x280);
    if (*slot == (uint32_t)(uintptr_t)G(0x4a63a9f8))
        *slot = (uint32_t)(uintptr_t)hide_panel;
    else
        LOGW("cheats: panel hide slot holds %#x; left alone", (unsigned)*slot);

    /* The panel's title: "Cheats" through a PC-relative literal that
     * 0x4a63a010 adds to its pc (0x4a63a018). */
    lit = (uint32_t *)G(0x4a63a08c);
    want = (uint32_t)(uintptr_t)s_title - (uint32_t)(uintptr_t)G(0x4a63a018);
    if (*lit == 0x4a815880 - 0x4a63a018) {
        sf_plat_write_text(lit, &want, sizeof(want));
        sf_plat_flush(lit, sizeof(want));
    } else
        LOGW("cheats: title literal holds %#x; left alone", (unsigned)*lit);
    LOGI("cheats: panel installed");
}
