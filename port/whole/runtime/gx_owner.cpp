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

#include "re4_gx_fifo.h"

#include <stddef.h>

namespace {

const unsigned kFigure = 0x100;  // re4_gx_frame.h RE4_GX_OWNER_FIGURE

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

}  // namespace

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
    re4_gx_put_owner((unsigned) (size_t) data, (unsigned) (size_t) func, data ? (unsigned) table | figure : 0xFFu,
                     life);
}
