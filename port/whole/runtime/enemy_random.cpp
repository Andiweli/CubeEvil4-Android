// The cheat "Random enemies" (0.99.12, decision of the project owner, 12.11.2026: "Ok den Randomizer Schalter wie von
// dir vorgeschlagen 'Stufe 1' und als Button neben dem Item Randomizer"; 0.99.13 "stage 1.5", the owner's choice of
// 13.11.2026 after his own test in r100, "sie sind alle ident zum original"; port/docs/gegner-randomizer-plan.md),
// compiled with the game's flags into libre4_main; patch 0231 (src/game/em_set.cpp EmSetFromList) asks it.
//
// When a story room starts, EmSetFromList creates the room's entries of the chapter's enemy list (pG->Em_list, 256
// entries, saved with the game; deaths kept by entry number in pG->Em_flg). With the switch on (a seed of its own,
// re4_core_set_enemy_random), for the room's ordinary Ganados:
//   - each takes the spot (position and facing) of another entry of the room where a Ganado stood or will stand (its
//     own spot among them), a shuffle from the seed: its kind, model, behaviour, life and guard radius stay its own;
//   - 0.99.13: from that spot it moves 1.5 to 4 m in a direction from the seed, to a place the room's collision says
//     is safe (NearSpot: the floor there within 25 cm of the spot's, no step down or gap on the way, no wall between
//     the two at 40 cm and 1 m, room for a figure above, not nearer than 6 m to Leon unless the spot already was, at
//     least 1 m from every other entry of the room and every place chosen before); eight tries, else the spot itself.
//     So the room really looks different, not only other Ganados on the same spots;
//   - 0.99.13, the number (the owner's "+/- 10 %"): a room with 10 or more Ganados at its start gets -1, 0 or +1, with
//     20 or more -2 .. +2; fewer than 10 keep their number. Left out are Ganados that come now, never the last of a
//     kind (an event may need its code); a copy is one of the room's ordinary Ganados that come now (its kind is
//     loaded) at a safe place beside another one, its death kept at an empty entry of the list (id 0: the game never
//     uses it), so it stays dead like the others; only while the room's enemy works keep room for what is still to
//     come (the entries of the room not there yet, and four more).
//   - stage 2 (prototype, branch random-enemies, 16.11.2026, port/docs/gegner-randomizer-plan.md section 10): before
//     all that, a place anywhere the room's route reaches (RouteSpot: the room's way points, which the enemies use to
//     find Leon, in the part of the room the Ganado's own spot reaches both ways; on a link between two of them, up to
//     0.8 m aside; the checks of NearSpot and: no fall, cliff or hiding floor, not in or near an area of the room
//     (doors 3 m, ladders, items, jumps, hiding places, save points 1.5 m), 10 m from Leon at the start unless the
//     spot was nearer, 1.2 m from every entry and every place chosen; 24 tries, the first 16 also 5 m from every
//     place where a Ganado of the room stands at this start: a player sees one there as at its old place), facing
//     anywhere. Only where that
//     finds nothing (no route, no way point near the spot) the stage 1.5 way. RE4_ENEMY_RANDOM_STAGE=1 (the PC
//     check) keeps stage 1.5 for a comparison.
// The list itself is never written: the enemy gets a copy of its entry with the new place (EmSetWork), so a save stays
// the game's own (only a copy's death sets a bit of an empty entry). Same seed, same list and room: the same places,
// the same ones left out, the same copies.
//
// Kinds of rooms (port/tools/enemy_random_table.py -> enemy_random_rooms.inc): open; "places only" (stage 2: the
// script counts, sweeps or names list entries; the places of the Ganados it does not drive change, the number never;
// 0.99.13 had this only for the 11 rooms that just count); untouched (a boss, r100), and rooms without a script file.
// Never touched: the extra games (Ada's, the Mercenaries), Ashley's part, stage 4; the entries a room script drives
// (EmSetFromList2, setEm, setPtr, GetEmPtrFromList, an Em_list field other than be_flag; switching an entry on or off
// or keeping its death does not count); the entries an area of the room waits for (waiting_type 1: a door, an item
// that comes when that enemy dies); entries that are not ordinary Ganados (ids 0x10..0x20 except the trader 0x18,
// `set` 0 = "Normal", no chainsaw, trader, Garrador or gatling form, t_emlist.cpp); waves and reinforcements (scripts
// create them later with EmSetFromList2, never through EmSetFromList).
#include "types.h"
#include "global.h"
#include "em.h"
#include "sce_at.h"
#include "atari.h"
#include "at_sub.h"
#include "area.h"
#include "route_ck.h"

#include <math.h>
#include <stdint.h>
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

enum { kOpen = 0, kUntouched = 1, kPlacesOnly = 2 };

