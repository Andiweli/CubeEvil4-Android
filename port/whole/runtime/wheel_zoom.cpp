// The mouse wheel zooms the scope and the binoculars (0.99.12, wish of the project owner, 12.11.2026: "Wenn jemand
// mit Tastatur und Maus spielt, soll mit dem Mausrad gezoomt werden können, wenn man ein Fernglas oder Zielfernrohr
// nutzt."), compiled with the game's flags into libre4_main.
//
// The game zooms both with the C stick up and down (src/game/cam_extra.cpp): CameraScope::move (the rifles, the
// rocket and mine launchers, every character; while the aim is held, Key.on 0x10) and CameraBinocular::move (r101's
// binoculars, again after the attache case) add substickY * 0.001 to their zoom (0..1) each game frame, up zooms in;
// the event cuts through the binoculars in r10b and r214 show the binoculars' frame only and do not zoom.
//
// The app hands each notch of the wheel over (re4_core_zoom, CoreActivity: up 1, down -1). A notch is an eighth of
// the zoom: 125 of substickY a frame summed up, handed to the game in PADRead as a short push of the C stick
// straight up or down (re4_pad_substick: 50, 50, 25 after the SDK's PADClamp, three game frames; game/pad.cpp
// zeroes 10 and less), so the game zooms by its own code (the field of view, the zoom bar, the focus blur when the
// zoom changes) and every notch the same. Notches queue up to the whole zoom (8); one the other way drops those
// still queued. Only while the scope (STA_SCOPE_CAMERA, CamCtrl.r0 0x10) or the binoculars (STA_BINOCULAR, r0 0xC)
// are up in play; anywhere else a notch is dropped and changes nothing, and so are those that would zoom past the
// end.
//
// The app also learns when they are up (re4_wheel_zoom_view, the rumble word's bit 6, runtime/pad.cpp): there the
// mouse's movement no longer drives the C stick (it zoomed the scope and the binoculars by moving up and down).
//
// The mouse looks around in the binoculars (0.99.12, the project owner: "Ja, Maus bewegt Blick"), as it moves the
// sight in the scope: its movement comes as the swipe's angles (CoreBridge.aimTurn, mouse sensitivity included;
// runtime/gyro.cpp hands each game frame's sum to re4_binocular_look). CameraBinocular::move turns by the main
// stick, gain * stickX * -0.05 degrees about the up axis and gain * stickY * 0.05 degrees up and down a frame (gain
// 1 - 0.9 * zoom), within the room's limits (SetBinocularRange); so the angles become main stick units for that frame
// (re4_pad_stick_add, on top of the move keys), and the game turns by its own code: slower at a higher zoom like the
// mouse in the scope, stopped at the limits. One frame turns at most as far as the stick at its end (3.6 degrees);
// more is dropped, the parts of a unit are kept. Up and down as the mouse aims: the game's "reverse" is undone here,
// because the app turns the mouse round itself with "Invert vertical" (CoreInput.mouseAim, decision of the project
// owner 12.11.2026), for aiming, the scope and the binoculars alike. Only the binoculars the player
// holds (r101, again after the attache case); the event cuts through the binoculars (r10b, r214) have the event's
// camera, which nothing turns.
#include "types.h"
#include "global.h"
#include "main.h"
#include "cam_ctrl.h"
#include "cam_extra.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern GLOBAL_WK* pG;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" void re4_pad_substick(int y);                    // runtime/pad.cpp
extern "C" void re4_pad_stick_add(int x, int y);            // runtime/pad.cpp
extern "C" void re4_pad_zoom_probe(int (*probe)(void));     // runtime/pad.cpp
extern "C" void re4_core_zoom(int notches);                 // below (runtime/re4_core.h)
extern "C" int re4_wheel_zoom_view(void);                   // below
extern "C" int re4_binocular_look(float yaw, float pitch);  // below (runtime/gyro.cpp)

