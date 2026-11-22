// Photo mode (1.1.0, wish of the project owner, 19.11.2026): the app menu's Display, Optional features, "Photo mode"
// (off by default). With it on, a double press of SELECT (the touch controls' MENU, the keyboard's F1) asks for it; the
// app sends re4_core_photo_mode(1, mask) and, while it is on, the camera input (re4_core_photo_input). Patch 0243 asks
// re4_photo_frame at the top of gameMainLoop (game.cpp).
//  - The game stands as with its own option screen (gameOption, START): gameMainLoop is left out as a whole (the
//    player, the partner, the enemies, objects, effects, lights, the scenario, the camera's control, the cockpit),
//    Stop_flg is all ones as there, the play time stops (SetGameTime, InitGameTime afterwards), the sound effects
//    pause (SndSePauseAll) and the rest (the music) is silent (runtime/sound_volume.cpp). What the main loop does after the game task (Trans: the models, effects and shadows as
//    they stand; the HUD's IdSys, the messages) goes on, so the picture is drawn every frame from the free camera,
//    with the game's own culling.
//  - The free camera (pG->Camera, its whole CAMERA saved and put back): it starts where the game's camera is, the
//    app's input moves it (controller: left stick forward, back, sideways, right stick turns and tilts, the D-pad up
//    and down since the owner's device test of 19.11.2026; the mouse turns and tilts); slow
//    and fast; it stays inside the world (since the owner's second device test of 19.11.2026 anywhere in the room, no
//    leash to Leon): the game's camera and sight collision stop it at walls, floors and ceilings (it slides along
//    them), and it never goes where no floor is below. Camera effects (the app's L and R): normal, wide angle,
//    telephoto, dolly zoom (the field of view follows the distance to Leon, so he keeps his size), Dutch angle (a
//    tilted horizon), orbit (it circles Leon slowly, looking at him).
//  - Hidden on request (mask): 1 the HUD (Disp_flg: cockpit, the IdSys units, messages, the action prompts), 2 Leon
//    (and his weapons), 4 the partner, 8 the enemies (the ids the health bars list, runtime/health_bars.cpp: no
//    merchant, animals, traps, vehicles). A figure is hidden by its be_flag bit 1 (setTrans(0)): the game then neither
//    draws it nor its shadow; the objects that hang on it (a weapon, a torch in the hand: their first part's or their
//    own parent is the figure or one of its parts) with it. Everything is put back as it was: nothing moved in between.
//  - Not allowed (the request is refused, the app says so): events and films, a sub screen or the item pick-up,
//    the death scene, the memory card, a door scene, the scope or the binoculars, Leon caught by an enemy, the game's
//    pictures standing, the player or the keys stopped by the game (the merchant, the typewriter). Only in play
//    (Rno0 3: not in the title, the option screen or a room's loading).
//  - Leaving (re4_core_photo_mode(0)): the camera, the hidden figures, Disp_flg and Stop_flg as they were, the play
//    time and the sound effects go on; the same frame then runs the game as usual. AI, timers and prompts never saw
//    a frame of it.
// The chunks carry RE4_GX_CHUNK_PHOTO while it is on and RE4_GX_CHUNK_PHOTO_REFUSED once after a refusal
// (runtime/gx_decode.cpp), so the app knows without asking.
#include "types.h"
#include "global.h"
#include "main.h"
#include "main_sub.h"
#include "player.h"
#include "pl_wep.h"
#include "em.h"
#include "obj.h"
#include "emwep.h"
#include "emshield.h"
#include "emtorch.h"
#include "camera.h"
#include "atari.h"
#include "view.h"
#include "snd.h"
#include "vec.h"
#include "math_sub_decl.h"

#include <atomic>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern GLOBAL_WK* pG;

extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_movie_playing(void);  // runtime/movie_status.cpp
extern "C" void re4_gx_photo(int on, int refused);  // runtime/gx_decode.cpp
extern "C" void re4_sound_volume_mute(int on);  // runtime/sound_volume.cpp
bool re4_hb_enemy(const cEm* e);  // runtime/health_bars.cpp
void re4_health_bars_call();   // runtime/health_bars.cpp (drawn with the HUD)

namespace {

enum { kHud = 1, kLeon = 2, kPartner = 4, kEnemies = 8 };

constexpr f32 kDt = 1.0f / 30.0f;      // a game frame
constexpr f32 kMove = 2200.0f;         // units (mm) a second at full stick
constexpr f32 kTurn = 1.75f;           // radians a second
constexpr f32 kTilt = 1.2f;
enum { kFxNormal, kFxWide, kFxTele, kFxDolly, kFxDutch, kFxOrbit, kFxCount };
constexpr f32 kPitchMax = 1.45f;       // about 83 degrees
constexpr f32 kDeltaUnit = 1.0f / 8192.0f;  // the app's turn deltas
constexpr f32 kPad = 300.0f;           // the camera keeps this far from walls, floors and ceilings
constexpr f32 kFloorSearch = 60000.0f; // no floor within 60 m below: outside the world

// the app's side (any thread)
std::atomic<int> gWant(0);         // 1 enter asked, 2 leave asked, 0 nothing
std::atomic<unsigned> gWantAt(0);  // pG->Frame_cnt then (an old request is dropped)
std::atomic<int> gMask(0);
std::atomic<int> gStickA(0), gStickB(0);
std::atomic<int> gYawDelta(0), gPitchDelta(0);
std::atomic<int> gReset(0);

// the game thread's
bool gActive;
CAMERA gCamBak;
u32 gStopBak, gDispBak;
int gMaskNow = -1;
Vec gPos, gStartPos, gAnchor;
f32 gYaw, gPitch, gStartYaw, gStartPitch, gFovy;
int gFx, gFxNow = -1;   // the camera effect asked for / in use
f32 gDollyD0;           // dolly zoom: the distance to Leon when it began
const char* const kFxName[kFxCount] = {"normal", "wide angle", "telephoto", "dolly zoom", "Dutch angle", "orbit"};
unsigned gFrames, gLines;

constexpr int kMaxHidden = 160;
cModel* gHidden[kMaxHidden];
int gHiddenN;

const char* refusal()
{
    if (!pG || !pPL || pG->Rno0 != 3) return "not in play";
    if (StaFlagChk(pG, STA_EVENT)) return "an event";
    if (StaFlagChk(pG, STA_MOVIE_ON) || StaFlagChk(pG, STA_MOVIE2_ON) || re4_movie_playing()) return "a film";
    if (StaFlagChk(pG, STA_SUB_SCRN) || StaFlagChk(pG, STA_ITEM_GET)) return "a sub screen";
    if (StaFlagChk(pG, STA_DIEDEMO)) return "the death scene";
    if (StaFlagChk(pG, STA_CARD_ACCESS)) return "the memory card";
    if (SysFlagChk(pG, SYS_DOORDEMO)) return "a door scene";
    if (SysFlagChk(pG, SYS_TRANS_STOP)) return "the game's pictures stand";
    if (StaFlagChk(pG, STA_SCOPE_CAMERA) || StaFlagChk(pG, STA_BINOCULAR)) return "the scope or the binoculars";
    if (StaFlagChk(pG, STA_PL_CATCHED) || StaFlagChk(pG, STA_PL_CATCHHOLD)) return "Leon is caught";
    if (SpfFlagChk(pG, SPF_PL) || SpfFlagChk(pG, SPF_KEY)) return "the game stopped the player or the keys";
    if (!(pPL->be_flag & 0x20)) return "no player";
    return 0;
}

bool isCoordOf(const cCoord* c, cModel* m)
{
    if (c == (const cCoord*) m) return true;
    int n = 0;
    for (cParts* p = m->pList; p && n < 256; p = p->pList, n++) {
        if (c == (const cCoord*) p) return true;
    }
    return false;
}

bool hidden(const cModel* m)
{
    for (int i = 0; i < gHiddenN; i++) {
        if (gHidden[i] == m) return true;
    }
    return false;
}

void hide(cModel* m)
{
    if (!m || gHiddenN >= kMaxHidden || hidden(m) || !(m->be_flag & 2)) return;
    gHidden[gHiddenN++] = m;
    m->setTrans(0);
}

// an object or unit hanging on a hidden figure (or on one of its parts): its own or its first part's parent, and for
// the enemies' weapons (0x42), torches (0x47) and shields (0x50) the model their work says they follow
bool hangsOnHidden(cModel* m, int emId)
{
    const cCoord* parents[3] = {m->pParent, m->pList ? m->pList->pParent : 0, 0};
    if (emId == 0x42) {
        parents[2] = (const cCoord*) (cModel*) EMWEP_WK(m)->pEm_oya;
    } else if (emId == 0x47) {
        parents[2] = (const cCoord*) EMTORCH_WK(m)->pParent;
    } else if (emId == 0x50) {
        parents[2] = (const cCoord*) (cModel*) EMSHIELD_WK(m)->pEm_oya;
    }
    for (int k = 0; k < 3; k++) {
        if (!parents[k]) continue;
        for (int i = 0; i < gHiddenN; i++) {
            if (isCoordOf(parents[k], gHidden[i])) return true;
        }
    }
    return false;
}

void restoreHidden()
{
    for (int i = 0; i < gHiddenN; i++) {
        gHidden[i]->setTrans(1);
    }
    gHiddenN = 0;
}

void applyMask(int mask)
{
    restoreHidden();
    pG->Disp_flg = gDispBak;
    if (mask & kHud) {
        DpfFlagOn(pG, DPF_COCKPIT);
        DpfFlagOn(pG, DPF_ID_SYSTEM);
        DpfFlagOn(pG, DPF_MESSAGE);
        DpfFlagOn(pG, DPF_ACTBTN);
    }
    if (mask & kLeon) {
        hide(pPL);
        if (pPL->Wep) {
            hide(pPL->Wep->m_pWep);
            hide(pPL->Wep->m_pWepHand);
        }
    }
    if ((mask & kPartner) && pSUB && (pSUB->be_flag & 0x20)) {
        hide(pSUB);
    }
    if (mask & kEnemies) {
        for (cEm* e = EmMgr.getActiveWork(); e; e = (cEm*) e->getNext()) {
            if ((cModel*) e != (cModel*) pPL && e != pSUB && re4_hb_enemy(e)) {
                hide(e);
            }
        }
    }
    // what hangs on them (their weapons, a torch), also on what hangs on them
    for (int round = 0; round < 3 && gHiddenN; round++) {
        const int before = gHiddenN;
        for (cObj* o = ObjMgr.getActiveWork(); o; o = (cObj*) o->getNext()) {
            if (!hidden(o) && (o->be_flag & 2) && hangsOnHidden(o, -1)) hide(o);
        }
        for (cEm* e = EmMgr.getActiveWork(); e; e = (cEm*) e->getNext()) {
            if (!hidden(e) && (e->be_flag & 2) && e->id >= 0x40 && hangsOnHidden(e, e->id)) hide(e);
        }
        if (gHiddenN == before) break;
    }
    if (gLines < 60) {
        gLines++;
        re4_log("photo mode: shown %s%s%s%s; %d models hidden", (mask & kHud) ? "" : "HUD ", (mask & kLeon) ? "" : "Leon ",
                (mask & kPartner) ? "" : "partner ", (mask & kEnemies) ? "" : "enemies", gHiddenN);
    }
    gMaskNow = mask;
}

// The game's own collision between two points: the camera's (SatMgr flag 0x8000 with the mask of the game's camera,
// cam_ctrl.cpp: the walls and the invisible bounds its own camera stops at) and the sight and shot collision
// (EatMgr 0x800: what can be seen, floors and ceilings too)
const char* gWhy = "";  // what stopped the last move (the log)
bool blocked(const Vec& a, const Vec& b)
{
    Vec p = a, q = b, hit;
    if (SatMgr.hitCheck(&p, &q, &hit, 0, 0x8000, 0x1C2810)) {
        gWhy = "the camera's collision";
        return true;
    }
    p = a;
    q = b;
    if (EatMgr.hitCheck(&p, &q, 0, 0, 0x800, 0) != 0) {
        gWhy = "a wall, floor or object";
        return true;
    }
    return false;
}

// Above a floor within kFloorSearch (the void outside a map has none), at least half of kPad over it unless it rises
bool overFloor(const Vec& at, bool rising)
{
    Vec p = at;
    const f32 y = EatMgr.getFloor(&p, 0, 10.0f, kFloorSearch, 0);
    return y > -99999.0f && (rising || at.y - y >= kPad * 0.5f);
}

// The camera from a to b: nothing in the way (the way lengthened by kPad, so it never touches a surface) and a floor
// below b
bool canMove(const Vec& a, const Vec& b)
{
    const f32 dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    const f32 len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 0.01f) return true;
    const f32 k = (len + kPad) / len;
    const Vec far = {a.x + dx * k, a.y + dy * k, a.z + dz * k};
    if (blocked(a, far)) return false;
    if (!overFloor(b, dy > 0.0f)) {
        gWhy = "no floor below (the edge of the world) or the floor too near";
        return false;
    }
    return true;
}

