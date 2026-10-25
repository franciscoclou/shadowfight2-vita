/* The s3eThread extension (hash 0xcc0b4a28, 20-entry table). The game's
 * libc builds pthread/semaphore and libstdc++'s gthread mutexes on it, so
 * without it the first std::mutex throws __concurrence_lock_error.
 *
 * Slot meanings, from the game's own wrappers (docs/RE_NOTES.md):
 *   3  Create(start, arg, ...)          thread class ctor: Create(fn, 0,0,0,0)
 *   4  GetCurrent()                     the game's pthread_self()
 *   8, 11  act on a thread handle (dtor / method of the same class)
 *   9  LockCreate()                     12 LockAcquire(lock, timeout_ms)
 *   13 LockRelease(lock)
 *   14 SemCreate(count)                 sem_init wrapper; NULL -> EINVAL
 *   16 SemWait(sem, timeout_ms)         sem_wait wrapper passes -1
 *   17 SemPost(sem)
 *
 * Thread handles are integer IDs, not pointers: the game's pthread_self()
 * (link 0x4a78d688) returns GetCurrent(), or 4000 when s3eThread is
 * missing, and its TLS (0x4a7915ec/0x4a791508) indexes a 129-entry table
 * by id - 4000. An ID outside 4000..4128 makes every TLS lookup fail and
 * the allocator's per-thread context (0x4a78f448) recurse until the stack
 * runs out. So the main thread is 4000 and created threads 4001 onwards.
 * Locks and semaphores are tagged pointers.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum { TAG_LOCK = 0x4c4f434b, TAG_SEM = 0x53454d41 };
enum { TID_MAIN = 4000, MAX_THREADS = 129 };

typedef void *(*start_fn)(void *);

typedef struct {
    int used;
    pthread_t th;
    start_fn fn;
    void *arg;
} sf_thread;

typedef struct {
    uint32_t tag;
    pthread_mutex_t m;
} sf_lock;

typedef struct {
    uint32_t tag;
    pthread_mutex_t m;
    pthread_cond_t c;
    int count;
} sf_sem;

static sf_thread s_threads[MAX_THREADS];
static pthread_mutex_t s_threads_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t s_self_key;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void init_once(void)
{
    pthread_key_create(&s_self_key, NULL);
    s_threads[0].used = 1;
    s_threads[0].th = pthread_self();
}

static void *trampoline(void *p)
{
    int id = (int)(intptr_t)p;
    sf_thread *t = &s_threads[id - TID_MAIN];
    sf_plat_worker_thread();
    pthread_setspecific(s_self_key, p);
    return t->fn(t->arg);
}

/* 3 */
static int32_t thr_create(start_fn fn, void *arg, void *a2, void *a3, void *a4)
{
    pthread_attr_t attr;
    int i, id = 0;

    pthread_once(&s_once, init_once);
    pthread_mutex_lock(&s_threads_lock);
    for (i = 1; i < MAX_THREADS; i++) {
        if (!s_threads[i].used) {
            s_threads[i].used = 1;
            s_threads[i].fn = fn;
            s_threads[i].arg = arg;
            id = TID_MAIN + i;
            break;
        }
    }
    pthread_mutex_unlock(&s_threads_lock);
    if (!id) {
        LOGE("s3eThreadCreate: out of thread slots");
        return 0;
    }
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (pthread_create(&s_threads[id - TID_MAIN].th, &attr, trampoline, (void *)(intptr_t)id) != 0) {
        LOGE("s3eThreadCreate failed");
        s_threads[id - TID_MAIN].used = 0;
        id = 0;
    }
    pthread_attr_destroy(&attr);
    LOGI("s3eThreadCreate(fn=%p, arg=%p, %p, %p, %p) -> %d", (void *)fn, arg, a2, a3, a4, id);
    return id;
}

/* 4 */
static int32_t thr_current(void)
{
    void *self;
    pthread_once(&s_once, init_once);
    self = pthread_getspecific(s_self_key);
    return self ? (int32_t)(intptr_t)self : TID_MAIN;
}

int sf_thread_is_main(void)
{
    return thr_current() == TID_MAIN;
}

/* 8 and 11: called on a handle by the game's thread class. A thread is
 * joined (always safe for an object being torn down) and its slot freed;
 * a lock or semaphore, should one ever arrive here, is destroyed. */
static int32_t thr_handle_op(int slot, uintptr_t h)
{
    if (h > TID_MAIN && h < TID_MAIN + MAX_THREADS) {
        sf_thread *t = &s_threads[h - TID_MAIN];
        if (t->used && (int32_t)h != thr_current()) {
            pthread_join(t->th, NULL);
            t->used = 0;
            LOGI("s3eThread[%d](%u): joined", slot, (unsigned)h);
        }
        return S3E_RESULT_SUCCESS;
    }
    if (h == TID_MAIN)
        return S3E_RESULT_SUCCESS;
    if (h > 0xffff && *(uint32_t *)h == TAG_LOCK) {
        pthread_mutex_destroy(&((sf_lock *)h)->m);
        free((void *)h);
        return S3E_RESULT_SUCCESS;
    }
    if (h > 0xffff && *(uint32_t *)h == TAG_SEM) {
        pthread_cond_destroy(&((sf_sem *)h)->c);
        pthread_mutex_destroy(&((sf_sem *)h)->m);
        free((void *)h);
        return S3E_RESULT_SUCCESS;
    }
    LOGW("s3eThread[%d](%#x): unknown handle", slot, (unsigned)h);
    return S3E_RESULT_ERROR;
}
static int32_t thr_slot8(uintptr_t h) { return thr_handle_op(8, h); }
static int32_t thr_slot11(uintptr_t h) { return thr_handle_op(11, h); }

