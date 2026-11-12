"""End-to-end test of the CubeEvil4 Texture Packer with a small made-up Dolphin pack (coloured test patterns, no game
data): game-ID folder, an Extra folder with variants, a copy "(1)", desktop.ini, DDS with mip levels, an _arb DDS,
PNGs with and without alpha and a large one to shrink. Checks the result with -check, that the pack folder is unchanged,
that an output inside the pack folder is refused, that the same input gives the same file, and that cut or damaged
pack files are found.  python -I run_e2e.py TEXPACK_EXE WORK_DIR"""
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import zlib

EXE, WORK = sys.argv[1], sys.argv[2]
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
check("4 images converted" in text and "files skipped" in text, "four images, the copy and the text file skipped")
check(os.path.exists(os.path.join(out, "CubeEvil4-textures-512.txt")), "report next to the pack file")
report = open(os.path.join(out, "CubeEvil4-textures-512.txt"), encoding="utf-8").read()
check("(1).dds" in report and "readme.txt" in report and "desktop.ini" not in report, "report lists the skipped files")
check("Case: Gold" in report, "report names the choice")
rc, text = run("-check", target, "-png", os.path.join(WORK, "png"), "-limit", "10")
print(text)
check(rc == 0 and "The pack is fine." in text and "Entries: 4 " in text, "-check: fine, 4 entries")
check("Exported 4 image(s)" in text, "-check exports PNGs")
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
