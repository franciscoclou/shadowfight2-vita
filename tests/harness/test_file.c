/* Unit test for src/s3e/file.c's buffered reader: random seeks and reads of
 * every shape against an in-memory model of the same file, through the
 * s3eFile API the game uses. Run with: make -C tests/harness test. */
#include "s3e/s3e_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char sf_data_dir[256] = "/tmp";
void sf_log_write(const char *l, const char *f, int n, const char *fmt, ...) {}
uint64_t sf_plat_time_us(void) { return 0; }
void sf_prof_add(int cat, uint64_t us, uint32_t bytes) {}
void sf_hud_file_opened(const char *name) {}
const uint8_t *sf_ui_asset(const char *path, uint32_t *size) { return NULL; }
int sf_thread_is_main(void) { return 1; }

extern const sf_export sf_exports_file[];
static void *fn(const char *name)
{
    const sf_export *e;
    for (e = sf_exports_file; e->name; e++)
        if (!strcmp(e->name, name))
            return e->fn;
    fprintf(stderr, "no %s\n", name);
    exit(2);
}

#define SIZE (300 * 1024 + 123)

int main(void)
{
    void *(*open_)(const char *, const char *) = fn("s3eFileOpen");
    uint32_t (*read_)(void *, uint32_t, uint32_t, void *) = fn("s3eFileRead");
    int32_t (*seek_)(void *, int32_t, int32_t) = fn("s3eFileSeek");
    int32_t (*tell_)(void *) = fn("s3eFileTell");
    int32_t (*size_)(void *) = fn("s3eFileGetSize");
    int32_t (*eof_)(void *) = fn("s3eFileEOF");
    int32_t (*getc_)(void *) = fn("s3eFileGetChar");
    int32_t (*close_)(void *) = fn("s3eFileClose");
    static uint8_t data[SIZE], got[SIZE + 64];
    long pos = 0;
    int i, fails = 0;
    FILE *f = fopen("/tmp/sf_test_file.bin", "wb");
    void *h;

    srand(1234);
    for (i = 0; i < SIZE; i++)
        data[i] = (uint8_t)rand();
    fwrite(data, 1, SIZE, f);
    fclose(f);

    h = open_("raw:///tmp/sf_test_file.bin", "rb");
    if (!h || size_(h) != SIZE) {
        printf("FAIL  open/size\n");
        return 1;
    }
    for (i = 0; i < 20000 && fails < 10; i++) {
        int op = rand() % 10;
        if (op < 3) {                       /* seek */
            int origin = rand() % 3;
            long off = rand() % (SIZE + 2000) - (origin ? 1000 : 0);
            long target = origin == 0 ? off : origin == 1 ? pos + off : SIZE + off - SIZE / 2;
            if (origin == 2)
                off -= SIZE / 2;
            if (target < 0) {
                if (seek_(h, (int32_t)off, origin) == 0) { printf("FAIL  negative seek accepted\n"); fails++; }
                continue;
            }
            if (seek_(h, (int32_t)off, origin) != 0) { printf("FAIL  seek %ld/%d\n", off, origin); fails++; }
            pos = target;
        } else if (op < 8) {                /* read */
            uint32_t size = 1 + rand() % 4;
            uint32_t count = rand() % 4 ? 1 + rand() % 64 : 1 + rand() % (40 * 1024);
            long avail = pos < SIZE ? SIZE - pos : 0;
            uint32_t want = (uint32_t)(avail / size < count ? avail / size : count);
            uint32_t n = read_(got, size, count, h);
            if (n != want || (n && memcmp(got, data + pos, (size_t)n * size))) {
                printf("FAIL  read %ux%u at %ld: got %u want %u\n", size, count, pos, n, want);
                fails++;
            }
            pos += (long)n * size;
        } else if (op == 8) {               /* getchar */
            int c = getc_(h), want = pos < SIZE ? data[pos] : -1;
            if (c != want) { printf("FAIL  getchar at %ld\n", pos); fails++; }
            if (pos < SIZE)
                pos++;
        } else if (tell_(h) != pos) {
            printf("FAIL  tell %d want %ld\n", tell_(h), pos);
            fails++;
        }
    }
    seek_(h, 0, 2);
    if (!(read_(got, 1, 1, h) == 0 && eof_(h))) { printf("FAIL  eof at end\n"); fails++; }
    close_(h);
    printf(fails ? "FAILURES: %d\n" : "buffered reader: 20000 random ops, all passed\n", fails);
    return fails != 0;
}
