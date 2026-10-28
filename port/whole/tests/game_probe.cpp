// Compiled with the game's flags and headers (re4_game_flags) for the runtime
// test "math": checks of what only exists inside game units -- the inline
// fabsf of math_sub.h, the EmRoutineSetW macro of em.h (patch 0023), the
// big-endian views of re4_whole_prelude.h and the length of __FILE__; for the
// test "partner" SetSubDamage on Luis (patch 0087); for the test "route"
// getNearPoint from a position far outside the room (patch 0088); for the test
// "enemylog" the enemy and menu lines of the error log (state_trace.cpp).
#include "types.h"
#include "math_sub.h"
#include "em.h"
#include "dolphin/gx/GXStruct.h"
#include "dolphin/gx/GXFifo.h"
#include "global.h"
#include "main_mem.h"
#include "esp.h"
#include "path.h"
#include "at_sub.h"
#include "at_mod.h"
#include "cam_extra.h"
#include "obj.h"
#include "light.h"
#include "pl14.h"
#include "pl_sub.h"
#include "pl_npc.h"
#include "route_ck.h"
#include "em10.h"
#include "emwep.h"
#include "joy.h"
#include "player.h"
#include "sce_at.h"
#include "area.h"
#include "puzzle.h"
#include "item.h"
#include "main.h"
#include "camera.h"
#include "view.h"
#include "id_sys.h"
#include "sscrn.h"
#include "ss_main.h"
#include "ss_pzzl.h"
#include "db_log.h"
#include "pad.h"
#include "pl_wep.h"
#include "snd_drv.h"
#include "act_btn.h"
#include "cockpit.h"

#include <new>
#include <stdlib.h>
#include <string.h>

// The SDK casts GXFifoObj (a byte array) to a struct of words (patch 0038).
static_assert(__alignof__(GXFifoObj) >= 4, "GXFifoObj must be word aligned for ARM");
// A subclass's work starts at the end of cEsp, 0xF8 as on the GameCube, with
// the vptr first here and last there (patch 0040 finds it by this size).
static_assert(sizeof(void*) != 4 || sizeof(cEsp) == 0xF8, "cEsp must keep the GameCube size (ILP32)");

struct ProbeRoutine {  // the four routine bytes of cModel (r_no_0..3)
    u8 r_no_0;
    u8 r_no_1;
    u8 r_no_2;
    u8 r_no_3;
};

struct ProbeAxAddr {  // the AX address pair the sound driver reads as one word
    u16 currentAddressHi;
    u16 currentAddressLo;
};

extern "C" float re4probe_fabsf(float x)
{
    return fabsf(x);
}

extern "C" void re4probe_routine(unsigned char out[4])
{
    ProbeRoutine r = {9, 9, 9, 9};
    ProbeRoutine* em = &r;
    EmRoutineSetW(em, 1, 0x2E, 3, 4);
    out[0] = r.r_no_0;
    out[1] = r.r_no_1;
    out[2] = r.r_no_2;
    out[3] = r.r_no_3;
}

// Returns a bit mask of failed checks (0 = all passed).
// The attribute word of a collision polygon as the hit checks see it (patch
// 0041): the stored word, in either byte order of the host.
extern "C" unsigned re4probe_poly_attr(unsigned status)
{
    SAT_POLY poly = {};
    poly.m_Status = status;
    return Get_poly_attr(&poly);
}

// Float to a small integer type in game code (-fno-strict-float-cast-overflow):
// clamped to the type's range as ProDG's -mfast-cast psq_st conversion does
// (the title logo's alpha went 300 -> 44 when it wrapped).
extern "C" unsigned re4probe_cast_u8(float f)
{
    return (u8) f;
}

extern "C" int re4probe_cast_s16(float f)
{
    return (s16) f;
}

// A finished special camera deleted twice, as CameraControl does (m_pProc stays
// set after `delete m_pProc`): the subclass destructor poisons the object with
// 9s, cCamera's destructor must leave a usable vtable pointer (patch 0046).
// 1 when the pointer is the poison (the second delete would crash).
extern "C" int re4probe_camera_double_delete(void)
{
    static u64 store[0x200 / 8];
    cCamera* c = new (store) CameraPushObject();
    delete c;
    u32 first;
    memcpy(&first, store, 4);  // the vtable pointer of the port's layout
    if (first == 0x09090909u) {
        return 1;
    }
    delete c;
    return 0;
}

// The debug page (state_trace.cpp: RE4_DEBUG_HUD=0, patch 0043); set < 0 only reads.
extern "C" int re4probe_debug_mode(int set)
{
    if (set >= 0) {
        pG->debug_mode = (s8) set;
    }
    return pG->debug_mode;
}

// The state trace after a soft reset (state_trace.cpp inArray): ObjMgr's alive list as the
// title found it there, two works of the manager's array and then a next pointer into memory the
// title used again (PC check 18.10.2026: the trace followed it and crashed). set 1 puts the list
// in place (step 3, so the trace counts the works), 0 puts ObjMgr and the step back.
extern "C" void re4probe_stale_list(int set)
{
    static u8 works[2 * 0x400] __attribute__((aligned(16)));
    static cObj* array;
    static u32 n, size;
    static cObj* alive;
    static u8 step;
    static_assert(sizeof(cUnit) <= 0x400, "the fake works hold a cUnit each");
    if (set) {
        array = ObjMgr.pArray;
        n = ObjMgr.nArray;
        size = ObjMgr.size;
        alive = ObjMgr.pAlive;
        step = pG->Rno0;
        memset(works, 0, sizeof(works));
        ((cUnit*) works)->setNext((cUnit*) (works + 0x400));
        ((cUnit*) (works + 0x400))->setNext((cUnit*) (unsigned long) 0xC7D14C2Bul);
        ObjMgr.pArray = (cObj*) works;
        ObjMgr.nArray = 2;
        ObjMgr.size = 0x400;
        ObjMgr.pAlive = (cObj*) works;
        pG->Rno0 = 3;
    } else {
        ObjMgr.pArray = array;
        ObjMgr.nArray = n;
        ObjMgr.size = size;
        ObjMgr.pAlive = alive;
        pG->Rno0 = step;
    }
}

// Light type 5 (light05.cpp) over the work bytes of a room light record, which keep the GameCube
// form: the path number Id at 0xC and Flag at 0xD, the path follower cLightPath before them (fixed
// form since 19.10.2026; on LP64 its two pointers had moved Id to 0x18, compare run r107). A light
// path file in MEM1 with two paths, 0: 200 and 1: 50, 100; the light asks for path 1. Returns
// DispCol.r after two frames with Col.r 200: 100 (0.5 * 200) when path 1 plays, 200 for path 0.
extern "C" int re4probe_light05(void)
{
    static_assert(__builtin_offsetof(LIT05_MOVE_FREE, Id) == 0xC, "LIT05 path number at its GameCube offset");
    static u8 buf[sizeof(cLight)] __attribute__((aligned(16)));
    u8* file = (u8*) (unsigned long) 0x82E00000ul;
    memset(file, 0, 0x20);
    file[0] = 2;
    *(u32*) (file + 4) = 0x10;
    *(u32*) (file + 8) = 0x14;
    file[0x10] = 200;
    file[0x11] = 0xFF;
    file[0x14] = 50;
    file[0x15] = 100;
    file[0x16] = 0xFF;
    if (!LightMgr.initPath((LightPathHeader*) file)) {
        return -1;
    }
    cLight* l = new (buf) cLight;
    l->Rno0 = 0;
    l->Col.r = l->Col.g = l->Col.b = 200;
    l->Col.a = 255;
    memset(l->work, 0, sizeof(l->work));
    l->work[0xC] = 1;
    l->work[0xD] = 0;
    Light05_Move(l);
    Light05_Move(l);
    return l->DispCol.r;
}

extern "C" int re4probe_byte_views(void)
{
    int failed = 0;
    GXColor c = {0, 0, 0, 0};
    // fade.h: `*(u32*) &col.start = 0xFF` is black with full alpha on the GameCube
    RE4_BE(u32, 1, &c) = 0xFF;
    if (c.r != 0 || c.g != 0 || c.b != 0 || c.a != 0xFF) {
        failed |= 1;
    }
    RE4_BE(u32, 1, &c) = 0x11223344;
    if (c.r != 0x11 || c.g != 0x22 || c.b != 0x33 || c.a != 0x44 || RE4_BE(u32, 1, &c) != 0x11223344) {
        failed |= 2;
    }
    // light.cpp: `(*(u32*) &l->DispCol & 0xFFFFFF00) == 0` tests r, g, b
    c.r = c.g = c.b = 0;
    if ((RE4_BE(u32, 1, &c) & 0xFFFFFF00) != 0) {
        failed |= 4;
    }
    // event.cpp: the EVD tag "event" read as a word
    char tag[8] = {'e', 'v', 'e', 'n', 't', 0, 0, 0};
    if (RE4_BE(u32, 1, tag) != 0x6576656E) {
        failed |= 8;
    }
    // snd_iss2.cpp: the AX Hi/Lo halfwords as one address
    ProbeAxAddr a = {0x0012, 0x3456};
    if (RE4_BE(u32, 2, &a.currentAddressHi) != 0x00123456) {
        failed |= 16;
    }
    // copy through two views keeps the bytes; compound assignment
    GXColor d = {1, 2, 3, 4};
    RE4_BE(u32, 1, &c) = RE4_BE(u32, 1, &d);
    RE4_BE(u32, 1, &c) |= 0xFF;
    if (c.r != 1 || c.g != 2 || c.b != 3 || c.a != 0xFF) {
        failed |= 32;
    }
    // motion.cpp: an s16 in a big-endian byte stream
    u8 stream[2] = {0xFF, 0x38};
    if ((s16) RE4_BE(s16, 1, stream) != -200) {
        failed |= 64;
    }
    // parts counted from the most significant end of a host value
    s16 parts = 0x0107;
    int fno = 0x00020005;
    f32 one = 1.0f;
    if (RE4_BE_AT(u8, &parts, 1) != 7 || RE4_BE_AT(u8, &parts, 0) != 1 || RE4_BE_AT(u16, &fno, 1) != 5 ||
        RE4_BE_AT(s8, &fno, 3) != 5 || RE4_BE_AT(u8, &one, 0) != 0x3F || RE4_BE_AT(u8, &one, 1) != 0x80) {
        failed |= 128;
    }
    return failed;
}

