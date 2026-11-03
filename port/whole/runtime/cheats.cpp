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
//   5..9 more weapons at their highest levels (wish of the project owner, 01.11.2026; kGifts): Handcannon,
//     Chicago Typewriter, the endless rocket launcher, the Mine Thrower with its scope, Matilda
// One button anywhere (pSys only; wish of the project owner, 29.10.2026), the app restarts the game after
// a question so the title menu shows it:
//   4 unlock the extra games: what clearing the game sets (r333.cpp, the end of the last room) for
//     Assignment Ada and The Mercenaries: EXT_HARD_MODE (the title menu's five entries, its other
//     background; Professional comes with it, the game has one flag for all), EXT_GET_OMAKE_ADA_GAME,
//     EXT_GET_OMAKE_ETC_GAME (the Mercenaries' records cleared the first time, as there). Not the
//     costume (EXT_COSTUME: the new game would ask for it).
// The story's player character (wish of the project owner, 03.11.2026; port/docs/cheat-menu-plan.md,
// "Spielfiguren"): bits 32..224 of the switches hold a player type (pl_type << 5: 2 Ada, 3 HUNK, 4
// Krauser, 5 Wesker; 0 Leon). Patch 0195 asks here: the room that loads next (gameRoomInit, before ReadPlayerData)
// gets that character instead of Leon (a change while the story runs starts the room again where the player
// stands: switchNow), in the story only (not Ashley's part, Assignment Ada or The
// Mercenaries). What is Leon's stays his: the case, the life, the money, the room items for Leon
// (SCE_AT player_type 1), the costume (his rule: the vest, chapter 2's jacket), and the save, which
// always holds Leon (pl_type 0), so a save made with a stand-in loads with Leon or the character chosen
// then. The character's own weapons (its Mercenaries ones) come from its own files, every other weapon
// of Leon's from Leon's; what its player archive lacks (an empty tag, past its table) from Leon's archive,
// which is read as well (the development half of MEM1: the radio, the rocket, the whistle); the events
// and the radio screen show Leon from that archive, as their motions are his.
#include "types.h"
#include "global.h"
#include "item.h"
#include "main.h"
#include "mercenaries.h"
#include "sscrn.h"
#include "dvd.h"
#include "read.h"
#include "scheduler.h"
#include "player.h"
#include "sce_at.h"

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_qte_auto(int on);                          // runtime/qte_touch.cpp

int re4CheatPerform(int what);
int PlSetCostume();  // pl_sub.cpp

