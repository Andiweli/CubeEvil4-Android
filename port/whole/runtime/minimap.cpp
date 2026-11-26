// The app's minimap (wish of the project owner, 04.11.2026: optional, the current room's outline only, turning
// with the player). Compiled with the game's flags into libre4_main.
//
// The inventory map of the game (src/Sscrn/ss_map.cpp) shows each room as a flat copy of its floor: its hit data
// in SS/cmn/map_objNN.dat has the same triangles as the room's own floor collision, and mapPositionCheck puts
// the player on it through the triangle under him. The outline of the current room comes straight from that
// floor here: the floor and slope triangles of every live scenario collision piece (SatMgr: the room's SAT and
// what its scripts add), in world space (each piece's matrix); an edge that only one triangle has is the
// outline. Then the foot of every wall triangle of those pieces (walls, pillars and closed doors that stand on the
// floor, where the floor itself goes on: 04.11.2026). No map file and no room table: the same on every disc (debug build, European, US, Japanese).
//
// The game thread builds it in a frame hook once the app asked for it (the first re4_core_room_outline call):
// anew when the room changes or its pieces do (checked every 30 frames). The app reads it with
// re4_core_room_outline and places the player from its own snapshot (the player model's matrix).
//
// 1.1.6 (the project owner: treasures and items on the minimap): the room's pickups too, for the app's dots
// (re4_core_room_pickups, every 5th frame once the app asked): the scenario item areas (sce_at.cpp, area type 3: what
// lies on the floor, hangs, drops from a crate or an enemy, or waits in a chest; the item id, its place, taken or
// not) and the blue medallions (cEmItem 0x4C type 1, until shot). Hidden ones (in a closed chest, a crate not broken,
// an enemy) carry their own mark; the app shows them only when asked to.
//
// No C++ containers here: libre4_main has the game's operator new (its heap), and a template the library
// instantiates would also serve the port's other code (ELF interposition: the runtime tests crashed in
// OSAllocFromHeap with a std::vector here). Plain malloc'd arrays instead.
#include "types.h"
#include "global.h"
#include "atari.h"
#include "at_sub.h"
#include "sce_at.h"
#include "item.h"
#include "em.h"
#include "em_set.h"
#include "emitem.h"
#include "etc_model.h"

#include <math.h>
#include <pthread.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" int re4_valid_port_ptr(const void*);                 // runtime/os_thread.cpp

