/* s3eFile: stdio-backed, with Marmalade's drive prefixes and user
 * filesystems.
 *
 *   rom://x  -> <data>/x        (read-only game data, the APK's assets/)
 *   ram://x  -> <data>/ram/x    (writable: saves, caches)
 *   raw://x  -> x               (host path)
 *   x        -> ram first, then rom (Marmalade's default search order)
 *
 * The port's own images (ui.c, textures/sfport/) are memory files.
 *
 * s3eFileAddUserFileSys is how Derbh (the .dz archive layer the game links
 * statically) makes archive members openable: every s3eFileOpen offers the
 * name to each registered user filesystem before touching the disk.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <dirent.h>
#include <pthread.h>
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

/* Derbh keeps one decoder state per archive (the file being decoded and
 * its buffer), and the game reads pack files from more than one thread:
 * a fight loads its PNGs on a loader thread while the main thread reads
 * the atlas .plists from the same pack. Unserialized, one thread's read
 * clobbered the other's, and getFileData's single whole-file read came
 * back short: the zone 2+ arenas got empty atlases and drew every piece
 * with the whole texture. Every user filesystem call holds this lock
 * (recursive: a size query is several calls). */
static pthread_mutex_t s_ufs_lock;
static pthread_once_t s_ufs_once = PTHREAD_ONCE_INIT;

static void ufs_lock_init(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_ufs_lock, &a);
    pthread_mutexattr_destroy(&a);
}

static void ufs_lock(void)
{
    pthread_once(&s_ufs_once, ufs_lock_init);
    pthread_mutex_lock(&s_ufs_lock);
}

static void ufs_unlock(void)
{
    pthread_mutex_unlock(&s_ufs_lock);
}

/* Read-only disk files (kind 4) get our own buffer instead of stdio's.
 * Derbh seeks before nearly every read of its archives and the XML parser
 * reads a byte at a time: through stdio each seek threw the buffer away
 * (so a few bytes cost a whole refill from the memory card) and each tiny
 * read paid newlib's full call path. Here a seek only moves pos, and a
 * read inside the buffered window is a memcpy. */
#define RBUF_SIZE (16 * 1024)

typedef struct sf_file {
    int kind;                 /* 1 = stdio (writable), 2 = user fs, 3 = memory, 4 = buffered read,
                                 5 = deferred write */
    int mem_owned;            /* kind 3: mem is ours to free */
    FILE *fp;
    uint8_t *rb;              /* kind 4: buffer holding file bytes [rb_off, rb_off + rb_len) */
    int64_t rb_off, pos, size, fpos;
    uint32_t rb_len;
    const sf_userfs *ufs;
    void *uh;
    const uint8_t *mem;
    uint32_t mem_size, mem_pos;
    int eof;
    int writing;
    char *wpath;              /* kind 5: the file's disk path */
    uint8_t *wb;              /* kind 5: everything written, [0, wb_len) */
    uint32_t wb_len, wb_cap, wb_pos;
    int append;
    int64_t base;             /* kind 5, append: size on disk before, -1 until asked */
} sf_file;

static void make_parents(const char *path);

/* --- write-behind ------------------------------------------------------ */

/* The game saves its profile on nearly every shop or dialog click, and on
 * the memory card creating or truncating a file and closing it again took
 * ~200 ms, all inside the frame that handled the click. A file opened only
 * to write ("w" or "a", no '+') is now kept in memory (kind 5) and handed
 * on close to a writer thread, which runs the queued jobs in order. Before
 * anything looks at a path that still has a job queued (an open for
 * reading, a stat, a listing, a rename) it waits for that job, so the game
 * never sees the disk behind its own writes. */

enum { JOB_WRITE, JOB_APPEND, JOB_DELETE, JOB_MKDIR };

typedef struct sf_job {
    struct sf_job *next;
    int type;
    char *path;
    uint8_t *data;
    uint32_t len;
} sf_job;

