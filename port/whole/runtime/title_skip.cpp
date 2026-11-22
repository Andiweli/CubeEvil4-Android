// "Skip intro" (1.1.0, wish of the project owner, 19.11.2026: "Beim Start sollen die Introscreens gar nicht gezeigt
// werden sondern sofort das Menu"; the app menu's Display, Optional features, off by default): the app starts the core
// with RE4_SKIP_INTRO=1. Patch 0242 asks re4_title_skip_logos in titleWait (title.cpp, state 1) at the point where the
// game itself skips the logos after a soft reset (pRK->logo_skip_enable): the memory card check, the system file, the
// title's sound bank and title.dat are read by then (titleWait waits for all of them first). From there the title goes
// straight to its main menu (state 5) instead of the Nintendo logo (state 2), the health warning (state 3) and the
// Capcom, CRI and Dolby logos (state 4); the title music those logos start (SndCall 6/4 at LOGO_CALL_FRAME) is started
// at once, and the title fades in from black over 18 frames (0.6 s; the project owner, 19.11.2026: "es sollte auch
// 0,5-0,8 Sekunden eingeblendet werden"). The idle demo movies of the main menu stay as in the game (after 600 idle frames, back to the menu).
#include "types.h"
#include "global.h"

#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern GLOBAL_WK* pG;

extern "C" int re4_title_skip_logos(void)
{
    static int lines;
    const char* v = getenv("RE4_SKIP_INTRO");
    const int skip = v && strcmp(v, "1") == 0;
    if (skip && lines < 4) {
        lines++;
        re4_log("title: skip intro at frame %u: straight to the title menu (no Nintendo logo, health warning, Capcom, "
                "CRI and Dolby logos; the title music at once, a 0.6 s fade-in)", pG ? (unsigned) pG->Frame_cnt : 0u);
    }
    return skip;
}
