// Dolphin OS threads and semaphores on one host thread.
//
// The GameCube runs one CPU with a strict priority scheduler: the ready thread
// with the highest priority (lowest number) runs until it blocks, or until a
// thread with a higher priority becomes ready. The game's task scheduler
// (src/game/scheduler.cpp) is built on exactly that: every task is an
// OSThread of priority 15 that the main thread (16) resumes once per frame and
// that hands control back by sleeping on its queue.
//
// The port keeps these semantics with stackful contexts
// (port/src/stack_context.cpp, reused from the earlier port): every OSThread
// gets a host stack, and switches happen only inside the OS calls below. A
// dispatcher on the launcher's stack (re4_os_run) resumes the chosen thread;
// a thread that has to give up the CPU returns to the dispatcher.
//
// Game-visible state follows the SDK: OSThread.state (READY 1, RUNNING 2,
// WAITING 4, MORIBUND 8; iTaskSuspend reads it), the suspend count, the
// priority, the queue links and the stack magic 0xDEADBABE at the bottom of
// the stack the game passes (StackOverflowCheck reads it). The game's stack
// memory itself is not used: native frames need more room than the 6-12 KB
// the original tasks had.
#include "re4_runtime.h"
#include "stack_context.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <link.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#if !defined(__ANDROID__)
#include <execinfo.h>
#define RE4_WAIT_TRACE_SUPPORTED 1
#endif

#include <memory>
#include <unordered_map>
#include <vector>