static pthread_mutex_t s_wq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_wq_cond = PTHREAD_COND_INITIALIZER;   /* a job queued or finished */
static sf_job *s_wq_head, *s_wq_tail, *s_wq_busy;
static int s_wq_started;

static void run_job(const sf_job *j)
{
    FILE *fp;
    switch (j->type) {
    case JOB_WRITE:
    case JOB_APPEND:
        make_parents(j->path);
        if (!(fp = fopen(j->path, j->type == JOB_APPEND ? "ab" : "wb"))) {
            LOGW("file: cannot write %s", j->path);
            break;
        }
        if (j->len && fwrite(j->data, 1, j->len, fp) != j->len)
            LOGW("file: short write to %s", j->path);
        fclose(fp);
        break;
    case JOB_DELETE:
        remove(j->path);
        break;
    case JOB_MKDIR:
        make_parents(j->path);
        mkdir(j->path, 0777);
        break;
    }
}

static void free_job(sf_job *j)
{
    free(j->path);
    free(j->data);
    free(j);
}

static void *writer_thread(void *arg)
{
    sf_plat_worker_thread();
    pthread_mutex_lock(&s_wq_lock);
    for (;;) {
        sf_job *j;
        while (!s_wq_head)
            pthread_cond_wait(&s_wq_cond, &s_wq_lock);
        j = s_wq_head;
        if (!(s_wq_head = j->next))
            s_wq_tail = NULL;
        s_wq_busy = j;
        pthread_mutex_unlock(&s_wq_lock);
        LOGV("file: writing %s (%u bytes)", j->path, (unsigned)j->len);
        run_job(j);
        pthread_mutex_lock(&s_wq_lock);
        s_wq_busy = NULL;
        free_job(j);
        pthread_cond_broadcast(&s_wq_cond);
    }
    return NULL;
}

/* Takes ownership of data. Without a writer thread the job runs here. */
static void wq_push(int type, const char *path, uint8_t *data, uint32_t len)
{
    sf_job *j = calloc(1, sizeof(*j));
    if (!j || !(j->path = strdup(path))) {
        sf_job tmp = { NULL, type, (char *)path, data, len };
        run_job(&tmp);
        free(data);
        free(j);
        return;
    }
    j->type = type;
    j->data = data;
    j->len = len;
    pthread_mutex_lock(&s_wq_lock);
    if (!s_wq_started) {
        pthread_attr_t a;
        pthread_t t;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, 64 * 1024);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        s_wq_started = pthread_create(&t, &a, writer_thread, NULL) == 0 ? 1 : -1;
        pthread_attr_destroy(&a);
        if (s_wq_started < 0)
            LOGW("file: no writer thread, writing synchronously");
    }
    if (s_wq_started < 0) {
        pthread_mutex_unlock(&s_wq_lock);
        run_job(j);
        free_job(j);
        return;
    }
    if (s_wq_tail)
        s_wq_tail->next = j;
    else
        s_wq_head = j;
    s_wq_tail = j;
    pthread_cond_broadcast(&s_wq_cond);
    pthread_mutex_unlock(&s_wq_lock);
}

/* path itself, or a file under the directory path. */
static int path_covers(const char *query, const char *job)
{
    size_t n = strlen(query);
    return !strncmp(job, query, n) && (job[n] == 0 || job[n] == '/');
}

static int wq_pending_locked(const char *path)
{
    const sf_job *j;
    if (s_wq_busy && (!path || path_covers(path, s_wq_busy->path)))
        return 1;
    for (j = s_wq_head; j; j = j->next)
        if (!path || path_covers(path, j->path))
            return 1;
    return 0;
}

static int wq_pending(const char *path)
{
    int r;
    if (s_wq_started <= 0)
        return 0;
    pthread_mutex_lock(&s_wq_lock);
    r = wq_pending_locked(path);
    pthread_mutex_unlock(&s_wq_lock);
    return r;
}

