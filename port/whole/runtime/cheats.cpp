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
// 512 invisibility      (wish of the project owner, 05.11.2026) enemies take no notice of the player: patch
//                       0204 makes the player as far away as can be to every enemy (cEm id 0x10..0x3F, em.cpp
//                       emMove: nothing that asks the distance finds, approaches or attacks him), and the
//                       Ganados (em10) find him by nothing else either (sight, steps, shots, the bell, a hit, a
//                       neighbour's alert) and forget him; DBG_EM_NO_ATK, the debug bit of the Ganados'
//                       attacks, is set with it. Bosses and room scripts that attack by themselves still can.
// 1024 random items     (wish of the project owner, 07.11.2026, issue #161) the items lying in the story's rooms
//                       dealt anew, the seed in bits 11..30 (the app's; patch 0218, re4_item_random below)
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
// Their own moves on the Ganados (04.11.2026, patch 0197; 05.11.2026 all of them): the game picks them by
// pl_type (em10.cpp: HUNK's neck break, Krauser's double kick and knee kick, Wesker's palm strike and heel, Ada's
// kicks and grabs) from the Ganado archive, whose copies of The Mercenaries are made per character
// (EmFileTbl_Ada/_Wesker/_Klauser, em46..em70); the story's Ganados (em11..em20) hold Leon's moves and lack
// the melee block 0x2B4..0x2BD (the empty entries made a motion of the REL behind the table: the animation
// stood still, the Ganado slid about). The character's entries are read from its Mercenaries file once
// (loadStandInSet) and stand in for the story's. The X button commands Ashley (wait / follow) as with Leon.
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
#include "item_model.h"
#include "em.h"
#include "pl_sub.h"
#include "pl_npc.h"
#include <dolphin/dvd.h>

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_formats_part_loaded(const char* name, int nest, int index, const void* part, unsigned addr);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_qte_auto(int on);                          // runtime/qte_touch.cpp

int re4CheatPerform(int what);
int PlSetCostume();  // pl_sub.cpp

