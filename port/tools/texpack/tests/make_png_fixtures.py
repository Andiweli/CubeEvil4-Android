"""PNG test files for texpack_tests: every colour type and bit depth, palettes with tRNS, colour keys, Adam7,
all five filters, stored / fixed / dynamic deflate. Each <name>.png comes with <name>.rgba, the expected RGBA8.
python -I make_png_fixtures.py OUT_DIR"""
import os
import random
import struct
import sys
import zlib

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
rng = random.Random(4)


def chunk(t, data):
    return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filter_rows(rows, bpp):
    out = b""
    prior = bytes(len(rows[0])) if rows else b""
    for y, row in enumerate(rows):
        f = y % 5
        res = bytearray()
        for i, v in enumerate(row):
            left = row[i - bpp] if i >= bpp else 0
            up = prior[i]
            ul = prior[i - bpp] if i >= bpp else 0
            pred = (0, left, up, (left + up) >> 1, paeth(left, up, ul))[f]
            res.append((v - pred) & 255)
        out += bytes([f]) + bytes(res)
        prior = row
    return out


def pack_samples(samples, depth):
    """one row of samples (ints) to bytes at a bit depth"""
    if depth == 8:
        return bytes(samples)
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    out = bytearray()
    acc = 0
    n = 0
    for s in samples:
        acc = (acc << depth) | s
        n += depth
        if n == 8:
            out.append(acc)
            acc = n = 0
    if n:
        out.append(acc << (8 - n))
    return bytes(out)


ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


def write(name, w, h, ctype, depth, pixels, expected, palette=None, trns=None, interlace=0, level=6):
    """pixels[y][x]: tuple of samples at `depth`"""
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, channels * depth // 8)
    passes = ADAM7 if interlace else [(0, 0, 1, 1)]
    raw = b""
    for x0, y0, dx, dy in passes:
        xs = list(range(x0, w, dx))
        ys = list(range(y0, h, dy))
        if not xs or not ys:
            continue
        rows = [pack_samples([s for x in xs for s in pixels[y][x]], depth) for y in ys]
        raw += filter_rows(rows, bpp)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, interlace))
    if palette:
        png += chunk(b"PLTE", b"".join(bytes(p) for p in palette))
    if trns is not None:
        png += chunk(b"tRNS", trns)
    data = zlib.compress(raw, level)
    # two IDAT chunks, the reader must join them
    half = len(data) // 2
    png += chunk(b"IDAT", data[:half]) + chunk(b"IDAT", data[half:]) + chunk(b"IEND", b"")
    open(os.path.join(OUT, name + ".png"), "wb").write(png)
    open(os.path.join(OUT, name + ".rgba"), "wb").write(bytes(v for y in range(h) for x in range(w) for v in expected[y][x]))


def to8(v, depth):
    if depth == 16:
        return v >> 8
    if depth == 8:
        return v
    return v * 255 // ((1 << depth) - 1)


n = 0
for interlace in (0, 1):
    for level in (0, 1, 9):
        for (w, h) in ((1, 1), (7, 5), (33, 17)):
            tag = "i%d_z%d_%dx%d" % (interlace, level, w, h)
            # grey 1/2/4/8/16, with a colour key for 8 and 16
            for depth in (1, 2, 4, 8, 16):
                mx = (1 << depth) - 1
                px = [[(rng.randint(0, mx),) for x in range(w)] for y in range(h)]
                key = px[0][0][0] if depth >= 8 else None
                trns = struct.pack(">H", key) if key is not None else None
                exp = [[(to8(p[0], depth),) * 3 + ((0 if p[0] == key else 255),) for p in row] for row in px]
                write("grey%d_%s" % (depth, tag), w, h, 0, depth, px, exp, trns=trns, interlace=interlace, level=level)
                n += 1
            # RGB 8/16 with a colour key
            for depth in (8, 16):
                mx = (1 << depth) - 1
                px = [[tuple(rng.randint(0, mx) for _ in range(3)) for x in range(w)] for y in range(h)]
                key = px[h - 1][w - 1]
                exp = [[tuple(to8(v, depth) for v in p) + ((0 if p == key else 255),) for p in row] for row in px]
                write("rgb%d_%s" % (depth, tag), w, h, 2, depth, px, exp, trns=struct.pack(">HHH", *key), interlace=interlace, level=level)
                n += 1
            # palette 1/2/4/8 with tRNS for some entries
            for depth in (1, 2, 4, 8):
                count = 1 << depth
                pal = [tuple(rng.randint(0, 255) for _ in range(3)) for _ in range(count)]
                alphas = [rng.randint(0, 255) for _ in range(count // 2 + 1)][:count]
                px = [[(rng.randint(0, count - 1),) for x in range(w)] for y in range(h)]
                exp = [[pal[p[0]] + ((alphas[p[0]] if p[0] < len(alphas) else 255),) for p in row] for row in px]
                write("pal%d_%s" % (depth, tag), w, h, 3, depth, px, exp, palette=pal, trns=bytes(alphas), interlace=interlace, level=level)
                n += 1
            # grey + alpha, RGBA, 8 and 16
            for ctype, ch in ((4, 2), (6, 4)):
                for depth in (8, 16):
                    mx = (1 << depth) - 1
                    px = [[tuple(rng.randint(0, mx) for _ in range(ch)) for x in range(w)] for y in range(h)]
                    if ctype == 4:
                        exp = [[(to8(p[0], depth),) * 3 + (to8(p[1], depth),) for p in row] for row in px]
                    else:
                        exp = [[tuple(to8(v, depth) for v in p) for p in row] for row in px]
                    write("ct%d_%d_%s" % (ctype, depth, tag), w, h, ctype, depth, px, exp, interlace=interlace, level=level)
                    n += 1
print(n, "fixtures in", OUT)