namespace {

enum { kHealth = 1, kAmmo = 2, kOneHit = 4, kAutoQte = 8, kExtras = 16, kCharShift = 5, kChar = 7 << kCharShift,
       kCostume = 256, kAll = 31 | kChar | kCostume };

std::atomic<int> gWanted(-1);  // the app's switches, -1 before RE4_CHEATS was read
int gApplied;                  // what the game frames have set so far
std::atomic<int> gQueued(0);   // a button pressed while the game ran (the hook does it)
int gHooked;
// pl_type's names (the log, the story's player character)
const char* const kCharName[8] = {"Leon", "Ashley", "Ada", "HUNK", "Krauser", "Wesker", "?", "?"};

// The weapon buttons: the item and its highest levels (fire, speed, reload, capacity; 0: none), the
// exclusive tune-ups of the merchant's last tables included (merchant.cpp level_ext_normal, level_ext_sw500 as
// patch 0146 has it, level_ext_tompson with capacity 7 = endless as the European game sells it, level_r301's
// homing mines 4); the rocket launcher the endless one (0x6D)
struct Gift {
    int what;
    u16 id;
    const char* name;
    u8 level[4];
    u16 part;  // a part put on it (0: none): the game keeps a weapon with a part as two items, the weapon
               // and the part attached to it (cItemMgr::partsCombine; 0xAB "with scope" has no shape in the case)
};
const Gift kGifts[] = {
    {1, 0x2A, "Killer7 3/1/3/3", {3, 1, 3, 3}, 0},
    {5, 0x37, "Handcannon 7/1/3/7", {7, 1, 3, 7}, 0},
    {6, 0x34, "Chicago Typewriter 7/1/3/7", {7, 1, 3, 7}, 0},
    {7, 0x6D, "rocket launcher (endless)", {0, 0, 0, 0}, 0},
    {8, 0x36, "Mine Thrower with scope 4/1/2/3", {4, 1, 2, 3}, 0xAA},
    {9, 0x03, "Matilda 6/1/3/7", {6, 1, 3, 7}, 0},
};

const Gift* gift(int what)
{
    for (const Gift& g : kGifts) {
        if (g.what == what) {
            return &g;
        }
    }
    return 0;
}

const char* actionName(int what)
{
    if (const Gift* g = gift(what)) {
        return g->name;
    }
    switch (what) {
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
        re4_log("cheat: health %s, ammo %s, one-hit kill %s, automatic QTEs %s, extra games %s, the story's player %s",
                wanted & kHealth ? "on" : "off", wanted & kAmmo ? "on" : "off", wanted & kOneHit ? "on" : "off",
                wanted & kAutoQte ? "on" : "off", wanted & kExtras ? "kept unlocked" : "as the system file has them",
                kCharName[(wanted & kChar) >> kCharShift]);
        if ((wanted ^ gApplied) & kCostume) {
            re4_log("cheat: special costume %s", wanted & kCostume ? "on" : "off");
        }
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

void switchNow();

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
    switchNow();
    int what = gQueued.exchange(0, std::memory_order_acq_rel);
    if (what) {
        re4CheatPerform(what);
    }
}

// ---- the story's player character ------------------------------------------------------------

// Leon's player archive for a stand-in (the events, the radio screen, what the character's archive
// lacks) and Krauser's weapon buffer (read.cpp: Game.omake_wep_addr, made by gameInit only for his own
// game), in the development half of MEM1 that nothing else uses (re4_runtime.h: the core's and the
// option data's second places end at 0x82000000)
const u32 kLeonArcAddr = 0x82000000u;
const u32 kLeonArcMax = 0x118000u;  // read.cpp ReadPlayerData: Leon's limit
const u32 kWepPlaceAddr = 0x82200000u;
// Ada's own right hands for Leon's weapons (patch 0196): her handgun's, TMP's and rifle's files (their grip
// hands 0xA, 0xC, 0xD, as wep38/39/40.cpp give initWepHand), each in a place of 0x80000 after Krauser's
// buffer (WEP_DATA_MAX 0x70000)
const u32 kAdaGripAddr[3] = {0x82300000u, 0x82380000u, 0x82400000u};
const u32 kAdaGripMax = 0x80000u;
const char* const kAdaGripFile[3] = {"em/wep38.drs", "em/wep39.drs", "em/wep40.drs"};
const u8 kAdaGripNo[3] = {0xA, 0xC, 0xD};
int gAdaGrips;  // bit k: kAdaGripFile[k] read

int gLeonCostume = -1;  // the costume of the archive at kLeonArcAddr, -1 none
int gSwitched;          // the type the cheat gave the story's player (0: none)
int gCostumeGiven;      // 1 while the cheat gave the special costume (game_costume 1)

int wantedBits()
{
    int wanted = gWanted.load(std::memory_order_acquire);
    if (wanted < 0) {
        const char* v = getenv("RE4_CHEATS");
        wanted = v ? (int) strtol(v, 0, 0) & kAll : 0;
    }
    return wanted;
}

// The special costume (wish of the project owner, 03.11.2026: the switch "Special costume (Leon/Ashley)"):
// game_costume 1 while the switch is on (Leon pl10, Ashley pl05, as a stand-in Ada's second outfit pl0c; the
// events take their files for it, event.cpp), 0 once when it is switched off; -1 leaves the game's own choice
// (a new game after the ending asks for it)
int wantedCostume()
{
    if (wantedBits() & kCostume) {
        return 1;
    }
    return gCostumeGiven ? 0 : -1;
}

// 1 when the next room start changes the costume
bool costumeChange()
{
    int c = wantedCostume();
    return c >= 0 && c != pG->game_costume;
}

int wantedChar()
{
    int wanted = wantedBits();
    int type = (wanted & kChar) >> kCharShift;
    return type >= 2 && type <= 5 ? type : 0;
}

bool storyGame()
{
    return !SysFlagChk(pG, SYS_OMAKE_ADA_GAME) && !SysFlagChk(pG, SYS_OMAKE_ETC_GAME);
}

// The story's player is a stand-in for Leon: Ada, HUNK, Krauser or Wesker in the story (also from the
// debug start menu, RE4_TEST_PLAYER)
bool standIn()
{
    return pG->pl_type >= 2 && pG->pl_type <= 5 && storyGame();
}

// Leon's costume by his rule (pl_sub.cpp PlSetCostume, pl_type 0)
int leonCostume()
{
    if (pG->game_costume == 1) {
        return 3;
    }
    if (ItemMgr.num(0xFE, 0)) {
        return 2;
    }
    return ScfFlagChk(pG, SCF_R106_EVENT) ? 1 : 0;
}

// Leon's archive for that costume to kLeonArcAddr, as ReadPlayerData reads his (on the game thread,
// in a room's start: the read waits frame by frame)
void loadLeon(int costume)
{
    static const char* const names[4] = {"em/pl00.drs", "em/pl08.drs", "em/pl09.drs", "em/pl10.drs"};
    if (gLeonCostume == costume) {
        return;
    }
    gLeonCostume = -1;
    DVD_READINFO info;
    int ret;
    int req = DvdReadN(names[costume], (void*) (unsigned long) kLeonArcAddr, 0, 0, 0, 0x8100, __FILE__, __LINE__);
    while ((ret = Dvd.ReadCheck(req, &info)) != 1) {
        if (ret < 0) {
            re4_log("cheat: %s not readable: the events show no Leon", names[costume]);
            return;
        }
        TaskSleep(1);
    }
    u32 size = info.read_size[0][0] + info.read_size[0][1];
    if (size > kLeonArcMax) {
        re4_log("cheat: %s is %u bytes, more than its place (%u)", names[costume], (unsigned) size, (unsigned) kLeonArcMax);
        return;
    }
    gLeonCostume = costume;
    re4_log("cheat: Leon's archive %s (%u bytes) read to 0x%08x for the events", names[costume], (unsigned) size,
            (unsigned) kLeonArcAddr);
}

// The type re4_story_char_room gives the story's player in the next room (the current one when
// nothing changes)
int nextType()
{
    int want = wantedChar();
    int type = pG->pl_type;
    if (type == 0 && want) {
        return want;
    }
    if (type >= 2 && type <= 5 && type == gSwitched && type != want) {
        return want;
    }
    return type;
}

// Another character or costume chosen while the story runs: at once (wish of the project owner, 03.11.2026),
// as soon as the player is free (the room's game loop, no event, sub screen or movie). The room
// starts again where the player stands, as through a door into itself (SceAtExecRoomJump): its start
// gives the player data of the new type. Once per choice: with Ashley too far the door's message
// shows and the change waits for the next room.
void switchNow()
{
    static int tried = -1;
    if (!pG || !pPL || !storyGame()) {
        return;
    }
    int want = wantedBits() & (kChar | kCostume);
    if (nextType() == pG->pl_type && !costumeChange()) {
        tried = -1;
        return;
    }
    if (tried == want || pG->Rno0 != 3 || StaFlagChk(pG, STA_EVENT) || StaFlagChk(pG, STA_SUB_SCRN) ||
        StaFlagChk(pG, STA_MOVIE_ON)) {
        return;
    }
    tried = want;
    Vec pos = pPL->pos;
    Vec ang = pPL->ang;
    SceAtExecRoomJump(G_ROOM_ID, &pos, &ang, pG->Part);
    re4_log("cheat: the story's player becomes %s%s at once: r%03x starts again at (%.0f %.0f %.0f)%s",
            kCharName[nextType()], costumeChange() ? (wantedCostume() ? " in the special costume" : " in the normal costume") : "",
            (unsigned) G_ROOM_ID, pos.x, pos.y, pos.z,
            pG->Rno0 == 4 ? "" : " - not now (Ashley too far?), from the next room on");
}

// Ada's grip hands to kAdaGripAddr, once (nothing else uses that memory). In a room's start, before
// ReadWepData: the files' sounds go to the weapon's sound block, which the weapon's own read fills again.
void loadAdaGrips()
{
    for (int k = 0; k < 3; k++) {
        if (gAdaGrips & (1 << k)) {
            continue;
        }
        DVD_READINFO info;
        int ret;
        int req = DvdReadN(kAdaGripFile[k], (void*) (unsigned long) kAdaGripAddr[k], 0, 0, 0, 0x8001, __FILE__,
                           __LINE__);
        while ((ret = Dvd.ReadCheck(req, &info)) != 1) {
            if (ret < 0) {
                re4_log("cheat: %s not readable: Ada holds Leon's weapons with her open hand", kAdaGripFile[k]);
                return;
            }
            TaskSleep(1);
        }
        u32 size = info.read_size[0][0] + info.read_size[0][1];
        if (size > kAdaGripMax || (u32) (unsigned long) info.start_addr[0][0] != kAdaGripAddr[k]) {
            re4_log("cheat: %s is %u bytes at %p, not in its place", kAdaGripFile[k], (unsigned) size,
                    (void*) info.start_addr[0][0]);
            continue;
        }
        gAdaGrips |= 1 << k;
        re4_log("cheat: Ada's grip hand from %s (%u bytes) read to 0x%08x", kAdaGripFile[k], (unsigned) size,
                (unsigned) kAdaGripAddr[k]);
    }
}

// Which of Ada's grips holds Leon's weapon `no`: 1 the TMP's (his TMP), 2 the rifle's (shotguns, rifles, the
// Chicago Typewriter, the rocket launcher, the mine thrower), 0 the handgun's (handguns, magnums)
int adaGripOf(int no)
{
    switch (no) {
    case 0x0B:
    case 0x14:
        return 1;
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x15:
    case 0x18:
    case 0x1E:
    case 0x1F:
    case 0x20:
    case 0x21:
        return 2;
    }
    return 0;
}

// The weapon numbers a character has files of its own for (read.cpp wep_data_*, ss_model.cpp
// weaponFilename): the knife, its Mercenaries weapons and the grenades
bool ownWeapon(int type, int no)
{
    switch (no) {
    case 0x00:
    case 0x13:
    case 0x16:
    case 0x17:
    case 0x19:
    case 0x1F:
    case 0x20:
        return true;
    }
    switch (type) {
    case 2:
        return no == 0x01 || no == 0x0A || no == 0x0B;
    case 3:
        return no == 0x0B;
    case 4:
        return no == 0x1C;
    case 5:
        return no == 0x02 || no == 0x06 || no == 0x0A;
    }
    return true;
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
    // swaps the pesetas; a stand-in of the story plays with Leon's), only in the room's game loop and
    // outside the sub screens
    if ((pG->pl_type != 0 && !standIn()) || pG->Rno0 != 3 || StaFlagChk(pG, STA_SUB_SCRN) || StaFlagChk(pG, STA_MOVIE_ON)) {
        re4_log("cheat: %s not now (player type %d, game step %d, sub screen %d)", actionName(what), pG->pl_type,
                pG->Rno0, StaFlagChk(pG, STA_SUB_SCRN) ? 1 : 0);
        return 0;
    }
    int result = 1;
    switch (what) {
    case 1:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9: {
        const Gift* g = gift(what);
        cItem* p = ItemMgr.search(g->id);
        if (p) {
            result = 2;  // already owned: only raised to the highest levels
        } else if (!ItemMgr.get(g->id, 0) || !(p = ItemMgr.search(g->id))) {
            re4_log("cheat: %s: no free slot in the inventory", actionName(what));
            return -1;
        }
        // the part, as the case attaches it (the project owner's test, 01.11.2026: the Mine Thrower given as
        // item 0xAB "with scope" had no shape in the case, which dropped it on closing)
        if (g->part && ItemMgr.weaponId(p) == g->id) {
            if (!ItemMgr.get(g->part, 0) || !ItemMgr.newbie()) {
                re4_log("cheat: %s: no free slot for the part 0x%02x", actionName(what), g->part);
                return -1;
            }
            ItemMgr.partsCombine(p, ItemMgr.newbie());
        }
        if (g->level[0]) {
            p->setPowerLevel(g->level[0] - 1);
            p->setSpeedLevel(g->level[1] - 1);
            p->setReloadLevel(g->level[2] - 1);
            p->setBulletLevel(g->level[3] - 1);
        }
        // the magazine full (0x8000: endless, patch 0146; the shop's own store, ss_shop.cpp)
        u16 full = WeaponId2ChargeNum(g->id, p->getBulletLevel() + 1);
        if (full != 0xFF) {
            p->bullet = (u16) ((p->bullet & 0xE000) | (full & 0x1FFF));
        }
        if (ItemMgr.weapon() == p) {
            ItemMgr.arm(p);
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

// Patch 0195 (game.cpp gameRoomInit, before ReadPlayerData): the story's player as the cheat wants it.
// Leon (0) becomes the chosen character, a character the cheat gave goes back to Leon when the cheat
// is off; Ashley's part (1) stays hers. A new type reads its player data (pl_flag), drops the weapon
// module (each character has its own files) and takes its costume (PlSetCostume, patch 0195: Leon's
// rule for the stand-ins but Ada).
void re4_story_char_room(void)
{
    if (!storyGame()) {
        gSwitched = 0;
        return;
    }
    int costume = wantedCostume();
    if (costume >= 0 && costume != pG->game_costume) {
        re4_log("cheat: the %s costume from r%03x on", costume ? "special" : "normal", (unsigned) pG->room_id);
        pG->game_costume = (u8) costume;
        PlSetCostume();
    }
    gCostumeGiven = costume == 1;
    int want = wantedChar();
    int type = pG->pl_type;
    if (type == 0 && want) {
        type = want;
    } else if (type >= 2 && type <= 5 && type == gSwitched && type != want) {
        type = want;  // the cheat off (0) or another character
    }
    if (type != pG->pl_type) {
        re4_log("cheat: the story's player is %s from r%03x on (was %s)", kCharName[type], (unsigned) pG->room_id,
                kCharName[pG->pl_type]);
        pG->pl_type = (u8) type;
        gSwitched = type >= 2 ? type : 0;
        ReleaseWepData();
        PlSetCostume();
        pG->pl_flag |= 1;
    }
    if (standIn()) {
        loadLeon(leonCostume());
        if (pG->pl_type == 2) {
            loadAdaGrips();
        }
    }
}

// Leon's player archive while a stand-in plays the story (0 otherwise, or when it could not be read)
void* re4_story_char_leon(void)
{
    return standIn() && gLeonCostume >= 0 ? (void*) (unsigned long) kLeonArcAddr : 0;
}

// 1 while the stand-in takes Leon's costume rule (HUNK, Krauser, Wesker: their archives have one
// costume; Ada's pl_costume picks her own, pl0b or pl0c)
int re4_story_char_leon_costume(void)
{
    return standIn() && pG->pl_type != 2;
}

// Leon's costume by his rule while a stand-in plays the story (the vest of PlIsArmor), else -1
int re4_story_char_costume(void)
{
    return standIn() ? leonCostume() : -1;
}

// 1 when weapon number `no` is none of the stand-in's own: Leon's file is read (read.cpp ReadWepData),
// the sub screen shows the character without a weapon (ss_model.cpp)
int re4_story_char_leon_weapon(int no)
{
    return standIn() && !ownWeapon(pG->pl_type, no);
}

// Patch 0196 (pl02.cpp cPlAda::setRightHand): Ada's own grip hand for the weapon she holds when it is one
// of Leon's (his grip hand is made for his skeleton: on hers it stood beside the weapon), 0 otherwise or
// when its file could not be read
void* re4_story_char_grip(void)
{
    if (!standIn() || pG->pl_type != 2 || !re4_story_char_leon_weapon(pG->weapon_no)) {
        return 0;
    }
    int k = adaGripOf(pG->weapon_no);
    if (!(gAdaGrips & (1 << k))) {
        return 0;
    }
    const PlArc* arc = (const PlArc*) (unsigned long) kAdaGripAddr[k];
    return (void*) (size_t) (arc->ofs[kAdaGripNo[k]] + kAdaGripAddr[k]);
}

// Patch 0196 (pl02.cpp cPlAda::setLeftHand): Ada's own left hand for Leon's weapon that brings a support hand
// of its own (his handguns: a model made for his skeleton): 4 the handgun's, 1 the TMP's, 2 the rifle's
int re4_story_char_left_grip(void)
{
    static const int kLeft[3] = {4, 1, 2};
    return kLeft[adaGripOf(pG->weapon_no)];
}

// Krauser's weapon buffer when gameInit made none (he took over a story started with Leon)
void* re4_story_char_wep_place(void)
{
    return (void*) (unsigned long) kWepPlaceAddr;
}

// Entry `no` of the player archive `arc` that its table lacks (an empty tag, or past the table;
// re4_whole_prelude.h re4_arc_entry): Leon's, while a stand-in plays the story and from index 0x1B on
// (the motions and the props behind the models: the radio 0x77..0x79, the rocket 0x70..0x76, the
// whistle 0x7C); `v` is what the table gives otherwise. Leon's props 0x70..0x7C also when the
// character's table has its own there, while it holds one of Leon's weapons: Krauser's arrow and arm
// (wep28) are at 0x70..0x73, where Leon has the rocket and the mines (his mine thrower stopped in
// modelInit with Krauser's motions as a model). Each index once in the log.
void* re4_arc_stand_in(const void* arc, unsigned int no, unsigned int v)
{
    void* fallback = (void*) (size_t) (unsigned int) (v + (unsigned int) (size_t) arc);
    if (no < 0x1B || no >= 0x100 || !pG || arc != (const void*) pG->pPlayer || !re4_story_char_leon()) {
        return fallback;
    }
    const u32* ofs = pG->pPlayer->ofs;
    bool lacks = no - 4 >= ofs[0] || ofs[ofs[0] + no] == 0;
    if (!lacks && !(no - 0x70u <= 0xCu && re4_story_char_leon_weapon(pG->weapon_no))) {
        return fallback;
    }
    static u8 logged[0x100 / 8];
    if (!(logged[no >> 3] & (1 << (no & 7)))) {
        logged[no >> 3] |= (u8) (1 << (no & 7));
        re4_log("cheat: %s plays: player archive index 0x%x from Leon's (%s)", kCharName[pG->pl_type & 7], no,
                lacks ? "its own lacks it" : "a prop of his weapon");
    }
    PlArc* leon = (PlArc*) (unsigned long) kLeonArcAddr;
    if (no - 4 >= leon->ofs[0]) {
        return fallback;
    }
    return (void*) (size_t) (leon->ofs[no] + kLeonArcAddr);
}

// Patch 0195 (game.cpp cGameSave::save): the save holds Leon. `image` is the copy of the save block
// that starts at `start` (pG->save_data_start_addr).
void re4_story_char_save(u8* image, const u8* start)
{
    if (!standIn()) {
        return;
    }
    image[(const u8*) &pG->pl_type - start] = 0;
    image[(const u8*) &pG->pl_costume - start] = (u8) leonCostume();
    re4_log("cheat: saved with Leon as the player (%s plays)", kCharName[pG->pl_type]);
}

}  // extern "C"
