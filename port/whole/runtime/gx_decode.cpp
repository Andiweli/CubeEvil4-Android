// The command decoder of the emulated graphics processor (GP) and the queue of
// decoded chunks to the renderer. Plan: port/docs/gx-plan.md (step G2);
// what a renderer gets: re4_gx_frame.h.
//
// The GP works through the command stream at once, as each command completes
// (gx_fifo.cpp feeds the bytes): vertex arrays, display lists, matrices,
// textures and palettes are read while the game still has them as they were
// when it sent the command. It keeps the register state (CP, XF, BP, the
// palette part of TMEM) and, while a renderer takes chunks, writes the
// decoded work into the current chunk.
//
// Byte order. On the GameCube everything the GP reads is big-endian. In the
// port:
//   - command streams are big-endian: the FIFO (re4_gx_put_* stores
//     big-endian), display lists recorded with GXBeginDisplayList, display
//     lists from the disc (the Formatumwandler leaves primitive streams as
//     they are);
//   - display lists the game builds with CPU stores (effect grids, the sprite
//     quad) are host order; the game registers them (re4_gx_host_order_list,
//     patch 0031) and the decoder reads them so;
//   - vertex arrays and matrices loaded by index are host order: the
//     Formatumwandler turned positions, normals and texture coordinates of
//     the disc to host order, the game's CPU code writes its buffers
//     (skinned positions, effect grids, matrices) in host order; bytes
//     (colours, u8/s8 data) are the same either way;
//   - texels and palette entries stay big-endian (the renderer decodes them).
#include "gx_internal.h"
#include "re4_cpu_info.h"
#include "re4_gx_frame.h"
#include "re4_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <pthread.h>
#include <stdlib.h>
#include <thread>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

inline u16 rd16(const u8* p, bool be)
{
    return be ? (u16) (p[0] << 8 | p[1]) : (u16) (p[1] << 8 | p[0]);
}

inline u32 rd32(const u8* p, bool be)
{
    return be ? (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3]
              : (u32) p[3] << 24 | (u32) p[2] << 16 | (u32) p[1] << 8 | p[0];
}

