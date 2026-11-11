// Video interface: the retrace interrupt and its callbacks.
//
// The game counts frames in its VI post-retrace callback (postVSyncCallback:
// vsync_cnt, iTaskSuspend, the hang check) and paces the main loop by waiting
// for that count. The port has no interrupts; retraces are delivered at
// interrupt points instead: when every thread waits (the dispatcher's idle),
// in VIWaitForRetrace, in the main loop's vsync waits (patch 0012) and at the
// checkpoints of long background work. A retrace is due every NTSC field
// (1/59.94 s) of real time; RE4_VI_FAST=1 delivers the next one immediately
// whenever the game waits (tests, no pacing).
//
// Locked to the display (M9): the app reports each vsync of its display
// (re4_vi_vsync, Choreographer). A display within 0.5 % of 59.94 Hz (60 Hz,
// or 120 Hz with two vsyncs a field) then sets the field: one retrace per
// vsync (or two), shortly after it. A 60 Hz panel and the 59.94 Hz of the
// GameCube drift one picture apart every 16.7 s, which showed as a short
// stutter in movies (a picture held three vsyncs). The game then runs 0.1 %
// faster; the app's sound output takes up such a difference. Without vsyncs
// for 250 ms (app paused, Linux) or RE4_VI_VSYNC=0: the NTSC field again.
//
// 0.99.9 (frame pacing, user suggestion 11.11.2026): within 1 % (was 0.5 %: a
// "60 Hz" screen measured at 60.5 Hz ran on the own clock and showed a picture
// three vsyncs long about every 1.8 s), and a screen on which two fields make a
// whole number of vsyncs (90 Hz: three) locks as well: a field of one and a
// half vsyncs, the retraces on the grid of half a vsync (every second one just
// after a vsync), so the game's 30 pictures a second come every three vsyncs
// (before: on the own clock, a picture only two vsyncs long about every 1.7 s
// on the Galaxy A24's 89.4 Hz). 60 pictures a second (movies, the title) stay
// uneven on 90 Hz: one and two vsyncs by turns, as before.
//
// RE4_VI_SIM_HZ=f (PC checks): a display of f Hz simulated (its vsyncs as the
// app reports them), and each picture the game ends (VIFlush) noted at the
// vsync it would be shown at (the first one at least 1 ms later): the app's
// "picture pacing" line (picture_cadence.h) every 10 s in the log.
#include "re4_runtime.h"
#include "../../android-decomp/app/src/main/cpp/picture_cadence.h"

#include <atomic>
#include <stdlib.h>
#include <time.h>

namespace {

typedef unsigned u32;
typedef void (*VIRetraceCallback)(u32 retraceCount);

const long long kFieldNs = 1000000000LL * 1001 / 60000;

// the display's vsync (re4_vi_vsync): the newest time and the smoothed period
std::atomic<long long> gVsyncAt{0};
std::atomic<long long> gVsyncPeriod{0};
bool gVsyncOff;
// RE4_VI_SIM_HZ: the simulated display's period and first vsync (0: none), the pictures' cadence
long long gSimPeriod, gSimT0;
re4gx::PictureCadence gSimCadence;
const long long kVsyncStale = 250000000LL;  // ns without a vsync: the own clock
const long long kVsyncLead = 1000000LL;     // the retrace this long after the vsync

VIRetraceCallback gPre;
VIRetraceCallback gPost;
u32 gRetraceCount;
long long gNextRetrace;
long long gIdleNs;  // time the CPU slept for the next retrace (re4_idle_ns)
bool gStarted;
bool gFast;
bool gDelivering;
void* gNextFrameBuffer;
void* gCurrentFrameBuffer;

long long nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long) ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void start()
{
    if (!gStarted) {
        gStarted = true;
        const char* fast = getenv("RE4_VI_FAST");
        gFast = fast && *fast && *fast != '0';
        const char* vsync = getenv("RE4_VI_VSYNC");
        gVsyncOff = vsync && *vsync == '0';
        gNextRetrace = nowNs() + kFieldNs;
        const char* sim = getenv("RE4_VI_SIM_HZ");
        if (sim && atof(sim) >= 20.0 && atof(sim) <= 250.0) {
            gSimPeriod = (long long) (1e9 / atof(sim) + 0.5);
            gSimT0 = nowNs();
        }
    }
}

