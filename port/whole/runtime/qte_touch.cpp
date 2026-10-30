// Quick-time events by a tap (wish of the project owner, 24.10.2026: without a controller the
// button pairs that flash up are hard to hit on the touch controls), compiled with the game's flags
// into libre4_main.
//
// The game shows a quick-time prompt through the action button (game/act_btn.cpp ActBtn: the
// cockpit draws the button icon its DISP_FLAG names, Cckpt.m_ActBttn) and checks the keys in
// cActionButton::checkButton. A tap on the picture (re4_core_touch, which also hands it here;
// CoreActivity.watchTap only passes taps beside the touch controls, so a player with a controller
// does not tap) while such a prompt is shown presses its buttons for the next two game frames, on
// top of the controller (a frame hook in PADRead, pad.cpp re4_pad_add), as a player would: L and R,
// A and B (the dodges, the knife fight), A or B alone (the mashes: each tap is one press of one
// frame with a frame off after it, taps in quick succession are kept and pressed one after the other,
// so a fast mash is not merged into one long press; the shake-off counts A too), X, Y, Z, L or R. The plain A prompt (doors, ladders, kicks: the touch
// controls have A) and the stick prompts (the jet ski, the levers) are left to the controls. A tap
// older than 400 ms, in a sub screen or without a prompt does nothing.
//
// Automatic QTEs (the cheat menu, runtime/cheats.cpp re4_qte_auto; wish of the project owner,
// 29.10.2026): while on, every such prompt is answered without a tap, as a tap would: the button pair
// or the single button pressed for two frames, released for two, again while the prompt stays; a
// mash pressed every second frame. X (Ashley: hide, sce_at.cpp) is a choice of the player, not a
// quick-time event, and stays his, as do the plain A and the stick prompts.
#include "types.h"
#include "global.h"
#include "main.h"
#include "act_btn.h"
#include "cockpit.h"

#include <time.h>

extern GLOBAL_WK* pG;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));
extern "C" void re4_pad_add(unsigned short buttons);
extern "C" unsigned short re4_qte_buttons(int disp);

namespace {

const u16 kA = 0x0100, kB = 0x0200, kX = 0x0400, kY = 0x0800, kZ = 0x0010, kL = 0x0040, kR = 0x0020;
const u32 kTapAgeMs = 400;
const unsigned kPressFrames = 2;

static_assert(sizeof(ActionButton) == 4, "ActionButton: the prompt shown is its first byte");

// the app's thread writes, the game's reads
volatile u32 gWhen, gSeq;
u32 gSeen;
bool gHooked;
u16 gButtons;       // being pressed
unsigned gLeft;     // frames
unsigned gQueued;   // mash taps not pressed yet (a press each second frame)
bool gPressedLast;  // the mash pressed in the frame before (this one is its release)
unsigned gTaps;
u8 gLogged[17];     // one line per prompt kind
volatile int gAuto;     // automatic QTEs (re4_qte_auto, any thread)
unsigned gAutoGap;      // frames released after an automatic press
unsigned gAutoPresses;  // automatic answers so far
u8 gAutoLogged[17];

u32 nowMs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u32) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

const char* names(u16 b)
{
    switch (b) {
    case kL | kR:
        return "L+R";
    case kA | kB:
        return "A+B";
    case kA:
        return "A";
    case kB:
        return "B";
    case kX:
        return "X";
    case kY:
        return "Y";
    case kZ:
        return "Z";
    case kL:
        return "L";
    case kR:
        return "R";
    }
    return "?";
}

bool mash(u8 shown)
{
    return shown == DISP_A_RAPID || shown == DISP_B_RAPID || shown == DISP_GACHA;
}

