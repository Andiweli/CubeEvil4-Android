// Compiled with the game's flags and headers: builds a fake game state for the
// core bridge test (tests/core_tests.cpp) -- one live enemy work in EmMgr with
// two parts and one model info, a camera and a room in pG -- and restores the
// original manager state afterwards. Only the fields the bridge reads are
// set; no game code runs on the fake objects.
#include "types.h"
#include "em.h"
#include "global.h"
#include "model.h"
#include "obj.h"

#include <string.h>

extern GLOBAL_WK* pG;
extern cEmMgr EmMgr;
extern cObjMgr ObjMgr;
extern "C" void re4_scroll_placed(void* obj, int blk, int index);

namespace {

cEm* gSavedArray;
u32 gSavedCount;
u32 gSavedSize;
u16 gSavedRoom;
u8 gSavedRno0;
CAMERA_POINT gSavedCamera;

void setMatrix(Mtx m, float tx, float ty, float tz)
{
    memset(m, 0, sizeof(Mtx));
    m[0][0] = m[1][1] = m[2][2] = 1.0f;
    m[0][3] = tx;
    m[1][3] = ty;
    m[2][3] = tz;
}

// what the cModel / cModelInfo constructors set: fully visible, white
void visible(cModel* m)
{
    m->invisible_factor = 1.0f;
    m->invisible_factor2 = 1.0f;
}

void visible(cModelInfo* info)
{
    info->invisible_factor = 1.0f;
    memset(info->color, 0xFF, 4);
}

}  // namespace

// `mem`: zeroed scratch memory in MEM1 (at least 0x6000 bytes), `bin` and `tpl`
// the addresses of two converted archive entries.
extern "C" void re4probe_core_setup(void* mem, void* bin, void* tpl)
{
    u8* base = (u8*) mem;
    cEm* em = (cEm*) base;
    cParts* parts = (cParts*) (base + 0x2000);
    cModelInfo* info = (cModelInfo*) (base + 0x3000);
    // a second, rigid info hung on parts 1 (cModelData with one part, no
    // weights) and moved up by its own matrix
    cModelInfo* rigid = (cModelInfo*) (base + 0x3400);
    cModelData* data = (cModelData*) (base + 0x3800);
    ModelDataHead* head = (ModelDataHead*) (base + 0x3900);
    em->be_flag = 1 | 2 | 4;
    em->id = 0x12;
    em->type = 3;
    visible(em);
    em->z_mode = 1;  // as the officers' heads in r100's events: depth test only
    em->nParts = 2;
    setMatrix(em->mat, 100.0f, 0.0f, -50.0f);
    em->pList = &parts[0];
    for (int i = 0; i < 2; i++) {
        setMatrix(parts[i].mat, 100.0f, 10.0f * (i + 1), -50.0f);
        setMatrix(parts[i].lt_inv_mat, 0.0f, -10.0f * (i + 1), 0.0f);
        parts[i].pList = i == 0 ? &parts[1] : 0;
    }
    info->model_addr = (cModelData*) bin;
    info->tpl_addr = tpl;
    info->be_flag = 8;
    visible(info);
    info->blend_mode = 2;  // added on (a glow)
    info->color[3] = 0x80;
    setMatrix(info->mat, 0.0f, 0.0f, 0.0f);
    info->pList = rigid;
    head->partsNo = 1;
    data->pHead = head;
    data->nParts = 1;
    data->weight_palette_num = 1;
    data->weight_ext_num = 0;
    rigid->model_addr = data;
    rigid->tpl_addr = tpl;
    rigid->be_flag = 8;
    visible(rigid);
    setMatrix(rigid->mat, 0.0f, 5.0f, 0.0f);
    // seven more skinned infos like Leon's (nine in all: body, costume, knife,
    // face, head, hair, hands); the last one with the hair's be_flag 0x40
    cModelInfo* last = rigid;
    for (int i = 0; i < 7; i++) {
        cModelInfo* more = (cModelInfo*) (base + 0x4000 + i * 0x400);
        more->model_addr = (cModelData*) bin;
        more->tpl_addr = tpl;
        more->be_flag = i == 6 ? 0x48 : 8;
        visible(more);
        setMatrix(more->mat, 0.0f, 0.0f, 0.0f);
        more->pList = 0;
        last->pList = more;
        last = more;
    }
    em->pModelInfo = info;

    gSavedArray = EmMgr.pArray;
    gSavedCount = EmMgr.nArray;
    gSavedSize = EmMgr.size;
    EmMgr.pArray = em;
    EmMgr.nArray = 1;
    EmMgr.size = 0x2000;

    gSavedRoom = pG->room_id;
    gSavedRno0 = pG->Rno0;
    gSavedCamera = pG->Camera.param;
    pG->room_id = 0x100;
    pG->Rno0 = 3;
    pG->Camera.param.Campos.x = 1.0f;
    pG->Camera.param.Campos.y = 2.0f;
    pG->Camera.param.Campos.z = 3.0f;
    pG->Camera.param.Target.x = 4.0f;
    pG->Camera.param.Target.y = 5.0f;
    pG->Camera.param.Target.z = 6.0f;
    pG->Camera.param.Roll = 0.25f;
    pG->Camera.param.Fovy = 50.0f;
}

extern "C" void re4probe_core_restore(void)
{
    EmMgr.pArray = gSavedArray;
    EmMgr.nArray = gSavedCount;
    EmMgr.size = gSavedSize;
    pG->room_id = gSavedRoom;
    pG->Rno0 = gSavedRno0;
    pG->Camera.param = gSavedCamera;
}

// The display switches the bridge follows (trans.cpp emTrans / ModelTrans),
// on the fake enemy of re4probe_core_setup (`mem` as given there): what 1 = an
// event holds the world (STA_SUSPEND), 2 = the same with the model's be_flag
// 0x800 (setNoSuspend), 3 = the model is the player and DPF_PL is on, 4 =
// DPF_EM is on (the model is an enemy), 5 = faded out completely
// (invisible_factor 0), 0 = everything back.
static u32 gSavedDisp;
static u32 gSavedStatus[4];
static cPlayer* gSavedPl;

