// Enemy health bars (1.1.0, wish of the project owner, 19.11.2026: the app menu's Cheats & Mods, ENEMIES AND
// WORLD, "Enemy health bars", off by default; like the RE4 remake: thin bars floating above the heads).
//
// Patch 0238 calls re4_health_bars() once a game frame right after the HUD's move (game.cpp Cckpt.move), when the
// frame's ordering tables are being filled. For each living enemy it queues one bar; all bars of the frame are
// drawn by one entry of the HUD's own table 0x13 (the cockpit units' otType), at its highest slot, which that table
// runs first: after the world, the effects and the picture filters (0x12: depth of field, blur), before the HUD's
// own units, so the life meter, the ammo and the messages stay on top. Drawn by the game's GX like its own 2D (an
// ortho projection over the viewport, vertex colours, no texture), so GLES, Vulkan and HD texture packs see the same.
//
//  - Health: the enemy's own hp / hp_max (cEm); without hp_max (0) the highest hp seen for that enemy is the max.
//    Green above 25 %, yellow up to 25 %, red up to 10 %. In the Plaga phase (the project owner, 19.11.2026: "wenn der
//    LasPlagas ausbricht durchgehend Orange") orange all the time, its length still the life left: a Ganado that lost
//    its head and grows the parasite (em10's lost-head routine: Parasite_on 1 and hp 3000, more than its hp_max, which
//    is why the bar was full again; the highest hp seen is its max from then on) or carries it (pParasite), the
//    parasite enemy itself (id 0x25) and the humanoid with the parasite head once it is out (em3c, Be_flg 0x800
//    "parasite set", em3cSetParasite). (cEm::flag 0x20 is no sign: the room lists give it to Ganados with heads.)
//  - Which: the enemies (ids 0x10..0x3F) apart from the friendly and the things: the merchant 0x18, the village dog
//    0x21, crows 0x23, the lake fish 0x27, chickens 0x28, traps 0x2A, the trucks and mine carts 0x3B, the support
//    helicopter 0x3D, the shooting-gallery targets 0x3E; never the player or the partner. Alive (hp > 0), drawn by
//    the game (cModel::isTrans), within 30 m of the camera, in front of it and on the screen, no wall between.
//  - Where: 25 cm above the highest part of its model (the head; a boss's top), centred on that part, the width by the distance (14 to 44
//    of the 512 units of the game's screen), a dark frame and the missing part dark grey.
//  - Not: in events and films (STA_EVENT, a movie, a door scene), with a sub screen (case, map, files), while the
//    game's pictures stand (SYS_TRANS_STOP), with the game not in play (Rno0 3).
#include "types.h"
#include "global.h"
#include "main.h"
#include "player.h"
#include "em.h"
#include "camera.h"
#include "sce_at.h"
#include "atari.h"
#include "at_sub.h"
#include "trans.h"
#include "trans_ot.h"
#include "light.h"
#include "math_sub_decl.h"
#include "gx.h"
#include "em3c.h"
#include "em10.h"
#include "vec.h"
#include "re4_gx_fifo.h"

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

extern GLOBAL_WK* pG;

extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_movie_playing(void);  // runtime/movie_status.cpp

