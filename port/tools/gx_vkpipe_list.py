#!/usr/bin/env python3
"""The app's list of Vulkan pipelines (asset gx-vkpipes.keys, 0.99.5): the pipelines the rooms need, so the Vulkan
renderer makes them ahead on a thread of its own (gx_vk.cpp StartAhead / AheadThread) and a room's first visit does
not stutter while they are made (20-54 ms each on the Retroid's Adreno 650, the project owner's report 08.11.2026).

Sources:
  - a room's pipeline list (room-NNN.vkpipes, gx_vk.cpp SaveRoomPipes): "RE4VKP01", the size of a key, the count,
    the records (the program's key, layout, stride, 16 bytes of state); the room from the file's name
  - folders holding such lists (the PC's room tours: gx_chunk_render with RE4_GX_API=vulkan, RE4_GX_PROGRAM_CACHE and
    RE4_GX_ROOM; a device's shader cache folder)
  - error reports (.zip): logs/vk-pipes.bin, also under previous/ (CoreActivity / ShaderCache.writeRoomPipes since
    0.99.5: the device's room lists in the app list's form)
  - app lists (gx-vkpipes.keys): "RE4VKL01", the size of a key, the pipelines, the rooms; the records; per room its
    number, the count and the records' indices (little-endian 32 bit)

Each pipeline is kept once, its key cleaned as the renderer cleans it (gx_gl_shader.cpp CanonicalShaderKey: a
stage's KONST selection when none of its inputs takes KONST, the swap tables no stage selects, the fog's projection
without fog set to 0; the last field's MSAA bit 0 set to 0: the app makes them for its own setting; keys of the PSone
demake, bit 1, are left out), in the order of the rooms (r100 first), each room with the indices of all its pipelines.
Keys of another size (another renderer version) are left out.

  gx_vkpipe_list.py make OUT.keys SOURCE...   (OUT.keys may be among the sources: it grows)
  gx_vkpipe_list.py add LIST.keys SOURCE...   players' reports (.zip or unpacked folders) into the app's list, with
                                              how many pipelines each room gains
  gx_vkpipe_list.py info FILE...
"""
import os
import re
import struct
import sys
import zipfile

ROOM_MAGIC = b"RE4VKP01"
LIST_MAGIC = b"RE4VKL01"
STATE_BYTES = 8 + 16  # layout, stride, then PipeKey's bytes from `merged` on


# word offsets of ShaderKey's fields (gx_gl_shader.h; the key's size checks the layout: 116 words)
_KEY_FIELDS = [("layout", 1), ("numChans", 1), ("numTexGens", 1), ("chan", 4), ("texGen", 8), ("postGen", 8),
               ("dualTex", 1), ("orthographic", 1), ("numStages", 1), ("numIndStages", 1), ("colorEnv", 16),
               ("alphaEnv", 16), ("order", 16), ("konst", 16), ("swap", 4), ("indCmd", 16), ("indRef", 1),
               ("alphaTest", 1), ("fog", 1), ("texMaps", 1), ("centroid", 1)]
AT = {}
_at = 0
for _name, _n in _KEY_FIELDS:
    AT[_name] = _at
    _at += _n
KEY_WORDS = _at


def canonical(words):
    """gx_gl_shader.cpp CanonicalShaderKey on the key's words (a list, changed in place)."""
    stages = words[AT["numStages"]]
    swaps = 0
    for s in range(16):
        if s >= stages:
            words[AT["konst"] + s] = 0
            continue
        cc, ac = words[AT["colorEnv"] + s], words[AT["alphaEnv"] + s]
        colour = any(((cc >> b) & 15) == 14 for b in (12, 8, 4, 0))
        alpha = any(((ac >> b) & 7) == 6 for b in (13, 10, 7, 4))
        k = words[AT["konst"] + s]
        words[AT["konst"] + s] = (k & 0xFF if colour else 0) | (k & 0xFF00 if alpha else 0)
        swaps |= 1 << (ac & 3) | 1 << ((ac >> 2) & 3)
    for t in range(4):
        if not swaps & (1 << t):
            words[AT["swap"] + t] = 0
    if ((words[AT["fog"]] >> 1) & 7) == 0:
        words[AT["fog"]] = 0


