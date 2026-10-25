/* Hiding the on-screen fight controls, live.
 *
 * The controls are drawn from two texture atlases, and everything else the
 * fight HUD needs (pause button, magic and ranged meters) is in the same
 * button atlas. So instead of touching game code we edit the atlases on the
 * GPU: when the game opens one of them, its .plist gives the atlas size and
 * the frame rectangles; the next glTexImage2D of that size is the atlas
 * upload, whose texture id and original pixels we keep (only if it comes
 * from the thread that opened the file, within PENDING_US, so an unrelated
 * texture of the same size is never mistaken for it). Hiding uploads a
 * copy with the control frames cleared, showing re-uploads the original,
 * both with glTexSubImage2D into the same texture, so toggling is instant.
 *
 * The same texture ids tell when a fight is on screen: sf_hud_in_fight()
 * is true while one of the atlases was bound during the last couple of
 * frames, which input.c uses to keep the Vita's buttons out of menus.
 *
 * Erased: the punch/kick/throw/magic/charge discs and the whole joystick.
 * Kept: the pause button, the magic charge ring and full icon, the ranged
 * ring and full icon, and the tutorial highlight.
 */
#include "s3e/s3e_internal.h"
#include "platform/platform.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum, GLuint;
typedef int GLint, GLsizei;

#define GL_TEXTURE_2D            0x0DE1
#define GL_TEXTURE_BINDING_2D    0x8069
#define GL_UNSIGNED_BYTE         0x1401
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#define GL_RGBA                  0x1908
#define GL_LUMINANCE_ALPHA       0x190A

#define MAX_RECTS   32
#define MAX_ATLASES 4
#define PENDING_US  (2 * 1000000ull)

static const char *const s_buttons_erase[] = {
    "btn_charge_action.png", "btn_charge_normal.png", "btn_kick_action.png",
    "btn_kick_normal.png", "btn_magic_action.png", "btn_magic_normal.png",
    "btn_punch_action.png", "btn_punch_normal.png", "btn_throw_action.png",
    "btn_throw_normal.png", NULL,
};

static const struct {
    const char *suffix;
    const char *const *erase;      /* NULL: erase every frame */
} s_atlases[] = {
    { "/textures/buttons/fight/batchButtonsFight.png", s_buttons_erase },
    { "/textures/joystick/batchJoystick.png", NULL },
};

typedef struct {
    int w, h, nrects;
    int rect[MAX_RECTS][4];        /* x, y, w, h in texture pixels */
    pthread_t opener;              /* pending only: who opened the image */
    uint64_t opened_us;
} sf_layout;

typedef struct {
    int used;
    GLuint tex;
    GLenum fmt, type;
    int bpp;
    uint8_t *orig;
    sf_layout lay;
} sf_atlas;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static sf_layout s_pending[MAX_ATLASES];
static int s_npending;
static sf_atlas s_atlas[MAX_ATLASES];

/* --- plist parsing ------------------------------------------------------- */

static int in_list(const char *name, size_t len, const char *const *list)
{
    if (!list)
        return 1;
    for (; *list; list++)
        if (strlen(*list) == len && !strncmp(*list, name, len))
            return 1;
    return 0;
}

/* TexturePacker format-2 plist: <key>frames</key><dict> of
 * <key>NAME</key><dict>...<key>frame</key><string>{{x,y},{w,h}}</string>
 * ...<key>rotated</key><true/>...</dict>, then metadata with
 * <key>size</key><string>{W,H}</string>. */