extern "C" const char* re4probe_file(void)
{
    return __FILE__;
}

// VALID_PTR (patch 0037): a stack variable of an OS thread (onThread: called
// on one), the game's static data, MEM1 and what stays invalid. Returns a bit
// mask of failed checks (0 = all passed).
extern "C" int re4probe_valid_ptr(int onThread)
{
    int failed = 0;
    volatile float local[3] = {1.0f, 2.0f, 3.0f};
    if (onThread && !VALID_PTR(&local[1])) {
        failed |= 0x1;
    }
    if (!VALID_PTR(&Global)) {
        failed |= 0x2;
    }
    if (!VALID_PTR((void*) 0x80001000)) {
        failed |= 0x4;
    }
    // host heap memory belongs to no game library and no stack
    void* heap = malloc(64);
    if (VALID_PTR((void*) 0) || VALID_PTR((void*) 0x1000) || VALID_PTR(heap)) {
        failed |= 0x8;
    }
    free(heap);
    return failed;
}

// The partner damage routine on Luis (pl_sub SetSubDamage, patch 0087): Luis (cSubLuis) is a cEm,
// not a cSubChar; the original stores the routine 4 function and his F_KARAMI status through
// cSubChar members that only on the GameCube lie on his m_pFunc and status. On zeroed Luis bytes
// (no constructor: SetSubDamage touches only plain members) as pSUB, id 4. Returns 0 when his
// m_pFunc, status, routine and catcher are set and his analysis work is untouched, else a bit
// for each fault.
static void probeLuisFunc() {}

extern "C" int re4probe_luis_damage(void)
{
    static u8 buf[sizeof(cSubLuis)] __attribute__((aligned(16)));
    static u8 catcher[sizeof(cEm)] __attribute__((aligned(16)));
    u8 analysis[sizeof(cAnalysis)];
    cSubLuis* luis = (cSubLuis*) buf;
    cEm* keep = pSUB;
    int bad = 0;

    memset(buf, 0, sizeof(buf));
    luis->id = 4;
    memset(&luis->analysis, 0x5A, sizeof(cAnalysis));
    memcpy(analysis, &luis->analysis, sizeof(cAnalysis));
    pSUB = (cEm*) luis;
    SetSubDamage((cEm*) catcher, probeLuisFunc);
    pSUB = keep;
    if (luis->m_pFunc != probeLuisFunc) bad |= 1;
    if (!luis->status.check(cSubLuis::F_KARAMI)) bad |= 2;
    if (luis->r_no_0 != 4) bad |= 4;
    if (luis->pEmCatch != (cEm*) catcher) bad |= 8;
    if (memcmp(analysis, &luis->analysis, sizeof(cAnalysis)) != 0) bad |= 0x10;
    return bad;
}

// Stack garbage where the next call keeps its locals (route test): a float-like pattern, as the
// tester's report had in getNearPoint's idx[].
__attribute__((noinline)) static void probeDirtyStack(void)
{
    volatile int fill[256];
    for (int i = 0; i < 256; i++) {
        fill[i] = 0x46464646;
    }
}

// The jet ski's rail in r333 (a PTH entry of the room archive; formats_eff.cpp convertPathEntry,
// SCV40 report 23.10.2026): a big-endian archive with one PTH entry of three vertices goes
// through the loader's converter, then the game's own path functions (path.cpp) read it. Returns
// a bit mask of faults: 1 the vertex count, 2 the length, 4 the point at 150, 8 a point beyond
// the end not (0 0 0), 16 the second vertex's normal.
extern "C" void re4_formats_part_loaded(const char* rawName, int nest, int index, const void* header, unsigned addr);

extern "C" int re4probe_path_entry(void)
{
    u8* arc = (u8*) (unsigned long) 0x82E60000ul;
    memset(arc, 0, 0x200);
    auto put32 = [](u8* p, u32 v) {
        p[0] = (u8) (v >> 24);
        p[1] = (u8) (v >> 16);
        p[2] = (u8) (v >> 8);
        p[3] = (u8) v;
    };
    auto putf = [&](u8* p, f32 f) {
        u32 v;
        memcpy(&v, &f, 4);
        put32(p, v);
    };
    put32(arc + 0x00, 1);     // entries
    put32(arc + 0x10, 0x20);  // the entry
    memcpy(arc + 0x14, "PTH\0", 4);
    u8* path = arc + 0x20;
    path[1] = 3;  // u16 num, big-endian
    for (int i = 0; i < 3; i++) {
        u8* v = path + 4 + i * 0x28;
        putf(v + 0x00, 100.0f * i);  // pos
        putf(v + 0x04, 5.0f);
        putf(v + 0x08, -50.0f * i);
        putf(v + 0x10, 1.0f);        // normal (0 1 0)
        putf(v + 0x18, 100.0f * i);  // Length
    }
    const u32 header[8] = {0, 0x20 + 4 + 3 * 0x28, 0, 0x400, 0, 0, 0, 0};  // DvdHeader
    re4_formats_part_loaded("em/em98.drs", 0, 0, header, 0x82E60000u);
    int bad = 0;
    const Path* p = (const Path*) path;
    if (p->num != 3) bad |= 1;
    if (PathGetLength(path) != 200.0f) bad |= 2;
    u16 seg = 0;
    Vec pos = {1, 1, 1};
    if (!PathGetPos(path, 150.0f, &seg, &pos) || pos.x != 150.0f || pos.y != 5.0f || pos.z != -75.0f) bad |= 4;
    seg = 0;
    if (PathGetPos(path, 250.0f, &seg, &pos) || pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f) bad |= 8;
    if (p->vtx[1].normal.y != 1.0f || p->vtx[1].normal.x != 0.0f) bad |= 16;
    return bad;
}

// getNearPoint (patch 0088) over route data with three points in MEM1 (host order, as the port
// loads it): a position near point 1 finds it; a position 2e8 away is out of reach of all three,
// which leaves the sorted list empty (the original then reads pt[garbage]). Returns a bit mask of
// faults: 1 near (mode 1) not point 1, 2 far (mode 1) not -1, 4 far (mode 0, the clear-line
// search) not -1.
extern "C" int re4probe_route_far(void)
{
    RTP* rtp = (RTP*) (unsigned long) 0x82E10000ul;
    RTP_POINT* pt = (RTP_POINT*) ((u8*) rtp + 0x20);
    auto keep = pG->Rtp;
    Vec near = {900.0f, 0.0f, 0.0f};
    Vec far = {0.0f, 2.0e8f, 0.0f};
    int bad = 0;

    memset(rtp, 0, 0x80);
    rtp->ID = 0x32525450;
    rtp->nPoint = 3;
    rtp->pPoint = 0x20;
    pt[1].pos.x = 1000.0f;
    pt[2].pos.x = 5000.0f;
    pG->Rtp = rtp;
    if (getNearPoint(&near, 1, 0) != 1) bad |= 1;
    probeDirtyStack();
    if (getNearPoint(&far, 1, 0) != -1) bad |= 2;
    probeDirtyStack();
    if (getNearPoint(&far, 0, 0) != -1) bad |= 4;
    pG->Rtp = keep;
    return bad;
}

extern "C" void re4_state_trace_enemy_frame(unsigned n);

