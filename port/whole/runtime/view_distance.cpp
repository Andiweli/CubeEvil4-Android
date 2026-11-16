// The app menu's "View distance" (PERFORMANCE, Potato mode; wish of the project owner, 09.11.2026, taken from the
// OUYA branch, port/docs/ouya-machbarkeit.md "Leichte OUYA-Fassung"): the distance fog nearer, and with it the far
// plane of the view the game culls against. The game already ties the two together: cLightMgr::setFog and hokanMove
// (src/game/light.cpp) program GX fog from the room's LightEnv.Fog and set View's far plane to the fog's end (scaled by
// the room's far_play_ratio); what lies beyond is not drawn (scenery, figures, effects). Patch 0223 asks re4_view_fog
// for the fog the GPU gets and the far plane; LightEnv.Fog itself stays as the room set it (the game's interpolation
// between two cuts reads it each frame). A new choice takes effect at once: hokanMove sets the fog again when
// re4_view_distance_changed says so.
//
// The factor: re4_core_set_view_distance (percent; the menu sends 100 "Normal", 65 "Medium" (75 until 0.99.18), 50
// "Short", since 0.99.18 35 "None" (the OUYA's own setting, "Very short" at first) and 160 "Far" (150 in 0.99.18)) or
// RE4_VIEW_DISTANCE=f at the start. At 1: the game's values unchanged. Rooms without fog (type 0, mostly indoors) keep
// their view at every level.
// - Below 1: start and end times f, the end not below kMinEnd (a room's own end below it stays), the far plane from
//   that end as the game does. Below Short (kDenseBelow, "None") the GPU's fog then ends at the far plane (the OUYA
//   branch's patch 0901): where the game stops drawing the fog is dense, so houses do not vanish from a thin fog.
// - Above 1 ("Far", since 1.0.0, the project owner's choice after the analysis in port/docs/whole-program-status.md,
//   1.0.0 section 1): the fog's end times f, its start as the room's, and the far plane out to where that fog is
//   kFarDensity dense (never nearer than the game's own, never past the fog's end). The view reaches further in every
//   room with fog (the forest's half-dense distance 37 -> 60 m). 0.99.18's Far kept the room's fog and only moved the
//   far plane; in rooms with a dense fog at the game's far plane (forest, gorge, lake) that drew up to twice as much
//   behind walls and in the fog and showed nothing more (RE4_VIEW_FAR_MODE=keep, measurements).
//
// Checks: one log line for each new fog the GPU gets ("view: ..."), at most kLogLines; RE4_VIEW_LOG=1 logs the
// room's fog at "Normal" as well (the measurements for the levels, 0.99.18). Unit check: re4_host --runtime-test
// "viewfog" (tests/runtime_tests.cpp).
#include "re4_runtime.h"
#include "re4_core.h"

#include <atomic>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

const float kMinEnd = 6000.0f;  // 6 m (1000 units a metre): the shortest end a scaled fog gets
const int kLogLines = 300;
const int kMinPercent = 20, kMaxPercent = 300;
const float kDenseBelow = 0.5f;  // levels below Short's 50 % ("Very short") get the GPU's fog dense at the far plane
const float kFarDensity = 0.95f; // "Far": the far plane out to where its fog is this dense

std::atomic<int> gPercent(-1);   // -1: not read yet (RE4_VIEW_DISTANCE)
std::atomic<int> gChanged(0);    // set by the menu, taken by re4_view_distance_changed (the game thread)
int gLogged;
int gLastType = -1;
float gLastStart, gLastEnd, gLastRatio;
int gLogAll = -1;                // RE4_VIEW_LOG

int clampPercent(int p)
{
    return p < kMinPercent ? kMinPercent : p > kMaxPercent ? kMaxPercent : p;
}

float factor()
{
    int p = gPercent.load(std::memory_order_acquire);
    if (p < 0) {
        const char* v = getenv("RE4_VIEW_DISTANCE");
        const float f = v && *v ? (float) atof(v) : 1.0f;
        int want = clampPercent((int) (f * 100.0f + 0.5f));
        int expected = -1;
        if (gPercent.compare_exchange_strong(expected, want) && want != 100) {
            re4_log("view: the distance fog and the view's far plane at %d %% of the room's (RE4_VIEW_DISTANCE)", want);
        }
        p = gPercent.load(std::memory_order_acquire);
    }
    return p / 100.0f;
}

