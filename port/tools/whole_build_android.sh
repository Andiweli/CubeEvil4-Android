#!/usr/bin/env bash
# Builds the game core for the Android app (the core mode of
# RE4-Android-Decomp) with the NDK and copies the libraries into the app:
#
#   port/tools/whole_build_android.sh [--ndk DIR] [--opt N] [--abi ABI] [--source REPO] [--work DIR] [--into APP]
#
#   --abi ABI      armeabi-v7a, arm64-v8a (milestone 12) or all (the default:
#                  both, one APK for 32- and 64-bit devices)
#   --source REPO  build the current commit of that checkout (for the Windows
#                  checkout under /mnt/...: cloned once to ~/re4-kern/RE4 and
#                  fetched, like whole_r100_check.sh; building on a Windows
#                  drive would be slow)
#   --work DIR     where --source is cloned (DIR/RE4; default ~/re4-kern, a
#                  relative DIR lies in the home folder): the OUYA checkout
#                  (port/scripts/ouya-probe.ps1) builds in ~/re4-ouya, apart
#                  from the app's
#   --into APP     copy the libraries into APP/src/core (default: the app of
#                  the built checkout); port/scripts/test-core-app.ps1 passes
#                  the Windows checkout's app folder
#   --ouya         the OUYA probe (port/docs/ouya-machbarkeit.md, step O1):
#                  Android 4.1 (android-16) with the NDK r23c
#                  (/opt/android-ndk-r23c, sudo port/tools/whole_setup_linux.sh
#                  --ouya), armeabi-v7a only, build folder whole-android-ouya;
#                  the libraries, the module libraries, libc++_shared.so and the
#                  probe re4_probe go flat into --stage DIR (default
#                  port/build/ouya-probe), not into the app
#
#   libre4_platform.so, libre4_main.so, libre4_<module>.so (114), libc++_shared.so,
#   for armeabi-v7a also libre4_coreproc.so (the core process, a program)
#     -> port/android-decomp/app/src/core/jniLibs/<abi>/   (not in git)
#
# The NDK: --ndk, else $ANDROID_NDK_HOME / $ANDROID_NDK_ROOT, else
# /opt/android-ndk-r28c (sudo port/tools/whole_setup_linux.sh --android
# installs it there). The app builds with NDK 28.2.13676358 (r28c); the core
# uses the same. Runs in Linux / WSL2 like whole_build.sh (python3, cmake,
# ninja). Optimisation -O2 (--opt 0 for a debug build); the same build is
# checked on Linux with RE4_OPT=2 port/tools/whole_build.sh.
#
# Checks before copying: every library built, no module below the expected
# count, no generated stub beyond port/whole/stubs-expected.txt (the list the
# Linux builds are checked against; an optimising compiler may drop
# definitions other libraries need, patch 0027), and the core's entry points
# exported. (Until M9 the list came from a Linux build folder next to it,
# which on the PC could be an old one.)
set -u
root="$(cd "$(dirname "$0")/../.." && pwd)"
ndk=""
opt=2
abi=all
source=""
work="re4-kern"
into=""
ouya=0
stage=""
while [ $# -gt 0 ]; do
    case "$1" in
        --ndk) ndk="$2"; shift 2 ;;
        --opt) opt="$2"; shift 2 ;;
        --abi) abi="$2"; shift 2 ;;
        --source) source="$2"; shift 2 ;;
        --work) work="$2"; shift 2 ;;
        --into) into="$2"; shift 2 ;;
        --ouya) ouya=1; shift ;;
        --stage) stage="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$ndk" ]; then
    if [ "$ouya" -eq 1 ]; then
        # not $ANDROID_NDK_HOME: that is the app's NDK, which cannot build for Android 4.1
        ndk=/opt/android-ndk-r23c
    else
        ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-/opt/android-ndk-r28c}}"
    fi
