// The cheat "Bloody mode" (1.1.3, the project owner: "Blutlachen auf dem Boden bleiben liegen, statt nach ein paar
// Sekunden zu verschwinden"; patch 0258, esp10.cpp). The floor blood of the game is the ground decal sprite (effect id
// 0x10, cEsp10): dropped onto the floor at its creation, it grows (m_Size_plus), holds and then fades (ColorUpdate:
// m_Col_d_* each frame after m_Col_max_cnt + m_Col_start_cnt frames, released below alpha 4), or ends with its life
// (m_Life_max) or its texture animation. With the switch on, a blood decal (bloodDecal: made by the effects of the
// game's blood helpers of a hit or a death, re4_bloody_rec; looked at in its first frame) is kept: no fade, no end of
// life, its animation holds the last picture. The room's next load clears the sprite pool (EspRoomInit) and with it the
// pools; pools kept stay when the switch goes off (new ones fade as in the game).
// The pool of sprites holds CONS_R_NESP (1024) for every effect of a room; so that muzzle flashes, sparks and the rest
// never run short: at most 160 pools are kept (cap(); the oldest released first when a new one comes), fewer while the pool
// has less than kReserve free sprites, and each kept pool carries the game's own low-priority mark (Tool_flg 0x40000:
// cEsp::operator new takes such a sprite when the pool is full) as a last resort.
// RE4_BLOODY_LOG=1 (PC): one line for each kind of floor decal made (owner, texture, colour, envelope, kept or not);
// RE4_TEST_BLOODY=1 (PC): the switch on from the start.
#include "types.h"
#include "global.h"
#include "esp.h"
#include "espgen.h"

#include <atomic>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_potato_mask(void);  // runtime/potato.cpp
extern GLOBAL_WK* pG;