namespace {

const int kNotch = 125;      // substickY summed up a notch: an eighth of the zoom (0.001 a unit)
const int kPush = 50;        // a frame's push at most (after PADClamp; the game zeroes 10 and less)
const int kQueue = 1000;     // the whole zoom
const float kFovy = 45.0f;   // the field of view without zoom (both cameras)
const float kBinoLimit = 3.0f;  // the binoculars' at the full zoom (CameraBinocular::move zoom_limit)

// the binoculars' look (re4_binocular_look, the game thread)
const float kStickUnit = 0.05f * 3.14159265f / 180.0f;  // radians a unit of the main stick turns them a frame
const float kPitchUp = 1.0f;     // stickY positive looks up (CameraBinocular::move, the game's "reverse" off)
const int kStickMax = 72;        // the main stick's end after PADClamp (src/lib/Padclamp.c: 72, octagon 40)
const int kStickXY = 40;
float gLookRestX, gLookRestY;    // the parts of a unit left
unsigned gLooked;                // frames the mouse turned them (the log)
bool gTraceLook;                 // RE4_TRACE_LOOK=1: the scope's or the binoculars' direction every 10 frames

// the app's thread adds, the game's takes: substickY units still to push (positive in)
volatile int gQueue;
volatile int gLock;
bool gHooked;
unsigned gLines;      // the log's lines
int gPushed;          // pushed since the last line (the log: "zoom 0.25 after ...")
unsigned gNotches;    // notches taken (the log)

void lock()
{
    while (__atomic_exchange_n(&gLock, 1, __ATOMIC_ACQUIRE)) {
    }
}

void unlock()
{
    __atomic_store_n(&gLock, 0, __ATOMIC_RELEASE);
}

// 1 the scope, 2 the binoculars, up in play (no sub screen, the camera of either); 0 not
int view()
{
    GLOBAL_WK* g = pG;
    if (!g || g->Rno0 != 3 || StaFlagChk(g, STA_SUB_SCRN) || !CamCtrl.m_pProc) {
        return 0;
    }
    if (StaFlagChk(g, STA_SCOPE_CAMERA) && CamCtrl.r0 == 0x10) {
        return 1;
    }
    if (StaFlagChk(g, STA_BINOCULAR) && CamCtrl.r0 == 0xC) {
        return 2;
    }
    return 0;
}

// the zoom 0..1 the camera had in the last frame
float zoomNow(int v)
{
    if (v == 1) {
        f32 zoom = 0.0f, angle = 0.0f;
        ((CameraScope*) CamCtrl.m_pProc)->getParam(&zoom, &angle);
        return zoom;
    }
    // CameraBinocular keeps its zoom private: from its field of view (45 .. 3, linear in the zoom)
    const float z = (kFovy - CamCtrl.m_pProc->param.Fovy) / (kFovy - kBinoLimit);
    return z < 0.0f ? 0.0f : z > 1.0f ? 1.0f : z;
}

const char* name(int v)
{
    return v == 1 ? "the scope" : "the binoculars";
}

// For tests on the PC: RE4_TEST_WHEEL="N@F ..." (up to 16), N notches (negative: down) at the PADRead count F, or
// with "N@vF" F frames after the scope or the binoculars first came up in the run, as the app's re4_core_zoom
struct TestNotch {
    int notches;
    unsigned frame;
    bool fromView;
};
TestNotch gTest[16];
int gTests;
long gViewAt = -1;  // the frame the scope or the binoculars first came up (the test)

void zoomHook(unsigned frame)
{
    const int v = view();
    if (gTraceLook && v != 0 && frame % 10 == 0) {
        // the direction the scope or the binoculars look in (yaw about the up axis, positive left of +z; pitch up)
        const CAMERA_POINT& p = CamCtrl.m_pProc->param;
        const float dx = p.Target.x - p.Campos.x, dy = p.Target.y - p.Campos.y, dz = p.Target.z - p.Campos.z;
        re4_log("look: frame %u %s yaw %.4f pitch %.4f fovy %.1f", frame, v == 1 ? "scope" : "binoculars",
                (double) atan2f(dx, dz),
                (double) atan2f(dy, sqrtf(dx * dx + dz * dz)), (double) p.Fovy);
    }
    if (v != 0 && gViewAt < 0) {
        gViewAt = frame;
        if (gTests) {
            re4_log("wheel: %s up at frame %u (RE4_TEST_WHEEL)", name(v), frame);
        }
    }
    for (int i = 0; i < gTests; i++) {
        const long at = gTest[i].fromView ? (gViewAt < 0 ? -1 : gViewAt + (long) gTest[i].frame) : (long) gTest[i].frame;
        if (at == (long) frame) {
            re4_log("wheel: test %d notch(es) at frame %u (RE4_TEST_WHEEL)", gTest[i].notches, frame);
            re4_core_zoom(gTest[i].notches);
        }
    }
    lock();
    int queue = gQueue;
    int push = 0;
    if (v == 0) {
        gQueue = 0;
        unlock();
        gPushed = 0;
        if (queue != 0 && gLines < 200) {
            gLines++;
            re4_log("wheel: %d notch(es) dropped: neither the scope nor the binoculars up", queue / kNotch);
        }
        return;
    }
    if (queue != 0) {
        const float zoom = zoomNow(v);
        if ((queue > 0 && zoom >= 0.999f) || (queue < 0 && zoom <= 0.001f)) {
            gQueue = 0;  // at the end: what is left would zoom nothing
            queue = 0;
        } else {
            push = queue > kPush ? kPush : queue < -kPush ? -kPush : queue;
            gQueue = queue - push;
        }
    }
    unlock();
    if (push != 0) {
        re4_pad_substick(push);
        gPushed += push;
    } else if (gPushed != 0 && v != 0) {
        // the pushes are done (the game took the last one in the frame before)
        if (gLines < 200) {
            gLines++;
            re4_log("wheel: %s zoomed to %.3f (field of view %.1f; %+d of the C stick in all, %u notch(es) so far)",
                    name(v), (double) zoomNow(v), (double) CamCtrl.m_pProc->param.Fovy, gPushed, gNotches);
        }
        gPushed = 0;
    }
}

struct Install {
    Install()
    {
        re4_pad_zoom_probe(re4_wheel_zoom_view);
        const char* tr = getenv("RE4_TRACE_LOOK");
        if (tr && *tr && *tr != '0') {
            gTraceLook = true;
            if (!__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
                re4_add_frame_hook(zoomHook);
            }
        }
        const char* t = getenv("RE4_TEST_WHEEL");
        if (!t || !*t) {
            return;
        }
        int n, used, at;
        unsigned frame;
        while (gTests < 16 && sscanf(t, " %d@%n", &n, &at) == 1) {
            const bool fromView = t[at] == 'v';
            if (sscanf(t + at + (fromView ? 1 : 0), "%u%n", &frame, &used) != 1) {
                break;
            }
            gTest[gTests].notches = n;
            gTest[gTests].frame = frame;
            gTest[gTests].fromView = fromView;
            gTests++;
            t += at + (fromView ? 1 : 0) + used;
        }
        if (gTests && !__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
            re4_add_frame_hook(zoomHook);
        }
        re4_log("wheel: %d test notch time(s) (RE4_TEST_WHEEL)", gTests);
    }
} gInstall;

}  // namespace

