// The app menu's "Aggressive frame skip" (saves CPU; wish of the project owner, 10.11.2026, version 0.99.8; patch 0227):
// the game leaves a picture out itself when its frames run late. The renderer's Frame skip (core_mode.cpp,
// GxRenderer::Skip) only leaves out a picture the game has built already: it saves the GPU's time, not the
// processor's. The game's frame (src/game/main.cpp) spends most of its processor time on the pictures: Render (the
// ordering tables the frame before filled: the models' GX commands, the port's vertex conversion) and Trans (the
// models' culling, lights and skinned vertices); the logic (the task scheduler: Leon, enemies, effects, cameras) took
// 7-13 % on the PC (r103, r105, 10.11.2026). Patch 0227:
//
//   - before Trans, re4_cpu_skip_decide: did the frame just gone take longer than a game frame (two fields,
//     SetSystemVcnt(2))? Then this frame's Trans registers no objects and enemies (re4_cpu_skip_models), and
//   - at the next frame's start, re4_cpu_skip_picture says so: Render_before, Render (Render_skip instead), the
//     cinema bars, the fade quads, DrawOTag, the debug texts, Render_done (Render_done_skip: the scenario's C task
//     keeps its time slices) and Render_swap are left out. The VI keeps the picture before.
//   - Everything else runs as in every frame: the logic, IdSys, the effects' and filters' registration (rain and snow
//     move in Filter06Trans), the fades (their quad only is not drawn), sound, DVD. The texture animations and
//     scrolls of the next picture advance once more for each picture left out (re4_cpu_skip_tex_steps).
//
// The clock: the next picture is left out when the frame just gone (from one decision to the next) took longer than
// its two fields and kSlackNs, at most one in a row (the picture at least every second game frame, 15 a second).
// A frame of more than 250 ms (a load, the app paused) does not count, and being ahead gives no credit. Unlike the
// Xbox port of Shaolin Monks (its FrameSkip, which carries a lag up to 250 ms on until the world caught up), a lag
// is not carried on: RE4's frames wait for the retrace (main.cpp: vsync_cnt starts at 0 each frame), so a frame
// left out is not shorter than two fields and the game cannot catch up; a carried lag stayed for good after one
// load and every second picture was left out on the PC that kept its pace (10.11.2026). Only while the player
// plays (main.cpp: Rno0 3, no movie, sub screen, held picture or stopped transform). 60 FPS (the renderer's
// in-between pictures) and this exclude each other: the app sends "off" while 60 FPS is on (core_mode.cpp).
//
// Switches: re4_core_set_cpu_skip (the app); RE4_CPU_SKIP=1 on from the start (PC); RE4_CPU_SKIP_MAX=2 or 3 (a test
// switch: pictures in a row); RE4_CPU_SKIP_TEST=1 leaves out every picture the rules allow, without the clock (the
// PC's check that the game runs the same as without: the state trace, RE4_TRACE); RE4_CPU_SKIP_SLOW_DRAW=MS makes each
// picture drawn MS milliseconds of processor time dearer (a busy wait at the frame's start: the PC as a phone with a
// slow processor, where drawing takes most of the frame).
// Checks: "frame skip (CPU): ..." on the first picture left out and every 500, the "frame: ..." line of
// state_trace.cpp counts them per 600 frames.
#include "re4_runtime.h"
#include "re4_core.h"

#include <atomic>
#include <stdlib.h>
#include <time.h>

extern "C" {
int re4_cpu_skip_models;          // Trans (patch 0227): this frame registers no objects and enemies
int re4_cpu_skip_tex_steps = 1;   // Trans: the texture steps of the models it registers (1 + pictures left out)
int re4_cpu_skip_now;             // this frame's picture is left out (fade.cpp)
}

