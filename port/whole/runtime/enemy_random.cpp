// The cheat "Random enemies" (0.99.12, decision of the project owner, 12.11.2026: "Ok den Randomizer Schalter wie von
// dir vorgeschlagen 'Stufe 1' und als Button neben dem Item Randomizer"; port/docs/gegner-randomizer-plan.md),
// compiled with the game's flags into libre4_main; patch 0231 (src/game/em_set.cpp EmSetFromList) asks it.
//
// When a story room starts, EmSetFromList creates the room's entries of the chapter's enemy list (pG->Em_list, 256
// entries, saved with the game; deaths kept by entry number in pG->Em_flg). With the switch on (a seed of its own,
// re4_core_set_enemy_random), for the room's ordinary Ganados:
//   - each takes the spot (position and facing) of another entry of the room where a Ganado stood or will stand (its
//     own spot among them), a shuffle from the seed: its kind, model, behaviour, life and guard radius stay its own;
//   - per room -1, 0 or +1 enemy: one of them is left out (only if another of its kind is in the room: an event may
//     need its code), or a copy of one of them comes at a spot nobody took. The copy's death is kept at an empty
//     entry of the list (id 0: the game never uses it), so it stays dead like the others.
// The list itself is never written: the enemy gets a copy of its entry with the new spot (EmSetWork), so a save
// stays the game's own (only a copy's death sets a bit of an empty entry). Same seed, same list and room: the same
// spots, the same one left out, the same copy.
//
// Never touched: the extra games (Ada's, the Mercenaries), Ashley's part, stage 4; rooms whose script counts or
// sweeps enemies, has a boss or names list entries through variables (port/tools/enemy_random_table.py ->
// enemy_random_rooms.inc, "untouched"), rooms without a script file; the entries a room script names by number;
// the entries an area of the room waits for (waiting_type 1: a door, an item that comes when that enemy dies);
// entries that are not ordinary Ganados (ids 0x10..0x20 except the trader 0x18, `set` 0 = "Normal", no chainsaw,
// trader, Garrador or gatling form, t_emlist.cpp); waves and reinforcements (scripts create them later with
// EmSetFromList2, never through EmSetFromList).
#include "types.h"
#include "global.h"
#include "em.h"
#include "sce_at.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern GLOBAL_WK* pG;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_core_set_enemy_random(int seed);
extern "C" void re4_enemy_random_room(void);
extern "C" int re4_enemy_random_entry(int no, EM_LIST* out);
extern "C" int re4_enemy_random_extra(EM_LIST* out);
extern "C" void re4_enemy_random_extra_made(int ok);

