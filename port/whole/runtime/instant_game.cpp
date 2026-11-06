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
#include "pl_npc.h"
#include "sce_at.h"
#include "motion.h"
#include "flag_rsf.h"
#include "math_sub_decl.h"
#include "re4_instant.h"
#include <dolphin/os.h>
#include <math.h>
#include <string.h>

extern int OptionOpenCheck();
extern void CRCInit();
extern u32 CRCCalc(u8*, u32);
extern "C" int re4_valid_port_ptr(const void*);
extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_story_char_stand_in(void);  // runtime/cheats.cpp

namespace {
// Ashley hidden in a box (wish of the project owner, 02.11.2026): the typewriter payload keeps no partner
// state, after a load she stood next to Leon. The slot keeps her hide behind the save data (0xFF00: outside
// the save's CRC and the card conversion, which end at 0xEAFC), and the loaded room's first frame with her
// free puts her back where she hid, at the step where her own routine settles (moveHide 0xD) with her hide
// area on its first step: the room's hide function and the whistle then run as after the hiding itself.
struct HideRecord {
    u32 magic;
    u16 room;
    u16 at;     // the hide area (SCE_AT_DATA no), 0xFFFF without one
    Vec pos;
    f32 angle;  // as at step 0xD (0xE has turned her round)
    Vec spot;   // m_VecWork0
};
const unsigned kHideAt = 0xFF00;
const u32 kHideMagic = 0x41484944;  // "AHID"
HideRecord gHide;
bool gHidePending;
unsigned gHideWait;

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

bool rsf(int no)
{
    return RsfCheck(G_ROOM_ID, no) != 0;
}

bool room0(u32 bits)
{
    return (pG->Room_flg[0] & bits) != 0;
}

// A boss fight or a scripted sequence of the room is running. Until 0.98.1 these rooms were refused as a whole
// (0.91.0 and 02.11.2026); the owner met rooms where an instant save was never possible (wish of the project
// owner, 04.11.2026: refuse only what really breaks). The instant slot keeps the room's save flags, not the
// sequence's tasks, its enemies or its partner: loaded in the middle, the room's init sees "started" and arms
// nothing (r119: no giant and no way on, r11f: no Mendez, no Ashley, no escape event), or brings back half of
// it (r224: a giant marked dead, patch 0181). Before the start flag and after the end flag the room is a room
// like any other (the game's own continue and later visits use the same states).
bool sequenceRunning()
{
    switch (pG->room_id) {
    case 0x10B: // Del Lago: the room is the fight on the boat, from its start to the chapter end
    case 0x333: // the jet ski escape: the count-down runs from the door to the ending
        return true;
    case 0x119: // El Gigante in the valley: bit 0 when it appears, bit 7 when it died
        return rsf(0) && !rsf(7);
    case 0x11C: // the besieged cabin with Luis: from the siege's start to its end event
        return ScfFlagChk(pG, SCF_R11C_BESIEGED_EVENT) && !ScfFlagChk(pG, SCF_R11C_BESIEGED_END_EVENT);
    case 0x11E: // El Gigante on the village path: bit 6 when it appears, until no giant (0x2B) lives
        return rsf(6) && SceCountEmAlive(0x2B, -1) != 0;
    case 0x11F: // Mendez: bit 0 at the meeting, until the escape event put Leon and Ashley outside
        return rsf(0) && !room0(0x40000000);
    case 0x206: // the sniper sequence covering Ashley runs from the room's start until her section (bit 0)
        return !rsf(0);
    case 0x21B: // the mine cart: from the start switch on, for the rest of the visit
        return room0(0x40000000);
    case 0x221: // the boss of the elevator hall (em 0x2C, ESL 0x8C or its first form 0x8D): from its appearance
                // (bit 4) while one of them lives in the room (a reload brings it back, as the room's init does)
        return rsf(4) && SceCountEmAlive(0x2C, -1) != 0;
    case 0x224: // the two El Gigantes: from their entrance until the exit opens (SCF 0x89, 270 frames after)
        return room0(0x04000000) && !ScfFlagChk(pG, SCF_89);
    case 0x226: // the statue chase: from the statue's awakening (bit 9) to the end of the bridge run (bit 13)
        return rsf(9) && !rsf(13);
    case 0x228: // Salazar: from the throne event (bit 0) to his death (bit 1)
        return rsf(0) && !rsf(1);
    case 0x317: // Krauser's knife fight (an event) restarted after a continue (bit 2) until it is over (bit 0)
        return rsf(2) && !rsf(0);
    case 0x31B: // U-3 in the cages: from its break-in (bit 0xC) to its end (bit 0xB)
        return rsf(0xC) && !rsf(0xB);
    case 0x31C: // Krauser's arena: from the first talk (bit 0) until the tower exploded
        return rsf(0) && !ScfFlagChk(pG, SCF_R31C_TOWER_EXPLODE);
    case 0x332: // Saddler: from his appearance (bit 0) on, the fight and the escape count-down after it
        return rsf(0);
    }
    return false;
}

// The instant slot is a typewriter-style save at the current position: enemies, Ashley and timers are
// not stored. Refuse where that would leave the player dead or stuck after loading. Free again since
// 0.98.2 (always at an idle moment, the states below still apply): r106 (the boulder and the hall's Ganados:
// their flags are saved), r11b (the lake shore: the boat is STA_PL_BOAT), r223 (the lift and its levers: saved
// flags, the moves are events); the boss rooms before and after their fight (sequenceRunning).
bool unsafePlace()
{
    // r10f (the gondola, with a merchant and a typewriter at both ends) no longer as a whole: the ride
    // itself is STA_RIDE_GONDOLA below (the project owner's report 03.11.2026: no instant save at the merchant)
    if (sequenceRunning())
        return true;
    return StaFlagChk(pG, STA_PL_BOAT) || StaFlagChk(pG, STA_PL_SWIM) || StaFlagChk(pG, STA_PL_LADDER) ||
           StaFlagChk(pG, STA_SUB_LADDER) || StaFlagChk(pG, STA_RIDE_GONDOLA) || StaFlagChk(pG, STA_PL_CATCHED) ||
           StaFlagChk(pG, STA_PL_CATCHHOLD) || StaFlagChk(pG, STA_SUB_CATCHED) || StaFlagChk(pG, STA_CRITICAL) ||
           StaFlagChk(pG, STA_PL_EM_ACTION) || StaFlagChk(pG, STA_INTO_SHOP) || StaFlagChk(pG, STA_SCOPE_CAMERA) ||
           StaFlagChk(pG, STA_BINOCULAR) || StaFlagChk(pG, STA_SUB_BULLDOZER) || StaFlagChk(pG, STA_WATER_CAMERA);
}

// The main game: no Mercenaries or Assignment Ada (the in-game load keeps their flags). Since 0.98.2 also
// the story's stand-ins (Ada, HUNK, Krauser, Wesker: patch 0195 saves Leon, the next room gives the cheat's
// character again) and Ashley's own section (pl_type 1: the save keeps her as the player and both cases).
bool mainGameLeon()
{
    return !SysFlagChk(pG, SYS_OMAKE_ADA_GAME) && !SysFlagChk(pG, SYS_OMAKE_ETC_GAME) &&
           (pG->pl_type <= 1 || re4_story_char_stand_in());
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

// Capture: Ashley settled in her hide (moveHide 0xD / 0xE) goes into the slot's hide record.
void noteHide()
{
    cSubChar* s = SUB_CHAR();
    if (!StaFlagChk(pG, STA_ASHLEY_HIDE) || !live(s) || s->id != 3 || s->r_no_0 != 0 || s->r_no_1 != 0x10 ||
        s->r_no_2 < 0xD)
        return;
    HideRecord r;
    memset(&r, 0, sizeof(r));
    r.magic = kHideMagic;
    r.room = pG->room_id;
    r.at = 0xFFFF;
    for (int no = 0; no < 0x100; ++no) {
        SCE_AT_DATA* w = SceAtPtr(no);
        if (w && w->id == SCEAT_ID_HIDE && w->hide.status != 0) {
            r.at = (u16) no;
            break;
        }
    }
    r.pos = s->pos;
    r.angle = s->r_no_2 == 0xD ? s->ang.y : LIMIT_ANGLE(s->ang.y - 3.1415927f);
    r.spot = s->m_VecWork0;
    memcpy(gData + kHideAt, &r, sizeof(r));
}

// The loaded room's first frames: Ashley back into her hide once she is free (no event, her own walk).
void restoreHide()
{
    if (!live(pG) || pG->Rno0 != 3)
        return;
    cSubChar* s = SUB_CHAR();
    if (pG->room_id != gHide.room || !live(s) || s->id != 3 || StaFlagChk(pG, STA_EVENT) || s->r_no_0 != 0 ||
        s->r_no_1 != 0) {
        if (++gHideWait > 600)
            gHidePending = false;
        return;
    }
    gHidePending = false;
    s->pos = gHide.pos;
    s->ang.y = gHide.angle;
    s->m_VecWork0 = gHide.spot;
    s->flg.off(cSubChar::F_STAY);
    s->m_Work0 = 1;
    s->m_Work1 = 0;
    s->dmg.set(0, 0x80);
    s->atari.onOba();
    s->atari.setPriority(PRI_LV3);
    s->atari.offSca();
    // her duck at its last picture (MotionSetCore clamps the start): from its first one she slid along again
    MotionSetCore(s, &s->Motion, PL_ARC_PTR(s->subArc, 0x43), 0, 7, 1, 0x3FFF);
    s->r_no_0 = 0;
    s->r_no_1 = 0x10;
    s->r_no_2 = 0xD;
    s->r_no_3 = 10;
    StaFlagOn(pG, STA_ASHLEY_HIDE);
    SCE_AT_DATA* w = gHide.at != 0xFFFF ? SceAtPtr(gHide.at) : 0;
    if (w && w->id == SCEAT_ID_HIDE)
        w->hide.status = 1;
    s->matUpdate();
    re4_log("instant: Ashley back in her hide in r%03x at (%.0f %.0f %.0f), hide area %d", (unsigned) gHide.room,
            (double) gHide.pos.x, (double) gHide.pos.y, (double) gHide.pos.z, w ? (int) gHide.at : -1);
}
}

extern "C" unsigned re4_instant_game_chapter(unsigned number)
{
    int chapter = 0, section = 0;
    getChapterSection((int) number, &chapter, &section);
    return chapter * 100 + section;
}

// Where a slot may be loaded: the game's pause-menu rules, main game as Leon (the in-game load does
// not clear the Mercenaries/Ada flags the way the title path does).
static int pauseReady()
{
    // Same event/menu restrictions as the game's pause menu, plus a live player and save image.
    if (gRestorePending || !live(pG) || StaFlagChk(pG, STA_TITLE) || pG->Rno0 != 3 || !live(pPL) || !live(pSaveData) || !live(pSys) ||
        !OptionOpenCheck() || SubScreenWk.open_flag != SS_OPEN_NULL || StaFlagChk(pG, STA_DIEDEMO) ||
        StaFlagChk(pG, STA_EVENT) || SysFlagChk(pG, SYS_CARD_ACCESS) || SysFlagChk(pG, SYS_TRANS_STOP) ||
        !pG->pl_life || pPL->r_no_0 != 0 || !mainGameLeon())
        return 0;
    return shapeFits() && isfinite(pPL->pos.x) && isfinite(pPL->pos.y) && isfinite(pPL->pos.z) &&
           isfinite(pPL->ang.y);
}

// For the PC checks (state_trace.cpp RE4_TEST_INSTANT): which of pauseReady's conditions fail, one bit each.
extern "C" unsigned re4_instant_game_why(void)
{
    if (!live(pG) || !live(pPL))
        return 0x80000000u;
    unsigned why = 0;
    why |= gRestorePending ? 1u : 0u;
    why |= StaFlagChk(pG, STA_TITLE) || pG->Rno0 != 3 ? 2u : 0u;
    why |= !OptionOpenCheck() ? 4u : 0u;
    why |= SubScreenWk.open_flag != SS_OPEN_NULL ? 8u : 0u;
    why |= StaFlagChk(pG, STA_DIEDEMO) ? 0x10u : 0u;
    why |= StaFlagChk(pG, STA_EVENT) ? 0x20u : 0u;
    why |= SysFlagChk(pG, SYS_CARD_ACCESS) || SysFlagChk(pG, SYS_TRANS_STOP) ? 0x40u : 0u;
    why |= !pG->pl_life ? 0x80u : 0u;
    why |= pPL->r_no_0 != 0 ? 0x100u : 0u;
    why |= !mainGameLeon() ? 0x200u : 0u;
    why |= !shapeFits() ? 0x400u : 0u;
    why |= unsafePlace() ? 0x800u : 0u;
    return why;
}

// Saving is stricter than loading: it needs a place that survives a reload without its enemies.
extern "C" int re4_instant_game_ready(void)
{
    return pauseReady() && !unsafePlace();
}

// In the game, but at a place where saving is refused (boat, ladder, boss fight ...): the menu says so.
extern "C" int re4_instant_game_save_blocked(void)
{
    return pauseReady() && unsafePlace();
}

extern "C" int re4_instant_game_can_load(void)
{
    return pauseReady() || titleReady();
}

// The title's main menu waits, a load would be taken from there (the benchmark, runtime/benchmark.cpp).
extern "C" int re4_instant_game_at_title(void)
{
    return titleReady() ? 1 : 0;
}

// The title's step (TITLE_WORK Rno0: 1 waiting for the card check, 2..4 the logos, 5 the main menu), -1 outside
// the title (the benchmark asked during the logos, runtime/benchmark.cpp).
extern "C" int re4_instant_title_step(void)
{
    if (!live(pG) || !StaFlagChk(pG, STA_TITLE) || !live(gTitle))
        return -1;
    return gTitle->Rno0;
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
    noteHide();
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
    memcpy(&gHide, gData + kHideAt, sizeof(gHide));
    gHidePending = gHide.magic == kHideMagic;
    gHideWait = 0;
    // The system data (options, Mercenaries records) stays as it is: it belongs to the memory card, not to
    // the point in the game. The slot only keeps it for the file check.
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
    if (gHidePending && !gRestorePending)
        restoreHide();
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
