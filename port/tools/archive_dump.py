#!/usr/bin/env python3
"""Print the table of contents of disc archives (.drs, .das, .dat with a table:
u32 count, u32 REL offset, two pad words, u32 offset per entry, 4-byte tag per
entry; runtime/formats/formats_arc.cpp).

Files with a part header (src/game/dvd.cpp cDvdQueue::readInit: 1 KB in front,
DvdHeader entries of 0x20 bytes from +0x20: type, size, dest, ofs ...; type 4 a
nested header at ofs, -2 skipped, -1 the end) are taken apart first: every part
is listed ("part N.M" as in the core log), the archive parts with their table.

Answers questions like "what does Hunk's player archive hold where the game
takes Leon's neck motions (PL_ARC 0x40..0x45)?" without a build:

    archive_dump.py --data DIR_OR_IMAGE --file em/pl06.drs --file em/pl00.drs [--entries 0x30-0x51]

--data takes a folder with the disc images (.iso/.gcm, read with the bounded
reader of original_import.gcm), a folder extracted with Dolphin (files/em/...),
a single image or the archive itself. Per entry: index, tag, offset, size (up to
the next entry), the first 16 bytes, and "same as #n" when an entry repeats an
offset. Indexes the game reads past the end of the table are listed as
"past the table" with the bytes the game would take as their offset. Nothing is
written.
"""
import argparse
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from original_import.gcm import Disc  # noqa: E402


def find_file(data: Path, name: str):
    """Yields (where, bytes) for every copy of `name` (path on the disc) in --data."""
    want = name.lower().replace("\\", "/").lstrip("/")
    base = want.rsplit("/", 1)[-1]
    if data.is_file():
        if data.name.lower() == base:
            yield str(data), data.read_bytes()
            return
        candidates = [data]
    else:
        loose = [p for p in data.rglob("*") if p.is_file() and p.as_posix().lower().endswith("/" + want)]
        for p in sorted(loose):
            yield str(p), p.read_bytes()
        if loose:
            return
        candidates = sorted(p for p in data.rglob("*") if p.is_file() and p.suffix.lower() in (".iso", ".gcm"))
    for image in candidates:
        try:
            disc = Disc(image)
        except (OSError, ValueError) as err:
            print(f"# {image}: not readable as a GameCube image ({err})", file=sys.stderr)
            continue
        for f in disc.files:
            if f["path"].lower() == want:
                yield f"{image} ({disc.info['game_id']} disc {disc.info['disc_number'] + 1})", disc.read(f["offset"], f["size"])


def parse_range(text):
    if not text:
        return None
    lo, _, hi = text.partition("-")
    lo = int(lo, 0)
    return lo, int(hi, 0) if hi else lo


TRANS = {0: "MRAM", 1: "sound block", 2: "sound PCM", 3: "ARAM", 4: "nested header"}


def parts(raw: bytes, base=0, nest=0):
    """(nest, index, type, offset, size) of each part of a headered file; empty for a plain file."""
    out = []
    index = 0
    for at in range(base + 0x20, min(base + 0x400, len(raw)) - 0x1F, 0x20):
        kind, size, _dest, ofs = struct.unpack_from(">iIII", raw, at)
        if kind == -1:
            return out
        if kind == -2:
            continue
        if kind not in TRANS or (kind != 4 and ofs + size > len(raw)):
            return []  # not a part header
        if kind == 4:
            if nest > 0:
                return []
            inner = parts(raw, ofs, nest + 1)
            if not inner:
                return []
            out += inner
        else:
            out.append((nest, index, kind, base + ofs if nest else ofs, size))
        index += 1
    return []


def dump(raw: bytes, entries=None):
    """Lines describing the archive `raw` (big-endian, as on the disc)."""
    if len(raw) < 0x20:
        return ["  too small for an archive"]
    count, rel, pad0, pad1 = struct.unpack_from(">4I", raw, 0)
    if count == 0 or count > 0x1000 or 0x10 + count * 8 > len(raw) or pad0 or pad1:
        return [f"  not an archive with a table (count {count:#x})"]
    offs = list(struct.unpack_from(f">{count}I", raw, 0x10))
    tags = [raw[0x10 + count * 4 + i * 4:0x14 + count * 4 + i * 4] for i in range(count)]
    end = rel if rel else len(raw)
    lines = [f"  {count} entries ({count:#x}), REL at {rel:#x}, {len(raw)} bytes"]
    lo, hi = entries if entries else (0, count - 1)
    for i in range(lo, hi + 1):
        if i >= count:
            # the game adds ofs[i] to the archive address without checking the count (PL_ARC_PTR)
            pos = 0x10 + i * 4
            word = raw[pos:pos + 4]
            note = (f"big-endian {struct.unpack('>I', word)[0]:#010x}, little-endian "
                    f"{struct.unpack('<I', word)[0]:#010x}") if len(word) == 4 else "past the file"
            lines.append(f"  #{i:02X} past the table: bytes {word.hex(' ')} ({note})")
            continue
        o = offs[i]
        tag = tags[i].rstrip(b"\0").decode("ascii", "replace")
        same = [k for k in range(count) if k != i and offs[k] == o]
        nxt = min([x for x in offs if x > o] + [end])
        size = nxt - o if o <= end else 0
        head = raw[o:o + 16].hex(" ") if o < len(raw) else "-"
        extra = f"  same as #{', #'.join(f'{k:02X}' for k in same)}" if same else ""
        lines.append(f"  #{i:02X} {tag:<4} at {o:#08x} size {size:#08x}  {head}{extra}")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, required=True, help="disc images, an extracted folder, or the file")
    ap.add_argument("--file", action="append", required=True, help="archive path on the disc, e.g. em/pl06.drs")
    ap.add_argument("--entries", help="entry range, e.g. 0x30-0x51 (default: all)")
    args = ap.parse_args()
    entries = parse_range(args.entries)
    found = 0
    for name in args.file:
        for where, raw in find_file(args.data, name):
            found += 1
            print(f"{name}: {where}")
            found_parts = parts(raw)
            if not found_parts:
                for line in dump(raw, entries):
                    print(line)
            for nest, index, kind, ofs, size in found_parts:
                print(f" part {nest}.{index}: {TRANS[kind]}, {size:#x} bytes at {ofs:#x}")
                if kind == 0:
                    for line in dump(raw[ofs:ofs + size], entries):
                        print(line)
            print()
    if not found:
        print(f"nothing found in {args.data}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
