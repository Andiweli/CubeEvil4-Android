#!/usr/bin/env python3
"""The pictures of the difficulty Berserker (1.1.2, patch 0255, runtime/berserker.cpp) as GameCube IA8 textures
(format 3: an alpha and an intensity byte per texel, 4x4 tiles), drawn like the game's own words:
  - kWord "BERSERKER", the title's difficulty menu (like PROFESSIONAL, NORMAL, PROFI, LEICHT: 14 texels high, light
    capitals 8 texels high of a wide bold serif, letters well apart, a dark edge and a shadow down-right; the menu tints
    them, white or orange);
  - kMark "BZK", the save list's Mode column (the project owner's choice, 24.11.2026; in the place of the game's
    mode picture PRO, 21 texels high, light upright serif capitals with a faint dark edge, tinted light yellow).
DejaVu Serif Bold (free font, Bitstream Vera licence), its capitals widened and spaced to the game's measure, a dark
edge. Nothing of the game: the font is DejaVu's.

  berserker_word.py FONT_DIR OUT.inc [PREVIEW_PREFIX]   (FONT_DIR: DejaVuSerif.ttf and DejaVuSerif-Bold.ttf)

OUT.inc: for each picture NAME: NAMEW, NAMEH (texels; the width a multiple of 4) and NAME[] (the tiles, W * H * 2 bytes
with the height rounded up to 4).
"""
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SS = 8  # supersampling
PICS = [
    # the title menu's words (measured on the PC's pictures, 24.11.2026: capitals 8 screen rows from texel row 3 of 14,
    # crisp stems 2 texels wide, bold serif letters 2-4 texels apart, a dark edge and a shadow down-right): DejaVu Serif
    # Bold drawn at 12 px with its hinting (capitals 9 texels, stems of whole texels), letters 2 texels further apart
    dict(name="kWord", word="BERSERKER", font="DejaVuSerif-Bold.ttf", height=14, cap=9.0, track=2.0, stretch=1.0,
         edge=(255, 0), shadow=True, top=3.0, weight=0, gain=1.0, level=1.0, hinted=12),
    # the save list's column words (PRO: 38x21 texels, light upright serif capitals about 9.6 texels high from the
    # top, a faint dark edge; tinted light yellow; measured on the PC's picture, 24.11.2026)
    dict(name="kMark", word="BZK", font="DejaVuSerif-Bold.ttf", height=21, cap=9.6, track=1.4, stretch=1.0,
         edge=(96, 0), shadow=False, top=1.0, weight=0, gain=1.0, level=1.0, hinted=0),
]


def render_hinted(font_path, word, height, size, track, cap_top):
    """The word drawn at its own size with the font's hinting (crisp stems of whole texels, as the game's letters),
    letters `track` texels apart, the capitals' top at texel row `cap_top`."""
    font = ImageFont.truetype(font_path, size)
    probe = Image.new("L", (size * 3, size * 3), 0)
    ImageDraw.Draw(probe).text((size, size), "H", font=font, fill=255)
    cap_y = probe.getbbox()[1] - size  # the capitals' top below the pen's
    w = int(sum(round(font.getlength(c)) + track for c in word) + 8)
    fill = Image.new("L", (w, height), 0)
    d = ImageDraw.Draw(fill)
    x = 2
    for c in word:
        d.text((x, int(cap_top) - cap_y), c, font=font, fill=255)
        x += round(font.getlength(c)) + track
    return fill


