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

// the clock core `cpu` may reach now in kHz: its scaling_max_freq, which the system lowers while the device is hot
// (0: not readable)
inline unsigned AllowedKHz(int cpu)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
    return ReadNumber(path);
}

// core `cpu` is online (switched on); true when the system does not say (core 0 has no switch)
inline bool CoreOnline(int cpu)
{
    char path[96];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);
    if (access(path, R_OK) != 0) {
        return true;
    }
    char c = '1';
    if (FILE* f = fopen(path, "r")) {
        if (fread(&c, 1, 1, f) != 1) {
            c = '1';
        }
        fclose(f);
    }
    return c != '0';
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
    int role;  // CoreRole: the render thread's cores follow the fastest one allowed now (KeepOnFastCores)
};
inline WantedCores& Wanted()
{
    static thread_local WantedCores w;
    return w;
}
inline bool KeepOnFastCores();  // below

// Which cores KeepOffSlowCores gives a thread. With three kinds of core (a prime core: the Retroid Pocket Flip 2's
// Snapdragon 865 has one at 2841 MHz, three at 2419 and four at 1804) the render thread gets the fastest kind alone
// and the game thread the middle kind (wish of the project owner, 06.11.2026: at the lake's dock the render thread
// took 9-12 ms of processor time a picture on the middle cores at 1862 MHz while the fastest core stayed unused).
// With two kinds, or RE4_PRIME_CORE=0, both stay off the slowest kind as before.
enum CoreRole { kAnyThread = 0, kRenderThread = 1, kGameThread = 2 };

// A middle kind of core exists (three kinds), so the fastest kind can be one thread's.
inline bool HasPrimeCores(const Layout& l)
{
    const char* env = getenv("RE4_PRIME_CORE");
    if (env && *env == '0') {
        return false;
    }
    for (int cpu = 0; cpu < l.count; cpu++) {
        if (l.maxKHz[cpu] && l.Kind(cpu) == 1) {
            return true;
        }
    }
    return false;
}

// Keeps the calling thread off the slowest kind of core (decision of the project owner,
// 23.10.2026): the game's thread and the renderer's each need a whole core, and Android put them
// on the slow ones at times (Retroid: the core thread 40 % of the pictures on them at the title;
// Galaxy A30s: two fast cores, six slow). Not when all cores are alike, the clocks are not
// readable or RE4_FAST_CORES=0. `out`: the cores the thread may use now, or why it stays as it was.
// `role`: see CoreRole (the fastest kind for the render thread, the middle one for the game thread).
inline bool KeepOffSlowCores(const Layout& l, char* out, size_t size, int role = kAnyThread)
{
    const bool prime = role != kAnyThread && HasPrimeCores(l);
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
        if (prime && (role == kRenderThread ? l.Kind(cpu) != 2 : l.Kind(cpu) == 2)) {
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
        const int error = errno;
        if (prime && role == kRenderThread) {
            // the fastest core refused now: off the slowest meanwhile; KeepOnFastCores tries the fastest again
            Wanted().set = set;
            Wanted().on = true;
            Wanted().role = kRenderThread;
            const bool off = KeepOnFastCores();
            snprintf(out, size, "%s (the fastest core refused now: %s)", off ? "off the slowest cores" : "left to Android",
                     strerror(error));
            return off;
        }
        snprintf(out, size, "left to Android (not allowed: %s)", strerror(error));
        return false;
    }
    if (len < static_cast<int>(size)) {
        if (prime) {
            snprintf(out + len, size - len, role == kRenderThread ? ", the fastest (at most %u MHz) for it alone"
                                                                  : ", off the slowest and off the fastest (at most %u MHz, the render thread's)",
                     l.fastest / 1000);
        } else {
            snprintf(out + len, size - len, ", off the slowest (at most %u MHz)", l.slowest / 1000);
        }
    }
    Wanted().set = set;
    Wanted().on = true;
    Wanted().role = prime ? role : kAnyThread;
    return true;
}