extern "C" void re4probe_core_display(void* mem, int what)
{
    cEm* em = (cEm*) mem;
    if (what == 1) {
        gSavedDisp = pG->Disp_flg;
        memcpy(gSavedStatus, pG->Status_flg, sizeof(gSavedStatus));
        gSavedPl = pPL;
    }
    pG->Disp_flg = gSavedDisp;
    memcpy(pG->Status_flg, gSavedStatus, sizeof(gSavedStatus));
    pPL = gSavedPl;
    em->be_flag &= ~0x800;
    em->invisible_factor = 1.0f;
    switch (what) {
    case 1:
        StaFlagOn(pG, STA_SUSPEND);
        break;
    case 2:
        StaFlagOn(pG, STA_SUSPEND);
        em->be_flag |= 0x800;
        break;
    case 3:
        pPL = (cPlayer*) em;
        DpfFlagOn(pG, DPF_PL);
        break;
    case 4:
        DpfFlagOn(pG, DPF_EM);
        break;
    case 5:
        em->invisible_factor = 0.0f;
        break;
    }
}

// Scenery in ObjMgr (cObjScr, id 2), for the bridge's SMD works (patch 0029):
// slot 0 is work 5 of the room's SMD, slot 1 a room script's scenery (never
// placed from an SMD), slot 2 was work 7 of scroll block 3 but now holds other
// model data (the slot was reused). `mem`: zeroed MEM1 memory, 0x2000 bytes.
static cObj* gSavedObjArray;
static u32 gSavedObjCount;
static u32 gSavedObjSize;
static u32 gSavedEmCount;

extern "C" void re4probe_scroll_setup(void* mem, void* bin, void* other)
{
    u8* base = (u8*) mem;
    const u32 stride = 0x500;
    cParts* parts = (cParts*) (base + 3 * stride);
    cModelInfo* info = (cModelInfo*) (base + 3 * stride + 0x200);
    gSavedObjArray = ObjMgr.pArray;
    gSavedObjCount = ObjMgr.nArray;
    gSavedObjSize = ObjMgr.size;
    gSavedEmCount = EmMgr.nArray;
    ObjMgr.pArray = (cObj*) base;
    ObjMgr.nArray = 3;
    ObjMgr.size = stride;
    EmMgr.nArray = 0;
    setMatrix(parts->mat, 0.0f, 0.0f, 0.0f);
    info->model_addr = (cModelData*) bin;
    info->tpl_addr = bin;
    info->be_flag = 8;
    visible(info);
    for (int i = 0; i < 3; i++) {
        cObj* o = (cObj*) (base + i * stride);
        o->be_flag = 1 | 2 | 4;
        visible(o);
        o->id = cObjMgr::ID_SCROLL;
        o->nParts = 1;
        o->pList = parts;
        o->pModelInfo = info;
        o->blk = i == 2 ? 3 : -1;
        setMatrix(o->mat, 10.0f * i, 0.0f, 0.0f);
    }
    re4_scroll_placed(base, -1, 5);
    re4_scroll_placed(base + 2 * stride, 3, 7);
    // slot 2 reused: another model there now
    cModelInfo* info2 = (cModelInfo*) (base + 3 * stride + 0x600);
    *info2 = *info;
    info2->model_addr = (cModelData*) other;
    ((cObj*) (base + 2 * stride))->pModelInfo = info2;
}

extern "C" void re4probe_scroll_restore(void)
{
    ObjMgr.pArray = gSavedObjArray;
    ObjMgr.nArray = gSavedObjCount;
    ObjMgr.size = gSavedObjSize;
    EmMgr.nArray = gSavedEmCount;
}

// A face with a running shape (shape.cpp): two vertices, one shape channel
// whose curve holds weight 100 % and moves vertex 1 by (4, -8, 12). `mem`:
// zeroed MEM1 memory, 0x4000 bytes.
static u32 gSavedShapeCount;
static cEm* gSavedShapeArray;
static u32 gSavedShapeSize;

static void host16(u8* p, u16 v)
{
    memcpy(p, &v, 2);  // counts and frames: host order, as the converter leaves them
}

extern "C" void re4probe_shape_setup(void* mem)
{
    u8* base = (u8*) mem;
    cEm* em = (cEm*) base;
    cParts* parts = (cParts*) (base + 0x2000);
    cModelInfo* info = (cModelInfo*) (base + 0x3000);
    cModelData* d = (cModelData*) (base + 0x3400);
    s16* vtx = (s16*) (base + 0x3500);
    u8* tbl = base + 0x3600;
    u8* sd = base + 0x3800;
    em->be_flag = 1 | 2 | 4;
    em->id = 0x18;
    visible(em);
    em->nParts = 1;
    em->pList = parts;
    em->pModelInfo = info;
    setMatrix(em->mat, 0.0f, 0.0f, 0.0f);
    setMatrix(parts->mat, 0.0f, 0.0f, 0.0f);
    const s16 v[8] = { 10, 20, 30, 0, 40, 50, 60, 0 };
    memcpy(vtx, v, sizeof(v));
    d->vtxOrig = vtx;
    d->nVtx = 2;
    d->shapeOfs = (u32) (tbl - (u8*) d) - 4;
    // shape table: the entry count, entry 0 -> one delta (vertex 1)
    const u32 count = 1;
    memcpy(tbl - 4, &count, 4);
    const u32 entry[2] = { 0x10, 1 };
    memcpy(tbl, entry, sizeof(entry));
    const s16 delta[4] = { 1, 4, -8, 12 };
    memcpy(tbl + 0x10, delta, sizeof(delta));
    info->model_addr = d;
    info->be_flag = 8 | 2;
    visible(info);
    // ShapeData: 20 frames, one channel (kind 4, float keys), shape entry 0,
    // its curve at +0x20: three axes of two keys, the weight axis (y) 100
    host16(sd, 20);
    sd[2] = 1;
    host16(sd + 3, 0x0004);
    sd[5] = 0;
    const s32 curve = 0x20;
    memcpy(sd + 0xC, &curve, 4);
    u8* c = sd + 0x20;
    for (int axis = 0; axis < 3; axis++) {
        host16(c, 2);
        host16(c + 2, 0);
        host16(c + 4, 10);
        // the key values stay big-endian (motion.cpp FCC_F32 reads them bytewise)
        for (int k = 0; k < 2; k++) {
            const float key[3] = { axis == 1 ? 100.0f : 0.0f, 0.0f, 0.0f };
            for (int j = 0; j < 3; j++) {
                u32 bits;
                memcpy(&bits, &key[j], 4);
                u8* q = c + 6 + k * 12 + j * 4;
                q[0] = (u8) (bits >> 24);
                q[1] = (u8) (bits >> 16);
                q[2] = (u8) (bits >> 8);
                q[3] = (u8) bits;
            }
        }
        c += 6 + 2 * 12;
    }
    info->shape[0].rate = 0.0f;
    info->shape[0].data = (ShapeData*) sd;
    gSavedShapeArray = EmMgr.pArray;
    gSavedShapeCount = EmMgr.nArray;
    gSavedShapeSize = EmMgr.size;
    EmMgr.pArray = em;
    EmMgr.nArray = 1;
    EmMgr.size = 0x2000;
}

