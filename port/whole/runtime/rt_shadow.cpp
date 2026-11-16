// The app's real-time shadows of the figures (port/docs/echtzeit-schatten-studie.md; debug builds only until after
// 1.0, decision of the project owner, 15.11.2026: "nur in der Debug Version die ich nutze aktiviert, nicht wenn man
// Release baut"). Compiled with the game's flags (re4_gamehooks_obj).
//
// The renderer draws the shadows (gx_core.cpp, GxCore::ShadowPass); the core says where and from which light:
//  - re4_rt_shadow_frame (patch 0240, at the start of Render): this frame casts real-time shadows when the app
//    switched them on (bit 5 of the Potato mask, core_api.cpp re4_core_set_potato -> re4_potato_set), Potato's
//    "Shadows & light" is on, the game is in play (main loop, no event or cut scene, no sub screen, no death demo or
//    continue screen, no movie, no thermal scope) and a foot shadow light reaches the player. That is the light the
//    room's designers set for the foot shadows (LIT: id 4, kind 5; 108 of the debug disc's 127 rooms have one):
//    its direction is the shadows' direction, as foot_shadow.cpp DrawFootShadow takes it (type 1/2: Ang_x, Ang_y;
//    type 0: from the light to the player), its red the strength, as there.
//  - re4_rt_shadow_blobs_off (patch 0240, DrawFootShadow): the game's foot shadow blobs are left out in such a frame,
//    so no figure gets a blob and a shadow. Rooms without a foot shadow light have no blobs and get no shadows.
//  - re4_rt_shadow_mark (patch 0240, in Render after the ordering table 14: the room and the figures (table 11) and
//    the game's own shadow receivers are drawn, the effects not yet): the port command re4_gx_put_shadow with the
//    light's direction in view space. The renderer draws the figures it saw so far from that light and darkens the
//    scene behind them.
// The game reads nothing of this: the blobs are only drawn, never read. Off (every release build), nothing changes:
// no command, the blobs as before.
#include "types.h"
#include "global.h"
#include "light.h"
#include "model.h"
#include "em.h"
#include "player.h"
#include "camera.h"
#include "math_sub.h"

#include "re4_gx_fifo.h"
#include "re4_runtime.h"

#include <math.h>

namespace {

volatile int gOn;          // the app's switch (bit 5 of the Potato mask)
volatile int gObjects;     // "Characters and objects" (bit 6 of the Potato mask)
bool gActive;              // this frame casts real-time shadows (re4_rt_shadow_frame)
unsigned gFrameNo;         // Render's frames (re4_rt_shadow_frame; gx_owner.cpp tells its frames apart)
unsigned gRoom = ~0u;      // the room told last
Vec gDir;                  // the light's direction (world)
unsigned gStrength;        // 0..255
int gWhy = -1;             // the reason logged last (Why)
unsigned gMarks;           // frames marked since the last log line

enum Why { kOff, kPotato, kNotPlaying, kNoLight, kOn };

const char* whyText(int why)
{
    switch (why) {
    case kOff: return "off (the app's switch)";
    case kPotato: return "off (Potato: shadows and light effects off)";
    case kNotPlaying: return "paused (event, sub screen, death or continue screen, movie)";
    case kNoLight: return "paused (no foot shadow light reaches the player in this room part; the game's own shadows)";
    case kOn:
        return gObjects ? "on (characters and objects; the foot shadow light reaches the player, no round blobs)"
                        : "on (characters; the foot shadow light reaches the player, no round blobs)";
    }
    return "?";
}

void note(int why)
{
    if (why == gWhy) {
        return;
    }
    gWhy = why;
    re4_log("real-time shadows: %s, r%03x (%u frames marked before)", whyText(why), pG ? (unsigned) pG->room_id : 0u,
            gMarks);
    gMarks = 0;
}

// The first foot shadow light that reaches the player, as DrawFootShadow chooses for a figure (its enable mask, the
// blackout, the radius; a spot type 2 by its cone); its direction into `dir`, its strength into `strength`.
bool footLight(cModel* m, Vec* dir, unsigned* strength)
{
    Vec pos = m->pos;
    cLight* l = LightMgr.getActiveWork();
    for (int first = 1; l; first = 0) {
        if (!first) {
            l = LightMgr.getNext(l);
            if (!l) {
                break;
            }
        }
        LIT04_MOVE_FREE* w = (LIT04_MOVE_FREE*) l->work;
        if ((l->be_flag & 3) != 3 || l->Id != 4 || w->Kind != 5) {
            continue;
        }
        if (!(l->EnableMask & m->LightInfo.EnableMask)) {
            continue;
        }
        if (StaFlagChk(pG, STA_BLACKOUT) && (l->Kind & 0x80)) {
            continue;
        }
        Vec lpos;
        l->getPos(&lpos);
        const f32 range = l->Radius == 0.0f ? 1000000000.0f : l->Radius;
        if (PSVECSquareDistance(&pos, &lpos) > range * range) {
            continue;
        }
        Vec d;
        if (l->Type == 1 || l->Type == 2) {
            // foot_shadow.cpp: RotY(Ang_y) * RotX(Ang_x) * (0, -1, 0)
            const f32 ax = (f32) w->Ang_x * 6.2831855f / 360.0f, ay = (f32) w->Ang_y * 6.2831855f / 360.0f;
            d.x = -sinf(ax) * sinf(ay);
            d.y = -cosf(ax);
            d.z = -sinf(ax) * cosf(ay);
            if (l->Type == 2) {
                // a spot: only inside its cone (DrawFootShadow)
                Vec t;
                PSVECSubtract(&m->pList->world, &lpos, &t);
                const f32 dot = PSVECDotProduct(&d, &t);
                Vec c;
                PSVECScale(&d, &c, dot);
                PSVECAdd(&lpos, &c, &c);
                f32 ang = (f32) w->Fovy;
                if (ang == 0.0f) {
                    ang = 90.0f;
                }
                if (PSVECSquareDistance(&pos, &c) > sinf(ang * (3.14159265f / 180.0f)) * dot) {
                    continue;
                }
            }
        } else if (l->Type == 0) {
            PSVECSubtract(&m->pList->world, &lpos, &d);
            const f32 len = PSVECMag(&d);
            if (!(len > 1.0f)) {
                continue;
            }
            PSVECScale(&d, &d, 1.0f / len);
        } else {
            continue;
        }
        *dir = d;
        *strength = (unsigned) l->Col.r * (255u - m->Shd_color) / 255u;
        return true;
    }
    return false;
}

}  // namespace

