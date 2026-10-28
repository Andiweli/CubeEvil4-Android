// Touch in the game's sub screens (wishes of the project owner, 22.10.2026: pick the items of the
// attache case and of the key items with a finger, change the tabs with a swipe, open a file with
// a tap), compiled with the game's flags into libre4_main.
//
// The app hands a tap on the picture over (re4_core_touch: u across the game's picture, v down
// it, 0..1; CoreActivity.watchTap only passes taps beside the touch controls). Patch 0106 calls
// re4_touch_sscrn once per frame of the sub screen task, before the current screen reads Key; a
// tap younger than 400 ms becomes the cursor and keys of that frame:
//
//  - the attache case, its cursor (PieceSelect state 0) or the combine target (PieceCombine): a
//    tap on a cell moves the cursor there (sound 6, like the cross); a tap on the piece under
//    the cursor is A (the command menu, or the combination);
//  - the command menu (PieceCommand mode 0): a tap on an entry selects it (sound 0xA), a second
//    tap is A; a tap on the case closes the menu (B). Its yes/no or Leon/Ashley question (mode
//    2): a tap selects the option (left/right), a second tap is A. So nothing the game cannot
//    undo (combine, discard, use) happens on a single tap;
//  - the key items and treasures (SsItemMain, ItemSelect): a tap on an item of either row moves
//    the cursor there as the cross does (up/down for the row, left/right for the item, one press
//    a frame), a tap on the item under the cursor is A (its command menu); while combining
//    (ItemCombine) the same in the row; the command menu (ItemCommand, the layout of
//    PieceCommand, entries from id[4]) and its question (up/down) as in the case, a tap on an
//    item closes it (B);
//  - the files (SsFileMain, FileSelect): a tap on a row of the list opens that file: from the
//    category row down into the list, up/down to the row, A (a file not found yet: the game's
//    buzzer);
// The cross presses go out one a frame (Key.trg and Key.rep, as a fresh press), and stop when the
// screen changes.
//
// A swipe across the picture (re4_core_swipe; only in the normal inventory, open_flag 1, the map's
// views included) changes the tab: a swipe to the left the one to the right, a swipe to the right
// the one to the left, in the game's order (sscrnMainMenu: key items, attache case, map, files;
// the exit never). At the ends nothing happens, and a swipe while a change is on its way counts
// from the tab it goes to (device test 22.10.2026 20:09: with a ring and counting from the screen
// shown, quick swipes seemed to jump to any tab); one while a screen loads with no change on its
// way counts from the screen to come (kept 90 frames at most). With the tab row focused:
// menu_next, the highlight on that tab (as idMainMenu, sound 0xA) for 6 frames, then A
// (sscrnMainMenu switches like the cross and A; device test 20:30: without the highlight the old
// tab or the exit flashed); else B until the row has the focus, at most three times 12 frames
// apart (the case only from its cursor with nothing on the spare board and nothing in hand, as B
// does there; the key items and the files; the map from its zoomed view, its whole view zooms in
// on the first B), and the tab at the frame the row has the focus (45 frames on the screens at
// most, their loading not counted). In the frame the B gives the row the focus,
// re4_touch_sscrn_moved (patch 0106, after cur->move, before the frame is drawn) puts the
// highlight on the chosen tab at once: the game's B puts it on the exit (the case) or on the
// screen's own tab (device test 21:22: from the case the exit flashed before the map or the key
// items).
//
// Positions: the sub screen draws its id units through IDSystem::m_scrn_mat and the 3D case
// through the camera of sscrnCameraInit, both with pG->Camera.ProjMat (4:3) over the whole
// picture (640 x 448 while the sub screen is open; u and v do not depend on the width); the map
// through its own camera in its viewport (SsMapWork::vp). A piece in hand, the item examine, the
// file reader, the map's whole view and mark menu and the merchant are not touched (their buttons
// as before). A tap on the tabs of the top row was taken out after the second device test
// (decision of the project owner, 22.10.2026: they did not react); so was dragging the zoomed map
// with a finger after the device test 21:22 (it kept swipes from leaving the map).
#include "types.h"
#include "global.h"
#include "main.h"
#include "camera.h"
#include "id_sys.h"
#include "puzzle.h"
#include "item.h"
#include "snd.h"
#include "sscrn.h"
#include "ss_main.h"
#include "ss_pzzl.h"

