// Original typewriter payloads without its card screen or files. Compiled with game flags so
// the same fixed-form structures are used on ARM32 and ARM64. All calls run on the game thread.
#include "types.h"
#include "global.h"
#include "game.h"
#include "em.h"
#include "player.h"
#include "item.h"
#include "room_data.h"
#include "merchant.h"
#include "sscrn.h"
#include "main.h"
#include "main_sub.h"
#include "snd.h"
#include "sce.h"
#include "title.h"
#include "re4_instant.h"
#include <dolphin/os.h>
#include <math.h>
#include <string.h>

extern int OptionOpenCheck();
extern void CRCInit();
extern u32 CRCCalc(u8*, u32);
extern "C" int re4_valid_port_ptr(const void*);

namespace {
// Offsets of cCard::makeSaveData, not SAVE_DATA_HEAD's ABI-dependent pointer header.
u8 gData[0x12000] __attribute__((aligned(32)));
u8 gRestore[0x12000] __attribute__((aligned(32)));
SAVE_DATA_HEAD gHead;
bool gClockPaused;
bool gRestorePending;
bool gRestoreFromTitle;
TITLE_WORK* gTitle;

bool live(const void* p)
{
    const unsigned long address = (unsigned long) p;
    return p && ((address >= 0x80000000ul && address < 0x83000000ul) || re4_valid_port_ptr(p));
}

bool shapeFits()
{
    return RoomData.getSaveDataSize() >= 0x10 && RoomData.getSaveDataSize() <= 0x7EA0 &&
           SscrnDataSize() == 4 && MerchantDataSize() <= 0x324 && ItemMgr.saveDataSize() == 0x1204;
}

bool titleReady()
{
    // Only the idle main title menu, after buffers/managers and the card check are initialized.
    // STA_TITLE is checked before the pointer: titleExit frees TITLE_WORK before chaining tasks.
    return !gRestorePending && live(pG) && StaFlagChk(pG, STA_TITLE) && live(gTitle) &&
           gTitle->Rno0 == 5 && gTitle->Rno1 == 1 && live(pSaveData) && live(pSys) &&
           !SysFlagChk(pG, SYS_CARD_ACCESS) && shapeFits();
}

void header()
{
    memset(&gHead, 0, sizeof(gHead));
    gHead.base = &gHead;
    gHead.pGlobal = (GameSaveBlock*) (gData + 0x2034);
    gHead.pItm = gData + 0x572C;
    gHead.pRm = gData + 0x6930;
    gHead.pSs = (SSCRN_SAVE_DATA*) (gData + 0xE7D0);
    gHead.pMr = gData + 0xE7D4;
}

void word(unsigned at, u32 value)
{
    memcpy(gData + at, &value, 4);
}
}

extern "C" unsigned re4_instant_game_chapter(unsigned number)
{
    int chapter = 0, section = 0;
    getChapterSection((int) number, &chapter, &section);
    return chapter * 100 + section;
}

extern "C" int re4_instant_game_ready(void)
{
    // Same event/menu restrictions as the game's pause menu, plus a live player and save image.
    if (gRestorePending || !live(pG) || StaFlagChk(pG, STA_TITLE) || pG->Rno0 != 3 || !live(pPL) || !live(pSaveData) || !live(pSys) ||
        !OptionOpenCheck() || SubScreenWk.open_flag != SS_OPEN_NULL || StaFlagChk(pG, STA_DIEDEMO) ||
        StaFlagChk(pG, STA_EVENT) || SysFlagChk(pG, SYS_CARD_ACCESS) || SysFlagChk(pG, SYS_TRANS_STOP) ||
        !pG->pl_life || pPL->r_no_0 != 0)
        return 0;
    return shapeFits() && isfinite(pPL->pos.x) && isfinite(pPL->pos.y) && isfinite(pPL->pos.z) &&
           isfinite(pPL->ang.y);
}

extern "C" int re4_instant_game_can_load(void)
{
    return re4_instant_game_ready() || titleReady();
}

extern "C" void re4_instant_game_pause(int on)
{
    if (on) {
        gClockPaused = live(pG) && !StaFlagChk(pG, STA_TITLE) && pG->Rno0 == 3;
        if (gClockPaused)
            SetGameTime();
    } else if (gClockPaused) {
        InitGameTime(); // Time spent in the app menu is not play time.
        gClockPaused = false;
    }
}