namespace {

std::atomic<bool> gOn(false);  // the app's switch (runtime/aim_options.cpp, bit 20 of the aim bits)

constexpr int kMaxBars = 48;
constexpr float kFar = 30000.0f;  // 30 m

struct Bar {
    f32 x, y, w, h;  // the bar's box in the viewport's units (its centre at x, its top at y)
    f32 frac;        // health 0..1
    u8 plaga;        // the Plaga phase: orange
};

struct Frame {
    f32 vw, vh;  // the viewport (the ortho projection's size)
    int n;
    Bar bar[kMaxBars];
};

// The highest hp seen of the enemies without hp_max, by their work
struct Seen {
    const cEm* e;
    u8 id;
    s16 max;
};
Seen gSeen[256];

bool listed(int id)
{
    if (id < 0x10 || id > 0x3F) {
        return false;
    }
    switch (id) {
    case 0x18: case 0x21: case 0x23: case 0x27: case 0x28: case 0x2A: case 0x3B: case 0x3D: case 0x3E:
        return false;
    default:
        return true;
    }
}

// The Plaga phase (the head lost, the parasite out), see the head comment
bool plagaPhase(cEm* e)
{
    if (e->id >= 0x10 && e->id <= 0x20 && e->id != 0x18) {
        const FREE_EM10* w = EM10_WK(e);
        return w->Parasite_on != 0 || w->pParasite != 0;
    }
    if (e->id == 0x25) {
        return true;
    }
    if (e->id == 0x3C) {
        return (EM3C_WK(e)->Be_flg & 0x800) != 0;
    }
    return false;
}

int maxOf(u32 slot, cEm* e)
{
    if (e->hp_max > 0 && e->hp <= e->hp_max) {
        return e->hp_max;
    }
    if (slot >= 256) {
        return e->hp_max > e->hp ? e->hp_max : e->hp;
    }
    Seen& s = gSeen[slot];
    if (s.e != e || s.id != e->id) {
        s.e = e;
        s.id = e->id;
        s.max = 0;
    }
    if (e->hp > s.max) {
        s.max = e->hp;
    }
    if (e->hp_max > s.max) {
        s.max = e->hp_max;
    }
    return s.max;
}

// The top of the model: the highest part (the head; a boss's top), else 1.8 m above its place
Vec topOf(cEm* e)
{
    Vec top = {e->pos.x, -1e30f, e->pos.z};
    int n = 0;
    for (cParts* p = e->pList; p && n < 128; p = p->pList, n++) {
        if (p->world.y > top.y) {
            top = p->world;
        }
    }
    const f32 dx = top.x - e->pos.x, dz = top.z - e->pos.z;
    if (n == 0 || top.y < e->pos.y || top.y > e->pos.y + 20000.0f || dx * dx + dz * dz > 4000.0f * 4000.0f) {
        top.x = e->pos.x;
        top.y = e->pos.y + 1800.0f;
        top.z = e->pos.z;
    }
    return top;
}

bool clearLine(const Vec& a, const Vec& b)
{
    Vec p = a, q = b, hit;
    return SatMgr.hitCheck(&p, &q, &hit, 0, 0, 0) == 0;
}

void quad(f32 x0, f32 y0, f32 x1, f32 y1, u8 r, u8 g, u8 b, u8 a)
{
    const f32 xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    for (int i = 0; i < 4; i++) {
        re4_gx_put_f32(xs[i]);  // the port's GX FIFO (re4_gx_fifo.h), as the game's GXWGFifo stores become
        re4_gx_put_f32(ys[i]);
        GXColor4u8(r, g, b, a);
    }
}

// The table 0x13 entry: every bar of the frame
void draw(void* data)
{
    const Frame* f = (const Frame*) data;
    if (!f || f->n <= 0) {
        return;
    }
    // as the game's Draw_tile (dbmodule.cpp Render_tile), with blending and without the depth test; the values are
    // the GX enums (the game's gx.h takes them as numbers)
    Mtx44 proj;
    Mtx m;
    C_MTXOrtho(proj, 0.0f, f->vh, 0.0f, f->vw, 0.0f, -100.0f);
    GXSetProjection(proj, 1);                     // GX_ORTHOGRAPHIC
    PSMTXIdentity(m);
    GXLoadPosMtxImm(m, 0);                        // GX_PNMTX0
    GXSetCurrentMtx(0);
    GXColor none = {0, 0, 0, 0};
    GXSetFog(0, 0.0f, 0.0f, 0.1f, 1.0f, none);    // GX_FOG_NONE
    GXSetBlendMode(1, 4, 5, 0);                   // GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR
    GXSetAlphaCompare(7, 0, 1, 7, 0);             // GX_ALWAYS (as ExecOt leaves it)
    GXSetZMode(0, 7, 0);                          // no depth test, no depth write
    GXSetColorUpdate(1);
    GXSetCullMode(0);                             // GX_CULL_NONE
    GXSetNumChans(1);
    GXSetChanCtrl(4, 0, 0, 1, 0, 0, 2);           // GX_COLOR0A0 from the vertex colour
    GXSetNumTexGens(0);
    GXSetNumIndStages(0);
    GXSetTevDirect(0);
    GXSetNumTevStages(1);
    GXSetTevOrder(0, 0xFF, 0xFF, 4);              // no texture, GX_COLOR0A0
    GXSetTevOp(0, 4);                             // GX_PASSCLR
    GXClearVtxDesc();
    GXSetVtxDesc(9, 1);                           // GX_VA_POS, GX_DIRECT
    GXSetVtxDesc(0xB, 1);                         // GX_VA_CLR0, GX_DIRECT
    GXSetVtxAttrFmt(0, 9, 0, 4, 0);               // GX_POS_XY, GX_F32
    GXSetVtxAttrFmt(0, 0xB, 1, 5, 0);             // GX_CLR_RGBA, GX_RGBA8
    GXBegin(0x80, 0, (u16) (f->n * 12));  // GX_QUADS
    for (int i = 0; i < f->n; i++) {
        const Bar& b = f->bar[i];
        const f32 x0 = b.x - b.w * 0.5f, x1 = b.x + b.w * 0.5f, y0 = b.y, y1 = b.y + b.h;
        const f32 e = 1.0f;  // the frame
        quad(x0 - e, y0 - e, x1 + e, y1 + e, 0, 0, 0, 200);
        const f32 split = x0 + b.w * b.frac;
        quad(split, y0, x1, y1, 60, 60, 60, 200);
        u8 r = 70, g = 200, bl = 60;  // green
        if (b.plaga) {
            r = 245, g = 130, bl = 20;  // orange: the Plaga phase
        } else if (b.frac <= 0.10f) {
            r = 225, g = 40, bl = 30;  // red
        } else if (b.frac <= 0.25f) {
            r = 235, g = 200, bl = 40;  // yellow
        }
        quad(x0, y0, split, y1, r, g, bl, 235);
    }
    LightMgr.setFog();
}

// For the pictures on the PC: RE4_TEST_HB_HP="F:HP[,F:HP...]": at game frame F the k-th nearest listed enemy (k the
// entry's place) is left with life HP, a real change of its hp as a hit that does not kill (yellow and red bars)
void testHp()
{
    static const char* v = getenv("RE4_TEST_HB_HP");
    static bool done[4];
    if (!v) {
        return;
    }
    const char* p = v;
    for (int k = 0; k < 4 && *p; k++) {
        unsigned frame = 0;
        int hp = 0;
        if (sscanf(p, "%u:%d", &frame, &hp) != 2) {
            return;
        }
        if (!done[k] && pG->Frame_cnt >= frame) {
            // the k-th nearest by the player's distance (l_pl, squared)
            cEm* pick = 0;
            f32 last = -1.0f;
            for (int r = 0; r <= k; r++) {
                cEm* best = 0;
                for (u32 i = 0; i < EmMgr.getArrayNum(); i++) {
                    cEm* e = EmMgr.fastAt(i);
                    if (!e || !e->isAlive() || !listed(e->id) || e->hp <= 0 || e->l_pl <= last) {
                        continue;
                    }
                    if (!best || e->l_pl < best->l_pl) {
                        best = e;
                    }
                }
                pick = best;
                last = best ? best->l_pl : 1e30f;
            }
            done[k] = true;
            if (pick) {
                re4_log("health bars: RE4_TEST_HB_HP: enemy id 0x%02x %.1f m away left with life %d of %d", pick->id,
                        sqrtf(pick->l_pl) / 1000.0f, hp, pick->hp_max);
                pick->hp = (s16) hp;
            }
        }
        const char* c = strchr(p, ',');
        if (!c) {
            break;
        }
        p = c + 1;
    }
}

// For the pictures on the PC: RE4_TEST_HB_PLAGA="F": at game frame F the nearest Ganado gets what em10's lost-head routine gives it: Parasite_on 1, hp 3000
// and Parasite_wait 1, so the game itself makes the parasite come out the next frame (em10SetParasite); its head stays
void testPlaga()
{
    static long frame = getenv("RE4_TEST_HB_PLAGA") ? strtol(getenv("RE4_TEST_HB_PLAGA"), 0, 10) : -1;
    if (frame < 0 || (long) pG->Frame_cnt < frame) {
        return;
    }
    frame = -1;
    cEm* pick = 0;
    for (u32 i = 0; i < EmMgr.getArrayNum(); i++) {
        cEm* e = EmMgr.fastAt(i);
        if (!e || !e->isAlive() || e->hp <= 0 || e->id < 0x10 || e->id > 0x20 || e->id == 0x18) {
            continue;
        }
        if (!pick || e->l_pl < pick->l_pl) {
            pick = e;
        }
    }
    if (!pick) {
        re4_log("health bars: RE4_TEST_HB_PLAGA: no Ganado");
        return;
    }
    FREE_EM10* w = EM10_WK(pick);
    w->Parasite_on = 1;
    w->Parasite_wait = 1;
    pick->hp = 3000;
    re4_log("health bars: RE4_TEST_HB_PLAGA: Ganado id 0x%02x type %d %.1f m away: the parasite comes out (hp 3000 of "
            "%d)", pick->id, (int) pick->type, sqrtf(pick->l_pl) / 1000.0f, pick->hp_max);
}

unsigned gLines, gSums;
// why enemies were left out (a line every 300 frames while any is listed)
struct Count {
    unsigned listed, far, behind, off, wall, shown;
} gCount;
const cEm* gLogged[64];

}  // namespace

