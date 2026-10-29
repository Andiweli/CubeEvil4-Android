// Controllers (PAD). Channel 0 is a connected controller: the device's
// controller (the app, re4_pad_live) or a test script; channels 1-3 are empty
// (the title screen skips its logos when a second pad answers).
//
// PADRead hands out raw values like the hardware (main stick about +-100,
// C stick about +-90, triggers 0..255); the game's PadRead then calls the
// SDK's own PADClamp (src/lib/Padclamp.c, compiled unchanged): the dead zones
// and the octagon of the original controller.
//
//   RE4_PAD_SCRIPT="START@300 A@400-3000/40"
//     BUTTONS@FRAME            hold for 3 frames from PADRead call FRAME
//     BUTTONS@FROM-TO/EVERY    the same every EVERY frames from FROM to TO
//     BUTTONS: A B X Y Z L R START UP DOWN LEFT RIGHT, joined with '+';
//              STICKUP STICKDOWN STICKLEFT STICKRIGHT push the main stick
//              fully (BUTTONS@FROM-TO/1 holds it the whole time)
//   RE4_PAD_UNTIL_ROOM=1       the script ends when the game loads its first
//                              room (state_trace.cpp)
//   RE4_PAD_ROOM_SCRIPT="START@1500 STICKUP@1700-2400/1"
//                              then this one runs, its frames counted from
//                              that moment
//   RE4_PAD_FREE_SCRIPT="STICKUP@30-330/1"
//                              and this one once the player is free in the
//                              room (no event, no sub screen for 300 frames;
//                              state_trace.cpp), its frames counted from then:
//                              the opening event and the radio call end on
//                              their own, then Leon walks
//
// The game calls PADRead once per frame (pad.cpp PadRead in the main loop).
//
// Embedded in the app (runtime/core_api.cpp), re4_pad_live sets channel 0
// from the device's controller; a running test script still wins.
#include "re4_core.h"
#include "re4_runtime.h"

#include <atomic>
#include <mutex>
#include <stdlib.h>
#include <string.h>

namespace {

typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef unsigned char u8;

struct PADStatus {
    u16 button;
    s8 stickX;
    s8 stickY;
    s8 substickX;
    s8 substickY;
    u8 triggerLeft;
    u8 triggerRight;
    u8 analogA;
    u8 analogB;
    s8 err;
};
static_assert(sizeof(PADStatus) == 12, "PADStatus layout");

const s8 kNoController = -1;  // PAD_ERR_NO_CONTROLLER
const u32 kHold = 3;

struct Press {
    u16 buttons;
    s8 stickX, stickY;
    u32 from;
    u32 to;
    u32 every;
};

Press gScript[64];
int gScriptLen;
u32 gFrame;

u16 buttonBit(const char* name, size_t len)
{
    static const struct {
        const char* name;
        u16 bit;
    } kNames[] = {
        { "LEFT", 0x0001 }, { "RIGHT", 0x0002 }, { "DOWN", 0x0004 }, { "UP", 0x0008 },
        { "Z", 0x0010 },    { "R", 0x0020 },     { "L", 0x0040 },    { "A", 0x0100 },
        { "B", 0x0200 },    { "X", 0x0400 },     { "Y", 0x0800 },    { "START", 0x1000 },
    };
    for (const auto& n : kNames) {
        if (strlen(n.name) == len && strncmp(n.name, name, len) == 0) {
            return n.bit;
        }
    }
    re4_log("pad: unknown button \"%.*s\" in RE4_PAD_SCRIPT", (int) len, name);
    return 0;
}

// The main stick from a script name (STICKUP ...): true when it is one.
bool stickName(const char* name, size_t len, Press& p)
{
    static const struct {
        const char* name;
        s8 x, y;
    } kSticks[] = {
        { "STICKUP", 0, 100 }, { "STICKDOWN", 0, -100 }, { "STICKLEFT", -100, 0 }, { "STICKRIGHT", 100, 0 },
    };
    for (const auto& n : kSticks) {
        if (strlen(n.name) == len && strncmp(n.name, name, len) == 0) {
            p.stickX = n.x ? n.x : p.stickX;
            p.stickY = n.y ? n.y : p.stickY;
            return true;
        }
    }
    return false;
}

// Appends the entries of `s`, their frames moved by `offset`.
void parseScript(const char* s, u32 offset = 0)
{
    while (*s && gScriptLen < 64) {
        while (*s == ' ' || *s == ',') {
            s++;
        }
        if (!*s) {
            break;
        }
        Press p = { 0, 0, 0, 0, 0, 0 };
        while (*s && *s != '@') {
            const char* e = s;
            while (*e && *e != '+' && *e != '@') {
                e++;
            }
            if (!stickName(s, (size_t) (e - s), p)) {
                p.buttons |= buttonBit(s, (size_t) (e - s));
            }
            s = *e == '+' ? e + 1 : e;
        }
        if (*s != '@') {
            re4_log("pad: RE4_PAD_SCRIPT entry without @FRAME ignored");
            break;
        }
        char* e;
        p.from = (u32) strtoul(s + 1, &e, 10);
        p.to = p.from;
        if (*e == '-') {
            p.to = (u32) strtoul(e + 1, &e, 10);
            if (*e == '/') {
                p.every = (u32) strtoul(e + 1, &e, 10);
            }
        }
        s = e;
        p.from += offset;
        p.to += offset;
        gScript[gScriptLen++] = p;
    }
    re4_log("pad: test script with %d entries%s", gScriptLen, offset ? " (from the room on)" : "");
}

// The scripted pad of `frame` (buttons and main stick).
Press scriptAt(u32 frame)
{
    Press b = { 0, 0, 0, 0, 0, 0 };
    for (int i = 0; i < gScriptLen; i++) {
        const Press& p = gScript[i];
        if (frame < p.from || frame >= p.to + kHold) {
            continue;
        }
        u32 phase = frame - p.from;
        if (p.every ? phase % p.every < kHold : phase < kHold) {
            b.buttons |= p.buttons;
            b.stickX = p.stickX ? p.stickX : b.stickX;
            b.stickY = p.stickY ? p.stickY : b.stickY;
        }
    }
    return b;
}

}  // namespace

