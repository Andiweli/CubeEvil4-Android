// The game core in a process of its own (port/docs/32bit-kernprozess.md):
// libre4_coreproc.so in the app's library folder (Android, armeabi-v7a), and
// re4_coreproc next to re4_host (the Linux check re4_core_remote_test).
//
// A 32-bit device whose Java heap lies on MEM1 (ART's "dalvik-main space" at
// 0x80000000, Android 14) cannot hold the core in the app's process. The app
// (core_mode.cpp) then starts this program, which has no Java: it holds MEM1
// at once, loads the core (libre4_main.so) and serves the app's table of core
// calls (runtime/re4_core_client.h) over the shared memory and the socket of
// runtime/re4_core_ipc.h:
//
//   the main thread    commands from the app (start, state, pause, memory
//                      card, log); the app's end (the socket closes) ends this
//                      process too
//   the input thread   controller and vsync queues and the gyro to the core,
//                      the state frames to the app (after each game frame)
//   the GX thread      the core's chunks into the shared slots; a chunk of the
//                      core is released only when the app released its copy,
//                      so as many pictures as in the app's process are on the
//                      way (the same input delay)
//   the sound thread   the core's sound into the shared ring, as much as the
//                      app took and asks for (the core keeps matching the
//                      device's clock); 8 ms kept ahead, a read that needs
//                      more waits for it; each frame as stereo and as the four
//                      channels of Dolby Pro Logic II (re4_core_audio6: the app
//                      plays one of them)
//   a frame hook       rumble, frame counter and the player's free frame into
//                      the shared mirrors, once per game frame (PADRead)
//
//   libre4_coreproc.so --parent <pid>    the socket is file 3
//
// Settings come as the app's environment (the RE4_* switches). Checks:
// RE4_CORE_IPC_CHECK=1 hashes every chunk and state frame (the app compares),
// RE4_CORE_IPC_SEGMENT=N starts the GX memory pieces at N bytes, and on Linux
// RE4_CORE_TEST_ENTRY=remote|crash starts the test entry of libre4_host.so
// (tests/core_remote_probe.cpp) instead of the game.
#include "re4_card_files.h"
#include "re4_core.h"
#include "re4_core_ipc.h"
#include "re4_gx_frame.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <mutex>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <thread>
#include <ucontext.h>
#include <unistd.h>
#include <vector>
#if defined(__ANDROID__)
#include <android/log.h>
#else
#include "low_memory.h"
#endif

using namespace re4ipc;

namespace {

const unsigned long kMem1Start = 0x80000000ul, kMem1Size = 0x3000000ul;

// --- the core's calls (dlsym, as the app's loadCore) ---------------------------

struct Core {
    void* main;
    int (*mem1Init)(void);
    int (*reserve)(const char*, const char*);
    int (*start)(void);
    int (*startEntry)(int (*)(void));
    int (*state)(void);
    const char* (*message)(void);
    void (*pad)(const Re4CorePad*);
    unsigned (*rumble)(void);
    int (*audio6)(int16_t*, int);
    int (*audioSurround)(void);
    void (*audioStats)(unsigned*);
    const Re4CoreSnapshot* (*acquire)(void);
    void (*release)(const Re4CoreSnapshot*);
    int (*logTail)(char*, int);
    const Re4GxChunk* (*gxAcquire)(void);
    void (*gxRelease)(const Re4GxChunk*);
    void (*gxDetach)(void);
    void (*gxNotify)(void (*)(void));
    void (*vsync)(int64_t);
    int (*pause)(int, int);
    int (*instant)(int, Re4InstantInfo*);
    void (*debugMenu)(void);
    void (*setDof)(int);
    void (*setLetterbox)(int);
    void (*setFastCores)(int);
    void (*setPotato)(int);
    void (*setAim)(int);
    void (*setSoundVolume)(int);
    void (*restartGame)(int);
    const char* (*discProblem)(void);
    void (*setCheats)(int);
    int (*cheatAction)(int);
    void (*touch)(float, float);
    void (*aimTurn)(float, float);
    void (*swipe)(int);
    void (*slash)(float);
    void (*gyro)(float, float);
    void (*depthSource)(const unsigned*, const unsigned*);
    int (*cardList)(Re4CardFile*, int);
    int (*cardFree)(void);
    int (*cardName)(int, char*, int);
    int (*cardExport)(int, unsigned char*, unsigned, char*, int);
    int (*cardImport)(const unsigned char*, unsigned, int, char*, int);
    unsigned (*frameCount)(void);
    int (*gameThread)(void);
    long (*freeAt)(void);
    void (*addFrameHook)(void (*)(unsigned));
    void (*log)(const char*, ...);
    void (*logFile)(const char*);
    int (*testEntry)(void);
} core;

std::string gMissing;

template <class F> void bind(F& slot, const char* name, bool needed = true)
{
    slot = reinterpret_cast<F>(dlsym(core.main, name));
    if (!slot && needed && gMissing.empty()) {
        gMissing = name;
    }
}

// Lines into the core's log (logcat "re4" on Android, stderr on Linux, the
// log tail of the app's overlay) once the core is loaded; before that directly.
void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (core.log) {
        core.log("core process: %s", line);
        return;
    }
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "re4", "core process: %s", line);
#else
    fprintf(stderr, "[re4] core process: %s\n", line);
