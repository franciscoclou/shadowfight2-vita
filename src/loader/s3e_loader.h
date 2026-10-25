#ifndef SF_S3E_LOADER_H
#define SF_S3E_LOADER_H

/* Loader for Marmalade XE3U executables (the decompressed .s3e).
 *
 * Layout, from scripts/s3e_info.py and docs/RE_NOTES.md:
 *   0x00 header (magic "XE3U", fixup/code/config offsets, link base)
 *   config  embedded system ICF text
 *   fixups  0 = import names, 1 = base relocations, 2/3/4 = import slots
 *   code    code+data blob linked at header.base; data starts at
 *           header.data_off and everything after code_file is BSS
 *
 * The code is position dependent only through data words (every base
 * relocation lands in the data segment), and imports are a table of
 * function-pointer slots, also in the data segment. So the text can be
 * mapped read-only/executable and never written after the initial copy.
 */

#include <stdint.h>

typedef struct sf_s3e_header {
    char     magic[4];
    uint32_t version;
    uint16_t flags;
    uint16_t arch;
    uint32_t fixup_off, fixup_size;
    uint32_t code_off, code_file, code_mem;
    uint32_t sig_off, sig_size;
    uint32_t entry;
    uint32_t config_off, config_size;
    uint32_t base;
    uint32_t extra_off, extra_size;
    uint32_t ext_hdr;
    uint32_t data_off;
} __attribute__((packed)) sf_s3e_header;

typedef struct sf_s3e_module {   /* struct tag used by s3e_internal.h */
    sf_s3e_header hdr;
    uintptr_t base;          /* where the blob is mapped */
    uint32_t  text_size;     /* bytes of the RX mapping */
    uint32_t  data_size;     /* bytes of the RW mapping (data + bss) */
    void    (*entry)(void);
    const char *config;      /* embedded ICF, NUL-terminated copy */
    int       unresolved;    /* imports that got a logging stub */
} sf_s3e_module;

/* Returns the host implementation for an import name, or NULL. */
typedef void *(*sf_resolve_fn)(const char *name);
/* Returns a logging stub for import 'index' named 'name'. */
typedef void *(*sf_stub_fn)(int index, const char *name);

int sf_s3e_load(sf_s3e_module *mod, const char *path, uintptr_t load_addr,
                sf_resolve_fn resolve, sf_stub_fn stub);

/* Game address -> offset in the original link layout (for Ghidra). */
static inline uint32_t sf_s3e_to_link(const sf_s3e_module *mod, uintptr_t addr)
{
    return (uint32_t)(addr - mod->base + mod->hdr.base);
}

extern sf_s3e_module sf_game;

#endif
