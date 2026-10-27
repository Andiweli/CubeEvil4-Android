// Dolphin OS replacement: boot, console info, time, interrupts and the debug
// output. The heap allocator itself is the SDK's own OSAlloc.c/OSArena.c
// (src/lib), compiled unchanged into libre4_main.
#include "re4_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" {
void OSSetArenaLo(void* lo);
void OSSetArenaHi(void* hi);
}

namespace {

typedef long long OSTime;

// OS_TIMER_CLOCK: a quarter of the 162 MHz bus clock.
const long long kTimerClock = 162000000 / 4;

struct CalendarTime {
    int sec, min, hour, mday, mon, year, wday, yday, msec, usec;
};

// OSReport text arrives in pieces ("FST Address = ", "\n"); the log gets lines.
char gReportLine[1024];
size_t gReportLen;

void reportText(const char* text)
{
    for (const char* p = text; *p; p++) {
        if (*p == '\n' || gReportLen == sizeof(gReportLine) - 1) {
            gReportLine[gReportLen] = 0;
            re4_log("OSReport: %s", gReportLine);
            gReportLen = 0;
            if (*p == '\n') {
                continue;
            }
        }
        gReportLine[gReportLen++] = *p;
    }
}

OSTime monotonicTicks()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (OSTime) ts.tv_sec * kTimerClock + (OSTime) ts.tv_nsec * kTimerClock / 1000000000;
}

// OSGetTime counts from 2000-01-01 00:00 local time (the GameCube epoch).
const time_t kEpoch2000 = 946684800;
OSTime gTimeOffset;
bool gTimeOffsetSet;

int gInterruptsEnabled = 1;
unsigned gProgressiveMode;
unsigned gSoundMode = 1;  // stereo

}  // namespace

extern "C" {

void OSInit(void)
{
    if (!re4_mem1_init()) {
        re4_log("OSInit: GameCube main memory is unavailable, cannot continue");
        abort();
    }
    OSSetArenaLo((void*) (unsigned long) RE4_MEM1_ARENA_LO);
    OSSetArenaHi((void*) (unsigned long) RE4_MEM1_FST_ADDR);
}

void OSReport(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    reportText(buf);
}

void OSPanic(const char* file, int line, const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    re4_log("OSPanic %s:%d: %s", file, line, buf);
    abort();
}

unsigned OSGetConsoleType(void)
{
    return RE4_CONSOLE_TYPE;
}

unsigned OSGetConsoleSimulatedMemSize(void)
{
    return RE4_MEM1_RETAIL_SIZE;
}

unsigned OSGetPhysicalMemSize(void)
{
    return RE4_MEM1_RETAIL_SIZE;
}

OSTime OSGetTime(void)
{
    if (!gTimeOffsetSet) {
        // the console's clock runs in local time (set in the IPL, no time
        // zone): the dates of the saves (card.cpp makeSaveData) are the
        // device's local time
        time_t now = time(0);
        struct tm local;
        long zone = localtime_r(&now, &local) ? local.tm_gmtoff : 0;
        gTimeOffset = (OSTime) (now + zone - kEpoch2000) * kTimerClock - monotonicTicks();
        gTimeOffsetSet = true;
    }
    return monotonicTicks() + gTimeOffset;
}

unsigned OSGetTick(void)
{
    return (unsigned) OSGetTime();
}

void OSTicksToCalendarTime(OSTime ticks, CalendarTime* td)
{
    OSTime secs = ticks / kTimerClock;
    OSTime rest = ticks % kTimerClock;
    if (rest < 0) {
        rest += kTimerClock;
        secs--;
    }
    time_t t = (time_t) (secs + kEpoch2000);
    struct tm tm;
    gmtime_r(&t, &tm);
    td->sec = tm.tm_sec;
    td->min = tm.tm_min;
    td->hour = tm.tm_hour;
    td->mday = tm.tm_mday;
    td->mon = tm.tm_mon;
    td->year = tm.tm_year + 1900;
    td->wday = tm.tm_wday;
    td->yday = tm.tm_yday;
    td->msec = (int) (rest * 1000 / kTimerClock);
    td->usec = (int) (rest * 1000000 / kTimerClock % 1000);
}

OSTime OSCalendarTimeToTicks(CalendarTime* td)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_sec = td->sec;
    tm.tm_min = td->min;
    tm.tm_hour = td->hour;
    tm.tm_mday = td->mday;
    tm.tm_mon = td->mon;
    tm.tm_year = td->year - 1900;
    OSTime secs = (OSTime) timegm(&tm) - kEpoch2000;
    return secs * kTimerClock + (OSTime) td->msec * (kTimerClock / 1000) +
           (OSTime) td->usec * (kTimerClock / 1000000);
}

