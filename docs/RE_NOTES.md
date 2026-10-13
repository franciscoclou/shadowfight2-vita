# Reverse-engineering notes

Build: Shadow Fight 2 1.9.38 (versionCode 1000109), armeabi-v7a.

## ShadowFight2.s3e container

| offset | value | meaning |
|---|---|---|
| 0x00 | `5D` | LZMA properties (lc=3, lp=0, pb=2) |
| 0x01 | `00 00 01 00` | dictionary size 0x10000 (64 KiB) |
| 0x05 | `B1 5C 94 00 00 00 00 00` | uncompressed size 9,723,057 |

- File size 3,177,132. LZMA decoding fails near offset 3,174,400, yielding
  9,710,704 bytes (12,353 short of the declared size). **Unresolved.**
- Decoded image starts with magic `XE3U` (Marmalade s3e executable).
- Imports are by name: `s3eMallocBase`, `s3eReallocBase`, `s3eFreeBase`,
  `s3eGLGetInt`, the GLES2 framebuffer/renderbuffer entry points, …

## lib/armeabi-v7a

`libs3e_android.so` (800 KB) is the Marmalade runtime that loads the .s3e;
the other `libs3e*.so` are extension bridges. libsmartfox/libs3eSmartFox are
the multiplayer client. None contain game logic.
