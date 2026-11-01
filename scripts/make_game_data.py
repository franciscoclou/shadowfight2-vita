#!/usr/bin/env python3
"""Make the game data the Vita port reads, from your own Shadow Fight 2 APK.

    python3 make_game_data.py <shadowfight2-1.9.38.apk> [-o <folder>]

Writes a folder called shadowfight2 next to the APK (or to -o). Copy it to
ux0:data/ on the Vita, so ShadowFight2.s3e.raw ends up in
ux0:data/shadowfight2/. Needs Python 3 and nothing else. It takes about 400 MB
and downloads about 300 MB. If a download fails, run it again: finished packs
are kept.

    --no-packs    only unpack the APK (the APK can be an unzipped folder too)
    --packs-only  only download the packs, into a folder that has the rest

The game code is assets/ShadowFight2.s3e in the APK: an LZMA-alone stream (props
0x5D, 64 KiB dictionary, 8-byte size field) wrapping an image whose magic is
"XE3U". The stream has no end marker and its last few bytes fail to decode. A
decoder that raises discards its buffered output, so the tail is fed one byte at
a time: that recovers all but the last 4 bytes, which belong to the trailing
licence ("extra") section the loader never reads. The output is zero-padded to
the declared size. Decompressing here keeps an LZMA decoder out of the Vita build.

Everything after the first act (Hermit's chess yard and every later zone, the
boss models, raids, clans) is not in the APK: the game downloads it at runtime as
Derbh .dz packs. The port has no network, so they are fetched here once. At boot
the loader lists every pack it finds in ram/assets/packs.xml (src/s3e/packs.c),
which is how the game knows a pack is installed. Nekki's CDN still serves them,
but its HTTPS certificate has expired, so they are fetched over plain HTTP and
checked against the MD5s from Nekki's own config (config_shadowfight_test.xml and
friends): the newest Android (PlatformID 2) entry of each pack whose MinVersion is
at most 1.9.38.

The raid pack is from 2017 (game 1.9.28) and still carries copies of model files
that 1.9.38 ships newer in files.dz. After its first boot the game reads files.dz's
data from its ram:// cache, while Derbh keeps serving the attached packs and
answers first: the cached file's .hash no longer matches what is read, and the game
wipes its whole cache, packs.xml included, on every boot. So each colliding entry
is renamed in the pack's header (the last character of its name becomes '~', so no
offsets move), and the game keeps reading the files.dz version it cached.
settings.xml is left alone: every pack carries its own and the game reads it by
design. This edits the packs, so the MD5s only hold for a fresh download.

Derbh header: "DTRZ", u16 files, u16 dirs, u8 version, the file names, the directory
names ('\\'-separated), then per file a u16 directory index (0 is the root) and its
chunk ids up to 0xffff.
"""
import argparse
import hashlib
import lzma
import os
import shutil
import struct
import sys
import urllib.request
import zipfile

S3E = "assets/ShadowFight2.s3e"
S3E_SHA256 = "4b22b4f9255e138d0c7bdfb8cc08eb451411fa9719f3123b99954301697048e7"

CDN = "http://assets.nekkimobile.ru/shadowfight/packs/"
PACKS = [
    # name, file on the CDN, MD5
    ("ZONE_2", "zone_2_2017-01-24_10-37.dz", "38df6b9142d34b2d2fed86cecd897b7f"),
    ("ZONE_3", "zone_3_2015-09-11_12-33.dz", "4ba23c69cdc69f1fc7bd0833d59aefe0"),
    ("ZONE_4", "zone_4_2015-09-11_12-34.dz", "e72945c6d730f30407e93d16848afc26"),
    ("ZONE_5", "zone_5_2015-09-11_12-35.dz", "40fae85d565a5b1da7c2b3d8402a7021"),
    ("ZONE_6", "zone_6_2016-03-29_17-59.dz", "2d2b9a3c751eb5107628b84f184cbc79"),
    ("ZONE_7", "zone_7_1_2016-09-29_12-34.dz", "0bdfc9363049072bbec6aa6ffc96c000"),
    ("ZONE_7_2", "zone_7_2_2015-09-11_12-40.dz", "202d758e8a7d9106445818f4d3f875fd"),
    ("ZONE_7_3", "zone_7_3_2016-09-15_12-53.dz", "1c02178bd8b9a4d5df5dd97692e4feda"),
    ("ZONE_IM", "zone_IM_2016-09-29_12-33.dz", "b7712251f74ea26ecadb62aebf786c16"),
    ("ZONE_RAID", "zone_raid_2017-02-08_10-42.dz", "204d17701e7f8f2a862001f821b5a32a"),
    ("MODELS", "models_fonts_2017-02-21_16-06.dz", "d68d8983fe76c2f59a2a914d38f274a7"),
    ("CLANS", "clans_3rd_boss_new_iteration_2019-01-30_11-00.dz",
     "d079b8592542c0909d6b7631f2247831"),
]
NEEDED_BYTES = 450 << 20


