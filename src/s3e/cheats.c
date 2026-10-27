/* The cheats panel: the shop's developer panel, made into a player feature.
 *
 * In the release build the shop builds a button to it only in debug mode
 * (patches.c "shop cheats" turns it on for the shop alone, so none of the
 * debug overlays come with it). The panel is the shop's InfoSprite (vtable
 * link 0x4a8eaf48); slot 0x290 (0x4a63a090) builds its buttons: Clear,
 * Money +1m, Bonus +1m, Level +1, Currency +1m, Reset Ads Counter, all in
 * the white skin, wired to 0x4a63a3e0.
 *
 * sf_cheats_install points that slot at build_panel(), which makes four
 * buttons with the game's own factory: coins, gems and every currency in
 * the beige skin, and RESET PROGRESS in the dark one. RESET PROGRESS opens
 * the game's SimpleDialog (the clans' "Are you sure..." prompt) and only
 * its YES runs the original Clear: delete the save and quit.
 *
 * Everything here runs on the game's main thread, from inside its own
 * calls. Floats cross as softfp; the rest are plain integers.
 */
#include "s3e/s3e_internal.h"
#include "loader/s3e_loader.h"
#include "platform/platform.h"

#include <string.h>

static uintptr_t s_delta;   /* load address - link address */
#define G(link) ((void *)((uintptr_t)(link) + s_delta))
#define VT(obj, off) (*(void **)(*(uint8_t **)(obj) + (off)))

/* STLport std::string as the game lays it out. */
typedef struct { char *begin, *end, *cap; } gstring;
/* A bound member function: the target gets obj + (adj >> 1). */
typedef struct { void *obj; void *fn; int32_t adj; } delegate;

enum { BTN_DARK = 0, BTN_BEIGE = 5 };
enum { ID_COINS = 1, ID_GEMS, ID_CURRENCIES, ID_RESET };

/* --- game functions (link addresses) ------------------------------------- */

typedef void *(*new_fn)(uint32_t size);                                  /* 0x4a78ac9c */
typedef void  (*free_fn)(void *p);                                       /* 0x4a78c7cc */
typedef void *(*vec_alloc_fn)(uint32_t size);                            /* 0x4a004450 */
typedef void  (*obj_fn)(void *obj);
/* style, text, id, colour, then 127.5f and 255.0f as raw bits, font, 0 */
typedef void *(*button_fn)(int style, const char *text, int id, uint32_t colour,
                           uint32_t f1, uint32_t f2, void *font, int flag);  /* 0x4a692ca4 */
typedef void  (*listen_fn)(void *source, int event, const delegate *d);  /* 0x4a2025c4 */
typedef void  (*string_fn)(gstring *s, const char *text, void *alloc);   /* 0x4a003a98 */
typedef void *(*confirm_fn)(gstring *title, gstring *text, gstring *ok, gstring *cancel,
                            void *on_close, int a6, int a7, int a8, int a9, gstring *a10,
                            int a11, int a12);                           /* 0x4a50f6a4 */
typedef void *(*dyncast_fn)(void *p, void *from, void *to, int hint);   /* 0x4a791850 */
typedef void  (*add_money_fn)(int amount, int flag);                     /* 0x4a27037c */
typedef void  (*add_gems_fn)(int amount, int a, int b, int c);           /* 0x4a2703c4 */
typedef void **(*list_fn)(void);                                         /* 0x4a6af74c */
typedef void *(*get_fn)(void);
typedef void  (*add_currency_fn)(void *profile, void *currency, int amount); /* 0x4a28a23c */

typedef SOFTFP float (*getf_fn)(void *node);
typedef SOFTFP void  (*setf_fn)(void *node, float v);
typedef void  (*setb_fn)(void *node, int v);
typedef void  (*add_child_fn)(void *node, void *child);

static void free_string(gstring *s)
{
    if (s->begin)
        ((free_fn)G(0x4a78c7cc))(s->begin);
}

/* --- actions --------------------------------------------------------------- */

/* The original Clear (0x4a63a448): the scene object's slot 7, then
 * 0x4a25e424, which deletes the four save files and quits. */
static void reset_confirmed(void *self, void *arg)
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
    gstring title, text, yes, no, empty;
    string_fn str = (string_fn)G(0x4a003a98);
    char alloc;
    void *dlg, *simple;
    delegate d;

    str(&title, "RESET PROGRESS", &alloc);
    str(&text, "This erases all your progress and closes the game. Are you sure?", &alloc);
    str(&yes, "rateDlgBtn1Text", &alloc);     /* YES */
    str(&no, "dlgStoryNegative", &alloc);     /* NO */
    str(&empty, "", &alloc);
    dlg = ((confirm_fn)G(0x4a50f6a4))(&title, &text, &yes, &no, NULL, 1, 0, 0, 0, &empty, 0, 0);
    free_string(&empty);
    free_string(&no);
    free_string(&yes);
    free_string(&text);
    free_string(&title);
    /* DialogObject -> SimpleDialog; its first button is at +0x180. */
    simple = dlg ? ((dyncast_fn)G(0x4a791850))(dlg, G(0x4a8cc5ac), G(0x4a8b8888), 0) : NULL;
    if (!simple) {
        LOGE("cheats: confirmation dialog not created");
        return;
    }
    d.obj = self + 0x114;
    d.fn = (void *)reset_confirmed;
    d.adj = -0x228;
    ((listen_fn)G(0x4a2025c4))(*(uint8_t **)((uint8_t *)simple + 0x180) + 0x114, 2, &d);
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
    delegate d;

    ((obj_fn)G(0x4a032dec))(btn);
    d.obj = self + 0x114;
    d.fn = (void *)on_click;
    d.adj = -0x228;
    ((listen_fn)G(0x4a2025c4))((uint8_t *)btn + 0x114, 2, &d);
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

/* Replaces slot 0x290 (0x4a63a090): the buttons hang below the title
 * (+0x140) in a container node (+0x20c), as the original lays them out. */
static void build_panel(uint8_t *self)
{
    static const struct { int style, id; float gap; const char *text; } buttons[] = {
        { BTN_BEIGE, ID_COINS,      10.0f, "+1,000,000 COINS" },
        { BTN_BEIGE, ID_GEMS,        5.0f, "+1,000,000 GEMS" },
        { BTN_BEIGE, ID_CURRENCIES,  5.0f, "+1,000,000 CURRENCIES" },
        { BTN_DARK,  ID_RESET,      24.0f, "RESET PROGRESS" },
    };
    void *title = *(void **)(self + 0x140);
    void *box = ((new_fn)G(0x4a78ac9c))(0x128);
    float y = ((getf_fn)VT(title, 0x68))(title);
    size_t i;

    ((obj_fn)G(0x4a196870))(box);
    *(void **)(self + 0x20c) = box;
    ((obj_fn)G(0x4a032dec))(box);
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

    s_delta = mod->base - mod->hdr.base;

    /* The panel's vtable slot 0x290 holds a relocated code address. */
    slot = (uint32_t *)G(0x4a8eb1d8);
    if (*slot != (uint32_t)(uintptr_t)G(0x4a63a090)) {
        LOGW("cheats: panel slot holds %#x, not the builder; left alone", (unsigned)*slot);
        return;
    }
    *slot = (uint32_t)(uintptr_t)build_panel;

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
