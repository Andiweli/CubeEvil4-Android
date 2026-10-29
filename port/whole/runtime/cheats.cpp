// The cheat menu of the app's in-game menu (port/docs/cheat-menu-plan.md; wish and decisions of the
// project owner, 27.10.2026). Compiled with game flags; the game's objects are touched on the game
// thread only (its frame hook, or while the app's menu holds it at core_api.cpp pausePoint).
//
// Three switches, kept by the app like its other settings (RE4_CHEATS at the start, then
// re4_core_set_cheats): the prototype's own debug bits, outside the save data (GLOBAL_WK::Debug_flg),
// set in every game frame while on (so they outlive the title, a soft reset and an instant load) and
// cleared once when switched off.
//   1 infinite health   DBG_NO_DEATH (LifeDownSet2 fills the life up again, deadly hits do not
//                       kill), and Leon's and the partner's life kept at the maximum; the grab and
//                       trap deaths that set the life to 0 themselves stay (decision 3)
//   2 infinite ammo     DBG_INF_BULLET (cItemMgr::trigger uses nothing), the armed weapon's magazine
//                       kept full; the display shows that number, not "100" (patch 0130, decision 6)
//   4 one-hit kill      DBG_EM_WEAK (LifeDownSet2: a hit takes the enemy's whole life)
//   8 automatic QTEs    every quick-time prompt answered without a press (runtime/qte_touch.cpp
//                       re4_qte_auto; wish of the project owner, 29.10.2026)
//  16 extra games       kept on by the app after button 4: the unlock below set again in every frame,
//                       so a system file read from the card without it (the player has not saved since)
//                       does not take it back; the next save writes it to the card (system file)
// Three buttons (decision 1: they change what the next save holds), Leon only, outside sub screens:
//   1 Killer7 with the highest levels 3/1/3/3 (item 0x2A, stored as level - 1), magazine full
//   2 attache case L (item 0x7E; the case grows when it is opened next, as after buying it)
//   3 +1 000 000 pesetas (addMoney: at most 99 999 999; decision 2)
// One button anywhere (pSys only; wish of the project owner, 29.10.2026), the app restarts the game after
// a question so the title menu shows it:
//   4 unlock the extra games: what clearing the game sets (r333.cpp, the end of the last room) for
//     Assignment Ada and The Mercenaries: EXT_HARD_MODE (the title menu's five entries, its other
//     background; Professional comes with it, the game has one flag for all), EXT_GET_OMAKE_ADA_GAME,
//     EXT_GET_OMAKE_ETC_GAME (the Mercenaries' records cleared the first time, as there). Not the
//     costume (EXT_COSTUME: the new game would ask for it).
#include "types.h"
#include "global.h"
#include "item.h"
#include "main.h"
#include "mercenaries.h"
#include "sscrn.h"

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_qte_auto(int on);                          // runtime/qte_touch.cpp

int re4CheatPerform(int what);

