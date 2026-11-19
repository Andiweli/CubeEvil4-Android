// Ashley catches up (1.1.0, wish of the project owner, 19.11.2026): a double press of the partner command key (the
// game's X, Key 0x200: "Wait" / "Follow me"; the controller's X, the keyboard's Q, the touch controls' X), the second
// press within kDoubleFrames (12 game frames, 0.4 s) of the first, puts Ashley right behind Leon, and she follows him
// (the owner's second wish of the day: a double press instead of the first version's 2 s hold, which stays as a quiet
// extra). A single press keeps its own use without any delay: cPlLeon::checkXbutton toggles wait / follow at the
// press, as before (a second press within its 8 frames it ignores); the double press then makes her follow in any
// case, also when she is already close. She does not pop up: she fades in over kFadeFrames (15 game frames, 0.5 s)
// through cModel::invisible_factor2, the factor the game's own fades use besides invisible_factor (her attached
// objects take it from her), drawn by the game's GX with GLES and Vulkan alike.
//
// Only where it is safe, else nothing happens (one log line says why):
//  - the game runs with the player free: no event or film (STA_EVENT), no sub screen (STA_SUB_SCRN);
//  - the partner is Ashley (pSUB id 3), alive, and under the player's own command: the strict half of the game's
//    SubCharCheckCtrl (F_PL_CTRL, not asleep, not walking to a scripted place, her own wait / follow routines, so
//    not carried, captured, in a hiding place, climbing or in a scene of her own);
//  - Leon stands on a floor (not on a ladder, in a jump or in the water);
//  - the place behind Leon is a floor of the room's collision within 40 cm of his, not steep, not a fall, a cliff
//    or a hiding place, with a clear line from Leon at knee and chest height (as runtime/enemy_random.cpp checks
//    its spots); tried 0.9, 1.3 and 0.6 m behind him, then behind to the left and right; none of them: nothing.
//  - more than 1.5 m from Leon (closer she already is behind him).
// She is put there by the game's own partner command "warp to the player" (SubCharCtrl 4: her routines start
// over, follow), then moved from Leon's place to the one behind him, facing as he does; the call sound plays.
#include "types.h"
#include "global.h"
#include "main.h"
#include "player.h"
#include "em.h"
#include "pl_npc.h"
#include "pl_sub.h"
#include "sce_at.h"
#include "atari.h"
#include "at_sub.h"
#include "snd.h"

#include <math.h>
#include <stdlib.h>

extern GLOBAL_WK* pG;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp

