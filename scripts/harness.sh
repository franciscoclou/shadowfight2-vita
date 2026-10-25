#!/usr/bin/env bash
# Boot the real game on the PC: the loader + s3e shims built for Linux
# armhf, run under qemu-arm with Mesa's software GLES2. Frames land in
# tests/harness/out/ as PNGs. No Vita needed.
#
#   ./scripts/harness.sh                 build and run (300 frames)
#   SF_MAX_FRAMES=900 ./scripts/harness.sh
#   SF_TAPS="400:480:400" ./scripts/harness.sh   tap (480,400) at frame 400
#
# Uses game_out/ from ./scripts/prepare_game.sh. The run log is
# tests/harness/out/run.log.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

IMAGE=shadowfight2-harness:latest
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build -t "$IMAGE" "$REPO_ROOT/docker/harness"
fi
OUT="$REPO_ROOT/tests/harness/out"
rm -rf "$OUT"; mkdir -p "$OUT"

docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -e SF_MAX_FRAMES="${SF_MAX_FRAMES:-300}" -e SF_SHOT_EVERY="${SF_SHOT_EVERY:-30}" \
  -e SF_TAPS="${SF_TAPS:-}" -e SF_LOAD_ADDR="${SF_LOAD_ADDR:-0x98000000}" -e SF_PROFILE="${SF_PROFILE:-0}" -e SF_OUT=/src/tests/harness/out \
  -e EGL_PLATFORM=surfaceless -e LIBGL_ALWAYS_SOFTWARE=1 -e GALLIUM_DRIVER="${GALLIUM_DRIVER:-softpipe}" \
  -v "$REPO_ROOT:/src" -w /src/tests/harness "$IMAGE" bash -c '
    set -e
    make -s -B harness VERBOSE='"${SF_VERBOSE:-0}"'
    timeout '"${SF_TIMEOUT:-1800}"' qemu-arm ./harness /src/game_out > /src/tests/harness/out/run.log 2>&1 || echo "exit status $?" >> /src/tests/harness/out/run.log
    for f in /src/tests/harness/out/*.ppm; do
      [ -e "$f" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[1][:-4]+\".png\")" "$f" && rm "$f"
    done; true'
tail -n 25 "$OUT/run.log"
ls "$OUT"/*.png 2>/dev/null | tail -n 3 || true
