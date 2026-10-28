#!/usr/bin/env python3
"""Artwork for the port's own UI, drawn with the game's font and icons.

    scripts/make_ui_art.py <game_out> <out_dir>

Writes 768-scale PNGs (the asset set the Vita loads), which ui_assets.S
embeds in the eboot:

  controls_help.png    the Vita button map shown by the CONTROLS dialog
  pause_controls.png   round CONTROLS icon for the pause dialog, in the
                       style of its home/play icons (144 px, drawn at 1.04)
  pause_selected.png   the pause icons' pressed disk, cut from the atlas
  watermark.png        GitHub mark + "franciscoclou" for the cheats screen
                       (in the parchment ink, like controls_help.png)

Text uses the game's bitmap font (fonts/eng/sakkal.fnt, Sakkal Majalla)
and the fight icons come from the fight button atlas, both read from the
1536 asset set and scaled down.
"""
import plistlib
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

game_out, out = Path(sys.argv[1]), Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)
HI = game_out / "assets/1536"

CREAM = (246, 233, 190)
GOLD = (222, 186, 104)
SHADOW = (20, 14, 8)
INK = (103, 62, 36)   # SimpleDialog's title on its parchment
ICON_RIM, ICON_MID = (255, 255, 205), (255, 255, 156)   # sampled from the pause atlas

# PlayStation symbol colours.
PS_GREEN, PS_RED, PS_BLUE, PS_PINK = (64, 226, 160), (255, 102, 102), (124, 178, 255), (240, 140, 220)


# --- the game's bitmap font -------------------------------------------------

class Font:
    def __init__(self, fnt):
        text = fnt.read_text()
        self.page = Image.open(fnt.with_name(re.search(r'file="([^"]+)"', text)[1])).convert("RGBA")
        self.size = abs(int(re.search(r"size=(-?\d+)", text)[1]))
        self.line = int(re.search(r"lineHeight=(\d+)", text)[1])
        self.base = int(re.search(r"base=(\d+)", text)[1])
        self.chars, self.kern = {}, {}
        for m in re.finditer(r"^char (.*)$", text, re.M):
            f = dict(kv.split("=") for kv in m[1].split())
            self.chars[int(f["id"])] = {k: int(v) for k, v in f.items() if k != "page" and k != "chnl"}
        for m in re.finditer(r"^kerning first=(\d+)\s+second=(\d+)\s+amount=(-?\d+)", text, re.M):
            self.kern[(int(m[1]), int(m[2]))] = int(m[3])

    def mask(self, s, px):
        """Alpha mask of s with a cap-to-baseline em of px pixels."""
        k = px / self.size
        width = 0
        prev = None
        for ch in s:
            c = self.chars.get(ord(ch))
            if c:
                width += c["xadvance"] + self.kern.get((prev, ord(ch)), 0)
            prev = ord(ch)
        big = Image.new("L", (width + self.size, self.line), 0)
        x, prev = 0, None
        for ch in s:
            c = self.chars.get(ord(ch))
            if not c:
                continue
            x += self.kern.get((prev, ord(ch)), 0)
            g = self.page.crop((c["x"], c["y"], c["x"] + c["width"], c["y"] + c["height"])).getchannel("A")
            big.paste(g, (x + c["xoffset"], c["yoffset"]), g)
            x += c["xadvance"]
            prev = ord(ch)
        big = big.crop(big.getbbox() or (0, 0, 1, 1))
        return big.resize((max(1, round(big.width * k)), max(1, round(big.height * k))), Image.LANCZOS)


FONT = Font(HI / "fonts/eng/sakkal.fnt")


