#!/usr/bin/env bash
# Lokale Prüfung des Spielkerns mit den Disc-Daten (WSL2 oder Linux). Started
# by port/scripts/test-r100-core.ps1 on Windows, or directly:
#
#   port/tools/whole_r100_check.sh --data DIR [--room 100] [--frames 10000]
#       [--out DIR] [--source REPO] [--work DIR] [--arm] [--lp64] [--no-build]
#
# --data DIR   the disc data, in one of these forms:
#              - the port's folder (disc0/sys, disc0/files, disc1/...),
#                as port/tools/whole_disc_folder.py writes it
#              - one extracted disc (sys/ and files/, Dolphin "Extract Entire Disc")
#              - the app's import folder (disc1/..., disc2/...)
#              - a folder with the disc images (*.iso, *.gcm): laid out once
#                into WORK/disc (never into the repository)
# --source     a checkout to build from (the Windows checkout under /mnt/c):
#              cloned into WORK/RE4 at its current commit, so the build runs on
#              the Linux file system; default: this script's checkout
# --work       working folder, default ~/re4-kern
# --out        where the report and the logs go, default WORK/runs/<time>
# --arm        also build ARM32 and run the game under qemu-arm (slow)
# --lp64       the 64-bit build (x86_64, milestone 12, stands for arm64-v8a)
#              instead of the 32-bit PC build (i686) for every step
# --card-only  after the build only the memory card runs (section 7)
# --rooms LIST after the build only a round of rooms (section 8): each room
#              by the debug start, --tour-frames frames of its main loop
#              (default 6000); once Leon is free he walks, runs, aims, fires,
#              opens the map and presses A here and there (--tour-script,
#              "none": no keys); --tour-jobs rooms at once (default: half the
#              cores). One line per room (runs, crash with where, stand still,
#              no room start, Leon or an enemy at NaN, a death loop), then the
#              findings of all rooms (check: lines, deaths, sanitizer reports
#              ...; port/tools/room_tour_report.py). LIST: room numbers
#              ("206,300,401"), "set" (two rooms of every room program
#              rel/stX_Y.rel) or "all" (every room of the game's room tables,
#              src/game/roomdata.cpp)
# --armhf      the ARM32 build (armeabi-v7a's code, the core on most devices)
#              under qemu-arm instead of the 32-bit PC build (i686), for the
#              room tour (slow)
# --aarch64    the 64-bit ARM build (arm64-v8a's code) under qemu-aarch64
#              instead of the 32-bit PC build (i686), for the room tour (slow)
# --sanitize   the check build with sanitizers (RE4_SANITIZE, default
#              address,undefined on i686, undefined on the 64-bit targets;
#              build folder whole-<target>-O2-san): AddressSanitizer stops at a
#              bad memory access, UndefinedBehaviorSanitizer names undefined
#              operations
# --room-info FILE  the debug build's debug/roomInfo.dat, for a round of rooms with discs that lack
#              it (the European ones): the debug start needs it for any room but r120 (patch 0119).
#              The run reads a folder of links to the discs (WORK/disc-ri) that holds the file as
#              well; the discs and the data folder stay as they are. The tour results are kept per
#              release (rundgang-<target>[-eu|-us].json) and a European or US tour is compared
#              with the debug discs' one
# --compare    the 32/64-bit comparison (milestone 12) instead of the normal steps: builds i686
#              and x86_64, plays both with the same keys (start room as --room, default 120:
#              the opening movie, the car ride, r100; 30000 frames unless --frames), records the
#              state trace of every frame and compares them (port/tools/trace_compare.py): the
#              first frame where 64 bit parts, with the values of both, and GX pictures of
#              both sides at that frame (a second, short run of each up to it)
#
# Steps: build and the cloud checks (whole_build.sh), the format comparison
# with the disc files (re4_format_compare --data), then the game from the
# title screen: debug start into the room (RE4_START_ROOM, patch 0026), the
# test script presses UP/A/START on the title screens until the room loads
# (RE4_PAD_UNTIL_ROOM), the opening event and the radio call end on their own,
# then Leon walks a few steps (RE4_PAD_FREE_SCRIPT, once the player is free),
# and the run ends after FRAMES frames of the main loop
# (RE4_STOP_AFTER). The report (bericht.txt) lists: crash or not, frames,
# GetDataExt coverage lines with "nein", converter messages, called stubs,
# the last state trace line. Then every movie of the discs through the CRI
# middleware (re4_host --movie-test): status, frames, stills, the opening
# movie's sound. Then the memory card (M10): a system file made at the boot
# check, a save in r100, a new start that loads it (run_memcard). Exit code 0
# when everything passed.
set -u

room=100
frames=10000
data=""
out=""
source=""
work="$HOME/re4-kern"
arm=0
host=i686
build=1
card_only=0
rooms=""
tour_frames=6000
tour_jobs=""
tour_script="wander"
room_info=""
reload_weapon=""
compare=0
sanitize=0
room_given=0
frames_given=0
while [ $# -gt 0 ]; do
    case "$1" in
    --data) data="$2"; shift 2 ;;
    --room) room="${2#r}"; room_given=1; shift 2 ;;
    --frames) frames="$2"; frames_given=1; shift 2 ;;
    --compare) compare=1; shift ;;
    --out) out="$2"; shift 2 ;;
    --source) source="$2"; shift 2 ;;
    --work) work="$2"; shift 2 ;;
    --arm) arm=1; shift ;;
    --lp64) host=x86_64; shift ;;
    --armhf) host=armhf; shift ;;
    --aarch64) host=aarch64; shift ;;
    --sanitize) sanitize=1; shift ;;
    --no-build) build=0; shift ;;
    --card-only) card_only=1; shift ;;
    --rooms) rooms="$2"; shift 2 ;;
    --tour-frames) tour_frames="$2"; shift 2 ;;
    --tour-jobs) tour_jobs="$2"; shift 2 ;;
    --tour-script) tour_script="$2"; shift 2 ;;
    --room-info) room_info="$2"; shift 2 ;;
    --reload) reload_weapon="$2"; shift 2 ;;
    *) echo "unbekannte Angabe: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$data" ]; then
    echo "Aufruf: $0 --data ORDNER [--room 100] [--frames 10000] [--out ORDNER] [--source REPO] [--arm]" >&2
    exit 2
fi
if [ "$compare" -eq 1 ]; then
    # the comparison starts with the car ride by default and runs into r100
    [ "$room_given" -eq 0 ] && room=120
    [ "$frames_given" -eq 0 ] && frames=30000
fi
if [ "$sanitize" -eq 1 ] && [ -z "${RE4_SANITIZE:-}" ]; then
    # AddressSanitizer only on i686 (CMakeLists.txt: on x86_64 its shadow lies where MEM1 is, under
    # qemu-aarch64 it does not run); UndefinedBehaviorSanitizer everywhere
    case "$host" in
    i686) export RE4_SANITIZE=address,undefined ;;
    *) export RE4_SANITIZE=undefined ;;
    esac
fi
room=$(echo "$room" | tr 'A-F' 'a-f')
mkdir -p "$work"
stamp=$(date +%Y%m%d-%H%M%S)
run="$work/runs/$stamp"
mkdir -p "$run"
report="$run/bericht.txt"
: > "$report"
say() { echo "$@" | tee -a "$report"; }
section() { say ""; say "== $* =="; }
# the report and the logs to --out (the Windows folder): what this run made replaces the entry of
# the same name from an earlier run there (a picture folder keeps no pictures of the run before)
copy_out() {
    local e
    for e in "$run"/* "$run"/.[!.]*; do
        [ -e "$e" ] && rm -rf "$out/${e##*/}"
    done
    cp -r "$run"/. "$out"/
}
overall=0
# Ctrl+C (also through wsl.exe): the game runs in the background (its
# SIGINT is ignored there), so it is ended here; the report and the logs so
# far still go to --out
on_abort() {
    trap - INT TERM
    local p
    for p in $(jobs -p); do
        kill -KILL "$p" 2> /dev/null
    done
    say ""
    say "ABGEBROCHEN (Strg+C)"
    if [ -n "$out" ]; then
        mkdir -p "$out"
        copy_out
        echo "Bericht und Logs bis hierher: $out"
    fi
    exit 130
}
trap on_abort INT TERM

# ---- source ------------------------------------------------------------------------
if [ -n "$source" ] && [ -d "$source/.git" ]; then
    # the checkout's current commit, cloned once and then fetched
    src="$work/RE4"
    git_s() { git -c safe.directory='*' "$@"; }
    if [ ! -d "$src/.git" ]; then
        echo "Quelltext wird nach $src kopiert (einmalig) ..."
        rm -rf "${src:?}"
        git_s clone -q "$source" "$src" || { echo "git clone von $source fehlgeschlagen" >&2; exit 1; }
    fi
    git_s -C "$src" fetch -q "$source" HEAD || { echo "git fetch von $source fehlgeschlagen" >&2; exit 1; }
    git_s -C "$src" checkout -q --detach FETCH_HEAD || exit 1
elif [ -n "$source" ]; then
    # a folder without git (a downloaded ZIP): copied as it is, without builds
    src="$work/RE4-copy"
    echo "Quelltext wird nach $src kopiert ..."
    rm -rf "${src:?}"
    mkdir -p "$src"
    (cd "$source" && tar --exclude=./port/build -cf - .) | (cd "$src" && tar -xf -) || exit 1
else
    src="$(cd "$(dirname "$0")/../.." && pwd)"
fi
commit=$(git -C "$src" -c safe.directory='*' log -1 --format='%h %s' 2>/dev/null || echo "(ohne git: $source)")
# parallel compile jobs: about 1.5 GB of memory each (clang on the big units)
mem_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
jobs=$((mem_kb / 1500000))
[ "$jobs" -gt "$(nproc)" ] && jobs=$(nproc)
[ "$jobs" -lt 1 ] && jobs=1
export RE4_JOBS="$jobs"

say "RE4-Kern: lokale Prüfung mit Disc-Daten, Raum r$room"
say "Zeit:    $(date '+%Y-%m-%d %H:%M:%S')"
say "Stand:   $commit"
say "System:  $(uname -srm), $(nproc) Kerne, $((mem_kb / 1048576)) GB Speicher, $jobs Build-Jobs"
say "Daten:   $data"
if [ "$compare" -eq 1 ]; then
    say "Kern:    i686 und x86_64 (Vergleichslauf 32/64 bit)"
else
    case "$host" in
    x86_64) say "Kern:    x86_64 (64 bit, wie arm64-v8a)" ;;
    aarch64) say "Kern:    aarch64 (64 bit, der Code von arm64-v8a, unter qemu-aarch64)" ;;
    *) say "Kern:    $host (32 bit, wie armeabi-v7a)" ;;
    esac
fi
[ -n "${RE4_SANITIZE:-}" ] && say "Prüf-Build mit Sanitizern: $RE4_SANITIZE (Ordner whole-$host-O${RE4_OPT-2}-san)"

# ---- disc data -------------------------------------------------------------------------
layout=""
kind=""
game_ok=1
if [ -f "$data/disc0/sys/fst.bin" ]; then
    layout="$data"; kind="Port-Ordner (disc0, disc1)"
elif [ -f "$data/sys/fst.bin" ] && [ -d "$data/files" ]; then
    layout="$data"; kind="eine entpackte Disc (sys, files)"
elif [ -d "$data/disc1/files" ]; then
    # the app's import folder counts the discs from 1
    links="$work/disc-links"
    rm -rf "${links:?}"
    mkdir -p "$links"
    ln -s "$data/disc1" "$links/disc0"
    [ -d "$data/disc2" ] && ln -s "$data/disc2" "$links/disc1"
    layout="$links"; kind="App-Import-Ordner (disc1, disc2)"
    if [ ! -f "$data/disc1/sys/fst.bin" ]; then
        game_ok=0
    fi