#endif
}

Control* gControl;
std::mutex gSendLock;
bool gCheck;
uint32_t gSegmentStart = 8u << 20;

void reply(uint32_t id, int a, const std::string& text, const void* data = nullptr, size_t dataBytes = 0)
{
    std::vector<char> payload(text.begin(), text.end());
    payload.push_back(0);
    if (dataBytes) {
        payload.insert(payload.end(), (const char*) data, (const char*) data + dataBytes);
    }
    MsgHeader h = {kReply, id, a, (int32_t) (text.size() + 1), 0, (uint32_t) payload.size()};
    std::lock_guard<std::mutex> l(gSendLock);
    sendMsg(kSocketFd, h, payload.data());
}

// --- crashes and hangs (as core_mode.cpp in the app) -------------------------------

std::atomic<int> gCrashFd{-1};
struct sigaction gOldActions[NSIG];
const int kCrashSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

void crashLine(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void crashLine(const char* fmt, ...)
{
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    n = n < (int) sizeof(line) - 2 ? n : (int) sizeof(line) - 2;
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR, "RE4Crash", "%s", line);
#else
    fprintf(stderr, "[re4] %s\n", line);
#endif
    const int fd = gCrashFd.load();
    if (fd >= 0) {
        line[n] = '\n';
        (void) !write(fd, line, (size_t) n + 1);
    }
}

void crashWhere(const char* what, uintptr_t address)
{
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (address && dladdr((void*) address, &info) && info.dli_fname) {
        const char* file = strrchr(info.dli_fname, '/');
        crashLine("  %s 0x%08lx  %s+0x%lx  %s", what, (unsigned long) address, file ? file + 1 : info.dli_fname,
                  (unsigned long) (address - (uintptr_t) info.dli_fbase), info.dli_sname ? info.dli_sname : "");
    } else {
        crashLine("  %s 0x%08lx", what, (unsigned long) address);
    }
}

// pc, lr and the words on the stack that point into a library
void crashContext(void* context)
{
    const ucontext_t* uc = (const ucontext_t*) context;
#if defined(__arm__)
    const uintptr_t pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr, sp = uc->uc_mcontext.arm_sp;
#elif defined(__i386__)
    const uintptr_t pc = (uintptr_t) uc->uc_mcontext.gregs[REG_EIP], lr = 0,
                    sp = (uintptr_t) uc->uc_mcontext.gregs[REG_ESP];
#else
    const uintptr_t pc = 0, lr = 0, sp = 0;
    (void) uc;
#endif
    crashWhere("pc", pc);
    if (lr) {
        crashWhere("lr", lr);
    }
    if (!sp) {
        return;
    }
    // process_vm_readv stops at the first part it cannot read instead of faulting (the system
    // call: the core builds for Android 5.0, the C library has the function from 6.0 on)
    static uintptr_t word[512];
    struct iovec local = {word, sizeof(word)};
    struct iovec remote[16];
    for (int c = 0; c < 16; c++) {
        remote[c].iov_base = (void*) (sp + c * sizeof(word) / 16);
        remote[c].iov_len = sizeof(word) / 16;
    }
    const ssize_t got = (ssize_t) syscall(SYS_process_vm_readv, getpid(), &local, 1, remote, 16, 0);
    const int words = got > 0 ? (int) (got / sizeof(uintptr_t)) : 0;
    int found = 0;
    for (int i = 0; i < words && found < 24; i++) {
        Dl_info d;
        memset(&d, 0, sizeof(d));
        if (word[i] > 0x10000 && dladdr((void*) word[i], &d) && d.dli_fname && strstr(d.dli_fname, ".so")) {
            char what[24];
            snprintf(what, sizeof(what), "sp+%03x", (unsigned) (i * sizeof(uintptr_t)));
            crashWhere(what, word[i]);
            found++;
        }
    }
}

void onCrash(int sig, siginfo_t* info, void* context)
{
    static volatile sig_atomic_t inside = 0;
    if (!inside) {
        inside = 1;
        crashLine("crash (core process): signal %d (%s), fault address %p, thread %d", sig, strsignal(sig),
                  info ? info->si_addr : nullptr, (int) syscall(SYS_gettid));
        crashContext(context);
        const int fd = gCrashFd.load();
        if (fd >= 0) {
            fsync(fd);
        }
    }
    // the handler before (the system's crash report), with the signal again
    for (int s : kCrashSignals) {
        sigaction(s, &gOldActions[s], nullptr);
    }
    if (sig == SIGABRT) {
        raise(sig);
    }
}

