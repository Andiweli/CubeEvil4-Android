#!/usr/bin/env python3
"""The app's disc catalogs from an extracted disc set (port/docs/retail-eu-plan.md, R4).

    make_disc_catalogs.py --data DIR [--out DIR] [--suffix -eu]

DIR: disc folders as port/tools/whole_disc_folder.py writes them (disc0/, disc1/ with sys/ and
files/) or the app's import folder (disc1/, disc2/). Writes into --out (default: print a summary):

  references<suffix>.tsv  disc, path, slice offset, slice size, SHA-1 of the release's code: sys/main.dol
                          of each disc, every Rel/*.rel, and the REL module inside every em/*.drs
                          (ReferenceCatalog, GameCubeDisc.validate: a damaged or foreign image fails)
  rooms<suffix>.tsv       disc, path, unpacked size, SHA-256 unpacked, SHA-256 of the file for every
                          St*/r*.das (RoomRepository, RoomFileCheck)
  blocks<suffix>.tsv      the same for the room block archives St*/r*_*.dat (stored unpacked)

A file on both discs is listed once, on the first disc that has it, as in the debug build's
catalogs; rooms are listed per disc. The catalogs hold names, sizes and hashes only, no disc data.
Checked by regenerating the debug build's catalogs (--check-debug: equal to the app's assets).
"""
import argparse
import hashlib
import os
import re
import sys
from multiprocessing import Pool
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "motion"))
sys.path.insert(0, str(ROOT / "tools"))
import drs  # noqa: E402
import yz2  # noqa: E402

ASSETS = ROOT / "port" / "android-decomp" / "app" / "src" / "main" / "assets"
ROOM = re.compile(r"St([0-9a-f])/r[0-9a-f]{3}\.das", re.I)
BLOCK = re.compile(r"St([0-9a-f])/r[0-9a-f]{3}_[0-9a-f]{2}\.dat", re.I)


def discs(data: Path):
    if (data / "disc0" / "files").is_dir():
        return [(n + 1, data / f"disc{n}") for n in (0, 1) if (data / f"disc{n}" / "files").is_dir()]
    return [(n, data / f"disc{n}") for n in (1, 2) if (data / f"disc{n}" / "files").is_dir()]


def files_of(base: Path):
    root = base / "files"
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for f in sorted(filenames):
            p = Path(dirpath) / f
            out.append((p.relative_to(root).as_posix(), p))
    return out


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def references(sets):
    rows, seen = [], set()
    for n, base in sets:
        dol = (base / "sys" / "main.dol").read_bytes()
        rows.append((n, "sys/main.dol", 0, len(dol), sha1(dol)))
    for n, base in sets:
        for rel, p in files_of(base):
            key = rel.lower()
            if key in seen:
                continue
            if key.startswith("rel/") and key.endswith(".rel"):
                seen.add(key)
                d = p.read_bytes()
                rows.append((n, "files/" + rel, 0, len(d), sha1(d)))
            elif key.startswith("em/") and key.endswith(".drs"):
                seen.add(key)
                d = p.read_bytes()
                try:
                    a = drs.Drs(d)
                except AssertionError:
                    continue
                if a.rel is None:
                    continue
                import struct
                rel_offset = struct.unpack(">I", d[drs.TABLE_SIZE + 4:drs.TABLE_SIZE + 8])[0]
                off = drs.TABLE_SIZE + rel_offset
                rows.append((n, "files/" + rel, off, len(a.rel), sha1(a.rel)))
    return rows


def room_row(job):
    n, rel, p = job
    d = Path(p).read_bytes()
    body, _, _ = yz2.decode(d, drs.TABLE_SIZE)
    return (n, rel, len(body), sha256(body), sha256(d))


def rooms(sets, jobs):
    work, blocks = [], []
    for n, base in sets:
        for rel, p in files_of(base):
            if ROOM.fullmatch(rel):
                work.append((n, rel, str(p)))
            elif BLOCK.fullmatch(rel):
                d = p.read_bytes()
                blocks.append((n, rel, len(d), sha256(d), sha256(d)))
    with Pool(jobs) as pool:
        room_rows = pool.map(room_row, work, chunksize=1)
    return room_rows, blocks


def write(path: Path, header: str, rows):
    with open(path, "w", encoding="utf-8", newline="\n") as w:
        w.write(header + "\n")
        for r in rows:
            w.write("\t".join(str(x) for x in r) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--suffix", default="")
    ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)))
    ap.add_argument("--check-debug", action="store_true",
                    help="compare with the debug build's catalogs of the app (as sets of rows)")
    args = ap.parse_args()
    sets = discs(args.data.resolve())
    refs = references(sets)
    room_rows, block_rows = rooms(sets, args.jobs)
    print(f"references: {len(refs)} ({', '.join(f'disc {n}: {sum(1 for r in refs if r[0] == n)}' for n, _ in sets)}), "
          f"rooms: {len(room_rows)}, blocks: {len(block_rows)}")
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        write(args.out / f"references{args.suffix}.tsv", "# disc\tpath\tslice_offset\tslice_size\tsha1", refs)
        write(args.out / f"rooms{args.suffix}.tsv", "# disc\tpath\tdecoded_size\tdecoded_sha256\tsource_sha256", room_rows)
        write(args.out / f"blocks{args.suffix}.tsv", "# disc\tpath\tdecoded_size\tdecoded_sha256\tsource_sha256", block_rows)
    if args.check_debug:
        bad = 0
        for name, rows in (("references.tsv", refs), ("rooms.tsv", room_rows), ("blocks.tsv", block_rows)):
            have = {tuple(l.rstrip("\n").split("\t")) for l in open(ASSETS / name, encoding="utf-8") if not l.startswith("#") and l.strip()}
            made = {tuple(str(x) for x in r) for r in rows}
            same = have == made
            bad += not same
            print(f"{name}: {'equal' if same else 'DIFFERS'} ({len(have)} in the app, {len(made)} made; "
                  f"only app {len(have - made)}, only made {len(made - have)})")
            if not same:
                for r in sorted(have - made)[:5]:
                    print("  only app ", r)
                for r in sorted(made - have)[:5]:
                    print("  only made", r)
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
