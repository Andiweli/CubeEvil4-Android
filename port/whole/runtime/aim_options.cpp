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
#include "types.h"
#include "global.h"
#include "main.h"

#include <atomic>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp

namespace {

std::atomic<int> gInvert(-1);       // the app's "Invert vertical", -1 before it sent anything
std::atomic<int> gSpeed(100);       // the app's "Aim speed" in percent
std::atomic<bool> gPending(false);  // the game's option to be set at the next game frame
std::atomic<bool> gHooked(false);

void apply(const char* why)
{
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

}  // namespace

extern "C" {

// The app (core_api.cpp re4_core_set_aim): bit 0 "Invert vertical", bits 8 and up the aim speed in
// percent. Any thread.
void re4_aim_set(int bits)
{
    int speed = bits >> 8;
    if (speed < 50) speed = 50;
    if (speed > 300) speed = 300;
    const int invert = bits & 1;
    const bool inverted = gInvert.exchange(invert) != invert;
    const bool faster = gSpeed.exchange(speed) != speed;
    if (inverted || faster) {
        re4_log("aim: stick aiming %s, speed %d %%", invert ? "inverted" : "normal", speed);
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

// After the system file was read into pSys (game thread): the app's switch, not the saved one.
void re4_aim_after_card(void)
{
    apply("the app's switch over the system file");
}

}  // extern "C"
