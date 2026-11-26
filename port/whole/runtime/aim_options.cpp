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
//  - "Reload without aiming" (1.1.0, keyboard; the idea from a user's "ModernTPS" mod, our own code): the app asks for
//    one reload when the keyboard's reload key is pressed without the aim (re4_core_quick_reload); patch 0241 lets
//    cPlayer::keyReload answer yes once, within 15 game frames. The game itself then reloads through its own way
//    (actionSelect while standing or walking, setRno 6/4; without the aim key PlReloadDirect 1, the direct reload of
//    its control type 1), only when the weapon can reload (reloadable), never in a scene or another routine.
//  - "Vibration" (1.1.0, a user's report 19.11.2026: no rumble on a DualSense nor the phone): the game's own
//    switch (its Options, Controller: "vibration", CFG_VIBRATION in pSys->Config_flg) follows the app's menu:
//    on unless the app's "Vibration" is Off. The game's switch was off in most testers' system files (the
//    prototype's new system file has it off, and its Options apply a change only with A), so the game never
//    started the motor and the app had nothing to pass on. Set as "Invert vertical" is.
#include "types.h"
#include "global.h"
#include "main.h"

#include <atomic>
#include <cstdlib>
#include <string.h>
#include <time.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_health_bars_set(int on);                 // runtime/health_bars.cpp (1.1.0)
extern "C" void re4_stack_items_set(int step);               // runtime/cheats.cpp (1.1.0, patch 0254)
extern "C" void re4_bloody_set(int on);                      // runtime/bloody.cpp (1.1.3, patch 0258)
extern "C" void re4_rapid_fire_set(int on);                  // runtime/rapid_fire.cpp (1.1.6, patch 0263)
extern "C" void re4_merchant_all_set(int bits);              // runtime/cheats.cpp (1.1.3, patch 0259)