#include <stdio.h>
#include <time.h>

extern GLOBAL_WK* pG;

extern "C" void re4_log(const char* fmt, ...);
extern "C" void re4_touch_item_keys(int curCol, int col, int n, int combine, u64* keys, int* count);
extern "C" void re4_touch_file_keys(int mode, int cursor, int target, u64* keys, int* count);

namespace {

const u64 kA = 0x80000000ull, kB = 0x40000000ull, kRight = 0x04000000ull, kLeft = 0x08000000ull;
const u64 kUp = 0x01000000ull, kDown = 0x02000000ull;
const f32 kCell = 100.0f;  // pzlGrid::size (ss_pzzl.cpp caseModelMove)
const u32 kTapAgeMs = 400;
const unsigned kTabFrames = 45;     // frames a tab waits for the row to get the focus
const unsigned kTabPresses = 3;     // B at most this often for it (the map: zoom in, then to the row)
const unsigned kTabPressGap = 12;   // frames between two of them (the map's zoom takes 10)
const unsigned kGoingFrames = 90;   // frames a chosen tab counts as the base of the next swipe
const unsigned kShowFrames = 6;     // frames the chosen tab is highlighted before A

// the app's thread writes, the game's reads: the tap as float bits, when, counted
volatile u32 gTapU, gTapV, gTapWhen, gTapSeq;
u32 gTapSeen;
bool gAnnounced;

// a tab waiting for the row's focus (after B)
struct Tab {
    int no;  // -1: none
    unsigned frames, presses, lastPress;
    unsigned shown;  // frames on the row with the highlight on it
};
Tab gTab = {-1, 0, 0, 0, 0};
// the tab chosen last (A on the row) until its screen shows, for the next swipe
int gGoing = -1;
unsigned gGoingFrames;
// swipes while a screen loads with no change on its way: they count from the screen to come
int gLater;
unsigned gLaterFrames;

unsigned gLines;

u32 nowMs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u32) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

u32 bits(float f)
{
    u32 b;
    __builtin_memcpy(&b, &f, 4);
    return b;
}

float value(u32 b)
{
    float f;
    __builtin_memcpy(&f, &b, 4);
    return f;
}

bool line()
{
    gLines++;
    return gLines <= 80 || gLines % 50 == 0;
}

// m (model to camera) then the camera's projection: the place in the picture, 0..1
bool project(const Mtx m, f32 x, f32 y, f32 z, f32* u, f32* v)
{
    Vec p = {x, y, z}, q;
    PSMTXMultVec(m, &p, &q);
    const Mtx44& P = pG->Camera.ProjMat;
    const f32 cx = P[0][0] * q.x + P[0][1] * q.y + P[0][2] * q.z + P[0][3];
    const f32 cy = P[1][0] * q.x + P[1][1] * q.y + P[1][2] * q.z + P[1][3];
    const f32 cw = P[3][0] * q.x + P[3][1] * q.y + P[3][2] * q.z + P[3][3];
    if (!(cw > 1e-6f)) {
        return false;
    }
    *u = 0.5f + 0.5f * cx / cw;
    *v = 0.5f - 0.5f * cy / cw;
    return true;
}

// inside the convex quad (either winding)
bool inQuad(const f32* qu, const f32* qv, f32 u, f32 v)
{
    int pos = 0, neg = 0;
    for (int i = 0; i < 4; i++) {
        const int j = (i + 1) & 3;
        const f32 c = (qu[j] - qu[i]) * (v - qv[i]) - (qv[j] - qv[i]) * (u - qu[i]);
        if (c > 0.0f) {
            pos++;
        } else if (c < 0.0f) {
            neg++;
        }
    }
    return pos == 0 || neg == 0;
}

} // namespace

