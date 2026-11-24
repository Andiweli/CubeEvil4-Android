// The difficulty "Berserker" (1.1.2, wish and decisions of the project owner, 24.11.2026: "sollte automatisch +40% mehr
// Gegner haben und sie sollen auch rund 25% schwerer zu töten sein"; a 4th row BERSERKER above Professional in the
// game's own difficulty menu, unlocked with Professional; animals keep their toughness; waves unchanged; not with the
// Japanese discs), compiled with the game's flags into libre4_main; patch 0255 asks it.
//
// Berserker is Professional (game_mode 6 stays: everything the game and the port do for Professional, the rank fixed at
// 10, the save list's picture) plus two boosts:
//   - "More enemies" (runtime/enemy_random.cpp) always on, its seed from the save, so a save gets the same copies:
//     40 % more of a room's ordinary Ganados at its start; bosses, waves and the extra games as there. With the app's
//     switch on too it stays one +40 % (Berserker's seed);
//   - enemies take 0.8 of the damage (LifeDownSet2, after the game's own rank factor, Professional 0.85): 1.25x the
//     hits of Professional. Only the ids the game itself scales there (Ganados, bosses, Plaga dogs 0x22, parasites
//     0x25 ...); left out the animals (dog 0x21, crows 0x23, snakes 0x24, cows 0x26, chickens 0x28, bats 0x29; the bass
//     0x27 is not among them). Leon, Ashley and Luis take what they take on Professional. One-hit kill still wins.
//
// The save keeps it in the three bytes after game_mode (GLOBAL_WK 0x8355..0x8357, the compiler's padding before
// SaveKind, in the saved block): 0xB5 and a 16-bit seed. No code of the debug build or the European, American or
// Japanese releases reads or writes them (every load and store at their offsets in main.dol and the RELs, checked
// 24.11.2026). A version without Berserker, Dolphin or a GameCube loads such a save as Professional. Patch 0255 keeps
// them through a new round (clearGlobalSaveData) like game_mode.
//
// The menu (titleLevelInit / titleLevelSelect, debug and American discs, the European menu of patch 0139; once
// Professional is unlocked, EXT_HARD_MODE): a copy of Professional's two units (the orange one the cursor shows and the
// light word) one row higher (the rows' own distance, 20), drawn with the texture of runtime/berserker_word.inc (the
// word in DejaVu Serif Bold, port/tools/berserker_word.py; nothing of the game) under a texture id the title does not
// use: IdTexSet and idSysMove04 ask re4_berserker_tex for it. The cursor's row 0 is then Berserker, 1 Professional ...
//
// The save list (the project owner's wish of 24.11.2026, "Eigenes Zeichen BZK", then "BSK"): a Berserker save shows BSK in the
// Mode column instead of PRO. The list reads only the save's header, whose byte 0x3F is GLOBAL_WK 0x4F8B (padding no
// release reads or writes): 0xB5 there for a Berserker game (set by re4_berserker_game_init), and card.cpp dispSaveInfo
// (patch 0255) asks re4_berserker_list_mark, which shows a copy of the PRO unit with the picture BSK (DejaVu Serif Bold,
// berserker_word.inc). An older version shows PRO.
//
//   RE4_TEST_BERSERKER=1   (the PC checks) a new game is Berserker (game_mode 6 with the marker), as if chosen, with
//                          the seed 0x4242 (the same copies every run)
//   RE4_TEST_BLOOD_OFF=1   (the PC checks) Berserker without the bigger blood (patch 0256), for a comparison
//   RE4_BERSERKER_LOG=1    (the PC checks) one line for every hit on an enemy the game scales: before and after
#include "types.h"
#include "global.h"
#include "id_sys.h"
#include "texture.h"
#include "gx.h"
#include "espgen.h"
#include "em.h"
#include "re4_mem1_map.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void re4_log(const char* fmt, ...);
extern "C" int re4_disc_retail_eu(void);  // runtime/dvd.cpp
extern "C" int re4_disc_retail_jp(void);  // runtime/dvd.cpp
extern GLOBAL_WK* pG;
extern SYSTEM_SAVE_WORK* pSys;
extern cTexSys* g_pIdTexSys;

