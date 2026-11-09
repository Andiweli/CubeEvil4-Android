#!/bin/bash
# Leon's walking with the stick on the PC (runtime/modern_move.cpp, patch 0227; decision of the project owner,
# 10.11.2026): "Modern movement" and the stepless turning, from debug starts with the disc data.
#
#   port/tools/whole_move_check.sh DISC_DIR [OUT_DIR] [CASE ...]
#
# DISC_DIR: the extracted disc (debug or European), OUT_DIR: the runs (default /tmp/re4-move-check), the build from
# RE4_BUILD (default port/build/whole-i686-O2, port/tools/whole_build.sh). Cases (all without one):
#   turn_full   r105, classic, stick fully up and right: turns at 100 % (as the game)
#   turn_soft   r105, classic, a soft push up and right: turns slower (50..65 %)
#   right       r105, modern, stick right: Leon turns to the camera's right and walks there (routine 1)
#   back        r105, modern, stick back: steps back (routine 2), his facing stays
#   aim         r105, aiming with R and the stick right: the same place and facing modern and classic, the
#               filter never changes the stick
#   run         r301 (castle), modern, B and stick left: runs (routine 3) the new way
#   castle      r200 (castle gate), modern, left then up
#   ashley      r20d, Ashley as the player (RE4_TEST_PLAYER=1), modern, stick right
#   gigante     r119, modern, the stick held through El Gigante's entrance: kept as the game reads it in the
#               event, the giant appears, Leon walks the new way in the fight
#   door        r105, modern, two turns left to the door to r107, A: the door opens (room r107)
# Exit code 0 when all asked cases pass.
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
disc=${1:?disc folder}
out=${2:-/tmp/re4-move-check}
shift $(( $# >= 2 ? 2 : 1 ))
build=${RE4_BUILD:-$root/port/build/whole-i686-O2}
cases=("$@")
[ ${#cases[@]} -eq 0 ] && cases=(turn_full turn_soft right back aim run castle ashley gigante door)
mkdir -p "$out"
failed=0

# run NAME ROOM FRAMES [VAR=VALUE ...]: a debug start into ROOM (the start menu's room choice), the log in OUT/NAME.log
run() {
    local name=$1 room=$2 frames=$3
    shift 3
    (cd "$build" && env RE4_VI_FAST=1 RE4_START_ROOM="0x$room" RE4_PAD_UNTIL_ROOM=1 RE4_MEMCARD=none RE4_ROOM_MENU=1 \
        RE4_PAD_SCRIPT="UP@100-60000/60 A@115-60000/60 START@130-60000/60" RE4_STOP_AFTER="$frames" \
        RE4_STOP_AT_FRAME=$((frames + 4000)) RE4_TRACE_MOVE=1 "$@" timeout 1200 ./re4_host --data "$disc") \
        > "$out/$name.log" 2>&1
}
# the log's lines from the moment the player is free
free() { awk '/player free/{f=1} f' "$out/$1.log"; }
verdict() {  # NAME OK TEXT
    if [ "$2" = 1 ]; then
        echo "  ok    $1: $3"
    else
        echo "  FAIL  $1: $3"
        failed=1
    fi
}
# Leon's place and facing every N frames (RE4_TRACE_POS), "frame x z facing routine"
places() { free "$1" | sed -nE 's/.*frame ([0-9]+), Leon at \(([-0-9]+) [-0-9]+ ([-0-9]+)\) facing ([-0-9.]+).*routine ([0-9]+\/[0-9]+).*/\1 \2 \3 \4 \5/p'; }

for c in "${cases[@]}"; do
    case $c in
    turn_full)
        run turn_full 105 1300 RE4_TRACE_POS=15 RE4_PAD_FREE_SCRIPT='STICKUP+STICKRIGHT@30-150/1'
        p=$(free turn_full | grep -m1 -oE 'turn at [0-9]+ %')
        verdict "$c" "$([ "$p" = "turn at 100 %" ] && echo 1)" "${p:-no turn}" ;;
    turn_soft)
        run turn_soft 105 1300 RE4_TRACE_POS=15 RE4_PAD_FREE_SCRIPT='STICKSOFTUPRIGHT@30-150/1'
        p=$(free turn_soft | grep -m1 -oE 'turn at [0-9]+' | grep -oE '[0-9]+$')
        verdict "$c" "$([ -n "$p" ] && [ "$p" -ge 50 ] && [ "$p" -le 65 ] && echo 1)" "turn at ${p:-?} %" ;;
    right)
        run right 105 1300 RE4_TEST_AIM=16 RE4_PAD_FREE_SCRIPT='STICKRIGHT@30-120/1'
        l=$(free right | grep 'move: Leon' | grep 'routine 1' | tail -1)
        f=$(echo "$l" | sed -nE 's/.*facing ([-0-9.]+), target ([-0-9.]+).*/\1 \2/p')
        verdict "$c" "$([ -n "$f" ] && [ "${f% *}" = "${f#* }" ] && echo 1)" "walks with facing = target (${f:-none})" ;;
    back)
        run back 105 1300 RE4_TEST_AIM=16 RE4_TRACE_POS=15 RE4_PAD_FREE_SCRIPT='STICKDOWN@30-120/1'
        b=$(places back | awk '$5 == "0/2"' | wc -l)
        moved=$(places back | awk '$5 == "0/2" {print $4}' | sort -u | wc -l)
        verdict "$c" "$([ "$b" -ge 3 ] && [ "$moved" = 1 ] && echo 1)" "$b samples stepping back, $moved facing(s)" ;;
    aim)
        run aim_modern 105 1300 RE4_TEST_AIM=16 RE4_TRACE_POS=10 RE4_PAD_FREE_SCRIPT='R@30-200/1 STICKRIGHT@60-120/1'
        run aim_classic 105 1300 RE4_TRACE_POS=10 RE4_PAD_FREE_SCRIPT='R@30-200/1 STICKRIGHT@60-120/1'
        same=$(diff <(places aim_modern) <(places aim_classic) > /dev/null && echo 1)
        n=$(free aim_modern | grep -c 'move: Leon')
        turned=$(places aim_classic | awk '{print $4}' | sort -u | wc -l)
        verdict "$c" "$([ "$same" = 1 ] && [ "$n" = 0 ] && [ "$turned" -gt 1 ] && echo 1)" \
            "places the same: ${same:-no}, stick changed $n times, the aim turned ($turned facings)" ;;
    run)
        run run 301 1300 RE4_TEST_AIM=16 RE4_PAD_FREE_SCRIPT='B+STICKLEFT@30-150/1'
        n=$(free run | grep 'move: Leon' | grep -c 'routine 3')
        verdict "$c" "$([ "$n" -ge 3 ] && echo 1)" "$n samples running the new way" ;;
    castle)
        run castle 200 1500 RE4_TEST_AIM=16 RE4_PAD_FREE_SCRIPT='STICKLEFT@30-90/1 STICKUP@100-200/1'
        n=$(free castle | grep 'move: Leon' | grep -c 'routine 1')
        verdict "$c" "$([ "$n" -ge 4 ] && echo 1)" "$n samples walking the new way" ;;
    ashley)
        run ashley 20d 1300 RE4_TEST_PLAYER=1 RE4_TEST_AIM=16 RE4_PAD_FREE_SCRIPT='STICKRIGHT@30-120/1'
        a=$(grep -c 'debug start with player type 1' "$out/ashley.log")
        n=$(free ashley | grep 'move: Leon' | grep -c 'routine 1')
        verdict "$c" "$([ "$a" -ge 1 ] && [ "$n" -ge 3 ] && echo 1)" "Ashley as the player: $a, $n samples walking" ;;
    gigante)
        run gigante 119 3600 RE4_TEST_AIM=16 RE4_TEST_PLACE=free+30:area=2 \
            RE4_PAD_FREE_SCRIPT='STICKRIGHT@20-2000/1 STICKLEFT@2000-2400/1'
        kept=$(free gigante | grep -c 'kept as the game reads it.*event 1')
        giant=$(grep -cE 'em: \+ s[0-9]+ 0x2b' "$out/gigante.log")
        after=$(awk '/em: \+ s[0-9]+ 0x2b/{f=1} f' "$out/gigante.log" | grep 'move: Leon' | grep -c 'routine 1')
        verdict "$c" "$([ "$kept" -ge 10 ] && [ "$giant" -ge 1 ] && [ "$after" -ge 5 ] && echo 1)" \
            "stick kept in the event ($kept lines), giant $giant, $after samples walking in the fight" ;;
    door)
        run door 105 1600 RE4_TEST_AIM=16 RE4_TEST_PLACE='free+20:-34356,0,-1736,1.30' \
            RE4_PAD_FREE_SCRIPT='STICKLEFT@40-60/1 STICKLEFT@80-99/1 A@100-200/8'
        n=$(free door | grep -c 'room: r107 runs')
        verdict "$c" "$([ "$n" -ge 1 ] && echo 1)" "through the door to r107: $n" ;;
    *)
        echo "unknown case $c"
        failed=1 ;;
    esac
done
exit $failed