// The quad an id unit is drawn as (IdCommonTrans: m_scrn_mat * mat, its four ver) holds (u, v).
extern "C" int re4_touch_unit_hit(const ID_UNIT* unit, f32 u, f32 v)
{
    if (!unit || unit->be_flag == 0xFF) {
        return 0;
    }
    Mtx m;
    PSMTXConcat(IDSystem::m_scrn_mat, unit->mat, m);
    f32 qu[4], qv[4];
    for (int i = 0; i < 4; i++) {
        if (!project(m, unit->ver[i].x, unit->ver[i].y, unit->ver[i].z, &qu[i], &qv[i])) {
            return 0;
        }
    }
    return inQuad(qu, qv, u, v) ? 1 : 0;
}

// The cell of board b under (u, v): cells of kCell, x right, y down (local y negative), z 0, in
// the board's matrix (caseModelMove) and the camera's view. 0 when none.
extern "C" int re4_touch_board_cell(pzlBoard* b, f32 u, f32 v, int* cellX, int* cellY)
{
    if (!b) {
        return 0;
    }
    Mtx m;
    PSMTXConcat(pG->Camera.v_mat, *b->orientation(), m);
    for (int y = 0; y < b->size_y(); y++) {
        for (int x = 0; x < b->size_x(); x++) {
            const f32 x0 = kCell * x, x1 = kCell * (x + 1), y0 = -kCell * y, y1 = -kCell * (y + 1);
            f32 qu[4], qv[4];
            if (!project(m, x0, y0, 0.0f, &qu[0], &qv[0]) || !project(m, x1, y0, 0.0f, &qu[1], &qv[1]) ||
                !project(m, x1, y1, 0.0f, &qu[2], &qv[2]) || !project(m, x0, y1, 0.0f, &qu[3], &qv[3])) {
                return 0;
            }
            if (inQuad(qu, qv, u, v)) {
                *cellX = x;
                *cellY = y;
                return 1;
            }
        }
    }
    return 0;
}

