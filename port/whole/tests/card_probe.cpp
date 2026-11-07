// Compiled with the game's flags and headers (re4_game_flags) for the core
// test (re4_host --core-test, "card"): the memory card library of the SDK
// (src/lib/CARD*.c) on the port's EXI functions and card (runtime/exi.cpp,
// memcard.cpp), without disc data, on a card file of its own:
//
//   - a new card file is a Memory Card 251 (16 Mbit, 8 KB sectors),
//     formatted and empty as one from the shop: it mounts, and its system
//     blocks are those the library's own format writes; slot B is empty
//   - a blank one (RE4_MEMCARD_NEW=blank): mount says "broken" (no valid
//     header), the format makes it usable,
//     CARDCheck finds directory and FAT in order, 251 blocks and 127 files free
//   - the game's two files (tests/../src/game/card.cpp): bh4_system (1 block)
//     and bh4_data00 (8 blocks) are made, written, given their status
//     (banner, icon, comment as cCard::makeCardStatus) and read back
//   - a file is deleted (its blocks are free again) and made anew
//   - a new start of the app (the card file read again, the SRAM flash ID
//     derived anew): mount, check and the files' contents as written
//   - no card in slot A: probe and mount say "no card"
//   - the synchronous calls (CARDMount, CARDFormat, CARDCreate, CARDWrite,
//     CARDRead, CARDDelete) on an OS thread of the port's scheduler
//     (re4_os_run): they sleep in __CARDSync until the card's interrupts,
//     delivered when the thread goes to sleep, wake them
//   - .gci export and import of the app's menu (M11, runtime/gci.cpp,
//     card_files.cpp): a save and the system file made by the library are
//     exported in GameCube byte order (fields checked against the game's
//     structures, CRCs as a GameCube computes them), imported again over the
//     old file and as a new one; the library then finds directory and FAT in
//     order and reads back the very bytes it wrote; files of another game,
//     cut short or with a wrong CRC are refused
//
// The asynchronous calls are waited for as the game's card task does: the
// result code polled, the pending EXI interrupts delivered in between (in
// the game the interrupt points of the OS emulation do that).
#define new newName  // a parameter of CARDRename(Async) in this C header
#include <dolphin/card.h>
#undef new
#include <dolphin/os.h>

#include "global.h"
#include "item.h"
#include "merchant.h"
#include "../runtime/re4_card_files.h"

#include <stddef.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void CRCInit();  // src/game/card.cpp
u32 CRCCalc(u8* bufPtr, u32 nByte);

extern "C" {
int re4_os_run(int (*entry)());  // runtime/os_thread.cpp
s32 CARDFormatAsync(s32 chan, CARDCallback callback);
void re4_memcard_open(const char* path);
void re4_exi_deliver(void);
void re4_log(const char* fmt, ...);
int re4_mem1_init(void);
}

namespace {

int gFailures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            re4_log("card-test: %s:%d: %s", __FILE__, __LINE__, #cond); \
            gFailures++;                                                \
        }                                                               \
    } while (0)

const u32 kSystemSize = 0x2000, kDataSize = 0x10000;  // card.cpp: 1 and 8 blocks
u8 gWork[CARD_WORKAREA_SIZE] __attribute__((aligned(32)));
u8 gBuf[kDataSize] __attribute__((aligned(32)));
u8 gRead[kDataSize] __attribute__((aligned(32)));
s32 gCallbackResult;
int gCallbacks;

void callback(s32, s32 result)
{
    gCallbackResult = result;
    gCallbacks++;
}

// An asynchronous call started with result `started`: its end.
s32 finish(s32 started)
{
    if (started < 0) {
        return started;
    }
    for (int i = 0; i < 5000 && CARDGetResultCode(0) == CARD_RESULT_BUSY; i++) {
        re4_exi_deliver();
        if (i > 50) {
            usleep(1000);  // a card timeout alarm (100 ms / 2 s) would come from here
        }
    }
    s32 result = CARDGetResultCode(0);
    if (result != CARD_RESULT_BUSY && result != gCallbackResult) {
        re4_log("card-test: result %d, callback %d", (int) result, (int) gCallbackResult);
    }
    return result;
}

void put32(u8* p, u32 v)
{
    memcpy(p, &v, 4);  // the game stores its words in host order (card.cpp)
}

