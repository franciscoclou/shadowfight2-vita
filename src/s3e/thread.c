/* The s3eThread extension (hash 0xcc0b4a28, 20-entry table). The game's
 * libc builds pthread/semaphore and libstdc++'s gthread mutexes on it, so
 * without it the first std::mutex throws __concurrence_lock_error.
 *
 * Slot meanings, from the game's own wrappers (docs/RE_NOTES.md):
 *   3  Create(start, arg, ...)          thread class ctor: Create(fn, 0,0,0,0)
 *   4  GetCurrent()
 *   8, 11  act on a thread handle (dtor / method of the same class)
 *   9  LockCreate()                     12 LockAcquire(lock, timeout_ms)
 *   13 LockRelease(lock)
 *   14 SemCreate(count)                 sem_init wrapper; NULL -> EINVAL
 *   16 SemWait(sem, timeout_ms)         sem_wait wrapper passes -1
 *   17 SemPost(sem)
 * Slots the game never calls log their arguments if they ever are. Every
 * handle carries a type tag, so the two thread-handle slots (8, 11) can
 * tell what they were given rather than relying on a guess.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum { TAG_THREAD = 0x54485244, TAG_LOCK = 0x4c4f434b, TAG_SEM = 0x53454d41 };

typedef void *(*start_fn)(void *);

typedef struct {
    uint32_t tag;
    pthread_t th;
    start_fn fn;
    void *arg, *ret;
    int done;
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

static sf_thread s_main = { TAG_THREAD };
static pthread_key_t s_self_key;
static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static void init_once(void)
{
    pthread_key_create(&s_self_key, NULL);
    s_main.th = pthread_self();
}

static void *trampoline(void *p)
{
    sf_thread *t = p;
    pthread_setspecific(s_self_key, t);
    t->ret = t->fn(t->arg);
    t->done = 1;
    return t->ret;
}

/* 3 */
static void *thr_create(start_fn fn, void *arg, void *a2, void *a3, void *a4)
{
    pthread_attr_t attr;
    sf_thread *t;

    pthread_once(&s_once, init_once);
    t = calloc(1, sizeof(*t));
    t->tag = TAG_THREAD;
    t->fn = fn;
    t->arg = arg;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    if (pthread_create(&t->th, &attr, trampoline, t) != 0) {
        LOGE("s3eThreadCreate failed");
        free(t);
        t = NULL;
    }
    pthread_attr_destroy(&attr);
    LOGI("s3eThreadCreate(fn=%p, arg=%p, %p, %p, %p) -> %p", (void *)fn, arg, a2, a3, a4, (void *)t);
    return t;
}

/* 4 */
static void *thr_current(void)
{
    LOGV("GetCurrent");
    sf_thread *t;
    pthread_once(&s_once, init_once);
    t = pthread_getspecific(s_self_key);
    return t ? t : &s_main;
}

/* 8 and 11: called on a handle by the game's thread class. A thread is
 * joined (always safe for an object being torn down); a lock or semaphore,
 * should one ever arrive here, is destroyed. */
static int32_t thr_handle_op(int slot, void *h)
{
    sf_thread *t = h;
    if (h && *(uint32_t *)h == TAG_LOCK) {
        pthread_mutex_destroy(&((sf_lock *)h)->m);
        free(h);
        return S3E_RESULT_SUCCESS;
    }
    if (h && *(uint32_t *)h == TAG_SEM) {
        pthread_cond_destroy(&((sf_sem *)h)->c);
        pthread_mutex_destroy(&((sf_sem *)h)->m);
        free(h);
        return S3E_RESULT_SUCCESS;
    }
    if (!t || t->tag != TAG_THREAD) {
        LOGW("s3eThread[%d](%p): not a thread handle", slot, h);
        return S3E_RESULT_ERROR;
    }
    if (t != &s_main && t != thr_current()) {
        pthread_join(t->th, NULL);
        LOGI("s3eThread[%d](%p): joined", slot, h);
    }
    return S3E_RESULT_SUCCESS;
}
static int32_t thr_slot8(void *h) { return thr_handle_op(8, h); }
static int32_t thr_slot11(void *h) { return thr_handle_op(11, h); }

/* 9 */
static void *lock_create(void)
{
    LOGV("LockCreate");
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
static int32_t lock_acquire(sf_lock *l, int32_t timeout_ms)
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
static int32_t sem_wait_ms(sf_sem *s, int32_t timeout_ms)
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
