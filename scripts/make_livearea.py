#!/usr/bin/env python3
"""Compose the Vita's LiveArea artwork.

    GAME_OUT=<game_out> scripts/make_livearea.py <apk_dir> <sce_sys_dir>

Sony fixes the sizes: icon0 128x128, the LiveArea background 840x500 and the
gate 280x158. Every one must be an 8-bit palette PNG -- truecolour makes the
install fail at 99% with 0x8010113D. scripts/check_sce_sys.py enforces that.

icon0 is the game's own icon. bg and the gate come from art/key_art.png
(the Shadow Fight 2 key art, 1200x554): style a1 puts the gate on the
right half of the background, so the characters are cut out of the art and
fitted into the left half, over its parchment with them painted out. The
gate is the logo.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_ui_art import CREAM, GOLD, text  # noqa: E402  the game's font

KEY_ART = Path(__file__).resolve().parents[1] / "art/key_art.png"
BG_W, BG_H = 840, 500

# In key_art.png: the columns holding the cast and the logo (everything else
# is parchment), the cast's top (below it only the logo), a parchment-only
# strip for grain, the gate's crop (at the gate's 280:158) and the logo's
# vertical centre.
CAST_X = (250, 955)
CAST_TOP = 150
GRAIN = (960, 0, 1200, 554)
GATE_CROP = (395, 0, 795, 226)
LOGO_MID = 76

CAST_RIGHT = 480   # the gate is right of this, so nobody is under it
MARGIN = 16


def palette(image):
    """Flatten onto black and quantise, which is the format the Vita wants."""
    flat = Image.new("RGB", image.size, (0, 0, 0))
    flat.paste(image, (0, 0), image if image.mode == "RGBA" else None)
    return flat.convert("P", palette=Image.ADAPTIVE, colors=256)


def lerp_rows(img, x0, x1, band=60):
    """img with columns x0..x1 replaced, row by row, by a blend from the
    colour left of them to the colour right of them: the parchment alone."""
    a = np.asarray(img, dtype=float).copy()
    left = a[:, x0 - band:x0].mean(1)[:, None, :]
    right = a[:, x1:x1 + band].mean(1)[:, None, :]
    t = ((np.arange(x0, x1) - x0) / (x1 - x0))[None, :, None]
    a[:, x0:x1] = left * (1 - t) + right * t
    return Image.fromarray(a.clip(0, 255).astype(np.uint8))


def matte(art, clean):
    """Alpha of what is not parchment: soft where the art differs from the
    clean parchment, and solid inside the cast's outline, whose skin and
    glows can be parchment-coloured."""
    diff = ImageChops.difference(art, clean)
    diff = Image.fromarray(np.asarray(diff).max(2))
    soft = diff.point(lambda v: max(0, min(255, (v - 35) * 255 // 45)))
    hard = soft.point(lambda v: 255 if v > 128 else 0)
    hard.paste(0, (0, 0, hard.width, CAST_TOP))
    hard = hard.filter(ImageFilter.MaxFilter(13)).filter(ImageFilter.MinFilter(13))
    w, h = hard.size
    flood = Image.new("L", (w + 2, h + 2))
    flood.paste(hard, (1, 1))
    ImageDraw.floodfill(flood, (0, 0), 128)   # the outside; the rest is the cast
    solid = flood.crop((1, 1, w + 1, h + 1)).point(lambda v: 0 if v == 128 else 255)
    solid = solid.filter(ImageFilter.MinFilter(7)).filter(ImageFilter.GaussianBlur(2))
    alpha = ImageChops.lighter(soft, solid)
    # Fade out at the art's top and at the cut's sides, where haze is cut off.
    fade = Image.new("L", alpha.size, 255)
    d = ImageDraw.Draw(fade)
    for i in range(24):
        v = 255 * i // 24
        d.line([(0, i), (w, i)], fill=v)
        d.line([(CAST_X[0] + i, 24), (CAST_X[0] + i, h)], fill=v)
        d.line([(CAST_X[1] - 1 - i, 24), (CAST_X[1] - 1 - i, h)], fill=v)
    return ImageChops.multiply(alpha, fade)


def background(art):
    clean = lerp_rows(art, *CAST_X).filter(ImageFilter.GaussianBlur(10))
    # Parchment, stretched to the LiveArea, with the strip's grain put back.
    bg = np.asarray(clean.resize((BG_W, BG_H), Image.LANCZOS), dtype=float)
    strip = art.crop(GRAIN)
    grain = np.asarray(strip, dtype=float) - np.asarray(strip.filter(ImageFilter.GaussianBlur(6)), dtype=float)
    grain = np.concatenate([grain, grain[:, ::-1]] * 2, axis=1)[:, :BG_W]
    grain = np.asarray(Image.fromarray((grain + 128).clip(0, 255).astype(np.uint8)).resize((BG_W, BG_H)),
                       dtype=float) - 128
    bg = Image.fromarray((bg + grain).clip(0, 255).astype(np.uint8)).convert("RGBA")
    # The cast and the logo, cut out and fitted into the left half, feet on
    # the bottom edge as in the key art.
    cut = art.convert("RGBA")
    cut.putalpha(matte(art, clean))
    cut = cut.crop((CAST_X[0], 0, CAST_X[1], art.height))
    k = (CAST_RIGHT - MARGIN) / cut.width
    cut = cut.resize((round(cut.width * k), round(cut.height * k)), Image.LANCZOS)
    bg.alpha_composite(cut, (MARGIN, BG_H - cut.height))
    sig = text("by franciscoclou", 40, CREAM, gradient=GOLD)
    bg.alpha_composite(sig, (BG_W - sig.width - 18, BG_H - sig.height - 14))
    return bg


def gate(art):
    """The logo alone, centred on its parchment."""
    clean = lerp_rows(art, *CAST_X).filter(ImageFilter.GaussianBlur(10))
    alpha = matte(art, clean).point(lambda v: max(0, v - 128) * 2)   # the ink, not the haze
    alpha.paste(0, (0, CAST_TOP - 8, alpha.width, alpha.height))
    logo = art.convert("RGBA")
    logo.putalpha(alpha)
    im = clean.convert("RGBA")
    x0, y0, x1, y1 = GATE_CROP
    im.alpha_composite(logo, (0, (y1 - y0) // 2 - LOGO_MID))
    return im.crop(GATE_CROP).resize((280, 158), Image.LANCZOS)


def main():
    apk, out = Path(sys.argv[1]), Path(sys.argv[2])
    icon = Image.open(apk / "res/drawable-xxxhdpi-v4/icon.png").convert("RGBA")
    art = Image.open(KEY_ART).convert("RGB")
    contents = out / "livearea/contents"
    contents.mkdir(parents=True, exist_ok=True)
    palette(icon.resize((128, 128), Image.LANCZOS)).save(out / "icon0.png")
    palette(background(art)).save(contents / "bg.png")
    palette(gate(art)).save(contents / "startup.png")


if __name__ == "__main__":
    main()
