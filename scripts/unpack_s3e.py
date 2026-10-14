#!/usr/bin/env python3
"""Decompress a Marmalade .s3e into the raw XE3U executable the loader reads.

    scripts/unpack_s3e.py <in.s3e> <out.s3e.raw>

The APK's ShadowFight2.s3e is an LZMA-alone stream (props 0x5D, 64 KiB
dictionary, 8-byte size field) wrapping an image whose magic is "XE3U".
The stream has no end marker and its last few bytes fail to decode. A
decoder that raises discards its buffered output, so the tail is fed one
byte at a time: that recovers all but the last 4 bytes, which belong to the
trailing licence ("extra") section the loader never reads. The output is
zero-padded to the declared size.
Decompressing on the PC keeps an LZMA decoder out of the Vita build.
"""

import lzma
import struct
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip())
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    raw = open(src, "rb").read()

    if raw[:4] == b"XE3U":
        out = raw
    else:
        declared = struct.unpack("<Q", raw[5:13])[0]
        dec = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
        out = bytearray()
        stop = len(raw)
        i = 0
        try:
            while i < len(raw) and not dec.eof:
                n = 4096 if len(out) < declared - (1 << 20) else 1
                out += dec.decompress(raw[i:i + n])
                i += n
        except lzma.LZMAError:
            stop = i
        out += bytes(max(0, declared - len(out)))
        print(f"  decoded {len(out)} bytes (header declares {declared}); "
              f"undecodable tail starts near offset {stop} of {len(raw)}")

    if out[:4] != b"XE3U":
        print(f"  FAIL: magic is {bytes(out[:4])!r}, expected b'XE3U'")
        return 1
    open(dst, "wb").write(out)
    print(f"  ok   {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