else
    mapfile -t images < <(find "$data" -maxdepth 2 -type f \( -iname '*.iso' -o -iname '*.gcm' \) | sort)
    if [ ${#images[@]} -gt 0 ]; then
        layout="$work/disc"; kind="Disc-Abbilder: ${images[*]##*/}"
        # the folder must hold exactly these discs: a run with the debug discs and one with the
        # European ones (port/docs/retail-eu-plan.md) share the work folder
        checks=()
        for i in "${images[@]}"; do checks+=(--disc "$i"); done
        if ! python3 "$src/port/tools/whole_disc_folder.py" --check "$layout" "${checks[@]}" > /dev/null 2>&1; then
            echo "Disc-Abbilder werden einmalig nach $layout entpackt ..."
            args=()
            for i in "${images[@]}"; do args+=(--disc "$i"); done
            rm -rf "${layout:?}"
            if ! python3 "$src/port/tools/whole_disc_folder.py" "${args[@]}" --out "$layout" > "$run/disc-folder.log" 2>&1; then
                say "FEHLER: Disc-Abbilder nicht lesbar:"
                sed 's/^/  /' "$run/disc-folder.log" | tail -20 | tee -a "$report"
                exit 1
            fi
        fi
    fi
fi
if [ -z "$layout" ]; then
    say "FEHLER: In $data liegen weder entpackte Disc-Dateien (sys/, files/) noch Disc-Abbilder (.iso/.gcm)."
    exit 1
fi
# --room-info: a folder of links to the discs plus the debug build's debug/roomInfo.dat (the
# European discs lack it; the debug start of the tour needs it), made again when its source changes.
# The runtime finds a file through the disc's file table (sys/fst.bin), so each disc's copy of it
# gets a folder "Debug" with "roomInfo.dat" appended to the root (disc offset 0: the port reads
# the file from files/)
if [ -n "$room_info" ] && [ -z "$(find "$layout"/disc*/files/Debug "$layout"/files/Debug -maxdepth 1 \
        -iname roominfo.dat 2> /dev/null)" ]; then
    if [ ! -f "$room_info" ] || [ ! -f "$layout/disc0/sys/fst.bin" ]; then
        say "FEHLER: --room-info braucht die Datei ($room_info) und Discs in der Form disc0, disc1"
        exit 1
    fi
    ri="$work/disc-ri"
    ri_source="$(readlink -f "$layout") $(sha1sum < "$room_info" | cut -c1-40)"
    if [ "$(cat "$ri/.source" 2> /dev/null)" != "$ri_source" ]; then
        rm -rf "${ri:?}"
        for d in "$layout"/disc*; do
            n=${d##*/}
            mkdir -p "$ri/$n/files/Debug" "$ri/$n/sys"
            for e in "$d"/sys/*; do
                [ "${e##*/}" = fst.bin ] || ln -s "$(readlink -f "$e")" "$ri/$n/sys/"
            done
            python3 - "$d/sys/fst.bin" "$ri/$n/sys/fst.bin" "$(stat -L -c %s "$room_info")" <<'PY'
import struct, sys
fst = bytearray(open(sys.argv[1], "rb").read())
size = int(sys.argv[3])
n = struct.unpack_from(">I", fst, 8)[0]
entries, names = fst[:n * 12], fst[n * 12:]
# the folder (parent: the root, next: past its one file) and the file, names at the end
entries += struct.pack(">III", 1 << 24 | len(names), 0, n + 2)
entries += struct.pack(">III", len(names) + 6, 0, size)
names += b"Debug\0roomInfo.dat\0"
struct.pack_into(">I", entries, 8, n + 2)
open(sys.argv[2], "wb").write(entries + names)
PY
            for e in "$d"/files/*; do
                case "${e##*/}" in [Dd][Ee][Bb][Uu][Gg]) continue ;; esac
                ln -s "$(readlink -f "$e")" "$ri/$n/files/"
            done
            cp "$room_info" "$ri/$n/files/Debug/roomInfo.dat"
        done
        echo "$ri_source" > "$ri/.source"
    fi
    layout="$ri"; kind="$kind, dazu debug/roomInfo.dat für den Debug-Start ($room_info)"
fi
say "Form:    $kind"
# which release the discs are (port/tools/disc_release.py, the same rule as runtime/dvd.cpp)
release=$(python3 -c 'import os, sys; sys.path.insert(0, sys.argv[1]); from disc_release import release
print(next((release(open(b, "rb").read(0x440)) for b in sys.argv[2:] if os.path.isfile(b)), ""))'     "$src/port/tools" "$layout"/disc0/sys/boot.bin "$layout"/sys/boot.bin 2>/dev/null)
say "Version: ${release:-unbekannt} (debug = Debug-Build G4BE08, retail-eu = europäische Verkaufsversion G4BP08, retail-us = US-Verkaufsversion G4BE08)"
if [ "$game_ok" -eq 0 ]; then
    say "Hinweis: sys/boot.bin und sys/fst.bin fehlen; der Spiellauf braucht sie (Disc-Abbilder angeben oder"
    say "         mit Dolphin \"Gesamte Disc extrahieren\"). Der Formatvergleich läuft trotzdem."
fi

# ---- build and cloud checks --------------------------------------------------------------
# optimised like the Android build (port/tools/whole_build_android.sh); RE4_OPT=
# (empty) checks the unoptimised build instead
export RE4_OPT="${RE4_OPT-2}"
opt="${RE4_OPT:+-O$RE4_OPT}"
# the build folder: whole-<target>-O2, with --sanitize whole-<target>-O2-san
bsuf="$opt${RE4_SANITIZE:+-san}"
if [ -n "${RE4_SANITIZE:-}" ]; then
    # as whole_build.sh; the symbolizer turns addresses into file and line
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:detect_odr_violation=0:detect_stack_use_after_return=0:handle_segv=0:handle_sigbus=0:handle_sigfpe=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:handle_segv=0:handle_sigbus=0:handle_sigfpe=0}"
    for s in llvm-symbolizer /usr/lib/llvm-18/bin/llvm-symbolizer /usr/bin/llvm-symbolizer-18; do
        if command -v "$s" > /dev/null; then
            export ASAN_SYMBOLIZER_PATH="${ASAN_SYMBOLIZER_PATH:-$(command -v "$s")}"
            break
        fi
    done
fi
targets=("$host")
[ "$compare" -eq 1 ] && targets=(i686 x86_64)
[ "$arm" -eq 1 ] && targets+=(armhf)
if [ "$build" -eq 1 ]; then
    section "1. Build und Prüfungen ohne Disc-Daten (whole_build.sh ${targets[*]})"
    if ! command -v clang > /dev/null || ! command -v ninja > /dev/null; then
        say "FEHLER: Werkzeuge fehlen. Einmal ausführen: sudo $src/port/tools/whole_setup_linux.sh"
        exit 1
    fi
    "$src/port/tools/whole_build.sh" "${targets[@]}" 2>&1 | tee -a "$report"
    [ "${PIPESTATUS[0]}" -ne 0 ] && overall=1
fi

# ---- format comparison with the disc files ---------------------------------------------
cmp="$src/port/build/whole-$host$bsuf/re4_format_compare"
[ "$card_only" -eq 0 ] && [ -z "$rooms" ] && [ -z "$reload_weapon" ] && [ "$compare" -eq 0 ] &&
    section "2. Formatvergleich App-Leser gegen Kern-Umwandler mit Disc-Daten"
if [ "$card_only" -eq 1 ] || [ -n "$rooms" ] || [ -n "$reload_weapon" ] || [ "$compare" -eq 1 ]; then
    : # --card-only / --rooms / --reload / --compare: straight to the memory card / the round of
      # rooms / the reload / the comparison
elif [ -x "$cmp" ]; then
    "$cmp" --data "$layout" --room "r$room" > "$run/formatvergleich.txt" 2> "$run/formatvergleich.log"
    code=$?
    tee -a "$report" < "$run/formatvergleich.txt"
    # converter messages: what was left undone or left as it is
    notes=$(grep -h "formats:" "$run/formatvergleich.log" |
        grep -E "no converter|incomplete|outside|left |no field list|blocks in its chain|more than|unknown" |
        sed 's/^\[re4\] //' | sort | uniq -c | sort -rn)
    if [ -n "$notes" ]; then
        say "Umwandler-Meldungen beim Vergleich ($(echo "$notes" | grep -c .) verschiedene):"
        echo "$notes" | head -80 | sed 's/^/  /' | tee -a "$report"
    fi
    [ "$code" -ne 0 ] && overall=1
else
    say "FEHLER: $cmp fehlt (Build fehlgeschlagen?)"
    overall=1
fi

# ---- the game: debug start into the room ---------------------------------------------------
# Every 60 frames UP, 15 frames later A, 15 frames later START: the timing of
# the boot test of 13.10.2026 (M4 part B), which got through the memory card
# dialog (UP+A picks "continue without saving", A alone retries). UP keeps the
# title menu on "new game" (patch 0026 puts it there); START skips the logos;
# A or START leaves the debug start menu (title.cpp titleExit). There UP moves
# the cursor (21 lines, wrapping; line 1 loads, 2 forces r120, 0x12 hard mode +
# reset): at most one UP can arrive before the next A/START, so the cursor is
# line 0 or 20, both the normal start. RE4_MEMCARD=none leaves memory card slot
# A empty, so that the boot check shows the "no memory card" dialog this
# timing was made for (and no card file is made in the data folder; since
# milestone 10 the check is the game's own with a real card), RE4_ROOM_MENU=1
# the debug start menu (patch 0043; the default, named because this timing
# needs it).
pad_script="UP@100-60000/60 A@115-60000/60 START@130-60000/60"
# No key in the room until the player is free: the opening event and the
# radio call that follows it must end on their own (the radio waits for its
# voice stream; on the Retroid it hung there, 14.10.2026, and no key worked).
# Once free (no event, no sub screen for 300 frames; "run: player free" in the
# log), Leon walks ahead for 10 s: the report shows how far he got.
room_script=""
# Before walking: the map (Z, as R1 on the Retroid) opens at free+30, a GX
# picture is taken at free+200 (RE4_GX_DUMP_FREE; on the Retroid the map
# stayed black) and B closes it at free+300; then Leon walks from free+450.
# At free+5000 the soft reset of "game over" -> "no" (RE4_TEST_SOFT_RESET;
# on the Retroid the screen stayed black): the title must come back.
free_script="Z@30 B@300 STICKUP@450-750/1"
map_dump=200
soft_reset=5000
# r100 only: once free the player is put in front of the first house, where
# the room script puts him after the s03 event (r100.cpp r100_Sce_look),
# facing the house; the walk then leads into the s03 trigger (the Retroid
# crashed there, 14.10.2026). Starting in r120 (a new game: the opening movie,
# played since milestone 9, here at the pace of the fields, and the car ride)
# ends in r100 as well, the player first free there (use --frames 30000).
# Other rooms: no placing.
place=""
case "$room" in 100 | 120) place="free+1:-82910,860,-38480,1.75" ;; esac

# "Bild N, Spielschritt R, Raum 0xXXX" from the last complete trace line
trace_state() {
    local line f r rm
    line=$(tail -n 2 "$1" 2>/dev/null | head -n 1)
    f=$(echo "$line" | grep -o '"frame":[0-9]*' | cut -d: -f2)
    r=$(echo "$line" | grep -o '"Rno0":[0-9]*' | cut -d: -f2)
    rm=$(echo "$line" | grep -o '"room":"0x[0-9a-fA-F]*"' | cut -d'"' -f4)
    if [ -n "$f" ]; then
        echo "Bild $f, Spielschritt $r, Raum $rm"
    else
        echo "noch kein Bild"
    fi
}

# The environment of a game run (run_game and the short runs of run_compare): the test
# script, the test helpers, the end. In the comparison both builds get the float functions of
# the C library through their double versions (host/det_math.c, LD_PRELOAD): glibc's float
# code differs between i686 and x86_64 in the last bits, the runs would part over that alone.
set_game_env() {
    GAME_ENV=(RE4_VI_FAST=1 RE4_WAIT_TRACE=1 RE4_START_ROOM="0x$room" RE4_PAD_UNTIL_ROOM=1
        RE4_MEMCARD=none RE4_ROOM_MENU=1 RE4_PAD_SCRIPT="$pad_script" RE4_PAD_ROOM_SCRIPT="$room_script"
        RE4_PAD_FREE_SCRIPT="$free_script" RE4_TEST_PLACE="$place" RE4_STOP_AFTER="$frames"
        RE4_GX_DUMP_FREE="$map_dump" RE4_TEST_SOFT_RESET="free+$soft_reset")
    if [ "$compare" -eq 1 ]; then
        # the debug build's own displays off, as in the app: its process time bars print the
        # time each part of the frame took (debug.cpp processBarDisp, OSGetTick), so their digits,
        # and with them the GP counts, depend on how fast the computer was (first compare run
        # 18.10.2026: 2 draws more on i686 in frame 2)
        GAME_ENV+=(LD_PRELOAD="$src/port/build/whole-$1$bsuf/libre4_detmath.so" RE4_DEBUG_HUD=0)
    fi
}

