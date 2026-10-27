#!/usr/bin/env bash
# Copy the game's save off the Vita, so the harness can boot past the
# tutorial (the shop, pause and Settings screens need a real profile).
# Read-only on the Vita: nothing is written there.
#
#   ./scripts/pull_save.sh            -> saves/vita/ram, saves/vita/device_id
#   SF_SAVE=saves/vita ./scripts/harness.sh
#
# Every cached file is hashed with the device ID (docs/RE_NOTES.md "Save
# hashes"), so the harness must report the Vita's: it is read from the
# Vita's debug.log, where device.c logs it at boot.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

OUT="$REPO_ROOT/saves/vita"
SRC="$FTP_URL/$VITA_DATA_DIR"
if ! curl -s --max-time 10 "$SRC/" >/dev/null; then
  echo "!! no response from $FTP_URL -- is VitaShell's FTP running?" >&2
  exit 1
fi

rm -rf "$OUT"; mkdir -p "$OUT"
# wget mirrors FTP directories; --cut-dirs drops ux0:/data/shadowfight2.
( cd "$OUT" && wget -q -m -nH --cut-dirs=3 "$SRC/ram/" )
find "$OUT" -name .listing -delete

id="$(curl -s --max-time 30 "$SRC/debug.log" \
      | grep -o 's3eDeviceUniqueID provided ([0-9a-f]*)' | head -n 1 | grep -o '[0-9a-f]\{32\}' || true)"
if [ -z "$id" ]; then
  echo "!! device ID not found in the Vita's debug.log; boot the game once and retry" >&2
  exit 1
fi
echo "$id" > "$OUT/device_id"
echo ">> saved $(du -sh "$OUT/ram" | cut -f1) to $OUT (device $id)"
