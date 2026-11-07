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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern GLOBAL_WK* pG;

extern "C" int re4_letterbox_off(void);
extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_gx_still(int on);                      // runtime/gx_decode.cpp
extern "C" void re4_gx_movie(int on);                      // runtime/gx_decode.cpp
extern "C" void re4_gx_wide(int on);                       // runtime/gx_decode.cpp
extern "C" int re4_gx_wide_now(void);                      // runtime/gx_decode.cpp
extern "C" void re4_add_frame_hook(void (*hook)(unsigned));  // runtime/pad.cpp
extern "C" int re4_movie_playing(void);                    // runtime/movie_status.cpp

extern "C" int re4_letterbox_open(void)
{
    // RE4_TEST_LETTERBOX_MENU=1 (the PC's check, 08.11.2026): open in the sub screens too. Not for the app: the sub
    // screens are made for the band (rows 56..392); above it the attache case's lid shows, below it bits of the
    // screen's parts (GX pictures of r315 with the US discs)
    static const char* menuTest = getenv("RE4_TEST_LETTERBOX_MENU");
    const int open = re4_letterbox_off() && pG && pG->Rno0 == 3 &&
                     (!StaFlagChk(pG, STA_SUB_SCRN) || (menuTest && *menuTest == '1')) && !StaFlagChk(pG, STA_EVENT);
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

// The life gauge in the picture's corner (wish of the project owner, 07.11.2026; patch 0215): the ring of the HUD
// (Leon's and Ashley's life, the ammo, the name; the id system's tree under the life meter's unit 0, class
// IDC_LIFE_METER, with the bullet icon of IDC_BLLT_ICON in it) goes to the bottom right corner of the part of the
// picture the screen shows, as far from the side as from the bottom: 4 % of that part's height, the margin the app
// gives its minimap in the bottom left corner (re4mm::Place). The app tells the core that part and how the screen
// shows it (re4_core_set_hud_layout): the game's rows (56..392 in the 16:9 formats and "4:3 (GC)", whose letterbox
// covers the rest; all 448 with "4:3"; the middle rows of "Native" on a screen narrower than 16:9), how much wider
// one unit shows across than down (15/14 with "4:3 (GC)", the screen's aspect / (16/9) with "Stretched") and whether
// the ring is drawn that much narrower to keep its shape ("Stretched"). The ring is drawn in front of the camera with
// its projection (IDSystem::m_scrn_mat: one unit a pixel of 480 lines, 240 x the projection's aspect to the side):
// game/id_sys.cpp takes the screen matrix from re4_hud_scrn, which adds the move (and the narrowing) for the ring's
// units. Its anchor is the tree's design place (pos0 of unit 0: 217, -130), so its own motions (sliding in and out,
// the needle) stay. Without a layout from the app (the PC, other picture formats before 0.99.0) nothing changes.
namespace {

int gHudTop = -1, gHudBottom = -1;  // re4_core_set_hud_layout: the game's rows the screen shows (-1: no layout)
float gHudStretch = 1.0f;           // one unit across / down on the screen
bool gHudNarrow;                    // draw the ring 1 / gHudStretch as wide (keeps its shape)
// The ring's visible extent from its anchor (the tree's unit 0 at its design place), in units: its lowest pixel 46
// below; to the right the plate under it that holds the player's name (unit 0x0F of the name's group, drawn for every
// character, empty for the stand-ins), 91 (the PC's pictures of r101 and the tester's picture of 07.11.2026 with
// Wesker); while Ashley is the partner her small ring and its plate (unit 1's group) 96 (hudExtent, every frame).
const float kRingBelow = 46.0f, kRingRight = 91.0f, kRingRightPartner = 96.0f;
const float kHudMargin = 0.04f;  // of the shown part's height, as re4mm::Place
const int kBandTop = 56, kBandBottom = 392;  // the band between the game's letterbox bars (cinesco.cpp, the scissor)
float gRingRight = kRingRight;   // the right end of what is shown

}  // namespace

extern "C" void re4_core_set_hud_layout(int rows, int stretch_e4, int flags)
{
    const int top = rows & 0xFFFF, bottom = (rows >> 16) & 0xFFFF;
    const float stretch = stretch_e4 > 0 ? (float) stretch_e4 / 10000.0f : 1.0f;
    const bool narrow = (flags & 1) != 0;
    if (rows < 0 || bottom <= top || bottom > 448 || !(stretch > 0.25f && stretch < 4.0f)) {
        if (gHudTop >= 0) {
            re4_log("core: life gauge where the game puts it");
        }
        gHudTop = gHudBottom = -1;
        return;
    }
    if (top != gHudTop || bottom != gHudBottom || stretch != gHudStretch || narrow != gHudNarrow) {
        re4_log("core: life gauge in the bottom right corner of the rows %d..%d, %.4f wider across%s", top, bottom,
                stretch, narrow ? ", drawn that much narrower" : "");
    }
    gHudStretch = stretch;
    gHudNarrow = narrow;
    gHudBottom = bottom;
    gHudTop = top;
}

// game/id_sys.cpp (patch 0215): the screen matrix a unit is drawn with; IDSystem::m_scrn_mat, for the life gauge's
// units moved into the corner (see above).
extern "C" float (*re4_hud_scrn(const void* unit))[4]
{
    static Mtx moved;
    const ID_UNIT* u = (const ID_UNIT*) unit;
    int top = gHudTop, bottom = gHudBottom;
    if (!u || top < 0) {
        return IDSystem::m_scrn_mat;
    }
    const ID_UNIT* root = u;
    for (int depth = 0; root->pParent && depth < 16; depth++) {
        root = root->pParent;
    }
    if (root->pParent || root->classNo != IDC_LIFE_METER || root->markNo != 0 || root->levelNo != 0) {
        return IDSystem::m_scrn_mat;
    }
    // While the game draws its letterbox (sub screens, events, door scenes: everything but play with "4:3" or the
    // "Native" of a screen narrower than 16:9, re4_letterbox_open), the rows the screen shows end at its bars: the
    // ring goes into the corner of the band between them, as with "4:3 (GC)" (user's report 08.11.2026, "4:3" and the
    // US discs: in the inventory and the files the ring lay half under the lower bar)
    if (!re4_letterbox_open()) {
        top = top > kBandTop ? top : kBandTop;
        bottom = bottom < kBandBottom ? bottom : kBandBottom;
    }
    // the picture's units: 240 x the projection's aspect to the side, (224 - row) x 480 / 448 up
    const float aspect = re4_gx_wide_now() == 1 && gChunkAspect > 1.3333334f ? gChunkAspect : 1.3333334f;
    const float side = 240.0f * aspect, low = (224.0f - (float) bottom) * (480.0f / 448.0f),
                high = (224.0f - (float) top) * (480.0f / 448.0f);
    const float my = kHudMargin * (high - low), mx = my / gHudStretch, g = gHudNarrow ? 1.0f / gHudStretch : 1.0f;
    const float ax = root->pos0.x, ay = root->pos0.y;
    const float tx = side - mx - g * gRingRight, ty = low + my + kRingBelow;
    memcpy(moved, IDSystem::m_scrn_mat, sizeof(moved));
    moved[0][0] *= g;
    moved[0][3] += tx - g * ax;
    moved[1][3] += ty - ay;
    return moved;
}

// Every frame while the gauge is in the corner: whether Ashley's small ring is shown (unit 1 of the life meter, as
// LifeMeter::move sets it while she is the partner), which reaches further right.
static void hudExtent()
{
    if (gHudTop < 0 || !pG || !IdSys.setCk(IDC_LIFE_METER)) {
        return;
    }
    const ID_UNIT* partner = IdSys.unitPtr(1, IDC_LIFE_METER);
    const float right = partner->be_flag != 0xFF && (partner->be_flag & 8) ? kRingRightPartner : kRingRight;
    if (right != gRingRight) {
        static unsigned told;
        if (told++ < 20) {
            re4_log("picture: life gauge %s Ashley's ring", right == kRingRightPartner ? "with" : "without");
        }
        gRingRight = right;
    }
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
    re4_gx_movie(re4_movie_playing());  // the app's "Sharpen" leaves a movie's pictures as they are
    re4_letterbox_frame();
    hudExtent();
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
        // the PC's checks: RE4_VIEW_ASPECT=1.625 (the 3D's aspect, as the app's "Native" sends it),
        // RE4_HUD_LAYOUT=TOP,BOTTOM,STRETCH[,NARROW] (the life gauge's corner, as re4_core_set_hud_layout)
        const char* v = getenv("RE4_VIEW_ASPECT");
        if (v && *v) {
            re4_core_set_view_aspect((int) (atof(v) * 10000.0 + 0.5));
        }
        const char* hud = getenv("RE4_HUD_LAYOUT");
        if (hud && *hud) {
            int top = 0, bottom = 0, narrow = 0;
            float stretch = 1.0f;
            if (sscanf(hud, "%d,%d,%f,%d", &top, &bottom, &stretch, &narrow) >= 3) {
                re4_core_set_hud_layout(top | bottom << 16, (int) (stretch * 10000.0f + 0.5f), narrow);
            }
        }
    }
} g_install;

}  // namespace