void onHangSample(int, siginfo_t*, void* context)
{
    crashLine("hang: the game thread (%d) is at", (int) syscall(SYS_gettid));
    crashContext(context);
}

void installCrashHandler()
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = onCrash;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int s : kCrashSignals) {
        sigaction(s, &action, &gOldActions[s]);
    }
    struct sigaction sample;
    memset(&sample, 0, sizeof(sample));
    sample.sa_sigaction = onHangSample;
    sample.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sample.sa_mask);
    sigaction(SIGUSR2, &sample, nullptr);
}

void reportHang(int seconds)
{
    const int tid = core.gameThread ? core.gameThread() : 0;
    crashLine("hang: no game frame for %d s (frame %u, core process)", seconds,
              core.frameCount ? core.frameCount() : 0u);
    if (tid <= 0) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        syscall(SYS_tgkill, getpid(), tid, SIGUSR2);
        usleep(200000);
    }
    const int fd = gCrashFd.load();
    if (fd >= 0) {
        fsync(fd);
    }
}

// --- the threads ----------------------------------------------------------------

std::atomic<bool> gNewFrame{false};

// the sound's counters and Dolby Pro Logic II for the app (after each game frame and each read of the sound)
void mirrorSound(Control* c)
{
    unsigned a[5] = {0, 0, 0, 0, 0};
    core.audioStats(a);
    for (int i = 0; i < 5; i++) {
        c->audioStats[i].store(a[i]);
    }
    c->audioSurround.store(core.audioSurround() ? 1 : 0);
}

// once per game frame, on the game's thread (PADRead, after the core's own hook)
void frameHook(unsigned)
{
    Control* c = gControl;
    c->frames.store(core.frameCount ? core.frameCount() : 0);
    c->rumble.store(core.rumble());
    c->freeAt.store(core.freeAt ? (int32_t) core.freeAt() : -1);
    gNewFrame.store(true);
    ring(c->inputBell);
}

void inputThread()
{
    Control* c = gControl;
    uint32_t gyroSeen = 0, serial = 0;
    uint32_t back = 0;  // the state frame buffer written here (Control::snapshotMiddle)
    Re4CoreSnapshot* buffers = (Re4CoreSnapshot*) ((char*) c + c->snapshotOffset);
    bool firstPad = true;
    for (;;) {
        const uint32_t seen = c->inputBell.seq.load(std::memory_order_acquire);
        // every controller change in order: a tap shorter than a game frame still reaches the game
        uint32_t tail = c->padTail.load(std::memory_order_relaxed);
        const uint32_t head = c->padHead.load(std::memory_order_acquire);
        for (; tail != head; tail++) {
            const Re4CorePad pad = c->pads[tail % kPadQueue];
            core.pad(&pad);
            c->padsForwarded.fetch_add(1);
            if (firstPad) {
                firstPad = false;
                say("the app's controller arrives");
            }
        }
        c->padTail.store(tail, std::memory_order_release);
        uint32_t vt = c->vsyncTail.load(std::memory_order_relaxed);
        const uint32_t vh = c->vsyncHead.load(std::memory_order_acquire);
        for (; vt != vh; vt++) {
            if (core.vsync) {
                core.vsync(c->vsyncs[vt % kVsyncQueue]);
            }
            c->vsyncsForwarded.fetch_add(1);
        }
        c->vsyncTail.store(vt, std::memory_order_release);
        const uint32_t gs = c->gyroSeq.load(std::memory_order_acquire);
        if (gs != gyroSeen && !(gs & 1)) {
            const uint32_t y = c->gyroYaw.load(std::memory_order_acquire);
            const uint32_t p = c->gyroPitch.load(std::memory_order_acquire);
            if (c->gyroSeq.load(std::memory_order_acquire) == gs) {
                gyroSeen = gs;
                float yaw, pitch;
                memcpy(&yaw, &y, 4);
                memcpy(&pitch, &p, 4);
                if (core.gyro) {
                    core.gyro(yaw, pitch);
                }
                c->gyroForwarded.fetch_add(1);
            }
        }
        if (gNewFrame.exchange(false)) {
            if (const Re4CoreSnapshot* s = core.acquire()) {
                if (s->serial != serial) {
                    serial = s->serial;
                    copySnapshot(buffers[back], *s);
                    if (gCheck) {
                        const uint64_t h = hashSnapshot(*s);
                        c->snapshotHashLo[back] = (uint32_t) h;
                        c->snapshotHashHi[back] = (uint32_t) (h >> 32);
                    }
                    back = c->snapshotMiddle.exchange(back | kSnapshotNew, std::memory_order_acq_rel) & 3;
                    c->snapshotsCopied.fetch_add(1);
                }
                core.release(s);
            }
            mirrorSound(c);
        }
        waitBell(c->inputBell, seen, 100);
    }
}

// the core queued a chunk (re4_core_gx_notify, the core's thread, its queue locked)
void gxNotify(void)
{
    ring(gControl->gxBell);
}

struct Piece {
    char* base = nullptr;
    size_t size = 0;
    uint32_t generation = 0;
};