namespace {

enum { kHealth = 1, kAmmo = 2, kOneHit = 4, kAutoQte = 8, kExtras = 16, kCharShift = 5, kChar = 7 << kCharShift,
       kCostume = 256, kInvisible = 512, kItemRandom = 1024, kSeedShift = 11, kSeedMask = 0xFFFFF << kSeedShift,
       kAll = 31 | kChar | kCostume | kInvisible | kItemRandom | kSeedMask };

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

void repairPartnerLife();

void apply(int wanted)
{
    struct {
        int bit, flag;
    } const bits[] = {{kHealth, DBG_NO_DEATH}, {kAmmo, DBG_INF_BULLET}, {kOneHit, DBG_EM_WEAK},
                      {kInvisible, DBG_EM_NO_ATK}};
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
        if ((wanted ^ gApplied) & kInvisible) {
            re4_log("cheat: invisibility %s", wanted & kInvisible ? "on" : "off");
        }
        if ((wanted ^ gApplied) & (kItemRandom | kSeedMask)) {
            if (wanted & kItemRandom) {
                re4_log("cheat: random items on (seed %u), from the next room on",
                        (unsigned) ((u32) (wanted & kSeedMask) >> kSeedShift));
            } else {
                re4_log("cheat: random items off, from the next room on");
            }
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
        // the partner only while Ashley is the partner: in her own part (pl_type 1) PlSelect (pl_sub.cpp) keeps
        // Leon's maximum in ashley_life_max, and filling up to it left Ashley above her own maximum afterwards
        // (tester picture 05.11.2026: gaps in her life meter, 0.98.4)
        if (pG->pl_type != 1 && pG->ashley_life > 0 && pG->ashley_life < pG->ashley_life_max) {
            pG->ashley_life = pG->ashley_life_max;
        }
    }
    repairPartnerLife();
    if (wanted & kAmmo) {
        fillMagazine();
    }
}

// Ashley's life above her maximum while she is the partner: the game never does that (healing stops at the
// maximum), but infinite health before 0.98.4 did in Ashley's own part (see apply), and saves keep it. Her
// life meter (cockpit.cpp LifeMeter::move: two covers turned by life / 200, made for at most 1200) then turned
// a cover too far and showed gaps. Put back to her maximum, once per case, logged.
void repairPartnerLife()
{
    static int logged;
    const s16 life = (s16) pG->ashley_life, max = (s16) pG->ashley_life_max;
    if (pG->pl_type == 1 || max <= 0 || life <= max) {
        return;
    }
    pG->ashley_life = max;
    if (logged < 5) {
        logged++;
        re4_log("cheat: Ashley's life %d above her maximum %d (infinite health in her own part before 0.98.4): "
                "set to %d", (int) life, (int) max, (int) max);
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

// The stand-ins' own entries of the Ganado archives (patch 0197, 05.11.2026 for all of them; tester reports
// 20261105-010526 and -162033: Krauser's knee kick played Wesker's palm strike, his kick no double kick and it
// knocked nobody down). The game picks the Ganado file by the player (read.cpp EmFileTbl_Ada, _Wesker for HUNK
// and Wesker, _Klauser): for The Mercenaries each character has its own copies of the Ganado kinds 0x16, 0x1C,
// 0x1D, 0x1F and 0x20 (em46/4c/4d/4f/50 Ada, em56..60 HUNK and Wesker, em66..70 Krauser). Compared entry by
// entry with the story's (em16, em1c, em1d, em1f, em20; debug, EU and US discs, disc 1 and 2 alike) they differ
// only in the player's moves on a Ganado and the Ganado's answers to them: Ada 0xFB, 0x100, 0x101, 0x103,
// 0x17A/0x17B, 0x1A7, 0x28F, 0x290, 0x292, 0x297..0x29E; Krauser 0x101, 0x103, 0x17A/0x17B, 0x1A7, 0x297..0x29E
// (his double kick 0x29D/0x29E), 0x2B4..0x2B8 (his knee kick); HUNK and Wesker 0x2B4..0x2BB (neck break, palm
// strike, heel kick), which the story's files lack altogether (they end at 0x2B3, em20 at its chainsaw walk
// 0x2BC/0x2BD). The kind 0x20 file of a character holds all of them. They are read from it once per character
// (loadStandInSet) to an archive of their own after Ada's grips and stand in for the story's entries (an
// empty tag: the entry whose data it shares). Left out: Ada's em4d entry 0x18E, the stun rod Ganado's weapon
// model, no motion. Leon and the stand-ins' own Mercenaries are not touched (only while a stand-in plays the
// story).
const u32 kSetAddr = 0x82480000u;
const u32 kSetMax = 0x60000u;  // Ada's set: 0x41700 bytes
const unsigned kSetLo = 0xFB;
const unsigned kSetHi = 0x2BD;
const unsigned kMeleeFirst = 0x2B4;  // the melee block, missing in the story's files
u32 gSetEntry[kSetHi - kSetLo + 1];  // the character's entry (its address), 0: the story's own
int gSetType;                        // the pl_type whose set is read, -pl_type: not readable

struct StandInSet {
    const char* file;
    u16 idx[48];  // ends with 0
};
// pl_type 2..5: Ada, HUNK, Krauser, Wesker
const StandInSet kStandInSet[4] = {
    {"em/em50.drs", {0xFB,  0x100, 0x101, 0x103, 0x17A, 0x17B, 0x1A7, 0x28F, 0x290, 0x292, 0x297, 0x298, 0x299,
                     0x29A, 0x29B, 0x29C, 0x29D, 0x29E, 0x2B4, 0x2B5, 0x2B6, 0x2B7, 0x2B8, 0x2B9, 0x2BA, 0x2BB,
                     0x2BC, 0x2BD, 0}},
    {"em/em60.drs", {0x2B4, 0x2B5, 0x2B6, 0x2B7, 0x2B8, 0x2B9, 0x2BA, 0x2BB, 0x2BC, 0x2BD, 0}},
    {"em/em70.drs", {0x101, 0x103, 0x17A, 0x17B, 0x1A7, 0x297, 0x298, 0x299, 0x29A, 0x29B, 0x29C, 0x29D, 0x29E,
                     0x2B4, 0x2B5, 0x2B6, 0x2B7, 0x2B8, 0x2B9, 0x2BA, 0x2BB, 0x2BC, 0x2BD, 0}},
    {"em/em60.drs", {0x2B4, 0x2B5, 0x2B6, 0x2B7, 0x2B8, 0x2B9, 0x2BA, 0x2BB, 0x2BC, 0x2BD, 0}},
};

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

// Patch 0211: 1 for the characters whose player archive has no rocket where the Ganados take theirs (index 0x70
// model, 0x71 texture; em10.cpp em10_R1_ShotRocket): Ada's (pl0b, pl0c) and Wesker's (pl0d) entries there are
// empty and share a small model of Ada's / Wesker's further on, Krauser's (pl0a) are his arrow. Leon's archives
// (pl00, pl08..pl10) and HUNK's (pl06) have the rocket; Ashley's (pl01, pl05) have motions there, and no Ganado
// with a rocket meets her. The same in the debug build's and every release's files (US, EU, JP).
bool lacksRocket(int type)
{
    return type == 2 || type == 4 || type == 5;
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
    re4_log("cheat: Leon's archive %s (%u bytes) read to 0x%08x for the events and the Ganados' rocket", names[costume],
            (unsigned) size, (unsigned) kLeonArcAddr);
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

u32 be32At(const u8* p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

void putBe32At(u8* p, u32 v)
{
    p[0] = (u8) (v >> 24);
    p[1] = (u8) (v >> 16);
    p[2] = (u8) (v >> 8);
    p[3] = (u8) v;
}

// The stand-in's entries of the Ganado archives (see kSetAddr), once per character: the file's first part (the
// archive: its file header names where), its table, then the data of each listed entry (up to the next entry's
// offset or the REL), each piece once (entries that share an offset share it). They go to kSetAddr as an archive
// of their own, big-endian as on the disc with the tag of the entry that owns the data, and are converted as a
// read archive is (re4_formats_part_loaded). Read with DVDRead: none of the file's sounds reaches a sound block.
// On the GameCube the pieces would come in 32-byte steps; the archive keeps every offset at that alignment.
void loadStandInSet(int type)
{
    if (type < 2 || type > 5 || gSetType == type || gSetType == -type) {
        return;
    }
    memset(gSetEntry, 0, sizeof(gSetEntry));
    gSetType = -type;
    const StandInSet& set = kStandInSet[type - 2];
    DVDFileInfo fi;
    if (!DVDOpen((char*) set.file, &fi)) {
        re4_log("cheat: %s not readable: %s's own moves on the Ganados are missing in the story", set.file,
                kCharName[type]);
        return;
    }
    u8* const base = (u8*) (unsigned long) kSetAddr;
    u8* const scratch = base + kSetMax;  // the file header and the table, read first
    u32 partOfs = 0, partSize = 0, count = 0, rel = 0;
    if (DVDReadPrio(&fi, scratch, 0x400, 0, 2) == 0x400) {
        for (u32 at = 0x20; at + 0x20 <= 0x400; at += 0x20) {
            u32 kind = be32At(scratch + at);
            if (kind == 0xFFFFFFFFu) {
                break;
            }
            if (kind == TRANS_MRAM) {
                partSize = be32At(scratch + at + 4);
                partOfs = be32At(scratch + at + 0xC);
                break;
            }
        }
    }
    if (partSize >= 0x10 && DVDReadPrio(&fi, scratch, 0x20, (s32) partOfs, 2) == 0x20) {
        count = be32At(scratch);
        rel = be32At(scratch + 4);
    }
    const u32 table = (0x10 + count * 8 + 0x1F) & ~0x1Fu;
    bool ok = count == 0x2BA && rel && rel <= partSize &&
              DVDReadPrio(&fi, scratch, (s32) table, (s32) partOfs, 2) == (s32) table;
    // the pieces: offset, end and the owning entry (the last one with that offset), in the file's order
    enum { kPieces = 48 };
    u32 pOfs[kPieces], pEnd[kPieces], pOwner[kPieces], pAt[kPieces];
    unsigned n = 0;
    for (unsigned k = 0; ok && set.idx[k]; k++) {
        unsigned no = set.idx[k];
        u32 o = be32At(scratch + no * 4);
        if (o >= rel) {
            continue;  // none in this file
        }
        bool have = false;
        for (unsigned j = 0; j < n; j++) {
            have = have || pOfs[j] == o;
        }
        if (have) {
            continue;
        }
        u32 end = rel;
        unsigned owner = no;
        for (u32 i = 4; i < count + 4; i++) {
            u32 oi = be32At(scratch + i * 4);
            if (oi > o && oi < end) {
                end = oi;
            }
            if (oi == o && i > owner) {
                owner = i;
            }
        }
        if (n == kPieces || (o & 0x1F) || end <= o) {
            ok = false;
            break;
        }
        unsigned at = n;
        while (at > 0 && pOfs[at - 1] > o) {  // sorted by offset: the converter sizes by the next one
            pOfs[at] = pOfs[at - 1];
            pEnd[at] = pEnd[at - 1];
            pOwner[at] = pOwner[at - 1];
            at--;
        }
        pOfs[at] = o;
        pEnd[at] = end;
        pOwner[at] = owner;
        n++;
    }
    const u32 head = (0x10 + n * 8 + 0x1F) & ~0x1Fu;
    u32 total = head;
    for (unsigned j = 0; ok && j < n; j++) {
        pAt[j] = total;
        total += (pEnd[j] - pOfs[j] + 0x1F) & ~0x1Fu;
        ok = total <= kSetMax;
    }
    if (ok && n) {
        u8 tbl[0x10 + kPieces * 8];
        memset(tbl, 0, sizeof(tbl));
        putBe32At(tbl, n);
        for (unsigned j = 0; j < n; j++) {
            putBe32At(tbl + 0x10 + j * 4, pAt[j]);
            memcpy(tbl + 0x10 + n * 4 + j * 4, scratch + 0x10 + count * 4 + (pOwner[j] - 4) * 4, 4);
        }
        for (unsigned j = 0; ok && j < n; j++) {
            u32 len = (pEnd[j] - pOfs[j] + 0x1F) & ~0x1Fu;
            ok = DVDReadPrio(&fi, base + pAt[j], (s32) len, (s32) (partOfs + pOfs[j]), 2) == (s32) len;
        }
        // the table last: the reads above used the scratch place's table, which lies after the set
        memset(base, 0, head);
        memcpy(base, tbl, 0x10 + n * 8);
    }
    if (ok && n) {
        for (unsigned k = 0; set.idx[k]; k++) {
            u32 o = be32At(scratch + set.idx[k] * 4);
            for (unsigned j = 0; j < n; j++) {
                if (pOfs[j] == o) {
                    gSetEntry[set.idx[k] - kSetLo] = kSetAddr + pAt[j];
                }
            }
        }
        DvdHeader h;
        memset(&h, 0, sizeof(h));
        h.type = TRANS_MRAM;
        h.size = total;
        h.dest = kSetAddr;
        re4_formats_part_loaded(set.file, 0, 0, &h, kSetAddr);
    }
    DVDClose(&fi);
    if (!ok || !n) {
        memset(gSetEntry, 0, sizeof(gSetEntry));
        re4_log("cheat: %s has not %s's moves on the Ganados (%u entries, REL at 0x%x)", set.file, kCharName[type],
                (unsigned) count, (unsigned) rel);
        return;
    }
    gSetType = type;
    re4_log("cheat: %s's own moves on the Ganados: %u entries 0x%x..0x%x from %s (%u bytes) read to 0x%08x",
            kCharName[type], (unsigned) n, (unsigned) set.idx[0], (unsigned) kSetHi, set.file, (unsigned) total,
            (unsigned) kSetAddr);
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
// weaponFilename): the knife, its Mercenaries weapons and the grenades. HUNK's TMP (wep29, Leon's wep11 code)
// only without the stock: its models for the weapon types 1..3 (0x7..0x9, the TMP with the stock and its
// variants) are a placeholder of a few points, the TMP vanished from his hands (tester report 20261105-010526);
// with the stock it is Leon's TMP, as each of his weapons the character has none of its own for. `wtype` is
// the weapon type (pG->weapon_type of the weapon held).
bool ownWeapon(int type, int no, int wtype)
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
        return no == 0x0B && wtype == 0;
    case 4:
        return no == 0x1C;
    case 5:
        return no == 0x02 || no == 0x06 || no == 0x0A;
    }
    return true;
}

// ---- random items (wish of the project owner, 07.11.2026, GitHub issue #161) ----------------------------------
// The switch kItemRandom with its seed (bits 11..30, kept by the app; a new one each time the switch goes on)
// deals the items lying free in the story's rooms anew: patch 0218 calls re4_item_random at the start of
// sceAtSetItem, whose first call in a room comes after the room's item models (ITM) are loaded and before any of
// its item areas made a model. That first call walks all ITA records of the room once and marks them
// (SCE_AT_UNIT pad2[0], zero on every disc: ITA records of the debug, European, US and Japanese discs read on
// 07.11.2026), so the room is dealt once per load and a switch turned on later leaves it as it is.
//
// What may change (port/docs/whole-program-status.md, version 0.99.2, item 7): records lying free (waiting_type 0:
// no enemy, barrel or box holds them, those the game fills itself), with ctrl_flag 0 (not handed to an enemy, no
// hanging or falling item, none the game's own random drop wrote back from the save), an item of the pool: Leon's
// ammunition, the grenades, recovery (herbs, the spray, eggs, fish), pesetas, treasures (types 5 and 12 but the
// three a room script handles by its id: 0x89, 0x8A in r103/r105, 0xA2). Never keys, files, maps, weapons, parts,
// the Mercenaries' items, the game's random drops (ids 0x1000..), anything in Ashley's part (she cannot use ammo)
// or in Assignment Ada and The Mercenaries. The new item is drawn from the pool's group by a fixed share (ammo 36,
// recovery 26, pesetas 14, grenades 12, treasures 12 of 100) and only among the items whose model the room
// already has (ItemGetBinTplAddr: the ITM block holds the models of the room's items and of what its enemies may
// drop); a group with none there gives way to the next. The draw is a hash of the seed, the room, the record and
// its item: the same room deals the same items after a reload, a death or a room change. The item's flags stay,
// so what was taken stays taken and a save plays with the switch on or off alike.
const u8 kMark = 0xA5;  // pad2[0] of a record the deal has seen
const u8 kAmmoIds[] = {0x04, 0x18, 0x07, 0x20, 0x00};
const u8 kGrenadeIds[] = {0x01, 0x02, 0x0E};
const u8 kHealIds[] = {0x06, 0x19, 0x1C, 0x05, 0x08, 0x09, 0x0A, 0x12, 0x13, 0x14, 0x15, 0x16, 0xA8, 0x95, 0x97};
const u8 kMoneyIds[] = {0x78, 0x79, 0x71};
enum { kGroupAmmo, kGroupHeal, kGroupMoney, kGroupGrenade, kGroupTreasure, kGroups };
const u8 kShare[kGroups] = {36, 26, 14, 12, 12};
const char* const kGroupName[kGroups] = {"ammo", "recovery", "pesetas", "grenade", "treasure"};

bool treasureId(unsigned id)
{
    if (id == 0x89 || id == 0x8A || id == 0xA2) {
        return false;
    }
    const u8 t = itemType((ITEM_ID) id);
    return t == 5 || t == 12;
}

template <size_t N> bool inList(const u8 (&list)[N], unsigned id)
{
    for (u8 v : list) {
        if (v == id) {
            return true;
        }
    }
    return false;
}

// The pool's group of item `id`, -1 when it is not in the pool.
int itemGroup(unsigned id)
{
    if (inList(kAmmoIds, id)) {
        return kGroupAmmo;
    }
    if (inList(kHealIds, id)) {
        return kGroupHeal;
    }
    if (inList(kMoneyIds, id)) {
        return kGroupMoney;
    }
    if (inList(kGrenadeIds, id)) {
        return kGroupGrenade;
    }
    return id < 0x100 && treasureId(id) ? kGroupTreasure : -1;
}

bool roomModel(unsigned id)
{
    void* bin;
    void* tpl;
    return ItemGetBinTplAddr((u8) id, &bin, &tpl) != 0;
}

// The items of group `g` whose model the room has, in a fixed order; their number.
int groupCandidates(int g, u8* out)
{
    int n = 0;
    auto take = [&](unsigned id) {
        if (roomModel(id)) {
            out[n++] = (u8) id;
        }
    };
    switch (g) {
    case kGroupAmmo:
        for (u8 id : kAmmoIds) take(id);
        break;
    case kGroupHeal:
        for (u8 id : kHealIds) take(id);
        break;
    case kGroupMoney:
        for (u8 id : kMoneyIds) take(id);
        break;
    case kGroupGrenade:
        for (u8 id : kGrenadeIds) take(id);
        break;
    default:
        for (unsigned id = 0; id < 0x100; id++) {
            if (treasureId(id)) take(id);
        }
        break;
    }
    return n;
}

// A count that suits item `id` (0: its default count, as most records of the discs have it): the ammunition as
// the rooms hold it, pesetas 300 to 1000 whatever bag it is (0x78 counts tens, 0x79 hundreds, 0x71 thousands).
u16 itemCount(unsigned id, u32 h)
{
    static const u16 kHandgun[] = {10, 15, 20}, kShotgun[] = {5, 10}, kTmp[] = {25, 50}, kMagnum[] = {3, 5},
                     kSmallBag[] = {0, 50, 100}, kBag[] = {5, 10};
    switch (id) {
    case 0x04: return kHandgun[h % 3];
    case 0x18: return kShotgun[h % 2];
    case 0x07: return 5;
    case 0x20: return kTmp[h % 2];
    case 0x00: return kMagnum[h % 2];
    case 0x78: return kSmallBag[h % 3];
    case 0x79: return kBag[h % 2];
    case 0x71: return 1;
    }
    return 0;
}

u32 mix(u32 x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// The deal of one room: every record of its ITA once (see above).
void dealRoom(SCE_AT_DATA* work, int num)
{
    const int wanted = wantedBits();
    const bool on = (wanted & kItemRandom) && storyGame() && pG->pl_type != 1;
    const u32 seed = (u32) (wanted & kSeedMask) >> kSeedShift;
    int pool = 0, changed = 0;
    char list[600];
    int len = 0;
    list[0] = 0;
    for (int i = 0; i < num; i++) {
        SCE_AT_ITEM* r = (SCE_AT_ITEM*) ((u8*) work + i * sizeof(SCE_AT_DATA));
        if (r->pad2[0] == kMark) {
            continue;
        }
        r->pad2[0] = kMark;
        SCE_AT_DATA_ITEM& it = r->item;
        if (!on || r->id != SCEAT_ID_ITEM || r->waiting_type != 0 || it.ctrl_flag != 0) {
            continue;
        }
        const unsigned old = it.item_id;
        const int own = itemGroup(old);
        if (own < 0) {
            continue;
        }
        pool++;
        u32 h = mix(seed * 0x9E3779B1u ^ mix((u32) pG->room_id << 16 | (u32) i << 8 | old));
        // the group by its share, then the first group from there with a model in the room
        int pick = (int) (h % 100), g = 0;
        while (g < kGroups - 1 && pick >= kShare[g]) {
            pick -= kShare[g];
            g++;
        }
        u8 cand[256];
        int n = 0;
        for (int k = 0; k < kGroups && n == 0; k++) {
            n = groupCandidates((g + k) % kGroups, cand);
            if (n) {
                g = (g + k) % kGroups;
            }
        }
        if (n == 0) {
            continue;
        }
        h = mix(h);
        const unsigned id = cand[h % (u32) n];
        if (id == old) {
            continue;  // drawn as it was: its count stays
        }
        const u16 count = itemCount(id, mix(h));
        changed++;
        if (len < (int) sizeof(list) - 40) {
            len += snprintf(list + len, sizeof(list) - len, "%s area 0x%02x 0x%02x x%u -> 0x%02x x%u (%s)",
                            changed > 1 ? "," : ":", (unsigned) r->no, old, (unsigned) it.item_num, id,
                            (unsigned) count, kGroupName[g]);
        }
        it.item_id = (u16) id;
        it.item_num = count;
    }
    if (on) {
        re4_log("cheat: random items in r%03x (seed %u): %d of %d items of the pool changed, %d records%s",
                (unsigned) pG->room_id, (unsigned) seed, changed, pool, num, list);
    }
}

}  // namespace

extern "C" {
// Patch 0218 (sce_at.cpp sceAtSetItem): the room's items dealt once, at the first of its item areas set up.
// `head` the room's ITA file (SceAtSys.pItemData, a former room's when this one has none), `work` its records.
void re4_item_random(SCE_AT_ITEM* w, void* head, SCE_AT_DATA* work)
{
    struct Head {
        char magic[4];
        u16 version;
        u16 num;
    };
    if (!w || !head || !work || memcmp(head, "ITA", 4) != 0 || w->pad2[0] == kMark) {
        return;
    }
    const int num = ((const Head*) head)->num;
    const uintptr_t a = (uintptr_t) w, b = (uintptr_t) work, size = sizeof(SCE_AT_DATA);
    // an item area of the room's ITA (the areas made at run time, dropped items, live elsewhere)
    if (a < b || a >= b + num * size || (a - b) % size != 0) {
        return;
    }
    dealRoom(work, num);
}

// re4_whole_prelude.h re4_arc_entry: 1 while a stand-in plays the story with its own Ganado entries read
// (every index 0x0FB..0x2BD of a Ganado archive then asks re4_arc_stand_in)
int re4_story_ganado;
}

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

// Patch 0204 (em.cpp, em10.cpp): the cheat "Invisibility" (any thread; the game frames read it).
int re4_invisible(void)
{
    const int wanted = gWanted.load(std::memory_order_relaxed);
    return wanted > 0 && (wanted & kInvisible) ? 1 : 0;
}

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
        re4_story_ganado = 0;
        // patch 0211: the Ganados' flying rocket takes Leon's model (re4_ganado_rocket); The Mercenaries and the
        // extra games with Ada, Krauser or Wesker read his archive once for it
        if (lacksRocket(pG->pl_type) && gLeonCostume < 0) {
            loadLeon(0);
        }
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
        loadStandInSet(pG->pl_type);
        if (pG->pl_type == 2) {
            loadAdaGrips();
        }
    }
    re4_story_ganado = standIn() && gSetType == pG->pl_type;
}

// 1 while Ada, HUNK, Krauser or Wesker plays the story for Leon (the instant save allows them, 04.11.2026)
int re4_story_char_stand_in(void)
{
    return standIn();
}

// Leon's player archive while a stand-in plays the story (0 otherwise, or when it could not be read)
void* re4_story_char_leon(void)
{
    return standIn() && gLeonCostume >= 0 ? (void*) (unsigned long) kLeonArcAddr : 0;
}

// Patch 0211 (em10.cpp em10_R1_ShotRocket): the model (tpl 0) or texture (tpl 1) of the rocket a Ganado fires,
// which the original takes from the player's archive (index 0x70 / 0x71). Ada, Krauser and Wesker have none there
// (lacksRocket): Leon's archive at kLeonArcAddr (read for the stand-ins' story and, for this, for The Mercenaries
// and the extra games: re4_story_char_room). The player's own entry otherwise, as in the original.
void* re4_ganado_rocket(int tpl)
{
    const unsigned no = tpl ? 0x71 : 0x70;
    const PlArc* own = pG->pPlayer;
    void* entry = (void*) (size_t) (own->ofs[no] + (u32) (size_t) own);
    if (!lacksRocket(pG->pl_type) || gLeonCostume < 0) {
        return entry;
    }
    const PlArc* leon = (const PlArc*) (unsigned long) kLeonArcAddr;
    static int logged;
    if (!logged) {
        logged = 1;
        re4_log("ganado rocket: %s's archive has no rocket, Leon's model and texture (patch 0211)",
                kCharName[pG->pl_type & 7]);
    }
    return (void*) (size_t) (leon->ofs[no] + kLeonArcAddr);
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
    return standIn() && !ownWeapon(pG->pl_type, no, no == pG->weapon_no ? pG->weapon_type : 0);
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

// Patch 0200 (cam_extra.cpp CameraScope, CameraBinocular; cam_ctrl.cpp StartLookDownEm): the eyes of HUNK
// standing in for Leon. His model has none at parts 0x20/0x21 (0x20 at his chest, 0x21 at the root), the
// first-person cameras looked from his hips; `out` becomes the point where Leon's eyes sit in the head part 4
// (measured: 0, 76, 85 for Leon, Wesker alike, Krauser 0, 81, 85; the head part is the same bone for all).
// 1 when it did.
int re4_story_char_eyes(Vec* out)
{
    static const Vec kEyes = {0.0f, 76.0f, 85.0f};
    if (!pG || !pPL || !standIn() || pG->pl_type != 3 || pPL->nParts <= 4) {
        return 0;
    }
    PSMTXMultVec(pPL->getPartsPtr(4)->mat, &kEyes, out);
    return 1;
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
    // a Ganado archive (0x2BA entries) while a stand-in plays the story: the character's own entry
    // (loadStandInSet). The entry whose data the game takes: an empty tag shares the next entry's (the last
    // with that offset); the melee block 0x2B4..0x2BD by its own number (the story's files lack it, em20's
    // empty ones share its chainsaw walk 0x2BC)
    if (no - kSetLo <= kSetHi - kSetLo && gSetType > 0 && pG && arc != (const void*) pG->pPlayer && standIn() &&
        gSetType == pG->pl_type && ((const u32*) arc)[0] == 0x2BA) {
        const u32* ofs = (const u32*) arc;
        unsigned k = no;
        if (no < kMeleeFirst && ofs[no] != ofs[1]) {
            while (k < kSetHi && ofs[k + 1] == ofs[no]) {
                k++;
            }
        }
        u32 at = gSetEntry[k - kSetLo];
        if (at) {
            static u8 logged[(kSetHi - kSetLo + 8) / 8];
            unsigned bit = no - kSetLo;
            if (!(logged[bit >> 3] & (1 << (bit & 7)))) {
                logged[bit >> 3] |= (u8) (1 << (bit & 7));
                re4_log("cheat: %s plays: Ganado archive index 0x%x from %s (own move%s)", kCharName[pG->pl_type & 7],
                        no, kStandInSet[pG->pl_type - 2].file, k != no ? ", shared entry" : "");
            }
            return (void*) (size_t) at;
        }
        if (no >= kMeleeFirst || no - 4 >= ofs[0]) {
            return fallback;
        }
    }
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

// Patch 0197 (player.h cPlayer::checkXbutton, pl_klauser.cpp): the X button of a stand-in while Ashley
// follows, as cPlLeon::checkXbutton (pl_leon.cpp) has it: "wait" / "follow" by turns, every 8 frames at
// most; the command flag on (the button's hint). Leon's call (SE 0x36 / 0x37 of the player's sound block) is
// left out: that block holds the character's own sounds. -1 without a stand-in or without Ashley (Krauser's
// arm attack keeps the button then), else 1 when a command was given.
int re4_story_char_xbutton(cPlayer* pl)
{
    if (!pG || !standIn() || pSUB == 0 || pSUB->id != 3) {
        return -1;
    }
    if (pl->m_CmdTimer) {
        pl->m_CmdTimer--;
    }
    StaFlagOn(pG, STA_SUBCHAR_CTRL);
    if (pl->m_CmdTimer != 0 || SubCharCheckCtrl() == 0 || !(Key.trg & 0x200)) {
        return 0;
    }
    bool wait = !(SubCharGetStatus() & 0x40000000);
    SubCharCtrl(wait ? 0 : 1, 0);
    re4_log("cheat: %s commands Ashley: %s", kCharName[pG->pl_type & 7], wait ? "wait" : "follow");
    pl->m_CmdTimer = 8;
    StaFlagOn(pG, STA_PL_FIRE);
    return 1;
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
