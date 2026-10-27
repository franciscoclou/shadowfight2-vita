#!/usr/bin/env python3
"""Hide the files a content pack shares with the game's own files.dz.

    fix_packs.py <files.dz> <pack.dz>...

The packs fetch_packs.sh downloads are the newest Nekki ever listed, but the
raid pack is from 2017 (game 1.9.28) and still carries copies of model files
that 1.9.38 ships newer in files.dz. After its first boot the game reads
files.dz's data from its ram:// cache, while Derbh keeps serving the attached
packs and answers first: the cached file's .hash no longer matches what is
read, and the game wipes its whole cache, packs.xml included, on every boot.

Each colliding entry is renamed in the pack's header (the last character of
its name becomes '~', so no offsets move), and the game keeps reading the
files.dz version it cached. settings.xml is left alone: every pack carries
its own and the game reads it by design. Running it again changes nothing.

Derbh header: "DTRZ", u16 files, u16 dirs, u8 version, the file names, the
directory names ('\\'-separated), then per file a u16 directory index (0 is
the root) and its chunk ids up to 0xffff.
"""
import struct
import sys


def entries(data):
    """[(path, offset of the name's last character)] for every file."""
    if data[:4] != b'DTRZ':
        raise ValueError('not a Derbh archive')
    nfiles, ndirs, _ = struct.unpack_from('<HHB', data, 4)
    p = 9
    names = []
    for _ in range(nfiles):
        end = data.index(b'\0', p)
        names.append((data[p:end].decode('latin1'), end - 1))
        p = end + 1
    dirs = []
    for _ in range(ndirs - 1):
        end = data.index(b'\0', p)
        dirs.append(data[p:end].decode('latin1').replace('\\', '/'))
        p = end + 1
    out = []
    for name, last in names:
        d = struct.unpack_from('<H', data, p)[0]
        p += 2
        while struct.unpack_from('<H', data, p)[0] != 0xffff:
            p += 2
        p += 2
        out.append((dirs[d - 1] + '/' + name if d else name, last))
    return out


def main():
    with open(sys.argv[1], 'rb') as f:
        base = {path for path, _ in entries(f.read(1 << 20))}
    for pack in sys.argv[2:]:
        with open(pack, 'r+b') as f:
            head = bytearray(f.read(1 << 20))
            hidden = [(path, last) for path, last in entries(head)
                      if path in base and path != 'settings.xml']
            for path, last in hidden:
                head[last] = ord('~')
            if hidden:
                f.seek(0)
                f.write(head)
        for path, _ in hidden:
            print('  %s: hid %s' % (pack.rsplit('/', 1)[-1], path))


if __name__ == '__main__':
    main()