fi
[ "$ouya" -eq 1 ] && abi=armeabi-v7a
if [ -n "$source" ]; then
    case "$work" in
        /*) ;;
        *) work="$HOME/$work" ;;
    esac
    root="$work/RE4"
    mkdir -p "$work"
    git_s() { git -c safe.directory='*' "$@"; }
    if [ ! -d "$root/.git" ]; then
        echo "Quelltext wird nach $root kopiert (einmalig) ..."
        rm -rf "${root:?}"
        git_s clone -q "$source" "$root" || { echo "git clone von $source fehlgeschlagen" >&2; exit 1; }
    fi
    git_s -C "$root" fetch -q "$source" HEAD || { echo "git fetch von $source fehlgeschlagen" >&2; exit 1; }
    git_s -C "$root" checkout -q --detach FETCH_HEAD || exit 1
    echo "Stand: $(git_s -C "$root" log -1 --format='%h %s')"
fi
if [ -z "${RE4_JOBS:-}" ]; then
    # about 1.5 GB of memory per compile job (clang on the big units)
    jobs=$(( $(awk '/MemTotal/ {print $2}' /proc/meminfo) / 1500000 ))
    [ "$jobs" -gt "$(nproc)" ] && jobs=$(nproc)
    [ "$jobs" -lt 1 ] && jobs=1
    export RE4_JOBS=$jobs
fi
app="${into:-$root/port/android-decomp/app}"
stage="${stage:-$root/port/build/ouya-probe}"

toolchain="$ndk/build/cmake/android.toolchain.cmake"
if [ ! -f "$toolchain" ]; then
    setup=--android
    [ "$ouya" -eq 1 ] && setup=--ouya
    echo "FEHLER: kein Android-NDK unter $ndk (einmal: sudo port/tools/whole_setup_linux.sh $setup)" >&2
    exit 1
fi
revision=$(sed -n 's/^Pkg.Revision *= *//p' "$ndk/source.properties" 2> /dev/null | tr -d '\r')
echo "NDK: $ndk ($revision)"
platform=android-21
if [ "$ouya" -eq 1 ]; then
    platform=android-16
    case "$revision" in
        23.*) ;;
        *) echo "FEHLER: die OUYA (Android 4.1) braucht das NDK r23c; dieses NDK ist $revision" >&2; exit 1 ;;
    esac
else
    case "$revision" in
        28.*) ;;
        *) echo "Hinweis: die App verwendet NDK 28.2.13676358; dieses NDK ist $revision" ;;
    esac
fi

# one ABI: build, check, copy into the app
build_abi() {
case "$1" in
    armeabi-v7a) tag=armv7; triple=arm-linux-androideabi ;;
    arm64-v8a) tag=arm64; triple=aarch64-linux-android ;;
    *) echo "unknown ABI: $1" >&2; return 2 ;;
esac
build="$root/port/build/whole-android-$tag"
[ "$ouya" -eq 1 ] && build="$root/port/build/whole-android-ouya"
log="$build.log"
out="$app/src/core/jniLibs/$1"
assets="$app/src/core/assets"
mkdir -p "$root/port/build"
echo "Build $1 $platform (Log: port/build/${build##*/}.log) ..."
cmake -S "$root/port/whole" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI="$1" \
    -DANDROID_PLATFORM=$platform \
    -DANDROID_STL=c++_shared \
    -DRE4_OPT_LEVEL="$opt" > "$log" 2>&1 &&
    ninja -C "$build" -k 0 ${RE4_JOBS:+-j "$RE4_JOBS"} >> "$log" 2>&1