/* 9 */
static void *lock_create(void)
{
    pthread_mutexattr_t a;
    sf_lock *l = calloc(1, sizeof(*l));
    l->tag = TAG_LOCK;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&l->m, &a);
    pthread_mutexattr_destroy(&a);
    return l;
}

/* 12 */
static int32_t lock_acquire_(sf_lock *l, int32_t timeout_ms);

/* Time the main thread spends blocked on the game's locks and semaphores
 * (prof.c "wait"): loading that waits on another thread shows up here. */
static int32_t lock_acquire(sf_lock *l, int32_t timeout_ms)
{
    uint64_t t0 = sf_plat_time_us();
    int32_t r = lock_acquire_(l, timeout_ms);
    if (sf_thread_is_main())
        sf_prof_add(SF_PROF_WAIT, sf_plat_time_us() - t0, 0);
    return r;
}

static int32_t lock_acquire_(sf_lock *l, int32_t timeout_ms)
{
    LOGV("LockAcquire(%p, %d)", (void *)l, (int)timeout_ms);
    if (!l || l->tag != TAG_LOCK)
        return S3E_RESULT_ERROR;
    if (timeout_ms < 0)
        return pthread_mutex_lock(&l->m) == 0 ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
    for (;;) {
        if (pthread_mutex_trylock(&l->m) == 0)
            return S3E_RESULT_SUCCESS;
        if (timeout_ms-- <= 0)
            return S3E_RESULT_ERROR;
        sf_plat_sleep_us(1000);
    }
}

/* 13 */
static int32_t lock_release(sf_lock *l)
{
    LOGV("LockRelease(%p)", (void *)l);
    if (!l || l->tag != TAG_LOCK)
        return S3E_RESULT_ERROR;
    return pthread_mutex_unlock(&l->m) == 0 ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
}

/* 14 */
static void *sem_create(uint32_t count)
{
    sf_sem *s = calloc(1, sizeof(*s));
    s->tag = TAG_SEM;
    s->count = (int)count;
    pthread_mutex_init(&s->m, NULL);
    pthread_cond_init(&s->c, NULL);
    return s;
}

/* 16 */
static int32_t sem_wait_ms_(sf_sem *s, int32_t timeout_ms);

static int32_t sem_wait_ms(sf_sem *s, int32_t timeout_ms)
{
    uint64_t t0 = sf_plat_time_us();
    int32_t r = sem_wait_ms_(s, timeout_ms);
    if (sf_thread_is_main())
        sf_prof_add(SF_PROF_WAIT, sf_plat_time_us() - t0, 0);
    return r;
}

static int32_t sem_wait_ms_(sf_sem *s, int32_t timeout_ms)
{
    int32_t r = S3E_RESULT_SUCCESS;
    if (!s || s->tag != TAG_SEM)
        return S3E_RESULT_ERROR;
    pthread_mutex_lock(&s->m);
    if (timeout_ms < 0) {
        while (s->count <= 0)
            pthread_cond_wait(&s->c, &s->m);
        s->count--;
    } else {
        while (s->count <= 0 && timeout_ms > 0) {
            pthread_mutex_unlock(&s->m);
            sf_plat_sleep_us(1000);
            timeout_ms--;
            pthread_mutex_lock(&s->m);
        }
        if (s->count > 0)
            s->count--;
        else
            r = S3E_RESULT_ERROR;
    }
    pthread_mutex_unlock(&s->m);
    return r;
}

/* 17 */
static int32_t sem_post(sf_sem *s)
{
    if (!s || s->tag != TAG_SEM)
        return S3E_RESULT_ERROR;
    pthread_mutex_lock(&s->m);
    s->count++;
    pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return S3E_RESULT_SUCCESS;
}

#define UNKNOWN(n) static uint32_t unk_##n(uint32_t a, uint32_t b, uint32_t c, uint32_t d) \
    { LOGW("s3eThread[%d](%#x, %#x, %#x, %#x) -- slot not implemented", n, \
           (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d); return 0; }
UNKNOWN(0) UNKNOWN(1) UNKNOWN(2) UNKNOWN(5) UNKNOWN(6) UNKNOWN(7) UNKNOWN(10)
UNKNOWN(15) UNKNOWN(18) UNKNOWN(19)

const void *const sf_s3eThread_table[20] = {
    unk_0, unk_1, unk_2, thr_create, thr_current, unk_5, unk_6, unk_7,
    thr_slot8, lock_create, unk_10, thr_slot11, lock_acquire, lock_release,
    sem_create, unk_15, sem_wait_ms, sem_post, unk_18, unk_19,
};