// The render thread's cores now (prime cores): the online cores off the slowest kind whose allowed clock is the
// highest (AllowedKHz, the thermal limit included). The fastest kind normally; when it is switched off or held
// below the middle kind while the device is hot, the middle cores (the project owner's log 06.11.2026: put on
// core 7, the render thread still ran on the middle cores at 1286 MHz most of the run). False when nothing is known.
inline bool FastestCoresNow(const Layout& l, cpu_set_t* set)
{
    CPU_ZERO(set);
    unsigned best = 0;
    unsigned allowed[Layout::kMax] = {};
    for (int cpu = 0; cpu < l.count; cpu++) {
        if (!l.maxKHz[cpu] || l.Kind(cpu) == 0 || !CoreOnline(cpu)) {
            continue;
        }
        allowed[cpu] = AllowedKHz(cpu);
        if (!allowed[cpu]) {
            allowed[cpu] = l.maxKHz[cpu];
        }
        best = allowed[cpu] > best ? allowed[cpu] : best;
    }
    int n = 0;
    for (int cpu = 0; cpu < l.count && best; cpu++) {
        if (allowed[cpu] == best) {
            CPU_SET(cpu, set);
            n++;
        }
    }
    return n > 0;
}

// "cores now: 4 1286/2419, 5 1286/2419, 6 1574/2419, 7 1516/2841 MHz (now/allowed), 7 offline": the cores off the
// slowest kind, their clock and the clock they may reach now (each step of the benchmark logs it: throttling).
inline void DescribeClocks(const Layout& l, char* out, size_t size)
{
    int len = snprintf(out, size, "cores now:");
    bool any = false;
    for (int cpu = 0; cpu < l.count && len < static_cast<int>(size); cpu++) {
        if (!l.maxKHz[cpu] || l.Kind(cpu) == 0) {
            continue;
        }
        if (!CoreOnline(cpu)) {
            len += snprintf(out + len, size - len, "%s %d offline", any ? "," : "", cpu);
        } else {
            len += snprintf(out + len, size - len, "%s %d %u/%u", any ? "," : "", cpu, CurrentKHz(cpu) / 1000,
                            AllowedKHz(cpu) / 1000);
        }
        any = true;
    }
    if (len < static_cast<int>(size)) {
        snprintf(out + len, size - len, any ? " MHz (now/allowed)" : " clocks not readable");
    }
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
    // the render thread on prime cores: the fastest cores allowed now (switched off or held back while hot: others)
    if (w.role == kRenderThread) {
        cpu_set_t best;
        if (FastestCoresNow(GetLayout(), &best)) {
            w.set = best;
        }
    }
    cpu_set_t now;
    CPU_ZERO(&now);
    if (sched_getaffinity(0, sizeof(now), &now) != 0 || CPU_EQUAL(&now, &w.set)) {
        return false;
    }
    if (sched_setaffinity(0, sizeof(w.set), &w.set) == 0) {
        return true;
    }
    // the fastest core refused (Android keeps the app in a group of cores without it now): the render thread on the
    // cores off the slowest kind meanwhile, the fastest again at a later call (the project owner's log 06.11.2026: it
    // sat on the middle cores while core 7 was free)
    if (w.role == kRenderThread) {
        const Layout& l = GetLayout();
        cpu_set_t off;
        CPU_ZERO(&off);
        for (int cpu = 0; cpu < l.count; cpu++) {
            if (l.maxKHz[cpu] && l.Kind(cpu) != 0 && CoreOnline(cpu)) {
                CPU_SET(cpu, &off);
            }
        }
        return !CPU_EQUAL(&now, &off) && sched_setaffinity(0, sizeof(off), &off) == 0;
    }
    return false;
}

// "cores 4 5 6": the cores the calling thread may use now (what Android and KeepOffSlowCores left it)
inline void AffinityText(char* out, size_t size)
{
    cpu_set_t now;
    CPU_ZERO(&now);
    int len = snprintf(out, size, "cores");
    if (sched_getaffinity(0, sizeof(now), &now) != 0) {
        snprintf(out, size, "cores not readable");
        return;
    }
    const long n = sysconf(_SC_NPROCESSORS_CONF);
    for (long cpu = 0; cpu < n && cpu < CPU_SETSIZE && len < static_cast<int>(size); cpu++) {
        if (CPU_ISSET(cpu, &now)) {
            len += snprintf(out + len, size - len, " %ld", cpu);
        }
    }
}

