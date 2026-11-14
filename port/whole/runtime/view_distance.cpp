// The app menu's "View distance" (PERFORMANCE, Potato mode; wish of the project owner, 09.11.2026, taken from the
// OUYA branch, port/docs/ouya-machbarkeit.md "Leichte OUYA-Fassung"): the distance fog nearer, and with it the far
// plane of the view the game culls against. The game already ties the two together: cLightMgr::setFog and hokanMove
// (src/game/light.cpp) program GX fog from the room's LightEnv.Fog and set View's far plane to the fog's end (scaled by
// the room's far_play_ratio); what lies beyond is not drawn (scenery, figures, effects). Patch 0223 asks
// re4_view_distance for the start and end the GPU gets and the far plane follows; LightEnv.Fog itself stays as the
// room set it (the game's interpolation between two cuts reads it each frame). A new choice takes effect at once:
// hokanMove sets the fog again when re4_view_distance_changed says so.
//
// The factor: re4_core_set_view_distance (percent, 20 .. 100; the menu sends 100 "Normal", 75 "Medium", 50 "Short",
// the OUYA's own setting) or RE4_VIEW_DISTANCE=f at the start (0.2 .. 1). Start and end times f, the end not below
// kMinEnd (a room's own end below it stays). Rooms without fog (type 0, mostly indoors) keep their view. At 1: the
// game's values unchanged.
// Checks: one log line for each new fog the GPU gets ("view: ..."), at most kLogLines.
#include "re4_runtime.h"
#include "re4_core.h"

#include <atomic>
#include <stdlib.h>
#include <string.h>

namespace {

const float kMinEnd = 6000.0f;  // 6 m (1000 units a metre): the shortest end a scaled fog gets
const int kLogLines = 300;

std::atomic<int> gPercent(-1);   // -1: not read yet (RE4_VIEW_DISTANCE)
std::atomic<int> gChanged(0);    // set by the menu, taken by re4_view_distance_changed (the game thread)
int gLogged;
int gLastType = -1;
float gLastStart, gLastEnd;

int clampPercent(int p)
{
    return p < 20 ? 20 : p > 100 ? 100 : p;
}

float factor()
{
    int p = gPercent.load(std::memory_order_acquire);
    if (p < 0) {
        const char* v = getenv("RE4_VIEW_DISTANCE");
        const float f = v && *v ? (float) atof(v) : 1.0f;
        int want = clampPercent((int) (f * 100.0f + 0.5f));
        int expected = -1;
        if (gPercent.compare_exchange_strong(expected, want) && want < 100) {
            re4_log("view: the distance fog and the view's far plane at %d %% of the room's (RE4_VIEW_DISTANCE)", want);
        }
        p = gPercent.load(std::memory_order_acquire);
    }
    return p / 100.0f;
}

}  // namespace

// The app menu (core_mode.cpp, the core process's kSetViewDistance): from the next frame on
extern "C" void re4_core_set_view_distance(int percent)
{
    const int want = clampPercent(percent);
    factor();  // (RE4_VIEW_DISTANCE read first, so it does not overwrite the menu)
    if (gPercent.exchange(want, std::memory_order_acq_rel) != want) {
        gChanged.store(1, std::memory_order_release);
        re4_log("view: the distance fog and the view's far plane at %d %% of the room's (the app's menu)", want);
    }
}

// (tests) RE4_VIEW_DISTANCE_TEST="N:P": after N game frames without a fog interpolation (N calls from hokanMove) the
// menu's choice P percent arrives, as from the app while the game runs
extern "C" int re4_view_distance_changed(void)
{
    static int testAt = -2, testPercent = 100, frames = 0;
    if (testAt == -2) {
        const char* t = getenv("RE4_VIEW_DISTANCE_TEST");
        testAt = -1;
        if (t && *t) {
            testAt = atoi(t);
            const char* colon = strchr(t, ':');
            testPercent = colon ? atoi(colon + 1) : 100;
        }
    }
    if (testAt >= 0 && ++frames == testAt) {
        re4_core_set_view_distance(testPercent);
    }
    return gChanged.exchange(0, std::memory_order_acq_rel);
}

extern "C" void re4_view_distance(int type, float* start, float* end)
{
    const float f = factor();
    const float s = *start, e = *end;
    if (f < 1.0f && type != 0) {
        float ne = e * f;
        if (ne < kMinEnd) {
            ne = e < kMinEnd ? e : kMinEnd;
        }
        // the start keeps its share of the end (a fog that begins at once stays so)
        *start = e > 0.0f ? s * (ne / e) : s;
        *end = ne;
    }
    if (f < 1.0f && gLogged < kLogLines &&
        (type != gLastType || (int) s != (int) gLastStart || (int) e != (int) gLastEnd)) {
        gLastType = type;
        gLastStart = s;
        gLastEnd = e;
        // during the game's interpolation between two cuts every frame differs: only the whole metres
        static int lastMetres = -1;
        const int metres = type * 100000 + (int) (e / 1000.0f);
        if (metres != lastMetres) {
            lastMetres = metres;
            gLogged++;
            re4_log("view: fog type %d, %.0f .. %.0f -> %.0f .. %.0f", type, s, e, *start, *end);
        }
    }
}
