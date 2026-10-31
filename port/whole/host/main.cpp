// Linux launcher for the whole-program port (32-bit host test build),
// libre4_host.so: the loader re4_host (host/loader.c) keeps the low address
// range free and calls re4_host_main.
//
//   re4_host                  run the game (re4_game_main)
//   re4_host --check-modules  load all REL module libraries and resolve every
//                             cross-module import, then exit
//   re4_host --self-test      call through a generated cross-module trampoline
//   re4_host --runtime-test   OSLink, DVD and yz2 checks without disc data
//                             (tests/runtime_tests.cpp)
//   re4_host --core-test      the embedding interface of the Android app
//                             without disc data (tests/core_tests.cpp)
//   re4_host --movie-test FILE YUV WAV FIELDS
//                             a movie of the data folder through the CRI
//                             middleware (port/tools/test_whole_movie.py)
//   re4_host --memcard-format FILE
//                             a formatted, empty memory card in FILE, made by
//                             the SDK's card library (tests/card_probe.cpp;
//                             the PC check boots with it)
//   re4_host --gci-export CARD DIR
//                             every file of the card file CARD as a .gci in
//                             GameCube byte order into DIR (as the app's menu
//                             exports them; runtime/gci.cpp)
//   re4_host --gci-import CARD FILE.gci [--replace]
//                             a .gci onto the card file CARD
//
// A device's display and sound output for a game run (the crackle report of 01.11.2026):
//   RE4_TEST_DISPLAY_HZ=59.6  a thread reports vsyncs at that rate (re4_vi_vsync, as the app's
//                             Choreographer does), the core's retraces lock onto it
//   RE4_TEST_AUDIO_OUT=FILE   a thread reads the sound as the app does (256 frames every 8 ms of
//                             RE4_TEST_AUDIO_CLOCK=1.0, the device's sound clock against the
//                             real one) into a 32 kHz stereo WAV; every 10 s one line with the
//                             reads that came short (a dropout: silence there) and the ring's fill
//
// kill -USR1 <pid> prints a backtrace of the running context and of every OS
// thread's last wait (with RE4_WAIT_TRACE=1): the hang search.
//
// Module libraries are looked up in <directory of re4_host>/modules. The
// extracted disc files are read from --data DIR, else $RE4_DATA_DIR, else
// <directory of re4_host>/data (layout: port/whole/runtime/dvd.cpp).
#include "re4_card_files.h"
#include "re4_runtime.h"

#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#include <execinfo.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int re4_runtime_tests();
int re4_core_tests();

static long long monoNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleepUntil(long long ns)
{
    struct timespec ts = {(time_t) (ns / 1000000000LL), (long) (ns % 1000000000LL)};
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, 0);
}

static double gDisplayHz;

static void* displayThread(void*)
{
    const double period = 1e9 / gDisplayHz;
    const long long t0 = monoNs();
    for (long long k = 1;; k++) {
        const long long at = t0 + (long long) (k * period);
        sleepUntil(at);
        re4_vi_vsync(at);
    }
    return 0;
}

static FILE* gAudioOut;
static double gAudioClock = 1.0;

