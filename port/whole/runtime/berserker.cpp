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
//   RE4_TEST_BERSERKER=1   (the PC checks) a new game is Berserker (game_mode 6 with the marker), as if chosen
//   RE4_BERSERKER_LOG=1    (the PC checks) one line for every hit on an enemy the game scales: before and after
#include "types.h"
#include "global.h"
#include "id_sys.h"
#include "texture.h"
#include "gx.h"
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
// the word's texels: in the cheats' place of MEM1 (re4_mem1_map.h; GX reads textures there like the game's own)
const u32 kWordAddr = RE4_MEM1_CHEAT_ADDR + 0x4F0000u;
static_assert(0x4F0000u + sizeof(kWord) <= RE4_MEM1_CHEAT_MAX, "the word's place runs past the cheats' places");

int gChosen;            // the title's choice for the next new game: 1 Berserker
u8 gTexId;              // the word's texture id (0: none yet)
GXTexObj gTex;
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

// a texture id the id texture system does not hold, from the top (0xFF is "none")
u8 freeTexId()
{
    for (int id = 0xFE; id > 0; id--) {
        if (!IdGetTexWk((u8) id, 1)) {
            return (u8) id;
        }
    }
    return 0;
}

ID_UNIT* copyOf(const ID_UNIT* from, u8 mark)
{
    ID_UNIT* u = IdSys.unitPull();
    if (!u) {
        return 0;
    }
    memcpy(u, from, sizeof(ID_UNIT));
    u->markNo = mark;
    u->unitNo = mark;
    u->texId = gTexId;
    u->texNo = 0;
    u->tex_ptn_no = 0;
    u->tex_flag |= 2;  // no texture animation
    u->size_W = (f32) kWordW;
    u->size_H = (f32) kWordH;
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
    if (!gTexId) {
        gTexId = freeTexId();
        if (!gTexId) {
            re4_log("berserker: no free texture id for the menu row");
            return num;
        }
        memcpy((void*) (uintptr_t) kWordAddr, kWord, sizeof(kWord));
        GXInitTexObj(&gTex, (void*) (uintptr_t) kWordAddr, kWordW, kWordH, GX_TF_IA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
        GXInitTexObjLOD(&gTex, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    } else if (IdGetTexWk(gTexId, 1)) {
        gTexId = freeTexId();  // the title took it since
        if (!gTexId) {
            return num;
        }
    }
    const f32 step = pro->pos0.y - normal->pos0.y;  // the rows' distance (20)
    gRow[0] = copyOf(pro, 0x21);
    gRow[1] = copyOf(word, 0x22);
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
    re4_log("berserker: menu row above Professional (texture id %02x, %dx%d, %d rows)", gTexId, kWordW, kWordH, num + 1);
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

// Patch 0255 (id_tex.cpp IdTexSet, id_sys.cpp idSysMove04): the word's texture for its id, else 0
GXTexObj* re4_berserker_tex(u8 id)
{
    return gTexId && id == gTexId && !IdGetTexWk(id, 1) ? &gTex : 0;
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
            u32 seed = 0;
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
    if (re4_berserker_on()) {
        re4_log("berserker: on (%s game, seed %04x): Professional, More enemies with the save's seed, enemies take 0.8 "
                "of the damage", SysFlagChk(pG, SYS_LOAD_GAME) ? "loaded" : "new", seedOf());
    }
}
