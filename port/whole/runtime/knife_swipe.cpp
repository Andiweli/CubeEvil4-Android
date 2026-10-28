// The knife by a swipe (wish of the project owner, 22.10.2026: draw the knife and slash with a
// swipe across the picture), compiled with the game's flags into libre4_main.
//
// The app hands a swipe over (re4_core_slash: tilt -1..1, 1 straight up; CoreActivity.watchTap,
// one finger beside the touch controls, with the switch of the app menu on). Once per game frame,
// before the game reads the pad (a frame hook in PADRead), it presses what a player would, on top
// of the controller (pad.cpp re4_pad_add); the game's own knife routine (pl_knife.cpp) does the
// rest:
//
//  - L (the knife stance, joyLKamae) until the knife is out (r_no_0 0, r_no_1 0xB, r_no_2 1 set);
//    started only where L draws it: the main loop, no sub screen, no event, Leon or Krauser with
//    the game's knife key option off, the player in a move routine (r_no_0 0, r_no_1 0-5, which
//    run actionSelect) or the knife already out. No A before that: in the move routines A is the
//    action button (doors, ladders, items);
//  - then L and A (the fire key, joyFireOn) until the slash starts (r_no_2 2). In the first of
//    these frames the swipe's tilt sets the height: the blend m3r (-1..1 for 45 degrees down .. up;
//    the draw took it from the camera's pitch) moves by the tilt beyond 0.35 (a mostly sideways
//    swipe slashes level), and the centre of the sway (cPlWep::pitch, m3r * PI/2) with it, as the
//    stick moves both in PlWepLockCtrl (and gyro.cpp);
//  - L alone while the slash swings, then 10 frames more: a further swipe in that time slashes
//    again at once. Released, the game puts the knife away.
// A swipe that cannot slash (see above, or the player hurt or busy) is dropped, one older than
// 400 ms as well. Each phase ends after its frames (draw 45, the slash's start 15, the swing 60)
// or when the player leaves the knife routine; the buttons then stop at once.
#include "types.h"
#include "global.h"
#include "main.h"
#include "player.h"
#include "pl_wep.h"

#include <time.h>

extern GLOBAL_WK* pG;
extern cPlayer* pPL;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));
extern "C" void re4_pad_add(unsigned short buttons);

namespace {

const u16 kPadL = 0x0040, kPadA = 0x0100;  // PAD_TRIGGER_L, PAD_BUTTON_A
const u32 kSwipeAgeMs = 400;
const unsigned kDrawFrames = 45, kFireFrames = 15, kSwingFrames = 60, kKeepFrames = 10;
const float kLevel = 0.35f;  // a tilt up to this slashes level
const float kPi = 3.14159265f;

// the app's thread writes, the game's reads: the tilt as float bits, when, counted
volatile u32 gTiltBits, gWhen, gSeq;
u32 gSeen;
bool gHooked;

enum Phase { kIdle, kDraw, kFire, kSwing, kKeep };
Phase gPhase = kIdle;
unsigned gFrames;  // in this phase
float gTilt;       // of the slash to come
bool gAimed;       // its tilt is set
bool gAgain;       // a swipe during the swing: slash again
float gAgainTilt;
unsigned gSlashes, gLines;

u32 nowMs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u32) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

u32 bits(float f)
{
    u32 b;
    __builtin_memcpy(&b, &f, 4);
    return b;
}

float value(u32 b)
{
    float f;
    __builtin_memcpy(&f, &b, 4);
    return f;
}

bool line()
{
    gLines++;
    return gLines <= 60 || gLines % 50 == 0;
}

// why the player cannot slash now (0: he can)
const char* blocked(cPlayer* pl)
{
    if (!pG || !pl || pG->Rno0 != 3) {
        return "not in a room";
    }
    if (StaFlagChk(pG, STA_SUB_SCRN)) {
        return "a sub screen is open";
    }
    if (StaFlagChk(pG, STA_EVENT)) {
        return "an event runs";
    }
    if (pG->pl_type != 0 && pG->pl_type != 4) {
        return "this character has no knife on L";
    }
    if (pSys && CfgFlagChk(pSys, CFG_KNIFE_MODE)) {
        return "the game's knife key option is on";
    }
    if (!pl->Wep) {
        return "no weapon work";
    }
    return 0;
}

bool knifeOut(cPlayer* pl)
{
    return pl->r_no_0 == 0 && pl->r_no_1 == 0xB;
}

// a move routine (actionSelect draws the knife on L) or the knife routine
bool canDraw(cPlayer* pl)
{
    return pl->r_no_0 == 0 && (pl->r_no_1 <= 5 || pl->r_no_1 == 0xB);
}

void stop(const char* why)
{
    if (why && line()) {
        re4_log("knife: swipe slash stopped (%s)", why);
    }
    gPhase = kIdle;
    gFrames = 0;
    gAgain = false;
}