# target, overall limit, limit until the main loop, stall limit (seconds)
run_game() {
    local t="$1" limit="$2" main_limit="$3" stall="$4" dir="$src/port/build/whole-$1$bsuf"
    local log="$run/spiel-$t.log" trace="$run/trace-$t.jsonl" cov="$run/abdeckung-$t.tsv"
    local bridge="$run/bruecke-$t.jsonl" snaps="$run/schnappschuesse-$t" gxdump="$run/gx-$t"
    local runner=() bad=0
    set_runner "$t"
    runner=("${RUNNER[@]}")
    section "3. Spiellauf r$room ($t): Titelbild, Debug-Start, $frames Bilder ab der Hauptschleife"
    if [ ! -x "$dir/re4_host" ]; then
        say "FEHLER: $dir/re4_host fehlt (Build fehlgeschlagen?)"
        overall=1
        return
    fi
    echo "  (Fortschritt alle 30 s; Abbruch bei $stall s ohne neues Bild, ohne Hauptschleife nach $main_limit s, spätestens nach $limit s)"
    rm -rf "$cov" "$trace" "$trace.gz" "$bridge" "$snaps" "$gxdump" "$run/ton-$t.wav"
    mkdir -p "$snaps" "$gxdump"
    local start=$SECONDS
    set_game_env "$t"
    (cd "$dir" && exec env "${GAME_ENV[@]}" \
        RE4_TRACE="$trace" RE4_COVERAGE_LOG="$cov" RE4_BRIDGE_DUMP="$bridge" RE4_BRIDGE_SNAPSHOT="$snaps" RE4_GX_DUMP="$gxdump" \
        RE4_AUDIO_WAV="$run/ton-$t.wav" RE4_AUDIO_WAV_SECONDS=240 \
        "${runner[@]}" ./re4_host --data "$layout") > "$log" 2>&1 &
    local pid=$!
    # watchdog: progress, stall (no new trace data), no main loop, overall limit
    local size last_size=-1 last_change=$SECONDS last_note=$SECONDS reason="" state
    while kill -0 "$pid" 2> /dev/null; do
        sleep 5
        size=$(stat -c %s "$trace" 2> /dev/null || echo 0)
        if [ "$size" != "$last_size" ]; then
            last_size=$size
            last_change=$SECONDS
        fi
        if [ $((SECONDS - last_note)) -ge 30 ]; then
            echo "  ... $((SECONDS - start)) s: $(trace_state "$trace")"
            last_note=$SECONDS
        fi
        if [ $((SECONDS - last_change)) -ge "$stall" ]; then
            reason="STILLSTAND: seit $stall s kein neues Bild"
        elif [ $((SECONDS - start)) -ge "$main_limit" ] && ! grep -q "run: main loop reached" "$log"; then
            reason="ABBRUCH: Hauptschleife nach $main_limit s nicht erreicht"
        elif [ $((SECONDS - start)) -ge "$limit" ]; then
            reason="ABBRUCH: Zeitlimit von $limit s erreicht"
        fi
        if [ -n "$reason" ]; then
            # backtrace of the running context into the log (host/main.cpp), then stop
            kill -USR1 "$pid" 2> /dev/null
            sleep 3
            kill -KILL "$pid" 2> /dev/null
            break
        fi
    done
    wait "$pid" 2> /dev/null
    local code=$?
    local secs=$((SECONDS - start))
    local stopped reached
    stopped=$(grep -m1 "run: stopped after" "$log" | sed 's/^\[re4\] //')
    reached=$(grep -m1 "run: main loop reached" "$log" | sed 's/^\[re4\] //')
    if [ -n "$reason" ]; then
        say "Ergebnis: $reason ($secs s)"
        bad=1
    elif [ "$code" -eq 0 ] && [ -n "$stopped" ] &&
        { echo "$stopped" | grep -q "room r$room" || grep -q "run: soft reset requested" "$log"; }; then
        # after the requested soft reset the run ends at the title (checked below)
        say "Ergebnis: OK, kein Absturz ($secs s)"
    elif grep -q "fatal signal" "$log"; then
        say "Ergebnis: ABSTURZ ($(grep -m1 'fatal signal' "$log" | sed 's/^\[re4\] //'))"
        bad=1
    else
        say "Ergebnis: FEHLER (Exit-Code $code, Ziel nicht erreicht)"
        bad=1
    fi
    say "Letzter Stand:  $(trace_state "$trace")"
    say "Hauptschleife: ${reached:-nicht erreicht}"
    say "Ende:          ${stopped:-kein regulärer Stopp}"
    local placed free
    free=$(grep -m1 "run: player free" "$log" | sed 's/^\[re4\] //')
    if [ -n "$free" ]; then
        say "Spieler frei:  $free"
    else
        say "Spieler frei:  NEIN - Ereignis oder Untermenü (Funkspruch) endete nicht von selbst"
        bad=1
    fi
    placed=$(grep -m1 "run: player placed" "$log" | sed 's/^\[re4\] //')
    [ -n "$placed" ] && say "Testhilfe:     $placed"
    # the camera of the debug start (patch 0044): the Retroid crashed at the
    # first camera cut (examining the skulls in r100)
    local camera
    camera=$(grep -m1 "run: camera after the room start" "$log" | sed 's/^\[re4\] run: //')
    if [ -n "$camera" ]; then
        say "Kamera:        $camera"
        case "$camera" in *DESTROYED*) bad=1 ;; esac
    fi
    # sub screens (radio, map with Z): the map stayed black on the Retroid and
    # could not be closed
    if grep -q "run: sub screen" "$log"; then
        say "Untermenüs:    $(grep "run: sub screen" "$log" | sed 's/^\[re4\] run: sub screen //' | head -n 8 | paste -sd ',' - | sed 's/,/, /g')"
        if grep -q "Read File: SS/.*/ss_map.dat" "$log"; then
            say "Kartendatei:   gelesen (ss_map.dat)"
        elif [ -n "$free" ]; then
            say "Kartendatei:   NICHT gelesen (Karte nicht geöffnet oder Laden hängt)"
        fi
        if [ "$(grep "run: sub screen" "$log" | tail -n 1 | grep -c ' open ')" = 1 ]; then
            say "Untermenü am Ende noch OFFEN (ließ sich nicht schließen)"
            bad=1
        fi
    fi
    # the soft reset of "game over" -> "no": the title must come back (it
    # reads title.dat anew); on the Retroid the screen stayed black
    local reset
    reset=$(grep -m1 "run: soft reset requested" "$log" | sed 's/^\[re4\] //')
    if [ -n "$reset" ]; then
        say "Soft-Reset:    $reset"
        grep "run: after the soft reset\|SOFT_RESET" "$log" | sed 's/^\[re4\] /  /' | head -n 10 | tee -a "$report"
        if sed -n '/run: soft reset requested/,$p' "$log" | grep -q "Read File: SS/.*/title.dat"; then
            say "Titel nach dem Soft-Reset: ja (title.dat neu gelesen)"
        else
            say "Titel nach dem Soft-Reset: NEIN (das Spiel kehrt nicht zum Titel zurück)"
            bad=1
        fi
    elif [ -n "$free" ]; then
        say "Soft-Reset:    nicht erreicht (Lauf endete vorher; mehr Bilder: --frames)"
    fi
    if grep -q "run: area " "$log"; then
        say "Auslöse-Bereiche des Raums (AEV, für RE4_TEST_PLACE):"
        grep "run: area " "$log" | sed 's/^\[re4\] run: /  /' | head -n 80 | tee -a "$report"
    fi
    if grep -q "backtrace of the running context" "$log"; then
        say "Rückverfolgung beim Abbruch (wo das Programm gerade war, dann jede Spielaufgabe mit ihrem Warteort):"
        sed -n '/backtrace of the running context/,$p' "$log" | grep -v "pad: frame" | head -n 400 |
            sed 's/^/  /' | tee -a "$report"
    fi
    # coverage: every GetDataExt access, converted yes / no
    if [ -f "$cov" ]; then
        local total no room_no
        total=$(wc -l < "$cov")
        no=$(grep -c $'\tnein' "$cov")
        room_no=$(grep -i "r$room" "$cov" | grep -c $'\tnein')
        say "Abdeckung (GetDataExt): $total Zugriffe, davon \"nein\": $no (im Archiv von r$room: $room_no)"
        if [ "$no" -gt 0 ]; then
            say "  Zeilen mit \"nein\" (Archiv, Kennung, Nummer, Status):"
            grep $'\tnein' "$cov" | awk -F'\t' '{print $1 "\t" $2 "\t" $3 "\t" $5}' | sort | uniq -c | sort -rn |
                head -40 | sed 's/^/    /' | tee -a "$report"
            bad=1
        fi
        say "  Kennungen im Archiv von r$room: $(grep -i "r$room" "$cov" | cut -f2 | sort | uniq -c | tr -s ' ' |
            tr '\n' ',' | sed 's/,$//')"
    else
        say "Abdeckung: keine GetDataExt-Zugriffe protokolliert"
    fi
    # converter messages that point at something left undone
    local msgs
    msgs=$(grep -h "formats:" "$log" | grep -E "no converter|incomplete|outside|left big-endian|left as|no field list|blocks in its chain|more than|overlaps|suspended|back again" |
        sed 's/^\[re4\] //' | sort | uniq -c | sort -rn)
    if [ -n "$msgs" ]; then
        say "Umwandler-Meldungen:"
        echo "$msgs" | head -40 | sed 's/^/  /' | tee -a "$report"
    else
        say "Umwandler-Meldungen: keine"
    fi
    # the radio archives: which tags their message and sequence halves carry
    if grep -q "codec archive" "$log"; then
        say "Funkarchive:"
        grep "codec archive" "$log" | sed 's/^\[re4\] formats: /  /' | sort -u | head -5 | tee -a "$report"
    fi
    local stubs
    stubs=$(grep -h "stub called:" "$log" | sed 's/^\[re4\] stub called: //' | sort -u)
    say "Aufgerufene Stubs (Funktionen ohne Umsetzung): $(echo -n "$stubs" | grep -c .)"
    [ -n "$stubs" ] && echo "$stubs" | head -60 | sed 's/^/  /' | tee -a "$report"
    # the game's own messages (pLog->err / warn, patch 0076), each distinct one once
    local game_lines game_msgs
    game_lines=$(grep -c "^\[re4\] game: " "$log")
    game_msgs=$(grep -h "^\[re4\] game: " "$log" | sed 's/^\[re4\] game: //; s/ ([0-9]* times)$//' | awk '!seen[$0]++')
    say "Spielmeldungen (pLog): $game_lines Zeilen, $(echo -n "$game_msgs" | grep -c .) verschiedene"
    [ -n "$game_msgs" ] && echo "$game_msgs" | head -40 | sed 's/^/  /' | tee -a "$report"
    # diagnosis on 64 bit (patch 0078, runtime/watch.cpp): who writes a watched place
    if grep -q "^\[re4\] \(watch\|MotionMoveCore\):" "$log"; then
        say "Überwachung (Hardware-Haltepunkte, Patch 0078):"
        grep -h "^\[re4\] \(watch\|MotionMoveCore\):" "$log" | head -80 | sed 's/^\[re4\] /  /' | tee -a "$report"
    fi
    if [ -f "$trace" ]; then
        local lines enemies last
        lines=$(wc -l < "$trace")
        last=$(tail -n 1 "$trace")
        enemies=$(echo "$last" | grep -o '"slot"' | wc -l)
        say "Zustands-Trace: $lines Bilder; im letzten Bild $enemies Gegner"
        say "  letzte Zeile: $(echo "$last" | cut -c1-600)"
        # the sound streams (status be, state rno, requests, volume, DVD
        # busy/requests/status, DMA, errors): the last frame, then every frame
        # where the set of streams (id, status, state) changes, up to 40 lines
        # (the radio call: ring tone stopped, voice ready, playing, ended)
        python3 - "$trace" <<'PY' | tee -a "$report"
import json, sys
last, prev, lines = None, None, []
for raw in open(sys.argv[1], encoding="utf-8", errors="replace"):
    try:
        row = json.loads(raw)
    except ValueError:
        continue
    streams = row.get("str", [])
    last = streams
    key = [(s.get("w"), s.get("id"), s.get("be"), s.get("rno")) for s in streams]
    if key != prev:
        lines.append("    Bild %s: %s" % (row.get("frame"), json.dumps(streams, separators=(",", ":"))))
        prev = key
if last:
    print("  Tonspuren im letzten Bild: " + json.dumps(last, separators=(",", ":")))
print("  Tonspuren-Verlauf (Bilder mit Änderungen):")
print("\n".join(lines[:40]) if any(l.split(": ", 1)[1] != "[]" for l in lines) else "    (keine Tonspur im ganzen Lauf)")
PY
        # control: how far Leon walked while the free script held the stick
        # (until an event took over, e.g. s03 at the first house)
        local freeframe walk
        freeframe=$(echo "$free" | sed -n 's/.* at frame \([0-9]*\).*/\1/p')
        if [ -n "$freeframe" ]; then
            walk=$(python3 - "$trace" "$freeframe" <<'PY'
import json, math, sys
free = int(sys.argv[2])
start, end, pos, why = free + 452, free + 750, {}, "Ende der Tastenfolge"
for line in open(sys.argv[1]):
    try:
        r = json.loads(line)
    except ValueError:
        continue
    f = r["frame"]
    if f < start or f > end or "pl" not in r:
        continue
    if "E" in r.get("cam", {}).get("sw", "") or "U" in r.get("cam", {}).get("sw", ""):
        why = f"Ereignis ab Bild {f}"
        break
    pos[f] = r["pl"]
if len(pos) < 2:
    print("0 keine Daten")
else:
    a, b = pos[min(pos)], pos[max(pos)]
    d = math.dist((a[0], a[2]), (b[0], b[2]))
    print(f"{d:.0f} Leon lief {d:.0f} Einheiten in {max(pos) - min(pos)} Bildern (Stick nach vorn, bis {why})")
PY
)
            if [ "${walk%% *}" -ge 500 ] 2> /dev/null; then
                say "Steuerung:     OK, ${walk#* }"
            else
                say "Steuerung:     FEHLER, ${walk#* }"
                bad=1
            fi
        fi
        # the sound (M8): what the core mixed, as a WAV of the first 240 s of game
        # time (to listen to on the PC) and its levels
        local audio
        audio=$(grep "audio: [0-9]* blocks" "$log" | tail -1 | sed 's/^\[re4\] //')
        say "Ton:           ${audio:-keine Tonblöcke im Protokoll}"
        if [ -s "$run/ton-$t.wav" ]; then
            say "Ton-Aufnahme:  ton-$t.wav (zum Anhören)"
            python3 "$src/port/tools/audio_wav_report.py" "$run/ton-$t.wav" 10 | tee -a "$report"
        fi
        # the camera over the run: every change of its switches, and every 2500 frames
        python3 - "$trace" <<'PY' | tee -a "$report"
