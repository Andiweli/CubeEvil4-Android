#!/usr/bin/env python3
"""The pictures of the difficulty Berserker (1.1.2, patch 0255, runtime/berserker.cpp) as GameCube IA8 textures
(format 3: an alpha and an intensity byte per texel, 4x4 tiles), drawn like the game's own words:
  - kWord "BERSERKER", the title's difficulty menu (like PROFESSIONAL, NORMAL, PROFI, LEICHT: 14 texels high, light
    capitals 8 texels high of a wide bold serif, letters well apart, a dark edge and a shadow down-right; the menu tints
    them, white or orange);
  - kMark "BSK", the save list's Mode column (the project owner's choice, 24.11.2026; in the place of the game's
    mode picture PRO, 21 texels high, light upright serif capitals with a faint dark edge, tinted light yellow).
DejaVu Serif Bold (free font, Bitstream Vera licence), its capitals widened and spaced to the game's measure, a dark
edge. Nothing of the game: the font is DejaVu's.

  berserker_word.py FONT_DIR OUT.inc [PREVIEW_PREFIX]   (FONT_DIR: DejaVuSerif.ttf and DejaVuSerif-Bold.ttf)

OUT.inc: for each picture NAME: NAMEW, NAMEH (texels; the width a multiple of 4) and NAME[] (the tiles, W * H * 2 bytes
with the height rounded up to 4).
"""
import os
import struct
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SS = 8  # supersampling
PICS = [
    # the title menu's words (measured on the PC's pictures, 24.11.2026: capitals 8 screen rows from texel row 3 of 14,
    # crisp stems 2 texels wide, bold serif letters 2-4 texels apart, a dark edge and a shadow down-right): DejaVu Serif
    # Bold drawn at 12 px with its hinting (capitals 9 texels, stems of whole texels), letters 2 texels further apart
    # (font5, the owner's device test 24.11.2026 "links nicht bündig": the game's words start alike, plate column 1,
    # half-bright serif 2, bright 3, first stem 4: stem0=4 puts B's stem there)
    # (font6, the owner's device test 24.11.2026 "seitlich gestreckt": the stems were right, but the serifs and legs
    # reached about a texel further than the game's letters, counted with their half-bright edge: E 11 texels (the
    # game's 9-10), R 13 (10-11), B 11 (P 10); widened 1.12 instead of 1.22 they are E 9, R 11, B 10, S 9 (the game's
    # 8-9), letters 2-3 texels apart like the game's (gap 1.7))
    dict(name="kWord", word="BERSERKER", font="DejaVuSerif-Bold.ttf", height=14, cap=9.25, track=0, stretch=1.12,
         edge=(0, 0), shadow=False, top=2.25, weight=0, gain=1.0, level=1.0, hinted=0, texel=dict(gap=1.7, plate=1.6, thin=0.25, stem0=4)),
    # the save list's column words (measured on the game's PRO texture, 24.11.2026: 38x21 IA8, capitals 11 texels from row
    # 1, crisp stems 2 texels, letters about 1 texel apart, no edge but a soft black shadow down-right; tinted light yellow)
    # (font5, the owner 24.11.2026 "bei PRO ist zwischen den Buchstaben mehr Platz": P-R and R-O measured per row, 2
    # texels at the stems (ink >= 60) and 3-4 bright; B-S and S-K one texel wider than round 4 for that; 44 texels wide so
    # the ink centred on the screen where PRO's is, the unit being centred: first stem on column 4)
    dict(name="kMark", word="BSK", font="DejaVuSerif-Bold.ttf", height=21, cap=11.0, track=0, stretch=1.0,
         edge=(0, 0), shadow=False, top=1.0, weight=0, gain=1.0, level=1.0, hinted=0,
         texel=dict(gap=-0.8, plate=0, thin=0.4, blur=(2, 3, 2.0, 130), gaps=[0.4, 0.0], width=44, stem0=4)),
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


def render_texel(font_path, word, height, cap, cap_top, stretch, gap, plate, thin=0.0, blur=None, stem0=None, gaps=None, only=None):
    """The word on the game's texel grid (measured on its own menu words, 24.11.2026): capitals exactly `cap` texels
    from row `cap_top`, each letter's main stem starting on a texel's edge (crisp stems), letters `gap` texels apart
    (ink to ink), widened `stretch`; and the game's dark plate: black, opaque within `plate` texels of the letters."""
    size = int(round(cap * SS / 0.729))
    font = ImageFont.truetype(font_path, size)
    glyphs = []
    for c in word:
        g = Image.new("L", (size * 2, size * 2), 0)
        ImageDraw.Draw(g).text((size // 2, size // 4), c, font=font, fill=255)
        g = g.crop(g.getbbox())
        g = g.resize((max(1, int(round(g.width * stretch))), int(round(cap * SS))), Image.LANCZOS)
        # the stems thinner by `thin` texels on each side (the game's: about 2 texels; the bars stay)
        for k in range(1, int(round(thin * SS)) + 1):
            left = Image.new("L", g.size, 0)
            left.paste(g.crop((k, 0, g.width, g.height)), (0, 0))
            right = Image.new("L", g.size, 0)
            right.paste(g.crop((0, 0, g.width - k, g.height)), (k, 0))
            g = ImageChops.darker(g, ImageChops.darker(left, right)) if k == int(round(thin * SS)) else g
        # the main stem: the first column (from the left) inked over 90 % of the middle of the capitals
        stem = 0
        for x in range(g.width):
            ink = sum(1 for y in range(g.height // 5, g.height * 4 // 5) if g.getpixel((x, y)) > 128)
            if ink >= (g.height * 3 // 5) * 0.9:
                stem = x
                break
        glyphs.append((g, stem))
    w = int(sum(g.width for g, _ in glyphs) / SS + gap * len(word) + 2 * plate + 8)
    big = Image.new("L", (w * SS, height * SS), 0)
    x = int(round((plate + 1) * SS))
    for i, (g, stem) in enumerate(glyphs):
        if i == 0 and stem0 is not None:
            x = stem0 * SS - stem  # (font5: the first stem on the texel column of the game's words: their left edge)
        else:
            x = ((x + stem + SS - 1) // SS) * SS - stem  # the stem on a texel's edge
        if only is None or only == i:  # (`only`: one letter alone at its place, for measuring the gaps)
            big.paste(g, (x, int(cap_top * SS)), g)
        x += g.width + int((gaps[i] if gaps and i < len(gaps) else gap) * SS)
    fill = big.resize((w, height), Image.BOX)
    if plate:
        edge = big.filter(ImageFilter.MaxFilter(2 * int(round(plate * SS)) + 1)).resize((w, height), Image.BOX)
    else:
        edge = Image.new("L", fill.size, 0)
    if blur:  # a soft black shadow (dx, dy, radius in texels, its strongest alpha), as the save list's words have it
        dx, dy, radius, amax = blur
        sh = Image.new("L", fill.size, 0)
        sh.paste(fill, (dx, dy))
        sh = sh.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: min(255, int(v * amax / 255 * 2.2)))
        edge = ImageChops.lighter(edge, sh)
    box = ImageChops.lighter(fill, edge).getbbox()
    return fill.crop((0, 0, box[2] + 1, height)), edge.crop((0, 0, box[2] + 1, height))


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


# XXH64 with seed 0, as tex_pack.cpp Xxh64 (the first part of Dolphin's texture name): the key the app knows a
# menu word by
_M = (1 << 64) - 1
_P1, _P2, _P3, _P4, _P5 = (11400714785074694791, 14029467366897019727, 1609587929392839161, 9650029242287828579,
                           2870177450012600261)


def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & _M


def _round(acc, v):
    return (_rotl((acc + v * _P2) & _M, 31) * _P1) & _M


def xxh64(data):
    n, i = len(data), 0
    if n >= 32:
        v = [(_P1 + _P2) & _M, _P2, 0, (-_P1) & _M]
        while i + 32 <= n:
            v = [_round(v[k], struct.unpack_from("<Q", data, i + 8 * k)[0]) for k in range(4)]
            i += 32
        h = (_rotl(v[0], 1) + _rotl(v[1], 7) + _rotl(v[2], 12) + _rotl(v[3], 18)) & _M
        for x in v:
            h = ((h ^ _round(0, x)) * _P1 + _P4) & _M
    else:
        h = _P5
    h = (h + n) & _M
    while i + 8 <= n:
        h = (_rotl(h ^ _round(0, struct.unpack_from("<Q", data, i)[0]), 27) * _P1 + _P4) & _M
        i += 8
    if i + 4 <= n:
        h = (_rotl(h ^ (struct.unpack_from("<I", data, i)[0] * _P1 & _M), 23) * _P2 + _P3) & _M
        i += 4
    while i < n:
        h = (_rotl(h ^ (data[i] * _P5 & _M), 11) * _P1) & _M
        i += 1
    h ^= h >> 33
    h = (h * _P2) & _M
    h ^= h >> 29
    h = (h * _P3) & _M
    return h ^ (h >> 32)


WORDS_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "android-decomp", "app", "src", "main", "cpp",
                       "tex_pack_words.h")
MARK = "// berserker_word.py: "


def menu_words(entries):
    """font6 (the owner's device test 24.11.2026: BERSERKER looked wider than the game's words): the app's "Sharper menu
    text" (on by default) and the HD packs know the game's words by their texels' hash (tex_pack_words.h kMenuWords);
    ours, on no disc, were not there, so only the game's words were drawn sharper at high resolutions. Our pictures'
    lines there (marked), sorted by the hash like the rest and kept up to date with the pictures."""
    with open(WORDS_H, encoding="utf-8") as f:
        text = f.read()
    start = "constexpr KeptTexture kMenuWords[] = {\n"
    head, rest = text.split(start, 1)
    body, tail = rest.split("\n};", 1)
    lines = [line for line in body.split("\n") if line and MARK not in line]
    for name, word, tex, w, h in entries:
        lines.append(f"    {{0x{tex:016x}ull, {w}, {h}, 3}}, {MARK}{name} \"{word}\" (the port's own, Berserker)")
    lines.sort(key=lambda line: int(line.split("0x", 1)[1].split("ull", 1)[0], 16))
    head_lines = head.rstrip("\n").split("\n")
    head_lines[-1] = f"// {len(lines)} pictures, sorted by tex, width, height, format"
    out = "\n".join(head_lines) + "\n" + start + "\n".join(lines) + "\n};" + tail
    if out != text:
        with open(WORDS_H, "w", newline="\n", encoding="utf-8") as f:
            f.write(out)
        print(f"tex_pack_words.h: {len(lines)} pictures, ours updated")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    lines = ["// generated by port/tools/berserker_word.py from DejaVu Serif (Bold); do not edit"]
    words = []
    for p in PICS:
        name, word, height = p["name"], p["word"], p["height"]
        if p["texel"]:
            fill, edge = render_texel(os.path.join(argv[1], p["font"]), word, height, p["cap"], p["top"], p["stretch"],
                                      p["texel"]["gap"], p["texel"]["plate"], p["texel"]["thin"],
                                      p["texel"].get("blur"), p["texel"].get("stem0"), p["texel"].get("gaps"))
            if p["texel"].get("width", 0) > fill.width:  # (a fixed width: the ink as far off the middle as the game's)
                fill = fill.crop((0, 0, p["texel"]["width"], height))
                edge = edge.crop((0, 0, p["texel"]["width"], height))
        else:
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
        words.append((name, word, xxh64(bytes(data)), w, height))
        print(f"{name} \"{word}\": {w}x{height}, {len(data)} bytes, texels' hash 0x{words[-1][2]:016x}")
    with open(argv[2], "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    menu_words(words)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