// The error log's enemy and menu lines (state_trace.cpp enemyLog / menuLog) over two fake works
// of EmMgr in MEM1: slot 1 becomes a Ganado (0x1a type 3) of room r209, is hit on the head (part
// 5, weapon 0x10), changes its routine, loses its head, is hidden, jumps, holds back and follows
// again, and goes; then the C
// stick moves the cursor of a sub screen; slot 0 is a weapon work (0x42) the Ganado throws as
// dynamite and that explodes. The test reads the lines from the log's tail.
extern "C" void re4probe_enemy_log(void)
{
    u8* works = (u8*) (unsigned long) 0x82E20000ul;
    const u32 stride = (u32) (sizeof(cEm10) > sizeof(cEmWep) ? sizeof(cEm10) : sizeof(cEmWep));
    cEm* keepArray = EmMgr.pArray;
    u32 keepCount = EmMgr.nArray, keepSize = EmMgr.size;
    u8 keepStep = pG->Rno0;
    u16 keepRoom = pG->room_id;
    JOY keepJoy = Joy[0];
    int keepSub = StaFlagChk(pG, STA_SUB_SCRN) != 0;

    memset(works, 0, 2 * stride);
    EmMgr.pArray = (cEm*) works;
    EmMgr.nArray = 2;
    EmMgr.size = stride;
    pG->Rno0 = 3;
    pG->room_id = 0x209;
    StaFlagOff(pG, STA_SUB_SCRN);
    cEm* e = (cEm*) (works + stride);
    re4_state_trace_enemy_frame(1000);  // the new array: no line
    e->be_flag = 1 | 2;
    e->id = 0x1A;
    e->type = 3;
    e->emset_no = 5;
    e->set = 0x24;
    e->flag = 0x00020000;
    e->hp = 300;
    e->pos.x = 100.0f;
    e->pos.z = 200.0f;
    e->r_no_0 = 1;
    re4_state_trace_enemy_frame(1001);  // em: + s1
    e->hp = 100;
    e->hitInfo.parts_no = 5;
    e->dmg.m_pDamageYarare = &e->hitInfo;
    e->dmg.m_Wep = 0x10;
    e->r_no_0 = 2;
    e->r_no_1 = 4;
    EM10_WK(e)->Be_flg = 0x4000;
    re4_state_trace_enemy_frame(1002);  // em: hit s1
    cEm* bomb = (cEm*) works;
    bomb->be_flag = 1 | 2;
    bomb->id = 0x42;
    bomb->r_no_0 = 1;
    bomb->r_no_1 = 3;
    e->r_no_1 = 9;
    re4_state_trace_enemy_frame(1003);  // em: s1 r 2/9/0/0, em: + s0 0x42/0
    bomb->r_no_1 = 9;
    bomb->hp = 1;
    bomb->pos.x = 120.0f;
    bomb->pos.y = 1500.0f;
    bomb->pos.z = 210.0f;
    EMWEP_WK(bomb)->pEm_old = e;
    EM10_WK(e)->Be_flg |= 0x80;
    re4_state_trace_enemy_frame(1004);  // em: s0 dynamite thrown by s1, em: s1 head lost
    bomb->r_no_1 = 2;
    bomb->hp = 0;
    bomb->pos.x = 900.0f;
    bomb->pos.y = 0.0f;
    e->be_flag &= ~2u;
    re4_state_trace_enemy_frame(1005);  // em: s0 dynamite exploded, em: s1 hidden
    e->pos.x = 5000.0f;
    re4_state_trace_enemy_frame(1006);  // em: s1 jumps
    e->r_no_0 = 1;
    e->r_no_1 = 0x50;
    e->r_no_2 = 0;
    re4_state_trace_enemy_frame(1006);  // em: s1 at the dynamite post
    e->r_no_2 = 4;
    re4_state_trace_enemy_frame(1006);  // em: s1 lights a dynamite stick
    // it holds back (R1 0x1B Stay) 6 m from Leon, then follows again 4 s later
    e->r_no_1 = 0x1B;
    e->r_no_2 = 0;
    e->l_pl = 6000.0f * 6000.0f;
    e->Guard_r = 10000.0f;
    EM10_WK(e)->L_pl_route = 7500.0f;
    EM10_WK(e)->L_pl_guard = 3000.0f;
    re4_state_trace_enemy_frame(1010);  // em: s1 leaves the dynamite post, em: s1 stays back (Stay)
    e->r_no_1 = 0x10;
    re4_state_trace_enemy_frame(1130);  // em: s1 follows again after 4.0 s
    e->be_flag = 0;
    re4_state_trace_enemy_frame(1131);  // em: - s1
    StaFlagOn(pG, STA_SUB_SCRN);
    Joy[0].rep = 0x00100000;
    Joy[0].substickX = 12;
    re4_state_trace_enemy_frame(1008);  // pad: menu frame 1008
    Joy[0] = keepJoy;
    if (!keepSub) {
        StaFlagOff(pG, STA_SUB_SCRN);
    }
    pG->Rno0 = keepStep;
    pG->room_id = keepRoom;
    EmMgr.pArray = keepArray;
    EmMgr.nArray = keepCount;
    EmMgr.size = keepSize;
    re4_state_trace_enemy_frame(1009);  // the old array again: starts over without a line
}

// Patch 0115 (at_mod.cpp EmAtCheck) over two fake works of EmMgr in MEM1: a door (0x41, box,
// priority 3) at the origin and one body overlapping it. A Ganado in the air (0x10, priority 3)
// is pushed out of the door like one on its feet, and EmAtCheck gives every body its m_flag back
// whole: the high bits too (0x100 scenery, 0x200 bodies, 0xC000), whose loss let Leon walk through
// doors and on in the air (0.91.0 test of 26.10.2026). Bits of the faults: 1 the flying Ganado not
// pushed, 2 its flag changed, 4 the Ganado on its feet not pushed, 8 its flag changed, 16 another
// body's flag changed (far from the door).
extern "C" int re4probe_em_at_flags(void)
{
    u8* works = (u8*) (unsigned long) 0x82E20000ul;
    const u32 stride = (u32) sizeof(cEm10);
    cEm* keepArray = EmMgr.pArray;
    u32 keepCount = EmMgr.nArray, keepSize = EmMgr.size;
    cEm* keepAlive = EmMgr.pAlive;
    cObj* keepObj = ObjMgr.pAlive;
    static const struct {
        u8 id;
        u16 flag;
        f32 z;
        int notPushed, flagChanged;
    } cases[] = {
        {0x10, 0x0318, 30.0f, 1, 2},
        {0x10, 0x0310, 30.0f, 4, 8},
        {0x00, 0xC308, 5000.0f, 0, 16},
    };
    int bad = 0;

    for (const auto& c : cases) {
        memset(works, 0, 2 * stride);
        EmMgr.pArray = (cEm*) works;
        EmMgr.nArray = 2;
        EmMgr.size = stride;
        ObjMgr.pAlive = 0;
        cEm* door = (cEm*) works;
        cEm* body = (cEm*) (works + stride);
        door->be_flag = 1;
        door->id = 0x41;
        door->atari.m_flag = 0x0200 | 0x18 | 2;
        door->atari.m_radius = 100.0f;
        door->atari.m_radius2 = 20.0f;
        door->atari.m_height = 200.0f;
        door->setNext(body);
        body->be_flag = 1;
        body->id = c.id;
        body->atari.m_flag = c.flag;
        body->atari.m_radius = 50.0f;
        body->atari.m_radius2 = 50.0f;
        body->atari.m_height = 100.0f;
        body->atari.m_stat = 1;
        body->pos.z = c.z;
        body->setNext(0);
        EmMgr.pAlive = door;
        EmAtCheck(body);
        if (c.notPushed && body->pos.z < 69.0f) {
            bad |= c.notPushed;
        }
        if (body->atari.m_flag != c.flag || door->atari.m_flag != (0x0200 | 0x18 | 2)) {
            bad |= c.flagChanged;
        }
    }
    EmMgr.pArray = keepArray;
    EmMgr.nArray = keepCount;
    EmMgr.size = keepSize;
    EmMgr.pAlive = keepAlive;
    ObjMgr.pAlive = keepObj;
    return bad;
}

extern cPlayer* pPL;

// The head of an AEV file as SceAtInit reads it (sce_at.cpp SceAtFileHead).
struct SceAtProbeHead {
    char magic[4];
    u16 version;
    u16 num;
    u8 pad[8];
};
static_assert(sizeof(cPlayer) <= 0x10000, "the fake player must fit below the fake scene areas");

// Leon's side of a death in the error log (state_trace.cpp playerLog) with a fake player and
// one scene area in MEM1: r21d starts running, the death demo starts with the areas 0x05 (a
// quadrilateral for the player) and 0x13 hit, Leon's position stops being a number, the room
// runs again (the continue); then an enemy of a fake EmMgr array goes to NaN (enemyLog).
extern "C" void re4probe_death_log(void)
{
    cPlayer* keepPl = pPL;
    cEm* keepSub = pSUB;
    u8 keepStep = pG->Rno0;
    u16 keepRoom = pG->room_id;
    u16 keepLife = pG->pl_life, keepMax = pG->pl_life_max;
    u8 keepWeapon = pG->weapon_no;
    int keepDying = StaFlagChk(pG, STA_DIEDEMO) != 0;
    cEm* keepArray = EmMgr.pArray;
    u32 keepCount = EmMgr.nArray, keepSize = EmMgr.size;

    u8* aev = (u8*) (unsigned long) 0x82E50000ul;
    memset(aev, 0, 0x10 + sizeof(SCE_AT_DATA));
    memcpy(aev, "AEV", 4);
    ((SceAtProbeHead*) aev)->version = 0x104;
    ((SceAtProbeHead*) aev)->num = 1;
    SCE_AT_DATA* w = (SCE_AT_DATA*) (aev + 0x10);
    w->no = 5;
    w->be_flg = 1;
    w->trg_type = 1;
    w->target_type = 1;
    w->parts_no = -1;
    w->area.type = AREA_TYPE_XZ4;
    w->area.xz4.height = 2000.0f;
    w->area.xz4.p[1].x = 1000.0f;
    w->area.xz4.p[2].x = 1000.0f;
    w->area.xz4.p[2].z = 1000.0f;
    w->area.xz4.p[3].z = 1000.0f;
    SceAtInit(aev, 0);

    cPlayer* pl = (cPlayer*) (unsigned long) 0x82E40000ul;
    memset((void*) pl, 0, sizeof(cPlayer));
    pPL = pl;
    pSUB = 0;
    pl->pos.x = 100.0f;
    pl->pos.z = 200.0f;
    pl->ang.y = 0.5f;
    pG->room_id = 0x21D;
    pG->pl_life = 1200;
    pG->pl_life_max = 1200;
    pG->weapon_no = 2;
    StaFlagOff(pG, STA_DIEDEMO);
    EmMgr.pArray = 0;  // no enemy lines yet
    pG->Rno0 = 4;
    re4_state_trace_enemy_frame(2000);
    pG->Rno0 = 3;
    re4_state_trace_enemy_frame(2001);  // room: r21d runs
    SceAtSetHitFlg(5);
    SceAtSetHitFlg(0x13);
    StaFlagOn(pG, STA_DIEDEMO);
    re4_state_trace_enemy_frame(2003);  // die: r21d after 2 frames, die: area 0x05
    pl->pos.x = __builtin_nanf("");
    re4_state_trace_enemy_frame(2004);  // pl: not a number
    pG->Rno0 = 4;
    re4_state_trace_enemy_frame(2005);
    StaFlagOff(pG, STA_DIEDEMO);
    pl->pos.x = 100.0f;
    pG->Rno0 = 3;
    re4_state_trace_enemy_frame(2006);  // room: r21d runs ..., continue after a death

    u8* works = (u8*) (unsigned long) 0x82E20000ul;
    const u32 stride = (u32) sizeof(cEm10);
    memset(works, 0, 2 * stride);
    EmMgr.pArray = (cEm*) works;
    EmMgr.nArray = 2;
    EmMgr.size = stride;
    cEm* e = (cEm*) (works + stride);
    e->be_flag = 1 | 2;
    e->id = 0x2D;
    e->hp = 100;
    re4_state_trace_enemy_frame(2007);  // em: + s1 0x2d/0
    e->pos.y = __builtin_nanf("");
    re4_state_trace_enemy_frame(2008);  // em: s1 position is not a number

    EmMgr.pArray = keepArray;
    EmMgr.nArray = keepCount;
    EmMgr.size = keepSize;
    SceAtInit(0, 0);
    pPL = keepPl;
    pSUB = keepSub;
    pG->Rno0 = keepStep;
    pG->room_id = keepRoom;
    pG->pl_life = keepLife;
    pG->pl_life_max = keepMax;
    pG->weapon_no = keepWeapon;
    if (keepDying) {
        StaFlagOn(pG, STA_DIEDEMO);
    }
}