namespace {

// the case model rests (its unit 0xFE finished the slide in); unknown counts as resting
bool caseResting()
{
    if (!IdSub.setCk(IDC_SSCRN_BACK_GROUND)) {
        return true;
    }
    const ID_UNIT* u = IdSub.unitPtr(0xFE, IDC_SSCRN_BACK_GROUND);
    return (u->rev_flag & 1) == 0 && (u->anima_state & 1) != 0;
}

unsigned itemId(const pzlPiece* p)
{
    return p && p->item ? p->item->id : 0;
}

// a cell of the case, or of the spare board while it holds pieces
pzlBoard* cellAt(pzlPlayer* pl, f32 u, f32 v, int* x, int* y)
{
    if (re4_touch_board_cell(pl->boardPtr(), u, v, x, y)) {
        return pl->boardPtr();
    }
    pzlBoard* space = pl->spacePtr();
    if (space && space->getPieceNum() > 0 && re4_touch_board_cell(space, u, v, x, y)) {
        return space;
    }
    return 0;
}

// the cursor on a cell (PieceSelect / PieceCombine): A when the cell's piece is under the cursor
// already, else the cursor moves there like with the cross
void pickCell(SUB_SCREEN* wk, pzlPlayer* pl, f32 u, f32 v, const char* what)
{
    int x, y;
    pzlBoard* b = cellAt(pl, u, v, &x, &y);
    if (!b) {
        if (line()) {
            re4_log("touch: %s: not on a cell (%.2f %.2f)", what, (double) u, (double) v);
        }
        return;
    }
    pzlBoard* active = pl->m_p_active_board;
    pzlPiece* tapped = b->getPiece(x, y);
    pzlPiece* under = active ? active->getPiece(active->m_cur_x, active->m_cur_y) : 0;
    if (tapped && tapped == under) {
        Key.trg |= kA;
        if (line()) {
            re4_log("touch: %s: A on item 0x%02x at (%d %d)", what, itemId(tapped), x, y);
        }
        return;
    }
    if (b == active && x == active->m_cur_x && y == active->m_cur_y) {
        return;
    }
    pl->m_p_active_board = b;
    b->m_cur_x = (s8) x;
    b->m_cur_y = (s8) y;
    wk->cursor_flag |= 1;
    SndCall(0, 6, 0, 0, 0, 0);
    if (line()) {
        re4_log("touch: %s: cursor to (%d %d) %s, item 0x%02x", what, x, y,
                b == pl->boardPtr() ? "in the case" : "on the spare board", itemId(tapped));
    }
}

// A command menu (PieceCommand, or ItemCommand with the same layout): entries id[first + 2i] and
// id[first + 2i + 1]; `close` a tap on the case or an item (B); the question's options sub[5] /
// sub[7] by left/right, or by up/down (`vertical`, the key items).
void commandMenu(SUB_SCREEN* wk, PieceCommand* c, int first, bool vertical, bool close, f32 u, f32 v)
{
    if (c->mode == 0) {
        for (int i = 0; i < c->num && first + 1 + 2 * i < 16; i++) {
            if (re4_touch_unit_hit(c->id[first + 2 * i], u, v) || re4_touch_unit_hit(c->id[first + 1 + 2 * i], u, v)) {
                const bool press = wk->cmd_menu_no == i;
                if (press) {
                    Key.trg |= kA;
                } else {
                    wk->cmd_menu_no = (s8) i;
                    SndCall(0, 0xA, 0, 0, 0, 0);
                }
                if (line()) {
                    re4_log("touch: command %d of %d%s", i, (int) c->num, press ? ": A" : "");
                }
                return;
            }
        }
        if (close) {
            Key.trg |= kB;
            if (line()) {
                re4_log("touch: command menu closed (a tap beside it)");
            }
        }
        return;
    }
    if (c->mode == 2) {
        const int option = re4_touch_unit_hit(c->sub[5], u, v) ? 0 : re4_touch_unit_hit(c->sub[7], u, v) ? 1 : -1;
        if (option < 0) {
            return;
        }
        const bool press = c->subSel == option;
        Key.trg |= press ? kA : option == 0 ? (vertical ? kUp : kLeft) : (vertical ? kDown : kRight);
        if (line()) {
            re4_log("touch: question option %d%s", option, press ? ": A" : "");
        }
    }
}

// the attache case's own taps (its main widget in state 0)
void caseScreen(SUB_SCREEN* wk, SsPzzlMain* main, f32 u, f32 v)
{
    pzlPlayer* pl = wk->puzzlePlayer;
    if (!pl || wk->board_size != wk->board_next || !caseResting()) {
        return;
    }
    Widget<SUB_SCREEN>* child = main->cur;
    if (child == main->select && main->select->state == 0) {
        if (pl->pieceInHand()) {
            return;
        }
        pickCell(wk, pl, u, v, "case");
    } else if (child == main->combine && main->combine->state == 0) {
        pickCell(wk, pl, u, v, "combine");
    } else if (child == main->command) {
        int x, y;
        commandMenu(wk, main->command, 6, false, cellAt(pl, u, v, &x, &y) != 0, u, v);
    }
}

// The tab of the screen the player is in (0 key items, 1 attache case, 2 map, 3 files), -1 none.
int screenTab(Widget<SUB_SCREEN>* cur, SsPzzlMain* pzzl, SsItemMain* item, SsMapMain* map, SsFileMain* file)
{
    if (pzzl && cur == pzzl) {
        return 1;
    }
    if (item && cur == item) {
        return 0;
    }
    if (map && cur == map) {
        return 2;
    }
    if (file && cur == file) {
        return 3;
    }
    return -1;
}

// The tab row has the focus (the main widget in state 1).
bool onRow(Widget<SUB_SCREEN>* cur, SsPzzlMain* pzzl, SsItemMain* item, SsMapMain* map, SsFileMain* file)
{
    return (pzzl && cur == pzzl && pzzl->state == 1) || (item && cur == item && item->state == 1) ||
           (map && cur == map && map->state == 1) || (file && cur == file && file->state == 1);
}

// B now gives the tab row the focus, as the player's B does: the case only from its cursor with
// nothing in hand and nothing on the spare board (B else asks about the spare board); the key
// items and the files from their main view; the map from its zoomed view (MapRead: B leaves to
// the row) or the whole view (B zooms in first).
bool backToRow(SUB_SCREEN* wk, Widget<SUB_SCREEN>* cur, SsPzzlMain* pzzl, SsItemMain* item, SsMapMain* map,
               SsFileMain* file)
{
    if (pzzl && cur == pzzl) {
        pzlPlayer* pl = wk->puzzlePlayer;
        return pzzl->state == 0 && pzzl->cur == pzzl->select && pzzl->select->state == 0 && pl &&
               !pl->pieceInHand() && pl->spacePtr()->getPieceNum() == 0;
    }
    if (map && cur == map) {
        // MapRead / MapEntire are only declared here; their Widget base is at offset 0
        const void* view = map->cur;
        return map->state == 0 && (view == (const void*) map->read || view == (const void*) map->entire);
    }
    return (item && cur == item && item->state == 0) || (file && cur == file && file->state == 0);
}

// the swipe of the app (re4_core_swipe): -1 the tab before, 1 the next one
volatile u32 gSwipeDir, gSwipeWhen, gSwipeSeq;
u32 gSwipeSeen;

// a new tab to go to (a swipe); a change on its way keeps its B presses and frames
void wantTab(int k, const char* why)
{
    if (gTab.no < 0) {
        gTab.frames = 0;
        gTab.presses = 0;
        gTab.lastPress = 0;
    }
    gTab.no = k;
    gTab.shown = 0;
    if (line()) {
        re4_log("touch: tab %d (%s)", k, why);
    }
}

// A swipe in the normal inventory: the tab beside the one a change goes to, else beside the
// screen shown; nothing beyond the ends.
void swipeTab(int swipe, int here)
{
    const int from = gTab.no >= 0 ? gTab.no : gGoing >= 0 ? gGoing : here;
    const int to = from + swipe;
    if (from < 0) {
        // a screen loads (chosen with the cross, or the sub screen opens): from the one to come
        gLater += swipe;
        gLaterFrames = 0;
        if (line()) {
            re4_log("touch: swipe kept until the screen shows");
        }
        return;
    }
    if (to < 0 || to > 3) {
        if (line()) {
            re4_log("touch: swipe %s: no tab there", swipe > 0 ? "to the next" : "to the one before");
        }
        return;
    }
    if (to == here && gTab.no >= 0 && gTab.presses == 0) {
        // back to the screen shown before the change began: nothing to do
        gTab.no = -1;
        if (line()) {
            re4_log("touch: tab %d stays (the swipes cancel out)", here);
        }
        return;
    }
    wantTab(to, swipe > 0 ? "swipe to the next" : "swipe to the one before");
}

// the highlight of the tab row on tab k, as idMainMenu draws it (sscrnMainMenu would only redraw
// it on a cross press)
void showTab(int k)
{
    if (!IdSub.setCk(IDC_SSCRN_MAIN_MENU)) {
        return;
    }
    for (int i = 0; i < 5; i++) {
        IdSub.unitPtr(i, IDC_SSCRN_MAIN_MENU)->be_flag &= ~8;
    }
    ID_UNIT* t = IdSub.unitPtr(k, IDC_SSCRN_MAIN_MENU);
    t->be_flag |= 8;
    IdSub.setTime(t, 0);
}

// Cross presses and A going out one a frame, for the screen they were planned on.
struct KeyQueue {
    u64 keys[16];
    int len, pos;
    const void* screen;
};
KeyQueue gKeys;

void queueKeys(const void* screen, const u64* keys, int n, const char* what)
{
    gKeys.len = 0;
    gKeys.pos = 0;
    gKeys.screen = screen;
    for (int i = 0; i < n && i < 16; i++) {
        gKeys.keys[gKeys.len++] = keys[i];
    }
    if (line()) {
        re4_log("touch: %s (%d key%s)", what, gKeys.len, gKeys.len == 1 ? "" : "s");
    }
}

// this frame's key of the queue (a fresh press: trg and rep); false when none goes out
bool queuedKey(const void* screen)
{
    if (gKeys.pos >= gKeys.len) {
        return false;
    }
    if (screen != gKeys.screen) {
        gKeys.len = gKeys.pos = 0;
        return false;
    }
    const u64 k = gKeys.keys[gKeys.pos++];
    Key.trg |= k;
    Key.rep |= k;
    return true;
}

// --- the key items and treasures (ss_item.cpp) ---

// the slot unit of row col (0 key items, 1 treasures) at n (-3..4) from the cursor (frameMarkNo)
int slotMark(int col, int n)
{
    return (col ? 0x55 : 0x45) - n;
}

// the shown item under (u, v): its row and its place from that row's cursor
bool itemSlotAt(f32 u, f32 v, int* col, int* n)
{
    if (!IdNum.setCk(IDC_SSCRN_1)) {
        return false;
    }
    for (int c = 0; c < 2; c++) {
        for (int k = -3; k <= 4; k++) {
            const ID_UNIT* unit = IdNum.unitPtr((u8) slotMark(c, k), IDC_SSCRN_1);
            if ((unit->be_flag & 8) && re4_touch_unit_hit(unit, u, v)) {
                *col = c;
                *n = k;
                return true;
            }
        }
    }
    return false;
}

void itemScreen(SUB_SCREEN* wk, SsItemMain* item, f32 u, f32 v)
{
    ItemScreenWork* iw = wk->item;
    if (!iw || iw->col < 0) {
        return;
    }
    // ItemSelect / ItemCommand / ItemCombine are only declared here; their Widget base is at 0
    const void* child = item->cur;
    int col, n;
    const bool slot = itemSlotAt(u, v, &col, &n);
    if (child == (const void*) item->cmd) {
        commandMenu(wk, (PieceCommand*) (void*) item->cmd, 4, true, slot, u, v);
        return;
    }
    const bool combine = child == (const void*) item->comb;
    if (!slot || (child != (const void*) item->sel && !combine)) {
        return;
    }
    u64 keys[8];
    int k = 0;
    re4_touch_item_keys(iw->col, col, n, combine, keys, &k);
    if (k == 0) {
        return;
    }
    char what[64];
    snprintf(what, sizeof what, "%s: row %d, %+d from the cursor", combine ? "combine" : "key items", col, n);
    queueKeys(item, keys, k, what);
}

// --- the files (ss_file.cpp) ---

// SsFileWork (ss_file.cpp) up to the cursor
struct FileWorkView {
    s8 mode;    // 0 category row, 1 list
    s8 cat;
    s8 scroll;  // first listed row
    s8 cursor;  // row (0: the radio log)
};
const int kFileRows[3] = {13, 11, 8};  // ss_file.cpp fileNum

// the place of an id unit's origin in the picture
bool unitCentre(const ID_UNIT* unit, f32* u, f32* v)
{
    Mtx m;
    PSMTXConcat(IDSystem::m_scrn_mat, unit->mat, m);
    return project(m, 0.0f, 0.0f, 0.0f, u, v);
}

void fileScreen(SUB_SCREEN* wk, SsFileMain* file, f32 u, f32 v)
{
    FileWorkView* fw = (FileWorkView*) (void*) wk->file;
    if (!fw || file->cur != (Widget<SUB_SCREEN>*) (void*) file->sel || fw->cat < 0 || fw->cat > 2 ||
        !IdSub.setCk(IDC_SSCRN_FAR_1)) {
        return;
    }
    // the five listed rows: their name units 0x21.. (dispFileList), a row's height apart
    f32 ru[5], rv[5];
    for (int k = 0; k < 5; k++) {
        if (!unitCentre(IdSub.unitPtr((u8) (0x21 + k), IDC_SSCRN_FAR_1), &ru[k], &rv[k])) {
            return;
        }
    }
    const f32 height = rv[1] - rv[0] > 0.0f ? rv[1] - rv[0] : rv[0] - rv[1];
    int row = -1;
    for (int k = 0; k < 5; k++) {
        const f32 dv = v - rv[k];
        if ((dv < 0.0f ? -dv : dv) < height * 0.5f && u > ru[k] - 0.06f && u < ru[k] + 0.5f) {
            row = k;
        }
    }
    const int target = fw->scroll + row;
    if (row < 0 || height <= 0.0f || target >= kFileRows[(int) fw->cat]) {
        return;
    }
    u64 keys[16];
    int k = 0;
    re4_touch_file_keys(fw->mode, fw->cursor, target, keys, &k);
    char what[48];
    snprintf(what, sizeof what, "files: open row %d", target);
    queueKeys(file, keys, k, what);
}

} // namespace