namespace {

std::atomic<int> gInvert(-1);       // the app's "Invert vertical", -1 before it sent anything
std::atomic<int> gLock(-1);         // the app's "Auto aim", -1 before it sent anything
std::atomic<int> gVib(-1);          // the app's "Vibration" not Off, -1 before it sent anything (bits 4/5)
std::atomic<bool> gRun(false);      // the app's "Always run"
std::atomic<bool> gFar(false);      // the app's "Far camera" (patch 0171)
std::atomic<int> gFov(0);           // the app's "Field of view" in degrees, -15 .. 15 (patch 0205)

// The field of view from the aim bits: bits 24..29 hold degrees + 32 (0: an app without it, as the game).
int fovOf(int bits)
{
    const int field = (bits >> 24) & 63;
    if (field == 0) {
        return 0;
    }
    const int fov = field - 32;
    return fov < -15 ? -15 : fov > 15 ? 15 : fov;
}
std::atomic<int> gSpeed(100);       // the app's "Aim speed" in percent
std::atomic<bool> gPending(false);  // the game's option to be set at the next game frame
std::atomic<bool> gHooked(false);
std::atomic<int> gFromGame(-1);     // the game's own options changed "reverse" (patch 0188): 0 / 1 for the app
std::atomic<long> gReloadAt{-1};    // when the app asked for a reload (re4_core_quick_reload), ms; -1 none waiting
unsigned gReloadLines;

long nowMs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
std::atomic<int> gVibFromGame(-1);  // the game's own options changed "vibration" (1.1.0): 0 / 1 for the app

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

void applyVibration(const char* why)
{
    const int vib = gVib.load();
    if (vib < 0 || !pSys) {
        return;
    }
    if ((CfgFlagChk(pSys, CFG_VIBRATION) != 0) == (vib != 0)) {
        return;
    }
    if (vib) {
        CfgFlagOn(pSys, CFG_VIBRATION);
    } else {
        CfgFlagOff(pSys, CFG_VIBRATION);
    }
    re4_log("pad: the game's vibration %s (%s)", vib ? "on" : "off", why);
}

void apply(const char* why)
{
    applyLock(why);
    applyVibration(why);
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
// always run, bit 5 vibration sent with bit 4 on, bits 8.. the speed; 0 there: 100 %), applied at the first game
// frame
struct StartSetting {
    StartSetting()
    {
        if (const char* v = getenv("RE4_TEST_AIM")) {
            const int bits = (int) strtol(v, 0, 0);
            const int speed = (bits >> 8) & 0xFFF;
            gSpeed = speed >= 50 && speed <= 300 ? speed : 100;
            gFov = fovOf(bits);
            gInvert = bits & 1;
            gLock = (bits >> 1) & 1;
            gRun = (bits & 4) != 0;
            gFar = (bits & 8) != 0;
            gVib = (bits & 0x20) ? (bits >> 4) & 1 : -1;
            re4_health_bars_set((bits >> 20) & 1);
            re4_stack_items_set((bits >> 21) & 3);
            re4_bloody_set((bits >> 23) & 1);
            re4_merchant_all_set((bits >> 6) & 3);
            re4_rapid_fire_set((bits >> 30) & 1);
            gHooked = true;
            gPending = true;
            re4_add_frame_hook(hook);
        }
    }
} gStartSetting;

// For tests on the PC: RE4_TEST_QUICK_RELOAD="F[,F...]": at PADRead frame F the app's reload without aiming
unsigned gQuickTest[8];
int gQuickTests;

void quickHook(unsigned frame)
{
    for (int i = 0; i < gQuickTests; i++) {
        if (gQuickTest[i] == frame) {
            re4_log("aim: RE4_TEST_QUICK_RELOAD at frame %u", frame);
            gReloadAt.store(nowMs());
        }
    }
}

struct QuickTest {
    QuickTest()
    {
        const char* v = getenv("RE4_TEST_QUICK_RELOAD");
        for (const char* p = v; p && *p && gQuickTests < 8;) {
            gQuickTest[gQuickTests++] = (unsigned) strtoul(p, 0, 10);
            p = strchr(p, ',');
            p = p ? p + 1 : 0;
        }
        if (gQuickTests) {
            re4_add_frame_hook(quickHook);
        }
    }
} gQuickTestSetting;

}  // namespace

extern "C" {

// The app (core_api.cpp re4_core_set_aim): bit 0 "Invert vertical", bit 1 "Auto aim", bit 2 "Always run",
// bit 3 "Far camera", bit 4 the game's vibration on with bit 5 set (1.1.0; bit 5 clear: the game's own switch), bits 6/7
// the cheats "All weapons" / "All tune-ups at the merchant" (1.1.3, runtime/cheats.cpp, patch 0259),
// bits 8..19 the aim speed in percent, bit 20 "Enemy health bars" (1.1.0, runtime/health_bars.cpp), bits 21..22 the cheat
// "Stack items" (1.1.0, runtime/cheats.cpp, patch 0254: 0 Standard, 1 250, 2 500, 3 999), bit 23 the cheat "Bloody mode"
// (1.1.3, runtime/bloody.cpp, patch 0258), bits 24..29
// "Field of view" (degrees + 32; 0.98.4), bit 30 "Rapid fire" (1.1.6, runtime/rapid_fire.cpp, patch 0263). Any thread.
void re4_aim_set(int bits)
{
    re4_rapid_fire_set((bits >> 30) & 1);
    re4_health_bars_set((bits >> 20) & 1);
    re4_stack_items_set((bits >> 21) & 3);
    re4_bloody_set((bits >> 23) & 1);
    re4_merchant_all_set((bits >> 6) & 3);
    int speed = (bits >> 8) & 0xFFF;
    if (speed < 50) speed = 50;
    if (speed > 300) speed = 300;
    const int invert = bits & 1;
    const int lock = (bits >> 1) & 1;
    const bool run = (bits & 4) != 0;
    const bool far = (bits & 8) != 0;
    const bool inverted = gInvert.exchange(invert) != invert;
    const bool faster = gSpeed.exchange(speed) != speed;
    const bool locked = gLock.exchange(lock) != lock;
    const bool ran = gRun.exchange(run) != run;
    const bool farther = gFar.exchange(far) != far;
    const int fov = fovOf(bits);
    const bool wider = gFov.exchange(fov) != fov;
    const int vib = (bits & 0x20) ? (bits >> 4) & 1 : -1;
    if (gVib.exchange(vib) != vib) {
        re4_log("pad: the app's vibration %s", vib < 0 ? "not sent (the game's own switch)" : vib ? "on" : "off");
    }
    if (inverted || faster || locked || ran || farther || wider) {
        re4_log("aim: stick aiming %s, speed %d %%, auto aim %s, always run %s, far camera %s, field of view %+d degrees",
                invert ? "inverted" : "normal", speed, lock ? "on" : "off", run ? "on" : "off", far ? "on" : "off", fov);
    }
    if (!gHooked.exchange(true)) {
        re4_add_frame_hook(hook);
    }
    gPending.store(true);
}

// Patch 0171 (cam_qfps.cpp): the shoulder camera further back while walking and standing.
int re4_far_camera(void)
{
    return gFar.load(std::memory_order_relaxed) ? 1 : 0;
}

// Patch 0205 (cam_qfps.cpp): degrees added to the shoulder camera's vertical field of view (0: as the game).
f32 re4_fov_offset(void)
{
    return (f32) gFov.load(std::memory_order_relaxed);
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

// Patch 0188 (OptionScreen::quit, game thread): the game's own options set "reverse"; that is the app's
// "Invert vertical" from now on, and the app keeps it (re4_aim_from_game).
void re4_aim_options_closed(void)
{
    if (!pSys || gPending.load()) {
        return;
    }
    // "vibration" too (1.1.0): the app's "Vibration" becomes Off, or Auto when it was Off
    const int vib = CfgFlagChk(pSys, CFG_VIBRATION) != 0;
    const int sent = gVib.load();
    if (sent >= 0 && sent != vib) {
        gVib = vib;
        gVibFromGame = vib;
        re4_log("pad: the game's vibration %s (the game's own options; the app keeps it)", vib ? "on" : "off");
    }
    const int now = CfgFlagChk(pSys, CFG_AIM_REVERSE) != 0;
    if (gInvert.load() == now) {
        return;
    }
    gInvert = now;
    gFromGame = now;
    re4_log("aim: vertical aiming %s (the game's own options; the app keeps it)", now ? "inverted" : "normal");
}

// The app (core_api.cpp re4_core_aim_from_game): -1, or the game's own choice since the last call (0 / 1).
int re4_aim_from_game(void)
{
    return gFromGame.exchange(-1);
}

// The app (core_api.cpp re4_core_vibration_from_game): -1, or the game's own "vibration" since the last call.
int re4_vibration_from_game(void)
{
    return gVibFromGame.exchange(-1);
}

// The app (core_api.cpp re4_core_quick_reload, any thread): the keyboard's reload key without the aim
void re4_quick_reload_request(void)
{
    gReloadAt.store(nowMs());
}

// Patch 0241 (cPlayer::keyReload, game thread): 1 once for a request of the last half second (one pressed during a
// scene or a menu lapses); PlReloadDirect as the game's control type 1 sets it (1 without the aim key: direct)
int re4_quick_reload_take(int aimHeld)
{
    const long at = gReloadAt.load();
    if (at < 0) {
        return 0;
    }
    gReloadAt.store(-1);
    if (nowMs() - at > 500) {
        return 0;
    }
    if (gReloadLines < 30) {
        gReloadLines++;
        re4_log("aim: reload without aiming asked (the keyboard), %s", aimHeld ? "while aiming" : "direct");
    }
    return 1;
}

// After the system file was read into pSys (game thread): the app's switch, not the saved one.
void re4_aim_after_card(void)
{
    apply("the app's switch over the system file");
}

}  // extern "C"