void qteHook(unsigned)
{
    const u32 seq = __atomic_load_n(&gSeq, __ATOMIC_ACQUIRE);
    const u8 shown = *reinterpret_cast<const u8*>(&Cckpt.m_ActBttn);
    if (seq != gSeen) {
        const u32 taps = seq - gSeen;
        gSeen = seq;
        const bool fresh = nowMs() - __atomic_load_n(&gWhen, __ATOMIC_RELAXED) <= kTapAgeMs;
        if (fresh && !StaFlagChk(pG, STA_SUB_SCRN) && ActBtn.isActive()) {
            const u16 b = re4_qte_buttons(shown);
            if (b && mash(shown)) {
                gButtons = b;
                gQueued = gQueued + taps > 4 ? 4 : gQueued + taps;
                gTaps += taps;
            } else if (b) {
                gButtons = b;
                gLeft = kPressFrames;
                gTaps++;
            }
            if (b && shown < sizeof gLogged && !gLogged[shown]) {
                gLogged[shown] = 1;
                re4_log("qte: a tap pressed %s for the prompt (kind %d), %u taps so far", names(b), shown, gTaps);
            }
        }
    }
    if (__atomic_load_n(&gAuto, __ATOMIC_RELAXED) && shown != DISP_X && !StaFlagChk(pG, STA_SUB_SCRN) &&
        ActBtn.isActive()) {
        const u16 b = re4_qte_buttons(shown);
        bool pressed = false;
        if (b && mash(shown)) {
            gButtons = b;
            pressed = gQueued == 0;  // (counted once per mash)
            if (gQueued < 2) {
                gQueued = 2;  // kept topped up: pressed every second frame while the mash lasts
            }
        } else if (b && !gLeft) {
            if (gAutoGap) {
                gAutoGap--;
            } else {
                gButtons = b;
                gLeft = kPressFrames;
                gAutoGap = 2;
                pressed = true;
            }
        }
        if (pressed) {
            gAutoPresses++;
            if (shown < sizeof gAutoLogged && !gAutoLogged[shown]) {
                gAutoLogged[shown] = 1;
                re4_log("qte: automatic, %s pressed for the prompt (kind %d), %u answers so far", names(b), shown,
                        gAutoPresses);
            }
        }
    } else {
        gAutoGap = 0;
    }
    if (gQueued && !mash(shown)) {
        gQueued = 0;  // the mash is over
    }
    if (gQueued) {
        // one frame pressed, one released: every tap is a press of its own
        if (gPressedLast) {
            gPressedLast = false;
        } else {
            re4_pad_add(gButtons);
            gPressedLast = true;
            gQueued--;
        }
    } else if (gLeft) {
        gLeft--;
        re4_pad_add(gButtons);
        gPressedLast = false;
    } else {
        gPressedLast = false;
    }
}

} // namespace

// The buttons a tap presses for the prompt the cockpit shows (DISP_FLAG, act_btn.h); 0: none.
extern "C" unsigned short re4_qte_buttons(int disp)
{
    switch (disp) {
    case DISP_L_R:
        return kL | kR;
    case DISP_A_B:
        return kA | kB;
    case DISP_A_RAPID:
    case DISP_A_ACCENT:
    case DISP_GACHA:
        return kA;
    case DISP_B:
    case DISP_B_RAPID:
        return kB;
    case DISP_X:
        return kX;
    case DISP_Y:
        return kY;
    case DISP_Z:
        return kZ;
    case DISP_L:
        return kL;
    case DISP_R:
        return kR;
    }
    return 0;
}

// Automatic QTEs on or off (the cheat switch, runtime/cheats.cpp). Any thread.
extern "C" void re4_qte_auto(int on)
{
    if (on && !__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
        re4_add_frame_hook(qteHook);
    }
    if (__atomic_exchange_n(&gAuto, on ? 1 : 0, __ATOMIC_ACQ_REL) != (on ? 1 : 0)) {
        re4_log("qte: automatic %s", on ? "on (every quick-time prompt answered)" : "off");
    }
}

// re4_core_touch (menu_touch.cpp) hands every tap on the picture over. Any thread.
extern "C" void re4_qte_tap(void)
{
    if (!__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
        re4_add_frame_hook(qteHook);
    }
    __atomic_store_n(&gWhen, nowMs(), __ATOMIC_RELAXED);
    __atomic_fetch_add(&gSeq, 1, __ATOMIC_RELEASE);
}
