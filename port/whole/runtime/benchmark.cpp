// The app's benchmark (wish of the project owner, 05.11.2026; app menu Display -> GRAPHICS -> Benchmark): the
// game's side. The app (core_mode.cpp, the render thread) leads it through the cheat channel
// (re4_core_cheat_action 20..23, so the core in its own process gets it the same way) and measures the
// pictures; here the game goes to the two places and is held still there:
//   20 + k  go to place k (0: r11b, the lake's dock with the torch and the boat; 1: r104, the village's rope
//           bridge with Ganados), the first time after an instant save of the game as it is (in memory only)
//   22      back: the instant save is loaded again (the game as it was, the place and the room's state)
//   23      the state: 16 * place + kState* (below), < 0 a refusal
// While it runs no event starts (DBG_NO_EVENT: jumping from an early point of the story, the places' rooms
// would play their first-visit cutscenes; the room's scripts and enemies stay), and an event that runs anyway is
// skipped with START.
// At a place: every enemy (cEm id 0x10..0x3F) stands still (patch 0208, em.cpp emMove: no routine, no damage;
// drawn as it stands), Leon holds his weapon up (R held, re4_pad_add), and the view goes round him once every
// 20 seconds at 10 m (patch 0208, camera.cpp CameraMove: re4_bench_camera puts the camera's point and target in
// pG->Camera after the game's camera, before the projection, so the view culling follows).
// Only from the story's normal play (re4_instant_game_ready: no event, no menu, a place the instant save takes)
// in the village part (stage 1, disc 1): the places belong to it, the game never goes back there afterwards and
// its rooms do not start right from a later point (r11b crashed on its boat from the castle).
#include "types.h"
#include "global.h"
#include "camera.h"
#include "em.h"
#include "player.h"
#include "sce_at.h"
#include "flag_rsf.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_pad_add(u16 buttons);                      // runtime/pad.cpp
extern "C" int re4_instant_game_ready(void);                   // runtime/instant_game.cpp
extern "C" int re4_instant_game_capture(unsigned char* out);
extern "C" int re4_instant_game_restore(const unsigned char* in);

