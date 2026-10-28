#!/usr/bin/env bash
# Strict shader check (Anbernic RG 477V report, 22.10.2026: the Mali driver refused a
# shader that Mesa and Adreno accept, a uniform named like the built-in `refract`).
# Runs the host GL checks of the app with tools/shader_dump_shim.c preloaded, which
# writes every shader they compile, then has glslangValidator (the GLSL reference
# compiler, as strict as Mali on names and versions) compile each one.
#
#   tools/check_shaders_strict.sh [build dir]   (default: port/build/app-host-gl)
#
# The build dir is the host GL build of the app (cmake -DRE4_BUILD_GL_CHECK=ON, see
# port/tools/whole_r100_check.sh); the checks it runs must be built. Needs
# glslangValidator (apt: glslang-tools) and a C compiler. Exit 0: every shader compiled.
set -uo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build="${1:-$here/../../build/app-host-gl}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

command -v glslangValidator > /dev/null || { echo "glslangValidator missing (apt install glslang-tools)"; exit 2; }
cc -shared -fPIC -O1 -o "$work/shim.so" "$here/shader_dump_shim.c" -ldl || exit 2

# the checks that compile shaders the core mode or the bridge use; gx_gl_check covers
# the GX programs (TEV combinations of its test chunks) and the GPU check of the movies
checks=(gx_gl_check scene_gl_check core_scene_gl_check actor_gl_check world_effects_gl_check
        environment_gl_check m33_subscreen_gl_check m37_event_focus_gl_check m35_display_brightness_check
        m36_startup_compositor_check)
ran=0
for c in "${checks[@]}"; do
    [ -x "$build/$c" ] || { echo "skipped: $c (not built)"; continue; }
    mkdir -p "$work/$c"
    (cd "$build" && RE4_SHADER_DUMP="$work/$c" LD_PRELOAD="$work/shim.so" timeout 600 "./$c") > "$work/$c.log" 2>&1
    echo "$c: exit $?, $(ls "$work/$c" | wc -l) shaders"
    ran=$((ran + 1))
done
[ "$ran" -gt 0 ] || { echo "no check built in $build"; exit 2; }

# identical sources once; each through glslangValidator (stage from the file name)
total=0 bad=0
declare -A seen
for f in "$work"/*/*.vert "$work"/*/*.frag "$work"/*/*.comp; do
    [ -f "$f" ] || continue
    sum=$(sha1sum "$f" | cut -c1-40)
    [ -n "${seen[$sum]:-}" ] && continue
    seen[$sum]=1
    total=$((total + 1))
    if ! out=$(glslangValidator "$f" 2>&1); then
        bad=$((bad + 1))
        echo "--- $f"
        echo "$out" | grep -v "^$f$" | head -8
        echo "    first lines: $(head -3 "$f" | tr '\n' ' ' | cut -c1-120)"
    fi
done
echo "strict shader check: $total distinct shaders, $bad refused"
[ "$bad" -eq 0 ]