namespace {

struct RoomRow {
    u16 room;
    u8 untouched;
    u32 named[8];
};

const RoomRow kRooms[] = {
#include "enemy_random_rooms.inc"
};

volatile int gSeed;  // 20 bits, 0 off (the app's switch)

u32 mix(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

u32 hashOf(u32 seed, u32 list, u32 room, u32 what)
{
    return mix(seed * 0x9E3779B1u ^ mix(list << 24 ^ room << 8 ^ mix(what + 0x51)));
}

bool namedIn(const RoomRow* row, int no)
{
    return row && (row->named[no >> 5] >> (no & 31) & 1);
}

// A Ganado's spot: ids 0x10..0x20 except the trader, set "Normal"
bool ganadoSpot(const EM_LIST& e)
{
    return e.id >= 0x10 && e.id <= 0x20 && e.id != 0x18 && e.set == 0;
}

// An ordinary Ganado: no special form (t_emlist.cpp: chainsaw "EM16 C.S." type 4 of 0x10..0x17 and "EM1F Chain Saw"
// type 22 or the chainsaw flag of 0x20, "EM18 Trader" type 6, "EM1C ClawMan" 0x1B type 10 and 0x1C types 10, 13,
// "Gatling Man" 0x1D type 2)
bool ordinary(const EM_LIST& e)
{
    if (!ganadoSpot(e)) {
        return false;
    }
    if (e.id <= 0x17 && e.type == 4) {
        return false;
    }
    if (e.type == 6 && (e.id == 0x13 || e.id == 0x14 || e.id == 0x1E)) {
        return false;
    }
    if ((e.id == 0x1B && e.type == 10) || (e.id == 0x1C && (e.type == 10 || e.type == 13))) {
        return false;
    }
    if (e.id == 0x1D && e.type == 2) {
        return false;
    }
    if (e.id == 0x20 && (e.type == 22 || (e.flag & (1u << 28)))) {
        return false;
    }
    return true;
}

bool deadBit(int no)
{
    if (pG->em_list_no < 0) {
        return false;
    }
    const u32* row = EM_FLG_ROW(pG->em_list_no);
    return (row[no >> 5] & (0x80000000u >> (no & 31))) != 0;
}

bool sameSpot(const EM_LIST& a, const EM_LIST& b)
{
    return a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] && a.pos[2] == b.pos[2];
}

struct Plan {
    bool on;
    int room, list;
    u32 seed;
    u8 spotOf[256];   // entry -> the entry whose spot it takes (0xFF: its own)
    int left;         // the entry to leave out (-1: none)
    bool leftDone;    // it was left out in this room's start
    int change;       // -1, 0, +1
    int extraSpot;    // the spot for a copy (-1: none)
    int movers, moved, spots, protectedByArea;
    char why[96];
};
Plan gPlan;
unsigned gLines;
int gTestSeed = -1;  // RE4_ENEMY_RANDOM (the PC check)
// RE4_ENEMY_RANDOM_CHECK=1 (the PC check): the list as EmSetFromList found it, compared at its end: the randomizer
// writes no entry (the game itself sets be_flag of what it created)
const bool gCheck = getenv("RE4_ENEMY_RANDOM_CHECK") && *getenv("RE4_ENEMY_RANDOM_CHECK") == '1';
EM_LIST gBefore[256];
bool gChecking;

void checkList()
{
    if (!gChecking) {
        return;
    }
    gChecking = false;
    int changed = 0, flags = 0, first = -1;
    for (int i = 0; i < 256; i++) {
        EM_LIST a = gBefore[i], b = pG->Em_list[i];
        if (a.be_flag != b.be_flag) {
            flags++;
        }
        a.be_flag = b.be_flag = 0;
        if (memcmp(&a, &b, sizeof(a)) != 0) {
            changed++;
            first = first < 0 ? i : first;
        }
    }
    re4_log("enemies: check r%03x: %s (%d entries' be_flag set by the game)", gPlan.room,
            changed ? "LIST WRITTEN" : "the list's entries unchanged", flags);
    if (changed) {
        re4_log("enemies: check: %d entries changed, the first 0x%02x", changed, first);
    }
}

int seedNow()
{
    if (gTestSeed < 0) {
        const char* v = getenv("RE4_ENEMY_RANDOM");
        gTestSeed = v && *v ? (int) (strtol(v, 0, 0) & 0xFFFFF) : 0;
        if (gTestSeed) {
            re4_log("enemies: random enemies with seed %d (RE4_ENEMY_RANDOM)", gTestSeed);
        }
    }
    const int s = __atomic_load_n(&gSeed, __ATOMIC_RELAXED);
    return s ? s : gTestSeed;
}

const RoomRow* rowOf(int room)
{
    for (const RoomRow& r : kRooms) {
        if (r.room == room) {
            return &r;
        }
    }
    return 0;
}

}  // namespace

// The app (CoreBridge.setEnemyRandom): the switch's seed, 0 off; from the next room on. Any thread.
extern "C" void re4_core_set_enemy_random(int seed)
{
    seed &= 0xFFFFF;
    if (__atomic_exchange_n(&gSeed, seed, __ATOMIC_RELAXED) != seed) {
        re4_log("core: random enemies %s", seed ? "on (from the next room)" : "off (from the next room)");
        if (seed) {
            re4_log("core: random enemies seed %d", seed);
        }
    }
}