namespace {

typedef unsigned u32;
typedef int s32;
typedef unsigned short u16;

struct OSThread;

struct OSThreadQueue {
    OSThread* head;
    OSThread* tail;
};

struct OSThreadLink {
    OSThread* next;
    OSThread* prev;
};

// include/dolphin/os/OSThread.h
struct OSThread {
    unsigned char context[0x2C8];  // OSContext: PPC registers, unused by the port
    u16 state;
    u16 attr;
    s32 suspend;
    s32 priority;
    s32 base;
    void* val;
    OSThreadQueue* queue;
    OSThreadLink link;
    OSThreadQueue queueJoin;
    void* mutex;
    void* queueMutex[2];
    OSThreadLink linkActive;
    unsigned char* stackBase;
    u32* stackEnd;
    s32 error;
    void* specific[2];
};

// the SDK's form (include/dolphin/os/OSThread.h), which the game allocates:
// the GameCube's on ILP32, pointers of 8 bytes on LP64 (milestone 12)
static_assert(offsetof(OSThread, state) == 0x2C8, "OSThread layout");
static_assert(offsetof(OSThread, queue) == (sizeof(void*) == 4 ? 0x2DC : 0x2E0), "OSThread layout");
static_assert(offsetof(OSThread, stackBase) == (sizeof(void*) == 4 ? 0x304 : 0x330), "OSThread layout");
static_assert(sizeof(OSThread) == (sizeof(void*) == 4 ? 0x318 : 0x358), "OSThread layout");

struct OSSemaphore {
    s32 count;
    OSThreadQueue queue;
};

enum {
    kReady = 1,
    kRunning = 2,
    kWaiting = 4,
    kMoribund = 8,
};

const u16 kAttrDetach = 1;
const u32 kStackMagic = 0xDEADBABE;
const s32 kMainPriority = 16;  // OSInit gives the boot thread priority 16
const size_t kMainStack = 8 << 20;
const size_t kThreadStack = 1 << 20;

using re4::port::detail::StackContext;

struct Host {
    OSThread* thread;
    void* (*func)(void*);
    void* param;
    std::unique_ptr<StackContext> context;
    long long readySeq;  // FIFO order among equal priorities
    bool finished;
    // RE4_WAIT_TRACE=1: where the thread last gave up the CPU (re4_os_dump_threads)
    void* waitFrames[24];
    int waitDepth;
};

OSThread gMainThread;
OSThread* gCurrent = &gMainThread;

// The boot thread exists before re4_os_run (OSGetCurrentThread in OSInit, ...).
struct MainThreadInit {
    MainThreadInit()
    {
        gMainThread.state = kRunning;
        gMainThread.priority = gMainThread.base = kMainPriority;
    }
} gMainThreadInit;
std::unordered_map<OSThread*, std::unique_ptr<Host>> gHosts;
std::vector<std::unique_ptr<StackContext>> gGraveyard;  // freed by the dispatcher
long long gSeq;
long long gFrontSeq;
bool gDispatching;
int gSchedulerDisabled;
int gInInterrupt;
bool gRescheduleDeferred;
int (*gMainEntry)(void);
int gMainResult;

Host* hostOf(OSThread* t)
{
    auto it = gHosts.find(t);
    return it == gHosts.end() ? 0 : it->second.get();
}

bool runnable(OSThread* t)
{
    return (t->state == kReady || t->state == kRunning) && t->suspend <= 0;
}

// The ready thread that should run: highest priority, then longest ready.
OSThread* pickReady()
{
    OSThread* best = 0;
    long long bestSeq = 0;
    for (auto& entry : gHosts) {
        OSThread* t = entry.first;
        Host* h = entry.second.get();
        if (h->finished || !runnable(t)) {
            continue;
        }
        if (!best || t->priority < best->priority || (t->priority == best->priority && h->readySeq < bestSeq)) {
            best = t;
            bestSeq = h->readySeq;
        }
    }
    return best;
}

void makeReady(OSThread* t)
{
    t->state = kReady;
    if (Host* h = hostOf(t)) {
        h->readySeq = gSeq++;
    }
}

void enqueue(OSThreadQueue* q, OSThread* t)
{
    // By priority, FIFO among equals (OSThread.c EnqueuePrio).
    OSThread* after = q->tail;
    while (after && after->priority > t->priority) {
        after = after->link.prev;
    }
    t->link.prev = after;
    t->link.next = after ? after->link.next : q->head;
    if (t->link.next) {
        t->link.next->link.prev = t;
    } else {
        q->tail = t;
    }
    if (after) {
        after->link.next = t;
    } else {
        q->head = t;
    }
    t->queue = q;
}

void dequeue(OSThread* t)
{
    OSThreadQueue* q = t->queue;
    if (!q) {
        return;
    }
    if (t->link.next) {
        t->link.next->link.prev = t->link.prev;
    } else {
        q->tail = t->link.prev;
    }
    if (t->link.prev) {
        t->link.prev->link.next = t->link.next;
    } else {
        q->head = t->link.next;
    }
    t->link.next = t->link.prev = 0;
    t->queue = 0;
}

const bool gWaitTrace = [] {
    const char* v = getenv("RE4_WAIT_TRACE");
    return v && *v && *v != '0';
}();

// Gives the CPU to the dispatcher; returns when this thread runs again.
void switchAway()
{
    Host* h = hostOf(gCurrent);
    if (!h || !h->context || !gDispatching) {
        re4_log("OS thread: a switch outside re4_os_run (thread %p)", (void*) gCurrent);
        abort();
    }
#ifdef RE4_WAIT_TRACE_SUPPORTED
    if (gWaitTrace) {
        h->waitDepth = backtrace(h->waitFrames, 24);
    }
#endif
    h->context->yield();
}

// After any change of thread states: switch if the current thread cannot go
// on, or if a ready thread has a higher priority.
void reschedule()
{
    if (!gDispatching) {
        return;
    }
    if (gInInterrupt) {
        gRescheduleDeferred = true;  // as the OS does at the end of an interrupt
        return;
    }
    OSThread* cur = gCurrent;
    bool mustSwitch = !runnable(cur) || hostOf(cur)->finished;
    if (!mustSwitch) {
        if (gSchedulerDisabled) {
            return;
        }
        OSThread* best = pickReady();
        if (!best || best == cur || best->priority >= cur->priority) {
            return;
        }
        // Preempted: first in line among its priority when it is ready again.
        cur->state = kReady;
        hostOf(cur)->readySeq = --gFrontSeq;
    }
    switchAway();
}

void threadEntry(void* arg)
{
    Host* h = static_cast<Host*>(arg);
    void* result = h->func(h->param);
    re4_os_exit_current(result);
}

void mainEntry(void*)
{
    gMainResult = gMainEntry();
    re4_os_exit_current(0);
}

void releaseContext(Host* h)
{
    if (h->context) {
        if (h->thread == gCurrent && gDispatching) {
            gGraveyard.push_back(std::move(h->context));
        } else {
            h->context.reset();
        }
    }
}

}  // namespace

