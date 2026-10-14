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

/* --- input.c --- */
void sf_input_poll(void);

/* --- gl.c --- */
void sf_gl_init(void);

#endif