namespace {

enum { kHealth = 1, kAmmo = 2, kOneHit = 4, kAutoQte = 8, kExtras = 16, kAll = 31 };

std::atomic<int> gWanted(-1);  // the app's switches, -1 before RE4_CHEATS was read
int gApplied;                  // what the game frames have set so far
std::atomic<int> gQueued(0);   // a button pressed while the game ran (the hook does it)
int gHooked;

const char* actionName(int what)
{
    switch (what) {
    case 1:
        return "Killer7 3/1/3/3";
    case 2:
        return "attache case L";
    case 3:
        return "+1000000 pesetas";
    case 4:
        return "extra games";
    }
    return "?";
}

bool extrasUnlocked()
{
    return ExtFlagChk(pSys, EXT_HARD_MODE) && ExtFlagChk(pSys, EXT_GET_OMAKE_ADA_GAME) &&
           ExtFlagChk(pSys, EXT_GET_OMAKE_ETC_GAME);
}

// What clearing the game sets for the extra games (r333.cpp), without the costume.
void unlockExtras(const char* why)
{
    ExtFlagOn(pSys, EXT_HARD_MODE);
    ExtFlagOn(pSys, EXT_GET_OMAKE_ADA_GAME);
    if (!ExtFlagChk(pSys, EXT_GET_OMAKE_ETC_GAME)) {
        MercSysSaveWork save;
        int i;

        ExtFlagOn(pSys, EXT_GET_OMAKE_ETC_GAME);
        for (i = 0; i < 4; i++) {
            pSys->MercSysRoom[i] = 0;
        }
        for (i = 0; i < 2; i++) {
            pSys->MercSysRank[i] = 0;
        }
        MercSysGetSaveWork(&save);
        for (i = 0; i < 4; i++) {
            save.stage[i].score = 0;
            save.stage[i].mode = 0;
        }
        MercSysSetSaveWork(&save);
    }
    re4_log("cheat: extra games unlocked (%s): Assignment Ada, The Mercenaries, Professional; extra flags 0x%08x",
            why, (unsigned) pSys->Extra_flg);
}

// The armed weapon's magazine full (a gun: its slot's loaded rounds).
void fillMagazine()
{
    cItem* p = ItemMgr.weapon();
    if (!p || itemType(p->id) != 1) {
        return;
    }
    u16 full = WeaponId2ChargeNum(p->id, p->getBulletLevel() + 1);
    // 0x8000: endless ammo (patch 0146), nothing to fill
    if (full != 0xFF && full != 0x8000 && (p->bullet & 0x1FFF) < full) {
        p->bullet = (u16) ((p->bullet & 0xE000) | full);
    }
}

void apply(int wanted)
{
    struct {
        int bit, flag;
    } const bits[] = {{kHealth, DBG_NO_DEATH}, {kAmmo, DBG_INF_BULLET}, {kOneHit, DBG_EM_WEAK}};
    for (const auto& b : bits) {
        if (wanted & b.bit) {
            DbgFlagOn(pG, b.flag);
        } else if (gApplied & b.bit) {
            DbgFlagOff(pG, b.flag);  // once: the debug discs' debug menu may switch them itself
        }
    }
    if (wanted != gApplied) {
        re4_log("cheat: health %s, ammo %s, one-hit kill %s, automatic QTEs %s, extra games %s",
                wanted & kHealth ? "on" : "off", wanted & kAmmo ? "on" : "off", wanted & kOneHit ? "on" : "off",
                wanted & kAutoQte ? "on" : "off", wanted & kExtras ? "kept unlocked" : "as the system file has them");
        if ((wanted ^ gApplied) & kAutoQte) {
            re4_qte_auto(wanted & kAutoQte);
        }
    }
    gApplied = wanted;
    if ((wanted & kExtras) && !extrasUnlocked()) {
        unlockExtras("kept by the app");
    }
    if (wanted & kHealth) {
        // a life of 0 is a death scene already running: left alone
        if (pG->pl_life > 0 && pG->pl_life < pG->pl_life_max) {
            pG->pl_life = pG->pl_life_max;
        }
        if (pG->ashley_life > 0 && pG->ashley_life < pG->ashley_life_max) {
            pG->ashley_life = pG->ashley_life_max;
        }
    }
    if (wanted & kAmmo) {
        fillMagazine();
    }
}

void hook(unsigned)
{
    int wanted = gWanted.load(std::memory_order_acquire);
    if (wanted < 0) {
        const char* v = getenv("RE4_CHEATS");
        wanted = v ? (int) strtol(v, 0, 0) & kAll : 0;
        int expected = -1;
        gWanted.compare_exchange_strong(expected, wanted);
        wanted = gWanted.load(std::memory_order_acquire);
    }
    apply(wanted);
    int what = gQueued.exchange(0, std::memory_order_acq_rel);
    if (what) {
        re4CheatPerform(what);
    }
}

}  // namespace

