#!/usr/bin/env bash
# Shared configuration for every script in this repo.
# Override any value by exporting it before calling a script,
# or by editing scripts/env.local.sh (gitignored -- copy env.local.sh.example).

# --- debug log -------------------------------------------------------------
# The game sends UDP log packets here. Set PC_IP to your PC's address.
PC_IP="${PC_IP:-255.255.255.255}"
LOG_PORT="${LOG_PORT:-18194}"

# --- paths -----------------------------------------------------------------
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
APK_DIR="${APK_DIR:-$REPO_ROOT/apk}"
GAME_OUT="${GAME_OUT:-$REPO_ROOT/game_out}"

# --- toolchain -------------------------------------------------------------
# All compilation runs in Docker.
DOCKER_IMAGE="${DOCKER_IMAGE:-shadowfight2-vitasdk:latest}"

if [ -f "$REPO_ROOT/scripts/env.local.sh" ]; then . "$REPO_ROOT/scripts/env.local.sh"; fi