import json, sys
def v(a):
    return "(" + ", ".join(f"{x:.0f}" for x in a) + ")"
prev, out = None, []
for line in open(sys.argv[1]):
    try:
        r = json.loads(line)
    except ValueError:
        continue
    c = r.get("cam")
    if not c:
        continue
    key = (r["Rno0"], r["room"], c["sw"], c["r0"])
    if key != prev or r["frame"] % 2500 == 0:
        out.append(f"  Bild {r['frame']:6d} Schritt {r['Rno0']} Raum {r['room']} Schalter [{c['sw']}] r0 {c['r0']}: "
                   f"Auge {v(c['eye'])} Ziel {v(c['at'])} Winkel {c['fov']:.0f}, CamCtrl-Auge {v(c['ctrl'])}"
                   + (f", Leon {v(r['pl'])}" if "pl" in r else ""))
        prev = key
print("Kamera-Verlauf (Schalter: C Kamera an, D Debug-Kamera, S Kamera gestoppt, E Ereignis, U Untermenü, X Sonderkamera):")
for l in out[:40]:
    print(l)
if len(out) > 40:
    print(f"  ... {len(out) - 40} weitere Wechsel")
PY
        gzip -f "$trace"
    fi
    # core bridge (what the app would draw): models and their disc sources
    if [ -s "$bridge" ]; then
        python3 - "$bridge" "$room" <<'PY' | tee -a "$report"
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
room = "0x" + sys.argv[2].lower()
inroom = [r for r in rows if r["Rno0"] == 3 and r["room"] == room]
import re
def summary(last):
    models = last["models"]
    drawn = [m for m in models if m["drawn"]]
    # the room's scenery objects made from SMD works (cObjScr, ObjMgr id 2, "smd" = block and
    # work): the app draws them with the room scene; scenery a room script made (SetObjSmd, like
    # r100's police car) is drawn like the other models
    scenery = [m for m in drawn if m["mgr"] == 1 and m["id"] == "0x02" and "smd" in m]
    drawn = [m for m in drawn if not (m["mgr"] == 1 and m["id"] == "0x02" and "smd" in m)]
    def found(src):
        return bool(src) and not src.startswith("?")
    def known(m):
        return m["infos"] and all(found(i["bin"]) and found(i["tpl"]) for i in m["infos"])
    unknown = [m for m in drawn if not known(m)]
    screen = last.get("screen", 0)
    print(f"Brücke zur App (Bild {last['frame']}, Schritt {last['Rno0']}, Raum {last['room']}): "
          f"{len(models)} Figuren, davon {len(drawn) + len(scenery)} sichtbar ({len(scenery)} Raumkulisse, die die App "
          f"mit dem Raum zeichnet), {len(drawn) - len(unknown)} von {len(drawn)} übrigen mit bekannter "
          f"Modell-Herkunft auf der Disc; {last['parts']} Körperteile; Kamera {last['camera']}"
          + ("; Untermenü offen (Kamera = Weltkamera)" if screen & 1 else "") + ("; Ereignis läuft" if screen & 2 else "")
          + (f"; {last['dropped']} Figuren über der Obergrenze" if last.get("dropped") else "")
          + (f"; {last['droppedInfos']} Modellteile über der Obergrenze" if last.get("droppedInfos") else ""))
    # scenery per SMD (room -1, scroll blocks 0..): shown and hidden works
    blocks = {}
    for m in models:
        if m["mgr"] == 1 and m["id"] == "0x02" and "smd" in m:
            b = blocks.setdefault(m["smd"][0], [0, 0])
            b[0 if m["drawn"] else 1] += 1
    if blocks:
        print("  Raumkulisse nach SMD: " + ", ".join(
            f"{'Raum' if k < 0 else f'Block {k}'} {v[0]} sichtbar" + (f" / {v[1]} versteckt" if v[1] else "")
            for k, v in sorted(blocks.items())))
    script = [m for m in models if m["mgr"] == 1 and m["id"] == "0x02" and "smd" not in m]
    if script:
        print(f"  Kulisse aus Raumskripten (nicht im SMD, z. B. das Polizeiauto): {len(script)}, "
              f"davon {sum(1 for m in script if m['drawn'])} sichtbar")
    def plain(src):
        # unknown sources carry their address: leave it out when grouping
        return re.sub(r"0x[0-9a-f]{6,8}|\+0x[0-9a-f]+", "…", src or "?")
    seen, lines = {}, []
    for m in drawn:
        def how(i):
            if not i.get("shown", 1):
                return " (versteckt)"
            r = i.get("rigid", -1)
            morph = f" (Form-Animation, {i['morph']} Punkte)" if i.get("morph") else ""
            return (f" (starr an Teil {r})" if r >= 0 else " (am Modell)" if r == -2 else "") + morph
        srcs = "; ".join(f"BIN {i['bin'] or '?'} TPL {i['tpl'] or '?'}{how(i)}" for i in m["infos"]) or "kein Modell"
        key = (m["mgr"], m["id"], m["parts"], plain(srcs))
        if key in seen:
            seen[key] += 1
            continue
        seen[key] = 1
        lines.append((key, f"  {'Gegner' if m['mgr'] == 0 else 'Objekt'} {m['id']} Typ {m['type']}, {m['parts']} Teile: {srcs}"))
    for key, text in lines[:40]:
        print(text + (f"  (×{seen[key]})" if seen[key] > 1 else ""))
    if len(lines) > 40:
        print(f"  ... {len(lines) - 40} weitere Arten")
    if unknown:
        print(f"  Hinweis: {len(unknown)} sichtbare Figuren ohne bekannte Herkunft (die App kann sie noch nicht zeichnen)")
# early in the room (before a sub screen may hide the world) and at the end
picks = inroom[:1] + inroom[-1:] if len(inroom) > 1 else (inroom or rows[-1:])
for row in picks:
    summary(row)

