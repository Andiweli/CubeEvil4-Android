// Gyro aiming of the core mode (wish of the project owner, 19.10.2026: as in
// the first Android port, leon_preview.cpp aimTick), compiled with the game's
// flags into libre4_main. The app hands the device's turn rates over
// (re4_core_gyro, rad/s about the screen's axes: GyroAim.java); once per game
// frame, before the player and the camera move (a frame hook in PADRead), they
// turn the aim, and only while the player aims a gun:
//
//  - the game runs its main loop (gameMainLoop), no sub screen, no event;
//  - the player's routine is the weapon's (r_no_0 0, r_no_1 6: pl_R1_Weapon),
//    not the knife (PlGetWeaponNo 0x10: the knife routine, L held, or the
//    knife as the equipped weapon);
//  - with the scope (STA_SCOPE_CAMERA, CamCtrl.r0 0x10: rifles, the rocket and
//    mine launchers) the body's yaw and the scope's pitch (CameraScope
//    angle_x, negative is up, kept within its limits), less at a higher zoom
//    as the stick (CameraScope::move);
//  - else while the gun is set or fires (r_no_2 1, 2): the body's yaw
//    (pPL->ang.y, positive turns left, which the aim's sway never touches)
//    and the aim's pitch (m3r, -1..1 for -45..45 degrees, positive is up)
//    together with the centre of the sway (cPlWep::pitch, m3r * PI/2):
//    PlWepLockRand pulls m3r back to that centre on the frames the stick
//    rests. A started auto aim (lock-on option, cPlWep::m_LockTime) ends.
//
// The harpoon in the boat of the Del Lago fight too (wish of the project owner, 23.10.2026): while
// the player aims it (pl0f plboat_R2_SpearSet / SpearSet2, routine 0/0xF/3 or 0/0xF/0xA, step 3,
// where PLBOAT_AIM_CONTROL reads the stick) the yaw moves the sight across (m_BoatPlDir, -22.5 ..
// 22.5 degrees, positive left; at the limits the boat turns, as with the stick) and the pitch up
// and down (m_Blend, -255 .. 255, positive up), as many pixels of the sight per degree up and
// down as across (plboatSightCurGet: 488.92 pixels per radian across, 0.390625 per unit up).
//
// The first port's gain: 35 % of the device's turn, at 30 game frames a
// second (main.cpp SetSystemVcnt(2)). A rate older than 150 ms counts as none
// (the app stopped sending).
//
// Swipe aiming (re4_core_aim_turn; the menu's "Touch Aiming: Touch Area", user report and wish of the
// project owner, 29.10.2026: as in mobile shooters, the aim follows the finger and stops when it
// stops, no stick that keeps turning): the app hands each finger movement over as angles, summed up
// to the next game frame and turned there all at once, on the same ways as the gyro (the scope's
// zoom too), with no gain.
#include "types.h"
#include "global.h"
#include "em.h"
#include "model.h"
#include "player.h"
#include "pl_wep.h"
#include "pl_sub.h"
#include "cam_ctrl.h"
#include "cam_extra.h"

#include <time.h>

extern GLOBAL_WK* pG;
extern cPlayer* pPL;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));