namespace {

pthread_mutex_t gLock = PTHREAD_MUTEX_INITIALIZER;
bool gWanted, gHooked;
const int kMaxEdges = 12000;
float* gEdges;  // x0 z0 x1 z1 y per edge (kMaxEdges), the published outline
int gCount;
float* gBuild;  // the one being built
unsigned gSerial;
unsigned gRoom = 0xFFFF;
unsigned gSignature;
// edge counting: an open hash of vertex pairs (key, count) per piece
unsigned* gKeys;
unsigned char* gCounts;
int* gIndex;  // the walls: the edge a line went to
unsigned gSlots;

bool live(const void* p)
{
    const unsigned long a = (unsigned long) p;
    return p && ((a >= 0x80000000ul && a < 0x83000000ul) || re4_valid_port_ptr(p));
}

// What the room's floor is made of now: its pieces, their triangles and places.
unsigned signature()
{
    unsigned h = 2166136261u;
    for (u32 i = 0; i < SatMgr.getArrayNum(); i++) {
        cSat* s = SatMgr.fastAt(i);
        if (!s->isAlive() || !live(s->poly_p) || !live(s->vtx))
            continue;
        const unsigned v[4] = {i, (unsigned) s->floor_num << 16 | s->slope_num, (unsigned) (int) (s->mat[0][3] / 64.0f),
                               (unsigned) (int) (s->mat[2][3] / 64.0f)};
        for (unsigned w : v) {
            h ^= w;
            h *= 16777619u;
        }
    }
    return h;
}

// The counting table for `edges` edges at most (a power of two, at least twice as large).
bool table(unsigned edges)
{
    unsigned want = 64;
    while (want < edges * 2)
        want <<= 1;
    if (want > gSlots) {
        free(gKeys);
        free(gCounts);
        free(gIndex);
        gKeys = (unsigned*) malloc(want * sizeof(unsigned));
        gCounts = (unsigned char*) malloc(want);
        gIndex = (int*) malloc(want * sizeof(int));
        gSlots = gKeys && gCounts && gIndex ? want : 0;
        if (!gSlots)
            return false;
    }
    memset(gCounts, 0, gSlots);
    return true;
}

void count(unsigned key)
{
    unsigned i = (key * 2654435761u) & (gSlots - 1);
    while (gCounts[i] && gKeys[i] != key)
        i = (i + 1) & (gSlots - 1);
    gKeys[i] = key;
    if (gCounts[i] < 255)
        gCounts[i]++;
}

int build(float* out)
{
    int n = 0;
    for (u32 i = 0; i < SatMgr.getArrayNum(); i++) {
        cSat* s = SatMgr.fastAt(i);
        if (!s->isAlive() || !live(s->poly_p) || !live(s->vtx))
            continue;
        const unsigned polys = (unsigned) s->floor_num + s->slope_num;
        if (polys == 0 || polys > s->polygon_num || !table(polys * 3))
            continue;
        // how many floor triangles share each edge (by its two vertex numbers)
        for (unsigned p = 0; p < polys; p++) {
            const SAT_POLY& t = s->poly_p[p];
            for (int k = 0; k < 3; k++) {
                const unsigned a = t.v[k], b = t.v[(k + 1) % 3];
                if (a < s->vertex_num && b < s->vertex_num)
                    count(a < b ? a << 16 | b : b << 16 | a);
            }
        }
        for (unsigned slot = 0; slot < gSlots && n < kMaxEdges; slot++) {
            if (gCounts[slot] != 1)
                continue;
            const Vec* v[2] = {&s->vtx[gKeys[slot] >> 16], &s->vtx[gKeys[slot] & 0xFFFF]};
            float w[2][3];
            for (int j = 0; j < 2; j++)
                for (int r = 0; r < 3; r++)
                    w[j][r] = s->mat[r][0] * v[j]->x + s->mat[r][1] * v[j]->y + s->mat[r][2] * v[j]->z + s->mat[r][3];
            if (!isfinite(w[0][0]) || !isfinite(w[1][0]) || !isfinite(w[0][2]) || !isfinite(w[1][2]))
                continue;
            float* e = out + n * 5;
            e[0] = w[0][0];
            e[1] = w[0][2];
            e[2] = w[1][0];
            e[3] = w[1][2];
            e[4] = (w[0][1] + w[1][1]) * 0.5f;
            n++;
        }
    }
    // The walls (wish of the project owner, 04.11.2026: walls and closed doors were missing where the floor runs on
    // under them, a closed door with the corridor behind it, a pillar or a wall standing on the floor): the wall
    // triangles follow the floor and slope ones in each piece; each gives its foot, the edge along its lowest
    // points, once (a wall of several triangles stacked up gives the same line again higher up: drawn once, at
    // its lowest height).
    for (u32 i = 0; i < SatMgr.getArrayNum() && n < kMaxEdges; i++) {
        cSat* s = SatMgr.fastAt(i);
        if (!s->isAlive() || !live(s->poly_p) || !live(s->vtx))
            continue;
        const unsigned first = (unsigned) s->floor_num + s->slope_num, walls = s->wall_num;
        if (walls == 0 || first + walls > s->polygon_num || !table(walls))
            continue;
        for (unsigned p = first; p < first + walls && n < kMaxEdges; p++) {
            const SAT_POLY& t = s->poly_p[p];
            float w[3][3];
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) {
                if (t.v[k] >= s->vertex_num) {
                    ok = false;
                    break;
                }
                const Vec& v = s->vtx[t.v[k]];
                for (int r = 0; r < 3; r++)
                    w[k][r] = s->mat[r][0] * v.x + s->mat[r][1] * v.y + s->mat[r][2] * v.z + s->mat[r][3];
                ok = isfinite(w[k][0]) && isfinite(w[k][1]) && isfinite(w[k][2]);
            }
            if (!ok)
                continue;
            // standing walls only (steep: the normal at most a little up or down; sloped roofs and ceilings of the
            // wall list would cross the map as long lines)
            {
                const float ux = w[1][0] - w[0][0], uy = w[1][1] - w[0][1], uz = w[1][2] - w[0][2];
                const float vx = w[2][0] - w[0][0], vy = w[2][1] - w[0][1], vz = w[2][2] - w[0][2];
                const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
                const float nn = nx * nx + ny * ny + nz * nz;
                if (!(nn > 0.0f) || ny * ny > 0.25f * nn)
                    continue;
            }
            const float low = fminf(w[0][1], fminf(w[1][1], w[2][1]));
            // the longest edge whose two ends are at the foot (within 150 units of the lowest point)
            int best = -1;
            float bestLen = 2500.0f;  // at least 5 cm (squared)
            for (int k = 0; k < 3; k++) {
                const float* a = w[k];
                const float* b = w[(k + 1) % 3];
                if (a[1] > low + 150.0f || b[1] > low + 150.0f)
                    continue;
                const float len = (a[0] - b[0]) * (a[0] - b[0]) + (a[2] - b[2]) * (a[2] - b[2]);
                if (len > bestLen) {
                    bestLen = len;
                    best = k;
                }
            }
            if (best < 0)
                continue;
            const float* a = w[best];
            const float* b = w[(best + 1) % 3];
            // once per line (its ends to 16 units, in either order)
            unsigned qa = (unsigned) ((int) floorf(a[0] / 16.0f) * 73856093) ^ (unsigned) ((int) floorf(a[2] / 16.0f) * 19349663);
            unsigned qb = (unsigned) ((int) floorf(b[0] / 16.0f) * 73856093) ^ (unsigned) ((int) floorf(b[2] / 16.0f) * 19349663);
            const unsigned key = (qa < qb ? qa * 2654435761u + qb : qb * 2654435761u + qa) | 1u;
            unsigned slot = (key * 2654435761u) & (gSlots - 1);
            while (gCounts[slot] && gKeys[slot] != key)
                slot = (slot + 1) & (gSlots - 1);
            if (gCounts[slot]) {
                float* had = out + gIndex[slot] * 5;
                had[4] = fminf(had[4], low);
                continue;
            }
            gKeys[slot] = key;
            gCounts[slot] = 1;
            gIndex[slot] = n;
            float* e = out + n * 5;
            e[0] = a[0];
            e[1] = a[2];
            e[2] = b[0];
            e[3] = b[2];
            e[4] = low;
            n++;
        }
    }
    return n;
}

