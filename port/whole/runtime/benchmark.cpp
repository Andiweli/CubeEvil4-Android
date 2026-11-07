// The app's benchmark (wish of the project owner, 05.11.2026; app menu Display -> GRAPHICS -> Benchmark): the
// game's side. The app (core_mode.cpp, the render thread) leads it through the cheat channel
// (re4_core_cheat_action 20..23, so the core in its own process gets it the same way) and measures the
// pictures; here the game goes to the place and is held still there:
//   20      go to the place: the lake's dock in r11b with the torch and the boat, in rain
//   22      back to where the game was
//   23      the state: 16 * place (always 0) + kState* (below), < 0 a refusal
// The place is the project owner's instant save at the dock (06.11.2026: "the place of my save state";
// runtime/instant_store.cpp re4_bench_place_payload, built into the core): it loads as the in-game menu loads an
// instant save, from the title's main menu or from free play in any chapter. The player's own instant slot, the
// memory card and the save count are not touched. Before it the game as it is goes into memory (an instant save
// of the moment, from the game) or the title is noted; back, that save loads again or a soft reset brings the title.
// While it runs no event starts (DBG_NO_EVENT; outside the save image, so it stays over the loads), and an event
// that runs anyway is skipped with START.
// At the place: every enemy (cEm id 0x10..0x3F) stands still (patch 0208, em.cpp emMove: no routine, no damage;
// drawn as it stands), Leon just stands (the app sends no pad), and the view goes round him once every 20 seconds
// at 5 m (patch 0208, camera.cpp CameraMove: re4_bench_camera puts the camera's point and target in pG->Camera
// after the game's camera, before the projection, and makes its orientation anew: pG->Camera.mat and Look, from
// which View's frustum (scenery) and the draw lists (AddOt*: figures, effects) cull. Without it they kept the game
// camera's orientation behind Leon: on the far half of the round Leon, the boat, the torch's flame and the land
// behind the dock were left out, device test of the project owner 06.11.2026).
#include "types.h"
#include "global.h"
#include "camera.h"
#include "em.h"
#include "player.h"

#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_pad_add(u16 buttons);                      // runtime/pad.cpp
extern "C" int re4_instant_game_ready(void);                   // runtime/instant_game.cpp
extern "C" int re4_instant_game_at_title(void);
extern "C" int re4_instant_title_step(void);
extern "C" int re4_instant_game_capture(unsigned char* out);
extern "C" int re4_instant_game_restore(const unsigned char* in);
extern "C" int re4_bench_place_payload(unsigned char* out);   // runtime/instant_store.cpp