// The content of a test file: noise, with the game's own CRCs (card.cpp
// CRCCalc) where makeSaveData / makeSystemSaveData put them, so that
// port/tools/memcard_list.py can be checked against the game's code
// (whole_build.sh, RE4_CARD_TEST_FILE).
void fill(u8* p, u32 n, u32 seed)
{
    for (u32 i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (u8) (seed >> 16);
    }
    CRCInit();
    if (n == kSystemSize) {
        put32(p + 0x1E78, CRCCalc(p, 0x1E78));  // SYS_CRC
    } else if (n == kDataSize) {
        put32(p + 0x2004, 0x116);  // SaveInfo: magic, 2026-10-16 12:34:56, room r100
        const int when[10] = { 56, 34, 12, 16, 9, 2026, 5, 288, 0, 0 };
        memcpy(p + 0x2008, when, sizeof(when));
        u16 room = 0x100;
        memcpy(p + 0x2050, &room, 2);
        put32(p + 0x2000, CRCCalc(p + 0x2004, 0x1FC));
        put32(p + 0xEAF8, CRCCalc(p, 0xEAF8));  // SAVE_CRC
    }
}

s32 mount()
{
    return finish(CARDMountAsync(0, gWork, 0, callback));
}

s32 check()
{
    s32 xfer = 0;
    return finish(CARDCheckExAsync(0, &xfer, callback));
}

// Makes and writes file `name` as the game does (cCard::fileCreate, fileWrite,
// makeCardStatus): banner RGB5A3 at 0x40, one C8 icon at 0x1840, comment at 0.
bool makeFile(const char* name, u32 size, u32 seed)
{
    CARDFileInfo fi;
    s32 r = finish(CARDCreateAsync(0, name, size, &fi, callback));
    CHECK(r == CARD_RESULT_READY);
    if (r != CARD_RESULT_READY) {
        return false;
    }
    fill(gBuf, size, seed);
    r = finish(CARDWriteAsync(&fi, gBuf, (s32) size, 0, callback));
    CHECK(r == CARD_RESULT_READY);
    CARDStat stat;
    CHECK(CARDGetStatus(0, fi.fileNo, &stat) == CARD_RESULT_READY);
    CHECK(strcmp(stat.fileName, name) == 0 && stat.length == size);
    CHECK(memcmp(stat.gameName, "G4BE", 4) == 0 && memcmp(stat.company, "08", 2) == 0);
    CARDSetCommentAddress(&stat, 0);
    CARDSetBannerFormat(&stat, CARD_STAT_BANNER_RGB5A3);
    CARDSetIconAddress(&stat, 0x40);
    CARDSetIconFormat(&stat, 0, CARD_STAT_ICON_C8);
    CARDSetIconSpeed(&stat, 0, CARD_STAT_SPEED_SLOW);
    CARDSetIconFormat(&stat, 1, CARD_STAT_ICON_NONE);
    CARDSetIconSpeed(&stat, 1, CARD_STAT_SPEED_END);
    r = finish(CARDSetStatusAsync(0, fi.fileNo, &stat, callback));
    CHECK(r == CARD_RESULT_READY);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
    return r == CARD_RESULT_READY;
}

// File `name` holds what makeFile(seed) wrote, with its status.
void readFile(const char* name, u32 size, u32 seed)
{
    CARDFileInfo fi;
    s32 r = CARDOpen(0, name, &fi);
    CHECK(r == CARD_RESULT_READY);
    if (r != CARD_RESULT_READY) {
        re4_log("card-test: %s does not open (%d)", name, (int) r);
        return;
    }
    memset(gRead, 0, size);
    r = finish(CARDReadAsync(&fi, gRead, (s32) size, 0, callback));
    CHECK(r == CARD_RESULT_READY);
    fill(gBuf, size, seed);
    CHECK(memcmp(gRead, gBuf, size) == 0);
    CARDStat stat;
    CHECK(CARDGetStatus(0, fi.fileNo, &stat) == CARD_RESULT_READY);
    CHECK(stat.length == size && CARDGetBannerFormat(&stat) == CARD_STAT_BANNER_RGB5A3);
    CHECK(stat.iconAddr == 0x40 && stat.commentAddr == 0 && CARDGetIconFormat(&stat, 0) == CARD_STAT_ICON_C8);
    // banner 96x32 RGB5A3, then the C8 icon and its palette (CARDStat.c)
    CHECK(stat.offsetBanner == 0x40 && stat.offsetIcon[0] == 0x40 + 0x1800 && stat.offsetIconTlut == 0x1C40);
    CHECK(stat.offsetData == 0x1E40);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
}

