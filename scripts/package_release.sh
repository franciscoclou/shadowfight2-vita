#!/usr/bin/env bash
# Build the public release and gather what gets published in release/<version>/.
#
#   ./scripts/package_release.sh v1.0
#
# The ELF goes in release/<version>-symbols/, not for publishing: it is what
# reads crash dumps from this exact build.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

VERSION="$1"
OUT="$REPO_ROOT/release/$VERSION"
"$REPO_ROOT/scripts/build.sh" release clean >/dev/null
BUILD_DIR="$REPO_ROOT/build-release"

rm -rf "$OUT" "$OUT-symbols"
mkdir -p "$OUT" "$OUT-symbols"
cp "$BUILD_DIR/ShadowFight2.vpk" "$REPO_ROOT/scripts/make_game_data.py" "$OUT/"
cp "$BUILD_DIR/shadowfight2" "$OUT-symbols/"

# One download for anyone not using GitHub.
cat > "$OUT/INSTALL.txt" <<TXT
Shadow Fight 2 for PS Vita $VERSION

This contains no game files. You need your own APK of Shadow Fight 2 1.9.38
(versionCode 1000109, armeabi-v7a).

Full instructions and troubleshooting:
https://github.com/franciscoclou/shadowfight2-vita

1. Vita setup, once: put kubridge.skprx in ux0:tai/, add it under *KERNEL in
   ux0:tai/config.txt and reboot. Then run ShaRKBR33D to extract
   libshacccg.suprx.
2. Install ShadowFight2.vpk with VitaShell.
3. Install Python 3 on your PC, then run make_game_data.py with your APK. It
   needs about 450 MB of space and downloads about 300 MB from Nekki's servers.
4. Copy the shadowfight2 folder it makes to ux0:data/ on the Vita.
5. Start the game. When the first screen appears, close it and start it again:
   the content packs are only picked up from the second launch on.
TXT
(cd "$OUT" && python3 -I -c "
import zipfile
z = zipfile.ZipFile('ShadowFight2-Vita-$VERSION.zip', 'w', zipfile.ZIP_DEFLATED)
for f in ('ShadowFight2.vpk', 'make_game_data.py', 'INSTALL.txt'):
    z.write(f)
z.close()")
(cd "$OUT" && sha256sum ShadowFight2.vpk make_game_data.py "ShadowFight2-Vita-$VERSION.zip" > SHA256SUMS.txt && cat SHA256SUMS.txt)
echo ">> release/$VERSION/ is ready to publish"