namespace {

#include "berserker_word.inc"

const u8 kMagic = 0xB5;

// The two pictures (berserker_word.inc): the menu's word BERSERKER and the save list's mark BSK. Their texels lie in
// the cheats' place of MEM1 (re4_mem1_map.h; GX reads textures there like the game's own), each under a texture id
// the id texture system does not hold.
struct Pic {
    const unsigned char* data;
    unsigned bytes;
    int w, h;
    u32 addr;
    u8 id;  // 0: none yet
    GXTexObj tex;
};
Pic gPics[2] = {{kWord, sizeof(kWord), kWordW, kWordH, RE4_MEM1_CHEAT_ADDR + 0x4F0000u, 0, {}},
                {kMark, sizeof(kMark), kMarkW, kMarkH, RE4_MEM1_CHEAT_ADDR + 0x4F8000u, 0, {}}};
static_assert(sizeof(kWord) <= 0x8000u && 0x4F8000u + sizeof(kMark) <= RE4_MEM1_CHEAT_MAX,
              "the pictures' places run past the cheats' places");
enum { kPicWord = 0, kPicMark = 1 };

int gChosen;            // the title's choice for the next new game: 1 Berserker
ID_UNIT* gRow[2];       // the copies: [0] the cursor's (orange), [1] the word
int gTest = -1;

bool testOn()
{
    if (gTest < 0) {
        const char* v = getenv("RE4_TEST_BERSERKER");
        gTest = v && strcmp(v, "1") == 0;
    }
    return gTest != 0;
}

bool logOn()
{
    static int on = -1;
    if (on < 0) {
        const char* v = getenv("RE4_BERSERKER_LOG");
        on = v && strcmp(v, "1") == 0;
    }
    return on != 0;
}

bool extraGame()
{
    return SysFlagChk(pG, SYS_OMAKE_ADA_GAME) || SysFlagChk(pG, SYS_OMAKE_ETC_GAME);
}

int seedOf()
{
    return pG->pad_8355[1] << 8 | pG->pad_8355[2];
}

// a texture id the id texture system does not hold and the other picture does not use, from the top (0xFF: none)
u8 freeTexId(const Pic& self)
{
    for (int id = 0xFE; id > 0; id--) {
        bool other = false;
        for (const Pic& p : gPics) {
            other = other || (&p != &self && p.id == id);
        }
        if (!other && !IdGetTexWk((u8) id, 1)) {
            return (u8) id;
        }
    }
    return 0;
}

// the picture's texture id, made the first time (or anew when the game took its id since); 0 when none is free
u8 picId(int which)
{
    Pic& p = gPics[which];
    if (p.id && !IdGetTexWk(p.id, 1)) {
        return p.id;
    }
    p.id = freeTexId(p);
    if (!p.id) {
        re4_log("berserker: no free texture id");
        return 0;
    }
    memcpy((void*) (uintptr_t) p.addr, p.data, p.bytes);
    GXInitTexObj(&p.tex, (void*) (uintptr_t) p.addr, p.w, p.h, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GXInitTexObjLOD(&p.tex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    return p.id;
}

ID_UNIT* copyOf(IDSystem* sys, const ID_UNIT* from, u8 mark, u8 texId, int w, int h)
{
    ID_UNIT* u = sys->unitPull();
    if (!u) {
        return 0;
    }
    memcpy(u, from, sizeof(ID_UNIT));
    u->markNo = mark;
    u->unitNo = mark;
    u->texId = texId;
    u->texNo = 0;
    u->tex_ptn_no = 0;
    u->tex_flag |= 2;  // no texture animation
    u->size_W = (f32) w;
    u->size_H = (f32) h;
    u->u0 = 0.0f;
    u->u1 = 1.0f;
    u->v0 = 0.0f;
    u->v1 = 1.0f;
    return u;
}

}  // namespace

extern "C" {

// Berserker plays: Professional with the marker, in the story (not Ada's game or the Mercenaries)
int re4_berserker_on(void)
{
    return pG && pG->game_mode == 6 && pG->pad_8355[0] == kMagic && seedOf() != 0 && !extraGame();
}

// "More enemies"' seed while Berserker plays (enemy_random.cpp moreNow), else 0
int re4_berserker_more_seed(void)
{
    return re4_berserker_on() ? (seedOf() << 4 | 0xB) & 0xFFFFF : 0;
}

// Patch 0255 (em_sub.cpp LifeDownSet2): the damage an enemy of `id` takes, after the game's rank factor; `hp` its life
// before the hit, `em` the enemy (RE4_BERSERKER_LOG=1, the PC check: one line a hit)
int re4_berserker_damage(int id, int dm_val, int hp, const void* em)
{
    int out = dm_val;
    if (dm_val > 0 && re4_berserker_on()) {
        switch (id) {
        case 0x21: case 0x23: case 0x24: case 0x26: case 0x28: case 0x29:  // the animals
            break;
        default:
            out = (dm_val * 4 + 4) / 5;  // 0.8, rounded up: a hit takes at least 1
            break;
        }
    }
    if (logOn()) {
        re4_log("berserker: hit %p id 0x%02x rank %d: %d -> %d, life %d -> %d", em, id, pG ? pG->Game_level : -1,
                dm_val, out, hp, hp - out);
    }
    return out;
}

// Patch 0255 (title.cpp titleLevelInit): the BERSERKER row above Professional, while Professional shows. `menu`
// holds the rows' cursor units from the top, `num` their count; returns the new count (the row first) or `num`.
int re4_berserker_menu_init(ID_UNIT** menu, int num)
{
    gChosen = 0;
    gRow[0] = gRow[1] = 0;
    if (!pG || re4_disc_retail_jp() || !ExtFlagChk(pSys, EXT_HARD_MODE) || num < 2 || num > 4) {
        return num;
    }
    ID_UNIT* pro = IdSys.unitPtr(1, IDC_TITLE_MENU);   // Professional: 1 the cursor's, 2 the word
    ID_UNIT* word = IdSys.unitPtr(2, IDC_TITLE_MENU);
    ID_UNIT* normal = IdSys.unitPtr(3, IDC_TITLE_MENU);
    if (menu[0] != pro || pro->be_flag == 0xFF || word->be_flag == 0xFF || normal->be_flag == 0xFF) {
        return num;
    }
    const u8 texId = picId(kPicWord);
    if (!texId) {
        return num;
    }
    const f32 step = pro->pos0.y - normal->pos0.y;  // the rows' distance (20)
    // the game draws its word pictures at their own scale (the units' size against their texels): the same for ours
    TEX_WK* wk = word->texId != 0xFF ? IdGetTexWk(word->texId, 1) : 0;
    const f32 sx = wk && wk->texHdr->width ? word->size_W / wk->texHdr->width : 1.0f;
    const f32 sy = wk && wk->texHdr->height ? word->size_H / wk->texHdr->height : 1.0f;
    re4_log("berserker: Professional's word: texture %ux%u, drawn %.1fx%.1f", wk ? wk->texHdr->width : 0,
            wk ? wk->texHdr->height : 0, word->size_W, word->size_H);
    gRow[0] = copyOf(&IdSys, pro, 0x21, texId, kWordW, kWordH);
    gRow[1] = copyOf(&IdSys, word, 0x22, texId, kWordW, kWordH);
    for (ID_UNIT* u : gRow) {
        if (u) {
            u->size_W = kWordW * sx;
            u->size_H = kWordH * sy;
        }
    }
    if (!gRow[0] || !gRow[1]) {
        re4_log("berserker: the id system is full, no menu row");
        if (gRow[0]) IdSys.unitPush(gRow[0]);
        gRow[0] = gRow[1] = 0;
        return num;
    }
    for (ID_UNIT* u : gRow) {
        u->pos0.y += step;
        u->pos.y += step;
    }
    gRow[0]->be_flag &= ~8;  // the cursor starts on Normal
    gRow[1]->be_flag |= 8;
    for (int i = num; i > 0; i--) {
        menu[i] = menu[i - 1];
    }
    menu[0] = gRow[0];
    re4_log("berserker: menu row above Professional (texture id %02x, %dx%d, %d rows)", texId, kWordW, kWordH, num + 1);
    return num + 1;
}

// Patch 0255 (titleLevelSelect): whether the row shows (the cursor's 0 is Berserker)
int re4_berserker_row(void)
{
    return gRow[0] != 0;
}

// Patch 0255: the title chose a difficulty (1 Berserker)
void re4_berserker_title_choice(int berserker)
{
    gChosen = berserker ? 1 : 0;
    if (berserker) {
        re4_log("berserker: chosen in the title menu");
    }
}

// Patch 0255 (card.cpp dispSaveInfo): the save list's Mode column of the entry of id class `type` (header bytes
// `mode` 0x3D, the copy of game_mode, and `mark` 0x3F, GLOBAL_WK's pad_4F8B: 0xB5 for a Berserker save): BSK instead of
// the game's mode picture (units 0x17, 0x18, 0x22), as a copy of unit 0x18 (mark 0x30) with the picture of
// berserker_word.inc; `broken` or another save: the copy hidden
void re4_berserker_list_mark(IDSystem* id, int type, int mode, int mark, int broken)
{
    const int max = *(const s32*) id;  // m_maxId (private, the first word)
    ID_UNIT* copy = 0;
    for (int i = 0; i < max && id->m_IdUnit; i++) {
        ID_UNIT* u = &id->m_IdUnit[i];
        if (u->be_flag != 0xFF && u->classNo == (u8) type && u->markNo == 0x30) {
            copy = u;
            break;
        }
    }
    const bool bz = mark == kMagic && mode == 6 && !broken;
    if (!bz) {
        if (copy) {
            copy->be_flag &= ~8;
        }
        return;
    }
    if (!copy) {
        ID_UNIT* pro = id->unitPtr(0x18, (ID_CLASS) type);
        const u8 texId = picId(kPicMark);
        // the game draws its picture somewhat taller than its texels (PRO: 38x21 texels at 38x23): the same scale
        TEX_WK* wk = pro->texId != 0xFF ? IdGetTexWk(pro->texId, 1) : 0;
        const f32 sx = wk && wk->texHdr->width ? pro->size_W / wk->texHdr->width : 1.0f;
        const f32 sy = wk && wk->texHdr->height ? pro->size_H / wk->texHdr->height : 1.0f;
        if (pro->be_flag == 0xFF || !texId || !(copy = copyOf(id, pro, 0x30, texId, kMarkW, kMarkH))) {
            return;
        }
        copy->size_W = kMarkW * sx;
        copy->size_H = kMarkH * sy;
        if (logOn()) {
            re4_log("berserker: save list class %02x: unit 0x18 tex %02x/%u %ux%u fmt %u, size %.1f %.1f, pos %.1f %.1f, "
                    "vtx %02x, col %02x%02x%02x%02x", type, pro->texId, pro->texNo, wk ? wk->texHdr->width : 0,
                    wk ? wk->texHdr->height : 0, wk ? wk->texHdr->format : 0, pro->size_W, pro->size_H, pro->pos0.x,
                    pro->pos0.y, pro->vtxType, pro->col0[0], pro->col0[1], pro->col0[2], pro->col0[3]);
        }
    }
    id->unitPtr(0x17, (ID_CLASS) type)->be_flag &= ~8;
    id->unitPtr(0x18, (ID_CLASS) type)->be_flag &= ~8;
    id->unitPtr(0x22, (ID_CLASS) type)->be_flag &= ~8;
    copy->be_flag |= 8;
}

// Patch 0255 (id_tex.cpp IdTexSet, id_sys.cpp idSysMove04): the texture of one of the two pictures for its id, else 0
GXTexObj* re4_berserker_tex(u8 id)
{
    for (Pic& p : gPics) {
        if (p.id && id == p.id && !IdGetTexWk(id, 1)) {
            return &p.tex;
        }
    }
    return 0;
}

}  // extern "C"

// Patch 0255 (game.cpp gameInit, after the save is loaded): a new game gets the marker when Berserker was chosen and
// loses an old one otherwise; a loaded game keeps its save's (C++ linkage: game.cpp declares it in the line of its call, patch 0255 adds no lines there)
void re4_berserker_game_init()
{
    if (!pG) {
        return;
    }
    if (SysFlagChk(pG, SYS_NEW_GAME) && !SysFlagChk(pG, SYS_LOAD_GAME)) {
        const bool want = (gChosen == 1 || testOn()) && !extraGame();
        if (want) {
            pG->game_mode = 6;
            u32 seed = testOn() && gChosen != 1 ? 0x4242u : 0;  // (the PC checks: the same copies every run)
            while ((seed & 0xFFFF) == 0) {
                seed = (u32) rand() ^ (u32) pG->Frame_cnt * 0x9E3779B1u ^ (u32) (uintptr_t) &seed;
                seed ^= seed >> 16;
            }
            pG->pad_8355[0] = kMagic;
            pG->pad_8355[1] = (u8) (seed >> 8);
            pG->pad_8355[2] = (u8) seed;
        } else {
            memset(pG->pad_8355, 0, sizeof(pG->pad_8355));
        }
        gChosen = 0;
    }
    // the save list's mark (card.cpp reads the header's byte 0x3F: GLOBAL_WK 0x4F8B, padding nothing reads)
    pG->pad_4F8B = re4_berserker_on() ? kMagic : 0;
    if (re4_berserker_on()) {
        re4_log("berserker: on (%s game, seed %04x): Professional, More enemies with the save's seed, enemies take 0.8 "
                "of the damage", SysFlagChk(pG, SYS_LOAD_GAME) ? "loaded" : "new", seedOf());
    }
}

// Patch 0256, Berserker's bigger blood (the project owner, 24.11.2026: "größere Blutfontäne und größere Blutlache bei
// Treffern, rund 1.5x"; no more bursts, no bigger or longer pools under the dead): while the game's blood helpers of a hit
// run (EmDmBloodSet, EmDmBloodSet2, EmDmBloodSet3, em10BloodSet, em1cBloodSet: Re4BloodScope) for an enemy the game scales
// the damage of (re4_berserker_damage's list, without the animals; not Ashley or Luis, not doors, barrels, boxes ...),
// the effects they start get the game's own control block (ESPSEQ_CONTROL, as EstSet's last argument: copied into the
// sequence and handed down to its sub-effects, so the floor blood of the drops gets it too) with the size multiplied by
// 1.5 (Mul_flg bit 1), the speed as it is. EspSeqSet applies it only to the blood sprites (kBloodIds), not the
// refraction or flash records of a hit. Nothing more is drawn and no random number is taken.
struct Re4BloodScope {  // (patch 0256 declares it the same in em_sub.cpp and em10.cpp)
    Re4BloodScope(void* em);
    ~Re4BloodScope();
};

namespace {
int gBloodDepth;
const void* gBloodEm;
ESPSEQ_CONTROL gBloodCtl;
const u8 kBloodMark = 0xB5;  // ESPSEQ_CONTROL::pad: the control is Berserker's
const f32 kBloodSize = 1.5f;

bool bloodEnemy(const void* em)
{
    if (!em || !re4_berserker_on()) {
        return false;
    }
    const int id = ((const cEm*) em)->id;
    switch (id) {
    case 0x21: case 0x23: case 0x24: case 0x26: case 0x27: case 0x28: case 0x29:  // the animals
        return false;
    }
    return id >= 0x10 && id <= 0x3F;
}
}  // namespace

Re4BloodScope::Re4BloodScope(void* em)
{
    if (gBloodDepth++ == 0) {
        gBloodEm = bloodEnemy(em) ? em : 0;
    }
}

Re4BloodScope::~Re4BloodScope()
{
    if (--gBloodDepth == 0) {
        gBloodEm = 0;
    }
}

extern "C" {

// Patch 0256 (est.cpp EstSet, esp_sub.cpp EspEstSetSelect): Berserker's control block inside a blood helper, else 0
ESPSEQ_CONTROL* re4_berserker_blood_ctl(void)
{
    static int off = -1;  // RE4_TEST_BLOOD_OFF=1 (the PC check): the same run with the game's blood, for a comparison
    if (off < 0) {
        const char* v = getenv("RE4_TEST_BLOOD_OFF");
        off = v && strcmp(v, "1") == 0;
    }
    if (!gBloodEm || gBloodDepth <= 0 || off) {
        return 0;
    }
    memset(&gBloodCtl, 0, sizeof(gBloodCtl));
    gBloodCtl.Mul_flg = 2;  // the size
    gBloodCtl.pad = kBloodMark;
    gBloodCtl.Size_base_x = kBloodSize;
    gBloodCtl.Size_base_y = kBloodSize;
    return &gBloodCtl;
}

// Patch 0256 (esp_sub.cpp EspSeqSet): whether a sprite of effect id `id` takes the control block `pSct`: the game's
// own blocks always, Berserker's only for the blood sprites (RE4_BERSERKER_LOG=1: one line for each id it meets)
int re4_berserker_blood_rec(ESPSEQ_CONTROL* pSct, int id)
{
    if (!pSct || pSct->pad != kBloodMark) {
        return 1;
    }
    bool blood = false;
    switch (id) {
    case 0x00: case 0x01: case 0x0B: case 0x10:  // blood drops, spray, splash, floor blood (esp10)
        blood = true;
        break;
    }
    static u32 seen[8];
    if (logOn() && !(seen[(id >> 5) & 7] & (1u << (id & 31)))) {
        seen[(id >> 5) & 7] |= 1u << (id & 31);
        re4_log("berserker: blood control meets sprite id 0x%02x: %s", id, blood ? "size x1.5" : "left as it is");
    }
    return blood;
}

}  // extern "C"
