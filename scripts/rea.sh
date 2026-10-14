#!/usr/bin/env bash
# Run REA (Ghidra-backed reverse engineering) against a project binary.
#
#   ./scripts/rea.sh decompile libs3e s3eSurfaceGetInt
#   ./scripts/rea.sh function  game    0x4a791144
#   ./scripts/rea.sh <any rea command> <target> [args...]
#
# Targets: libs3e = apk/lib/armeabi-v7a/libs3e_android.so
#          game   = ghidra/ShadowFight2.elf (the XE3U image rewrapped as an
#                   ELF by scripts/s3e_info.py, with import/veneer symbols)
# Analysis snapshots are kept in ghidra/ (gitignored) so repeat queries
# do not re-run auto-analysis. REA is also registered as an MCP server
# ("rea") for Claude Code; see CLAUDE.md.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

export GHIDRA_INSTALL_DIR="${GHIDRA_INSTALL_DIR:-$HOME/.local/share/ghidra_12.1.4_PUBLIC}"
export JAVA_HOME="${JAVA_HOME_REA:-$HOME/.local/share/jdk-25.0.4.1+1}"
export PATH="$JAVA_HOME/bin:$PATH"
REA="${REA:-node /home/francisco/rea/scripts/rea.mjs}"

cmd="$1"; target="$2"; shift 2
mkdir -p "$REPO_ROOT/ghidra"
case "$target" in
  libs3e) path="$APK_DIR/lib/armeabi-v7a/libs3e_android.so" ;;
  game)
    path="$REPO_ROOT/ghidra/ShadowFight2.elf"
    if [ ! -f "$path" ] || [ "$GAME_OUT/ShadowFight2.s3e.raw" -nt "$path" ]; then
      python3 -I "$REPO_ROOT/scripts/s3e_info.py" "$GAME_OUT/ShadowFight2.s3e.raw" elf "$path"
    fi ;;
  *) path="$target" ;;
esac
exec $REA "$cmd" "$path" "$@" --snapshot "$REPO_ROOT/ghidra/$(basename "$path").snapshot"