extern "C" {

// The app's switch (runtime/aim_options.cpp)
void re4_health_bars_set(int on)
{
    if (gOn.exchange(on != 0) != (on != 0)) {
        re4_log("health bars: %s (the app's switch)", on ? "on" : "off");
    }
}

// Patch 0238 (game.cpp, after Cckpt.move): the bars of this frame
void re4_health_bars(void)
{
    if (!gOn.load(std::memory_order_relaxed) || !pG || !pPL || pG->Rno0 != 3) {
        return;
    }
    if (StaFlagChk(pG, STA_EVENT) || StaFlagChk(pG, STA_SUB_SCRN) || SysFlagChk(pG, SYS_TRANS_STOP) ||
        SysFlagChk(pG, SYS_DOORDEMO) || re4_movie_playing()) {
        return;
    }
    testHp();
    testPlaga();
    Frame* f = (Frame*) GetPrimBuff(sizeof(Frame));
    if (!f) {
        return;
    }
    f32 vp[6];
    GXGetViewportv(vp);
    f->vw = vp[2] > 1.0f ? vp[2] : 512.0f;
    f->vh = vp[3] > 1.0f ? vp[3] : 448.0f;
    f->n = 0;
    const Vec cam = pG->Camera.param.Campos;
    for (u32 i = 0; i < EmMgr.getArrayNum() && f->n < kMaxBars; i++) {
        cEm* e = EmMgr.fastAt(i);
        if (!e || !e->isAlive() || !listed(e->id) || (cEm*) e == (cEm*) pPL || e == pSUB || e->hp <= 0 ||
            !e->isTrans()) {
            continue;
        }
        const int max = maxOf(i, e);
        if (max <= 0) {
            continue;
        }
        gCount.listed++;
        Vec top = topOf(e);
        top.y += 250.0f;
        const f32 dx = top.x - cam.x, dy = top.y - cam.y, dz = top.z - cam.z;
        const f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist > kFar || dist < 300.0f) {
            gCount.far++;
            continue;
        }
        Vec scr;
        if (!GetScreenPos(&top, &scr)) {
            gCount.behind++;
            continue;
        }
        f32 sx = scr.x - vp[0], sy = scr.y - vp[1];
        if (sy < 12.0f && sx > 0.0f && sx < f->vw) {
            // the head above the picture (a boss right in front): the bar at the top edge while its body is shown
            Vec base = {e->pos.x, e->pos.y + 500.0f, e->pos.z}, bs;
            if (GetScreenPos(&base, &bs) && bs.y - vp[1] > 12.0f && bs.y - vp[1] < f->vh) {
                sy = 12.0f;
            }
        }
        if (!(sx > -20.0f && sx < f->vw + 20.0f && sy > 0.0f && sy < f->vh)) {
            gCount.off++;
            continue;
        }
        if (!clearLine(cam, top)) {
            gCount.wall++;
            continue;
        }
        gCount.shown++;
        f32 w = 44.0f * 4000.0f / dist;
        w = w < 14.0f ? 14.0f : w > 44.0f ? 44.0f : w;
        f32 h = w * 0.11f;
        h = h < 2.5f ? 2.5f : h > 4.5f ? 4.5f : h;
        Bar& b = f->bar[f->n++];
        b.x = sx;
        b.y = sy - h;
        b.w = w;
        b.h = h;
        f32 frac = (f32) e->hp / (f32) max;
        b.frac = frac < 0.0f ? 0.0f : frac > 1.0f ? 1.0f : frac;
        b.plaga = plagaPhase(e) ? 1 : 0;
        bool known = false;
        for (unsigned k = 0; k < gLines && k < 64; k++) {
            known |= gLogged[k] == e;
        }
        if (!known && gLines < 64) {
            gLogged[gLines++] = e;
            re4_log("health bars: r%03x frame %u: enemy id 0x%02x type %d hp %d/%d (max %s) at %.1f m, bar at "
                    "(%.0f %.0f) %.0fx%.1f%s", (unsigned) pG->room_id, (unsigned) pG->Frame_cnt, (unsigned) e->id,
                    (int) e->type, (int) e->hp, max, e->hp_max > 0 ? "hp_max" : "highest seen", dist / 1000.0f,
                    b.x, b.y, w, h, b.plaga ? ", the Plaga phase (orange)" : "");
        }
        // the Plaga phase coming (once an enemy, while the frame's lines last)
        static const cEm* plagaLogged[32];
        static unsigned plagaLines;
        if (b.plaga && plagaLines < 32) {
            bool seen = false;
            for (unsigned k = 0; k < plagaLines; k++) {
                seen |= plagaLogged[k] == e;
            }
            if (!seen) {
                plagaLogged[plagaLines++] = e;
                re4_log("health bars: r%03x frame %u: enemy id 0x%02x type %d in the Plaga phase, hp %d/%d: orange",
                        (unsigned) pG->room_id, (unsigned) pG->Frame_cnt, (unsigned) e->id, (int) e->type,
                        (int) e->hp, max);
            }
        }
    }
    if (gCount.listed && pG->Frame_cnt % 300 == 0 && gSums < 40) {
        gSums++;
        re4_log("health bars: r%03x frame %u: %u enemies, %u bars; left out: %u far, %u behind the camera, %u off the "
                "screen, %u behind a wall", (unsigned) pG->room_id, (unsigned) pG->Frame_cnt, gCount.listed,
                gCount.shown, gCount.far, gCount.behind, gCount.off, gCount.wall);
    }
    gCount = Count();
    if (f->n > 0) {
        AddOtDirect(0x13, f, (void (*)()) draw, 0xFFFF, 0x1000, 0, 0.0f);
    }
}

}  // extern "C"

// Patch 0238 declares it at block scope in game.cpp (C++ linkage there)
void re4_health_bars_call()
{
    re4_health_bars();
}
