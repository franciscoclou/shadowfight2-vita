/* EGL and OpenGL ES for the game, on top of vitaGL.
 *
 * The game drives EGL itself (IwGL): eglGetDisplay -> eglInitialize ->
 * eglGetConfigs/eglGetConfigAttrib to pick a config -> eglCreateWindowSurface
 * on s3eGLGetNativeWindow() -> eglCreateContext -> eglMakeCurrent, then
 * eglSwapBuffers once per frame. vitaGL has a single implicit context and
 * framebuffer, so EGL here is bookkeeping around vglInit/vglSwapBuffers.
 */
#include "s3e/s3e_internal.h"

#include "platform/platform.h"

#include <string.h>

/* --- softfp wrappers for every imported GL function with float args ----
 * Each resolves the native entry point once through the platform (vitaGL
 * on the Vita, Mesa on the harness) and forwards with the native ABI. A
 * name the platform lacks becomes a no-op (logged once). */

typedef unsigned int GLenum;
typedef int GLint;
typedef float GLfloat;

static void *gl_proc(const char *name)
{
    void *fn = sf_plat_gl_proc(name);
    if (!fn)
        LOGW("%s not provided by the platform GL; ignoring calls", name);
    return fn;
}

#define WRAP(n, params, args, types)                                   \
    static SOFTFP void w_##n params                                    \
    {                                                                  \
        static void (*fn) types;                                       \
        static int looked;                                             \
        if (!looked) { fn = (void (*) types)gl_proc(#n); looked = 1; } \
        if (fn) fn args;                                               \
    }
#define F GLfloat
WRAP(glAlphaFunc,     (GLenum a, F b), (a, b), (GLenum, F))
WRAP(glBlendColor,    (F a, F b, F c, F d), (a, b, c, d), (F, F, F, F))
WRAP(glClearColor,    (F a, F b, F c, F d), (a, b, c, d), (F, F, F, F))
WRAP(glClearDepthf,   (F a), (a), (F))
WRAP(glColor4f,       (F a, F b, F c, F d), (a, b, c, d), (F, F, F, F))
WRAP(glDepthRangef,   (F a, F b), (a, b), (F, F))
WRAP(glFogf,          (GLenum a, F b), (a, b), (GLenum, F))
WRAP(glLineWidth,     (F a), (a), (F))
WRAP(glMaterialf,     (GLenum a, GLenum b, F c), (a, b, c), (GLenum, GLenum, F))
WRAP(glNormal3f,      (F a, F b, F c), (a, b, c), (F, F, F))
WRAP(glOrthof,        (F a, F b, F c, F d, F e, F f), (a, b, c, d, e, f), (F, F, F, F, F, F))
WRAP(glPolygonOffset, (F a, F b), (a, b), (F, F))
WRAP(glRotatef,       (F a, F b, F c, F d), (a, b, c, d), (F, F, F, F))
WRAP(glTexEnvf,       (GLenum a, GLenum b, F c), (a, b, c), (GLenum, GLenum, F))
WRAP(glUniform1f,     (GLint a, F b), (a, b), (GLint, F))
WRAP(glUniform4f,     (GLint a, F b, F c, F d, F e), (a, b, c, d, e), (GLint, F, F, F, F))
WRAP(glLightf,        (GLenum a, GLenum b, F c), (a, b, c), (GLenum, GLenum, F))
WRAP(glLightModelf,   (GLenum a, F b), (a, b), (GLenum, F))
WRAP(glMultiTexCoord4f, (GLenum a, F b, F c, F d, F e), (a, b, c, d, e), (GLenum, F, F, F, F))
#undef F

/* --- timed wrappers: shader builds and texture uploads, for prof.c ----- */

#define TIMED(n, cat, params, args, types)                             \
    static void t_##n params                                           \
    {                                                                  \
        static void (*fn) types;                                       \
        static int looked;                                             \
        uint64_t t0;                                                   \
        if (!looked) { fn = (void (*) types)gl_proc(#n); looked = 1; } \
        if (!fn) return;                                               \
        t0 = sf_plat_time_us();                                        \
        fn args;                                                       \
        sf_prof_add(cat, sf_plat_time_us() - t0, 0);                   \
    }
typedef unsigned int GLuint;
typedef int GLsizei;
TIMED(glCompileShader, SF_PROF_SHADER, (GLuint s), (s), (GLuint))
TIMED(glLinkProgram, SF_PROF_LINK, (GLuint p), (p), (GLuint))
TIMED(glTexSubImage2D, SF_PROF_TEXTURE,
      (GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *d),
      (t, l, x, y, w, h, f, ty, d), (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))
TIMED(glCompressedTexImage2D, SF_PROF_TEXTURE,
      (GLenum t, GLint l, GLenum fi, GLsizei w, GLsizei h, GLint b, GLsizei sz, const void *d),
      (t, l, fi, w, h, b, sz, d), (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *))

/* glTexImage2D also lets hud.c swap in the fight-control atlases with the
 * controls erased (see hud.c), and glDeleteTextures tells it when a texture
 * it tracks goes away. */
static void t_glTexImage2D(GLenum t, GLint l, GLint fi, GLsizei w, GLsizei h, GLint b, GLenum f,
                           GLenum ty, const void *d)
{
    static void (*fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    static int looked;
    const void *alt;
    uint64_t t0;
    if (!looked) {
        fn = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
             gl_proc("glTexImage2D");
        looked = 1;
    }
    if (!fn)
        return;
    t0 = sf_plat_time_us();
    alt = sf_hud_tex_image(l, w, h, f, ty, d);
    fn(t, l, fi, w, h, b, f, ty, alt ? alt : d);
    sf_prof_add(SF_PROF_TEXTURE, sf_plat_time_us() - t0, 0);
}

static void t_glBindTexture(GLenum target, GLuint tex)
{
    static void (*fn)(GLenum, GLuint);
    static int looked;
    if (!looked) {
        fn = (void (*)(GLenum, GLuint))gl_proc("glBindTexture");
        looked = 1;
    }
    sf_hud_tex_bound(tex);
    if (fn)
        fn(target, tex);
}

static void t_glDeleteTextures(GLsizei n, const GLuint *ids)
{
    static void (*fn)(GLsizei, const GLuint *);
    static int looked;
    if (!looked) {
        fn = (void (*)(GLsizei, const GLuint *))gl_proc("glDeleteTextures");
        looked = 1;
    }
    sf_hud_tex_deleted(n, ids);
    if (fn)
        fn(n, ids);
}

/* --- EGL --------------------------------------------------------------- */

typedef int32_t EGLint;
typedef void *EGLDisplay, *EGLConfig, *EGLSurface, *EGLContext;

#define EGL_SUCCESS            0x3000
#define EGL_BAD_ATTRIBUTE      0x3004
#define EGL_BUFFER_SIZE        0x3020
#define EGL_ALPHA_SIZE         0x3021
#define EGL_BLUE_SIZE          0x3022
#define EGL_GREEN_SIZE         0x3023
#define EGL_RED_SIZE           0x3024
#define EGL_DEPTH_SIZE         0x3025
#define EGL_STENCIL_SIZE       0x3026
#define EGL_CONFIG_CAVEAT      0x3027
#define EGL_CONFIG_ID          0x3028
#define EGL_NATIVE_RENDERABLE  0x302D
#define EGL_NATIVE_VISUAL_ID   0x302E
#define EGL_SAMPLES            0x3031
#define EGL_SAMPLE_BUFFERS     0x3032
#define EGL_SURFACE_TYPE       0x3033
#define EGL_NONE               0x3038
#define EGL_COLOR_BUFFER_TYPE  0x303F
#define EGL_RENDERABLE_TYPE    0x3040
#define EGL_CONFORMANT         0x3042
#define EGL_VENDOR             0x3053
#define EGL_VERSION            0x3054
#define EGL_EXTENSIONS         0x3055
#define EGL_HEIGHT             0x3056
#define EGL_WIDTH              0x3057
#define EGL_CLIENT_APIS        0x308D
#define EGL_RGB_BUFFER         0x308E
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WINDOW_BIT         0x0004
#define EGL_OPENGL_ES_BIT      0x0001
#define EGL_OPENGL_ES2_BIT     0x0004

#define DPY ((EGLDisplay)0x5F0D15)
#define SURF ((EGLSurface)0x5F0D5F)

typedef struct { EGLint id, r, g, b, a, depth, stencil; } sf_cfg;
/* vitaGL renders to RGBA8888 with a D32F/S8 buffer; report the closest
 * EGL shapes. The first entry is what a "best match" picker should take. */
static const sf_cfg s_cfgs[] = {
    { 1, 8, 8, 8, 8, 24, 8 },
    { 2, 8, 8, 8, 0, 24, 8 },
    { 3, 5, 6, 5, 0, 16, 0 },
    { 4, 5, 6, 5, 0, 24, 8 },
};
#define NCFG (int)(sizeof(s_cfgs) / sizeof(s_cfgs[0]))

static int s_gl_ready, s_ctx_version = 2;
static EGLContext s_cur_ctx;
static EGLSurface s_cur_surf;

void sf_gl_init(void)
{
    const unsigned char *(*get_string)(GLenum);
    if (s_gl_ready)
        return;
    sf_plat_gl_init(SF_SCREEN_W, SF_SCREEN_H);
    s_gl_ready = 1;
    get_string = (const unsigned char *(*)(GLenum))sf_plat_gl_proc("glGetString");
    LOGI("GL ready (%dx%d): %s / %s", SF_SCREEN_W, SF_SCREEN_H,
         get_string ? (const char *)get_string(0x1F01) : "?",
         get_string ? (const char *)get_string(0x1F02) : "?");
}

static EGLDisplay sf_eglGetDisplay(void *native) { (void)native; return DPY; }

static unsigned sf_eglInitialize(EGLDisplay d, EGLint *major, EGLint *minor)
{
    sf_gl_init();
    if (major) *major = 1;
    if (minor) *minor = 4;
    LOGI("eglInitialize");
    return 1;
}

static unsigned sf_eglTerminate(EGLDisplay d) { return 1; }
static unsigned sf_eglBindAPI(unsigned api) { return 1; }
static EGLint sf_eglGetError(void) { return EGL_SUCCESS; }

static unsigned sf_eglGetConfigs(EGLDisplay d, EGLConfig *cfgs, EGLint size, EGLint *num)
{
    int i, n = NCFG;
    if (cfgs) {
        if (n > size)
            n = size;
        for (i = 0; i < n; i++)
            cfgs[i] = (EGLConfig)&s_cfgs[i];
    }
    if (num)
        *num = n;
    return 1;
}

static unsigned sf_eglGetConfigAttrib(EGLDisplay d, EGLConfig c, EGLint attr, EGLint *v)
{
    const sf_cfg *cfg = (const sf_cfg *)c;
    if (!cfg || !v)
        return 0;
    switch (attr) {
    case EGL_BUFFER_SIZE:     *v = cfg->r + cfg->g + cfg->b + cfg->a; break;
    case EGL_RED_SIZE:        *v = cfg->r; break;
    case EGL_GREEN_SIZE:      *v = cfg->g; break;
    case EGL_BLUE_SIZE:       *v = cfg->b; break;
    case EGL_ALPHA_SIZE:      *v = cfg->a; break;
    case EGL_DEPTH_SIZE:      *v = cfg->depth; break;
    case EGL_STENCIL_SIZE:    *v = cfg->stencil; break;
    case EGL_CONFIG_ID:       *v = cfg->id; break;
    case EGL_CONFIG_CAVEAT:   *v = EGL_NONE; break;
    case EGL_SAMPLES:
    case EGL_SAMPLE_BUFFERS:
    case EGL_NATIVE_RENDERABLE:
    case EGL_NATIVE_VISUAL_ID: *v = 0; break;
    case EGL_SURFACE_TYPE:    *v = EGL_WINDOW_BIT; break;
    case EGL_COLOR_BUFFER_TYPE: *v = EGL_RGB_BUFFER; break;
    case EGL_RENDERABLE_TYPE:
    case EGL_CONFORMANT:      *v = EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT; break;
    default:
        LOGV("eglGetConfigAttrib %#x unknown", (unsigned)attr);
        *v = 0;
        break;
    }
    return 1;
}

static EGLSurface sf_eglCreateWindowSurface(EGLDisplay d, EGLConfig c, void *win, const EGLint *attrs)
{
    LOGI("eglCreateWindowSurface config=%d", c ? ((const sf_cfg *)c)->id : -1);
    return SURF;
}

static unsigned sf_eglDestroySurface(EGLDisplay d, EGLSurface s) { return 1; }

static EGLContext sf_eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attrs)
{
    s_ctx_version = 1;
    for (; attrs && attrs[0] != EGL_NONE; attrs += 2)
        if (attrs[0] == EGL_CONTEXT_CLIENT_VERSION)
            s_ctx_version = attrs[1];
    LOGI("eglCreateContext: GLES %d", s_ctx_version);
    return (EGLContext)(uintptr_t)(0x5F0C00 + s_ctx_version);
}

static unsigned sf_eglDestroyContext(EGLDisplay d, EGLContext c) { return 1; }

static unsigned sf_eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext c)
{
    s_cur_ctx = c;
    s_cur_surf = draw;
    return 1;
}

static EGLContext sf_eglGetCurrentContext(void) { return s_cur_ctx; }
static EGLDisplay sf_eglGetCurrentDisplay(void) { return s_cur_ctx ? DPY : 0; }
static EGLSurface sf_eglGetCurrentSurface(EGLint which) { return s_cur_surf; }

static unsigned sf_eglQueryContext(EGLDisplay d, EGLContext c, EGLint attr, EGLint *v)
{
    if (!v)
        return 0;
    if (attr == EGL_CONTEXT_CLIENT_VERSION)
        *v = s_ctx_version;
    else if (attr == EGL_CONFIG_ID)
        *v = 1;
    else
        *v = 0;
    return 1;
}

static const char *sf_eglQueryString(EGLDisplay d, EGLint name)
{
    switch (name) {
    case EGL_VENDOR:      return "vitaGL";
    case EGL_VERSION:     return "1.4 vitaGL";
    case EGL_CLIENT_APIS: return "OpenGL_ES";
    default:              return "";
    }
}

static unsigned sf_eglSwapBuffers(EGLDisplay d, EGLSurface s)
{
    static unsigned frames;
    sf_plat_swap();
    sf_prof_frame();
    sf_hud_frame();
    if (frames++ == 0)
        LOGI("first eglSwapBuffers");
    return 1;
}

static void *sf_eglGetProcAddress(const char *name)
{
    void *fn = sf_import_resolve(name);
    LOGI("eglGetProcAddress(%s) = %p", name, fn);
    return fn;
}

const sf_export sf_exports_gl[] = {
    { "glAlphaFunc", (void *)w_glAlphaFunc },
    { "glBlendColor", (void *)w_glBlendColor },
    { "glClearColor", (void *)w_glClearColor },
    { "glClearDepthf", (void *)w_glClearDepthf },
    { "glColor4f", (void *)w_glColor4f },
    { "glDepthRangef", (void *)w_glDepthRangef },
    { "glFogf", (void *)w_glFogf },
    { "glLineWidth", (void *)w_glLineWidth },
    { "glMaterialf", (void *)w_glMaterialf },
    { "glNormal3f", (void *)w_glNormal3f },
    { "glOrthof", (void *)w_glOrthof },
    { "glPolygonOffset", (void *)w_glPolygonOffset },
    { "glRotatef", (void *)w_glRotatef },
    { "glTexEnvf", (void *)w_glTexEnvf },
    { "glUniform1f", (void *)w_glUniform1f },
    { "glUniform4f", (void *)w_glUniform4f },
    { "glLightf", (void *)w_glLightf },
    { "glLightModelf", (void *)w_glLightModelf },
    { "glMultiTexCoord4f", (void *)w_glMultiTexCoord4f },

    { "glCompileShader", (void *)t_glCompileShader },
    { "glLinkProgram", (void *)t_glLinkProgram },
    { "glTexImage2D", (void *)t_glTexImage2D },
    { "glDeleteTextures", (void *)t_glDeleteTextures },
    { "glBindTexture", (void *)t_glBindTexture },
    { "glTexSubImage2D", (void *)t_glTexSubImage2D },
    { "glCompressedTexImage2D", (void *)t_glCompressedTexImage2D },

    { "eglGetDisplay", (void *)sf_eglGetDisplay },
    { "eglInitialize", (void *)sf_eglInitialize },
    { "eglTerminate", (void *)sf_eglTerminate },
    { "eglBindAPI", (void *)sf_eglBindAPI },
    { "eglGetError", (void *)sf_eglGetError },
    { "eglGetConfigs", (void *)sf_eglGetConfigs },
    { "eglGetConfigAttrib", (void *)sf_eglGetConfigAttrib },
    { "eglCreateWindowSurface", (void *)sf_eglCreateWindowSurface },
    { "eglDestroySurface", (void *)sf_eglDestroySurface },
    { "eglCreateContext", (void *)sf_eglCreateContext },
    { "eglDestroyContext", (void *)sf_eglDestroyContext },
    { "eglMakeCurrent", (void *)sf_eglMakeCurrent },
    { "eglGetCurrentContext", (void *)sf_eglGetCurrentContext },
    { "eglGetCurrentDisplay", (void *)sf_eglGetCurrentDisplay },
    { "eglGetCurrentSurface", (void *)sf_eglGetCurrentSurface },
    { "eglQueryContext", (void *)sf_eglQueryContext },
    { "eglQueryString", (void *)sf_eglQueryString },
    { "eglSwapBuffers", (void *)sf_eglSwapBuffers },
    { "eglGetProcAddress", (void *)sf_eglGetProcAddress },
    { NULL, NULL }
};
