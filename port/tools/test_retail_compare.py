#!/usr/bin/env python3
"""Tests retail_compare.py on two synthetic disc folders (no game data): files on one side only,
archive layouts same / appended / shifted / fewer, REL module ids kept or changed, and the release
names from the disc headers."""
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "retail_compare.py"


def body(tags, fill=b"\x11"):
    """A bare container body (ss/*.dat layout): count, 0, 0, 0, offsets, tags, 0x20-aligned entries."""
    n = len(tags)
    first = (16 + 8 * n + 31) & ~31
    offsets = [first + 0x20 * i for i in range(n)]
    out = bytearray(struct.pack(">4I", n, 0, 0, 0))
    out += struct.pack(f">{n}I", *offsets)
    out += b"".join(t.encode().ljust(4, b"\0") for t in tags)
    out += bytes(first - len(out))
    for _ in tags:
        out += fill * 0x20
    return bytes(out)


def rel(module_id):
    """A REL module header (version 3) with one section, as the core reads its id."""
    h = bytearray(0x80)
    struct.pack_into(">8I", h, 0, module_id, 0, 0, 1, 0x4C, 0, 0, 3)
    struct.pack_into(">3I", h, 0x24, 0x60, 0x70, 8)
    struct.pack_into(">2I", h, 0x4C, 0x58, 8)
    return bytes(h)


def boot(game_id, fst_offset):
    b = bytearray(0x440)
    b[:6] = game_id.encode()
    b[0x1C:0x20] = bytes.fromhex("c2339f3d")
    struct.pack_into(">I", b, 0x424, fst_offset)
    return bytes(b)


class RetailCompareTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def disc(self, name, header, files):
        base = self.dir / name / "disc0"
        (base / "sys").mkdir(parents=True)
        (base / "sys" / "boot.bin").write_bytes(header)
        for path, data in files.items():
            p = base / "files" / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        return self.dir / name

    def test_report(self):
        ref = self.disc("debug", boot("G4BE08", 0x28FE00), {
            "ss/Eng/same.dat": body(["BIN", "TPL"]),
            "ss/Eng/more.dat": body(["EFF", "UWF"]),
            "ss/Eng/term.dat": body(["BIN", "TPL", "BIN", "TPL", "FCV"]),
            "ss/Eng/less.dat": body(["BIN", "TPL", "FCV"]),
            "Rel/st1_0.rel": rel(0x4A),
            "Rel/st1_1.rel": rel(0x49),
            "Rel/Tools.rel": rel(0x01),
            "etc/moji8.tpl": b"font",
        })
        other = self.disc("eu", boot("G4BP08", 0x236500), {
            "ss/Eng/same.dat": body(["BIN", "TPL"], b"\x22"),
            "ss/Eng/more.dat": body(["EFF", "UWF", "UWF"]),
            "ss/Eng/term.dat": body(["BIN", "TPL", "BIN", "FCV"]),
            "ss/Eng/less.dat": body(["BIN", "TPL"]),
            "Rel/st1_0.rel": rel(0x4A) + b"\0" * 32,
            "Rel/st1_1.rel": rel(0x55),
            "ss/Ger/title.dat": body(["EFF"]),
        })
        out = self.dir / "report.txt"
        js = self.dir / "report.json"
        r = subprocess.run([sys.executable, str(TOOL), "--ref", ref, "--other", other, "--out", out,
                            "--json", js, "--jobs", "1"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        text = out.read_text(encoding="utf-8")
        self.assertIn("debug -> retail-eu", text)
        self.assertIn("2 only debug", text)
        self.assertIn("rel/tools.rel", text)
        self.assertIn("etc/moji8.tpl", text)
        rows = {row["path"]: row for row in json.loads(js.read_text())["discs"][0]["rows"]}
        self.assertEqual(rows["ss/eng/same.dat"]["archive"][0], "same")
        self.assertEqual(rows["ss/eng/more.dat"]["archive"][0], "appended")
        self.assertEqual(rows["ss/eng/term.dat"]["archive"][0], "shifted")
        self.assertIn("entry 3 (archive index 7): TPL -> FCV", rows["ss/eng/term.dat"]["archive"][1])
        self.assertEqual(rows["ss/eng/less.dat"]["archive"][0], "fewer")
        self.assertEqual(rows["rel/st1_0.rel"]["modules"], [[0x4A], [0x4A]])
        self.assertEqual(rows["rel/st1_1.rel"]["modules"], [[0x49], [0x55]])
        self.assertIn("rel/st1_1.rel: [73] -> [85]", text)


if __name__ == "__main__":
    unittest.main()