// --- the pickups (1.1.6) ---
const int kMaxPickups = 160;
bool gPickWanted;
float* gPicks;      // x z y kind per pickup (kMaxPickups), the published list
float* gPickBuild;  // the one being built
int gPickCount;
unsigned gPickSerial, gPickRoom = 0xFFFF;
unsigned gPickSeen = ~0u;  // the last list's sum, for the log line
int gPickDetail = -1;      // RE4_MINIMAP_PICKS=1 (PC): one line per item area when the list changes
bool gPickDetailNow;

// A pickup's place in the world: its model's (it hangs on something or falls) while it has one, else the area's.
void place(const SCE_AT_UNIT* w, const SCE_AT_DATA_ITEM* it, float* xyz)
{
    const cModel* m = it->pModel;
    if (live(m) && (w->pParent || (it->ctrl_flag & 0x50))) {
        xyz[0] = m->mat[0][3];
        xyz[1] = m->mat[1][3];
        xyz[2] = m->mat[2][3];
        return;
    }
    xyz[0] = it->item_pos.x;
    xyz[1] = it->item_pos.y;
    xyz[2] = it->item_pos.z;
}

int buildPickups(float* out)
{
    GLOBAL_WK* g = pG;
    const int mask = g->pl_type == 0 ? 1 : 2;  // the game modes of an item (sceAtSetItem)
    int n = 0;
    SCE_AT_DATA* d = sceAtSetOtStart();
    int guard = 0;
    while (n < kMaxPickups && (d = sceAtGetOtAddr(d)) != 0 && ++guard < 4096) {
        if (!live(d))
            break;
        const SCE_AT_UNIT* w = (const SCE_AT_UNIT*) d;
        if (w->id != SCEAT_ID_ITEM)
            continue;
        SCE_AT_DATA_ITEM* it = &((SCE_AT_ITEM*) d)->item;
        const unsigned id = it->item_id;
        if (gPickDetailNow)
            re4_log("minimap: area 0x%02x be 0x%02x ctrl 0x%02x item 0x%04x x%u flg %u/%u waiting %u/%u modes %u taken %d at "
                    "(%.0f %.0f %.0f) model %p (be 0x%x, at %.0f %.0f %.0f) parent %p", (unsigned) w->no, (unsigned) w->be_flg, (unsigned) it->ctrl_flag, id,
                    (unsigned) it->item_num, (unsigned) it->item_flg, (unsigned) it->auto_item_flg, (unsigned) w->waiting_type,
                    (unsigned) w->waiting_no, (unsigned) it->player_type, sceAtItemFlgCk(it), it->item_pos.x, it->item_pos.y,
                    it->item_pos.z, (void*) it->pModel, live(it->pModel) ? (unsigned) it->pModel->be_flag : 0u,
                    live(it->pModel) ? it->pModel->mat[0][3] : 0.0f, live(it->pModel) ? it->pModel->mat[1][3] : 0.0f,
                    live(it->pModel) ? it->pModel->mat[2][3] : 0.0f, (void*) w->pParent);
        if (id == 0x1000 || id == 0xFFFF)  // an enemy instead of an item (SceAtCheckSystemItemSet), none
            continue;
        int hidden = 0;
        if (!(w->be_flg & 1)) {
            // not out yet: in a crate (waiting 2, its etc model not broken), an enemy (waiting 1) or a closed chest
            // (waiting 0, opened by the room's own event); taken ones and those of other game modes are not counted
            if (sceAtItemFlgCk(it) || (it->player_type != 0 && !(it->player_type & mask)))
                continue;
            hidden = 8;
        }
        float xyz[3];
        place(w, it, xyz);
        if (xyz[0] == 0.0f && xyz[1] == 0.0f && xyz[2] == 0.0f && w->waiting_type != 2)
            continue;  // no place yet (an event of the room gives it, e.g. a chest's): left out
        if (hidden && w->waiting_type == 2) {
            cEm* box = 0;
            if (getRoomEtcBreak(w->waiting_no, &box, 0) && live(box)) {
                xyz[0] = box->pos.x;
                xyz[1] = box->pos.y;
                xyz[2] = box->pos.z;
            }
        } else if (hidden && w->waiting_type == 1) {
            cEm* carrier = GetEmPtrFromList(w->waiting_no);
            if (!live(carrier))
                continue;  // its enemy is not in the room (yet)
            xyz[0] = carrier->pos.x;
            xyz[1] = carrier->pos.y;
            xyz[2] = carrier->pos.z;
        }
        const u8 type = id < 0x1000 ? itemType((ITEM_ID) id) : 2;  // the random ones (0x1001..) are ammo or recovery
        const int kind = (type == 5 || type == 12 ? 2 : 1) | (hidden ? 8 : 0) | (hidden && w->waiting_type == 1 ? 16 : 0);
        float* e = out + n * 4;
        e[0] = xyz[0];
        e[1] = xyz[2];
        e[2] = xyz[1];
        e[3] = (float) kind;
        n++;
    }
    // the blue medallions: cEmItem 0x4C type 1 (a shootable medal), until it is broken (Status 2)
    for (u32 i = 0; i < EmMgr.getArrayNum() && n < kMaxPickups; i++) {
        cEm* e = EmMgr.fastAt(i);
        if (!live(e) || !e->isAlive() || e->id != 0x4C || e->type != 1 || EMITEM_WK(e)->Status == 2 || e->hp <= 0)
            continue;
        float* o = out + n * 4;
        o[0] = e->pos.x;
        o[1] = e->pos.z;
        o[2] = e->pos.y;
        o[3] = 3.0f;
        n++;
    }
    return n;
}

