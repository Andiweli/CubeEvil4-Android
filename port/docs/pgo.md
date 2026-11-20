# Profile-guided optimisation (PGO) of the core – measured, not used (20.11.2026)

Task 1.1.0 C (idea from the review of the RE4Wii-Android port, `reports/fork-checks/re4wii-android-2026-11-20.md`):
does clang's profile-guided optimisation make the core (game logic and GX conversion) faster? Rule: take it into the
release build only at about 8–10 % or more of the core's processor time.

## How it was measured

- Switch in the core build (`port/whole/CMakeLists.txt`, `port/tools/whole_build.sh`):
  - `RE4_OPT=2 RE4_PGO=generate port/tools/whole_build.sh i686` builds an instrumented core (`whole-i686-O2-pgogen`);
  - `RE4_OPT=2 RE4_PGO=/path/core.profdata port/tools/whole_build.sh i686` builds with the profile (`whole-i686-O2-pgo`).
- Profile: the room tour over all 126 rooms with the debug discs and the tour's keys (walk, run, aim and shoot,
  map, knife), with the instrumented core (`RE4_BUILD_SUFFIX=-pgogen port/tools/whole_r100_check.sh … --rooms all`,
  `LLVM_PROFILE_FILE=…/re4-%m-%p.profraw`), merged with `llvm-profdata-18 merge` (332 raw files, 16 MB → 0.85 MB;
  5 232 functions with counts, among them the enemy modules and the room scripts).
- Comparison: the same 8 rooms (r103, r10a, r111, r11a, r208, r225, r303, r310), each 2 000 frames after the room's
  start with the tour's keys, the game unthrottled (`RE4_VI_FAST=1`), the process's processor time (user + system),
  three rounds, the smallest of each; host clang 18.1.3, i686, `-O2` both.

## Result

| Room | -O2 | -O2 + PGO |
|---|---|---|
| r103 | 8.47 s | 8.62 s |
| r10a | 4.96 s | 5.12 s |
| r111 | 6.47 s | 6.57 s |
| r11a | 7.30 s | 7.50 s |
| r208 | 10.44 s | 10.58 s |
| r225 | 6.47 s | 6.57 s |
| r303 | 5.26 s | 5.42 s |
| r310 | 5.98 s | 6.15 s |
| **total** | **55.34 s** | **56.53 s (+2.1 %, slower)** |

PGO made the core slower in every room. The profile was applied (all 1 397 translation units with
`-fprofile-instr-use`). The game's code spends its time in many small calls spread over the managers (the
hottest: `cUnit::isAlive`, `cManager<cEm>::fastAt`, `em10SomebodyDamageNowCk`); the profile gives the compiler
little to reorder, and its other inlining costs a bit. **Not taken into the Android build.** The switch stays for a
later measurement (for example with ThinLTO or a newer NDK).

## Refreshing a measurement

1. `RE4_OPT=2 RE4_PGO=generate port/tools/whole_build.sh i686`
2. `LLVM_PROFILE_FILE=$HOME/pgo-raw/re4-%m-%p.profraw RE4_OPT=2 RE4_BUILD_SUFFIX=-pgogen port/tools/whole_r100_check.sh --data DISC --no-build --rooms all --tour-jobs 12 --out OUT`
3. `llvm-profdata merge -o core.profdata $HOME/pgo-raw/*.profraw`
4. `RE4_OPT=2 RE4_PGO=$PWD/core.profdata port/tools/whole_build.sh i686`
5. Compare `whole-i686-O2` and `whole-i686-O2-pgo` on the same rooms (the script of this measurement:
   unthrottled, 2 000 frames, processor time).

A profile for the Android build would have to come from the NDK's clang (its llvm-profdata reads the host's indexed
profile only when the versions fit) or from an instrumented APK on a device; neither is needed while PGO does not pay.