extern "C" unsigned re4_arc_clamp_count(void);

// acosf / asinf of the game code (re4_whole_prelude.h re4_acosf, whole_prepare.py ARC_CALL): the
// insects' wall step (em2d/em2c em2dSetWallMatrix2: sinf(acosf(dot(nrm, wallNrm))) * 700 added
// to the position) on a slope with the normal (0.1, 1, -0.05), all four probes on that face, with
// the port's own PSVECNormalize / PSVECAdd / PSVECDotProduct: the dot product comes out above 1,
// which made the insect's position NaN. Bits: 1 the dot product is not above 1 here (the case
// is not reproduced), 2 the step is not a number, 4 a rounding step beyond 1 is not 0 / pi /
// pi/2, 8 further out is not NaN (as on the GameCube), 16 an ordinary value changed, 32 the
// clamps were not counted.
extern "C" int re4probe_arc(void)
{
    int bad = 0;
    unsigned before = re4_arc_clamp_count();
    Vec raw = {0.1f, 1.0f, -0.05f};
    Vec nrm, sum, wall;
    PSVECNormalize(&raw, &nrm);
    PSVECAdd(&nrm, &nrm, &sum);
    PSVECAdd(&nrm, &sum, &sum);
    PSVECAdd(&nrm, &sum, &sum);
    PSVECNormalize(&sum, &wall);
    f32 dot = PSVECDotProduct(&nrm, &wall);
    if (!(dot > 1.0f)) bad |= 1;
    f32 step = sinf(re4_acosf(dot)) * 700.0f;
    if (step - step != 0.0f) bad |= 2;
    volatile f32 up = 1.00000012f, down = -1.00000012f, far = 1.001f, half = 0.5f;
    if (re4_acosf(up) != 0.0f || re4_acosf(down) != acosf(-1.0f) || re4_asinf(up) != asinf(1.0f)) bad |= 4;
    f32 out = re4_acosf(far);
    if (out == out) bad |= 8;
    if (re4_acosf(half) != acosf(half) || re4_asinf(half) != asinf(half)) bad |= 16;
    if (re4_arc_clamp_count() - before != (dot > 1.0f ? 4u : 3u)) bad |= 32;
    return bad;
}

// Patch 0098: the partner's damage handler (pAuxDm, the bulldozer's Sub_dm_bull while Ashley drives)
// gets the partner as its argument, as on the GameCube (r3 = this). A partner in plain memory:
// moveDamage reads only pAuxDm before the call.
static cEm* gPartnerDamageArg;

static void probePartnerDamage(cEm* em)
{
    gPartnerDamageArg = em;
}

extern "C" int re4probe_partner_damage(void)
{
    alignas(16) static unsigned char mem[sizeof(cSubChar)];
    memset(mem, 0, sizeof(mem));
    cSubChar* sub = (cSubChar*) mem;
    sub->pAuxDm = probePartnerDamage;
    gPartnerDamageArg = 0;
    sub->moveDamage();
    return gPartnerDamageArg == (cEm*) sub ? 0 : 1;
}

// Patch 0100: items whose saved place in the case is taken or outside the case get the first free
// place (pzlPlayer::init). A small case with the handgun and a spray where a new game puts them,
// a Killer7 saved on the handgun's place and handgun ammo saved outside the case (as the debug
// build's case maker left them, tester 22.10.2026). Returns the bits of the failed checks.
extern "C" int re4probe_case_free_place(void)
{
    static cItem items[4];
    const struct {
        u16 id;
        s8 x;
        s8 y;
    } set[] = {{0x23, 2, 1}, {0x2A, 2, 1}, {0x05, 6, 3}, {0x04, 40, 2}};
    memset(items, 0, sizeof(items));
    for (int i = 0; i < 4; i++) {
        items[i].id = set[i].id;
        items[i].num = 1;
        items[i].flags = 1;
        items[i].x = set[i].x;
        items[i].y = set[i].y;
        items[i].board = 1;
    }
    cItem* keepItems = ItemMgr.m_pItem;
    s32 keepNum = ItemMgr.m_array_num;
    u8 keepType = pG->pl_type;
    ItemMgr.m_pItem = items;
    ItemMgr.m_array_num = 4;
    pG->pl_type = 0;
    if (pLog == 0) {
        LogInit();
    }
    alignas(16) static unsigned char mem[sizeof(pzlPlayer)];
    memset(mem, 0, sizeof(mem));
    pzlPlayer* pl = (pzlPlayer*) mem;
    int bad = 0;
    if (!pl->init(0)) {
        bad |= 1;
    } else {
        for (int i = 0; i < 4; i++) {
            pzlPiece* p = pl->piecePtr(&items[i]);
            if (p == 0 || !p->isOnBoard() || !pl->boardPtr()->search(p)) {
                bad |= 2 << i;
            }
        }
        pl->save();
        // the handgun and the spray keep their places; the Killer7 and the ammo take the first
        // free ones in reading order (half cells: Killer7 at cells 4..7 x 0..1, ammo 8..9 x 0)
        const s8 want[4][2] = {{2, 1}, {11, 1}, {6, 3}, {17, 0}};
        for (int i = 0; i < 4; i++) {
            if (items[i].x != want[i][0] || items[i].y != want[i][1] || items[i].board != 1) {
                re4_log("  casefree: item 0x%02x at (%d %d) board %d, want (%d %d) board 1", items[i].id,
                        items[i].x, items[i].y, items[i].board, want[i][0], want[i][1]);
                bad |= 0x20 << i;
            }
        }
        pl->quit();
    }
    ItemMgr.m_pItem = keepItems;
    ItemMgr.m_array_num = keepNum;
    pG->pl_type = keepType;
    return bad;
}

// Touch in the attache case (runtime/menu_touch.cpp, patch 0106): the camera of sscrnCameraInit,
// IDSystem's screen matrix as IDSystem::move sets it, a case board of 10 x 6 with the handgun
// and the spray at (-500 300 0), and fake widgets of the case screen
// (open as for an item pick-up, so without the tab row). The taps: a cell moves the cursor, the
// same piece again is A; an entry of the command menu is selected, again A; a tap on the case
// closes the menu (B); the question: the other option is left/right, the selected one A; an id
// unit's quad. Bits of the result: each failed step.
extern "C" int re4_touch_board_cell(pzlBoard* b, f32 u, f32 v, int* x, int* y);
extern "C" int re4_touch_unit_hit(const ID_UNIT* unit, f32 u, f32 v);
extern "C" void re4_touch_sscrn(SUB_SCREEN* wk, Widget<SUB_SCREEN>* cur, SsPzzlMain* pzzl, SsItemMain* item,
                                SsMapMain* map, SsFileMain* file);
extern "C" void re4_core_touch(float u, float v);
extern "C" void re4_core_swipe(int dir);
extern "C" void re4_touch_item_keys(int curCol, int col, int n, int combine, u64* keys, int* count);
extern "C" void re4_touch_file_keys(int mode, int cursor, int target, u64* keys, int* count);
extern "C" void re4_touch_sscrn_moved(SUB_SCREEN* wk, Widget<SUB_SCREEN>* cur, SsPzzlMain* pzzl, SsItemMain* item,
                                      SsMapMain* map, SsFileMain* file);
extern "C" void re4_core_set_letterbox(int off);
extern "C" int re4_letterbox_open(void);
extern "C" int re4_picture_still(void);

namespace {

const f32 kTan10 = 0.17632698f;  // tan(10 degrees): half the sub screen's 20 degree fov

// the place of a world point (z 0) in the picture as the sub screen camera shows it
void worldUv(f32 x, f32 y, f32* u, f32* v)
{
    *u = 0.5f + 0.5f * x / (5000.0f * kTan10 * 4.0f / 3.0f);
    *v = 0.5f - 0.5f * y / (5000.0f * kTan10);
}

// an id unit (anchor in the middle) at (x, y) of the id space (+-320 x +-240, y up)
void probeUnit(ID_UNIT* u, f32 x, f32 y, f32 w, f32 h)
{
    memset(u, 0, sizeof(*u));
    u->be_flag = 0x0D;
    PSMTXIdentity(u->mat);
    u->mat[0][3] = x;
    u->mat[1][3] = y;
    u->ver[0].x = -w / 2;
    u->ver[0].y = h / 2;
    u->ver[1].x = w / 2;
    u->ver[1].y = h / 2;
    u->ver[2].x = w / 2;
    u->ver[2].y = -h / 2;
    u->ver[3].x = -w / 2;
    u->ver[3].y = -h / 2;
}

f32 unitU(f32 x) { return 0.5f + x / 640.0f; }
f32 unitV(f32 y) { return 0.5f - y / 480.0f; }

// one tap, one frame of the sub screen task: Key.trg afterwards
u64 tapFrame(SsPzzlMain* main, f32 u, f32 v)
{
    Key.trg = 0;
    re4_core_touch(u, v);
    re4_touch_sscrn(&SubScreenWk, main, main, 0, 0, 0);
    return Key.trg;
}

} // namespace