def render(font_path, word, height, cap, track, stretch, edge_alpha, shadow, cap_top, weight=0, gain=1.0, hinted=0):
    size = int(round(cap * SS / 0.729))  # DejaVu Serif's capitals: 0.729 em
    font = ImageFont.truetype(font_path, size)
    widths = [font.getlength(c) * stretch for c in word]
    total = sum(widths) + track * SS * (len(word) - 1)
    w = int(total + 4 * SS)
    big = Image.new("L", (w, height * SS), 0)
    x = 2 * SS
    if hinted:
        big = None
    top = (height * SS - cap * SS) / 2 if cap_top is None else cap_top * SS
    for c, cw in zip(word, widths if big else []):
        g = Image.new("L", (int(font.getlength(c)) + 2 * SS, height * SS * 2), 0)
        ImageDraw.Draw(g).text((SS, 0), c, font=font, fill=255)
        if weight > 0:  # (a little bolder)
            g = g.filter(ImageFilter.MaxFilter(weight))
        elif weight < 0:  # (a little thinner: the game's stems are 2 texels, crisp)
            g = g.filter(ImageFilter.MinFilter(-weight))
        box = g.getbbox()
        if stretch == 1.0:  # (the letters as the font places them, side bearings and all)
            g = g.crop((0, box[1], g.width, box[3]))
            g = g.resize((g.width, int(round(cap * SS))), Image.LANCZOS)
            big.paste(g, (int(x - SS), int(top)), g)
        else:  # (widened: the ink alone, from the pen's place, its left edge on a texel's edge: crisp stems)
            g = g.crop(box)
            g = g.resize((max(1, int(round(g.width * stretch))), int(round(cap * SS))), Image.LANCZOS)
            big.paste(g, (int(round(x / SS)) * SS, int(top)), g)
        x += cw + track * SS
    fill = render_hinted(font_path, word, height, hinted, track, cap_top) if hinted else big.resize((w // SS, height),
                                                                                                    Image.LANCZOS)
    if gain != 1.0:  # (crisper strokes with bright cores, as the game's letters have them)
        fill = fill.point(lambda v: min(255, int(v * gain)))
    # the dark edge: one texel around the letters (and a second one, `far`), and with `shadow` the same once more a
    # texel down and right, as the game's words have it (the menu draws the words at 0.76 alpha over a dark picture)
    near = fill.filter(ImageFilter.MaxFilter(3)).point(lambda v: min(edge_alpha[0], v * 2))
    far = fill.filter(ImageFilter.MaxFilter(5)).point(lambda v: min(edge_alpha[1], v))
    edge = ImageChops.lighter(near, far)
    if shadow:
        moved = Image.new("L", edge.size, 0)
        moved.paste(near, (1, 1))
        edge = ImageChops.lighter(edge, moved)
    box = edge.getbbox()
    fill = fill.crop((0, 0, box[2] + 1, height))
    edge = edge.crop((0, 0, box[2] + 1, height))
    return fill, edge


def texels_of(fill, edge, height, level=1.0):
    w = (fill.width + 3) // 4 * 4
    rows = []
    for y in range(height):
        row = []
        for x in range(w):
            f = fill.getpixel((x, y)) if x < fill.width else 0
            e = edge.getpixel((x, y)) if x < edge.width else 0
            a = max(f, e)
            i = 0 if a == 0 else min(255, round(0xFF * level * f / max(a, 1)))
            row.append((i, a))
        rows.append(row)
    return w, rows


def tiles(w, rows, height):
    data = bytearray()
    for ty in range(0, height, 4):
        for tx in range(0, w, 4):
            for y in range(ty, ty + 4):
                for x in range(tx, tx + 4):
                    i, a = rows[y][x] if y < height else (0, 0)
                    data += bytes((a, i))  # IA8: alpha first, then intensity (GX_TF_IA8)
    return data


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    lines = ["// generated by port/tools/berserker_word.py from DejaVu Serif (Bold); do not edit"]
    for p in PICS:
        name, word, height = p["name"], p["word"], p["height"]
        fill, edge = render(os.path.join(argv[1], p["font"]), word, height, p["cap"], p["track"], p["stretch"],
                            p["edge"], p["shadow"], p["top"], p["weight"], p["gain"], p["hinted"])
        w, rows = texels_of(fill, edge, height, p["level"])
        data = tiles(w, rows, height)
        lines.append(f"static const int {name}W = {w}, {name}H = {height};  // \"{word}\"")
        lines.append(f"static const unsigned char {name}[{len(data)}] = {{")
        for k in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[k:k + 24]) + ",")
        lines.append("};")
        if len(argv) > 3:
            prev = Image.new("RGBA", (w, height))
            for y in range(height):
                for x in range(w):
                    i, a = rows[y][x]
                    prev.putpixel((x, y), (i, i, i, a))
            bg = Image.new("RGBA", (w, height), (60, 55, 45, 255))
            bg.alpha_composite(prev)
            bg.resize((w * 6, height * 6), Image.NEAREST).save(f"{argv[3]}-{word.lower()}.png")
        print(f"{name} \"{word}\": {w}x{height}, {len(data)} bytes")
    with open(argv[2], "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