// the display's latest vsync and period at `now`: the app's, or the simulated display's (RE4_VI_SIM_HZ)
void displayNow(long long now, long long* at, long long* period)
{
    if (gSimPeriod > 0) {
        *period = gSimPeriod;
        *at = now < gSimT0 ? gSimT0 : gSimT0 + (now - gSimT0) / gSimPeriod * gSimPeriod;
        return;
    }
    *at = gVsyncAt.load();
    *period = gVsyncPeriod.load();
}

}  // namespace

// The field for the display with vsyncs `period` apart, the last at `at`
// (0: none), else 0: a whole number of vsyncs within 1 % of the NTSC field (60,
// 120 Hz), else two fields a whole, odd number of vsyncs within 1 % (90 Hz:
// three; the field is then one and a half vsyncs).
extern "C" long long re4_vi_locked_field(long long now, long long at, long long period)
{
    if (at <= 0 || period <= 0 || now - at > kVsyncStale) {
        return 0;
    }
    long long n = (kFieldNs + period / 2) / period;
    if (n >= 1 && n <= 4) {
        long long field = n * period;
        long long off = field > kFieldNs ? field - kFieldNs : kFieldNs - field;
        if (off * 100 <= kFieldNs) {
            return field;
        }
    }
    long long m = (2 * kFieldNs + period / 2) / period;
    if (m >= 1 && m <= 7 && m % 2 == 1) {
        long long two = m * period;
        long long off = two > 2 * kFieldNs ? two - 2 * kFieldNs : 2 * kFieldNs - two;
        if (off * 100 <= 2 * kFieldNs) {
            return two / 2;
        }
    }
    return 0;
}

// The next retrace `next` (on the own grid of `field`) moved a little toward
// kVsyncLead after the vsync grid (`at`, `period`; a field of a half number of
// vsyncs: the grid of half a vsync): at most 1/50 of a field per retrace, an
// eighth of the distance, so it follows the display smoothly.
extern "C" long long re4_vi_lock_phase(long long next, long long field, long long at, long long period)
{
    long long target = at + kVsyncLead;
    if (field % period != 0) {
        period /= 2;
    }
    long long e = (next - target) % period;
    if (e < 0) {
        e += period;
    }
    if (e > period / 2) {
        e -= period;
    }
    long long step = e / 8, limit = field / 50;
    if (step > limit) {
        step = limit;
    } else if (step < -limit) {
        step = -limit;
    }
    return next - step;
}

// A vsync of the display at `ns` (CLOCK_MONOTONIC, any thread): the period is
// smoothed over the intervals (a late callback counts as several vsyncs).
extern "C" void re4_vi_vsync(long long ns)
{
    long long last = gVsyncAt.exchange(ns);
    if (last <= 0 || ns <= last) {
        return;
    }
    long long d = ns - last, p = gVsyncPeriod.load();
    if (p == 0) {
        if (d >= 4000000 && d <= 34000000) {
            gVsyncPeriod.store(d);
        }
        return;
    }
    long long n = (d + p / 2) / p;
    if (n >= 1 && n <= 4) {
        long long one = d / n;
        gVsyncPeriod.store(p + (one - p) / 16);
    } else if (d > 0 && d < p / 2) {
        gVsyncPeriod.store(0);  // much shorter than thought: start over
    }
}

namespace {

long long fieldNs(long long now)
{
    if (gVsyncOff) {
        return kFieldNs;
    }
    long long at = 0, period = 0;
    displayNow(now, &at, &period);
    long long f = re4_vi_locked_field(now, at, period);
    return f ? f : kFieldNs;
}

// the retrace after the one due at gNextRetrace, seen at `now`
long long nextRetrace(long long now)
{
    long long field = fieldNs(now);
    long long next = now + field - (now - gNextRetrace) % field;
    if (field != kFieldNs) {
        long long at = 0, period = 0;
        displayNow(now, &at, &period);
        next = re4_vi_lock_phase(next, field, at, period);
    }
    return next;
}

void deliver()
{
    gDelivering = true;
    gRetraceCount++;
    gCurrentFrameBuffer = gNextFrameBuffer;
    re4_os_interrupt_begin();
    if (gPre) {
        gPre(gRetraceCount);
    }
    if (gPost) {
        gPost(gRetraceCount);
    }
    // the audio blocks of this field (runtime/ai.cpp: AX, the sound driver's tick)
    re4_ai_field();
    // the graphics processor's interrupts of what it finished meanwhile
    re4_gx_deliver_interrupts();
    re4_os_interrupt_end();
    gDelivering = false;
}

}  // namespace