extern "C" int re4probe_menu_touch(void)
{
    const u64 kA = 0x80000000ull, kB = 0x40000000ull, kLeft = 0x08000000ull;
    static cItem items[2];
    memset(items, 0, sizeof(items));
    items[0].id = 0x23;
    items[0].x = 2;
    items[0].y = 1;
    items[1].id = 0x05;
    items[1].x = 6;
    items[1].y = 3;
    for (int i = 0; i < 2; i++) {
        items[i].num = 1;
        items[i].flags = 1;
        items[i].board = 1;
    }
    cItem* keepItems = ItemMgr.m_pItem;
    s32 keepNum = ItemMgr.m_array_num;
    u8 keepType = pG->pl_type;
    CAMERA keepCam = pG->Camera;
    SUB_SCREEN keepWk = SubScreenWk;
    KEY keepKey = Key;
    Mtx keepScrn;
    PSMTXCopy(IDSystem::m_scrn_mat, keepScrn);
    const int keepSeStop = DbgFlagChk(pG, DBG_SE_STOP) != 0;
    DbgFlagOn(pG, DBG_SE_STOP);  // no sound system here: SndCall returns at once
    ItemMgr.m_pItem = items;
    ItemMgr.m_array_num = 2;
    pG->pl_type = 0;
    if (pLog == 0) {
        LogInit();
    }

    Vec eye = {0.0f, 0.0f, 5000.0f}, up = {0.0f, 1.0f, 0.0f}, at = {0.0f, 0.0f, 0.0f};
    C_MTXPerspective(pG->Camera.ProjMat, 20.0f, 1.3333334f, ZNEAR, ZFAR);
    C_MTXLookAt(pG->Camera.v_mat, &eye, &up, &at);
    PSMTXIdentity(IDSystem::m_scrn_mat);
    IDSystem::m_scrn_mat[2][3] = -240.0f / kTan10;

    int bad = 0;
    alignas(16) static unsigned char playerMem[sizeof(pzlPlayer)];
    memset(playerMem, 0, sizeof(playerMem));
    pzlPlayer* pl = (pzlPlayer*) playerMem;
    if (!pl->init(0)) {
        bad |= 1;
    } else {
        pzlBoard* b = pl->boardPtr();
        PSMTXIdentity(*b->orientation());
        (*b->orientation())[0][3] = -500.0f;
        (*b->orientation())[1][3] = 300.0f;
        pl->m_p_active_board = b;
        b->m_cur_x = 0;
        b->m_cur_y = 0;
        // a cell of the spray (its piece's place, as pzlPlayer::init put it)
        int sx = -1, sy = -1;
        for (int cy = 0; cy < b->size_y() && sx < 0; cy++) {
            for (int cx = 0; cx < b->size_x() && sx < 0; cx++) {
                pzlPiece* p = b->getPiece(cx, cy);
                if (p && p->item && p->item->id == 0x05) {
                    sx = cx;
                    sy = cy;
                }
            }
        }
        if (sx < 0) {
            bad |= 1;
            sx = 6;
            sy = 3;
        }
        re4_log("menutouch: the spray at (%d %d)", sx, sy);
        // its cell's middle: local (100 sx + 50, -(100 sy + 50)), the board at (-500 300 0)
        const f32 sprayX = -500.0f + 100.0f * sx + 50.0f, sprayY = 300.0f - 100.0f * sy - 50.0f;
        f32 u, v;
        int x = -1, y = -1;
        worldUv(sprayX, sprayY, &u, &v);
        if (!re4_touch_board_cell(b, u, v, &x, &y) || x != sx || y != sy) {
            re4_log("  menutouch: cell (%d %d), want (%d %d)", x, y, sx, sy);
            bad |= 2;
        }
        // id unit quad
        ID_UNIT unit;
        probeUnit(&unit, 100.0f, 50.0f, 40.0f, 20.0f);
        if (!re4_touch_unit_hit(&unit, unitU(110.0f), unitV(45.0f)) || re4_touch_unit_hit(&unit, unitU(130.0f), unitV(50.0f))) {
            bad |= 4;
        }

        // the case screen's widgets, never constructed (their code lives in the Sscrn module)
        alignas(16) static unsigned char mainMem[sizeof(SsPzzlMain)], selMem[sizeof(PieceSelect)],
            cmdMem[sizeof(PieceCommand)], combMem[sizeof(PieceCombine)];
        memset(mainMem, 0, sizeof(mainMem));
        memset(selMem, 0, sizeof(selMem));
        memset(cmdMem, 0, sizeof(cmdMem));
        memset(combMem, 0, sizeof(combMem));
        SsPzzlMain* main = (SsPzzlMain*) mainMem;
        PieceSelect* sel = (PieceSelect*) selMem;
        PieceCommand* cmd = (PieceCommand*) cmdMem;
        main->select = sel;
        main->command = cmd;
        main->combine = (PieceCombine*) combMem;
        main->cur = sel;
        SUB_SCREEN* wk = &SubScreenWk;
        wk->Loop = 1;
        wk->open_flag = SS_OPEN_PZZL;
        wk->puzzlePlayer = pl;
        wk->board_size = wk->board_next = 0;
        wk->pzzl_debug_open = 0;
        wk->item_make_open = 0;
        wk->cursor_flag = 0;

        // a cell: the cursor moves; the same piece again: A
        if (tapFrame(main, u, v) != 0 || b->m_cur_x != sx || b->m_cur_y != sy || !(wk->cursor_flag & 1)) {
            bad |= 8;
        }
        if (tapFrame(main, u, v) != kA) {
            bad |= 0x10;
        }
        // no tap: nothing
        Key.trg = 0;
        re4_touch_sscrn(wk, main, main, 0, 0, 0);
        if (Key.trg != 0) {
            bad |= 0x20;
        }
        // an empty cell (0 5): the cursor moves, no A
        worldUv(-500.0f + 50.0f, 300.0f - 550.0f, &u, &v);
        if (b->getPiece(0, 5) || tapFrame(main, u, v) != 0 || b->m_cur_x != 0 || b->m_cur_y != 5) {
            bad |= 0x40;
        }

        // the command menu: three entries at y 40, 0, -40 of the id space
        ID_UNIT entries[6], sub[11];
        for (int i = 0; i < 3; i++) {
            probeUnit(&entries[2 * i], 200.0f, 40.0f - 40.0f * i, 80.0f, 30.0f);
            probeUnit(&entries[2 * i + 1], 200.0f, 40.0f - 40.0f * i, 60.0f, 20.0f);
            cmd->id[6 + 2 * i] = &entries[2 * i];
            cmd->id[7 + 2 * i] = &entries[2 * i + 1];
        }
        cmd->num = 3;
        cmd->mode = 0;
        wk->cmd_menu_no = 0;
        main->cur = cmd;
        if (tapFrame(main, unitU(200.0f), unitV(0.0f)) != 0 || wk->cmd_menu_no != 1) {
            bad |= 0x80;
        }
        if (tapFrame(main, unitU(205.0f), unitV(-5.0f)) != kA || wk->cmd_menu_no != 1) {
            bad |= 0x100;
        }
        // a tap on the case closes the menu
        worldUv(sprayX, sprayY, &u, &v);
        if (tapFrame(main, u, v) != kB) {
            bad |= 0x200;
        }
        // the question (discard: option 0 yes, 1 no, 1 selected)
        for (int i = 0; i < 11; i++) {
            probeUnit(&sub[i], -1000.0f, -1000.0f, 1.0f, 1.0f);
            cmd->sub[i] = &sub[i];
        }
        probeUnit(&sub[5], 150.0f, -100.0f, 60.0f, 30.0f);
        probeUnit(&sub[7], 250.0f, -100.0f, 60.0f, 30.0f);
        cmd->mode = 2;
        cmd->subSel = 1;
        if (tapFrame(main, unitU(150.0f), unitV(-100.0f)) != kLeft) {
            bad |= 0x400;
        }
        cmd->subSel = 0;
        if (tapFrame(main, unitU(150.0f), unitV(-100.0f)) != kA) {
            bad |= 0x800;
        }
        if (tapFrame(main, unitU(0.0f), unitV(200.0f)) != 0) {
            bad |= 0x1000;
        }
        // the normal inventory: a tap on the tab row gives no keys (taps on the tabs were taken
        // out, decision of the project owner, 22.10.2026)
        wk->open_flag = SS_OPEN_NORMAL;
        main->cur = sel;
        main->state = 0;
        sel->state = 0;
        pl->m_p_active_board = b;
        if (tapFrame(main, unitU(-200.0f), unitV(215.0f)) != 0) {
            bad |= 0x2000;
        }
        // swipes (the tabs in the game's order: 0 key items, 1 case, 2 map, 3 files): to the left
        // the map; B gives the row the focus, A then in the frame the row has it
        alignas(8) static u8 mapMem[sizeof(SsMapMain)], itemMem[sizeof(SsItemMain)];
        memset(mapMem, 0, sizeof mapMem);
        memset(itemMem, 0, sizeof itemMem);
        SsMapMain* fmap = (SsMapMain*) mapMem;
        SsItemMain* fitem = (SsItemMain*) itemMem;
        auto frame = [&](Widget<SUB_SCREEN>* cur) -> u64 {
            Key.trg = 0;
            re4_touch_sscrn(wk, cur, main, fitem, fmap, 0);
            return Key.trg;
        };
        Widget<SUB_SCREEN>* mapW = (Widget<SUB_SCREEN>*) fmap;
        Widget<SUB_SCREEN>* itemW = (Widget<SUB_SCREEN>*) fitem;
        static int wholeView;  // the map's whole view (MapEntire): swipes change the tab there
        fmap->entire = (MapEntire*) (void*) &wholeView;
        fmap->cur = (Widget<SUB_SCREEN>*) (void*) &wholeView;
        // on the row: the chosen tab highlighted 6 frames, A in the 7th
        auto rowA = [&](Widget<SUB_SCREEN>* cur) -> int {
            for (int i = 1; i <= 12; i++) {
                if (frame(cur) == kA) {
                    return i;
                }
            }
            return 0;
        };
        re4_core_swipe(1);
        bad |= frame(main) != kB ? 0x4000 : 0;
        main->state = 1;  // the row has the focus (SsPzzlMain after PieceSelect mode 2)
        bad |= rowA(main) != 7 || wk->menu_next != 2 ? 0x8000 : 0;
        main->state = 0;
        // the map shows; to the right: the case again, counted from the map (B: the map's view)
        bad |= frame(mapW) != 0 ? 0x10000 : 0;
        re4_core_swipe(-1);
        bad |= frame(mapW) != kB ? 0x10000 : 0;
        fmap->state = 1;
        bad |= rowA(mapW) != 7 || wk->menu_next != 1 ? 0x20000 : 0;
        fmap->state = 0;
        // the case shows; two quick swipes to the left: the files, not the map (the second counts
        // from the tab the first goes to, and keeps its B)
        bad |= frame(main) != 0 ? 0x40000 : 0;
        re4_core_swipe(1);
        bad |= frame(main) != kB ? 0x40000 : 0;
        re4_core_swipe(1);
        bad |= frame(main) != 0 ? 0x40000 : 0;
        main->state = 1;
        bad |= rowA(main) != 7 || wk->menu_next != 3 ? 0x80000 : 0;
        main->state = 0;
        // the files never show here: after 90 frames the case counts again; to the right the key
        // items, and a swipe to the right on the way there or from them: nothing beyond them
        for (int i = 0; i < 91; i++) {
            frame(main);
        }
        re4_core_swipe(-1);
        bad |= frame(main) != kB ? 0x100000 : 0;
        main->state = 1;
        bad |= rowA(main) != 7 || wk->menu_next != 0 ? 0x100000 : 0;
        main->state = 0;
        re4_core_swipe(-1);
        bad |= frame(main) != 0 ? 0x200000 : 0;
        bad |= frame(itemW) != 0 ? 0x200000 : 0;
        re4_core_swipe(-1);
        bad |= frame(itemW) != 0 ? 0x200000 : 0;
        // the zoomed map (MapRead): a swipe changes the tab there too (dragging the map was taken
        // out, device test 22.10.2026 21:22): B to the row, A with the files
        static int zoomedView;
        fmap->read = (MapRead*) (void*) &zoomedView;
        fmap->cur = (Widget<SUB_SCREEN>*) (void*) &zoomedView;
        for (int i = 0; i < 91; i++) {
            frame(mapW);
        }
        re4_core_swipe(1);
        bad |= frame(mapW) != kB ? 0x800000 : 0;
        fmap->state = 1;
        bad |= rowA(mapW) != 7 || wk->menu_next != 3 ? 0x800000 : 0;
        fmap->state = 0;
        for (int i = 0; i < 91; i++) {
            frame(main);
        }
        // from the case to the map: the case's B puts the highlight on the exit (PieceSelect,
        // menu_next 4); after cur->move, in that same frame, the map's tab gets it, A 6 frames on
        re4_core_swipe(1);
        bad |= frame(main) != kB ? 0x2000000 : 0;
        wk->menu_next = 4;
        main->state = 1;
        re4_touch_sscrn_moved(wk, main, main, fitem, fmap, 0);
        bad |= wk->menu_next != 2 ? 0x2000000 : 0;
        bad |= rowA(main) != 6 || wk->menu_next != 2 ? 0x2000000 : 0;
        main->state = 0;
        // after cur->move without a change on its way, or off the row: nothing
        wk->menu_next = 4;
        re4_touch_sscrn_moved(wk, main, main, fitem, fmap, 0);
        bad |= wk->menu_next != 4 ? 0x2000000 : 0;
        // a swipe while the chosen screen loads long (its init widget, 60 frames): the change goes
        // on from the tab chosen, its frames wait for the screen (logcat 20:48: a tab dropped
        // while the map loaded); B and A on the map
        static int initWidget;
        Widget<SUB_SCREEN>* loading = (Widget<SUB_SCREEN>*) (void*) &initWidget;
        for (int i = 0; i < 60; i++) {
            bad |= frame(loading) != 0 ? 0x4000000 : 0;
        }
        re4_core_swipe(-1);
        for (int i = 0; i < 60; i++) {
            bad |= frame(loading) != 0 ? 0x4000000 : 0;
        }
        bad |= frame(mapW) != kB ? 0x4000000 : 0;
        fmap->state = 1;
        bad |= rowA(mapW) != 7 || wk->menu_next != 1 ? 0x4000000 : 0;
        fmap->state = 0;
        // a swipe while a screen loads with nothing on its way (the cross chose it): kept, then
        // counted from that screen (the case): the map
        bad |= frame(main) != 0 ? 0x8000000 : 0;
        bad |= frame(loading) != 0 ? 0x8000000 : 0;
        re4_core_swipe(1);
        bad |= frame(loading) != 0 ? 0x8000000 : 0;
        bad |= frame(main) != kB ? 0x8000000 : 0;
        main->state = 1;
        bad |= rowA(main) != 7 || wk->menu_next != 2 ? 0x8000000 : 0;
        main->state = 0;
        for (int i = 0; i < 91; i++) {
            frame(main);
        }
        // a swipe while the sub screen closes (Loop 0): no keys, the tab forgotten; a tap neither
        re4_core_swipe(1);
        wk->Loop = 0;
        bad |= frame(main) != 0 ? 0x400000 : 0;
        wk->Loop = 1;
        bad |= frame(main) != 0 ? 0x400000 : 0;
        wk->Loop = 0;
        worldUv(sprayX, sprayY, &u, &v);
        if (tapFrame(main, u, v) != 0) {
            bad |= 0x400000;
        }
        wk->Loop = 1;
        // the presses for the key items and the files
        {
            u64 keys[16];
            int n = 0;
            re4_touch_item_keys(0, 0, 0, 0, keys, &n);
            bool ok = n == 1 && keys[0] == kA;
            re4_touch_item_keys(0, 1, -2, 0, keys, &n);
            ok = ok && n == 3 && keys[0] == 0x02000000ull && keys[1] == kLeft && keys[2] == kLeft;
            re4_touch_item_keys(1, 0, 3, 0, keys, &n);
            ok = ok && n == 4 && keys[0] == 0x01000000ull && keys[3] == 0x04000000ull;
            re4_touch_item_keys(0, 1, 1, 1, keys, &n);  // combining: only within the row
            ok = ok && n == 0;
            re4_touch_file_keys(0, 2, 4, keys, &n);  // category row: down into the list, 2 down, A
            ok = ok && n == 4 && keys[0] == 0x02000000ull && keys[2] == 0x02000000ull && keys[3] == kA;
            re4_touch_file_keys(1, 3, 1, keys, &n);  // the list: 2 up, A
            ok = ok && n == 3 && keys[0] == 0x01000000ull && keys[1] == 0x01000000ull && keys[2] == kA;
            bad |= ok ? 0 : 0x1000000;
        }
        pl->quit();
    }
    SubScreenWk = keepWk;
    Key = keepKey;
    pG->Camera = keepCam;
    PSMTXCopy(keepScrn, IDSystem::m_scrn_mat);
    if (!keepSeStop) {
        DbgFlagOff(pG, DBG_SE_STOP);
    }
    ItemMgr.m_pItem = keepItems;
    ItemMgr.m_array_num = keepNum;
    pG->pl_type = keepType;
    return bad;
}

