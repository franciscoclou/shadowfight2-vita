/* s3eFile: stdio-backed, with Marmalade's drive prefixes and user
 * filesystems.
 *
 *   rom://x  -> <data>/x        (read-only game data, the APK's assets/)
 *   ram://x  -> <data>/ram/x    (writable: saves, caches)
 *   raw://x  -> x               (host path)
 *   x        -> ram first, then rom (Marmalade's default search order)
 *
 * s3eFileAddUserFileSys is how Derbh (the .dz archive layer the game links
 * statically) makes archive members openable: every s3eFileOpen offers the
 * name to each registered user filesystem before touching the disk.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Error codes (s3eFileError): the subset the game can observe. */
enum {
    S3E_FILE_ERR_NONE = 0, S3E_FILE_ERR_PARAM = 1, S3E_FILE_ERR_TOO_MANY = 2,
    S3E_FILE_ERR_NOT_FOUND = 4, S3E_FILE_ERR_GENERIC = 8, S3E_FILE_ERR_EOF = 1004,
};
static int32_t s_err;

/* s3eFileUserFileSys, from the table the game builds at link 0x4a7528cc:
 * 9 callbacks the runtime requires to be non-NULL, then 2 optional words.
 * Slot roles were identified from the callees' code (docs/RE_NOTES.md). */
typedef struct sf_userfs {
    void    *(*open)(const char *name, const char *mode);
    uint32_t (*read)(void *buf, uint32_t size, uint32_t count, void *fh);
    int32_t  (*eof)(void *fh);
    int32_t  (*seek)(void *fh, int32_t off, int32_t origin);
    int32_t  (*tell)(void *fh);
    int32_t  (*close)(void *fh);
    void     *fn6, *fn7, *fn8;
    void     *opt9, *opt10;
} sf_userfs;

#define MAX_USERFS 4
static sf_userfs s_userfs[MAX_USERFS];
static int s_nuserfs;

typedef struct sf_file {
    int kind;                 /* 1 = stdio, 2 = user fs, 3 = memory */
    FILE *fp;
    const sf_userfs *ufs;
    void *uh;
    const uint8_t *mem;
    uint32_t mem_size, mem_pos;
    int eof;
} sf_file;

/* --- paths ------------------------------------------------------------- */

static void join(char *out, size_t n, const char *a, const char *b)
{
    while (*b == '/')
        b++;
    snprintf(out, n, "%s/%s", a, b);
}

static int is_writable_mode(const char *mode)
{
    return mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'));
}

/* Resolve a game path. For reads, unprefixed names try ram then rom. */
static void resolve(const char *name, char *out, size_t n, int for_write)
{
    char ram[300];
    struct stat st;

    snprintf(ram, sizeof(ram), "%s/ram", sf_data_dir);
    if (strncmp(name, "rom://", 6) == 0)
        join(out, n, sf_data_dir, name + 6);
    else if (strncmp(name, "ram://", 6) == 0)
        join(out, n, ram, name + 6);
    else if (strncmp(name, "raw://", 6) == 0)
        snprintf(out, n, "%s", name + 6);
    else {
        join(out, n, ram, name);
        if (!for_write && stat(out, &st) != 0)
            join(out, n, sf_data_dir, name);
    }
}

/* mkdir -p for the parent of a ram:// file about to be written. */
static void make_parents(const char *path)
{
    char tmp[512], *p;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
}

/* --- open/close -------------------------------------------------------- */

static sf_file *sf_s3eFileOpen(const char *name, const char *mode)
{
    char path[512];
    sf_file *f;
    int i;

    if (!name || !mode) {
        s_err = S3E_FILE_ERR_PARAM;
        return NULL;
    }
    /* User filesystems first, read-only opens only (Derbh is read-only). */
    if (!is_writable_mode(mode)) {
        for (i = s_nuserfs - 1; i >= 0; i--) {
            void *h = s_userfs[i].open(name, mode);
            if (h) {
                f = calloc(1, sizeof(*f));
                f->kind = 2;
                f->ufs = &s_userfs[i];
                f->uh = h;
                LOGV("s3eFileOpen(%s) -> userfs %d", name, i);
                return f;
            }
        }
    }

    resolve(name, path, sizeof(path), is_writable_mode(mode));
    if (is_writable_mode(mode))
        make_parents(path);
    {
        char m[8];
        snprintf(m, sizeof(m), "%s%s", mode, strchr(mode, 'b') ? "" : "b");
        FILE *fp = fopen(path, m);
        if (!fp) {
            s_err = S3E_FILE_ERR_NOT_FOUND;
            LOGI("s3eFileOpen(%s, %s) -> not found (%s)", name, mode, path);
            return NULL;
        }
        f = calloc(1, sizeof(*f));
        f->kind = 1;
        f->fp = fp;
    }
    LOGI("s3eFileOpen(%s, %s) -> %s", name, mode, path);
    return f;
}

