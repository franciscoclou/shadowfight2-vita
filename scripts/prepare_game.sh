#!/usr/bin/env bash
# Build game_out/ -- what the loader reads from ux0:data/shadowfight2/.
#
#   ./scripts/prepare_game.sh
#
# The Marmalade game code is not a .so: it is apk/assets/ShadowFight2.s3e, an
# LZMA-compressed XE3U image that libs3e_android.so loads at runtime. This
# decompresses it, and copies the game's data tree and .icf config next to it.
# Nothing here is committed. Deploy it with option (C) of ./scripts/deploy.sh.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

rm -rf "$GAME_OUT"
mkdir -p "$GAME_OUT"

echo ">> ShadowFight2.s3e"
python3 -I "$REPO_ROOT/scripts/unpack_s3e.py" \
  "$APK_DIR/assets/ShadowFight2.s3e" "$GAME_OUT/ShadowFight2.s3e.raw"

echo ">> config and data"
cp "$APK_DIR"/assets/*.icf "$APK_DIR"/assets/*.txt "$APK_DIR"/assets/*.xml "$GAME_OUT/"
cp -r "$APK_DIR/assets/assets" "$GAME_OUT/assets"

echo
echo ">> game_out/ is $(du -sh "$GAME_OUT" | cut -f1)"
echo ">> deploy it with option (C) from ./scripts/deploy.sh"