extern "C" void re4_trace_snd_look(int reset, unsigned out[3]);
extern "C" void re4_trace_sections(unsigned n);
extern u32 proc_tick[32];
extern const char* proc_name[32];
extern int proc_tick_idx_bak;

// The frame's sections (patch 0108, state_trace.cpp): 600 frames of 1 ms setup, 2 ms logic, 5 ms
// drawing, 3 ms rest, 1 ms frame end and 21.3 ms waiting give one line with those averages; a
// frame outside a room or without the scheduler's marks counts not. Bits: each failed case.
extern "C" int re4probe_frame_sections(void)
{
    u32 keepTick[32];
    const char* keepName[32];
    memcpy(keepTick, proc_tick, sizeof keepTick);
    memcpy(keepName, proc_name, sizeof keepName);
    const int keepIdx = proc_tick_idx_bak;
    const u8 keepStep = pG->Rno0;
    const u16 keepRoom = pG->room_id;
    const u32 ms = 40500;  // OSGetTick per millisecond
    proc_tick[4] = 1 * ms;
    proc_tick[5] = 3 * ms;
    proc_tick[6] = 8 * ms;
    proc_tick[2] = 11 * ms;
    proc_tick[3] = 12 * ms;
    proc_tick[0] = 33 * ms + ms / 3;
    proc_name[5] = "TaskScheduler";
    proc_name[6] = "SndWatcher";
    proc_tick_idx_bak = 2;
    pG->room_id = 0x105;
    pG->Rno0 = 2;  // not in a room: not counted
    for (unsigned i = 0; i < 700; i++) {
        re4_trace_sections(i);
    }
    pG->Rno0 = 3;
    for (unsigned i = 0; i < 600; i++) {
        re4_trace_sections(1000 + i);
    }
    memcpy(proc_tick, keepTick, sizeof keepTick);
    memcpy(proc_name, keepName, sizeof keepName);
    proc_tick_idx_bak = keepIdx;
    pG->Rno0 = keepStep;
    pG->room_id = keepRoom;
    return 0;
}

