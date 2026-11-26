// "Move and aim" (1.1.5, wish of the project owner, 26.11.2026; the app menu's Controls, "Stick options", off by
// default; compiled with the game's flags into libre4_main): while Leon aims, the app's left stick (a controller's, the
// touch move stick, the keyboard's move keys) walks him at the same time, forward, back and to the sides as he faces,
// and the aim stays on the other stick (the right stick, the touch area or the touch C stick, the mouse). The app only
// sends it with the option on and only while L or R is held; it then keeps the left stick off the game's main stick
// (CoreInput.pad), so the game's aim (PlWepLockCtrl, which reads the main stick) turns by the other stick alone.
//
// The app hands the stick over (re4_core_move_aim: x right, y forward, -1 .. 1); once per game frame, before the player
// moves (a frame hook in PADRead), it moves him, and only:
//
//  - while the game runs its main loop (gameMainLoop), no sub screen, no event, the keys not stopped (SPF_KEY);
//  - with a gun set or firing (routine 0/6, the weapon's r_no_2 1 or 2: every weapon's table has its ready 0, set 1,
//    fire 2; not the draw 0, the reload 4, the holstering or a throw's next step) or the knife held or slashing
//    (routine 0/0xB, r_no_2 1 or 2);
//  - not with the scope or the binoculars up (the left stick zooms there, as before), not crouching, not in the boat.
//
// Leon moves at kSpeed (60 % of his walk, by the stick's tilt) along his own facing; nothing else of the game changes.
// The game has no walking aim motions: move_aim_legs.cpp plays his walk on the legs below the aim (1.1.x). The game's own collision then pushes him
// off walls, objects and enemies as when he walks (cPlayer::move: EmAtCheck, SatMgr.check, every frame in every
// routine) and keeps him on the floor. Damage, a grab, an event or the end of the aim change his routine, and the
// movement stops by itself. The app sends the stick when it changes (and 0, 0 when L and R are let go, the option goes
// off or the app loses its input), so the last one holds as the game's pad does.
#include "types.h"
#include "global.h"
#include "em.h"
#include "model.h"
#include "player.h"
#include "pl_wep.h"
#include "pl_sub.h"

#include <math.h>
#include <stdlib.h>

extern GLOBAL_WK* pG;
extern cPlayer* pPL;
extern cEm* pSUB;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));
extern "C" long re4_trace_free_at(void);  // runtime/state_trace.cpp

