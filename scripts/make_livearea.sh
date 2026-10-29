#!/usr/bin/env bash
# Regenerate the VPK's sce_sys artwork from the original Android app icon.
#
#   ./scripts/make_livearea.sh
#
# These PNGs must be 8-bit PALETTE (PNG colour type 3, with a PLTE chunk).
# A truecolour RGB PNG makes scePromoterUtility reject the package at 99%
# with error 0x8010113D. Sizes are fixed by Sony: icon0 128x128,
# startup 280x158, bg 840x500.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

cd "$REPO_ROOT"
GAME_OUT="$GAME_OUT" python3 scripts/make_livearea.py "$APK_DIR" sce_sys

# The gate's place comes from the style: a1 centres it, psmobile puts it
# on the right, where make_livearea.py leaves room for it.
cat > sce_sys/livearea/contents/template.xml <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<livearea style="psmobile" format-ver="01.00" content-rev="1">
  <livearea-background>
    <image>bg.png</image>
  </livearea-background>
  <gate>
    <startup-image>startup.png</startup-image>
  </gate>
</livearea>
XML

# Fail loudly rather than shipping a VPK that will not install.
python3 scripts/check_sce_sys.py

echo
echo ">> sce_sys artwork regenerated -- rebuild and reinstall the full VPK"
