<img width="1000" height="333" alt="image" src="https://github.com/user-attachments/assets/e73dfdba-2372-4a4e-9c7e-b84dc76e619d" />

# CubeEvil4

![Beta](https://img.shields.io/badge/Status-Beta-E67E22)
![Android 6.0+](https://img.shields.io/badge/Android-6.0%2B-3DDC84?logo=android&logoColor=white)
![ARM 32/64-bit](https://img.shields.io/badge/ARM-32%2F64--bit-0091BD?logo=arm&logoColor=white)
![OpenGL ES 3.0+](https://img.shields.io/badge/OpenGL%20ES-3.0%2B-5586A4?logo=opengl&logoColor=white)
![Controller or touch](https://img.shields.io/badge/Input-Controller%20%7C%20Touch-9A1B1C)

[![Join The Lair on Discord](https://img.shields.io/badge/Discord-The%20Lair-5865F2?logo=discord&logoColor=white)](https://discord.gg/AB3WVBqD5G)
[![Support via PayPal](https://img.shields.io/badge/Support%20via-PayPal-0070BA?logo=paypal&logoColor=white)](https://paypal.me/andiweli)

**[REQUIREMENTS](#requirements) | [STATUS](#status) | [HOW TO USE](#how-to-use) | [DISCORD](https://discord.gg/AB3WVBqD5G) | [BETA RELEASE](https://github.com/Andiweli/CubeEvil4-Android/releases)**

**CubeEvil4** is an unofficial, native Android port of the GameCube debug build of Resident Evil 4
(`G4BE08`, the "Nov 25 2004" prototype on two discs). It does not recreate the game: the app runs
the original game code, compiled from the complete decompilation in this repository for ARM.
Since v0.92.0 it also plays the **European retail release** (`G4BP08`, two discs) in English,
German, French, Spanish and Italian; the code stays the debug build's, with small patches where the
retail data needs them. It is not affiliated with or endorsed by Capcom, Nintendo or CRI Middleware,
and it contains no game data: you need your own images of both discs of one of the two releases.

<!--
<p align="center"><img src="port/android-decomp/app/src/main/res/drawable-nodpi/icon_cubeevil4.png" width="160" alt="CubeEvil4 icon"></p>
-->

## Reviews

- [Gamesworld Channel](https://www.youtube.com/watch?v=PvpjyH123YI) (Youtube)  
- [EightiesJapan's Channel](https://www.youtube.com/shorts/afU4VWHp1uQ) (Youtube)  
- [Android Gamer's Channel](https://www.youtube.com/watch?v=m-XGtGydIVg) (Youtube)  


## Currently in development

- **Full button mapping:** every action on any button, combinations like aim + fire, and play with
  keyboard and mouse (mouse aiming with adjustable sensitivity).
- **Better performance on weaker devices**, especially phones with only a few fast CPU cores.
- **US and JP retail release** as supported disc versions.
- **New cheats:** automatic quick-time events and unlocking Assignment Ada and The Mercenaries.
- **Touch aiming by swipe:** the aim follows your finger and stops when it stops.
- **Various fixes from testers' reports** 

## What the app does

- The whole game code (`src/`, all 114 REL modules) runs natively, as ARM32 (`armeabi-v7a`) and
  ARM64 (`arm64-v8a`) in one APK. Only the GameCube platform layer is replaced (graphics, video
  timing, OS, disc, controller, sound, memory card, ARAM, movies).
- Graphics: the game's own GX commands are translated to OpenGL ES 3 (logos, menus, rooms,
  characters, effects, subtitles), at 1080p and above, with optional anti-aliasing,
  anisotropic filtering and depth of field.
- Sound: the original SDK sound libraries with an emulated DSP; movies through the original CRI
  middleware with picture and ADX sound.
- Two releases: the debug build (`G4BE08`) and the European retail release (`G4BP08`) with its five
  languages, chosen in the app menu (default: your phone's language). Both use the same memory card.
- Memory card as a file inside the app, with `.gci` import and export (Dolphin, GameCube).
- Controller like the GameCube pad, with vibration, or a second layout (Alternate Controls,
  aiming with the right stick); touch controls when no controller is connected, with touch in the
  inventory; comfort controls: gyro aiming, knife by swipe, latching L, R and B; with a controller,
  X, Y, L1, R1, L3 and R3 can take other functions (Controller Configuration).
  SELECT opens the app menu (graphics, controls, cheats, memory card, error report); with the
  debug build, holding SELECT opens the game's own debug menu.
- Android 6.0 or later, OpenGL ES 3.0.

## Requirements

| | Minimum | Recommended |
|---|---|---|
| Android | 6.0 | – |
| Processor | ARM, 32- or 64-bit | 64-bit (see the note below) |
| Graphics | OpenGL ES 3.0 | – |
| RAM | – | 4 GB or more |
| Free storage | about 3 GB, to import both discs | – |
| Input | a game controller (built in, USB or Bluetooth) or the touch screen | a game controller |
| Gyroscope | – | for gyro aiming (optional) |
| Game data | your own disc images (ISO or GCM) of **both** discs of either the GameCube debug build from November 25, 2004 (`G4BE08`) or the European retail release (`G4BP08`) | – |

> [!NOTE]
> **32-bit-only devices:** some of them cannot start the game. One with Android 14 showed
> "core not started: MEM1 … is taken in this process": Android keeps its own memory at the
> address the game needs. This does not depend on the amount of RAM. A device that runs 64-bit
> apps is recommended.

## Status

Beta: the whole game can be played from start to finish.  
Tested on the following devices:
- Retroid Pocket Flip 2 (Android 13)
- POCO X7 Pro (Android 16)
- Samsung Galaxy S10 (SCV40, Android 10): played from the village to the end credits

Played through on a 64-bit phone: title, movies, every chapter up to the jet ski ride and the end
credits; saving at the typewriter and loading on 32- and 64-bit devices. With the European discs,
the first chapters were played in German on the Retroid, boss fights included. On the PC, a tour
starts each of the game's 126 rooms without a crash, with the debug discs and with the European
discs, and a comparison run plays the 32- and 64-bit core
with the same input through the first parts and finds every frame and every compared picture
identical. An effect can stutter briefly the first time it appears while its shader is built; the
app prepares the 9006 shaders it knows ahead (village, lake, castle, much of the island), the
rest follows from testers' reports. Progress, test results and every patch are
recorded in `port/docs/whole-program-status.md` (German).

> [!IMPORTANT]
> With the debug discs, the game's debug menu is still switched on for the test runs: holding
SELECT opens it (rooms are chosen there). Use the debug menu with care: it is
the developers' internal tool from 2004, and some of its options can hang or crash the game or
leave the game (and a save made afterwards) in an odd state. Such problems come from the debug build itself, not from the port.
With the European discs there is no debug menu; the app menu has cheats instead (Management).

## How to use?

**You need**

- An Android device with Android 6.0 or later, OpenGL ES 3.0 and a 32- or 64-bit ARM processor
  (details and recommendations: [Requirements](#requirements)).
- A game controller (built in, USB or Bluetooth) or the touch screen: the touch controls appear
  on their own when no controller is connected, and after a tap on the screen (app menu → Touch
  Controls: Auto, Force or Suppress).
- Your own disc images (ISO or GCM) of **both** discs of the GameCube debug build from
  November 25, 2004 (`G4BE08`) or of the European retail release (`G4BP08`), and enough free
  storage to import them. Both discs must be of the same release.

**First start**

1. Install the APK and open **CubeEvil4**.
2. Choose **Import Disc 1** and **Import Disc 2** and pick the image files. The game starts only
   once both discs are imported. The import checks every file against the original disc and
   reports a damaged image right away.
3. Choose **Start game**. If your controller's face buttons are labelled the other way round,
   tick "Swap A/B and X/Y" on the start screen.

**SELECT button**

- **Press SELECT** (with touch controls: **MENU**): the app menu, in your phone's language where
  there is a translation (English otherwise; Arabic, Chinese (simplified), French, German, Hindi,
  Italian, Portuguese, Russian and Spanish so far; on Android 13 and later also per app under
  Settings → Apps → CubeEvil4 → Language); with the European discs also the game language
  (a change restarts the game); graphics (resolution Dynamic or 1×–4× in half steps,
  anti-aliasing, texture filter, picture 4:3 (GC), 4:3, 16:9 or Stretched, depth of field, FPS
  display, 60 FPS (experimental), Force Performance: Default or Use performance cores), controls (touch controls Auto, Force or Suppress; comfort
  controls: Alternate Controls, gyro aiming with its sensitivity and inversion, knife by swipe,
  Latch L and R, Latch B; Controller Configuration), cheats (infinite health, infinite ammo,
  one-hit kill, a fully upgraded Killer7, the attaché case L, +1,000,000 PTAS), memory card (export
  and import saves as `.gci` for Dolphin and the GameCube) and the error log (send or save a
  report; R3 = graphics capture). B returns to the game.
- **Hold SELECT for a second** (debug discs only): the game's own debug menu (use with care, see
  Status: some of its options can break the game in ways the port has nothing to do with).

**Buttons**

| Controller | GameCube | In the game | Alternate Controls |
|---|---|---|---|
| A, B, X, Y | A, B, X, Y | by their labels | A, B as before; X Ashley; Y map |
| Left stick | Control stick | move | move |
| Right stick | C stick | | aim while L1 or L2 is held |
| D-pad | D-pad | | as before |
| START | START | | as before |
| R2 | R | ready weapon | fire, or slash with the knife ready |
| L2 | L | knife | ready weapon (aim) |
| R1 | Z | map | inventory (weapons and recovery) |
| L1 | – | | ready the knife |
| SELECT | – | app menu (hold: debug menu, debug discs only) | as before |
| R3 | – | graphics capture for error reports (only when "R3 = Graphics capture" is on) | as before |

**Controller Configuration** (app menu → Controls, with a controller connected): X, Y, L1, R1, L3
and R3 can each take any function or none; A, B, L2, R2, START, SELECT and the D-pad keep theirs, so
the game's button prompts and menus always match. Giving Ashley, the inventory or the map to
another button swaps the two buttons. With Alternate Controls on, their own layout applies.

**Touch controls**

On screen: A, B, X, Y, Z, L, R, START, MENU (= SELECT), the D-pad, a move stick and a look zone
(the C stick). App menu → Touch Controls: **Auto** (shown without a controller; a tap on the
screen shows them, a button hides them), **Force** (always shown) or **Suppress** (hidden while a
controller is connected). In the inventory you can tap items, files and the map, and swipe left or
right to change the tab. When a quick-time prompt flashes up (L+R, A+B, mash A …), a tap on it (or
anywhere on the picture) presses its buttons; tap again and again to mash. The editor (gear) can
add an **R3** button (graphics capture for error reports, as on a controller).

**Comfort controls** (app menu → Controls)

- **Alternate Controls:** the second layout of the table above.
- **Gyro aiming:** turning the device left, right, up or down moves the aim while you aim a gun,
  look through a scope or aim the harpoon in the boat at the lake; a slider sets the sensitivity.
- **Knife by swipe:** a quick swipe across the picture draws the knife and slashes; swipe upward
  or downward to slash high or low.
- **Latch L and R** and **Latch B:** one press (button or touch) holds L, R or B until the next
  press, on the touch controls and on a controller (R latched: Leon keeps the weapon ready; B
  latched: Leon keeps running).

The controller (or the device) vibrates where the game uses the rumble, if it can.

## Building the app

No disc images or game files ever go into the repository.

1. Build the game core for Android (Linux or WSL2, Android NDK r28c):
   `port/tools/whole_build_android.sh` (on Windows,
   `powershell -ExecutionPolicy Bypass -File .\port\scripts\test-core-app.ps1 -SkipApk -SkipDevice`
   does the same through WSL). The libraries land in
   `port/android-decomp/app/src/core/jniLibs/` (not in git).
2. Open `port/android-decomp` in Android Studio, choose the build variant **coreRelease** and
   build a signed APK (Build → Generate Signed App Bundle or APK).
3. Install the APK, start **CubeEvil4** and import Disc 1 and Disc 2 (ISO or GCM of the debug
   build or of the European retail release).

`port/scripts/test-core-app.ps1` builds, installs and tests the app on a device in one go;
`port/tools/whole_build.sh` and `port/tools/whole_r100_check.sh` check the core on Linux.

## Where the port lives

- `port/whole/` builds the whole game for the host and for Android: the rewrites, replacement
  headers and patches applied to the unchanged `src/` and `include/`, and the platform layer
  (`port/whole/runtime/`).
- `port/android-decomp/` is the Android app (Gradle project, Android Studio).
- `port/tools/` has the build and check scripts, `port/docs/` the plans and the status file.

## The decompilation

The port is built on a complete decompilation of the debug build. The debug build shipped with
`Bio4.sym` symbol files that name every function, and that is what made a full decompilation
possible.

Building the repository gives back the exact original files: `main.dol` and all 114 REL overlays,
checked against `config/G4BE08/build.sha1` on every build. The source is about 570,000 lines of C and
C++ in `src/` and 44,000 lines of headers in `include/`, with no assembly files.

The code is built with the compilers the original was built with:

| Code | Compiler |
|---|---|
| The game | SN Systems ProDG 3.9.3, GCC 2.95.3 "SN BUILD v1.79", built from SN's GPL source release |
| CRI middleware (`adx_*`, `sfd_*`, `mpv_*` and the rest in `src/lib/`) | Metrowerks CodeWarrior 2.4.7, which CRI shipped the libraries with |
| Nintendo SDK (`OS*`, `GX*` and the rest in `src/lib/`) | Metrowerks CodeWarrior GC/1.2.5n, sources from [dolsdk2004](https://github.com/doldecomp/dolsdk2004) |

There are no game assets in this repository. To build it you need your own images of the debug
discs. Disc 1 has `main.dol` and most of the RELs, disc 2 has the four RELs for the island stages.

This repository is a fork of the decompilation by
[adonis-singh](https://github.com/adonis-singh/re4); its build and research documentation follows.
The CC0 terms in [LICENSE](LICENSE) apply only to the project's own tools, configuration and
documentation, not to Capcom's game code or Nintendo/CRI SDK and middleware code. The Android
port does not change those ownership or licensing limitations.

### Building the original files

You need Linux, Python 3 and [ninja](https://ninja-build.org/). The first configure run downloads
decomp-toolkit, objdiff, wibo and the CodeWarrior compilers. The SN GCC is built once from SN's
source:

```sh
# 1. build the SN compiler once (needs SN's GPL source release, see tools/sn-gcc/build.sh)
SN_GCC_SRC=/path/to/NGC_GNU_SRC/NGC tools/sn-gcc/build.sh

# 2. copy in your disc images
cp re4_debug_disc1.iso re4_debug_disc2.gcm orig/G4BE08/

# 3. build
python3 configure.py && ninja
```

`ninja` finishes with a progress report that should say 100% matched and linked. To check the result,
`build/tools/dtk shasum -c config/G4BE08/build.sha1` should print one `OK` per file, 115 in all.

When working on one unit, `python3 tools/bytecmp.py game/foo` compares its object file with the
original, and `python3 tools/fdiff.py game/foo <symbol>` shows one function side by side.

### What's where

- `src/game/` is the game itself. `src/em*/` are the enemies, `src/wep*/` the weapons, `src/pl*/` the
  player characters and `src/st*/` the rooms, one REL per room. `src/t_*/`, `src/Tools/` and
  `src/tools/` are the in-game debug editors, `src/Sscrn/` the menu screens and `src/lib/` the SDK,
  CRI and runtime libraries.
- `include/` has the headers, including the reconstructed structs.
- `config/G4BE08/` has the unit lists, symbols, splits, linker scripts and per-REL data.
- `tools/` has the build scripts, the compare tools, the SN compiler build and the research kit.
  `tools/motion_export.py` exports the game's animations to glTF and BVH, see `tools/motion/README.md`.
- `docs/overview.md` is a guide to how the engine fits together. `docs/matching.md` explains how the
  matching was done and `docs/unit-notes.md` has notes per unit. `docs/research/` is the detailed log.

### How close is the match

Every unit compiles to the original bytes with the original compilers. In some places the compiler
only picks the same registers or instruction order as the original if the code is written in a
particular way, and no natural way of writing it was found. Those spots are marked with a
`// COMPILER-DIFF:` comment, 526 of them at the moment. Most are a dead test, an empty `asm("")` or a
`register T x asm("rN")` declaration. None of them puts an instruction into the output, and
`python3 tools/asmcheck.py --all` checks that. `docs/matching.md` explains the reason behind each
kind. Getting this number down is ongoing work.

Almost all the assembly left is code the original developers also wrote in assembly, because their
compilers couldn't express it in C. That's the paired-single math and matrix routines, the GQR setup,
an exception handler, the cache and SPR code in the CRI video decoder, and eight startup, debugger and
decompression units that were assembly to begin with. In the game code `asmcheck.py` counts 231
instructions of it, with the eight assembly units counted separately. The exceptions are in the CRI
libraries: one small block in `dct_ac` that works around a difference between our build of
CodeWarrior and Capcom's, and 28 register pins that emit nothing. `docs/naming.md` lists all of it.

### Names

Function names are Capcom's own, from `Bio4.sym`. They are C++ mangled names, which is why the game
code is C++ and the SDK, CRI and C library code is C. File names and the split into units come from
the `D:/Bio4/Prog/<file>.cpp` paths the asserts left in the binaries.

Struct and field names come from three places. Most are Capcom's, taken from the debug information in
the PS2 debug build and matched to the GameCube layouts with `tools/ps2sym.py`. Some are ours, named
from how the code uses them. The rest are placeholders like `x1C`, named after their offset because
their meaning isn't known yet. Capcom's names keep Capcom's spelling, so the naming style is mixed on
purpose. Constants use the PS2 build's enums. `docs/naming.md` has the details.

### Contributing

See `CONTRIBUTING.md` for the build, the three checks a change has to pass and the rules. The main rule
is that the output bytes never change.

## Support

If you enjoy this project and would like to support my work, you can make a small contribution via PayPal.

Your support helps me spend more time maintaining existing projects, fixing bugs, improving compatibility, and working on new features.

[![Support via PayPal](https://img.shields.io/badge/Support%20via-PayPal-0070BA?logo=paypal&logoColor=white)](https://paypal.me/andiweli)

[![Join The Lair on Discord](https://img.shields.io/badge/Discord-The%20Lair-5865F2?logo=discord&logoColor=white)](https://discord.gg/AB3WVBqD5G)

Thank you for your support!

## Legal

CubeEvil4 is an unofficial fan project, not affiliated with or endorsed by Capcom, Nintendo or CRI
Middleware. Resident Evil is a trademark of Capcom; it is named here only to say which game the
port runs. The reconstructed game and SDK source is the property of Capcom, Nintendo and CRI
Middleware and is published for research and preservation only. No game data is included or
distributed; you need your own discs. The build scripts, tools, configuration and documentation
written for this project are released under CC0, see `LICENSE`.
