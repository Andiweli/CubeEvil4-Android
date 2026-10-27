// re4_cpu_info.h: the processor time a thread takes and the kind of processor core it runs on,
// for the performance lines of the log (the core's "GX core" line, the renderer's "GX" line).
// Header only: the core's runtime and the app's renderer both use it.
//
// Phones mix fast and slow cores (Galaxy A30s: two at 1.8 GHz, six at 1.6 GHz; Retroid: one
// fast, three middle, four slow). A thread that needs a whole core gets the frame rate of the
// core Android gives it; processor time next to the time a picture took tells whether the
// thread computed (time = processor time) or waited (for the GPU, a lock, another thread).
#pragma once

#include <dlfcn.h>
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace re4cpu {

// processor time of the calling thread in ns
inline long long ThreadNs()
{
    timespec t;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) {
        return 0;
    }
    return static_cast<long long>(t.tv_sec) * 1000000000LL + t.tv_nsec;
}

// a number from a file of /sys (0: not readable)
inline unsigned ReadNumber(const char* path)
{
    unsigned value = 0;
    if (FILE* f = fopen(path, "r")) {
        if (fscanf(f, "%u", &value) != 1) {
            value = 0;
        }
        fclose(f);
    }
    return value;
}

// current clock of processor core `cpu` in kHz (0: not readable)
inline unsigned CurrentKHz(int cpu)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
    return ReadNumber(path);
}

struct Layout {
    static constexpr int kMax = 32;
    int count = 0;
    unsigned maxKHz[kMax] = {};
    unsigned slowest = 0, fastest = 0;  // highest clocks of the slowest / fastest kind

    // 0 the slowest kind of core, 2 the fastest, 1 in between; 2 when all are alike or the
    // clocks are not readable
    int Kind(int cpu) const
    {
        if (cpu < 0 || cpu >= count || !maxKHz[cpu] || slowest == fastest) {
            return 2;
        }
        return maxKHz[cpu] == fastest ? 2 : maxKHz[cpu] == slowest ? 0 : 1;
    }

    // "8 cores: 0-5 at most 1586 MHz, 6-7 at most 1794 MHz"
    void Describe(char* out, size_t size) const
    {
        int n = snprintf(out, size, "%d cores", count);
        if (!fastest) {
            snprintf(out + n, n < static_cast<int>(size) ? size - n : 0, ", clocks not readable");
            return;
        }
        for (int i = 0; i < count && n < static_cast<int>(size);) {
            int j = i;
            while (j + 1 < count && maxKHz[j + 1] == maxKHz[i]) {
                j++;
            }
            if (j > i) {
                n += snprintf(out + n, size - n, "%s %d-%d at most %u MHz", i ? "," : ":", i, j, maxKHz[i] / 1000);
            } else {
                n += snprintf(out + n, size - n, "%s %d at most %u MHz", i ? "," : ":", i, maxKHz[i] / 1000);
            }
            i = j + 1;
        }
    }
};

inline const Layout& GetLayout()
{
    static const Layout layout = [] {
        Layout l;
        char path[96];
        for (int cpu = 0; cpu < Layout::kMax; cpu++) {
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d", cpu);
            if (access(path, F_OK) != 0) {
                break;
            }
            snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
            l.maxKHz[cpu] = ReadNumber(path);
            l.count = cpu + 1;
        }
        for (int cpu = 0; cpu < l.count; cpu++) {
            const unsigned khz = l.maxKHz[cpu];
            if (khz && (!l.slowest || khz < l.slowest)) {
                l.slowest = khz;
            }
            if (khz > l.fastest) {
                l.fastest = khz;
            }
        }
        return l;
    }();
    return layout;
}

// Per picture: processor time and the kind of core the thread was on when the picture ended.
struct ThreadUse {
    long long cpuNs = 0;
    unsigned onKind[3] = {};
    unsigned pictures = 0;
    long long lastNs = 0;
    int lastCpu = -1;

    // at the end of a picture of the calling thread
    void Picture()
    {
        const long long now = ThreadNs();
        if (lastNs) {
            cpuNs += now - lastNs;
            pictures++;
            lastCpu = sched_getcpu();
            onKind[GetLayout().Kind(lastCpu)]++;
        }
        lastNs = now;
    }

    // "11.2 ms of processor time each, on fast/middle/slow cores 80/0/20 %, last on core 6 at
    // 1794 MHz"; then counts from 0
    void Describe(char* out, size_t size)
    {
        const Layout& l = GetLayout();
        const double n = pictures ? pictures : 1;
        int len = snprintf(out, size, "%.1f ms of processor time each", cpuNs / 1e6 / n);
        if (l.slowest != l.fastest && len < static_cast<int>(size)) {
            len += snprintf(out + len, size - len, ", on fast/middle/slow cores %.0f/%.0f/%.0f %%",
                            onKind[2] * 100.0 / n, onKind[1] * 100.0 / n, onKind[0] * 100.0 / n);
        }
        if (lastCpu >= 0 && len < static_cast<int>(size)) {
            const unsigned khz = CurrentKHz(lastCpu);
            if (khz) {
                snprintf(out + len, size - len, ", last on core %d at %u MHz", lastCpu, khz / 1000);
            } else {
                snprintf(out + len, size - len, ", last on core %d", lastCpu);
            }
        }
        cpuNs = 0;
        pictures = 0;
        onKind[0] = onKind[1] = onKind[2] = 0;
    }
};

// the calling thread's id (as the system's tools and the performance hint name it)
inline int ThreadId()
{
    return static_cast<int>(syscall(SYS_gettid));
}

