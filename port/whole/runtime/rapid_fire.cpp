// "Rapid fire" (1.1.6, the project owner: Leon and every other playable character fire twice as fast; the app menu's
// Controls, off by default; patch 0262). How the game times a shot (30 game frames a second):
//  - the guns with a wait (pl_handgun.cpp: the handguns and magnums, PlShotFrameTbl by the firing-speed tune-up, a
//    handgun 14 frames a shot; wep17.cpp: the Matilda's burst, 4 frames a round, 13 to the next burst; pl_machine.cpp:
//    the TMP, the Chicago Typewriter and the other machine guns, 3 frames a round; pl_rifle.cpp: the rifles' frame
//    counters): the wait is halved (re4_rapid_fire_wait), the way the merchant's firing-speed tune-up shortens it; the
//    recoil motion plays as always and the next shot starts it again earlier. The machine guns' 3 frames cannot be
//    halved on a 30-frame grid: every other round is fired in the frame its wait ends (re4_rapid_fire_now), 1 and 2
//    frames in turn, 20 rounds a second.
//  - the guns whose next shot waits for a hand movement (pl_shotgun.cpp the pump, pl_rifle.cpp the bolt, pl_rocket.cpp,
//    pl_bow.cpp, wep14.cpp the mine thrower): the fire state's motion (the player's and the gun's own) plays at double
//    speed (re4_rapid_fire_begin / _end around the routine), so the movement is done in half the time.
// The knife, the grenades and the empty hand are not touched; reloading, ammo (one round each shot), the laser, the
// sound (one shot sound each shot) and the saves stay as they are. Off: as the game.
// RE4_TEST_AIM bit 30 (PC): the switch on from the start (runtime/aim_options.cpp).
#include "types.h"
#include "global.h"
#include "player.h"
#include "pl_wep.h"

#include <atomic>

extern "C" void re4_log(const char* fmt, ...);
extern GLOBAL_WK* pG;

namespace {
std::atomic<int> gOn(0);

bool on()
{
    return gOn.load(std::memory_order_relaxed) != 0;
}

// re4_rapid_fire_begin's doubling, undone by _end: the player whose motion was doubled and its speed before; the gun
// object whose motion runs doubled (compared by address only: it may be gone after a weapon change)
cPlayer* gDoubled;
f32 gSpeed = 1.0f;
cObjWep* gObj;
unsigned gOwed;   // re4_rapid_fire_now: rounds fired late so far (every other one is fired at once)
}  // namespace

extern "C" {

// The app (CoreBridge.setAim bit 30 via runtime/aim_options.cpp): the switch; any thread.
void re4_rapid_fire_set(int on)
{
    const int want = on ? 1 : 0;
    if (gOn.exchange(want) != want) {
        re4_log("rapid fire: %s", want ? "on (the guns fire twice as fast)" : "off (as the game)");
    }
}

int re4_rapid_fire_on(void)
{
    return on() ? 1 : 0;
}

// A wait of `frames` before a shot, and `extra` game frames the shot takes besides it (a handgun: its routine's set
// state and the shot's own frame): with the switch on, half of the whole time between two shots minus those frames,
// at least one frame. Off: `frames`.
f32 re4_rapid_fire_wait(f32 frames, f32 extra)
{
    if (!on()) {
        return frames;
    }
    f32 half = (frames + extra) * 0.5f - extra;
    return half < 1.0f ? 1.0f : half;
}

// The handgun's shot frame (PlShotFrameTbl[weapon][firing speed], the frames between two shots): the motion frame its
// recoil must pass before the next shot, as the game takes it (the shot frame - 2); with the switch on, the frame for
// half the time (whole frames: a shot frame of 21 waits 10).
f32 re4_rapid_fire_shot_frame(int shotFrame)
{
    if (!on()) {
        return (f32) (shotFrame - 2);
    }
    int half = shotFrame / 2 - 2;
    return (f32) (half < 1 ? 1 : half);
}

// The step of a frame counter the rifles count their wait with (1, 2 with the switch on).
int re4_rapid_fire_step(void)
{
    return on() ? 2 : 1;
}

// The rifles' fire delay counted down by a frame: by two with the switch on, not below 0.
int re4_rapid_fire_count_down(int count)
{
    const int left = count - re4_rapid_fire_step();
    return left < 0 ? 0 : left;
}

// The machine guns (pl_machine.cpp fire step 1, its wait for the next round ended in this frame): 1 when that round is
// fired at once in this frame (with the switch on, every other round), so that the rounds come 1 and 2 frames apart.
int re4_rapid_fire_now(void)
{
    if (!on()) {
        gOwed = 0;
        return 0;
    }
    return (gOwed++ & 1) != 0;
}

// Around a routine of the guns whose next shot waits for a hand movement: with the switch on and the player in its
// fire state (r_no_1 6 the weapon routine, r_no_2 2 fire), its motion at double speed for this call (the speed it had
// comes back in _end: the game's slow motion, STA_SLOW, keeps its own).
void re4_rapid_fire_begin(cPlayer* pl)
{
    if (!on() || !pl || pl->r_no_1 != 6 || pl->r_no_2 != 2 || gDoubled) {
        return;
    }
    gDoubled = pl;
    gSpeed = pl->Motion.Seq_speed;
    pl->Motion.Seq_speed = gSpeed * 2.0f;
}

// ... after it: the player's speed back, and the gun object's own fire motion (the pump, the bolt on the gun's model,
// cObjWep r_no_0 2) at double speed while it plays with the switch on, the player's speed again after it (the game
// gives both the same, cPlayer::setSlow).
void re4_rapid_fire_end(cPlayer* pl)
{
    f32 base = pl ? pl->Motion.Seq_speed : 1.0f;
    if (gDoubled && gDoubled == pl) {
        pl->Motion.Seq_speed = gSpeed;
        base = gSpeed;
    }
    gDoubled = 0;
    if (!pl || !pl->Wep) {
        gObj = 0;
        return;
    }
    cObjWep* obj = pl->Wep->m_pWep;
    if (gObj && gObj != obj) {
        gObj = 0;   // another weapon: the old one is gone with its speed
    }
    const bool fast = on() && obj && obj->r_no_0 == cObjWep::S_FIRE;
    if (fast) {
        gObj = obj;
        obj->Motion.Seq_speed = base * 2.0f;
    } else if (gObj) {
        gObj->Motion.Seq_speed = base;
        gObj = 0;
    }
}

}  // extern "C"
