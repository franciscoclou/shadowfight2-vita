#ifndef SF_S3E_INTERNAL_H
#define SF_S3E_INTERNAL_H

/* Shared declarations for the s3e shims that replace libs3e_android.so.
 *
 * Calling convention: the game is armeabi-v7a **softfp** (floats travel in
 * core registers -- glClearColor(0,0,0,1) loads 0x3f800000 into r3), while
 * vitasdk builds hardfp. Integer-only signatures are identical under both,
 * but every shim the game calls with a float argument must be SOFTFP, and
 * so must every game callback we call that takes or returns a float.
 */

#include <stddef.h>
#include <stdint.h>
#include "platform/log.h"

#define SOFTFP __attribute__((pcs("aapcs")))

typedef struct sf_export {
    const char *name;
    void *fn;
} sf_export;
#define SF_EXPORT(fn) { #fn, (void *)(fn) }

#define SF_SCREEN_W 960
#define SF_SCREEN_H 544

typedef int32_t s3eResult;   /* S3E_RESULT_SUCCESS = 0, ERROR = 1 */
#define S3E_RESULT_SUCCESS 0
#define S3E_RESULT_ERROR   1

/* Game callbacks: int32 (*)(void *systemData, void *userData). */
typedef int32_t (*s3eCallback)(void *system_data, void *user_data);

/* --- imports.c --- */
void *sf_import_resolve(const char *name);
void *sf_import_stub(int index, const char *name);

/* --- device.c --- */
void sf_device_init(const char *embedded_config);
uint64_t sf_time_ms(void);
int  sf_quit_requested(void);

/* --- prof.c --- */
enum { SF_PROF_SHADER, SF_PROF_LINK, SF_PROF_TEXTURE, SF_PROF_FOPEN, SF_PROF_FREAD,
       SF_PROF_MALLOC, SF_PROF_INFLATE, SF_PROF_YIELD, SF_PROF_WRITE, SF_PROF_WAIT,
       SF_PROF_N };
void sf_prof_add(int cat, uint64_t us, uint32_t bytes);
void sf_prof_frame(void);   /* once per presented frame */

/* --- patches.c --- */
struct sf_s3e_module;
void sf_apply_patches(const struct sf_s3e_module *mod);

/* --- cheats.c --- */
void sf_cheats_install(const struct sf_s3e_module *mod);
void sf_cheats_poll(void);   /* main thread, once per yield */

/* --- ui.c --- */
void sf_ui_install(const struct sf_s3e_module *mod);
void sf_ui_show_controls(void);
void sf_ui_poll(void);   /* main thread, once per yield */
void sf_ui_frame(void);  /* once per presented frame */
const uint8_t *sf_ui_asset(const char *path, uint32_t *size);

/* --- packs.c --- */
void sf_packs_register(void);
void sf_packs_install_unlocks(void);   /* after sf_game_init */   /* before the entry point: list the packs in packs.xml */

/* --- hud.c --- */
void sf_hud_file_opened(const char *name);
const void *sf_hud_tex_image(int32_t level, int32_t w, int32_t h, uint32_t fmt, uint32_t type,
                             const void *data);
void sf_hud_tex_deleted(int32_t n, const uint32_t *ids);
void sf_hud_apply(int shown);
void sf_hud_tex_bound(uint32_t tex);
void sf_hud_frame(void);
int  sf_hud_in_fight(void);   /* the fight controls were drawn just now */

/* --- thread.c --- */
int  sf_thread_is_main(void);

/* --- file.c --- */
void *sf_file_load(const char *name, uint32_t *size);
void sf_file_selftest(const char *name);   /* harness diagnostics */
void sf_file_sync(void);   /* wait for the deferred writes to reach the disk */
void *sf_file_load_raw(const char *name, uint32_t *size);   /* without quests.c's edits */

/* --- quests.c --- */
int sf_quests_wants(const char *name);
void *sf_quests_load(const char *name, uint32_t *size);
void sf_quests_selftest(const char *dir);   /* harness diagnostics */

/* --- sound.c --- */
void sf_sound_poll(void);   /* main thread: deliver queued audio callbacks */

/* --- input.c --- */
int  sf_touch_controls_shown(void);
int  sf_option_cheats(void);
int  sf_option_remove_online(void);
void sf_input_poll(void);

/* --- gl.c --- */
void sf_gl_init(void);
void *sf_gl_trace(const char *name, void *fn);   /* fn behind a GL error check */

#endif