void pickups(unsigned frame)
{
    if (frame % 5 != 0)
        return;
    if (!gPickBuild && !(gPickBuild = (float*) malloc(kMaxPickups * 4 * sizeof(float))))
        return;
    if (gPickDetail < 0)
        gPickDetail = getenv("RE4_MINIMAP_PICKS") && atoi(getenv("RE4_MINIMAP_PICKS")) > 0;
    gPickDetailNow = gPickDetail && pG->room_id != gPickRoom;
    // (RE4_MINIMAP_PICKS: the time of the list, the mean of 100 lists, once)
    static double spent;
    static int timed;
    struct timespec t0, t1;
    if (gPickDetail && timed < 100)
        clock_gettime(CLOCK_MONOTONIC, &t0);
    const int n = buildPickups(gPickBuild);
    if (gPickDetail && timed < 100) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        spent += (t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3;
        if (++timed == 100)
            re4_log("minimap: pickups list %.1f us on average (100 lists, every 5th frame)", spent / 100.0);
    }
    unsigned sum = (unsigned) n;
    for (int i = 0; i < n; i++)
        sum = sum * 31u + (unsigned) gPickBuild[i * 4 + 3];
    pthread_mutex_lock(&gLock);
    float* done = gPickBuild;
    gPickBuild = gPicks;
    gPicks = done;
    gPickCount = n;
    gPickRoom = pG->room_id;
    gPickSerial++;
    pthread_mutex_unlock(&gLock);
    if (sum != gPickSeen) {
        gPickSeen = sum;
        int treasures = 0, items = 0, medals = 0, hidden = 0;
        for (int i = 0; i < n; i++) {
            const int k = (int) gPicks[i * 4 + 3];
            hidden += (k & 8) != 0;
            if ((k & 7) == 2) treasures++;
            else if ((k & 7) == 3) medals++;
            else items++;
        }
        re4_log("minimap: r%03x pickups: %d treasures, %d items, %d medallions (%d of them hidden)", (unsigned) pG->room_id,
                treasures, items, medals, hidden);
    }
}