size_t align64(size_t n)
{
    return (n + 63) & ~(size_t) 63;
}

// One chunk of the core into the free slot `i`; false when there is no
// memory for it (the chunk is then given back).
bool fillSlot(int i, const Re4GxChunk* k, Piece& piece, uint32_t epoch, uint32_t order)
{
    Control* c = gControl;
    GxSlot& s = c->slots[i];
    GxInfo info;
    fillInfo(info, *k);
    const size_t ops = align64(info.opWords * 4u), vertices = align64(info.vertexBytes),
                 indices = align64(info.indexCount * 2u), blob = align64(info.blobBytes);
    const size_t need = ops + vertices + indices + blob;
    if (piece.size < need) {
        // a bigger piece (its pages are taken only where written), sent before the slot is marked ready
        size_t size = need + need / 4;
        size = size > piece.size * 2 ? size : piece.size * 2;
        size = size > gSegmentStart ? size : gSegmentStart;
        size = (size + 0xFFFF) & ~(size_t) 0xFFFF;
        const int fd = shmCreate("re4-gx", size);
        char* base = fd >= 0 ? (char*) shmMap(fd, size) : nullptr;
        if (!base) {
            say("no shared memory of %u bytes for a GX chunk (%s): left out", (unsigned) size, strerror(errno));
            if (fd >= 0) {
                close(fd);
            }
            return false;
        }
        MsgHeader h = {kSegment, 0, i, (int32_t) (piece.generation + 1), (int32_t) size, 0};
        {
            std::lock_guard<std::mutex> l(gSendLock);
            sendMsg(kSocketFd, h, nullptr, fd);
        }
        close(fd);
        if (piece.base) {
            munmap(piece.base, piece.size);
        }
        piece.base = base;
        piece.size = size;
        piece.generation++;
        c->segmentsMade.fetch_add(1);
        if (piece.size >= (16u << 20)) {
            say("GX slot %d: memory piece of %u MB", i, (unsigned) (piece.size >> 20));
        }
    }
    s.opOffset = 0;
    s.vertexOffset = (uint32_t) ops;
    s.indexOffset = (uint32_t) (ops + vertices);
    s.blobOffset = (uint32_t) (ops + vertices + indices);
    memcpy(piece.base + s.opOffset, k->ops, info.opWords * 4u);
    memcpy(piece.base + s.vertexOffset, k->vertices, info.vertexBytes);
    memcpy(piece.base + s.indexOffset, k->indices, info.indexCount * 2u);
    memcpy(piece.base + s.blobOffset, k->blob, info.blobBytes);
    s.info = info;
    s.generation = piece.generation;
    s.epoch = epoch;
    s.order = order;
    if (gCheck) {
        const uint64_t h = hashChunk(info, k->ops, k->vertices, k->indices, k->blob);
        s.hashLo = (uint32_t) h;
        s.hashHi = (uint32_t) (h >> 32);
    }
    s.state.store(kSlotReady, std::memory_order_release);
    c->chunksCopied.fetch_add(1);
    return true;
}