inline bool KeepOffSlowCores(char* out, size_t size, int role = kAnyThread)
{
    return KeepOffSlowCores(GetLayout(), out, size, role);
}

// The cores faster than the slowest kind (0 when all are alike or the clocks are not readable).
inline int FasterCores(const Layout& l)
{
    if (!l.fastest || l.slowest == l.fastest) {
        return 0;
    }
    int n = 0;
    for (int cpu = 0; cpu < l.count; cpu++) {
        n += l.maxKHz[cpu] && l.Kind(cpu) != 0;
    }
    return n;
}

// Puts thread `tid` (a helper, e.g. a vertex thread) on the slowest kind of core when no more than
// two cores are faster: those two are for the game thread and the render thread, which each need a
// whole one (decision of the project owner, 29.10.2026; Redmi 15C, Helio G81 with two fast cores
// and six slow ones, 28.10.2026: in r101 the render thread had 29.8 ms of processor time in 37.7 ms,
// the game thread 22 ms in 43 ms, with the vertex threads on the two fast cores too; Galaxy S7: two
// and two). With more fast cores (Retroid: four) the helper stays as it is. RE4_FAST_CORES=0 or
// RE4_HELPERS_SLOW=0 leave it. `set` (may be null): the cores it got. `out`: what happened.
inline bool PutOnSlowCores(const Layout& l, int tid, cpu_set_t* set, char* out, size_t size)
{
    const char* env = getenv("RE4_FAST_CORES");
    const char* helpers = getenv("RE4_HELPERS_SLOW");
    if ((env && *env == '0') || (helpers && *helpers == '0')) {
        snprintf(out, size, "left to Android (%s=0)", env && *env == '0' ? "RE4_FAST_CORES" : "RE4_HELPERS_SLOW");
        return false;
    }
    const int faster = FasterCores(l);
    if (!faster) {
        snprintf(out, size, "left to Android (%s)", l.fastest ? "all cores alike" : "clocks not readable");
        return false;
    }
    if (faster > 2) {
        snprintf(out, size, "left to Android (%d faster cores: room for them too)", faster);
        return false;
    }
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(tid, sizeof(allowed), &allowed) != 0) {
        snprintf(out, size, "left to Android (its cores not readable)");
        return false;
    }
    cpu_set_t slow;
    CPU_ZERO(&slow);
    int n = 0, len = snprintf(out, size, "on cores");
    for (int cpu = 0; cpu < l.count; cpu++) {
        if (l.Kind(cpu) != 0 || !CPU_ISSET(cpu, &allowed)) {
            continue;
        }
        CPU_SET(cpu, &slow);
        n++;
        if (len < static_cast<int>(size)) {
            len += snprintf(out + len, size - len, " %d", cpu);
        }
    }
    if (!n) {
        snprintf(out, size, "left to Android (no slow core allowed to the app)");
        return false;
    }
    if (sched_setaffinity(tid, sizeof(slow), &slow) != 0) {
        snprintf(out, size, "left to Android (not allowed: %s)", strerror(errno));
        return false;
    }
    if (len < static_cast<int>(size)) {
        snprintf(out + len, size - len, ", the slowest (only %d faster cores)", faster);
    }
    if (set) {
        *set = slow;
    }
    return true;
}

// Thread `tid` back on every core Android allows (after PutOnSlowCores).
inline void ReleaseThread(int tid)
{
    cpu_set_t all;
    CPU_ZERO(&all);
    const long n = sysconf(_SC_NPROCESSORS_CONF);
    for (long cpu = 0; cpu < n && cpu < CPU_SETSIZE; cpu++) {
        CPU_SET(cpu, &all);
    }
    sched_setaffinity(tid, sizeof(all), &all);
}

// Thread `tid` back on `set` when Android gave it other cores (as KeepOnFastCores). true when it had to.
inline bool KeepThreadOn(int tid, const cpu_set_t& set)
{
    cpu_set_t now;
    CPU_ZERO(&now);
    if (sched_getaffinity(tid, sizeof(now), &now) != 0 || CPU_EQUAL(&now, &set)) {
        return false;
    }
    return sched_setaffinity(tid, sizeof(set), &set) == 0;
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
