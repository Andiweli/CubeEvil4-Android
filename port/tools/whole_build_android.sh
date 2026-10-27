#!/usr/bin/env bash
# Builds the game core for the Android app (the core mode of
# RE4-Android-Decomp) with the NDK and copies the libraries into the app:
#
#   port/tools/whole_build_android.sh [--ndk DIR] [--opt N] [--abi ABI] [--source REPO] [--into APP]
#
#   --abi ABI      armeabi-v7a, arm64-v8a (milestone 12) or all (the default:
#                  both, one APK for 32- and 64-bit devices)
#   --source REPO  build the current commit of that checkout (for the Windows
#                  checkout under /mnt/...: cloned once to ~/re4-kern/RE4 and
#                  fetched, like whole_r100_check.sh; building on a Windows
#                  drive would be slow)
#   --into APP     copy the libraries into APP/src/core (default: the app of
#                  the built checkout); port/scripts/test-core-app.ps1 passes
#                  the Windows checkout's app folder
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
ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-/opt/android-ndk-r28c}}"
opt=2
abi=all
source=""
into=""
while [ $# -gt 0 ]; do
    case "$1" in
        --ndk) ndk="$2"; shift 2 ;;
        --opt) opt="$2"; shift 2 ;;
        --abi) abi="$2"; shift 2 ;;
        --source) source="$2"; shift 2 ;;
        --into) into="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [ -n "$source" ]; then
    work="$HOME/re4-kern"
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

toolchain="$ndk/build/cmake/android.toolchain.cmake"
if [ ! -f "$toolchain" ]; then
    echo "FEHLER: kein Android-NDK unter $ndk (einmal: sudo port/tools/whole_setup_linux.sh --android)" >&2
    exit 1
fi
revision=$(sed -n 's/^Pkg.Revision *= *//p' "$ndk/source.properties" 2> /dev/null | tr -d '\r')
echo "NDK: $ndk ($revision)"
case "$revision" in
    28.*) ;;
    *) echo "Hinweis: die App verwendet NDK 28.2.13676358; dieses NDK ist $revision" ;;
esac

# one ABI: build, check, copy into the app
build_abi() {
case "$1" in
    armeabi-v7a) tag=armv7; triple=arm-linux-androideabi ;;
    arm64-v8a) tag=arm64; triple=aarch64-linux-android ;;
    *) echo "unknown ABI: $1" >&2; return 2 ;;
esac
build="$root/port/build/whole-android-$tag"
log="$build.log"
out="$app/src/core/jniLibs/$1"
assets="$app/src/core/assets"
mkdir -p "$root/port/build"
echo "Build $1 (Log: port/build/whole-android-$tag.log) ..."
cmake -S "$root/port/whole" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI="$1" \
    -DANDROID_PLATFORM=android-21 \
    -DANDROID_STL=c++_shared \
    -DRE4_OPT_LEVEL="$opt" > "$log" 2>&1 &&
    ninja -C "$build" -k 0 ${RE4_JOBS:+-j "$RE4_JOBS"} >> "$log" 2>&1
code=$?
failed=$(grep -c '^FAILED' "$log")
errors=$(grep -E ': error:|fatal error:|ld.lld: error' "$log" | sort -u | wc -l)
echo "android $1 -O$opt: $failed failed build steps, $errors distinct errors"
grep -h '^link plan:' "$log" | tail -1 | sed 's/^/  /'
if [ "$code" -ne 0 ] || [ "$failed" -ne 0 ] || [ "$errors" -ne 0 ]; then
    grep -E ': error:|fatal error:|ld.lld: error' "$log" | sort -u | head -40 | sed 's/^/  /'
    if [ "$failed" -eq 0 ] && [ "$errors" -eq 0 ]; then
        # no compile step failed: CMake or the source preparation (whole_prepare.py,
        # patches) stopped, or ninja itself was ended; the end of the log says which
        echo "  (exit code $code; end of port/build/whole-android-$tag.log:)"
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

case "$abi" in
    all) abis=(armeabi-v7a arm64-v8a) ;;
    *) abis=("$abi") ;;
esac
result=0
for a in "${abis[@]}"; do
    build_abi "$a" || result=1
done
exit $result