static int parse_plist(const char *text, const char *const *erase, sf_layout *lay)
{
    const char *p = strstr(text, "<key>frames</key>"), *meta = strstr(text, "<key>metadata</key>");
    const char *sz;

    memset(lay, 0, sizeof(*lay));
    if (!p || !meta || !(sz = strstr(meta, "<key>size</key>")) ||
        sscanf(strstr(sz, "<string>") ? strstr(sz, "<string>") + 8 : "", "{%d,%d}", &lay->w, &lay->h) != 2)
        return -1;
    p = strstr(p, "<dict>");
    while (p && (p = strstr(p, "<key>")) && p < meta) {
        const char *name = p + 5, *end = strstr(name, "</key>"), *fr, *dict_end, *rot;
        int x, y, w, h;
        if (!end)
            break;
        dict_end = strstr(end, "</dict>");
        fr = strstr(end, "<key>frame</key>");
        if (!dict_end || !fr || fr > dict_end)
            break;
        if (sscanf(strstr(fr, "<string>") + 8, "{{%d,%d},{%d,%d}}", &x, &y, &w, &h) == 4 &&
            in_list(name, (size_t)(end - name), erase) && lay->nrects < MAX_RECTS) {
            rot = strstr(fr, "<key>rotated</key>");
            if (rot && rot < dict_end) {
                rot += strlen("<key>rotated</key>");
                while (*rot == ' ' || *rot == '\t' || *rot == '\r' || *rot == '\n')
                    rot++;
            }
            if (rot && rot < dict_end && !strncmp(rot, "<true/>", 7)) {
                int t = w; w = h; h = t;
            }
            lay->rect[lay->nrects][0] = x;
            lay->rect[lay->nrects][1] = y;
            lay->rect[lay->nrects][2] = w;
            lay->rect[lay->nrects][3] = h;
            lay->nrects++;
        }
        p = dict_end + 7;
    }
    return lay->nrects ? 0 : -1;
}

/* --- hooks --------------------------------------------------------------- */

void sf_hud_file_opened(const char *name)
{
    char norm[400], plist_name[400], *p;
    size_t len, i;
    sf_layout lay;
    char *text;
    uint32_t size;

    snprintf(norm, sizeof(norm), "%s", name);
    for (p = norm; *p; p++)
        if (*p == '\\')
            *p = '/';
    len = strlen(norm);
    for (i = 0; i < sizeof(s_atlases) / sizeof(s_atlases[0]); i++) {
        size_t sl = strlen(s_atlases[i].suffix);
        if (len > sl && !strcmp(norm + len - sl, s_atlases[i].suffix))
            break;
    }
    if (i == sizeof(s_atlases) / sizeof(s_atlases[0]))
        return;
    snprintf(plist_name, sizeof(plist_name), "%.*s.plist", (int)(len - 4), norm);
    if (!(text = sf_file_load(plist_name, &size)))
        return;
    text = realloc(text, size + 1);
    text[size] = 0;
    if (parse_plist(text, s_atlases[i].erase, &lay) == 0) {
        lay.opener = pthread_self();
        lay.opened_us = sf_plat_time_us();
        pthread_mutex_lock(&s_lock);
        if (s_npending < MAX_ATLASES)
            s_pending[s_npending++] = lay;
        pthread_mutex_unlock(&s_lock);
        LOGI("hud: %s is a %dx%d atlas, %d control frames", norm, lay.w, lay.h, lay.nrects);
    }
    free(text);
}

static int bytes_per_pixel(GLenum fmt, GLenum type)
{
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1)
        return 2;
    if (type == GL_UNSIGNED_BYTE && fmt == GL_RGBA)
        return 4;
    if (type == GL_UNSIGNED_BYTE && fmt == GL_LUMINANCE_ALPHA)
        return 2;
    return 0;   /* no alpha to clear */
}

static void erase(const sf_atlas *a, uint8_t *px)
{
    int r, y;
    for (r = 0; r < a->lay.nrects; r++) {
        int x0 = a->lay.rect[r][0], y0 = a->lay.rect[r][1];
        int w = a->lay.rect[r][2], h = a->lay.rect[r][3];
        if (x0 < 0 || y0 < 0 || x0 + w > a->lay.w || y0 + h > a->lay.h)
            continue;
        for (y = y0; y < y0 + h; y++)
            memset(px + ((size_t)y * a->lay.w + x0) * a->bpp, 0, (size_t)w * a->bpp);
    }
}

/* Called by the glTexImage2D wrapper before it uploads. Returns the pixels
 * to upload instead (owned here), or NULL to upload the game's own. */
