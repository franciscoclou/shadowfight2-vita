#!/usr/bin/env bash
# One-time bootstrap. Safe to re-run.
#
#   ./scripts/setup.sh
#
# Installs the third-party tools into tools/ (gitignored) and builds the
# vitasdk Docker image. Nothing here touches the Vita.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

TOOLS="$REPO_ROOT/tools"
mkdir -p "$TOOLS" "$DUMP_DIR"

# --- vita-parse-core: symbolises psp2core crash dumps ------------------------
VPC="$TOOLS/vita-parse-core"
if [ ! -d "$VPC" ]; then
  echo ">> cloning vita-parse-core"
  git clone -q --depth 1 https://github.com/xyzz/vita-parse-core.git "$VPC"
else
  echo ">> vita-parse-core already cloned"
fi

# It targets pyelftools 0.24, whose elftools.common.py3compat is gone from
# current releases. Both helpers it wants are one-liners, so inline them
# instead of pinning an ancient pyelftools.
if grep -q "from elftools.common.py3compat import" "$VPC"/*.py 2>/dev/null; then
  echo ">> patching vita-parse-core for modern pyelftools"
  cat > "$VPC/py3compat_shim.py" <<'SHIM'
"""Replacement for the removed elftools.common.py3compat helpers."""


def str2bytes(s):
    return s.encode('latin-1') if isinstance(s, str) else s


def bytes2str(b):
    return b.decode('latin-1') if isinstance(b, bytes) else b
SHIM
  sed -i 's/^from elftools\.common\.py3compat import /from py3compat_shim import /' "$VPC"/*.py
fi

# --- docker build image ------------------------------------------------------
echo ">> building docker image $DOCKER_IMAGE"
docker build -t "$DOCKER_IMAGE" "$REPO_ROOT/docker"

echo
echo "=========================================================="
echo " setup complete"
echo
echo "   ./scripts/prepare_game.sh  apk/ -> game_out/ (decompressed .s3e + data)"
echo "   ./scripts/build.sh         build the VPK"
echo "   ./scripts/log.sh           watch the live debug log"
echo "   ./scripts/getdump.sh       pull a crash dump off the Vita"
echo "   ./scripts/parsedump.sh     symbolise it"
echo "=========================================================="
