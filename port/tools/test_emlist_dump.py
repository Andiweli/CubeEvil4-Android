#!/usr/bin/env python3
"""Tests emlist_dump.py on a synthetic enemy list (no game data): read from two
disc images, from a Dolphin-style folder and from the .esl itself; room filter,
editor names, dynamite and thrower marks."""
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_whole_disc_folder import image  # noqa: E402

TOOL = Path(__file__).resolve().parent / "emlist_dump.py"


def entry(alive, eid, etype, eset, flag, hp, room, pos=(0, 0, 0), guard=0):
    e = bytearray(0x20)
    struct.pack_into(">BBBBIHBB", e, 0, alive, eid, etype, eset, flag, hp, 0, 0)
    struct.pack_into(">3h", e, 0x0C, *pos)
    struct.pack_into(">Hh", e, 0x18, room, guard)
    return bytes(e)


ESL = b"".join([
    entry(1, 0x13, 0, 0x00, 0x00032000, 60, 0x104, (100, -20, 300), 3),   # waist dynamite, unlimited
    entry(0, 0x10, 1, 0x00, 0x00000001, 50, 0x104),                       # plain Ganado, not alive
    entry(1, 0x10, 0, 0x24, 0x00020000, 50, 0x106),                       # fixed thrower
    entry(1, 0x10, 0, 0x00, 0x00020000, 50, 0x200),                       # other room
    bytes(0x20),                                                          # empty slot
])


class EmListDumpTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, data, *extra):
        out = subprocess.run([sys.executable, "-I", str(TOOL), "--data", str(data), *extra],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        return out.stdout

    def check_rooms(self, text):
        lines = [l for l in text.splitlines() if not l.startswith("#")]
        self.assertEqual(len(lines), 3, text)
        self.assertIn("00 r104 id 13 Ganado 1+w", lines[0])
        self.assertIn("Pickup waist, Weapon Mugen, Weapon Bomb", lines[0])
        self.assertIn("<< DYNAMITE (spare on the waist), unlimited", lines[0])
        self.assertIn("at (1000 -200 3000) guard 3000", lines[0])
        self.assertIn("alive 0", lines[1])
        self.assertNotIn("DYNAMITE", lines[1])
        self.assertIn("set 24 R106 Fix Bomber", lines[2])
        self.assertIn("<< DYNAMITE (in hand)", lines[2])
        self.assertIn("<< THROWER", lines[2])
        self.assertIn("# 3 entries in r104, r106, 2 with dynamite", text)

    def test_images(self):
        discs = self.dir / "discs"
        discs.mkdir()
        for no in (0, 1):
            img, _ = image("G4BE08", no, [(("etc", "emleon00.esl"), ESL), (("etc", "emleon01.esl"), bytes(0x20))])
            (discs / f"disc{no + 1}.iso").write_bytes(img)
        text = self.run_tool(discs, "--rooms", "104,106")
        self.check_rooms(text)
        self.assertIn("disc1.iso (G4BE08 disc 1)", text)
        self.assertIn("1 more identical copy", text)
        everything = self.run_tool(discs)
        self.assertIn("# 4 entries, 3 with dynamite", everything)

    def test_extracted_folder_and_file(self):
        esl = self.dir / "disc0" / "files" / "etc" / "emleon00.esl"
        esl.parent.mkdir(parents=True)
        esl.write_bytes(ESL)
        self.check_rooms(self.run_tool(self.dir, "--rooms", "104,106"))
        self.check_rooms(self.run_tool(esl, "--rooms", "104,106"))

    def test_missing(self):
        out = subprocess.run([sys.executable, "-I", str(TOOL), "--data", str(self.dir)],
                             capture_output=True, text=True)
        self.assertNotEqual(out.returncode, 0)
        self.assertIn("not found", out.stderr)


if __name__ == "__main__":
    unittest.main()