// The five system blocks (header, directories, FATs) of the card file
void readSystemBlocks(const char* path, u8* out)
{
    memset(out, 0, 5 * 0x2000);
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0 && read(fd, out, 5 * 0x2000) == 5 * 0x2000);
    if (fd >= 0) {
        close(fd);
    }
}

s32 freeBlocks()
{
    s32 bytes = -1, files = -1;
    CHECK(CARDFreeBlocks(0, &bytes, &files) == CARD_RESULT_READY);
    return bytes / 0x2000;
}

// .gci: a save as the game makes one, with words where a GameCube would
// show their byte order (makeFileWith writes it through the library)
const u32 kSaveGame = 0x2034, kSaveItem = 0x572C, kSaveRoom = 0x6930, kSaveMerchant = 0xE7D4;

void gameSave(u8* p)
{
    fill(p, kDataSize, 21);
    memset(p + 0x1E40, 0, 0x1C0);  // between the palette and the header: nothing
    memset(p + kSaveGame, 0, 0x36F8);
    GLOBAL_WK* g = (GLOBAL_WK*) (p + kSaveGame - offsetof(GLOBAL_WK, save_data_start_addr));
    g->play_time = 0x01020304;
    g->peseta = 0x00012345;
    g->room_id = 0x0100;
    g->pl_pos.x = -1340.5f;
    g->Status_flg[0] = 0x80000001u;
    g->save_cnt = 0x0203;
    // items, rooms and the merchant: their first fields
    _ITEM_SAVE_DATA* items = (_ITEM_SAVE_DATA*) (p + kSaveItem);
    memset(items, 0, sizeof(*items));
    items->arm_no = 0x0102;
    items->item_list[0].id = 0x8023;
    u32 rooms[4] = { 0x10 + 3 * 0xD8, 3, 0, 0 };
    memset(p + kSaveRoom, 0, 0xE7D0 - kSaveRoom);
    memcpy(p + kSaveRoom, rooms, sizeof(rooms));
    u16 roomNo = 0x0120;
    memcpy(p + kSaveRoom + 0x10 + 0xD8, &roomNo, 2);
    MERCHANT_DATA* m = (MERCHANT_DATA*) (p + kSaveMerchant);
    memset(m, 0, sizeof(*m));
    m->stock.e[0].id = 0x0035;
    CRCInit();
    put32(p + 0x2004, 0x116);
    put32(p + 0x2000, CRCCalc(p + 0x2004, 0x1FC));
    put32(p + 0xEAF8, CRCCalc(p, 0xEAF8));
}