void gxThread()
{
    Control* c = gControl;
    std::deque<const Re4GxChunk*> held;  // taken from the core, not in a slot yet
    const Re4GxChunk* inSlot[kGxSlots] = {};
    Piece pieces[kGxSlots];
    uint32_t epoch = 0, order = 0;
    // what the copies cost (the log every 600 chunks: the price of this process on the device)
    unsigned timed = 0;
    double copyMs = 0, copyMax = 0, copyBytes = 0;
    for (;;) {
        const uint32_t seen = c->gxBell.seq.load(std::memory_order_acquire);
        // the app gave slots back: the core's chunks go back too
        for (int i = 0; i < kGxSlots; i++) {
            if (c->slots[i].state.load(std::memory_order_acquire) == kSlotReleased) {
                if (inSlot[i]) {
                    core.gxRelease(inSlot[i]);
                    inSlot[i] = nullptr;
                }
                c->slots[i].state.store(kSlotFree, std::memory_order_release);
            }
        }
        // the renderer went away (re4_core_gx_detach in the app): what waits is dropped
        const uint32_t appEpoch = c->appEpoch.load(std::memory_order_acquire);
        if (appEpoch != epoch) {
            for (int i = 0; i < kGxSlots; i++) {
                uint32_t ready = kSlotReady;
                if (c->slots[i].state.compare_exchange_strong(ready, kSlotFilling, std::memory_order_acq_rel)) {
                    core.gxRelease(inSlot[i]);
                    inSlot[i] = nullptr;
                    c->chunksDropped.fetch_add(1);
                    c->slots[i].state.store(kSlotFree, std::memory_order_release);
                }
            }
            for (const Re4GxChunk* k : held) {
                core.gxRelease(k);
                c->chunksDropped.fetch_add(1);
            }
            held.clear();
            core.gxDetach();
            epoch = appEpoch;
        }
        // the app takes chunks (it asked within the last second, since its last detach): so
        // does this thread, as soon as the core has them; the core's own rule (a renderer
        // that takes nothing for a second loses the queue) keeps working through these calls
        const bool attached = c->pollEpoch.load() == epoch && (uint32_t) (nowMs() - c->pollMs.load()) < 1000;
        if (attached) {
            while (held.size() < 8) {
                const Re4GxChunk* k = core.gxAcquire();
                if (!k) {
                    break;
                }
                held.push_back(k);
            }
        }
        // into free slots, in order
        while (!held.empty()) {
            int free = -1;
            for (int i = 0; i < kGxSlots && free < 0; i++) {
                if (c->slots[i].state.load(std::memory_order_acquire) == kSlotFree) {
                    free = i;
                }
            }
            if (free < 0) {
                break;
            }
            const Re4GxChunk* k = held.front();
            held.pop_front();
            c->slots[free].state.store(kSlotFilling, std::memory_order_relaxed);
            const auto t0 = std::chrono::steady_clock::now();
            if (fillSlot(free, k, pieces[free], epoch, ++order)) {
                inSlot[free] = k;
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                copyMs += ms;
                copyMax = ms > copyMax ? ms : copyMax;
                copyBytes += (double) k->opWords * 4 + k->vertexBytes + k->indexCount * 2.0 + k->blobBytes;
                if (++timed == 600) {
                    say("GX copies: 600 chunks, %.0f KB each, %.2f ms each (max %.1f)", copyBytes / 1024.0 / 600,
                        copyMs / 600, copyMax);
                    timed = 0;
                    copyMs = copyMax = copyBytes = 0;
                }
            } else {
                // without the chunk the app's picture state would be wrong: start over
                c->slots[free].state.store(kSlotFree, std::memory_order_release);
                core.gxRelease(k);
                for (const Re4GxChunk* h : held) {
                    core.gxRelease(h);
                }
                held.clear();
                core.gxDetach();
                c->chunksDropped.fetch_add(1);
            }
        }
        waitBell(c->gxBell, seen, attached ? 50 : 250);
    }
}

void audioThread()
{
    Control* c = gControl;
    // the sound thread of the app plays at THREAD_PRIORITY_AUDIO; this one feeds it
    if (setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), -16) != 0) {
        say("sound thread: priority not raised (%s)", strerror(errno));
    }
    const uint32_t channels = c->audioChannels;
    const uint32_t kAhead = 256;  // frames kept ahead for the app's next read (8 ms)
    int16_t* ring16 = (int16_t*) ((char*) c + c->audioOffset);
    std::vector<int16_t> block(kAudioFrames * channels);
    uint32_t taken = c->audioBell.seq.load();
    for (;;) {
        const uint32_t seen = c->audioBell.seq.load(std::memory_order_acquire);
        // only after the app read or asked: the core's reader keeps the app's pace and its clock matching
        if (seen != taken) {
            taken = seen;
            const uint32_t w = c->audioWrite.load(std::memory_order_relaxed);
            const uint32_t fill = w - c->audioRead.load(std::memory_order_acquire);
            // what the app waits for, else what is kept ahead
            const int32_t wanted = (int32_t) (c->audioWant.load(std::memory_order_acquire) - w);
            uint32_t n = fill < kAhead ? kAhead - fill : 0;
            if (wanted > 0 && (uint32_t) wanted > n) {
                n = (uint32_t) wanted;
            }
            if (n > kAudioFrames - fill) {
                n = kAudioFrames - fill;
            }
            if (n) {
                const int got = core.audio6(block.data(), (int) n);
                if (got < (int) n) {
                    c->audioCoreSilence.fetch_add(n - (uint32_t) (got > 0 ? got : 0));
                }
                for (uint32_t i = 0; i < n; i++) {
                    memcpy(ring16 + ((w + i) & (kAudioFrames - 1)) * channels, &block[i * channels],
                           channels * sizeof(int16_t));
                }
                c->audioWrite.store(w + n, std::memory_order_release);
                c->audioForwarded.fetch_add(n);
                ring(c->audioDone);
                mirrorSound(c);
            }
        }
        waitBell(c->audioBell, seen, 250);
    }
}

// --- start ------------------------------------------------------------------------

std::string gExeDir;

std::string addressMap(const char* when, const char* placeholder)
{
    std::string text = std::string("--- core process (pid ") + std::to_string((long) getpid()) + ", " +
                       std::to_string((int) (sizeof(void*) * 8)) + "-bit, " + when + ")\n";
    text += std::string("MEM1 placeholder: ") + placeholder + "\n";
    FILE* f = fopen("/proc/self/maps", "re");
    if (!f) {
        return text + "(cannot read /proc/self/maps)\n";
    }
    char line[512];
    int lines = 0;
    while (fgets(line, sizeof(line), f) && lines < 2000) {
        unsigned long a = 0, b = 0;
        if (sscanf(line, "%lx-%lx", &a, &b) != 2) {
            continue;
        }
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == ' ')) {
            l.pop_back();
        }
        if (a < kMem1Start + kMem1Size && b > kMem1Start) {
            l += "   <- MEM1";
        }
        text += l + '\n';
        lines++;
    }
    fclose(f);
    return text;
}

