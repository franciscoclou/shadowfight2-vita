Libraries in this port's eboot.bin

The port's own code is MIT licensed (see LICENSE), and its source is at
https://github.com/franciscoclou/shadowfight2-vita

vitaGL, LGPL-3.0 (LGPL-3.0.txt, which builds on GPL-3.0.txt)
    https://github.com/Rinnegatamante/vitaGL
    commit 76f43e6fbe1513cee44118da7ff2c16728c4fed2

vitaShaRK, LGPL-3.0 (LGPL-3.0.txt, which builds on GPL-3.0.txt)
    https://github.com/Rinnegatamante/vitaShaRK
    commit df24065e65098b2d1ac533760109ad4367573f28

SceShaccCgExt 1.0.1, LGPL-2.1 (LGPL-2.1.txt)
    https://github.com/bythos14/SceShaccCgExt

math-neon, MIT (math-neon.txt)
    https://github.com/Rinnegatamante/math-neon

zlib 1.3.2, zlib licence
    Copyright (C) 1995-2026 Jean-loup Gailly and Mark Adler
    https://zlib.net/

minimp3, CC0-1.0
    https://github.com/lieff/minimp3

To use a modified LGPL library: change it in docker/Dockerfile in the source
above, run scripts/build.sh release, and install the VPK it makes.
