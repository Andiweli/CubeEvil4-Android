#!/usr/bin/env python3
"""The message tables of two releases (port/docs/retail-eu-plan.md, R3): the debug build's against the
European ones. Read-only; prints numbers and the start of English texts (for the analysis, nothing kept).

  retail_messages.py tables DEBUG EU              every MDT entry whose English messages moved (inserted,
                                                  removed or a different count; difflib over the messages)
  retail_messages.py side DEBUG EU PATH ENTRY     one entry's English messages side by side, each with the
                                                  most similar message number of the other release
  retail_messages.py calls DEBUG EU               the room modules (Rel/st*.rel) whose SceMesSet calls ask for
                                                  other numbers: the r3 of each `li r6,0x64` call in order

DEBUG / EU: disc folders as port/tools/whole_disc_folder.py writes them (disc0/, disc1/ with files/).
Texts are shown with glyph g as chr(g - 0x80 + 0x20): lower case reads right, capitals are moved by six
(the font's order), "|" is a line break. Used for patches 0127 and 0128 (27.10.2026): r101 asks for the
wrong message in both releases, r221's European table moved one place from message 6 on; the other
tables only revised texts or added some at the end (memcard.das, ss_cmmn.dat, most rooms).
"""
import difflib
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "motion"))
sys.path.insert(0, str(ROOT / "tools"))
import archive  # noqa: E402


def files(root):
    out = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root).replace(os.sep, "/").lower()] = p
    return out


def messages(d):
    """The English messages of an MDT entry (block 1 of a six-language table, or a one-language table)."""
    if len(d) < 16:
        return None
    w0, w1 = struct.unpack_from(">2I", d, 0)
    if w0 == 6 and w1 == 0x1C:
        offs = list(struct.unpack_from(">6I", d, 4))
        o = offs[1]
        ends = sorted(set(offs + [len(d)]))
    elif 0 < w1 < 0x2000 and len(d) >= 8 + 4 * w1 and struct.unpack_from(">I", d, 8)[0] == 8 + 4 * w1:
        o, ends = 0, [len(d)]
    else:
        return None
    end = min(e for e in ends if e > o)
    if o + 8 > len(d):
        return []
    count = struct.unpack_from(">I", d, o + 4)[0]
    if o + 8 + 4 * count > end:
        return []
    mo = struct.unpack_from(f">{count}I", d, o + 8)
    out = []
    for i in range(count):
        a, b = o + mo[i], min(o + mo[i + 1] if i + 1 < count else end, end)
        out.append(d[a:b] if a < b else b"")
    return out


def text(m):
    codes = struct.unpack(f">{len(m) // 2}H", m[:len(m) // 2 * 2])
    return "".join(chr(c - 0x80 + 0x20) if c >= 0x80 else ("|" if c == 3 else "") for c in codes)


def entries(path):
    try:
        return archive.Archive(path)._load()
    except Exception:
        return []


def tables(a_root, b_root):
    fa, fb = files(a_root), files(b_root)
    for rel in sorted(fa):
        if rel not in fb or not rel.endswith((".das", ".dat", ".drs", ".evd")):
            continue
        ea, eb = entries(fa[rel]), entries(fb[rel])
        for i, (tag, d, _) in enumerate(ea):
            if tag != "MDT" or i >= len(eb) or eb[i][0] != "MDT":
                continue
            ma, mb = messages(d), messages(eb[i][1])
            if not ma or mb is None:
                continue
            ops = difflib.SequenceMatcher(None, ma, mb, autojunk=False).get_opcodes()
            moved = [o for o in ops if o[0] in ("insert", "delete") or (o[0] == "replace" and o[2] - o[1] != o[4] - o[3])]
            if moved or len(ma) != len(mb):
                desc = "; ".join(f"{op} debug {i1:#x}..{i2:#x} EU {j1:#x}..{j2:#x}" for op, i1, i2, j1, j2 in moved)
                print(f"{rel}#{i}: debug {len(ma)}, EU {len(mb)}: {desc}", flush=True)


def side(a_root, b_root, rel, entry):
    fa, fb = files(a_root), files(b_root)
    a = [text(m) for m in messages(entries(fa[rel.lower()])[entry][1])]
    b = [text(m) for m in messages(entries(fb[rel.lower()])[entry][1])]

    def best(s, other):
        r = [(difflib.SequenceMatcher(None, s, t, autojunk=False).ratio(), j) for j, t in enumerate(other)]
        return max(r) if r else (0, -1)

    print(f"debug {len(a)}, EU {len(b)}")
    for i in range(max(len(a), len(b))):
        left = right = ""
        if i < len(a):
            r, j = best(a[i], b)
            left = f"{a[i][:52]!r:<56} ~EU {j:#x} {r:.2f}"
        if i < len(b):
            r, j = best(b[i], a)
            right = f"{b[i][:52]!r} ~debug {j:#x} {r:.2f}"
        print(f"{i:#04x} {left:<80} | {right}")


def mes_calls(path):
    d = Path(path).read_bytes()
    nsec, sio = struct.unpack_from(">II", d, 0x0C)
    for i in range(nsec):
        off, size = struct.unpack_from(">II", d, sio + i * 8)
        if off & 1 and size:
            t = d[off & ~1:(off & ~1) + size]
            break
    else:
        return []
    words = struct.unpack(f">{len(t) // 4}I", t)
    out = []
    for k, w in enumerate(words):
        if w != 0x38C00064:  # li r6, 0x64: the message x of SceMesSet(no, se, ?, 0x64, y)
            continue
        r3 = None
        for j in range(k - 1, max(0, k - 10), -1):
            p = words[j]
            if p >> 26 == 18:
                break
            if p >> 26 == 14 and (p >> 16) & 31 == 0 and (p >> 21) & 31 == 3:
                r3 = p & 0xFFFF
                break
        out.append("?" if r3 is None else str(r3))
    return out


def calls(a_root, b_root):
    fa, fb = files(a_root), files(b_root)
    for rel in sorted(fa):
        if "/rel/st" not in rel or not rel.endswith(".rel") or rel not in fb:
            continue
        a, b = mes_calls(fa[rel]), mes_calls(fb[rel])
        if a != b:
            print(f"{rel}\n  debug: {' '.join(a)}\n  EU:    {' '.join(b)}")


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == "tables":
        tables(sys.argv[2], sys.argv[3])
    elif cmd == "side" and len(sys.argv) == 6:
        side(sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5]))
    elif cmd == "calls":
        calls(sys.argv[2], sys.argv[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