bool loadCore(std::string& problem)
{
#if defined(__ANDROID__)
    // by name from the app's library folder (LD_LIBRARY_PATH); RTLD_LOCAL as in the app
    core.main = dlopen("libre4_main.so", RTLD_NOW | RTLD_LOCAL);
#else
    core.main = dlopen((gExeDir + "/libre4_main.so").c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!core.main) {
        const char* e = dlerror();
        problem = std::string("core libraries missing in the core process: ") + (e ? e : "?");
        return false;
    }
    bind(core.log, "re4_log");
    bind(core.mem1Init, "re4_mem1_init");
    bind(core.reserve, "re4_core_reserve");
    bind(core.start, "re4_core_start");
    bind(core.startEntry, "re4_core_start_entry");
    bind(core.state, "re4_core_state");
    bind(core.message, "re4_core_message");
    bind(core.pad, "re4_core_pad");
    bind(core.rumble, "re4_core_rumble");
    bind(core.audio6, "re4_core_audio6");
    bind(core.audioSurround, "re4_core_audio_surround");
    bind(core.audioStats, "re4_core_audio_stats");
    bind(core.acquire, "re4_core_acquire");
    bind(core.release, "re4_core_release");
    bind(core.logTail, "re4_core_log_tail");
    bind(core.gxAcquire, "re4_core_gx_acquire");
    bind(core.gxRelease, "re4_core_gx_release");
    bind(core.gxDetach, "re4_core_gx_detach");
    bind(core.gxNotify, "re4_core_gx_notify");
    bind(core.addFrameHook, "re4_add_frame_hook");
    bind(core.vsync, "re4_core_vsync");
    bind(core.pause, "re4_core_pause");
    bind(core.debugMenu, "re4_core_debug_menu");
    bind(core.setDof, "re4_core_set_dof");
    bind(core.setLetterbox, "re4_core_set_letterbox");
    bind(core.setFastCores, "re4_core_set_fast_cores");
    bind(core.setPotato, "re4_core_set_potato");
    bind(core.setAim, "re4_core_set_aim");
    bind(core.setSoundVolume, "re4_core_set_sound_volume");
    bind(core.restartGame, "re4_core_restart_game");
    bind(core.discProblem, "re4_core_disc_problem");
    bind(core.setCheats, "re4_core_set_cheats");
    bind(core.cheatAction, "re4_core_cheat_action");
    bind(core.touch, "re4_core_touch");
    bind(core.aimTurn, "re4_core_aim_turn");
    bind(core.swipe, "re4_core_swipe");
    bind(core.slash, "re4_core_slash");
    bind(core.gyro, "re4_core_gyro");
    bind(core.depthSource, "re4_core_gx_depth_source", false);
    bind(core.instant, "re4_core_instant");
    bind(core.cardList, "re4_core_card_list");
    bind(core.cardFree, "re4_core_card_free_blocks");
    bind(core.cardName, "re4_core_card_file_name");
    bind(core.cardExport, "re4_core_card_export");
    bind(core.cardImport, "re4_core_card_import");
    bind(core.frameCount, "re4_core_frame_count");
    bind(core.gameThread, "re4_core_game_thread");
    bind(core.freeAt, "re4_trace_free_at", false);
    bind(core.logFile, "re4_log_file", false);
    if (!gMissing.empty()) {
        problem = "missing in libre4_main.so (core process): " + gMissing;
        return false;
    }
    // the app's error log: the core's lines into its file too (CoreBridge.setLogFile)
    const char* logFile = getenv("RE4_LOG_FILE");
    if (core.logFile && logFile && *logFile) {
        core.logFile(logFile);
    }
#if !defined(__ANDROID__)
    // the checks' entry instead of the game (tests/core_remote_probe.cpp, in libre4_host.so)
    if (const char* entry = getenv("RE4_CORE_TEST_ENTRY")) {
        if (*entry) {
            void* host = dlopen((gExeDir + "/libre4_host.so").c_str(), RTLD_NOW | RTLD_LOCAL);
            core.testEntry = host ? (int (*)(void)) dlsym(host, "re4_coreproc_test_entry") : nullptr;
            if (!core.testEntry) {
                problem = "RE4_CORE_TEST_ENTRY: no re4_coreproc_test_entry in libre4_host.so";
                return false;
            }
        }
    }
#endif
    // MEM1 before anything else maps memory there
    if (!core.mem1Init()) {
        problem = "MEM1 0x80000000..0x83000000 is taken in the core process too";
        return false;
    }
    return true;
}

