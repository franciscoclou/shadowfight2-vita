#!/usr/bin/env bash
# Download the game's content packs into game_out/ as <NAME>.dz.
#
#   ./scripts/fetch_packs.sh
#
# Everything after the first act (Hermit's chess yard and every later zone,
# the boss models, raids, clans) is not in the APK: the game downloads it at
# runtime as Derbh .dz packs. The port has no network, so the packs are
# fetched here once and copied next to ShadowFight2.s3e.raw on the Vita
# (ux0:data/shadowfight2/). At boot the loader lists every pack it
# finds there in ram/assets/packs.xml (src/s3e/packs.c), which is how the
# game knows a pack is installed.
#
# Nekki's CDN still serves them, but its HTTPS certificate has expired, so
# they are fetched over plain HTTP and checked against the MD5s from Nekki's
# own config (config_shadowfight_test.xml and friends): the newest Android
# (PlatformID 2) entry of each pack whose MinVersion is at most 1.9.38.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"

CDN=http://assets.nekkimobile.ru/shadowfight/packs
OUT="$GAME_OUT"
mkdir -p "$OUT"

# name  file on the CDN  MD5
PACKS="
ZONE_2    zone_2_2017-01-24_10-37.dz                  38df6b9142d34b2d2fed86cecd897b7f
ZONE_3    zone_3_2015-09-11_12-33.dz                  4ba23c69cdc69f1fc7bd0833d59aefe0
ZONE_4    zone_4_2015-09-11_12-34.dz                  e72945c6d730f30407e93d16848afc26
ZONE_5    zone_5_2015-09-11_12-35.dz                  40fae85d565a5b1da7c2b3d8402a7021
ZONE_6    zone_6_2016-03-29_17-59.dz                  2d2b9a3c751eb5107628b84f184cbc79
ZONE_7    zone_7_1_2016-09-29_12-34.dz                0bdfc9363049072bbec6aa6ffc96c000
ZONE_7_2  zone_7_2_2015-09-11_12-40.dz                202d758e8a7d9106445818f4d3f875fd
ZONE_7_3  zone_7_3_2016-09-15_12-53.dz                1c02178bd8b9a4d5df5dd97692e4feda
ZONE_IM   zone_IM_2016-09-29_12-33.dz                 b7712251f74ea26ecadb62aebf786c16
ZONE_RAID zone_raid_2017-02-08_10-42.dz               204d17701e7f8f2a862001f821b5a32a
MODELS    models_fonts_2017-02-21_16-06.dz            d68d8983fe76c2f59a2a914d38f274a7
CLANS     clans_3rd_boss_new_iteration_2019-01-30_11-00.dz d079b8592542c0909d6b7631f2247831
"

failed=0
while read -r name file md5; do
  [ -n "$name" ] || continue
  dst="$OUT/$name.dz"
  if [ -f "$dst" ]; then
    echo ">> $name: already here"
    continue
  fi
  echo ">> $name: $file"
  if ! curl -fSL --retry 3 -o "$dst.part" "$CDN/$file"; then
    echo "!! $name: download failed"; rm -f "$dst.part"; failed=1; continue
  fi
  got="$(md5sum "$dst.part" | cut -d' ' -f1)"
  if [ "$got" != "$md5" ]; then
    echo "!! $name: MD5 $got, expected $md5"; rm -f "$dst.part"; failed=1; continue
  fi
  mv "$dst.part" "$dst"
done <<< "$PACKS"

# The raid pack (2017) still carries older copies of files 1.9.38 ships in
# files.dz; left visible they make the game wipe its cache on every boot.
# This edits the packs, so the MD5s above only hold for a fresh download.
echo ">> hiding pack files that files.dz ships newer"
python3 -I "$REPO_ROOT/scripts/fix_packs.py" "$GAME_OUT/assets/files.dz" "$OUT"/*.dz

echo
echo ">> packs: $(du -ch "$OUT"/*.dz | tail -n 1 | cut -f1)"
echo ">> copy them to ux0:data/shadowfight2/ on the Vita"
exit $failed