code=$?
failed=$(grep -c '^FAILED' "$log")
errors=$(grep -E ': error:|fatal error:|ld.lld: error' "$log" | sort -u | wc -l)
echo "android $1 $platform -O$opt: $failed failed build steps, $errors distinct errors"
grep -h '^link plan:' "$log" | tail -1 | sed 's/^/  /'
if [ "$code" -ne 0 ] || [ "$failed" -ne 0 ] || [ "$errors" -ne 0 ]; then
    grep -E ': error:|fatal error:|ld.lld: error' "$log" | sort -u | head -40 | sed 's/^/  /'
    # the first five distinct errors once in full: where they were included from and the
    # compiler's notes after them (the previous declaration ...)
    grep -E ': error:|fatal error:' "$log" | sort -u | head -5 | while IFS= read -r message; do
        at=$(grep -n -m 1 -F -- "$message" "$log" | cut -d: -f1)
        [ -n "$at" ] || continue
        echo "  --- $(basename "${message%%:*}"): first time in full"
        sed -n "$((at > 12 ? at - 12 : 1)),$((at + 10))p" "$log" | grep -vE '^\[[0-9]+/[0-9]+\]|^FAILED:|^ninja:' |
            cut -c1-300 | sed 's/^/    /'
    done
    if [ "$failed" -eq 0 ] && [ "$errors" -eq 0 ]; then
        # no compile step failed: CMake or the source preparation (whole_prepare.py,
        # patches) stopped, or ninja itself was ended; the end of the log says which
        echo "  (exit code $code; end of port/build/${build##*/}.log:)"
        tail -n 25 "$log" | sed 's/^/  /'
    fi
    return 1
fi

status=0
modules=("$build"/modules/libre4_*.so)
count=0
for m in "${modules[@]}"; do
    case "$(basename "$m")" in
        libre4_re4test_*) ;;
        *) count=$((count + 1)) ;;
    esac
done
echo "  module libraries: $count"
[ "$count" -lt 114 ] && { echo "FEHLER: weniger als 114 Modul-Bibliotheken"; status=1; }

# the generated stubs: the list of the repository (port/whole/stubs-expected.txt,
# which whole_build.sh checks the Linux builds against); no Android-only stub
python3 "$root/port/tools/whole_stub_check.py" "$build/generated/link_report.json" --android || status=1

readelf=$(ls "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-readelf 2> /dev/null | head -1)
strip=$(ls "$ndk"/toolchains/llvm/prebuilt/*/bin/llvm-strip 2> /dev/null | head -1)
cxxshared=$(ls "$ndk"/toolchains/llvm/prebuilt/*/sysroot/usr/lib/$triple/libc++_shared.so 2> /dev/null | head -1)
for sym in re4_core_reserve re4_core_start re4_core_state re4_core_message re4_core_pad re4_core_rumble re4_core_audio re4_core_acquire \
    re4_core_release re4_core_log_tail re4_core_instant; do
    if ! "$readelf" -W --dyn-syms "$build/libre4_main.so" | grep -q " $sym$"; then
        echo "  FEHLER: libre4_main.so exportiert $sym nicht"
        status=1
    fi
done
# what the core process takes from the platform library (dlsym through libre4_main.so finds it
# there, as the app's re4_core_gx_acquire)
for sym in re4_core_gx_acquire re4_core_gx_notify re4_add_frame_hook; do
    if ! "$readelf" -W --dyn-syms "$build/libre4_platform.so" | grep -v ' UND ' | grep -q " $sym$"; then
        echo "  FEHLER: libre4_platform.so exportiert $sym nicht"
        status=1
    fi
done
[ -f "$cxxshared" ] || { echo "  FEHLER: libc++_shared.so nicht im NDK gefunden"; status=1; }
# 32 bit: the core process program (the core runs in it when MEM1 is taken in the app's
# process, port/docs/32bit-kernprozess.md), an executable named like a library
coreproc=""
if [ "$1" = armeabi-v7a ]; then
    coreproc="$build/libre4_coreproc.so"
    if [ ! -f "$coreproc" ]; then
        echo "  FEHLER: libre4_coreproc.so (Kernprozess) fehlt"
        status=1
    elif ! "$readelf" -l -W "$coreproc" | grep -q 'INTERP'; then
        echo "  FEHLER: libre4_coreproc.so ist kein ausfuehrbares Programm"
        status=1
    fi
fi
[ "$status" -ne 0 ] && return 1
if [ "$ouya" -eq 1 ]; then
    stage_ouya
    return $?
fi

rm -rf "$out"
mkdir -p "$out" "$assets"
cp "$build/libre4_platform.so" "$build/libre4_main.so" "$cxxshared" $coreproc "$out/"
for m in "${modules[@]}"; do
    case "$(basename "$m")" in
        libre4_re4test_*) ;;
        *) cp "$m" "$out/" ;;
    esac