static sf_file *sf_s3eFileOpenFromMemory(void *buf, uint32_t size)
{
    sf_file *f;
    if (!buf) {
        s_err = S3E_FILE_ERR_PARAM;
        return NULL;
    }
    f = calloc(1, sizeof(*f));
    f->kind = 3;
    f->mem = buf;
    f->mem_size = size;
    return f;
}

static int32_t sf_s3eFileClose(sf_file *f)
{
    if (!f) {
        s_err = S3E_FILE_ERR_PARAM;
        return S3E_RESULT_ERROR;
    }
    if (f->kind == 1)
        fclose(f->fp);
    else if (f->kind == 2)
        f->ufs->close(f->uh);
    free(f);
    return S3E_RESULT_SUCCESS;
}

/* --- io ---------------------------------------------------------------- */

static uint32_t sf_s3eFileRead(void *buf, uint32_t size, uint32_t count, sf_file *f)
{
    uint32_t n;
    if (!f || !buf || !size)
        return 0;
    switch (f->kind) {
    case 1:
        n = (uint32_t)fread(buf, size, count, f->fp);
        f->eof = n < count;
        return n;
    case 2:
        return f->ufs->read(buf, size, count, f->uh);
    default: {
        uint32_t avail = (f->mem_size - f->mem_pos) / size;
        n = count < avail ? count : avail;
        memcpy(buf, f->mem + f->mem_pos, n * size);
        f->mem_pos += n * size;
        f->eof = n < count;
        return n;
    }
    }
}

static uint32_t sf_s3eFileWrite(const void *buf, uint32_t size, uint32_t count, sf_file *f)
{
    if (!f || f->kind != 1)
        return 0;
    return (uint32_t)fwrite(buf, size, count, f->fp);
}

static int32_t sf_s3eFileSeek(sf_file *f, int32_t off, int32_t origin)
{
    int whence = origin == 1 ? SEEK_CUR : origin == 2 ? SEEK_END : SEEK_SET;
    if (!f)
        return S3E_RESULT_ERROR;
    f->eof = 0;
    switch (f->kind) {
    case 1:
        return fseek(f->fp, off, whence) == 0 ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
    case 2:
        return f->ufs->seek(f->uh, off, origin);
    default: {
        int64_t pos = whence == SEEK_SET ? off : whence == SEEK_CUR ? (int64_t)f->mem_pos + off
                                                                    : (int64_t)f->mem_size + off;
        if (pos < 0 || pos > f->mem_size)
            return S3E_RESULT_ERROR;
        f->mem_pos = (uint32_t)pos;
        return S3E_RESULT_SUCCESS;
    }
    }
}

static int32_t sf_s3eFileTell(sf_file *f)
{
    if (!f)
        return -1;
    switch (f->kind) {
    case 1: return (int32_t)ftell(f->fp);
    case 2: return f->ufs->tell(f->uh);
    default: return (int32_t)f->mem_pos;
    }
}

static int32_t sf_s3eFileGetSize(sf_file *f)
{
    int32_t cur, size;
    if (!f)
        return -1;
    if (f->kind == 3)
        return (int32_t)f->mem_size;
    cur = sf_s3eFileTell(f);
    sf_s3eFileSeek(f, 0, 2);
    size = sf_s3eFileTell(f);
    sf_s3eFileSeek(f, cur, 0);
    return size;
}

static int32_t sf_s3eFileEOF(sf_file *f)
{
    if (!f)
        return 1;
    if (f->kind == 2)
        return f->ufs->eof(f->uh);
    if (f->kind == 1)
        return f->eof || feof(f->fp);
    return f->mem_pos >= f->mem_size;
}

static int32_t sf_s3eFileGetChar(sf_file *f)
{
    uint8_t c;
    return sf_s3eFileRead(&c, 1, 1, f) == 1 ? c : -1;
}

static char *sf_s3eFileReadString(char *buf, uint32_t max, sf_file *f)
{
    uint32_t i = 0;
    int c = -1;
    if (!buf || !max || !f)
        return NULL;
    while (i + 1 < max && (c = sf_s3eFileGetChar(f)) >= 0) {
        buf[i++] = (char)c;
        if (c == '\n')
            break;
    }
    buf[i] = 0;
    return i ? buf : NULL;
}

/* --- names ------------------------------------------------------------- */

static int32_t sf_s3eFileCheckExists(const char *name)
{
    char path[512];
    struct stat st;
    sf_file *f;

    if (!name)
        return 0;
    resolve(name, path, sizeof(path), 0);
    if (stat(path, &st) == 0)
        return 1;
    f = s_nuserfs ? sf_s3eFileOpen(name, "rb") : NULL;
    if (f) {
        sf_s3eFileClose(f);
        return 1;
    }
    return 0;
}

/* s3eFileGetFileInt(path, prop): from libs3e_android.so --
 * 0 is-directory, 1 is-file, 2 writable, 3 user-dir?, 4 size,
 * 5 last-modified (ms), 6/8 user-fs queries. Returns int64. */
