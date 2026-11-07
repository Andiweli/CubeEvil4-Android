#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_build="${RE4_TEST_BUILD:-$task_root/.host-tests}"
mkdir -p "$task_build/java"

# Pure-Java contracts require no Android runtime. Optional arguments retain
# the original DiscTests route: Disc1, Disc2 and an empty extraction directory.
javac -d "$task_build/java" "$task_root"/app/src/main/java/com/ast/re4decomp/disc/*.java "$task_root/tests/DiscTests.java" "$task_root/tests/RoomFileCheckTests.java"
java -cp "$task_build/java" DiscTests
java -cp "$task_build/java" RoomFileCheckTests
if [[ $# -eq 3 ]]; then
    java -cp "$task_build/java" DiscTests "$task_root/app/src/main/assets/references.tsv" "$1" "$2" "$3"
fi

javac -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/ControllerPolicy.java" "$task_root/tests/ControllerPolicyTests.java"
java -cp "$task_build/java" ControllerPolicyTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/CoreInput.java" "$task_root/app/src/main/java/com/ast/re4decomp/ActionMap.java" "$task_root/tests/ActionMapTests.java"
java -cp "$task_build/java" com.ast.re4decomp.ActionMapTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/CoreInput.java" "$task_root/app/src/main/java/com/ast/re4decomp/ActionMap.java" "$task_root/tests/CoreInputTests.java"
java -cp "$task_build/java" CoreInputTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/CoreInput.java" "$task_root/app/src/main/java/com/ast/re4decomp/ActionMap.java" "$task_root/app/src/main/java/com/ast/re4decomp/CoreTouch.java" "$task_root/tests/CoreTouchTests.java"
java -cp "$task_build/java" com.ast.re4decomp.CoreTouchTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/FramePacing.java" "$task_root/tests/FramePacingTests.java"
java -cp "$task_build/java" com.ast.re4decomp.FramePacingTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/GyroAxes.java" "$task_root/tests/GyroAxesTests.java"
java -cp "$task_build/java" com.ast.re4decomp.GyroAxesTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/SurroundPcm.java" "$task_root/tests/SurroundPcmTests.java"
java -cp "$task_build/java" com.ast.re4decomp.SurroundPcmTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/LatencyTuner.java" "$task_root/tests/LatencyTunerTests.java"
java -cp "$task_build/java" com.ast.re4decomp.LatencyTunerTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/TouchShowRule.java" "$task_root/tests/TouchShowRuleTests.java"
java -cp "$task_build/java" com.ast.re4decomp.TouchShowRuleTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/ControllerLog.java" "$task_root/tests/ControllerLogTests.java"
java -cp "$task_build/java" com.ast.re4decomp.ControllerLogTests
python3 "$task_root/tools/check_translations.py"
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/GameMenuLayout.java" "$task_root/app/src/main/java/com/ast/re4decomp/GameMenuNavigation.java" "$task_root/tests/GameMenuNavigationTests.java"
java -cp "$task_build/java" GameMenuNavigationTests
javac -Xlint:all -cp "$task_build/java" -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/GameMenuLayout.java" "$task_root/tests/GameMenuLayoutTests.java"
java -cp "$task_build/java" GameMenuLayoutTests
javac -Xlint:all -cp "$task_build/java" -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/GameMapCamera.java" "$task_root/tests/GameMapCameraTests.java"
java -cp "$task_build/java" GameMapCameraTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/StartupMath.java" "$task_root/app/src/main/java/com/ast/re4decomp/StartupFlow.java" "$task_root/app/src/main/java/com/ast/re4decomp/StartupOptions.java" "$task_root/tests/StartupFlowTests.java" "$task_root/tests/StartupOptionsTests.java"
java -cp "$task_build/java" com.ast.re4decomp.StartupFlowTests
java -cp "$task_build/java" com.ast.re4decomp.StartupOptionsTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/CockpitState.java" "$task_root/app/src/main/java/com/ast/re4decomp/ContinueFlow.java" "$task_root/tests/CockpitContinueTests.java"
java -cp "$task_build/java" com.ast.re4decomp.CockpitContinueTests
javac -Xlint:all -cp "$task_build/java" -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/AudioPlaybackFence.java" "$task_root/tests/StartupVoiceTests.java"
java -cp "$task_build/java" com.ast.re4decomp.StartupVoiceTests
javac -Xlint:all -cp "$task_build/java" -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/StartupViewport.java" "$task_root/app/src/main/java/com/ast/re4decomp/StartupMovieState.java" "$task_root/app/src/main/java/com/ast/re4decomp/StartupPixels.java" "$task_root/tests/StartupPresentationTests.java"
java -cp "$task_build/java" com.ast.re4decomp.StartupPresentationTests
javac -Xlint:all -cp "$task_build/java" -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/OriginalFrameClock.java" "$task_root/tests/OriginalPresentationFix2Tests.java"
java -cp "$task_build/java" com.ast.re4decomp.OriginalPresentationFix2Tests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/SceneLoadGate.java" "$task_root/tests/SceneLoadGateTests.java"
java -cp "$task_build/java" com.ast.re4decomp.SceneLoadGateTests
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/RoomRepository.java" "$task_root/tests/RoomRepositoryTest.java"
java -cp "$task_build/java" RoomRepositoryTest
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/RoomCatalog.java" "$task_root/tests/RoomCatalogTests.java"
java -cp "$task_build/java" RoomCatalogTests "$task_root/app/src/main/assets/rooms.tsv"

# Use the canonical native targets, compile flags and transitive dependencies.
# An existing configured host build can be shared with developer validation:
# RE4_TEST_NATIVE_BUILD=/path/to/native-build bash tools/test_host.sh
# Executable paths may be overridden by RE4_CMAKE and RE4_CTEST.
task_native_build="${RE4_TEST_NATIVE_BUILD:-$task_build/native}"
task_cmake="${RE4_CMAKE:-cmake}"
task_ctest="${RE4_CTEST:-ctest}"
task_native_source="$(cd -- "$task_root/app/src/main/cpp" && pwd -P)"
if [[ -f "$task_native_build/CMakeCache.txt" ]]; then
    task_configured_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$task_native_build/CMakeCache.txt")"
    if [[ "$task_configured_source" != "$task_native_source" ]]; then
        printf 'Native build belongs to another source directory: %s\n' "$task_native_build" >&2
        exit 1
    fi
else
    "$task_cmake" -S "$task_native_source" -B "$task_native_build" -DCMAKE_BUILD_TYPE=Debug
fi
"$task_cmake" --build "$task_native_build" --parallel "${RE4_TEST_JOBS:-2}"
"$task_ctest" --test-dir "$task_native_build" --output-on-failure

# Cross-check the Java card UI/storage against the native original-format
# fixture when the authorized Disc resources are supplied. No save fixture or
# retail resources are committed into the project.
javac -Xlint:all -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/OriginalSaveHeader.java" "$task_root/app/src/main/java/com/ast/re4decomp/GameSaveStore.java" "$task_root/app/src/main/java/com/ast/re4decomp/CardFlow.java" "$task_root/tests/OriginalSaveTests.java" "$task_root/tests/CardFlowTests.java"
if [[ -n "${RE4_TEST_DISC_FILES:-}" ]]; then
    "$task_native_build/m37_save_game_check" "$RE4_TEST_DISC_FILES" "$task_build/original-card.bin"
    java -cp "$task_build/java" com.ast.re4decomp.OriginalSaveTests "$task_build/original-card.bin"
    java -cp "$task_build/java" com.ast.re4decomp.CardFlowTests "$task_build/original-card.bin"
    if [[ -x "$task_native_build/m35_title_assets_check" ]]; then
        "$task_native_build/m35_title_assets_check" "$RE4_TEST_DISC_FILES" "$task_build/card-layout"
        javac -Xlint:all -cp "$task_build/java" -d "$task_build/java" "$task_root/app/src/main/java/com/ast/re4decomp/CardMessageLayout.java" "$task_root/tests/CardMessageLayoutTests.java"
        java -cp "$task_build/java" com.ast.re4decomp.CardMessageLayoutTests "$task_build/card-layout/3000.json" "$task_build/card-layout/5015.json" "$task_build/card-layout/5017.json" "$task_build/card-layout/5019.json"
    fi
else
    printf 'Original Java card asset tests skipped: set RE4_TEST_DISC_FILES to supplied Disc files.\n'
fi

# Build all optional M35 asset/GLES tools above. Original-data tools use their
# documented supplied-resource arguments separately; the brightness check
# needs no original assets and runs whenever the optional GLES target exists.
if [[ -x "$task_native_build/m35_display_brightness_check" ]]; then
    "$task_native_build/m35_display_brightness_check"
fi

# Original inventory tables and the exhaustive current Leon dispatch inventory.
python3 "$task_root/tools/check_inventory_source.py" "$task_root/upstream/re4"
python3 "$task_root/tools/audit_player_actions.py" --check
