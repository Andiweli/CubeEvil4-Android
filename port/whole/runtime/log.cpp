// Logging for the port runtime and the generated stubs.
// The last lines also stay in a small ring for the app's diagnostic overlay
// (re4_core_log_tail), and with re4_log_file they go into a file of their own
// as well (the app's error log, without logcat).
#include "re4_runtime.h"

#include <atomic>
#include <fcntl.h>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#if defined(__GLIBC__)
#include <execinfo.h>
#endif
#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace {

const int kTailLines = 24;
const int kTailWidth = 120;
std::mutex gTailLock;
char gTail[kTailLines][kTailWidth];
int gTailNext;
int gTailCount;

void remember(const char* fmt, va_list ap)
{
    char line[kTailWidth];
    vsnprintf(line, sizeof(line), fmt, ap);
    for (char* c = line; *c; c++) {
        if (*c == '\n' || *c == '\r') {
            *c = ' ';
        }
    }
    std::lock_guard<std::mutex> lock(gTailLock);
    memcpy(gTail[gTailNext], line, sizeof(line));
    gTailNext = (gTailNext + 1) % kTailLines;
    if (gTailCount < kTailLines) {
        gTailCount++;
    }
}

// The log file (re4_log_file): each line written at once, so a crash loses none; at 4 MB
// (RE4_LOG_FILE_LIMIT bytes, the runtime test) it becomes <file>.1 and a new one starts (two
// files at most, as the app's logcat recording).
const off_t kFileLimit = 4 << 20;
const int kFileLine = 1024;
std::mutex gFileLock;
int gFile = -1;
off_t gFileSize;
off_t gFileLimit = kFileLimit;
std::string gFilePath;

void writeLine(const char* text, int length)
{
    // "10-21 14:03:07.512 " as logcat's time
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm t;
    time_t sec = now.tv_sec;
    localtime_r(&sec, &t);
    char line[kFileLine + 32];
    int n = snprintf(line, sizeof(line), "%02d-%02d %02d:%02d:%02d.%03ld ", t.tm_mon + 1, t.tm_mday, t.tm_hour,
                     t.tm_min, t.tm_sec, now.tv_nsec / 1000000L);
    if (length > kFileLine) {
        length = kFileLine;
    }
    memcpy(line + n, text, (size_t) length);
    n += length;
    line[n++] = '\n';
    if (gFileSize + n > gFileLimit) {
        close(gFile);
        rename(gFilePath.c_str(), (gFilePath + ".1").c_str());
        gFile = open(gFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND | O_CLOEXEC, 0644);
        gFileSize = 0;
        if (gFile < 0) {
            return;
        }
    }
    if (write(gFile, line, (size_t) n) == n) {
        gFileSize += n;
    }
}

void fileLine(const char* fmt, va_list ap)
{
    std::lock_guard<std::mutex> lock(gFileLock);
    if (gFile < 0) {
        return;
    }
    char text[kFileLine];
    int n = vsnprintf(text, sizeof(text), fmt, ap);
    if (n < 0) {
        return;
    }
    writeLine(text, n < (int) sizeof(text) ? n : (int) sizeof(text) - 1);
}

}  // namespace

extern "C" int re4_log_tail(char* out, int size);

