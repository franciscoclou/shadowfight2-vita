/* s3eMemory: the game links its own libc, and that libc's malloc calls
 * s3eMallocBase/s3eReallocBase/s3eFreeBase, so this is the game's heap.
 * newlib's allocator (sized by _newlib_heap_size_user in main.c) backs it.
 *
 * A load makes over a million small allocations per 10 s, so small blocks
 * (up to SMALL_MAX bytes) come from segregated free lists instead of newlib:
 * 16-byte size classes carved from SLAB_SIZE slabs, a free is a push onto
 * its class list. Every block has a 16-byte header holding its class (0 =
 * large, newlib-owned, with its size), which keeps payloads 16-byte aligned
 * as memalign(16) did. Small-block memory is reused, never returned.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

extern int _newlib_heap_size_user;

#define HDR        16
#define SMALL_MAX  1024
#define NCLASS     (SMALL_MAX / 16)
#define SLAB_SIZE  (256 * 1024)
#define MAGIC      0x5f0d0000u

typedef struct {
    uint32_t tag;        /* MAGIC | class (1..NCLASS), or MAGIC for large */
    uint32_t size;       /* large: requested size */
    uint32_t pad[2];
} sf_hdr;

static void *s_free[NCLASS + 1];
static uint8_t *s_slab;
static uint32_t s_slab_left;
static uint32_t s_small_used;   /* bytes in live small blocks, for USED */
static volatile int s_spin;

static void lock(void)
{
    while (__atomic_test_and_set(&s_spin, __ATOMIC_ACQUIRE)) {}
}
static void unlock(void)
{
    __atomic_clear(&s_spin, __ATOMIC_RELEASE);
}

static uint32_t class_of(uint32_t size)
{
    return size <= SMALL_MAX ? (size + 15) / 16 : 0;
}

static void *small_alloc(uint32_t cls)
{
    uint32_t block = HDR + cls * 16;
    sf_hdr *h;

    lock();
    if (s_free[cls]) {
        h = s_free[cls];
        s_free[cls] = *(void **)h;
    } else {
        if (s_slab_left < block) {
            uint8_t *slab = memalign(16, SLAB_SIZE);
            if (!slab) {
                unlock();
                return NULL;
            }
            s_slab = slab;
            s_slab_left = SLAB_SIZE;
        }
        h = (sf_hdr *)s_slab;
        s_slab += block;
        s_slab_left -= block;
    }
    s_small_used += cls * 16;
    unlock();
    h->tag = MAGIC | cls;
    return (uint8_t *)h + HDR;
}

static void *large_alloc(uint32_t size)
{
    sf_hdr *h = memalign(16, (size_t)size + HDR);
    if (!h)
        return NULL;
    h->tag = MAGIC;
    h->size = size;
    return (uint8_t *)h + HDR;
}

static void *sf_s3eMallocBase(uint32_t size)
{
    uint32_t cls = class_of(size ? size : 1);
    void *p = cls ? small_alloc(cls) : large_alloc(size);
    sf_prof_add(SF_PROF_MALLOC, 0, 0);
    if (!p)
        LOGE("s3eMallocBase(%u) failed", (unsigned)size);
    return p;
}

static void sf_s3eFreeBase(void *ptr)
{
    sf_hdr *h;
    uint32_t cls;

    if (!ptr)
        return;
    h = (sf_hdr *)((uint8_t *)ptr - HDR);
    if ((h->tag & 0xffff0000u) != MAGIC) {
        LOGE("s3eFreeBase(%p): not one of our blocks", ptr);
        return;
    }
    cls = h->tag & 0xffff;
    if (!cls) {
        free(h);
        return;
    }
    lock();
    *(void **)h = s_free[cls];
    s_free[cls] = h;
    s_small_used -= cls * 16;
    unlock();
}

static void *sf_s3eReallocBase(void *ptr, uint32_t size)
{
    sf_hdr *h;
    uint32_t cls, cap;
    void *p;

    if (!ptr)
        return sf_s3eMallocBase(size);
    h = (sf_hdr *)((uint8_t *)ptr - HDR);
    if ((h->tag & 0xffff0000u) != MAGIC) {
        LOGE("s3eReallocBase(%p): not one of our blocks", ptr);
        return NULL;
    }
    cls = h->tag & 0xffff;
    cap = cls ? cls * 16 : h->size;
    if (cls && class_of(size ? size : 1) == cls)
        return ptr;                     /* same class: nothing to move */
    if (!cls && size <= cap && size > SMALL_MAX) {
        h->size = size;                 /* large shrink: keep the block */
        return ptr;
    }
    /* Everything else moves: realloc() would not keep memalign's 16-byte
     * alignment for large blocks. */
    p = sf_s3eMallocBase(size);
    if (!p)
        return NULL;
    memcpy(p, ptr, cap < size ? cap : size);
    sf_s3eFreeBase(ptr);
    return p;
}

/* s3eMemoryProperty, from libs3e_android.so's s3eMemoryGetInt:
 *   0 HEAP (current heap index)  1 SIZE  2 USED  3 FREE
 *   4 LFB (largest free block)   5 FLAGS 6 (global, 0)
 * The game brackets every allocation with old = GetInt(HEAP);
 * SetInt(HEAP, 7); ...; SetInt(HEAP, old), so HEAP must round-trip and be
 * cheap: mallinfo() walks newlib's heap, so it is only called for the
 * properties that need it. Every heap is backed by the same newlib heap. */
static int32_t s_heap;

static int32_t heap_used(void)
{
    struct mallinfo mi = mallinfo();
    return (int32_t)mi.uordblks;
}

static int32_t sf_s3eMemoryGetInt(uint32_t prop)
{
    switch (prop) {
    case 0: return s_heap;
    case 1: return _newlib_heap_size_user;
    case 2: return heap_used();
    case 3: case 4: return _newlib_heap_size_user - heap_used();
    case 5: case 6: return 0;
    default:
        LOGW("s3eMemoryGetInt(%u) unknown", (unsigned)prop);
        return -1;
    }
}

static int32_t sf_s3eMemorySetInt(uint32_t prop, int32_t value)
{
    if (prop == 0 && value >= 0 && value < 8) {
        s_heap = value;
        return S3E_RESULT_SUCCESS;
    }
    LOGI("s3eMemorySetInt(%u, %d)", (unsigned)prop, (int)value);
    return S3E_RESULT_SUCCESS;
}

/* The game's allocator may install a user manager; it routes back through
 * the Base functions anyway, so remembering it is enough. */
static int32_t sf_s3eMemorySetUserMemMgr(void *mgr)
{
    LOGV("s3eMemorySetUserMemMgr(%p)", mgr);
    return S3E_RESULT_SUCCESS;
}

const sf_export sf_exports_memory[] = {
    { "s3eMallocBase", (void *)sf_s3eMallocBase },
    { "s3eReallocBase", (void *)sf_s3eReallocBase },
    { "s3eFreeBase", (void *)sf_s3eFreeBase },
    { "s3eMemoryGetInt", (void *)sf_s3eMemoryGetInt },
    { "s3eMemorySetInt", (void *)sf_s3eMemorySetInt },
    { "s3eMemorySetUserMemMgr", (void *)sf_s3eMemorySetUserMemMgr },
    { NULL, NULL }
};