void hook(unsigned frame)
{
    GLOBAL_WK* g = pG;
    if (!live(g) || g->Rno0 != 3)
        return;
    if (gPickWanted)
        pickups(frame);
    if (!gWanted)
        return;
    const unsigned room = g->room_id;
    if (room == gRoom && frame % 30 != 0)
        return;
    const unsigned sig = signature();
    if (room == gRoom && sig == gSignature)
        return;
    if (!gBuild && !(gBuild = (float*) malloc(kMaxEdges * 5 * sizeof(float))))
        return;
    const int n = build(gBuild);
    pthread_mutex_lock(&gLock);
    float* done = gBuild;
    gBuild = gEdges;
    gEdges = done;
    gCount = n;
    gRoom = room;
    gSignature = sig;
    gSerial++;
    pthread_mutex_unlock(&gLock);
    re4_log("minimap: r%03x outline, %d edges (outline %u)", room, n, gSerial);
}

}  // namespace

extern "C" {

// The current room's outline for the app's minimap: up to `maxEdges` edges into `out` (x0, z0, x1, z1, y each,
// world units), info[0] = its number (rises with every new outline), info[1] = the room; returns the edges, 0
// before the first one. The first call starts the building (game thread, frame hook). Any thread.
int re4_core_room_outline(float* out, int maxEdges, unsigned* info)
{
    pthread_mutex_lock(&gLock);
    gWanted = true;
    if (!gHooked) {
        gHooked = true;
        re4_add_frame_hook(hook);
    }
    const int n = gEdges && gCount < maxEdges ? gCount : gEdges ? maxEdges : 0;
    if (out && n > 0)
        memcpy(out, gEdges, (size_t) n * 5 * sizeof(float));
    if (info) {
        info[0] = gSerial;
        info[1] = gRoom;
    }
    pthread_mutex_unlock(&gLock);
    return n;
}

// 1.1.6: the current room's pickups for the minimap's dots: up to `max` into `out` (x, z, y, kind each, world units;
// kind & 7: 1 an item, 2 a treasure, 3 a blue medallion; | 8 hidden: in a closed chest, a crate or (| 16) an enemy),
// info[0] = its number, info[1] = the room; returns the count. The first call starts it (game thread, every 5th
// frame). Any thread.
int re4_core_room_pickups(float* out, int max, unsigned* info)
{
    pthread_mutex_lock(&gLock);
    if (!gPickWanted) {
        gPickWanted = true;
        if (!gHooked) {
            gHooked = true;
            re4_add_frame_hook(hook);
        }
    }
    const int n = gPicks ? (gPickCount < max ? gPickCount : max) : 0;
    if (out && n > 0)
        memcpy(out, gPicks, (size_t) n * 4 * sizeof(float));
    if (info) {
        info[0] = gPickSerial;
        info[1] = gPickRoom;
    }
    pthread_mutex_unlock(&gLock);
    return n;
}

}  // extern "C"