extern "C" {

int re4_interrupt_point(void)
{
    if (!gStarted || gDelivering || gFast) {
        return 0;
    }
    long long now = nowNs();
    if (now < gNextRetrace) {
        return 0;
    }
    // Retraces missed while the CPU was busy are counted at once; the clock
    // then restarts from now instead of catching up further.
    int due = (int) ((now - gNextRetrace) / fieldNs(now)) + 1;
    if (due > 4) {
        due = 4;
    }
    gNextRetrace = nextRetrace(now);
    for (int i = 0; i < due; i++) {
        deliver();
    }
    return due;
}

// Waits for the next interrupt and delivers it (the CPU idles until then).
void re4_os_idle(void)
{
    start();
    if (gDelivering) {
        return;
    }
    // what the EXI devices (memory card) and the alarms have pending comes
    // first: the card library continues its transfers in those handlers
    re4_exi_deliver();
    if (gFast) {
        deliver();
        return;
    }
    long long now = nowNs();
    if (now < gNextRetrace) {
        long long wait = gNextRetrace - now;
        struct timespec ts = {(time_t) (wait / 1000000000LL), (long) (wait % 1000000000LL)};
        nanosleep(&ts, 0);
        gIdleNs += nowNs() - now;
    }
    re4_interrupt_point();
}

long long re4_idle_ns(void)
{
    return gIdleNs;
}

// The clock of the CRI middleware (UTY_GetTmr, patch 0048). Sofdec times the
// picture against the ADX sound and smooths that with this clock
// (sfd_adxt.c sfadxt_UpdateTime). The sound advances with the fields
// delivered (re4_ai_field), on the GameCube both run in real time. With
// RE4_VI_FAST=1 (tests without pacing) fields come faster than real time, so
// the clock counts them too: a field is 1/59.94 s of the time base
// (OSGetTime's bus clock / 4). Otherwise it is OSGetTime.
long long OSGetTime(void);

long long re4_cri_timer(void)
{
    start();
    if (!gFast) {
        return OSGetTime();
    }
    return (long long) gRetraceCount * (162000000LL / 4 * 1001 / 60000);
}

void re4_wait_interrupt(void)
{
    re4_os_idle();
}

void VIInit(void)
{
    start();
}

void VIWaitForRetrace(void)
{
    start();
    u32 count = gRetraceCount;
    while (gRetraceCount == count) {
        re4_os_idle();
    }
}

u32 VIGetRetraceCount(void)
{
    return gRetraceCount;
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = gPre;
    gPre = cb;
    return old;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
    VIRetraceCallback old = gPost;
    gPost = cb;
    return old;
}

void VISetNextFrameBuffer(void* fb)
{
    gNextFrameBuffer = fb;
    re4_gx_vi_next(fb);
}

void* VIGetNextFrameBuffer(void)
{
    return gNextFrameBuffer;
}

void* VIGetCurrentFrameBuffer(void)
{
    return gCurrentFrameBuffer;
}

// VISetBlack: the VI outputs black from the next VIFlush on, as on the
// GameCube (the game hides a frame it does not want seen: the title without its
// fade before a demo movie, room changes). The GX chunks carry it to the app.
static bool gBlackNext;

void VISetBlack(int black)
{
    gBlackNext = black != 0;
}

void VIConfigure(const void* renderMode)
{
    (void) renderMode;
}

// The picture the game set up goes to the screen with the next retrace; the
// GX decoder decides now whether the last display copy is shown or held.
void VIFlush(void)
{
    re4_gx_vi_black(gBlackNext);
    re4_gx_vi_flush();
    if (gSimPeriod > 0) {
        // the simulated display (RE4_VI_SIM_HZ): this picture at the first vsync at least 1 ms later
        long long now = nowNs() + 1000000;
        long long shown = gSimT0 + (now - gSimT0 + gSimPeriod - 1) / gSimPeriod * gSimPeriod;
        std::string line;
        if (gSimCadence.Note(shown, gSimPeriod, 1e9 / (double) gSimPeriod, &line)) {
            re4_log("%s (simulated, the field %.3f ms)", line.c_str(), fieldNs(nowNs()) / 1e6);
        }
    }
}

u32 VIGetTvFormat(void)
{
    return 0;  // VI_NTSC
}

u32 VIGetDTVStatus(void)
{
    return 0;  // no component cable: interlaced output
}

u32 VIGetNextField(void)
{
    return gRetraceCount & 1;
}

}  // extern "C"