extern "C" {

int PADInit(void)
{
    if (const char* s = getenv("RE4_PAD_SCRIPT")) {
        if (gScriptLen == 0) {
            parseScript(s);
        }
    }
    return 1;
}

int PADReset(u32) { return 1; }
int PADRecalibrate(u32) { return 1; }
void PADSetAnalogMode(u32) {}

// Rumble of channel 0. The game's VibControl (game/pad.cpp) switches the motor
// on and off from frame to frame, weaker levels as pulses, and never calls it
// with the game's vibration option off. PADRead notes each frame's state; the
// app reads the last four frames (re4_core_rumble) and drives the device's
// vibrator from them.
static std::atomic<u32> g_motor;   // PAD_MOTOR_STOP 0, RUMBLE 1, STOP_HARD 2
static std::atomic<u32> g_rumble;  // bits 0-3: on in the last 4 frames, bits 8-31: frames

void PADControlMotor(long chan, u32 cmd)
{
    if (chan == 0) {
        g_motor = cmd;
    }
}

u32 re4_pad_rumble(void)
{
    return g_rumble;
}

// Whether the game shows a menu: runtime/state_trace.cpp (libre4_main) hands its test over when
// the library loads (absent in tools without the game). A weak reference to it from here stayed
// empty on Android: its linker looks only in the libraries this one needs, and the platform layer
// does not need libre4_main (the app loads it on its own; device test 27.10.2026: the own
// controller bindings applied in the options screen too).
static int (*g_menuProbe)(void);

extern "C" void re4_pad_menu_probe(int (*probe)(void))
{
    g_menuProbe = probe;
}

// the frame that just ended into the rumble record (PADRead), bit 4 the menu (re4_core.h)
static void noteRumble()
{
    static u32 offFrames = 30, starts;
    const u32 before = g_rumble;
    const u32 on = g_motor == 1;
    // bit 4 a menu (the app's button mapping gives way to the device's default there), bit 5 a button
    // prompt (only noted: switching the mapping at prompts made it unusable, device test 27.10.2026)
    const int probe = g_menuProbe ? g_menuProbe() : 0;  // 1 a menu, 2 a button prompt
    const u32 menu = probe == 1 ? 0x10u : probe == 2 ? 0x20u : 0u;
    static u32 menuLines;
    if (menu != (before & 0x30u) && menuLines < 60) {
        menuLines++;
        re4_log("pad: game %s at frame %u", menu == 0x10u ? "menu shown (the app's button mapping: the default)"
                                            : menu ? "button prompt shown" : "menu or prompt closed",
                gFrame);
    }
    g_rumble = ((gFrame + 1) << 8) | menu | (((before << 1) | on) & 0xF);
    if (on && offFrames >= 30 && starts < 16) {
        starts++;
        re4_log("pad: rumble from frame %u", gFrame);
    }
    offFrames = on ? 0 : offFrames + 1;
}

// Per-frame observers, called once per game frame with the frame number: the
// state trace (runtime/state_trace.cpp, re4_set_frame_hook replaces it) and
// the core bridge (runtime/core_api.cpp, re4_add_frame_hook).
static void (*g_frameHook)(unsigned);
static void (*g_moreHooks[16])(unsigned);  // (8 until 29.10.2026: with the automatic QTEs nearly full)

void re4_set_frame_hook(void (*hook)(unsigned))
{
    g_frameHook = hook;
}

void re4_add_frame_hook(void (*hook)(unsigned))
{
    for (auto& h : g_moreHooks) {
        if (h == hook) {
            return;
        }
    }
    for (auto& h : g_moreHooks) {
        if (!h) {
            h = hook;
            return;
        }
    }
    re4_log("pad: no room for another frame hook");
}

// Buttons a frame hook adds to channel 0 for the frame it runs in (re4_pad_add:
// the knife swipe, runtime/knife_swipe.cpp), on top of the controller or script.
static u16 g_add;

void re4_pad_add(u16 buttons)
{
    g_add |= buttons;
}

// Live controller (the app): written by its input thread, read by PADRead.
static std::mutex g_liveLock;
static Re4CorePad g_live;
static bool g_hasLive;
// Presses of each button the game has not seen as a press yet (re4_pad_live counts every down). The
// game counts a press where a button is on in a frame and was off in the one before (Key.trg, game/
// pad.cpp). A tap shorter than a game frame (33 ms, longer while the game stutters) still reaches the
// game once; and a new tap while the game still saw the last one held (a fast mash: release and press
// again between two reads) first gets a frame with the button off, so it counts too (r226, the run
// from the statue, report 25.10.2026 by video: "A rapidly" with fast taps, the game saw few presses).
static u8 g_presses[16];
static u16 g_reported;  // buttons the game got at the last read

void re4_pad_live(const Re4CorePad* pad)
{
    std::lock_guard<std::mutex> lock(g_liveLock);
    if (pad) {
        const u16 down = pad->buttons & ~g_live.buttons;
        for (int i = 0; i < 16; i++) {
            if ((down >> i & 1) && g_presses[i] < 4) {
                g_presses[i]++;
            }
        }
        g_live = *pad;
        g_hasLive = true;
    } else {
        g_hasLive = false;
    }
}

// Ends the test script (state_trace.cpp: RE4_PAD_UNTIL_ROOM).
void re4_pad_script_stop(void)
{
    if (gScriptLen) {
        re4_log("pad: test script ends at frame %u", gFrame);
        gScriptLen = 0;
        if (const char* room = getenv("RE4_PAD_ROOM_SCRIPT")) {
            parseScript(room, gFrame);
        }
    }
}

// The player is free in the room (state_trace.cpp): RE4_PAD_FREE_SCRIPT starts,
// replacing what is left of the room script.
void re4_pad_script_free(void)
{
    const char* free = getenv("RE4_PAD_FREE_SCRIPT");
    if (free && *free) {
        gScriptLen = 0;
        parseScript(free, gFrame);
    }
}

u32 PADRead(PADStatus* status)
{
    noteRumble();
    if (g_frameHook) {
        g_frameHook(gFrame);
    }
    for (auto h : g_moreHooks) {
        if (h) {
            h(gFrame);
        }
    }
    memset(status, 0, 4 * sizeof(PADStatus));
    const Press scripted = scriptAt(gFrame);
    status[0].button = scripted.buttons;
    status[0].stickX = scripted.stickX;
    status[0].stickY = scripted.stickY;
    // like a real pad: a trigger that clicks is pressed all the way, A and B
    // are full when pressed (game/pad.cpp takes the click or any trigger value)
    status[0].triggerRight = (scripted.buttons & 0x0020) ? 0xFF : 0;
    status[0].triggerLeft = (scripted.buttons & 0x0040) ? 0xFF : 0;
    status[0].analogA = (scripted.buttons & 0x0100) ? 0xFF : 0;
    status[0].analogB = (scripted.buttons & 0x0200) ? 0xFF : 0;
    if (!gScriptLen) {
        std::lock_guard<std::mutex> lock(g_liveLock);
        if (g_hasLive) {
            u16 buttons = g_live.buttons, released = 0;
            for (int i = 0; i < 16; i++) {
                const u16 bit = (u16) (1u << i);
                if (!g_presses[i]) {
                    continue;
                }
                if (g_reported & bit) {
                    buttons &= ~bit;  // held at the last read: off for one frame, the press next frame
                    released |= bit;
                } else {
                    buttons |= bit;
                    g_presses[i]--;
                }
            }
            status[0].button = buttons;
            status[0].stickX = g_live.stickX;
            status[0].stickY = g_live.stickY;
            status[0].substickX = g_live.substickX;
            status[0].substickY = g_live.substickY;
            // a trigger counts as pressed by its value too (game/pad.cpp): off with its button
            status[0].triggerLeft = (released & 0x0040) ? 0 : g_live.triggerL;
            status[0].triggerRight = (released & 0x0020) ? 0 : g_live.triggerR;
            // analog A/B like a real pad: full when pressed
            status[0].analogA = (status[0].button & 0x0100) ? 0xFF : 0;
            status[0].analogB = (status[0].button & 0x0200) ? 0xFF : 0;
        }
    }
    if (g_add) {
        status[0].button |= g_add;
        if (g_add & 0x0040) {
            status[0].triggerLeft = 0xFF;
        }
        if (g_add & 0x0020) {
            status[0].triggerRight = 0xFF;
        }
        if (g_add & 0x0100) {
            status[0].analogA = 0xFF;
        }
        g_add = 0;
    }
    g_reported = status[0].button;
    // every change of the buttons (scripted or from the device)
    static u16 lastButtons;
    if (status[0].button != lastButtons && status[0].button) {
        re4_log("pad: frame %u buttons 0x%04x", gFrame, status[0].button);
    }
    lastButtons = status[0].button;
    for (int i = 1; i < 4; i++) {
        status[i].err = kNoController;
    }
    gFrame++;
    return 0;
}

}  // extern "C"
