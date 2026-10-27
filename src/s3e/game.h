#ifndef SF_GAME_H
#define SF_GAME_H

/* Calling into the game's own code, for the port's UI (cheats.c, ui.c).
 *
 * Addresses are link-time (docs/RE_NOTES.md); G() relocates them by the
 * load delta, which sf_game_init sets before the entry point runs. All of
 * this runs on the game's main thread, from inside its own calls. Floats
 * cross as softfp, so game functions that take them are declared here
 * with raw IEEE bits (sf_f32) or as SOFTFP.
 */
#include "s3e/s3e_internal.h"

#include <stdint.h>

struct sf_s3e_module;

extern uintptr_t sf_game_delta;   /* load address - link address */
#define G(link) ((void *)((uintptr_t)(link) + sf_game_delta))
#define VT(obj, off) (*(void **)(*(uint8_t **)(obj) + (off)))

/* STLport std::string as the game lays it out. */
typedef struct { char *begin, *end, *cap; } gstring;
/* A bound member function: the target gets obj + (adj >> 1). */
typedef struct { void *obj; void *fn; int32_t adj; } delegate;

static inline uint32_t sf_f32(float f)
{
    union { float f; uint32_t u; } v = { f };
    return v.u;
}

typedef void  (*obj_fn)(void *obj);
typedef void *(*get_fn)(void);
typedef void *(*node_get_fn)(void *node);
typedef SOFTFP float (*getf_fn)(void *node);
typedef SOFTFP void  (*setf_fn)(void *node, float v);
typedef SOFTFP void  (*setxy_fn)(void *node, float x, float y);
typedef void  (*setb_fn)(void *node, int v);
typedef void  (*add_child_fn)(void *node, void *child);

/* CCNode virtuals (cocos2d-x 2.x order, checked against their callers). */
enum {
    NODE_SET_SCALE = 0x40, NODE_SET_POS = 0x54, NODE_SET_X = 0x5c, NODE_GET_X = 0x60,
    NODE_SET_Y = 0x64, NODE_GET_Y = 0x68, NODE_GET_SIZE = 0x8c, NODE_SET_VISIBLE = 0x90, NODE_IS_VISIBLE = 0x94,
    NODE_ADD_CHILD = 0xc8, NODE_ADD_CHILD_Z = 0xcc, NODE_GET_PARENT = 0xe4,
    NODE_REMOVE_FROM_PARENT = 0xe8,
};

void sf_game_init(const struct sf_s3e_module *mod);

void  sf_gstring(gstring *s, const char *text);
void  sf_gstring_free(gstring *s);
/* CCObject: autorelease is what the game calls on everything it creates
 * with new (0x4a032dec); retain/release keep a node past its parent. */
void  sf_game_autorelease(void *obj);
void  sf_game_retain(void *obj);
void  sf_game_release(void *obj);
/* Calls fn(self, &event) when button is tapped; event's first word is the
 * button's id. self is the object whose listener base is at +0x114. */
void  sf_game_listen(void *button, void *self, void (*fn)(void *self, int *event));
/* A sprite from an image file (path relative to the asset set). */
void *sf_game_sprite(const char *path);
/* The game's text label in its own font (sakkal), em size px. */
void *sf_game_label(const char *text, float px);
/* The game's SimpleDialog; cancel may be "" for a single button. The
 * buttons are localisation keys. Returns the dialog or NULL. */
void *sf_game_dialog(const char *title, const char *text, const char *ok, const char *cancel);
/* Replace the function at link with fn: its first two words (checked
 * against orig0/orig1) become "ldr pc, [pc, #-4]" and fn's address. */
int   sf_game_hook(const char *name, uint32_t link, uint32_t orig0, uint32_t orig1, void *fn);

#endif