// The cross presses for a tap on the key items: the row (up/down) and the item (left/right) from
// the cursor's, A on the item under it; combining only within the row. Test hook too.
extern "C" void re4_touch_item_keys(int curCol, int col, int n, int combine, u64* keys, int* count)
{
    int k = 0;
    if (combine && col != curCol) {
        *count = 0;
        return;
    }
    if (col == curCol && n == 0) {
        keys[k++] = kA;
        *count = k;
        return;
    }
    if (col != curCol) {
        keys[k++] = col > curCol ? kDown : kUp;
    }
    for (int i = 0; i < (n < 0 ? -n : n) && k < 8; i++) {
        keys[k++] = n > 0 ? kRight : kLeft;
    }
    *count = k;
}

// The presses that open file row `target`: from the category row down into the list, up/down
// from the cursor, A. Test hook too.
extern "C" void re4_touch_file_keys(int mode, int cursor, int target, u64* keys, int* count)
{
    int k = 0;
    if (mode == 0) {
        keys[k++] = kDown;
    }
    for (int i = cursor; i < target && k < 15; i++) {
        keys[k++] = kDown;
    }
    for (int i = cursor; i > target && k < 15; i--) {
        keys[k++] = kUp;
    }
    keys[k++] = kA;
    *count = k;
}

