#!/usr/bin/env python3
"""List the strings a game function refers to.

    scripts/func_strings.py <link_start> <link_end> [image]

The game reaches strings PC-relatively (ldr rY, [pc, #lit]; add rX, pc, rY
or add rX, rY, pc), which Ghidra often leaves as DAT_ + constant. This
resolves each such pair in [start, end) and prints address -> string.
"""
import struct
import sys

start, end = int(sys.argv[1], 16), int(sys.argv[2], 16)
image = sys.argv[3] if len(sys.argv) > 3 else "game_out/ShadowFight2.s3e.raw"
d = open(image, "rb").read()
CODE, BASE = 0x50ea5, 0x4a000000


def word(a):
    return struct.unpack_from("<I", d, CODE + a - BASE)[0]


def string(a):
    o = CODE + a - BASE
    if not 0 <= o < len(d):
        return None
    e = d.find(b"\0", o)
    s = d[o:e]
    if 1 < len(s) < 200 and all(32 <= c < 127 for c in s):
        return s.decode()
    return None


regs = {}
for a in range(start, end, 4):
    ins = word(a)
    if (ins & 0x0F7F0000) == 0x051F0000:                  # ldr rd, [pc, #imm]
        imm = ins & 0xFFF
        lit = a + 8 + (imm if ins >> 23 & 1 else -imm)
        regs[ins >> 12 & 0xF] = word(lit)
    elif (ins & 0x0FE00010) == 0x00800000:                # add rd, rn, rm
        rn, rm = ins >> 16 & 0xF, ins & 0xF
        other = rm if rn == 15 else rn if rm == 15 else None
        if other is not None and other in regs:
            target = (a + 8 + regs[other]) & 0xFFFFFFFF
            s = string(target)
            if s is not None:
                print(f"{a:#x}: {target:#x} {s!r}")