unsigned gStops;  // moves the collision stopped (logged now and then)

// to `want` as far as the collision lets it: all at once, else along each axis alone (it slides along a wall)
void moveTo(const Vec& want)
{
    if (canMove(gPos, want)) {
        gPos = want;
        return;
    }
    Vec t = gPos, c = t;
    c.x = want.x;
    if (canMove(t, c)) t = c;
    c = t;
    c.z = want.z;
    if (canMove(t, c)) t = c;
    c = t;
    c.y = want.y;
    if (canMove(t, c)) t = c;
    gPos = t;
    if (gStops++ % 90 == 0 && gLines < 60) {
        gLines++;
        re4_log("photo mode: the camera stops at %.0f %.0f %.0f (%s), %.1f m from Leon", (double) gPos.x, (double) gPos.y,
                (double) gPos.z, gWhy,
                (double) (sqrtf((gPos.x - pPL->pos.x) * (gPos.x - pPL->pos.x) + (gPos.z - pPL->pos.z) * (gPos.z - pPL->pos.z)) /
                          1000.0f));
    }
}

void lookFrom(CAMERA* cam)
{
    const Vec d = {cam->param.Target.x - cam->param.Campos.x, cam->param.Target.y - cam->param.Campos.y,
                   cam->param.Target.z - cam->param.Campos.z};
    const f32 h = sqrtf(d.x * d.x + d.z * d.z);
    gYaw = atan2f(d.x, d.z);
    gPitch = atan2f(d.y, h > 0.001f ? h : 0.001f);
    if (gPitch > kPitchMax) gPitch = kPitchMax;
    if (gPitch < -kPitchMax) gPitch = -kPitchMax;
}