// The sound driver's request bank (patch 0107: 256 slots) and its count before an audio frame
// (state_trace.cpp): 70 requests are over the GameCube's 64, a full bank counts as full; the
// game refuses the 257th. Bits: each failed case.
extern "C" int re4probe_snd_bank(void)
{
    int bad = 0;
    static SND_REQ keep[SND_REQ_BANK_MAX][SND_REQ_MAX];
    memcpy(keep, Snd_req_work, sizeof keep);
    const s32 keepPush = Snd_ctrl_work.req_push_idx;
    const s32 keepReset = Snd_ctrl_work.reset_flag;
    bad |= SND_REQ_MAX == 256 ? 0 : 1;
    memset(Snd_req_work, 0, sizeof Snd_req_work);
    Snd_ctrl_work.req_push_idx = 1;
    Snd_ctrl_work.reset_flag = 0;
    unsigned c[3];
    for (int i = 0; i < 70; i++) {
        Snd_req_work[1][i].be_flag = 1;
    }
    Snd_req_work[0][0].be_flag = 1;  // the other bank does not count
    re4_trace_snd_look(1, c);
    bad |= c[0] == 70 && c[1] == 1 && c[2] == 0 ? 0 : 2;
    int opened = 0;
    while (SND_REQ* r = Snd_open_req_work()) {
        r->be_flag = 1;
        opened++;
    }
    bad |= opened == SND_REQ_MAX - 70 ? 0 : 4;
    re4_trace_snd_look(0, c);
    bad |= c[0] == SND_REQ_MAX && c[1] == 2 && c[2] == 1 ? 0 : 8;
    memcpy(Snd_req_work, keep, sizeof keep);
    Snd_ctrl_work.req_push_idx = keepPush;
    Snd_ctrl_work.reset_flag = keepReset;
    return bad;
}

// The menu screens without in-between pictures (runtime/picture.cpp re4_picture_still): a sub
// screen, a memory card screen, the tool menu or a result screen (an id table of class IDC_TITLE in
// the main loop; here an empty one: the flag only, no units). Bits: each failed case.
extern "C" int re4probe_picture_still(void)
{
    const int keepSub = StaFlagChk(pG, STA_SUB_SCRN) != 0, keepCard = SysFlagChk(pG, SYS_TYPEWRITER) != 0;
    const int keepTool = DbgFlagChk(pG, DBG_TEST_MODE) != 0;
    const u8 keepStep = pG->Rno0;
    int bad = 0;
    StaFlagOff(pG, STA_SUB_SCRN);
    SysFlagOff(pG, SYS_TYPEWRITER);
    DbgFlagOff(pG, DBG_TEST_MODE);
    bad |= re4_picture_still() ? 1 : 0;
    StaFlagOn(pG, STA_SUB_SCRN);
    bad |= re4_picture_still() ? 0 : 2;
    StaFlagOff(pG, STA_SUB_SCRN);
    SysFlagOn(pG, SYS_TYPEWRITER);
    bad |= re4_picture_still() ? 0 : 4;
    SysFlagOff(pG, SYS_TYPEWRITER);
    DbgFlagOn(pG, DBG_TEST_MODE);
    bad |= re4_picture_still() ? 0 : 8;
    DbgFlagOff(pG, DBG_TEST_MODE);
    // a result screen: only in the main loop; a result table the game holds now is left as it is
    if (!IdSys.setCk(IDC_TITLE)) {
        static const ID_FILE_HEADER empty = { { '2', '.', '0', '0' }, 0, 0, { 0, 0 } };
        pG->Rno0 = 3;
        bad |= re4_picture_still() ? 0x10 : 0;
        IdSys.set((ID_FILE_HEADER*) &empty, 0xFF, IDC_TITLE, 0x13, 6, 0);
        bad |= re4_picture_still() ? 0 : 0x20;
        pG->Rno0 = 2;
        bad |= re4_picture_still() ? 0x40 : 0;
        IdSys.kill(0xFF, IDC_TITLE);
        pG->Rno0 = 3;
        bad |= re4_picture_still() ? 0x80 : 0;
        pG->Rno0 = keepStep;
    }
    if (keepSub) {
        StaFlagOn(pG, STA_SUB_SCRN);
    }
    if (keepCard) {
        SysFlagOn(pG, SYS_TYPEWRITER);
    }
    if (keepTool) {
        DbgFlagOn(pG, DBG_TEST_MODE);
    }
    return bad;
}

// The picture format "4:3 without bars" (patch 0105, runtime/picture.cpp): open only with the
// switch on, in the main loop, without sub screen and event. Bits: each failed case.
extern "C" int re4probe_letterbox(void)
{
    const u8 keepStep = pG->Rno0;
    const int keepSub = StaFlagChk(pG, STA_SUB_SCRN) != 0, keepEvent = StaFlagChk(pG, STA_EVENT) != 0;
    int bad = 0;
    pG->Rno0 = 3;
    StaFlagOff(pG, STA_SUB_SCRN);
    StaFlagOff(pG, STA_EVENT);
    re4_core_set_letterbox(0);
    bad |= re4_letterbox_open() ? 1 : 0;
    re4_core_set_letterbox(1);
    bad |= re4_letterbox_open() ? 0 : 2;
    StaFlagOn(pG, STA_EVENT);
    bad |= re4_letterbox_open() ? 4 : 0;
    StaFlagOff(pG, STA_EVENT);
    StaFlagOn(pG, STA_SUB_SCRN);
    bad |= re4_letterbox_open() ? 8 : 0;
    StaFlagOff(pG, STA_SUB_SCRN);
    pG->Rno0 = 2;
    bad |= re4_letterbox_open() ? 0x10 : 0;
    re4_core_set_letterbox(0);
    pG->Rno0 = keepStep;
    if (keepSub) {
        StaFlagOn(pG, STA_SUB_SCRN);
    }
    if (keepEvent) {
        StaFlagOn(pG, STA_EVENT);
    }
    return bad;
}

extern "C" void re4_core_gyro(float yaw, float pitch);

// Gyro aiming with the harpoon in the boat of the Del Lago fight (runtime/gyro.cpp): a fake Leon
// aiming it (pl0f routine 0/0xF/3, step 3) on a fake boat; the yaw moves the sight across
// (m_BoatPlDir, positive left), the pitch up (m_Blend), the boat turns at the sight's limit; not
// while he only stands up to aim (step 1), in the mouth chance's aim (0xA, step 3) again. Bits:
// each failed case.
extern "C" int re4probe_gyro_boat(void)
{
    cPlayer* keepPl = pPL;
    const u8 keepStep = pG->Rno0;
    const int keepSub = StaFlagChk(pG, STA_SUB_SCRN) != 0, keepEvent = StaFlagChk(pG, STA_EVENT) != 0;

    cPlayer* pl = (cPlayer*) (unsigned long) 0x82E40000ul;
    memset((void*) pl, 0, sizeof(cPlayer));
    cEm* boat = (cEm*) (unsigned long) 0x82E20000ul;
    memset((void*) boat, 0, sizeof(cEm));
    pl->m_pBoat = boat;
    pl->r_no_0 = 0;
    pl->r_no_1 = 0xF;
    pl->r_no_2 = 3;
    pl->r_no_3 = 3;
    pPL = pl;
    pG->Rno0 = 3;
    StaFlagOff(pG, STA_SUB_SCRN);
    StaFlagOff(pG, STA_EVENT);

    PADStatus pads[4];
    int bad = 0;
    auto near = [](float a, float b) { return a - b < 1e-4f && b - a < 1e-4f; };
    const float step = 0.35f / 30.0f;  // the gain per frame and rad/s

    // turned 1 rad/s left and 0.5 rad/s up for one frame
    re4_core_gyro(1.0f, 0.5f);
    PADRead(pads);
    bad |= !near(pl->m_BoatPlDir, step) ? 1 : 0;
    bad |= !near(pl->m_Blend / 1251.6454f, 0.5f * step) ? 2 : 0;
    bad |= boat->ang.y != 0.0f ? 4 : 0;
    // far to the right: the sight stops at its limit, the boat turns on
    for (int i = 0; i < 60; i++) {
        re4_core_gyro(-3.0f, 0.0f);
        PADRead(pads);
    }
    bad |= !near(pl->m_BoatPlDir, -0.3926991f) ? 8 : 0;
    bad |= !(boat->ang.y < -0.5f) ? 0x10 : 0;
    // far up: m_Blend at 255
    for (int i = 0; i < 60; i++) {
        re4_core_gyro(0.0f, 3.0f);
        PADRead(pads);
    }
    bad |= pl->m_Blend != 255.0f ? 0x20 : 0;
    // standing up to aim (step 1): nothing moves
    pl->r_no_3 = 1;
    const float dir = pl->m_BoatPlDir, blend = pl->m_Blend;
    re4_core_gyro(1.0f, -1.0f);
    PADRead(pads);
    bad |= pl->m_BoatPlDir != dir || pl->m_Blend != blend ? 0x40 : 0;
    // the mouth chance's aim (0xA, step 3): moves again
    pl->r_no_2 = 0xA;
    pl->r_no_3 = 3;
    re4_core_gyro(1.0f, -1.0f);
    PADRead(pads);
    bad |= !(pl->m_BoatPlDir > dir) || !(pl->m_Blend < blend) ? 0x80 : 0;
    // a sub screen open: nothing
    StaFlagOn(pG, STA_SUB_SCRN);
    const float dir2 = pl->m_BoatPlDir;
    re4_core_gyro(1.0f, 0.0f);
    PADRead(pads);
    bad |= pl->m_BoatPlDir != dir2 ? 0x100 : 0;
    StaFlagOff(pG, STA_SUB_SCRN);
    re4_core_gyro(0.0f, 0.0f);

    pPL = keepPl;
    pG->Rno0 = keepStep;
    if (keepSub) {
        StaFlagOn(pG, STA_SUB_SCRN);
    }
    if (keepEvent) {
        StaFlagOn(pG, STA_EVENT);
    }
    return bad;
}