extern "C" int re4_instant_game_capture(unsigned char* out)
{
    if (!re4_instant_game_ready())
        return RE4_INSTANT_BUSY;
    memset(gData, 0, sizeof(gData));
    header();
    // cGameSave changes these while capturing. Keep the live game and the normal Continue
    // checkpoint untouched, including on a subsequent I/O failure. Increment only after commit.
    const Vec pos = pG->pl_pos;
    const f32 angle = pG->pl_ang_y;
    const s32 kind = pG->SaveKind;
    ++pG->save_cnt;
    const bool captured = GameSave.save(&gHead, 1);
    --pG->save_cnt;
    pG->pl_pos = pos;
    pG->pl_ang_y = angle;
    pG->SaveKind = kind;
    if (!captured)
        return RE4_INSTANT_BAD;
    OSCalendarTime cal;
    OSTicksToCalendarTime(OSGetTime(), &cal);
    word(0x2004, 0x116);
    memcpy(gData + 0x2008, &cal, sizeof(cal));
    word(0x2030, 1);
    gData[0x203D] = gData[0x5408];
    memcpy(gData + 0x10000 + 0x1E40, pSys, sizeof(SYSTEM_SAVE_WORK));
    CRCInit();
    word(0x2000, CRCCalc(gData + 0x2004, 0x1FC));
    word(0xEAF8, CRCCalc(gData, 0xEAF8));
    word(0x11E78, CRCCalc(gData + 0x10000, 0x1E78));
    memcpy(out, gData, sizeof(gData));
    return RE4_INSTANT_OK;
}

extern "C" void re4_instant_game_saved(void)
{
    ++pG->save_cnt;
}

extern "C" int re4_instant_game_restore(const unsigned char* in)
{
    if (!re4_instant_game_can_load())
        return RE4_INSTANT_BUSY;
    memcpy(gData, in, sizeof(gData));
    header();
    u32 roomBytes;
    memcpy(&roomBytes, gData + 0x6930, 4);
    if (roomBytes != RoomData.getSaveDataSize())
        return RE4_INSTANT_BAD;
    memcpy(gRestore, gData, sizeof(gRestore));
    gRestoreFromTitle = titleReady();
    gRestorePending = true;
    // Loading can read a different weapon archive and sleep. Never run that inside PADRead's
    // pause callback. Patches 0111/0112 service it on the game/title task after menu dismissal.
    return RE4_INSTANT_OK;
}

namespace {
void installRestore()
{
    gRestorePending = false;
    memcpy(gData, gRestore, sizeof(gData));
    u32 roomBytes;
    memcpy(&roomBytes, gData + 0x6930, 4);
    // Use the existing checkpoint's allocated sections. They remain pointer-bearing only in RAM.
    GameSave.checkAddr(pSaveData);
    memcpy(pSaveData->pGlobal, gData + 0x2034, 0x36F8);
    memcpy(pSaveData->pItm, gData + 0x572C, 0x1204);
    memcpy(pSaveData->pRm, gData + 0x6930, roomBytes);
    memcpy(pSaveData->pSs, gData + 0xE7D0, SscrnDataSize());
    memcpy(pSaveData->pMr, gData + 0xE7D4, MerchantDataSize());
    memcpy(pSys, gData + 0x11E40, sizeof(SYSTEM_SAVE_WORK));
    SndAllFadeOut();
}
}

extern "C" void re4_instant_title_tick(void* work)
{
    gTitle = (TITLE_WORK*) work;
    if (!gRestorePending || !gRestoreFromTitle)
        return;
    // Mark the transition before calls that may yield, so a second menu cannot queue another load.
    gTitle->Rno0 = 7;
    gTitle->Rno1 = 0;
    installRestore();
    gRestoreFromTitle = false;
    // Use the same route as CardLoad in the title, without GameContinue's existing-room teardown.
    SysFlagOff(pG, SYS_NEW_GAME);
    SysFlagOff(pG, SYS_OMAKE_ADA_GAME);
    SysFlagOff(pG, SYS_OMAKE_ETC_GAME);
    GameLoad();
    // Original titleExit cleans up the title and chains into GameTask / gameInit.
}

extern "C" int re4_instant_game_tick(void)
{
    if (!gRestorePending || gRestoreFromTitle)
        return 0;
    pG->Stop_flg = 0xFFFFFFFF;
    installRestore();
    // The game's own option-menu load path: rebuild the room at the saved position, then
    // restore the saved play time/counters (GameContinue alone intentionally keeps current time).
    GameContinue(1);
    GameLoad();
    InitGameTime();
    return 1; // skip the old room's frame; GameContinue selected the original room-load path
}