namespace {

enum {
    kIdle = 0,      // nothing asked
    kGoing = 1,     // the place is loading
    kReady = 2,     // at the place: enemies still, the view going round
    kBack = 3,      // the way back (the game's save loading, or the soft reset to the title)
    kDone = 4,      // back where the game was
    kRefused = -1,  // not now (no free play and not the title's main menu: an event, a menu, a boss fight ...)
    kFailed = -2,   // the game as it is could not be kept, or the place could not be read
    kNoPlace = -4,  // the place's save does not fit these discs (its room data has another size)
};

const u16 kRoom = 0x11B;           // the place's room
f32 kRadius = 5000.0f;             // 5 m (the project owner, 06.11.2026: half of the first 10 m; RE4_BENCH_RADIUS on the PC)
f32 kEye = 2000.0f;                // the view's height over Leon's feet, looking down on him (RE4_BENCH_EYE)
const f32 kLook = 1100.0f;         // what it looks at: Leon's chest
const unsigned kRound = 600;       // game frames a round (20 s)
const unsigned kSettle = 90;       // free frames at the place before it counts as ready
const unsigned kBufferBytes = 0x12000;

std::atomic<int> gState(kIdle);
std::atomic<int> gAsk(0);          // 20, 22 from the app; 0 nothing
unsigned char* gSave;              // the game as it was (an instant save in memory); 0: from the title
unsigned char* gPlace;             // the place's save, host order
bool gFromTitle;
unsigned gLogoWait;                // frames the start waited for the title's menu (0: not waiting)
bool gResetAsked;                  // the soft reset to the title was asked for
bool gHooked;
unsigned gArrived;                 // frames since the place's load was handed over
unsigned gFree;                    // free frames in a row at the place
unsigned gTurn;                    // frames since the view started going round
u16 gKeepRoom;                     // the room the game was in
unsigned gBackFrames;              // frames on the way back
unsigned gBackTries;               // frames the load of the game as it was was refused

bool playing()
{
    return pG && pPL && pG->Rno0 == 3 && !StaFlagChk(pG, STA_TITLE) && !StaFlagChk(pG, STA_EVENT) &&
           !StaFlagChk(pG, STA_SUB_SCRN) && !StaFlagChk(pG, STA_MOVIE_ON);
}

void release()
{
    free(gSave);
    gSave = 0;
    free(gPlace);
    gPlace = 0;
}

void go()
{
    if (!pG) {
        gState = kRefused;
        return;
    }
    // the title's logos (Capcom, CRI, Dolby; the project owner was refused there, 06.11.2026): START until the main
    // menu waits, then the start from the title's menu as always (hook)
    const int step = re4_instant_title_step();
    if (!re4_instant_game_at_title() && step >= 1 && step <= 4) {
        gLogoWait = 1;
        gState = kGoing;
        re4_log("benchmark: the title's logos (step %d): START until its menu waits", step);
        return;
    }
    gFromTitle = re4_instant_game_at_title() != 0;
    if (!gFromTitle) {
        if (!re4_instant_game_ready()) {
            gState = kRefused;
            re4_log("benchmark: not now (neither free play at a place the instant save takes nor the title's menu)");
            return;
        }
        gSave = (unsigned char*) malloc(kBufferBytes);
        if (!gSave || re4_instant_game_capture(gSave) != 0) {
            release();
            gState = kFailed;
            re4_log("benchmark: the game as it is could not be kept (instant save failed)");
            return;
        }
        gKeepRoom = G_ROOM_ID;
        re4_log("benchmark: the game kept in r%03x at (%.0f %.0f %.0f)", (unsigned) G_ROOM_ID, pPL->pos.x, pPL->pos.y,
                pPL->pos.z);
    } else {
        re4_log("benchmark: from the title's menu");
    }
    gPlace = (unsigned char*) malloc(kBufferBytes);
    if (!gPlace || re4_bench_place_payload(gPlace) != 0) {
        release();
        gState = kFailed;
        re4_log("benchmark: the place's save could not be read");
        return;
    }
    const int r = re4_instant_game_restore(gPlace);
    if (r != 0) {
        release();
        gState = r == -3 ? kNoPlace : kRefused;  // (RE4_INSTANT_BAD: another room data size, another release)
        re4_log("benchmark: the place's save %s (%d)", r == -3 ? "does not fit these discs" : "cannot load now", r);
        return;
    }
    DbgFlagOn(pG, DBG_NO_EVENT);
    gArrived = 0;
    gFree = 0;
    gTurn = 0;
    gState = kGoing;
    re4_log("benchmark: to the place, r%03x, the lake's dock", (unsigned) kRoom);
}

void back()
{
    gBackFrames = 0;
    gBackTries = 0;
    gResetAsked = false;
    gState = kBack;
}

// The way back from the game: its save loads as soon as the game takes a load (free play).
void loadBack()
{
    if (!gSave) {
        return;
    }
    const int r = re4_instant_game_restore(gSave);
    if (r != 0) {
        if (pG && pG->Rno0 == 3 && StaFlagChk(pG, STA_EVENT) && (++gBackTries % 30) == 0) {
            re4_pad_add(0x1000);  // START: an event in the way is skipped
        }
        if (gBackTries && gBackTries % 300 == 0) {
            re4_log("benchmark: the game as it was could not be loaded yet (%d)", r);
        }
        return;
    }
    release();
    DbgFlagOff(pG, DBG_NO_EVENT);
    re4_log("benchmark: back to the game as it was");
}

void hook(unsigned)
{
    const int ask = gAsk.exchange(0);
    if (ask == 20) {
        if (gState != kGoing && gState != kReady && gState != kBack) {
            go();
        }
    } else if (ask == 22) {
        if (gLogoWait) {
            gLogoWait = 0;  // cancelled while the logos ran: nothing loaded yet
            gState = kDone;
        } else if (gState == kGoing || gState == kReady) {
            back();  // the view and the enemies free at once; the way back follows
        } else if (gState != kBack) {
            gState = kDone;
        }
    }
    if (gLogoWait) {
        // waiting for the title's menu: START during the logos (not during the card check, step 1), at most 20 s
        const int step = re4_instant_title_step();
        if (re4_instant_game_at_title()) {
            gLogoWait = 0;
            go();
        } else if (++gLogoWait > 600 || step < 1 || step > 5) {
            gLogoWait = 0;
            gState = kRefused;
            re4_log("benchmark: the title's menu did not come (step %d)", step);
        } else if (step >= 2 && step <= 4 && (gLogoWait % 20) == 0) {
            re4_pad_add(0x1000);
        }
        return;
    }
    switch (gState.load()) {
    case kGoing:
        gArrived++;
        if (pG && pG->Rno0 == 3 && StaFlagChk(pG, STA_EVENT) && (gArrived % 30) == 0) {
            re4_pad_add(0x1000);  // START: an event that runs anyway is skipped
        }
        if (pG && pPL && (gArrived % 300) == 0) {
            re4_log("benchmark: waiting in r%03x: step %d, title %d, event %d, sub screen %d, movie %d, screen stop %d",
                    (unsigned) G_ROOM_ID, pG->Rno0, StaFlagChk(pG, STA_TITLE) ? 1 : 0, StaFlagChk(pG, STA_EVENT) ? 1 : 0,
                    StaFlagChk(pG, STA_SUB_SCRN) ? 1 : 0, StaFlagChk(pG, STA_MOVIE_ON) ? 1 : 0,
                    SysFlagChk(pG, SYS_SCREEN_STOP) ? 1 : 0);
        }
        gFree = playing() && G_ROOM_ID == kRoom ? gFree + 1 : 0;
        if (gFree >= kSettle) {
            gState = kReady;
            re4_log("benchmark: at the place (%.0f %.0f %.0f), the view goes round", pPL->pos.x, pPL->pos.y, pPL->pos.z);
        }
        break;
    case kReady:
        if (playing()) {
            gTurn++;
        }
        break;
    case kBack:
        gBackFrames++;
        if (gFromTitle && !gResetAsked) {
            // the title again as the game makes it after "game over" -> "no", once the place's load is through
            // (a load still waiting would otherwise run at the title)
            if (pG && pG->Rno0 == 3 && !StaFlagChk(pG, STA_TITLE)) {
                DbgFlagOff(pG, DBG_NO_EVENT);
                SysFlagOn(pG, SYS_SOFT_RESET);
                gResetAsked = true;
                re4_log("benchmark: back to the title (soft reset)");
            }
        } else if (gFromTitle) {
            // the soft reset goes through the logos: done once the title's main menu waits
            if (re4_instant_game_at_title()) {
                release();
                gState = kDone;
                re4_log("benchmark: back at the title after %u frames", gBackFrames);
            } else if (gBackFrames % 300 == 0) {
                re4_log("benchmark: on the way to the title (step %d)", pG ? pG->Rno0 : -1);
            }
        } else if (gSave) {
            loadBack();
        } else if (gBackFrames > 60 && playing() && G_ROOM_ID == gKeepRoom) {
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
        re4_add_frame_hook(hook);
    }
    if (what == 23) {
        return gState.load();
    }
    if (what == 20) {
        gState = kIdle;
        gLogoWait = 0;
    }
    gAsk = what;
    return 3;  // queued
}

// Patch 0208 (em.cpp emMove): the enemies stand still at the place of the benchmark.
int re4_bench_freeze(void)
{
    return gState.load() == kReady || gState.load() == kGoing;
}

// Patch 0208 (camera.cpp CameraMove): at the place the view goes round Leon.
void re4_bench_camera(CAMERA* cam)
{
    if (gState.load() != kReady || !pPL) {
        return;
    }
    const f32 a = 6.2831853f * (f32) (gTurn % kRound) / (f32) kRound + pPL->ang.y + 3.1415927f;
    cam->param.Campos.x = pPL->pos.x + kRadius * sinf(a);
    cam->param.Campos.y = pPL->pos.y + kEye;
    cam->param.Campos.z = pPL->pos.z + kRadius * cosf(a);
    cam->param.Target.x = pPL->pos.x;
    cam->param.Target.y = pPL->pos.y + kLook;
    cam->param.Target.z = pPL->pos.z;
    cam->param.Roll = 0.0f;
    CameraSetOrientationRoll(cam);  // the camera's matrix and look vector: the culling follows the view shown
}

}  // extern "C"
