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
//    0x21, crows 0x23, snakes 0x24, cows 0x26, the lake fish 0x27, chickens 0x28, bats 0x29, traps 0x2A, the trucks
//    and mine carts 0x3B, the support helicopter 0x3D, the shooting-gallery targets 0x3E; never the player or the
//    partner (Ashley, Luis: ids below 0x10, the sub characters). The Plaga dogs 0x22, the parasites 0x25, the bosses
//    (Del Lago 0x2F too) keep theirs. Alive (hp > 0), drawn by
//    the game (cModel::isTrans), within 15 m of the camera (kShow; 30 m before 1.1.0's fade), in front of it and on
//    the screen, no wall between.
//  - Fading (the project owner, 21.11.2026: "erst ab einer gewissen Entfernung einblenden und einfaden. Nicht hart
//    einblenden"): each enemy's bar fades in over 0.4 s (kFadeFrames, 12 of the game's 30 frames a second, the same at
//    30 and 60 FPS) when it comes within 15 m with a clear sight, and fades out over the same time when it goes
//    farther, behind a wall or a closed door, or dies (its last sliver of red). Off the screen, behind the camera, or
//    after a pause of the bars (an event, a sub screen, a new room) it starts from nothing again.
//  - Where: 25 cm above the head, centred on it: the humanoids' head part (see anchorOf; a raised axe or sickle no
//    longer pulls it away), the other enemies' highest part (a boss's top); small jumps smoothed. The width by the
//    distance (14 to 44 of the 512 units of the game's screen), a dark frame and the missing part dark grey. "No wall
//    between": the sight and shot collision from the camera to the head or the aim point, and closed doors (see
//    clearLine, doorBetween).
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
#include "emdoor.h"
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
constexpr float kFar = 30000.0f;   // 30 m: the figures the room's log line names (nearby)
constexpr float kShow = 15000.0f;  // 15 m: the bars' distance from the camera (1.1.0, with the fade)
constexpr float kFadeFrames = 12.0f;  // 0.4 s of the game's 30 frames a second