static void* audioThread(void*)
{
    enum { kFrames = 256 };
    static short pcm[kFrames * 2];
    const double period = kFrames * 1e9 / 32000.0 * gAudioClock;
    const long long t0 = monoNs();
    unsigned reads = 0, shortReads = 0, shortAll = 0, fillMin = ~0u, frames = 0;
    unsigned long long fillSum = 0;
    for (long long k = 1;; k++) {
        sleepUntil(t0 + (long long) (k * period));
        const int got = re4_audio_read(pcm, kFrames);
        unsigned blocks, fill, overruns, underruns, skips;
        re4_audio_stats(&blocks, &fill, &overruns, &underruns, &skips);
        fwrite(pcm, sizeof pcm, 1, gAudioOut);
        frames += kFrames;
        reads++;
        // before the game's sound starts every read is empty: not a dropout
        if (got > 0 && got < kFrames) {
            shortReads++;
        }
        fillMin = fill < fillMin ? fill : fillMin;
        fillSum += fill;
        if (reads % 1250 == 0) {  // 10 s
            shortAll += shortReads;
            re4_log("audio out: %u s, %u short reads in the last 10 s (%u in all), ring %u..%llu ms (min..mean), "
                    "core: %u ran dry", reads / 125, shortReads, shortAll, fillMin / 32, fillSum / 1250 / 32,
                    underruns);
            shortReads = 0;
            fillMin = ~0u;
            fillSum = 0;
            // the WAV header with the size so far (the run ends with _exit)
            const unsigned bytes = frames * 4, riff = 36 + bytes, fmt = 16, rate = 32000, byteRate = 128000;
            const unsigned short pcmTag = 1, channels = 2, align = 4, bits = 16;
            fseek(gAudioOut, 0, SEEK_SET);
            fwrite("RIFF", 1, 4, gAudioOut);
            fwrite(&riff, 4, 1, gAudioOut);
            fwrite("WAVEfmt ", 1, 8, gAudioOut);
            fwrite(&fmt, 4, 1, gAudioOut);
            fwrite(&pcmTag, 2, 1, gAudioOut);
            fwrite(&channels, 2, 1, gAudioOut);
            fwrite(&rate, 4, 1, gAudioOut);
            fwrite(&byteRate, 4, 1, gAudioOut);
            fwrite(&align, 2, 1, gAudioOut);
            fwrite(&bits, 2, 1, gAudioOut);
            fwrite("data", 1, 4, gAudioOut);
            fwrite(&bytes, 4, 1, gAudioOut);
            fseek(gAudioOut, 0, SEEK_END);
            fflush(gAudioOut);
        }
    }
    return 0;
}

static void startDevice()
{
    pthread_t t;
    if (const char* hz = getenv("RE4_TEST_DISPLAY_HZ")) {
        gDisplayHz = atof(hz);
        if (gDisplayHz > 20 && gDisplayHz < 250) {
            re4_log("display: vsyncs at %.3f Hz (RE4_TEST_DISPLAY_HZ)", gDisplayHz);
            pthread_create(&t, 0, displayThread, 0);
        }
    }
    if (const char* out = getenv("RE4_TEST_AUDIO_OUT")) {
        if (const char* clock = getenv("RE4_TEST_AUDIO_CLOCK")) {
            gAudioClock = atof(clock) > 0.9 && atof(clock) < 1.1 ? atof(clock) : 1.0;
        }
        if ((gAudioOut = fopen(out, "wb")) != 0) {
            static const char header[44] = {0};
            fwrite(header, 1, sizeof header, gAudioOut);
            re4_log("audio out: %s, 256 frames every %.4f ms (RE4_TEST_AUDIO_OUT)", out, 8.0 * gAudioClock);
            pthread_create(&t, 0, audioThread, 0);
        }
    }
}

// Where an address lies: library and offset (for addr2line), nearest symbol.
static void printAddress(const char* what, void* address)
{
    Dl_info info;
    if (address && dladdr(address, &info) && info.dli_fname) {
        fprintf(stderr, "[re4]   %s %p = %s+0x%lx (%s)\n", what, address, info.dli_fname,
                (unsigned long) ((char*) address - (char*) info.dli_fbase), info.dli_sname ? info.dli_sname : "?");
    } else {
        fprintf(stderr, "[re4]   %s %p\n", what, address);
    }
}

// Game memory around a register that points into MEM1: what the code that
// crashed was working on (r229: a draw list entry with a broken function
// pointer). Only mapped pages are read (mincore), inside the handler.
static bool pageMapped(unsigned long a)
{
    unsigned char v;
    unsigned long page = (unsigned long) sysconf(_SC_PAGESIZE);
    return mincore((void*) (a & ~(page - 1)), 1, &v) == 0;
}

