/* Calling into the game's own code; see game.h. */
#include "s3e/game.h"
#include "loader/s3e_loader.h"
#include "platform/platform.h"

uintptr_t sf_game_delta;

typedef void  (*string_fn)(gstring *s, const char *text, void *alloc);   /* 0x4a003a98 */
typedef void  (*listen_fn)(void *source, int event, const delegate *d);  /* 0x4a2025c4 */
typedef void *(*sprite_fn)(const char *path, int res);                   /* 0x4a19013c */
typedef void *(*label_fn)(const char *text, int a, int b);               /* 0x4a6964c8 */
typedef void  (*label_size_fn)(void *label, uint32_t px);                /* 0x4a184700 */
typedef void *(*dialog_fn)(gstring *title, gstring *text, gstring *ok, gstring *cancel,
                           void *on_close, int a6, int a7, int a8, int a9, gstring *a10,
                           int a11, int a12);                            /* 0x4a50f6a4 */

void sf_game_init(const sf_s3e_module *mod)
{
    sf_game_delta = mod->base - mod->hdr.base;
}

void sf_gstring(gstring *s, const char *text)
{
    char alloc;
    ((string_fn)G(0x4a003a98))(s, text, &alloc);
}

void sf_gstring_free(gstring *s)
{
    if (s->begin)
        ((obj_fn)G(0x4a78c7cc))(s->begin);
}

void sf_game_retain(void *node)
{
    ((obj_fn)G(0x4a032dec))(node);
}

/* Every UI object has its listener base at +0x114; the delegate is bound
 * there and adjusted back to the object, as the game binds its own. */
void sf_game_listen(void *button, void *self, void (*fn)(void *self, int *event))
{
    delegate d;
    d.obj = (uint8_t *)self + 0x114;
    d.fn = (void *)fn;
    d.adj = -0x228;
    ((listen_fn)G(0x4a2025c4))((uint8_t *)button + 0x114, 2, &d);
}

/* -1: the current asset set (assets/768 on the Vita). */
void *sf_game_sprite(const char *path)
{
    return ((sprite_fn)G(0x4a19013c))(path, -1);
}

/* As SimpleDialog makes its message: 0x4a6964c8(text, 0, 2), then the
 * size (65 there). */
void *sf_game_label(const char *text, float px)
{
    void *label = ((label_fn)G(0x4a6964c8))(text, 0, 2);
    ((label_size_fn)G(0x4a184700))(label, sf_f32(px));
    return label;
}

void *sf_game_dialog(const char *title, const char *text, const char *ok, const char *cancel)
{
    gstring t, m, o, c, e;
    void *dlg;

    sf_gstring(&t, title);
    sf_gstring(&m, text);
    sf_gstring(&o, ok);
    sf_gstring(&c, cancel);
    sf_gstring(&e, "");
    dlg = ((dialog_fn)G(0x4a50f6a4))(&t, &m, &o, &c, NULL, 1, 0, 0, 0, &e, 0, 0);
    sf_gstring_free(&e);
    sf_gstring_free(&c);
    sf_gstring_free(&o);
    sf_gstring_free(&m);
    sf_gstring_free(&t);
    return dlg;
}

int sf_game_hook(const char *name, uint32_t link, uint32_t orig0, uint32_t orig1, void *fn)
{
    uint32_t *at = (uint32_t *)G(link);
    uint32_t code[2] = { 0xe51ff004 /* ldr pc, [pc, #-4] */, (uint32_t)(uintptr_t)fn };

    if (at[0] != orig0 || at[1] != orig1) {
        LOGW("hook %s: %#x holds %#x %#x; left alone", name, (unsigned)link, (unsigned)at[0],
             (unsigned)at[1]);
        return -1;
    }
    sf_plat_write_text(at, code, sizeof(code));
    sf_plat_flush(at, sizeof(code));
    LOGI("hook %s installed at %#x", name, (unsigned)link);
    return 0;
}