def normal(record, key_size):
    """The record with its key cleaned (canonical) and the MSAA bit (bit 0 of the last field) cleared; None for the
    PSone demake's keys."""
    words = list(struct.unpack_from("<%dI" % (key_size // 4), record, 0))
    if words[-1] & 2:
        return None
    words[-1] &= ~1
    if len(words) == KEY_WORDS:
        canonical(words)
    return struct.pack("<%dI" % len(words), *words) + record[key_size:]


class Lists:
    def __init__(self):
        self.key_size = 0
        self.records = {}   # record bytes -> index (first seen)
        self.order = []     # record bytes in index order
        self.rooms = {}     # room -> list of record bytes (in its order, once each)
        self.sources = 0

    def size_fits(self, size, name):
        if self.key_size and size != self.key_size:
            print(f"  {name}: keys of {size} bytes, not {self.key_size}: left out")
            return False
        self.key_size = size
        return True

    def add(self, room, record):
        rec = normal(record, self.key_size)
        if rec is None:
            return
        room_list = self.rooms.setdefault(room, [])
        if rec not in self.records:
            self.records[rec] = None
        if rec not in room_list:
            room_list.append(rec)

    def room_file(self, data, name, room):
        if data[:8] != ROOM_MAGIC or len(data) < 16:
            print(f"  {name}: not a room's pipeline list")
            return
        size, count = struct.unpack_from("<II", data, 8)
        if not self.size_fits(size, name):
            return
        rb = size + STATE_BYTES
        if len(data) < 16 + count * rb:
            print(f"  {name}: cut short, left out")
            return
        for i in range(count):
            self.add(room, data[16 + i * rb:16 + (i + 1) * rb])
        self.sources += 1

    def app_list(self, data, name):
        size, pipes, rooms = struct.unpack_from("<III", data, 8)
        if not self.size_fits(size, name):
            return
        rb = size + STATE_BYTES
        recs = [data[20 + i * rb:20 + (i + 1) * rb] for i in range(pipes)]
        at = 20 + pipes * rb
        for _ in range(rooms):
            room, n = struct.unpack_from("<II", data, at)
            at += 8
            for j in range(n):
                idx = struct.unpack_from("<I", data, at + 4 * j)[0]
                if idx < pipes:
                    self.add(room, recs[idx])
            at += 4 * n
        self.sources += 1

    def bytes_of(self, data, name):
        base = os.path.basename(name)
        m = re.match(r"room-([0-9a-f]+)\.vkpipes$", base)
        if data[:8] == LIST_MAGIC:
            self.app_list(data, name)
        elif m:
            self.room_file(data, name, int(m.group(1), 16))
        else:
            print(f"  {name}: neither a room's list nor the app's list")

    def source(self, path):
        if os.path.isdir(path):
            for root, _dirs, files in os.walk(path):
                for f in sorted(files):
                    if f.endswith(".vkpipes") or f.endswith(".zip") or f in ("gx-vkpipes.keys", "vk-pipes.bin"):
                        self.source(os.path.join(root, f))
        elif path.endswith(".zip"):
            with zipfile.ZipFile(path) as z:
                for info in z.infolist():
                    if re.search(r"(^|/)(room-[0-9a-f]+\.vkpipes|vk-pipes\.bin)$", info.filename):
                        self.bytes_of(z.read(info), f"{path}:{info.filename}")
        else:
            with open(path, "rb") as f:
                self.bytes_of(f.read(), path)

    def write(self, out):
        # the pipelines in the order of the rooms (each room's new ones in its order)
        order, index = [], {}
        for room in sorted(self.rooms):
            for rec in self.rooms[room]:
                if rec not in index:
                    index[rec] = len(order)
                    order.append(rec)
        body = [LIST_MAGIC, struct.pack("<III", self.key_size, len(order), len(self.rooms))]
        body += order
        for room in sorted(self.rooms):
            ids = [index[r] for r in self.rooms[room]]
            body.append(struct.pack("<II", room, len(ids)) + struct.pack("<%dI" % len(ids), *ids))
        data = b"".join(body)
        with open(out + ".tmp", "wb") as f:
            f.write(data)
        os.replace(out + ".tmp", out)
        return len(order), len(data)


def info(path):
    lists = Lists()
    lists.source(path)
    merged = sum(1 for r in lists.records if r[lists.key_size + 8] if lists.key_size)
    print(f"{path}: {len(lists.records)} pipelines ({merged} merged draws), {len(lists.rooms)} rooms, keys of "
          f"{lists.key_size} bytes")
    for room in sorted(lists.rooms):
        print(f"  r{room:03x}: {len(lists.rooms[room])}")


def main(argv):
    if len(argv) >= 3 and argv[1] == "make":
        out, sources = argv[2], argv[3:]
        lists = Lists()
        for s in sources:
            lists.source(s)
        if not lists.records:
            print("no pipelines found")
            return 1
        count, size = lists.write(out)
        print(f"{out}: {count} pipelines of {len(lists.rooms)} rooms from {lists.sources} lists, {size} bytes")
        return 0
    if len(argv) >= 4 and argv[1] == "add":
        out, sources = argv[2], argv[3:]
        lists = Lists()
        lists.source(out)
        before = {room: len(v) for room, v in lists.rooms.items()}
        count_before = len(lists.records)
        for s in sources:
            lists.source(s)
        count, size = lists.write(out)
        print(f"{out}: {count} pipelines (was {count_before}, +{count - count_before}), {size} bytes")
        for room in sorted(lists.rooms):
            gained = len(lists.rooms[room]) - before.get(room, 0)
            if gained:
                print(f"  r{room:03x}: +{gained} (now {len(lists.rooms[room])})")
        return 0
    if len(argv) >= 3 and argv[1] == "info":
        for p in argv[2:]:
            info(p)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
