// The emulated graphics processor (GP) on a thread of its own, like the
// GameCube's GP next to its CPU (and Dolphin's "dual core"): the bytes the
// game puts into the FIFO go into a ring, a thread of their own feeds them to
// the command decoder (gx_decode.cpp). On a device with several cores the
// decoder's time (over half of the core's time on the OUYA) leaves the game's
// thread. Switched on with RE4_GX_THREAD=1 or re4_gx_threaded(1); off, the
// game's thread decodes each command as it completes (as before).
//
// What stays as it was:
//   - the order: the port's own calls between the bytes (VISetNextFrameBuffer,
//     VISetBlack, VIFlush, re4_gx_mark, re4_gx_host_order_list) go into the
//     ring's queue of events at their place in the stream, the GP thread
//     applies them there;
//   - the GP catches up whenever the game's thread waits or takes the GP's
//     interrupts (OSSleepThread, the VI retrace: re4_gx_deliver_interrupts
//     waits for the GP thread first), and before a test reads its state;
//   - the GP never runs further behind than the ring holds: 512 KB, the game's
//     FIFO is 448 KB (GXInit, main_sub.cpp); with the ring full the game waits,
//     as the GameCube's CPU waits on a full FIFO.
// What changes: the GP reads vertex arrays, display lists and textures a
// little later than the game sent the command, as on the GameCube (the game
// syncs with the GP where it matters: GXDrawDone, draw sync tokens).
//
// The OUYA branch only (21.10.2026: r100 51 instead of 25 frames a second without a picture; master's
// decoder decodes on the game's thread, its vertex jobs take only the vertices). Dropped with the
// merge of master (28.10.2026), set up again on master's decoder 07.11.2026, when the OUYA app
// measured the game's thread at 42 ms a frame in r100, 41 % of it the decoder (port/docs/
// ouya-machbarkeit.md, "Stufe O3"). master's additions: the vertex jobs run from the GP thread (it
// hands out the batches and helps); the game's thread waits for them only after sync() (a display
// list recorded over memory they read, the tests); re4_gx_still (the app's 60 FPS) an event in its
// place. re4_gx_wide and re4_gx_movie stay flags the GP reads at the display copy (the main app's
// picture format "Native" and its "Sharpen", which the OUYA app does not have).
#include "gx_internal.h"
#include "re4_runtime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

namespace re4gx {

bool gThreaded = getenv("RE4_GX_THREAD") && atoi(getenv("RE4_GX_THREAD")) > 0;

}  // namespace re4gx

