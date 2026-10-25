/* Stress test for src/s3e/memory.c: two threads doing random
 * malloc/realloc/free of small and large blocks through s3eMallocBase &co,
 * checking 16-byte alignment and that every block keeps its contents.
 * Run with: make -C tests/harness test. */
#include "s3e/s3e_internal.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int _newlib_heap_size_user = 200 * 1024 * 1024;
void sf_log_write(const char *l, const char *f, int n, const char *fmt, ...)
{ va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); }
uint64_t sf_plat_time_us(void) { return 0; }
void sf_prof_add(int cat, uint64_t us, uint32_t bytes) {}

extern const sf_export sf_exports_memory[];
static void *(*m_)(uint32_t);
static void *(*r_)(void *, uint32_t);
static void (*f_)(void *);

#define SLOTS 2000
static int s_fail;

static uint32_t rnd_size(unsigned *seed)
{
    int k = rand_r(seed) % 100;
    return k < 70 ? rand_r(seed) % 128 : k < 95 ? rand_r(seed) % 1100 : rand_r(seed) % 70000;
}

static void fill(uint8_t *p, uint32_t n, uint8_t tag) { for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(tag + i); }
static int check(const uint8_t *p, uint32_t n, uint8_t tag)
{ for (uint32_t i = 0; i < n; i++) if (p[i] != (uint8_t)(tag + i)) return 0; return 1; }

static void *worker(void *arg)
{
    unsigned seed = (unsigned)(uintptr_t)arg;
    static __thread uint8_t *ptr[SLOTS];
    static __thread uint32_t len[SLOTS];
    static __thread uint8_t tag[SLOTS];
    int i;
    for (i = 0; i < 300000 && !s_fail; i++) {
        int s = rand_r(&seed) % SLOTS, op = rand_r(&seed) % 3;
        if (ptr[s] && !check(ptr[s], len[s], tag[s])) { fprintf(stderr, "corrupt block\n"); s_fail = 1; break; }
        if (!ptr[s] || op == 0) {
            if (ptr[s]) f_(ptr[s]);
            len[s] = rnd_size(&seed); ptr[s] = m_(len[s]); tag[s] = (uint8_t)rand_r(&seed);
            if (!ptr[s] || ((uintptr_t)ptr[s] & 15)) { fprintf(stderr, "bad malloc\n"); s_fail = 1; break; }
            fill(ptr[s], len[s], tag[s]);
        } else if (op == 1) {
            uint32_t n = rnd_size(&seed), keep = n < len[s] ? n : len[s];
            uint8_t *p = r_(ptr[s], n);
            if (!p || ((uintptr_t)p & 15) || !check(p, keep, tag[s])) { fprintf(stderr, "bad realloc\n"); s_fail = 1; break; }
            ptr[s] = p; len[s] = n; fill(p, n, tag[s]);
        } else {
            f_(ptr[s]); ptr[s] = NULL;
        }
    }
    for (i = 0; i < SLOTS; i++) if (ptr[i]) f_(ptr[i]);
    return NULL;
}

int main(void)
{
    pthread_t a, b;
    m_ = (void *(*)(uint32_t))sf_exports_memory[0].fn;
    r_ = (void *(*)(void *, uint32_t))sf_exports_memory[1].fn;
    f_ = (void (*)(void *))sf_exports_memory[2].fn;
    pthread_create(&a, NULL, worker, (void *)1);
    pthread_create(&b, NULL, worker, (void *)2);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf(s_fail ? "FAILURES in allocator\n" : "allocator: 600000 random ops on 2 threads, all passed\n");
    return s_fail;
}