// The scope or the binoculars are up in play (the C stick zooms): 1, else 0. The platform layer asks once a game
// frame for the rumble word's bit 6 (runtime/pad.cpp), the app then keeps the mouse off the C stick.
extern "C" int re4_wheel_zoom_view(void)
{
    return view() != 0;
}

// The mouse's turn of a game frame (runtime/gyro.cpp, the swipe's sum: yaw positive left, pitch positive up, in
// radians) while the player holds the binoculars: the main stick of this frame, 1; not in the binoculars: 0 (the
// aim takes it). The game thread, in PADRead's frame hooks (before the pad is read).
extern "C" int re4_binocular_look(float yaw, float pitch)
{
    if (view() != 2 || !(yaw == yaw) || !(pitch == pitch)) {
        gLookRestX = gLookRestY = 0.0f;
        return 0;
    }
    // stickX positive turns right (yaw negative); stickY as the mouse aims, the game's "reverse" undone (the app's
    // "Invert vertical" is in the pitch already, CoreInput.mouseAim)
    const float up = pSys && CfgFlagChk(pSys, CFG_AIM_REVERSE) ? -kPitchUp : kPitchUp;
    float fx = gLookRestX - yaw / kStickUnit;
    float fy = gLookRestY + up * pitch / kStickUnit;
    int x = (int) fx, y = (int) fy;  // towards 0
    gLookRestX = fx - (float) x;
    gLookRestY = fy - (float) y;
    // one frame at most as far as the stick at its end (inside PADClamp's octagon: the game gets them exactly)
    if (x > kStickMax) x = kStickMax;
    if (x < -kStickMax) x = -kStickMax;
    if (y > kStickMax) y = kStickMax;
    if (y < -kStickMax) y = -kStickMax;
    const int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    const int d = ay <= ax ? kStickXY * ax + (kStickMax - kStickXY) * ay : kStickXY * ay + (kStickMax - kStickXY) * ax;
    if (d > kStickXY * kStickMax) {
        x = x * kStickXY * kStickMax / d;
        y = y * kStickXY * kStickMax / d;
    }
    if (x != 0 || y != 0) {
        re4_pad_stick_add(x, y);
        if (gLooked++ % 900 == 0) {
            re4_log("wheel: the mouse turned the binoculars (%u frames so far; stick %+d %+d)", gLooked, x, y);
        }
    }
    return 1;
}

// The app (CoreBridge.zoom): notches of the mouse wheel, up (positive) zooms in; any thread.
extern "C" void re4_core_zoom(int notches)
{
    if (notches == 0) {
        return;
    }
    if (!__atomic_exchange_n(&gHooked, true, __ATOMIC_ACQ_REL)) {
        re4_add_frame_hook(zoomHook);
        re4_log("wheel: the mouse wheel zooms the scope and the binoculars");
    }
    if (notches > 8) notches = 8;
    if (notches < -8) notches = -8;
    lock();
    int queue = gQueue;
    if ((queue > 0 && notches < 0) || (queue < 0 && notches > 0)) {
        queue = 0;  // the other way: the notches still queued go
    }
    queue += notches * kNotch;
    if (queue > kQueue) queue = kQueue;
    if (queue < -kQueue) queue = -kQueue;
    gQueue = queue;
    gNotches += (unsigned) (notches < 0 ? -notches : notches);
    unlock();
}