PY
    else
        say "Brücke zur App: keine Ausgabe (RE4_BRIDGE_DUMP)"
    fi
    if [ "$bad" -ne 0 ]; then
        say "Letzte 60 Zeilen des Spiel-Logs (ohne Tastenzeilen):"
        grep -v "pad: frame" "$log" | tail -n 60 | sed 's/^/  /' | tee -a "$report"
        overall=1
    fi
}
# PPM -> PNG (python's zlib), for viewing on Windows
ppm_to_png() {
    python3 - "$1" <<'PY'
import struct, sys, zlib
from pathlib import Path
for ppm in sorted(Path(sys.argv[1]).glob("*.ppm")):
    data = ppm.read_bytes()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pixels = parts[3]
    raw = b"".join(b"\0" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    ppm.with_suffix(".png").write_bytes(png)
    ppm.unlink()
PY
}

# ---- pictures: the core's snapshots drawn as the app's core mode draws them ---------------
# (the app's renderer on a headless GLES3 context, port/android-decomp/tools/
# core_snapshot_render.cpp; needs the Mesa EGL/GLES packages of
# whole_setup_linux.sh)
render_pictures() {
    local snaps="$run/schnappschuesse-$host" pics="$run/bilder" tool_build="$src/port/build/app-host-gl"
    section "4. Bilder der App-Brücke (wie der Kernmodus der App zeichnet)"
    if ! ls "$snaps"/*.bin > /dev/null 2>&1; then
        say "Keine Schnappschüsse (Hauptschleife nicht erreicht?)"
        return
    fi
    mkdir -p "$pics"
    if ! cmake -S "$src/port/android-decomp/app/src/main/cpp" -B "$tool_build" -G Ninja \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
        -DRE4_BUILD_GL_CHECK=ON > "$run/bild-werkzeug.log" 2>&1 ||
        ! ninja -C "$tool_build" ${RE4_JOBS:+-j "$RE4_JOBS"} core_snapshot_render >> "$run/bild-werkzeug.log" 2>&1; then
        say "FEHLER: Bild-Werkzeug nicht gebaut (Log: bild-werkzeug.log; Pakete: sudo $src/port/tools/whole_setup_linux.sh)"
        tail -n 20 "$run/bild-werkzeug.log" | sed 's/^/  /' | tee -a "$report"
        return
    fi
    # the discs as the app imports them: disc1, disc2
    local approot="$run/app-disc"
    rm -rf "$approot"
    mkdir -p "$approot"
    if [ -d "$layout/disc0" ]; then
        ln -s "$layout/disc0" "$approot/disc1"
        [ -d "$layout/disc1" ] && ln -s "$layout/disc1" "$approot/disc2"
    else
        ln -s "$layout" "$approot/disc1"
    fi
    "$tool_build/core_snapshot_render" "$approot" "$pics" "$snaps"/*.bin 2> "$run/bild-werkzeug-lauf.log" |
        sed 's/^/  /' | tee -a "$report"
    rm -f "$pics"/room-*.dat
    # which models the app could load from the disc, and why not
    local loaded failed
    loaded=$(grep -c "^\[RE4Core\] model .*: [0-9]* joints$" "$run/bild-werkzeug-lauf.log" 2> /dev/null)
    failed=$(grep -h "^\[RE4Core\] model " "$run/bild-werkzeug-lauf.log" 2> /dev/null | grep -v ": [0-9]* joints$" |
        sed 's/^\[RE4Core\] //' | sort | uniq -c | sort -rn)
    say "  Modelle der App geladen: ${loaded:-0}; nicht ladbar: $(echo -n "$failed" | grep -c .)"
    [ -n "$failed" ] && echo "$failed" | head -25 | sed 's/^/    /' | tee -a "$report"
    grep -h "^\[RE4Core\] room " "$run/bild-werkzeug-lauf.log" 2> /dev/null | sort -u | head -5 | sed 's/^\[RE4Core\] /  /' |
        tee -a "$report"
    # where the characters' model infos land (one line per info of the player and
    # of models with more than 20 parts, from the first snapshot that has them)
    say "  Lage der Modellteile (Spieler und Figuren mit vielen Teilen; darunter je Info, was Dreiecke"
    say "  unsichtbar macht: auf 0 skalierte Teile, Alpha-Test, Mischmodus/Tiefe/Seite/Ausblenden des Spiels):"
    grep -h "^\[RE4Core\] bounds \|^\[RE4Core\]   info " "$run/bild-werkzeug-lauf.log" 2> /dev/null |
        sed 's/^\[RE4Core\] bounds //; s/^\[RE4Core\]   info /      info /' |
        awk '/^      info / {if (keep) print; keep=0; next}
             {keep=0; key=$4" "$6" "$8; if (!(key in seen) && ($6 == "00" || $0 ~ /joints [2-9][0-9]|joints [0-9][0-9][0-9]/)) {seen[key]=1; keep=1; print "    " $0}}' |
        head -80 | tee -a "$report"
    rm -rf "$approot"  # links to the disc folder: not copied with the report
    ppm_to_png "$pics"
    say "Bilder: $(ls "$pics"/*.png 2> /dev/null | wc -l) PNG-Dateien im Ordner bilder (bitte mitschicken)"
}

# ---- pictures through the GX emulation: what the game itself draws (logos,
# title, menus, displays, effects), drawn with the app's GX renderer
# (port/android-decomp/tools/gx_chunk_render.cpp; frames from RE4_GX_DUMP)
render_gx_pictures() {
    local dump="$run/gx-$host" pics="$run/bilder-gx" tool_build="$src/port/build/app-host-gl"
    section "5. Bilder über die GX-Nachbildung (was das Spiel selbst zeichnet)"
    if ! ls "$dump"/*.bin > /dev/null 2>&1; then
        say "Keine GX-Bilder aufgezeichnet (Spiel nicht weit genug gelaufen?)"
        return
    fi
    mkdir -p "$pics"
    if ! cmake -S "$src/port/android-decomp/app/src/main/cpp" -B "$tool_build" -G Ninja \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
        -DRE4_BUILD_GL_CHECK=ON > "$run/gx-werkzeug.log" 2>&1 ||
        ! ninja -C "$tool_build" ${RE4_JOBS:+-j "$RE4_JOBS"} gx_chunk_render >> "$run/gx-werkzeug.log" 2>&1; then
        say "FEHLER: GX-Bild-Werkzeug nicht gebaut (Log: gx-werkzeug.log; Pakete: sudo $src/port/tools/whole_setup_linux.sh)"
        tail -n 20 "$run/gx-werkzeug.log" | sed 's/^/  /' | tee -a "$report"
        return
    fi
    if ! "$tool_build/gx_chunk_render" "$pics" "$dump"/*.bin 2> "$run/gx-werkzeug-lauf.log" | sed 's/^/  /' |
        tee -a "$report"; then
        say "FEHLER: GX-Bilder (Log: gx-werkzeug-lauf.log)"
        overall=1
    fi
    grep -i "shader failed" -A 3 "$run/gx-werkzeug-lauf.log" 2> /dev/null | head -20 | sed 's/^/  /' | tee -a "$report"
    ppm_to_png "$pics"
    # GPU step b (19.10.2026): the same frames with what the renderer lets the GPU forget painted
    # magenta (RE4_GX_INVALIDATE_POISON): a picture that changes still needed the old content
    local poisoned="$run/bilder-gx-magenta"
    rm -rf "$poisoned"
    mkdir -p "$poisoned"
    RE4_GX_INVALIDATE_POISON=1 "$tool_build/gx_chunk_render" "$poisoned" "$dump"/*.bin > /dev/null \
        2>> "$run/gx-werkzeug-lauf.log"
    ppm_to_png "$poisoned"
    if python3 "$src/port/tools/picture_compare.py" "$pics" "$poisoned" --names normal,magenta \
        > "$run/bilder-gx-magenta.txt" 2>&1; then
        say "Wegwerfen (GPU-Schritt b), das Vergessene magenta$(grep 'Bildvergleich' "$run/bilder-gx-magenta.txt" | sed 's/^ *Bildvergleich normal \/ magenta//')"
        rm -rf "$poisoned" "$run/bilder-gx-magenta.txt"
    else
        say "FEHLER Wegwerfen (GPU-Schritt b): Bilder ändern sich, wenn das Vergessene magenta ist (bilder-gx-magenta.txt, Ordner bilder-gx-magenta; bitte mitschicken)"
        head -n 12 "$run/bilder-gx-magenta.txt" | sed 's/^/  /' | tee -a "$report"
        overall=1
    fi
    # GPU step d2 (19.10.2026): the same frames with 4x MSAA on zink (lavapipe, mesa-vulkan-drivers:
    # EXT_multisampled_render_to_texture2), in own buffers and on the tiles, where the EFB is drawn
    # back after every pass. They may differ only at edges drawn before a copy (drawn back with
    # their resolved colour and depth, not the samples; the Retroid's pictures at scale 3, 4x:
    # 0.4 % of the pixels more than 8). Then on the tiles again as on a GPU that loses everything
    # at a pass's end (RE4_GX_LOSE_TILES=1, magenta and depth 0; the Adreno 650 lost the depth):
    # the pictures must not change at all
    local msaa_own="$run/bilder-gx-msaa-puffer" msaa_tiles="$run/bilder-gx-msaa-kacheln"
    local msaa_lost="$run/bilder-gx-msaa-verlust"
    # (4: lavapipe has no 2x buffers of its own, the comparison was 2 against 4 samples)
    local zink=(env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=zink RE4_GX_SAMPLES=4)
    rm -rf "$msaa_own" "$msaa_tiles" "$msaa_lost"
    mkdir -p "$msaa_own" "$msaa_tiles" "$msaa_lost"
    "${zink[@]}" RE4_GX_MSAA_TILES=0 "$tool_build/gx_chunk_render" "$msaa_own" "$dump"/*.bin > /dev/null \
        2> "$run/gx-msaa-puffer.log"
    "${zink[@]}" RE4_GX_MSAA_TILES=1 "$tool_build/gx_chunk_render" "$msaa_tiles" "$dump"/*.bin > /dev/null \
        2> "$run/gx-msaa-kacheln.log"
    "${zink[@]}" RE4_GX_MSAA_TILES=1 RE4_GX_LOSE_TILES=1 "$tool_build/gx_chunk_render" "$msaa_lost" "$dump"/*.bin \
        > /dev/null 2> "$run/gx-msaa-verlust.log"
    if grep -q "MSAA: .* in own buffers" "$run/gx-msaa-puffer.log" && grep -q "MSAA: .* on the tiles" "$run/gx-msaa-kacheln.log"; then
        ppm_to_png "$msaa_own"
        ppm_to_png "$msaa_tiles"
        ppm_to_png "$msaa_lost"
        python3 "$src/port/tools/picture_compare.py" "$msaa_own" "$msaa_tiles" --names puffer,kacheln \
            > "$run/bilder-gx-msaa.txt" 2>&1
        say "MSAA auf den Kacheln (GPU-Schritt d2) gegenüber eigenen Puffern, 4 Proben, je Bild (zink):"
        sed 's/^/  /' "$run/bilder-gx-msaa.txt" | tee -a "$report"
        say "  (Bilder: Ordner bilder-gx-msaa-puffer und bilder-gx-msaa-kacheln)"
        if python3 "$src/port/tools/picture_compare.py" "$msaa_tiles" "$msaa_lost" --names kacheln,verlust \
            > "$run/bilder-gx-msaa-verlust.txt" 2>&1; then
            say "MSAA auf den Kacheln, alles verloren am Ende jedes Durchgangs (GPU-Schritt d2):$(grep 'Bildvergleich' "$run/bilder-gx-msaa-verlust.txt" | sed 's/^ *Bildvergleich kacheln \/ verlust://')"
            rm -rf "$msaa_lost" "$run/bilder-gx-msaa-verlust.txt"
        else
            say "FEHLER MSAA auf den Kacheln (GPU-Schritt d2): Bilder ändern sich, wenn am Ende eines Durchgangs alles verloren geht (bilder-gx-msaa-verlust.txt, Ordner bilder-gx-msaa-verlust; bitte mitschicken)"
            head -n 12 "$run/bilder-gx-msaa-verlust.txt" | sed 's/^/  /' | tee -a "$report"
            overall=1
        fi
    else
        say "MSAA auf den Kacheln (GPU-Schritt d2): nicht geprüft (zink fehlt: sudo $src/port/tools/whole_setup_linux.sh; Log: gx-msaa-kacheln.log)"
        rm -rf "$msaa_own" "$msaa_tiles" "$msaa_lost"
    fi
    # GPU step e (19.10.2026): the same frames with the depth of field's blur at the full resolution
    # as before step e (RE4_GX_DOF_HALF=0) against the half one of the pictures above. Only the
    # blurred background may differ, by a few steps (the cloud with a fixed focus on the Retroid's
    # pictures: on average under 1 of 255, 0.1 % of the pixels more than 8, at most 25); more than
    # 40 somewhere: the pictures stay for a look
    local dof_full="$run/bilder-gx-tiefe-voll" worst
    rm -rf "$dof_full"
    mkdir -p "$dof_full"
    RE4_GX_DOF_HALF=0 "$tool_build/gx_chunk_render" "$dof_full" "$dump"/*.bin > "$run/gx-tiefe-voll.log" 2>&1
    ppm_to_png "$dof_full"
    python3 "$src/port/tools/picture_compare.py" "$dof_full" "$pics" --names voll,halb > "$run/bilder-gx-tiefe.txt" 2>&1
    worst=$(sed -n 's/.*höchstens \([0-9]*\) von 255.*/\1/p' "$run/bilder-gx-tiefe.txt" | sort -n | tail -n 1)
    say "Tiefenschärfe in halber Auflösung (GPU-Schritt e) gegenüber voller: $(grep -c 'Autofokus' "$run/gx-tiefe-voll.log") Bilder mit Autofokus,$(grep 'Bildvergleich' "$run/bilder-gx-tiefe.txt" | sed 's/^ *Bildvergleich voll \/ halb://'), größte Abweichung ${worst:-0} von 255"
    if [ "${worst:-0}" -gt 40 ]; then
        say "  auffällig (mehr als 40): Ordner bilder-gx-tiefe-voll und bilder-gx-tiefe.txt bitte mitschicken"
    else
        rm -rf "$dof_full"
    fi
    # a few frames as data for the renderer work in the cloud (gx_chunk_render
    # draws them there; game data: never into the repository): the first
    # three and the first in the room
    local keep
    # the picture with the map open (free + map_dump, the dump starts one or two frames later)
    local freeframe mapfile=""
    freeframe=$(grep -m1 "run: player free" "$run/spiel-$host.log" 2> /dev/null | sed -n 's/.* at frame \([0-9]*\).*/\1/p')
    if [ -n "$freeframe" ]; then
        for d in 0 1 2 3 4 5; do
            f=$(printf '%s/gx-%06u.bin' "$dump" $((freeframe + map_dump + d)))
            if [ -f "$f" ]; then mapfile="$f"; break; fi
        done
        if [ -n "$mapfile" ]; then
            say "Karte offen (Z, Bild $((freeframe + map_dump))): $(basename "$mapfile" .bin).png"
        else
            say "Karte offen: kein GX-Bild aufgezeichnet"
        fi
    fi
    # and frames asked for by number (RE4_GX_KEEP, default: 901, the opening
    # event's view of the car from above with its shadow on the ground)
    local wanted=""
    for n in ${RE4_GX_KEEP:-901}; do
        f=$(printf '%s/gx-%06u.bin' "$dump" "$n")
        [ -f "$f" ] && wanted="$wanted $f"
    done
    keep=$( (ls "$dump"/gx-*.bin | head -3; ls "$dump"/gx-*.bin | sed -n '4,99p' |
        while read -r f; do [ "$(stat -c %s "$f")" -gt 2000000 ] && echo "$f"; done | head -1; echo "$mapfile";
        for f in $wanted; do echo "$f"; done) |
        grep . | sort -u)
    python3 - "$run/gx-daten.zip" $keep <<'PY'
import sys, zipfile
from pathlib import Path
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for f in sys.argv[2:]:
        z.write(f, Path(f).name)
PY
    rm -rf "$dump"  # large; the pictures and gx-daten.zip stay
    say "GX-Bilder: $(ls "$pics"/*.png 2> /dev/null | wc -l) PNG-Dateien im Ordner bilder-gx (bitte mitschicken)"
    say "GX-Daten: gx-daten.zip ($(du -h "$run/gx-daten.zip" | cut -f1), $(echo "$keep" | wc -w) Bilder als Daten; bitte mitschicken)"
}

# ---- the movies of the disc through the CRI middleware (M9) --------------------------------
# Every .sfd file of both discs is played to its end the way game/sofdec.cpp
# plays it (re4_host --movie-test, tests/movie_probe.cpp), in fast mode
# (RE4_VI_FAST: the fields at once, the CRI clock counts them; at most
# RE4_CHECK_MOVIE_MINUTES minutes of movie, default 15): the player's status
# at the end (3 = played to its end), the frames, frames the player skipped,
# CRI errors, the time one step takes; a still every 30 s as PNG
# (bilder-film/) and the sound of the opening movie as WAV (film-*.wav).
run_movies() {
    local dir="$src/port/build/whole-$host$bsuf" mins="${RE4_CHECK_MOVIE_MINUTES:-15}"
    section "6. Filme der Disc über die CRI-Middleware (ganz, im Schnelllauf)"
    if [ ! -x "$dir/re4_host" ]; then
        say "FEHLER: $dir/re4_host fehlt (Build fehlgeschlagen?)"
        overall=1
        return
    fi
    local stills="$run/bilder-film" bad=0 n=0 disc f name rel log line
    rm -rf "$stills"
    mkdir -p "$stills"
    for disc in 0 1; do
        [ -d "$layout/disc$disc/files" ] || continue
        while IFS= read -r f; do
            rel="${f#"$layout/disc$disc/files/"}"
            name="d$((disc + 1))-$(basename "$f" | sed 's/\.[sS][fF][dD]$//')"
            log="$run/film-$name.log"
            n=$((n + 1))
            (cd "$dir" && exec env RE4_MEMCARD=none RE4_MOVIE_DISC="$disc" RE4_VI_FAST=1 RE4_MOVIE_FAST=1 RE4_MOVIE_EVERY=1800 \
                timeout 1200 ./re4_host --data "$layout" --movie-test "$rel" "$run/film.yuv" \
                "$run/film-$name.wav" $((mins * 3600))) > "$log" 2>&1
            local code=$?
            line=$(grep -m1 "\[movie\] end:" "$log" | sed 's/^.*\[movie\] end: //')
            say "$name ($rel): exit $code, ${line:-kein Ende gemeldet}"
            grep -m1 "\[movie\] step" "$log" | sed 's/^.*\[movie\] /  /' | tee -a "$report"
            grep "CRI error\|last CRI error\|fatal signal" "$log" | head -3 | sed 's/^.*\[movie\] /  /;s/^\[re4\] /  /' | tee -a "$report"
            grep -o "Stub aufgerufen: .*\|stub called: .*" "$log" | sort -u | head -5 | sed 's/^/  /' | tee -a "$report"
            if [ -f "$run/film.yuv" ]; then
                python3 "$src/port/tools/movie_report.py" --sparse "$run/film.yuv" "$stills" "$name" 2>&1 | sed 's/^/  /' | tee -a "$report"
                rm -f "$run/film.yuv"
            fi
            if [ "$code" -ne 0 ]; then
                bad=1
            fi
            # the sound of the opening movie is kept (with its levels), the others not (size)
            if [ "$(basename "$f" | tr 'A-Z' 'a-z')" = "opening.sfd" ]; then
                python3 "$src/port/tools/audio_wav_report.py" "$run/film-$name.wav" 2>&1 | head -8 | sed 's/^/  /' | tee -a "$report"
            else
                rm -f "$run/film-$name.wav"
            fi
        done < <(find -L "$layout/disc$disc/files" -iname '*.sfd' | sort)
    done
    if [ "$n" -eq 0 ]; then
        say "Keine .sfd-Dateien gefunden."
        bad=1
    fi
    rm -f "$run/bilder-film.zip"
    (cd "$run" && python3 -m zipfile -c bilder-film.zip bilder-film)
    say "Standbilder: $(ls "$stills"/*.png 2> /dev/null | wc -l) PNG-Dateien im Ordner bilder-film, gepackt als bilder-film.zip ($(du -h "$run/bilder-film.zip" 2> /dev/null | cut -f1); bitte mitschicken)"
    [ "$bad" -ne 0 ] && overall=1
}

# ---- the memory card (M10): system file, save, a new start, load ---------------------------
# Slot A holds a card file of this run (runtime/memcard.cpp), formatted first
# by the SDK's card library (re4_host --memcard-format). The game puts the
# cursor of its yes/no questions on the second choice (mostly "no");
# RE4_TEST_CARD_YES=1 keeps it on the first, so A alone says yes. Run A: the
# boot check finds no system file and makes one; new game into r100 (debug
# start), once the player is free the save screen of a typewriter opens
# (RE4_TEST_SAVE, after setting the brightness option to 72; the default is
# 64), A picks file 1 and confirms, START closes "saved"; the run ends when
# the screen has closed. Run B, a new start with the same card: the boot
# check reads the system file (brightness 72 again), the title menu stands on
# "load" (no debug start), A loads file 1, the game continues in r100; once
# the player is free, "retry from a continue point" (RE4_TEST_CONTINUE,
# GameContinue as the pause menu calls it, also after a load there) must
# bring r100 back (it hung before patch 0059). Each
# run ends at the latest at a fixed frame (RE4_STOP_AT_FRAME) or after
# RE4_CHECK_CARD_LIMIT seconds, or when the log stands still for
# RE4_CHECK_CARD_STALL seconds (with a backtrace); every 30 s the last step is shown. The card is
# listed afterwards (port/tools/memcard_list.py: files, banner/icon, CRCs).
card_progress() {
    local frame step
    frame=$(grep -o "pad: frame [0-9]*" "$1" 2> /dev/null | tail -n 1 | grep -o "[0-9]*$")
    step=$(grep -E "card: |run: (memory card check|save |main loop|player free|stopped)" "$1" 2> /dev/null |
        tail -n 1 | sed 's/^\[re4\] //')
    echo "Taste zuletzt in Bild ${frame:-?}; ${step:-noch kein Kartenschritt}"
}
run_card_game() {
    local name="$1" limit="$2" dir="$3" log="$run/$1.log"
    shift 3
    (cd "$dir" && exec env RE4_VI_FAST=1 RE4_MEMCARD="$card" RE4_PAD_UNTIL_ROOM=1 RE4_TEST_CARD_YES=1 "$@" \
        "${CARD_RUNNER[@]}" ./re4_host --data "$layout") > "$log" 2>&1 &
    local pid=$! start=$SECONDS last_note=$SECONDS reason="" size last_size=-1 last_change=$SECONDS
    local stall=$((${RE4_CHECK_CARD_STALL:-120} * CARD_SLOW))
    while kill -0 "$pid" 2> /dev/null; do
        sleep 2
        size=$(stat -c %s "$log" 2> /dev/null || echo 0)
        if [ "$size" != "$last_size" ]; then
            last_size=$size
            last_change=$SECONDS
        fi
        if [ $((SECONDS - last_note)) -ge 30 ]; then
            echo "  ... $((SECONDS - start)) s: $(card_progress "$log")"
            last_note=$SECONDS
        fi
        if [ $((SECONDS - last_change)) -ge "$stall" ]; then
            reason="STILLSTAND: seit $stall s keine neue Zeile im Log"
        elif [ $((SECONDS - start)) -ge "$limit" ]; then
            reason="ABBRUCH nach $limit s"
        fi
        if [ -n "$reason" ]; then
            # backtrace of the running context into the log (host/main.cpp), then stop
            kill -USR1 "$pid" 2> /dev/null
            sleep 3
            kill -KILL "$pid" 2> /dev/null
            break
        fi
    done
    wait "$pid" 2> /dev/null
    local code=$?
    [ -n "$reason" ] && say "  $reason (Log: $name.log)"
    grep -E "memcard: (files|.* saved)|card: |run: (memory card check|save |main loop|player free|stopped|retry|after the retry)|fatal signal|stub called: (OSResetSystem|CARD|__CARD)" \
        "$log" | sed 's/^\[re4\] /  /' | head -n 80 | tee -a "$report"
    # what the game itself reported (pLog, patch 0076: "alloc[...]:free[...]" when a heap is full)
    if grep -q "game: " "$log"; then
        say "  Spielmeldungen (pLog):"
        grep "game: " "$log" | sed 's/^\[re4\] //' | sort | uniq -c | sort -rn | head -n 30 |
            sed 's/^/    /' | tee -a "$report"
    fi
    # a crash: the faulting place, registers and the call chain (host/main.cpp)
    if grep -q "fatal signal" "$log"; then
        say "  Absturzstelle und Aufrufkette:"
        sed -n '/fatal signal/,$p' "$log" | grep -v "pad: frame" | head -n 60 | sed 's/^/    /' | tee -a "$report"
    fi
    if [ -n "$reason" ] && grep -q "backtrace of the running context" "$log"; then
        say "  Rückverfolgung (wo das Programm hing):"
        sed -n '/backtrace of the running context/,$p' "$log" | grep -v "pad: frame" | head -n 60 |
            sed 's/^/    /' | tee -a "$report"
    fi
    [ -n "$reason" ] && return 124
    return $code
}
# the line the card run ended with, for the result
card_stop() {
    local line
    line=$(grep -m1 "run: stopped" "$1" 2> /dev/null | sed 's/^\[re4\] run: //')
    echo "${line:-kein regulärer Stopp}"
}
run_memcard() {
    local dir="$src/port/build/whole-$host$bsuf" card="$run/speicherkarte-a.bin" bad=0 code
    # the ARM targets (--armhf, --aarch64) run under qemu-user, with longer limits (as the tour)
    set_runner "$host"
    CARD_RUNNER=("${RUNNER[@]}")
    CARD_SLOW=1
    [ ${#CARD_RUNNER[@]} -gt 0 ] && CARD_SLOW=${RE4_TOUR_SLOW:-8}
    local limit=$((${RE4_CHECK_CARD_LIMIT:-900} * CARD_SLOW))
    section "7. Speicherkarte: Systemdatei anlegen, im Raum speichern, Neustart, im Titel laden"
    if [ ! -x "$dir/re4_host" ]; then
        say "FEHLER: $dir/re4_host fehlt (Build fehlgeschlagen?)"
        overall=1
        return
    fi
    rm -f "$card"
    if ! (cd "$dir" && "${CARD_RUNNER[@]}" ./re4_host --memcard-format "$card") > "$run/karte-formatieren.log" 2>&1; then
        say "FEHLER: die Karte ließ sich nicht formatieren (karte-formatieren.log)"
        tail -n 5 "$run/karte-formatieren.log" | sed 's/^/  /' | tee -a "$report"
        overall=1
        return
    fi
    say "Karte: speicherkarte-a.bin (2 MB, formatiert, leer)"
    say "Lauf A: Systemdatei anlegen, neues Spiel in r100, dort speichern (Datei 1)"
    # Discs without debug/roomInfo.dat (the European ones, patch 0119): no debug start in r100, the
    # new game stays in r120, and saving there never leaves the file list (the debug discs alike,
    # 26.10.2026). Leon is put at the door to r100 as in the game run, the save follows there.
    local card_save="free+60,72,stop" card_place="" card_free="A@90-600/30 START@900-3000/120"
    if [ -z "$(find "$layout"/disc0/files/Debug "$layout"/files/Debug -maxdepth 1 -iname roominfo.dat 2> /dev/null)" ]; then
        # the keys of the game run take him through (B at free+300, the room changes at once)
        card_save="free+700,72,stop"
        card_place="free+1:-82910,860,-38480,1.75"
        card_free="Z@30 B@300 A@760-1300/30 START@1600-3400/120"
        say "  (ohne debug/roomInfo.dat: neues Spiel in r120, an der Tür nach r100 abgesetzt, dort gespeichert)"
    fi
    run_card_game karte-speichern "$limit" "$dir" RE4_START_ROOM=0x100 RE4_ROOM_MENU=1 RE4_STOP_AT_FRAME=30000 \
        RE4_PAD_SCRIPT="A@100-60000/60 START@130-60000/60" RE4_TEST_SAVE="$card_save" RE4_TEST_PLACE="$card_place" \
        RE4_PAD_FREE_SCRIPT="$card_free"
    code=$?
    local made saved
    made=$(grep -c "card: boot check: making the system file" "$run/karte-speichern.log")
    saved=$(grep -c "card: save: saving (file 1" "$run/karte-speichern.log")
    if [ "$code" -ne 0 ] || ! grep -q "run: stopped after the save" "$run/karte-speichern.log"; then
        say "Ergebnis A: FEHLER (Exit-Code $code, $(card_stop "$run/karte-speichern.log"); Log: karte-speichern.log)"
        bad=1
    elif [ "$made" -eq 0 ]; then
        say "Ergebnis A: FEHLER (die Startprüfung hat keine Systemdatei angelegt)"
        bad=1
    elif [ "$saved" -eq 0 ]; then
        say "Ergebnis A: FEHLER (der Speicherbildschirm hat Datei 1 nicht gespeichert)"
        bad=1
    else
        say "Ergebnis A: OK (Systemdatei angelegt, Datei 1 gespeichert, Bildschirm geschlossen)"
    fi
    if [ "$bad" -ne 0 ]; then
        say "Lauf B entfällt (ohne Spielstand wartete der Ladebildschirm ewig auf eine Taste)."
    else
        say "Lauf B: Neustart mit derselben Karte, im Titel laden, dann \"Retry from a continue point\""
        run_card_game karte-laden "$limit" "$dir" RE4_STOP_AFTER=2400 RE4_STOP_AT_FRAME=15000 \
            RE4_PAD_SCRIPT="A@100-60000/60 START@130-60000/60" RE4_TEST_CONTINUE=free+300
        code=$?
        local check loaded
        check=$(grep -m1 "run: memory card check done" "$run/karte-laden.log")
        loaded=$(grep -m1 "run: save loaded from the memory card" "$run/karte-laden.log")
        if [ "$code" -ne 0 ] || ! grep -q "run: stopped after .* frames from the main loop" "$run/karte-laden.log"; then
            say "Ergebnis B: FEHLER (Exit-Code $code, $(card_stop "$run/karte-laden.log"); Log: karte-laden.log)"
            bad=1
        elif [ -z "$loaded" ]; then
            say "Ergebnis B: FEHLER (kein Spielstand geladen)"
            bad=1
        elif ! echo "$check" | grep -q "brightness 72,"; then
            say "Ergebnis B: FEHLER (Optionen nicht aus der Systemdatei: ${check#*options })"
            bad=1
        elif ! grep -q "run: main loop reached .* room r100" "$run/karte-laden.log"; then
            say "Ergebnis B: FEHLER (das geladene Spiel lief nicht in r100 weiter)"
            bad=1
        elif ! grep -q "run: after the retry: room r100 runs again" "$run/karte-laden.log"; then
            say "Ergebnis B: FEHLER (nach \"Retry from a continue point\" lief r100 nicht wieder an)"
            bad=1
        else
            say "Ergebnis B: OK (Optionen aus der Systemdatei, Spielstand geladen, r100 läuft, auch nach \"Retry\")"
        fi
    fi
    say "Inhalt der Karte (port/tools/memcard_list.py):"
    python3 "$src/port/tools/memcard_list.py" "$card" --expect-system --expect-save 0 2>&1 | sed 's/^/  /' | tee -a "$report"
    [ "${PIPESTATUS[0]}" -ne 0 ] && bad=1
    [ "$bad" -ne 0 ] && overall=1
}

# ---- a round of rooms (--rooms): every room by the debug start, crashes, stand-stills, NaN ------
# Each room as the game run above starts it (debug start from the title, RE4_START_ROOM): the
# room loads, its opening event runs, then (once Leon is free) the tour's keys; the run ends after
# --tour-frames frames of the main loop. --tour-jobs rooms run at once, each in a process of its
# own; port/tools/room_tour_report.py reads their logs (bug hunt, part 2, 22.10.2026).
# the rooms of LIST ("206,300", "set", "all", or mixed: "222,set"), each once, in order
tour_list() {
    local item
    for item in $(echo "$1" | tr ',;' '  '); do
        tour_list_item "$item"
    done | tr ' ' '\n' | awk 'NF && !seen[$0]++' | tr '\n' ' '
}

tour_list_item() {
    case "$1" in
    set)
        # two rooms of each room program (the first and one further in)
        echo "100 120 101 105 10c 119 10f 117 200 207 204 206 211 216 21a 223 22c 300 305 30d 311 315 31a 320 329 400 409"
        ;;
    all)
        python3 - "$src/src/game/roomdata.cpp" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
rooms = []
for st in range(1, 5):
    m = re.search(r"ROOM_DATA St%d_data_tbl\[\d+\] = \{(.*?)\n\};" % st, text, re.S)
    if m:
        for i, rel in enumerate(re.findall(r"\{\d+, \d+, (\d+), [^}]*\}", m.group(1))):
            if int(rel):
                rooms.append("%x%02x" % (st, i))
print(" ".join(rooms))
PY
        ;;
    *) echo "$1" ;;
    esac
}