extern "C" void re4_core_slash(float tilt);
extern "C" void re4_core_slash(float tilt);

// The knife by a swipe (runtime/knife_swipe.cpp): the buttons it adds to the pad (pad.cpp
// re4_pad_add) frame by frame, while the probe moves a fake Leon through the knife routine the way
// pl_knife.cpp would. Bits: each failed case.
extern "C" int re4probe_knife_swipe(void)
{
    const u16 kL = 0x0040, kLA = 0x0140;
    cPlayer* keepPl = pPL;
    const u8 keepStep = pG->Rno0, keepType = pG->pl_type;
    const int keepSub = StaFlagChk(pG, STA_SUB_SCRN) != 0, keepEvent = StaFlagChk(pG, STA_EVENT) != 0;
    const int keepKnifeKey = pSys && CfgFlagChk(pSys, CFG_KNIFE_MODE);
    const f32 keepM3r = m3r;

    cPlayer* pl = (cPlayer*) (unsigned long) 0x82E40000ul;
    memset((void*) pl, 0, sizeof(cPlayer));
    cPlWep* wep = (cPlWep*) (unsigned long) 0x82E30000ul;
    memset((void*) wep, 0, sizeof(cPlWep));
    pl->Wep = wep;
    pPL = pl;
    pG->Rno0 = 3;
    pG->pl_type = 0;
    StaFlagOff(pG, STA_SUB_SCRN);
    StaFlagOff(pG, STA_EVENT);
    if (pSys) {
        CfgFlagOff(pSys, CFG_KNIFE_MODE);
    }
    m3r.setDelay(0.0f);
    m3r.reset(0.0f);

    PADStatus pads[4];
    int bad = 0;
    auto frame = [&]() -> u16 {
        PADRead(pads);
        return pads[0].button;
    };

    // a swipe in a sub screen: dropped, not kept for later
    StaFlagOn(pG, STA_SUB_SCRN);
    re4_core_slash(0.0f);
    bad |= frame() != 0 ? 1 : 0;
    StaFlagOff(pG, STA_SUB_SCRN);
    bad |= frame() != 0 ? 1 : 0;

    // standing (0/0), a swipe straight down: L until the knife is out (the trigger pressed fully)
    re4_core_slash(-1.0f);
    bad |= frame() != kL || pads[0].triggerLeft != 0xFF ? 2 : 0;
    pl->r_no_1 = 0xB;  // the game draws the knife
    bad |= frame() != kL ? 2 : 0;
    // set: L and A, the slash aimed 45 degrees down
    pl->r_no_2 = 1;
    pl->r_no_3 = 4;
    bad |= frame() != kLA ? 4 : 0;
    bad |= (f32) m3r > -0.99f || wep->pitch > -1.5f ? 8 : 0;
    // the slash swings: L alone
    pl->r_no_2 = 2;
    bad |= frame() != kL ? 0x10 : 0;
    bad |= frame() != kL ? 0x10 : 0;
    // a second, almost level swipe while it swings: slashes again once it ends, level
    re4_core_slash(0.2f);
    bad |= frame() != kL ? 0x20 : 0;
    m3r.reset(0.0f);
    wep->pitch = 0.0f;
    pl->r_no_2 = 1;
    bad |= frame() != kLA || (f32) m3r != 0.0f ? 0x20 : 0;
    pl->r_no_2 = 2;
    bad |= frame() != kL ? 0x20 : 0;
    // the swing ends: L ten frames more, then nothing (the game puts the knife away)
    pl->r_no_2 = 1;
    for (int i = 0; i < 10; i++) {
        bad |= frame() != kL ? 0x40 : 0;
    }
    bad |= frame() != 0 ? 0x40 : 0;

    // the knife already out and set (L held on the controller): L and A at once; the player hurt
    // (routine 1): the buttons stop at once
    re4_core_slash(0.0f);
    bad |= frame() != kLA ? 0x80 : 0;
    pl->r_no_0 = 1;
    bad |= frame() != 0 ? 0x80 : 0;

    // aiming a gun (0/6): ignored
    pl->r_no_0 = 0;
    pl->r_no_1 = 6;
    pl->r_no_2 = 0;
    re4_core_slash(0.0f);
    bad |= frame() != 0 ? 0x100 : 0;
    // Ashley, the game's knife key option: ignored
    pl->r_no_1 = 0;
    pG->pl_type = 1;
    re4_core_slash(0.0f);
    bad |= frame() != 0 ? 0x200 : 0;
    pG->pl_type = 0;
    if (pSys) {
        CfgFlagOn(pSys, CFG_KNIFE_MODE);
        re4_core_slash(0.0f);
        bad |= frame() != 0 ? 0x200 : 0;
        CfgFlagOff(pSys, CFG_KNIFE_MODE);
    }

    // the game never draws the knife: L for 45 frames, then nothing
    re4_core_slash(0.0f);
    int held = 0;
    for (int i = 0; i < 60; i++) {
        held += frame() == kL;
    }
    bad |= held != 45 ? 0x400 : 0;

    pPL = keepPl;
    pG->Rno0 = keepStep;
    pG->pl_type = keepType;
    if (keepSub) {
        StaFlagOn(pG, STA_SUB_SCRN);
    }
    if (keepEvent) {
        StaFlagOn(pG, STA_EVENT);
    }
    if (keepKnifeKey) {
        CfgFlagOn(pSys, CFG_KNIFE_MODE);
    }
    m3r.reset(keepM3r);
    return bad;
}

extern "C" void re4_qte_tap(void);
extern "C" unsigned short re4_qte_buttons(int disp);

// Quick-time events by a tap (runtime/qte_touch.cpp): the buttons a tap adds to the pad while the
// cockpit shows a prompt, for two frames; nothing for the plain A prompt, in a sub screen or with no
// prompt active. Bits: each failed case.
extern "C" int re4probe_qte_touch(void)
{
    const int keepSub = StaFlagChk(pG, STA_SUB_SCRN) != 0;
    const int keepActive = ActBtn.isActive();
    const u8 keepShown = *reinterpret_cast<const u8*>(&Cckpt.m_ActBttn);
    PADStatus pads[4];
    int bad = 0;
    auto frame = [&]() -> u16 {
        PADRead(pads);
        return pads[0].button;
    };
    StaFlagOff(pG, STA_SUB_SCRN);
    ActBtn.setActive(1);

    // the dodge (L+R): both for two frames, then nothing
    Cckpt.m_ActBttn.setDispFlag(DISP_L_R);
    re4_qte_tap();
    bad |= frame() != 0x0060 ? 1 : 0;
    bad |= frame() != 0x0060 ? 1 : 0;
    bad |= frame() != 0 ? 1 : 0;
    // A+B, and a mash: each tap one press of A
    Cckpt.m_ActBttn.setDispFlag(DISP_A_B);
    re4_qte_tap();
    bad |= frame() != 0x0300 ? 2 : 0;
    frame();
    Cckpt.m_ActBttn.setDispFlag(DISP_A_RAPID);
    int presses = 0;
    for (int i = 0; i < 3; i++) {
        re4_qte_tap();
        u16 last = 0;
        for (int f = 0; f < 3; f++) {
            const u16 b = frame();
            presses += b == 0x0100 && last != 0x0100;
            last = b;
        }
    }
    bad |= presses != 3 ? 4 : 0;
    // a fast mash: a tap in every frame (and two in one) still gives one press each, one frame
    // pressed and one released, instead of one long press (r226, 25.10.2026)
    presses = 0;
    {
        u16 last = 0;
        for (int f = 0; f < 12; f++) {
            if (f < 3) {
                re4_qte_tap();
            }
            if (f == 1) {
                re4_qte_tap();  // two taps before one frame
            }
            const u16 b = frame();
            presses += b == 0x0100 && last != 0x0100;
            last = b;
        }
    }
    bad |= presses != 4 ? 0x80 : 0;
    // the plain A prompt (a door): nothing
    Cckpt.m_ActBttn.setDispFlag(DISP_A_NORMAL);
    re4_qte_tap();
    bad |= frame() != 0 ? 8 : 0;
    // a sub screen, no prompt: nothing
    Cckpt.m_ActBttn.setDispFlag(DISP_L_R);
    StaFlagOn(pG, STA_SUB_SCRN);
    re4_qte_tap();
    bad |= frame() != 0 ? 0x10 : 0;
    StaFlagOff(pG, STA_SUB_SCRN);
    ActBtn.setActive(0);
    re4_qte_tap();
    bad |= frame() != 0 ? 0x20 : 0;
    // the table
    bad |= re4_qte_buttons(DISP_B_RAPID) != 0x0200 || re4_qte_buttons(DISP_X) != 0x0400 ||
                   re4_qte_buttons(DISP_GACHA) != 0x0100 || re4_qte_buttons(DISP_STICK_UP) != 0 ||
                   re4_qte_buttons(DISP_OFF) != 0
               ? 0x40
               : 0;

    Cckpt.m_ActBttn.setDispFlag(keepShown);
    ActBtn.setActive(keepActive);
    if (keepSub) {
        StaFlagOn(pG, STA_SUB_SCRN);
    }
    return bad;
}