namespace {

enum {
    kIdle = 0,      // nothing asked
    kGoing = 1,     // the place's room is loading
    kReady = 2,     // at the place: enemies still, Leon aiming, the view going round
    kBack = 3,      // the instant save is loading
    kDone = 4,      // back in the game as it was
    kRefused = -1,  // not now (no free play: an event, a menu, a place the instant save refuses)
    kFailed = -2,   // the instant save failed
    kNotVillage = -4,  // not in the village part (stage 1): the places' rooms are not part of the game after it
};

struct Place {
    u16 room;
    Vec pos;
    f32 angle;
    const char* what;
    int seen;  // the room's flag (RsfSet) of its arrival scene, -1 none: set before the jump, so the room starts
               // as on a later visit (r104 holds the picture for its scene); the instant save puts it back
};
// the project owner's places (05.11.2026): r11b from his R3 series (the dock, the torch, the boat, rain), r104
// the middle of the rope bridge with the Ganados (its trigger area 0x0a; his save starts in the house beside it)
Place kPlaces[2] = {
    {0x11B, {127708.0f, -175.0f, 171017.0f}, 1.50f, "r11b, the lake's dock", -1},
    {0x104, {-3300.0f, -300.0f, 700.0f}, 0.72f, "r104, the village's rope bridge", 1},
};
f32 kRadius = 10000.0f;            // 10 m (RE4_BENCH_RADIUS for checks on the PC)
f32 kEye = 4000.0f;                // the view's height over Leon's feet, looking down on him (RE4_BENCH_EYE)
const f32 kLook = 1100.0f;         // what it looks at: Leon's chest
const unsigned kRound = 600;       // game frames a round (20 s)
const unsigned kSettle = 90;       // frames at the place before it counts as ready

std::atomic<int> gState(kIdle);
std::atomic<int> gPlace(0);
std::atomic<int> gAsk(0);          // 20, 21, 22 from the app; 0 nothing
unsigned char* gSave;              // the instant save of the game as it was
bool gHooked;
unsigned gArrived;                 // frames since the place's room runs
unsigned gTurn;                    // frames since the view started going round
u16 gKeepRoom;                     // where the game was: the way back when the instant save cannot load yet
Vec gKeepPos, gKeepAng;
unsigned gBackTries;               // frames the load was refused
unsigned gBackFrames;              // frames since the load was handed over

bool playing()
{
    return pG && pPL && pG->Rno0 == 3 && !StaFlagChk(pG, STA_EVENT) && !StaFlagChk(pG, STA_SUB_SCRN) &&
           !StaFlagChk(pG, STA_MOVIE_ON);
}

void go(int k)
{
    const Place& p = kPlaces[k];
    if (!gSave) {
        // the village part only: after it the game never goes back there, and its rooms do not start right
        // from a later point (r11b set its boat from an empty list entry and crashed, debug discs from r204)
        if (pG->stage_no != 1) {
            gState = kNotVillage;
            re4_log("benchmark: not in the village part (stage %d)", pG->stage_no);
            return;
        }
        if (!re4_instant_game_ready()) {
            gState = kRefused;
            re4_log("benchmark: not now (the game is not in free play at a place the instant save takes)");
            return;
        }
        gSave = (unsigned char*) malloc(0x12000);
        if (!gSave || re4_instant_game_capture(gSave) != 0) {
            free(gSave);
            gSave = 0;
            gState = kFailed;
            re4_log("benchmark: the game as it is could not be kept (instant save failed)");
            return;
        }
        gKeepRoom = G_ROOM_ID;
        gKeepPos = pPL->pos;
        gKeepAng = pPL->ang;
        re4_log("benchmark: the game kept in r%03x at (%.0f %.0f %.0f)", (unsigned) G_ROOM_ID, pPL->pos.x, pPL->pos.y,
                pPL->pos.z);
    }
    DbgFlagOn(pG, DBG_NO_EVENT);
    if (p.seen >= 0) {
        RsfSet(p.room, p.seen);
    }
    Vec pos = p.pos;
    Vec ang = {0.0f, p.angle, 0.0f};
    SceAtExecRoomJump(p.room, &pos, &ang, 0);
    gPlace = k;
    gArrived = 1000;  // (counts on from here in the room; below it, the START presses of an event)
    gTurn = 0;
    gState = kGoing;
    re4_log("benchmark: to place %d, %s", k + 1, p.what);
}

void back()
{
    if (!gSave) {
        gState = kDone;
        return;
    }
    const int r = re4_instant_game_restore(gSave);
    if (r != 0) {
        // not in free play here (a place reached too early in the story waits for its own scene): first back to
        // the room it was in, then the load
        if (++gBackTries == 90 && pG && pG->Rno0 == 3) {
            Vec pos = gKeepPos, ang = gKeepAng;
            SceAtExecRoomJump(gKeepRoom, &pos, &ang, 0);
            re4_log("benchmark: the load waits (%d): first back to r%03x", r, (unsigned) gKeepRoom);
        } else if (gBackTries % 300 == 0) {
            re4_log("benchmark: the game as it was could not be loaded yet (%d)", r);
        }
        if (gBackTries > 900) {
            gBackTries = 0;  // (and again: the room jump once more)
        }
        return;
    }
    gBackTries = 0;
    free(gSave);
    gSave = 0;
    DbgFlagOff(pG, DBG_NO_EVENT);
    gBackFrames = 0;
    gState = kBack;
    re4_log("benchmark: back to the game as it was");
}

void hook(unsigned)
{
    const int ask = gAsk.exchange(0);
    if (ask == 20 || ask == 21) {
        if (gState != kGoing && gState != kBack) {
            go(ask - 20);
        }
    } else if (ask == 22) {
        gState = kBack;  // the views and the enemies free at once; the load follows
        back();
    } else if (gState == kBack && gSave) {
        back();  // the load was refused in its frame: again
    }
    switch (gState.load()) {
    case kGoing:
        gArrived++;
        if (pG && pG->Rno0 == 3 && StaFlagChk(pG, STA_EVENT) && (gArrived % 30) == 0) {
            re4_pad_add(0x1000);  // START: an event that runs anyway is skipped
        }
        if (pG && pPL && (gArrived % 300) == 0) {
            re4_log("benchmark: waiting in r%03x: step %d, event %d, sub screen %d, movie %d, screen stop %d, player "
                    "routine %d", (unsigned) G_ROOM_ID, pG->Rno0, StaFlagChk(pG, STA_EVENT) ? 1 : 0,
                    StaFlagChk(pG, STA_SUB_SCRN) ? 1 : 0, StaFlagChk(pG, STA_MOVIE_ON) ? 1 : 0,
                    SysFlagChk(pG, SYS_SCREEN_STOP) ? 1 : 0, pPL->r_no_0);
        }
        if (playing() && G_ROOM_ID == kPlaces[gPlace].room) {
            if (gArrived >= kSettle + 1000) {
                gState = kReady;
                re4_log("benchmark: at place %d (%.0f %.0f %.0f), the view goes round", gPlace.load() + 1, pPL->pos.x,
                        pPL->pos.y, pPL->pos.z);
            }
        }
        break;
    case kReady:
        if (playing()) {
            re4_pad_add(0x0020);  // R: the weapon up
            gTurn++;
        }
        break;
    case kBack:
        if (!gSave && ++gBackFrames > 60 && playing() && G_ROOM_ID == gKeepRoom) {
            gState = kDone;
            re4_log("benchmark: back in r%03x", (unsigned) G_ROOM_ID);
        }
        break;
    }
}

}  // namespace

