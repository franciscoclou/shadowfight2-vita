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

/* s3eMemoryProperty (values from libs3e_android.so, see docs/RE_NOTES.md):
 *   0 S3E_MEMORY_SIZE   1 S3E_MEMORY_FREE   2 S3E_MEMORY_USED
 *   3 S3E_MEMORY_LFB    4 S3E_MEMORY_HEAP   5 S3E_MEMORY_FLAGS */
static int32_t sf_s3eMemoryGetInt(uint32_t prop)
{
    struct mallinfo mi = mallinfo();
    switch (prop) {
    case 0: return _newlib_heap_size_user;
    case 1: return _newlib_heap_size_user - (int32_t)mi.uordblks;
    case 2: return (int32_t)mi.uordblks;
    case 3: return _newlib_heap_size_user - (int32_t)mi.uordblks;
    case 4: return 0;
    default:
        LOGW("s3eMemoryGetInt(%u) unknown", (unsigned)prop);
        return 0;
    }
}

static int32_t sf_s3eMemorySetInt(uint32_t prop, int32_t value)
{
    LOGI("s3eMemorySetInt(%u, %d)", (unsigned)prop, (int)value);
    return S3E_RESULT_SUCCESS;
}

/* The game's allocator may install a user manager; it routes back through
 * the Base functions anyway, so remembering it is enough. */
static int32_t sf_s3eMemorySetUserMemMgr(void *mgr)
{
    LOGI("s3eMemorySetUserMemMgr(%p)", mgr);
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
