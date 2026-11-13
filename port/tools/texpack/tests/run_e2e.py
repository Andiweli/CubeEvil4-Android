"""End-to-end test of the CubeEvil4 Texture Packer with a small made-up Dolphin pack (coloured test patterns, no game
data): game-ID folder, an Extra folder with variants, a copy "(1)", desktop.ini, DDS with mip levels, an _arb DDS,
PNGs with and without alpha and a large one to shrink. Checks the result with -check, that the pack folder is unchanged,
that an output inside the pack folder is refused, that the same input gives the same file, and that cut or damaged
pack files are found. Also BC7 DDS, JPG and JPEG files (from make_bc7_jpeg_fixtures.py), the order DDS before PNG
before JPG for the same texture, and a BC6H file refused with a clear reason.
python -I run_e2e.py TEXPACK_EXE WORK_DIR FIXTURE_DIR"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib

EXE, WORK, FIXTURES = sys.argv[1], sys.argv[2], sys.argv[3]
failures = 0


def check(ok, what):
    global failures
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def png(path, w, h, fn, alpha=True):
    raw = b""
    for y in range(h):
        raw += b"\0" + b"".join(bytes(fn(x, y)[: 4 if alpha else 3]) for x in range(w))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def dds_bc1(path, w, h, levels, colour_of_level):
    """BC1 with flat-coloured levels (c0 = c1, all indices 0)"""
    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<IIIIIII", hdr, 4, 124, 0x1007 | 0x20000, h, w, max(1, w // 4) * 8, 0, levels)
    struct.pack_into("<II4s", hdr, 76, 32, 4, b"DXT1")
    struct.pack_into("<I", hdr, 108, 0x401008)
    body = b""
    for l in range(levels):
        lw, lh = max(1, w >> l), max(1, h >> l)
        r, g, b = colour_of_level(l)
        c = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)
        body += struct.pack("<HHI", c, c, 0) * (((lw + 3) // 4) * ((lh + 3) // 4))
    open(path, "wb").write(bytes(hdr) + body)


def snapshot(root):
    out = {}
    for dp, _d, fs in os.walk(root):
        for fn in fs:
            p = os.path.join(dp, fn)
            out[os.path.relpath(p, root)] = hashlib.sha1(open(p, "rb").read()).hexdigest()
    return out


def run(*args):
    r = subprocess.run([EXE] + list(args), capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + r.stderr


shutil.rmtree(WORK, ignore_errors=True)
pack = os.path.join(WORK, "Test Pack")
game = os.path.join(pack, "G4BE08")
os.makedirs(os.path.join(game, "sub"))
H1, H2, H3, H4, T2 = "0123456789abcdef", "fedcba9876543210", "00000000deadbeef", "1111222233334444", "aaaabbbbccccdddd"
dds_bc1(os.path.join(game, "tex1_64x64_m_%s_14.dds" % H1), 256, 256, 9, lambda l: (200, 100 + l * 10, 50))
shutil.copy(os.path.join(game, "tex1_64x64_m_%s_14.dds" % H1), os.path.join(game, "tex1_64x64_m_%s_14 (1).dds" % H1))
png(os.path.join(game, "sub", "tex1_32x32_%s_%s_9.png" % (H2, T2)), 128, 128, lambda x, y: (x * 2, y * 2, 128, (x + y) % 256))
png(os.path.join(game, "tex1_16x16_%s_3.png" % H3), 2048, 1024, lambda x, y: ((x // 64) * 8 % 256, (y // 32) * 8 % 256, 90), alpha=False)
dds_bc1(os.path.join(game, "tex1_32x32_m_%s_0_arb.dds" % H4), 128, 128, 8, lambda l: (255 * (l & 1), 0, 255 * (1 - (l & 1))))
open(os.path.join(game, "desktop.ini"), "w").write("[.ShellClassInfo]\n")
open(os.path.join(game, "readme.txt"), "w").write("test\n")
# never below the game's own size: a full-screen 640x360 picture (HD 1280x720), a 128x128 texture whose HD image has
# another shape (1024x256), a 256x256 texture whose pack image is smaller (128x128)
H5, H6, H7 = "5555666677778888", "9999aaaabbbbcccc", "ddddeeeeffff0000"
png(os.path.join(game, "tex1_640x360_%s_14.png" % H5), 1280, 720, lambda x, y: (x % 256, y % 256, 60), alpha=False)
png(os.path.join(game, "tex1_128x128_%s_14.png" % H6), 1024, 256, lambda x, y: (x % 256, 90, y % 256), alpha=False)
png(os.path.join(game, "tex1_256x256_%s_14.png" % H7), 128, 128, lambda x, y: (200, x * 2, y * 2), alpha=False)
# BC7 (random blocks of every mode, 128x128), a JPG (200x120, progressive 4:2:0), a .jpeg (37x23), and one texture as
# PNG and JPG (the PNG is used)
H8, H9, H10, H11 = "8888000011112222", "9999000011112222", "aaaa000011112222", "bbbb000011112222"
shutil.copy(os.path.join(FIXTURES, "bc7_mode6.dds"), os.path.join(game, "tex1_32x32_%s_14.dds" % H8))
shutil.copy(os.path.join(FIXTURES, "jpg_rgb_200x120_s2_prog.jpg"), os.path.join(game, "tex1_50x30_%s_14.jpg" % H9))
shutil.copy(os.path.join(FIXTURES, "jpg_rgb_37x23_s0_base.jpg"), os.path.join(game, "tex1_37x23_%s_14.jpeg" % H10))
png(os.path.join(game, "tex1_16x16_%s_14.png" % H11), 64, 64, lambda x, y: (255, 0, 0), alpha=False)
shutil.copy(os.path.join(FIXTURES, "jpg_rgb_200x120_s1_base.jpg"), os.path.join(game, "tex1_16x16_%s_14.jpg" % H11))
extra = os.path.join(pack, "Extra", "Case")
os.makedirs(os.path.join(extra, "Original"))
os.makedirs(os.path.join(extra, "Gold"))
shutil.copy(os.path.join(game, "tex1_64x64_m_%s_14.dds" % H1), os.path.join(extra, "Original"))
png(os.path.join(extra, "Gold", "tex1_64x64_m_%s_14.png" % H1), 256, 256, lambda x, y: (255, 215, 0, 255))
before = snapshot(pack)
out = os.path.join(WORK, "out")

rc, text = run("-in", pack, "-size", "512", "-out", out, "-variant", "Case=Gold", "-threads", "4")
print(text)
target = os.path.join(out, "CubeEvil4-textures-512.c4tex")
check(rc == 0 and os.path.exists(target), "conversion with a variant")
check("11 images converted" in text and "files skipped" in text, "eleven images, the copy and the text file skipped")
check(os.path.exists(os.path.join(out, "CubeEvil4-textures-512.txt")), "report next to the pack file")
report = open(os.path.join(out, "CubeEvil4-textures-512.txt"), encoding="utf-8").read()
check("(1).dds" in report and "readme.txt" in report and "desktop.ini" not in report, "report lists the skipped files")
check(("tex1_16x16_%s_14.jpg: the same texture again (its PNG is used)" % H11) in report, "PNG before JPG of the same texture")
check("Case: Gold" in report, "report names the choice")
rc, text = run("-check", target, "-png", os.path.join(WORK, "png"), "-limit", "10")
print(text)
check(rc == 0 and "The pack is fine." in text and "Entries: 11 " in text, "-check: fine, 11 entries")
check("Exported 10 image(s)" in text, "-check exports PNGs (at most 10)")
check(snapshot(pack) == before, "the pack folder is unchanged")

# the gold variant replaced the main folder's case: its PNG is all gold
gold = os.path.join(WORK, "png", "tex1_64x64_m_%s_14.png" % H1)
check(os.path.exists(gold), "exported the case texture")
# the large PNG was shrunk to 512x256, the soft-alpha PNG became RGBA EAC
info = open(target, "rb").read()
count, = struct.unpack_from("<I", info, 16)
index_off, = struct.unpack_from("<Q", info, 24)
entries = [struct.unpack_from("<QQHHBBBBHHIQII", info, index_off + 48 * i) for i in range(count)]
by_hash = {e[0]: e for e in entries}
e3 = by_hash[int(H3, 16)]
check((e3[8], e3[9]) == (512, 256) and e3[6] == 1 and e3[7] == 10, "2048x1024 PNG: 512x256, opaque, 10 levels")
e2 = by_hash[int(H2, 16)]
check(e2[6] == 3 and e2[1] == int(T2, 16) and (e2[5] & 2), "soft alpha: RGBA EAC, palette part kept")
e1 = by_hash[int(H1, 16)]
check(e1[6] == 1 and (e1[8], e1[9]) == (256, 256) and (e1[5] & 1), "the gold case: 256x256, _m kept")
e4 = by_hash[int(H4, 16)]
check(e4[7] == 8 and (e4[5] & 8), "_arb: its own 8 levels kept")


def sizes(path):
    d = open(path, "rb").read()
    n, = struct.unpack_from("<I", d, 16)
    at, = struct.unpack_from("<Q", d, 24)
    return {e[0]: (e[8], e[9]) for e in (struct.unpack_from("<QQHHBBBBHHIQII", d, at + 48 * i) for i in range(n))}


s512 = sizes(target)
check(s512.get(int(H8, 16)) == (128, 128), "BC7 DDS converted (128x128)")
check(s512.get(int(H9, 16)) == (200, 120) and s512.get(int(H10, 16)) == (37, 23), "JPG and JPEG converted")
check(s512.get(int(H11, 16)) == (64, 64), "the PNG of a texture that is also a JPG")
check(s512[int(H5, 16)] == (640, 360), "512: the 640x360 picture keeps 640x360 (not 512x288)")
check(s512[int(H6, 16)] == (512, 128), "512: another shape keeps the game's 128 rows (512x128)")
check(s512[int(H7, 16)] == (128, 128), "512: a pack image smaller than the game's keeps its size")
# size 256: the saving level, never below the game's size either
rc, text = run("-in", pack, "-size", "256", "-out", os.path.join(WORK, "out256"))
s256 = sizes(os.path.join(WORK, "out256", "CubeEvil4-textures-256.c4tex")) if rc == 0 else {}
check(rc == 0 and s256.get(int(H3, 16)) == (256, 128), "256: 2048x1024 becomes 256x128")
check(s256.get(int(H5, 16)) == (640, 360) and s256.get(int(H6, 16)) == (512, 128), "256: never below the game's size")
check(s256.get(int(H1, 16)) == (256, 256) and s256.get(int(H2, 16)) == (128, 128), "256: small images stay as they are")

# the same pack as a ZIP (stored, deflated, without a top folder) gives the same file as the folder
for label, method, top in (("stored", zipfile.ZIP_STORED, True), ("deflated", zipfile.ZIP_DEFLATED, True),
                           ("no top folder", zipfile.ZIP_DEFLATED, False)):
    zdir = os.path.join(WORK, "zip-" + label.replace(" ", "-"))
    os.makedirs(zdir)
    zpath = os.path.join(zdir, "Test Pack.zip")
    with zipfile.ZipFile(zpath, "w", compression=method) as z:
        for dp, _d, fs in os.walk(pack):
            for fn in sorted(fs):
                full = os.path.join(dp, fn)
                rel = os.path.relpath(full, pack).replace(os.sep, "/")
                z.write(full, ("Test Pack/" + rel) if top else rel)
    rc, text = run(zpath, "-size", "512", "-out", os.path.join(zdir, "out"), "-variant", "Case=Gold", "-threads", "2")
    zout = os.path.join(zdir, "out", "CubeEvil4-textures-512.c4tex")
    check(rc == 0 and os.path.exists(zout) and open(zout, "rb").read() == info, "ZIP (%s): the same file as the folder" % label)
check(snapshot(pack) == before, "the pack folder is unchanged after the ZIPs")

# the same input gives the same file
out2 = os.path.join(WORK, "out2")
rc, _ = run("-in", pack, "-size", "512", "-out", out2, "-variant", "Case=Gold", "-threads", "3")
check(rc == 0 and open(os.path.join(out2, "CubeEvil4-textures-512.c4tex"), "rb").read() == info, "same input, same file")
# without a choice: the main folder's case (a DDS, flat)
rc, text = run("-in", pack, "-size", "1024", "-out", os.path.join(WORK, "out3"))
check(rc == 0 and os.path.exists(os.path.join(WORK, "out3", "CubeEvil4-textures-1024.c4tex")), "size 1024, no choice")
# an output inside the pack folder is refused
rc, text = run("-in", pack, "-size", "512", "-out", os.path.join(game, "x"))
check(rc == 2 and "inside the texture pack folder" in text and snapshot(pack) == before, "output inside the pack refused")
# a choice that does not exist
rc, text = run("-in", pack, "-size", "512", "-out", out, "-variant", "Case=Silver")
check(rc == 2, "unknown choice refused")
# BC6H (an HDR format) is refused with its reason, the rest of the pack converts
bad = os.path.join(WORK, "Bad Pack")
os.makedirs(bad)
with open(os.path.join(bad, "tex1_32x32_cccc000011112222_14.dds"), "wb") as f:
    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<IIIIIII", hdr, 4, 124, 0x1007, 32, 32, 128, 0, 1)
    struct.pack_into("<II4s", hdr, 76, 32, 4, b"DX10")
    f.write(bytes(hdr) + struct.pack("<IIIII", 95, 3, 0, 1, 0) + bytes(64 * 16))
png(os.path.join(bad, "tex1_16x16_dddd000011112222_14.png"), 32, 32, lambda x, y: (1, 2, 3), alpha=False)
rc, text = run("-in", bad, "-size", "512", "-out", os.path.join(WORK, "outbad"))
report = open(os.path.join(WORK, "outbad", "CubeEvil4-textures-512.txt"), encoding="utf-8").read() if rc in (0, 3) else ""
check(rc == 3 and "1 images converted" in text and "BC6H is not supported" in report, "BC6H refused with its reason")
# cut and damaged files
cut = os.path.join(WORK, "cut.c4tex")
open(cut, "wb").write(info[: len(info) - 100])
rc, text = run("-check", cut)
check(rc != 0 and "incomplete" in text, "cut file found")
bad = bytearray(info)
bad[index_off + 5] ^= 0xFF
open(cut, "wb").write(bytes(bad))
rc, text = run("-check", cut)
check(rc != 0 and "index checksum" in text, "damaged index found")
bad = bytearray(info)
data_off, = struct.unpack_from("<Q", info, 32)
bad[data_off + 40] ^= 0xFF
open(cut, "wb").write(bytes(bad))
rc, text = run("-check", cut)
check(rc != 0 and "wrong checksum" in text, "damaged image data found")
open(cut, "wb").write(b"not a pack")
rc, text = run("-check", cut)
check(rc != 0, "a different file refused")
print("%d failed" % failures)
sys.exit(1 if failures else 0)