f32 distToLeon()
{
    const f32 dx = gPos.x - gAnchor.x, dy = gPos.y - gAnchor.y, dz = gPos.z - gAnchor.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

void setCamera()
{
    CAMERA* cam = &pG->Camera;
    const f32 cp = cosf(gPitch);
    Vec target = {gPos.x + 1000.0f * cp * sinf(gYaw), gPos.y + 1000.0f * sinf(gPitch), gPos.z + 1000.0f * cp * cosf(gYaw)};
    f32 fovy = gFovy, roll = 0.0f;
    switch (gFx) {
    case kFxWide:
        fovy = gFovy * 1.6f;
        break;
    case kFxTele:
        fovy = gFovy * 0.45f;
        break;
    case kFxDolly: {
        // Leon keeps his size: tan(fov/2) * distance stays as it was when the effect began
        const f32 d = distToLeon();
        const f32 t = tanf(gFovy * 0.5f * 0.017453293f) * gDollyD0 / (d > 300.0f ? d : 300.0f);
        fovy = 2.0f * atanf(t) / 0.017453293f;
        break;
    }
    case kFxDutch:
        roll = 0.30f;  // about 17 degrees
        break;
    case kFxOrbit:
        target = gAnchor;
        break;
    default:
        break;
    }
    fovy = fovy < 8.0f ? 8.0f : fovy > 110.0f ? 110.0f : fovy;
    CameraSetWithRoll(cam, &gPos, &target, roll, fovy);
    cam->Distance = PSVECDistance(&cam->param.Campos, &cam->param.Target);
    C_MTXLookAt(cam->v_mat, &cam->param.Campos, &cam->Up, &cam->param.Target);
    // the field of view of the world only: the view matrix's x and y scaled by tan(old / 2) / tan(new / 2). The game
    // draws its HUD and the letterbox bars with the same projection (a wider projection shrank the HUD and brought the
    // bars, normally outside the picture, into it); the culling follows param.Fovy (VIEW::move)
    if (fovy != gFovy) {
        const f32 k = tanf(gFovy * 0.5f * 0.017453293f) / tanf(fovy * 0.5f * 0.017453293f);
        for (int c = 0; c < 4; c++) {
            cam->v_mat[0][c] *= k;
            cam->v_mat[1][c] *= k;
        }
    }
    View.move();
}

void enter()
{
    CAMERA* cam = &pG->Camera;
    gCamBak = *cam;
    gStopBak = pG->Stop_flg;
    gDispBak = pG->Disp_flg;
    SetGameTime();
    pG->Stop_flg = 0xFFFFFFFF;
    SndSePauseAll(1);
    re4_sound_volume_mute(1);  // the music and what else plays on: silent
    gPos = gStartPos = cam->param.Campos;
    lookFrom(cam);
    gStartYaw = gYaw;
    gStartPitch = gPitch;
    gFovy = cam->param.Fovy > 1.0f ? cam->param.Fovy : 50.0f;
    gAnchor = pPL->pos;
    gAnchor.y += 1000.0f;
    gStickA.store(0);
    gStickB.store(0);
    gYawDelta.store(0);
    gPitchDelta.store(0);
    gReset.store(0);
    gHiddenN = 0;
    gMaskNow = -1;
    gFx = kFxNormal;
    gFxNow = -1;
    gActive = true;
    gFrames = 0;
    re4_gx_photo(1, 0);
    re4_log("photo mode: on in r%03x at frame %u (camera at %.0f %.0f %.0f, Leon at %.0f %.0f %.0f)",
            (unsigned) pG->room_id, (unsigned) pG->Frame_cnt, (double) gPos.x, (double) gPos.y, (double) gPos.z,
            (double) pPL->pos.x, (double) pPL->pos.y, (double) pPL->pos.z);
}

void leave()
{
    restoreHidden();
    pG->Disp_flg = gDispBak;
    pG->Stop_flg = gStopBak;
    pG->Camera = gCamBak;
    View.move();
    InitGameTime();
    SndSePauseAll(0);
    re4_sound_volume_mute(0);
    gActive = false;
    gMaskNow = -1;
    re4_gx_photo(0, 0);
    re4_log("photo mode: off after %u frames, the game goes on as it stood", gFrames);
}

s8 byteAt(int v, int shift)
{
    return (s8) ((v >> shift) & 0xFF);
}

void moveCamera()
{
    const int a = gStickA.load(), b = gStickB.load();
    const f32 mx = byteAt(a, 0) / 127.0f, mz = byteAt(a, 8) / 127.0f, rise = byteAt(a, 16) / 127.0f;
    const f32 yawRate = byteAt(a, 24) / 127.0f, pitchRate = byteAt(b, 0) / 127.0f;
    const int speed = (b >> 8) & 0xFF;
    const int fx = (b >> 17) & 0xF;
    gFx = fx < kFxCount ? fx : kFxNormal;
    if (gFx != gFxNow) {
        if (gFx == kFxDolly) {
            gDollyD0 = distToLeon();
        }
        if (gFx == kFxOrbit) {
            // facing Leon from where it is (the orbit looks at him)
            const f32 dx = gAnchor.x - gPos.x, dz = gAnchor.z - gPos.z;
            gYaw = atan2f(dx, dz);
        }
        if (gLines < 60) {
            gLines++;
            re4_log("photo mode: camera effect %s", kFxName[gFx]);
        }
        gFxNow = gFx;
    }
    const f32 mul = speed == 1 ? 0.25f : speed == 2 ? 3.0f : 1.0f;
    if (gReset.exchange(0)) {
        gPos = gStartPos;
        gYaw = gStartYaw;
        gPitch = gStartPitch;
    }
    // turning right lowers the yaw (the view's right is (-cos, 0, sin) of it)
    gYaw -= yawRate * kTurn * kDt * (speed == 1 ? 0.4f : 1.0f) + gYawDelta.exchange(0) * kDeltaUnit;
    gPitch += pitchRate * kTilt * kDt * (speed == 1 ? 0.4f : 1.0f) + gPitchDelta.exchange(0) * kDeltaUnit;
    if (gPitch > kPitchMax) gPitch = kPitchMax;
    if (gPitch < -kPitchMax) gPitch = -kPitchMax;
    if (gYaw > 3.14159265f) gYaw -= 6.2831853f;
    if (gYaw < -3.14159265f) gYaw += 6.2831853f;
    const f32 step = kMove * mul * kDt;
    const f32 sy = sinf(gYaw), cy = cosf(gYaw);
    Vec want = gPos;
    want.x += (sy * mz - cy * mx) * step;
    want.z += (cy * mz + sy * mx) * step;
    want.y += rise * step;
    if (gFx == kFxOrbit) {
        // around Leon, about 12 degrees a second, at the distance and height it has; the sticks still move it
        const f32 rx = want.x - gAnchor.x, rz = want.z - gAnchor.z;
        const f32 a = 0.21f * kDt, ca = cosf(a), sa = sinf(a);
        want.x = gAnchor.x + rx * ca - rz * sa;
        want.z = gAnchor.z + rx * sa + rz * ca;
    }
    // walls, floors and the edge of the world stop it (the owner's device test of 19.11.2026: under the floor and
    // out of bounds sideways)
    if (want.x != gPos.x || want.y != gPos.y || want.z != gPos.z) moveTo(want);
}

// for the pictures on the PC: RE4_TEST_PHOTO="F:MASK[:a:b[:N]]": F frames after photo mode could first come photo mode comes on with the mask, the
// stick words a and b (hex) are held for N frames (default: to the end); "F:off" leaves it
void testScript()
{
    // RE4_TEST_PHOTO_ASK="F[,F...]": a request at these game frames (absolute; the refusals' test)
    static const char* asks = getenv("RE4_TEST_PHOTO_ASK");
    if (asks && pG) {
        for (const char* p = asks; *p;) {
            const unsigned at = (unsigned) strtoul(p, (char**) &p, 10);
            if (at == (unsigned) pG->Frame_cnt && !gActive) {
                gWant.store(1);
                gWantAt.store(at);
                re4_log("photo test: asked at game frame %u", at);
            }
            if (*p != ',') break;
            p++;
        }
    }
    static const char* script = getenv("RE4_TEST_PHOTO");
    if (!script || !pG) return;
    // its frames count from the first frame where photo mode could come (in play, no event)
    static unsigned base;
    if (!base) {
        if (refusal()) return;
        base = (unsigned) pG->Frame_cnt;
        re4_log("photo test: frame 0 is game frame %u", base);
    }
    const unsigned f = (unsigned) pG->Frame_cnt - base;
    static unsigned holdUntil;
    if (holdUntil && f >= holdUntil) {
        holdUntil = 0;
        gStickA.store(0);
        gStickB.store(0);
        re4_log("photo test: frame %u, sticks let go, camera at %.0f %.0f %.0f", f, (double) gPos.x, (double) gPos.y,
                (double) gPos.z);
    }
    for (const char* p = script; p && *p;) {
        unsigned at = (unsigned) strtoul(p, (char**) &p, 10);
        if (*p != ':') break;
        p++;
        if (!strncmp(p, "off", 3)) {
            p += 3;
            if (at == f) {
                gWant.store(2);
                re4_log("photo test: frame %u, off", f);
            }
        } else {
            const int mask = (int) strtol(p, (char**) &p, 16);
            int a = 0, b = 0;
            unsigned n = 0;
            if (*p == ':') a = (int) strtoul(p + 1, (char**) &p, 16);
            if (*p == ':') b = (int) strtoul(p + 1, (char**) &p, 16);
            if (*p == ':') n = (unsigned) strtoul(p + 1, (char**) &p, 10);
            if (at == f) {
                if (!gActive) {
                    gWant.store(1);
                    gWantAt.store((unsigned) pG->Frame_cnt);
                }
                gMask.store(mask);
                if (a || b) {
                    gStickA.store(a);
                    gStickB.store(b);
                    holdUntil = n ? f + n : 0;
                }
                re4_log("photo test: frame %u, mask %x, sticks %08x %08x for %u frames", f, mask, a, b, n);
            }
        }
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
}

}  // namespace

// The app (core_api.cpp re4_core_photo_mode, any thread): op 1 enter with the mask, 0 leave, 2 the mask alone
extern "C" void re4_photo_mode_request(int op, int mask)
{
    if (op == 1) {
        gMask.store(mask);
        gWantAt.store(pG ? (unsigned) pG->Frame_cnt : 0u);
        gWant.store(1);
    } else if (op == 0) {
        gWant.store(2);
    } else if (op == 2) {
        gMask.store(mask);
    }
}

// The app (re4_core_photo_input, any thread): a = move x, move z, rise, turn (s8 each, low byte first); b = tilt (s8),
// speed (0 normal, 1 slow, 2 fast), bit 16 the camera back to its start; c = turn and tilt deltas (s16 each, 1/8192 rad)
extern "C" void re4_photo_mode_input(int a, int b, int c)
{
    gStickA.store(a);
    gStickB.store(b & ~0x10000);
    if (b & 0x10000) gReset.store(1);
    if (c) {
        gYawDelta.fetch_add((s16) (c & 0xFFFF));
        gPitchDelta.fetch_add((s16) ((c >> 16) & 0xFFFF));
    }
}

// Patch 0243, the top of gameMainLoop: 1 while photo mode is on (the game's frame is left out)
int re4_photo_frame()
{
    testScript();
    const int want = gWant.exchange(0);
    if (!gActive) {
        if (want != 1) return 0;
        // a request the game did not reach in time (a loading, the option screen) is dropped
        const unsigned f = (unsigned) pG->Frame_cnt;
        if (f - gWantAt.load() > 20) {
            re4_log("photo mode: a request of frame %u came at frame %u, dropped", gWantAt.load(), f);
            re4_gx_photo(0, 1);
            return 0;
        }
        if (const char* why = refusal()) {
            re4_log("photo mode: not now in r%03x (%s)", (unsigned) pG->room_id, why);
            re4_gx_photo(0, 1);
            return 0;
        }
        enter();
    } else if (want == 2) {
        leave();
        return 0;
    }
    gFrames++;
    const int mask = gMask.load();
    if (mask != gMaskNow) applyMask(mask);
    moveCamera();
    setCamera();
    // the health bars with the HUD (not over hidden enemies)
    if (!(mask & (kHud | kEnemies))) re4_health_bars_call();
    return 1;
}

extern "C" int re4_photo_mode_on(void)
{
    return gActive ? 1 : 0;
}
