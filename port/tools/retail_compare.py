#!/usr/bin/env python3
"""Compare the disc files of another release with the debug build's (port/docs/retail-eu-plan.md).

    retail_compare.py --ref DEBUG_DATA --other EU_DATA [--out REPORT.txt] [--json OUT.json] [--jobs N]

DEBUG_DATA / EU_DATA: disc folders as whole_disc_folder.py writes them (disc0/, disc1/ with sys/ and
files/), the app's import folder (disc1/, disc2/) or one extracted disc (sys/, files/).

The port runs the debug build's code on the other release's data. What that code can trip over:

  files     present on one release only (by path, case-insensitive), changed, identical
  modules   the REL modules in every file (Rel/*.rel, embedded in em/*.drs ...): the core loads the
            library by the module id of the header (runtime/os_link.cpp), so a module of the same
            name must keep its id
  archives  the layout of every archive on both (.drs, .das with the room body unpacked, .dat
            bodies, .evd events, read with tools/motion/archive.py). The code addresses archive
            entries by index (PL_ARC, SS_ARC_PTR, ROOM_ARC) and event bins by name:
              same      the same kinds of entries in the same order
              appended  the other release adds entries at the end only (index access still lands
                        on the same kinds)
              shifted   an entry of another kind at an index the debug code reaches
              fewer     the other release has fewer entries
              evd-missing  event bins of the debug build the other lacks (looked up by name)
              unreadable   not an archive of the expected layout on one side (tables, sound)

Disc data stays outside the repository; the report holds names, sizes and kinds only.
"""
import argparse
import json
import os
import re
import struct
import sys
from collections import Counter
from multiprocessing import Pool
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "motion"))
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import archive  # noqa: E402
from disc_release import release  # noqa: E402

ARCHIVE_EXTS = (".drs", ".das", ".dat", ".evd")
REL_HEADER = re.compile(rb"(?s)(?=.{4}\x00{8}.{4}\x00\x00\x00[\x40\x48\x4c])")
REL_INFO_OFFSET = {1: 0x40, 2: 0x48, 3: 0x4C}


def discs(data: Path) -> list:
    """[(disc number from 0, folder with sys/ and files/)]."""
    if (data / "disc0" / "files").is_dir():
        return [(n, data / f"disc{n}") for n in (0, 1) if (data / f"disc{n}" / "files").is_dir()]
    if (data / "disc1" / "files").is_dir():
        return [(n - 1, data / f"disc{n}") for n in (1, 2) if (data / f"disc{n}" / "files").is_dir()]
    if (data / "files").is_dir():
        return [(0, data)]
    sys.exit(f"{data}: no disc folder (disc0/files, disc1/files or files)")


def file_index(base: Path) -> dict:
    out = {}
    files = base / "files"
    for dirpath, dirnames, filenames in os.walk(files):
        dirnames.sort()
        for name in filenames:
            p = Path(dirpath) / name
            out[p.relative_to(files).as_posix().lower()] = p
    return out


def rel_modules(data: bytes) -> list:
    """[(offset, module id)] of the REL module headers in a file."""
    out = []
    for m in REL_HEADER.finditer(data):
        o = m.start()
        if o % 4 or o + 0x4C > len(data):
            continue
        mid, _, _, nsec, info, _, _, version = struct.unpack_from(">8I", data, o)
        if version not in REL_INFO_OFFSET or info != REL_INFO_OFFSET[version]:
            continue
        rel_ofs, imp_ofs = struct.unpack_from(">2I", data, o + 0x24)
        if not 1 <= nsec <= 64 or not 0 < mid <= 0x1000 or not 0 < rel_ofs <= len(data) or not 0 < imp_ofs <= len(data):
            continue
        out.append((o, mid))
    return out


def layout(path: Path):
    """("evd", [bin names]) or (kind, [entry tags]) of an archive."""
    a = archive.Archive(str(path))
    if a.kind == "evd":
        return "evd", [label for _, _, label in a._load()]
    return a.kind, [tag for tag, _, _ in a._load()]


def classify(ref, other) -> tuple:
    """(status, detail) for two layouts as layout() returns them."""
    (kind, a), (_, b) = ref, other
    if kind == "evd":
        missing = [n for n in a if n not in set(b)]
        if missing:
            return "evd-missing", f"{len(a)} -> {len(b)} bins, missing: {', '.join(missing[:6])}"
        return ("same", f"{len(a)} bins") if a == b else ("same", f"{len(a)} -> {len(b)} bins, none missing")
    if a == b:
        return "same", f"{len(a)} entries"
    first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), None)
    if first is None:
        status = "appended" if len(b) > len(a) else "fewer"
        return status, f"{len(a)} -> {len(b)} entries"
    return "shifted", (f"entry {first} (archive index {first + archive.ARC_INDEX_BASE}): "
                       f"{a[first] or '(empty)'} -> {b[first] or '(empty)'}; "
                       f"debug {' '.join(t or '-' for t in a)} | other {' '.join(t or '-' for t in b)}")