void start(const MsgHeader& h, const std::vector<char>& payload)
{
    const char* data = payload.data();
    const size_t n = payload.size();
    std::string dataDir, moduleDir;
    if (n) {
        dataDir.assign(data, strnlen(data, n));
        if (dataDir.size() + 1 < n) {
            moduleDir.assign(data + dataDir.size() + 1, strnlen(data + dataDir.size() + 1, n - dataDir.size() - 1));
        }
    }
    int ok = 0;
    if (core.state() == RE4_CORE_IDLE) {
        ok = core.reserve(dataDir.c_str(), moduleDir.c_str());
        if (ok) {
            ok = core.testEntry ? core.startEntry(core.testEntry) : core.start();
        }
        if (ok) {
            core.addFrameHook(frameHook);
            core.gxNotify(gxNotify);
            say("the game runs (data %s)", dataDir.c_str());
        }
    } else {
        ok = core.state() == RE4_CORE_RUNNING;
    }
    reply(h.id, ok, core.message());
}

void serve()
{
    std::vector<char> payload;
    for (;;) {
        MsgHeader h;
        int fd = -1;
        if (!receiveMsg(kSocketFd, h, payload, &fd)) {
            // the app ended (or closed the socket): so does the game
            say("the app went: the core process ends");
            _exit(0);
        }
        switch (h.type) {
        case kStart:
            start(h, payload);
            break;
        case kState:
            reply(h.id, core.state(), core.message());
            break;
        case kPause:
            reply(h.id, core.pause(h.a, h.b), "");
            break;
        case kInstant: {
            Re4InstantInfo info = {};
            const int result = core.instant(h.a, &info);
            reply(h.id, result, "", &info, sizeof(info));
            break;
        }
        case kDebugMenu:
            core.debugMenu();
            break;
        case kSetDof:
            core.setDof(h.a);
            break;
        case kSetLetterbox:
            core.setLetterbox(h.a);
            break;
        case kSetFastCores:
            core.setFastCores(h.a);
            break;
        case kSetPotato:
            core.setPotato(h.a);
            break;
        case kSetAim:
            core.setAim(h.a);
            break;
        case kSetSoundVolume:
            core.setSoundVolume(h.a);
            break;
        case kRestartGame:
            core.restartGame(h.a);
            break;
        case kDiscProblem:
            reply(h.id, 0, core.discProblem());
            break;
        case kSetCheats:
            core.setCheats(h.a);
            break;
        case kCheatAction:
            reply(h.id, core.cheatAction(h.a), "");
            break;
        case kSwipe:
            core.swipe(h.a);
            break;
        case kSlash: {
            float tilt;
            memcpy(&tilt, &h.a, 4);
            core.slash(tilt);
            break;
        }
        case kAimTurn: {
            float yaw, pitch;
            memcpy(&yaw, &h.a, 4);
            memcpy(&pitch, &h.b, 4);
            core.aimTurn(yaw, pitch);
            break;
        }
        case kTouch: {
            float u, v;
            memcpy(&u, &h.a, 4);
            memcpy(&v, &h.b, 4);
            core.touch(u, v);
            break;
        }
        case kLogTail: {
            std::vector<char> text((size_t) (h.a > 0 && h.a < (1 << 20) ? h.a : 4096));
            core.logTail(text.data(), (int) text.size());
            text.back() = 0;
            reply(h.id, 0, text.data());
            break;
        }
        case kCardList: {
            std::vector<Re4CardFile> files((size_t) (h.a > 0 && h.a <= 1024 ? h.a : 127));
            const int got = core.cardList(files.data(), (int) files.size());
            reply(h.id, got, "", files.data(), got > 0 ? (size_t) got * sizeof(Re4CardFile) : 0);
            break;
        }
        case kCardFree:
            reply(h.id, core.cardFree(), "");
            break;
        case kCardName: {
            std::vector<char> name((size_t) (h.b > 0 && h.b < 4096 ? h.b : 80), 0);
            const int r = core.cardName(h.a, name.data(), (int) name.size());
            name.back() = 0;
            reply(h.id, r, name.data());
            break;
        }
        case kCardExport: {
            const unsigned capacity = h.b > 0 && h.b < (64 << 20) ? (unsigned) h.b : 0;
            std::vector<unsigned char> out(capacity);
            std::vector<char> note((size_t) (h.c > 0 && h.c < 4096 ? h.c : 256), 0);
            const int bytes = core.cardExport(h.a, out.data(), capacity, note.data(), (int) note.size());
            note.back() = 0;
            reply(h.id, bytes, note.data(), out.data(), bytes > 0 ? (size_t) bytes : 0);
            break;
        }
        case kCardImport: {
            std::vector<char> note((size_t) (h.c > 0 && h.c < 4096 ? h.c : 256), 0);
            const int r = core.cardImport((const unsigned char*) payload.data(), (unsigned) payload.size(), h.a,
                                          note.data(), (int) note.size());
            note.back() = 0;
            reply(h.id, r, note.data());
            break;
        }
        case kHang:
            reportHang(h.a);
            break;
        case kCrashFile:
            if (fd >= 0) {
                const int old = gCrashFd.exchange(fd);
                fd = -1;
                if (old >= 0) {
                    close(old);
                }
            }
            break;
        default:
            say("unknown command %u", (unsigned) h.type);
            if (h.id) {
                reply(h.id, -1, "unknown command");
            }
            break;
        }
        if (fd >= 0) {
            close(fd);
        }
    }
}