int re4CheatPerform(int what)
{
    // the extra games: the system flags only, at any time (the title, Ada's and HUNK's games too)
    if (what == 4) {
        if (extrasUnlocked()) {
            re4_log("cheat: %s already unlocked (extra flags 0x%08x)", actionName(what), (unsigned) pSys->Extra_flg);
            return 2;
        }
        unlockExtras("the cheat button");
        return 1;
    }
    // only Leon has the case and his own money (Ashley, Ada, HUNK ...: other inventories, PlSelect
    // swaps the pesetas), only in the room's game loop and outside the sub screens
    if (pG->pl_type != 0 || pG->Rno0 != 3 || StaFlagChk(pG, STA_SUB_SCRN) || StaFlagChk(pG, STA_MOVIE_ON)) {
        re4_log("cheat: %s not now (player type %d, game step %d, sub screen %d)", actionName(what), pG->pl_type,
                pG->Rno0, StaFlagChk(pG, STA_SUB_SCRN) ? 1 : 0);
        return 0;
    }
    int result = 1;
    switch (what) {
    case 1: {
        cItem* p = ItemMgr.search(0x2A);
        if (p) {
            result = 2;  // already owned: only raised to the highest levels
        } else if (!ItemMgr.get(0x2A, 0) || !(p = ItemMgr.search(0x2A))) {
            re4_log("cheat: %s: no free slot in the inventory", actionName(what));
            return -1;
        }
        // highest levels of 0x2A in wep_level_info: fire 3, speed 1, reload 3, capacity 3 (stored - 1)
        p->setPowerLevel(WeaponId2MaxLevel(0x2A, 0) - 1);
        p->setSpeedLevel(WeaponId2MaxLevel(0x2A, 1) - 1);
        p->setReloadLevel(WeaponId2MaxLevel(0x2A, 2) - 1);
        p->setBulletLevel(WeaponId2MaxLevel(0x2A, 3) - 1);
        u16 full = WeaponId2ChargeNum(0x2A, p->getBulletLevel() + 1);
        if (full != 0xFF) {
            p->bullet = (u16) ((p->bullet & 0xE000) | full);
        }
        re4_log("cheat: %s %s (levels 0x%04x, %d rounds)", actionName(what), result == 2 ? "raised" : "added", p->lv,
                p->bullet & 0x1FFF);
        break;
    }
    case 2:
        if (ItemMgr.search(0x7E) || ItemMgr.search(0x7F)) {
            re4_log("cheat: %s: the case is L or XL already", actionName(what));
            return 2;
        }
        ItemMgr.dump(0x7C);
        ItemMgr.dump(0x7D);
        if (!ItemMgr.get(0x7E, 0)) {
            re4_log("cheat: %s: no free slot in the inventory", actionName(what));
            return -1;
        }
        // as after buying it (ss_shop.cpp buyItem): the case grows when it is opened next
        SubScreenWk.board_next = 2;
        re4_log("cheat: %s added (case size %d, grows to 2 when opened)", actionName(what), SubScreenWk.board_size);
        break;
    case 3:
        addMoney(1000000);
        re4_log("cheat: %s: %u pesetas", actionName(what), (unsigned) pG->peseta);
        break;
    default:
        return 0;
    }
    return result;
}

extern "C" {

void re4_cheats_set(int bits)
{
    gWanted.store(bits & kAll, std::memory_order_release);
}

int re4_cheats_wanted(void)
{
    int w = gWanted.load(std::memory_order_acquire);
    return w < 0 ? 0 : w;
}

// A button while the game runs: done by the next game frame (returns 3 = queued).
int re4_cheat_queue(int what)
{
    gQueued.store(what, std::memory_order_release);
    return 3;
}

// While the app's menu holds the game thread (core_api.cpp pausePoint): done right there.
int re4_cheat_perform(int what)
{
    return re4CheatPerform(what);
}

// runtime/state_trace.cpp configure (static init and the core's start): the frame hook (the
// switches in every frame, queued buttons)
void re4_cheats_install(void)
{
    if (!gHooked) {
        gHooked = 1;
        re4_add_frame_hook(hook);
    }
}

}  // extern "C"
