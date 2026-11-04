// The app's minimap (wish of the project owner, 04.11.2026: optional, the current room's outline only, turning
// with the player). Compiled with the game's flags into libre4_main.
//
// The inventory map of the game (src/Sscrn/ss_map.cpp) shows each room as a flat copy of its floor: its hit data
// in SS/cmn/map_objNN.dat has the same triangles as the room's own floor collision, and mapPositionCheck puts
// the player on it through the triangle under him. The outline of the current room comes straight from that
// floor here: the floor and slope triangles of every live scenario collision piece (SatMgr: the room's SAT and
// what its scripts add), in world space (each piece's matrix); an edge that only one triangle has is the
// outline. No map file and no room table: the same on every disc (debug build, European, US, Japanese).
//
// The game thread builds it in a frame hook once the app asked for it (the first re4_core_room_outline call):
// anew when the room changes or its pieces do (checked every 30 frames). The app reads it with
// re4_core_room_outline and places the player from its own snapshot (the player model's matrix).
//
// No C++ containers here: libre4_main has the game's operator new (its heap), and a template the library
// instantiates would also serve the port's other code (ELF interposition: the runtime tests crashed in
// OSAllocFromHeap with a std::vector here). Plain malloc'd arrays instead.
#include "types.h"
#include "global.h"
#include "atari.h"
#include "at_sub.h"

#include <math.h>
#include <pthread.h>
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
        gKeys = (unsigned*) malloc(want * sizeof(unsigned));
        gCounts = (unsigned char*) malloc(want);
        gSlots = gKeys && gCounts ? want : 0;
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
    return n;
}

void hook(unsigned frame)
{
    GLOBAL_WK* g = pG;
    if (!gWanted || !live(g) || g->Rno0 != 3)
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
    if (!gHooked) {
        gHooked = true;
        gWanted = true;
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

}  // extern "C"
