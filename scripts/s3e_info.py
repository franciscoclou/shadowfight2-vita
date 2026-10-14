#!/usr/bin/env python3
"""Inspect a decompressed Marmalade XE3U image (game_out/ShadowFight2.s3e.raw).

    scripts/s3e_info.py <image> header        header fields
    scripts/s3e_info.py <image> imports       slot address, symbol per import
    scripts/s3e_info.py <image> veneers       veneer address -> symbol
    scripts/s3e_info.py <image> elf <out.elf> ELF for Ghidra/objdump: blob at
                                              base_addr, symbols on veneers
                                              and import slots

Format (see docs/RE_NOTES.md): a 0x48-byte header, an embedded ICF config,
fixup subsections (0 = symbol names, 1 = base relocations, 2/4 = import
slots), then the code+data blob, a signature and an XOR'd licence string.
"""

import struct
import sys

HDR = ("magic version flags arch fixup_off fixup_size code_off code_file "
       "code_mem sig_off sig_size entry config_off config_size base "
       "extra_off extra_size ext_hdr data_off").split()


class Image:
    def __init__(self, data):
        self.d = data
        vals = struct.unpack_from("<4sIHHIIIIIIIIIIIIIII", data, 0)
        self.h = dict(zip(HDR, vals))
        self.blob = data[self.h["code_off"]:self.h["code_off"] + self.h["code_file"]]
        self.names, self.relocs, self.imports = [], [], []
        p, end = self.h["fixup_off"], self.h["fixup_off"] + self.h["fixup_size"]
        while p < end:
            kind, size = struct.unpack_from("<II", data, p)
            body = p + 8
            if kind == 0:
                (n,) = struct.unpack_from("<H", data, body)
                q = body + 2
                for _ in range(n):
                    e = data.index(b"\0", q)
                    self.names.append(data[q:e].decode())
                    q = e + 1
            elif kind == 1:
                (n,) = struct.unpack_from("<I", data, body)
                self.relocs = list(struct.unpack_from(f"<{n}I", data, body + 4))
            elif kind in (2, 3, 4):
                (n,) = struct.unpack_from("<I", data, body)
                for i in range(n):
                    hi, lo, sym = struct.unpack_from("<HHH", data, body + 4 + 6 * i)
                    self.imports.append(((hi << 16) | lo, sym, kind))
            p += size

    def veneers(self):
        """add ip, pc, #A ; add ip, ip, #B ; ldr pc, [ip, #C]!"""
        slot_sym = {off: self.names[s] for off, s, _ in self.imports}
        out = {}
        b = self.blob
        for off in range(0, self.h["data_off"] - 12, 4):
            w0, w1, w2 = struct.unpack_from("<III", b, off)
            if (w0 & 0xFFFFF000) != 0xE28FC000 or (w1 & 0xFFFFF000) != 0xE28CC000 \
                    or (w2 & 0xFFFFF000) != 0xE5BCF000:
                continue
            ror = lambda imm: ((imm & 0xFF) >> (2 * (imm >> 8 & 0xF)) |
                               (imm & 0xFF) << (32 - 2 * (imm >> 8 & 0xF))) & 0xFFFFFFFF
            slot = (off + 8 + ror(w0 & 0xFFF) + ror(w1 & 0xFFF) + (w2 & 0xFFF)) & 0xFFFFFFFF
            if slot in slot_sym:
                out[off] = slot_sym[slot]
        return out


def write_elf(img, path):
    """Minimal ET_EXEC with one RWX PT_LOAD and a symbol table."""
    base = img.h["base"]
    blob = bytearray(img.blob)
    syms = [(f"__entry", base + img.h["entry"])]
    syms += [(n, base + o) for o, n in img.veneers().items()]
    syms += [(f"slot_{img.names[s]}", base + o) for o, s, _ in img.imports]
    strtab = bytearray(b"\0")
    symtab = bytearray(16)
    for name, addr in syms:
        symtab += struct.pack("<IIIBBH", len(strtab), addr, 4, 0x12 if not name.startswith("slot_") else 0x11, 0, 1)
        strtab += name.encode() + b"\0"
    shstr = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    ehsize, phsize = 52, 32
    text_off = 0x1000
    sym_off = text_off + len(blob)
    str_off = sym_off + len(symtab)
    shs_off = str_off + len(strtab)
    sh_off = (shs_off + len(shstr) + 3) & ~3
    eh = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + b"\0" * 9, 2, 40, 1,
                     base + img.h["entry"], ehsize, sh_off, 0x05000200,
                     ehsize, phsize, 1, 40, 5, 4)
    ph = struct.pack("<IIIIIIII", 1, text_off, base, base, len(blob),
                     img.h["code_mem"], 7, 0x1000)
    sh = bytes(40)
    sh += struct.pack("<IIIIIIIIII", 1, 1, 7, base, text_off, len(blob), 0, 0, 4, 0)
    sh += struct.pack("<IIIIIIIIII", 7, 2, 0, 0, sym_off, len(symtab), 3, 1, 4, 16)
    sh += struct.pack("<IIIIIIIIII", 15, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0)
    sh += struct.pack("<IIIIIIIIII", 23, 3, 0, 0, shs_off, len(shstr), 0, 0, 1, 0)
    out = bytearray(eh + ph)
    out += bytes(text_off - len(out)) + blob + symtab + strtab + shstr
    out += bytes(sh_off - len(out)) + sh
    open(path, "wb").write(out)


def main():
    img = Image(open(sys.argv[1], "rb").read())
    cmd = sys.argv[2] if len(sys.argv) > 2 else "header"
    if cmd == "header":
        for k in HDR:
            v = img.h[k]
            print(f"{k:12} {v if isinstance(v, bytes) else hex(v)}")
        print(f"{'symbols':12} {len(img.names)}\n{'relocs':12} {len(img.relocs)}"
              f"\n{'imports':12} {len(img.imports)}")
    elif cmd == "imports":
        for off, sym, kind in img.imports:
            print(f"{img.h['base'] + off:#010x} {kind} {img.names[sym]}")
    elif cmd == "veneers":
        for off, name in sorted(img.veneers().items()):
            print(f"{img.h['base'] + off:#010x} {name}")
    elif cmd == "elf":
        write_elf(img, sys.argv[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