void enter(Phase p)
{
    gPhase = p;
    gFrames = 0;
    if (p == kFire) {
        gAimed = false;
    }
}

// the swipe's tilt on the aim, once per slash (the frame A goes down)
void aim(cPlayer* pl)
{
    gAimed = true;
    const float a = gTilt < 0.0f ? -gTilt : gTilt;
    if (a <= kLevel) {
        return;
    }
    const f32 before = m3r;
    const float d = (a > 1.0f ? 1.0f - kLevel : a - kLevel) / (1.0f - kLevel);
    m3r += gTilt < 0.0f ? -d : d;
    m3r.limit(-1.0f, 1.0f);
    pl->Wep->pitch += ((f32) m3r - before) * (kPi / 2.0f);
    pl->Wep->m_LockTime = 0;
}

void knifeHook(unsigned)
{
    bool fresh = false;
    float tilt = 0.0f;
    const u32 seq = __atomic_load_n(&gSeq, __ATOMIC_ACQUIRE);
    if (seq != gSeen) {
        gSeen = seq;
        if (nowMs() - __atomic_load_n(&gWhen, __ATOMIC_RELAXED) <= kSwipeAgeMs) {
            fresh = true;
            tilt = value(__atomic_load_n(&gTiltBits, __ATOMIC_RELAXED));
        }
    }
    cPlayer* pl = pPL;
    const char* why = blocked(pl);
    if (gPhase == kIdle) {
        if (!fresh) {
            return;
        }
        if (!why && !canDraw(pl)) {
            why = "the player is busy";
        }
        if (why) {
            if (line()) {
                re4_log("knife: swipe ignored (%s)", why);
            }
            return;
        }
        gTilt = tilt;
        enter(knifeOut(pl) && pl->r_no_2 == 1 ? kFire : kDraw);
    } else if (why) {
        stop(why);
        return;
    } else if (fresh) {
        if (gPhase == kSwing || gPhase == kKeep) {
            gAgain = true;
            gAgainTilt = tilt;
        } else {
            gTilt = tilt;  // not slashed yet: the latest swipe's height
        }
    }

    switch (gPhase) {
    case kDraw:
        if (knifeOut(pl) && pl->r_no_2 == 1) {
            enter(kFire);
            break;
        }
        if (!canDraw(pl) || ++gFrames > kDrawFrames) {
            stop("the knife did not come out");
            return;
        }
        re4_pad_add(kPadL);
        return;
    case kSwing:
        if (!knifeOut(pl)) {
            stop("the player left the knife");
            return;
        }
        if (pl->r_no_2 == 2 && ++gFrames <= kSwingFrames) {
            re4_pad_add(kPadL);
            return;
        }
        if (pl->r_no_2 == 2) {
            stop("the slash did not end");
            return;
        }
        if (gAgain) {
            gAgain = false;
            gTilt = gAgainTilt;
            enter(kFire);
            break;
        }
        enter(kKeep);
        break;
    default:
        break;
    }

    if (gPhase == kFire) {
        if (!knifeOut(pl)) {
            stop("the player left the knife");
            return;
        }
        if (++gFrames > kFireFrames) {
            stop("the knife did not slash");
            return;
        }
        if (pl->r_no_2 == 2) {
            gSlashes++;
            if (line()) {
                re4_log("knife: slash %u by a swipe (tilt %+.2f)", gSlashes, gTilt);
            }
            enter(kSwing);
            re4_pad_add(kPadL);
            return;
        }
        if (pl->r_no_2 == 1 && !gAimed) {
            aim(pl);
        }
        re4_pad_add(kPadL | kPadA);
        return;
    }
    if (gPhase == kKeep) {
        if (gAgain) {
            gAgain = false;
            gTilt = gAgainTilt;
            enter(kFire);
            if (knifeOut(pl) && pl->r_no_2 == 1) {
                aim(pl);
            }
            re4_pad_add(kPadL | kPadA);
            return;
        }
        if (!knifeOut(pl) || ++gFrames > kKeepFrames) {
            stop(0);
            return;
        }
        re4_pad_add(kPadL);
    }
}

} // namespace

// The app (CoreBridge.slash): a swipe while playing, tilt 1 straight up .. -1 straight down; any
// thread.
extern "C" void re4_core_slash(float tilt)
{
    if (!gHooked) {
        gHooked = true;
        re4_add_frame_hook(knifeHook);
        re4_log("knife: swipes draw the knife and slash");
    }
    if (!(tilt == tilt)) {
        tilt = 0.0f;
    }
    __atomic_store_n(&gTiltBits, bits(tilt), __ATOMIC_RELAXED);
    __atomic_store_n(&gWhen, nowMs(), __ATOMIC_RELAXED);
    __atomic_fetch_add(&gSeq, 1, __ATOMIC_RELEASE);
}
