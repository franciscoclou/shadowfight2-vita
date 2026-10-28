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
void sf_plat_worker_thread(void) {}
int sf_quests_wants(const char *name) { return 0; }
void *sf_quests_load(const char *name, uint32_t *size) { return NULL; }
void sf_prof_add(int cat, uint64_t us, uint32_t bytes) {}
void sf_hud_file_opened(const char *name) {}
const uint8_t *sf_ui_asset(const char *path, uint32_t *size) { return NULL; }

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

/* Read a file back through s3eFile, as the game would right after saving. */
static int read_back(const char *name, char *out, int cap)
{
    void *(*open_)(const char *, const char *) = fn("s3eFileOpen");
    uint32_t (*read_)(void *, uint32_t, uint32_t, void *) = fn("s3eFileRead");
    int32_t (*close_)(void *) = fn("s3eFileClose");
    void *h = open_(name, "rb");
    int n;
    if (!h)
        return -1;
    n = (int)read_(out, 1, (uint32_t)cap, h);
    close_(h);
    return n;
}

/* Write-behind: every read, stat and delete must see the writes queued
 * before it, many times over so the writer thread is caught mid-job. */
static int test_deferred(void)
{
    void *(*open_)(const char *, const char *) = fn("s3eFileOpen");
    uint32_t (*write_)(const void *, uint32_t, uint32_t, void *) = fn("s3eFileWrite");
    int32_t (*seek_)(void *, int32_t, int32_t) = fn("s3eFileSeek");
    int32_t (*tell_)(void *) = fn("s3eFileTell");
    int32_t (*close_)(void *) = fn("s3eFileClose");
    int32_t (*exists_)(const char *) = fn("s3eFileCheckExists");
    int32_t (*delete_)(const char *) = fn("s3eFileDelete");
    int64_t (*fileint_)(const char *, int32_t) = fn("s3eFileGetFileInt");
    static char big[200 * 1024], got[sizeof(big) + 16];
    char want[64];
    int i, fails = 0;
    void *h;

    for (i = 0; i < (int)sizeof(big); i++)
        big[i] = (char)('a' + i % 26);
    for (i = 0; i < 200 && fails < 10; i++) {
        int n;
        big[0] = (char)('0' + i % 10);
        h = open_("ram://wb/save.xml", "wb");
        write_(big, 1, sizeof(big) - (size_t)i, h);
        close_(h);
        if (fileint_("ram://wb/save.xml", 4) != (int64_t)(sizeof(big) - (size_t)i)) {
            printf("FAIL  size after deferred write %d\n", i); fails++;
        }
        n = read_back("ram://wb/save.xml", got, sizeof(got));
        if (n != (int)(sizeof(big) - (size_t)i) || memcmp(got, big, (size_t)n)) {
            printf("FAIL  read back after deferred write %d (%d bytes)\n", i, n); fails++;
        }
    }
    h = open_("ram://wb/log.txt", "wb");
    write_("one ", 1, 4, h);
    close_(h);
    h = open_("ram://wb/log.txt", "ab");
    write_("two ", 1, 4, h);
    if (tell_(h) != 8) { printf("FAIL  append tell %d\n", tell_(h)); fails++; }
    close_(h);
    h = open_("ram://wb/log.txt", "ab");
    write_("three", 1, 5, h);
    close_(h);
    if (read_back("ram://wb/log.txt", got, sizeof(got)) != 13 || memcmp(got, "one two three", 13)) {
        printf("FAIL  appends\n"); fails++;
    }
    h = open_("ram://wb/seek.bin", "wb");
    write_("abcdef", 1, 6, h);
    seek_(h, 2, 0);
    write_("XY", 1, 2, h);
    seek_(h, 0, 2);
    write_("!", 1, 1, h);
    close_(h);
    snprintf(want, sizeof(want), "abXYef!");
    if (read_back("ram://wb/seek.bin", got, sizeof(got)) != 7 || memcmp(got, want, 7)) {
        printf("FAIL  seek inside a deferred write\n"); fails++;
    }
    if (delete_("ram://wb/seek.bin") != 0 || exists_("ram://wb/seek.bin")) {
        printf("FAIL  delete of a written file\n"); fails++;
    }
    if (delete_("ram://wb/never.bin") == 0) {
        printf("FAIL  delete of a missing file succeeded\n"); fails++;
    }
    printf(fails ? "FAILURES: %d\n" : "deferred writes: all passed\n", fails);
    return fails != 0;
}

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
    return test_deferred() || fails != 0;
}
