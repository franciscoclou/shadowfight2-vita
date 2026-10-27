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

## Shop cheats panel

- Shop constructor 0x4a65fc28 copies the debug flag into shop+0x134; the
  shop builds its "Hanger" button to the panel only then (0x4a656444).
- The panel is the shop's InfoSprite (vtable 0x4a8eaf48). Slot 0x248
  (0x4a639fc8) shows it with the title "Cheats" (literal at 0x4a63a08c);
  slot 0x290 (0x4a63a090) builds the buttons below the title (+0x140) in a
  container (+0x20c); slot 0x294 (0x4a63d248) is the button factory, which
  keeps every button in the vector at +0x210.
- Buttons: 0x4a692ca4(style, text, id, 0xffffff80, 127.5f, 255.0f, font,
  0). Styles index the 9-slice skins at 0x4a8f4088: 0 Dark, 1 White,
  2 Green, 3 Silver, 4 Yellow, 5 Beige. Clicks: 0x4a2025c4(btn+0x114, 2,
  &{obj, fn, adj}) calls fn(obj + adj/2, &event), event first word = id.
- Original actions (0x4a63a3e0): 0 Clear = scene (0x4a22c904) slot 7, then
  0x4a25e424 (delete the save files, quit via 0x4a6ad268); 1
  0x4a27037c(1000000, 0) coins; 2 0x4a2703c4(1000000, 0, 7, 0) gems;
  3 level +1; 4 for each currency in 0x4a6af74c(): 0x4a28a23c(0x4a27b924(),
  id, 1000000); 5 reset the ad counter. Each ends with 0x4a6ac6a4.
- Unlimited energy is the user's byte +0x2a8: set 0x4a285fa4, read
  0x4a285f9c.
- Dialogs: 0x4a50f6a4(&title, &text, &ok, &cancel, 0, 1, 0, 0, 0, &"", 0,
  0) shows a SimpleDialog (title and text are plain, the buttons are
  localisation keys; an unknown key shows as "err_KEY"). Its first button
  is at +0x180 after __dynamic_cast (0x4a791850) from DialogObject
  (0x4a8cc5ac) to SimpleDialog (0x4a8b8888). The dialog factory is
  0x4a51c1b0; type 4 is the quit dialog (ExitDialog, 0x4a376e74).
- Level +1 (original case 3): profile 0x4a27b924(), XP the next level
  needs 0x4a287f24(profile), awarded through 0x4a28fe44(profile, xp),
  which levels up and stops at the cap; then 0x4a6ac6d4 (dialog refresh).
  0x4a266ed4 (manager) and 0x4a27b924 (manager+0xb1c, the current user)
  are singletons and ignore r0.

## Controller scale

- Profile key `ControllerScale` (users.xml). Getter 0x4a67c364: the
  profile's value, or !byte 0x4a8fa440 when absent; nonzero = Large.
  Toggle 0x4a67c540 (only caller: the Settings click 0x4a3db2d4).
- The HUD (0x4a3f0cd0, 0x4a3f0d3c) scales its controls by 1.32 when Large.
- Advanced Settings row: icon at dlg+0x238, label slot dlg+0x25c, written
  by 0x4a3d97ac ("Settings_Controller_Scale: Large/Small") through
  0x4a3dbe68(dlg, slot, anchor, text), which places a size-64 label right
  of the anchor. Music/sound/GDPR rows use slots +0x264/+0x260/+0x268.
- ui.c forces Large and turns the row into "Controls".

## Pause dialog (PauseFightDialog)

- Constructor 0x4a3901d0, vtable 0x4a8c4330; slot 0x26c (0x4a390c60)
  builds the buttons into the body dlg+0x174: home (-300,-104), music
  (-100,-104), sound, play (300,-104), all through slot 0x2a4
  (0x4a38fd9c) from the batchPause atlas, scaled 2.08 (72 px icons).
- Image button: 0x4a161010(btn[0x1b8], normal, pressed, id, z, 255f,
  255f, atlas, 1); with atlas NULL the images are files (0x4a1608b8 ->
  0x4a19013c). Then slot 0x284(name), 0x4a1607dc(press scale 1.3).
  z is *(int *)0x4a8f3fb0 - 1.

## UI building blocks

- Sprite from a file: 0x4a19013c(path, -1), path relative to the asset
  set ("textures/..." opens "assets/768/textures/...").
- Label in the game font: 0x4a6964c8(text, 0, 2) ("sakkal"), size
  0x4a184700(label, px). Its origin is the centre of its text.
- Text button: 0x4a692ca4(style, ...); the last-but-one argument is the
  skin atlas (NULL loads files). Dialog buttons pass dlg+0x158.
- CCNode vtable: 0x40 setScale, 0x54 setPosition(x, y), 0x5c/0x60 X,
  0x64/0x68 Y, 0x88/0x8c content size, 0x90 setVisible, 0xc8 addChild.
- SimpleDialog 0x4a50f6a4: 0 buttons if ok and cancel are empty, 1 if
  only cancel is; a6 picks the button skin (1 beige, 0 brown). The
  message label is dlg+0x1bc and the dialog's height follows it.
  0x4a324678 reads the content as a label (+0x128), so it cannot be a
  plain sprite. Its scroll ends are fight/popups/result/Background_Left
  and _Right.png, shipped as Background_left/right.png: found on the
  Vita's case-insensitive exFAT, shown as misc/no_image.png ("?") on the
  Linux harness.

## vitaGL gaps the game hits

Found with gl.c's GL error tracer (every integer-only GL import checks
glGetError after the call and logs the first failures by name).

- glGetFloatv(GL_SCISSOR_BOX): rejected (INVALID_ENUM), output left
  untouched. The zone 2+ arenas save and restore the scissor box with it,
  so they were clipped to garbage: scattered tiles over stale frames. gl.c
  tracks glScissor and answers GL_SCISSOR_BOX itself.
- glBufferSubData into a buffer that earlier draws of the same frame
  still use: vitaGL runs the frame's draws later from the buffer's one
  copy, so they all read the last data written. The zone 2+ arenas
  stream their sprites through one vertex buffer this way and drew the
  right images in the wrong places (Mesa copies, the harness looked
  fine). gl.c keeps a RAM copy of each buffer and turns every partial
  update into a whole glBufferData, which vitaGL gives new memory.
- Harmless at boot: glEnable(GL_LIGHTING), glDisable(0x809e/0x80a0
  multisample), glPixelStorei(GL_PACK/UNPACK_ALIGNMENT),
  glHint(GL_GENERATE_MIPMAP_HINT).
