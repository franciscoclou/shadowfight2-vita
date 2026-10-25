#!/usr/bin/env python3
"""Reference images for tests/harness/test_hud.c.

    scripts/hud_reference.py <game_out> <out_dir>

Writes the game's 768 fight-button atlas as raw RGBA (atlas.rgba) and the
same atlas with the control frames cleared (atlas_hidden.rgba), computed
independently of src/s3e/hud.c with PIL and plistlib.
"""
import plistlib
import re
import sys
from pathlib import Path

from PIL import Image

ERASE = {
    "btn_charge_action.png", "btn_charge_normal.png", "btn_kick_action.png",
    "btn_kick_normal.png", "btn_magic_action.png", "btn_magic_normal.png",
    "btn_punch_action.png", "btn_punch_normal.png", "btn_throw_action.png",
    "btn_throw_normal.png",
}

game_out, out = Path(sys.argv[1]), Path(sys.argv[2])
base = game_out / "assets/768/textures/buttons/fight/batchButtonsFight"
out.mkdir(parents=True, exist_ok=True)
img = Image.open(f"{base}.png").convert("RGBA")
(out / "atlas.rgba").write_bytes(img.tobytes())
clear = Image.new("RGBA", img.size, (0, 0, 0, 0))
for name, entry in plistlib.load(open(f"{base}.plist", "rb"))["frames"].items():
    if name in ERASE:
        x, y, w, h = map(int, re.findall(r"-?\d+", entry["frame"]))
        if entry.get("rotated"):
            w, h = h, w
        img.paste(clear.crop((x, y, x + w, y + h)), (x, y, x + w, y + h))
(out / "atlas_hidden.rgba").write_bytes(img.tobytes())
