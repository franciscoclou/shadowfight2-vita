# Reverse-engineering notes

Build: Shadow Fight 2 1.9.38 (versionCode 1000109), armeabi-v7a.
Addresses are **link-time** (image base `0x4A000000`) unless noted; the
loader logs crash addresses in this form.

## ShadowFight2.s3e container

| offset | value | meaning |
|---|---|---|
| 0x00 | `5D` | LZMA properties (lc=3, lp=0, pb=2) |
| 0x01 | `00 00 01 00` | dictionary size 0x10000 (64 KiB) |
| 0x05 | `B1 5C 94 00 00 00 00 00` | uncompressed size 9,723,057 |

The stream has no end marker and its last 7 bytes do not decode. A decoder
that raises discards buffered output, so `scripts/unpack_s3e.py` feeds the
tail byte by byte: that yields 9,723,053 bytes. The 4 missing bytes are the
end of the XOR'd licence string ("extra" section), which nothing reads.

## XE3U image (decompressed)

Header (`scripts/s3e_info.py <image> header`):

| field | value |
|---|---|
| magic / version | `XE3U` / 0x42800 |
| fixups | 0x12d9 + 0x4fbcc |
| code (file / mem) | 0x50ea5, 0x8f4cf0 / 0x918204 |
| data_off (in blob) | 0x89c000 (text is everything before) |
| entry | blob + 0 (crt0: init, `main` at 0x4a791144, exit) |
| config | 0x4c + 0x128d: embedded s3e.icf (system defaults) |
| base | 0x4a000000 |
| sig / extra | 0x945b95+0x8c / 0x945c21+0x90 (licence XOR 0xEE) |

Fixup subsections:

- **0** import names: u16 count (347), NUL-terminated names.
- **1** base relocations: u32 count (79,662), u32 blob offsets. All lie in
  the data segment and hold absolute addresses; add (load − 0x4a000000).
- **2** empty. **4** import slots: u32 count (355), 6-byte entries
  `{off_hi u16, off_lo u16, sym u16}`; each slot is a data word that gets
  the function's address. Code reaches them through PLT-style veneers
  (`add ip,pc,#..; add ip,ip,#..; ldr pc,[ip,#..]!`), 334 of them at the
  start of text (`s3e_info.py <image> veneers`).

Code is ARM mode, ARMv7 + VFP, **softfp**: `glClearColor(0,0,0,1)` loads
0x3f800000 into r3. There are no libc imports; the game links its own libc
and allocates through `s3eMallocBase`.

## libs3e_android.so property enums (decompiled with REA)

- `s3eSurfaceGetInt`: 0 WIDTH, 1 HEIGHT, 2 PITCH, 3 PIXEL_TYPE,
  4/5 DEVICE_WIDTH/HEIGHT, 6 DEVICE_PIXEL_TYPE, 7 DEVICE_PITCH,
  8 BLIT_DIRECTION, 9 DISPLAY, 10 NUM_DISPLAYS, 11 DEVICE_BLIT_DIRECTION,
  12/13 *_QUANTISED, 14 ORIENTATION_LOCK, 15 GL_MODE, 16/17 *_UNSCALED.
- `s3eGLGetInt`: 0 GL version, 1 EGL version, 2 must-suspend flag.
- `s3eMemoryGetInt`: **0 current heap index**, 1 SIZE, 2 USED, 3 FREE,
  4 LFB, 5 FLAGS, 6 global. The game brackets every allocation with
  `old = GetInt(0); SetInt(0, 7); ...; SetInt(0, old)` (0x4a78c528).
- `s3ePointerGetInt`: 0 AVAILABLE (1), 1 (0), 2/3 TYPE (2 = touch),
  4 multi-touch flag.
- `s3eFileGetFileInt(path, prop)` returns int64: 0 is-dir, 1 is-file,
  2 writable, 3 user dir, 4 size, 5 mtime, 6/8 user-fs queries.
- `s3eFileListNext(list, out)`: two arguments, no length.
- `s3eCompressionDecompInit(type, read_cb, user)`: streaming, type <= 4.
- `s3eDevice*` / `s3eDebug*` enums are exact from Marmalade's own headers,
  preserved in the preprocessed `.ii` files of
  github.com/marmalade/s3eGooglePlayGames. Android is OS id 19.

## Runtime config

libs3e reads `s3e.icf` (embedded), `app.icf` and `game.icf`. The APK ships
`app_android.icf`; `prepare_game.sh` installs it as `app.icf`.

## Derbh user filesystem (0x4a7528cc)

The game registers its `.dz` archive layer with `s3eFileAddUserFileSys`,
passing 11 words: 9 required callbacks then 2 zero words.

