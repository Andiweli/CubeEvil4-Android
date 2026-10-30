// The app menu's aim settings for touch and controllers (wish of the project owner, 30.10.2026: the
// Controls page, "Invert vertical" and "Aim speed" beside each other, for aiming the knife and the guns
// with a stick, the touch controls' or a controller's, not with the gyro). Compiled with the game's
// flags into libre4_main; the app sends both at its start and at every change (re4_core_set_aim).
//
//  - "Invert vertical" is the game's own option (its Options, Controller: "reverse", CFG_AIM_REVERSE in
//    pSys->Config_flg; PlWepLockCtrl, the scope, the binoculars and the harpoon read it). The app's switch
//    sets it at the next game frame (the game thread, a frame hook) and again after the system file was
//    read (card.cpp through runtime/game_language.cpp re4_game_language_after_card): the app's choice wins
//    over the saved one, as the language does. A change in the game's own options holds until then.
//  - "Aim speed" scales the stick's turn while aiming (patch 0151: PlWepLockCtrl and CameraScope::move),
//    0.5 .. 3, 1 as the game.
//  - "Auto aim" (wish of the project owner, 30.10.2026) is the game's own lock-on, CFG_LOCK_ON in
//    pSys->Config_flg, until now only in the debug menu's config page (AUTO_LOCK_ON): at aim start the
//    gun turns to the best enemy in front (cPlWep::lockInit, SearchLockEm, PlSetLockPitch) and follows it
//    for 10 frames (lockMove, PlWepAutoTrack); set as "Invert vertical" is.
//  - "Always run" (wish of the project owner, 30.10.2026): Leon runs without the run key and walks while
//    it is held (patch 0156: the four run checks of player.cpp ask re4_run_held). Only there: the key
//    keeps its other uses (cancel, quick turn, menus).
#include "types.h"
#include "global.h"
#include "main.h"

#include <atomic>
#include <cstdlib>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp

namespace {

std::atomic<int> gInvert(-1);       // the app's "Invert vertical", -1 before it sent anything
std::atomic<int> gLock(-1);         // the app's "Auto aim", -1 before it sent anything
std::atomic<bool> gRun(false);      // the app's "Always run"
std::atomic<int> gSpeed(100);       // the app's "Aim speed" in percent
std::atomic<bool> gPending(false);  // the game's option to be set at the next game frame
std::atomic<bool> gHooked(false);

void applyLock(const char* why)
{
    const int lock = gLock.load();
    if (lock < 0 || !pSys) {
        return;
    }
    if ((CfgFlagChk(pSys, CFG_LOCK_ON) != 0) == (lock != 0)) {
        return;
    }
    if (lock) {
        CfgFlagOn(pSys, CFG_LOCK_ON);
    } else {
        CfgFlagOff(pSys, CFG_LOCK_ON);
    }
    re4_log("aim: auto aim (the game's lock-on) %s (%s)", lock ? "on" : "off", why);
}

void apply(const char* why)
{
    applyLock(why);
    const int invert = gInvert.load();
    if (invert < 0 || !pSys) {
        return;
    }
    const bool now = CfgFlagChk(pSys, CFG_AIM_REVERSE) != 0;
    if (now == (invert != 0)) {
        return;
    }
    if (invert) {
        CfgFlagOn(pSys, CFG_AIM_REVERSE);
    } else {
        CfgFlagOff(pSys, CFG_AIM_REVERSE);
    }
    re4_log("aim: vertical aiming %s (%s)", invert ? "inverted" : "normal", why);
}

void hook(unsigned)
{
    if (gPending.exchange(false)) {
        apply("the app's switch");
    }
}

// For tests on the PC: RE4_TEST_AIM=bits at the start, as the app's re4_core_set_aim (bit 1 auto aim, bit 2
// always run, bits 8.. the speed; 0 there: 100 %), applied at the first game frame
struct StartSetting {
    StartSetting()
    {
        if (const char* v = getenv("RE4_TEST_AIM")) {
            const int bits = (int) strtol(v, 0, 0);
            const int speed = bits >> 8;
            gSpeed = speed >= 50 && speed <= 300 ? speed : 100;
            gInvert = bits & 1;
            gLock = (bits >> 1) & 1;
            gRun = (bits & 4) != 0;
            gHooked = true;
            gPending = true;
            re4_add_frame_hook(hook);
        }
    }
} gStartSetting;

}  // namespace

extern "C" {

// The app (core_api.cpp re4_core_set_aim): bit 0 "Invert vertical", bit 1 "Auto aim", bit 2 "Always run",
// bits 8 and up the aim speed in percent. Any thread.
void re4_aim_set(int bits)
{
    int speed = bits >> 8;
    if (speed < 50) speed = 50;
    if (speed > 300) speed = 300;
    const int invert = bits & 1;
    const int lock = (bits >> 1) & 1;
    const bool run = (bits & 4) != 0;
    const bool inverted = gInvert.exchange(invert) != invert;
    const bool faster = gSpeed.exchange(speed) != speed;
    const bool locked = gLock.exchange(lock) != lock;
    const bool ran = gRun.exchange(run) != run;
    if (inverted || faster || locked || ran) {
        re4_log("aim: stick aiming %s, speed %d %%, auto aim %s, always run %s", invert ? "inverted" : "normal",
                speed, lock ? "on" : "off", run ? "on" : "off");
    }
    if (!gHooked.exchange(true)) {
        re4_add_frame_hook(hook);
    }
    gPending.store(true);
}

// Patch 0151: the factor of the stick's turn while aiming (1: as the game).
f32 re4_aim_speed(void)
{
    return (f32) gSpeed.load(std::memory_order_relaxed) / 100.0f;
}

// Patch 0156 (player.cpp): the run key as Leon's walk and run routines see it; with "Always run" turned
// around (run without it, walk while it is held).
int re4_run_held(int held)
{
    return gRun.load(std::memory_order_relaxed) ? !held : held != 0;
}

// After the system file was read into pSys (game thread): the app's switch, not the saved one.
void re4_aim_after_card(void)
{
    apply("the app's switch over the system file");
}

}  // extern "C"