u32 be32At(const u8* p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

u16 be16At(const u8* p)
{
    return (u16) (p[0] << 8 | p[1]);
}

bool makeFileWith(const char* name, u32 size, const u8* content)
{
    CARDFileInfo fi;
    s32 r = finish(CARDCreateAsync(0, name, size, &fi, callback));
    CHECK(r == CARD_RESULT_READY);
    if (r != CARD_RESULT_READY) {
        return false;
    }
    memcpy(gBuf, content, size);
    r = finish(CARDWriteAsync(&fi, gBuf, (s32) size, 0, callback));
    CHECK(r == CARD_RESULT_READY);
    CARDStat stat;
    CHECK(CARDGetStatus(0, fi.fileNo, &stat) == CARD_RESULT_READY);
    CARDSetCommentAddress(&stat, 0);
    CARDSetBannerFormat(&stat, CARD_STAT_BANNER_RGB5A3);
    CARDSetIconAddress(&stat, 0x40);
    CARDSetIconFormat(&stat, 0, CARD_STAT_ICON_C8);
    CARDSetIconSpeed(&stat, 0, CARD_STAT_SPEED_SLOW);
    CARDSetIconFormat(&stat, 1, CARD_STAT_ICON_NONE);
    CARDSetIconSpeed(&stat, 1, CARD_STAT_SPEED_END);
    r = finish(CARDSetStatusAsync(0, fi.fileNo, &stat, callback));
    CHECK(r == CARD_RESULT_READY);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
    return r == CARD_RESULT_READY;
}

// File `name` holds `content`, with the status makeFileWith gave it.
void readFileIs(const char* name, u32 size, const u8* content)
{
    CARDFileInfo fi;
    s32 r = CARDOpen(0, name, &fi);
    CHECK(r == CARD_RESULT_READY);
    if (r != CARD_RESULT_READY) {
        re4_log("card-test: %s does not open (%d)", name, (int) r);
        return;
    }
    memset(gRead, 0, size);
    CHECK(finish(CARDReadAsync(&fi, gRead, (s32) size, 0, callback)) == CARD_RESULT_READY);
    CHECK(memcmp(gRead, content, size) == 0);
    CARDStat stat;
    CHECK(CARDGetStatus(0, fi.fileNo, &stat) == CARD_RESULT_READY);
    CHECK(stat.length == size && CARDGetBannerFormat(&stat) == CARD_STAT_BANNER_RGB5A3);
    CHECK(stat.iconAddr == 0x40 && stat.commentAddr == 0 && CARDGetIconFormat(&stat, 0) == CARD_STAT_ICON_C8);
    CHECK(CARDGetIconSpeed(&stat, 0) == CARD_STAT_SPEED_SLOW);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
}

int findFile(const Re4CardFile* files, int n, const char* name)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void gciTest(const char* path)
{
    static u8 save[kDataSize], system[kSystemSize];
    static u8 gci[0x40 + kDataSize], gciSystem[0x40 + kSystemSize];
    char note[256];
    unlink(path);
    re4_memcard_open(path);
    CHECK(mount() == CARD_RESULT_READY);
    gameSave(save);
    fill(system, kSystemSize, 22);
    u32 config = 0x0A000000u;
    memcpy(system + 0x1E40, &config, 4);  // SYSTEM_SAVE_WORK Config_flg
    put32(system + 0x1E78, CRCCalc(system, 0x1E78));
    CHECK(makeFileWith("bh4_system", kSystemSize, system));
    CHECK(makeFileWith("bh4_data00", kDataSize, save));
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);

    // the list of the menu
    Re4CardFile files[8];
    int n = re4_card_list(files, 8);
    CHECK(n == 2);
    int s = findFile(files, n, "bh4_data00"), y = findFile(files, n, "bh4_system");
    CHECK(s >= 0 && y >= 0);
    if (s < 0 || y < 0) {
        return;
    }
    CHECK(files[s].save && files[s].intact && files[s].room == 0x100 && files[s].blocks == 8);
    CHECK(files[s].year == 2026 && files[s].saveCount == 0x0203 && files[s].playTime == 0x01020304);
    CHECK(!files[y].save && files[y].intact);
    CHECK(re4_card_free_blocks() == 251 - 9);
    char name[64];
    CHECK(re4_gci_file_name(files[s].index, name, sizeof(name)) == RE4_CARD_OK);
    CHECK(strcmp(name, "08-G4BE-bh4_data00.gci") == 0);

    // export: GameCube byte order
    int bytes = re4_gci_export(files[s].index, gci, sizeof(gci), note, sizeof(note));
    CHECK(bytes == (int) sizeof(gci));
    const u8* d = gci + 0x40;
    CHECK(memcmp(gci, "G4BE08", 6) == 0 && strcmp((const char*) gci + 8, "bh4_data00") == 0);
    CHECK(be16At(gci + 0x38) == 8 && be32At(gci + 0x2C) == 0x40 && be32At(gci + 0x3C) == 0);
    CHECK(be32At(d + 0x2004) == 0x116);
    // signed: on LP64 the save block starts later in GLOBAL_WK than kSaveGame (pointers in front)
    const long g = (long) kSaveGame - (long) offsetof(GLOBAL_WK, save_data_start_addr);
    CHECK(be32At(d + g + offsetof(GLOBAL_WK, play_time)) == 0x01020304);
    CHECK(be32At(d + g + offsetof(GLOBAL_WK, peseta)) == 0x00012345);
    CHECK(be16At(d + g + offsetof(GLOBAL_WK, room_id)) == 0x0100);
    // on the GameCube the stage byte comes first (the port's union keeps it at the other end, patch 0016)
    CHECK(d[g + offsetof(GLOBAL_WK, room_id)] == 0x01 && d[g + offsetof(GLOBAL_WK, room_id) + 1] == 0x00);
    CHECK(be32At(d + g + offsetof(GLOBAL_WK, Status_flg)) == 0x80000001u);
    CHECK(be16At(d + g + offsetof(GLOBAL_WK, save_cnt)) == 0x0203);
    float x = -1340.5f;
    u32 xBits;
    memcpy(&xBits, &x, 4);
    CHECK(be32At(d + g + offsetof(GLOBAL_WK, pl_pos)) == xBits);
    CHECK(be16At(d + kSaveItem + offsetof(_ITEM_SAVE_DATA, arm_no)) == 0x0102);
    CHECK(be16At(d + kSaveItem + offsetof(_ITEM_SAVE_DATA, item_list)) == 0x8023);
    CHECK(be32At(d + kSaveRoom + 4) == 3 && be16At(d + kSaveRoom + 0x10 + 0xD8) == 0x0120);
    CHECK(be16At(d + kSaveMerchant) == 0x0035);
    CHECK(memcmp(d + 0x40, save + 0x40, 0x1E00) == 0);  // banner, icon, palette: as they were
    CHECK(be32At(d + 0x2000) == CRCCalc((u8*) d + 0x2004, 0x1FC));
    CHECK(be32At(d + 0xEAF8) == CRCCalc((u8*) d, 0xEAF8));
    bytes = re4_gci_export(files[y].index, gciSystem, sizeof(gciSystem), note, sizeof(note));
    CHECK(bytes == (int) sizeof(gciSystem));
    CHECK(be32At(gciSystem + 0x40 + 0x1E40) == 0x0A000000u);
    CHECK(be32At(gciSystem + 0x40 + 0x1E78) == CRCCalc(gciSystem + 0x40, 0x1E78));
    const char* keep = getenv("RE4_GCI_TEST_FILE");
    if (keep && *keep) {
        if (FILE* f = fopen(keep, "wb")) {
            fwrite(gci, 1, sizeof(gci), f);
            fclose(f);
        }
    }

    // import: over the file, refused without replace
    CHECK(re4_gci_import(gci, sizeof(gci), 0, note, sizeof(note)) == RE4_CARD_EXISTS);
    CHECK(re4_gci_import(gci, sizeof(gci), 1, note, sizeof(note)) == RE4_CARD_OK);
    CHECK(re4_gci_import(gciSystem, sizeof(gciSystem), 1, note, sizeof(note)) == RE4_CARD_OK);
    CHECK(re4_card_free_blocks() == 251 - 9);
    // ... and as a new file (the name is not under a CRC)
    static u8 other[sizeof(gci)];
    memcpy(other, gci, sizeof(gci));
    other[8 + 9] = '1';
    CHECK(re4_gci_import(other, sizeof(other), 0, note, sizeof(note)) == RE4_CARD_OK);
    CHECK(re4_card_free_blocks() == 251 - 17);
    // a save of the European release (G4BP08, the same form): over it, kept as G4BE08
    memcpy(other, "G4BP", 4);
    CHECK(re4_gci_import(other, sizeof(other), 1, note, sizeof(note)) == RE4_CARD_OK);
    CHECK(re4_card_free_blocks() == 251 - 17);
    n = re4_card_list(files, 8);
    int eu = findFile(files, n, "bh4_data01");
    CHECK(eu >= 0 && files[eu].intact && re4_gci_file_name(files[eu].index, name, sizeof(name)) == RE4_CARD_OK
          && strcmp(name, "08-G4BE-bh4_data01.gci") == 0);
    // refused: another game, cut short, a wrong CRC, another version
    memcpy(other, gci, sizeof(gci));
    memcpy(other, "GZLE", 4);
    CHECK(re4_gci_import(other, sizeof(other), 1, note, sizeof(note)) == RE4_CARD_BAD);
    CHECK(re4_gci_import(gci, sizeof(gci) - 0x2000, 1, note, sizeof(note)) == RE4_CARD_BAD);
    memcpy(other, gci, sizeof(gci));
    other[0x40 + 0x3000] ^= 1;
    CHECK(re4_gci_import(other, sizeof(other), 1, note, sizeof(note)) == RE4_CARD_BAD);
    CHECK(re4_card_free_blocks() == 251 - 17);

    // the library: directory and FAT in order, the bytes it wrote
    CHECK(mount() == CARD_RESULT_READY);
    CHECK(check() == CARD_RESULT_READY);
    CHECK(freeBlocks() == 251 - 17);
    readFileIs("bh4_data00", kDataSize, save);
    readFileIs("bh4_data01", kDataSize, save);
    readFileIs("bh4_system", kSystemSize, system);
    CHECK(finish(CARDDeleteAsync(0, "bh4_data01", callback)) == CARD_RESULT_READY);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    // a new start: the card from its file
    re4_memcard_open(path);
    CHECK(mount() == CARD_RESULT_READY);
    CHECK(check() == CARD_RESULT_READY);
    CHECK(freeBlocks() == 251 - 9);
    readFileIs("bh4_data00", kDataSize, save);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    // the conversion both ways gives the bytes back
    memcpy(gRead, save, kDataSize);
    CHECK(re4_gci_convert("bh4_data07", gRead, kDataSize, 1, note, sizeof(note)) == RE4_CARD_OK);
    CHECK(re4_gci_convert("bh4_data07", gRead, kDataSize, 0, note, sizeof(note)) == RE4_CARD_OK);
    CHECK(memcmp(gRead, save, kDataSize) == 0);
    unlink(path);
}

