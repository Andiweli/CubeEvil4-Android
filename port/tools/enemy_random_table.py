#!/usr/bin/env python3
"""The enemy randomizer's protected list (0.99.12, port/docs/gegner-randomizer-plan.md): reads the room scripts
src/st1..st3/rXYZ.cpp and writes port/whole/runtime/enemy_random_rooms.inc.

Stage 2 (prototype, branch random-enemies, 16.11.2026, plan section 10): a room script names an enemy list entry
either to drive the live enemy ("driven": EmSetFromList2, setEm, setPtr, GetEmPtrFromList, GetEmIdFromList, an
Em_list[N] access other than its be_flag) or only to switch the entry on or off and keep its death ("lifecycle":
EmListSetAlive, EmSetDie*, Em_list[N].be_flag). Only driven entries are protected (their places stay); a lifecycle
entry may come at another place, the script never looks where it stands. A driven call whose entry is a variable
protects every number of the tables its argument names (a brace initializer in the file), or, when it names none
the tool knows, every number 0..255 of every brace initializer in the file (more than needed, never less).

For each room script:
  - kind 1 "untouched" when a boss is there (lifeMeterBoss, GamePointBossReset, PlRegistBoss) or the room is in
    KEEP below (with the reason);
  - kind 0 "open" when the script neither counts nor sweeps enemies, names no list entry directly and through no
    variable (stage 1): places and number may change (the number only from 10 Ganados on, runtime);
  - kind 2 "places only" otherwise: the places of the room's unprotected ordinary Ganados may change, the number
    never (the script may count them).
Rooms without a script file are not in the table (the runtime leaves them as they are). Prints a summary; --check
compares the include file with what it would write.

    port/tools/enemy_random_table.py [--check]
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "port", "whole", "runtime", "enemy_random_rooms.inc")
DRIVEN = ("EmSetFromList2", "GetEmPtrFromList", "GetEmIdFromList", "setEm", "setPtr")
COUNTS = r"SceCountEmAlive|SceDestroyEm|EmMgr\s*\.|getArrayNum|c_kill_cnt|[kK]ill[A-Z_]*[cC]nt|[dD]ead[A-Z_]*[cC]nt|emNum"
DIRECT = r"Em_list|Em_flg|EM_FLG_ROW|EM_DEAD_BIT"
BOSS = r"lifeMeterBoss|GamePointBossReset|PlRegistBoss"
# Rooms kept as they are although no boss is there (stage 2 prototype; the owner decides)
KEEP = {
    0x100: "the first encounter (em10 knows r100 and its entry 0 by name)",
}
NUMBER = re.compile(r"^\s*(0[xX][0-9a-fA-F]+|\d+)\s*$")
INT = re.compile(r"\b(0[xX][0-9a-fA-F]+|\d+)\b")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def first_argument(text, start):
    """The first argument of the call whose '(' is at text[start]."""
    depth, i = 0, start
    for i in range(start, len(text)):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
        elif c == "," and depth == 1:
            return text[start + 1:i]
    return text[start + 1:]


def argument_count(text, start):
    """The number of arguments of the call whose '(' is at text[start]."""
    depth, count = 0, 1
    for i in range(start, len(text)):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                return count
        elif c == "," and depth == 1:
            count += 1
    return count


def tables_of(text):
    """Every brace-initialised array of the file: name -> the numbers 0..255 in its initializer."""
    out = {}
    for m in re.finditer(r"\b(\w+)\s*(?:\[[^\]]*\]\s*)+=\s*\{", text):
        depth, i = 0, m.end() - 1
        for i in range(m.end() - 1, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
        nums = {int(v, 0) for v in INT.findall(text[m.end():i])}
        out.setdefault(m.group(1), set()).update(v for v in nums if 0 <= v < 256)
    return out


def numbers_of(expr, tables, every):
    """The list entries a non-number argument may name: the numbers of the tables it names, else all."""
    names = [w for w in re.findall(r"\b[A-Za-z_]\w*\b", expr) if w in tables]
    if not names:
        return set(every)
    out = set()
    for w in names:
        out |= tables[w]
    return out


def kind_of(why):
    """0 open, 1 untouched, 2 places only."""
    if "a boss" in why or any(w.startswith("kept:") for w in why):
        return 1
    return 2 if why else 0


def room_of(path):
    m = re.match(r"r([1-3])([0-9a-f]{2})\.cpp$", os.path.basename(path), re.I)
    return int(m.group(1), 16) << 8 | int(m.group(2), 16) if m else None


def scan(path, room):
    text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
    tables = tables_of(text)
    every = set()
    for v in tables.values():
        every |= v
    why = []
    if re.search(COUNTS, text):
        why.append("counts or sweeps enemies")
    if re.search(DIRECT, text):
        why.append("names list entries directly")
    if re.search(BOSS, text):
        why.append("a boss")
    if room in KEEP:
        why.append("kept: " + KEEP[room])
    protected = set()
    variable = False
    for name in DRIVEN:
        for m in re.finditer(r"\b%s\s*\(" % name, text):
            arg = first_argument(text, m.end() - 1)
            n = NUMBER.match(arg)
            if n:
                v = int(n.group(1), 0)
                if 0 <= v < 256:
                    protected.add(v)
            elif name == "setPtr" and argument_count(text, m.end() - 1) == 2:
                pass  # setPtr(cEm*, errOn) takes a live enemy, not a list entry
            else:
                protected |= numbers_of(arg, tables, every)
                variable = True
    # an Em_list entry read or written other than its be_flag: driven
    for m in re.finditer(r"Em_list\s*\[([^\]]*)\]\s*(\.\s*be_flag\b)?", text):
        if m.group(2):
            continue
        n = NUMBER.match(m.group(1))
        if n:
            v = int(n.group(1), 0)
            if 0 <= v < 256:
                protected.add(v)
        elif not re.match(r"^\s*i\s*$", m.group(1)) or "r327" not in path:
            # (r327's loop over every entry only clears the be_flag of the 0x1D ones: lifecycle)
            protected |= numbers_of(m.group(1), tables, every)
            variable = True
    if variable:
        why.append("variable list entries")
    return why, protected


def main():
    rows = []
    for stage in ("st1", "st2", "st3"):
        folder = os.path.join(ROOT, "src", stage)
        for name in sorted(os.listdir(folder)):
            room = room_of(name)
            if room is None:
                continue
            why, protected = scan(os.path.join(folder, name), room)
            rows.append((room, why, protected))
    lines = ["// Generated by port/tools/enemy_random_table.py from the room scripts (src/st1..st3/rXYZ.cpp): do not",
             "// edit by hand. One row a room: the room (stage << 8 | room), its kind (0 open: places and number may",
             "// change; 1 untouched: a boss, or kept by hand; 2 places only: the script counts, sweeps or names list",
             "// entries, the places of the others may change, the number never), the list entries the script drives",
             "// (bits, entry 0 the lowest bit of word 0): their places stay.",
             ""]
    for room, why, protected in rows:
        words = [0] * 8
        for v in protected:
            words[v >> 5] |= 1 << (v & 31)
        note = ", ".join(why)
        if protected:
            note += ("; " if note else "") + "%d driven" % len(protected)
        lines.append("{0x%03x, %d, {%s}},  // r%03x%s" % (room, kind_of(why),
                                                          ", ".join("0x%08xu" % w for w in words), room,
                                                          ": " + note if note else ""))
    out = "\n".join(lines) + "\n"
    if "--check" in sys.argv:
        old = open(OUT, encoding="utf-8").read().replace("\r\n", "\n") if os.path.exists(OUT) else ""
        print("enemy_random_rooms.inc: %s" % ("up to date" if old == out else "OUT OF DATE"))
        return 0 if old == out else 1
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    untouched = [r for r in rows if kind_of(r[1]) == 1]
    places = [r for r in rows if kind_of(r[1]) == 2]
    print("%d room scripts: %d untouched, %d places only, %d open (%d of them drive entries by number)" % (
        len(rows), len(untouched), len(places), len(rows) - len(untouched) - len(places),
        sum(1 for r in rows if not r[1] and r[2])))
    print("untouched: " + " ".join("r%03x" % r[0] for r in untouched))
    print("places only: " + " ".join("r%03x" % r[0] for r in places))
    print("open: " + " ".join("r%03x" % r[0] for r in rows if not r[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