namespace {

typedef uint8_t u8;
typedef uint64_t u64;

const size_t kRing = 512 * 1024;  // a power of two, more than the game's FIFO (0x70000)
const size_t kPublishEvery = 512;  // bytes put before the GP thread may take them
// A sleeping GP thread is woken for this much (or for an event, a sync, a full
// ring): a wake for every few hundred bytes the game puts cost more than their
// decoding (RE4_GX_BENCH, values put one by one: 1.7 ms instead of 0.4)
const u64 kWakeFor = 16 * 1024;
// Caught up, the GP thread looks for more this long before it sleeps, and
// sleeping it looks again this often: a display list call is 9 bytes but may
// be thousands of vertices of work
const auto kSpinFor = std::chrono::microseconds(100);
const auto kSleepFor = std::chrono::milliseconds(2);

u8* gRing;
std::thread* gThread;
std::atomic<int> gTid{0};

// Each on a cache line of its own: what one thread writes often (the game's
// thread its counters and gHanded, the GP thread gTaken and the lock) would
// otherwise move between the cores with every write of the other (the values
// put one by one in RE4_GX_BENCH: 0.9 ms on the game's thread instead of 0.5;
// the lock lies elsewhere, on the heap)
#define RE4_LINE alignas(64)

// the game's thread
RE4_LINE u64 gPut;  // bytes put
u64 gPublished;     // of them handed to the GP thread
u64 gRoomUntil;     // gPut may grow to this without asking the GP thread

RE4_LINE std::atomic<u64> gHanded{0};  // bytes the GP thread may take
RE4_LINE std::atomic<u64> gPosted{0};  // events posted
RE4_LINE std::atomic<u64> gTaken{0};   // bytes the GP thread took: room in the ring
RE4_LINE std::atomic<bool> gGpWaiting{false};
RE4_LINE std::atomic<bool> gRoomWaiting{false};

// never destroyed: the GP thread may still sleep on them while the process ends
std::mutex& gLock = *new std::mutex;  // the events, the GP thread's sleep, the waits for it
std::condition_variable& gWake = *new std::condition_variable;  // the GP thread: work
std::condition_variable& gIdle = *new std::condition_variable;  // the game's thread: caught up / bytes taken
std::deque<re4gx::Event>& gEvents = *new std::deque<re4gx::Event>;
u64 gDone;            // under gLock: bytes worked through, with the events up to there
bool gBusy = true;    // under gLock: the GP thread is at work
bool gSyncWaiting;    // under gLock: the game's thread waits in sync()

void wakeGp()
{
    if (gGpWaiting.load()) {
        std::lock_guard<std::mutex> lock(gLock);
        gWake.notify_one();
    }
}

// The bytes put so far to the GP thread; `wake`: also when it sleeps on less than kWakeFor
void publish(bool wake)
{
    if (gPublished != gPut) {
        gPublished = gPut;
        gHanded.store(gPut);  // after the bytes and what the game wrote before them
        if (wake || gPut - gTaken.load() >= kWakeFor) {
            wakeGp();
        }
    } else if (wake) {
        wakeGp();
    }
}

inline void relax()
{
#if defined(__i386__) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}

// The bytes from..to of the stream to the decoder, in at most two pieces
void feedRange(u64 from, u64 to)
{
    while (from < to) {
        const size_t at = (size_t) (from & (kRing - 1));
        size_t n = (size_t) (to - from);
        if (n > kRing - at) {
            n = kRing - at;
        }
        re4gx::feed(gRing + at, n);
        from += n;
    }
}

void gpThread()
{
#if defined(__linux__)
    prctl(PR_SET_NAME, "re4-gp", 0, 0, 0);
    gTid.store((int) syscall(SYS_gettid));
#endif
    u64 taken = 0, applied = 0;
    for (;;) {
        // the events at the place reached (the lock only while some wait)
        while (gPosted.load() != applied) {
            re4gx::Event e;
            {
                std::lock_guard<std::mutex> lock(gLock);
                if (gEvents.empty() || gEvents.front().at > taken) {
                    break;
                }
                e = gEvents.front();
                gEvents.pop_front();
            }
            re4gx::apply(e);
            applied++;
        }
        u64 limit = gHanded.load();
        if (gPosted.load() != applied) {
            std::lock_guard<std::mutex> lock(gLock);
            if (!gEvents.empty() && gEvents.front().at < limit) {
                limit = gEvents.front().at;
            }
        }
        if (limit > taken) {
            feedRange(taken, limit);
            taken = limit;
            gTaken.store(taken);
            if (gRoomWaiting.load()) {
                std::lock_guard<std::mutex> lock(gLock);
                gIdle.notify_all();
            }
            continue;
        }
        // nothing to do: caught up. A wait for that (sync()) ends now ...
        {
            std::lock_guard<std::mutex> lock(gLock);
            gDone = taken;
            gBusy = false;
            if (gSyncWaiting) {
                gIdle.notify_all();
            }
        }
        // ... then it looks for more a while (the game is likely putting the
        // next bytes), then it sleeps
        const u64 posted = gPosted.load();
        const auto until = std::chrono::steady_clock::now() + kSpinFor;
        bool more = false;
        for (int spin = 1; !more; spin++) {
            relax();
            more = gHanded.load(std::memory_order_relaxed) != taken || gPosted.load(std::memory_order_relaxed) != posted;
            if (spin % 64 == 0 && std::chrono::steady_clock::now() > until) {
                break;
            }
        }
        std::unique_lock<std::mutex> lock(gLock);
        if (!more) {
            gGpWaiting.store(true);  // before looking again (publish() looks at it after handing bytes)
            if (gHanded.load() == taken && gEvents.empty()) {
                gWake.wait_for(lock, kSleepFor);
            }
            gGpWaiting.store(false);
        }
        gBusy = true;
    }
}

void start()
{
    if (!gThread) {
        gRing = new u8[kRing];
        gThread = new std::thread(gpThread);
        gThread->detach();
    }
}

}  // namespace

namespace re4gx {

void ringPut(const uint8_t* bytes, size_t n)
{
    if (gPut + n > gRoomUntil) {
        start();
        gRoomUntil = gTaken.load() + kRing;
        if (gPut + n > gRoomUntil) {  // full: the GP thread has to take some first
            publish(true);
            std::unique_lock<std::mutex> lock(gLock);
            gRoomWaiting.store(true);
            while ((gRoomUntil = gTaken.load() + kRing) < gPut + n) {
                gWake.notify_one();
                gIdle.wait_for(lock, std::chrono::milliseconds(5));
            }
            gRoomWaiting.store(false);
        }
    }
    const size_t at = (size_t) (gPut & (kRing - 1));
    u8* w = gRing + at;
    if (n == 4 && at + 4 <= kRing) {  // most values (copies of a known size: no call)
        memcpy(w, bytes, 4);
    } else if (n == 1) {
        *w = *bytes;
    } else {
        for (size_t i = 0; i < n; i++) {
            gRing[(gPut + i) & (kRing - 1)] = bytes[i];
        }
    }
    gPut += n;
    if (gPut - gPublished >= kPublishEvery) {
        publish(false);
    }
}

void post(Event e)
{
    start();
    publish(false);
    e.at = gPut;
    std::lock_guard<std::mutex> lock(gLock);
    gEvents.push_back(e);
    gPosted.fetch_add(1);
    gWake.notify_one();
}

void catchUp()
{
    if (gThreaded) {
        sync();
    } else {
        drain();
    }
}

bool busy()
{
    if (!gThread) {
        return false;
    }
    std::lock_guard<std::mutex> lock(gLock);
    return gBusy || gDone != gPut || !gEvents.empty();
}

void sync()
{
    if (!gThread) {
        return;
    }
    publish(true);
    std::unique_lock<std::mutex> lock(gLock);
    gSyncWaiting = true;
    while (gBusy || gDone != gPut || !gEvents.empty()) {
        gWake.notify_one();
        gIdle.wait_for(lock, std::chrono::milliseconds(5));
    }
    gSyncWaiting = false;
}

}  // namespace re4gx

extern "C" {

// on: 1 the GP thread, 0 the game's thread, -1 only asks; returns the mode before
int re4_gx_threaded(int on)
{
    const int was = re4gx::gThreaded ? 1 : 0;
    if (on >= 0 && (on != 0) != re4gx::gThreaded) {
        re4gx::sync();  // the GP thread caught up and waits: the decoder is free
        re4gx::gThreaded = on != 0;
    }
    return was;
}

// Waits until the GP has worked through all the game sent (tests, before they
// take a chunk)
void re4_gx_sync(void)
{
    if (re4gx::gThreaded) {
        re4gx::sync();
    }
}

// The GP thread's thread id (0: none yet), for the probe's measurement of its time
int re4_gx_thread_id(void)
{
    return gTid.load();
}

}  // extern "C"