# The tour's keys once Leon is free (frames from then; runtime/pad.cpp, at most 64 entries):
# walk, A (a door, an item, a typewriter in front of him), turn, run, walk on, aim and fire five
# shots while turning, the map (Z) and back (B), walk backwards and in a curve, the knife (L, A),
# Y and START with B after each; the whole block twice. What a player does in his first minute
# in a room, without steering anywhere: the room's enemies, events and areas react to it.
tour_keys() {
    local block="STICKUP@0-240/1 A@120 STICKRIGHT@240-300/1 STICKUP+B@300-540/1 A@420 STICKLEFT@540-660/1
        STICKUP@660-840/1 A@800 R@900-1140/1 A@960-1140/40 STICKLEFT@1000-1060/1 Z@1220 B@1400
        STICKDOWN@1460-1580/1 STICKUP+STICKRIGHT@1600-1800/1 L@1850-2050/1 A@1900-2050/50 Y@2100 B@2250
        START@2320 B@2450"
    case "$tour_script" in
    none) echo "" ;;
    wander)
        local e out=""
        for e in $block; do out="$out $e"; done
        for e in $block; do
            # the same entry 2500 frames later
            out="$out $(echo "$e" | awk -F@ '{split($2, r, "/"); n = split(r[1], f, "-");
                s = f[1] + 2500; if (n > 1) s = s "-" f[2] + 2500; if (r[2] != "") s = s "/" r[2]; print $1 "@" s}')"
        done
        echo "$out"
        ;;
    *) echo "$tour_script" ;;
    esac
}