extern "C" void re4probe_shape_restore(void)
{
    EmMgr.pArray = gSavedShapeArray;
    EmMgr.nArray = gSavedShapeCount;
    EmMgr.size = gSavedShapeSize;
}

static_assert(sizeof(cObj) <= 0x500, "the fake scenery works overlap");
static_assert(sizeof(cEm) <= 0x2000, "the fake enemy work overlaps its parts");
static_assert(2 * sizeof(cParts) <= 0x1000, "the fake parts overlap the model info");
static_assert(sizeof(cModelInfo) <= 0x400 && sizeof(cModelData) <= 0x100, "the fake infos overlap");


// Sound (M8): the SDK's AX library (src/lib/AX*.c, unchanged) on the DSP of
// runtime/dsp.cpp and the AI of runtime/ai.cpp: voices with the SDK's
// allocation, PCM16 and DSP-ADPCM from ARAM (decoded exactly), volume, pan,
// envelope, loops, rate conversion, an aux bus through a CPU effect, the AI
// clock. Returns the number of failed checks (each logged).
#include "dolphin/ax.h"
#include "dolphin/syn.h"

#include <stdlib.h>

extern "C" {
void __SYNSetupSample(SYNVOICE* voice);
void __SYNSetupSrc(SYNVOICE* voice);
void __SYNUpdateSrc(SYNVOICE* voice);
void re4_log(const char* fmt, ...);
void re4_ai_field(void);
void re4_ai_block(void);
unsigned re4_ai_blocks(void);
int re4_audio_take(short* out, int frames);
int re4_audio_read(short* out, int frames);
int re4_audio_take4(short* out, short* out4, int frames);
int re4_audio_read6(short* out, int frames);
unsigned re4_audio_surround_blocks(void);
int re4_audio_surround_now(void);
void re4_dsp_loudness(unsigned* compressed, unsigned* clipped);
unsigned char* re4_aram(void);
void AIInit(unsigned char* stack);
}

static int gAxDrops;
static int gAxFrames;
static void axDrop(void*)
{
    gAxDrops++;
}
static void axFrame()
{
    gAxFrames++;
}