// Patch 0231, EmSetFromList's start: the room's plan.
extern "C" void re4_enemy_random_room(void)
{
    Plan& p = gPlan;
    memset(&p, 0, sizeof(p));
    memset(p.spotOf, 0xFF, sizeof(p.spotOf));
    p.left = p.extraSpot = -1;
    const int seed = seedNow();
    if (!seed || !pG) {
        return;
    }
    p.room = pG->stage_no << 8 | pG->room_no;
    p.list = pG->em_list_no;
    p.seed = (u32) seed;
    const RoomRow* row = rowOf(p.room);
    if (SysFlagChk(pG, SYS_OMAKE_ADA_GAME) || SysFlagChk(pG, SYS_OMAKE_ETC_GAME) || pG->pl_type == 1) {
        snprintf(p.why, sizeof(p.why), "not in the story's Leon part");
    } else if (pG->stage_no < 1 || pG->stage_no > 3 || p.list < 0 || p.list > 7) {
        snprintf(p.why, sizeof(p.why), "not a story room of stages 1..3");
    } else if (!row) {
        snprintf(p.why, sizeof(p.why), "a room without a script of its own");
    } else if (row->untouched) {
        snprintf(p.why, sizeof(p.why), "its script counts or names enemies, or a boss");
    }
    if (p.why[0]) {
        if (gLines < 300) {
            gLines++;
            re4_log("enemies: random enemies leave r%03x as it is (%s)", p.room, p.why);
        }
        return;
    }
    // the entries the room's areas wait for (a door, an item that comes with a death)
    bool waited[256] = {};
    for (SCE_AT_DATA* w = sceAtSetOtStart(); (w = sceAtGetOtAddr(w)) != 0;) {
        if (w->waiting_type == 1) {
            waited[w->waiting_no] = true;
        }
    }
    // the movers (static: alive or not) and the spots, in entry order
    int movers[256], spots[256];
    int nm = 0, ns = 0;
    for (int i = 0; i < 256; i++) {
        const EM_LIST& e = pG->Em_list[i];
        if (e.id == 0 || e.room != (u16) p.room) {
            continue;
        }
        const bool protect = namedIn(row, i) || waited[i];
        if (protect && ganadoSpot(e)) {
            p.protectedByArea += waited[i] && !namedIn(row, i);
        }
        if (ordinary(e) && !protect) {
            movers[nm++] = i;
            spots[ns++] = i;  // its own spot
        } else if (ganadoSpot(e) && !(e.be_flag & 1)) {
            // a Ganado's spot that is not used now (a later wave, another part of the chapter)
            bool dup = false;
            for (int k = 0; k < ns && !dup; k++) {
                dup = sameSpot(pG->Em_list[spots[k]], e);
            }
            if (!dup) {
                spots[ns++] = i;
            }
        }
    }
    p.movers = nm;
    p.spots = ns;
    if (nm == 0) {
        if (gLines < 300) {
            gLines++;
            re4_log("enemies: random enemies in r%03x (list %d, seed %u): no ordinary Ganado to change", p.room,
                    p.list, p.seed);
        }
        return;
    }
    p.on = true;
    // the spots shuffled (Fisher-Yates from the seed), the k-th mover takes the k-th
    for (int k = ns - 1; k > 0; k--) {
        const int j = (int) (hashOf(p.seed, (u32) p.list, (u32) p.room, 0x100u + (u32) k) % (u32) (k + 1));
        const int t = spots[k];
        spots[k] = spots[j];
        spots[j] = t;
    }
    for (int k = 0; k < nm; k++) {
        if (spots[k] != movers[k]) {
            p.spotOf[movers[k]] = (u8) spots[k];
            p.moved++;
        }
    }
    // which movers come now (alive, not created yet, not dead): the one left out is one of them; a copy takes a spot
    // nobody took, else the spot of a mover that does not come now (dead or not in the room at this time)
    int now[256], nn = 0;
    int idle = -1;
    for (int k = 0; k < nm; k++) {
        const EM_LIST& e = pG->Em_list[movers[k]];
        if ((e.be_flag & 1) && !(e.be_flag & 2) && !deadBit(movers[k])) {
            now[nn++] = movers[k];
        } else if (idle < 0) {
            idle = spots[k];
        }
    }
    p.change = (int) (hashOf(p.seed, (u32) p.list, (u32) p.room, 0x4E) % 3u) - 1;
    if (p.change < 0 && nn > 0) {
        p.left = now[hashOf(p.seed, (u32) p.list, (u32) p.room, 0x4C) % (u32) nn];
    } else if (p.change > 0) {
        p.extraSpot = ns > nm ? spots[nm] : idle;
    }
    if (gCheck) {
        memcpy(gBefore, pG->Em_list, sizeof(gBefore));
        gChecking = true;
    }
}