// Patch 0106 (src/Sscrn/ss_main.cpp SubScreenTask), every frame before cur->move: the screens'
// main widgets (0 outside the normal inventory) to tell where the player is.
extern "C" void re4_touch_sscrn(SUB_SCREEN* wk, Widget<SUB_SCREEN>* cur, SsPzzlMain* pzzl, SsItemMain* item,
                                SsMapMain* map, SsFileMain* file)
{
    bool tap = false;
    f32 u = 0.0f, v = 0.0f;
    const u32 seq = __atomic_load_n(&gTapSeq, __ATOMIC_ACQUIRE);
    if (seq != gTapSeen) {
        gTapSeen = seq;
        u = value(__atomic_load_n(&gTapU, __ATOMIC_RELAXED));
        v = value(__atomic_load_n(&gTapV, __ATOMIC_RELAXED));
        tap = nowMs() - __atomic_load_n(&gTapWhen, __ATOMIC_RELAXED) <= kTapAgeMs;
    }
    int swipe = 0;
    const u32 swipeSeq = __atomic_load_n(&gSwipeSeq, __ATOMIC_ACQUIRE);
    if (swipeSeq != gSwipeSeen) {
        gSwipeSeen = swipeSeq;
        if (nowMs() - __atomic_load_n(&gSwipeWhen, __ATOMIC_RELAXED) <= kTapAgeMs) {
            swipe = (int) __atomic_load_n(&gSwipeDir, __ATOMIC_RELAXED) == 1 ? 1 : -1;
        }
    }
    if (!wk || !cur || wk->Loop != 1 || wk->pzzl_debug_open || wk->item_make_open) {
        gTab.no = -1;
        gGoing = -1;
        gLater = 0;
        gKeys.len = gKeys.pos = 0;
        return;
    }
    const bool row = onRow(cur, pzzl, item, map, file);
    // here -1: a screen still loads (its init widget); the frames of a change wait for it
    const int here = screenTab(cur, pzzl, item, map, file);
    if (gGoing >= 0 && (here == gGoing || (here >= 0 && ++gGoingFrames > kGoingFrames))) {
        gGoing = -1;
    }
    if (gLater != 0 && (here >= 0 || ++gLaterFrames > kGoingFrames)) {
        const int later = gLater;
        gLater = 0;
        for (int i = 0; here >= 0 && i < (later < 0 ? -later : later) && wk->open_flag == SS_OPEN_NORMAL; i++) {
            swipeTab(later < 0 ? -1 : 1, here);
        }
    }
    if (swipe && wk->open_flag == SS_OPEN_NORMAL) {
        swipeTab(swipe, here);
        gKeys.len = gKeys.pos = 0;
        tap = false;
    }
    if (gTab.no >= 0) {
        if (row) {
            // the chosen tab highlighted a few frames (with the cross's sound), then A
            if (gTab.shown == 0 || wk->menu_next != (u8) gTab.no) {
                wk->menu_next = (u8) gTab.no;
                showTab(gTab.no);
                SndCall(0, 0xA, 0, 0, 0, 0);
                gTab.shown = 0;
            }
            if (++gTab.shown > kShowFrames) {
                Key.trg |= kA;
                gGoing = gTab.no;
                gGoingFrames = 0;
                gTab.no = -1;
            }
            return;
        }
        // B until the row has the focus (the map's whole view zooms in first), a few frames
        // apart so that each is a new press
        if (gTab.presses < kTabPresses && (gTab.presses == 0 || gTab.frames - gTab.lastPress >= kTabPressGap) &&
            backToRow(wk, cur, pzzl, item, map, file)) {
            Key.trg |= kB;
            gTab.presses++;
            gTab.lastPress = gTab.frames;
        }
        if (here >= 0 && ++gTab.frames > kTabFrames) {
            if (line()) {
                re4_log("touch: tab %d dropped (the row did not get the focus)", gTab.no);
            }
            gTab.no = -1;
        }
        return;
    }
    if (queuedKey(cur)) {
        return;
    }
    if (!tap) {
        return;
    }
    if (pzzl && cur == pzzl && pzzl->state == 0 && (wk->open_flag == SS_OPEN_NORMAL || wk->open_flag == SS_OPEN_PZZL)) {
        caseScreen(wk, pzzl, u, v);
    } else if (item && cur == item && item->state == 0) {
        itemScreen(wk, item, u, v);
    } else if (file && cur == file && file->state == 0) {
        fileScreen(wk, file, u, v);
    }
}