/* Wait until no queued job touches path (NULL: until the queue is empty). */
static void wq_wait(const char *path)
{
    uint64_t t0 = 0, us;
    if (s_wq_started <= 0)
        return;
    pthread_mutex_lock(&s_wq_lock);
    while (wq_pending_locked(path)) {
        if (!t0)
            t0 = sf_plat_time_us();
        pthread_cond_wait(&s_wq_cond, &s_wq_lock);
    }
    pthread_mutex_unlock(&s_wq_lock);
    if (!t0)
        return;
    us = sf_plat_time_us() - t0;
    sf_prof_add(SF_PROF_WRITE, us, 0);
    if (us >= 10000)
        LOGI("file: waited %u ms for the pending write of %s", (unsigned)(us / 1000), path ? path : "all");
}

/* Everything written so far is on the disk once this returns. */
void sf_file_sync(void)
{
    wq_wait(NULL);
}

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

/* Resolve a game path. For reads, unprefixed names try ram then rom.
 * Derbh spells paths Windows-style ("rom:\\assets\files.dz"), so a drive
 * is "rom:" followed by any run of '/' or '\', and every '\' is a '/'. */
static void resolve(const char *name, char *out, size_t n, int for_write)
{
    char ram[300], norm[400], *p;
    const char *rest = NULL;
    int drive = 0;   /* 0 none, 1 rom, 2 ram, 3 raw */
    struct stat st;

    snprintf(norm, sizeof(norm), "%s", name);
    for (p = norm; *p; p++)
        if (*p == '\\')
            *p = '/';
    if (!strncmp(norm, "rom:", 4)) drive = 1;
    else if (!strncmp(norm, "ram:", 4)) drive = 2;
    else if (!strncmp(norm, "raw:", 4)) drive = 3;
    if (drive) {
        rest = norm + 4;
        while (*rest == '/' && drive != 3)
            rest++;
        if (drive == 3 && !strncmp(rest, "//", 2))
            rest += 2;
    }

    snprintf(ram, sizeof(ram), "%s/ram", sf_data_dir);
    if (drive == 1)
        join(out, n, sf_data_dir, rest);
    else if (drive == 2)
        join(out, n, ram, rest);
    else if (drive == 3)
        snprintf(out, n, "%s", rest);
    else {
        join(out, n, ram, norm);
        if (!for_write) {
            wq_wait(out);
            if (stat(out, &st) != 0)
                join(out, n, sf_data_dir, norm);
        }
    }
    if (!for_write)
        wq_wait(out);
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

static sf_file *sf_s3eFileOpen_(const char *name, const char *mode);
static sf_file *open_raw(const char *name, const char *mode);

static sf_file *sf_s3eFileOpen(const char *name, const char *mode)
{
    uint64_t t0 = sf_plat_time_us();
    sf_file *f = sf_s3eFileOpen_(name, mode);
    /* Creating and truncating files is what is slow on the memory card,
     * so opens for writing are counted with the writes. */
    sf_prof_add(is_writable_mode(mode) ? SF_PROF_WRITE : SF_PROF_FOPEN, sf_plat_time_us() - t0, 0);
    return f;
}

/* Quest scripts are read edited (quests.c); everything else as is. */
static sf_file *sf_s3eFileOpen_(const char *name, const char *mode)
{
    if (name && mode && !is_writable_mode(mode) && sf_quests_wants(name)) {
        uint32_t size = 0;
        void *data = sf_quests_load(name, &size);
        sf_file *f;
        if (data && (f = calloc(1, sizeof(*f)))) {
            f->kind = 3;
            f->mem = data;
            f->mem_size = size;
            f->mem_owned = 1;
            return f;
        }
        free(data);
    }
    return open_raw(name, mode);
}

static sf_file *open_raw(const char *name, const char *mode)
{
    char path[512];
    sf_file *f;
    int i;

    if (!name || !mode) {
        s_err = S3E_FILE_ERR_PARAM;
        return NULL;
    }
    if (!is_writable_mode(mode)) {
        uint32_t size;
        const uint8_t *data = sf_ui_asset(name, &size);
        if (data) {
            f = calloc(1, sizeof(*f));
            f->kind = 3;
            f->mem = data;
            f->mem_size = size;
            return f;
        }
    }
    /* User filesystems first, read-only opens only (Derbh is read-only).
     * They get the mode as "rb": Derbh accepts exactly "r" or "rb"
     * (0x4a753158), and cocos2d's plist parser opens with "rt" (its
     * CCSAXParser::parse, 0x4a05da7c). Passed as is, every .plist inside a
     * content pack fell through to the disk and was missing: the zone 2+
     * arenas found none of their atlas frames and drew each piece with
     * the whole atlas. Marmalade hands user filesystems a plain mode too. */
    if (!is_writable_mode(mode)) {
        for (i = s_nuserfs - 1; i >= 0; i--) {
            void *h;
            ufs_lock();
            h = s_userfs[i].open(name, "rb");
            ufs_unlock();
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

    if (!is_writable_mode(mode))
        sf_hud_file_opened(name);
    resolve(name, path, sizeof(path), is_writable_mode(mode));
    if (is_writable_mode(mode) && !strchr(mode, '+') && !strchr(mode, 'r')) {
        f = calloc(1, sizeof(*f));
        if (f && (f->wpath = strdup(path))) {
            f->kind = 5;
            f->append = strchr(mode, 'a') != NULL;
            f->base = -1;
            LOGV("s3eFileOpen(%s, %s) -> %s (deferred)", name, mode, path);
            return f;
        }
        free(f);
    }
    if (is_writable_mode(mode)) {
        wq_wait(path);   /* read-write: the disk copy must be current */
        make_parents(path);
    }
    {
        char m[8];
        snprintf(m, sizeof(m), "%s%s", mode, strchr(mode, 'b') ? "" : "b");
        FILE *fp = fopen(path, m);
        if (!fp) {
            s_err = S3E_FILE_ERR_NOT_FOUND;
            LOGV("s3eFileOpen(%s, %s) -> not found (%s)", name, mode, path);
            return NULL;
        }
        f = calloc(1, sizeof(*f));
        f->fp = fp;
        if (is_writable_mode(mode)) {
            /* newlib's default buffer is tiny and the game writes its saves
             * a few bytes at a time: one I/O call per piece otherwise. */
            setvbuf(fp, NULL, _IOFBF, 64 * 1024);
            f->kind = 1;
            f->writing = 1;
        } else {
            setvbuf(fp, NULL, _IONBF, 0);
            f->kind = 4;
            f->rb = malloc(RBUF_SIZE);
            fseek(fp, 0, SEEK_END);
            f->size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
        }
    }
    LOGV("s3eFileOpen(%s, %s) -> %s", name, mode, path);
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
    if (f->kind == 5) {
        wq_push(f->append ? JOB_APPEND : JOB_WRITE, f->wpath, f->wb, f->wb_len);
        free(f->wpath);
    } else if (f->kind == 1 && f->writing) {
        uint64_t t0 = sf_plat_time_us();
        fclose(f->fp);
        sf_prof_add(SF_PROF_WRITE, sf_plat_time_us() - t0, 0);
    } else if (f->kind == 1 || f->kind == 4) {
        fclose(f->fp);
        free(f->rb);
    } else if (f->kind == 2) {
        ufs_lock();
        f->ufs->close(f->uh);
        ufs_unlock();
    } else if (f->kind == 3 && f->mem_owned) {
        free((void *)f->mem);
    }
    free(f);
    return S3E_RESULT_SUCCESS;
}

/* --- io ---------------------------------------------------------------- */

/* Read len bytes at f->pos through the buffer; returns bytes read. Only
 * real disk reads are timed (prof "fread"): timing every call cost more
 * than the calls themselves. */
static uint32_t buffered_read(sf_file *f, uint8_t *dst, uint32_t len)
{
    uint32_t done = 0;
    while (done < len && f->pos < f->size) {
        if (f->pos >= f->rb_off && f->pos < f->rb_off + f->rb_len) {
            uint32_t at = (uint32_t)(f->pos - f->rb_off), n = f->rb_len - at;
            if (n > len - done)
                n = len - done;
            memcpy(dst + done, f->rb + at, n);
            done += n;
            f->pos += n;
            continue;
        }
        {
            /* Large reads go straight to the destination. */
            int direct = len - done >= RBUF_SIZE || !f->rb;
            uint8_t *into = direct ? dst + done : f->rb;
            uint32_t want = direct ? len - done : RBUF_SIZE;
            uint64_t t0 = sf_plat_time_us();
            size_t got;
            if (f->fpos != f->pos && fseek(f->fp, (long)f->pos, SEEK_SET) != 0)
                break;
            got = fread(into, 1, want, f->fp);
            f->fpos = f->pos + (int64_t)got;
            sf_prof_add(SF_PROF_FREAD, sf_plat_time_us() - t0, (uint32_t)got);
            if (!got)
                break;
            if (direct) {
                done += (uint32_t)got;
                f->pos += (int64_t)got;
            } else {
                f->rb_off = f->pos;
                f->rb_len = (uint32_t)got;
            }
        }
    }
    return done;
}

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
    case 4: {
        int64_t start = f->pos;
        uint32_t got = buffered_read(f, buf, size * count);
        /* fread semantics: whole items only, a partial one is not consumed. */
        n = got / size;
        f->pos = start + (int64_t)n * size;
        f->eof = n < count;
        return n;
    }
    case 2:
        ufs_lock();
        n = f->ufs->read(buf, size, count, f->uh);
        ufs_unlock();
        return n;
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

static uint32_t deferred_write(sf_file *f, const void *buf, uint32_t size, uint32_t count)
{
    uint32_t len = size * count, at = f->append ? f->wb_len : f->wb_pos;
    if (!size || count > UINT32_MAX / size || at > UINT32_MAX - len)
        return 0;
    if (at + len > f->wb_cap) {
        uint32_t cap = f->wb_cap ? f->wb_cap : 16 * 1024;
        uint8_t *nb;
        while (cap < at + len)
            cap *= 2;
        if (!(nb = realloc(f->wb, cap)))
            return 0;
        f->wb = nb;
        f->wb_cap = cap;
    }
    if (at > f->wb_len)
        memset(f->wb + f->wb_len, 0, at - f->wb_len);   /* seeked past the end */
    memcpy(f->wb + at, buf, len);
    at += len;
    if (at > f->wb_len)
        f->wb_len = at;
    if (!f->append)
        f->wb_pos = at;
    return count;
}

/* Size of an append file's disk copy before this open, once all queued
 * writes to it have landed. */
static int64_t append_base(sf_file *f)
{
    struct stat st;
    if (f->base < 0) {
        wq_wait(f->wpath);
        f->base = stat(f->wpath, &st) == 0 ? (int64_t)st.st_size : 0;
    }
    return f->base;
}

static uint32_t sf_s3eFileWrite(const void *buf, uint32_t size, uint32_t count, sf_file *f)
{
    uint64_t t0;
    uint32_t n;
    if (f && f->kind == 5 && buf)
        return deferred_write(f, buf, size, count);
    if (!f || f->kind != 1)
        return 0;
    t0 = sf_plat_time_us();
    n = (uint32_t)fwrite(buf, size, count, f->fp);
    sf_prof_add(SF_PROF_WRITE, sf_plat_time_us() - t0, n * size);
    return n;
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
    case 5: {
        /* Appends always go to the end, as with stdio. */
        int64_t pos = whence == SEEK_SET ? off : whence == SEEK_CUR ? (int64_t)f->wb_pos + off
                                                                    : (int64_t)f->wb_len + off;
        if (pos < 0 || pos > UINT32_MAX)
            return S3E_RESULT_ERROR;
        f->wb_pos = (uint32_t)pos;
        return S3E_RESULT_SUCCESS;
    }
    case 4: {
        int64_t pos = whence == SEEK_SET ? off : whence == SEEK_CUR ? f->pos + off : f->size + off;
        if (pos < 0)
            return S3E_RESULT_ERROR;
        f->pos = pos;
        return S3E_RESULT_SUCCESS;
    }
    case 2: {
        int32_t r;
        ufs_lock();
        r = f->ufs->seek(f->uh, off, origin);
        ufs_unlock();
        return r;
    }
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
    case 4: return (int32_t)f->pos;
    case 5: return (int32_t)(f->append ? append_base(f) + f->wb_len : f->wb_pos);
    case 2: {
        int32_t r;
        ufs_lock();
        r = f->ufs->tell(f->uh);
        ufs_unlock();
        return r;
    }
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
    if (f->kind == 4)
        return (int32_t)f->size;
    if (f->kind == 5)
        return (int32_t)((f->append ? append_base(f) : 0) + f->wb_len);
    ufs_lock();   /* one atomic query, for the user filesystems' sake */
    cur = sf_s3eFileTell(f);
    sf_s3eFileSeek(f, 0, 2);
    size = sf_s3eFileTell(f);
    sf_s3eFileSeek(f, cur, 0);
    ufs_unlock();
    return size;
}

static int32_t sf_s3eFileEOF(sf_file *f)
{
    if (!f)
        return 1;
    if (f->kind == 2) {
        int32_t r;
        ufs_lock();
        r = f->ufs->eof(f->uh);
        ufs_unlock();
        return r;
    }
    if (f->kind == 1)
        return f->eof || feof(f->fp);
    if (f->kind == 4)
        return f->eof || f->pos >= f->size;
    if (f->kind == 5)
        return f->wb_pos >= f->wb_len;
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

    uint32_t size;

    if (!name)
        return 0;
    if (sf_ui_asset(name, &size))
        return 1;
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
    uint32_t size;

    if (!name)
        return -1;
    if (sf_ui_asset(name, &size))
        return prop == 1 ? 1 : prop == 4 ? (int64_t)size : prop == 5 ? 0 : prop <= 3 ? 0 : -1;
    if (prop == 4 && sf_quests_wants(name)) {
        sf_file *f = sf_s3eFileOpen_(name, "rb");
        if (f) {
            int64_t r = sf_s3eFileGetSize(f);
            sf_s3eFileClose(f);
            return r;
        }
    }
    /* A file inside a content pack exists only in the user filesystems
     * (Derbh), which answer first, as they do for opens. Without this
     * the game sized every pack file at nothing: the zone 2+ arenas read
     * their .plist atlases as empty, found none of their frames and drew
     * each piece with its whole atlas. */
    if (prop == 0 || prop == 1 || prop == 2 || prop == 4) {
        int i;
        for (i = s_nuserfs - 1; i >= 0; i--) {
            void *h;
            ufs_lock();
            h = s_userfs[i].open(name, "rb");
            if (h) {
                int64_t r = 0;
                if (prop == 1)
                    r = 1;
                else if (prop == 4) {
                    s_userfs[i].seek(h, 0, 2);
                    r = s_userfs[i].tell(h);
                }
                s_userfs[i].close(h);
                ufs_unlock();
                return r;
            }
            ufs_unlock();
        }
    }
    resolve(name, path, sizeof(path), 0);
    if (stat(path, &st) != 0) {
        if (prop == 0 || prop == 1 || prop == 2)
            return 0;
        return -1;
    }
    switch (prop) {
    case 0: return S_ISDIR(st.st_mode) ? 1 : 0;
    case 1: return S_ISREG(st.st_mode) ? 1 : 0;
    case 2: return strncmp(name, "rom:", 4) != 0;
    case 3: return 0;
    case 4: return (int64_t)st.st_size;
    case 5: return (int64_t)st.st_mtime * 1000;
    default:
        LOGW("s3eFileGetFileInt(%s, %d) unknown", name, (int)prop);
        return -1;
    }
}

static int32_t sf_s3eFileGetError(void) { return s_err; }

/* Queued like a write; only whether there is anything to delete is
 * answered now. */
static int32_t sf_s3eFileDelete(const char *name)
{
    char path[512];
    struct stat st;
    resolve(name, path, sizeof(path), 1);
    LOGV("s3eFileDelete(%s) -> %s", name, path);
    if (!wq_pending(path) && stat(path, &st) != 0)
        return S3E_RESULT_ERROR;
    wq_push(JOB_DELETE, path, NULL, 0);
    return S3E_RESULT_SUCCESS;
}

static int32_t sf_s3eFileRename(const char *from, const char *to)
{
    char a[512], b[512];
    resolve(from, a, sizeof(a), 0);
    resolve(to, b, sizeof(b), 1);
    wq_wait(b);
    remove(b);
    return rename(a, b) == 0 ? S3E_RESULT_SUCCESS : S3E_RESULT_ERROR;
}

static int32_t sf_s3eFileMakeDirectory(const char *name)
{
    char path[512];
    resolve(name, path, sizeof(path), 1);
    wq_push(JOB_MKDIR, path, NULL, 0);
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

static void *load(sf_file *f, uint32_t *size);

/* Read a whole game file (any drive or user filesystem) into a malloc'd
 * buffer, for shims that take a file name, like s3eAudioPlay. */
void *sf_file_load(const char *name, uint32_t *size)
{
    return load(sf_s3eFileOpen(name, "rt"), size);   /* as CCSAXParser opens */
}

/* The same, bypassing quests.c's edits. */
void *sf_file_load_raw(const char *name, uint32_t *size)
{
    return load(open_raw(name, "rb"), size);
}

static void *load(sf_file *f, uint32_t *size)
{
    int32_t n;
    void *buf = NULL;

    if (!f)
        return NULL;
    n = sf_s3eFileGetSize(f);
    if (n > 0 && (buf = malloc((size_t)n)) != NULL &&
        sf_s3eFileRead(buf, 1, (uint32_t)n, f) != (uint32_t)n) {
        free(buf);
        buf = NULL;
    }
    sf_s3eFileClose(f);
    if (buf && size)
        *size = (uint32_t)n;
    return buf;
}

/* Harness self-test (SF_FILE_TEST=<path>, see ui.c): the sequence
 * cocos2d's getFileData uses (0x4a05e650): open, size, one whole read. */
void sf_file_selftest(const char *name)
{
    sf_file *f = sf_s3eFileOpen(name, "rt");   /* as CCSAXParser opens */
    int32_t size, got;
    uint8_t *buf;
    if (!f) {
        LOGI("selftest %s: open failed", name);
        return;
    }
    size = sf_s3eFileGetSize(f);
    buf = calloc(1, size > 0 ? (size_t)size + 1 : 1);
    got = buf && size > 0 ? (int32_t)sf_s3eFileRead(buf, (uint32_t)size, 1, f) : -1;
    LOGI("selftest %s: kind %d, size %d, read(size, 1) = %d, tell %d, eof %d", name, f->kind, (int)size,
         (int)got, (int)sf_s3eFileTell(f), (int)sf_s3eFileEOF(f));
    if (buf && size > 0) {
        uint32_t n = 0;
        sf_s3eFileSeek(f, 0, 0);
        memset(buf, 0, (size_t)size);
        n = sf_s3eFileRead(buf, 1, (uint32_t)size, f);
        LOGI("selftest %s: read(1, size) = %u bytes, tail \"%s\"", name, (unsigned)n,
             n > 20 ? (char *)buf + n - 20 : (char *)buf);
    }
    free(buf);
    sf_s3eFileClose(f);
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