class Failure(Exception):
    pass


# --- the APK, zipped or unzipped --------------------------------------------

class Source:
    def __init__(self, path):
        self.zip = None
        self.dir = None
        if os.path.isdir(path):
            self.dir = path
        elif zipfile.is_zipfile(path):
            self.zip = zipfile.ZipFile(path)
        else:
            raise Failure("%s is not an APK." % path)

    def names(self):
        if self.zip:
            return [n for n in self.zip.namelist() if not n.endswith("/")]
        found = []
        for root, _, files in os.walk(self.dir):
            rel = os.path.relpath(root, self.dir).replace(os.sep, "/")
            found += [f if rel == "." else rel + "/" + f for f in files]
        return found

    def open(self, name):
        if self.zip:
            return self.zip.open(name)
        return open(os.path.join(self.dir, *name.split("/")), "rb")


def read_s3e(src):
    if S3E not in src.names():
        raise Failure("No %s in this file, so it isn't the Shadow Fight 2 APK.\n"
                      "An .xapk or split APK won't work: the port needs the single "
                      "APK of version 1.9.38 for armeabi-v7a." % S3E)
    with src.open(S3E) as f:
        raw = f.read()
    if hashlib.sha256(raw).hexdigest() != S3E_SHA256:
        raise Failure("This APK holds a different build of the game. The port only "
                      "runs version 1.9.38 (versionCode 1000109), because it patches "
                      "fixed addresses in the game's code.")
    return raw


def unpack_s3e(raw):
    declared = struct.unpack("<Q", raw[5:13])[0]
    dec = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
    out = bytearray()
    i = 0
    try:
        while i < len(raw) and not dec.eof:
            n = 4096 if len(out) < declared - (1 << 20) else 1
            out += dec.decompress(raw[i:i + n])
            i += n
    except lzma.LZMAError:
        pass
    out += bytes(max(0, declared - len(out)))
    if out[:4] != b"XE3U":
        raise Failure("The game code did not unpack to an XE3U image.")
    return out


