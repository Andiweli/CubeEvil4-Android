// The app's picture format "4:3 without bars" (Anbernic RG 477V report, 22.10.2026), compiled with
// the game's flags into libre4_main: patch 0105 asks re4_letterbox_open every frame (Render: the
// scissor to the letterbox rows, main loop: Draw_cinesco). Open while the app's switch is on
// (re4_core_set_letterbox, runtime/core_api.cpp) and the player plays: the game's main loop, no
// sub screen, no event (events keep their letterbox, SceEventStart sets the scissor for them).
// Also the picture format "Native" (patch 0214, re4_view_aspect below) and the menu screens'
// RE4_GX_CHUNK_STILL.
#include "types.h"
#include "global.h"
#include "id_sys.h"

#include <stdlib.h>

extern GLOBAL_WK* pG;

extern "C" int re4_letterbox_off(void);
extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_gx_still(int on);                      // runtime/gx_decode.cpp
extern "C" void re4_gx_wide(int on);                       // runtime/gx_decode.cpp
extern "C" int re4_gx_wide_now(void);                      // runtime/gx_decode.cpp
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" int re4_movie_playing(void);                    // runtime/movie_status.cpp

extern "C" int re4_letterbox_open(void)
{
    const int open = re4_letterbox_off() && pG && pG->Rno0 == 3 && !StaFlagChk(pG, STA_SUB_SCRN) &&
                     !StaFlagChk(pG, STA_EVENT);
    // each change into the error log (at most 40): whether the game drew without its bars
    static int last = -1;
    static unsigned changes;
    if (open != last && (open || last == 1) && changes < 40) {
        changes++;
        re4_log("picture: the game draws %s its letterbox (r%03x)", open ? "without" : "with", pG ? pG->room_id : 0);
    }
    last = open;
    return open;
}

// The app's 60 FPS (Retroid report, 25.10.2026: the list of the load screen at the typewriter
// flickered while it scrolled): no picture between two of a menu screen. The game's sub screens
// (inventory, map, merchant, files: STA_SUB_SCRN) and the memory card screens (load, save, the
// boot check: SYS_TYPEWRITER, card.cpp CardMainTask) draw their lists letter by letter with the
// same draws; the renderer cannot tell which letter moved where and mixed them up. The same for
// the tool menu (SELECT held, patch 0116) and its tools (DBG_TEST_MODE): lists of debug text over a
// frozen game. The result screens of the game too (tester's video 27.10.2026, the chapter end with
// 60 FPS: a "%" stood halfway between two rows): the chapter end, the game's end, the Mercenaries and
// the shooting range results set a table of the id class IDC_TITLE over a stopped game (option.cpp
// ChapterEnd/GameResult, mercenaries.cpp, r22c.cpp); their digits and signs are alike draws as well.
// And the option screen START opens over the paused game (game.cpp gameOption, Rno0 6; Retroid
// 28.10.2026 with 60 FPS: the screen flashed white for a moment when it opened).
// Every frame (re4_add_frame_hook); the GX chunks carry it (RE4_GX_CHUNK_STILL, runtime/gx_decode.cpp).
extern "C" int re4_picture_still(void)
{
    if (!pG) {
        return 0;
    }
    if (StaFlagChk(pG, STA_SUB_SCRN) || SysFlagChk(pG, SYS_TYPEWRITER) || DbgFlagChk(pG, DBG_TEST_MODE) ||
        pG->Rno0 == 6) {
        return 1;
    }
    return pG->Rno0 == 3 && IdSys.setCk(IDC_TITLE) ? 1 : 0;
}

