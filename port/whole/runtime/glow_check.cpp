// The light itself behind something (patch 0213; port/docs/whole-program-status.md, version 0.98.6). Compiled with
// game flags; called on the game thread from the glows' after-render tests.
//
// The game's glows (esp0e, esp45 and the espgen01 flare) are screen sprites drawn over everything; whether they
// show, each decides after the picture with GXPeekZ at 12 (esp45: 24) points on a ring of hide_r pixels around the
// light, never at the light itself. A light behind a head smaller than the ring keeps most of its glow: in r102 the
// lantern exactly behind Leon's head had 10 of its 12 points on the wall (ring 50 px) and shone at 80 % over his
// head (tester's R3 series, 06.11.2026; the same on the GameCube, the US, EU and Japanese code test the same way).
// Decision of the project owner (06.11.2026): when the light itself is covered, no glow. The ring stays as it was,
// and the light's own point counts only when something lies more than kGap in front of it, so a lamp's own glass,
// shade or housing (a few dozen centimetres around the light) never hides its glow.
#include "types.h"
#include "global.h"
#include "main_sub.h"
#include "view.h"

extern "C" void GXPeekZ(u16 x, u16 y, u32* z);
extern "C" void re4_log(const char* fmt, ...);

namespace {

const float kGap = 1000.0f;  // 1 m in the game's units
unsigned gHidden;            // lights found covered (the log)

}  // namespace

// Whether the depth at the light's screen position (sx, sy) lies more than kGap in front of the light (its view-space
// z, negative in front of the camera), by the game's own depth formula (esp0e Esp0e_HideCheck: ZNEAR, ZFAR, 24 bits).
// 0 when the point is off the picture (outside `margin` rows at top and bottom) or the light is within kGap of the
// camera: then the ring decides alone, as before.
extern "C" int re4_glow_center_hidden_at(float sx, float sy, float viewZ, float width, float height, float margin,
                                         float znear, float zfar)
{
    const float nz = viewZ + kGap;
    if (!(nz < -znear) || !(zfar > znear)) {
        return 0;
    }
    if (!(sx >= 0.0f && sx < width && sy >= margin && sy < height - margin)) {
        return 0;
    }
    const float inv = 1.0f / (zfar - znear);
    const float m22 = -znear * inv;
    const float m23 = -(zfar * znear) * inv;
    const float depth = (m23 + m22 * nz) / -nz + 1.0f;
    const s32 zi = (s32) (u32) (depth * 16777215.0f);
    u32 z = 0x00FFFFFF;
    GXPeekZ((u16) sx, (u16) sy, &z);
    return zi > (s32) (z & 0x00FFFFFF);
}

// The game's call (patch 0213): its screen, the letterbox margin as the glows' tests take it, ZNEAR / ZFAR.
extern "C" int re4_glow_center_hidden(float sx, float sy, float viewZ)
{
    const float margin = SysFlagChk(pG, SYS_SCISSOR_ON) ? 56.0f : 0.0f;
    const int hidden = re4_glow_center_hidden_at(sx, sy, viewZ, Screen.width, Screen.height, margin, ZNEAR, ZFAR);
    if (hidden && (gHidden++ % 1800) == 0) {
        re4_log("glow: a light behind something in front of it at (%.0f %.0f), its glow off (patch 0213; %u so far)",
                (double) sx, (double) sy, gHidden);
    }
    return hidden;
}