char gSyncPath[256];

// The synchronous API on the boot thread of the OS emulation.
int syncMain()
{
    int before = gFailures;
    re4_memcard_open(gSyncPath);
    CHECK(CARDMount(0, gWork, 0) == CARD_RESULT_READY);  // a new card: formatted
    CHECK(CARDFormat(0) == CARD_RESULT_READY);
    CHECK(CARDCheck(0) == CARD_RESULT_READY);
    CARDFileInfo fi;
    CHECK(CARDCreate(0, "bh4_data05", kDataSize, &fi) == CARD_RESULT_READY);
    fill(gBuf, kDataSize, 7);
    CHECK(CARDWrite(&fi, gBuf, (s32) kDataSize, 0) == CARD_RESULT_READY);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, "bh4_data05", &fi) == CARD_RESULT_READY);
    memset(gRead, 0, kDataSize);
    CHECK(CARDRead(&fi, gRead, (s32) kDataSize, 0) == CARD_RESULT_READY);
    CHECK(memcmp(gRead, gBuf, kDataSize) == 0);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
    CHECK(freeBlocks() == 251 - 8);
    CHECK(CARDDelete(0, "bh4_data05") == CARD_RESULT_READY);
    CHECK(freeBlocks() == 251);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    return gFailures - before;
}

}  // namespace

