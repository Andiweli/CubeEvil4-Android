#!/usr/bin/env python3
# realtime-shadows stage 4: the census summed up. Per room (its own lines only): the ObjMgr works by construct id,
# kind, type, motion and the flags 0x10 (the game's shadow caster) / 0x20 (moving); the EmMgr props by name; the other
# draw functions. Then the totals over all rooms.
#   census_sum.py DIR [ROOM...]
import collections
import glob
import os
import re
import sys

d = sys.argv[1]
want = set(sys.argv[2:])
line_re = re.compile(r"census: r(\w+) t(\d+) (\w+) (.*)$")
total = collections.Counter()
rooms_of = collections.defaultdict(set)
for path in sorted(glob.glob(os.path.join(d, "r*.txt"))):
    room = os.path.basename(path)[1:-4]
    per = collections.Counter()
    for l in open(path, errors="replace"):
        m = line_re.search(l)
        if not m or m.group(1) != room:
            continue
        what = m.group(4)
        t = int(m.group(2))
        if what.startswith("EM id"):
            em = re.match(r"EM id 0x(\w+) (.*?) be 0x(\w+) motion (\d)", what)
            i = int(em.group(1), 16)
            if i < 0x40:
                continue
            be = int(em.group(3), 16)
            key = "EM %s (0x%02x) t%d motion %s caster0x10 %d" % (em.group(2), i, t, em.group(4), (be >> 4) & 1)
        elif what.startswith("OBJ"):
            ob = re.match(r"OBJ id (\d+) kind (\d+) type (\d+) be 0x(\w+) motion (\d) ot (\d+)", what)
            be = int(ob.group(4), 16)
            key = "OBJ id %s kind %s type %s motion %s caster0x10 %d moving0x20 %d t%d" % (
                ob.group(1), ob.group(2), ob.group(3), ob.group(5), (be >> 4) & 1, (be >> 5) & 1, t)
        else:
            f = what.replace("OTHER func ", "")
            if "IdGeneral" in f or "Esp" in f or "esp" in f:
                continue
            key = "OTHER %s t%d" % (f, t)
        per[key] += 1
        total[key] += 1
        rooms_of[key].add(room)
    if not want or room in want:
        print("== r%s" % room)
        for k, n in sorted(per.items()):
            print("  %3d  %s" % (n, k))
if not want:
    print("== totals over all rooms (count, rooms)")
    for k, n in sorted(total.items(), key=lambda kv: -kv[1]):
        rs = sorted(rooms_of[k])
        print("  %5d  %-90s %s" % (n, k, " ".join(rs[:8]) + (" ..." if len(rs) > 8 else "")))
