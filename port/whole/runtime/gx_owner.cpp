// The owner tags of the app's 60 FPS (port/docs/60fps-plan.md, step S1; patch 0137): ExecOt
// (src/game/trans_ot.cpp) names the object of each ordering-table entry before its draws, so the
// renderer's in-between picture finds every draw of the picture before by its object instead of
// guessing by its look and distance. Compiled with the game's flags (re4_gamehooks_obj): a
// particle (an entry whose data lies in the effect pool, g_pEspSys->EspArray) also carries its
// effect id and life frames, so that a slot the game used anew in this picture is told apart.
#include "types.h"
#include "esp.h"
#include "espgen.h"

#include "re4_gx_fifo.h"

#include <stddef.h>

extern "C" void re4_gx_owner(void* data, void* func, int table)
{
    unsigned life = 0xFFFFFFFFu;
    const cEspSystem* sys = g_pEspSys;
    if (data && sys && sys->EspArray) {
        const size_t slot = RE4_LP64_SIZE(0x150, 0x1A8);
        const u8* p = (const u8*) data;
        if (p >= sys->EspArray && p < sys->EspArray + (size_t) sys->nEsp * slot &&
            (size_t) (p - sys->EspArray) % slot == 0) {
            const cEsp* esp = (const cEsp*) data;
            life = (unsigned) esp->m_Id << 16 | esp->m_Life_time;
        }
    }
    re4_gx_put_owner((unsigned) (size_t) data, (unsigned) (size_t) func, data ? (unsigned) table : 0xFFu, life);
}