// Patch 0231, EmSetFromList for entry `no` (it passed the game's checks): -1 left out, 0 as it is, 1 created from
// `out` (the entry with another spot).
extern "C" int re4_enemy_random_entry(int no, EM_LIST* out)
{
    Plan& p = gPlan;
    if (!p.on || no < 0 || no > 255) {
        return 0;
    }
    const EM_LIST& e = pG->Em_list[no];
    if (no == p.left) {
        // only while another of its kind is or comes in the room (an event may use its code)
        int same = 0;
        for (int i = 0; i < 256; i++) {
            const EM_LIST& o = pG->Em_list[i];
            if (i != no && o.id == e.id && o.room == e.room && (o.be_flag & 1) && !deadBit(i)) {
                same++;
            }
        }
        if (same > 0) {
            p.leftDone = true;
            return -1;
        }
        p.left = -1;  // the last of its kind: it comes
    }
    const int spot = p.spotOf[no];
    if (spot == 0xFF) {
        return 0;
    }
    const EM_LIST& s = pG->Em_list[spot];
    *out = e;
    memcpy(out->pos, s.pos, sizeof(out->pos));
    memcpy(out->rot, s.rot, sizeof(out->rot));
    return 1;
}

// Patch 0231, EmSetFromList's end: a copy of an ordinary Ganado whose kind is in the room now (its code is loaded) at
// the spot nobody took; returns the empty entry its death is kept at (-1: no copy). The patch creates it unless that
// entry is marked dead or a live enemy carries it already.
extern "C" int re4_enemy_random_extra(EM_LIST* out)
{
    Plan& p = gPlan;
    checkList();
    if (!p.on) {
        return -1;
    }
    char text[200];
    const int left = p.leftDone ? p.left : -1;
    int extra = -1, from = -1;
    if (p.extraSpot >= 0) {
        // the template: a mover of a kind that is alive in the room now
        int kinds[256], nk = 0;
        for (int i = 0; i < 256; i++) {
            const EM_LIST& e = pG->Em_list[i];
            if (e.room != (u16) p.room || !ordinary(e) || namedIn(rowOf(p.room), i)) {
                continue;
            }
            bool live = false;
            for (u32 k = 0; k < EmMgr.getArrayNum() && !live; k++) {
                cEm* em = EmMgr.at(k);
                live = em->isAlive() && em->id == e.id;
            }
            if (live) {
                kinds[nk++] = i;
            }
        }
        // an empty entry of the list keeps its death
        int empty[256], ne = 0;
        for (int i = 0; i < 255; i++) {
            if (pG->Em_list[i].id == 0) {
                empty[ne++] = i;
            }
        }
        if (nk > 0 && ne > 0) {
            from = kinds[hashOf(p.seed, (u32) p.list, (u32) p.room, 0x46) % (u32) nk];
            extra = empty[hashOf(p.seed, (u32) p.list, (u32) p.room, 0x45) % (u32) ne];
            *out = pG->Em_list[from];
            const EM_LIST& s = pG->Em_list[p.extraSpot];
            memcpy(out->pos, s.pos, sizeof(out->pos));
            memcpy(out->rot, s.rot, sizeof(out->rot));
            out->be_flag = 1;
        }
    }
    if (gLines < 300) {
        gLines++;
        int len = snprintf(text, sizeof(text), "%d placed elsewhere", p.moved);
        if (left >= 0) {
            len += snprintf(text + len, sizeof(text) - len, ", entry 0x%02x left out", left);
        } else if (p.change < 0) {
            len += snprintf(text + len, sizeof(text) - len, ", none left out (%s)",
                            p.left < 0 ? "the last of its kind" : "the one chosen is not in the room now");
        }
        if (extra >= 0) {
            len += snprintf(text + len, sizeof(text) - len, ", one more: a copy of 0x%02x at the spot of 0x%02x (its death "
                            "kept at the empty entry 0x%02x)", from, p.extraSpot, extra);
        } else if (p.change > 0) {
            snprintf(text + len, sizeof(text) - len, ", no copy (%s)", p.extraSpot < 0 ? "no free spot" : "no kind alive");
        }
        re4_log("enemies: random enemies in r%03x (list %d, seed %u): %d ordinary Ganados, %d spots: %s%s", p.room,
                p.list, p.seed, p.movers, p.spots, text,
                p.protectedByArea ? " (some kept: an area waits for them)" : "");
    }
    return extra;
}

// Patch 0231: the copy was created (1) or not (0: dead before, a live one carries its entry, the room is full)
extern "C" void re4_enemy_random_extra_made(int ok)
{
    if (!ok && gLines < 300) {
        gLines++;
        re4_log("enemies: random enemies in r%03x: the copy did not come (dead before, there already or the room full)",
                gPlan.room);
    }
}
