#!/usr/bin/env bash
# Builds the whole-program port for the Linux test targets and runs the checks:
#
#   port/tools/whole_build.sh            # i686 and armhf
#   port/tools/whole_build.sh i686       # one target
#   RE4_JOBS=4 port/tools/whole_build.sh # at most 4 compile jobs (memory)
#   RE4_OPT=2 port/tools/whole_build.sh  # optimised like the Android build
#                                        # (build folders whole-<target>-O2)
#   RE4_SANITIZE=address,undefined ...   # check build with sanitizers (CMakeLists.txt;
#                                        # build folders whole-<target>-O2-san), the checks
#                                        # below then run under them as well
#
# Per target: build (libre4_main.so, 114 module libraries, re4_host), then
#   whole_stub_check.py        the generated stubs as port/whole/stubs-expected.txt lists them
#   re4_host --self-test       cross-module trampolines pass arguments intact
#   re4_host --check-modules   every module loads, every cross-module import resolves
#   re4_host --runtime-test    OSLink, DVD folder and yz2 work without disc data
#   re4_host --core-test       the embedding interface of the app (core thread,
#                              controller, snapshots) without disc data, the
#                              memory card library on a card file
#   memcard_list.py            that card file read back (files, the game's CRCs),
#                              and the .gci the core test exported (GameCube byte order)
#   re4_core_remote_test       (32 bit) the core in a process of its own: the app's side
#                              against re4_coreproc (frames, sound, controller, card, a crash)
#   re4_probe                  (32 bit) the core without a picture (host/probe.cpp, the OUYA
#                              probe) with the entry of that check: frames, GX chunks, sound
#   save_layout.py --check     the byte-order table of the save files
#                              (runtime/save_layout.inc) still fits the game headers
#   re4_format_compare         the Formatumwandler against the app's checked
#                              big-endian readers (synthetic samples, then the
#                              same as a disc folder: test_whole_format_compare.py)
#   test_whole_movie.py        a synthetic Sofdec movie (ffmpeg) through the CRI
#                              middleware: frames against ffmpeg, pacing, sound
# With i686 and x86_64 in one call, test_movie_compare.py plays that movie on both and
# compares them frame by frame (pictures, sound, GP counts: the 32/64-bit comparison).
# Further targets: x86_64 and aarch64 (64 bit, milestone 12; aarch64 is the
# arm64-v8a code generation). armhf runs under qemu-arm, aarch64 under
# qemu-aarch64 when it is installed. Then the port's runtime files
# are compiled against libc++, the NDK's C++ library (whole_libcxx_check.py:
# what only the Android build would notice), and the disc folder tool is
# tested (port/tools/test_whole_disc_folder.py, synthetic images), as is
# the release comparison (test_retail_compare.py, synthetic folders). Needs
# clang, lld, cmake, ninja, python3, the lib32 C/C++ libraries,
# g++-arm-linux-gnueabihf and qemu-user: sudo port/tools/whole_setup_linux.sh
# (cloud sessions run it through .claude/hooks/session-start.sh).
set -u
root="$(cd "$(dirname "$0")/../.." && pwd)"
targets=("$@")
[ $# -eq 0 ] && targets=(i686 armhf)
status=0
# the runtime test checks the per-thread wait record of the hang search
export RE4_WAIT_TRACE=1
suffix="${RE4_OPT:+-O$RE4_OPT}${RE4_SANITIZE:+-san}"
if [ -n "${RE4_SANITIZE:-}" ]; then
    # no leak check (the game frees little before it ends), no ODR check (a module carries its own
    # copies of shared units, CMakeLists.txt -Bsymbolic), the port's own crash handler for SIGSEGV,
    # SIGBUS and SIGFPE (the core process test expects the signal), no fake stacks for returned
    # frames (11 MB for each of the game's stacks: the 32-bit address space ran out in the room tour,
    # r106); every report with its call chain
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:detect_odr_violation=0:detect_stack_use_after_return=0:handle_segv=0:handle_sigbus=0:handle_sigfpe=0}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:handle_segv=0:handle_sigbus=0:handle_sigfpe=0}"
fi
for t in "${targets[@]}"; do
    name="whole-$t$suffix"
    build="$root/port/build/$name"
    log="$root/port/build/$name.log"
    mkdir -p "$root/port/build"
    cmake -S "$root/port/whole" -B "$build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$root/port/whole/cmake/linux-$t.cmake" -DRE4_OPT_LEVEL="${RE4_OPT:-}" -DRE4_SANITIZE="${RE4_SANITIZE:-}" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > "$log" 2>&1 &&
        ninja -C "$build" -k 0 ${RE4_JOBS:+-j "$RE4_JOBS"} >> "$log" 2>&1
    code=$?
    failed=$(grep -c '^FAILED' "$log")
    errors=$(grep -E ': error:|fatal error:|ld.lld: error' "$log" | sort -u | wc -l)
    echo "$t${RE4_OPT:+ -O$RE4_OPT}${RE4_SANITIZE:+ sanitize=$RE4_SANITIZE}: $failed failed build steps, $errors distinct errors (log: port/build/$name.log)"
    grep -h '^link plan:' "$log" | tail -1 | sed 's/^/  /'
    if [ "$code" -ne 0 ] || [ "$failed" -ne 0 ] || [ "$errors" -ne 0 ]; then
        # a failed CMake configure (whole_prepare.py, patches) leaves the programs of
        # the build before: the checks below would test that old state
        [ "$failed" -eq 0 ] && [ "$errors" -eq 0 ] && { echo "  build stopped (exit code $code):"; tail -n 8 "$log" | sed 's/^/    /'; }
        status=1
        continue
    fi
    # the generated stubs against the list of the repository (port/whole/stubs-expected.txt)
    python3 "$root/port/tools/whole_stub_check.py" "$build/generated/link_report.json" || status=1
    # code built with the game's flags inside libre4_platform (re4_formats_obj) must not
    # call the file functions Android's C library wraps for _FORTIFY_SOURCE: the wrappers
    # call __open_real and friends, renames the prelude's empty __asm__ loses, and the
    # Android link fails (M10). Such code goes through runtime/host_file.cpp.
    fortified=$(find "$build/CMakeFiles/re4_formats_obj.dir" -name '*.o' -exec nm -u {} + 2> /dev/null |
        awk 'NF == 2 {print $2}' | sort -u |
        grep -xE 'open|open64|openat|openat64|creat|read|write|pread|pread64|pwrite|pwrite64|readlink|readlinkat|getcwd|umask|poll|ppoll|recv|recvfrom|send|sendto|strlcpy|strlcat' |
        tr '\n' ' ')
    if [ -n "$fortified" ]; then
        echo "  FEHLER: game-flag code in libre4_platform calls $fortified(Android fortify: use runtime/host_file.cpp)"
        status=1
    fi
    # aarch64: no fused multiply-add in the core (CMakeLists.txt -ffp-contract=off): float results
    # as on armhf and x86, where the instructions do not exist. Left out: the two functions that
    # call fmaf on purpose (the original's paired-single ps_madd, game/trans.cpp Sk1Loop and
    # game/math_sub.cpp PsSeries; fmaf rounds alike everywhere)
    if [ "$t" = aarch64 ]; then
        if command -v llvm-objdump > /dev/null; then
            fused=$(for so in "$build"/libre4_*.so "$build"/modules/*.so; do
                llvm-objdump -d --no-show-raw-insn "$so" 2> /dev/null; done |
                awk '/^[0-9a-f]+ <.*>:$/ {fn = $2} /\tf(n)?m(add|sub)\t/ && fn !~ /Sk1Loop|PsSeries/ {n++; print fn > "/dev/stderr"} END {print n + 0}' \
                2> "$build/fused-functions.txt")
            echo "  fused multiply-add in the core's libraries: $fused (must be 0; functions: $(basename "$build")/fused-functions.txt)"
            [ "$fused" -ne 0 ] && status=1
        else
            echo "  (llvm-objdump missing: fused multiply-add not counted)"
        fi
    fi
    run=()
    if [ "$t" = armhf ]; then
        if ! command -v qemu-arm > /dev/null; then
            echo "  (qemu-arm missing: armhf not executed)"
            continue
        fi
        run=(env QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf qemu-arm)
    fi
    if [ "$t" = aarch64 ]; then
        if ! command -v qemu-aarch64 > /dev/null; then
            echo "  (qemu-aarch64 missing: aarch64 not executed)"
            continue
        fi
        # a guest space of 4 GB, as in the room tour (whole_r100_check.sh set_runner): qemu's own
        # mappings leave holes above 4 GB that re4_host cannot reserve; with the UBSan runtime
        # the yz2 test got such memory (WSL, 22.10.2026)
        run=(env QEMU_LD_PREFIX=/usr/aarch64-linux-gnu QEMU_RESERVED_VA=0x100000000 qemu-aarch64)
    fi
    # the core test keeps the card of its memory card checks (RE4_CARD_TEST_FILE),
    # which port/tools/memcard_list.py then reads back
    card="$build/card-test.bin"
    gci="$build/card-test.gci"
    rm -f "$card" "$gci"
    for check in --self-test --check-modules --runtime-test --core-test; do
        out=$(cd "$build" && RE4_CARD_TEST_FILE="$card" RE4_GCI_TEST_FILE="$gci" timeout 300 "${run[@]}" ./re4_host "$check" 2>&1)
        code=$?
        echo "  re4_host $check: exit $code | $(echo "$out" | grep -E 'self-test:|check-modules:|runtime-test|core-test' | tr '\n' ' ' | sed 's/\[re4\] //g')"
        [ "$code" -ne 0 ] && status=1
    done
    instant_runner=""
    [ "$t" = armhf ] && instant_runner="$(command -v qemu-arm)"
    [ "$t" = aarch64 ] && instant_runner="$(command -v qemu-aarch64)"
    out=$(RE4_INSTANT_TEST_RUNNER="$instant_runner" "${run[@]}" "$build/re4_instant_store_test" 2>&1)
    code=$?
    echo "  instant slot file tests: exit $code | $out"
    [ "$code" -ne 0 ] && status=1
    # the GX decoder with its vertices on the core thread alone (RE4_GX_THREADS=0; the run above
    # puts them on 3 more threads): the same chunks byte for byte
    out=$(cd "$build" && RE4_GX_THREADS=0 timeout 300 "${run[@]}" ./re4_host --runtime-test 2>&1)
    code=$?
    echo "  re4_host --runtime-test, GX vertices on the core thread: exit $code | $(echo "$out" | grep -E 'runtime-test (gx|sections):|runtime-test: [0-9]+ failed' | tr '\n' ' ' | sed 's/\[re4\] //g')"
    [ "$code" -ne 0 ] && status=1
    out=$(python3 "$root/port/tools/memcard_list.py" "$card" --expect-system --expect-save 0 2>&1)
    code=$?
    echo "  memcard_list.py (the card of the core test): exit $code | $(echo "$out" | grep -cE '^  file ') files, $(echo "$out" | grep -oE 'CRC[^;,]*' | tr '\n' ' ')"
    [ "$code" -ne 0 ] && { echo "$out" | sed 's/^/    /'; status=1; }
    out=$(python3 "$root/port/tools/memcard_list.py" "$gci" --expect-save 0 2>&1)
    code=$?
    echo "  memcard_list.py (the .gci of the core test): exit $code | $(echo "$out" | grep -oE 'CRC[^;,]*|version [0-9a-fx]+|room r[0-9a-f]+' | tr '\n' ' ')"
    [ "$code" -ne 0 ] && { echo "$out" | sed 's/^/    /'; status=1; }
    # the core in a process of its own (32 bit, port/docs/32bit-kernprozess.md): the app's side
    # (re4_core_remote_test, MEM1 taken in its process) against the core process; the core
    # process runs through qemu-arm as well (the "app" starts it with --wrapper)
    if [ -x "$build/re4_core_remote_test" ]; then
        wrapper=()
        [ "$t" = armhf ] && wrapper=(--wrapper "$(command -v qemu-arm)")
        for mode in main --crash --missing; do
            args=(--program "$build/re4_coreproc" "${wrapper[@]}" --modules "$build/modules"
                --card "$build/remote-card.bin" --gci "$gci" --crash-file "$build/remote-crash.txt")
            [ "$mode" != main ] && args+=("$mode")
            out=$(cd "$build" && timeout 300 "${run[@]}" ./re4_core_remote_test "${args[@]}" 2>&1)
            code=$?
            echo "  re4_core_remote_test ${mode#--}: exit $code | $(echo "$out" | grep -E '^core-remote-test' | tr '\n' ' ')"
            [ "$code" -ne 0 ] && { echo "$out" | tail -n 30 | sed 's/^/    /'; status=1; }
        done
    fi
    # the core without a picture (host/probe.cpp, step O1 of port/docs/ouya-machbarkeit.md), with
    # the entry of the core process check instead of the game: frames counted, GX chunks taken
    # and copied, the sound read in real time, the summary line the PC script reads
    if [ -x "$build/re4_probe" ]; then
        out=$(cd "$build" && RE4_CORE_TEST_ENTRY=remote timeout 120 "${run[@]}" ./re4_probe --seconds 4 --report 2 2>&1)
        code=$?
        result=$(echo "$out" | grep -E '^probe-result: set=all ')
        value() { echo "$result" | grep -oE " $1=[0-9.]+" | cut -d= -f2 | cut -d. -f1; }
        frames=$(value frames)
        vertices=$(value vertices)
        sound=$(value sound_from_game_pct)
        echo "  re4_probe (test entry): exit $code | frames ${frames:-?}, fps $(value fps), vertices per chunk ${vertices:-?}, sound from the game ${sound:-?} %"
        if [ "$code" -ne 0 ] || [ "${frames:-0}" -lt 100 ] || [ "${vertices:-0}" -lt 1 ] || [ "${sound:-0}" -lt 50 ]; then
            echo "$out" | grep -v '^map: ' | tail -n 30 | sed 's/^/    /'
            status=1
        fi
    fi
    if command -v llvm-dwarfdump > /dev/null; then
        out=$(python3 "$root/port/tools/save_layout.py" "$build" --check 2>&1)
        code=$?
        echo "  save_layout.py --check: exit $code | $out"
        [ "$code" -ne 0 ] && status=1
    else
        echo "  (llvm-dwarfdump missing: save_layout.py --check not run)"
    fi
    out=$(cd "$build" && timeout 300 "${run[@]}" ./re4_format_compare 2>&1)
    code=$?
    echo "  re4_format_compare: exit $code | $(echo "$out" | grep '^Ergebnis:')"
    if [ "$code" -ne 0 ]; then
        echo "$out" | grep -vE '^  OK |^\[re4\]' | sed 's/^/    /'
        status=1
    fi
    out=$(python3 "$root/port/tools/test_whole_format_compare.py" "$build" "${run[@]}" 2>&1)
    code=$?
    echo "  re4_format_compare --data (synthetic disc folder): exit $code | $(echo "$out" | tail -1)"
    [ "$code" -ne 0 ] && { echo "$out" | sed 's/^/    /'; status=1; }
    # a synthetic Sofdec movie through the CRI middleware (needs ffmpeg)
    out=$(python3 "$root/port/tools/test_whole_movie.py" "$build" "${run[@]}" 2>&1)
    code=$?
    echo "  re4_host --movie-test (synthetic movie): exit $code | $(echo "$out" | grep -E '^  (frames|sound|A/V|GX picture[^:]*|fast mode \(RE4_VI_FAST, the PC check\)|60 Hz fields):' | sed 's/^  //' | tr '\n' ';') $(echo "$out" | tail -1)"
    [ "$code" -ne 0 ] && { echo "$out" | sed 's/^/    /'; status=1; }
done
# the comparison tool of the PC check (port/tools/trace_compare.py) on synthetic traces
out=$(python3 "$root/port/tools/test_trace_compare.py" 2>&1)
code=$?
echo "trace_compare self-test: exit $code | $(echo "$out" | tail -1)"
[ "$code" -ne 0 ] && { echo "$out" | grep -v "^ok" | sed 's/^/  /'; status=1; }
# and its picture and GX data comparisons (picture_compare.py, gx_chunk_inspect.py diff)
out=$(python3 "$root/port/tools/test_compare_tools.py" 2>&1)
code=$?
echo "compare tools self-test: exit $code | $(echo "$out" | tail -1)"
[ "$code" -ne 0 ] && { echo "$out" | grep -v "^ok" | sed 's/^/  /'; status=1; }
# the 32/64-bit comparison without game data (milestone 12): the synthetic movie on i686 and
# x86_64, frame by frame (port/tools/test_movie_compare.py), when both were built here
built=" ${targets[*]} "
if [[ "$built" == *" i686 "* && "$built" == *" x86_64 "* ]]; then
    out=$(python3 "$root/port/tools/test_movie_compare.py" "$root/port/build/whole-i686$suffix" \
        "$root/port/build/whole-x86_64$suffix" 2>&1)
    code=$?
    echo "movie comparison i686 / x86_64: exit $code | $(echo "$out" | grep -E 'Vergleich|ERSTES|stimmen|Ende' | sed 's/^ *//' | tr '\n' ';')"
    [ "$code" -ne 0 ] && status=1
fi
# the self-test of the 64-bit checks (port/tools/m12/selftest): every check finds exactly its
# examples; only where the libclang Python bindings are installed (the cloud)
if python3 -c "import clang.cindex" 2> /dev/null; then
    out=$(python3 "$root/port/tools/m12/selftest/run_selftest.py" 2>&1)
    code=$?
    echo "m12 check self-test: exit $code | $(echo "$out" | tail -1)"
    [ "$code" -ne 0 ] && { echo "$out" | grep -v ": ok" | sed 's/^/  /'; status=1; }
fi
first="$root/port/build/whole-${targets[0]}$suffix"
if [ -f "$first/compile_commands.json" ]; then
    python3 "$root/port/tools/whole_libcxx_check.py" "$first" || status=1
fi
if python3 "$root/port/tools/test_whole_disc_folder.py" > "$root/port/build/disc-folder-test.log" 2>&1; then
    echo "disc folder tool: ok"
else
    echo "disc folder tool: FAILED (log: port/build/disc-folder-test.log)"
    status=1
fi
if python3 "$root/port/tools/test_retail_compare.py" > "$root/port/build/retail-compare-test.log" 2>&1; then
    echo "retail compare tool: ok"
else
    echo "retail compare tool: FAILED (log: port/build/retail-compare-test.log)"
    status=1
fi
if python3 "$root/port/tools/test_emlist_dump.py" > "$root/port/build/emlist-dump-test.log" 2>&1; then
    echo "enemy list tool: ok"
else
    echo "enemy list tool: FAILED (log: port/build/emlist-dump-test.log)"
    status=1
fi
exit $status