namespace {

// Leon's walk: about 52 units (mm) a game frame on the flat (measured on the PC, r100, RE4_TRACE_MOVE); 60 % of it
const float kSpeed = 0.6f * 52.0f;
const float kDead = 0.15f;  // the app's stick already has its dead zone; a little more against a resting stick's noise
volatile u32 gXBits, gYBits;
bool gHooked;
unsigned gMoved;      // frames it moved him (the log)
bool gToldWhyNot;     // (the log: once why a stick did not move him)
// this frame's move for the legs (move_aim_legs.cpp, after his routine): x right, y forward (the stick's direction),
// the speed in units a frame; gMovedNow false when the hook did not move him
bool gMovedNow;
float gMoveX, gMoveY, gMoveSpeed;

// RE4_TEST_MOVE_AIM="x,y,from,to" (frames after the player is free): the app's stick at each game frame from .. to;
// RE4_TRACE_MOVE=1: one "move:" line a frame (his routine, his position) from the player being free
float gTestX, gTestY;
long gTestFrom = -1, gTestTo = -1;
bool gTrace;
unsigned gTraceLines;

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

// Where Leon may move while he aims; else why not (the log)
const char* why(cPlayer* pl)
{
    if (!pG || !pl || pG->Rno0 != 3) {
        return "not in play";
    }
    if (StaFlagChk(pG, STA_SUB_SCRN)) {
        return "sub screen";
    }
    if (StaFlagChk(pG, STA_EVENT)) {
        return "event";
    }
    if (SpfFlagChk(pG, SPF_KEY)) {
        return "keys stopped";
    }
    if (StaFlagChk(pG, STA_SCOPE_CAMERA) || StaFlagChk(pG, STA_BINOCULAR) || pl->stat.check(cPlayer::F_BINOCULAR)) {
        return "scope or binoculars";
    }
    if (pl->m_pBoat || pl->stat.check(cPlayer::F_CROUCH)) {
        return "boat or crouching";
    }
    if (pl->r_no_0 != 0 || !pl->Wep) {
        return "routine";
    }
    const bool set = pl->r_no_2 == 1 || pl->r_no_2 == 2;
    if (pl->r_no_1 == 0xB && set) {
        return 0;  // the knife held or slashing
    }
    if (pl->r_no_1 == 6 && set && PlGetWeaponNo() != 0x10) {
        return 0;  // a gun set or firing
    }
    return "not aiming";
}

void moveHook(unsigned frame)
{
    const long freeAt = re4_trace_free_at();
    if (freeAt >= 0 && gTestFrom >= 0 && (long) frame == freeAt + gTestFrom) {
        __atomic_store_n(&gXBits, bits(gTestX), __ATOMIC_RELAXED);
        __atomic_store_n(&gYBits, bits(gTestY), __ATOMIC_RELAXED);
    }
    if (freeAt >= 0 && gTestFrom >= 0 && (long) frame == freeAt + gTestTo + 1) {
        __atomic_store_n(&gXBits, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&gYBits, 0u, __ATOMIC_RELAXED);
    }
    cPlayer* pl = pPL;
    if (gTrace && freeAt >= 0 && pl && gTraceLines < 6000) {
        gTraceLines++;
        cEm* sub = pSUB;
        re4_log("move: frame %u pl %u/%u/%u/%u pos %.1f %.1f %.1f ang %.4f hp %d sub %d hp %d pos %.0f %.0f %.0f r %u/%u",
                frame, (unsigned) pl->r_no_0, (unsigned) pl->r_no_1, (unsigned) pl->r_no_2, (unsigned) pl->r_no_3,
                (double) pl->pos.x, (double) pl->pos.y, (double) pl->pos.z, (double) pl->ang.y, (int) pl->hp,
                sub ? (int) sub->id : -1, sub ? (int) sub->hp : 0, sub ? (double) sub->pos.x : 0.0,
                sub ? (double) sub->pos.y : 0.0, sub ? (double) sub->pos.z : 0.0, sub ? (unsigned) sub->r_no_0 : 0u,
                sub ? (unsigned) sub->r_no_1 : 0u);
    }
    float x = value(__atomic_load_n(&gXBits, __ATOMIC_RELAXED));
    float y = value(__atomic_load_n(&gYBits, __ATOMIC_RELAXED));
    if (!(x == x) || !(y == y)) {
        return;
    }
    float len = sqrtf(x * x + y * y);
    if (len < kDead) {
        return;
    }
    if (len > 1.0f) {
        x /= len;
        y /= len;
        len = 1.0f;
    }
    const char* no = why(pl);
    if (no) {
        if (!gToldWhyNot && pl && pG && pG->Rno0 == 3 && (pl->r_no_1 == 6 || pl->r_no_1 == 0xB)) {
            gToldWhyNot = true;
            re4_log("move and aim: the left stick moved nothing (%s, routine %u/%u/%u)", no, (unsigned) pl->r_no_0,
                    (unsigned) pl->r_no_1, (unsigned) pl->r_no_2);
        }
        return;
    }
    // scaled from the dead zone's edge, so a small tilt starts slowly
    const float k = kSpeed * (len - kDead) / (1.0f - kDead) / len;
    // his facing: ang.y 0 looks along +z, positive turns left; forward (sin, cos), right (-cos, sin)
    const float s = sinf(pl->ang.y), c = cosf(pl->ang.y);
    pl->pos.x += (s * y - c * x) * k;
    pl->pos.z += (c * y + s * x) * k;
    gMovedNow = true;
    gMoveX = x;
    gMoveY = y;
    gMoveSpeed = k * len;
    if (gMoved++ % 900 == 0) {
        re4_log("move and aim: Leon moved while aiming (%u frames so far; routine %u/%u/%u, weapon 0x%02x)", gMoved,
                (unsigned) pl->r_no_0, (unsigned) pl->r_no_1, (unsigned) pl->r_no_2, (unsigned) PlGetWeaponNo());
    }
}

void hook()
{
    if (!__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
        re4_add_frame_hook(moveHook);
    }
}

// the test switches at the library's start
struct TestInit {
    TestInit()
    {
        const char* t = getenv("RE4_TEST_MOVE_AIM");
        if (t && *t) {
            float x = 0, y = 0;
            long from = -1, to = -1;
            char* e;
            x = strtof(t, &e);
            if (*e == ',') y = strtof(e + 1, &e);
            if (*e == ',') from = strtol(e + 1, &e, 10);
            if (*e == ',') to = strtol(e + 1, &e, 10);
            gTestX = x;
            gTestY = y;
            gTestFrom = from;
            gTestTo = to;
        }
        const char* tr = getenv("RE4_TRACE_MOVE");
        gTrace = tr && *tr == '1';
        if ((t && *t) || gTrace) {
            hook();
        }
    }
} gInit;

}  // namespace

// The app (CoreBridge.moveAim, 1.1.5): the left stick while L or R is held with "Move and aim" on, x right and y
// forward (-1 .. 1), when it changes; 0, 0 when it rests or the aim key is let go. Any thread.
extern "C" void re4_core_move_aim(float x, float y)
{
    if (!(x == x) || !(y == y)) {
        return;
    }
    hook();
    __atomic_store_n(&gXBits, bits(x), __ATOMIC_RELAXED);
    __atomic_store_n(&gYBits, bits(y), __ATOMIC_RELAXED);
}

// For the legs (move_aim_legs.cpp), once a frame after his routine: 1 and the move of this frame (x right, y forward,
// the stick's direction; speed in units a frame) when the hook moved him; 0 when not (then *x, *y, *speed untouched)
extern "C" int re4_move_aim_moved(float* x, float* y, float* speed)
{
    if (!gMovedNow) {
        return 0;
    }
    gMovedNow = false;
    *x = gMoveX;
    *y = gMoveY;
    *speed = gMoveSpeed;
    return 1;
}

// 1 while he may move with the aim (the routines of why(): a gun set or firing, the knife held or slashing, no scope)
extern "C" int re4_move_aim_aiming(void)
{
    return why(pPL) == 0 ? 1 : 0;
}
