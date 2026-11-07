#!/usr/bin/env python3
"""Tests whole_disc_folder.py on a synthetic two-disc image set (no game data):
image -> folder, store -> folder (same bytes), --check, refusal of the
repository as target and of a foreign game id; the European release (G4BP08),
refusal of a debug/European mix, of the untested US release, and --check with
images notices a folder laid out from other discs."""
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "whole_disc_folder.py"
ROOT = Path(__file__).resolve().parents[2]


def fst(entries):
    """entries: (path parts..., data) for files; directories are implied."""
    table, names = [(1, 0, 0, 0)], b""
    tree = {}
    for parts, data in entries:
        node = tree
        for d in parts[:-1]:
            node = node.setdefault(d, {})
        node[parts[-1]] = data
    files = []

    def walk(node, parent):
        nonlocal names
        for name in sorted(node, key=str.lower):
            index = len(table)
            offset = len(names)
            names += name.encode() + b"\0"
            if isinstance(node[name], dict):
                table.append([1, offset, parent, 0])
                walk(node[name], index)
                table[index][3] = len(table)
            else:
                table.append([0, offset, 0, len(node[name])])
                files.append((index, node[name]))
    walk(tree, 0)
    table[0] = (1, 0, 0, len(table))
    return table, names, files


def image(game_id, disc_no, entries, fst_off=0x28FE00):
    """A minimal GCM: header, apploader, one-section DOL, FST, file data. The FST sits where
    the debug build has it (disc_release.py tells the debug build by that), unless `fst_off`."""
    table, names, files = fst(entries)
    data_start, data = 0x2A0000, bytearray()
    for index, content in files:
        table[index][2] = data_start + len(data)
        data += content + bytes((-len(content)) % 32)
    raw_fst = b"".join(struct.pack(">III", (t[0] << 24) | t[1], t[2], t[3]) for t in table) + names
    dol = bytearray(0x200)
    struct.pack_into(">I", dol, 0, 0x100)            # text0 offset
    struct.pack_into(">I", dol, 0x90, 0x100)         # text0 size
    dol[0x100:0x110] = b"Nov 25 2004\x0012:00:00"[:16]
    img = bytearray(data_start + len(data))
    img[0:6] = game_id.encode()
    img[6] = disc_no
    img[0x1C:0x20] = bytes.fromhex("c2339f3d")
    img[0x20:0x30] = b"biohazard4 test\0"
    dol_off = 0x3000
    struct.pack_into(">III", img, 0x420, dol_off, fst_off, len(raw_fst))
    struct.pack_into(">II", img, 0x2440 + 20, 0x40, 0)  # apploader body size
    img[dol_off:dol_off + len(dol)] = dol
    img[fst_off:fst_off + len(raw_fst)] = raw_fst
    img[data_start:] = data
    return bytes(img), raw_fst


class DiscFolderTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.discs = {
            0: [(("bgm", "bio4str.hed"), b"HED" * 40), (("etc", "sizetbl.dat"), b"\x00\x01" * 9),
                (("Font", "common_p.fnt"), bytes(range(256)))],
            1: [(("st3", "r300.das"), b"DAS!" * 100)],
        }
        self.images = []
        for n, entries in self.discs.items():
            raw, _ = image("G4BE08", n, entries)
            path = self.dir / f"disc{n + 1}.iso"
            path.write_bytes(raw)
            self.images.append(path)

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)

    def test_images_store_and_check(self):
        out = self.dir / "data"
        r = self.run_tool("--disc", self.images[0], "--disc", self.images[1], "--out", out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        for n, entries in self.discs.items():
            for parts, content in entries:
                self.assertEqual((out / f"disc{n}" / "files" / Path(*parts)).read_bytes(), content)
        self.assertEqual((out / "disc1" / "sys" / "boot.bin").read_bytes()[:7], b"G4BE08\x01")
        self.assertEqual(self.run_tool("--check", out).returncode, 0)

        # A store as import_original.py writes it, built from the folder above.
        store = self.dir / "store"
        (store / "blobs").mkdir(parents=True)

        def put(data):
            digest = hashlib.sha256(data).hexdigest()
            (store / "blobs" / digest).write_bytes(data)
            return digest
        discs, files = [], []
        for n, entries in self.discs.items():
            sysdir = out / f"disc{n}" / "sys"
            discs.append(dict(game_id="G4BE08", disc_number=n, system_blobs={
                k: put((sysdir / f).read_bytes()) for k, f in
                dict(boot="boot.bin", bi2="bi2.bin", apploader="apploader.img", dol="main.dol",
                     fst="fst.bin").items()}))
            for parts, content in entries:
                files.append(dict(path="/".join(parts), disc_number=n, size=len(content), sha256=put(content)))
        (store / "manifest.json").write_text(json.dumps(dict(discs=discs, files=files)))
        out2 = self.dir / "data2"
        r = self.run_tool("--store", store, "--out", out2)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        for p in out.rglob("*"):
            if p.is_file():
                self.assertEqual((out2 / p.relative_to(out)).read_bytes(), p.read_bytes(), p)

        # --check notices a damaged folder.
        (out2 / "disc0" / "files" / "etc" / "sizetbl.dat").write_bytes(b"x")
        self.assertEqual(self.run_tool("--check", out2).returncode, 1)

    def test_refusals(self):
        r = self.run_tool("--disc", self.images[0], "--out", ROOT / "port" / "build" / "disc-test")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("repository", r.stderr)
        raw, _ = image("GXXE01", 0, self.discs[0])
        other = self.dir / "other.iso"
        other.write_bytes(raw)
        r = self.run_tool("--disc", other, "--out", self.dir / "x")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("G4BE08", r.stderr)


class ReleaseTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.entries = {0: [(("etc", "core.das"), b"CORE" * 20)], 1: [(("st3", "r300.das"), b"DAS!" * 10)]}

    def tearDown(self):
        self.tmp.cleanup()

    def make(self, name, game_id, n, fst_off=0x236500):
        raw, _ = image(game_id, n, self.entries[n], fst_off)
        path = self.dir / name
        path.write_bytes(raw)
        return path

    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True)

    def test_european_release(self):
        eu = [self.make("eu1.iso", "G4BP08", 0), self.make("eu2.iso", "G4BP08", 1)]
        out = self.dir / "eu"
        r = self.run_tool("--disc", eu[0], "--disc", eu[1], "--out", out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("(retail-eu)", r.stdout)
        self.assertNotIn("build date", r.stderr)  # the date check is for the debug build only
        self.assertEqual(self.run_tool("--check", out, "--disc", eu[0], "--disc", eu[1]).returncode, 0)
        # the same folder checked against the debug images: laid out from other discs
        dbg = [self.make("dbg1.iso", "G4BE08", 0, 0x28FE00), self.make("dbg2.iso", "G4BE08", 1, 0x28FE00)]
        r = self.run_tool("--check", out, "--disc", dbg[0], "--disc", dbg[1])
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("other disc", r.stdout)

    def test_us_release(self):
        # the US release: G4BE08 like the debug build, the FST elsewhere (0x231A00)
        us = [self.make("us1.iso", "G4BE08", 0, 0x231A00), self.make("us2.iso", "G4BE08", 1, 0x231A00)]
        out = self.dir / "us"
        r = self.run_tool("--disc", us[0], "--disc", us[1], "--out", out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("(retail-us)", r.stdout)
        self.assertNotIn("build date", r.stderr)
        self.assertEqual(self.run_tool("--check", out, "--disc", us[0], "--disc", us[1]).returncode, 0)

    def test_mix_refused(self):
        dbg = self.make("dbg1.iso", "G4BE08", 0, 0x28FE00)
        eu = self.make("eu2.iso", "G4BP08", 1)
        r = self.run_tool("--disc", dbg, "--disc", eu, "--out", self.dir / "mix")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("different releases", r.stderr)
        us = self.make("us2.iso", "G4BE08", 1, 0x231A00)  # a debug disc next to a US one
        r = self.run_tool("--disc", dbg, "--disc", us, "--out", self.dir / "mix2")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("different releases", r.stderr)


if __name__ == "__main__":
    unittest.main()
