#!/usr/bin/env python3
"""Lay out the disc files for the whole-program port (runtime/dvd.cpp): the debug
build (G4BE08), the European release (G4BP08, port/docs/retail-eu-plan.md) or the US
release (G4BE08 with another layout, port/docs/retail-us-plan.md).

The port reads the disc from a folder, one subfolder per disc, in the layout
Dolphin's "Extract Entire Disc" writes:

    <out>/disc0/sys/boot.bin  bi2.bin  apploader.img  main.dol  fst.bin
    <out>/disc0/files/<every file of the FST>
    <out>/disc1/...

Sources, reusing the earlier port's import (port/tools/original_import):

    whole_disc_folder.py --disc disc1.iso --disc disc2.iso --out DIR
        reads GameCube images with original_import.gcm.Disc, the bounded reader
        that checks the header, the DOL and the FST of every image;
    whole_disc_folder.py --store STORE --out DIR
        materialises a private store written by import_original.py
        (manifest.json + blobs/<sha256>, every blob checked against its hash);
    whole_disc_folder.py --check DIR [--disc IMAGE ...]
        verifies an existing folder (also one extracted with Dolphin): every
        FST file present with its FST size; with images also that the folder
        holds exactly these discs (header and FST), not another release.

Both discs of a folder come from the same release (disc_release.py); a debug
disc next to a European or US one is refused.

Disc data is never written into the repository; --out must lie outside it.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from disc_release import SUPPORTED, release  # noqa: E402
from original_import.gcm import Disc, parse_fst  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]


def outside_repository(path: Path) -> Path:
    path = path.resolve()
    if path == ROOT or ROOT in path.parents:
        sys.exit(f"{path}: disc data must not be written into the repository")
    return path


def safe_target(files: Path, rel: str) -> Path:
    target = (files / rel).resolve()
    if files.resolve() not in target.parents:
        sys.exit(f"unsafe FST path {rel!r}")
    return target


def write_bytes(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".part")
    tmp.write_bytes(data)
    os.replace(tmp, path)


def copy_span(image: Path, offset: int, size: int, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_name(target.name + ".part")
    with image.open("rb") as src, tmp.open("wb") as dst:
        src.seek(offset)
        left = size
        while left:
            chunk = src.read(min(left, 4 << 20))
            if not chunk:
                sys.exit(f"{image}: short read at {offset:#x}")
            dst.write(chunk)
            left -= len(chunk)
    os.replace(tmp, target)


def one_release(named: list) -> str:
    """The release of (name, boot.bin) pairs; exits on an unsupported one or a mix."""
    kinds = {}
    for name, boot in named:
        kind = release(boot)
        if kind not in SUPPORTED:
            sys.exit(f"{name}: game id {boot[:6].decode('ascii', 'replace')} ({kind}), "
                     f"the port needs G4BE08 (debug build or US release) or G4BP08 (European release)")
        kinds.setdefault(kind, name)
    if len(kinds) > 1:
        sys.exit("discs of different releases: " + ", ".join(f"{n} is {k}" for k, n in kinds.items()))
    return next(iter(kinds))


def from_images(images: list, out: Path) -> None:
    seen = set()
    discs = [Disc(image) for image in images]  # bounded checks of header, DOL and FST
    kind = one_release([(str(d.path), d.header) for d in discs])
    for disc in discs:
        image, number = disc.path, disc.info["disc_number"]
        if number in seen:
            sys.exit(f"{image}: disc {number + 1} given twice")
        seen.add(number)
        if kind == "debug" and not disc.info["build_date_evidence"]:
            print(f"warning: {image}: main.dol carries no 'Nov 25 2004' build date; "
                  f"the port's code matches the debug build of that date", file=sys.stderr)
        base = out / f"disc{number}"
        sys_dir, files = base / "sys", base / "files"
        app_header = disc.read(0x2440, 32)
        app_size = 32 + struct.unpack_from(">I", app_header, 20)[0] + struct.unpack_from(">I", app_header, 24)[0]
        write_bytes(sys_dir / "boot.bin", disc.header)
        write_bytes(sys_dir / "bi2.bin", disc.read(0x440, 0x2000))
        write_bytes(sys_dir / "apploader.img", disc.read(0x2440, app_size))
        write_bytes(sys_dir / "main.dol", disc.dol)
        write_bytes(sys_dir / "fst.bin", disc.fst)
        for i, entry in enumerate(disc.files):
            copy_span(disc.path, entry["offset"], entry["size"], safe_target(files, entry["path"]))
            if i % 200 == 0:
                print(f"disc {number + 1}: {i + 1}/{len(disc.files)} {entry['path']}", flush=True)
        print(f"disc {number + 1}: {len(disc.files)} files -> {base}")


def blob(store: Path, digest: str) -> bytes:
    data = (store / "blobs" / digest).read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        sys.exit(f"store blob {digest} fails its SHA-256 check")
    return data


def from_store(store: Path, out: Path) -> None:
    manifest = json.loads((store / "manifest.json").read_text())
    one_release([(f"store disc {d['disc_number'] + 1}", blob(store, d["system_blobs"]["boot"]))
                 for d in manifest["discs"]])
    for disc in manifest["discs"]:
        number = disc["disc_number"]
        base = out / f"disc{number}"
        names = {"boot": "boot.bin", "bi2": "bi2.bin", "apploader": "apploader.img",
                 "dol": "main.dol", "fst": "fst.bin"}
        for key, name in names.items():
            write_bytes(base / "sys" / name, blob(store, disc["system_blobs"][key]))
        files = [f for f in manifest["files"] if f["disc_number"] == number]
        for i, f in enumerate(files):
            data = blob(store, f["sha256"])
            if len(data) != f["size"]:
                sys.exit(f"store file {f['path']}: size {len(data)}, FST says {f['size']}")
            write_bytes(safe_target(base / "files", f["path"]), data)
            if i % 200 == 0:
                print(f"disc {number + 1}: {i + 1}/{len(files)} {f['path']}", flush=True)
        print(f"disc {number + 1}: {len(files)} files -> {base}")


def check(out: Path, images: list = ()) -> int:
    bases = [out / d for d in ("disc0", "disc1") if (out / d).is_dir()] or [out]
    problems = 0
    kind = one_release([(str(b), (b / "sys" / "boot.bin").read_bytes()) for b in bases])
    for image in images:
        disc = Disc(image)
        base = out / f"disc{disc.info['disc_number']}"
        if not base.is_dir():
            print(f"missing: {base} (disc {disc.info['disc_number'] + 1} of {image})")
            problems += 1
        elif ((base / "sys" / "boot.bin").read_bytes() != disc.header or
              (base / "sys" / "fst.bin").read_bytes() != disc.fst):
            print(f"other disc: {base} was not laid out from {image}")
            problems += 1
    for base in bases:
        fst = (base / "sys" / "fst.bin").read_bytes()
        boot = (base / "sys" / "boot.bin").read_bytes()
        entries = parse_fst(fst, 1 << 40)
        files = [e for e in entries if e["kind"] == "file"]
        for e in files:
            path = base / "files" / e["path"]
            if not path.is_file():
                print(f"missing: {path}")
                problems += 1
            elif path.stat().st_size != e["size"]:
                print(f"size differs: {path} ({path.stat().st_size}, FST {e['size']})")
                problems += 1
        print(f"{base}: {boot[:6].decode('ascii', 'replace')} ({kind}) disc {boot[6] + 1}, "
              f"{len(files)} files, {problems} problems")
    return 1 if problems else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--disc", action="append", type=Path, help="GameCube disc image (.iso/.gcm), once per disc")
    ap.add_argument("--store", type=Path, help="private store of import_original.py")
    ap.add_argument("--check", type=Path, help="verify an existing disc folder (with --disc: made from these)")
    ap.add_argument("--out", type=Path, help="target folder (outside the repository)")
    args = ap.parse_args()
    if not args.check and bool(args.disc) == bool(args.store):
        ap.error("give --disc images or --store (or --check)")
    if args.check:
        return check(args.check, args.disc or ())
    if not args.out:
        ap.error("--out is required")
    out = outside_repository(args.out)
    if args.disc:
        from_images(args.disc, out)
    else:
        from_store(args.store.resolve(), out)
    return check(out)


if __name__ == "__main__":
    sys.exit(main())