inline float asFloat(u32 v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

inline u32 rotl(u32 v, int s)
{
    return v << s | v >> (32 - s);
}

inline u32 fmix(u32 h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

// 64-bit content hash (two 32-bit lanes: fast on ARM32).
u64 hashBytes(const u8* p, size_t n, u64 seed)
{
    u32 h1 = (u32) seed ^ 0x9E3779B9u;
    u32 h2 = (u32) (seed >> 32) ^ 0x7F4A7C15u;
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        u32 a, b;
        memcpy(&a, p + i, 4);
        memcpy(&b, p + i + 4, 4);
        h1 = rotl(h1 ^ (a * 0xCC9E2D51u), 15) * 5 + 0xE6546B64u;
        h2 = rotl(h2 ^ (b * 0x1B873593u), 13) * 5 + 0x561CCD1Bu;
    }
    for (; i < n; i++) {
        h1 = rotl(h1 ^ (p[i] * 0xCC9E2D51u), 15) * 5 + 0xE6546B64u;
    }
    h1 = fmix(h1 ^ (u32) n);
    h2 = fmix(h2 + h1);
    return (u64) h2 << 32 | h1;
}

u64 hashWords(const u32* w, int n)
{
    return hashBytes((const u8*) w, (size_t) n * 4, 0x5245344758ull);
}

// --- GP state -----------------------------------------------------------------

struct Gp {
    u32 vcdLo, vcdHi;
    u32 vatA[8], vatB[8], vatC[8];
    u32 arrayBase[16], arrayStride[16];
    u32 matIndexA, matIndexB;
    u32 bp[0x100];
    u32 bpMask = 0xFFFFFF;
    u32 xfMem[0x800];  // XF memory 0x0000-0x07FF (matrices, lights)
    u32 xfReg[0x58];   // XF registers 0x1000-0x1057
};
Gp gGp;

const u32 kTlutBytes = 0x80000;  // the TLUT half of TMEM (BP 0x65 address << 9)
u8 gTlut[kTlutBytes];
bool gTlutLoaded[kTlutBytes >> 9];

struct Stats {
    u32 bytes, commands, bp, cp, xf, xfIndexed, draws, vertices, calls, unknown, dlBytes;
    u32 xfbCopies, texCopies, tlutLoads, textures, textureBytes, chunks, dropped, hostLists, skipped, held;
};
Stats gStats;
u32 gUnknownLogged;

// The last commands of the stream (depth 0), logged once when an unknown
// command shows that the stream lost its place: which command was read with
// a wrong length, and in what vertex format.
struct Recent {
    u8 op;
    u32 bytes, count, vertexSize, vcdLo, vcdHi, vatA;
};
Recent gRecent[16];
u32 gRecentAt;
bool gDesyncLogged;

void remember(u8 op, size_t bytes, u32 count, u32 vertexSize)
{
    Recent& r = gRecent[gRecentAt++ % 16];
    r.op = op;
    r.bytes = (u32) bytes;
    r.count = count;
    r.vertexSize = vertexSize;
    r.vcdLo = gGp.vcdLo;
    r.vcdHi = gGp.vcdHi;
    r.vatA = gGp.vatA[op & 7];
}

void logRecent(const u8* p, size_t n)
{
    if (gDesyncLogged) {
        return;
    }
    gDesyncLogged = true;
    re4_log("GX: the command stream lost its place; the commands before (oldest first):");
    for (u32 i = 0; i < 16; i++) {
        const Recent& r = gRecent[(gRecentAt + i) % 16];
        if (r.bytes == 0) {
            continue;
        }
        if (r.op >= 0x80 && r.op < 0xC0) {
            re4_log("GX:   draw 0x%02x: %u vertices of %u bytes (%u bytes), VCD %08x %08x, VAT A %08x", r.op, r.count,
                    r.vertexSize, r.bytes, r.vcdLo, r.vcdHi, r.vatA);
        } else {
            re4_log("GX:   command 0x%02x (%u bytes)", r.op, r.bytes);
        }
    }
    char bytes[3 * 16 + 1] = {0};
    for (size_t i = 0; i < n && i < 16; i++) {
        snprintf(bytes + 3 * i, 4, "%02x ", p[i]);
    }
    re4_log("GX:   then: %s", bytes);
}

// Display lists the game builds with CPU stores, in host order (patch 0031).
struct HostList {
    u32 size;
    u64 hash;
};
std::unordered_map<uintptr_t, HostList> gHostLists;

// EFB copies to a texture leave this marker at their destination: the
// renderer has the pixels, a texture bound there later is that copy as long
// as the marker is intact (memory the game reloads loses it).
const u8 kCopyMagic[8] = {'R', 'E', '4', 'E', 'F', 'B', 'C', 'P'};
u32 gCopySerial;

// --- chunks -------------------------------------------------------------------

// An allocator whose resize leaves new elements as they are: the vertex and index bytes of a
// draw are all written by its conversion (convertVertices clears its own vertices first, on the
// thread that converts them; the core thread clearing them made every cache line change cores).
template <typename T>
struct NoInit : std::allocator<T> {
    template <typename U>
    struct rebind {
        typedef NoInit<U> other;
    };
    NoInit() = default;
    template <typename U>
    NoInit(const NoInit<U>&)
    {
    }
    template <typename U>
    void construct(U* p)
    {
        ::new ((void*) p) U;
    }
    template <typename U, typename... A>
    void construct(U* p, A&&... a)
    {
        ::new ((void*) p) U(std::forward<A>(a)...);
    }
};

struct Chunk {
    std::vector<u32> ops;
    std::vector<u8, NoInit<u8>> vertices;
    std::vector<u16, NoInit<u16>> indices;
    std::vector<u8> blob;
    Re4GxChunk view;
    // ops.size() right after the last draw record (0: none); a draw that
    // follows it with nothing in between may join it (draw())
    size_t lastDrawEnd = 0;
    // the RE4_GX_OP_COMPACT_FORMAT last sent in this chunk (RE4_GX_COMPACT)
    u32 compactWords[RE4_GX_COMPACT_ATTRS];
    bool compactSent = false;

    void clear()
    {
        ops.clear();
        vertices.clear();
        indices.clear();
        blob.clear();
        memset(&view, 0, sizeof(view));
        lastDrawEnd = 0;
        compactSent = false;
    }
};

const int kSlots = 4;
// kPending: ended with a display copy, waits for the game to put it on screen
// (VISetNextFrameBuffer before VIFlush) or not (SYS_SCREEN_STOP holds the old
// picture while an event or a room is set up)
enum SlotState { kFree, kFilling, kPending, kReady, kReading };
Chunk gSlots[kSlots];
SlotState gSlotState[kSlots];
std::deque<int> gReady;
std::mutex gMutex;
std::condition_variable gCv;
std::atomic<bool> gWanted(false);
// re4_core_gx_notify: the core process's server takes a chunk as soon as it is queued
std::atomic<void (*)(void)> gNotify(nullptr);
std::atomic<long long> gLastPoll(0);
const long long kStallMs = 1000;

int gCurrent = -1;  // slot being filled (core thread); -1: not recording
int gPending = -1;  // slot waiting for the game's VIFlush (kPending); -1: none
u32 gViNext;        // physical address of the XFB the VI shows next (0: never set)
bool gViBlack;      // the VI outputs black (VISetBlack, from VIFlush on): RE4_GX_CHUNK_BLACK
std::atomic<bool> gStill(false);  // a menu screen of the game (re4_gx_still): RE4_GX_CHUNK_STILL
u32 gSequence;
bool gNeedReset = true;

// Textures the renderer has: key -> last chunk that used it, bytes (as sent,
// and as the renderer holds them decoded: RGBA8, gx_texture.cpp).
struct Sent {
    u32 lastChunk;
    u32 bytes;
    u32 decoded;
};
std::unordered_map<u64, Sent> gSent;
u64 gSentBytes;
u64 gDecodedBytes;
const u32 kForgetAfter = 600;           // chunks (10-20 s)
const u64 kSentBudget = 96u << 20;      // texture bytes the renderer keeps at most
// Decoded: a movie (game/sofdec.cpp) brings two new textures every frame, its
// Y8 and C4:4 planes (1.4 MB decoded at 512 x 448); without this limit the
// renderer would hold some 400 MB of past frames before kForgetAfter.
const u64 kDecodedBudget = 256u << 20;

// Per chunk: texture descriptor -> key (hashing once per chunk), and what each
// map is bound to.
struct Bound {
    u64 key;  // texels (with the palette for CI formats: added at each bind)
    u32 flags;
    u32 dataBytes;
    u32 address;
    // RE4_GX_TEXTURE_CHECK=sampled: kept from chunk to chunk (sampledStillSame)
    u64 sample;   // hash of samples of the texels
    u32 checked;  // chunk (gSequence) of the last check
    u32 fullAt;   // chunk of the last hash of all texels
};
std::unordered_map<u64, Bound> gTexMemo;
// Each chunk hashes the texels of every texture it binds (a texture may change
// at the same address: a room loads, a movie decodes into it). With
// RE4_GX_TEXTURE_CHECK=sampled (the OUYA branch: 18 % of the GP thread's time
// in r100 went into those hashes, port/docs/whole-program-status.md) a texture
// keeps its key from chunk to chunk while 16 samples of 32 bytes stay the
// same, and all its texels are hashed again every kFullEvery chunks: a change
// the samples miss shows that much later.
bool gTexturesSampled = [] {
    const char* v = getenv("RE4_GX_TEXTURE_CHECK");
    return v && strcmp(v, "sampled") == 0;
}();
const u32 kFullEvery = 64;
// A register bindTextures() reads, a palette or an EFB copy changed since the
// last draw, or a chunk began: the maps are bound again.
bool gTexturesDirty = true;
u64 gMapKey[8];
u32 gMapFlags[8];
bool gMapValid[8];

long long nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

long long nowNs()
{
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

Chunk* chunk()
{
    return gCurrent >= 0 ? &gSlots[gCurrent] : nullptr;
}

void emit(u32 op, const u32* payload, u32 n)
{
    Chunk* c = chunk();
    c->ops.push_back(op | n << 8);
    c->ops.insert(c->ops.end(), payload, payload + n);
}

u32 blobAppend(const u8* data, u32 n)
{
    Chunk* c = chunk();
    u32 at = (u32) c->blob.size();
    c->blob.insert(c->blob.end(), data, data + n);
    while (c->blob.size() & 31) {
        c->blob.push_back(0);  // the next item 32-byte aligned
    }
    return at;
}

void emitXfRange(u32 address, u32 count)
{
    std::vector<u32> payload(count + 1);
    payload[0] = address;
    for (u32 i = 0; i < count; i++) {
        u32 a = address + i;
        payload[1 + i] = a < 0x800 ? gGp.xfMem[a] : gGp.xfReg[a - 0x1000];
    }
    emit(RE4_GX_OP_XF, payload.data(), count + 1);
}

void emitTlutRange(u32 offset, u32 bytes)
{
    u32 at = blobAppend(gTlut + offset, bytes);
    const u32 payload[3] = {offset, bytes, at};
    emit(RE4_GX_OP_TLUT, payload, 3);
}

void emitFullState()
{
    emit(RE4_GX_OP_BP_STATE, gGp.bp, 0x100);
    emitXfRange(0x000, 0x100);
    emitXfRange(0x400, 0x60);
    emitXfRange(0x500, 0x100);
    emitXfRange(0x600, 0x80);
    emitXfRange(0x1000, 0x58);
    for (u32 b = 0; b < (kTlutBytes >> 9);) {
        if (!gTlutLoaded[b]) {
            b++;
            continue;
        }
        u32 e = b;
        while (e < (kTlutBytes >> 9) && gTlutLoaded[e]) {
            e++;
        }
        emitTlutRange(b << 9, (e - b) << 9);
        b = e;
    }
}

void forgetTextures()
{
    if (gSequence % 64 != 0 && gSentBytes <= kSentBudget && gDecodedBytes <= kDecodedBudget) {
        return;
    }
    std::vector<std::pair<u32, u64>> old;
    for (auto& e : gSent) {
        old.push_back(std::make_pair(e.second.lastChunk, e.first));
    }
    std::sort(old.begin(), old.end());
    for (auto& o : old) {
        bool stale = o.first + kForgetAfter < gSequence;
        if (!stale && gSentBytes <= kSentBudget && gDecodedBytes <= kDecodedBudget) {
            break;
        }
        auto it = gSent.find(o.second);
        gSentBytes -= it->second.bytes;
        gDecodedBytes -= it->second.decoded;
        gSent.erase(it);
        const u32 payload[2] = {(u32) o.second, (u32) (o.second >> 32)};
        emit(RE4_GX_OP_FORGET, payload, 2);
    }
}

// Starts a chunk in a free slot (gMutex held). False when none is free.
bool beginLocked()
{
    int slot = -1;
    for (int i = 0; i < kSlots; i++) {
        if (gSlotState[i] == kFree) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return false;
    }
    gSlotState[slot] = kFilling;
    gCurrent = slot;
    Chunk* c = chunk();
    c->clear();
    c->view.sequence = ++gSequence;
    if (!gTexturesSampled || gTexMemo.size() > 4096) {
        gTexMemo.clear();
    }
    memset(gMapValid, 0, sizeof(gMapValid));
    gTexturesDirty = true;
    if (gNeedReset) {
        gNeedReset = false;
        c->view.flags |= RE4_GX_CHUNK_RESET;
        gSent.clear();
        gSentBytes = 0;
        gDecodedBytes = 0;
        emitFullState();
    } else {
        forgetTextures();
    }
    return true;
}

// Queues the pending chunk for the renderer (gMutex held). A picture the VI
// does not switch to (the game skipped VISetNextFrameBuffer, Render_swap with
// SYS_SCREEN_STOP) is drawn but not shown: RE4_GX_CHUNK_HOLD.
void publishPendingLocked()
{
    if (gPending < 0) {
        return;
    }
    if (!gWanted.load()) {
        // the renderer went away meanwhile: nobody takes it
        gSlotState[gPending] = kFree;
        gStats.dropped++;
        gPending = -1;
        return;
    }
    Re4GxChunk& v = gSlots[gPending].view;
    if (gViNext != 0 && gViNext != v.xfbAddress) {
        v.flags |= RE4_GX_CHUNK_HOLD;
        gStats.held++;
    }
    if (gViBlack) {
        v.flags |= RE4_GX_CHUNK_BLACK;
    }
    gSlotState[gPending] = kReady;
    gReady.push_back(gPending);
    gPending = -1;
    gStats.chunks++;
    gCv.notify_all();
    if (void (*notify)(void) = gNotify.load()) {
        notify();
    }
}

void dropQueueLocked()
{
    for (int s : gReady) {
        gSlotState[s] = kFree;
        gStats.dropped++;
    }
    if (gPending >= 0) {
        gSlotState[gPending] = kFree;
        gStats.dropped++;
        gPending = -1;
    }
    gReady.clear();
    gNeedReset = true;
}

// The vertex jobs (draw(), further down): workers started (-1: not decided yet), whether draws
// become jobs (RE4_GX_THREADS, re4_gx_threads), and since the last "GX core" line the draws
// handed out as batches and the draws converted at once.
int gWorkers = -1;
bool gJobsOn;
u32 gJobCount, gJobInline;
std::atomic<u32> gJobMicros(0);  // the time the batches took (any thread)
std::atomic<int> gWorkerTids[8];   // the workers' thread ids, each as it starts (performance hint)
std::atomic<int> gWorkerTidCount(0);

// The game thread off the slowest cores and Android's performance hint for it and the vertex
// threads (decision of the project owner, 23.10.2026; re4_cpu_info.h), since 24.10.2026 only with
// the app menu's "Force Performance: Use performance cores" (re4_core_set_fast_cores; the default
// "Default" leaves the threads to Android: cooler, slower on some phones). The threads are known at
// the first chunk after the workers started; a change of the setting takes effect at the next
// chunk. While on: after each chunk the game thread's work since the chunk before (without its
// sleep for the retrace and its wait for the renderer) against 33.3 ms, and about once a second
// back on the fast cores if Android moved the thread (realme reports 24.10.2026).
re4cpu::PerfHint gHint;
int gHintState;                       // 0 the threads not known yet, 1 known
int gHintTids[9], gHintTidCount;
std::atomic<int> gFastWanted(0);      // the menu's setting (any thread)
int gFastNow = -1;                    // the setting the threads run with (-1: none yet)
long long gHintFrom, gHintIdle, gHintWait;  // ns: the chunk before, re4_idle_ns() then, the wait since

void hintChunk()
{
    const long long now = nowNs(), idle = re4_idle_ns();
    if (gHintState == 0) {
        if (gWorkers < 0) {
            return;
        }
        int n = 0;
        gHintTids[n++] = re4cpu::ThreadId();
        for (int i = 0; i < (gJobsOn ? gWorkers : 0); i++) {
            const int tid = gWorkerTids[i].load();
            if (!tid) {
                return;  // a worker not running yet: at the next chunk
            }
            gHintTids[n++] = tid;
        }
        gHintTidCount = n;
        gHintState = 1;
    }
    const int fast = gFastWanted.load() ? 1 : 0;
    if (fast != gFastNow) {
        const bool first = gFastNow < 0;
        gFastNow = fast;
        if (fast) {
            char cores[160];
            re4cpu::KeepOffSlowCores(cores, sizeof(cores));
            re4_log("cores: the game thread %s", cores);
            const char* why = "";
            gHint.Open(gHintTids, gHintTidCount, 33333333LL, &why);
            re4_log("performance hint: %s (the game thread and %d vertex threads, 33.3 ms a picture)", why,
                    gHintTidCount - 1);
        } else {
            gHint.Close();
            if (!first) {
                re4cpu::ReleaseCores();
            }
            re4_log("cores: the game thread and the vertex threads left to Android (Force Performance: Default)");
        }
        gHintFrom = 0;
    } else if (gHint.On() && gHintFrom) {
        gHint.Report(now - gHintFrom - (idle - gHintIdle) - gHintWait);
    }
    // about once a second: back on the fast cores if Android moved the thread off them
    static unsigned checks, moves;
    if (fast && ++checks % 30 == 0 && re4cpu::KeepOnFastCores() && ++moves <= 4) {
        re4_log("cores: the game thread put back on its cores (Android had moved it, %u times so far)", moves);
    }
    gHintFrom = now;
    gHintIdle = idle;
    gHintWait = 0;
}

// Timing for the log: game time per chunk (display copy to display copy) and
// the part of it the core waited for a free slot (the renderer is behind).
long long gBoundaryAt;
long long gFrameMs, gWaitMs;
u32 gTimedChunks;
long long gIdleAt;                    // re4_idle_ns() at the last log line
double gFifoBytes, gFifoVertices;     // since the last log line (FIFO: written by the CPU)
double gListBytes;                    // display list bytes read since the last log line
re4cpu::ThreadUse gCoreUse;           // processor time of the core thread per chunk, its kind of core

// A display copy ends the chunk: queue it, start the next one if a renderer
// still takes chunks. Waits while every slot is queued or being read.
void chunkBoundary(u32 xfb, u32 width, u32 height)
{
    re4gx::finishJobs();  // the chunk's vertices complete before it is queued
    hintChunk();
    const long long start = nowMs();
    if (gBoundaryAt && gWanted.load()) {
        gFrameMs += start - gBoundaryAt;
        gCoreUse.Picture();
        if (++gTimedChunks == 600) {
            // idle: asleep for the next retrace (time to spare); what is neither is the
            // game's own work plus this decoder. The processor time next to it: less than the
            // rest means the thread was held back (another thread on its core, a lock, a read)
            static bool layoutLogged;
            if (!layoutLogged) {
                layoutLogged = true;
                char layout[256];
                re4cpu::GetLayout().Describe(layout, sizeof(layout));
                re4_log("GX core: processor %s", layout);
            }
            char use[160];
            gCoreUse.Describe(use, sizeof(use));
            const long long idle = re4_idle_ns();
            re4_log("GX core: 600 chunks, %.1f ms each, of it %.1f ms waiting for the renderer, %.1f ms idle, "
                    "%.0f KB FIFO, %.0f KB display lists, %.0f vertices; %s; vertices: %.0f batches in %.1f ms on "
                    "%d more threads (with the core thread's share), %.0f draws at once",
                    gFrameMs / 600.0, gWaitMs / 600.0, (idle - gIdleAt) / 1e6 / 600.0, gFifoBytes / 1024.0 / 600.0,
                    gListBytes / 1024.0 / 600.0, gFifoVertices / 600.0, use, gJobCount / 600.0,
                    gJobMicros.exchange(0) / 1000.0 / 600.0, gJobsOn ? gWorkers : 0, gJobInline / 600.0);
            gJobCount = gJobInline = 0;
            gIdleAt = idle;
            gTimedChunks = 0;
            gFrameMs = gWaitMs = 0;
            gFifoBytes = gFifoVertices = gListBytes = 0;
        }
    }
    gBoundaryAt = start;
    struct WaitTimer {
        long long from, fromNs;
        ~WaitTimer()
        {
            gWaitMs += nowMs() - from;
            gHintWait += nowNs() - fromNs;
        }
    } waited = {start, nowNs()};
    std::unique_lock<std::mutex> lock(gMutex);
    if (gCurrent >= 0) {
        Chunk* c = chunk();
        Re4GxChunk& v = c->view;
        v.version = RE4_GX_FRAME_VERSION;
        v.flags |= RE4_GX_CHUNK_XFB;
        if (gStill.load()) {
            v.flags |= RE4_GX_CHUNK_STILL;
        }
        v.ops = c->ops.data();
        v.opWords = (u32) c->ops.size();
        v.vertices = c->vertices.data();
        v.vertexBytes = (u32) c->vertices.size();
        v.indices = c->indices.data();
        v.indexCount = (u32) c->indices.size();
        v.blob = c->blob.data();
        v.blobBytes = (u32) c->blob.size();
        v.xfbAddress = xfb;
        v.xfbWidth = width;
        v.xfbHeight = height;
        // a copy without VIFlush since the last one: decided now
        publishPendingLocked();
        gSlotState[gCurrent] = kPending;
        gPending = gCurrent;
        gCurrent = -1;
    }
    if (!gWanted.load()) {
        if (!gReady.empty() || gPending >= 0) {
            dropQueueLocked();
        }
        gNeedReset = true;
        return;
    }
    while (!beginLocked()) {
        if (!gWanted.load() || nowMs() - gLastPoll.load() > kStallMs) {
            // the renderer stopped taking chunks: drop the queue, record again
            // (from a full state) once it asks for chunks
            gWanted.store(false);
            dropQueueLocked();
            return;
        }
        gCv.wait_for(lock, std::chrono::milliseconds(20));
    }
}

// --- vertex formats -------------------------------------------------------------

unsigned componentSize(unsigned format)
{
    return format <= 1 ? 1 : format <= 3 ? 2 : 4;  // u8 s8 / u16 s16 / f32
}

unsigned colorSize(unsigned format)
{
    static const unsigned kSize[8] = {2, 3, 4, 2, 3, 4, 4, 4};  // 565 888 888x 4444 6666 8888
    return kSize[format & 7];
}

unsigned indexSize(unsigned mode)
{
    return mode == 2 ? 1 : mode == 3 ? 2 : 0;  // direct (1) is sized by the format
}

float component(const u8* p, unsigned format, bool be, float scale)
{
    switch (format) {
    case 0:
        return p[0] * scale;
    case 1:
        return (signed char) p[0] * scale;
    case 2:
        return rd16(p, be) * scale;
    case 3:
        return (short) rd16(p, be) * scale;
    default:
        return asFloat(rd32(p, be));
    }
}

inline u8 x5(u32 v)
{
    return (u8) (v << 3 | v >> 2);
}

inline u8 x6(u32 v)
{
    return (u8) (v << 2 | v >> 4);
}

void color(const u8* p, unsigned format, bool be, u8 out[4])
{
    u32 v;
    switch (format) {
    case 0:  // RGB565
        v = rd16(p, be);
        out[0] = x5(v >> 11);
        out[1] = x6((v >> 5) & 63);
        out[2] = x5(v & 31);
        out[3] = 255;
        break;
    case 1:  // RGB888
    case 2:  // RGB888x
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        out[3] = 255;
        break;
    case 3:  // RGBA4444
        v = rd16(p, be);
        out[0] = (u8) ((v >> 12) * 17);
        out[1] = (u8) (((v >> 8) & 15) * 17);
        out[2] = (u8) (((v >> 4) & 15) * 17);
        out[3] = (u8) ((v & 15) * 17);
        break;
    case 4:  // RGBA6666, 24 bits
        v = (u32) p[0] << 16 | (u32) p[1] << 8 | p[2];
        out[0] = x6(v >> 18);
        out[1] = x6((v >> 12) & 63);
        out[2] = x6((v >> 6) & 63);
        out[3] = x6(v & 63);
        break;
    default:  // RGBA8888
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        out[3] = p[3];
        break;
    }
}

// One float into the vertex being written (out is zero where nothing is put)
inline void put(u8* out, float v)
{
    memcpy(out, &v, 4);
}

// The components of one attribute, converted as component() does, specialized for the format,
// the count and the byte order.
template <unsigned Format, bool BE>
inline float convert(const u8* p, float scale)
{
    if (Format == 0) {
        return p[0] * scale;
    }
    if (Format == 1) {
        return (signed char) p[0] * scale;
    }
    if (Format == 2) {
        return rd16(p, BE) * scale;
    }
    if (Format == 3) {
        return (short) rd16(p, BE) * scale;
    }
    return asFloat(rd32(p, BE));
}

template <unsigned Format, unsigned Comps, bool BE>
inline void readComponents(u8* out, const u8* src, float scale)
{
    const unsigned cs = Format <= 1 ? 1 : Format <= 3 ? 2 : 4;
    for (unsigned k = 0; k < Comps; k++) {
        put(out + 4 * k, convert<Format, BE>(src + k * cs, scale));
    }
}

// One attribute of a block of vertices (draw()): src[v] the element of vertex v (null: none, the
// vertex keeps its zeros), written at out + v * stride. One call per attribute and block instead
// of one per vertex: in the profile of RE4_GX_BENCH (22.10.2026) the call per attribute of each
// vertex and the general vertex loop around it were two thirds of the decoder's time.
typedef void (*BlockReader)(u8* out, u32 stride, const u8* const* src, u32 n, float scale);

template <unsigned Format, unsigned Comps, bool BE>
void readBlock(u8* out, u32 stride, const u8* const* src, u32 n, float scale)
{
    for (u32 v = 0; v < n; v++, out += stride) {
        if (const u8* p = src[v]) {
            readComponents<Format, Comps, BE>(out, p, scale);
        }
    }
}

template <unsigned Format, bool BE>
BlockReader blockReaderFor(unsigned comps)
{
    switch (comps) {
    case 1:
        return readBlock<Format, 1, BE>;
    case 2:
        return readBlock<Format, 2, BE>;
    case 3:
        return readBlock<Format, 3, BE>;
    default:
        return readBlock<Format, 9, BE>;
    }
}

// format: 0 u8, 1 s8, 2 u16, 3 s16, 4 and above f32 (componentSize, component)
BlockReader blockReaderFor(unsigned format, unsigned comps, bool be)
{
    switch (format) {
    case 0:
        return be ? blockReaderFor<0, true>(comps) : blockReaderFor<0, false>(comps);
    case 1:
        return be ? blockReaderFor<1, true>(comps) : blockReaderFor<1, false>(comps);
    case 2:
        return be ? blockReaderFor<2, true>(comps) : blockReaderFor<2, false>(comps);
    case 3:
        return be ? blockReaderFor<3, true>(comps) : blockReaderFor<3, false>(comps);
    default:
        return be ? blockReaderFor<4, true>(comps) : blockReaderFor<4, false>(comps);
    }
}

// A colour of a block of vertices: src[v] never null (an indexed colour outside memory reads
// kZero), always written, as color() converts it.
typedef void (*BlockColor)(u8* out, u32 stride, const u8* const* src, u32 n);

template <unsigned Format, bool BE>
void colorBlock(u8* out, u32 stride, const u8* const* src, u32 n)
{
    for (u32 v = 0; v < n; v++, out += stride) {
        u8 rgba[4];
        color(src[v], Format, BE, rgba);
        memcpy(out, rgba, 4);
    }
}

template <bool BE>
BlockColor colorBlockFor(unsigned format)
{
    switch (format & 7) {
    case 0:
        return colorBlock<0, BE>;
    case 1:
        return colorBlock<1, BE>;
    case 2:
        return colorBlock<2, BE>;
    case 3:
        return colorBlock<3, BE>;
    case 4:
        return colorBlock<4, BE>;
    default:
        return colorBlock<5, BE>;
    }
}

// The vertex format of one draw: what the stream holds per vertex and where
// each attribute goes in the chunk's vertex layout.
struct Format {
    u32 inSize;
    bool pnmtx, texmtx[8], anyMatrix;
    u32 posMode, posFormat, posComps;
    float posScale;
    u32 nrmMode, nrmFormat;
    bool nbt, nbt3;
    float nrmScale;
    u32 clrMode[2], clrFormat[2];
    u32 texMode[8], texFormat[8], texComps[8];
    float texScale[8];
    u32 layout, stride;
    u32 offNormal, offBt, offColor[2], offTex[8], offMatrix;
    u8 defaults[9];
    // derived for the vertex loop (draw()): bytes of each attribute's element,
    // the texture coordinates and texture matrix indices present, in order
    u32 posBytes, nrmCs, nrmComps, clrBytes[2], texCs[8], texBytes[8];
    u8 texList[8], texCount;
    u8 mtxList[8], mtxCount;
    // the block readers of draw(), [0] host order, [1] big-endian (direct data of a big-endian
    // stream; arrays are host order)
    BlockReader posBlock[2], nrmBlock[2], nrmPartBlock, texBlock[8][2];
    BlockColor clrBlock[2][2];
    // the compact vertex (RE4_GX_COMPACT, re4_gx_frame.h; the OUYA branch): layout and stride
    // above are then the compact ones, the offsets where each attribute goes in it; the words of
    // RE4_GX_OP_COMPACT_FORMAT; the matrix index bytes the vertex carries
    bool compact;
    u32 posShift, texShift[8];
    u32 cWords[RE4_GX_COMPACT_ATTRS], cMatrixBytes;
};

// RE4_GX_COMPACT=1 (the OUYA): vertices in the compact form (re4_gx_frame.h, RE4_GX_V_COMPACT)
bool gCompact = [] {
    const char* v = getenv("RE4_GX_COMPACT");
    return v && *v && *v != '0';
}();

void compactLayout(Format& f);

void describe(u32 vat, Format& f)
{
    const Gp& g = gGp;
    u32 lo = g.vcdLo, hi = g.vcdHi, a = g.vatA[vat], b = g.vatB[vat], c = g.vatC[vat];
    memset(&f, 0, sizeof(f));
    f.pnmtx = lo & 1;
    f.anyMatrix = f.pnmtx;
    u32 size = f.pnmtx;
    for (int i = 0; i < 8; i++) {
        f.texmtx[i] = (lo >> (1 + i)) & 1;
        f.anyMatrix |= f.texmtx[i];
        size += f.texmtx[i];
    }
    f.posMode = (lo >> 9) & 3;
    f.posComps = (a & 1) ? 3 : 2;
    f.posFormat = (a >> 1) & 7;
    f.posShift = (a >> 4) & 31;
    f.posScale = 1.0f / (float) (1u << f.posShift);
    size += f.posMode == 1 ? f.posComps * componentSize(f.posFormat) : indexSize(f.posMode);
    f.nrmMode = (lo >> 11) & 3;
    f.nbt = (a >> 9) & 1;
    f.nrmFormat = (a >> 10) & 7;
    f.nrmScale = componentSize(f.nrmFormat) == 1 ? 1.0f / 64 : 1.0f / 16384;
    f.nbt3 = f.nbt && (a >> 31);
    if (f.nrmMode == 1) {
        size += (f.nbt ? 9 : 3) * componentSize(f.nrmFormat);
    } else if (f.nrmMode) {
        size += indexSize(f.nrmMode) * (f.nbt3 ? 3 : 1);
    }
    for (int k = 0; k < 2; k++) {
        f.clrMode[k] = (lo >> (13 + 2 * k)) & 3;
        f.clrFormat[k] = k == 0 ? (a >> 14) & 7 : (a >> 18) & 7;
        size += f.clrMode[k] == 1 ? colorSize(f.clrFormat[k]) : indexSize(f.clrMode[k]);
    }
    const u32 comps[8] = {(a >> 21) & 1, b & 1, (b >> 9) & 1, (b >> 18) & 1,
                          (b >> 27) & 1, (c >> 5) & 1, (c >> 14) & 1, (c >> 23) & 1};
    const u32 formats[8] = {(a >> 22) & 7, (b >> 1) & 7, (b >> 10) & 7, (b >> 19) & 7,
                            (b >> 28) & 7, (c >> 6) & 7, (c >> 15) & 7, (c >> 24) & 7};
    const u32 shifts[8] = {(a >> 25) & 31, (b >> 4) & 31, (b >> 13) & 31, (b >> 22) & 31,
                           c & 31, (c >> 9) & 31, (c >> 18) & 31, (c >> 27) & 31};
    for (int i = 0; i < 8; i++) {
        f.texMode[i] = (hi >> (2 * i)) & 3;
        f.texComps[i] = comps[i] ? 2 : 1;
        f.texFormat[i] = formats[i];
        f.texShift[i] = shifts[i];
        f.texScale[i] = 1.0f / (float) (1u << shifts[i]);
        size += f.texMode[i] == 1 ? f.texComps[i] * componentSize(f.texFormat[i]) : indexSize(f.texMode[i]);
    }
    f.inSize = size;

    // output layout (re4_gx_frame.h)
    u32 at = 12;
    if (f.nrmMode) {
        f.layout |= RE4_GX_V_NORMAL;
        f.offNormal = at;
        at += 12;
        if (f.nbt) {
            f.layout |= RE4_GX_V_BINORMAL_TANGENT;
            f.offBt = at;
            at += 24;
        }
    }
    for (int k = 0; k < 2; k++) {
        if (f.clrMode[k]) {
            f.layout |= k == 0 ? RE4_GX_V_COLOR0 : RE4_GX_V_COLOR1;
            f.offColor[k] = at;
            at += 4;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (f.texMode[i]) {
            f.layout |= RE4_GX_V_TEX0 << i;
            f.offTex[i] = at;
            at += 8;
        }
    }
    if (f.anyMatrix) {
        f.layout |= RE4_GX_V_MATRIX;
        f.offMatrix = at;
        at += 12;
    }
    f.stride = at;
    u32 ia = g.matIndexA, ib = g.matIndexB;
    f.defaults[0] = ia & 63;
    f.defaults[1] = (ia >> 6) & 63;
    f.defaults[2] = (ia >> 12) & 63;
    f.defaults[3] = (ia >> 18) & 63;
    f.defaults[4] = (ia >> 24) & 63;
    f.defaults[5] = ib & 63;
    f.defaults[6] = (ib >> 6) & 63;
    f.defaults[7] = (ib >> 12) & 63;
    f.defaults[8] = (ib >> 18) & 63;

    f.posBytes = f.posComps * componentSize(f.posFormat);
    f.nrmCs = componentSize(f.nrmFormat);
    f.nrmComps = f.nbt ? 9 : 3;
    for (int k = 0; k < 2; k++) {
        f.clrBytes[k] = colorSize(f.clrFormat[k]);
    }
    for (int i = 0; i < 8; i++) {
        f.texCs[i] = componentSize(f.texFormat[i]);
        f.texBytes[i] = f.texComps[i] * f.texCs[i];
        if (f.texMode[i]) {
            f.texList[f.texCount++] = (u8) i;
        }
        if (f.texmtx[i]) {
            f.mtxList[f.mtxCount++] = (u8) i;
        }
    }
    for (int be = 0; be < 2; be++) {
        f.posBlock[be] = blockReaderFor(f.posFormat, f.posComps, be != 0);
        f.nrmBlock[be] = blockReaderFor(f.nrmFormat, f.nrmComps, be != 0);
        for (int k = 0; k < 2; k++) {
            f.clrBlock[k][be] = be ? colorBlockFor<true>(f.clrFormat[k]) : colorBlockFor<false>(f.clrFormat[k]);
        }
        for (u32 j = 0; j < f.texCount; j++) {
            const u32 i = f.texList[j];
            f.texBlock[i][be] = blockReaderFor(f.texFormat[i], f.texComps[i], be != 0);
        }
    }
    f.nrmPartBlock = blockReaderFor(f.nrmFormat, 3, false);  // one of three indexed parts (NBT3)
    if (gCompact) {
        compactLayout(f);
    }
}

// The compact layout of a format (re4_gx_frame.h): each attribute as GX read it, on 4 bytes, in
// the order of the float vertex; the matrix indices only those the vertex carries.
void compactLayout(Format& f)
{
    u32 at = 0;
    auto word = [&](u32 type, u32 comps, u32 fraction) {
        return RE4_GX_CA_PRESENT | at | type << 8 | comps << 12 | fraction << 16;
    };
    auto typeOf = [](u32 format) { return format >= 4 ? (u32) RE4_GX_CT_F32 : format; };
    f.compact = true;
    f.layout |= RE4_GX_V_COMPACT;
    if (f.posMode) {
        f.cWords[RE4_GX_CA_POSITION] = word(typeOf(f.posFormat), f.posComps, f.posFormat >= 4 ? 0 : f.posShift);
        at += (f.posBytes + 3) & ~3u;
    }
    if (f.nrmMode) {
        f.offNormal = at;
        f.cWords[RE4_GX_CA_NORMAL] = word(typeOf(f.nrmFormat), f.nrmComps, f.nrmCs == 1 ? 6 : f.nrmCs == 2 ? 14 : 0);
        at += (f.nrmComps * f.nrmCs + 3) & ~3u;
    }
    for (int k = 0; k < 2; k++) {
        if (f.clrMode[k]) {
            f.offColor[k] = at;
            f.cWords[RE4_GX_CA_COLOR0 + k] = word(RE4_GX_CT_RGBA8, 4, 0);
            at += 4;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (f.texMode[i]) {
            f.offTex[i] = at;
            f.cWords[RE4_GX_CA_TEX0 + i] =
                word(typeOf(f.texFormat[i]), f.texComps[i], f.texFormat[i] >= 4 ? 0 : f.texShift[i]);
            at += (f.texBytes[i] + 3) & ~3u;
        }
    }
    if (f.anyMatrix) {
        u32 slots = f.pnmtx ? 1 : 0;
        for (u32 j = 0; j < f.mtxCount; j++) {
            slots |= 2u << f.mtxList[j];
        }
        f.offMatrix = at;
        f.cMatrixBytes = (f.pnmtx ? 1 : 0) + f.mtxCount;
        f.cWords[RE4_GX_CA_MATRIX] = RE4_GX_CA_PRESENT | at | RE4_GX_CT_MATRIX << 8 | f.cMatrixBytes << 12 | slots << 16;
        at += (f.cMatrixBytes + 3) & ~3u;
    }
    f.stride = at ? at : 4;  // a format without attributes: 4 bytes of zeros
}

// The vertex formats as the CP registers describe them now: worked out once
// after a change of VCD, VAT or MATINDEX (cpWrite), not for every draw
// command (describe() for each draw and each size check was a few percent of
// the core's time).
Format gFormat[8];
bool gFormatValid[8];

u32 gFormatSerial;  // formats described so far (the vertex jobs' snapshots)

const Format& format(u32 vat)
{
    if (!gFormatValid[vat]) {
        describe(vat, gFormat[vat]);
        gFormatValid[vat] = true;
        gFormatSerial++;
    }
    return gFormat[vat];
}

unsigned vertexSize(u32 vat)
{
    return format(vat).inSize;
}

// An indexed attribute: the element `index` of array `array` (host order),
// `bytes` long; NULL when outside memory.
const u8 kZero[64] = {0};

// An indexed array as host memory, resolved once per draw (element() went
// through re4gx::memory for every attribute of every vertex: a tenth of the
// core's time in the village).
struct Array {
    const u8* base;  // null: the array is not memory
    u32 limit;       // bytes from base that are memory
    u32 stride;
};

// The arrays as host memory, kept until their CP registers or the windows of port memory change
// (memorySpan for each array of each draw: about 2 % of the decoder with the forest's short
// draws of RE4_GX_BENCH_STRIP=8). The windows are checked once per draw or matrix load
// (arraysCurrent), not per array.
Array gArray[16];
bool gArrayValid[16];
u32 gArrayGeneration;
u32 gArraySerial;  // arrays resolved so far (the vertex jobs' snapshots)

void arraysCurrent()
{
    const u32 generation = re4gx::aliasGeneration();
    if (gArrayGeneration != generation) {
        gArrayGeneration = generation;
        memset(gArrayValid, 0, sizeof(gArrayValid));
    }
}

const Array& array(u32 index)
{
    if (!gArrayValid[index]) {
        Array& a = gArray[index];
        a.stride = gGp.arrayStride[index];
        a.base = re4gx::memorySpan(gGp.arrayBase[index], &a.limit);
        gArrayValid[index] = true;
        gArraySerial++;
    }
    return gArray[index];
}

inline const u8* at(const Array& a, u32 index, u32 bytes)
{
    const u32 offset = index * a.stride;
    return a.base && offset < a.limit && bytes <= a.limit - offset ? a.base + offset : nullptr;
}

// Where each attribute of the vertices of one block comes from (convertVertices; one per thread).
const u32 kBlock = 128;
struct VertexBlock {
    const u8* pos[kBlock];
    const u8* nrm[3][kBlock];
    const u8* clr[2][kBlock];
    const u8* tex[8][kBlock];
};
VertexBlock gBlock;  // the core thread's

// The arrays a draw reads, as host memory at the time of the draw: positions, normals, colours
// 0 and 1, texture coordinates 0-7 (not read: no memory).
struct DrawArrays {
    Array a[12];
};

u32 convertCompact(const Format& f, const DrawArrays& arrays, const u8* in, u32 count, bool be, u8* out);

// The vertices of one draw: `count` of them from the stream bytes at `in` (and the arrays) into
// `out` (count * f.stride bytes, cleared here). First where each attribute of each vertex comes
// from and the matrix indices, then each attribute of a block at once. Returns how many vertices
// carry the index that skips them. Any thread (the vertex jobs).
u32 convertVertices(const Format& f, const DrawArrays& arrays, const u8* in, u32 count, bool be, u8* out,
                    VertexBlock& blk)
{
    if (f.compact) {
        return convertCompact(f, arrays, in, count, be, out);
    }
    const Array& pos = arrays.a[0];
    const Array& nrm = arrays.a[1];
    const u32 stride = f.stride;
    const u32 posMode = f.posMode, nrmMode = f.nrmMode;
    const u32 posIndex = indexSize(posMode), nrmIndex = indexSize(nrmMode);
    const u32 posBytes = f.posBytes, nrmBytes = f.nrmComps * f.nrmCs, nrmCs = f.nrmCs;
    const bool nbt3 = f.nbt3, anyMatrix = f.anyMatrix;
    const int sb = be ? 1 : 0;
    u32 skipped = 0;
    memset(out, 0, (size_t) count * stride);  // what a vertex does not have stays 0
    for (u32 done = 0; done < count;) {
        const u32 n = count - done < kBlock ? count - done : kBlock;
        u8* first = out + (size_t) done * stride;
        u8* o = first;
        for (u32 v = 0; v < n; v++, o += stride) {
            if (anyMatrix) {  // the matrix indices: the defaults, then those of the vertex
                u8* matrix = o + f.offMatrix;
                memcpy(matrix, f.defaults, 9);
                if (f.pnmtx) {
                    matrix[0] = *in++ & 63;
                }
                for (u32 j = 0; j < f.mtxCount; j++) {
                    matrix[1 + f.mtxList[j]] = *in++ & 63;
                }
            }
            // position (the vertex bytes start as zeros: what is not there stays 0)
            if (posMode == 1) {
                blk.pos[v] = in;
                in += posBytes;
            } else if (posMode) {
                u32 index = posMode == 2 ? *in : rd16(in, be);
                in += posIndex;
                if ((posMode == 2 && index == 0xFF) || (posMode == 3 && index == 0xFFFF)) {
                    skipped++;  // GX skips such a vertex; kept here (rare)
                }
                blk.pos[v] = at(pos, index, posBytes);
            }
            // normal (binormal, tangent: the three one after the other from offNormal)
            if (nrmMode == 1) {
                blk.nrm[0][v] = in;
                in += nrmBytes;
            } else if (nbt3) {
                // three indices: normal, binormal, tangent, each the part of its element
                for (u32 part = 0; part < 3; part++) {
                    u32 index = nrmMode == 2 ? *in : rd16(in, be);
                    in += nrmIndex;
                    const u8* src = at(nrm, index, 9 * nrmCs);
                    blk.nrm[part][v] = src ? src + part * 3 * nrmCs : nullptr;
                }
            } else if (nrmMode) {
                u32 index = nrmMode == 2 ? *in : rd16(in, be);
                in += nrmIndex;
                blk.nrm[0][v] = at(nrm, index, nrmBytes);
            }
            for (int k = 0; k < 2; k++) {
                if (f.clrMode[k] == 1) {
                    blk.clr[k][v] = in;
                    in += f.clrBytes[k];
                } else if (f.clrMode[k]) {
                    u32 index = f.clrMode[k] == 2 ? *in : rd16(in, be);
                    in += indexSize(f.clrMode[k]);
                    const u8* src = at(arrays.a[2 + k], index, f.clrBytes[k]);
                    blk.clr[k][v] = src ? src : kZero;
                }
            }
            for (u32 j = 0; j < f.texCount; j++) {
                const u32 i = f.texList[j];
                if (f.texMode[i] == 1) {
                    blk.tex[i][v] = in;
                    in += f.texBytes[i];
                } else {
                    u32 index = f.texMode[i] == 2 ? *in : rd16(in, be);
                    in += indexSize(f.texMode[i]);
                    blk.tex[i][v] = at(arrays.a[4 + i], index, f.texBytes[i]);
                }
            }
        }
        // direct data in the stream's byte order, array elements in host order
        if (posMode) {
            f.posBlock[posMode == 1 ? sb : 0](first, stride, blk.pos, n, f.posScale);
        }
        if (nrmMode == 1) {
            f.nrmBlock[sb](first + f.offNormal, stride, blk.nrm[0], n, f.nrmScale);
        } else if (nbt3) {
            for (u32 part = 0; part < 3; part++) {
                f.nrmPartBlock(first + f.offNormal + 12 * part, stride, blk.nrm[part], n, f.nrmScale);
            }
        } else if (nrmMode) {
            f.nrmBlock[0](first + f.offNormal, stride, blk.nrm[0], n, f.nrmScale);
        }
        for (int k = 0; k < 2; k++) {
            if (f.clrMode[k]) {
                f.clrBlock[k][f.clrMode[k] == 1 ? sb : 0](first + f.offColor[k], stride, blk.clr[k], n);
            }
        }
        for (u32 j = 0; j < f.texCount; j++) {
            const u32 i = f.texList[j];
            f.texBlock[i][f.texMode[i] == 1 ? sb : 0](first + f.offTex[i], stride, blk.tex[i], n, f.texScale[i]);
        }
        done += n;
    }
    return skipped;
}

// One element of the compact vertex as GX read it, in host order (direct data of a big-endian
// stream turned around component by component); null: it stays zero.
inline void copyElement(u8* out, const u8* src, u32 comps, u32 cs, bool swap)
{
    if (!src) {
        return;
    }
    if (swap && cs > 1) {
        for (u32 k = 0; k < comps; k++) {
            for (u32 b = 0; b < cs; b++) {
                out[k * cs + b] = src[k * cs + cs - 1 - b];
            }
        }
    } else {
        memcpy(out, src, comps * cs);
    }
}

// convertVertices for the compact vertex (RE4_GX_COMPACT): the elements copied, not converted;
// colours as in the float vertex (RGBA8). Any thread.
u32 convertCompact(const Format& f, const DrawArrays& arrays, const u8* in, u32 count, bool be, u8* out)
{
    const u32 stride = f.stride, posCs = componentSize(f.posFormat), nrmCs = f.nrmCs;
    const u32 nrmBytes = f.nrmComps * nrmCs;
    u32 skipped = 0;
    memset(out, 0, (size_t) count * stride);  // what a vertex does not have stays 0
    for (u32 v = 0; v < count; v++, out += stride) {
        if (f.anyMatrix) {  // the indices the vertex carries; the others are the draw's defaults
            u8* matrix = out + f.offMatrix;
            for (u32 j = 0; j < f.cMatrixBytes; j++) {
                matrix[j] = *in++ & 63;
            }
        }
        if (f.posMode == 1) {
            copyElement(out, in, f.posComps, posCs, be);
            in += f.posBytes;
        } else if (f.posMode) {
            const u32 index = f.posMode == 2 ? *in : rd16(in, be);
            in += indexSize(f.posMode);
            if ((f.posMode == 2 && index == 0xFF) || (f.posMode == 3 && index == 0xFFFF)) {
                skipped++;
            }
            copyElement(out, at(arrays.a[0], index, f.posBytes), f.posComps, posCs, false);
        }
        if (f.nrmMode == 1) {
            copyElement(out + f.offNormal, in, f.nrmComps, nrmCs, be);
            in += nrmBytes;
        } else if (f.nbt3) {
            for (u32 part = 0; part < 3; part++) {
                const u32 index = f.nrmMode == 2 ? *in : rd16(in, be);
                in += indexSize(f.nrmMode);
                const u8* src = at(arrays.a[1], index, 9 * nrmCs);
                copyElement(out + f.offNormal + part * 3 * nrmCs, src ? src + part * 3 * nrmCs : nullptr, 3, nrmCs,
                            false);
            }
        } else if (f.nrmMode) {
            const u32 index = f.nrmMode == 2 ? *in : rd16(in, be);
            in += indexSize(f.nrmMode);
            copyElement(out + f.offNormal, at(arrays.a[1], index, nrmBytes), f.nrmComps, nrmCs, false);
        }
        for (int k = 0; k < 2; k++) {
            if (f.clrMode[k] == 1) {
                color(in, f.clrFormat[k], be, out + f.offColor[k]);
                in += f.clrBytes[k];
            } else if (f.clrMode[k]) {
                const u32 index = f.clrMode[k] == 2 ? *in : rd16(in, be);
                in += indexSize(f.clrMode[k]);
                const u8* src = at(arrays.a[2 + k], index, f.clrBytes[k]);
                color(src ? src : kZero, f.clrFormat[k], false, out + f.offColor[k]);
            }
        }
        for (u32 j = 0; j < f.texCount; j++) {
            const u32 i = f.texList[j];
            if (f.texMode[i] == 1) {
                copyElement(out + f.offTex[i], in, f.texComps[i], f.texCs[i], be);
                in += f.texBytes[i];
            } else {
                const u32 index = f.texMode[i] == 2 ? *in : rd16(in, be);
                in += indexSize(f.texMode[i]);
                copyElement(out + f.offTex[i], at(arrays.a[4 + i], index, f.texBytes[i]), f.texComps[i], f.texCs[i],
                            false);
            }
        }
    }
    return skipped;
}

// How many indices a draw of `count` vertices with the primitive `kind` (op & 0xF8) gets.
size_t indexCount(u32 kind, u32 count)
{
    switch (kind) {
    case 0x80:
    case 0x88:
        return (size_t) (count / 4) * 6;
    case 0x90:
        return (size_t) (count / 3) * 3;
    case 0x98:
    case 0xA0:
        return count >= 3 ? (size_t) (count - 2) * 3 : 0;
    case 0xA8:
        return (size_t) (count / 2) * 2;
    case 0xB0:
        return count >= 2 ? (size_t) (count - 1) * 2 : 0;
    default:
        return count;
    }
}

// The indices of such a draw at `w`, relative to the first vertex of the renderer draw it is part
// of (`o`: the vertices of the draws it joins, before it). Any thread (the vertex jobs).
void writeIndices(u32 kind, u32 count, u32 o, u16* w)
{
    switch (kind) {
    case 0x80:  // quads
    case 0x88:  // quads (second encoding)
        for (u32 q = 0; q + 3 < count; q += 4) {
            w[0] = (u16) (o + q);
            w[1] = (u16) (o + q + 1);
            w[2] = (u16) (o + q + 2);
            w[3] = (u16) (o + q);
            w[4] = (u16) (o + q + 2);
            w[5] = (u16) (o + q + 3);
            w += 6;
        }
        break;
    case 0x90:  // triangles
        for (u32 i = 0; i + 2 < count; i += 3) {
            w[0] = (u16) (o + i);
            w[1] = (u16) (o + i + 1);
            w[2] = (u16) (o + i + 2);
            w += 3;
        }
        break;
    case 0x98:  // triangle strip: every second triangle flipped back
        for (u32 i = 2; i < count; i++) {
            u32 k = i - 2;
            w[0] = (u16) (o + ((k & 1) ? k + 1 : k));
            w[1] = (u16) (o + ((k & 1) ? k : k + 1));
            w[2] = (u16) (o + i);
            w += 3;
        }
        break;
    case 0xA0:  // triangle fan
        for (u32 i = 2; i < count; i++) {
            w[0] = (u16) o;
            w[1] = (u16) (o + i - 1);
            w[2] = (u16) (o + i);
            w += 3;
        }
        break;
    case 0xA8:  // lines
        for (u32 i = 0; i + 1 < count; i += 2) {
            w[0] = (u16) (o + i);
            w[1] = (u16) (o + i + 1);
            w += 2;
        }
        break;
    case 0xB0:  // line strip
        for (u32 i = 1; i < count; i++) {
            w[0] = (u16) (o + i - 1);
            w[1] = (u16) (o + i);
            w += 2;
        }
        break;
    default:  // points
        for (u32 i = 0; i < count; i++) {
            *w++ = (u16) (o + i);
        }
        break;
    }
}

// --- vertex jobs ------------------------------------------------------------------
//
// The vertices of the draws are converted on other cores (wish of the project owner, 22.10.2026:
// on weak devices the core thread spends most of its time here, and it grows with the vertices of
// a picture). The core thread decodes the commands in order as before; each draw gets its place in
// the chunk at once (vertices, indices, the draw record), and converting its vertices becomes an
// entry of a batch. Most draws are small (the forest of r100: about 8 vertices per GXBegin), so a
// batch collects entries until kBatchVertices; worker threads take whole batches (and the core
// thread, when it has to wait for them). A batch reads the arrays and display lists in game memory
// a little after the command, as the GameCube's GP did while the CPU went on; all of them are done
// (finishJobs) before the GP reports back to the game (draw done, tokens: the game may then change
// what was drawn), before an EFB copy marks memory, before a chunk goes to the renderer, before a
// display list is recorded into memory and before the chunk's buffers would move.
// RE4_GX_THREADS=N: N workers (0: none, every draw on the core thread as before); by default up
// to 3, leaving 3 cores to the game, the renderer and the sound.

// The vertex format and arrays of a run of draws (copied: the registers change meanwhile).
struct Snapshot {
    Format f;
    DrawArrays arrays;
};
struct DrawEntry {
    const Snapshot* snap;
    const u8* in;
    u8* out;
    u16* ix;
    u32 count, kind, o;
    bool be;
    u64 result;  // RE4_GX_JOB_CHECK=1: a hash of its vertices converted when the draw came
};
// RE4_GX_JOB_CHECK=1 (diagnosis): every draw also converted at once on the core thread; a batch
// whose result differs logs it (memory the draw reads changed between the command and the batch)
bool gJobCheck;
struct Batch {
    u32 begin, end;  // entries
};
const u32 kJobs = 1024;              // batches between two finishJobs at most
const u32 kEntries = 16384;          // draws ...
const u32 kSnapshots = 2048;         // formats and arrays ...
const size_t kJobBytes = 1u << 20;   // vertex bytes of FIFO draws kept for their batches (the stream moves on)
const u32 kBatchVertices = 384;      // a batch is handed out from this many vertices on
const u32 kBatchEntries = 64;        // ... or draws
const int kMaxWorkers = 3;
static_assert(kMaxWorkers <= 8, "gWorkerTids holds 8");

std::vector<Batch> gJobs;            // a ring; a place is used again after finishJobs
std::vector<DrawEntry> gEntries;     // from 0 again after each finishJobs
std::vector<Snapshot> gSnaps;
std::vector<u8> gJobBytes;
u32 gEntryUsed, gSnapUsed;
size_t gJobBytesUsed;
u32 gBatchBegin, gBatchVertices;     // the batch being filled: its first entry, its vertices
int gSnapVat = -1;                   // the snapshot the last entry used: its format and arrays
u32 gSnapFormat, gSnapArrays;
u32 gJobBase;                        // batches handed out at the last finishJobs
// (each on a cache line of its own: the core thread and the workers write them)
alignas(64) std::atomic<u32> gJobsTail(0);     // batches handed out
alignas(64) std::atomic<u32> gJobsNext(0);     // batches taken
alignas(64) std::atomic<u32> gJobsDone(0);     // batches converted
alignas(64) std::atomic<u32> gJobsSkipped(0);  // their vertices with the skip index
alignas(64) std::atomic<int> gSleepers(0);
alignas(64) std::atomic<int> gLooking(0);      // workers awake without a batch (handOut wakes none then)
std::mutex gJobMutex;
std::condition_variable gJobCv;

bool jobWaiting()
{
    return (int32_t) (gJobsTail.load() - gJobsNext.load()) > 0;
}

// Takes one batch and converts its draws; false when none is waiting.
bool runOneJob(VertexBlock& blk)
{
    u32 n = gJobsNext.load();
    for (;;) {
        if ((int32_t) (gJobsTail.load() - n) <= 0) {
            return false;
        }
        if (gJobsNext.compare_exchange_weak(n, n + 1)) {
            break;
        }
    }
    const Batch b = gJobs[n % kJobs];
    const auto start = std::chrono::steady_clock::now();
    u32 skipped = 0;
    for (u32 e = b.begin; e < b.end; e++) {
        const DrawEntry& d = gEntries[e];
        skipped += convertVertices(d.snap->f, d.snap->arrays, d.in, d.count, d.be, d.out, blk);
        writeIndices(d.kind, d.count, d.o, d.ix);
        if (gJobCheck && hashBytes(d.out, (size_t) d.count * d.snap->f.stride, 0) != d.result) {
            re4_log("GX job check: a draw of %u vertices came out differently than when it came (entry %u): "
                    "memory it reads changed meanwhile",
                    d.count, e);
        }
    }
    if (skipped) {
        gJobsSkipped.fetch_add(skipped);
    }
    gJobMicros.fetch_add((u32) std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count());
    gJobsDone.fetch_add(1);
    return true;
}

inline void cpuRelax()
{
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

void worker()
{
    pthread_setname_np(pthread_self(), "re4-gx-verts");  // in profiles (simpleperf) and crash reports
    const int slot = gWorkerTidCount.fetch_add(1);
    if (slot < 8) {
        gWorkerTids[slot].store(re4cpu::ThreadId());
    }
    VertexBlock* blk = new VertexBlock;
    for (;;) {
        if (runOneJob(*blk)) {
            continue;
        }
        // the batches of a picture come in bursts, a wake-up costs more than a batch: awake for
        // 0.3 ms before sleeping
        gLooking++;
        bool found = false;
        const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(300);
        while (!found) {
            for (int i = 0; i < 256 && !found; i++) {
                cpuRelax();
                found = jobWaiting();
            }
            if (!found && std::chrono::steady_clock::now() > until) {
                break;
            }
        }
        gLooking--;
        if (found) {
            continue;
        }
        std::unique_lock<std::mutex> lock(gJobMutex);
        gSleepers++;
        gJobCv.wait(lock, [] { return jobWaiting(); });
        gSleepers--;
    }
}

void startWorkers(int want)
{
    if (gJobs.empty()) {
        gJobs.resize(kJobs);
        gEntries.resize(kEntries);
        gSnaps.resize(kSnapshots);
        gJobBytes.resize(kJobBytes);
    }
    want = std::min(want, kMaxWorkers);
    while (gWorkers < want) {
        std::thread(worker).detach();
        gWorkers++;
    }
}

// The workers wanted: RE4_GX_THREADS, else the cores minus 3 (at most 3).
int defaultWorkers()
{
    const char* env = getenv("RE4_GX_THREADS");
    const int want = env && *env ? atoi(env) : (int) std::thread::hardware_concurrency() - 3;
    return std::max(0, std::min(want, kMaxWorkers));
}

// The workers the first time a draw needs them.
void decideWorkers()
{
    gWorkers = 0;
    const int want = defaultWorkers();
    if (want > 0) {
        startWorkers(want);
    }
    gJobsOn = want > 0;
    const char* check = getenv("RE4_GX_JOB_CHECK");
    gJobCheck = check && *check == '1';
    re4_log("GX: the vertices of the draws converted on %d more threads (%u cores)", want,
            std::thread::hardware_concurrency());
}

// Waits until the batches handed out are converted (the core thread takes some itself).
void waitHandedOut()
{
    const u32 tail = gJobsTail.load();
    if (tail == gJobBase) {
        return;
    }
    while (runOneJob(gBlock)) {
    }
    while (gJobsDone.load() != tail) {
        std::this_thread::yield();
    }
    gJobBase = tail;
    gStats.skipped += gJobsSkipped.exchange(0);
}

// Hands out the batch being filled.
void handOut()
{
    if (gBatchBegin == gEntryUsed) {
        return;
    }
    if (gJobsTail.load() - gJobBase >= kJobs) {
        waitHandedOut();  // the ring is full
    }
    const u32 tail = gJobsTail.load();
    gJobs[tail % kJobs] = Batch{gBatchBegin, gEntryUsed};
    gJobsTail.store(tail + 1);
    gBatchBegin = gEntryUsed;
    gBatchVertices = 0;
    gJobCount++;
    if (gLooking.load() == 0 && gSleepers.load() > 0) {
        std::lock_guard<std::mutex> lock(gJobMutex);
        gJobCv.notify_one();
    }
}

}  // namespace

namespace re4gx {

void finishJobs()
{
    handOut();
    waitHandedOut();
    gEntryUsed = gBatchBegin = 0;
    gSnapUsed = 0;
    gSnapVat = -1;
    gJobBytesUsed = 0;
}

}  // namespace re4gx

namespace {

// A draw's vertices as an entry of the batch being filled. False when its bytes do not fit the
// store (converted at once then).
bool addEntry(u32 vat, const Format& f, const DrawArrays& arrays, const u8* data, bool stable, u32 count, bool be,
              u8* out, u16* ix, u32 kind, u32 o)
{
    const u8* in = data;
    if (!stable) {
        const size_t bytes = (size_t) count * f.inSize;
        if (bytes > kJobBytes) {
            return false;
        }
        if (gJobBytesUsed + bytes > kJobBytes) {
            re4gx::finishJobs();
        }
        memcpy(gJobBytes.data() + gJobBytesUsed, data, bytes);
        in = gJobBytes.data() + gJobBytesUsed;
        gJobBytesUsed += bytes;
    }
    if (gEntryUsed == kEntries) {
        re4gx::finishJobs();
        if (!stable) {  // the store began anew
            memcpy(gJobBytes.data(), data, (size_t) count * f.inSize);
            in = gJobBytes.data();
            gJobBytesUsed = (size_t) count * f.inSize;
        }
    }
    if ((int) vat != gSnapVat || gSnapFormat != gFormatSerial || gSnapArrays != gArraySerial) {
        if (gSnapUsed == kSnapshots) {
            re4gx::finishJobs();
            if (!stable) {
                memcpy(gJobBytes.data(), data, (size_t) count * f.inSize);
                in = gJobBytes.data();
                gJobBytesUsed = (size_t) count * f.inSize;
            }
        }
        Snapshot& sn = gSnaps[gSnapUsed++];
        sn.f = f;
        sn.arrays = arrays;
        gSnapVat = (int) vat;
        gSnapFormat = gFormatSerial;
        gSnapArrays = gArraySerial;
    }
    DrawEntry& e = gEntries[gEntryUsed++];
    e.snap = &gSnaps[gSnapUsed - 1];
    e.in = in;
    e.out = out;
    e.ix = ix;
    e.count = count;
    e.kind = kind;
    e.o = o;
    e.be = be;
    if (gJobCheck) {
        std::vector<u8> now((size_t) count * f.stride);
        convertVertices(f, arrays, in, count, be, now.data(), gBlock);
        e.result = hashBytes(now.data(), now.size(), 0);
    }
    gBatchVertices += count;
    if (gBatchVertices >= kBatchVertices || gEntryUsed - gBatchBegin >= kBatchEntries) {
        handOut();
    }
    return true;
}

// Decodes `count` vertices at `data` into the chunk, appends the indices. `stable`: the bytes
// stay where they are (a display list; the FIFO's stream moves on).
void draw(u8 op, u32 count, const u8* data, bool be, bool stable)
{
    gStats.draws++;
    gStats.vertices += count;
    gFifoVertices += count;
    Chunk* c = chunk();
    if (!c || count == 0) {
        return;
    }
    if (gWorkers < 0) {
        decideWorkers();
    }
    u32 vat = op & 7;
    const Format& f = format(vat);
    // the arrays this format reads
    static const Array kNone = {nullptr, 0, 0};
    arraysCurrent();
    DrawArrays arrays;
    arrays.a[0] = f.posMode >= 2 ? array(0) : kNone;
    arrays.a[1] = f.nrmMode >= 2 ? array(1) : kNone;
    arrays.a[2] = f.clrMode[0] >= 2 ? array(2) : kNone;
    arrays.a[3] = f.clrMode[1] >= 2 ? array(3) : kNone;
    for (u32 i = 0; i < 8; i++) {
        arrays.a[4 + i] = f.texMode[i] >= 2 ? array(4 + i) : kNone;
    }
    // the compact vertex (RE4_GX_COMPACT): its format before the draws that use it
    if (f.compact && (!c->compactSent || memcmp(c->compactWords, f.cWords, sizeof(f.cWords)) != 0)) {
        memcpy(c->compactWords, f.cWords, sizeof(f.cWords));
        c->compactSent = true;
        emit(RE4_GX_OP_COMPACT_FORMAT, f.cWords, RE4_GX_COMPACT_ATTRS);
    }
    // the place of the vertices and indices in the chunk (batches write there: they must not move)
    const u32 base = (u32) c->vertices.size();
    const size_t vertexEnd = base + (size_t) count * f.stride;
    const u32 kind = op & 0xF8;
    std::vector<u16, NoInit<u16>>& ix = c->indices;
    const u32 first = (u32) ix.size();
    const size_t added = indexCount(kind, count);
    if (vertexEnd > c->vertices.capacity() || first + added > ix.capacity()) {
        re4gx::finishJobs();
        c->vertices.reserve(std::max(vertexEnd, c->vertices.capacity() * 2));
        ix.reserve(std::max(first + added, ix.capacity() * 2));
    }
    c->vertices.resize(vertexEnd);
    ix.resize(first + added);

    // the draw: a draw right after another one (no state op between them) with the same
    // primitive class, vertex layout and default matrices joins it: one renderer draw instead of
    // one per GXBegin (a forest frame of r100 has about 16 000 GXBegins and 470 such runs). The
    // triangle order stays.
    const u32 primitive = kind == 0xA8 || kind == 0xB0 ? RE4_GX_LINES : kind == 0xB8 ? RE4_GX_POINTS : RE4_GX_TRIANGLES;
    Re4GxDraw d;
    d.primitive = primitive | (u32) (op & 0xF8) << 8 | vat << 16;
    d.layout = f.layout;
    d.stride = f.stride;
    d.vertexOffset = base;
    d.vertexCount = count;
    d.indexOffset = first;
    d.indexCount = (u32) added;
    d.matrices[0] = f.defaults[0] | f.defaults[1] << 8 | f.defaults[2] << 16 | (u32) f.defaults[3] << 24;
    d.matrices[1] = f.defaults[4] | f.defaults[5] << 8 | f.defaults[6] << 16 | (u32) f.defaults[7] << 24;
    d.matrices[2] = f.defaults[8];
    const u32 words = sizeof(Re4GxDraw) / 4;
    Re4GxDraw last;
    bool join = false;
    if (c->lastDrawEnd && c->ops.size() == c->lastDrawEnd) {
        memcpy(&last, &c->ops[c->lastDrawEnd - words], sizeof(last));
        join = (last.primitive & 0xFF) == primitive && last.layout == d.layout && last.stride == d.stride &&
               memcmp(last.matrices, d.matrices, sizeof(d.matrices)) == 0 &&
               last.vertexOffset + last.vertexCount * last.stride == base && last.indexOffset + last.indexCount == first &&
               last.vertexCount + count <= 0x10000;
    }
    // the vertices and the indices (relative to the first vertex of the joined draw): in a batch,
    // or at once
    const u32 o = join ? last.vertexCount : 0;
    u8* out = c->vertices.data() + base;
    u16* w = ix.data() + first;
    if (!gJobsOn || !addEntry(vat, f, arrays, data, stable, count, be, out, w, kind, o)) {
        gStats.skipped += convertVertices(f, arrays, data, count, be, out, gBlock);
        writeIndices(kind, count, o, w);
        gJobInline++;
    }
    c->view.vertexCount += count;
    if (join) {
        last.vertexCount += count;
        last.indexCount += d.indexCount;
        memcpy(&c->ops[c->lastDrawEnd - words], &last, sizeof(last));
        return;
    }
    emit(RE4_GX_OP_DRAW, (const u32*) &d, words);
    c->lastDrawEnd = c->ops.size();
    c->view.draws++;
}

// --- textures -------------------------------------------------------------------

void tileShape(u32 format, u32* w, u32* h, u32* bytes)
{
    *bytes = 32;
    switch (format) {
    case 0:   // I4
    case 8:   // C4
    case 14:  // CMPR
        *w = 8;
        *h = 8;
        break;
    case 1:  // I8
    case 2:  // IA4
    case 9:  // C8
        *w = 8;
        *h = 4;
        break;
    case 6:  // RGBA8: two 32-byte halves
        *w = 4;
        *h = 4;
        *bytes = 64;
        break;
    default:  // IA8, RGB565, RGB5A3, C14X2
        *w = 4;
        *h = 4;
        break;
    }
}

u32 textureBytes(u32 format, u32 width, u32 height, u32 levels)
{
    u32 tw, th, tb, total = 0;
    tileShape(format, &tw, &th, &tb);
    for (u32 l = 0; l < levels; l++) {
        total += ((width + tw - 1) / tw) * ((height + th - 1) / th) * tb;
        width = width > 1 ? width >> 1 : 1;
        height = height > 1 ? height >> 1 : 1;
    }
    return total;
}

u32 mipChain(u32 width, u32 height)
{
    u32 n = 1;
    for (u32 m = width > height ? width : height; m > 1; m >>= 1) {
        n++;
    }
    return n;
}

bool isCi(u32 format)
{
    return format == 8 || format == 9 || format == 10;
}

u32 tlutEntries(u32 format)
{
    return format == 8 ? 16 : format == 9 ? 256 : 16384;
}

u32 mapRegister(u32 map, u32 offset)
{
    return (map < 4 ? 0x80 + map : 0xA0 + (map - 4)) + offset;
}

u32 gLoggedMissing;

// 16 blocks of 32 bytes spread over the texels (the first and the last among
// them), hashed; small textures whole
u64 sampleHash(u32 address, u32 bytes)
{
    const u8* data = re4gx::memory(address, bytes);
    if (!data) {
        return 0;
    }
    if (bytes <= 1024) {
        return hashBytes(data, bytes, 0x53414D50ull);
    }
    u8 picked[16 * 32];
    for (u32 k = 0; k < 16; k++) {
        const u32 at = (u32) ((u64) (bytes - 32) * k / 15) & ~3u;
        memcpy(picked + 32 * k, data + at, 32);
    }
    return hashBytes(picked, sizeof(picked), 0x53414D50ull ^ bytes);
}

// RE4_GX_TEXTURE_CHECK=sampled: a texture of an earlier chunk still has the same
// texels (an EFB copy: the same marker; else the same samples, at most
// kFullEvery chunks after its last full hash)
bool sampledStillSame(const Bound& b)
{
    const u8* marker = re4gx::memory(b.address, 32);
    const bool copy = marker && memcmp(marker, kCopyMagic, 8) == 0;
    if (copy != ((b.flags & RE4_GX_TEX_COPY) != 0)) {
        return false;
    }
    if (copy) {
        u32 serial, check;
        memcpy(&serial, marker + 8, 4);
        memcpy(&check, marker + 12, 4);
        return check == (serial ^ b.address ^ 0x5A5A5A5Au) && b.key == ((u64) serial << 32 | b.address);
    }
    if (b.dataBytes == 0 || gSequence - b.fullAt >= kFullEvery) {
        return false;
    }
    return sampleHash(b.address, b.dataBytes) == b.sample;
}

void bindMap(u32 map)
{
    const u32* bp = gGp.bp;
    u32 mode0 = bp[mapRegister(map, 0x00)];
    u32 mode1 = bp[mapRegister(map, 0x04)];
    u32 image0 = bp[mapRegister(map, 0x08)];
    u32 image3 = bp[mapRegister(map, 0x14)];
    u32 tlut = bp[mapRegister(map, 0x18)];
    Re4GxTexture t;
    memset(&t, 0, sizeof(t));
    t.map = map;
    t.address = (image3 & 0x1FFFFF) << 5;
    t.width = (image0 & 0x3FF) + 1;
    t.height = ((image0 >> 10) & 0x3FF) + 1;
    t.format = (image0 >> 20) & 0xF;
    bool mip = ((mode0 >> 5) & 3) != 0;  // HW min filter with a mip mode
    u32 maxLod = (mode1 >> 8) & 0xFF;
    t.levels = 1;
    if (mip) {
        t.levels = (maxLod + 15) / 16 + 1;
        u32 chain = mipChain(t.width, t.height);
        t.levels = t.levels > chain ? chain : t.levels;
    }
    t.tlut = isCi(t.format) ? tlut & 0xFFF : 0;
    const u32 desc[3] = {t.address, image0 & 0xFFFFFF, t.levels};
    u64 dkey = hashWords(desc, 3);
    auto memo = gTexMemo.find(dkey);
    Bound bound;
    if (memo != gTexMemo.end() && gTexturesSampled && memo->second.checked != gSequence &&
        !sampledStillSame(memo->second)) {
        gTexMemo.erase(memo);
        memo = gTexMemo.end();
    }
    if (memo != gTexMemo.end()) {
        memo->second.checked = gSequence;
        bound = memo->second;
    } else {
        bound.key = 0;
        bound.flags = 0;
        bound.address = t.address;
        bound.dataBytes = textureBytes(t.format, t.width, t.height, t.levels);
        const u8* marker = re4gx::memory(t.address, 32);
        u32 serial = 0;
        if (marker && memcmp(marker, kCopyMagic, 8) == 0) {
            u32 check;
            memcpy(&serial, marker + 8, 4);
            memcpy(&check, marker + 12, 4);
            if (check != (serial ^ t.address ^ 0x5A5A5A5Au)) {
                serial = 0;
            }
        }
        if (serial) {
            bound.flags = RE4_GX_TEX_COPY;
            bound.key = (u64) serial << 32 | t.address;
            bound.dataBytes = 0;
        } else {
            const u8* data = re4gx::memory(t.address, bound.dataBytes);
            if (!data) {
                if (gLoggedMissing++ < 8) {
                    re4_log("GX: texture map %u at 0x%08x (%ux%u format %u) is not in memory", map, t.address,
                            t.width, t.height, t.format);
                }
                bound.dataBytes = 0;
            } else {
                const u32 shape[4] = {t.format, t.width, t.height, t.levels};
                bound.key = hashBytes(data, bound.dataBytes, hashWords(shape, 4)) | 1;
            }
        }
        bound.sample = gTexturesSampled && bound.dataBytes ? sampleHash(t.address, bound.dataBytes) : 0;
        bound.checked = bound.fullAt = gSequence;
        gTexMemo[dkey] = bound;
    }
    if (isCi(t.format) && bound.key != 0 && !(bound.flags & RE4_GX_TEX_COPY)) {
        // the palette as TMEM holds it now (a load may change it any time)
        u32 offset = (t.tlut & 0x3FF) << 9;
        u32 bytes = tlutEntries(t.format) * 2;
        if (offset + bytes > kTlutBytes) {
            bytes = kTlutBytes - offset;
        }
        bound.key ^= hashBytes(gTlut + offset, bytes, (t.tlut >> 10) & 3) & ~1ull;
    }
    if (gMapValid[map] && gMapKey[map] == bound.key && gMapFlags[map] == bound.flags) {
        auto s = gSent.find(bound.key);
        if (s != gSent.end()) {
            s->second.lastChunk = gSequence;
        }
        return;
    }
    gMapValid[map] = true;
    gMapKey[map] = bound.key;
    gMapFlags[map] = bound.flags;
    t.keyLo = (u32) bound.key;
    t.keyHi = (u32) (bound.key >> 32);
    t.flags = bound.flags;
    if (bound.key != 0 && !(bound.flags & RE4_GX_TEX_COPY)) {
        auto s = gSent.find(bound.key);
        if (s == gSent.end()) {
            const u8* data = re4gx::memory(t.address, bound.dataBytes);
            t.dataOffset = blobAppend(data, bound.dataBytes);
            t.dataBytes = bound.dataBytes;
            Sent sent;
            sent.lastChunk = gSequence;
            sent.bytes = bound.dataBytes;
            sent.decoded = 0;
            for (u32 l = 0, w = t.width, h = t.height; l < t.levels; l++) {
                sent.decoded += w * h * 4;
                w = w > 1 ? w >> 1 : 1;
                h = h > 1 ? h >> 1 : 1;
            }
            gSent[bound.key] = sent;
            gSentBytes += bound.dataBytes;
            gDecodedBytes += sent.decoded;
            gStats.textures++;
            gStats.textureBytes += bound.dataBytes;
            chunk()->view.textureBytes += bound.dataBytes;
        } else {
            s->second.lastChunk = gSequence;
        }
    }
    emit(RE4_GX_OP_TEXTURE, (const u32*) &t, sizeof(t) / 4);
}

// The texture maps the TEV stages and the indirect stages of the next draw
// read (BP genmode, TEV orders 0x28-0x2F, RAS1_IREF 0x27). Nothing to do while
// none of that changed since the last draw (gTexturesDirty): the same maps
// with the same keys (bindMap per draw was a few percent of the core's time).
void bindTextures()
{
    if (!gTexturesDirty) {
        return;
    }
    gTexturesDirty = false;
    const u32* bp = gGp.bp;
    u32 genmode = bp[0x00];
    u32 used = 0;
    u32 stages = ((genmode >> 10) & 15) + 1;
    for (u32 s = 0; s < stages; s++) {
        u32 word = bp[0x28 + s / 2];
        u32 shift = (s & 1) ? 12 : 0;
        if ((word >> (shift + 6)) & 1) {
            used |= 1u << ((word >> shift) & 7);
        }
    }
    u32 indirect = (genmode >> 16) & 7;
    for (u32 k = 0; k < indirect && k < 4; k++) {
        used |= 1u << ((bp[0x27] >> (6 * k)) & 7);
    }
    for (u32 map = 0; map < 8; map++) {
        if (used & (1u << map)) {
            bindMap(map);
        }
    }
}

// --- registers -------------------------------------------------------------------

void cpWrite(u8 reg, u32 value)
{
    gStats.cp++;
    if (reg == 0x30 || reg == 0x40 || reg == 0x50 || reg == 0x60) {
        memset(gFormatValid, 0, sizeof(gFormatValid));  // every format reads these
    } else if (reg >= 0x70 && reg < 0xA0) {
        gFormatValid[reg & 7] = false;
    }
    if (reg == 0x30) {
        gGp.matIndexA = value;
    } else if (reg == 0x40) {
        gGp.matIndexB = value;
    } else if (reg == 0x50) {
        gGp.vcdLo = value;
    } else if (reg == 0x60) {
        gGp.vcdHi = value;
    } else if ((reg & 0xF8) == 0x70) {
        gGp.vatA[reg & 7] = value;
    } else if ((reg & 0xF8) == 0x80) {
        gGp.vatB[reg & 7] = value;
    } else if ((reg & 0xF8) == 0x90) {
        gGp.vatC[reg & 7] = value;
    } else if ((reg & 0xF0) == 0xA0) {
        gGp.arrayBase[reg & 15] = value;
        gArrayValid[reg & 15] = false;
    } else if ((reg & 0xF0) == 0xB0) {
        gGp.arrayStride[reg & 15] = value;
        gArrayValid[reg & 15] = false;
    }
}

u32* xfSlot(u32 address)
{
    if (address < 0x800) {
        return &gGp.xfMem[address];
    }
    if (address >= 0x1000 && address < 0x1058) {
        return &gGp.xfReg[address - 0x1000];
    }
    return nullptr;
}

void xfWrite(u32 address, const u8* data, u32 count, bool be)
{
    bool changed = false;
    if (address + count <= 0x800) {  // XF memory (matrices, lights): the common case
        u32* slot = &gGp.xfMem[address];
        for (u32 i = 0; i < count; i++) {
            u32 v = rd32(data + 4 * i, be);
            changed |= slot[i] != v;
            slot[i] = v;
        }
    } else {
        for (u32 i = 0; i < count; i++) {
            u32 v = rd32(data + 4 * i, be);
            u32* slot = xfSlot(address + i);
            if (slot && *slot != v) {
                *slot = v;
                changed = true;
            }
        }
    }
    if (changed && chunk()) {
        Chunk* c = chunk();
        const size_t at = c->ops.size();
        c->ops.resize(at + 2 + count);
        u32* w = c->ops.data() + at;
        w[0] = RE4_GX_OP_XF | (count + 1) << 8;
        w[1] = address;
        for (u32 i = 0; i < count; i++) {
            w[2 + i] = rd32(data + 4 * i, be);
        }
    }
}

// XF loads from the indexed arrays 12..15 (GXLoadPosMtxIndx ...): matrices
// the CPU wrote, host order.
void xfIndexed(u8 op, u32 word)
{
    gStats.xfIndexed++;
    u32 array = 12 + ((op >> 3) & 3);
    u32 index = word >> 16;
    u32 count = ((word >> 12) & 15) + 1;
    u32 address = word & 0xFFF;
    arraysCurrent();
    const u8* src = at(::array(array), index, count * 4);
    if (src) {
        xfWrite(address, src, count, false);
    }
}

void tlutLoad()
{
    gStats.tlutLoads++;
    gTexturesDirty = true;
    u32 source = (gGp.bp[0x64] & 0x1FFFFF) << 5;
    u32 offset = (gGp.bp[0x65] & 0x3FF) << 9;
    u32 bytes = ((gGp.bp[0x65] >> 10) & 0x7FF) * 32;
    if (offset + bytes > kTlutBytes) {
        bytes = kTlutBytes - offset;
    }
    const u8* src = re4gx::memory(source, bytes);
    if (!src || bytes == 0) {
        return;
    }
    memcpy(gTlut + offset, src, bytes);
    for (u32 b = offset >> 9; b < ((offset + bytes + 511) >> 9); b++) {
        gTlutLoaded[b] = true;
    }
    if (chunk()) {
        emitTlutRange(offset, bytes);
    }
}

// 0x5A: not a GameCube register, the port's marker of the app's gameplay
// autofocus (runtime/dof.cpp): sent each time like the registers that act
bool isAction(u8 reg)
{
    return reg == 0x52 || reg == 0x55 || reg == 0x56 || reg == 0x5A || reg == 0x5B || (reg >= 0x60 && reg <= 0x63) ||
           reg == 0x66 || reg == 0x67;
}

void efbCopy(u32 control)
{
    u32 dest = (gGp.bp[0x4B] & 0x1FFFFF) << 5;
    u32 width = (gGp.bp[0x4A] & 0x3FF) + 1;
    u32 height = ((gGp.bp[0x4A] >> 10) & 0x3FF) + 1;
    if (control & (1u << 14)) {
        gStats.xfbCopies++;
        re4_gx_peek_new_frame();  // the next GXPeekZ marks the next picture
        chunkBoundary(dest, width, height);
        return;
    }
    gStats.texCopies++;
    gTexturesDirty = true;
    for (auto it = gTexMemo.begin(); it != gTexMemo.end();) {
        if (it->second.address == dest) {
            it = gTexMemo.erase(it);
        } else {
            ++it;
        }
    }
    u8* marker = re4gx::memory(dest, 32);
    if (marker) {
        // the copy writes memory a draw before it may still read (a vertex job): as on the
        // GameCube, those draws read it first
        re4gx::finishJobs();
        u32 serial = ++gCopySerial;
        u32 check = serial ^ dest ^ 0x5A5A5A5Au;
        memcpy(marker, kCopyMagic, 8);
        memcpy(marker + 8, &serial, 4);
        memcpy(marker + 12, &check, 4);
    }
}

void bpWrite(u32 word)
{
    gStats.bp++;
    u8 reg = (u8) (word >> 24);
    u32 value = word & 0xFFFFFF;
    if (reg == 0xFE) {  // the mask for the next write
        gGp.bpMask = value;
        return;
    }
    u32 old = gGp.bp[reg];
    u32 now = (old & ~gGp.bpMask) | (value & gGp.bpMask);
    gGp.bp[reg] = now;
    gGp.bpMask = 0xFFFFFF;
    // genmode, RAS1_IREF, TEV orders, the texture map registers (0x80-0xBF)
    if (now != old && (reg == 0x00 || (reg >= 0x27 && reg <= 0x2F) || (reg >= 0x80 && reg <= 0xBF))) {
        gTexturesDirty = true;
    }
    switch (reg) {
    case 0x45:  // PE_DONE: everything before it is drawn (the vertex jobs read their memory)
        re4gx::finishJobs();
        re4gx::peFinish();
        return;
    case 0x47:  // PE_TOKEN: the token register, no interrupt
        re4gx::finishJobs();
        re4gx::peToken((u16) now, false);
        return;
    case 0x48:  // PE_TOKEN_INT
        re4gx::finishJobs();
        re4gx::peToken((u16) now, true);
        return;
    case 0x64:  // TLUT source, loaded with 0x65
        return;
    case 0x65:
        tlutLoad();
        return;
    }
    if (chunk() && (now != old || isAction(reg))) {
        const u32 payload = (u32) reg << 24 | now;
        emit(RE4_GX_OP_BP, &payload, 1);
    }
    if (reg == 0x52) {
        efbCopy(now);
    }
}

// --- the command stream ---------------------------------------------------------

size_t command(const u8* p, size_t n, size_t* need, bool be, int depth);

void callDisplayList(u32 address, u32 size, int depth)
{
    gStats.calls++;
    if (depth > 0) {
        return;  // the GP does not nest display lists
    }
    const u8* list = re4gx::listMemory(address, size);
    if (!list) {
        if (gUnknownLogged++ < 8) {
            re4_log("GX: display list at 0x%08x (%u bytes) is not in memory", address, size);
        }
        return;
    }
    gStats.dlBytes += size;
    gListBytes += size;
    bool be = true;
    auto host = gHostLists.find((uintptr_t) list);
    if (host != gHostLists.end() && host->second.size == size && hashBytes(list, size, 0) == host->second.hash) {
        be = false;
    }
    size_t at = 0;
    while (at < size) {
        size_t need = 0;
        size_t used = command(list + at, size - at, &need, be, depth + 1);
        if (used == 0) {
            break;  // a command cut off at the end of the list
        }
        at += used;
    }
}

// One command at p (n bytes available): its length, or 0 when it is cut off
// (*need: the bytes it takes).
size_t command(const u8* p, size_t n, size_t* need, bool be, int depth)
{
    *need = 1;
    if (n == 0) {
        return 0;
    }
    u8 op = p[0];
    switch (op) {
    case 0x00:  // NOP
        return 1;
    case 0x08:  // CP register
        *need = 6;
        if (n < 6) {
            return 0;
        }
        gStats.commands++;
        cpWrite(p[1], rd32(p + 2, be));
        return 6;
    case 0x10: {  // XF registers / memory: (count - 1) << 16 | address, then the words
        *need = 5;
        if (n < 5) {
            return 0;
        }
        u32 head = rd32(p + 1, be);
        size_t count = (head >> 16) + 1;
        *need = 5 + 4 * count;
        if (n < *need) {
            return 0;
        }
        gStats.commands++;
        gStats.xf++;
        xfWrite(head & 0xFFFF, p + 5, (u32) count, be);
        return *need;
    }
    case 0x20:
    case 0x28:
    case 0x30:
    case 0x38:  // XF loads from the indexed arrays 12..15
        *need = 5;
        if (n < 5) {
            return 0;
        }
        gStats.commands++;
        xfIndexed(op, rd32(p + 1, be));
        return 5;
    case 0x40:  // call a display list
        *need = 9;
        if (n < 9) {
            return 0;
        }
        gStats.commands++;
        callDisplayList(rd32(p + 1, be), rd32(p + 5, be), depth);
        return 9;
    case 0x02: {  // the port's owner tag (gx_fifo.cpp re4_gx_put_owner, patch 0137)
        *need = 17;
        if (n < 17) {
            return 0;
        }
        const u32 owner[4] = {rd32(p + 1, be), rd32(p + 5, be), rd32(p + 9, be), rd32(p + 13, be)};
        if (chunk()) {
            emit(RE4_GX_OP_OWNER, owner, 4);
        }
        return 17;
    }
    case 0x44:  // metrics
    case 0x48:  // invalidate the vertex cache
        gStats.commands++;
        return 1;
    case 0x61:  // BP register
        *need = 5;
        if (n < 5) {
            return 0;
        }
        gStats.commands++;
        bpWrite(rd32(p + 1, be));
        return 5;
    default:
        break;
    }
    if (op >= 0x80 && op < 0xC0) {  // draw: primitive | vertex format, count, vertices
        *need = 3;
        if (n < 3) {
            return 0;
        }
        u32 count = rd16(p + 1, be);
        *need = 3 + (size_t) count * vertexSize(op & 7);
        if (n < *need) {
            return 0;
        }
        gStats.commands++;
        if (chunk()) {
            bindTextures();
        }
        draw(op, count, p + 3, be, depth > 0);
        return *need;
    }
    if (depth == 0) {
        logRecent(p, n);
    }
    if (gUnknownLogged++ < 8) {
        re4_log("GX: unknown command 0x%02x, skipped", op);
    }
    gStats.unknown++;
    return 1;
}

// The GP's input from the FIFO: worked through as soon as a command is
// complete.
std::vector<u8> gStream;
size_t gRead;
size_t gNeed = 1;

}  // namespace

namespace re4gx {

void drain()
{
    for (;;) {
        size_t need = 1;
        const size_t avail = gStream.size() - gRead;
        const u8 op = avail ? gStream[gRead] : 0;
        const bool isDraw = op >= 0x80 && op < 0xC0 && avail >= 3;
        const u32 count = isDraw ? rd16(gStream.data() + gRead + 1, true) : 0;
        size_t used = command(gStream.data() + gRead, avail, &need, true, 0);
        if (used == 0) {
            gNeed = need;
            break;
        }
        remember(op, used, count, isDraw ? vertexSize(op & 7) : 0);
        gRead += used;
    }
    if (gRead == gStream.size()) {
        gStream.clear();
        gRead = 0;
    } else if (gRead >= 4096) {
        gStream.erase(gStream.begin(), gStream.begin() + (long) gRead);
        gRead = 0;
    }
}

void feed(const uint8_t* bytes, size_t n)
{
    gStats.bytes += (u32) n;
    gFifoBytes += (double) n;
    gStream.insert(gStream.end(), bytes, bytes + n);
    if (gStream.size() - gRead >= gNeed) {
        drain();
    }
}

}  // namespace re4gx

extern "C" {

// The app menu's "Force Performance" (core_api.cpp re4_core_set_fast_cores): 1 the game thread and
// the vertex threads on the fast cores with Android's performance hint, 0 left to Android.
void re4_gx_set_fast_cores(int on)
{
    gFastWanted.store(on ? 1 : 0);
}

void re4_gx_mark(void)
{
    // the chunk being recorded carries the mark; the next one starts over, so
    // that the app can keep it (and those after it) as files of their own
    if (Chunk* c = chunk()) {
        c->view.flags |= RE4_GX_CHUNK_MARK;
    }
    gNeedReset = true;
}

void re4_gx_still(int on)
{
    gStill.store(on != 0);
}

void re4_gx_host_order_list(const void* list, unsigned int size)
{
    HostList h;
    h.size = size;
    h.hash = hashBytes((const u8*) list, size, 0);
    gHostLists[(uintptr_t) list] = h;
    gStats.hostLists++;
}

const Re4GxChunk* re4_core_gx_acquire(void)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gLastPoll.store(nowMs());
    gWanted.store(true);
    if (gReady.empty()) {
        return nullptr;
    }
    int slot = gReady.front();
    gReady.pop_front();
    gSlotState[slot] = kReading;
    return &gSlots[slot].view;
}

void re4_core_gx_release(const Re4GxChunk* chunk)
{
    std::lock_guard<std::mutex> lock(gMutex);
    for (int i = 0; i < kSlots; i++) {
        if (&gSlots[i].view == chunk && gSlotState[i] == kReading) {
            gSlotState[i] = kFree;
        }
    }
    gCv.notify_all();
}

// VI (runtime/vi.cpp): the XFB the game switches the picture to
// (VISetNextFrameBuffer, virtual or physical address) ...
void re4_gx_vi_next(const void* fb)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gViNext = (u32) (uintptr_t) fb & 0x03FFFFE0;  // as BP 0x4B holds a copy destination
}

// VISetBlack as VIFlush applies it (vi.cpp): the pictures from now on are
// black on screen until it is lifted
void re4_gx_vi_black(int black)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gViBlack = black != 0;
}

// ... and VIFlush, which ends the game's frame (Render_swap): the display copy
// made before it is shown, or held when the VI stays on the other XFB.
void re4_gx_vi_flush(void)
{
    re4gx::drain();
    std::lock_guard<std::mutex> lock(gMutex);
    publishPendingLocked();
}

void re4_core_gx_notify(void (*notify)(void))
{
    gNotify.store(notify);
}

void re4_core_gx_detach(void)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gWanted.store(false);
    dropQueueLocked();
    gCv.notify_all();
}

// Tests (gx_probe.cpp): the GP done with what came so far, as after GXDrawDone (the test then
// writes memory a draw before read).
void re4_gx_wait_jobs(void)
{
    re4gx::drain();
    re4gx::finishJobs();
}

// Tests (runtime_tests.cpp): the vertex jobs on `n` workers (started as needed, at most 3), none
// (0: every draw on the core thread) or as by default (-1). Returns the workers running.
int re4_gx_threads(int n)
{
    re4gx::finishJobs();
    if (gWorkers < 0) {
        gWorkers = 0;
    }
    if (n < 0) {
        n = defaultWorkers();
    }
    if (n > 0) {
        startWorkers(n);
    }
    gJobsOn = n > 0;
    return gWorkers;
}

// Tests (runtime_tests.cpp, gx_probe.cpp): what the GP has seen.
void re4_gx_stats(unsigned out[11])
{
    re4gx::drain();
    re4gx::finishJobs();
    const u32 v[11] = {gStats.bytes, gStats.commands, gStats.bp, gStats.cp, gStats.xf, gStats.xfIndexed,
                       gStats.draws, gStats.vertices, gStats.calls, gStats.unknown, gStats.dlBytes};
    memcpy(out, v, sizeof(v));
}

// More counters: xfb copies, texture copies, TLUT loads, textures sent, texture
// bytes sent, chunks, chunks dropped, host-order lists registered, skipped
// vertices.
void re4_gx_stats2(unsigned out[9])
{
    re4gx::drain();
    re4gx::finishJobs();
    const u32 v[9] = {gStats.xfbCopies, gStats.texCopies, gStats.tlutLoads, gStats.textures, gStats.textureBytes,
                      gStats.chunks, gStats.dropped, gStats.hostLists, gStats.skipped};
    memcpy(out, v, sizeof(v));
}

// RE4_GX_TEXTURE_CHECK at run time (tests): 1 sampled, 0 every chunk, -1 only
// asks; returns the mode before
int re4_gx_texture_check(int sampled)
{
    re4gx::drain();
    const int was = gTexturesSampled ? 1 : 0;
    if (sampled >= 0) {
        gTexturesSampled = sampled != 0;
        gTexMemo.clear();
    }
    return was;
}

// RE4_GX_COMPACT at run time (tests): 1 the compact vertex, 0 the float vertex,
// -1 only asks; returns the mode before
int re4_gx_compact(int on)
{
    re4gx::drain();
    const int was = gCompact ? 1 : 0;
    if (on >= 0 && (on != 0) != gCompact) {
        re4gx::finishJobs();  // batches of the other form are converted first
        gCompact = on != 0;
        memset(gFormatValid, 0, sizeof(gFormatValid));
        if (Chunk* c = chunk()) {
            c->compactSent = false;
        }
    }
    return was;
}

unsigned re4_gx_bp(unsigned reg)
{
    re4gx::drain();
    return gGp.bp[reg & 0xFF];
}

unsigned re4_gx_xf(unsigned address)
{
    re4gx::drain();
    u32* slot = xfSlot(address);
    return slot ? *slot : 0;
}

}  // extern "C"