namespace {

const long long kFieldNs = 1000000000LL * 1001 / 60000;  // an NTSC field (runtime/vi.cpp)
const long long kStepNs = 2 * kFieldNs;                  // a game frame
const long long kSlackNs = 2000000LL;                    // the retrace's own jitter: not late
const long long kMaxLagNs = 250000000LL;                 // a longer frame (a load, a pause) does not count

std::atomic<int> gWant(-1);  // the app's choice (-1: not set yet, RE4_CPU_SKIP)
bool gOn;                    // as the game thread sees it
int gMaxRun = 1;
bool gTest;
long long gSlowDrawNs;  // RE4_CPU_SKIP_SLOW_DRAW
bool gRead;
long long gLast;   // the last decision (0: none yet)
int gRun;          // pictures left out in a row
int gPending;      // the next frame's picture is left out
unsigned long long gLeftOut;
std::atomic<unsigned> gWindow{0};

long long nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void readEnv()
{
    if (gRead) {
        return;
    }
    gRead = true;
    const char* on = getenv("RE4_CPU_SKIP");
    int expected = -1;
    if (on && *on && gWant.compare_exchange_strong(expected, atoi(on) != 0 ? 1 : 0) && atoi(on) != 0) {
        re4_log("frame skip (CPU): on (RE4_CPU_SKIP)");
    }
    if (const char* max = getenv("RE4_CPU_SKIP_MAX")) {
        const int m = atoi(max);
        gMaxRun = m < 1 ? 1 : m > 3 ? 3 : m;
        re4_log("frame skip (CPU): at most %d pictures in a row (RE4_CPU_SKIP_MAX)", gMaxRun);
    }
    const char* test = getenv("RE4_CPU_SKIP_TEST");
    gTest = test && atoi(test) != 0;
    if (gTest) {
        re4_log("frame skip (CPU): every picture the rules allow is left out, without the clock (RE4_CPU_SKIP_TEST)");
    }
    if (const char* slow = getenv("RE4_CPU_SKIP_SLOW_DRAW")) {
        gSlowDrawNs = (long long) (atof(slow) * 1000000.0);
        re4_log("frame skip (CPU): each picture drawn costs %.1f ms more (RE4_CPU_SKIP_SLOW_DRAW)", gSlowDrawNs / 1e6);
    }
}

}  // namespace

// The app menu (core_mode.cpp, the core process's kSetCpuSkip): from the next frame on
extern "C" void re4_core_set_cpu_skip(int on)
{
    readEnv();
    const int want = on ? 1 : 0;
    if (gWant.exchange(want) != want) {
        re4_log("frame skip (CPU): %s", want ? "on (the game leaves a picture out while its frames run late)"
                                             : "off");
    }
}

// The game thread before Trans (main.cpp, patch 0227): whether this frame's picture is left out. `allowed`: the
// player plays.
extern "C" void re4_cpu_skip_decide(int allowed)
{
    readEnv();
    const bool on = gWant.load() == 1 || (gWant.load() < 0 && gTest);
    const long long now = nowNs();
    const long long frame = gLast ? now - gLast : 0;  // the frame just gone
    gLast = now;
    bool skip = false;
    if (on && allowed && gTest) {
        skip = gRun < gMaxRun;
    } else if (on && allowed) {
        // late: longer than its two fields; a frame of more than 250 ms (a load, the app paused) does not count
        skip = frame > kStepNs + kSlackNs && frame <= kMaxLagNs && gRun < gMaxRun;
    }
    gOn = on;
    re4_cpu_skip_models = skip ? 1 : 0;
    if (!skip) {
        re4_cpu_skip_tex_steps = 1 + gRun;
    }
    gRun = skip ? gRun + 1 : 0;
    gPending = skip ? 1 : 0;
    if (skip) {
        gLeftOut++;
        gWindow.fetch_add(1);
        if (gLeftOut == 1 || gLeftOut % 500 == 0) {
            re4_log("frame skip (CPU): %llu pictures left out so far (a frame took longer than its two fields)", gLeftOut);
        }
    }
}

// The game thread at the frame's start (main.cpp, patch 0227): 1 when the picture the last frame prepared is left out
extern "C" int re4_cpu_skip_picture(void)
{
    const int p = gPending;
    gPending = 0;
    re4_cpu_skip_now = p;
    if (!p && gSlowDrawNs > 0) {
        readEnv();
        for (const long long until = nowNs() + gSlowDrawNs; nowNs() < until;) {
        }
    }
    return p;
}

// The pictures left out since the last call (state_trace.cpp's "frame: ..." line)
extern "C" unsigned re4_cpu_skip_window(void)
{
    return gWindow.exchange(0);
}

// The switch as the game thread used it last (state_trace.cpp)
extern "C" int re4_cpu_skip_on(void)
{
    return gOn ? 1 : 0;
}
