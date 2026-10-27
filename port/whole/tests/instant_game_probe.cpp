// Exercise the production capture against the real original GameSave / ItemMgr / RoomData /
// merchant / subscreen serializers, using a synthetic playable state in MEM1, without disc data.
#include "types.h"
#include "global.h"
#include "game.h"
#include "em.h"
#include "player.h"
#define private public
#include "item.h"
#include "room_data.h"
#undef private
#include "merchant.h"
#include "sscrn.h"
#include "main.h"
#include "title.h"
#include "../runtime/re4_instant.h"
#include <string.h>

extern "C" void re4_log(const char*, ...);
extern "C" int re4_mem1_init();

extern "C" int re4probe_instant_game_test()
{
    if (!re4_mem1_init())
        return 1;
    int failures = 0;
#define CHECK_INSTANT(c)                     \
    do {                                     \
        if (!(c)) {                          \
            ++failures;                      \
            re4_log("instant-game: %s", #c); \
        }                                    \
    } while (0)
    GLOBAL_WK* oldGlobal = pG;
    cPlayer* oldPlayer = pPL;
    SYSTEM_SAVE_WORK* oldSystem = pSys;
    SAVE_DATA_HEAD* oldSave = pSaveData;
    const cRoomData oldRoom = RoomData;
    const cItemMgr oldItems = ItemMgr;
    const SubScreenWork oldScreen = SubScreenWk;
    const MERCHANT_DATA oldMerchant = merchantData[0];
    // Isolated MEM1 scratch; no running game or room heap in --core-test.
    u8* mem = (u8*) 0x82000000;
    memset(mem, 0, 0x50000);
    pG = (GLOBAL_WK*) mem;
    pPL = (cPlayer*) (mem + 0x10000);
    pSys = (SYSTEM_SAVE_WORK*) (mem + 0x14000);
    pSaveData = (SAVE_DATA_HEAD*) (mem + 0x15000);
    memset(pSaveData, 0x5A, 0x100);
    pG->Rno0 = 3;
    pG->pl_life = 789;
    pG->pl_life_max = 1200;
    pG->peseta = 54321;
    pG->room_id = 0x100;
    pG->play_time = 1234;
    pG->save_cnt = 7;
    pG->SaveKind = -1;
    pG->pl_pos.x = 999;
    pG->pl_ang_y = 2;
    pPL->pos.x = -1340.5f;
    pPL->pos.y = 42;
    pPL->pos.z = 717;
    pPL->ang.y = .75f;
    pSys->language = 1;
    SubScreenWk.open_flag = SS_OPEN_NULL;
    SubScreenWk.wait_cnt = 0;
    SubScreenWk.save = 0x01020304;
    RoomData.m_SaveNum = 1;
    RoomData.m_pRoomSaveHead = (RoomSaveHdr*) (mem + 0x16000);
    RoomData.m_pRoomSaveHead->size = 0xE8;
    RoomData.m_pRoomSaveHead->num = 1;
    RoomData.m_pRoomSaveData = mem + 0x16010;
    ROOM_SAVE_DATA* record = (ROOM_SAVE_DATA*) RoomData.m_pRoomSaveData;
    record->RoomNo = 0x100;
    record->item_flg[1] = 0x10203040;
    record->EtcModelFlg[3] = 0x1234;
    ItemMgr.m_array_num = 1;
    ItemMgr.m_pItem = (cItem*) (mem + 0x17000);
    ItemMgr.m_pWep = ItemMgr.m_pItem;
    ItemMgr.m_wep_id = 0x23;
    cItem* item = ItemMgr.m_pItem;
    item->id = 0x23;
    item->flags = 1;
    item->lv = 0x1234;
    item->bullet = 17;
    item->x = 4;
    item->y = 6;
    item->orient = 1;
    item->board = 1;
    merchantData[0].stock.e[0].id = 0x35;
    u8* out = mem + 0x20000;
    CHECK_INSTANT(re4_instant_game_ready());
    CHECK_INSTANT(re4_instant_game_capture(out) == 0);
    GLOBAL_WK* saved = (GLOBAL_WK*) (out + 0x2034 - __builtin_offsetof(GLOBAL_WK, save_data_start_addr));
    CHECK_INSTANT(saved->pl_pos.x == -1340.5f && saved->pl_pos.y == 42 && saved->pl_pos.z == 717 &&
                  saved->pl_ang_y == .75f);
    CHECK_INSTANT(saved->save_cnt == 8 && saved->play_time == 1234 && saved->peseta == 54321 &&
                  saved->pl_life == 789);
    CHECK_INSTANT(pG->save_cnt == 7 && pG->SaveKind == -1 && pG->pl_pos.x == 999 && pG->pl_ang_y == 2);
    for (int i = 0; i < 0x100; ++i)
        CHECK_INSTANT(((u8*) pSaveData)[i] == 0x5A);
    _ITEM_SAVE_DATA* inventory = (_ITEM_SAVE_DATA*) (out + 0x572C);
    CHECK_INSTANT(inventory->arm_no == 0 && inventory->item_list[0].bullet == 17 &&
                  inventory->item_list[0].num == 0x1234);
    CHECK_INSTANT(inventory->item_list[0].x == 4 && inventory->item_list[0].y == 6);
    CHECK_INSTANT(memcmp(out + 0x6930, RoomData.m_pRoomSaveHead, 0xE8) == 0);
    CHECK_INSTANT(memcmp(out + 0xE7D0, &SubScreenWk.save, 4) == 0);
    CHECK_INSTANT(memcmp(out + 0xE7D4, merchantData, MerchantDataSize()) == 0);
    CHECK_INSTANT(memcmp(out + 0x11E40, pSys, sizeof(*pSys)) == 0);
    re4_instant_game_saved();
    CHECK_INSTANT(pG->save_cnt == 8);
    // Unsafe places refuse saving but not loading; the other game modes refuse both.
    const u16 room = pG->room_id;
    pG->room_id = 0x10B; // Del Lago
    CHECK_INSTANT(!re4_instant_game_ready() && re4_instant_game_can_load());
    CHECK_INSTANT(re4_instant_game_capture(out) == RE4_INSTANT_BUSY);
    pG->room_id = room;
    StaFlagOn(pG, STA_PL_BOAT);
    CHECK_INSTANT(!re4_instant_game_ready() && re4_instant_game_can_load());
    StaFlagOff(pG, STA_PL_BOAT);
    StaFlagOn(pG, STA_PL_LADDER);
    CHECK_INSTANT(!re4_instant_game_ready() && re4_instant_game_can_load());
    StaFlagOff(pG, STA_PL_LADDER);
    SysFlagOn(pG, SYS_OMAKE_ETC_GAME); // Mercenaries
    CHECK_INSTANT(!re4_instant_game_ready() && !re4_instant_game_can_load());
    SysFlagOff(pG, SYS_OMAKE_ETC_GAME);
    SysFlagOn(pG, SYS_OMAKE_ADA_GAME); // Assignment Ada
    CHECK_INSTANT(!re4_instant_game_ready() && !re4_instant_game_can_load());
    SysFlagOff(pG, SYS_OMAKE_ADA_GAME);
    pG->pl_type = 1; // Ashley section
    CHECK_INSTANT(!re4_instant_game_ready() && !re4_instant_game_can_load());
    pG->pl_type = 0;
    CHECK_INSTANT(re4_instant_game_ready());
    pG->Rno0 = 2;
    CHECK_INSTANT(!re4_instant_game_ready());
    pG->Rno0 = 3;
    SubScreenWk.open_flag = SS_OPEN_NORMAL;
    CHECK_INSTANT(!re4_instant_game_ready());
    SubScreenWk.open_flag = SS_OPEN_NULL;
    StaFlagOn(pG, STA_EVENT);
    CHECK_INSTANT(!re4_instant_game_ready());
    StaFlagOff(pG, STA_EVENT);
    pG->pl_life = 0;
    CHECK_INSTANT(!re4_instant_game_ready());
    // Cold title: no player/room was entered. Loading and saving have separate eligibility.
    TITLE_WORK* title = (TITLE_WORK*) (mem + 0x18000);
    title->Rno0 = 5;
    title->Rno1 = 1;
    StaFlagOn(pG, STA_TITLE);
    pPL = 0;
    re4_instant_title_tick(title);
    CHECK_INSTANT(re4_instant_game_can_load());
    CHECK_INSTANT(!re4_instant_game_ready());
    CHECK_INSTANT(re4_instant_game_capture(out) == RE4_INSTANT_BUSY);
    title->Rno1 = 6; // demo movie
    CHECK_INSTANT(!re4_instant_game_can_load());
    title->Rno1 = 7; // normal card loading
    CHECK_INSTANT(!re4_instant_game_can_load());
    title->Rno1 = 1;
    SysFlagOn(pG, SYS_CARD_ACCESS);
    CHECK_INSTANT(!re4_instant_game_can_load());
    SysFlagOff(pG, SYS_CARD_ACCESS);
    u32 badRoomSize = 1;
    memcpy(out + 0x6930, &badRoomSize, 4);
    CHECK_INSTANT(re4_instant_game_restore(out) == RE4_INSTANT_BAD);
    CHECK_INSTANT(re4_instant_game_can_load()); // invalid payload never queues a load
    StaFlagOff(pG, STA_TITLE);
    CHECK_INSTANT(!re4_instant_game_can_load());
    RoomData = oldRoom;
    ItemMgr = oldItems;
    SubScreenWk = oldScreen;
    merchantData[0] = oldMerchant;
    pG = oldGlobal;
    pPL = oldPlayer;
    pSys = oldSystem;
    pSaveData = oldSave;
    return failures;
}