struct RoomRow {
    u16 room;
    u8 kind;
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

bool ganadoId(int id)
{
    return id >= 0x10 && id <= 0x20 && id != 0x18;
}

// A Ganado's spot: ids 0x10..0x20 except the trader, set "Normal"
bool ganadoSpot(const EM_LIST& e)
{
    return ganadoId(e.id) && e.set == 0;
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

// The entry comes at this room's start (switched on, not created yet, not dead)
bool comesNow(const EM_LIST& e, int no)
{
    return (e.be_flag & 1) && !(e.be_flag & 2) && !deadBit(no);
}

// A place in the list's units (centimetres) with its facing
struct Place {
    s16 pos[3];
    s16 rot[3];
};

constexpr int kMaxCopies = 2;

struct Plan {
    bool on;
    int room, list, kind;
    u32 seed;
    bool moved[256];   // the entry comes at place[entry]
    Place place[256];
    bool left[256];    // left out
    int leftCount, leftDone;
    int change;        // -2 .. +2
    int ganados;       // Ganados that come at the start
    struct Copy {
        int from, beside, entry;
        Place at;
    } copies[kMaxCopies];
    int copyCount, copyNext, copiesMade, copiesFailed;
    int movers, swapped, nearby, spots, protectedByArea, copyRefused;
    // stage 2: the room's way points, the movers placed through them, those without a way point near their spot,
    // how far the movers came from their own spot (cm, summed; the largest), the tries turned down and why
    int routePoints, routed, noHome;
    float movedSum, movedMax;
    int moverCount;
    int tries, noFloor, badFloor, wall, nearLeon, crowded, inArea, oldPlace;
    char why[96];
    char refused[64];
};
Plan gPlan;
unsigned gLines;
int gTestSeed = -1;  // RE4_ENEMY_RANDOM (the PC check)
// RE4_ENEMY_RANDOM_CHECK=1 (the PC check): the list as EmSetFromList found it, compared at its end: the randomizer
// writes no entry (the game itself sets be_flag of what it created; a copy's death bit is no part of the list)
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

// ---- a safe place nearby (0.99.13) -------------------------------------------------------------------------------

constexpr float kStepMax = 250.0f;     // the floor at the new place within 25 cm of the spot's (no box, no step up)
constexpr float kGapMax = 500.0f;      // on the way there no drop or rise of more than 50 cm
constexpr float kNearLeon = 6000.0f;   // not nearer than 6 m to Leon at the room's start (unless the spot was)
constexpr float kApart = 1000.0f;      // 1 m from every other entry of the room and every place chosen

struct Avoid {
    float p[256 + kMaxCopies][3];
    int n;
};

// (the game's math.h has no fabsf)
float absOf(float v)
{
    return v < 0.0f ? -v : v;
}

// The floor under (x, z) from height y (the room's collision, as the Ganados themselves find it); false: none near
bool floorAt(float x, float y, float z, float* out)
{
    Vec p = {x, y, z};
    Vec* n = 0;
    const f32 f = SatMgr.getFloor(&p, &n, 600.0f, 1500.0f, 0);
    if (f < -99999.0f) {
        return false;
    }
    if (n && n->y < 0.6f) {
        return false;  // steep: a slope or the side of something
    }
    *out = f;
    return true;
}

bool clearLine(float ax, float ay, float az, float bx, float by, float bz)
{
    Vec a = {ax, ay, az}, b = {bx, by, bz}, hit;
    return SatMgr.hitCheck(&a, &b, &hit, 0, 0, 0) == 0;
}

// A place 1.5 .. 4 m from `base` (list units) in a direction from `h`, checked as the head comment says
bool nearSpot(const Place& base, u32 h, const Avoid& avoid, Place* out)
{
    const float bx = base.pos[0] * 10.0f, by = base.pos[1] * 10.0f, bz = base.pos[2] * 10.0f;
    const float lx = pG->pl_pos.x, lz = pG->pl_pos.z;
    const float leonBefore = sqrtf((bx - lx) * (bx - lx) + (bz - lz) * (bz - lz));
    for (u32 t = 0; t < 8; t++) {
        const u32 r = mix(h + t * 0x9E3779B9u);
        const float ang = (float) (r & 0xFFFF) / 65536.0f * 6.2831853f;
        const float d = 1500.0f + (float) ((r >> 16) % 2500u);
        const float cx = bx + cosf(ang) * d, cz = bz + sinf(ang) * d;
        float cy;
        if (!floorAt(cx, by, cz, &cy) || absOf(cy - by) > kStepMax) {
            continue;
        }
        bool ok = true;
        for (int k = 1; k <= 3 && ok; k++) {
            const float s = (float) k / 4.0f;
            float my;
            ok = floorAt(bx + (cx - bx) * s, by, bz + (cz - bz) * s, &my) && absOf(my - by) <= kGapMax;
        }
        if (!ok || !clearLine(bx, by + 400.0f, bz, cx, cy + 400.0f, cz) ||
            !clearLine(bx, by + 1000.0f, bz, cx, cy + 1000.0f, cz) || !clearLine(cx, cy + 100.0f, cz, cx, cy + 1700.0f, cz)) {
            continue;
        }
        const float leon = sqrtf((cx - lx) * (cx - lx) + (cz - lz) * (cz - lz));
        if (leon < kNearLeon && leon < leonBefore) {
            continue;
        }
        for (int k = 0; k < avoid.n && ok; k++) {
            const float dx = cx - avoid.p[k][0], dz = cz - avoid.p[k][2], dy = cy - avoid.p[k][1];
            ok = dx * dx + dz * dz >= kApart * kApart || absOf(dy) > 2000.0f;
        }
        if (!ok) {
            continue;
        }
        *out = base;
        out->pos[0] = (s16) lroundf(cx / 10.0f);
        out->pos[1] = (s16) lroundf(cy / 10.0f);
        out->pos[2] = (s16) lroundf(cz / 10.0f);
        out->rot[1] = (s16) (out->rot[1] + (s16) (mix(r) & 0x3FFF) - 0x2000);  // a little turned, within 90 degrees
        return true;
    }
    return false;
}

// ---- a place anywhere the room's route reaches (stage 2 prototype, 16.11.2026) -------------------------------------

constexpr int kRouteTries = 24;
constexpr float kRouteAside = 800.0f;    // up to 0.8 m beside the link between two way points
constexpr float kRouteFloor = 700.0f;    // the floor there within 70 cm of the link's height
constexpr float kLeonAway = 10000.0f;    // 10 m from Leon at the room's start (unless the spot was nearer)
constexpr float kApartRoute = 1200.0f;   // 1.2 m from every entry of the room and every place chosen
constexpr float kFresh = 5000.0f;        // the first tries want a place 5 m from every Ganado spot of the room
constexpr int kFreshTries = 16;          // (a player sees a Ganado there as at its old place)
// floors no Ganado should stand on: falls, cliffs, fences, jump-over and hiding places, the edges up and down
constexpr u32 kBadFloor = SAT_ATTR_FALL | SAT_ATTR_CLIFF | SAT_ATTR_FALL_FANCE | SAT_ATTR_JUMPOVER | SAT_ATTR_HIDE |
                          SAT_ATTR_FANCE | SAT_ATTR_UP | SAT_ATTR_DOWN | SAT_ATTR_UP2 | SAT_ATTR_DOWN2;

int gStage = -1;  // RE4_ENEMY_RANDOM_STAGE (the PC check): 1 keeps stage 1.5; else 2

int stageNow()
{
    if (gStage < 0) {
        const char* v = getenv("RE4_ENEMY_RANDOM_STAGE");
        gStage = v && *v == '1' ? 1 : 2;
    }
    return gStage;
}

// The room's route (pG->Rtp, route_ck.h; the tables at byte offsets from the header)
struct Route {
    int n, nLine;
    const RTP_POINT* pt;
    const RTP_LINE* ln;
    const s8* next;
};
Route gRoute;

bool routeOpen()
{
    gRoute.n = 0;
    const RTP* r = (const RTP*) pG->Rtp;
    if (!r || r->nPoint < 2 || r->nPoint > 127 || r->nNext != r->nPoint * r->nPoint) {
        return false;
    }
    const uintptr_t base = (uintptr_t) r;
    gRoute.pt = (const RTP_POINT*) (base + r->pPoint);
    gRoute.ln = (const RTP_LINE*) (base + r->pLine);
    gRoute.next = (const s8*) (base + r->pNext);
    gRoute.nLine = r->nLine;
    gRoute.n = r->nPoint;
    return true;
}

// Both ways: from a to b and from b to a (the table's next point, -1 none)
bool connected(int a, int b)
{
    if (a == b) {
        return true;
    }
    const int n = gRoute.n;
    return gRoute.next[a * n + b] >= 0 && gRoute.next[b * n + a] >= 0;
}

// The way point a spot belongs to: the nearest with a clear line (getNearPoint, as the enemies find theirs); -1 none
int homeOf(const s16 pos[3])
{
    if (gRoute.n == 0) {
        return -1;
    }
    Vec v = {pos[0] * 10.0f, pos[1] * 10.0f, pos[2] * 10.0f};
    const int h = getNearPoint(&v, 0, 0);
    return h >= 0 && h < gRoute.n ? h : -1;
}

// The areas of the room no Ganado should stand in or near
struct KeepOut {
    SCE_AT_DATA* at;
    float margin;
};
KeepOut gKeep[128];
int gKeepN;

void keepOutCollect()
{
    gKeepN = 0;
    for (SCE_AT_DATA* w = sceAtSetOtStart(); (w = sceAtGetOtAddr(w)) != 0;) {
        if (w->pParent) {
            continue;  // on a moving model: its area is in the model's space
        }
        float m = 0.0f;
        switch (w->id) {
        case SCEAT_ID_DOOR:
            m = 3000.0f;
            break;
        case SCEAT_ID_ITEM:
        case SCEAT_ID_JUMP:
        case SCEAT_ID_SAVE:
        case SCEAT_ID_DAMAGE:
        case SCEAT_ID_STOOP:
        case SCEAT_ID_SKEY:
        case SCEAT_ID_LADDER:
        case SCEAT_ID_USE:
        case SCEAT_ID_HIDE:
        case SCEAT_ID_POS_JUMP:
        case SCEAT_ID_ITEM_PARENT:
            m = 1500.0f;
            break;
        case SCEAT_ID_EXEC:
            m = (w->target_type & 2) ? 1500.0f : 0.0f;  // an event an enemy can set off
            break;
        default:
            break;
        }
        if (m > 0.0f && gKeepN < (int) (sizeof(gKeep) / sizeof(gKeep[0]))) {
            gKeep[gKeepN].at = w;
            gKeep[gKeepN].margin = m;
            gKeepN++;
        }
    }
}

bool keptOut(float x, float y, float z)
{
    for (int k = 0; k < gKeepN; k++) {
        const float m = gKeep[k].margin;
        const float d[5][2] = {{0.0f, 0.0f}, {m, 0.0f}, {-m, 0.0f}, {0.0f, m}, {0.0f, -m}};
        for (const auto& o : d) {
            Vec p = {x + o[0], y + 100.0f, z + o[1]};
            if (AreaHitCheck(&gKeep[k].at->area, &p)) {
                return true;
            }
        }
    }
    return false;
}

// The floor under (x, z) near height y, as a Ganado would stand on it: false when none, steep or of a bad kind
int floorKind(float x, float y, float z, float* out)
{
    Vec top = {x, y + 800.0f, z}, bottom = {x, y - 1500.0f, z}, hit;
    Vec* n = 0;
    const u32 attr = (u32) SatMgr.hitCheck2(&top, &bottom, &hit, &n, SAT_CK_FLOOR, 0);
    if (!attr) {
        return 1;  // none
    }
    if ((attr & kBadFloor) || (n && n->y < 0.7f)) {
        return 2;  // bad
    }
    *out = hit.y;
    return 0;
}

// The room's Ganado spots of this start (the places a player knows): the first tries of routeSpot keep 5 m from them
Avoid gOld;

// A place anywhere on the route of the part of the room way point `home` reaches both ways, checked as the head
// comment says; `own` the mover's own spot (its facing and the rest of the place)
bool routeSpot(int home, const Place& own, u32 h, const Avoid& avoid, Plan& p, Place* out)
{
    const Route& R = gRoute;
    int cand[128], nc = 0;
    for (int k = 0; k < R.n; k++) {
        if (connected(home, k)) {
            cand[nc++] = k;
        }
    }
    if (nc < 2) {
        return false;
    }
    const float lx = pG->pl_pos.x, lz = pG->pl_pos.z;
    const float ox = own.pos[0] * 10.0f, oz = own.pos[2] * 10.0f;
    const float leonBefore = sqrtf((ox - lx) * (ox - lx) + (oz - lz) * (oz - lz));
    for (u32 t = 0; t < (u32) kRouteTries; t++) {
        const u32 r = mix(h + t * 0x9E3779B9u);
        const u32 r2 = mix(r ^ 0x5bd1e995u);
        p.tries++;
        const RTP_POINT& a = R.pt[cand[r % (u32) nc]];
        // a link of it to another way point of the same part, a point along it, up to 0.8 m aside
        int links[16], nl = 0;
        for (int k = 0; k < a.nLine && nl < 16; k++) {
            const int li = a.offLine + k;
            if (li >= R.nLine) {
                break;
            }
            const int c = R.ln[li].connect;
            if (c >= 0 && c < R.n && connected(home, c)) {
                links[nl++] = c;
            }
        }
        float bx = a.pos.x, by = a.pos.y, bz = a.pos.z, dx = 0.0f, dz = 0.0f;
        if (nl > 0) {
            const RTP_POINT& b = R.pt[links[(r >> 8) % (u32) nl]];
            const float s = (float) (r2 & 0x3FF) / 1024.0f;
            dx = b.pos.x - a.pos.x;
            dz = b.pos.z - a.pos.z;
            bx += dx * s;
            by += (b.pos.y - a.pos.y) * s;
            bz += dz * s;
        }
        float cx = bx, cz = bz;
        const float len = sqrtf(dx * dx + dz * dz);
        if (len > 1.0f) {
            const float side = ((float) ((r2 >> 10) & 0x3FF) / 512.0f - 1.0f) * kRouteAside;
            cx += -dz / len * side;
            cz += dx / len * side;
        }
        float fy, cy;
        const int kb = floorKind(bx, by, bz, &fy), kc = kb ? kb : floorKind(cx, by, cz, &cy);
        if (kb || kc) {
            (kb == 1 || kc == 1 ? p.noFloor : p.badFloor)++;
            continue;
        }
        if (absOf(fy - by) > kRouteFloor || absOf(cy - fy) > kStepMax) {
            p.noFloor++;
            continue;
        }
        // from the link to the place: the floor on the way, no wall at 40 cm and 1 m, room for a figure above
        bool ok = true;
        for (int k = 1; k <= 3 && ok; k++) {
            const float s = (float) k / 4.0f;
            float my;
            ok = floorKind(bx + (cx - bx) * s, fy, bz + (cz - bz) * s, &my) == 0 && absOf(my - fy) <= kGapMax;
        }
        if (!ok || !clearLine(bx, fy + 400.0f, bz, cx, cy + 400.0f, cz) ||
            !clearLine(bx, fy + 1000.0f, bz, cx, cy + 1000.0f, cz) || !clearLine(cx, cy + 100.0f, cz, cx, cy + 1700.0f, cz)) {
            p.wall++;
            continue;
        }
        const float leon = sqrtf((cx - lx) * (cx - lx) + (cz - lz) * (cz - lz));
        if (leon < kLeonAway && leon < leonBefore) {
            p.nearLeon++;
            continue;
        }
        for (int k = 0; k < avoid.n && ok; k++) {
            const float ex = cx - avoid.p[k][0], ez = cz - avoid.p[k][2], ey = cy - avoid.p[k][1];
            ok = ex * ex + ez * ez >= kApartRoute * kApartRoute || absOf(ey) > 2000.0f;
        }
        if (!ok) {
            p.crowded++;
            continue;
        }
        for (int k = 0; k < gOld.n && ok && t < (u32) kFreshTries; k++) {
            const float ex = cx - gOld.p[k][0], ez = cz - gOld.p[k][2], ey = cy - gOld.p[k][1];
            ok = ex * ex + ez * ez >= kFresh * kFresh || absOf(ey) > 2000.0f;
        }
        if (!ok) {
            p.oldPlace++;
            continue;
        }
        if (keptOut(cx, cy, cz)) {
            p.inArea++;
            continue;
        }
        *out = own;
        out->pos[0] = (s16) lroundf(cx / 10.0f);
        out->pos[1] = (s16) lroundf(cy / 10.0f);
        out->pos[2] = (s16) lroundf(cz / 10.0f);
        out->rot[1] = (s16) ((int) (mix(r2) & 0x7FFF) - 0x4000);  // facing anywhere
        return true;
    }
    return false;
}

float distOf(const s16 a[3], const s16 b[3])
{
    const float dx = (a[0] - b[0]) * 10.0f, dz = (a[2] - b[2]) * 10.0f;
    return sqrtf(dx * dx + dz * dz);
}

void avoidAdd(Avoid& a, const s16 pos[3])
{
    if (a.n < (int) (sizeof(a.p) / sizeof(a.p[0]))) {
        a.p[a.n][0] = pos[0] * 10.0f;
        a.p[a.n][1] = pos[1] * 10.0f;
        a.p[a.n][2] = pos[2] * 10.0f;
        a.n++;
    }
}

Place placeOf(const EM_LIST& e)
{
    Place p;
    memcpy(p.pos, e.pos, sizeof(p.pos));
    memcpy(p.rot, e.rot, sizeof(p.rot));
    return p;
}

void logLine(const char* fmt, const char* a, const char* b)
{
    if (gLines < 300) {
        gLines++;
        re4_log(fmt, a, b);
    }
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
    const int seed = seedNow();
    if (!seed || !pG) {
        return;
    }
    p.room = pG->stage_no << 8 | pG->room_no;
    p.list = pG->em_list_no;
    p.seed = (u32) seed;
    const RoomRow* row = rowOf(p.room);
    p.kind = row ? row->kind : kUntouched;
    if (SysFlagChk(pG, SYS_OMAKE_ADA_GAME) || SysFlagChk(pG, SYS_OMAKE_ETC_GAME) || pG->pl_type == 1) {
        snprintf(p.why, sizeof(p.why), "not in the story's Leon part");
    } else if (pG->stage_no < 1 || pG->stage_no > 3 || p.list < 0 || p.list > 7) {
        snprintf(p.why, sizeof(p.why), "not a story room of stages 1..3");
    } else if (!row) {
        snprintf(p.why, sizeof(p.why), "a room without a script of its own");
    } else if (row->kind == kUntouched) {
        snprintf(p.why, sizeof(p.why), "its script names enemies, has a boss, or counts and does more");
    }
    char roomText[16];
    snprintf(roomText, sizeof(roomText), "r%03x", p.room);
    if (p.why[0]) {
        logLine("enemies: random enemies leave %s as it is (%s)", roomText, p.why);
        return;
    }
    // the entries the room's areas wait for (a door, an item that comes with a death)
    bool waited[256] = {};
    for (SCE_AT_DATA* w = sceAtSetOtStart(); (w = sceAtGetOtAddr(w)) != 0;) {
        if (w->waiting_type == 1) {
            waited[w->waiting_no] = true;
        }
    }
    // the movers (static: alive or not) and the spots, in entry order; the Ganados that come now
    int movers[256], spots[256];
    int nm = 0, ns = 0;
    Avoid avoid;
    avoid.n = 0;
    gOld.n = 0;
    for (int i = 0; i < 256; i++) {
        const EM_LIST& e = pG->Em_list[i];
        if (e.id == 0 || e.room != (u16) p.room) {
            continue;
        }
        avoidAdd(avoid, e.pos);
        if (ganadoId(e.id) && comesNow(e, i)) {
            p.ganados++;
            avoidAdd(gOld, e.pos);
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
                const EM_LIST& o = pG->Em_list[spots[k]];
                dup = o.pos[0] == e.pos[0] && o.pos[1] == e.pos[1] && o.pos[2] == e.pos[2];
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
            re4_log("enemies: random enemies in r%03x (list %d, seed %u): no ordinary Ganado to change", p.room, p.list,
                    p.seed);
        }
        return;
    }
    p.on = true;
    // the spots shuffled (Fisher-Yates from the seed), the k-th mover takes the k-th; from there a safe place nearby
    for (int k = ns - 1; k > 0; k--) {
        const int j = (int) (hashOf(p.seed, (u32) p.list, (u32) p.room, 0x100u + (u32) k) % (u32) (k + 1));
        const int t = spots[k];
        spots[k] = spots[j];
        spots[j] = t;
    }
    // stage 2: the room's route and the areas to keep out of
    const bool route = stageNow() >= 2 && routeOpen();
    p.routePoints = route ? gRoute.n : 0;
    gKeepN = 0;
    if (route) {
        keepOutCollect();
    }
    if (gCheck && route) {
        // the PC check's maps (re-map.py): Leon at the start, the room's way points and their links, the doors
        static u16 drawn[64];
        static int drawnN;
        bool seen = false;
        for (int k = 0; k < drawnN && !seen; k++) {
            seen = drawn[k] == (u16) p.room;
        }
        if (!seen && drawnN < 64) {
            drawn[drawnN++] = (u16) p.room;
            re4_log("enemies: map r%03x leon %.0f %.0f %.0f", p.room, (double) pG->pl_pos.x, (double) pG->pl_pos.y,
                    (double) pG->pl_pos.z);
            for (int k = 0; k < gRoute.n; k++) {
                char links[160] = "";
                int len = 0;
                const RTP_POINT& a = gRoute.pt[k];
                for (int j = 0; j < a.nLine && a.offLine + j < gRoute.nLine && len < (int) sizeof(links) - 8; j++) {
                    len += snprintf(links + len, sizeof(links) - len, " %d", gRoute.ln[a.offLine + j].connect);
                }
                re4_log("enemies: map r%03x point %d %.0f %.0f %.0f links%s", p.room, k, (double) a.pos.x,
                        (double) a.pos.y, (double) a.pos.z, links);
            }
            for (int k = 0; k < gKeepN; k++) {
                Vec c;
                AreaGetCenterPos(&c, &gKeep[k].at->area);
                re4_log("enemies: map r%03x area %d kind %d %.0f %.0f %.0f", p.room, gKeep[k].at->no, gKeep[k].at->id,
                        (double) c.x, (double) c.y, (double) c.z);
            }
        }
    }
    int now[256], nn = 0, idle = -1;
    for (int k = 0; k < nm; k++) {
        const int no = movers[k];
        if (!comesNow(pG->Em_list[no], no)) {
            // (dead or later in the chapter: EmSetFromList does not create it now, nothing to place; its spot stays
            // one the others may take in stage 1.5's shuffle)
            idle = idle < 0 ? no : idle;
            continue;
        }
        const Place own = placeOf(pG->Em_list[no]);
        const Place spot = placeOf(pG->Em_list[spots[k]]);
        Place near;
        const int home = route ? homeOf(own.pos) : -1;
        p.noHome += route && home < 0;
        if (home >= 0 && routeSpot(home, own, hashOf(p.seed, (u32) p.list, (u32) p.room, 0x500u + (u32) no), avoid, p,
                                   &near)) {
            p.place[no] = near;
            p.moved[no] = true;
            p.routed++;
            avoidAdd(avoid, near.pos);
        } else if (nearSpot(spot, hashOf(p.seed, (u32) p.list, (u32) p.room, 0x200u + (u32) no), avoid, &near)) {
            p.place[no] = near;
            p.moved[no] = true;
            p.nearby++;
            avoidAdd(avoid, near.pos);
        } else if (spots[k] != no) {
            p.place[no] = spot;
            p.moved[no] = true;
            p.swapped++;
        }
        now[nn++] = no;
        if (p.moved[no]) {
            const float d = distOf(p.place[no].pos, own.pos);
            p.movedSum += d;
            p.movedMax = d > p.movedMax ? d : p.movedMax;
        }
        p.moverCount++;
    }
    // the number: +/- 10 % (rooms with 10 or more Ganados at the start), never in a room that counts
    const int range = p.kind == kPlacesOnly ? 0 : p.ganados >= 20 ? 2 : p.ganados >= 10 ? 1 : 0;
    p.change = range ? (int) (hashOf(p.seed, (u32) p.list, (u32) p.room, 0x4E) % (u32) (2 * range + 1)) - range : 0;
    if (p.change < 0) {
        // left out: Ganados that come now, never the last of a kind in the room
        for (int c = 0; c < -p.change && nn > 0; c++) {
            const int start = (int) (hashOf(p.seed, (u32) p.list, (u32) p.room, 0x4C + (u32) c) % (u32) nn);
            for (int k = 0; k < nn; k++) {
                const int no = now[(start + k) % nn];
                if (p.left[no]) {
                    continue;
                }
                int same = 0;
                for (int i = 0; i < 256; i++) {
                    const EM_LIST& o = pG->Em_list[i];
                    same += i != no && !p.left[i] && o.id == pG->Em_list[no].id && o.room == (u16) p.room && comesNow(o, i);
                }
                if (same > 0) {
                    p.left[no] = true;
                    p.leftCount++;
                    break;
                }
            }
        }
    } else if (p.change > 0) {
        // the room's enemy works keep room for what is to come: this start's entries, the room's later ones, 4 more
        int coming = 0;
        for (int i = 0; i < 256; i++) {
            const EM_LIST& e = pG->Em_list[i];
            coming += e.id != 0 && e.room == (u16) p.room && !(e.be_flag & 2) && !deadBit(i);
        }
        int used = 0;  // (counted here: countActiveWork would also move the manager's own statistics)
        for (u32 k = 0; k < EmMgr.getArrayNum(); k++) {
            used += !EmMgr.fastAt(k)->isEmpty();
        }
        const int free = (int) EmMgr.getArrayNum() - used - coming - 4;
        // empty entries of the list keep the copies' deaths
        int empty[256], ne = 0;
        for (int i = 0; i < 255; i++) {
            if (pG->Em_list[i].id == 0) {
                empty[ne++] = i;
            }
        }
        for (int c = 0; c < p.change; c++) {
            if (c >= free) {
                snprintf(p.refused, sizeof(p.refused), "the room's enemy works full");
                break;
            }
            if (nn == 0 || ne <= c) {
                snprintf(p.refused, sizeof(p.refused), nn ? "no empty list entry" : "no Ganado comes now");
                break;
            }
            Plan::Copy& cp = p.copies[p.copyCount];
            cp.from = now[hashOf(p.seed, (u32) p.list, (u32) p.room, 0x46 + (u32) c) % (u32) nn];
            cp.beside = now[hashOf(p.seed, (u32) p.list, (u32) p.room, 0x47 + (u32) c) % (u32) nn];
            const int first = (int) (hashOf(p.seed, (u32) p.list, (u32) p.room, 0x45) % (u32) ne);
            cp.entry = empty[(first + c) % ne];
            const Place anchor = p.moved[cp.beside] ? p.place[cp.beside] : placeOf(pG->Em_list[cp.beside]);
            Place at;
            // stage 2: anywhere the route of the other one's part reaches, else beside it
            const int home = route ? homeOf(pG->Em_list[cp.beside].pos) : -1;
            const Place own = placeOf(pG->Em_list[cp.from]);
            if (!(home >= 0 &&
                  routeSpot(home, own, hashOf(p.seed, (u32) p.list, (u32) p.room, 0x600u + (u32) c), avoid, p, &at)) &&
                !nearSpot(anchor, hashOf(p.seed, (u32) p.list, (u32) p.room, 0x300u + (u32) c), avoid, &at)) {
                p.copyRefused++;
                continue;
            }
            cp.at = at;
            avoidAdd(avoid, at.pos);
            p.copyCount++;
        }
    }
    (void) idle;
    if (gCheck) {
        memcpy(gBefore, pG->Em_list, sizeof(gBefore));
        gChecking = true;
    }
}

// Patch 0231, EmSetFromList for entry `no` (it passed the game's checks): -1 left out, 0 as it is, 1 created from
// `out` (the entry with another place).
extern "C" int re4_enemy_random_entry(int no, EM_LIST* out)
{
    Plan& p = gPlan;
    if (!p.on || no < 0 || no > 255) {
        return 0;
    }
    if (p.left[no]) {
        p.leftDone++;
        return -1;
    }
    if (!p.moved[no]) {
        return 0;
    }
    *out = pG->Em_list[no];
    memcpy(out->pos, p.place[no].pos, sizeof(out->pos));
    memcpy(out->rot, p.place[no].rot, sizeof(out->rot));
    return 1;
}

// Patch 0231, EmSetFromList's end, again and again until -1: the next copy (a Ganado of the room that came now, at its
// safe place beside another one) and the empty entry its death is kept at. The patch creates it unless that entry is
// marked dead or a live enemy carries it already. The first call writes the room's line.
extern "C" int re4_enemy_random_extra(EM_LIST* out)
{
    Plan& p = gPlan;
    if (!p.on) {
        checkList();
        return -1;
    }
    if (p.copyNext == 0 && gLines < 300) {
        gLines++;
        char count[160] = "";
        int len = 0;
        if (p.kind == kPlacesOnly) {
            len = snprintf(count, sizeof(count), "; the number stays (places only: the script counts or names enemies)");
        } else if (p.change == 0) {
            len = snprintf(count, sizeof(count), "; the number stays (%s)", p.ganados >= 10 ? "as drawn" : "under 10");
        } else if (p.change < 0) {
            len = snprintf(count, sizeof(count), "; %d -> %d, left out:", p.ganados, p.ganados - p.leftDone);
            for (int i = 0; i < 256 && len < (int) sizeof(count) - 8; i++) {
                if (p.left[i]) {
                    len += snprintf(count + len, sizeof(count) - len, " 0x%02x", i);
                }
            }
        } else {
            len = snprintf(count, sizeof(count), "; %d -> %d:", p.ganados, p.ganados + p.copyCount);
            for (int c = 0; c < p.copyCount && len < (int) sizeof(count) - 40; c++) {
                len += snprintf(count + len, sizeof(count) - len, " a copy of 0x%02x beside 0x%02x (death at 0x%02x)",
                                p.copies[c].from, p.copies[c].beside, p.copies[c].entry);
            }
            if (p.copyCount < p.change) {
                snprintf(count + len, sizeof(count) - len, " (%d not: %s)", p.change - p.copyCount,
                         p.refused[0] ? p.refused : "no safe place");
            }
        }
        re4_log("enemies: random enemies in r%03x (list %d, seed %u%s): %d Ganados at the start, %d ordinary, %d spots: "
                "%d anywhere on the route, %d at a new place nearby, %d on another's spot%s%s",
                p.room, p.list, p.seed, p.kind == kPlacesOnly ? ", places only" : "", p.ganados, p.movers, p.spots,
                p.routed, p.nearby, p.swapped, count, p.protectedByArea ? " (some kept: an area waits for them)" : "");
        if (gLines < 300) {
            gLines++;
            re4_log("enemies: r%03x stage %d: route %d way points (%d of the movers without one near), the %d that come "
                    "moved %.1f m on average, at most %.1f m; %d tries turned down: %d no floor, %d bad floor, %d wall, "
                    "%d near Leon, %d crowded, %d near an old Ganado spot, %d in an area (%d areas kept out of)",
                    p.room, stageNow(), p.routePoints, p.noHome, p.moverCount,
                    p.moverCount ? (double) (p.movedSum / p.moverCount / 1000.0f) : 0.0, (double) (p.movedMax / 1000.0f),
                    p.noFloor + p.badFloor + p.wall + p.nearLeon + p.crowded + p.oldPlace + p.inArea, p.noFloor,
                    p.badFloor, p.wall, p.nearLeon, p.crowded, p.oldPlace, p.inArea, gKeepN);
        }
    }
    if (p.copyNext >= p.copyCount) {
        // (after the copies: their entries are empty ones, whose be_flag the game may set; the check sees the rest)
        checkList();
        p.copyNext = kMaxCopies + 1;  // the line is written once
        return -1;
    }
    const Plan::Copy& cp = p.copies[p.copyNext++];
    *out = pG->Em_list[cp.from];
    memcpy(out->pos, cp.at.pos, sizeof(out->pos));
    memcpy(out->rot, cp.at.rot, sizeof(out->rot));
    out->be_flag = 1;
    return cp.entry;
}

// Patch 0231: the copy was created (1) or not (0: dead before, a live one carries its entry, the room is full)
extern "C" void re4_enemy_random_extra_made(int ok)
{
    Plan& p = gPlan;
    if (ok) {
        p.copiesMade++;
        return;
    }
    p.copiesFailed++;
    if (gLines < 300) {
        gLines++;
        re4_log("enemies: random enemies in r%03x: a copy did not come (dead before, there already or the room full)",
                p.room);
    }
}
