// Core bridge, game side: one snapshot of what the GameCube would draw this
// frame, for the renderer of the Android app (runtime/re4_core.h). Compiled
// with the game's flags into libre4_main because it reads the game's own
// structures (model.h, em.h, obj.h, global.h, camera.h).
//
// Per frame: the game step and room, the camera (pG->Camera.param, which
// CameraMove turns into the projection; CamCtrl's camera while a sub screen
// is open) and every live work of EmMgr (player,
// partner, enemies) and ObjMgr (weapons, objects) with
//   - cModel::mat (world placement) and be_flag (drawn when bits 1 and 2 are
//     set, trans.cpp ModelTrans),
//   - the world matrix of each cParts (after partsWorldCalc) and its bind
//     position (cParts::lt_inv_mat is the translation by minus it; the skin
//     palette is mat * lt_inv_mat),
//   - each cModelInfo's BIN and TPL with their disc source from the format
//     converter's notes, so the app loads the same data from the disc,
//   - for scenery the SMD work it shows (patch 0029: scroll.cpp setObj calls
//     re4_scroll_placed), so the app's room scene follows the game: hidden,
//     moved, or (made by a room script) not in the SMD at all.
// Every pointer is checked against MEM1 / the game libraries first: the
// snapshot may be taken in any state, also while a room is being built.
#include "types.h"
#include "em.h"
#include "global.h"
#include "model.h"
#include "cam_ctrl.h"
#include "obj.h"
#include "shape.h"

#include "re4_core.h"

#include <stdlib.h>
#include <string.h>

extern GLOBAL_WK* pG;
extern cEmMgr EmMgr;
extern cObjMgr ObjMgr;

extern "C" int re4_dvd_inserted_disc(void);
extern "C" int re4_valid_port_ptr(const void* p);

namespace {

// The faces' running shapes (CalculateShape_new per model info) only for the bridge view
// (RE4_VIEW=bridge, RE4_BRIDGE_SHAPES=1, re4_bridge_shapes): the GX view draws what the game's own
// trans.cpp computes. While a room is loaded or a checkpoint continued, a live model's shape list
// can already point at freed and reused memory; the game draws no such model, but the bridge read
// it every frame (SIGSEGV in HermiteInterpolation from re4_bridge_capture, SCV40 tester, two
// crashes on loading, 22.10.2026). -1: not decided yet.
int gShapes = -1;

bool shapesWanted()
{
    if (gShapes < 0) {
        const char* view = getenv("RE4_VIEW");
        const char* shapes = getenv("RE4_BRIDGE_SHAPES");
        gShapes = (view && strcmp(view, "bridge") == 0) || (shapes && *shapes == '1');
    }
    return gShapes != 0;
}

bool inMem1(const void* p)
{
    unsigned long a = (unsigned long) p;
    return a >= 0x80000000ul && a < 0x83000000ul;
}

// A game structure (parts, model infos, model data) lies on a 4-byte boundary. While a room is torn
// down and the next one unpacked over it, an old object's lists can point at any byte (Samsung A13,
// 32-bit, report 30.10.2026 18:24: a "Bus error" in captureModel as r205 replaced r204 - ARM32 loads a
// float only from a word-aligned address, ARM64 reads the garbage without a fault)
bool word(const void* p)
{
    return ((unsigned long) p & 3) == 0;
}

bool live(const void* p)
{
    // MEM1 (0x80000000..0x83000000, always mapped) or a game library; while a
    // room is torn down a model info can still point anywhere (0xabc28685 on
    // the Retroid, 579 s, entering r101)
    return inMem1(p) || re4_valid_port_ptr(p);
}

// The SMD work of each scroll object, per ObjMgr slot. A slot a later object
// reuses is told apart by its block and its model data.
struct ScrollNote {
    const void* obj;
    const void* bin;
    s16 blk;
    u16 index;
};
const u32 kScrollSlots = 4096;
ScrollNote gScroll[kScrollSlots];

// The ObjMgr slot of `obj`, or kScrollSlots.
u32 objSlot(const void* obj)
{
    unsigned long arr = (unsigned long) ObjMgr.pArray;
    unsigned long a = (unsigned long) obj;
    u32 stride = ObjMgr.size;
    if (!live((const void*) arr) || stride == 0 || a < arr || (a - arr) % stride) {
        return kScrollSlots;
    }
    unsigned long slot = (a - arr) / stride;
    return slot < ObjMgr.nArray && slot < kScrollSlots ? (u32) slot : kScrollSlots;
}

void copy34(float* out, const Mtx m)
{
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = m[r][c];
        }
    }
}

