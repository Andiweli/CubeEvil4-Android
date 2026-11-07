#!/usr/bin/env python3
"""The report of the room tour (whole_r100_check.sh --rooms): reads the log of every room and
sorts what went wrong into one report (bug hunt, part 2: port/docs/whole-program-status.md).

Per room the tour leaves in its folder:

  r<room>.log    the game's log (stderr of re4_host)
  r<room>.meta   key=value lines of the tour: code (exit code), reason (why the tour stopped
                 the run: a stand-still, the time limit; empty when the game ended itself), secs,
                 state (the last frame of the state trace: frame, game step, room)

Errors (the room counts as failed, exit code 1):

  crash        a fatal signal (with library, offset and function), OSPanic, AddressSanitizer
  stand-still  no new frame for a while (an endless loop inside a frame, a deadlock), with the
               backtrace of the running context
  no start     the room's main loop never came (RE4_STOP_AT_FRAME ended the run)
  not a number Leon or an enemy at NaN (state_trace.cpp), an angle that is not finite
               (LIMIT_ANGLE, patch 0020: the GameCube would hang there)
  death loop   Leon dies again and again right after the room (re)starts (r21d, 22.10.2026)

Findings that do not fail the room: the port's checks ("check: ..."), UndefinedBehaviorSanitizer
("runtime error: ...", each place once per run), deaths, the rooms Leon walked into, game heaps
without room, called stubs, the game's own messages (pLog), converter messages, the acosf/asinf
guard (ARC_CALL). Each is listed once with the rooms it came from.

    port/tools/room_tour_report.py DIR --rooms "100 101 ..." [--details FILE] [--json FILE]
        [--title TEXT]
    port/tools/room_tour_report.py --compare A.json B.json [--names i686,aarch64] [--messages]
    port/tools/room_tour_report.py --merge NEW.json KEEP.json

--compare lists the rooms of B whose result differs from A (say i686 and aarch64); with --messages
also the game's messages (pLog) that only one of the two gave, with their rooms (the debug and the
European discs). --merge puts the rooms of a tour into the kept results of its build, replacing only
those rooms.
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEATH_LOOP_FRAMES = 120  # a death this soon after the room (re)started
# converter messages about data left undone (not the bookkeeping of overlapping archives, "its
# notes are dropped", nor blocks left as they are on purpose: they come in every room)
CONVERTER = re.compile(r"no converter|incomplete|outside|no field list|blocks in its chain|more than|suspended|"
                       r"back again|conversion stopped")


def demangle(names):
    names = list(names)
    if not names:
        return []
    try:
        out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout
        lines = out.splitlines()
        if len(lines) == len(names):
            return lines
    except OSError:
        pass
    return names


def plain(line):
    return line[6:] if line.startswith("[re4] ") else line


def numbers_out(text):
    """A message without its numbers and addresses: the same finding from several rooms."""
    return re.sub(r"0x[0-9a-fA-F]+|\b[0-9a-fA-F]{6,16}\b|-?\d+(\.\d+)?", "#", text)


def frame_name(line):
    """'/x/libre4_main.so(_Z9SeAtCheckv+0x12)[0x8...]' -> 'libre4_main.so SeAtCheck()+0x12'."""
    m = re.match(r"\s*(\S*?)\(([^)+]*)(\+0x[0-9a-f]+)?\)\s*\[(0x[0-9a-f]+)\]", line)
    if not m:
        return line.strip()
    lib = m.group(1).rsplit("/", 1)[-1]
    sym = demangle([m.group(2)])[0] if m.group(2) else "?"
    return f"{lib} {sym}{m.group(3) or ''}"


def handler_cut(chain_lines):
    """The frames after the signal handler: backtrace() starts in the handler (libre4_host.so),
    then comes the kernel's return frame ([0x...], linux-gate, __restore_rt), then the code that was
    running. Without such a frame, the leading frames of libre4_host.so are left out."""
    for i, l in enumerate(chain_lines):
        if re.match(r"\s*\[0x[0-9a-f]+\]$", l) or "linux-gate" in l or "__kernel_" in l or "__restore_rt" in l:
            return chain_lines[i + 1:]
    i = 0
    while i < len(chain_lines) and "libre4_host.so" in chain_lines[i]:
        i += 1
    return chain_lines[i:]


def backtrace_after(lines, start):
    """The backtrace_symbols lines from lines[start] on, up to the first other line."""
    out = []
    for l in lines[start:]:
        if not re.search(r"\[0x[0-9a-f]+\]$", l.strip()):
            break
        out.append(l)
    return [frame_name(l) for l in handler_cut(out)]


def read_meta(path):
    meta = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                meta[k.strip()] = v.strip()
    return meta


def analyse(room, log_path, meta):
    """One room: its result, its errors and its findings."""
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    lines = [plain(l) for l in text.splitlines() if "pad: frame" not in l]
    r = {"room": room, "errors": [], "notes": {}, "secs": int(meta.get("secs", "0") or 0),
         "code": int(meta.get("code", "0") or 0), "reason": meta.get("reason", ""), "state": meta.get("state", "")}

    def note(kind, item):
        r["notes"].setdefault(kind, []).append(item)

    main = next((l for l in lines if l.startswith("run: main loop reached")), "")
    m = re.search(r"at frame (\d+)", main)
    r["main"] = int(m.group(1)) if m else None
    stopped = next((l for l in lines if l.startswith("run: stopped after") or l.startswith("run: stopped at frame")), "")
    r["stopped"] = stopped
    free = next((l for l in lines if l.startswith("run: player free")), "")
    m = re.search(r"at frame (\d+), (\d+) after", free)
    r["free"] = int(m.group(2)) if m else None
    rooms = []
    for l in lines:
        m = re.match(r"room: r([0-9a-f]{3}) runs \(frame (\d+)\)", l)
        if m:
            rooms.append((m.group(1), int(m.group(2)), "continue after a death" in l))
    r["visited"] = sorted({x[0] for x in rooms if x[0] != room})
    m = re.search(r"room r([0-9a-f]{3})", stopped)
    r["end_room"] = m.group(1) if m else None

    # crashes: the fatal signal, where it was (pc), the backtrace
    sig = next((i for i, l in enumerate(lines) if l.startswith("fatal signal")), None)
    if sig is not None:
        where = ""
        for l in lines[sig + 1:sig + 4]:
            m = re.match(r"\s*pc (0x[0-9a-f]+) = (\S+)\+(0x[0-9a-f]+) \((.*)\)", l)
            if m:
                where = f"{m.group(2).rsplit('/', 1)[-1]}+{m.group(3)} ({demangle([m.group(4)])[0]})"
        first = next((i for i in range(sig + 1, len(lines)) if re.search(r"\[0x[0-9a-f]+\]$", lines[i].strip())), None)
        chain = backtrace_after(lines, first) if first is not None else []
        r["errors"].append(("Absturz", f"{lines[sig].split(', backtrace')[0]}; {where or '(Stelle unbekannt)'}",
                            [f"  bei {r['state'] or '?'}"] + ["  " + c for c in chain[:12]]))
    for l in lines:
        if l.startswith("OSPanic"):
            r["errors"].append(("Absturz", l, []))
    # AddressSanitizer: kind, the access, the first frames of the game, SUMMARY
    for i, l in enumerate(lines):
        if "ERROR: AddressSanitizer" in l:
            body = []
            summary = ""
            for x in lines[i + 1:i + 80]:
                if x.startswith("SUMMARY: AddressSanitizer"):
                    summary = x.replace("SUMMARY: AddressSanitizer: ", "")
                    break
                if re.match(r"\s*#\d+ ", x) or re.match(r"(READ|WRITE) of size", x) or "is located" in x:
                    body.append("  " + x.strip())
            r["errors"].append(("AddressSanitizer", summary or l.split("ERROR: ", 1)[1], body[:24]))
            break
    # stand-still: the reason from the tour and the running context
    if r["reason"]:
        kind = "Stillstand" if r["reason"].startswith("STILLSTAND") else "Abbruch"
        chain = []
        at = next((i for i, l in enumerate(lines) if "backtrace of the running context" in l), None)
        if at is not None:
            chain = backtrace_after(lines, at + 1)
        waits = [l.strip() for l in lines[at + 1:] if l.startswith("OS thread ")][:8] if at is not None else []
        r["errors"].append((kind, f"{r['reason']} bei {r['state'] or '?'}",
                            ["  " + c for c in chain[:12]] + ["  " + w for w in waits]))
    # what the game and the converters said before a crash or a stand-still (its report ends the log)
    cut = next((i for i, l in enumerate(lines) if l.startswith("fatal signal") or "ERROR: AddressSanitizer" in l or
                "backtrace of the running context" in l or l.startswith("OSPanic")), None)
    if r["errors"] and cut is not None:
        body = r["errors"][0][2]
        body.append("  letzte Zeilen davor:")
        body.extend("    " + l for l in lines[max(0, cut - 30):cut])
        stops = sorted({l for l in lines if "conversion stopped" in l})
        if stops:
            body.append("  Umwandlungen, die abbrachen:")
            body.extend("    " + l for l in stops[:20])
    if not r["errors"] and not r["reason"] and r["main"] is None:
        r["errors"].append(("Kein Raumstart", f"Hauptschleife nie erreicht ({stopped or 'kein Stopp'}; {r['state']})", []))
    elif not r["errors"] and r["code"] != 0:
        r["errors"].append(("Abbruch", f"Exit-Code {r['code']} ohne Absturzmeldung bei {r['state']}", []))
    elif not r["errors"] and not stopped:
        r["errors"].append(("Abbruch", f"kein regulärer Stopp (Exit-Code {r['code']}) bei {r['state']}", []))
    # not a number
    nan = [l for l in lines if "is not a number" in l or "is not finite" in l]
    if nan:
        r["errors"].append(("Keine Zahl (NaN)", f"{len(nan)} Zeilen, zuerst: {nan[0]}", ["  " + l for l in nan[:6]]))
    # deaths and death loops
    deaths = []
    for l in lines:
        m = re.match(r"die: r([0-9a-f]{3}) after (\d+) frames", l)
        if m:
            deaths.append((m.group(1), int(m.group(2)), l))
    r["deaths"] = len(deaths)
    quick = [d for d in deaths if d[1] <= DEATH_LOOP_FRAMES]
    if len(quick) >= 2:
        r["errors"].append(("Todesschleife", f"{len(quick)} Tode höchstens {DEATH_LOOP_FRAMES} Bilder nach dem "
                            f"Raumstart (von {len(deaths)})",
                            ["  " + d[2] for d in quick[:4]] +
                            ["  " + l for l in lines if l.startswith("die: area")][:6]))
    for d in deaths:
        note("Tode", (f"r{d[0]} nach {d[1]} Bildern", d[2]))
    # findings
    for l in lines:
        if l.startswith("check: "):
            note("check", (numbers_out(l), l))
        elif "runtime error:" in l:
            m = re.match(r"(\S+?):(\d+):(\d+): runtime error: (.*)", l)
            key = f"{m.group(1)}:{m.group(2)}: {numbers_out(m.group(4))}" if m else numbers_out(l)
            note("ubsan", (key, l))
        elif l.startswith("heap: no room") or l.startswith("heap: heap "):
            note("heap", (numbers_out(l), l))
        elif l.startswith("stub called: "):
            note("stub", (l[13:], l))
        elif l.startswith("game: "):
            note("game", (numbers_out(re.sub(r" \(\d+ times\)$", "", l)), l))
        elif l.startswith("formats: ") and CONVERTER.search(l):
            note("formats", (numbers_out(l), l))
        elif l.startswith("math: acosf/asinf"):
            note("math", ("acosf/asinf", l))
        elif l.startswith("LIMIT_ANGLE"):
            pass  # counted with the NaN lines
        elif re.match(r"em: s\d+ jumps", l):
            note("jumps", (numbers_out(l), l))
    math = [int(m.group(1)) for _k, l in r["notes"].get("math", []) for m in [re.search(r"\((\d+) times so far", l)] if m]
    r["math"] = max(math) if math else 0
    r["ok"] = not r["errors"]
    return r


def room_line(r):
    if r["errors"]:
        kind, text, _ = r["errors"][0]
        more = f" (+{len(r['errors']) - 1} weitere)" if len(r["errors"]) > 1 else ""
        return f"{kind.upper()}: {text}{more}"
    parts = [f"läuft (Raum ab Bild {r['main']}"]
    parts.append(f"Leon frei nach {r['free']} Bildern" if r["free"] is not None else "Leon nie frei (Ereignis/Untermenü)")
    parts[-1] += f", {r['secs']} s)"
    if r["deaths"]:
        parts.append(f"{r['deaths']}x gestorben")
    if r["visited"]:
        parts.append("weiter nach " + ", ".join("r" + v for v in r["visited"]))
    for kind, label in (("check", "check:"), ("ubsan", "UBSan"), ("heap", "Heap voll")):
        if r["notes"].get(kind):
            parts.append(f"{len(r['notes'][kind])} {label}")
    return "; ".join(parts)


def grouped(results, kind):
    """kind -> [(key, example line, rooms)] in the order of the most rooms."""
    seen = {}
    for r in results:
        for key, line in r["notes"].get(kind, []):
            e = seen.setdefault(key, [line, []])
            if r["room"] not in e[1]:
                e[1].append(r["room"])
    return sorted(((k, v[0], v[1]) for k, v in seen.items()), key=lambda x: (-len(x[2]), x[0]))


def rooms_text(rooms, limit=12):
    text = " ".join("r" + x for x in rooms[:limit])
    return text + (f" … (+{len(rooms) - limit})" if len(rooms) > limit else "")


def report(results, out, details, title):
    bad = [r for r in results if not r["ok"]]
    say = lambda s="": out.append(s)  # noqa: E731
    if title:
        say(title)
    for r in results:
        say(f"  r{r['room']}: {room_line(r)}")
    say(f"Ergebnis: {len(results) - len(bad)} von {len(results)} Räumen ohne Fehler" +
        (f"; Fehler: {' '.join('r' + r['room'] + ' (' + r['errors'][0][0] + ')' for r in bad)}" if bad else ""))
    kinds = {}
    for r in bad:
        for kind, text, body in r["errors"]:
            kinds.setdefault(kind, []).append((r, text, body))
    for kind in ("Absturz", "AddressSanitizer", "Stillstand", "Abbruch", "Kein Raumstart", "Keine Zahl (NaN)",
                 "Todesschleife"):
        if kind in kinds:
            say(f"{kind} ({len(kinds[kind])} Räume):")
            for r, text, body in kinds[kind]:
                say(f"  r{r['room']}: {text}")
                details.append(f"== r{r['room']}: {kind}: {text}")
                details.extend(body)
                details.append("")
    sections = (
        ("check", "Hinweise der Port-Prüfungen (check:)", 30),
        ("ubsan", "UndefinedBehaviorSanitizer (je Stelle einmal je Lauf)", 60),
        ("heap", "Spiel-Heap ohne Platz", 20),
        ("formats", "Umwandler-Meldungen", 20),
        ("stub", "Aufgerufene Stubs", 30),
        ("game", "Meldungen des Spiels (pLog)", 30),
        ("jumps", "Gegner springen (mehr als 3000 in einem Bild)", 10),
    )
    for kind, label, limit in sections:
        g = grouped(results, kind)
        if not g:
            continue
        say(f"{label}: {len(g)} verschiedene")
        for key, line, rooms in g[:limit]:
            say(f"  [{len(rooms)} Räume: {rooms_text(rooms)}] {line[:300]}")
        if len(g) > limit:
            say(f"  … {len(g) - limit} weitere (rundgang-einzelheiten.txt)")
        details.append(f"== {label}, alle:")
        details.extend(f"  [{rooms_text(rooms, 1000)}] {line}" for key, line, rooms in g)
        details.append("")
    died = [r for r in results if r["deaths"]]
    if died:
        say("Tode (Leon gestorben): " + " ".join(f"r{r['room']}×{r['deaths']}" for r in died))
    moved = [r for r in results if r["visited"]]
    if moved:
        say("Raumwechsel (durch eine Tür oder ein Ereignis): " +
            ", ".join(f"r{r['room']} → {' '.join('r' + v for v in r['visited'])}" for r in moved))
    never = [r for r in results if r["ok"] and r["free"] is None]
    if never:
        say(f"Leon nie frei (Eingaben liefen nicht; Ereignis, Film oder Untermenü bis zum Ende): "
            f"{rooms_text([r['room'] for r in never], 60)}")
    math = [r for r in results if r["math"]]
    if math:
        say("acosf/asinf-Schutz griff (ARC_CALL, Insekten-Wandschritt u. a.): " +
            " ".join(f"r{r['room']}×{r['math']}" for r in math))
    return 1 if bad else 0


def compare(a_path, b_path, names, messages=False):
    a = {r["room"]: r for r in json.loads(Path(a_path).read_text())}
    b = {r["room"]: r for r in json.loads(Path(b_path).read_text())}
    na, nb = names
    diff = 0
    missing = [room for room in sorted(b) if room not in a]
    print(f"Vergleich der Rundgänge {na} / {nb} ({len(b) - len(missing)} Räume):")
    if missing:
        print(f"  ohne {na}-Ergebnis: {rooms_text(missing, 60)}")
    for room in sorted(b):
        x, y = a.get(room), b[room]
        if not x:
            continue
        kx = [e[0] for e in x["errors"]]
        ky = [e[0] for e in y["errors"]]
        cx = {k for k, _ in x["notes"].get("check", [])}
        cy = {k for k, _ in y["notes"].get("check", [])}
        if kx != ky or cx != cy or x["deaths"] != y["deaths"] or x["visited"] != y["visited"]:
            diff += 1
            print(f"  r{room}: {na}: {room_line(x)}")
            print(f"  {' ' * len(room)}  {nb}: {room_line(y)}")
    print(f"  {diff} Räume verschieden" if diff else "  alle Räume gleich beurteilt")
    if messages:
        # only the rooms both tours ran
        both = [room for room in sorted(b) if room in a]
        ga = {k: (line, rooms) for k, line, rooms in grouped([a[room] for room in both], "game")}
        gb = {k: (line, rooms) for k, line, rooms in grouped([b[room] for room in both], "game")}
        for x, y, name in ((gb, ga, nb), (ga, gb, na)):
            rows = []
            for k, (line, rooms) in x.items():
                other = set(y.get(k, ("", []))[1])
                only = [room for room in rooms if room not in other]
                if only:
                    rows.append((only, line))
            rows.sort(key=lambda row: (-len(row[0]), row[1]))
            print(f"  Meldungen des Spiels nur mit {name}: {len(rows)} verschiedene" + (":" if rows else ""))
            for only, line in rows[:40]:
                print(f"    [{len(only)} Räume: {rooms_text(only)}] {line[:300]}")
    return 1 if diff else 0


def merge(new_path, keep_path):
    """The rooms of a tour into the kept results of its build (a tour of a few rooms replaces only
    those): the reference of --compare."""
    keep = {}
    if Path(keep_path).exists():
        keep = {r["room"]: r for r in json.loads(Path(keep_path).read_text(encoding="utf-8"))}
    for r in json.loads(Path(new_path).read_text(encoding="utf-8")):
        keep[r["room"]] = r
    rows = [keep[k] for k in sorted(keep)]
    Path(keep_path).write_text(json.dumps(rows, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", nargs="?", type=Path)
    ap.add_argument("--rooms", default="")
    ap.add_argument("--details", type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--title", default="")
    ap.add_argument("--compare", nargs=2)
    ap.add_argument("--names", default="A,B")
    ap.add_argument("--messages", action="store_true")
    ap.add_argument("--merge", nargs=2, metavar=("NEW", "KEEP"))
    args = ap.parse_args()
    if args.merge:
        return merge(*args.merge)
    if args.compare:
        return compare(*args.compare, args.names.split(",", 1), args.messages)
    if not args.dir:
        ap.error("DIR fehlt")
    rooms = args.rooms.split() or sorted(p.stem[1:] for p in args.dir.glob("r*.meta"))
    results = [analyse(room, args.dir / f"r{room}.log", read_meta(args.dir / f"r{room}.meta")) for room in rooms]
    out, details = [], []
    code = report(results, out, details, args.title)
    print("\n".join(out))
    if args.details:
        args.details.write_text("\n".join(details) + "\n", encoding="utf-8")
    if args.json:
        slim = [{k: v for k, v in r.items()} for r in results]
        args.json.write_text(json.dumps(slim, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return code


if __name__ == "__main__":
    sys.exit(main())