// The card checks; the number of failed checks.
extern "C" int re4probe_card_test(void)
{
    gFailures = 0;
    CHECK(re4_mem1_init());  // the disc ID in low memory (CARDInit, CARDCreate)
    // RE4_CARD_TEST_FILE: the card stays there afterwards (whole_build.sh lists it)
    char path[256];
    const char* keep = getenv("RE4_CARD_TEST_FILE");
    if (keep && *keep) {
        snprintf(path, sizeof(path), "%s", keep);
    } else {
        snprintf(path, sizeof(path), "/tmp/re4-card-test-%d.bin", (int) getpid());
    }
    unlink(path);
    re4_memcard_open(path);
    CARDInit();

    // a new card (no file yet): present, formatted and empty like one from the shop
    s32 memSize = 0, sectorSize = 0;
    CHECK(CARDProbeEx(0, &memSize, &sectorSize) == CARD_RESULT_READY);
    CHECK(memSize == 16 && sectorSize == 0x2000);
    CHECK(CARDProbeEx(1, 0, 0) == CARD_RESULT_NOCARD);
    // (the game's headers have a struct stat of their own; lseek64 by name, as
    // runtime/memcard.cpp says)
    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0 && lseek64(fd, 0, SEEK_END) == 2 * 1024 * 1024);
    if (fd >= 0) {
        close(fd);
    }
    s32 r = mount();
    CHECK(r == CARD_RESULT_READY);
    CHECK(gCallbacks > 0);
    CHECK(check() == CARD_RESULT_READY);
    CHECK(freeBlocks() == 251);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    // ... its system blocks as the library formats them (but for the format
    // time and the serial derived from it)
    static u8 made[5 * 0x2000], formatted[5 * 0x2000];
    readSystemBlocks(path, made);
    re4_memcard_open(path);
    CHECK(mount() == CARD_RESULT_READY);
    CHECK(finish(CARDFormatAsync(0, callback)) == CARD_RESULT_READY);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
    readSystemBlocks(path, formatted);
    CHECK(memcmp(made + 20, formatted + 20, 0x1FC - 20) == 0);        // header after the serial
    CHECK(memcmp(made + 0x200, formatted + 0x200, 0x1E00) == 0);      // the rest of block 0
    CHECK(memcmp(made + 0x2000, formatted + 0x2000, 0x8000) == 0);    // directories and FATs
    CHECK(memcmp(made, formatted, 12) != 0);                          // a serial of its own

    // a blank card (RE4_MEMCARD_NEW=blank): unformatted, "broken" at mount
    unlink(path);
    setenv("RE4_MEMCARD_NEW", "blank", 1);
    re4_memcard_open(path);
    unsetenv("RE4_MEMCARD_NEW");
    r = mount();
    CHECK(r == CARD_RESULT_BROKEN);

    // format: an empty card
    r = finish(CARDFormatAsync(0, callback));
    CHECK(r == CARD_RESULT_READY);
    CHECK(check() == CARD_RESULT_READY);
    CHECK(freeBlocks() == 251);
    u16 encode = 0xFFFF;
    CHECK(CARDGetEncoding(0, &encode) == CARD_RESULT_READY && encode == CARD_ENCODE_ANSI);
    u64 serial = 0;
    CHECK(CARDGetSerialNo(0, &serial) == CARD_RESULT_READY);

    // the game's files
    CHECK(makeFile("bh4_system", kSystemSize, 1));
    CHECK(makeFile("bh4_data00", kDataSize, 2));
    CHECK(freeBlocks() == 251 - 1 - 8);
    CARDFileInfo fi;
    CHECK(finish(CARDCreateAsync(0, "bh4_system", kSystemSize, &fi, callback)) == CARD_RESULT_EXIST);
    readFile("bh4_system", kSystemSize, 1);
    readFile("bh4_data00", kDataSize, 2);
    CHECK(CARDOpen(0, "bh4_data01", &fi) == CARD_RESULT_NOFILE);

    // delete and make anew
    CHECK(finish(CARDDeleteAsync(0, "bh4_data00", callback)) == CARD_RESULT_READY);
    CHECK(freeBlocks() == 251 - 1);
    CHECK(CARDOpen(0, "bh4_data00", &fi) == CARD_RESULT_NOFILE);
    CHECK(makeFile("bh4_data00", kDataSize, 3));
    CHECK(check() == CARD_RESULT_READY);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);

    // a new start of the app: the card from its file
    re4_memcard_open(path);
    r = mount();
    CHECK(r == CARD_RESULT_READY);
    CHECK(check() == CARD_RESULT_READY);
    u64 serialAgain = 1;
    CHECK(CARDGetSerialNo(0, &serialAgain) == CARD_RESULT_READY && serialAgain == serial);
    CHECK(freeBlocks() == 251 - 1 - 8);
    readFile("bh4_system", kSystemSize, 1);
    readFile("bh4_data00", kDataSize, 3);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);

    // the synchronous calls on an OS thread, on a card of their own
    snprintf(gSyncPath, sizeof(gSyncPath), "%s.sync", path);
    unlink(gSyncPath);
    re4_os_run(syncMain);
    unlink(gSyncPath);

    // .gci export and import, on a card of its own
    char gciPath[300];
    snprintf(gciPath, sizeof(gciPath), "%s.gci-card", path);
    gciTest(gciPath);

    // no card in slot A
    re4_memcard_open(0);
    CHECK(CARDProbeEx(0, 0, 0) == CARD_RESULT_NOCARD);
    CHECK(mount() == CARD_RESULT_NOCARD);
    if (!keep || !*keep) {
        unlink(path);
    }
    re4_log("card-test: %d failed", gFailures);
    return gFailures;
}

// re4_host --memcard-format FILE: FILE becomes a formatted, empty card (made
// when missing; what was on it is gone). 1 on success.
extern "C" int re4probe_card_format(const char* path)
{
    re4_mem1_init();
    re4_memcard_open(path);
    CARDInit();
    s32 r = mount();
    if (r == CARD_RESULT_READY || r == CARD_RESULT_BROKEN || r == CARD_RESULT_ENCODING) {
        r = finish(CARDFormatAsync(0, callback));
    }
    if (r == CARD_RESULT_READY) {
        r = check();
    }
    s32 blocks = r == CARD_RESULT_READY ? freeBlocks() : 0;
    CARDUnmount(0);
    re4_memcard_open(0);  // the file synced and closed
    re4_log("memcard-format: %s %s (%d blocks free)", path, r == CARD_RESULT_READY ? "formatted" : "FAILED",
            (int) blocks);
    return r == CARD_RESULT_READY && blocks == 251;
}