// The app's picture format "Native" (wish of the project owner, 07.11.2026, on the widescreen of Noname267's fork;
// patch 0214). The app shows the rows 56..392 of the picture, the 16:9 inside the game's letterbox, over a screen
// wider than 16:9: the game's 3D is drawn for 4/3 x (the screen's aspect) / (16/9) instead of 4/3 then (19.5:9: 1.625),
// in the camera's projection and in everything that projects with it (the culling frustum, the effects that lay a
// copy of the picture over the scene, the mirrors, the camera's near plane against walls and water). The HUD of the
// game's id system is drawn in front of the camera with its projection and keeps its 16:9 shape in the middle; the
// message text (game/mes.cpp messageCamera) is drawn flat and gets the same narrowing (re4_view_flat_edge). Only
// while the player plays (the main loop, Rno0 3, also in events): not in a menu screen of the game
// (re4_picture_still: sub screens, the memory card screens, the option screen, the result tables), not through the
// rifle's scope (its black mask is 4:3 wide), not in a movie. Those keep the game's 4:3 and the app shows them in a
// 16:9 box in the middle, as "16:9". Every picture says which it was (RE4_GX_CHUNK_WIDE, re4_view_load), by the
// projection matrix it loads: the game builds the camera's projection in its logic and draws with it in the next
// frame, and a sub screen may draw without building one.
namespace {

volatile int gViewAspectE4;       // re4_core_set_view_aspect: the app's wish (x 10000), 0 the game's 4:3
float gWideAspect = 1.3333334f;   // that as a float
bool gWideFrame;                  // this game frame builds the wide projection (stillHook, every frame)
float gChunkAspect = 1.3333334f;  // the aspect of the camera's projection the picture being drawn loaded first

}  // namespace

// The aspect of a perspective projection (C_MTXPerspective: m[0][0] = cot / aspect, m[1][1] = cot): the game's
// 4:3 exactly (the original's constant) when it is that or no perspective (m[3][2] -1), else the wider one.
extern "C" float re4_view_mtx_aspect(const float (*m)[4])
{
    if (!m || m[3][2] != -1.0f || !(m[0][0] > 0.0f)) {
        return 1.3333334f;
    }
    const float a = m[1][1] / m[0][0];
    return a > 1.3334f ? a : 1.3333334f;
}

extern "C" void re4_core_set_view_aspect(int aspect_e4)
{
    // a 3D narrower than 4:3 or wider than 4:1 is no picture format of a screen
    const int v = aspect_e4 <= 13334 ? 0 : aspect_e4 > 40000 ? 40000 : aspect_e4;
    if (v != gViewAspectE4) {
        if (v) {
            re4_log("core: picture format Native: the 3D at %d.%04d:1 while playing (4:3 = 1.3333)", v / 10000,
                    v % 10000);
        } else {
            re4_log("core: picture format Native off: the 3D at 4:3");
        }
    }
    gWideAspect = v ? (float) v / 10000.0f : 1.3333334f;
    gViewAspectE4 = v;
}

// The aspect of the camera's projection being built (game/camera.cpp CameraMove and the culling frustum,
// game/view.cpp; patch 0214): the screen's wider one while this frame draws wide, else the game's 4:3, the same
// constant as the original.
extern "C" float re4_view_aspect(void)
{
    return gWideFrame && gViewAspectE4 ? gWideAspect : 1.3333334f;
}

// The aspect pG->Camera's projection has (patch 0214): for the code that projects with that camera's fovy (the
// effects' screen copies, the item camera, the camera's near plane against walls and water, screen to ray).
extern "C" float re4_view_camera_aspect(void)
{
    return pG ? re4_view_mtx_aspect(pG->Camera.ProjMat) : 1.3333334f;
}

// game/camera.cpp CameraSetProjection loads pG->Camera's perspective (patch 0214): returns `type` for
// GXSetProjection; the first load of a picture tells the GX layer which projection the picture's 3D has.
extern "C" int re4_view_load(int type)
{
    if (re4_gx_wide_now() < 0) {
        gChunkAspect = re4_view_camera_aspect();
        re4_gx_wide(gChunkAspect != 1.3333334f);
    }
    return type;
}