extern "C" {

// bits 5 (on) and 6 (also the objects) of the Potato mask (potato.cpp re4_potato_set). Each change is logged as it
// arrives (16.11.2026: a device test showed nothing, and nothing in the log told whether the core had the setting;
// the app had cut the bit off); never in a release build, which never sets the bits.
void re4_rt_shadow_set(int bits)
{
    const int on = (bits & 0x20) != 0, objects = on && (bits & 0x40) != 0;
    if (gOn != on || gObjects != objects) {
        gOn = on;
        gObjects = objects;
        re4_log("real-time shadows: the app's setting arrived: %s",
                on ? (objects ? "characters and objects" : "characters") : "off");
        gWhy = on ? -1 : kOff;  // on: the next frame tells where they stand
    }
}

// gx_owner.cpp: the classes the owner tags carry in this picture: 0 none (off, or this frame casts none: the tags as
// before), 1 the figures' parts, 2 also the objects
int re4_rt_shadow_classes(void)
{
    return gActive ? (gObjects ? 2 : 1) : 0;
}

// gx_owner.cpp: the number of this frame (Render's)
unsigned re4_rt_shadow_frame_no(void)
{
    return gFrameNo;
}

int re4_potato_no_shadows(void);

// Patch 0240, Render's start: does this frame cast real-time shadows?
void re4_rt_shadow_frame(void)
{
    gActive = false;
    gFrameNo++;
    if (!gOn) {
        return;
    }
    // each room tells anew where they stand (on, or paused and why)
    if (pG && pG->room_id != gRoom) {
        gRoom = pG->room_id;
        gWhy = -1;
    }
    if (re4_potato_no_shadows()) {
        note(kPotato);
        return;
    }
    if (!pG || !pPL || pG->Rno0 != 3 || DpfFlagChk(pG, DPF_SHADOW) || StaFlagChk(pG, STA_EVENT) ||
        StaFlagChk(pG, STA_SUB_SCRN) || StaFlagChk(pG, STA_DIEDEMO) || StaFlagChk(pG, STA_MOVIE_ON) ||
        StaFlagChk(pG, STA_MOVIE2_ON) || StaFlagChk(pG, STA_THERMO_GRAPH)) {
        note(kNotPlaying);
        return;
    }
    if (!footLight(pPL, &gDir, &gStrength) || gStrength == 0) {
        note(kNoLight);
        return;
    }
    gActive = true;
    note(kOn);
}

// Patch 0240, DrawFootShadow: no blobs in a frame with real-time shadows.
int re4_rt_shadow_blobs_off(void)
{
    return gActive;
}

// Patch 0240, Render after the ordering table 14: the shadows go on the scene here.
void re4_rt_shadow_mark(void)
{
    if (!gActive) {
        return;
    }
    Vec v;
    PSMTXMultVecSR(pG->Camera.v_mat, &gDir, &v);
    re4_gx_put_shadow(v.x, v.y, v.z, gStrength);
    gMarks++;
}

}  // extern "C"
