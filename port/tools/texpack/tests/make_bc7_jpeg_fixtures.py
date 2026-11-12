"""Test files for texpack_tests made with Pillow (python -m pip install pillow; not part of the tool): BC7 DDS files
with random blocks of each of the eight modes (and partial blocks, mip levels), and JPG files: grey and colour,
4:4:4 / 4:2:2 / 4:2:0, baseline and progressive, optimized tables, restart markers, odd sizes, and a CMYK file the
tool must refuse. Each <name>.dds / <name>.jpg comes with <name>.rgba, Pillow's decoding as RGBA8 (the reference).
python make_bc7_jpeg_fixtures.py OUT_DIR   (Pillow is in the user's site-packages: without -I)"""
import os
import random
import struct
import sys

from PIL import Image

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
rng = random.Random(12)
made = 0


def dds_bc7(path, w, h, levels, blocks_of_level):
    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<IIIIIII", hdr, 4, 124, 0x1007 | 0x20000 | 0x80000, h, w, max(1, (w + 3) // 4) * 16, 0, levels)
    struct.pack_into("<II4s", hdr, 76, 32, 4, b"DX10")
    struct.pack_into("<I", hdr, 108, 0x401008)
    dx10 = struct.pack("<IIIII", 98, 3, 0, 1, 0)  # BC7_UNORM, 2D texture, array size 1
    body = b""
    for l in range(levels):
        lw, lh = max(1, w >> l), max(1, h >> l)
        body += blocks_of_level(((lw + 3) // 4) * ((lh + 3) // 4))
    open(path, "wb").write(bytes(hdr) + dx10 + body)


def reference(src, name):
    global made
    im = Image.open(src)
    im.load()
    open(os.path.join(OUT, name + ".rgba"), "wb").write(im.convert("RGBA").tobytes())
    made += 1


def random_blocks(mode):
    def make(n):
        out = bytearray()
        for _ in range(n):
            b = bytearray(rng.getrandbits(8) for _ in range(16))
            m = mode if mode is not None else rng.randrange(8)
            b[0] = ((b[0] << (m + 1)) | (1 << m)) & 0xFF
            out += b
        return bytes(out)
    return make


# BC7: every mode on its own (1024 blocks each, all partitions and p-bits), then mixed, partial blocks, mip levels
for mode in range(8):
    p = os.path.join(OUT, "bc7_mode%d.dds" % mode)
    dds_bc7(p, 128, 128, 1, random_blocks(mode))
    reference(p, "bc7_mode%d" % mode)
p = os.path.join(OUT, "bc7_mixed_37x23.dds")
dds_bc7(p, 37, 23, 1, random_blocks(None))
reference(p, "bc7_mixed_37x23")
p = os.path.join(OUT, "bc7_mips_64x32.dds")
dds_bc7(p, 64, 32, 4, random_blocks(None))
reference(p, "bc7_mips_64x32")


# JPG: a picture with smooth parts, edges, colour detail and noise
def picture(w, h, mode):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            r = (x * 255) // max(1, w - 1)
            g = (y * 255) // max(1, h - 1)
            b = 255 if ((x // 5) + (y // 7)) % 2 else 40
            if (x * 3 + y * 5) % 31 < 3:
                r, g, b = 250, 30, 200
            n = rng.randrange(-12, 13)
            px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g - n)), max(0, min(255, b + n)))
    return im.convert(mode)


def save_jpeg(name, im, **options):
    p = os.path.join(OUT, name + ".jpg")
    try:
        im.save(p, "JPEG", **options)
    except TypeError:
        return
    reference(p, name)


for w, h in ((64, 48), (37, 23), (1, 1), (17, 9), (200, 120)):
    for mode in ("L", "RGB"):
        im = picture(w, h, mode)
        tag = "%s_%dx%d" % ("grey" if mode == "L" else "rgb", w, h)
        subs = (None,) if mode == "L" else (0, 1, 2)
        for sub in subs:
            sname = "" if sub is None else "_s%d" % sub
            extra = {} if sub is None else {"subsampling": sub}
            save_jpeg("jpg_%s%s_base" % (tag, sname), im, quality=85, **extra)
            save_jpeg("jpg_%s%s_prog" % (tag, sname), im, quality=85, progressive=True, **extra)
            save_jpeg("jpg_%s%s_opt" % (tag, sname), im, quality=60, optimize=True, **extra)
            save_jpeg("jpg_%s%s_prog_q30" % (tag, sname), im, quality=30, progressive=True, optimize=True, **extra)
# restart markers (Pillow 10+: restart_marker_blocks / restart_marker_rows)
im = picture(200, 120, "RGB")
save_jpeg("jpg_rgb_restart_blocks", im, quality=80, subsampling=2, restart_marker_blocks=5)
save_jpeg("jpg_rgb_restart_rows", im, quality=80, subsampling=1, progressive=True, restart_marker_rows=1)
# CMYK: the tool must say it cannot read it (no reference picture)
picture(32, 32, "RGB").convert("CMYK").save(os.path.join(OUT, "jpg_cmyk_refuse.jpg"), "JPEG", quality=80)
print(made, "fixtures with references in", OUT)