def text(s, px, colour=CREAM, gradient=None, shadow=True):
    """Text sprite: optional vertical gradient, dark outline and drop shadow."""
    m = FONT.mask(s, px)
    pad = max(3, px // 10)
    w, h = m.width + 2 * pad, m.height + 2 * pad
    out = Image.new("RGBA", (w, h))
    if shadow:
        o = Image.new("L", (w, h))
        o.paste(m, (pad, pad))
        o = o.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(px / 40))
        sh = Image.new("RGBA", (w, h), SHADOW + (0,))
        sh.putalpha(o.point(lambda v: int(v * 0.9)))
        out.alpha_composite(sh, (0, 0))
        out.alpha_composite(sh, (0, max(1, px // 30)))
    fill = Image.new("RGBA", m.size, colour + (255,))
    if gradient:
        d = ImageDraw.Draw(fill)
        for y in range(m.height):
            t = y / max(1, m.height - 1)
            d.line([(0, y), (m.width, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(colour, gradient)) + (255,))
    fill.putalpha(m)
    out.alpha_composite(fill, (pad, pad))
    return out


# --- atlas frames -------------------------------------------------------------

def frame(atlas, name):
    p = plistlib.load(open(f"{atlas}.plist", "rb"))["frames"][name]
    x, y, w, h = map(int, re.findall(r"-?\d+", p["frame"]))
    img = Image.open(f"{atlas}.png").convert("RGBA")
    if p.get("rotated"):
        return img.crop((x, y, x + h, y + w)).rotate(90, expand=True)
    return img.crop((x, y, x + w, y + h))


FIGHT = HI / "textures/buttons/fight/batchButtonsFight"
PAUSE = HI / "textures/fight/pause/batchPause"


def fit(img, px):
    k = px / max(img.size)
    return img.resize((round(img.width * k), round(img.height * k)), Image.LANCZOS)


# --- glyphs (drawn at 4x, then reduced) ---------------------------------------

SS = 4


def disc_glyph(px, symbol, colour):
    """A face button: dark disc, light rim, coloured symbol."""
    n = px * SS
    im = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(im)
    d.ellipse((0, 0, n - 1, n - 1), fill=(24, 24, 28, 255))
    d.ellipse((n * 0.04, n * 0.04, n * 0.96, n * 0.96), outline=(110, 110, 118, 255), width=round(n * 0.035))
    c, r, w = n / 2, n * 0.24, round(n * 0.085)
    col = colour + (255,)
    if symbol == "cross":
        d.line((c - r, c - r, c + r, c + r), fill=col, width=w)
        d.line((c - r, c + r, c + r, c - r), fill=col, width=w)
    elif symbol == "circle":
        d.ellipse((c - r, c - r, c + r, c + r), outline=col, width=w)
    elif symbol == "square":
        d.rectangle((c - r * 0.92, c - r * 0.92, c + r * 0.92, c + r * 0.92), outline=col, width=w)
    elif symbol == "triangle":
        pts = [(c, c - r * 1.08), (c + r * 1.05, c + r * 0.75), (c - r * 1.05, c + r * 0.75)]
        d.line(pts + [pts[0]], fill=col, width=w, joint="curve")
    return im.resize((px, px), Image.LANCZOS)


def pill_glyph(label, px_h, min_w=0):
    """START / SELECT / L: a dark rounded key with its name."""
    t = FONT.mask(label, px_h * 0.62)
    w = max(min_w, t.width + px_h)
    n_w, n_h = w * SS, px_h * SS
    im = Image.new("RGBA", (n_w, n_h))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, n_w - 1, n_h - 1), radius=n_h // 2, fill=(24, 24, 28, 255),
                        outline=(110, 110, 118, 255), width=round(n_h * 0.035))
    im = im.resize((w, px_h), Image.LANCZOS)
    fill = Image.new("RGBA", t.size, (225, 225, 230, 255))
    fill.putalpha(t)
    im.alpha_composite(fill, ((w - t.width) // 2, (px_h - t.height) // 2))
    return im


def dpad_glyph(px):
    n = px * SS
    im = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(im)
    a, b = n * 0.33, n * 0.67
    arm = [(a, 0), (b, 0), (b, a), (n, a), (n, b), (b, b), (b, n), (a, n), (a, b), (0, b), (0, a), (a, a)]
    d.polygon(arm, fill=(24, 24, 28, 255), outline=(110, 110, 118, 255), width=round(n * 0.03))
    tri = n * 0.09
    for (cx, cy, dx, dy) in ((n / 2, n * 0.12, 0, -1), (n / 2, n * 0.88, 0, 1), (n * 0.12, n / 2, -1, 0), (n * 0.88, n / 2, 1, 0)):
        if dx == 0:
            pts = [(cx, cy + dy * tri), (cx - tri, cy - dy * tri * 0.6), (cx + tri, cy - dy * tri * 0.6)]
        else:
            pts = [(cx + dx * tri, cy), (cx - dx * tri * 0.6, cy - tri), (cx - dx * tri * 0.6, cy + tri)]
        d.polygon(pts, fill=(200, 200, 205, 255))
    return im.resize((px, px), Image.LANCZOS)


def stick_glyph(px):
    n = px * SS
    im = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(im)
    d.ellipse((0, 0, n - 1, n - 1), fill=(24, 24, 28, 255), outline=(110, 110, 118, 255), width=round(n * 0.035))
    r = n * 0.27
    d.ellipse((n / 2 - r, n / 2 - r, n / 2 + r, n / 2 + r), fill=(70, 70, 78, 255), outline=(150, 150, 158, 255),
              width=round(n * 0.03))
    return im.resize((px, px), Image.LANCZOS)


# --- controls_help.png --------------------------------------------------------

def controls_help():
    """One column for SimpleDialog's narrow parchment, in its title's brown."""
    row_h, glyph, icon, label_px = 78, 62, 66, 50
    pills_w = max(pill_glyph(s, 42).width for s in ("START", "SELECT"))
    rows = [
        ([disc_glyph(glyph, "cross", PS_BLUE)], "btn_kick_normal.png", "KICK"),
        ([disc_glyph(glyph, "circle", PS_RED)], "btn_punch_normal.png", "PUNCH"),
        ([disc_glyph(glyph, "square", PS_PINK)], "btn_throw_normal.png", "RANGED WEAPON"),
        ([disc_glyph(glyph, "triangle", PS_GREEN)], "btn_magic_normal.png", "MAGIC"),
        None,
        ([dpad_glyph(glyph), stick_glyph(glyph)], None, "MOVE, JUMP, CROUCH"),
        ([pill_glyph("START", 42, pills_w)], None, "PAUSE"),
        ([pill_glyph("SELECT", 42, pills_w)], None, "SHOW/HIDE TOUCH CONTROLS"),
        ([pill_glyph("L", 42, 64)], None, "DASH LEFT"),
        ([pill_glyph("R", 42, 64)], None, "DASH RIGHT"),
    ]
    keys_w = max(glyph * 2 + 12, pills_w, glyph + 14 + icon) + 22
    labels = [text(r[2], label_px, INK, shadow=False) if r else None for r in rows]
    w = keys_w + max(t.width for t in labels if t)
    gap_h = 26
    h = sum(row_h if r else gap_h for r in rows)
    im = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(im)
    y = 0
    for r, t in zip(rows, labels):
        if not r:
            d.line((10, y + gap_h // 2, w - 10, y + gap_h // 2), fill=INK + (90,), width=2)
            y += gap_h
            continue
        x = 0
        for g in r[0]:
            im.alpha_composite(g, (x, y + (row_h - g.height) // 2))
            x += g.width + 12
        if r[1]:
            fi = fit(frame(FIGHT, r[1]), icon)
            im.alpha_composite(fi, (glyph + 14, y + (row_h - fi.height) // 2))
        im.alpha_composite(t, (keys_w, y + (row_h - t.height) // 2))
        y += row_h
    im = im.crop(im.getbbox())
    im.save(out / "controls_help.png", optimize=True)
    return im


# --- pause icon ---------------------------------------------------------------

def gamepad_mask(n):
    """Line-art gamepad, the pause icons' weight, in an n-pixel square."""
    m = Image.new("L", (n, n))
    d = ImageDraw.Draw(m)
    w = round(n * 0.055)
    body = (n * 0.20, n * 0.34, n * 0.80, n * 0.66)
    d.rounded_rectangle(body, radius=n * 0.15, outline=255, width=w)
    # grips
    d.ellipse((n * 0.16, n * 0.44, n * 0.38, n * 0.74), outline=255, width=w)
    d.ellipse((n * 0.62, n * 0.44, n * 0.84, n * 0.74), outline=255, width=w)
    inner = Image.new("L", (n, n))
    di = ImageDraw.Draw(inner)
    di.rounded_rectangle((body[0] + w, body[1] + w, body[2] - w, body[3] - w), radius=n * 0.12, fill=255)
    m = ImageChops.subtract(m, inner)
    d = ImageDraw.Draw(m)
    # d-pad
    cx, cy, a = n * 0.34, n * 0.49, n * 0.055
    d.rectangle((cx - a * 2, cy - a / 2, cx + a * 2, cy + a / 2), fill=255)
    d.rectangle((cx - a / 2, cy - a * 2, cx + a / 2, cy + a * 2), fill=255)
    # face buttons
    bx, by, r = n * 0.66, n * 0.49, n * 0.032
    for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
        x, y = bx + dx * r * 2.6, by + dy * r * 2.6
        d.ellipse((x - r, y - r, x + r, y + r), fill=255)
    return m


def pause_icon():
    """144 px: cream ring and gamepad in the colours of home/play."""
    n = 144 * SS
    ring = Image.new("L", (n, n))
    d = ImageDraw.Draw(ring)
    rw = round(n * 0.045)
    d.ellipse((n * 0.035, n * 0.035, n * 0.965, n * 0.965), outline=255, width=rw)
    mask = ImageChops.lighter(ring, gamepad_mask(n)).resize((144, 144), Image.LANCZOS)
    grad = Image.new("RGBA", (144, 144))
    gd = ImageDraw.Draw(grad)
    for y in range(144):
        t = abs(y / 143 - 0.5) * 2   # the game's icons: pale cream rim, yellower middle
        gd.line([(0, y), (143, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(ICON_MID, ICON_RIM)) + (255,))
    grad.putalpha(mask)
    sh = Image.new("RGBA", (144, 144), SHADOW + (0,))
    sh.putalpha(mask.filter(ImageFilter.GaussianBlur(2)).point(lambda v: int(v * 0.7)))
    icon = Image.new("RGBA", (144, 144))
    icon.alpha_composite(sh, (0, 2))
    icon.alpha_composite(grad)
    icon.save(out / "pause_controls.png", optimize=True)
    frame(PAUSE, "Pause_selected.png").save(out / "pause_selected.png", optimize=True)
    return icon


# --- watermark.png ------------------------------------------------------------

GITHUB_MARK = (
    "M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49"
    "-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82"
    ".72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15"
    "-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82"
    " 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48"
    " 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42-3.58-8-8-8z"
)


def github_mask(px):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{px * SS}" height="{px * SS}" viewBox="0 0 16 16">'
           f'<rect width="16" height="16" fill="black"/><path fill="white" d="{GITHUB_MARK}"/></svg>')
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = Path(tmp) / "m.svg", Path(tmp) / "m.png"
        src.write_text(svg)
        subprocess.run(["convert", f"msvg:{src}", "-colorspace", "Gray", str(dst)], check=True)
        return Image.open(dst).convert("L").resize((px, px), Image.LANCZOS)


def watermark():
    """Gold like the game's own titles: gradient, dark outline, drop shadow."""
    mark_px, text_px = 72, 70
    t = text("franciscoclou", text_px, CREAM, gradient=GOLD)
    m = github_mask(mark_px)
    pad = max(3, mark_px // 10)
    mark = Image.new("RGBA", (mark_px + 2 * pad, mark_px + 2 * pad))
    o = Image.new("L", mark.size)
    o.paste(m, (pad, pad))
    o = o.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(mark_px / 40))
    sh = Image.new("RGBA", mark.size, SHADOW + (0,))
    sh.putalpha(o.point(lambda v: int(v * 0.9)))
    mark.alpha_composite(sh, (0, 0))
    mark.alpha_composite(sh, (0, max(1, mark_px // 30)))
    fill = Image.new("RGBA", (mark_px, mark_px))
    d = ImageDraw.Draw(fill)
    for y in range(mark_px):
        k = y / (mark_px - 1)
        d.line([(0, y), (mark_px, y)], fill=tuple(round(c + (g - c) * k) for c, g in zip(CREAM, GOLD)) + (255,))
    fill.putalpha(m)
    mark.alpha_composite(fill, (pad, pad))
    w = mark.width + 12 + t.width
    h = max(mark.height, t.height)
    im = Image.new("RGBA", (w, h))
    im.alpha_composite(mark, (0, (h - mark.height) // 2))
    im.alpha_composite(t, (mark.width + 12, (h - t.height) // 2))
    im = im.crop(im.getbbox())
    im.save(out / "watermark.png", optimize=True)
    return im


if __name__ == "__main__":
    for name, im in (("controls_help", controls_help()), ("watermark", watermark())):
        print(f"{name}.png {im.size}")
    pause_icon()
    print("pause_controls.png (144, 144), pause_selected.png")