| slot | addr | role |
|---|---|---|
| 0 | 0x4a753300 | open(name, mode) -> handle or 0 |
| 1 | 0x4a75373c | read(buf, size, count, fh) |
| 2 | 0x4a7525d4 | eof(fh) |
| 3 | 0x4a7537d8 | seek(fh, off, origin) |
| 4 | 0x4a7525ec | tell(fh) |
| 5 | 0x4a75386c | close(fh) |
| 6 | 0x4a75343c | list-directory(path) |
| 7 | 0x4a75335c | list-next(list, out, len) |
| 8 | 0x4a7525f4 | list-close(list) |

Derbh spells paths Windows-style: `rom:\\assets\files.dz`.

## Extensions requested at boot (s3eExtGetHash)

- **s3eThread** `0xcc0b4a28`, 80-byte table, stubs at 0x4a790710..0x4a790fc8.
  Thread handles are **integer IDs**: the game's `pthread_self()`
  (0x4a78d688) returns `GetCurrent()`, or 4000 if s3eThread is missing, and
  its TLS (get 0x4a7915ec, set 0x4a791508) indexes a 129-entry table by
  `id - 4000`. Slots: 3 Create(fn, arg, 0,0,0), 4 GetCurrent,
  8/11 on thread handles, 9 LockCreate, 12 LockAcquire(lock, ms),
  13 LockRelease, 14 SemCreate(count), 16 SemWait(sem, ms), 17 SemPost.
  Without it the first std::mutex throws `__concurrence_lock_error`.
- **s3eWindow** `0xd31aa8a8`, 72 bytes: not requested at boot.
- **s3eDeviceUniqueID** `0x91a0de04`, 4 bytes: slot 0 `Get() -> const char *`.
  Wrappers at 0x4a23b5e4 (available?) and 0x4a23b650 (call). Required:
  see "Anti-tamper" below.
- **s3eDialog** `0xa6c1a5c0`, 28 bytes (loaders 0x4a001198..0x4a001384):
  answered "unavailable"; the game logs an assert and continues.
- `0x502bcf80` (8 bytes) is **s3eFBAnalytics**, `0xda590ec5` (4 bytes)
  **s3eGetSimOperator**: unavailable, harmless.

Extension names come from the `error loading extension: <name>` string
each loader passes to `s3eDebugAssertShow` next to its `movw/movt` hash.

## Anti-tamper: empty device ID = "Hack error"

The game writes its own log to `ram://logs/<date>.log`. Read it first when
the game stalls.

- 0x4a1ca390 classifies `s3eDeviceGetString(2)` (S3E_DEVICE_ID): "Windows
  Simulator", "Windows", "OSX", "Windows Desktop" set the **desktop** flag
  (byte 0x4a8fa441, accessor 0x4a1c7510).
- 0x4a69df90 (users loader) builds the device identifier with 0x4a69d984,
  which on a fresh install takes it from s3eDeviceUniqueID via 0x4a1c8738.
  If the identifier is empty and the device is not a desktop, it sets the
  **hack** flag (byte 0x4a90cb2a, accessor 0x4a6aee00).
- With the hack flag set, 0x4a69fa30 calls the handler 0x4a25e7b0, which
  logs `Hack error: user.xml`, deletes the save and resets; the title
  screen never advances. Providing s3eDeviceUniqueID fixes it.
- 0x4a1d4f04 makes the online GUID only on Windows/OSX/Bada/iOS (OS ids
  3, 13, 18, 14). On Android (19) it logs `Unsupported device! GUID isn't
  generated!`, which is expected and harmless.

## s3eCompression (libs3e_android.so)

`DecompInit(type, read_cb, ud)` -> handle 1..4 (max 4 open), 0 on error.
Types: 0 auto, 1 stored, 2 zlib, 3 gzip, 4 LZMA-alone. Auto reads 2 bytes:
gzip magic, else a valid zlib header, else (13 bytes) an LZMA header with
props <= 0xe1, dict <= 0x900000 and zero size bytes 9..12, else stored.
`read_cb` is an s3e callback: `cb(&{void *buf, int32 len}, ud)` returns
the bytes supplied, fewer than `len` at the end. `DecompRead(h, buf,
&len)` returns 0 with `*len` set, or 1 when nothing was produced.
`DecompFinal(h)` frees the handle.

## Boot milestones on the qemu harness

1. `_IwMain`, config reads, s3eThread.
2. EGL: the game picks config 3 (RGB565/D16) from `eglGetConfigs`,
   creates a GLES2 context, looks up OES extensions.
3. First `eglSwapBuffers`, then the splash screen (logo on parchment).
   Without s3eDeviceUniqueID it stays there forever ("Hack error").
4. With it: the Personal data consent screen ("I AGREE" at 480,493).

