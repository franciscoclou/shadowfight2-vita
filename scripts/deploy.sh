#!/usr/bin/env bash
# Prints the commands to copy the build onto the Vita.
# It deliberately does NOT transfer anything -- you run these yourself.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

VPK="$BUILD_DIR/$VPK_NAME"
EBOOT="$BUILD_DIR/eboot.bin"

cat <<TXT
------------------------------------------------------------------
 SEND TO VITA   (Vita $VITA_IP:$VITA_FTP_PORT, title $TITLE_ID)
------------------------------------------------------------------

 A) Fast path -- code changes only (usual case).
    Replaces just the executable of an already-installed app:

    curl -T "$EBOOT" "$FTP_URL/ux0:/app/$TITLE_ID/eboot.bin"

    Then close and relaunch the app on the Vita.

 B) Full install -- first time, or when LiveArea / param.sfo changed:

    curl -T "$VPK" "$FTP_URL/ux0:/data/$VPK_NAME"

    Then on the Vita open VitaShell, browse to ux0:/data/,
    press X on $VPK_NAME and confirm the install.

 C) Game data -- once, and again whenever ./scripts/prepare_game.sh
    changes what it produces (the decompressed .s3e and the
    game's assets/ tree, ~90 MB). Existing files are overwritten.
    ram/ is skipped: it holds the save data the PC harness wrote.

    cd "$GAME_OUT" && find . -path ./ram -prune -o -type f -print |
      while read -r f; do
        echo "\${f#./}"
        curl -sS --ftp-create-dirs -T "\$f" "$FTP_URL/$VITA_DATA_DIR/\${f#./}" ||
          echo "!! failed: \$f"
      done; cd - >/dev/null

 Prerequisites on the Vita (once): kubridge.skprx in ux0:tai/config.txt
 under *KERNEL, and ur0:data/libshacccg.suprx extracted.

------------------------------------------------------------------
TXT
