#!/usr/bin/env bash
# Real game pictures for the OUYA's GLES 2 renderer (port/ouya, stage O2), on the PC with the disc
# data (WSL): the game core plays r100 and the village r101 from the title screen (the test keys
# of port/tools/whole_r100_check.sh, RE4_VI_FAST) with the compact vertex (RE4_GX_COMPACT=1, as for
# the OUYA) and dumps GX frames (RE4_GX_DUMP: logos, title, room; three frames in a row after the
# player is free); gx_gl2_compare then draws each with the GLES 2 renderer and with the main app's
# GLES 3 one and compares them: as it runs, with the GLES 3 renderer's rounding (must be equal) and
# as on the Tegra 3.
#
#   port/ouya/tools/compare_frames.sh --data DIR [--build DIR] [--out DIR] [--frames N]
#
# --data   the disc data (port/tools/whole_r100_check.sh --data: the port's folder, an extracted
#          disc, the app's import folder); RE4-OUYA-Daten next to the OUYA clone has Disc 1
# --build  the core's i686 build with re4_host (port/tools/whole_build.sh i686, RE4_OPT=2):
#          default ~/re4-ouya/RE4/port/build/whole-i686-O2
# --out    default ~/re4-ouya/o2; the frames go to OUT/frames-compact (gx-r100-*.bin,
#          gx-r101-*.bin), the pictures to OUT/compare-*
# --frames frames of the main loop per room (default 6000)
set -u
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
data=""
build="$HOME/re4-ouya/RE4/port/build/whole-i686-O2"
out="$HOME/re4-ouya/o2"
frames=6000
while [ $# -gt 0 ]; do
    case "$1" in
    --data) data="$2"; shift 2 ;;
    --build) build="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --frames) frames="$2"; shift 2 ;;
    *) echo "unknown: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$data" ] || [ ! -x "$build/re4_host" ]; then
    echo "usage: $0 --data DIR [--build DIR] (re4_host in $build: port/tools/whole_build.sh i686)" >&2
    exit 2
fi
tools="$out/build"
cmake -S "$here/../app/src/main/cpp" -B "$tools" -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null &&
    cmake --build "$tools" --parallel 8 --target gx_gl2_compare > /dev/null || exit 1
frameDir="$out/frames-compact"
rm -rf "$frameDir"
mkdir -p "$frameDir"
status=0
for room in 100 101; do
    dump="$out/dump-r$room"
    rm -rf "$dump"
    mkdir -p "$dump"
    echo "r$room: the game with the compact vertex, $frames frames of the main loop ..."
    # r101 starts with Leon's binoculars and a manual page, sub screens that wait for B (as
    # port/scripts/ouya-probe.ps1); once free Leon walks (its key script)
    roomKeys=""
    [ "$room" = 101 ] && roomKeys="B@300-9000/60"
    (cd "$build" && exec env RE4_VI_FAST=1 RE4_WAIT_TRACE=1 RE4_START_ROOM="0x$room" RE4_PAD_UNTIL_ROOM=1 \
        RE4_MEMCARD=none RE4_ROOM_MENU=1 RE4_PAD_SCRIPT="UP@100-60000/60 A@115-60000/60 START@130-60000/60" \
        RE4_PAD_ROOM_SCRIPT="$roomKeys" \
        RE4_PAD_FREE_SCRIPT="STICKUP@30-400/1 STICKLEFT@400-440/1 STICKUP@440-900/1 STICKRIGHT@900-960/1 STICKUP@960-1500/1" \
        RE4_STOP_AFTER="$frames" RE4_TRACE="$dump/trace.jsonl" RE4_GX_DUMP="$dump" \
        RE4_GX_DUMP_FREE="150,151,152,600,1200" RE4_GX_DUMP_AT="3600,3601,3602,4800" \
        RE4_GX_COMPACT=1 ./re4_host --data "$data") > "$dump/game.log" 2>&1
    echo "  exit $?, $(ls "$dump"/gx-*.bin 2> /dev/null | wc -l) frames"
    for f in "$dump"/gx-*.bin; do
        [ -e "$f" ] && cp "$f" "$frameDir/gx-r$room-$(basename "$f" .bin | sed 's/^gx-//').bin"
    done
done
for mode in direct exact tegra3; do
    case "$mode" in
    direct) env=() ;;
    # the same arithmetic: off only where a tie x.5 of a linear read at a texel corner breaks
    # the other way (gx_gl2_check, "random tev, indirect")
    exact) env=(RE4_GX2_EXACT=1 RE4_GX2_ALLOWED=0.001) ;;
    tegra3) env=(RE4_GX2_TEGRA3=1 RE4_GX2_ALLOWED=0.05) ;;
    esac
    mkdir -p "$out/compare-$mode"
    rm -f "$out/compare-$mode"/*
    env "${env[@]}" "$tools/gx_gl2_compare" "$out/compare-$mode" $(ls "$frameDir"/gx-*.bin | sort) 2> /dev/null \
        > "$out/compare-$mode.txt"
    code=$?
    [ "$code" -ne 0 ] && status=1
    echo "$mode: $(tail -1 "$out/compare-$mode.txt") ($(grep -c '^FAIL' "$out/compare-$mode.txt") frames off)"
done
exit $status