extern "C" {

// The app's command (core_api.cpp re4_core_cheat_action 20..23), any thread.
int re4_bench_command(int what)
{
    if (!gHooked) {
        gHooked = true;
        if (const char* r = getenv("RE4_BENCH_RADIUS")) kRadius = (f32) atof(r);
        if (const char* e = getenv("RE4_BENCH_EYE")) kEye = (f32) atof(e);
        if (const char* q = getenv("RE4_BENCH_PLACE2")) {  // "x,y,z,angle" (checks on the PC)
            sscanf(q, "%f,%f,%f,%f", &kPlaces[1].pos.x, &kPlaces[1].pos.y, &kPlaces[1].pos.z, &kPlaces[1].angle);
        }
        re4_add_frame_hook(hook);
    }
    if (what == 23) {
        const int s = gState.load();
        return s < 0 ? s : 16 * gPlace.load() + s;
    }
    if (what == 20 || what == 21) {
        gState = kIdle;
    }
    gAsk = what;
    return 3;  // queued
}

// Patch 0208 (em.cpp emMove): the enemies stand still at a place of the benchmark.
int re4_bench_freeze(void)
{
    return gState.load() == kReady || gState.load() == kGoing;
}

// Patch 0208 (camera.cpp CameraMove): at a place the view goes round Leon.
void re4_bench_camera(CAMERA* cam)
{
    if (gState.load() != kReady || !pPL) {
        return;
    }
    const f32 a = 6.2831853f * (f32) (gTurn % kRound) / (f32) kRound + kPlaces[gPlace].angle + 3.1415927f;
    cam->param.Campos.x = pPL->pos.x + kRadius * sinf(a);
    cam->param.Campos.y = pPL->pos.y + kEye;
    cam->param.Campos.z = pPL->pos.z + kRadius * cosf(a);
    cam->param.Target.x = pPL->pos.x;
    cam->param.Target.y = pPL->pos.y + kLook;
    cam->param.Target.z = pPL->pos.z;
    cam->param.Roll = 0.0f;
}

}  // extern "C"