namespace {
const u8 kMark = 0xB6;        // cEsp::xF3 (unused by the game; the pool clears a slot when it hands it out again): kept
const u8 kSeen = 0xB7;        // ... looked at, not kept
const u8 kBlood = 0xB8;       // ... blood, not looked at yet (re4_bloody_rec)
const u8 kBloodCtl = 0xB5;    // ESPSEQ_CONTROL::pad of the blood helpers' block (runtime/berserker.cpp)
const int kMaxKept = 256;     // the ring's room (a quarter of the sprite pool)
const u32 kReserve = 256;     // free sprites left for the other effects: below that the oldest pool goes
const u32 kRoundOff = 0x40000;  // Tool_flg: the pool may take this sprite when it is full

std::atomic<int> gOn(-1);
cEsp* gKept[kMaxKept];        // the oldest first (a ring)
int gHead, gCount;
unsigned gMade, gReleased, gLines;
int gLog = -1;

bool logOn()
{
    if (gLog < 0) {
        const char* v = getenv("RE4_BLOODY_LOG");
        gLog = v && strcmp(v, "1") == 0;
    }
    return gLog != 0;
}

bool on()
{
    int v = gOn.load(std::memory_order_relaxed);
    if (v < 0) {
        const char* t = getenv("RE4_TEST_BLOODY");
        v = t && strcmp(t, "1") == 0 ? 1 : 0;
        int expected = -1;
        gOn.compare_exchange_strong(expected, v);
        v = gOn.load(std::memory_order_relaxed);
    }
    return v > 0;
}

// The sprite pool the entries point into: a room's load makes a new one (EspRoomInit allocates the effect system and
// its pool anew), so the ring is emptied when it changes (the pools of before went with the old pool).
const u8* gArray;
const u8* gArrayEnd;

void sync()
{
    const u8* array = g_pEspSys ? g_pEspSys->EspArray : 0;
    const u8* end = array ? array + g_pEspSys->nEsp * 0x150 : 0;
    if (array != gArray || end != gArrayEnd) {
        if (gCount > 0) {
            re4_log("bloody: a new sprite pool (the room changed): %d kept pools of before forgotten", gCount);
        }
        gArray = array;
        gArrayEnd = end;
        gHead = gCount = 0;
    }
}

bool alive(cEsp* e)
{
    const u8* at = (const u8*) e;
    return e && gArray && at >= gArray && at < gArrayEnd && (e->m_Be_flg & 1) && e->m_Id == 0x10 && e->xF3 == kMark;
}


// A decal of the game's blood: made by the effects of a blood helper (re4_bloody_rec marked it; the same textures and
// sequences also make dust, dirt and footprints, so neither the texture nor the colour tells blood).
bool bloodDecal(const cEsp* e)
{
    return e->xF3 == kBlood;
}

// Pools kept at most: each kept pool in sight is one more draw (on the PC 230 pools took a frame from 308 draws to 538,
// 1.9 to 2.6 ms): 160, with the app's Potato mode "Effects: less" 64 (it draws every second sprite anyway), "none" 32.
int cap()
{
    switch ((re4_potato_mask() >> 2) & 3) {
    case 0:
        return 160;
    case 1:
        return 64;
    default:
        return 32;
    }
}

// Drops the entries the game released (or a new room cleared) from the front and the middle of the ring, and an entry of
// `fresh` (its slot handed out again: a new room's pool may lie where the old one lay).
void compact(const cEsp* fresh)
{
    int n = 0;
    for (int i = 0; i < gCount; i++) {
        cEsp* e = gKept[(gHead + i) % kMaxKept];
        if (e != fresh && alive(e)) {
            gKept[(gHead + n) % kMaxKept] = e;
            n++;
        }
    }
    gCount = n;
}

// Releases the oldest kept pool (the game's own PushEsp).
void releaseOldest(const char* why)
{
    while (gCount > 0) {
        cEsp* e = gKept[gHead];
        gHead = (gHead + 1) % kMaxKept;
        gCount--;
        if (alive(e)) {
            e->xF3 = 0;
            PushEsp(e);
            if (gReleased++ % 200 == 0) {
                re4_log("bloody: the oldest pool released (%s; %u so far, %d kept)", why, gReleased, gCount);
            }
            return;
        }
    }
}
}  // namespace

extern "C" {

// The app (CoreBridge.setAim bit 23 via runtime/aim_options.cpp): the switch; any thread.
void re4_bloody_set(int on)
{
    const int was = gOn.exchange(on ? 1 : 0);
    if (was != (on ? 1 : 0)) {
        re4_log("cheat: bloody mode %s", on ? "on (blood pools stay until the room changes)" : "off (pools fade as in the game)");
    }
}

int re4_bloody_on(void)
{
    return on() ? 1 : 0;
}

}  // extern "C"