// There are no hardware interrupts; the game's interrupt-time work (the VI
// retrace callback, the interrupt task scheduler) is driven by the frame loop.
int OSDisableInterrupts(void)
{
    int old = gInterruptsEnabled;
    gInterruptsEnabled = 0;
    return old;
}

int OSEnableInterrupts(void)
{
    int old = gInterruptsEnabled;
    gInterruptsEnabled = 1;
    return old;
}

int OSRestoreInterrupts(int level)
{
    int old = gInterruptsEnabled;
    gInterruptsEnabled = level;
    return old;
}

// Hardware interrupt handlers (__OSSetInterruptHandler). The port's devices
// raise them themselves: the emulated graphics processor its PE "draw done"
// and "token" interrupts (gx_fifo.cpp, through re4_os_hw_interrupt).
typedef void (*InterruptHandler)(short interrupt, void* context);
static InterruptHandler gHandlers[32];

InterruptHandler __OSSetInterruptHandler(short interrupt, InterruptHandler handler)
{
    if (interrupt < 0 || interrupt >= 32) {
        return 0;
    }
    InterruptHandler old = gHandlers[interrupt];
    gHandlers[interrupt] = handler;
    return old;
}

InterruptHandler __OSGetInterruptHandler(short interrupt)
{
    return interrupt >= 0 && interrupt < 32 ? gHandlers[interrupt] : 0;
}

// Interrupt masks: every handler the port raises is delivered.
unsigned __OSMaskInterrupts(unsigned mask)
{
    (void) mask;
    return 0;
}

unsigned __OSUnmaskInterrupts(unsigned mask)
{
    (void) mask;
    return 0;
}

// Runs the handler of `interrupt` as the hardware would: in interrupt
// context (thread switches it asks for wait until it returns). 0 when none is
// registered.
int re4_os_hw_interrupt(short interrupt)
{
    InterruptHandler handler = __OSGetInterruptHandler(interrupt);
    if (!handler) {
        return 0;
    }
    re4_os_interrupt_begin();
    handler(interrupt, 0);
    re4_os_interrupt_end();
    return 1;
}

// Exception contexts: the port switches no register state.
void OSClearContext(void* context)
{
    (void) context;
}

void OSSetCurrentContext(void* context)
{
    (void) context;
}

// Reset callbacks (OSResetSystem is a stub) and library version strings.
void OSRegisterResetFunction(void* info)
{
    (void) info;
}

void OSRegisterVersion(const char* id)
{
    (void) id;
}

// Gekko special registers of the write-gather pipe (HID2, WPAR): the port's
// pipe passes every store on at once, so it is never busy.
unsigned PPCMfhid2(void)
{
    return 0;
}

void PPCMthid2(unsigned value)
{
    (void) value;
}

unsigned PPCMfwpar(void)
{
    return 0;
}

void PPCMtwpar(unsigned value)
{
    (void) value;
}

// Soft-reset survival region (pRK): process memory survives anyway.
void OSSetSaveRegion(void* start, void* end)
{
    (void) start;
    (void) end;
}