def write_game_files(src, raw, out):
    """Maps the APK's assets/ onto what the loader reads from its data folder."""
    print("Unpacking the game code...")
    with open(os.path.join(out, "ShadowFight2.s3e.raw"), "wb") as f:
        f.write(unpack_s3e(raw))

    print("Copying the game files...")
    count = 0
    for name in src.names():
        parts = name.split("/")
        if parts[0] != "assets" or ".." in parts:
            continue
        if parts[1] == "assets":
            rel = parts[1:]
        elif len(parts) == 2 and parts[1].endswith((".icf", ".txt", ".xml")):
            rel = parts[1:]
        else:
            continue
        dst = os.path.join(out, *rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with src.open(name) as r, open(dst, "wb") as w:
            shutil.copyfileobj(r, w)
        count += 1
    if count == 0:
        raise Failure("The APK has no game files.")
    # Marmalade's runtime reads its app settings from app.icf; the APK ships the
    # Android variant under its source name.
    shutil.copyfile(os.path.join(out, "app_android.icf"), os.path.join(out, "app.icf"))
    print("  %d files" % count)


# --- content packs -----------------------------------------------------------

def download(url, dst, label):
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    digest = hashlib.md5()
    done = 0
    with urllib.request.urlopen(request, timeout=60) as r, open(dst, "wb") as w:
        total = int(r.headers.get("Content-Length") or 0)
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            w.write(chunk)
            digest.update(chunk)
            done += len(chunk)
            if total:
                print("\r  %s: %3d%% of %d MB" % (label, done * 100 // total, total >> 20),
                      end="", flush=True)
    print()
    return digest.hexdigest()


def fetch_packs(out):
    failed = []
    for i, (name, file, md5) in enumerate(PACKS):
        dst = os.path.join(out, name + ".dz")
        label = "%2d/%d %s" % (i + 1, len(PACKS), name)
        if os.path.exists(dst):
            print("  %s: already here" % label)
            continue
        part = dst + ".part"
        for attempt in range(3):
            try:
                got = download(CDN + file, part, label)
            except OSError as e:
                print("\n  %s: %s" % (name, e))
                continue
            if got == md5:
                os.replace(part, dst)
                break
            print("  %s: the download was corrupt" % name)
        else:
            failed.append(name)
        if os.path.exists(part):
            os.remove(part)
    return failed


def pack_entries(data):
    """[(path, offset of the name's last character)] for every file."""
    if data[:4] != b"DTRZ":
        raise ValueError("not a Derbh archive")
    nfiles, ndirs, _ = struct.unpack_from("<HHB", data, 4)
    p = 9
    names = []
    for _ in range(nfiles):
        end = data.index(b"\0", p)
        names.append((data[p:end].decode("latin1"), end - 1))
        p = end + 1
    dirs = []
    for _ in range(ndirs - 1):
        end = data.index(b"\0", p)
        dirs.append(data[p:end].decode("latin1").replace("\\", "/"))
        p = end + 1
    entries = []
    for name, last in names:
        d = struct.unpack_from("<H", data, p)[0]
        p += 2
        while struct.unpack_from("<H", data, p)[0] != 0xffff:
            p += 2
        p += 2
        entries.append((dirs[d - 1] + "/" + name if d else name, last))
    return entries


def fix_packs(out):
    """Hides the files a pack shares with files.dz. Running it again changes nothing."""
    with open(os.path.join(out, "assets", "files.dz"), "rb") as f:
        base = {path for path, _ in pack_entries(f.read(1 << 20))}
    for name, _, _ in PACKS:
        pack = os.path.join(out, name + ".dz")
        if not os.path.exists(pack):
            continue
        with open(pack, "r+b") as f:
            head = bytearray(f.read(1 << 20))
            hidden = [(path, last) for path, last in pack_entries(head)
                      if path in base and path != "settings.xml"]
            for _, last in hidden:
                head[last] = ord("~")
            if hidden:
                f.seek(0)
                f.write(head)
        for path, _ in hidden:
            print("  %s: hid %s" % (name, path))


# --- main ---------------------------------------------------------------------

def run(args):
    apk = args.apk
    if not apk and not args.packs_only:
        apk = input("Drag your Shadow Fight 2 1.9.38 APK here and press Enter: ")
        apk = apk.strip().strip("\"'")

    out = args.out
    if not out:
        if not apk:
            raise Failure("--packs-only needs -o <folder>.")
        out = os.path.join(os.path.dirname(os.path.abspath(apk)), "shadowfight2")

    if not args.packs_only:
        if not os.path.exists(apk):
            raise Failure("Can't find %s." % apk)
        src = Source(apk)
        raw = read_s3e(src)
        os.makedirs(out, exist_ok=True)
        if not args.no_packs and shutil.disk_usage(out).free < NEEDED_BYTES:
            raise Failure("Not enough disk space. This needs about 450 MB free.")
        write_game_files(src, raw, out)

    if args.no_packs:
        failed = []
    else:
        if not os.path.exists(os.path.join(out, "assets", "files.dz")):
            raise Failure("%s has no assets/files.dz. Unpack the APK first." % out)
        print("Downloading the content packs from Nekki's servers...")
        failed = fetch_packs(out)
        fix_packs(out)

    if failed:
        raise Failure("These packs didn't download: %s\nRun this again to retry them. "
                      "If it keeps failing, Nekki's servers may have stopped serving "
                      "them." % ", ".join(failed))
    print("\nDone: %s" % out)
    if not args.no_packs and not args.packs_only:
        print("Copy that folder to ux0:data/ on your Vita.")


def main():
    parser = argparse.ArgumentParser(
        description="Make the Vita port's game data from your own Shadow Fight 2 APK.")
    parser.add_argument("apk", nargs="?", help="the 1.9.38 APK, or an unzipped copy")
    parser.add_argument("-o", "--out", help="output folder")
    parser.add_argument("--no-packs", action="store_true", help="only unpack the APK")
    parser.add_argument("--packs-only", action="store_true", help="only download the packs")
    status = 0
    try:
        run(parser.parse_args())
    except Failure as e:
        print("\nError: %s" % e, file=sys.stderr)
        status = 1
    except KeyboardInterrupt:
        status = 130
    # A window opened by double-clicking would close before this could be read.
    if os.name == "nt" and sys.stdin.isatty():
        input("\nPress Enter to close.")
    return status


if __name__ == "__main__":
    sys.exit(main())