namespace {

const float kGain = 0.35f;
const float kFrame = 1.0f / 30.0f;
const float kPi = 3.14159265f;
const float kBoatDir = 0.3926991f;                  // the harpoon sight's limit across (pl0f)
const float kBoatBlend = 488.92398f / 0.390625f;    // m_Blend units per radian up, for the same pixels

// the app's thread writes, the game's reads: the floats as bits, and when (ms)
volatile u32 gYawBits, gPitchBits, gWhen;
bool gHooked;
unsigned gTurned;  // frames the gyro turned the aim (the log)
// the swipe's angles since the last game frame (re4_core_aim_turn; the app's thread adds them)
volatile u32 gSwipeLock;
float gSwipeYaw, gSwipePitch;
unsigned gSwiped;  // frames a swipe turned the aim (the log)

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

void lockSwipe()
{
    while (__atomic_exchange_n(&gSwipeLock, 1u, __ATOMIC_ACQUIRE)) {
    }
}

void unlockSwipe()
{
    __atomic_store_n(&gSwipeLock, 0u, __ATOMIC_RELEASE);
}

// Turns the aim by yaw (positive left) and pitch (positive up), in radians, where the player aims;
// false: not aiming (nothing turned).
bool turnAim(float yaw, float pitch)
{
    cPlayer* pl = pPL;
    if (!pG || !pl || pG->Rno0 != 3 || StaFlagChk(pG, STA_SUB_SCRN) || StaFlagChk(pG, STA_EVENT)) {
        return false;
    }
    if (pl->r_no_0 == 0 && pl->r_no_1 == 0xF && (pl->r_no_2 == 3 || pl->r_no_2 == 0xA) && pl->r_no_3 == 3 &&
        pl->m_pBoat) {
        const float d = yaw;
        float dir = pl->m_BoatPlDir + d;
        if (dir > kBoatDir) {
            dir = kBoatDir;
            pl->m_pBoat->ang.y += d;
        }
        if (dir < -kBoatDir) {
            dir = -kBoatDir;
            pl->m_pBoat->ang.y += d;
        }
        pl->m_BoatPlDir = dir;
        float blend = pl->m_Blend + pitch * kBoatBlend;
        if (blend > 255.0f) blend = 255.0f;
        if (blend < -255.0f) blend = -255.0f;
        pl->m_Blend = blend;
        return true;
    }
    if (!pl->Wep || pl->r_no_0 != 0 || pl->r_no_1 != 6 || PlGetWeaponNo() == 0x10) {
        return false;
    }
    if (StaFlagChk(pG, STA_SCOPE_CAMERA) && CamCtrl.r0 == 0x10 && CamCtrl.m_pProc) {
        CameraScope* scope = (CameraScope*) CamCtrl.m_pProc;
        f32 zoom = 0.0f, angle = 0.0f;
        scope->getParam(&zoom, &angle);
        const float gain = 1.0f - 0.9f * zoom;
        pl->ang.y += yaw * gain;
        float x = scope->angle_x - pitch * gain;
        if (x < scope->angle_min) x = scope->angle_min;
        if (x > scope->angle_max) x = scope->angle_max;
        scope->angle_x = x;
    } else if (pl->r_no_2 == 1 || pl->r_no_2 == 2) {
        pl->ang.y += yaw;
        const f32 before = m3r;
        m3r += pitch / (kPi / 4.0f);
        m3r.limit(-1.0f, 1.0f);
        pl->Wep->pitch += ((f32) m3r - before) * (kPi / 2.0f);
        pl->Wep->m_LockTime = 0;
    } else {
        return false;
    }
    return true;
}

void gyroHook(unsigned)
{
    // the swipe since the last frame: taken whether it turns or not (a swipe while not aiming is gone)
    lockSwipe();
    const float swipeYaw = gSwipeYaw, swipePitch = gSwipePitch;
    gSwipeYaw = gSwipePitch = 0.0f;
    unlockSwipe();
    if (swipeYaw != 0.0f || swipePitch != 0.0f) {
        const bool harpoon = pPL && pPL->r_no_1 == 0xF;
        if (turnAim(swipeYaw, swipePitch) && gSwiped++ % 900 == 0) {
            re4_log("touch: the %s turned by a swipe (%u frames so far)", harpoon ? "harpoon's sight" : "aim", gSwiped);
        }
    }
    const float yaw = value(__atomic_load_n(&gYawBits, __ATOMIC_RELAXED));
    const float pitch = value(__atomic_load_n(&gPitchBits, __ATOMIC_RELAXED));
    if (yaw == 0.0f && pitch == 0.0f) {
        return;
    }
    if (nowMs() - __atomic_load_n(&gWhen, __ATOMIC_RELAXED) > 150) {
        return;
    }
    const bool harpoon = pPL && pPL->r_no_1 == 0xF;
    if (turnAim(yaw * kGain * kFrame, pitch * kGain * kFrame) && gTurned++ % 900 == 0) {
        re4_log(harpoon ? "gyro: the harpoon's sight moved by the gyroscope (%u frames so far)"
                        : "gyro: the aim turned by the gyroscope (%u frames so far)",
                gTurned);
    }
}

} // namespace

// The app (CoreBridge.aimTurn): a swipe of the touch area while aiming, in radians (yaw positive
// left, pitch positive up), added up to the next game frame. Any thread.
extern "C" void re4_core_aim_turn(float yaw, float pitch)
{
    if (!(yaw == yaw) || !(pitch == pitch)) {
        return;
    }
    if (!__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
        re4_add_frame_hook(gyroHook);
    }
    lockSwipe();
    gSwipeYaw += yaw;
    gSwipePitch += pitch;
    unlockSwipe();
}

// The app (CoreBridge.gyro): the device's turn rates, any thread; 0, 0: still.
extern "C" void re4_core_gyro(float yaw, float pitch)
{
    if (!__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
        re4_add_frame_hook(gyroHook);
        re4_log("gyro: aiming by the gyroscope while a gun is aimed");
    }
    __atomic_store_n(&gWhen, nowMs(), __ATOMIC_RELAXED);
    __atomic_store_n(&gYawBits, bits(yaw == yaw ? yaw : 0.0f), __ATOMIC_RELAXED);
    __atomic_store_n(&gPitchBits, bits(pitch == pitch ? pitch : 0.0f), __ATOMIC_RELAXED);
}