const void *sf_hud_tex_image(GLint level, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *data)
{
    static void (*get_int)(GLenum, GLint *);
    static uint8_t *scratch;
    static size_t scratch_size;
    sf_atlas *a = NULL;
    GLint tex = 0;
    int i, bpp;
    size_t n;

    if (level != 0 || !data || !s_npending)
        return NULL;
    pthread_mutex_lock(&s_lock);
    for (i = 0; i < s_npending; i++) {
        if (sf_plat_time_us() - s_pending[i].opened_us > PENDING_US) {
            s_pending[i--] = s_pending[--s_npending];   /* stale: the image was cached */
            continue;
        }
        if (s_pending[i].w == w && s_pending[i].h == h &&
            pthread_equal(s_pending[i].opener, pthread_self()))
            break;
    }
    if (i == s_npending || !(bpp = bytes_per_pixel(fmt, type))) {
        pthread_mutex_unlock(&s_lock);
        return NULL;
    }
    if (!get_int)
        get_int = (void (*)(GLenum, GLint *))sf_plat_gl_proc("glGetIntegerv");
    get_int(GL_TEXTURE_BINDING_2D, &tex);
    for (int j = 0; j < MAX_ATLASES && !a; j++)
        if (!s_atlas[j].used || s_atlas[j].tex == (GLuint)tex)
            a = &s_atlas[j];
    if (a) {
        free(a->orig);
        n = (size_t)w * h * bpp;
        a->used = 1;
        a->tex = (GLuint)tex;
        a->fmt = fmt;
        a->type = type;
        a->bpp = bpp;
        a->lay = s_pending[i];
        a->orig = malloc(n);
        if (a->orig)
            memcpy(a->orig, data, n);
    }
    s_pending[i] = s_pending[--s_npending];
    pthread_mutex_unlock(&s_lock);
    if (!a || !a->orig)
        return NULL;
    LOGI("hud: atlas uploaded to texture %d (%dx%d, %d bytes/pixel)", (int)tex, (int)w, (int)h, bpp);
    if (sf_touch_controls_shown())
        return NULL;
    if (scratch_size < n) {
        free(scratch);
        scratch = malloc(n);
        scratch_size = scratch ? n : 0;
    }
    if (!scratch)
        return NULL;
    memcpy(scratch, a->orig, n);
    erase(a, scratch);
    return scratch;
}

void sf_hud_tex_deleted(GLsizei n, const GLuint *ids)
{
    int i, j;
    pthread_mutex_lock(&s_lock);
    for (i = 0; i < n; i++)
        for (j = 0; j < MAX_ATLASES; j++)
            if (s_atlas[j].used && s_atlas[j].tex == ids[i]) {
                free(s_atlas[j].orig);
                memset(&s_atlas[j], 0, sizeof(s_atlas[j]));
            }
    pthread_mutex_unlock(&s_lock);
}

/* Main (GL) thread only: re-upload every known atlas in the new state. */
void sf_hud_apply(int shown)
{
    static void (*get_int)(GLenum, GLint *);
    static void (*bind)(GLenum, GLuint);
    static void (*sub)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    GLint prev = 0;
    int j;

    if (!bind) {
        get_int = (void (*)(GLenum, GLint *))sf_plat_gl_proc("glGetIntegerv");
        bind = (void (*)(GLenum, GLuint))sf_plat_gl_proc("glBindTexture");
        sub = (void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))
              sf_plat_gl_proc("glTexSubImage2D");
        if (!get_int || !bind || !sub)
            return;
    }
    get_int(GL_TEXTURE_BINDING_2D, &prev);
    pthread_mutex_lock(&s_lock);
    for (j = 0; j < MAX_ATLASES; j++) {
        sf_atlas *a = &s_atlas[j];
        size_t n;
        uint8_t *px;
        if (!a->used || !a->orig)
            continue;
        n = (size_t)a->lay.w * a->lay.h * a->bpp;
        px = shown ? a->orig : malloc(n);
        if (!px)
            continue;
        if (!shown) {
            memcpy(px, a->orig, n);
            erase(a, px);
        }
        bind(GL_TEXTURE_2D, a->tex);
        sub(GL_TEXTURE_2D, 0, 0, 0, a->lay.w, a->lay.h, a->fmt, a->type, px);
        if (!shown)
            free(px);
    }
    pthread_mutex_unlock(&s_lock);
    bind(GL_TEXTURE_2D, (GLuint)prev);
}

/* --- fight detection ----------------------------------------------------- */

static unsigned s_frame, s_last_bound_frame;
static int s_ever_bound;

void sf_hud_tex_bound(uint32_t tex)
{
    int j;
    if (!tex)
        return;
    for (j = 0; j < MAX_ATLASES; j++)
        if (s_atlas[j].used && s_atlas[j].tex == tex) {
            s_last_bound_frame = s_frame;
            s_ever_bound = 1;
            return;
        }
}

void sf_hud_frame(void)
{
    s_frame++;
}

int sf_hud_in_fight(void)
{
    return s_ever_bound && s_frame - s_last_bound_frame <= 2;
}
