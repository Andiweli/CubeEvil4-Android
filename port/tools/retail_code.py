#!/usr/bin/env python3
"""Compare the code of two releases (port/docs/retail-eu-plan.md): the debug build's main.dol / REL
modules against the European ones. The debug build's symbols (config/G4BE08/sym_map.tsv) name its
functions; this finds the same function in the other release by its instruction words and shows
both side by side. Read-only; nothing of the discs is written or kept.

  retail_code.py find  DEBUG OTHER ADDR SIZE      the other release's function with the most equal words
                                                  (bl/bc targets masked); DOL: addresses, REL: text offsets
  retail_code.py dis   DEBUG OTHER ADDR SIZE      both disassembled side by side (llvm-objdump, PowerPC)
  retail_code.py func  FILE ADDR                  the function around ADDR (stwu r1 prologue .. blr)
  retail_code.py calls FILE TARGET                every `bl TARGET`, with the r3..r5 constants before it
  retail_code.py str   FILE TEXT                  where a string lies and the lis/addi pairs that build it
  retail_code.py word  FILE MASK VALUE            instruction words with word & MASK == VALUE (hex)

FILE is a main.dol, a .rel (its text section; addresses are then offsets into it) or a disc image
(.gcm/.iso: its main.dol, read with the bounded reader of original_import.gcm). `dis` shows the other
release's whole function (up to its blr), so code the other release added at the end is not cut off.
Used for patches 0117-0126 (R1, R2, R3, R7: e.g. the European SubScreenExec skips four id tables for the
radio screen).
"""
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def load(path):
    """The bytes of a DOL or REL, or the main.dol of a disc image."""
    if str(path).lower().endswith((".gcm", ".iso")):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from original_import.gcm import Disc
        return bytes(Disc(path).dol)
    return Path(path).read_bytes()


def spans(path):
    """(file offset, address, size) of the code: the DOL's text sections, or a REL's text section at 0."""
    d = load(path)
    if str(path).lower().endswith(".rel"):
        nsec, sio = struct.unpack_from(">II", d, 0x0C)
        for i in range(nsec):
            off, size = struct.unpack_from(">II", d, sio + i * 8)
            if off & 1 and size:
                return d, [(off & ~1, 0, size)]
        raise SystemExit(f"{path}: no text section")
    offs = struct.unpack_from(">18I", d, 0)
    addrs = struct.unpack_from(">18I", d, 0x48)
    sizes = struct.unpack_from(">18I", d, 0x90)
    return d, [(o, a, s) for o, a, s in zip(offs[:7], addrs[:7], sizes[:7]) if s]


def read(d, sp, addr, size):
    for o, a, s in sp:
        if a <= addr < a + s:
            return d[o + addr - a:o + addr - a + size]
    raise SystemExit(f"{addr:#x} is not in the code")


def norm(w):
    op = w >> 26
    if op == 18:  # b / bl: the targets differ between the builds
        return w & 0xFC000003
    if op == 16:  # bc
        return w & 0xFFFF0003
    return w


def find(debug, other, addr, size):
    d, sd = spans(debug)
    e, se = spans(other)
    want = [norm(w) for w in struct.unpack(f">{size // 4}I", read(d, sd, addr, size))]
    best = (0, 0)
    for o, a, s in se:
        words = [norm(w) for w in struct.unpack(f">{s // 4}I", e[o:o + s])]
        for i in range(len(words) - len(want)):
            if words[i] != want[0] and words[i + 1] != want[1]:
                continue
            score = sum(1 for k in range(len(want)) if words[i + k] == want[k])
            if score > best[0]:
                best = (score, a + i * 4)
    # the other release's function to its end (its blr before the next prologue), at least as long as ours
    _, other_size = func(other, best[1])
    return best[1], best[0], len(want), read(d, sd, addr, size), read(e, se, best[1], max(size, other_size))


def disassemble(code):
    tool = shutil.which("llvm-objdump-18") or shutil.which("llvm-objdump")
    copy = shutil.which("llvm-objcopy-18") or shutil.which("llvm-objcopy")
    if not tool or not copy:
        raise SystemExit("llvm-objdump / llvm-objcopy needed")
    with tempfile.TemporaryDirectory() as t:
        b, o = Path(t) / "f.bin", Path(t) / "f.o"
        b.write_bytes(code)
        subprocess.run([copy, "-I", "binary", "-O", "elf32-powerpc", str(b), str(o)], check=True)
        out = subprocess.run([tool, "-D", "-j", ".data", "--triple=powerpc-unknown-unknown", str(o)], check=True,
                             capture_output=True, text=True).stdout
    lines = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(.*)$", line)
        if m:
            lines.append((int(m.group(1), 16), re.sub(r"\s+", " ", m.group(2)).split("<")[0].strip()))
    return lines


