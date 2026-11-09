// Leon's walking with the stick (decision of the project owner, 10.11.2026: "B + A", one switch for the controller
// and the touch controls). Compiled with the game's flags into libre4_main.
//
// The game makes four keys of the main stick (game/pad.cpp PadRead: up within 70 degrees of the stick's top, down
// beyond 110, left and right between 30 and 150, past 30 of its 72): eight ways. Leon walks forward at the walk
// motion's own speed and turns at a fixed SPEED_WALK_TURN (2.4 degrees a frame) while left or right is held, as on
// the GameCube.
//
// A, stepless turning (always on, patch 0227): the turns of walking, stepping back and running take the stick's
// sideways push into account (re4_turn_scale): full at 56 of 72 and beyond, slower below, 35 % at the least. The
// D-pad and a stick pushed fully sideways turn as the game does. (Always on: it changes nothing at a full push or
// with the D-pad, only a stick half way turns slower; one switch less in the menu.)
//
// B, "Modern movement" (the app menu's switch, off by default, bit 4 of the aim bits, re4_aim_set): Leon turns
// towards the way the stick points as seen from the camera and walks there. A stick filter in PADRead
// (runtime/pad.cpp re4_set_stick_filter), before the game reads the stick:
//
//  - only while the player walks freely: the game's main loop (Rno0 3), no sub screen, no event, the player in his
//    movement routines (r_no_0 0, r_no_1 0..4: standing, walking, stepping back, running, turning). Aiming, the
//    knife, ladders, the boat, the jet ski, pushing, jumping, cutscenes, quick-time events and menus keep the
//    stick as the game reads it;
//  - not on a frame with the aim, the knife, X, Y, Z or START pressed (the game's own use of that frame);
//  - the stick past the game's walking threshold and not pulled back (within 110 degrees of its top): pulled back
//    it stays as it is, Leon steps back as in the original (the project owner's choice);
//  - the camera's direction when the stick leaves its rest is the reference while it stays pushed (the shoulder
//    camera swings behind Leon as he turns: the camera of that moment, else he would walk in circles); at rest or
//    pulled back the next push takes the camera again;
//  - Leon turns towards the reference turned by the stick's angle, kTurn a frame at the most; the game gets the
//    stick straight up with the same push (it walks or runs forward, the run key as before). More than 100 degrees
//    off he first turns where he stands (the game gets no stick).
//
// Test on the PC: RE4_TEST_AIM bit 4 (aim_options.cpp) or RE4_MODERN_MOVE=1; RE4_TRACE_MOVE=1 logs every 15th frame
// the filter changed the stick (Leon's place, his angle, the target).
#include "types.h"
#include "global.h"
#include "main.h"
#include "joy.h"
#include "em.h"
#include "model.h"
#include "player.h"
#include "camera.h"

#include <atomic>
#include <math.h>
#include <stdlib.h>

extern GLOBAL_WK* pG;
extern cPlayer* pPL;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_set_stick_filter(void (*filter)(s8* x, s8* y, u16 buttons));  // runtime/pad.cpp

