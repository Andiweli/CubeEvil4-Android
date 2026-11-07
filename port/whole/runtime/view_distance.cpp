// The OUYA branch's shorter view (wish of the project owner, 07.11.2026; port/docs/ouya-machbarkeit.md,
// "Leichte OUYA-Fassung"): the distance fog nearer, and with it the far plane of the view the game culls
// against. The game already ties the two together: cLightMgr::setFog and hokanMove (src/game/light.cpp)
// program GX fog from the room's LightEnv.Fog and set View's far plane to the fog's end (scaled by the
// room's far_play_ratio); what lies beyond is not drawn (scenery, figures, effects). Patch 0900 asks
// re4_view_distance for the start and end the GPU gets and the far plane follows; LightEnv.Fog itself stays
// as the room set it (the game's interpolation between two cuts reads it each frame).
//
// RE4_VIEW_DISTANCE=f (0.2 .. 1; default 1, as the game): start and end times f, the end not below
// kMinEnd (a room's own end below it stays). Rooms without fog (type 0, mostly indoors) keep their view.
// Checks: one log line for each new fog the GPU gets ("view: ..."), at most kLogLines.
#include "re4_runtime.h"

#include <stdlib.h>

namespace {

const float kMinEnd = 6000.0f;  // 6 m (1000 units a metre): the shortest end a scaled fog gets
const int kLogLines = 300;

float gFactor = -1.0f;  // -1: not read yet (RE4_VIEW_DISTANCE)
int gLogged;
int gLastType = -1;
float gLastStart, gLastEnd;

void readEnv()
{
    if (gFactor >= 0.0f) {
        return;
    }
    const char* v = getenv("RE4_VIEW_DISTANCE");
    float f = v && *v ? (float) atof(v) : 1.0f;
    gFactor = f < 0.2f ? 0.2f : f > 1.0f ? 1.0f : f;
    if (gFactor < 1.0f) {
        re4_log("view: the distance fog and the view's far plane at %.2f of the room's (RE4_VIEW_DISTANCE)",
                gFactor);
    }
}

}  // namespace

extern "C" void re4_view_distance_set(float factor)
{
    gFactor = factor < 0.2f ? 0.2f : factor > 1.0f ? 1.0f : factor;
}

extern "C" void re4_view_distance(int type, float* start, float* end)
{
    readEnv();
    const float s = *start, e = *end;
    if (gFactor < 1.0f && type != 0) {
        float ne = e * gFactor;
        if (ne < kMinEnd) {
            ne = e < kMinEnd ? e : kMinEnd;
        }
        // the start keeps its share of the end (a fog that begins at once stays so)
        *start = e > 0.0f ? s * (ne / e) : s;
        *end = ne;
    }
    if (gLogged < kLogLines && (type != gLastType || (int) s != (int) gLastStart || (int) e != (int) gLastEnd)) {
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
