#!/usr/bin/env python3
"""Compose the Vita's LiveArea artwork from the game's own icon.

    scripts/make_livearea.py <apk_dir> <sce_sys_dir>

Sony fixes the sizes: icon0 128x128, the LiveArea background 840x500 and the
gate 280x158. Every one must be an 8-bit palette PNG -- truecolour makes the
install fail at 99% with 0x8010113D. scripts/check_sce_sys.py enforces that.
Placeholder art: the icon centred on black. Swap in real art later.
"""

import sys
from pathlib import Path

from PIL import Image


def palette(image):
    """Flatten onto black and quantise, which is the format the Vita wants."""
    flat = Image.new("RGB", image.size, (0, 0, 0))
    flat.paste(image, (0, 0), image if image.mode == "RGBA" else None)
    return flat.convert("P", palette=Image.ADAPTIVE, colors=256)


def centred(icon, width, height):
    side = min(width, height)
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 255))
    scaled = icon.resize((side, side), Image.LANCZOS)
    canvas.paste(scaled, ((width - side) // 2, (height - side) // 2), scaled)
    return canvas


def main():
    apk, out = Path(sys.argv[1]), Path(sys.argv[2])
    icon = Image.open(apk / "res/drawable-xxxhdpi-v4/icon.png").convert("RGBA")
    contents = out / "livearea/contents"
    contents.mkdir(parents=True, exist_ok=True)
    palette(icon.resize((128, 128), Image.LANCZOS)).save(out / "icon0.png")
    palette(centred(icon, 840, 500)).save(contents / "bg.png")
    palette(centred(icon, 280, 158)).save(contents / "startup.png")


if __name__ == "__main__":
    main()
