#!/usr/bin/env bash
# Shared configuration for every script in this repo.
# Override any value by exporting it before calling a script,
# or by editing scripts/env.local.sh (gitignored -- copy env.local.sh.example).

# --- network ---------------------------------------------------------------
# The Vita's VitaShell/FTP server. PLACEHOLDER: set the real address in
# scripts/env.local.sh -- it changes with whatever network the Vita is on.
VITA_IP="${VITA_IP:-192.168.1.177}"
VITA_FTP_PORT="${VITA_FTP_PORT:-1337}"
# This PC. The game sends UDP log packets here.
PC_IP="${PC_IP:-192.168.1.149}"
LOG_PORT="${LOG_PORT:-18194}"

# --- app identity ----------------------------------------------------------
TITLE_ID="${TITLE_ID:-SHDF00002}"
APP_NAME="${APP_NAME:-Shadow Fight 2}"
VPK_NAME="${VPK_NAME:-ShadowFight2.vpk}"
# Where the loader expects the game's files on the Vita.
VITA_DATA_DIR="${VITA_DATA_DIR:-ux0:/data/shadowfight2}"   # FTP path form

# --- paths -----------------------------------------------------------------
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
DEBUG_LOG="${DEBUG_LOG:-$REPO_ROOT/debug.log}"
DUMP_DIR="${DUMP_DIR:-$REPO_ROOT/dumps}"
APK_DIR="${APK_DIR:-$REPO_ROOT/apk}"
GAME_OUT="${GAME_OUT:-$REPO_ROOT/game_out}"

# --- toolchain -------------------------------------------------------------
# The host vitasdk in ~/vitasdk is unusable on this machine (its binaries need
# glibc 2.38, Ubuntu 22.04 has 2.35), so all compilation runs in Docker.
DOCKER_IMAGE="${DOCKER_IMAGE:-shadowfight2-vitasdk:latest}"

if [ -f "$REPO_ROOT/scripts/env.local.sh" ]; then . "$REPO_ROOT/scripts/env.local.sh"; fi

# Anything derived from the values above has to come after the local overrides,
# or a VITA_IP set in env.local.sh would leave FTP_URL pointing at the default.
FTP_URL="ftp://${VITA_IP}:${VITA_FTP_PORT}"