## Frame rate and graphics quality

- Main loop (cocos2d-x, link 0x4a05dd30): per frame it reads the time,
  yields once, updates and draws (eglSwapBuffers), then calls
  s3eDeviceYield until the animation interval (object +8/+12, 64-bit ms)
  has passed. A fight at 30 fps spends ~80% of its time in that wait.
- internalSettings.xml: `<FrameRate Value="60"/>`, plus QualityOptions
  that apply per graphics quality: ReduceFPS (LOW), ParticlesOff and
  SequencesOff (LOW and MEDIUM).
- The quality is stored per profile in users.xml as
  `<QualityCondition Value="LOW|MEDIUM|HIGH"/>`, and the in-game Settings
  dialog changes it (Settings_Graphics_Quality). The first value comes
  from assets/devices.xml, matched against s3eDeviceGetString(2)
  (S3E_DEVICE_ID) in 0x4a1c8b70; "PSVita" is not listed.
- The background thread (4001, 0x4a1d5d24) loops sleep(10) -> locked
  periodic task 0x4a1d58a0; its sleep (0x4a78c6e8) spins on
  s3eDeviceYield(remaining ms).
- scripts/func_strings.py <start> <end> lists the PC-relative strings a
  function uses, which Ghidra shows as DAT_ + constant.

## Save hashes (anti-tamper)

Every file the game caches in ram:// has `<file>.hash`: the MD5 (hex) of
`"<path>" + contents + salt`, e.g. `"assets/packs.xml" + ... `. The file
holds two lines; a fresh write has the same digest on both. The salt is
the device identifier: s3eDeviceUniqueID, or s3eDeviceGetString(25) for an
old users.xml (GameLoader::getSalt, set at 0x4a69df90 through 0x4a1f65f0
into the string at 0x4a8fab4c; the byte 0x4a8f3f2d says to prepend the path).

- 0x4a1fa4f8 MD5(buf, len) -> hex std::string; 0x4a1f6858 builds the input.
- 0x4a1f8e4c saves an XML document and its .hash; 0x4a1f88f8 re-reads a file,
  checks it against its .hash (0x4a1f849c) and rewrites it, returning 0 on a
  mismatch.
- 0x4a25e6f0 runs that check over every cached file (list at 0x4a90d018);
  one failure wipes the ram:// cache, packs.xml and settings.xml included.
  users.xml and localSettings.bin are not touched.

## Content packs

Zone 2 onwards (Hermit's chess_yard, every later location and boss), the
newer models, raids and clans are Derbh .dz packs the game downloads; the
APK has none of them. ?PackAssert(NAME).Availability in the quests only
looks NAME up in ram://assets/packs.xml (`<Packs><Pack Name Url Version
Attach Hash [EndDate]/>`, parsed at 0x4a260a30 into the list at
manager+0xb90, manager 0x4a266ed4, lookup 0x4a25e490).

- Boot attacher 0x4a69ece0: for each Attach="1" pack, path =
  0x4a1fc730(Name) + ".dz". The prefix is "cache://" on iOS (OS 14), none
  on Android, so the file is found ram then rom. It MD5s the whole file
  (0x4a1fa6fc, 1 KB freads) against Hash, then attaches it with
  dzArchiveAttach (0x4a752ef8). The call at 0x4a69f028 is patched to the
  string copy 0x4a004084, so Hash holds the path instead.
- 0x4a69e924 detaches and deletes every Attach="1" pack and packs.xml.
- 0x4a265a78 adds a pack after a download (0x4a26702c).
- Derbh's open (0x4a753300) tries the attached archives newest first. Once
  files.dz's data is cached in ram the game reads that, but packs keep
  answering first: a pack file with the same path as a cached one is read
  instead and fails its .hash. The 2017 raid pack has 11 such model files,
  older than 1.9.38's; scripts/fix_packs.py hides them.
- Pack URLs and MD5s: Nekki's configs config_SF2.xml,
  config_shadowfight_test.xml, config_shadowfight_raid_stage.xml and
  config_shadowfight_raid_dev.xml under
  assets.nekkimobile.ru/shadowfight/data/ (plain HTTP still works; the
  HTTPS certificate has expired).
- Derbh header: "DTRZ", u16 files, u16 dirs, u8 version, file names, dir
  names ('\'-separated), then per file a u16 dir index (0 = root) and chunk
  ids up to 0xffff. Every pack carries its own settings.xml.

## Crash notification

Quest CrashNotification (quests.xml) fires at SessionStart when
?SysInfo().LastSessionCrashed and DeviceTotalMem <= 512 (MB); its OK
button sets LOW graphics. Closing the app from the LiveArea counts as a
crash, so s3eDeviceGetInt reports 640 MB (RAM + CDRAM).