static int64_t sf_s3eFileGetFileInt(const char *name, int32_t prop)
{
    char path[512];
    struct stat st;

    if (!name)
        return -1;
    resolve(name, path, sizeof(path), 0);
    if (stat(path, &st) != 0) {
        if (prop == 0 || prop == 1 || prop == 2)
            return 0;
        return -1;
    }
    switch (prop) {
    case 0: return S_ISDIR(st.st_mode) ? 1 : 0;
    case 1: return S_ISREG(st.st_mode) ? 1 : 0;
    case 2: return strncmp(name, "rom://", 6) != 0;
    case 3: return 0;
    case 4: return (int64_t)st.st_size;
    case 5: return (int64_t)st.st_mtime * 1000;
    default:
        LOGW("s3eFileGetFileInt(%s, %d) unknown", name, (int)prop);
        return -1;
    }
}

static int32_t sf_s3eFileGetError(void) { return s_err; }

static int32_t sf_s3eFileDelete(const char *name)
{
    char path[512];
    resolve(name, path, sizeof(path), 1);
    return remove(path) == 0 ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
}

static int32_t sf_s3eFileRename(const char *from, const char *to)
{
    char a[512], b[512];
    resolve(from, a, sizeof(a), 0);
    resolve(to, b, sizeof(b), 1);
    remove(b);
    return rename(a, b) == 0 ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
}

static int32_t sf_s3eFileMakeDirectory(const char *name)
{
    char path[512];
    resolve(name, path, sizeof(path), 1);
    make_parents(path);
    mkdir(path, 0777);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eFileAddUserFileSys(const sf_userfs *fs)
{
    if (!fs || s_nuserfs >= MAX_USERFS) {
        s_err = fs ? S3E_FILE_ERR_TOO_MANY : S3E_FILE_ERR_PARAM;
        return S3E_RESULT_ERROR;
    }
    s_userfs[s_nuserfs++] = *fs;
    LOGI("s3eFileAddUserFileSys #%d (open=%p)", s_nuserfs, (void *)fs->open);
    return S3E_RESULT_SUCCESS;
}

/* --- directory listing ------------------------------------------------- */

typedef struct sf_list {
    char *names;      /* NUL-separated, double-NUL terminated */
    char *next;
} sf_list;

static sf_list *sf_s3eFileListDirectory(const char *name)
{
    char path[512];
    DIR *d;
    struct dirent *e;
    size_t cap = 4096, len = 0;
    sf_list *l;

    resolve(name && *name ? name : ".", path, sizeof(path), 0);
    d = opendir(path);
    if (!d) {
        s_err = S3E_FILE_ERR_NOT_FOUND;
        return NULL;
    }
    l = calloc(1, sizeof(*l));
    l->names = malloc(cap);
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name) + 1;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (len + n + 1 > cap)
            l->names = realloc(l->names, cap *= 2);
        memcpy(l->names + len, e->d_name, n);
        len += n;
    }
    closedir(d);
    l->names[len] = 0;
    l->next = l->names;
    return l;
}

static int32_t sf_s3eFileListNext(sf_list *l, char *out)
{
    if (!l || !out || !*l->next)
        return S3E_RESULT_ERROR;
    strcpy(out, l->next);
    l->next += strlen(l->next) + 1;
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eFileListClose(sf_list *l)
{
    if (l) {
        free(l->names);
        free(l);
    }
    return S3E_RESULT_SUCCESS;
}

const sf_export sf_exports_file[] = {
    { "s3eFileOpen", (void *)sf_s3eFileOpen },
    { "s3eFileOpenFromMemory", (void *)sf_s3eFileOpenFromMemory },
    { "s3eFileClose", (void *)sf_s3eFileClose },
    { "s3eFileRead", (void *)sf_s3eFileRead },
    { "s3eFileWrite", (void *)sf_s3eFileWrite },
    { "s3eFileSeek", (void *)sf_s3eFileSeek },
    { "s3eFileTell", (void *)sf_s3eFileTell },
    { "s3eFileGetSize", (void *)sf_s3eFileGetSize },
    { "s3eFileEOF", (void *)sf_s3eFileEOF },
    { "s3eFileGetChar", (void *)sf_s3eFileGetChar },
    { "s3eFileReadString", (void *)sf_s3eFileReadString },
    { "s3eFileCheckExists", (void *)sf_s3eFileCheckExists },
    { "s3eFileGetFileInt", (void *)sf_s3eFileGetFileInt },
    { "s3eFileGetError", (void *)sf_s3eFileGetError },
    { "s3eFileDelete", (void *)sf_s3eFileDelete },
    { "s3eFileRename", (void *)sf_s3eFileRename },
    { "s3eFileMakeDirectory", (void *)sf_s3eFileMakeDirectory },
    { "s3eFileAddUserFileSys", (void *)sf_s3eFileAddUserFileSys },
    { "s3eFileListDirectory", (void *)sf_s3eFileListDirectory },
    { "s3eFileListNext", (void *)sf_s3eFileListNext },
    { "s3eFileListClose", (void *)sf_s3eFileListClose },
    { NULL, NULL }
};