# Until Leon is free (frames from the room start): from frame 1500 on A every second, A+B and L+R
# (the reaction keys of events) and now and then START (skips a cutscene). Rooms that wait for a
# key (a radio call or hint left open, a reaction in an event, the Mercenaries menu) get on; the
# first i686 tour (22.10.2026) found 15 rooms where Leon was never free without them.
tour_room_keys() {
    [ "$tour_script" = wander ] && echo "A@1500-9000/60 A+B@1530-9000/120 L+R@1590-9000/240 START@1545-9000/600"
}

# The programs of the ARM targets run under qemu-user (with the C library of the cross compiler).
# aarch64 with a guest address space of 4 GB (QEMU_RESERVED_VA): without it qemu's own mappings
# leave holes above 4 GB that re4_host cannot reserve (in WSL 22.10.2026: 53919 pages), and a
# module loaded later in the game landed there (em23 in r100; re4_check_game_address stops).
set_runner() {
    RUNNER=()
    case "$1" in
    armhf) RUNNER=(env QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf qemu-arm) ;;
    aarch64) RUNNER=(env QEMU_LD_PREFIX=/usr/aarch64-linux-gnu QEMU_RESERVED_VA=0x100000000 qemu-aarch64) ;;
    esac
}

# one room: tour_room DIR ROOM FOLDER; leaves rROOM.log and rROOM.meta in FOLDER
tour_room() {
    local dir="$1" r="$2" tdir="$3"
    local log="$tdir/r$r.log" trace="$tdir/r$r.jsonl" start=$SECONDS reason=""
    # limits (seconds) times TOUR_SLOW under qemu: no new frame, no main loop, the whole run
    local stall=$((${RE4_TOUR_STALL:-90} * TOUR_SLOW)) main_limit=$((${RE4_TOUR_MAIN_LIMIT:-300} * TOUR_SLOW))
    local limit=$((${RE4_TOUR_LIMIT:-900} * TOUR_SLOW))
    (cd "$dir" && exec env RE4_VI_FAST=1 RE4_WAIT_TRACE=1 RE4_START_ROOM="0x$r" RE4_PAD_UNTIL_ROOM=1 \
        RE4_MEMCARD=none RE4_ROOM_MENU=1 RE4_PAD_SCRIPT="$pad_script" RE4_PAD_ROOM_SCRIPT="$TOUR_ROOM_KEYS" \
        RE4_PAD_FREE_SCRIPT="$TOUR_KEYS" RE4_STOP_AFTER="$tour_frames" RE4_STOP_AT_FRAME=$((tour_frames + 8000)) \
        RE4_TRACE="$trace" \
        "${RUNNER[@]}" ./re4_host --data "$layout") > "$log" 2>&1 &
    local pid=$! size last_size=-1 last_change=$SECONDS
    while kill -0 "$pid" 2> /dev/null; do
        sleep 2
        size=$(stat -c %s "$trace" 2> /dev/null || echo 0)
        if [ "$size" != "$last_size" ]; then
            last_size=$size
            last_change=$SECONDS
        fi
        if [ $((SECONDS - last_change)) -ge "$stall" ]; then
            reason="STILLSTAND (seit $stall s kein neues Bild)"
        elif [ $((SECONDS - start)) -ge "$main_limit" ] && ! grep -q "run: main loop reached" "$log"; then
            reason="Hauptschleife nach $main_limit s nicht erreicht"
        elif [ $((SECONDS - start)) -ge "$limit" ]; then
            reason="Zeitlimit $limit s"
        fi
        if [ -n "$reason" ]; then
            # backtrace of the running context and the threads' waits into the log, then stop
            kill -USR1 "$pid" 2> /dev/null
            sleep $((3 * TOUR_SLOW))
            kill -KILL "$pid" 2> /dev/null
            break
        fi
    done
    wait "$pid" 2> /dev/null
    local code=$? secs=$((SECONDS - start))
    printf 'room=%s\ncode=%s\nreason=%s\nsecs=%s\nstate=%s\n' "$r" "$code" "$reason" "$secs" \
        "$(trace_state "$trace")" > "$tdir/r$r.meta"
    rm -f "$trace"
    echo "  r$r: Exit-Code $code, $secs s${reason:+, $reason}"
}

