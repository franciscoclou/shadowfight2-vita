#!/usr/bin/env bash
# Build game_out/ -- what the loader reads from ux0:data/shadowfight2/.
#
#   ./scripts/prepare_game.sh
#
# Starts game_out/ afresh from the APK unzipped in apk/, keeping the content
# packs. See make_game_data.py. Copy the result to ux0:data/shadowfight2/ on
# the Vita.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

# Keep the content packs from ./scripts/fetch_packs.sh: they are ~300 MB
# from Nekki's CDN and nothing here changes them.
PACKS_KEEP="$(mktemp -d)"
trap 'rm -rf "$PACKS_KEEP"' EXIT
mv "$GAME_OUT"/*.dz "$PACKS_KEEP"/ 2>/dev/null || true
rm -rf "$GAME_OUT"
mkdir -p "$GAME_OUT"
mv "$PACKS_KEEP"/*.dz "$GAME_OUT"/ 2>/dev/null || true

python3 -I "$REPO_ROOT/scripts/make_game_data.py" "$APK_DIR" -o "$GAME_OUT" --no-packs
echo ">> game_out/ is $(du -sh "$GAME_OUT" | cut -f1)"