namespace {

constexpr u64 kAshleyKey = 0x200;        // the partner command (cPlLeon::checkXbutton)
constexpr unsigned kHoldFrames = 60;     // the hold: two seconds of the game's 30 frames a second
constexpr unsigned kDoubleFrames = 12;   // a double press: the second within 0.4 s of the first
constexpr unsigned kFadeFrames = 15;     // the fade-in: 0.5 s
constexpr float kFloorStep = 400.0f;     // the floor behind Leon within 40 cm of his
float gNearEnough = 1500.0f;             // closer than 1.5 m: nothing to do (RE4_ASHLEY_CATCHUP_MIN for tests)
constexpr u32 kBadFloor = SAT_ATTR_FALL | SAT_ATTR_CLIFF | SAT_ATTR_FALL_FANCE | SAT_ATTR_JUMPOVER | SAT_ATTR_HIDE |
                          SAT_ATTR_FANCE | SAT_ATTR_UP | SAT_ATTR_DOWN | SAT_ATTR_UP2 | SAT_ATTR_DOWN2;

float absOf(float v)
{
    return v < 0.0f ? -v : v;
}

// The floor under (x, z) near height y: false when none, steep or of a bad kind (runtime/enemy_random.cpp floorKind)
bool floorAt(float x, float y, float z, float* out)
{
    Vec top = {x, y + 800.0f, z}, bottom = {x, y - 1500.0f, z}, hit;
    Vec* n = 0;
    const u32 attr = (u32) SatMgr.hitCheck2(&top, &bottom, &hit, &n, SAT_CK_FLOOR, 0);
    if (!attr || (attr & kBadFloor) || (n && n->y < 0.7f)) {
        return false;
    }
    *out = hit.y;
    return true;
}

bool clearLine(float ax, float ay, float az, float bx, float by, float bz)
{
    Vec a = {ax, ay, az}, b = {bx, by, bz}, hit;
    return SatMgr.hitCheck(&a, &b, &hit, 0, 0, 0) == 0;
}

// Ashley under the player's own command: SubCharCheckCtrl without its F_CALL_ENABLE shortcut
const char* ashleyNotFree(cSubChar* sub)
{
    if (!sub || sub->id != 3) {
        return "no Ashley with Leon";
    }
    if (sub->hp <= 0) {
        return "Ashley is down";
    }
    if (!sub->flg.check(cSubChar::F_PL_CTRL) || sub->flg.check(cSubChar::F_SLEEP) ||
        sub->flg.check(cSubChar::F_MOVE_TO) || sub->r_no_0 != 0) {
        return "Ashley is not under Leon's command (a scene, carried, captured or sent somewhere)";
    }
    switch (sub->r_no_1) {
    case 0: case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9: case 0xA: case 0xB: case 0xC: case 0xD:
    case 0xE: case 0x11: case 0x12: case 0x13:
        return 0;
    default:
        return "Ashley is busy (hiding or in a move of her own)";
    }
}

// A place behind Leon, checked as the head comment says
bool behindLeon(Vec* out, float* dist)
{
    const Vec& p = pPL->pos;
    float leonFloor;
    if (!floorAt(p.x, p.y, p.z, &leonFloor) || absOf(leonFloor - p.y) > 150.0f) {
        return false;
    }
    const float a = pPL->ang.y;
    // behind, then behind to the left and to the right (the angle from straight behind), at the distances
    static const float kTry[][2] = {{0.0f, 900.0f},    {0.0f, 1300.0f},   {0.0f, 600.0f},   {0.6f, 900.0f},
                                    {-0.6f, 900.0f},   {1.1f, 800.0f},    {-1.1f, 800.0f}};
    for (const auto& t : kTry) {
        const float dir = a + 3.1415927f + t[0];
        const float x = p.x + sinf(dir) * t[1], z = p.z + cosf(dir) * t[1];
        float y;
        if (!floorAt(x, p.y, z, &y) || absOf(y - leonFloor) > kFloorStep) {
            continue;
        }
        if (!clearLine(p.x, p.y + 300.0f, p.z, x, y + 300.0f, z) || !clearLine(p.x, p.y + 900.0f, p.z, x, y + 900.0f, z)) {
            continue;
        }
        out->x = x;
        out->y = y;
        out->z = z;
        *dist = t[1];
        return true;
    }
    return false;
}

unsigned gHeld;       // the game frames the key has been held
unsigned gFrames;     // game frames counted by the hook
long gLastPress = -1000;  // the frame of the last single press
cSubChar* gFading;    // Ashley while she fades in
unsigned gFade;       // frames of the fade done
bool gHoldDone;       // this hold already did its work (or was refused)
unsigned gLines;

void line(const char* what)
{
    if (gLines < 40) {
        gLines++;
        re4_log("ashley: hold of the partner key: %s", what);
    }
}

// she follows (the double press: whatever the two presses toggled)
void follow(cSubChar* sub)
{
    if (SubCharGetStatus() & 0x40000000) {  // waiting (cPlLeon::checkXbutton)
        SubCharCtrl(1, 0);
    }
}

void catchUp(bool twice)
{
    if (!pG || !pPL || pG->Rno0 != 3 || StaFlagChk(pG, STA_EVENT) || StaFlagChk(pG, STA_SUB_SCRN)) {
        line("nothing (no free player: an event, a film or a sub screen)");
        return;
    }
    cSubChar* sub = SUB_CHAR();
    if (const char* why = ashleyNotFree(sub)) {
        line(why);
        return;
    }
    const float dx = sub->pos.x - pPL->pos.x, dz = sub->pos.z - pPL->pos.z;
    const float away = sqrtf(dx * dx + dz * dz);
    if (away < gNearEnough && absOf(sub->pos.y - pPL->pos.y) < kFloorStep) {
        if (twice) {
            follow(sub);
        }
        if (gLines < 40) {
            gLines++;
            re4_log("ashley: %s: Ashley is already close (%.1f m)%s", twice ? "double press" : "hold", away / 1000.0f,
                    twice ? ", she follows" : "");
        }
        return;
    }
    Vec spot;
    float dist;
    if (!behindLeon(&spot, &dist)) {
        if (twice) {
            follow(sub);
        }
        line("nothing (no safe floor behind Leon)");
        return;
    }
    SubCharCtrl(4, 0);  // the game's own "warp to the player": her routines start over, she follows
    sub->setPos(&spot);
    Vec ang = sub->ang;
    ang.y = pPL->ang.y;
    sub->setAng(&ang);
    // the fade-in from nothing (the hook raises it a frame at a time)
    sub->invisible_factor2 = 0.0f;
    gFading = sub;
    gFade = 0;
    if (!twice) {
        SndCall(1, 0x37, &pPL->pos, 0, 0, 0);  // the call "follow me" (the double press: the game's own calls)
    }
    if (gLines < 40) {
        gLines++;
        re4_log("ashley: caught up (%s), %.1f m behind Leon (she was %.1f m away), room r%03x, fading in", twice ? "double press" : "hold", dist / 1000.0f,
                away / 1000.0f, (unsigned) pG->room_id);
    }
}

// the fade-in, a step a frame; ended at once when she is gone or in a scene of her own
void fade()
{
    if (!gFading) {
        return;
    }
    if (gFading != SUB_CHAR() || gFading->hp <= 0) {
        gFading = 0;
        return;
    }
    gFade++;
    if (gFade >= kFadeFrames || StaFlagChk(pG, STA_EVENT)) {
        gFading->invisible_factor2 = 1.0f;
        gFading = 0;
        return;
    }
    gFading->invisible_factor2 = (f32) gFade / (f32) kFadeFrames;
}

// once a game frame (runtime/pad.cpp PADRead): game frames, not the clock, so a slow moment or a PC test that
// runs faster than real time counts the same game time (Key: the last frame's)
void hook(unsigned)
{
    gFrames++;
    fade();
    if (Key.trg & kAshleyKey) {
        if ((long) gFrames - gLastPress <= (long) kDoubleFrames) {
            gLastPress = -1000;
            gHoldDone = true;  // not the hold's catch-up as well
            catchUp(true);
        } else {
            gLastPress = (long) gFrames;
        }
    }
    if (!(Key.on & kAshleyKey)) {
        gHeld = 0;
        gHoldDone = false;
        return;
    }
    if (gHoldDone || ++gHeld < kHoldFrames) {
        return;
    }
    gHoldDone = true;
    catchUp(false);
}

struct Install {
    Install()
    {
        // off for a test on the PC that wants the game alone (RE4_ASHLEY_CATCHUP=0)
        const char* v = getenv("RE4_ASHLEY_CATCHUP");
        if (!v || atoi(v) != 0) {
            re4_add_frame_hook(hook);
        }
        // a PC test of the placing itself: the distance in metres below which nothing happens (0: always)
        if (const char* m = getenv("RE4_ASHLEY_CATCHUP_MIN")) {
            gNearEnough = (float) atof(m) * 1000.0f;
        }
    }
} gInstall;

}  // namespace