def compare_file(job):
    rel, ref_path, other_path = job
    row = {"path": rel}
    ref_data, other_data = ref_path.read_bytes(), other_path.read_bytes()
    row["changed"] = ref_data != other_data
    row["sizes"] = [len(ref_data), len(other_data)]
    mods = [[m for _, m in rel_modules(d)] if len(d) <= 64 << 20 else None for d in (ref_data, other_data)]
    if mods[0] or mods[1]:
        row["modules"] = mods
    if os.path.splitext(rel)[1] in ARCHIVE_EXTS and row["changed"]:
        try:
            row["archive"] = classify(layout(ref_path), layout(other_path))
        except Exception as e:  # noqa: BLE001 - tables and sound files are no archives
            row["archive"] = ("unreadable", f"{type(e).__name__}: {e}"[:160])
    elif os.path.splitext(rel)[1] in ARCHIVE_EXTS:
        row["archive"] = ("same", "identical file")
    return row


def compare(ref: Path, other: Path, jobs: int) -> dict:
    ref_discs, other_discs = dict(discs(ref)), dict(discs(other))
    result = {"ref": str(ref), "other": str(other), "discs": []}
    for n in sorted(set(ref_discs) & set(other_discs)):
        boots = [(d[n] / "sys" / "boot.bin").read_bytes()[:0x440] for d in (ref_discs, other_discs)]
        a, b = file_index(ref_discs[n]), file_index(other_discs[n])
        common = sorted(set(a) & set(b))
        with Pool(jobs) as pool:
            rows = pool.map(compare_file, [(r, a[r], b[r]) for r in common], chunksize=4)
        result["discs"].append({
            "disc": n + 1,
            "releases": [release(x) for x in boots],
            "only_ref": sorted(set(a) - set(b)),
            "only_other": sorted(set(b) - set(a)),
            "rows": rows,
        })
    return result


def report(result: dict) -> str:
    out = [f"Debug data:  {result['ref']}", f"Other data:  {result['other']}", ""]
    for d in result["discs"]:
        rows = d["rows"]
        out.append(f"== Disc {d['disc']}: {d['releases'][0]} -> {d['releases'][1]} ==")
        changed = sum(r["changed"] for r in rows)
        out.append(f"files: {len(rows)} on both ({len(rows) - changed} identical, {changed} changed), "
                   f"{len(d['only_ref'])} only debug, {len(d['only_other'])} only other")
        by_dir = Counter(p.rsplit("/", 1)[0] if "/" in p else "." for p in d["only_ref"])
        out.append("  only debug, by folder: " + ", ".join(f"{k} {v}" for k, v in sorted(by_dir.items())))
        out += [f"    {p}" for p in d["only_ref"] if not p.startswith("bio4.")]
        by_dir = Counter(p.rsplit("/", 1)[0] if "/" in p else "." for p in d["only_other"])
        out.append("  only other, by folder: " + ", ".join(f"{k} {v}" for k, v in sorted(by_dir.items())))
        # modules: same ids at the same places?
        mod_rows = [r for r in rows if "modules" in r]
        moved = [r for r in mod_rows if r["modules"][0] != r["modules"][1]]
        gone = [p for p in d["only_ref"] if p.endswith(".rel")]
        out.append(f"modules: {len(mod_rows)} files with REL modules on both, {len(moved)} with other ids; "
                   f"REL files only debug: {', '.join(gone) or 'none'}")
        out += [f"    {r['path']}: {r['modules'][0]} -> {r['modules'][1]}" for r in moved]
        statuses = Counter(r["archive"][0] for r in rows if "archive" in r)
        out.append("archives: " + ", ".join(f"{k} {v}" for k, v in sorted(statuses.items())))
        for status in ("shifted", "fewer", "evd-missing", "appended", "unreadable"):
            for r in rows:
                if r.get("archive", ("",))[0] == status:
                    out.append(f"  {status:12} {r['path']}: {r['archive'][1]}")
        out.append("")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", type=Path, required=True, help="disc data of the debug build")
    ap.add_argument("--other", type=Path, required=True, help="disc data of the release to compare")
    ap.add_argument("--out", type=Path, help="write the report here as well")
    ap.add_argument("--json", type=Path, help="every compared file as JSON")
    ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)))
    args = ap.parse_args()
    result = compare(args.ref.resolve(), args.other.resolve(), args.jobs)
    text = report(result)
    print(text)
    if args.out:
        args.out.write_text(text + "\n", encoding="utf-8")
    if args.json:
        args.json.write_text(json.dumps(result, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