void hello(int controlFd, const std::string& problem, const std::string& map)
{
    std::vector<char> payload(problem.begin(), problem.end());
    payload.push_back(0);
    payload.insert(payload.end(), map.begin(), map.end());
    MsgHeader h = {kHello, 0, (int32_t) getpid(), (int32_t) (problem.size() + 1), (int32_t) controlBytes(),
                   (uint32_t) payload.size()};
    std::lock_guard<std::mutex> l(gSendLock);
    sendMsg(kSocketFd, h, payload.data(), controlFd);
}

}  // namespace

int main(int argc, char** argv)
{
    // what the app's process had set does not hold here: every signal free, broken pipes as errors
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    for (int s = 1; s < NSIG; s++) {
        struct sigaction old;
        if (s != SIGKILL && s != SIGSTOP && sigaction(s, nullptr, &old) == 0 && old.sa_handler == SIG_IGN) {
            signal(s, SIG_DFL);
        }
    }
    signal(SIGPIPE, SIG_IGN);
    // MEM1 at once, before anything else maps memory (the core's libraries load above it)
    const char* placeholder;
#if defined(__ANDROID__)
    void* want = (void*) kMem1Start;
    void* p = mmap(want, kMem1Size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | 0x100000 /* NOREPLACE */,
                   -1, 0);
    if (p == want) {
        setenv("RE4_MEM1_PLACEHOLDER", "1", 1);
        placeholder = "reserved";
    } else {
        if (p != MAP_FAILED) {
            munmap(p, kMem1Size);
        }
        unsetenv("RE4_MEM1_PLACEHOLDER");
        placeholder = "NOT reserved";
    }
    // files of the app's process this program does not use (the socket is 3)
    if (DIR* d = opendir("/proc/self/fd")) {
        std::vector<int> stray;
        while (dirent* e = readdir(d)) {
            const int fd = atoi(e->d_name);
            if (fd > kSocketFd && fd != dirfd(d)) {
                stray.push_back(fd);
            }
        }
        closedir(d);
        for (int fd : stray) {
            close(fd);
        }
    }
#else
    // as the Linux loader of re4_host: everything below 0x83000000 held, MEM1 first
    unsetenv("RE4_MEM1_PLACEHOLDER");
    reserveLowMemory();
    placeholder = getenv("RE4_MEM1_PLACEHOLDER") ? "reserved" : "NOT reserved";
#endif
    // the app's end ends this process (the socket closes as well)
    long parent = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--parent") == 0) {
            parent = atol(argv[i + 1]);
        }
    }
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (parent > 0 && getppid() != (pid_t) parent) {
        _exit(0);
    }
    prctl(PR_SET_NAME, "re4-core");
    char exe[PATH_MAX];
    const ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = 0;
        gExeDir = exe;
        gExeDir = gExeDir.substr(0, gExeDir.rfind('/'));
    }
    gCheck = getenv("RE4_CORE_IPC_CHECK") && atoi(getenv("RE4_CORE_IPC_CHECK")) == 1;
    if (const char* s = getenv("RE4_CORE_IPC_SEGMENT")) {
        const long v = atol(s);
        if (v >= 4096) {
            gSegmentStart = (uint32_t) v;
        }
    }
    std::string problem;
    const bool loaded = loadCore(problem);
    const std::string map = addressMap("after loading the core", placeholder);
    if (!loaded) {
        say("%s", problem.c_str());
        hello(-1, problem, map);
        _exit(1);
    }
    const size_t bytes = controlBytes();
    const int controlFd = shmCreate("re4-core", bytes);
    gControl = controlFd >= 0 ? (Control*) shmMap(controlFd, bytes) : nullptr;
    if (!gControl) {
        problem = std::string("no shared memory for the core process: ") + strerror(errno);
        say("%s", problem.c_str());
        hello(-1, problem, map);
        _exit(1);
    }
    controlInit(gControl, (uint32_t) getpid());
    gControl->check = gCheck ? 1 : 0;
    // the game's GXPeekZ reads the depth map the app's renderer writes into the shared memory
    if (core.depthSource) {
        core.depthSource((const unsigned*) ((char*) gControl + gControl->depthOffset),
                         (const unsigned*) &gControl->depthSerial);
    }
    installCrashHandler();
    say("ready (pid %d, MEM1 placeholder %s, libre4_main.so loaded%s)", (int) getpid(), placeholder,
        gCheck ? ", checks on" : "");
    std::thread(inputThread).detach();
    std::thread(gxThread).detach();
    std::thread(audioThread).detach();
    hello(controlFd, "", map);
    close(controlFd);
    serve();
    return 0;
}