void* OSSetErrorHandler(unsigned short error, void* handler)
{
    (void) error;
    (void) handler;
    return 0;
}

int DBIsDebuggerPresent(void)
{
    return 0;
}

void OSInitAlarm(void) {}

unsigned OSGetResetButtonState(void)
{
    return 0;
}

unsigned OSGetProgressiveMode(void)
{
    return gProgressiveMode;
}

void OSSetProgressiveMode(unsigned on)
{
    gProgressiveMode = on;
}

unsigned OSGetSoundMode(void)
{
    return gSoundMode;
}

void OSSetSoundMode(unsigned mode)
{
    gSoundMode = mode;
}

// Caches and the locked cache: coherent host memory, nothing to do.
void DCFlushRange(void*, unsigned) {}
void DCFlushRangeNoSync(void*, unsigned) {}
void DCInvalidateRange(void*, unsigned) {}
void DCStoreRange(void*, unsigned) {}
void DCStoreRangeNoSync(void*, unsigned) {}
void LCEnable(void) {}
void PPCSync(void) {}

// RE4_START_ROOM: room the original debug start menu preselects (stage << 8 |
// room, hex). With it the title menu starts on "new game" (patch 0026), so a
// test script only presses A: new game, then the debug start menu jumps there.
static unsigned long debugStartRoom()
{
    const char* v = getenv("RE4_START_ROOM");
    unsigned long room = v && *v ? strtoul(v, 0, 16) : 0;
    return room >= 0x100 && room < 0x600 ? room : 0;
}

int re4_debug_start_room_set(void)
{
    return debugStartRoom() != 0;
}

// The debug build asks for the start room after "new game" (title.cpp
// titleExit): a debug menu with stage, room and jump point waits for A or
// START. Patch 0043 calls this every frame of it (open 1) and once after it
// (open 0). Its text needs debug_mode, which the app clears (RE4_DEBUG_HUD=0,
// state_trace.cpp) except while the menu is open. RE4_ROOM_MENU=0 skips the
// menu, as if A came at once (line 0, "DEBUG GAME" with the preset room).
static int g_roomMenuOpen;

int re4_title_room_menu(int open)
{
    static int shown = -1;
    if (shown < 0) {
        const char* v = getenv("RE4_ROOM_MENU");
        shown = !(v && v[0] == '0');
    }
    open = open && shown;
    if (open != g_roomMenuOpen) {
        re4_log("title: debug start menu (room selection) %s", open ? "open" : "closed");
    }
    g_roomMenuOpen = open;
    return open;
}

int re4_title_room_menu_open(void)
{
    return g_roomMenuOpen;
}

void re4_keep_vtable(void* object)
{
    (void) object;
}

// Without debug/roomInfo.dat (the European discs) the debug start menu reads a stand-in table
// with the new game's room alone (patch 0119, main.cpp systemRestartInit): no other room can be
// preselected, and the game starts as the retail one, the opening (r120) runs into r100.
static unsigned g_standInRoom;

void re4_room_info_stand_in(unsigned room)
{
    if (!g_standInRoom) {
        re4_log("debug: no debug/roomInfo.dat on these discs (debug build only): the debug start "
                "menu holds the new game's room r%03x alone", room);
    }
    g_standInRoom = room;
}

void re4_debug_start_room(unsigned short* room_id)
{
    if (unsigned long room = debugStartRoom()) {
        if (g_standInRoom && room != g_standInRoom) {
            re4_log("debug: start room r%03lx (RE4_START_ROOM) needs debug/roomInfo.dat; the new game "
                    "starts in r%03x%s", room, g_standInRoom,
                    room == 0x100 && g_standInRoom == 0x120 ? " and runs into r100 on its own" : "");
            return;
        }
        re4_log("debug: start room r%03lx preselected (RE4_START_ROOM)", room);
        *room_id = (unsigned short) room;
    }
}

}  // extern "C"