done
# debug sections out (the APK stays small); symbols stay for crash reports
for f in "$out"/libre4_*.so; do
    "$strip" --strip-debug "$f"
done
commit=$(git -C "$root" rev-parse --short HEAD 2> /dev/null || echo unknown)
{
    echo "commit=$commit"
    echo "ndk=$revision"
    echo "opt=$opt"
    echo "libraries=$(ls "$out" | grep -c '\.so$')"
} > "$assets/core-libs-$1.properties"
# the file of earlier versions (one ABI)
rm -f "$assets/core-libs.properties"
echo "Kern-Bibliotheken $1: $(ls "$out" | grep -c '\.so$') Dateien, $(du -sh "$out" | cut -f1) -> $out"
}

# The OUYA probe: what Android 4.1's linker needs checked, then everything flat into $stage
# (port/scripts/ouya-probe.ps1 pushes it to /data/local/tmp/re4-ouya)
stage_ouya() {
    local probe="$build/re4_probe" f bad=0 needed
    if [ ! -f "$probe" ] || ! "$readelf" -l -W "$probe" | grep -q 'INTERP'; then
        echo "  FEHLER: re4_probe (die Kern-Probe) fehlt oder ist kein Programm"
        return 1
    fi
    for f in "$build/libre4_platform.so" "$build/libre4_main.so" "${modules[@]}" "$probe" "$coreproc"; do
        [ -f "$f" ] || continue
        # the classic hash table (DT_HASH): Android before 6.0 knows only that one
        if ! "$readelf" -d -W "$f" | grep -q '(HASH)'; then
            echo "  FEHLER: ${f##*/} ohne DT_HASH (Android 4.1 kann sie nicht laden)"
            bad=1
        fi
        # thread-local storage of the ELF kind and packed relocations: Android 10 / 6.0 and later
        if "$readelf" -l -W "$f" | grep -qE '^ *TLS '; then
            echo "  FEHLER: ${f##*/} hat ein TLS-Segment"
            bad=1
        fi
        if "$readelf" -d -W "$f" | grep -qE '\((ANDROID_REL|ANDROID_RELA|RELR)[A-Z]*\)'; then
            echo "  FEHLER: ${f##*/} hat gepackte Relokationen"
            bad=1
        fi
        needed=$("$readelf" -d -W "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' |
            grep -vE '^(libc|libm|libdl|liblog|libc\+\+_shared|libre4_[a-z0-9_]+)\.so$')
        if [ -n "$needed" ]; then
            echo "  FEHLER: ${f##*/} braucht $(echo $needed)"
            bad=1
        fi
    done
    [ "$bad" -ne 0 ] && return 1
    rm -rf "$stage"
    mkdir -p "$stage"
    cp "$build/libre4_platform.so" "$build/libre4_main.so" "$cxxshared" "$probe" $coreproc "$stage/"
    for m in "${modules[@]}"; do
        case "$(basename "$m")" in
            libre4_re4test_*) ;;
            *) cp "$m" "$stage/" ;;
        esac
    done
    for f in "$stage"/libre4_*.so "$stage/re4_probe"; do
        "$strip" --strip-debug "$f"
    done
    {
        echo "commit=$(git -C "$root" rev-parse --short HEAD 2> /dev/null || echo unknown)"
        echo "ndk=$revision"
        echo "platform=$platform"
        echo "opt=$opt"
        echo "files=$(ls "$stage" | wc -l)"
    } > "$stage/probe-build.properties"
    echo "OUYA-Probe: $(ls "$stage" | wc -l) Dateien, $(du -sh "$stage" | cut -f1) -> $stage"
}

case "$abi" in
    all) abis=(armeabi-v7a arm64-v8a) ;;
    *) abis=("$abi") ;;
esac
result=0
for a in "${abis[@]}"; do
    build_abi "$a" || result=1
done
exit $result
