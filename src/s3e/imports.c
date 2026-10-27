/* Import resolution: s3e/EGL names map to our shims, GL names map to
 * softfp wrappers (float arguments) or straight to vitaGL. Anything left
 * over gets a stub that logs its name the first few times it is called
 * and returns 0, so a missing import shows up in the log instead of as a
 * prefetch abort at a NULL slot.
 */
#include "s3e/s3e_internal.h"

#include "platform/platform.h"

#include <string.h>

/* --- unresolved stubs --------------------------------------------------- */

#define MAX_STUBS 512
static const char *s_stub_names[MAX_STUBS];
static uint8_t s_stub_hits[MAX_STUBS];

static int unresolved_hit(int idx)
{
    if (s_stub_hits[idx] < 8) {
        s_stub_hits[idx]++;
        LOGW("UNIMPLEMENTED %s called%s", s_stub_names[idx],
             s_stub_hits[idx] == 8 ? " (silencing)" : "");
    }
    return 0;
}

#define STUB1(n)  static int SOFTFP stub_##n(void) { return unresolved_hit(n); }
#define STUB8(n)  STUB1(n##0) STUB1(n##1) STUB1(n##2) STUB1(n##3) \
                  STUB1(n##4) STUB1(n##5) STUB1(n##6) STUB1(n##7)
#define STUB64(n) STUB8(n##0) STUB8(n##1) STUB8(n##2) STUB8(n##3) \
                  STUB8(n##4) STUB8(n##5) STUB8(n##6) STUB8(n##7)
STUB64(00) STUB64(01) STUB64(02) STUB64(03) STUB64(04) STUB64(05) STUB64(06) STUB64(07)

#define REF1(n)  (void *)stub_##n,
#define REF8(n)  REF1(n##0) REF1(n##1) REF1(n##2) REF1(n##3) \
                 REF1(n##4) REF1(n##5) REF1(n##6) REF1(n##7)
#define REF64(n) REF8(n##0) REF8(n##1) REF8(n##2) REF8(n##3) \
                 REF8(n##4) REF8(n##5) REF8(n##6) REF8(n##7)
/* Every index is spelled with a leading 0, so it is an octal literal:
 * stub_0000..stub_0777 is exactly 512 functions, and
 * the table below lists them in the same order, so table[i] is stub i. */
static void *const s_stubs[MAX_STUBS] = {
    REF64(00) REF64(01) REF64(02) REF64(03) REF64(04) REF64(05) REF64(06) REF64(07)
};

void *sf_import_stub(int index, const char *name)
{
    if (index < 0 || index >= MAX_STUBS)
        index = MAX_STUBS - 1;
    s_stub_names[index] = name;
    LOGW("unresolved import %s -> logging stub", name);
    return s_stubs[index];
}

/* --- host table ---------------------------------------------------------- */

extern const sf_export sf_exports_device[], sf_exports_memory[], sf_exports_file[],
                       sf_exports_input[], sf_exports_sound[], sf_exports_net[],
                       sf_exports_gl[], sf_exports_compress[];

void *sf_import_resolve(const char *name)
{
    const sf_export *tables[] = {
        sf_exports_device, sf_exports_memory, sf_exports_file, sf_exports_input,
        sf_exports_sound, sf_exports_net, sf_exports_gl, sf_exports_compress,
    };
    unsigned t;
    const sf_export *e;
    void *fn;

    for (t = 0; t < sizeof(tables) / sizeof(tables[0]); t++)
        for (e = tables[t]; e->name; e++)
            if (strcmp(e->name, name) == 0)
                return e->fn;

    /* Integer-only GL entry points are ABI-identical under softfp and
     * hardfp, so the platform's own functions can be called directly.
     * Anything with a float argument is in sf_exports_gl above. */
    if (name[0] == 'g' && name[1] == 'l') {
        fn = sf_plat_gl_proc(name);
        if (fn)
            return sf_gl_trace(name, fn);
    }
    return NULL;
}