// trans.cpp: models the game leaves out of this frame although their be_flag
// would let them draw -- emTrans / objTrans (the display switches of
// pG->Disp_flg), ModelTrans (while STA_SUSPEND holds the world for an event
// or a close-up, only be_flag 0x800 models are drawn) and ModelRender (a
// model faded out completely).
bool gameHides(cModel* m, u8 manager)
{
    GLOBAL_WK* g = pG;
    if (manager == 0) {
        if (m == (cModel*) pPL) {
            if (DpfFlagChk(g, DPF_PL)) {
                return true;
            }
        } else if (m == (cModel*) pSUB) {
            if (DpfFlagChk(g, DPF_SUBCHAR)) {
                return true;
            }
        } else if (DpfFlagChk(g, DPF_EM)) {
            return true;
        }
    } else if (m->kindid == 2) {
        if (DpfFlagChk(g, DPF_SCR)) {
            return true;
        }
    } else if (DpfFlagChk(g, DPF_OBJ)) {
        return true;
    }
    if (StaFlagChk(g, STA_SUSPEND) && !(m->be_flag & 0x800)) {
        return true;
    }
    // ModelRender: a model faded out completely is skipped
    return m->invisible_factor * m->invisible_factor2 == 0.0f;
}

void captureModel(Re4CoreSnapshot* s, cModel* m, u8 manager, u32 slot)
{
    if (s->nModels >= RE4_CORE_MAX_MODELS) {
        s->droppedModels++;
        return;
    }
    Re4CoreModel& out = s->models[s->nModels];
    memset(&out, 0, sizeof(out));
    out.address = (u32) (unsigned long) m;
    out.beFlag = m->be_flag;
    out.manager = manager;
    out.id = m->id;
    out.type = m->type;
    copy34(out.matrix, m->mat);
    out.scrollIndex = RE4_CORE_NO_SCROLL;
    out.scrollBlock = -1;
    out.flags = gameHides(m, manager) ? RE4_CORE_MODEL_HIDDEN : 0;
    out.opacity = m->invisible_factor * m->invisible_factor2;
    out.zMode = m->z_mode;
    out.cullMode = m->CullMode;
    out.otType = m->ot_type;
    out.pad = 0;
    if (manager == 1 && m->id == cObjMgr::ID_SCROLL && slot < kScrollSlots) {
        const ScrollNote& n = gScroll[slot];
        if (n.obj == m && n.blk == ((cObj*) m)->blk && inMem1(m->pModelInfo) &&
            n.bin == m->pModelInfo->model_addr) {
            out.scrollIndex = n.index;
            out.scrollBlock = n.blk;
        }
    }
    out.firstMatrix = s->nParts;
    u32 n = m->nParts;
    cParts* p = m->pList;
    for (u32 i = 0; i < n && inMem1(p) && word(p); i++) {
        if (s->nParts >= RE4_CORE_MAX_PARTS) {
            break;
        }
        Re4CorePart& part = s->parts[s->nParts++];
        copy34(part.matrix, p->mat);
        part.bind[0] = -p->lt_inv_mat[0][3];
        part.bind[1] = -p->lt_inv_mat[1][3];
        part.bind[2] = -p->lt_inv_mat[2][3];
        out.nParts++;
        p = p->pList;
    }
    out.firstInfo = s->nInfos;
    cModelInfo* info = m->pModelInfo;
    // a guard against a broken (cyclic) list: no model has nearly 255 infos
    for (u32 guard = 0; inMem1(info) && word(info) && guard < 4096; guard++, info = info->pList) {
        if (s->nInfos >= RE4_CORE_MAX_INFOS || out.nInfos == 0xFF) {
            s->droppedInfos++;
            continue;
        }
        Re4CoreModelInfo& mi = s->infos[s->nInfos++];
        out.nInfos++;
        memset(&mi, 0, sizeof(mi));
        mi.binAddress = (u32) (unsigned long) info->model_addr;
        mi.tplAddress = (u32) (unsigned long) info->tpl_addr;
        mi.flags = info->be_flag;
        copy34(mi.matrix, info->mat);
        memcpy(mi.color, info->color, 4);
        mi.opacity = info->invisible_factor;
        mi.blendMode = info->blend_mode;
        // trans.cpp commonModelTrans: which matrix the info is drawn with
        cModelData* d = info->model_addr;
        mi.rigidPart = -1;
        if (m->be_flag & 0x4000) {
            mi.rigidPart = -2;
        } else if (live(d) && word(d) && d->weight_palette_num <= 1 && d->weight_ext_num <= 0xFF && !(info->be_flag & 2) &&
                   d->nParts == 1 && live(d->pHead) && word(d->pHead)) {
            mi.rigidPart = d->pHead->partsNo;
        }
        re4_formats_locate(info->model_addr, &mi.bin);
        re4_formats_locate(info->tpl_addr, &mi.tpl);
        // a running shape: the positions as trans.cpp makes them before skinning
        if (shapesWanted() && (info->be_flag & 2) && live(d) && word(d) && inMem1(d->vtxOrig) && d->nVtx != 0) {
            bool shaped = false;
            for (int k = 0; k < 5; k++) {
                shaped = shaped || inMem1(info->shape[k].data);
            }
            if (shaped && s->nMorph + d->nVtx <= RE4_CORE_MAX_MORPH) {
                u8* dst = (u8*) s->morph[s->nMorph];
                ResetShape(info, dst);
                for (int k = 0; k < 5; k++) {
                    if (inMem1(info->shape[k].data)) {
                        CalculateShape_new(info, info->shape[k].rate, info->shape[k].data, dst);
                    }
                }
                mi.firstMorph = s->nMorph;
                mi.nMorph = d->nVtx;
                s->nMorph += d->nVtx;
            } else if (shaped) {
                s->droppedMorph++;
            }
        }
    }
    s->nModels++;
}