namespace {
// A decal's first frame (its colour final then; an effect's control block may change it after SetFreeWork): keeps it
// when it is blood and the switch is on.
void decide(cEsp* e)
{
    const bool blood = bloodDecal(e);
    static u32 seenKey[96];
    const u32 key = (u32) e->info.owner << 24 | (u32) e->m_Tex_id << 16 | (u32) e->m_Col_start_r << 8 | e->m_Col_start_g;
    bool fresh = true;
    for (unsigned i = 0; i < gLines && i < 96; i++) {
        fresh = fresh && seenKey[i] != key;
    }
    if (logOn() && fresh && gLines < 96) {
        seenKey[gLines] = key;
        cAnm* anm = 0;
        EspGetAnmAddr(e->m_Tex_id, &anm);
        gLines++;
        re4_log("bloody: decal owner %d tex %d col %d %d %d %d d %.3f %.3f blend %d life %d col_cnt %d+%d size %.3f+%.3f*%.3f "
                "anm %d/%d loop %d -> %s",
                e->info.owner, e->m_Tex_id, e->m_Col_start_r, e->m_Col_start_g, e->m_Col_start_b, e->m_Col_start_a,
                (double) e->m_Col_d_r, (double) e->m_Col_d_a, e->m_Blend_type, e->m_Life_max, e->m_Col_max_cnt,
                e->m_Col_start_cnt, (double) e->m_Size_mul, (double) e->m_Size_plus, (double) e->m_D_size_plus,
                anm ? (int) anm->Frames : -1, e->m_Anm_rate, anm ? (anm->Loop & 3) : -1,
                !blood ? "not blood" : on() ? "kept" : "blood, the switch off");
    }
    e->xF3 = kSeen;
    if (!blood || !on() || !(e->m_Be_flg & 1)) {
        return;
    }
    sync();
    compact(e);
    while (gCount >= cap() || gCount >= kMaxKept) {
        releaseOldest("the most pools kept");
    }
    const u32 used = g_pEspSys ? g_pEspSys->ActiveEspNum : 0, all = g_pEspSys ? g_pEspSys->nEsp : 0;
    while (gCount > 0 && all > 0 && used < all && all - used < kReserve) {
        const u32 before = g_pEspSys->ActiveEspNum;
        releaseOldest("few free sprites");
        if (g_pEspSys->ActiveEspNum >= before) {
            break;
        }
        if (all - g_pEspSys->ActiveEspNum >= kReserve) {
            break;
        }
    }
    e->xF3 = kMark;
    e->m_Tool_flg |= kRoundOff;
    // no end: the envelope holds (the colour's per-frame factors 1 once it would fade), no end of life
    e->m_Col_d_r = e->m_Col_d_g = e->m_Col_d_b = e->m_Col_d_a = 1.0f;
    e->m_Life_max = 0;
    if (e->m_Size_plus < 0.0f) {
        e->m_Size_plus = 0.0f;  // a pool that would shrink away keeps its size
    }
    gKept[(gHead + gCount) % kMaxKept] = e;
    gCount++;
    if (gMade++ % 100 == 0) {
        re4_log("bloody: blood pool kept (%u so far, %d kept now, %u of %u sprites in use)", gMade, gCount, used, all);
    }
}

}  // namespace

extern "C" {

// Patch 0258 (esp_sub.cpp EspSeqSet, in place of re4_berserker_blood_rec): a sprite made under a control block; a floor
// decal under the blood helpers' block (runtime/berserker.cpp: Berserker's, or the neutral one of "Bloody mode") is blood.
// Then as re4_berserker_blood_rec: whether the block applies.
int re4_berserker_blood_rec(ESPSEQ_CONTROL* pSct, int id);
int re4_bloody_rec(ESPSEQ_CONTROL* pSct, cEsp* e)
{
    if (pSct && pSct->pad == kBloodCtl && e->m_Id == 0x10 && e->xF3 == 0) {
        e->xF3 = kBlood;
    }
    return re4_berserker_blood_rec(pSct, e->m_Id);
}

// Patch 0258 (cEsp10::move): a decal's first frame decides (decide); a kept pool moves as the game's (CommonMove:
// fade-in, growth) without an end, its animation holds the last picture. 1: done here, 0: the game's own move.
int re4_bloody_move(cEsp* e)
{
    if (e->xF3 == 0 || e->xF3 == kBlood) {
        decide(e);
    }
    if (e->xF3 != kMark) {
        return 0;
    }
    if (e->m_Life_time > 60000) {
        e->m_Life_time = 60000;  // (u16: no wrap back into the fade-in)
    }
    if (!e->CommonMove()) {
        return 1;  // (released by the game's own rules: a zero size)
    }
    if (!e->AnmMove()) {
        cAnm* anm = 0;
        if (EspGetAnmAddr(e->m_Tex_id, &anm) && anm && anm->Frames > 0) {
            e->m_Ptn_no = (u8) (anm->Frames - 1);
            e->m_Anm_cnt = 0;
        }
    }
    return 1;
}

}  // extern "C"
