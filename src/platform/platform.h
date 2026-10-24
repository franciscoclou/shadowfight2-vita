#ifndef SF_PLATFORM_H
#define SF_PLATFORM_H

/* The few things the loader and the s3e shims need from the machine they
 * run on. Two implementations:
 *   src/platform/vita.c      the PS Vita (kubridge, vitaGL, sceCtrl/Touch)
 *   tests/harness/plat_linux.c  Linux armhf under qemu-arm, Mesa EGL --
 *                               boots and screenshots the game on the PC
 * Everything in src/s3e/ and src/loader/ is otherwise plain C + stdio, so
 * both builds run the same shim code.
 */

#include <stdint.h>

/* Root of the game's data (rom://). ram:// is <root>/ram. */
extern char sf_data_dir[256];

/* Map 'text_size' RX bytes at 'addr' followed by 'data_size' RW bytes.
 * Returns the actual base (addr, or wherever the host put it), 0 on error. */
uintptr_t sf_plat_map_image(uintptr_t addr, uint32_t text_size, uint32_t data_size);
/* Copy into the RX mapping, then make it visible to instruction fetch. */
void sf_plat_write_text(void *dst, const void *src, uint32_t len);
void sf_plat_flush(void *addr, uint32_t len);

uint64_t sf_plat_time_us(void);
void sf_plat_sleep_us(uint32_t us);

/* A stable identifier for this device, as a hex string. The game refuses
 * to start (it flags its save as hacked) when the device ID is empty. */
const char *sf_plat_device_id(void);

/* Graphics: bring up a GLES2-capable context of the given size, present a
 * frame, and look up GL entry points by name (hardfp/native ABI). */
void  sf_plat_gl_init(int width, int height);
void  sf_plat_swap(void);
void *sf_plat_gl_proc(const char *name);

/* Audio: start a thread that keeps calling fill() for blocks of
 * interleaved stereo 16-bit frames at the given rate, in real time. */
typedef void (*sf_audio_fill)(int16_t *out, int frames);
void sf_plat_audio_start(int rate, sf_audio_fill fill);

/* Input, sampled once per s3eDeviceYield. Coordinates are in surface
 * pixels. buttons uses the SF_BTN_* bits. */
#define SF_MAX_TOUCH 6
typedef struct sf_input {
    uint32_t buttons;
    int touch_count;
    struct { int id, x, y; } touch[SF_MAX_TOUCH];
} sf_input;
enum {
    SF_BTN_UP = 1 << 0, SF_BTN_DOWN = 1 << 1, SF_BTN_LEFT = 1 << 2, SF_BTN_RIGHT = 1 << 3,
    SF_BTN_CROSS = 1 << 4, SF_BTN_CIRCLE = 1 << 5, SF_BTN_SQUARE = 1 << 6,
    SF_BTN_TRIANGLE = 1 << 7, SF_BTN_L = 1 << 8, SF_BTN_R = 1 << 9,
    SF_BTN_START = 1 << 10, SF_BTN_SELECT = 1 << 11,
};
void sf_plat_input(sf_input *out);

#endif
