// The mouse in the game's title and options menus (1.1.0; the idea from a user's "ModernTPS" mod, our own code). The
// app captures the mouse for the camera, so there is no pointer on the screen and no place of a click. While the game
// shows a menu the app moves a pointer of its own with the mouse (u across the game's picture, v down it, 0..1) and
// hands it over with each move and each left click (re4_core_mouse_menu). The menus then work as on a PC:
//
//  - the entry under the pointer gets the highlight: patch 0239 asks re4_mouse_menu_keys at the start of the title
//    menu, the difficulty menu (also the European one) and the options' top menu, before they read Key, and gets the
//    cross's up or down for this frame, one press a frame, until the game's own cursor is on that entry; so the
//    game's sound, its wrap-around and the entries it skips (the hidden Professional, the options' first row on the
//    title) stay as they are. Only for a short time after the mouse moved (kHoverFrames), so the cross keeps working;
//  - a left click on the highlighted entry is A; a click on another entry moves the highlight there first and is A
//    as soon as the cursor reached it (at most kClickFrames later); a click beside the entries does nothing.
// Where an entry lies: re4_touch_unit_hit (runtime/menu_touch.cpp), the quad its id unit is drawn as.
#include "types.h"
#include "global.h"
#include "main.h"
#include "id_sys.h"

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_touch_unit_hit(const ID_UNIT* unit, f32 u, f32 v);  // runtime/menu_touch.cpp
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));             // runtime/pad.cpp
extern "C" void re4_mouse_menu_point(float u, float v, int click);

namespace {

constexpr unsigned kHoverFrames = 45;  // the pointer moves the highlight this long after the mouse moved
constexpr unsigned kClickFrames = 20;  // a click on another entry: A once the cursor is there, this long

std::atomic<unsigned> gPoint{0};       // u, v as two 16-bit fractions
std::atomic<unsigned> gMoves{0};       // counts the app's moves
std::atomic<unsigned> gClicks{0};      // counts the app's clicks
unsigned gSeenMoves, gSeenClicks;
unsigned gHover;                       // game frames the pointer still moves the highlight
unsigned gClickWait;                   // a click waiting for the cursor to reach its entry
int gClickEntry = -1;
unsigned gLines;

void pointer(f32* u, f32* v)
{
    const unsigned p = gPoint.load(std::memory_order_relaxed);
    *u = (f32) (p & 0xFFFF) / 65535.0f;
    *v = (f32) (p >> 16) / 65535.0f;
}

// For tests on the PC: RE4_TEST_MOUSE_MENU="F:U:V[:c],..." at PADRead frame F the pointer moves to (U, V), with c
// a left click there too (as the app hands them over)
struct Test {
    unsigned frame;
    float u, v;
    int click;
};
Test gTest[16];
int gTests;

void testHook(unsigned frame)
{
    for (int i = 0; i < gTests; i++) {
        if (gTest[i].frame == frame) {
            re4_mouse_menu_point(gTest[i].u, gTest[i].v, 0);
            if (gTest[i].click) {
                re4_mouse_menu_point(gTest[i].u, gTest[i].v, 1);
            }
            re4_log("mouse: RE4_TEST_MOUSE_MENU at frame %u: (%.2f %.2f)%s", frame, gTest[i].u, gTest[i].v,
                    gTest[i].click ? ", click" : "");
        }
    }
}

struct Install {
    Install()
    {
        const char* v = getenv("RE4_TEST_MOUSE_MENU");
        if (!v) {
            return;
        }
        const char* p = v;
        while (*p && gTests < 16) {
            Test t = {0, 0.0f, 0.0f, 0};
            char c = 0;
            const int n = sscanf(p, "%u:%f:%f:%c", &t.frame, &t.u, &t.v, &c);
            if (n < 3) {
                break;
            }
            t.click = n == 4 && c == 'c';
            gTest[gTests++] = t;
            const char* comma = strchr(p, ',');
            if (!comma) {
                break;
            }
            p = comma + 1;
        }
        re4_add_frame_hook(testHook);
    }
} gInstall;

}  // namespace

extern "C" {

// The app (core_api.cpp re4_core_mouse_menu, any thread): the pointer and whether this event is a left click
void re4_mouse_menu_point(float u, float v, int click)
{
    u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    gPoint.store((unsigned) (u * 65535.0f) | (unsigned) (v * 65535.0f) << 16, std::memory_order_relaxed);
    if (click) {
        gClicks.fetch_add(1);
    } else {
        gMoves.fetch_add(1);
    }
}

// Patch 0239 (game thread, the menu's own frame): the cross's up/down or A for this frame, 0 nothing. `units` the
// entries' id units in the menu's order, `first` the first one the cursor can take, `cursor` where it is now.
unsigned long long re4_mouse_menu_keys(ID_UNIT** units, int n, int first, int cursor)
{
    const unsigned moves = gMoves.load(), clicks = gClicks.load();
    const bool moved = moves != gSeenMoves, clicked = clicks != gSeenClicks;
    gSeenMoves = moves;
    gSeenClicks = clicks;
    if (moved) {
        gHover = kHoverFrames;
    }
    f32 u, v;
    pointer(&u, &v);
    int under = -1;
    if (moved || clicked || gHover || gClickWait) {
        // on the entry, else a little above or below it (the gaps between the rows of words)
        static const f32 kNear[3] = {0.0f, -0.012f, 0.012f};
        for (int k = 0; k < 3 && under < 0; k++) {
            for (int i = first; i < n && under < 0; i++) {
                if (units[i] && re4_touch_unit_hit(units[i], u, v + kNear[k])) {
                    under = i;
                }
            }
        }
    }
    if (moved && gLines < 40) {
        gLines++;
        re4_log("mouse: pointer in a game menu at (%.2f %.2f): entry %d under it, the cursor on %d", u, v, under, cursor);
    }
    if (clicked) {
        gClickEntry = under;
        gClickWait = under >= 0 ? kClickFrames : 0;
        if (gLines < 40) {
            gLines++;
            re4_log("mouse: click in a game menu at (%.2f %.2f): %s", u, v,
                    under < 0 ? "beside the entries" : under == cursor ? "the highlighted entry, A" : "another entry");
        }
    }
    if (gClickWait) {
        gClickWait--;
        if (gClickEntry == cursor) {
            gClickWait = 0;
            gClickEntry = -1;
            gHover = 0;
            return KEY_A;
        }
        return gClickEntry < cursor ? KEY_UP : KEY_DOWN;
    }
    if (gHover) {
        gHover--;
        if (under >= 0 && under != cursor) {
            return under < cursor ? KEY_UP : KEY_DOWN;
        }
    }
    return 0;
}

}  // extern "C"