// The cores KeepOffSlowCores gave the calling thread (KeepOnFastCores puts it back there).
struct WantedCores {
    cpu_set_t set;
    bool on;
};
inline WantedCores& Wanted()
{
    static thread_local WantedCores w;
    return w;
}

// Keeps the calling thread off the slowest kind of core (decision of the project owner,
// 23.10.2026): the game's thread and the renderer's each need a whole core, and Android put them
// on the slow ones at times (Retroid: the core thread 40 % of the pictures on them at the title;
// Galaxy A30s: two fast cores, six slow). Not when all cores are alike, the clocks are not
// readable or RE4_FAST_CORES=0. `out`: the cores the thread may use now, or why it stays as it was.
inline bool KeepOffSlowCores(const Layout& l, char* out, size_t size)
{
    const char* env = getenv("RE4_FAST_CORES");
    if (env && *env == '0') {
        snprintf(out, size, "left to Android (RE4_FAST_CORES=0)");
        return false;
    }
    if (!l.fastest || l.slowest == l.fastest) {
        snprintf(out, size, "left to Android (%s)", l.fastest ? "all cores alike" : "clocks not readable");
        return false;
    }
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        snprintf(out, size, "left to Android (its cores not readable)");
        return false;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    int n = 0, len = snprintf(out, size, "on cores");
    for (int cpu = 0; cpu < l.count; cpu++) {
        if (l.Kind(cpu) == 0 || !CPU_ISSET(cpu, &allowed)) {
            continue;
        }
        CPU_SET(cpu, &set);
        n++;
        if (len < static_cast<int>(size)) {
            len += snprintf(out + len, size - len, " %d", cpu);
        }
    }
    if (!n) {
        snprintf(out, size, "left to Android (no faster core allowed to the app)");
        return false;
    }
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        snprintf(out, size, "left to Android (not allowed: %s)", strerror(errno));
        return false;
    }
    if (len < static_cast<int>(size)) {
        snprintf(out + len, size - len, ", off the slowest (at most %u MHz)", l.slowest / 1000);
    }
    Wanted().set = set;
    Wanted().on = true;
    return true;
}

// The calling thread back on every core Android allows (the menu's "Force Performance: Default"
// after "Use performance cores").
inline void ReleaseCores()
{
    Wanted().on = false;
    cpu_set_t all;
    CPU_ZERO(&all);
    const long n = sysconf(_SC_NPROCESSORS_CONF);
    for (long cpu = 0; cpu < n && cpu < CPU_SETSIZE; cpu++) {
        CPU_SET(cpu, &all);
    }
    sched_setaffinity(0, sizeof(all), &all);
}

// Android gives a thread all the cores of its group again when it moves the app between its
// groups (cpusets: a dialog, the app menu, the screen; realme reports 24.10.2026: the game thread
// placed on the fast cores ran 90 % on the slow ones a minute later). Called now and then by a
// thread KeepOffSlowCores placed: back on its cores. true when it had to.
inline bool KeepOnFastCores()
{
    WantedCores& w = Wanted();
    if (!w.on) {
        return false;
    }
    cpu_set_t now;
    CPU_ZERO(&now);
    if (sched_getaffinity(0, sizeof(now), &now) != 0 || CPU_EQUAL(&now, &w.set)) {
        return false;
    }
    return sched_setaffinity(0, sizeof(w.set), &w.set) == 0;
}

inline bool KeepOffSlowCores(char* out, size_t size)
{
    return KeepOffSlowCores(GetLayout(), out, size);
}

// Android's performance hint (APerformanceHint of the NDK, Android 13; looked up at run time:
// older Android and the Linux builds have none, and nothing happens). A session for the threads
// that make one picture, told after each picture how long its work took against the target; the
// system raises the clocks or moves the threads to faster cores before pictures come late.
// RE4_PERF_HINT=0 leaves it out.
class PerfHint {
public:
    // false: no session; `why` says why ("on" when there is one)
    bool Open(const int* tids, int count, long long targetNs, const char** why)
    {
        Close();
        const char* env = getenv("RE4_PERF_HINT");
        if (env && *env == '0') {
            *why = "off (RE4_PERF_HINT=0)";
            return false;
        }
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        if (!lib) {
            *why = "not on this system";
            return false;
        }
        using Manager = void* (*)();
        using Create = void* (*)(void*, const int32_t*, size_t, int64_t);
        const auto manager = reinterpret_cast<Manager>(dlsym(lib, "APerformanceHint_getManager"));
        const auto create = reinterpret_cast<Create>(dlsym(lib, "APerformanceHint_createSession"));
        report_ = reinterpret_cast<int (*)(void*, int64_t)>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
        close_ = reinterpret_cast<void (*)(void*)>(dlsym(lib, "APerformanceHint_closeSession"));
        if (!manager || !create || !report_) {
            *why = "not on this Android (before 13)";
            return false;
        }
        void* m = manager();
        int32_t ids[16];
        const int n = count < 16 ? count : 16;
        for (int i = 0; i < n; i++) {
            ids[i] = tids[i];
        }
        session_ = m ? create(m, ids, static_cast<size_t>(n), targetNs) : nullptr;
        *why = session_ ? "on" : "the system gave no session";
        return session_ != nullptr;
    }

    // the work of one picture
    void Report(long long ns)
    {
        if (session_ && ns > 0) {
            report_(session_, ns);
        }
    }

    bool On() const { return session_ != nullptr; }

    void Close()
    {
        if (session_ && close_) {
            close_(session_);
        }
        session_ = nullptr;
    }

private:
    void* session_ = nullptr;
    int (*report_)(void*, int64_t) = nullptr;
    void (*close_)(void*) = nullptr;
};

}  // namespace re4cpu
