// The owner tags of the app's 60 FPS (port/docs/60fps-plan.md, step S1; patch 0137): ExecOt
// (src/game/trans_ot.cpp) names the object of each ordering-table entry before its draws, so the
// renderer's in-between picture finds every draw of the picture before by its object instead of
// guessing by its look and distance. Compiled with the game's flags (re4_gamehooks_obj): a
// particle (an entry whose data lies in the effect pool, g_pEspSys->EspArray) also carries its
// effect id and life frames, so that a slot the game used anew in this picture is told apart.
//
// The table word's bit 8 (RE4_GX_OWNER_FIGURE, 09.11.2026): the object is a figure (a work of the
// character manager EmMgr with an id below 0x40: Leon, Ashley, Luis, every enemy and the merchant; not the
// room's props, which are works there too: doors, barrels, torches 0x47) or a particle attached to one
// (cEsp::m_pMod, or its parent coordinate inside such a work). The app's lens flares take no light
// from these (wish of the project owner, 09.11.2026: "nicht an bewegenden Figuren"; the merchant's
// glowing eyes in r11c drew a star on his face). A table word of an older core lacks the bit.
#include "types.h"
#include "esp.h"
#include "espgen.h"
#include "em.h"

#include "obj.h"
#include "cloth.h"
#include "shadow.h"
#include "re4_gx_fifo.h"

#include <stddef.h>

namespace {

const unsigned kFigure = 0x100;  // re4_gx_frame.h RE4_GX_OWNER_FIGURE
const unsigned kObject = 0x200;  // RE4_GX_OWNER_OBJECT
const unsigned kPart = 0x400;    // RE4_GX_OWNER_PART

bool isFigure(const void* p)
{
    if (!p) {
        return false;
    }
    if (p == (const void*) pPL || p == (const void*) pSUB) {
        return true;
    }
    // a work of the character manager whose id is a figure: below 0x40 (cEmMgr::idName: the player, Ashley,
    // Luis, the Ganados (the merchant is a Ganado of type 6), animals, bosses, vehicles); from 0x40 on the room's
    // props (OBJ, DOOR, ..., TORCH 0x47, BARREL ...) keep their lights
    const u8* a = (const u8*) EmMgr.pArray;
    const size_t size = EmMgr.size;
    if (a && size && (const u8*) p >= a && (const u8*) p < a + (size_t) EmMgr.nArray * size) {
        const cEm* em = (const cEm*) (a + ((const u8*) p - a) / size * size);
        return em->id < 0x40;
    }
    return false;
}

// The real-time shadows' class of an object (RE4_GX_OWNER_OBJECT / _PART), asked only in a
// picture that casts them (`level` 1 the figures, 2 also the objects). Whole kinds, so that all of a kind cast or
// none (the room census of 15.11.2026 over the debug disc's 127 rooms, port/docs/echtzeit-schatten-studie.md):
//  - parts of a figure (with the figures): the weapons the player holds (the ObjMgr's weapon ids), the weapons and
//    shields the enemies hold (EmMgr WEP, SHIELD), the enemies' parasites and the El Gigante's weapon;
//  - objects: the EmMgr's props a figure can move, open, break or hide behind (DOOR, BOX, RACK, BARREL, TREE, ROCK,
//    SWITCH, BARRED; not WINDOW, flush in its wall, nor TORCH, a light itself, nor ITEM, HIT, MINE), the
//    room's models that turn, swing or a room script moves (the windmill's blades in r103), cloth (flags,
//    banners) and the ObjMgr's machinery (ladders, bells, the gatling, carts, the gondola, ...). Never the room's
//    static scenery (pre-lit: its shadows are painted in), pick-up items or effects.
unsigned shadowClass(const void* p, const void* func, int level)
{
    if (!p || level <= 0) {
        return 0;
    }
    const unsigned object = level >= 2 ? kObject : 0;
    if (func == (const void*) clothTrans) {
        return object;
    }
    const u8* a = (const u8*) EmMgr.pArray;
    if (a && EmMgr.size && (const u8*) p >= a && (const u8*) p < a + (size_t) EmMgr.nArray * EmMgr.size &&
        ((const u8*) p - a) % EmMgr.size == 0) {
        switch (((const cEm*) p)->id) {
        case 0x42:  // WEP: a weapon an enemy holds
        case 0x50:  // SHIELD: an enemy's shield
            return kPart;
        case 0x41:  // DOOR
        case 0x43:  // BOX
        case 0x45:  // RACK
        case 0x48:  // BARREL
        case 0x49:  // TREE
        case 0x4A:  // ROCK
        case 0x4B:  // SWITCH
        case 0x4E:  // BARRED
            return object;
        default:
            return 0;
        }
    }
    const u8* o = (const u8*) ObjMgr.pArray;
    if (o && ObjMgr.size && (const u8*) p >= o && (const u8*) p < o + (size_t) ObjMgr.nArray * ObjMgr.size &&
        ((const u8*) p - o) % ObjMgr.size == 0) {
        const cObj* ob = (const cObj*) p;
        if (ob->kindid == 2) {
            // the room's scroll models: only those that turn (SMX type 1, cObjScr::moveRotate) or swing (2,
            // moveSwingRot), and those a room script set up (SetObjSmd: type 0 with be_flag 0x20, moved by the
            // script, never pre-lit into the room); not the other types (no motion of their own; 15 a mirror)
            return ob->type == 1 || ob->type == 2 || (ob->type == 0 && (ob->be_flag & 0x20)) ? object : 0;
        }
        switch (ob->id) {
        case 11: case 16: case 17: case 26: case 33: case 34: case 35: case 36: case 37: case 39: case 40:
        case 41: case 42: case 43: case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51:
        case 52: case 54: case 58: case 60: case 61: case 63: case 66:  // the player's weapons
        case 18: case 22: case 38: case 65:  // the El Gigante's weapon, the parasites, the tentacle
            return kPart;
        case 2:   // a room model of its own (with its motion)
        case 5:   // KABOOM
        case 6:   // BOX
        case 19:  // LADDER
        case 20:  // BELL
        case 21:  // GATLING
        case 24:  // EVENT (an event's model left in the room)
        case 27:  // SPEAR
        case 28:  // FLOATISLAND
        case 29:  // CHAIN
        case 31:  // PILLAR
        case 53:  // GONDOLA
        case 55:  // ROBO
        case 57:  // YAGURA
        case 59:  // TROLLEY
        case 62:  // BULL
        case 64:  // GON
            return object;
        default:
            return 0;
        }
    }
    return 0;
}

// The game's own texture shadows of this frame ("Characters and objects"): ExecOt runs ordering
// table 8 (shadowModelRender, an entry per ShadowMng of game/shadow.cpp) before the models' tables. An object one of
// them casts (be_flag 0x10 in a room with a shadow light reaching it: r100, r206, r228, ...) gets no real-time shadow
// on top, which darkened the game's receivers twice. Self shadows (ShadowMng::self) are the model's own and count
// not.
const void* gGameCast[64];
int gGameCastN;
unsigned gGameCastFrame = ~0u;

void gameCastFrame(unsigned frame)
{
    if (gGameCastFrame != frame) {
        gGameCastFrame = frame;
        gGameCastN = 0;
    }
}

void noteGameShadow(const ShadowMng* mng, unsigned frame)
{
    gameCastFrame(frame);
    if (!mng || mng->self) {
        return;
    }
    for (int i = 0; i < mng->num && i < 8 && gGameCastN < 64; i++) {
        gGameCast[gGameCastN++] = mng->pModel[i];
    }
}

bool gameCasts(const void* p, unsigned frame)
{
    gameCastFrame(frame);
    for (int i = 0; i < gGameCastN; i++) {
        if (gGameCast[i] == p) {
            return true;
        }
    }
    return false;
}

}  // namespace

