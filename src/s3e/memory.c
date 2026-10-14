/* s3eMemory: the game links its own libc, and that libc's malloc calls
 * s3eMallocBase/s3eReallocBase/s3eFreeBase, so this is the game's heap.
 * newlib's allocator (sized by _newlib_heap_size_user in main.c) backs it.
 */
#include "s3e/s3e_internal.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>

extern int _newlib_heap_size_user;

static void *sf_s3eMallocBase(uint32_t size)
{
    void *p = memalign(16, size ? size : 1);
    if (!p)
        LOGE("s3eMallocBase(%u) failed", (unsigned)size);
    return p;
}

static void *sf_s3eReallocBase(void *ptr, uint32_t size)
{
    void *p = realloc(ptr, size ? size : 1);
    if (!p)
        LOGE("s3eReallocBase(%p, %u) failed", ptr, (unsigned)size);
    return p;
}

static void sf_s3eFreeBase(void *ptr)
{
    free(ptr);
}

/* s3eMemoryProperty, from libs3e_android.so's s3eMemoryGetInt:
 *   0 HEAP (current heap index)  1 SIZE  2 USED  3 FREE
 *   4 LFB (largest free block)   5 FLAGS 6 (global, 0)
 * The game brackets allocations with old = GetInt(HEAP); SetInt(HEAP, 7);
 * ...; SetInt(HEAP, old), so HEAP must round-trip. Every heap is backed by
 * the same newlib heap here. */
static int32_t s_heap;

static int32_t sf_s3eMemoryGetInt(uint32_t prop)
{
    struct mallinfo mi = mallinfo();
    int32_t used = (int32_t)mi.uordblks;
    switch (prop) {
    case 0: return s_heap;
    case 1: return _newlib_heap_size_user;
    case 2: return used;
    case 3: case 4: return _newlib_heap_size_user - used;
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