// A flat layer of `width` units over the whole picture (game/mes.cpp messageCamera's 512, patch 0214): its left
// (right == 0) or right edge for C_MTXOrtho, so that it keeps its shape in the middle of a wide picture, as the
// id system's HUD does; 0 and `width` in every other picture.
extern "C" float re4_view_flat_edge(float width, int right)
{
    if (re4_gx_wide_now() != 1 || !(gChunkAspect > 1.3333334f)) {
        return right ? width : 0.0f;
    }
    const float half = width * 0.5f, wider = half * gChunkAspect / 1.3333334f;
    return right ? half + wider : half - wider;
}

static bool gCinescoHidden;  // re4_letterbox_frame turned the HUD's letterbox units off

// The game letterboxes play a third time (Retroid report 29.10.2026, the GX capture of r104: the 3D
// filled the height, then the last two draws of the picture laid two black strips over rows 0..56
// and 392..448): the HUD's id class IDC_CINESCO, two units Cockpit::roomInit sets at every room
// start (cockpit.cpp). While "4:3 without bars" is open they are hidden (IDSystem::dispSw), and shown
// again when it closes (events, sub screens, other picture formats); every frame, as the room start
// clears the switch. Returns 1 while they are hidden.
extern "C" int re4_letterbox_frame(void)
{
    const int open = re4_letterbox_open();
    if (open && IdSys.setCk(IDC_CINESCO)) {
        IdSys.dispSw(IDC_CINESCO, 0);
        gCinescoHidden = true;
    } else if (!open && gCinescoHidden) {
        IdSys.dispSw(IDC_CINESCO, 1);
        gCinescoHidden = false;
    }
    return gCinescoHidden ? 1 : 0;
}

namespace {

void stillHook(unsigned)
{
    const int still = re4_picture_still();
    // each change into the error log (at most 40)
    static int last;
    static unsigned changes;
    if (still != last && changes < 40) {
        changes++;
        re4_log("picture: %s (r%03x)", still ? "a menu screen: no pictures between" : "the menu screen closed",
                pG ? pG->room_id : 0);
    }
    last = still;
    re4_gx_still(still);
    re4_letterbox_frame();
    // "Native": the projection this frame builds (CameraMove) is the wide one while the player plays
    const bool wide = gViewAspectE4 && pG && pG->Rno0 == 3 && !still && !StaFlagChk(pG, STA_SCOPE_CAMERA) &&
                      !re4_movie_playing();
    static int wideLast = -1;
    static unsigned wideChanges;
    if (gViewAspectE4 && (int) wide != wideLast && wideChanges < 40) {
        wideChanges++;
        re4_log("picture: Native %s (r%03x%s%s%s)", wide ? "wide" : "in the 16:9 box", pG ? pG->room_id : 0,
                pG && StaFlagChk(pG, STA_SCOPE_CAMERA) ? ", scope" : "", re4_movie_playing() ? ", movie" : "",
                still ? ", menu screen" : "");
    }
    wideLast = wide;
    gWideFrame = wide;
    // out of play the camera's projection is the game's 4:3 from the frame's start: a sub screen draws with
    // the one built before it opened (its logic builds none), the next CameraMove builds it anew
    if (!wide && pG && re4_view_mtx_aspect(pG->Camera.ProjMat) != 1.3333334f) {
        pG->Camera.ProjMat[0][0] = pG->Camera.ProjMat[1][1] / 1.3333334f;
    }
}

struct Install {
    Install()
    {
        re4_add_frame_hook(stillHook);
        // the PC's checks: RE4_VIEW_ASPECT=1.625 (the 3D's aspect, as the app's "Native" sends it)
        const char* v = getenv("RE4_VIEW_ASPECT");
        if (v && *v) {
            re4_core_set_view_aspect((int) (atof(v) * 10000.0 + 0.5));
        }
    }
} g_install;

}  // namespace
