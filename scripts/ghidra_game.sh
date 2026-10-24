#!/usr/bin/env bash
# Decompile functions in the game image with a persistent headless Ghidra
# project (ghidra/project, gitignored). The first run imports and
# auto-analyses the image (~5-10 min); later runs reuse the database.
#
#   ./scripts/ghidra_game.sh 0x4a7528cc 0x4a753300 ...
#   ./scripts/ghidra_game.sh xrefs 0x4a7fe374 ...   references to addresses
#
# Addresses are link-time (base 0x4a000000), as the loader's crash log
# prints them. For libs3e_android.so use ./scripts/rea.sh instead: REA
# re-analyses on every query, which is fine for that small library but
# takes ~5 minutes per query on the 9 MB game image.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

GHIDRA="${GHIDRA_INSTALL_DIR:-$HOME/.local/share/ghidra_12.1.4_PUBLIC}"
export JAVA_HOME="${JAVA_HOME_REA:-$HOME/.local/share/jdk-25.0.4.1+1}"
export PATH="$JAVA_HOME/bin:$PATH"
PROJ="$REPO_ROOT/ghidra/project"
ELF="$REPO_ROOT/ghidra/ShadowFight2.elf"
mkdir -p "$PROJ"
SCRIPT=DecompileAt.java
if [ "${1:-}" = xrefs ]; then SCRIPT=XrefsTo.java; shift; fi

if [ ! -f "$ELF" ] || [ "$GAME_OUT/ShadowFight2.s3e.raw" -nt "$ELF" ]; then
  python3 -I "$REPO_ROOT/scripts/s3e_info.py" "$GAME_OUT/ShadowFight2.s3e.raw" elf "$ELF"
fi
if [ ! -d "$PROJ/SF.rep" ]; then
  echo ">> first run: importing and analysing the game image (several minutes)" >&2
  "$GHIDRA/support/analyzeHeadless" "$PROJ" SF -import "$ELF" -max-cpu 8 \
      >"$REPO_ROOT/ghidra/import.log" 2>&1
fi
"$GHIDRA/support/analyzeHeadless" "$PROJ" SF -process ShadowFight2.elf -noanalysis \
    -scriptPath "$REPO_ROOT/scripts/ghidra" -postScript "$SCRIPT" "$@" 2>&1 \
  | sed -n '/====/,$p' \
  | sed -E 's/^INFO  [A-Za-z]+\.java> (.*) \(GhidraScript\) *$/\1/' \
  | grep -v -E '^INFO |^WARN |^    /|\(GhidraScript\)|^ *$'
