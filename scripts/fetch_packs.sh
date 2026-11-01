#!/usr/bin/env bash
# Download the game's content packs into game_out/ as <NAME>.dz, once.
#
#   ./scripts/fetch_packs.sh
#
# Needs ./scripts/prepare_game.sh first. See make_game_data.py. Copy them to
# ux0:data/shadowfight2/ on the Vita.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

python3 -I "$REPO_ROOT/scripts/make_game_data.py" -o "$GAME_OUT" --packs-only
