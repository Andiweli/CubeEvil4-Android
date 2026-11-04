// The laser sight's colour (wish of the project owner, 04.11.2026: CONTROLS, LASER SIGHT: Default (Red), Blue,
// Green, Yellow). Compiled with the game's flags into libre4_main; patch 0198 calls re4_laser_tint for the two
// effects the game makes for the sight each frame: the beam (EspDrawLaserLine, est 3, cEsp19: a GX line in the
// effect's colour) and the dot (drawPoint, est 0x50, its colour on the dot's texture).
//
// The game's red is a colour with a strong red part and a weaker white part: per colour (start, current and its
// change per frame) the white part w = (g + b) / 2 stays and the red part r - w goes into the chosen colour's
// direction, so brightness, fading and the white core are the game's own. "Default" (0) changes nothing: the
// picture is the game's, unchanged. Any thread sets it (re4_core_set_laser_colour); RE4_LASER_COLOUR=N at the
// start for the PC checks.
#include "types.h"
#include "global.h"
#include "esp.h"

#include <stdlib.h>

extern "C" void re4_log(const char* fmt, ...);

namespace {

volatile int gColour = -1;  // -1 not read yet; 0 default (red), 1 blue, 2 green, 3 yellow

// the chosen colour's direction (its brightest part 1)
const float kTint[4][3] = {
    {1.0f, 0.0f, 0.0f},
    {0.25f, 0.55f, 1.0f},
    {0.2f, 1.0f, 0.25f},
    {1.0f, 0.9f, 0.1f},
};

int colour()
{
    if (gColour < 0) {
        const char* e = getenv("RE4_LASER_COLOUR");
        gColour = e && *e >= '0' && *e <= '3' ? *e - '0' : 0;
        if (gColour)
            re4_log("laser sight: colour %d (RE4_LASER_COLOUR)", gColour);
    }
    return gColour;
}

void tint(f32& r, f32& g, f32& b, const float* t)
{
    const f32 w = (g + b) * 0.5f, red = r - w;
    r = w + red * t[0];
    g = w + red * t[1];
    b = w + red * t[2];
}

u8 clamp8(f32 v)
{
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (u8) (v + 0.5f);
}

unsigned gLogged;

}  // namespace

extern "C" {

void re4_core_set_laser_colour(int c)
{
    const int want = c >= 0 && c <= 3 ? c : 0;
    if (gColour != want)
        re4_log("laser sight: colour %d", want);
    gColour = want;
}

// Patch 0198: the beam (dot 0) or the dot (dot 1) the game just made.
void re4_laser_tint(cEsp* esp, int dot)
{
    const int c = colour();
    if (!esp || c == 0)
        return;
    const float* t = kTint[c];
    if (gLogged < 2) {
        gLogged++;
        re4_log("laser sight: %s (est 0x%x, type %u) colour %.0f %.0f %.0f %.0f, change %.1f %.1f %.1f, start %u %u %u",
                dot ? "dot" : "beam", (unsigned) esp->m_Id, (unsigned) esp->m_Type, (double) esp->m_Col_r,
                (double) esp->m_Col_g, (double) esp->m_Col_b, (double) esp->m_Col_a, (double) esp->m_Col_d_r,
                (double) esp->m_Col_d_g, (double) esp->m_Col_d_b, (unsigned) esp->m_Col_start_r,
                (unsigned) esp->m_Col_start_g, (unsigned) esp->m_Col_start_b);
    }
    tint(esp->m_Col_r, esp->m_Col_g, esp->m_Col_b, t);
    tint(esp->m_Col_d_r, esp->m_Col_d_g, esp->m_Col_d_b, t);
    f32 r = esp->m_Col_start_r, g = esp->m_Col_start_g, b = esp->m_Col_start_b;
    tint(r, g, b, t);
    esp->m_Col_start_r = clamp8(r);
    esp->m_Col_start_g = clamp8(g);
    esp->m_Col_start_b = clamp8(b);
}

}  // extern "C"