// The core's lines into `path` too (appended), from now on; the lines remembered for the overlay
// first, so the start of the run is there. nullptr or "" stops it.
extern "C" void re4_log_file(const char* path)
{
    char tail[kTailLines * (kTailWidth + 1) + 1];
    int tailLength = 0;
    if (path && *path) {
        tailLength = re4_log_tail(tail, (int) sizeof(tail));
    }
    std::lock_guard<std::mutex> lock(gFileLock);
    if (gFile >= 0) {
        close(gFile);
        gFile = -1;
    }
    gFilePath = path ? path : "";
    if (gFilePath.empty()) {
        return;
    }
    gFile = open(gFilePath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (gFile < 0) {
        return;
    }
    struct stat st;
    gFileSize = fstat(gFile, &st) == 0 ? st.st_size : 0;
    gFileLimit = kFileLimit;
    if (const char* limit = getenv("RE4_LOG_FILE_LIMIT")) {
        if (atol(limit) >= 1024) {
            gFileLimit = (off_t) atol(limit);
        }
    }
    char head[96];
    int n = snprintf(head, sizeof(head), "--- core log, process %d; first the lines before it (up to %d)",
                     (int) getpid(), kTailLines);
    writeLine(head, n);
    for (char* line = tail; line < tail + tailLength;) {
        char* end = (char*) memchr(line, '\n', (size_t) (tail + tailLength - line));
        if (!end) {
            end = tail + tailLength;
        }
        writeLine(line, (int) (end - line));
        line = end + 1;
    }
}

extern "C" void re4_log(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    remember(fmt, copy);
    va_end(copy);
    va_copy(copy, ap);
    fileLine(fmt, copy);
    va_end(copy);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_INFO, "re4", fmt, ap);
#else
    fputs("[re4] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
#endif
    va_end(ap);
}

// A line of the app (its renderer and core mode, gx_gl.cpp re4gx::LogTo) into the log file only:
// logcat has it under the app's tag already, and Android 10 and older give the app no logcat to
// record, so the error report has only this file (Galaxy S10 tester, 21.10.2026).
extern "C" void re4_log_file_line(const char* text)
{
    std::lock_guard<std::mutex> lock(gFileLock);
    if (gFile < 0 || !text) {
        return;
    }
    writeLine(text, (int) strlen(text));
}

// The remembered lines, oldest first, '\n'-separated; returns the length.
extern "C" int re4_log_tail(char* out, int size)
{
    if (!out || size <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(gTailLock);
    int len = 0;
    out[0] = 0;
    for (int i = 0; i < gTailCount; i++) {
        const char* line = gTail[(gTailNext - gTailCount + i + kTailLines) % kTailLines];
        int n = snprintf(out + len, (size_t) (size - len), "%s\n", line);
        if (n < 0 || n >= size - len) {
            break;
        }
        len += n;
    }
    return len;
}

// runtime/state_trace.cpp (the game's id systems; absent in tools without the game)
extern "C" void re4_idsys_report(void) __attribute__((weak));

// RE4_GAME_LOG_STACK=TEXT (PC builds): a game line that starts with TEXT also logs the call
// stack that gave it (library and offset; addr2line -f -C -e LIBRARY OFFSET names the
// function), to find which code a message of the room tour comes from (10b, 27.10.2026)
static void gameLogStack(const char* text)
{
#if defined(__GLIBC__)
    static const char* want = getenv("RE4_GAME_LOG_STACK");
    if (!want || !*want || strncmp(text, want, strlen(want)) != 0) {
        return;
    }
    void* frames[24];
    int depth = backtrace(frames, 24);
    char** names = backtrace_symbols(frames, depth);
    for (int i = 2; names && i < depth; i++) {
        re4_log("game:   at %s", names[i]);
    }
    free(names);
#else
    (void) text;
#endif
}

// The game's own log lines (pLog->err / warn / mes, db_log.cpp, patch 0076): on
// the GameCube they went to the screen and the console. Here each message
// (by its format string) is logged the first 3 times, then every 1000th time
// with its count, so that a message of every frame does not flood the log.
extern "C" void re4_game_log(const char* format, const char* text)
{
    struct Seen {
        const char* format;
        unsigned count;
    };
    static Seen seen[512];
    static int used;
    static std::mutex lock;
    unsigned count = 4;  // a full table: not logged
    {
        std::lock_guard<std::mutex> guard(lock);
        int i = 0;
        while (i < used && seen[i].format != format) {
            i++;
        }
        if (i == used && used < 512) {
            seen[used++] = {format, 0};
        }
        if (i < used) {
            count = ++seen[i].count;
        }
    }
    if (count <= 3) {
        re4_log("game: %s", text);
        gameLogStack(text);
        // who holds the units when an id system is full (R3: the radio's subtitle bar with the European discs)
        if (strncmp(text, "IDSystem::set() work full", 25) == 0 && re4_idsys_report) {
            re4_idsys_report();
        }
    } else if (count % 1000 == 0) {
        re4_log("game: %s (%u times)", text, count);
    }
}

// An allocation of the game that found no room (main_mem.cpp, patch 0081):
// heap number, size, the flag of mem_alloc (1 the game reports it itself, 0 a
// try with a fallback such as cDataSwap's ARAM, -1 Debug_alloc) and the place
// ("card.cpp(2190)"). Each place the first 3 times, then every 1000th time;
// re4_heap_fail_count for the state trace.
static std::atomic<unsigned> gHeapFails{0};

extern "C" void re4_heap_failed(int heap, unsigned size, int flag, const char* where)
{
    unsigned n = ++gHeapFails;
    struct Seen {
        char where[32];
        unsigned count;
    };
    static Seen seen[256];
    static int used;
    static std::mutex lock;
    unsigned count = 4;
    {
        std::lock_guard<std::mutex> guard(lock);
        const char* w = where ? where : "?";
        int i = 0;
        while (i < used && strncmp(seen[i].where, w, sizeof(seen[i].where) - 1) != 0) {
            i++;
        }
        if (i == used && used < 256) {
            snprintf(seen[used].where, sizeof(seen[used].where), "%s", w);
            seen[used++].count = 0;
        }
        if (i < used) {
            count = ++seen[i].count;
        }
    }
    if (count <= 3 || count % 1000 == 0) {
        if (flag == 0) {
            // a try with a way out (DataCtrl, cDataSwap): the game takes it, as on the GameCube. Worded so
            // that testers do not take it for an error (user report 30.10.2026: the line in r101 read as
            // the cause of a stutter)
            re4_log("heap: heap %d full for 0x%x bytes (%s): the game loads it through ARAM instead, as on the "
                    "GameCube; not an error (%u so far)", heap, size, where ? where : "?", n);
        } else {
            re4_log("heap: no room for 0x%x bytes in heap %d (%s, flag %d; %u failures so far)", size, heap,
                    where ? where : "?", flag, n);
        }
    }
}

extern "C" unsigned re4_heap_fail_count(void)
{
    return gHeapFails.load();
}

// acosf / asinf of the game with an argument a rounding step beyond +-1 (re4_whole_prelude.h
// re4_arc_arg): the first time and at every tenfold count, so that the log shows how often.
static std::atomic<unsigned> gArcClamps{0};

extern "C" void re4_arc_clamped(float x)
{
    unsigned n = ++gArcClamps;
    unsigned step = 1;
    while (step <= n / 10) {
        step *= 10;
    }
    if (n == step) {
        re4_log("math: acosf/asinf of %.9g taken as %d (%u times so far; on the GameCube the unit vectors "
                "stay within 1)", (double) x, x > 0 ? 1 : -1, n);
    }
}

extern "C" unsigned re4_arc_clamp_count(void)
{
    return gArcClamps.load();
}

// An archive index past its table (re4_whole_prelude.h re4_arc_entry, patch 0134): each archive
// address and index once (at most 32 lines), with what the word gives as on the GameCube.
extern "C" void re4_arc_past_table(unsigned base, unsigned no, unsigned count, unsigned word)
{
    static std::atomic<unsigned> seen[32][2];
    static std::atomic<unsigned> used{0};
    unsigned n = used.load();
    for (unsigned i = 0; i < n && i < 32; i++) {
        if (seen[i][0].load() == base && seen[i][1].load() == no) {
            return;
        }
    }
    if (n >= 32) {
        return;
    }
    unsigned slot = used++;
    if (slot >= 32) {
        return;
    }
    seen[slot][0] = base;
    seen[slot][1] = no;
    unsigned ptr = base + word;
    re4_log("formats: archive at 0x%08x (%u entries) read at index 0x%x, past its table: word 0x%08x as "
            "on the GameCube, 0x%08x%s", base, count, no, word, ptr,
            ptr - 0x80000000u <= 0x02FFFFFFu ? "" : " (outside the game memory, VALID_PTR2 drops it)");
}

extern "C" void re4_stub_hit(const char* const* names, unsigned char* seen, int index)
{
    if (!seen[index]) {
        seen[index] = 1;
        re4_log("stub called: %s", names[index]);
    }
}
