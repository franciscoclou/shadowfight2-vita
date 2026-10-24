#!/usr/bin/env python3
"""Make the "hidden on-screen controls" texture atlases.

    scripts/make_hidden_hud.py <game_out>

For every resolution set under <game_out>/assets, writes copies of the
fight-button and joystick atlases to <game_out>/hud_hidden/assets/... with
the control graphics made transparent. The loader serves these instead of
the originals when touch_controls = 0 in controls.txt (src/s3e/file.c).

Kept visible: the pause button, the magic charge ring and its full icon,
the ranged-weapon ring and its full icon, and the tutorial highlight.
Erased: the punch/kick/throw/magic/charge discs and the whole joystick.
"""
import plistlib
import re
import sys
from pathlib import Path

from PIL import Image

BUTTONS = "textures/buttons/fight/batchButtonsFight"
JOYSTICK = "textures/joystick/batchJoystick"
ERASE_BUTTONS = {
    "btn_charge_action.png", "btn_charge_normal.png",
    "btn_kick_action.png", "btn_kick_normal.png",
    "btn_magic_action.png", "btn_magic_normal.png",
    "btn_punch_action.png", "btn_punch_normal.png",
    "btn_throw_action.png", "btn_throw_normal.png",
}


def frame_box(entry):
    x, y, w, h = map(int, re.findall(r"-?\d+", entry["frame"]))
    if entry.get("rotated"):
        w, h = h, w
    return x, y, x + w, y + h


def blank(src_base, dst_base, erase):
    plist = plistlib.load(open(f"{src_base}.plist", "rb"))
    img = Image.open(f"{src_base}.png").convert("RGBA")
    clear = Image.new("RGBA", img.size, (0, 0, 0, 0))
    for name, entry in plist["frames"].items():
        if erase is None or name in erase:
            box = frame_box(entry)
            img.paste(clear.crop(box), box)
    dst_base.parent.mkdir(parents=True, exist_ok=True)
    img.save(f"{dst_base}.png")


def main():
    game_out = Path(sys.argv[1])
    made = 0
    for res in sorted(p for p in (game_out / "assets").iterdir() if p.is_dir()):
        for atlas, erase in ((BUTTONS, ERASE_BUTTONS), (JOYSTICK, None)):
            src = res / atlas
            if Path(f"{src}.plist").exists():
                blank(src, game_out / "hud_hidden" / "assets" / res.name / atlas, erase)
                made += 1
    print(f">> hud_hidden: {made} atlases")


if __name__ == "__main__":
    main()