extern "C" int re4_rt_shadow_classes(void);       // runtime/rt_shadow.cpp
extern "C" unsigned re4_rt_shadow_frame_no(void);  // runtime/rt_shadow.cpp

extern "C" void re4_gx_owner(void* data, void* func, int table)
{
    unsigned life = 0xFFFFFFFFu;
    unsigned figure = isFigure(data) ? kFigure : 0;
    const cEspSystem* sys = g_pEspSys;
    if (data && sys && sys->EspArray) {
        const size_t slot = RE4_LP64_SIZE(0x150, 0x1A8);
        const u8* p = (const u8*) data;
        if (p >= sys->EspArray && p < sys->EspArray + (size_t) sys->nEsp * slot &&
            (size_t) (p - sys->EspArray) % slot == 0) {
            const cEsp* esp = (const cEsp*) data;
            life = (unsigned) esp->m_Id << 16 | esp->m_Life_time;
            if (isFigure(esp->m_pMod) || isFigure(esp->parent)) {
                figure = kFigure;
            }
        }
    }
    // the real-time shadows' classes (0 in every picture without them)
    const int classes = figure ? 0 : re4_rt_shadow_classes();
    unsigned shadow = 0;
    if (classes > 0) {
        if (classes >= 2 && func == (void*) shadowModelRender) {
            noteGameShadow((const ShadowMng*) data, re4_rt_shadow_frame_no());
        }
        shadow = shadowClass(data, func, classes);
        if (shadow == kObject && gameCasts(data, re4_rt_shadow_frame_no())) {
            shadow = 0;  // the game's own shadow already
        }
    }
    re4_gx_put_owner((unsigned) (size_t) data, (unsigned) (size_t) func,
                     data ? (unsigned) table | figure | shadow : 0xFFu, life);
}