run_tour() {
    local dir="$src/port/build/whole-$host$bsuf" list count
    list=$(tour_list "$rooms" | tr 'A-F' 'a-f')
    count=$(echo $list | wc -w)
    [ -z "$tour_jobs" ] && tour_jobs=$(($(nproc) / 2))
    [ "$tour_jobs" -lt 1 ] && tour_jobs=1
    set_runner "$host"
    TOUR_SLOW=1
    [ ${#RUNNER[@]} -gt 0 ] && TOUR_SLOW=${RE4_TOUR_SLOW:-8}
    TOUR_KEYS=$(tour_keys)
    TOUR_ROOM_KEYS=$(tour_room_keys)
    local what="Leon läuft, zielt, schießt und drückt A (sobald er frei ist; vorher ab Bild 1500 A, A+B, L+R, START)"
    [ -z "$TOUR_KEYS" ] && what="ohne Eingaben"
    [ "$tour_script" != wander ] && [ -n "$TOUR_KEYS" ] && what="Eingaben: $TOUR_KEYS"
    section "8. Raum-Rundgang ($host${RE4_SANITIZE:+, Sanitizer $RE4_SANITIZE}): $count Räume, je $tour_frames Bilder ab der Hauptschleife, $tour_jobs gleichzeitig; $what"
    if [ ! -x "$dir/re4_host" ]; then
        say "FEHLER: $dir/re4_host fehlt (Build fehlgeschlagen?)"
        overall=1
        return
    fi
    if [ ${#RUNNER[@]} -gt 0 ] && ! command -v "${RUNNER[-1]}" > /dev/null; then
        say "FEHLER: ${RUNNER[-1]} fehlt (sudo $src/port/tools/whole_setup_linux.sh)"
        overall=1
        return
    fi
    local tdir="$run/rundgang" r
    rm -rf "$tdir"
    mkdir -p "$tdir"
    echo "  (ein Raum nach dem anderen fertig:)"
    for r in $list; do
        while [ "$(jobs -rp | wc -l)" -ge "$tour_jobs" ]; do
            wait -n 2> /dev/null || sleep 1
        done
        tour_room "$dir" "$r" "$tdir" &
    done
    wait
    local json="$run/rundgang.json" code
    python3 "$src/port/tools/room_tour_report.py" "$tdir" --rooms "$list" --details "$run/rundgang-einzelheiten.txt" \
        --json "$json" | tee -a "$report"
    code=${PIPESTATUS[0]}
    [ "$code" -ne 0 ] && overall=1
    # the results of each build stay in the work folder, room by room (a tour of a few rooms
    # replaces only those); a tour of another target is compared with the i686 results (same keys,
    # so the same rooms should give the same result)
    # (per release: the European discs' results in rundgang-<target>-eu.json, compared with the
    # debug discs' of the same target: the game's messages that only one release gives as well)
    local rel_suffix="" cheats=""
    [ "$release" = retail-eu ] && rel_suffix="-eu"
    [ "$release" = retail-us ] && rel_suffix="-us"
    # a tour with the cheats of the app's menu (RE4_CHEATS=<bits>, runtime/cheats.cpp) keeps its
    # results apart as well (-cheatsN) and is compared with the tour without them
    [ "${RE4_CHEATS:-0}" != 0 ] && cheats="-cheats$RE4_CHEATS"
    python3 "$src/port/tools/room_tour_report.py" --merge "$json" "$work/rundgang-$host$bsuf$rel_suffix$cheats.json"
    local ref="$work/rundgang-i686$opt$rel_suffix$cheats.json"
    if [ "$host$bsuf" != "i686$opt" ] && [ -f "$ref" ]; then
        python3 "$src/port/tools/room_tour_report.py" --compare "$ref" "$json" --names "i686,$host$bsuf" |
            tee -a "$report"
    fi
    ref="$work/rundgang-$host$bsuf$cheats.json"
    if [ -n "$rel_suffix" ] && [ -f "$ref" ]; then
        python3 "$src/port/tools/room_tour_report.py" --compare "$ref" "$json" --names "debug,${rel_suffix#-}" --messages |
            tee -a "$report"
    fi
    ref="$work/rundgang-$host$bsuf$rel_suffix.json"
    if [ -n "$cheats" ] && [ -f "$ref" ]; then
        python3 "$src/port/tools/room_tour_report.py" --compare "$ref" "$json" --names "ohne,cheats" |
            tee -a "$report"
    fi
    if [ -s "$run/rundgang-einzelheiten.txt" ]; then
        say "Einzelheiten (rundgang-einzelheiten.txt, die ersten 400 Zeilen):"
        head -n 400 "$run/rundgang-einzelheiten.txt" | tee -a "$report"
    fi
    # all logs packed (rundgang-logs.tar.gz); those of the rooms with errors also stay as they are
    (cd "$tdir" && tar -czf ../rundgang-logs.tar.gz r*.log && rm -f r*.meta) 2> /dev/null
    python3 - "$json" "$tdir" <<'PY'
import json, sys
from pathlib import Path
bad = {r["room"] for r in json.load(open(sys.argv[1])) if r["errors"]}
for p in Path(sys.argv[2]).glob("r*.log"):
    if p.stem[1:] not in bad:
        p.unlink()
PY
}

# ---- the reload check (--reload W): weapon W in r100 (M11) ---------------------------------
# The debug start into r100; once the player is free (the opening event and
# the radio call over), weapon W is equipped the way the debug option tool
# does it (RE4_TEST_WEAPON, state_trace.cpp; endless ammo, so a reload is
# always possible), then two reloads (frames from the moment he is free):
#   1  aim held through it: R from 100 to 380, a shot (A) at 140, the
#      reload (B with R held) at 200
#   2  aim let go early: R from 460 to 585, a shot at 500, the reload at
#      560, R released 25 frames into it
# The Super Magnum (15) reloaded wrongly on the Retroid (17.10.2026): the
# rounds flew into the gun while Leon's left hand stayed down. One "wep:"
# line per frame in nachladen.log; the report sums each reload up (how long
# the player's reload motion ran, how close his left hand came to the gun,
# what the weapon object did) and has GX pictures of both reloads.
run_reload() {
    local dir="$src/port/build/whole-$host$bsuf" log="$run/nachladen.log" trace="$run/nachladen.jsonl"
    local dump="$run/gx-$host"
    section "8. Nachlade-Prüfung: Waffe $reload_weapon in r100, mit gehaltenem und mit losgelassenem Zielen"
    if [ ! -x "$dir/re4_host" ]; then
        say "FEHLER: $dir/re4_host fehlt (Build fehlgeschlagen?)"
        overall=1
        return
    fi
    local script="R@100-380/1 A@140 B@200 R@460-585/1 A@500 B@560"
    local shots="190,205,220,235,250,265,280,300,320,345,550,565,575,585,595,610,625,640,660"
    rm -rf "$dump"
    mkdir -p "$dump"
    local start=$SECONDS
    (cd "$dir" && exec env RE4_VI_FAST=1 RE4_WAIT_TRACE=1 RE4_START_ROOM=0x100 RE4_PAD_UNTIL_ROOM=1 \
        RE4_MEMCARD=none RE4_ROOM_MENU=1 RE4_PAD_SCRIPT="$pad_script" RE4_PAD_FREE_SCRIPT="$script" \
        RE4_TEST_WEAPON="free+20:$reload_weapon,stop=760" RE4_GX_DUMP="$dump" RE4_GX_DUMP_FREE="$shots" \
        RE4_STOP_AFTER=20000 RE4_TRACE="$trace" timeout 900 ./re4_host --data "$layout") > "$log" 2>&1
    local code=$?
    rm -f "$trace"
    say "  Lauf: Exit-Code $code, $((SECONDS - start)) s ($(grep -m1 'run: player free' "$log" | sed 's/.*\] //'))"
    grep -E "run: weapon|RE4_TEST_WEAPON|Wep[0-9]+ .*prolog|fatal signal|  pc 0x" "$log" | sed 's/^\[re4\] /  /' |
        head -n 12 | tee -a "$report"
    if ! grep -q "^\[re4\] wep: " "$log"; then
        say "FEHLER: keine Aufzeichnung der Waffe (wep:-Zeilen fehlen)"
        overall=1
        return
    fi
    # each reload: the frames with the player's routine in its reload state
    # (r_no_2 4 while r_no_1 is 6, the weapon routine)
    grep "^\[re4\] wep: " "$log" | sed 's/^\[re4\] wep: //' | awk -v free="$(grep -m1 'run: player free' "$log" |
        sed -n 's/.* at frame \([0-9]*\).*/\1/p')" '
        function flush() {
            if (!inr) return
            printf "  Nachladen %d: Bild %d..%d (frei+%d, %d Bilder), Leons Bewegung %s bis Bild %.1f (Länge %d), " \
                "Zielen %s; linke Hand zur Waffe: vor dem Nachladen %.0f, dabei %.0f..%.0f; Waffe: Zustand %s, " \
                "Bewegung %s bis Bild %.1f; danach Leon %s\n", count, first, last, first - free, last - first + 1,
                mot, maxf, len, released ? "losgelassen bei Bild " released : "gehalten", before, hmin, hmax,
                ostates, omot, omax, after
            inr = 0
        }
        {
            n = $1; split($3, r, "/"); routine = r[1] "/" r[2] "/" r[3] "/" r[4]
            split($6, f, "/"); kam = $8; hand = $NF
            reloading = (r[2] == 6 && r[3] == 4)
            if (reloading && !inr) {
                inr = 1; count++; first = n; mot = $5; maxf = 0; len = f[2]; released = 0
                hmin = 1e9; hmax = -1; before = lasthand; ostates = ""; omot = "-"; omax = 0
            }
            if (inr && !reloading) { after = routine; flush() }
            if (inr) {
                last = n
                if (f[1] + 0 > maxf) maxf = f[1] + 0
                if (kam == 0 && !released) released = n
                if (hand + 0 >= 0) { if (hand + 0 < hmin) hmin = hand + 0; if (hand + 0 > hmax) hmax = hand + 0 }
                split($13, o, "/"); st = o[1] "/" o[2]
                if (index(" " ostates " ", " " st " ") == 0) ostates = ostates (ostates == "" ? "" : " ") st
                if ($15 != "-" && omot == "-") omot = $15
                if ($16 + 0 > omax) omax = $16 + 0
            }
            if (!reloading) lasthand = hand
        }
        END { after = "(Ende der Aufzeichnung)"; flush(); if (!count) print "  kein Nachladen aufgezeichnet" }' |
        tee -a "$report"
    # the frames of each reload, every fourth (the whole record: nachladen.log)
    say "  Verlauf (jedes vierte Bild beim Nachladen; Spalten: Bild, Leon r_no 0/1/2/3, Bewegung, Bild/Länge,"
    say "  Zielen, Neigung | Waffe r_no, Bewegung, Bild | Abstand linke Hand zur rechten):"
    grep "^\[re4\] wep: " "$log" | sed 's/^\[re4\] wep: //' |
        awk '{split($3, r, "/"); if (r[2] == 6 && r[3] == 4) { if (k++ % 4 == 0) print "    " $0 } else k = 0}' |
        head -n 120 | tee -a "$report"
    render_gx_pictures
}

# ---- the 32/64-bit comparison (--compare, milestone 12) -----------------------------------
# Both builds play the same input (run_game: RE4_VI_FAST, the same test script, the disc
# reads complete at once), so their state traces should agree frame by frame; the first frame
# where the 64-bit run parts points at a 64-bit error (a structure read at GameCube offsets,
# a pointer cut short ...), long before it crashes or looks wrong on the device. At that frame
# both runs are made once more, up to it, with GX pictures of the frame before and the frame.
run_compare() {
    local t first code
    for t in i686 x86_64; do
        run_game "$t" "${RE4_CHECK_LIMIT:-3600}" "${RE4_CHECK_MAIN_LIMIT:-900}" "${RE4_CHECK_STALL:-180}"
    done
    section "9. Vergleichslauf 32/64 bit: i686 gegen x86_64, gleiche Tastenfolge, Start r$room, $frames Bilder"
    say "  (Gleitkomma beider über SSE; die float-Funktionen der C-Bibliothek über ihre double-Fassung,"
    say "  libre4_detmath.so; Debug-Anzeige des Spiels aus wie in der App, RE4_DEBUG_HUD=0: ihre"
    say "  Zeitbalken messen echte Zeit; Positionen bis 1 Einheit toleriert)"
    python3 "$src/port/tools/trace_compare.py" "$run/trace-i686.jsonl.gz" "$run/trace-x86_64.jsonl.gz" \
        --names i686,x86_64 --json "$run/vergleich.json" 2>&1 | tee -a "$report"
    code=${PIPESTATUS[0]}
    [ "$code" -ne 0 ] && overall=1
    first=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(",".join(map(str, d.get("gx_frames") or [])))' \
        "$run/vergleich.json" 2> /dev/null)
    # the pictures of both sides from their GX data (the frames RE4_GX_DUMP picks in the main runs,
    # and the frames around the first difference of the traces), compared pixel by pixel: what the
    # traces do not hold (texels, matrices, colours) shows there; for each picture that differs, the
    # first differing GX ops (gx_chunk_inspect.py diff), and the data of both sides go into
    # gx-vergleich-daten.zip
    local pics="$run/bilder-vergleich" tool_build="$src/port/build/app-host-gl" diffs="$run/bilder-verschieden.txt"
    rm -rf "$pics" "$diffs" "$run/gx-vergleich-daten.zip"
    mkdir -p "$pics/i686" "$pics/x86_64"
    if ! cmake -S "$src/port/android-decomp/app/src/main/cpp" -B "$tool_build" -G Ninja \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
        -DRE4_BUILD_GL_CHECK=ON > "$run/gx-werkzeug.log" 2>&1 ||
        ! ninja -C "$tool_build" ${RE4_JOBS:+-j "$RE4_JOBS"} gx_chunk_render >> "$run/gx-werkzeug.log" 2>&1; then
        say "  FEHLER: GX-Bild-Werkzeug nicht gebaut (Log: gx-werkzeug.log)"
        overall=1
        return
    fi
    if [ -n "$first" ]; then
        local last=${first##*,}
        say "GX-Daten am ersten Unterschied (und am ersten im Raum, Schritt 3; Bilder $first): je ein kurzer Lauf bis dorthin"
        for t in i686 x86_64; do
            local dir="$src/port/build/whole-$t$bsuf" dump="$run/gx-vergleich-$t"
            rm -rf "$dump"
            mkdir -p "$dump" "$run/gx-$t"
            set_game_env "$t"
            (cd "$dir" && exec env "${GAME_ENV[@]}" RE4_GX_DUMP="$dump" RE4_GX_DUMP_AT="$first" \
                RE4_STOP_AT_FRAME=$((last + 3)) timeout 1800 ./re4_host --data "$layout") > "$run/vergleich-$t.log" 2>&1
            if ls "$dump"/*.bin > /dev/null 2>&1; then
                mv -f "$dump"/*.bin "$run/gx-$t"/
            else
                say "  $t: kein GX-Bild aufgezeichnet (Log: vergleich-$t.log)"
            fi
            rm -rf "$dump"
        done
    fi
    say "Bilder beider Seiten aus ihren GX-Daten, Pixel für Pixel verglichen:"
    for t in i686 x86_64; do
        if ls "$run/gx-$t"/*.bin > /dev/null 2>&1; then
            "$tool_build/gx_chunk_render" "$pics/$t" "$run/gx-$t"/*.bin > "$run/bilder-vergleich-$t.txt" \
                2>> "$run/gx-werkzeug-lauf.log"
            ppm_to_png "$pics/$t"
        else
            say "  $t: keine GX-Daten aufgezeichnet"
        fi
    done
    python3 "$src/port/tools/picture_compare.py" "$pics/i686" "$pics/x86_64" --names i686,x86_64 \
        --list "$diffs" 2>&1 | tee -a "$report"
    [ "${PIPESTATUS[0]}" -ne 0 ] && overall=1
    if [ -s "$diffs" ]; then
        local stem kept=()
        for stem in $(head -n 6 "$diffs"); do
            if [ -f "$run/gx-i686/$stem.bin" ] && [ -f "$run/gx-x86_64/$stem.bin" ]; then
                say "  $stem, GX-Daten i686 / x86_64:"
                python3 "$src/port/tools/gx_chunk_inspect.py" diff "$run/gx-i686/$stem.bin" "$run/gx-x86_64/$stem.bin" \
                    --lines 10 2>&1 | sed 's/^/    /' | tee -a "$report"
                kept+=("$stem")
            fi
        done
        python3 - "$run" "${kept[@]}" <<'PY'
import sys, zipfile
from pathlib import Path
run = Path(sys.argv[1])
with zipfile.ZipFile(run / "gx-vergleich-daten.zip", "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for stem in sys.argv[2:]:
        for t in ("i686", "x86_64"):
            z.write(run / f"gx-{t}" / f"{stem}.bin", f"{t}/{stem}.bin")
PY
        say "  GX-Daten der verschiedenen Bilder: gx-vergleich-daten.zip ($(du -h "$run/gx-vergleich-daten.zip" | cut -f1); bitte mitschicken)"
    fi
    rm -rf "$run/gx-i686" "$run/gx-x86_64"  # large; the pictures and the zip stay
    if [ -s "$diffs" ]; then
        say "Bilder: Ordner bilder-vergleich (i686/ und x86_64/, bitte mitschicken)"
    else
        say "Bilder: Ordner bilder-vergleich (i686/ und x86_64/; alle gleich, nicht nötig mitzuschicken)"
    fi
}

if [ "$game_ok" -eq 1 ] && [ "$compare" -eq 1 ]; then
    run_compare
elif [ "$game_ok" -eq 1 ] && [ -n "$reload_weapon" ]; then
    run_reload
elif [ "$game_ok" -eq 1 ] && [ -n "$rooms" ]; then
    run_tour
elif [ "$game_ok" -eq 1 ] && [ "$card_only" -eq 1 ]; then
    run_memcard
elif [ "$game_ok" -eq 1 ]; then
    # limits in seconds: overall, until the main loop, without a new frame
    run_game "$host" "${RE4_CHECK_LIMIT:-3600}" "${RE4_CHECK_MAIN_LIMIT:-600}" "${RE4_CHECK_STALL:-180}"
    render_pictures
    render_gx_pictures
    run_movies
    run_memcard
    [ "$arm" -eq 1 ] && run_game armhf 14400 5400 600
fi

section "Zusammenfassung"
if [ "$overall" -eq 0 ]; then
    say "ALLES OK"
else
    say "NICHT BESTANDEN (Einzelheiten oben)"
fi
say "Logs und Trace: $run"
if [ -n "$out" ]; then
    mkdir -p "$out"
    copy_out
    echo "Bericht: $out/bericht.txt"
fi
exit $overall