// Patch 0106, after cur->move (before the frame is drawn): the row got the focus in this frame (the
// B above), so the chosen tab gets the highlight now, not one frame later: the game's B put it on
// the exit (the case, PieceSelect) or on the screen's own tab. Its sound came with the game's B.
extern "C" void re4_touch_sscrn_moved(SUB_SCREEN* wk, Widget<SUB_SCREEN>* cur, SsPzzlMain* pzzl, SsItemMain* item,
                                      SsMapMain* map, SsFileMain* file)
{
    if (gTab.no < 0 || gTab.shown != 0 || !wk || !cur || wk->Loop != 1 || !onRow(cur, pzzl, item, map, file)) {
        return;
    }
    wk->menu_next = (u8) gTab.no;
    showTab(gTab.no);
    gTab.shown = 1;
}

// The app (CoreBridge.swipe): a swipe across the picture, -1 to the right (the tab before), 1 to
// the left (the next tab); any thread.
extern "C" void re4_core_swipe(int dir)
{
    static bool announced;
    if (!announced) {
        announced = true;
        re4_log("touch: swipes change the tab of the sub screen");
    }
    __atomic_store_n(&gSwipeDir, (u32) (dir > 0 ? 1 : 0), __ATOMIC_RELAXED);
    __atomic_store_n(&gSwipeWhen, nowMs(), __ATOMIC_RELAXED);
    __atomic_fetch_add(&gSwipeSeq, 1, __ATOMIC_RELEASE);
}

// The app (CoreBridge.tap): a tap on the picture, any thread.
extern "C" void re4_qte_tap(void);

extern "C" void re4_core_touch(float u, float v)
{
    // the quick-time prompts take any tap on the picture (qte_touch.cpp)
    re4_qte_tap();
    if (!gAnnounced) {
        gAnnounced = true;
        re4_log("touch: taps on the picture reach the sub screens");
    }
    if (!(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f)) {
        return;
    }
    __atomic_store_n(&gTapU, bits(u), __ATOMIC_RELAXED);
    __atomic_store_n(&gTapV, bits(v), __ATOMIC_RELAXED);
    __atomic_store_n(&gTapWhen, nowMs(), __ATOMIC_RELAXED);
    __atomic_fetch_add(&gTapSeq, 1, __ATOMIC_RELEASE);
}