bool logAll()
{
    if (gLogAll < 0) {
        const char* v = getenv("RE4_VIEW_LOG");
        gLogAll = v && *v && *v != '0' ? 1 : 0;
    }
    return gLogAll != 0;
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

// How far into the fog `density` (0..1) is reached: GX's perspective fog curves (GXSetFog's types, as the app's renderer
// draws them, gx_gl_shader.cpp): x = (eye z - start) / (end - start), then 2 linear x, 4 exp 1 - 2^(-8x), 5 exp2
// 1 - 2^(-8x^2), 6 reverse exp 2^(-8(1-x)), 7 reverse exp2 2^(-8(1-x)^2). Other types: the end.
extern "C" float re4_view_fog_reach(int type, float start, float end, float density)
{
    float x = 1.0f;
    if (density <= 0.0f) {
        x = 0.0f;
    } else if (density < 1.0f) {
        switch (type) {
        case 2: x = density; break;
        case 4: x = -log2f(1.0f - density) / 8.0f; break;
        case 5: x = sqrtf(-log2f(1.0f - density) / 8.0f); break;
        case 6: x = 1.0f + log2f(density) / 8.0f; break;
        case 7: x = 1.0f - sqrtf(-log2f(density) / 8.0f); break;
        default: x = 1.0f; break;
        }
    }
    x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
    return start + x * (end - start);
}

// The fog the GPU gets and the far plane (patch 0223): `start`, `end` the room's fog (LightEnv.Fog) in, the GPU's out;
// `farPlane` the game's (end * (1 - farRatio) + 1) in, the one to cull against out. Type 0 (no fog): unchanged.
// `farMode` (f above 1): 0 "Far", the fog's end times f, its start kept, the far plane out to where that fog is
// `farDensity` dense; (measurements) 1 the fog's start and end times f as below 1 (the far plane follows), 2 0.99.18's
// Far, the room's fog kept and only the far plane out to where it is `farDensity` dense
extern "C" void re4_view_fog_at(float f, int dense, int farMode, float farDensity, int type, float farRatio,
                                float* start, float* end, float* farPlane)
{
    const float s = *start, e = *end;
    if (type == 0 || f == 1.0f || e <= 0.0f) {
        return;
    }
    if (f > 1.0f && farMode != 1) {
        const float ne = farMode == 2 ? e : e * f;
        float reach = re4_view_fog_reach(type, s, ne, farDensity) + 1.0f;
        reach = reach > ne + 1.0f ? ne + 1.0f : reach;
        if (reach > *farPlane) {
            *farPlane = reach;
        }
        *end = ne;
        return;
    }
    float ne = e * f;
    if (f < 1.0f && ne < kMinEnd) {
        ne = e < kMinEnd ? e : kMinEnd;
    }
    // the start keeps its share of the end (a fog that begins at once stays so)
    float ns = s * (ne / e);
    float fp = ne * (1.0f - farRatio) + 1.0f;
    if (f < 1.0f && dense) {
        // dense at the far plane (the OUYA branch's patch 0901), not nearer than kMinEnd: the short fogs between two
        // cuts of an event (6 m and a far plane of 2.4 m in the car of r100) misted the figures a metre or two away
        const float least = ne < kMinEnd ? ne : kMinEnd;
        const float edge = fp < least ? least : fp;
        if (edge < ne) {
            ns *= edge / ne;
            ne = edge;
        }
    }
    *start = ns;
    *end = ne;
    *farPlane = fp;
}

extern "C" void re4_view_fog(int type, float farRatio, float* start, float* end, float* farPlane)
{
    const float f = factor();
    const float s = *start, e = *end, fp = *farPlane;
    // the GPU's fog dense at the far plane: "Very short" (below Short's 50 %); Medium and Short as since 0.99.6
    // (measurements: RE4_VIEW_DENSE=0 never, 2 at every shorter level)
    static int denseMode = -1;
    if (denseMode < 0) {
        const char* v = getenv("RE4_VIEW_DENSE");
        denseMode = v && *v ? atoi(v) : 1;
    }
    const int dense = denseMode == 2 ? 1 : denseMode == 1 ? f < kDenseBelow : 0;
    // (measurements) RE4_VIEW_FAR_MODE=scale: "Far" scales the fog as the shorter levels do; keep: 0.99.18's Far
    static int farMode = -1;
    static float farDensity = kFarDensity;  // (measurements) RE4_VIEW_FAR_DENSITY
    if (farMode < 0) {
        const char* v = getenv("RE4_VIEW_FAR_MODE");
        farMode = v && strcmp(v, "scale") == 0 ? 1 : v && strcmp(v, "keep") == 0 ? 2 : 0;
        const char* d = getenv("RE4_VIEW_FAR_DENSITY");
        if (d && *d) {
            farDensity = (float) atof(d);
        }
    }
    re4_view_fog_at(f, dense, farMode, farDensity, type, farRatio, start, end, farPlane);
    if ((f != 1.0f || logAll()) && gLogged < kLogLines &&
        (type != gLastType || (int) s != (int) gLastStart || (int) e != (int) gLastEnd || farRatio != gLastRatio)) {
        gLastType = type;
        gLastStart = s;
        gLastEnd = e;
        gLastRatio = farRatio;
        // during the game's interpolation between two cuts every frame differs: only the whole metres
        static int lastMetres = -1;
        const int metres = type * 100000 + (int) (e / 1000.0f);
        if (metres != lastMetres) {
            lastMetres = metres;
            gLogged++;
            re4_log("view: fog type %d, %.0f .. %.0f far %.0f (ratio %.3f) -> %.0f .. %.0f far %.0f", type, s, e,
                    type == 0 ? 0.0f : fp, farRatio, *start, *end, type == 0 ? 0.0f : *farPlane);
        }
    }
}