static void dumpAround(const char* reg, unsigned long v)
{
    if (v < RE4_MEM1_BASE || v >= (unsigned long) RE4_MEM1_BASE + RE4_MEM1_SIZE) {
        return;
    }
    unsigned long from = (v & ~0xFUL) - 0x40, to = (v & ~0xFUL) + 0x20;
    if (from < RE4_MEM1_BASE || to > (unsigned long) RE4_MEM1_BASE + RE4_MEM1_SIZE || !pageMapped(from) ||
        !pageMapped(to - 1)) {
        return;
    }
    fprintf(stderr, "[re4]   memory around %s 0x%08lx (host order):\n", reg, v);
    for (unsigned long a = from; a < to; a += 16) {
        const unsigned* w = (const unsigned*) a;
        fprintf(stderr, "[re4]     %08lx: %08x %08x %08x %08x%s\n", a, w[0], w[1], w[2], w[3],
                v >= a && v < a + 16 ? "  <-" : "");
    }
}

static void crashHandler(int sig, siginfo_t* si, void* context)
{
    void* frames[64];
    int n = backtrace(frames, 64);
    fprintf(stderr, "[re4] fatal signal %d (%s), fault address %p, backtrace:\n", sig, strsignal(sig),
            si ? si->si_addr : 0);
    // the faulting instruction (backtrace() does not unwind through the signal frame on ARM)
    const ucontext_t* uc = (const ucontext_t*) context;
#if defined(__arm__)
    printAddress("pc", (void*) uc->uc_mcontext.arm_pc);
    printAddress("lr", (void*) uc->uc_mcontext.arm_lr);
    const unsigned long* r = &uc->uc_mcontext.arm_r0;  // r0..r10, fp, ip, sp in a row
    static const char* const kNames[] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
                                          "r8", "r9", "r10", "fp", "ip", "sp" };
    fprintf(stderr, "[re4]   registers:");
    for (int i = 0; i < 14; i++) {
        fprintf(stderr, " %s=%08lx", kNames[i], r[i]);
    }
    fprintf(stderr, "\n");
    for (int i = 0; i < 14; i++) {
        dumpAround(kNames[i], r[i]);
    }
#elif defined(__i386__)
    printAddress("pc", (void*) uc->uc_mcontext.gregs[REG_EIP]);
    static const struct {
        const char* name;
        int reg;
    } kRegs[] = { { "eax", REG_EAX }, { "ebx", REG_EBX }, { "ecx", REG_ECX }, { "edx", REG_EDX },
                  { "esi", REG_ESI }, { "edi", REG_EDI }, { "ebp", REG_EBP }, { "esp", REG_ESP } };
    fprintf(stderr, "[re4]   registers:");
    for (const auto& k : kRegs) {
        fprintf(stderr, " %s=%08lx", k.name, (unsigned long) (unsigned) uc->uc_mcontext.gregs[k.reg]);
    }
    fprintf(stderr, "\n");
    for (const auto& k : kRegs) {
        dumpAround(k.name, (unsigned long) (unsigned) uc->uc_mcontext.gregs[k.reg]);
    }
#elif defined(__x86_64__)
    printAddress("pc", (void*) uc->uc_mcontext.gregs[REG_RIP]);
    static const struct {
        const char* name;
        int reg;
    } kRegs[] = { { "rax", REG_RAX }, { "rbx", REG_RBX }, { "rcx", REG_RCX }, { "rdx", REG_RDX },
                  { "rsi", REG_RSI }, { "rdi", REG_RDI }, { "rbp", REG_RBP }, { "rsp", REG_RSP },
                  { "r8", REG_R8 },   { "r9", REG_R9 },   { "r12", REG_R12 }, { "r13", REG_R13 },
                  { "r14", REG_R14 }, { "r15", REG_R15 } };
    fprintf(stderr, "[re4]   registers:");
    for (const auto& k : kRegs) {
        fprintf(stderr, " %s=%016lx", k.name, (unsigned long) uc->uc_mcontext.gregs[k.reg]);
    }
    fprintf(stderr, "\n");
    for (const auto& k : kRegs) {
        dumpAround(k.name, (unsigned long) uc->uc_mcontext.gregs[k.reg]);
    }