namespace {

const float kPi = 3.14159265f;
const float kTurn = 0.21f;          // radians a game frame at the most (about 360 degrees a second at 30 frames)
const float kOnSpot = 100.0f * kPi / 180.0f;
const float kBack = 110.0f * kPi / 180.0f;  // the game's "down" begins there (game/pad.cpp)
const float kPushed = 47.0f;        // raw: past the game's 30 after PADClamp's 15 (game/pad.cpp STICK_ON)
const u16 kOwnUse = 0x0020 | 0x0040 | 0x0400 | 0x0800 | 0x1000 | 0x0010;  // R, L, X, Y, START, Z

std::atomic<bool> gOn(false);
bool gHaveRef;
float gRef;
unsigned gFrames;
bool gTrace;

float wrap(float a)
{
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

bool walkingFreely()
{
    cPlayer* pl = pPL;
    return pG && pl && pG->Rno0 == 3 && !StaFlagChk(pG, STA_SUB_SCRN) && !StaFlagChk(pG, STA_EVENT) &&
           pl->r_no_0 == 0 && pl->r_no_1 <= 4;
}

void filter(s8* x, s8* y, u16 buttons)
{
    if (!gOn.load(std::memory_order_relaxed)) {
        gHaveRef = false;
        return;
    }
    const float fx = (float) *x, fy = (float) *y;
    const float push = sqrtf(fx * fx + fy * fy);
    const float stick = atan2f(fx, fy);  // 0 up, positive right
    if (push < kPushed || fabsf(stick) > kBack) {
        gHaveRef = false;  // at rest or pulled back: the next push takes the camera again
        return;
    }
    if ((buttons & kOwnUse) || !walkingFreely()) {
        static unsigned kept;
        if (gTrace && kept++ % 30 == 0 && pG) {
            re4_log("move: stick kept as the game reads it (buttons 0x%04x, step %d, sub screen %d, event %d, "
                    "routine %d/%d)", buttons, (int) pG->Rno0, StaFlagChk(pG, STA_SUB_SCRN) ? 1 : 0,
                    StaFlagChk(pG, STA_EVENT) ? 1 : 0, pPL ? (int) pPL->r_no_0 : -1, pPL ? (int) pPL->r_no_1 : -1);
        }
        return;
    }
    cPlayer* pl = pPL;
    if (!gHaveRef) {
        const Vec& from = pG->Camera.param.Campos;
        const Vec& to = pG->Camera.param.Target;
        const float dx = to.x - from.x, dz = to.z - from.z;
        gRef = dx * dx + dz * dz > 1.0f ? atan2f(dx, dz) : (float) pl->ang.y;
        gHaveRef = true;
    }
    // the game's angles turn left when they grow: the stick's right is the reference minus its angle
    const float target = wrap(gRef - stick);
    const float off = wrap(target - (float) pl->ang.y);
    const float step = off > kTurn ? kTurn : off < -kTurn ? -kTurn : off;
    pl->ang.y = wrap((float) pl->ang.y + step);
    if (fabsf(off) > kOnSpot) {
        *x = 0;
        *y = 0;
    } else {
        *x = 0;
        *y = (s8) (push > 127.0f ? 127 : (int) push);
    }
    if (gFrames++ % 900 == 0) {
        re4_log("move: modern movement turned the stick (%u frames so far)", gFrames);
    }
    if (gTrace && gFrames % 15 == 1) {
        re4_log("move: Leon at (%.0f %.0f) facing %.2f, target %.2f, stick (%d %d) as (%d %d), routine %d",
                (double) pl->pos.x, (double) pl->pos.z, (double) pl->ang.y, (double) target, (int) fx, (int) fy,
                (int) *x, (int) *y, (int) pl->r_no_1);
    }
}

struct Install {
    Install()
    {
        re4_set_stick_filter(filter);
        const char* on = getenv("RE4_MODERN_MOVE");
        if (on && *on == '1') {
            gOn = true;
        }
        const char* trace = getenv("RE4_TRACE_MOVE");
        gTrace = trace && *trace == '1';
    }
} gInstall;

}  // namespace

extern "C" {

// The app's switch (aim_options.cpp re4_aim_set, bit 4), any thread.
void re4_modern_move_set(int on)
{
    if (gOn.exchange(on != 0) != (on != 0)) {
        re4_log("move: modern movement %s", on ? "on" : "off");
    }
}

// Patch 0227 (player.cpp): the factor of the turn while walking, stepping back and running (A, stepless turning).
f32 re4_turn_scale(void)
{
    // how far the stick is pushed in its direction, of the most the controller's octagon lets it (PADClamp: 72 up,
    // down and across, 40 and 40 on a diagonal: the edge x + 0.8 y = 72 with x the larger part)
    const f32 x = (f32) abs((int) Joy[0].stickX), y = (f32) abs((int) Joy[0].stickY);
    const f32 m = sqrtf(x * x + y * y);
    if (x < 12.0f || m < 1.0f) {
        return 1.0f;  // the D-pad (or a key without the stick): the game's turn
    }
    const f32 big = x > y ? x : y, small = x > y ? y : x;
    const f32 push = m / (72.0f * m / (big + 0.8f * small));  // 0 .. 1 of the full push that way
    // the game turns from a push of 30 (0.42); full from 0.85 on, 35 % at the least
    f32 t = (push - 0.42f) / (0.85f - 0.42f);
    t = t > 1.0f ? 1.0f : t < 0.0f ? 0.0f : t;
    const f32 s = 0.35f + 0.65f * t;
    static unsigned calls;
    if (gTrace && calls++ % 15 == 0) {
        re4_log("move: turn at %.0f %% (stick %d %d, %.0f %% pushed)", (double) (s * 100.0f), (int) Joy[0].stickX,
                (int) Joy[0].stickY, (double) (push * 100.0f));
    }
    return s;
}

}  // extern "C"