template <class Mgr>
void captureManager(Re4CoreSnapshot* s, Mgr& mgr, u8 manager)
{
    u8* arr = (u8*) mgr.pArray;
    u32 n = mgr.nArray;
    u32 stride = mgr.size;
    if (!live(arr) || stride == 0 || n > 4096) {
        return;
    }
    for (u32 i = 0; i < n; i++) {
        cModel* m = (cModel*) (arr + i * stride);
        // alive (bit0) and not being destroyed (bit9)
        if ((m->be_flag & 0x201) != 1 || m->nParts == 0) {
            continue;
        }
        captureModel(s, m, manager, manager == 1 ? i : kScrollSlots);
    }
}

}  // namespace

// Patch 0029 (scroll.cpp setObj): scroll object `obj` shows work `index` of
// the room's SMD (blk -1) or of scroll block `blk`.
extern "C" void re4_scroll_placed(void* obj, int blk, int index)
{
    u32 slot = objSlot(obj);
    if (slot >= kScrollSlots || index < 0 || index >= 0xFFFF) {
        return;
    }
    cModel* m = (cModel*) obj;
    ScrollNote& n = gScroll[slot];
    n.obj = obj;
    n.bin = inMem1(m->pModelInfo) ? (const void*) m->pModelInfo->model_addr : 0;
    n.blk = (s16) blk;
    n.index = (u16) index;
}

extern "C" void re4_bridge_shapes(int on)
{
    gShapes = on ? 1 : 0;
}

extern "C" void re4_bridge_capture(Re4CoreSnapshot* s)
{
    s->apiVersion = RE4_CORE_API_VERSION;
    s->nModels = 0;
    s->nParts = 0;
    s->droppedModels = 0;
    s->nInfos = 0;
    s->droppedInfos = 0;
    s->nMorph = 0;
    s->droppedMorph = 0;
    s->cameraValid = 0;
    s->screen = 0;
    s->disc = (int8_t) re4_dvd_inserted_disc();
    GLOBAL_WK* g = pG;
    if (!live(g)) {
        return;
    }
    s->rno[0] = g->Rno0;
    s->rno[1] = g->Rno1;
    s->rno[2] = g->Rno2;
    s->rno[3] = g->Rno3;
    s->room = g->room_id;
    s->screen = (StaFlagChk(g, STA_SUB_SCRN) ? RE4_CORE_SCREEN_SUB : 0) |
                (StaFlagChk(g, STA_EVENT) ? RE4_CORE_SCREEN_EVENT : 0) |
                (DbgFlagChk(g, DBG_DBG_CAM) ? RE4_CORE_SCREEN_DEBUGCAM : 0);
    // a sub screen draws its own items with its own camera in pG->Camera
    // (sscrn.cpp keeps the world camera aside); the world behind it is the
    // controller's camera
    const CAMERA_POINT& cam = (s->screen & RE4_CORE_SCREEN_SUB) ? CamCtrl.camera.param : g->Camera.param;
    for (int k = 0; k < 3; k++) {
        s->cameraEye[k] = (&cam.Campos.x)[k];
        s->cameraTarget[k] = (&cam.Target.x)[k];
    }
    s->cameraRoll = cam.Roll;
    s->cameraFovy = cam.Fovy;
    s->cameraValid = cam.Fovy > 0.0f && cam.Fovy < 180.0f;
    captureManager(s, EmMgr, 0);
    captureManager(s, ObjMgr, 1);
}