#elif defined(__aarch64__)
    printAddress("pc", (void*) uc->uc_mcontext.pc);
    printAddress("lr", (void*) uc->uc_mcontext.regs[30]);
    fprintf(stderr, "[re4]   registers:");
    for (int i = 0; i < 31; i++) {
        fprintf(stderr, " x%d=%016llx", i, (unsigned long long) uc->uc_mcontext.regs[i]);
    }
    fprintf(stderr, "\n");
    for (int i = 0; i < 31; i++) {
        char name[8];
        snprintf(name, sizeof(name), "x%d", i);
        dumpAround(name, (unsigned long) uc->uc_mcontext.regs[i]);
    }
#else
    (void) uc;
#endif
    backtrace_symbols_fd(frames, n, 2);
    _exit(128 + sig);
}

// SIGUSR1 (kill -USR1 <pid>): backtrace of the running context without
// stopping, to find out where the game waits or loops.
static void traceHandler(int sig)
{
    void* frames[64];
    int n = backtrace(frames, 64);
    fprintf(stderr, "[re4] signal %d (%s), backtrace of the running context:\n", sig, strsignal(sig));
    backtrace_symbols_fd(frames, n, 2);
    re4_os_dump_threads(2);
}

static int checkModules()
{
    int failed = 0;
    int count = 0;
    for (const Re4ModuleEntry* e = kRe4Modules; e->name; e++) {
        if (!re4_module_load(e->name)) {
            failed++;
        }
        count++;
    }
    int unresolved = 0;
    for (const Re4ModuleEntry* e = kRe4Modules; e->name; e++) {
        char fn[128];
        snprintf(fn, sizeof(fn), "re4_module_check_imports_%s", e->name);
        void* handle = re4_module_load(e->name);
        if (!handle) {
            continue;
        }
        int (*check)() = (int (*)()) dlsym(handle, fn);
        if (!check) {
            re4_log("module %s: %s missing", e->name, fn);
            unresolved++;
            continue;
        }
        unresolved += check();
    }
    re4_log("check-modules: %d modules, %d failed to load, %d unresolved imports, %d stubs",
            count, failed, unresolved, re4_stub_count());
    return failed || unresolved ? 1 : 0;
}

// Loads two test modules; the caller reaches the target only through generated
// cross-module trampolines, with arguments in registers and on the stack.
static int selfTest()
{
    if (!re4_module_load("re4test_target") || !re4_module_load("re4test_caller")) {
        return 1;
    }
    int (*run)() = (int (*)()) dlsym(re4_module_load("re4test_caller"), "re4test_run");
    int result = run ? run() : 4;
    re4_log("self-test: trampolines %s (code %d)", result == 0 ? "ok" : "FAILED", result);
    return result;
}

extern "C" int re4probe_movie_play(const char* fname, const char* yuvPath, const char* wavPath, int maxFields);
extern "C" int re4probe_card_format(const char* path);
extern "C" void re4_memcard_open(const char* path);
extern "C" void re4_memcard_flush(int force);

// --gci-export / --gci-import: the card functions of the app's menu on a card file.
static int gciTool(int argc, char** argv)
{
    re4_memcard_open(argv[2]);
    char note[256] = "";
    if (strcmp(argv[1], "--gci-export") == 0) {
        static Re4CardFile files[127];
        int n = re4_card_list(files, 127);
        if (n < 0) {
            fprintf(stderr, "gci-export: %s: no formatted card\n", argv[2]);
            return 1;
        }
        int failed = 0;
        static unsigned char out[RE4_GCI_HEADER + 251 * RE4_CARD_BLOCK];
        for (int i = 0; i < n; i++) {
            char name[64], path[PATH_MAX];
            re4_gci_file_name(files[i].index, name, sizeof(name));
            int bytes = re4_gci_export(files[i].index, out, sizeof(out), note, sizeof(note));
            if (bytes < 0) {
                printf("gci-export: %s: FAILED (%s)\n", files[i].name, note);
                failed++;
                continue;
            }
            snprintf(path, sizeof(path), "%s/%s", argv[3], name);
            FILE* f = fopen(path, "wb");
            if (!f || fwrite(out, 1, (size_t) bytes, f) != (size_t) bytes) {
                printf("gci-export: %s: cannot write %s\n", files[i].name, path);
                failed++;
            } else {
                printf("gci-export: %s -> %s (%d bytes; %s)\n", files[i].name, path, bytes, note);
            }
            if (f) {
                fclose(f);
            }
        }
        return failed ? 1 : 0;
    }
    FILE* f = fopen(argv[3], "rb");
    if (!f) {
        fprintf(stderr, "gci-import: %s cannot be opened\n", argv[3]);
        return 1;
    }
    static unsigned char in[RE4_GCI_HEADER + 252 * RE4_CARD_BLOCK];
    size_t size = fread(in, 1, sizeof(in), f);
    fclose(f);
    bool replace = argc > 4 && strcmp(argv[4], "--replace") == 0;
    int r = re4_gci_import(in, (unsigned) size, replace, note, sizeof(note));
    printf("gci-import: %s: %s (%d)\n", argv[3], r == RE4_CARD_OK ? "ok" : note, r);
    re4_memcard_open(0);  // synced and closed
    return r == RE4_CARD_OK ? 0 : 1;
}

