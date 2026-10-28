// The app's picture format "4:3 without bars" (Anbernic RG 477V report, 22.10.2026), compiled with
// the game's flags into libre4_main: patch 0105 asks re4_letterbox_open every frame (Render: the
// scissor to the letterbox rows, main loop: Draw_cinesco). Open while the app's switch is on
// (re4_core_set_letterbox, runtime/core_api.cpp) and the player plays: the game's main loop, no
// sub screen, no event (events keep their letterbox, SceEventStart sets the scissor for them).
#include "types.h"
#include "global.h"
#include "id_sys.h"

extern GLOBAL_WK* pG;

extern "C" int re4_letterbox_off(void);
extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_gx_still(int on);                      // runtime/gx_decode.cpp
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp

extern "C" int re4_letterbox_open(void)
{
    const int open = re4_letterbox_off() && pG && pG->Rno0 == 3 && !StaFlagChk(pG, STA_SUB_SCRN) &&
                     !StaFlagChk(pG, STA_EVENT);
    // each change into the error log (at most 40): whether the game drew without its bars
    static int last = -1;
    static unsigned changes;
    if (open != last && (open || last == 1) && changes < 40) {
        changes++;
        re4_log("picture: the game draws %s its letterbox (r%03x)", open ? "without" : "with", pG ? pG->room_id : 0);
    }
    last = open;
    return open;
}

// The app's 60 FPS (Retroid report, 25.10.2026: the list of the load screen at the typewriter
// flickered while it scrolled): no picture between two of a menu screen. The game's sub screens
// (inventory, map, merchant, files: STA_SUB_SCRN) and the memory card screens (load, save, the
// boot check: SYS_TYPEWRITER, card.cpp CardMainTask) draw their lists letter by letter with the
// same draws; the renderer cannot tell which letter moved where and mixed them up. The same for
// the tool menu (SELECT held, patch 0116) and its tools (DBG_TEST_MODE): lists of debug text over a
// frozen game. The result screens of the game too (tester's video 27.10.2026, the chapter end with
// 60 FPS: a "%" stood halfway between two rows): the chapter end, the game's end, the Mercenaries and
// the shooting range results set a table of the id class IDC_TITLE over a stopped game (option.cpp
// ChapterEnd/GameResult, mercenaries.cpp, r22c.cpp); their digits and signs are alike draws as well.
// And the option screen START opens over the paused game (game.cpp gameOption, Rno0 6; Retroid
// 28.10.2026 with 60 FPS: the screen flashed white for a moment when it opened).
// Every frame (re4_add_frame_hook); the GX chunks carry it (RE4_GX_CHUNK_STILL, runtime/gx_decode.cpp).
extern "C" int re4_picture_still(void)
{
    if (!pG) {
        return 0;
    }
    if (StaFlagChk(pG, STA_SUB_SCRN) || SysFlagChk(pG, SYS_TYPEWRITER) || DbgFlagChk(pG, DBG_TEST_MODE) ||
        pG->Rno0 == 6) {
        return 1;
    }
    return pG->Rno0 == 3 && IdSys.setCk(IDC_TITLE) ? 1 : 0;
}

namespace {

void stillHook(unsigned)
{
    const int still = re4_picture_still();
    // each change into the error log (at most 40)
    static int last;
    static unsigned changes;
    if (still != last && changes < 40) {
        changes++;
        re4_log("picture: %s (r%03x)", still ? "a menu screen: no pictures between" : "the menu screen closed",
                pG ? pG->room_id : 0);
    }
    last = still;
    re4_gx_still(still);
}

struct Install {
    Install() { re4_add_frame_hook(stillHook); }
} g_install;

}  // namespace