extern "C" {

// Every OS thread with its state and, with RE4_WAIT_TRACE=1, the place where
// it last gave up the CPU (host/main.cpp calls it on SIGUSR1: hang search).
void re4_os_dump_threads(int fd)
{
    char line[160];
    for (auto& entry : gHosts) {
        OSThread* t = entry.first;
        Host* h = entry.second.get();
        int n = snprintf(line, sizeof(line), "[re4] OS thread %p%s: priority %d, state %u, suspend %d%s, waits at:\n",
                         (void*) t, t == &gMainThread ? " (main)" : "", (int) t->priority, (unsigned) t->state,
                         (int) t->suspend, h->finished ? ", finished" : "");
        if (write(fd, line, n) < 0) {
            return;
        }
#ifdef RE4_WAIT_TRACE_SUPPORTED
        if (h->waitDepth > 0) {
            backtrace_symbols_fd(h->waitFrames, h->waitDepth, fd);
        } else {
            const char* none = "  (no wait recorded; RE4_WAIT_TRACE=1 records it)\n";
            if (write(fd, none, strlen(none)) < 0) {
                return;
            }
        }
#endif
    }
}

// Ends the running thread (OSExitThread, or its function returned).
void re4_os_exit_current(void* val)
{
    OSThread* t = gCurrent;
    Host* h = hostOf(t);
    t->val = val;
    t->state = (t->attr & kAttrDetach) ? 0 : kMoribund;
    if (h) {
        h->finished = true;
    }
    // Joiners (OSJoinThread) wait on queueJoin.
    for (OSThread* w = t->queueJoin.head; w;) {
        OSThread* next = w->link.next;
        w->link.next = w->link.prev = 0;
        w->queue = 0;
        makeReady(w);
        w = next;
    }
    t->queueJoin.head = t->queueJoin.tail = 0;
    switchAway();
    re4_log("OS thread: an exited thread was resumed");
    abort();
}

// Runs `entry` as the boot thread of the game (priority 16) and dispatches all
// OS threads until it returns.
int re4_os_run(int (*entry)(void))
{
    gMainEntry = entry;
    gMainThread.priority = gMainThread.base = kMainPriority;
    gMainThread.suspend = 0;
    std::unique_ptr<Host> host(new Host());
    host->thread = &gMainThread;
    host->context.reset(new StackContext(kMainStack, mainEntry, 0));
    gHosts[&gMainThread] = std::move(host);
    makeReady(&gMainThread);
    gDispatching = true;
    for (;;) {
        Host* mainHost = hostOf(&gMainThread);
        if (mainHost->finished) {
            break;
        }
        OSThread* next = pickReady();
        if (!next) {
            // Every thread waits: on the GameCube the idle loop runs until an
            // interrupt readies one. Here time advances to the next retrace.
            re4_os_idle();
            continue;
        }
        gCurrent = next;
        next->state = kRunning;
        hostOf(next)->context->resume();
        gGraveyard.clear();
        // Contexts of threads that ended are freed here, off their stacks.
        for (auto& entry : gHosts) {
            if (entry.second->finished && entry.first != &gMainThread) {
                entry.second->context.reset();
            }
        }
    }
    gDispatching = false;
    gCurrent = &gMainThread;
    gMainThread.state = kRunning;
    return gMainResult;
}

// Interrupt delivery (vi.cpp): thread switches requested by callbacks wait
// until the interrupt ends.
void re4_os_interrupt_begin(void)
{
    gInInterrupt++;
}

void re4_os_interrupt_end(void)
{
    if (gInInterrupt == 1) {
        re4_os_run_deferred();
    }
    if (--gInInterrupt == 0 && gRescheduleDeferred) {
        gRescheduleDeferred = false;
        reschedule();
    }
}

int re4_os_in_interrupt(void)
{
    return gInInterrupt > 0;
}

// Completions of DVD reads and ARAM DMAs started inside an interrupt handler
// (dvd.cpp, aram.cpp). On the GameCube their own interrupt comes later, never
// before the handler that started them has run on: the sound driver's stream
// code (snd_str2.cpp) marks a read or DMA busy only after starting it, and
// its completion callback clears the mark. Completing at once inside the
// start call would leave the mark set for good (the stream never gets ready).
namespace {
struct Deferred {
    Re4Deferred fn;
    void* object;
    int value;
};
const unsigned kDeferredMax = 64;
Deferred gDeferred[kDeferredMax];
unsigned gDeferredFirst;
unsigned gDeferredCount;
}  // namespace

int re4_os_defer(Re4Deferred fn, void* object, int value)
{
    if (gDeferredCount == kDeferredMax) {
        static bool logged;
        if (!logged) {
            logged = true;
            re4_log("OS: more than %u completions pending, the next ones finish at once", kDeferredMax);
        }
        return 0;
    }
    gDeferred[(gDeferredFirst + gDeferredCount) % kDeferredMax] = {fn, object, value};
    gDeferredCount++;
    return 1;
}

int re4_os_deferred_set(Re4Deferred fn, void* object, int value)
{
    for (unsigned i = 0; i < gDeferredCount; i++) {
        Deferred& d = gDeferred[(gDeferredFirst + i) % kDeferredMax];
        if (d.fn == fn && d.object == object) {
            d.value = value;
            return 1;
        }
    }
    return 0;
}

// Runs the completions pending now; ones they start in turn wait for the
// next call (the end of the next handler).
void re4_os_run_deferred(void)
{
    for (unsigned n = gDeferredCount; n > 0 && gDeferredCount > 0; n--) {
        Deferred d = gDeferred[gDeferredFirst];
        gDeferredFirst = (gDeferredFirst + 1) % kDeferredMax;
        gDeferredCount--;
        d.fn(d.object, d.value);
    }
}

int re4_os_in_dispatcher(void)
{
    return gDispatching;
}

OSThread* OSGetCurrentThread(void)
{
    return gCurrent;
}

void OSInitThreadQueue(OSThreadQueue* q)
{
    q->head = q->tail = 0;
}

int OSCreateThread(OSThread* t, void* (*func)(void*), void* param, void* stack, u32 stackSize, s32 priority,
                   u16 attr)
{
    if (priority < 0 || priority > 31) {
        return 0;
    }
    if (Host* old = hostOf(t)) {
        releaseContext(old);
    }
    memset(t, 0, sizeof(*t));
    t->state = kReady;
    t->attr = attr & kAttrDetach;
    t->suspend = 1;  // created suspended; OSResumeThread starts it
    t->priority = t->base = priority;
    t->stackBase = (unsigned char*) stack;
    t->stackEnd = (u32*) ((unsigned char*) stack - stackSize);
    *t->stackEnd = kStackMagic;
    std::unique_ptr<Host>& slot = gHosts[t];
    if (!slot) {
        slot.reset(new Host());
    }
    Host* h = slot.get();
    h->thread = t;
    h->func = func;
    h->param = param;
    h->finished = false;
    h->readySeq = gSeq++;
    h->context.reset(new StackContext(kThreadStack, threadEntry, h));
    return 1;
}

s32 OSResumeThread(OSThread* t)
{
    s32 prev = t->suspend;
    if (prev > 0 && --t->suspend == 0 && t->state == kReady) {
        if (Host* h = hostOf(t)) {
            h->readySeq = gSeq++;
        }
        reschedule();
    }
    return prev;
}

s32 OSSuspendThread(OSThread* t)
{
    s32 prev = t->suspend++;
    if (prev == 0 && t == gCurrent && t->state == kRunning) {
        t->state = kReady;
        reschedule();
    }
    return prev;
}

int OSIsThreadSuspended(OSThread* t)
{
    return t->suspend > 0;
}

int OSIsThreadTerminated(OSThread* t)
{
    return t->state == kMoribund || t->state == 0;
}

void OSSleepThread(OSThreadQueue* q)
{
    OSThread* t = gCurrent;
    t->state = kWaiting;
    enqueue(q, t);
    // While the CPU waits the graphics processor catches up (gx_fifo.cpp): its
    // "draw done" interrupt may wake this very thread (GXWaitDrawDone).
    re4_gx_deliver_interrupts();
    // ... and the memory card's transfers end (runtime/exi.cpp)
    re4_exi_deliver();
    if (t->state == kReady) {
        t->state = kRunning;
    }
    reschedule();
}

void OSWakeupThread(OSThreadQueue* q)
{
    while (OSThread* t = q->head) {
        dequeue(t);
        makeReady(t);
    }
    reschedule();
}

void OSExitThread(void* val)
{
    re4_os_exit_current(val);
}

void OSCancelThread(OSThread* t)
{
    if (t == gCurrent) {
        re4_os_exit_current(0);
    }
    if (t->state == kWaiting) {
        dequeue(t);
    }
    t->state = (t->attr & kAttrDetach) ? 0 : kMoribund;
    t->suspend = 0;
    if (Host* h = hostOf(t)) {
        h->finished = true;
        releaseContext(h);
    }
    for (OSThread* w = t->queueJoin.head; w;) {
        OSThread* next = w->link.next;
        w->link.next = w->link.prev = 0;
        w->queue = 0;
        makeReady(w);
        w = next;
    }
    t->queueJoin.head = t->queueJoin.tail = 0;
    reschedule();
}

int OSJoinThread(OSThread* t, void** val)
{
    if (!(t->attr & kAttrDetach) && t->state != kMoribund && t->queueJoin.head == 0) {
        OSSleepThread(&t->queueJoin);
    }
    if (t->state != kMoribund) {
        return 0;
    }
    if (val) {
        *val = t->val;
    }
    t->state = 0;
    return 1;
}

void OSDetachThread(OSThread* t)
{
    t->attr |= kAttrDetach;
    if (t->state == kMoribund) {
        t->state = 0;
    }
}

void OSYieldThread(void)
{
    OSThread* t = gCurrent;
    t->state = kReady;
    if (Host* h = hostOf(t)) {
        h->readySeq = gSeq++;  // behind the others of its priority
    }
    if (gDispatching) {
        switchAway();
    }
}

int OSSetThreadPriority(OSThread* t, s32 priority)
{
    if (priority < 0 || priority > 31) {
        return 0;
    }
    t->priority = t->base = priority;
    reschedule();
    return 1;
}

s32 OSGetThreadPriority(OSThread* t)
{
    return t->base;
}

s32 OSDisableScheduler(void)
{
    return gSchedulerDisabled++;
}

s32 OSEnableScheduler(void)
{
    s32 prev = gSchedulerDisabled;
    if (gSchedulerDisabled > 0) {
        gSchedulerDisabled--;
    }
    return prev;
}

void OSSetThreadSpecific(s32 index, void* ptr)
{
    if (index >= 0 && index < 2) {
        gCurrent->specific[index] = ptr;
    }
}

void* OSGetThreadSpecific(s32 index)
{
    return index >= 0 && index < 2 ? gCurrent->specific[index] : 0;
}

void OSInitSemaphore(OSSemaphore* s, s32 count)
{
    s->count = count;
    OSInitThreadQueue(&s->queue);
}

s32 OSWaitSemaphore(OSSemaphore* s)
{
    while (s->count <= 0) {
        OSSleepThread(&s->queue);
    }
    return s->count--;
}

s32 OSTryWaitSemaphore(OSSemaphore* s)
{
    s32 count = s->count;
    if (count > 0) {
        s->count--;
    }
    return count;
}

s32 OSSignalSemaphore(OSSemaphore* s)
{
    s32 count = s->count++;
    OSWakeupThread(&s->queue);
    return count;
}

s32 OSGetSemaphoreCount(OSSemaphore* s)
{
    return s->count;
}

// VALID_PTR (include/main_mem.h, patch 0037): the game checks pointers
// against MEM1, where on the GameCube its stacks and static data lie too. In
// the port the threads run on host stacks and the static data is in the game
// libraries; both count as valid here. Called only for pointers outside MEM1.
int re4_valid_port_ptr(const void* p)
{
    if (!p) {
        return 0;
    }
    for (auto& entry : gHosts) {
        if (entry.second && entry.second->context && entry.second->context->contains(p)) {
            return 1;
        }
    }
    // the libraries of the game (libre4_main, libre4_<module>): the spans
    // found so far, else the loader's list
    static uintptr_t spans[256][2];
    static int spanCount;
    uintptr_t a = (uintptr_t) p;
    for (int i = 0; i < spanCount; i++) {
        if (a >= spans[i][0] && a < spans[i][1]) {
            return 1;
        }
    }
    struct Search {
        uintptr_t address, begin, end;
        bool found;
    } search = {a, 0, 0, false};
    dl_iterate_phdr(
        [](struct dl_phdr_info* info, size_t, void* data) -> int {
            Search* s = (Search*) data;
            if (!info->dlpi_name || !strstr(info->dlpi_name, "libre4_")) {
                return 0;
            }
            uintptr_t lo = UINTPTR_MAX, hi = 0;
            for (int i = 0; i < info->dlpi_phnum; i++) {
                const ElfW(Phdr)& ph = info->dlpi_phdr[i];
                if (ph.p_type == PT_LOAD) {
                    uintptr_t b = info->dlpi_addr + ph.p_vaddr;
                    if (b < lo) {
                        lo = b;
                    }
                    if (b + ph.p_memsz > hi) {
                        hi = b + ph.p_memsz;
                    }
                }
            }
            if (s->address >= lo && s->address < hi) {
                s->begin = lo;
                s->end = hi;
                s->found = true;
                return 1;
            }
            return 0;
        },
        &search);
    if (!search.found) {
        return 0;
    }
    if (spanCount < 256) {
        spans[spanCount][0] = search.begin;
        spans[spanCount][1] = search.end;
        spanCount++;
    }
    return 1;
}

}  // extern "C"