extern "C" __attribute__((visibility("default"))) int re4_host_main(int argc, char** argv)
{
    char exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
        exe[len] = 0;
        char dir[PATH_MAX + 16];
        const char* exeDir = dirname(exe);
        snprintf(dir, sizeof(dir), "%s/modules", exeDir);
        re4_set_module_dir(dir);
        snprintf(dir, sizeof(dir), "%s/data", exeDir);
        re4_set_data_dir(dir);
    }
    if (const char* env = getenv("RE4_DATA_DIR")) {
        re4_set_data_dir(env);
    }
    if (argc > 2 && strcmp(argv[1], "--data") == 0) {
        re4_set_data_dir(argv[2]);
        argc -= 2;
        argv += 2;
    }
    struct sigaction crash;
    memset(&crash, 0, sizeof(crash));
    crash.sa_sigaction = crashHandler;
    crash.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &crash, 0);
    sigaction(SIGBUS, &crash, 0);
    sigaction(SIGILL, &crash, 0);
    sigaction(SIGFPE, &crash, 0);
    sigaction(SIGABRT, &crash, 0);
    signal(SIGUSR1, traceHandler);

    // _exit: the game never ends on the GameCube, so its static destructors
    // (EventMgr, ...) were never meant to run.
    if (argc > 1 && strcmp(argv[1], "--check-modules") == 0) {
        _exit(checkModules());
    }
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) {
        _exit(selfTest());
    }
    if (argc > 1 && strcmp(argv[1], "--runtime-test") == 0) {
        _exit(re4_runtime_tests() ? 1 : 0);
    }
    if (argc > 1 && strcmp(argv[1], "--core-test") == 0) {
        _exit(re4_core_tests() ? 1 : 0);
    }
    if (argc > 5 && strcmp(argv[1], "--movie-test") == 0) {
        // FILE YUV WAV FIELDS: a movie of the data folder through the CRI
        // middleware (tests/movie_probe.cpp, port/tools/test_whole_movie.py)
        int frames = re4probe_movie_play(argv[2], argv[3], argv[4], atoi(argv[5]));
        re4_log("movie-test: %d frames", frames);
        _exit(frames > 0 ? 0 : 1);
    }
    if (argc > 2 && strcmp(argv[1], "--memcard-format") == 0) {
        _exit(re4probe_card_format(argv[2]) ? 0 : 1);
    }
    if (argc > 3 && (strcmp(argv[1], "--gci-export") == 0 || strcmp(argv[1], "--gci-import") == 0)) {
        int r = gciTool(argc, argv);
        fflush(stdout);
        _exit(r);
    }
    // The game tests bit 31 of addresses of its own data (as on the GameCube).
    re4_check_game_address("libre4_main", (const void*) &re4_game_main);
    re4_log("starting re4_game_main");
    startDevice();
    // The game runs as the boot thread of the Dolphin OS thread emulation.
    int ret = re4_os_run(re4_game_main);
    re4_log("re4_game_main returned %d", ret);
    _exit(ret);
}