#define AX_CHECK(cond)                                                     \
    do {                                                                   \
        if (!(cond)) {                                                     \
            re4_log("core-test ax: %s:%d: %s", __FILE__, __LINE__, #cond); \
            failed++;                                                      \
        }                                                                  \
    } while (0)

static u32 axCurrent(AXVPB* v)
{
    return ((u32) v->pb.addr.currentAddressHi << 16) | v->pb.addr.currentAddressLo;
}

// `blocks` AI blocks; their output (left/right pairs) into `out` (at most `max` frames)
static int axRun(int blocks, short* out, int max)
{
    int got = 0;
    for (int i = 0; i < blocks; i++) {
        re4_ai_block();
        static short block[2 * 160];
        int n = re4_audio_take(block, 160);
        for (int k = 0; k < n && got < max; k++, got++) {
            out[got * 2] = block[k * 2];
            out[got * 2 + 1] = block[k * 2 + 1];
        }
    }
    return got;
}

static int firstNonZero(const short* out, int frames)
{
    for (int i = 0; i < frames; i++) {
        if (out[i * 2] || out[i * 2 + 1]) {
            return i;
        }
    }
    return -1;
}

static void putPcm16(u32 byteAddr, int n, int (*f)(int))
{
    unsigned char* a = re4_aram() + byteAddr;
    for (int i = 0; i < n; i++) {
        int v = f(i);
        a[i * 2] = (unsigned char) (v >> 8);
        a[i * 2 + 1] = (unsigned char) v;
    }
}

static int ramp8(int i)
{
    return i * 8;
}
static int dc3000(int)
{
    return 3000;
}
static int dc20000(int)
{
    return 20000;
}
static int saw(int i)
{
    return (i % 100) * 300 - 15000;
}

static void pcmVoice(AXVPB* v, u32 sampleAddr, u32 count, u16 vl, u16 vr, int loop)
{
    AXPBADDR addr;
    memset(&addr, 0, sizeof(addr));
    addr.format = 10;
    addr.loopFlag = (u16) (loop >= 0);
    u32 loopAt = sampleAddr + (loop >= 0 ? (u32) loop : 0);
    addr.loopAddressHi = (u16) (loopAt >> 16);
    addr.loopAddressLo = (u16) loopAt;
    u32 end = sampleAddr + count - 1;
    addr.endAddressHi = (u16) (end >> 16);
    addr.endAddressLo = (u16) end;
    addr.currentAddressHi = (u16) (sampleAddr >> 16);
    addr.currentAddressLo = (u16) sampleAddr;
    AXSetVoiceType(v, 0);
    AXSetVoiceAddr(v, &addr);
    AXSetVoiceSrcType(v, AX_SRC_TYPE_NONE);
    AXPBVE ve = { 0x8000, 0 };
    AXSetVoiceVe(v, &ve);
    AXPBMIX mix;
    memset(&mix, 0, sizeof(mix));
    mix.vL = vl;
    mix.vR = vr;
    AXSetVoiceMix(v, &mix);
}

// DSP-ADPCM as documented: sample = ((nibble << scale) << 11 + c1 * yn1 + c2 * yn2 + 1024) >> 11
static int adpcmRef(const unsigned char* frames, int count, const s16 coef[8][2], int ps, s16* out)
{
    int yn1 = 0, yn2 = 0, n = 0;
    for (int f = 0; n < count; f++) {
        const unsigned char* fr = frames + f * 8;
        if (f > 0) {
            ps = fr[0];
        }
        for (int k = 0; k < 14 && n < count; k++) {
            int b = fr[1 + k / 2];
            int nib = (k & 1) ? (b & 15) : (b >> 4);
            if (nib >= 8) {
                nib -= 16;
            }
            int c = (ps >> 4) & 7;
            int v = (((nib << (ps & 15)) << 11) + coef[c][0] * yn1 + coef[c][1] * yn2 + 1024) >> 11;
            v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            out[n++] = (s16) v;
            yn2 = yn1;
            yn1 = v;
        }
    }
    return n;
}

static int gAuxSeen;
static void auxEffect(void* data, void*)
{
    s32** bus = (s32**) data;  // AX_AUX_DATA: l, r, s
    for (int i = 0; i < 160; i++) {
        if (bus[0][i] > gAuxSeen) {
            gAuxSeen = bus[0][i];
        }
        bus[0][i] = 1000;
        bus[1][i] = 0;
        bus[2][i] = 0;
    }
}

// Dolby Pro Logic II's aux A effect (AX_AUX_DATA_DPL2: l, r, ls, rs): what it saw of Ls and Rs,
// and a constant back on each channel
static int gDpl2SeenLs, gDpl2SeenRs;
static void auxEffectDpl2(void* data, void*)
{
    s32** bus = (s32**) data;
    for (int i = 0; i < 160; i++) {
        gDpl2SeenLs = bus[2][i] > gDpl2SeenLs ? bus[2][i] : gDpl2SeenLs;
        gDpl2SeenRs = bus[3][i] > gDpl2SeenRs ? bus[3][i] : gDpl2SeenRs;
        bus[0][i] = 100;
        bus[1][i] = 200;
        bus[2][i] = 300;
        bus[3][i] = 400;
    }
}

// `blocks` AI blocks; the stereo and the four channels of their last frame
static void axRun4(int blocks, short last2[2], short last4[4])
{
    static short st[2 * 160], four[4 * 160];
    for (int i = 0; i < blocks; i++) {
        re4_ai_block();
        int n;
        while ((n = re4_audio_take4(st, four, 160)) > 0) {
            memcpy(last2, &st[(n - 1) * 2], 2 * sizeof(short));
            memcpy(last4, &four[(n - 1) * 4], 4 * sizeof(short));
        }
    }
}

extern "C" int re4probe_ax_test(void)
{
    int failed = 0;
    static short out[2 * 160 * 40];
    AIInit(0);
    AXInitEx(1);  // triple buffering, as snd_main.cpp starts it
    AXCallback old = AXRegisterCallback(axFrame);
    re4_audio_take(out, 160 * 40);  // whatever an earlier test left

    // the SDK's allocation: 64 voices; the 65th of a higher priority takes the
    // oldest of the lowest, its callback runs with the next frame
    AXVPB* v[64];
    for (int i = 0; i < 64; i++) {
        v[i] = AXAcquireVoice(i < 2 ? 1 : 5, axDrop, i);
        AX_CHECK(v[i] != 0);
    }
    AX_CHECK(AXAcquireVoice(1, axDrop, 99) == 0);
    AXVPB* taken = AXAcquireVoice(10, axDrop, 100);
    AX_CHECK(taken == v[0] && taken->userContext == 100);
    axRun(1, out, 160);
    AX_CHECK(gAxDrops == 1 && gAxFrames >= 1);
    for (int i = 1; i < 64; i++) {
        AXFreeVoice(v[i]);
    }
    AXFreeVoice(taken);
    axRun(3, out, 160 * 3);

    // PCM16 at 1:1 into left (full) and right (half), stops at its end
    putPcm16(0x10000, 4000, ramp8);
    AXVPB* p = AXAcquireVoice(5, 0, 0);
    pcmVoice(p, 0x8000, 4000, 0x8000, 0x4000, -1);
    AXSetVoiceState(p, 1);
    int got = axRun(32, out, 160 * 32);
    int at = firstNonZero(out, got);
    AX_CHECK(at >= 0 && at <= 2 * 160 + 4);
    // (the last samples still in the rate converter when the voice stops are lost)
    bool exact = at >= 0;
    for (int j = 0; exact && j < 3990; j++) {
        exact = out[(at + j) * 2] == 8 * (j + 1) && out[(at + j) * 2 + 1] == (8 * (j + 1)) >> 1;
    }
    AX_CHECK(exact);
    AX_CHECK(at >= 0 && at + 3999 < got && out[(at + 3999) * 2] == 0);
    AX_CHECK(p->pb.state == 0 && axCurrent(p) == 0x8000 + 3999);

    // DSP-ADPCM: three frames, the first with the PB's header, then their own
    static unsigned char frames[3 * 8];
    for (int f = 0; f < 3; f++) {
        frames[f * 8] = f == 0 ? 0x13 : (f == 1 ? 0x02 : 0x24);
        for (int k = 1; k < 8; k++) {
            frames[f * 8 + k] = (unsigned char) (f * 37 + k * 29 + 11);
        }
    }
    memcpy(re4_aram() + 0x20000, frames, sizeof(frames));
    AXPBADPCM ad;
    memset(&ad, 0, sizeof(ad));
    s16 coef[8][2] = { { 0, 0 }, { 0x0800, -0x0400 }, { 0x0600, 0 }, { 0, 0 },
                       { 0x0C00, -0x0600 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
    for (int c = 0; c < 8; c++) {
        ad.a[c][0] = (u16) coef[c][0];
        ad.a[c][1] = (u16) coef[c][1];
    }
    ad.pred_scale = 0x13;
    s16 ref[42];
    adpcmRef(frames, 42, coef, 0x13, ref);
    AXPBADDR addr;
    memset(&addr, 0, sizeof(addr));
    addr.format = 0;
    u32 start = 0x20000 * 2 + 2, end = 0x20000 * 2 + 2 * 16 + 15;
    addr.currentAddressHi = (u16) (start >> 16);
    addr.currentAddressLo = (u16) start;
    addr.endAddressHi = (u16) (end >> 16);
    addr.endAddressLo = (u16) end;
    AXSetVoiceAddr(p, &addr);
    AXSetVoiceAdpcm(p, &ad);
    AXSetVoiceState(p, 1);
    got = axRun(6, out, 160 * 6);
    at = -1;
    for (int i = 0; i < got && at < 0; i++) {
        if (out[i * 2] == ref[0] && out[(i + 1) * 2] == ref[1] && out[(i + 2) * 2] == ref[2]) {
            at = i;
        }
    }
    exact = at >= 0 && at + 42 <= got;
    for (int j = 0; exact && j < 42; j++) {
        exact = out[(at + j) * 2] == ref[j];
    }
    AX_CHECK(exact);
    AX_CHECK(p->pb.state == 0);

    // a loop keeps playing: the saw of 100 samples, looped over its second half
    putPcm16(0x30000, 200, saw);
    pcmVoice(p, 0x18000, 200, 0x8000, 0x8000, 100);
    AXSetVoiceState(p, 1);
    got = axRun(10, out, 160 * 10);
    exact = true;
    for (int i = 1000; exact && i < 1500; i++) {
        exact = out[i * 2] == out[(i + 100) * 2] && out[i * 2] == out[i * 2 + 1];
    }
    AX_CHECK(exact && p->pb.state == 1 && out[1200 * 2] != out[1250 * 2]);

    // rate conversion: linear at 0.5 moves 80 samples a block
    AXSetVoiceSrcType(p, AX_SRC_TYPE_LINEAR);
    AXSetVoiceSrcRatio(p, 0.5f);
    AXSetVoiceLoopAddr(p, 0x18000);
    AXSetVoiceEndAddr(p, 0x18000 + 0xFFF);
    putPcm16(0x30000, 0x1000, saw);
    AXSetVoiceCurrentAddr(p, 0x18000);
    axRun(2, out, 320);
    u32 a0 = axCurrent(p);
    axRun(10, out, 1600);
    u32 a1 = axCurrent(p);
    AX_CHECK(a1 - a0 >= 798 && a1 - a0 <= 802);
    AXSetVoiceState(p, 0);
    axRun(4, out, 640);  // the depop fade of the stopped voice

    // the volume envelope ramps the voice in
    putPcm16(0x40000, 4000, dc3000);
    pcmVoice(p, 0x20000, 4000, 0x8000, 0, -1);
    AXPBVE ve = { 0, 16 };
    AXSetVoiceVe(p, &ve);
    AXSetVoiceState(p, 1);
    got = axRun(10, out, 1600);
    at = firstNonZero(out, got);
    AX_CHECK(at >= 0 && out[(at + 1000) * 2] > out[(at + 100) * 2] && out[(at + 100) * 2] > 0 &&
             out[(at + 1000) * 2] < 3000);
    AXSetVoiceState(p, 0);
    axRun(3, out, 480);

    // aux A: the voice only on the aux bus; the effect (on the CPU) sees it and
    // returns a constant that ends up in the main mix
    gAuxSeen = 0;
    AXRegisterAuxACallback(auxEffect, 0);
    pcmVoice(p, 0x20000, 4000, 0, 0, -1);
    AXPBMIX mix;
    memset(&mix, 0, sizeof(mix));
    mix.vAuxAL = 0x8000;
    AXSetVoiceMix(p, &mix);
    AXSetVoiceState(p, 1);
    got = axRun(8, out, 160 * 8);
    AX_CHECK(gAuxSeen == 3000);
    AX_CHECK(out[(got - 1) * 2] == 1000 && out[(got - 1) * 2 + 1] == 0);
    // the reader of the app (rate matching between ring frames): 1000 stays 1000
    for (int i = 0; i < 12; i++) {
        re4_ai_block();
    }
    static short dev[2 * 512];
    AX_CHECK(re4_audio_read(dev, 512) == 512 && dev[0] == 1000 && dev[1023 - 1] == 1000 && dev[1023] == 0);
    AXRegisterAuxACallback(0, 0);
    AXSetVoiceState(p, 0);
    AXFreeVoice(p);
    axRun(4, out, 640);

    // Dolby Pro Logic II (AXSetMode 2, the game's AUDIO SETUP; MIX puts the front on the main
    // buses and the rear on aux B's left/right): the stereo has the rear mixed in as before, the
    // four channels keep it apart (dsp.cpp, ai.cpp re4_audio_read4)
    {
        const unsigned surroundBefore = re4_audio_surround_blocks();
        re4_audio_take(out, 160 * 40);  // what the reader test above left in the ring
        AXSetMode(2);
        p = AXAcquireVoice(5, 0, 0);
        AX_CHECK(p != 0);
        pcmVoice(p, 0x20000, 4000, 0, 0, -1);  // 3000 (dc3000 at 0x40000)
        AXPBMIX mix;
        memset(&mix, 0, sizeof(mix));
        mix.vL = 0x4000;      // front left 1500
        mix.vAuxBR = 0x8000;  // rear right 3000 (back a block later)
        AXSetVoiceMix(p, &mix);
        AXSetVoiceState(p, 1);
        short st[2], four[4];
        axRun4(10, st, four);
        AX_CHECK(four[0] == 1500 && four[1] == 0 && four[2] == 0 && four[3] == 3000);
        AX_CHECK(st[0] == 1500 && st[1] == 3000);
        AX_CHECK(re4_audio_surround_blocks() > surroundBefore && re4_audio_surround_now() == 1);
        // both at once (re4_audio_read6, for the core in a process of its own): per frame the
        // stereo, then the four channels, from the same place in the ring
        for (int i = 0; i < 12; i++) {
            re4_ai_block();
        }
        static short six[6 * 256];
        AX_CHECK(re4_audio_read6(six, 256) == 256);
        AX_CHECK(six[0] == 1500 && six[1] == 3000 && six[2] == 1500 && six[3] == 0 && six[4] == 0 && six[5] == 3000);
        AX_CHECK(six[255 * 6] == 1500 && six[255 * 6 + 1] == 3000 && six[255 * 6 + 5] == 3000);
        // aux A in DPL2: the effect gets Ls from aux A S and Rs from aux B S (MIX's vBS), its
        // returns go to the front and the rear; the stereo takes the effect's rear at half
        gDpl2SeenLs = gDpl2SeenRs = 0;
        AXRegisterAuxACallback(auxEffectDpl2, 0);
        memset(&mix, 0, sizeof(mix));
        mix.vAuxAS = 0x8000;  // aux A Ls 3000
        mix.vAuxBS = 0x4000;  // aux A Rs 1500
        AXSetVoiceMix(p, &mix);
        axRun4(10, st, four);
        AX_CHECK(gDpl2SeenLs == 3000 && gDpl2SeenRs == 1500);
        AX_CHECK(four[0] == 100 && four[1] == 200 && four[2] == 300 && four[3] == 400);
        AX_CHECK(st[0] == 100 + 150 && st[1] == 200 + 200);
        AXRegisterAuxACallback(0, 0);
        AXSetVoiceState(p, 0);
        AXSetMode(0);
        // stereo again: the four channels are the stereo and a silent rear; 0.5 s later the
        // surround is over
        pcmVoice(p, 0x20000, 4000, 0x8000, 0x4000, -1);
        AXSetVoiceState(p, 1);
        axRun4(10, st, four);
        AX_CHECK(st[0] == 3000 && st[1] == 1500 && four[0] == 3000 && four[1] == 1500 && four[2] == 0 &&
                 four[3] == 0);
        axRun4(100, st, four);
        AX_CHECK(re4_audio_surround_now() == 0);
        AXSetVoiceState(p, 0);
        AXFreeVoice(p);
        axRun(4, out, 640);
    }

    // the compressor (dsp.cpp 0x12, on as AXInit leaves it and snd_main.cpp sets it): two voices
    // of 20000 in the left reach 40000, the block turns down to the table's lowest gain (0x50C3:
    // 25238 instead of a clipped 32767); with one voice left it comes back to 20000 in 10 blocks
    {
        unsigned compBefore, clipBefore, compAfter, clipAfter;
        re4_dsp_loudness(&compBefore, &clipBefore);
        putPcm16(0x50000, 4000, dc20000);
        AXVPB* loud[2];
        for (int i = 0; i < 2; i++) {
            loud[i] = AXAcquireVoice(5, 0, 0);
            AX_CHECK(loud[i] != 0);
            pcmVoice(loud[i], 0x28000, 4000, 0x8000, 0, 0);
            AXSetVoiceState(loud[i], 1);
        }
        got = axRun(8, out, 160 * 8);
        re4_dsp_loudness(&compAfter, &clipAfter);
        AX_CHECK(got > 0 && abs(out[(got - 1) * 2] - 25238) <= 2 && out[(got - 1) * 2 + 1] == 0);
        AX_CHECK(compAfter >= compBefore + 6 && clipAfter - clipBefore < 160);
        AXSetVoiceState(loud[1], 0);
        got = axRun(16, out, 160 * 16);
        AX_CHECK(got > 0 && out[(got - 1) * 2] == 20000);
        // the stopped voice fades out (AX's ramp), then the release: up a step a block, never down
        int low = 0;
        for (int i = 1; i < got; i++) {
            low = out[i * 2] < out[low * 2] ? i : low;
        }
        bool rising = out[low * 2] > 12600;  // never below the lowest gain
        for (int i = low; i + 1 < got; i++) {
            rising = rising && out[(i + 1) * 2] >= out[i * 2];
        }
        AX_CHECK(rising);
        AXSetVoiceState(loud[0], 0);
        AXFreeVoice(loud[0]);
        AXFreeVoice(loud[1]);
        axRun(4, out, 640);
    }

    // AX's DSP budget (dsp.cpp DSPAddTask): 40 voices on every bus with ramps
    // stay; with the GameCube's 405000 cycles AX stops some of them
    AX_CHECK(AXGetMaxDspCycles() == 4 * 405000);
    {
        static AXVPB* many[40];
        AXPBMIX all;
        memset(&all, 0xFF, sizeof(all));
        for (int round = 0; round < 2; round++) {
            gAxDrops = 0;
            for (int i = 0; i < 40; i++) {
                many[i] = AXAcquireVoice(20, axDrop, 0);
                pcmVoice(many[i], 0x20000, 4000, 0x8000, 0x8000, 0);
                AXSetVoiceSrcType(many[i], AX_SRC_TYPE_4TAP_16K);
                AXSetVoiceSrcRatio(many[i], 1.5f);
                AXSetVoiceMix(many[i], &all);
                AXSetVoiceState(many[i], 1);
            }
            axRun(3, out, 480);
            AX_CHECK(round == 0 ? gAxDrops == 0 : gAxDrops > 0);
            for (int i = 0; i < 40; i++) {
                if (many[i]->priority != 0 && many[i]->callback == (void (*)(void*)) axDrop) {
                    AXSetVoiceState(many[i], 0);
                    AXFreeVoice(many[i]);
                }
            }
            axRun(4, out, 640);
            AXSetMaxDspCycles(405000);  // round 2: the GameCube's budget
        }
        AXSetMaxDspCycles(4 * 405000);
        gAxDrops = 0;
    }

    // SYN, the synthesizer of the game's sequenced sounds (snd_seq*.cpp): the
    // voice set-up (synsample.c) and the pitch update (synpitch.c) write the
    // parameter block as the GameCube did, 32-bit values high half first
    // (patch 0056); before, the halves were swapped on this host (sample
    // addresses far outside ARAM, the ratio's fraction as its integer part:
    // AX read its cycle table out of bounds and stopped voices)
    {
        static SYNSYNTH synth;
        memset(&synth, 0, sizeof(synth));
        synth.aramBaseByte = 0x30000;
        synth.aramBaseWord = 0x30000 >> 1;
        synth.aramBaseNibble = 0x30000 << 1;
        synth.zeroBaseByte = 0x100;
        synth.zeroBaseWord = 0x100 >> 1;
        synth.zeroBaseNibble = 0x100 << 1;
        WTREGION region;
        memset(&region, 0, sizeof(region));
        region.loopStart = 30;   // samples: 2 frames + 2 -> nibble 0x24
        region.loopLength = 100;
        WTSAMPLE sample;
        memset(&sample, 0, sizeof(sample));
        sample.format = SYN_SAMPLE_FORMAT_ADPCM;
        sample.sampleRate = 32000;
        sample.offset = 0x40;  // nibbles
        sample.length = 500;
        WTADPCM adpcm;
        memset(&adpcm, 0, sizeof(adpcm));
        adpcm.a[0][0] = 0x0800;
        adpcm.a[7][1] = 0x1234;
        adpcm.pred_scale = 0x12;
        adpcm.loop_pred_scale = 0x34;
        static SYNVOICE voice;
        memset(&voice, 0, sizeof(voice));
        voice.axvpb = AXAcquireVoice(15, axDrop, 0);
        voice.synth = &synth;
        voice.region = &region;
        voice.sample = &sample;
        voice.adpcm = &adpcm;
        AX_CHECK(voice.axvpb != 0);
        if (voice.axvpb) {
            AXPB& pb = voice.axvpb->pb;
            __SYNSetupSample(&voice);
            u32 start = (0x30000 << 1) + 0x40;
            AX_CHECK(pb.addr.loopFlag == 1 && pb.addr.format == 0);
            AX_CHECK(axCurrent(voice.axvpb) == start + 2);
            AX_CHECK((((u32) pb.addr.loopAddressHi << 16) | pb.addr.loopAddressLo) == start + 2 * 16 + 2 + 2);
            u32 end = start + (129 / 14) * 16 + 2 + 129 % 14;  // last loop sample: 30 + 100 - 1
            AX_CHECK((((u32) pb.addr.endAddressHi << 16) | pb.addr.endAddressLo) == end);
            AX_CHECK(pb.adpcm.a[0][0] == 0x0800 && pb.adpcm.a[7][1] == 0x1234 && pb.adpcm.pred_scale == 0x12);
            AX_CHECK(pb.adpcmLoop.loop_pred_scale == 0x34);
            sample.format = SYN_SAMPLE_FORMAT_PCM16;
            __SYNSetupSample(&voice);
            AX_CHECK(pb.addr.loopFlag == 1 && pb.addr.format == 10);
            AX_CHECK(axCurrent(voice.axvpb) == (0x30000 >> 1) + 0x40);
            AX_CHECK(pb.adpcm.gain == 0x0800);
            sample.format = SYN_SAMPLE_FORMAT_PCM8;
            region.loopStart = region.loopLength = 0;
            __SYNSetupSample(&voice);
            AX_CHECK(pb.addr.loopFlag == 0 && pb.addr.format == 0x19);
            AX_CHECK((((u32) pb.addr.loopAddressHi << 16) | pb.addr.loopAddressLo) == 0x100);
            AX_CHECK(pb.adpcm.gain == 0x0100);
            // pitch: 1.5 = 0x18000 as ratio 1 + 0x8000/65536
            voice.srcRatio = 1.5f;
            __SYNSetupSrc(&voice);
            AX_CHECK(pb.src.ratioHi == 1 && pb.src.ratioLo == 0x8000);
            voice.srcRatio = 0.75f;
            __SYNUpdateSrc(&voice);
            AX_CHECK(pb.src.ratioHi == 0 && pb.src.ratioLo == 0xC000);
            AXFreeVoice(voice.axvpb);
        }
    }

    // the AI clock: 200 blocks per second of video fields
    unsigned before = re4_ai_blocks();
    for (int i = 0; i < 60; i++) {
        re4_ai_field();
    }
    AX_CHECK(re4_ai_blocks() - before == 200 || re4_ai_blocks() - before == 201);
    re4_audio_take(out, 160 * 40);
    AXRegisterCallback(old);
    return failed;
}

// The AXFX effects of src/lib (the assembly of the reverbs and the chorus in C,
// patch 0047) on the CPU, as the aux callbacks run them: an impulse through
// the high reverb (crosstalk, the dry share at once, no wet sound before the
// shortest comb, a decaying tail), the standard reverb with its pre-delay, the
// chorus on a sine, the delay's echo. Returns the failed checks.
#include "dolphin/axfx.h"

#include <math.h>
#include <stdlib.h>

extern "C" void AXFXSetHooks(void* (*alloc)(u32), void (*free)(void*));

static void* fxAlloc(u32 size)
{
    return malloc(size);
}

static void fxFree(void* p)
{
    free(p);
}

static s32 gFx[3][160];

static void fxBlock(void (*cb)(AXFX_BUFFERUPDATE*, void*), void* fx, int block, s32 impulse)
{
    memset(gFx, 0, sizeof(gFx));
    if (block == 0) {
        gFx[0][0] = impulse;
    }
    AXFX_BUFFERUPDATE b = { gFx[0], gFx[1], gFx[2] };
    cb(&b, fx);
}

extern "C" int re4probe_axfx_test(void)
{
    int failed = 0;
    AXFXSetHooks(fxAlloc, fxFree);
    static s32 left[160 * 100], right[160 * 100], sur[160 * 100];

    // high reverb
    static AXFX_REVERBHI hi;
    memset(&hi, 0, sizeof(hi));
    hi.coloration = 0.5f;
    hi.mix = 0.5f;
    hi.time = 2.0f;
    hi.damping = 0.5f;
    hi.preDelay = 0.0f;
    hi.crosstalk = 0.3f;
    AX_CHECK(AXFXReverbHiInit(&hi) == 1);
    for (int b = 0; b < 100; b++) {
        fxBlock((void (*)(AXFX_BUFFERUPDATE*, void*)) AXFXReverbHiCallback, &hi, b, 10000);
        memcpy(left + b * 160, gFx[0], sizeof(gFx[0]));
        memcpy(right + b * 160, gFx[1], sizeof(gFx[1]));
        memcpy(sur + b * 160, gFx[2], sizeof(gFx[2]));
    }
    // crosstalk: 8500 left, 1500 right; the dry share 0.6 * (1 - mix)
    AX_CHECK(left[0] == 2550 && right[0] == 450);
    bool quiet = true;
    for (int i = 1; i < 1789; i++) {
        quiet = quiet && left[i] == 0 && right[i] == 0;
    }
    AX_CHECK(quiet);
    double early = 0, late = 0;
    bool silentSur = true;
    for (int i = 0; i < 16000; i++) {
        (i < 8000 ? early : late) += (double) left[i] * left[i];
        silentSur = silentSur && sur[i] == 0;
    }
    AX_CHECK(early > 1000.0 && late > 0 && late < early && silentSur);
    AXFXReverbHiShutdown(&hi);

    // standard reverb with 10 ms of pre-delay
    static AXFX_REVERBSTD std;
    memset(&std, 0, sizeof(std));
    std.coloration = 0.5f;
    std.mix = 1.0f;
    std.time = 1.0f;
    std.damping = 0.5f;
    std.preDelay = 0.01f;
    AX_CHECK(AXFXReverbStdInit(&std) == 1);
    for (int b = 0; b < 40; b++) {
        fxBlock((void (*)(AXFX_BUFFERUPDATE*, void*)) AXFXReverbStdCallback, &std, b, 10000);
        memcpy(left + b * 160, gFx[0], sizeof(gFx[0]));
    }
    int first = -1;
    for (int i = 0; i < 6400 && first < 0; i++) {
        if (left[i]) {
            first = i;
        }
    }
    // no dry share at mix 1; nothing before pre-delay (319 samples) and the shortest comb
    AX_CHECK(first >= 319 + 1789 && first < 6400);
    AXFXReverbStdShutdown(&std);

    // chorus on a 500 Hz sine: bounded, not silent
    static AXFX_CHORUS ch;
    memset(&ch, 0, sizeof(ch));
    ch.baseDelay = 15;
    ch.variation = 5;
    ch.period = 500;
    AX_CHECK(AXFXChorusInit(&ch) == 1);
    s32 peak = 0;
    for (int b = 0; b < 40; b++) {
        for (int i = 0; i < 160; i++) {
            float t = (float) (b * 160 + i) / 32000.0f;
            gFx[0][i] = gFx[1][i] = (s32) (8000.0f * sinf(6.2831853f * 500.0f * t));
            gFx[2][i] = 0;
        }
        AXFX_BUFFERUPDATE u = { gFx[0], gFx[1], gFx[2] };
        AXFXChorusCallback(&u, &ch);
        for (int i = 0; b >= 10 && i < 160; i++) {
            s32 v = gFx[0][i] < 0 ? -gFx[0][i] : gFx[0][i];
            peak = v > peak ? v : peak;
        }
    }
    AX_CHECK(peak > 2000 && peak < 32000);
    AXFXChorusShutdown(&ch);

    // delay: 50 ms in whole blocks ((45 * 32 + 159) / 160 = 9), the echo comes
    // back after 9 * 160 samples
    static AXFX_DELAY dl;
    memset(&dl, 0, sizeof(dl));
    for (int c = 0; c < 3; c++) {
        dl.delay[c] = 50;
        dl.feedback[c] = 0;
        dl.output[c] = 100;
    }
    AX_CHECK(AXFXDelayInit(&dl) == 1);
    for (int b = 0; b < 20; b++) {
        fxBlock((void (*)(AXFX_BUFFERUPDATE*, void*)) AXFXDelayCallback, &dl, b, 10000);
        memcpy(left + b * 160, gFx[0], sizeof(gFx[0]));
    }
    first = -1;
    for (int i = 0; i < 3200 && first < 0; i++) {
        if (left[i]) {
            first = i;
        }
    }
    AX_CHECK(first == 1440 && left[1440] == 10000);
    AXFXDelayShutdown(&dl);
    return failed;
}

// Movies (M9): the CRI middleware of src/lib analyses the head of a Sofdec
// file the way cSofdec::initApp asks it to (mwPlyGetHdrInf): the pack size
// from three pack headers and the picture size from the MPEG-1 sequence header
// of the video stream. The size reaches the game's view of the result
// (include/mwply.h, patch 0052). The whole player with a synthetic movie:
// port/tools/test_whole_movie.py (re4_host --movie-test). Returns a bit mask
// of failed checks.
#include "mwply.h"

extern "C" int re4probe_movie_test(void)
{
    static bool initialised;
    if (!initialised) {
        initialised = true;
        MWS_PLY_INIT_SFD prm;  // SofdecInit
        memset(&prm, 0, sizeof(prm));
        prm.disp_cycle = 59.94f;
        prm.x04 = 1;
        prm.x08 = 1;
        prm.x0C = 1;
        mwPlyInitSfdFx(&prm);
    }
    static u8 head[3 * 0x800];
    memset(head, 0, sizeof(head));
    static const u8 pack[12] = {0, 0, 1, 0xBA, 0x21, 0, 1, 0, 1, 0x81, 0x86, 0xA1};
    for (int i = 0; i < 3; i++) {
        u8* p = head + i * 0x800;
        memcpy(p, pack, sizeof(pack));
        // one PES packet over the rest of the pack: stream 0xE0 (video), then padding 0xBE
        p[12] = 0;
        p[13] = 0;
        p[14] = 1;
        p[15] = i == 0 ? 0xE0 : 0xBE;
        p[16] = (0x800 - 18) >> 8;
        p[17] = (0x800 - 18) & 0xFF;
        memset(p + 18, 0xFF, 0x800 - 18);
        if (i == 0) {
            static const u8 seq[] = {0x0F, 0, 0, 1, 0xB3, 0x14, 0x00, 0xF0, 0x14, 0xFF, 0xFF, 0xE0, 0x18};
            memcpy(p + 18, seq, sizeof(seq));  // no PTS (0x0F), 320 x 240, 29.97 Hz
        }
    }
    int failed = 0;
    MWS_SFD_HDRINF info;
    memset(&info, 0xCD, sizeof(info));
    mwPlyGetHdrInf(head, (int) sizeof(head), &info);
    if (info.width != 320 || info.height != 240) {
        re4_log("  movie header: %d x %d", info.width, info.height);
        failed |= 0x1;
    }
    // with the 0x0C bytes the game keeps after it (sofdec.h AP_OBJ pad_48): the CRI side reads the
    // creation parameters as 0x30 bytes (MWSFD_CRPRM, buffmt at 0x24); without them the sanitizer
    // build stopped at the read past the test's 0x24 bytes (22.10.2026)
    struct {
        MWS_PLY_CPRM_SFD prm;
        u8 more[0xC];
    } area;
    memset(&area, 0, sizeof(area));
    MWS_PLY_CPRM_SFD& cprm = area.prm;
    cprm.ftype = 1;
    cprm.max_bps = 8000000;
    cprm.nfrm_pool_wk = 4;
    cprm.max_width = 320;
    cprm.max_height = 240;
    cprm.max_stm = 2;
    if (mwPlyCalcWorkCprmSfd(&cprm) < 320 * 240 * 3) {
        failed |= 0x2;  // the work holds the frame pool
    }
    return failed;
}