def func(path, addr):
    d, sp = spans(path)
    for o, a, s in sp:
        if not a <= addr < a + s:
            continue
        word = lambda x: struct.unpack_from(">I", d, o + x - a)[0]
        start = addr
        while start > a and (word(start) & 0xFFFF0000) != 0x94210000:  # stwu r1, -N(r1)
            start -= 4
        end = addr
        while end < a + s - 4 and not (word(end) == 0x4E800020 and (word(end + 4) & 0xFFFF0000) == 0x94210000):
            end += 4
        return start, end + 4 - start
    raise SystemExit(f"{addr:#x} is not in the code")


def calls(path, target):
    d, sp = spans(path)
    for o, a, s in sp:
        for i in range(0, s, 4):
            w = struct.unpack_from(">I", d, o + i)[0]
            if w >> 26 != 18 or w & 3 != 1:
                continue
            li = w & 0x03FFFFFC
            if li & 0x02000000:
                li -= 0x04000000
            if a + i + li != target:
                continue
            ctx = []
            for k in range(1, 9):
                if i - 4 * k < 0:
                    break
                p = struct.unpack_from(">I", d, o + i - 4 * k)[0]
                op, rd, ra, imm = p >> 26, (p >> 21) & 31, (p >> 16) & 31, p & 0xFFFF
                simm = imm - 0x10000 if imm & 0x8000 else imm
                if op == 14 and rd in (3, 4, 5):
                    ctx.append(f"li r{rd},{simm:#x}" if ra == 0 else f"addi r{rd},r{ra},{simm:#x}")
                elif op == 15 and rd in (3, 4, 5):
                    ctx.append(f"lis r{rd},{imm:#x}")
            print(f"{a + i:#010x}: " + "; ".join(reversed(ctx)))


def strings(path, text):
    d, sp = spans(path)
    dol = not str(path).lower().endswith(".rel")
    secs = sp
    if dol:
        offs = struct.unpack_from(">18I", d, 0)
        addrs = struct.unpack_from(">18I", d, 0x48)
        sizes = struct.unpack_from(">18I", d, 0x90)
        secs = [(o, a, s) for o, a, s in zip(offs, addrs, sizes) if s]
    for o, a, s in secs:
        i = d.find(text.encode(), o, o + s)
        while i >= 0:
            at = a + i - o
            print(f"string at {at:#010x}")
            hi, lo = (at + 0x8000) >> 16 & 0xFFFF, at & 0xFFFF
            for co, ca, cs in sp:
                for k in range(0, cs - 4, 4):
                    w = struct.unpack_from(">I", d, co + k)[0]
                    if w >> 26 == 15 and (w & 0xFFFF) == hi:
                        rd = (w >> 21) & 31
                        for n in range(1, 12):
                            if k + 4 * n >= cs:
                                break
                            p = struct.unpack_from(">I", d, co + k + 4 * n)[0]
                            if p >> 26 in (14, 32, 36) and (p >> 16) & 31 == rd and (p & 0xFFFF) == lo:
                                print(f"  built at {ca + k:#010x}")
                                break
            i = d.find(text.encode(), i + 1, o + s)


def words(path, mask, value):
    d, sp = spans(path)
    for o, a, s in sp:
        for i in range(0, s, 4):
            w = struct.unpack_from(">I", d, o + i)[0]
            if w & mask == value:
                print(f"{a + i:#010x} {w:08x}")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd, a = sys.argv[1], sys.argv[2:]
    if cmd in ("find", "dis"):
        at, score, n, mine, theirs = find(a[0], a[1], int(a[2], 16), int(a[3], 16))
        print(f"debug {int(a[2], 16):#x}: best match {at:#x}, {score} of {n} words equal, "
              f"{len(theirs):#x} bytes there")
        if cmd == "dis":
            left, right = disassemble(mine), disassemble(theirs)
            for k in range(max(len(left), len(right))):
                l = f"{left[k][0]:4x}: {left[k][1]}" if k < len(left) else ""
                r = f"{right[k][0]:4x}: {right[k][1]}" if k < len(right) else ""
                print(f"{l:<48}| {r}")
    elif cmd == "func":
        start, size = func(a[0], int(a[1], 16))
        print(f"{start:#010x} {size:#x}")
    elif cmd == "calls":
        calls(a[0], int(a[1], 16))
    elif cmd == "str":
        strings(a[0], a[1])
    elif cmd == "word":
        words(a[0], int(a[1], 16), int(a[2], 16))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
