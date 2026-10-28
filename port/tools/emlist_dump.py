#!/usr/bin/env python3
"""Print the room enemy lists (etc/*.esl, EM_LIST in include/global.h) of the disc.

Answers questions like "which enemies does the prototype place in r104, and does
any of them carry dynamite?" without a build: the list is read straight from the
disc data, the names come from the game's own list editor (src/t_emlist/t_emlist.cpp:
enemy names, flag bits, set names).

    emlist_dump.py --data DIR_OR_IMAGE [--list emleon00.esl] [--rooms 104,106,107]

--data takes a folder with the disc images (.iso/.gcm, read with the bounded
reader of original_import.gcm), a folder extracted with Dolphin (files/etc/...),
a single image or the .esl file itself. Nothing is written.
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from original_import.gcm import Disc  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EDITOR = ROOT / "src/t_emlist/t_emlist.cpp"
ENTRY = 0x20
BOMB, WAIST, MUGEN = 0x20000, 0x2000, 0x10000


def editor_names(path=EDITOR):
    """{enemy id: (name, flag names, set names)} from EmListIdTbl; empty when unreadable."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return {}
    arrays = {}
    for m in re.finditer(r"static const char\*\s*(\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
        arrays[m.group(1)] = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))

    def table(ref):
        m = re.fullmatch(r"\s*(\w+)\s*(?:\+\s*(\d+))?\s*", ref)
        if not m or m.group(1) == "NULL" or m.group(1) not in arrays:
            return []
        return arrays[m.group(1)][int(m.group(2) or 0):]

    m = re.search(r"EmListIdTbl\[\d+\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        return {}
    names = {}
    for i, row in enumerate(re.findall(r"\{([^{}]*)\}", m.group(1))):
        parts = [p.strip() for p in row.split(",")]
        if len(parts) < 4 or not parts[0].startswith('"'):
            continue
        names[i] = (parts[0].strip('"'), table(parts[1]), table(parts[3]))
    return names


def find_list(data: Path, name: str):
    """Yields (where, bytes) for every copy of etc/<name> in --data."""
    want = "etc/" + name.lower()
    if data.is_file():
        if data.name.lower() == name.lower():
            yield str(data), data.read_bytes()
            return
        candidates = [data]
    else:
        loose = [p for p in data.rglob("*") if p.is_file() and p.name.lower() == name.lower()]
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


def entries(raw: bytes):
    for i in range(len(raw) // ENTRY):
        e = raw[i * ENTRY:(i + 1) * ENTRY]
        be_flag, eid, etype, eset, flag, hp, _no, chr_ = struct.unpack_from(">BBBBIHBB", e, 0)
        pos = struct.unpack_from(">3h", e, 0x0C)
        room, guard = struct.unpack_from(">Hh", e, 0x18)
        yield dict(index=i, alive=be_flag & 1, be_flag=be_flag, id=eid, type=etype, set=eset, flag=flag,
                   hp=hp, chr=chr_, pos=tuple(p * 10 for p in pos), room=room, guard=guard * 1000)


def describe(e, names):
    name, flag_names, set_names = names.get(e["id"], (f"id {e['id']:02X}", [], []))
    bits = [flag_names[b] if b < len(flag_names) and flag_names[b] else f"bit{b}"
            for b in range(32) if e["flag"] >> b & 1]
    set_name = set_names[e["set"]] if e["set"] < len(set_names) else ""
    line = (f"{e['index']:02X} r{e['room']:03X} id {e['id']:02X} {name:<12} type {e['type']:02X} "
            f"set {e['set']:02X}{' ' + set_name if set_name and e['set'] else ''} hp {e['hp']} "
            f"alive {e['alive']} at ({e['pos'][0]} {e['pos'][1]} {e['pos'][2]}) guard {e['guard']} "
            f"flag {e['flag']:08X}{' [' + ', '.join(bits) + ']' if bits else ''}")
    if e["flag"] & BOMB:
        line += "  << DYNAMITE" + (" (spare on the waist)" if e["flag"] & WAIST else " (in hand)") + \
            (", unlimited" if e["flag"] & MUGEN else "")
    if "Bomber" in set_name:
        line += "  << THROWER"
    return line


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data", type=Path, required=True, help="disc images, extracted disc or the .esl file")
    ap.add_argument("--list", default="emleon00.esl", help="list file in etc/ (default emleon00.esl)")
    ap.add_argument("--rooms", default="", help="rooms in hex, comma separated (default: all)")
    args = ap.parse_args()
    if not args.data.exists():
        sys.exit(f"{args.data}: not found")
    rooms = {int(r, 16) for r in args.rooms.replace(" ", "").split(",") if r}
    copies = list(find_list(args.data, args.list))
    if not copies:
        sys.exit(f"etc/{args.list} not found in {args.data}")
    where, raw = copies[0]
    same = sum(1 for _, other in copies[1:] if other == raw)
    print(f"# etc/{args.list} from {where}: {len(raw)} bytes, {len(raw) // ENTRY} entries"
          + (f"; {same} more identical cop{'y' if same == 1 else 'ies'}" if same else "")
          + (f"; {len(copies) - 1 - same} DIFFERENT cop{'y' if len(copies) - 1 - same == 1 else 'ies'}"
             if len(copies) - 1 - same else ""))
    names = editor_names()
    shown = bombs = 0
    for e in entries(raw):
        if e["id"] == 0 or (rooms and e["room"] not in rooms):
            continue
        shown += 1
        bombs += bool(e["flag"] & BOMB)
        print(describe(e, names))
    print(f"# {shown} entries{' in r' + ', r'.join(f'{r:03X}' for r in sorted(rooms)) if rooms else ''}, "
          f"{bombs} with dynamite")
    return 0


if __name__ == "__main__":
    sys.exit(main())