struct Bar {
    f32 x, y, w, h;  // the bar's box in the viewport's units (its centre at x, its top at y)
    f32 frac;        // health 0..1
    u8 plaga;        // the Plaga phase: orange
    u8 alpha;        // the fade (255: whole)
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

// Only the enemy types the game's list editor names (t_emlist.cpp) and that fight Leon: a list of what gets a bar, not
// of what does not (the owner's device test of 19.11.2026: a grey bar over the merchant; whatever it was, a type
// outside this list never gets one): the Ganados 0x10..0x20 without the merchant 0x18, the Plaga dogs 0x22, the
// parasites 0x25, the giant 0x2B, the insect boss and its kind 0x2C/0x2D, 0x2E, Del Lago 0x2F, the bosses and big
// enemies 0x30..0x39 (no 0x33, 0x37: other forms of 0x34), the gunship 0x3A, the armours 0x3C. Never: the merchant,
// the dog 0x21, crows 0x23, snakes 0x24, cows 0x26, fish 0x27, chickens 0x28, bats 0x29, traps 0x2A, vehicles 0x3B,
// the support helicopter 0x3D, targets 0x3E, the partners and the player (ids below 0x10).
bool listed(int id)
{
    if (id >= 0x10 && id <= 0x20) {
        return id != 0x18;
    }
    switch (id) {
    case 0x22: case 0x25: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x32:
    case 0x34: case 0x35: case 0x36: case 0x38: case 0x39: case 0x3A: case 0x3C:
        return true;
    default:
        return false;
    }
}

// The merchant as a Ganado figure: type 6 of the modules 0x13, 0x14 and 0x1E wears the merchant's cloth (em10.cpp
// Em18ClothMove), stands inactive and has 1 life left (r104, r10f, r11c, the castle and the island; the owner's device
// test of 19.11.2026: the grey bar over the merchant in the night forest, r10f). The randomizer leaves it out as well
// (enemy_random.cpp ordinary).
bool traderForm(const cEm* e)
{
    return e->type == 6 && (e->id == 0x13 || e->id == 0x14 || e->id == 0x1E);
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

// The part `no` by the parts' own chain (getPartsPtr does not check the end of a sequential list)
cParts* partAt(cEm* e, int no)
{
    cParts* part = e->pList;
    for (int i = 0; part && i < no && i < 128; i++) {
        part = part->pList;
    }
    return part;
}

// Where the bar sits: the head of a humanoid (the Ganados, Zealots and soldiers 0x10..0x20, em10's skeleton: part 4,
// the head em10NeckMove turns, as the player's own), not in the Plaga phase (the head gone); else the top of the model.
// The highest part was the hand with a raised axe or sickle at times (the project owner's device test, 20.11.2026:
// "springt die Healthbar ... auf über die Waffe").
Vec anchorOf(cEm* e)
{
    // RE4_HB_OLD_ANCHOR=1 (a PC comparison only): the highest part, as before
    static const bool old = getenv("RE4_HB_OLD_ANCHOR") != 0;
    if (!old && e->id >= 0x10 && e->id <= 0x20 && e->id != 0x18 && !plagaPhase(e)) {
        if (cParts* head = partAt(e, 4)) {
            const Vec h = head->world;
            const f32 dx = h.x - e->pos.x, dz = h.z - e->pos.z;
            if (h.y > e->pos.y - 500.0f && h.y < e->pos.y + 3500.0f && dx * dx + dz * dz < 2000.0f * 2000.0f) {
                return h;
            }
        }
    }
    return topOf(e);
}

// The anchor of the frame before, by the enemy's slot: a jump of less than 40 cm is halved (the head's bob, a top
// part changing), a larger one or a new enemy taken at once
struct Anchor {
    const cEm* e;
    u32 frame;
    Vec v;
};
Anchor gAnchor[256];

Vec smoothed(u32 slot, cEm* e, const Vec& now)
{
    if (slot >= 256) {
        return now;
    }
    Anchor& a = gAnchor[slot];
    const u32 frame = (u32) pG->Frame_cnt;
    const f32 dx = now.x - a.v.x, dy = now.y - a.v.y, dz = now.z - a.v.z;
    if (a.e == e && frame - a.frame <= 2 && dx * dx + dy * dy + dz * dz < 400.0f * 400.0f) {
        a.v.x += dx * 0.5f;
        a.v.y += dy * 0.5f;
        a.v.z += dz * 0.5f;
    } else {
        a.v = now;
    }
    a.e = e;
    a.frame = frame;
    return a.v;
}

// The fade of each enemy's bar by its slot (see the head comment): 0 nothing .. 1 whole
struct Fade {
    const cEm* e;
    u32 frame;
    f32 a;
};
Fade gFade[256];

}  // namespace

// One step of a bar's fade: `frames` game frames since its last step, towards whole (`want`) or nothing
extern "C" float re4_hb_fade_step(float a, int want, unsigned frames)
{
    const float step = (float) (frames > 3 ? 3 : frames) / kFadeFrames;
    a += want ? step : -step;
    return a < 0.001f ? 0.0f : a > 0.999f ? 1.0f : a;  // (the float sum of 12 twelfths: whole)
}

namespace {

// The fade of the enemy in `slot` this frame: a new enemy there or a gap of more than 3 frames (the bars paused, off
// the screen, a new room) starts from nothing, so a bar always fades in
// RE4_HB_NO_FADE=1 (a PC comparison only): as before 1.1.0's fade, 30 m and no fade
bool noFade()
{
    static const bool off = getenv("RE4_HB_NO_FADE") != 0;
    return off;
}

f32 fadeTo(u32 slot, cEm* e, bool want)
{
    if (slot >= 256 || noFade()) {
        return want ? 1.0f : 0.0f;
    }
    Fade& f = gFade[slot];
    const u32 frame = (u32) pG->Frame_cnt;
    if (f.e != e || frame - f.frame > 3) {
        f.a = 0.0f;
        f.frame = frame - 1;
    }
    f.a = re4_hb_fade_step(f.a, want ? 1 : 0, frame - f.frame);
    f.e = e;
    f.frame = frame;
    return f.a;
}

// Whether the enemy in `slot` still shows a bar fading out (without a step)
bool fading(u32 slot, cEm* e)
{
    if (slot >= 256) {
        return false;
    }
    const Fade& f = gFade[slot];
    return f.e == e && f.a > 0.0f && (u32) pG->Frame_cnt - f.frame <= 3;
}

// A bar dropped at once (off the screen, behind the camera): from nothing when it comes back
void dropFade(u32 slot)
{
    if (slot < 256) {
        gFade[slot].a = 0.0f;
    }
}

// The doors (cEmDoor, construct id 0x41) between a and b: their effect collision pieces (emdoor.cpp emDoorSatSet: the
// leaf, the panel above it, the panes; turning with the leaf as it opens, a broken pane's piece gone) carry
// EAT_ATTR_SMALL_NOHIT (the door takes the bullets itself), which the sight test below leaves out as the game's aim
// test does. The project owner's device test, 20.11.2026: a bar through a closed door. Barred gates and racks carry
// the same attribute and stay see-through.
unsigned gDoorLines;  // the sight lines a door blocked (the summary in the log)

bool doorBetween(const Vec& a, const Vec& b)
{
    // RE4_HB_NO_DOORS=1 (a PC comparison only): without this test, as before
    static const bool off = getenv("RE4_HB_NO_DOORS") != 0;
    if (off) {
        return false;
    }
    for (u32 i = 0; i < EmMgr.getArrayNum(); i++) {
        cEm* e = EmMgr.fastAt(i);
        if (!e || !e->isAlive() || e->id != 0x41 || e->hp <= 0) {
            continue;
        }
        FREE_EMDOOR* w = EMDOOR_WK(e);
        for (int k = 1; k < 6; k++) {
            cSat* sat = w->pSat[k];
            if (!sat || !sat->isAlive() || !sat->poly_p) {
                continue;
            }
            Vec pa = a, pb = b, la, lb, cross;
            PSMTXMultVec(sat->imat, &pa, &la);
            PSMTXMultVec(sat->imat, &pb, &lb);
            for (u32 n = 0; n < sat->polygon_num; n++) {
                if (At_poly_line_ck(sat, &cross, &sat->poly_p[n], &la, &lb, 0, 0)) {
                    gDoorLines++;
                    return true;
                }
            }
        }
    }
    return false;
}

// A clear sight from the camera: the room's sight and shot collision (EatMgr, flag 0x800), as the game's own aim
// test asks it (pl_wep.cpp, the lock-on target's visibility); not the walking collision (SatMgr), whose invisible
// edges of balconies, upper floors and ledges hid the bars when Leon stood above or below an enemy (the project
// owner's device test, 19.11.2026)
bool clearLine(const Vec& a, const Vec& b)
{
    // RE4_HB_WALKING_WALLS=1 (a PC comparison only): the first version's test through the walking collision
    static const bool walking = getenv("RE4_HB_WALKING_WALLS") != 0;
    Vec p = a, q = b, hit;
    if (walking) {
        return SatMgr.hitCheck(&p, &q, &hit, 0, 0, 0) == 0;
    }
    return EatMgr.hitCheck(&p, &q, 0, 0, 0x800, 0) == 0 && !doorBetween(a, b);
}

// Seen from the camera: the bar's point above the head or the enemy's own aim point (its lock-on part and offset)
bool seen(const Vec& cam, const Vec& top, cEm* e)
{
    if (clearLine(cam, top)) {
        return true;
    }
    // the lock-on part
    cParts* part = partAt(e, (int) e->lockParts);
    if (!part) {
        return false;
    }
    Vec aim;
    PSMTXMultVec(part->mat, &e->lockOfs, &aim);
    return clearLine(cam, aim);
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
        const u8 frameA = (u8) (200 * b.alpha / 255), barA = (u8) (235 * b.alpha / 255);  // the fade
        quad(x0 - e, y0 - e, x1 + e, y1 + e, 0, 0, 0, frameA);
        const f32 split = x0 + b.w * b.frac;
        quad(split, y0, x1, y1, 60, 60, 60, frameA);
        u8 r = 70, g = 200, bl = 60;  // green
        if (b.plaga) {
            r = 245, g = 130, bl = 20;  // orange: the Plaga phase
        } else if (b.frac <= 0.10f) {
            r = 225, g = 40, bl = 30;  // red
        } else if (b.frac <= 0.25f) {
            r = 235, g = 200, bl = 40;  // yellow
        }
        quad(x0, y0, split, y1, r, g, bl, barA);
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

// For the pictures on the PC: RE4_TEST_HB_DOOR="F[:open][:DOOR]": from game frame F on for 200 frames, the player
// stands 3.5 m in front of door number DOOR (by the list order, 0 the first; id 0x41) facing it and the nearest
// listed enemy 1.2 m behind it, both held there; ":open" opens the door at F + 30 (cEmDoor::setOpen, away from the
// player)
void testDoor()
{
    static const char* v = getenv("RE4_TEST_HB_DOOR");
    static long from = v ? strtol(v, 0, 10) : -1;
    static const bool open = v && strstr(v, ":open");
    static const int pick = v && strrchr(v, ':') && strrchr(v, ':')[1] >= '0' && strrchr(v, ':')[1] <= '9'
                                ? atoi(strrchr(v, ':') + 1)
                                : 0;
    static cEm* enemy;
    if (from < 0 || (long) pG->Frame_cnt < from || !pPL) {
        return;
    }
    const long t = (long) pG->Frame_cnt - from;
    if (t > 200) {
        from = -1;
        return;
    }
    cEm* door = 0;
    int n = 0;
    for (u32 i = 0; i < EmMgr.getArrayNum() && !door; i++) {
        cEm* e = EmMgr.fastAt(i);
        if (e && e->isAlive() && e->id == 0x41 && e->hp > 0 && n++ == pick) {
            door = e;
        }
    }
    if (!door) {
        re4_log("health bars: RE4_TEST_HB_DOOR: no door %d", pick);
        from = -1;
        return;
    }
    FREE_EMDOOR* w = EMDOOR_WK(door);
    // the closed door's middle at the floor and its face (base_mat: closed door -> world, its x the door's width)
    const Vec mid = {w->base_mat[0][3], door->pos.y, w->base_mat[2][3]};
    f32 nx = w->base_mat[0][2], nz = w->base_mat[2][2];
    const f32 len = sqrtf(nx * nx + nz * nz);
    nx = len > 0.0f ? nx / len : 0.0f;
    nz = len > 0.0f ? nz / len : 1.0f;
    if (t == 0) {
        enemy = 0;
        for (u32 i = 0; i < EmMgr.getArrayNum(); i++) {
            cEm* e = EmMgr.fastAt(i);
            if (e && e->isAlive() && listed(e->id) && !traderForm(e) && e->hp > 0 && (!enemy || e->l_pl < enemy->l_pl)) {
                enemy = e;
            }
        }
        re4_log("health bars: RE4_TEST_HB_DOOR: door %d type %d at (%.0f %.0f %.0f), face (%.2f %.2f), enemy id 0x%02x%s",
                pick, (int) door->type, mid.x, mid.y, mid.z, nx, nz, enemy ? enemy->id : 0, open ? ", opened" : "");
    }
    Vec pos = {mid.x + nx * 3500.0f, mid.y, mid.z + nz * 3500.0f};
    Vec ang = {0.0f, atan2f(-nx, -nz), 0.0f};
    pPL->setPos(&pos);
    pPL->setAng(&ang);
    if (enemy && enemy->isAlive()) {
        enemy->setPos(mid.x - nx * 1200.0f, mid.y, mid.z - nz * 1200.0f);
        enemy->ang.y = ang.y + 3.1415927f;
    }
    if (open && t == 30) {
        ((cEmDoor*) door)->setOpen(&pPL->pos, 0, 1, 0);
    }
}

// For the PC checks: the old anchor (the highest part) away from the head by more than 25 cm, a line per enemy and
// room (the project owner's device test, 20.11.2026: the bar on a raised axe)
void logAnchor(cEm* e, const Vec& head)
{
    const Vec top = topOf(e);
    const f32 dx = top.x - head.x, dy = top.y - head.y, dz = top.z - head.z;
    if (dx * dx + dy * dy + dz * dz < 250.0f * 250.0f) {
        return;
    }
    // RE4_HB_LOG_ANCHOR=1 (PC pictures): every frame, not once per enemy and room
    static const bool all = getenv("RE4_HB_LOG_ANCHOR") != 0;
    if (all) {
        re4_log("health bars: anchor: frame %u enemy id 0x%02x slot %p: the highest part %.0f cm from the head (%.0f "
                "cm higher)", (unsigned) pG->Frame_cnt, e->id, (const void*) e,
                sqrtf(dx * dx + dy * dy + dz * dz) / 10.0f, dy / 10.0f);
        return;
    }
    static const cEm* logged[32];
    static unsigned room = ~0u, count;
    if (room != (unsigned) pG->room_id) {
        room = (unsigned) pG->room_id;
        count = 0;
        memset(logged, 0, sizeof(logged));
    }
    for (unsigned k = 0; k < count; k++) {
        if (logged[k] == e) {
            return;
        }
    }
    if (count < 32) {
        logged[count++] = e;
        re4_log("health bars: r%03x frame %u: enemy id 0x%02x: the highest part %.0f cm from the head (%.0f cm "
                "higher), the bar stays on the head", (unsigned) pG->room_id, (unsigned) pG->Frame_cnt, e->id,
                sqrtf(dx * dx + dy * dy + dz * dz) / 10.0f, dy / 10.0f);
    }
}

// For the pictures on the PC: RE4_TEST_HB_NEAR="F": at game frame F the nearest listed enemy is put 1.8 m in front
// of the player, facing him, once (then it attacks as it will)
void testNear()
{
    static long frame = getenv("RE4_TEST_HB_NEAR") ? strtol(getenv("RE4_TEST_HB_NEAR"), 0, 10) : -1;
    if (frame < 0 || (long) pG->Frame_cnt < frame || !pPL) {
        return;
    }
    frame = -1;
    cEm* pick = 0;
    for (u32 i = 0; i < EmMgr.getArrayNum(); i++) {
        cEm* e = EmMgr.fastAt(i);
        if (e && e->isAlive() && listed(e->id) && !traderForm(e) && e->hp > 0 && (!pick || e->l_pl < pick->l_pl)) {
            pick = e;
        }
    }
    if (!pick) {
        re4_log("health bars: RE4_TEST_HB_NEAR: no enemy");
        return;
    }
    pick->setPos(pPL->pos.x + 1800.0f * sinf(pPL->ang.y), pPL->pos.y, pPL->pos.z + 1800.0f * cosf(pPL->ang.y));
    pick->ang.y = pPL->ang.y + (pPL->ang.y > 0.0f ? -3.1415927f : 3.1415927f);
    re4_log("health bars: RE4_TEST_HB_NEAR: enemy id 0x%02x type %d at %p put in front of the player", pick->id,
            (int) pick->type, (const void*) pick);
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

// Once a room, 10 s after its start (for an error report): the figures of the character manager within 30 m of the
// camera, by id, and whether they get a bar (the owner's device test of 19.11.2026: the merchant and Luis must not)
static void nearby(const Vec& cam)
{
    static int room = -1;
    static unsigned at;
    if ((int) pG->room_id != room) {
        room = (int) pG->room_id;
        at = (unsigned) pG->Frame_cnt + 300;
        return;
    }
    if (!at || (unsigned) pG->Frame_cnt < at) {
        return;
    }
    at = 0;
    char line[600];
    int len = snprintf(line, sizeof(line), "health bars: r%03x, figures within 30 m:", (unsigned) pG->room_id);
    int n = 0;
    for (u32 i = 0; i < EmMgr.getArrayNum() && len < (int) sizeof(line) - 40; i++) {
        cEm* e = EmMgr.fastAt(i);
        if (!e || !e->isAlive() || e->id >= 0x40) {
            continue;
        }
        const f32 dx = e->pos.x - cam.x, dz = e->pos.z - cam.z;
        if (dx * dx + dz * dz > kFar * kFar) {
            continue;
        }
        const bool own = (cEm*) e == (cEm*) pPL || e == pSUB;
        len += snprintf(line + len, sizeof(line) - len, " 0x%02x%s%s", (unsigned) e->id,
                        (cEm*) e == (cEm*) pPL ? "(player)" : e == pSUB ? "(partner)" : traderForm(e) ? "(merchant)" : "",
                        !own && listed(e->id) && !traderForm(e) ? "+bar" : "");
        n++;
    }
    if (n) {
        re4_log("%s", line);
    }
}

// No HUD of ours now (the bars here, the app's minimap through RE4_CORE_SCREEN_NOHUD, core_bridge.cpp; the owner,
// 19.11.2026: "dass die Minimap nicht in Zwischensequenzen oder dem Introvideo angezeigt wird"): not in play (the title,
// its intro and demo movies, a room's loading, the game's options, a door scene: Rno0 other than 3), an event
// (cutscene, QTE scene), a film, a sub screen (case, map, files, the merchant), the item pick-up, the memory card (the
// typewriter), the death scene, a door scene, the game's pictures standing.
static const char* hudOffWhy()
{
    if (!pG || pG->Rno0 != 3) return "not in play (title, intro, options, door, loading)";
    if (StaFlagChk(pG, STA_EVENT)) return "an event";
    if (StaFlagChk(pG, STA_MOVIE_ON) || StaFlagChk(pG, STA_MOVIE2_ON) || re4_movie_playing()) return "a film";
    if (StaFlagChk(pG, STA_SUB_SCRN) || StaFlagChk(pG, STA_ITEM_GET)) return "a sub screen";
    if (StaFlagChk(pG, STA_CARD_ACCESS)) return "the memory card";
    if (StaFlagChk(pG, STA_DIEDEMO)) return "the death scene";
    if (SysFlagChk(pG, SYS_DOORDEMO)) return "a door scene";
    if (SysFlagChk(pG, SYS_TRANS_STOP)) return "the pictures stand";
    return 0;
}

extern "C" int re4_hud_off_now(void)
{
    const char* why = hudOffWhy();
    // its changes for an error report (the first 40)
    static const char* last = "";
    static unsigned lines;
    if (why != last && lines < 40 && pG) {
        lines++;
        re4_log("hud: %s in r%03x at frame %u", why ? why : "shown", (unsigned) pG->room_id, (unsigned) pG->Frame_cnt);
    }
    last = why;
    return why ? 1 : 0;
}

// Patch 0238 (game.cpp, after Cckpt.move): the bars of this frame
void re4_health_bars(void)
{
    if (!gOn.load(std::memory_order_relaxed) || !pPL || re4_hud_off_now()) {
        return;
    }
    testHp();
    testPlaga();
    testDoor();
    testNear();
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
    nearby(cam);
    for (u32 i = 0; i < EmMgr.getArrayNum() && f->n < kMaxBars; i++) {
        cEm* e = EmMgr.fastAt(i);
        if (!e || !e->isAlive() || !listed(e->id) || traderForm(e) || (cEm*) e == (cEm*) pPL || e == pSUB ||
            !e->isTrans()) {
            continue;
        }
        // a dying enemy (no life left) only fades its bar out
        bool want = e->hp > 0;
        if (!want && !fading(i, e)) {
            continue;
        }
        const int max = maxOf(i, e);
        if (max <= 0 || e->hp > max) {
            continue;
        }
        gCount.listed += want ? 1 : 0;
        const Vec anchor = anchorOf(e);
        logAnchor(e, anchor);
        Vec top = smoothed(i, e, anchor);
        top.y += 250.0f;
        const f32 dx = top.x - cam.x, dy = top.y - cam.y, dz = top.z - cam.z;
        const f32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < 300.0f) {
            gCount.far++;
            dropFade(i);
            continue;
        }
        if (dist > (noFade() ? kFar : kShow)) {
            gCount.far += want ? 1 : 0;
            want = false;
            if (!fading(i, e)) {
                continue;
            }
        }
        Vec scr;
        if (!GetScreenPos(&top, &scr)) {
            gCount.behind++;
            dropFade(i);
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
            dropFade(i);
            continue;
        }
        if (want && !seen(cam, top, e)) {
            gCount.wall++;
            want = false;
        }
        const f32 fade = fadeTo(i, e, want);
        if (fade <= 0.0f) {
            continue;
        }
        gCount.shown += want ? 1 : 0;
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
        // never an empty, grey bar: a living enemy keeps at least a pixel of red; one that would look empty is
        // logged (id, type, life, its max and where that came from) for an error report
        if (b.frac * b.w < 1.0f) {
            static unsigned thin;
            if (thin < 20) {
                thin++;
                if (e->hp > 0)  // (a dying enemy's bar fading out: expected)
                re4_log("health bars: r%03x frame %u: nearly empty bar: id 0x%02x type %d hp %d of %d (%s) at %.1f m",
                        (unsigned) pG->room_id, (unsigned) pG->Frame_cnt, (unsigned) e->id, (int) e->type, (int) e->hp,
                        max, e->hp_max > 0 && e->hp <= e->hp_max ? "hp_max" : "highest seen", dist / 1000.0f);
            }
            b.frac = b.w > 0.0f ? 1.0f / b.w : 0.0f;
        }
        b.plaga = plagaPhase(e) ? 1 : 0;
        b.alpha = (u8) (fade * 255.0f + 0.5f);
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
        unsigned doors = 0, pieces = 0;
        for (u32 i = 0; i < EmMgr.getArrayNum(); i++) {
            cEm* e = EmMgr.fastAt(i);
            if (e && e->isAlive() && e->id == 0x41) {
                doors++;
                for (int k = 1; k < 6; k++) {
                    cSat* sat = EMDOOR_WK(e)->pSat[k];
                    pieces += sat && sat->isAlive() ? 1 : 0;
                }
            }
        }
        re4_log("health bars: r%03x: %u doors, %u of their collision pieces on", (unsigned) pG->room_id, doors, pieces);
        // RE4_HB_LOG_POS=1 (PC checks): where the player, the doors and the listed enemies are
        static const bool where = getenv("RE4_HB_LOG_POS") != 0;
        for (u32 i = 0; where && i < EmMgr.getArrayNum(); i++) {
            cEm* e = EmMgr.fastAt(i);
            if (e && e->isAlive() && (e->id == 0x41 || listed(e->id) || (cEm*) e == (cEm*) pPL)) {
                re4_log("health bars: where: id 0x%02x hp %d at (%.0f %.0f %.0f) ang %.2f", e->id, e->hp, e->pos.x,
                        e->pos.y, e->pos.z, e->ang.y);
            }
        }
        re4_log("health bars: r%03x frame %u: %u enemies, %u bars; left out: %u far, %u behind the camera, %u off the "
                "screen, %u behind a wall (%u sight lines through a door)", (unsigned) pG->room_id,
                (unsigned) pG->Frame_cnt, gCount.listed, gCount.shown, gCount.far, gCount.behind, gCount.off,
                gCount.wall, gDoorLines);
    }
    gCount = Count();
    gDoorLines = 0;
    if (f->n > 0) {
        AddOtDirect(0x13, f, (void (*)()) draw, 0xFFFF, 0x1000, 0, 0.0f);
    }
}

}  // extern "C"

// Photo mode (runtime/photo_mode.cpp): the enemies it hides are the ones listed here
bool re4_hb_enemy_id(int id)
{
    return listed(id);
}

// ... and with the merchant's Ganado form left out
bool re4_hb_enemy(const cEm* e)
{
    return listed(e->id) && !traderForm(e);
}

// Patch 0238 declares it at block scope in game.cpp (C++ linkage there)
void re4_health_bars_call()
{
    re4_health_bars();
}
