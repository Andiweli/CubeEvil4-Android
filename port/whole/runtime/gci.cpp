// The game's save files between the port's byte order and the GameCube's
// (M11; re4_card_files.h): .gci export and import that Dolphin and a
// GameCube (with a copy tool such as GCMM) take.
//
// The port keeps the game's variables in the device's order (little-endian);
// a save file is a copy of some of them (src/game/card.cpp makeSaveData), so
// every field wider than a byte is turned around by its own size. Which bytes
// are fields of which size comes from the game headers
// (runtime/save_layout.inc, port/tools/save_layout.py). The parts of a file:
//
//   bh4_dataNN (8 blocks, 0xEAFC bytes used)
//     0x0000  two comment lines           bytes
//     0x0040  banner, icon, palette       texels stay big-endian in the port
//                                         (formats_tex.cpp), nothing to turn
//     0x2000  header: CRC, version 0x116, save time (OSCalendarTime), mode
//     0x2034  GLOBAL_WK 0x4F80..0x8678    kSaveGlobal
//     0x572C  items                       kSaveItem
//     0x6930  rooms: header + num records kSaveRoomHeader, kSaveRoom
//     0xE7D0  sub screen word             kSaveSscrn
//     0xE7D4  merchant                    kSaveMerchant
//     0xEAF8  CRC of 0x0000..0xEAF7       (the header CRC covers 0x2004..0x21FF)
//   bh4_system (1 block, 0x1E7C bytes used)
//     0x1E40  SYSTEM_SAVE_WORK            kSaveSystem
//     0x1E78  CRC of 0x0000..0x1E77
//
// Both CRCs are the game's (CRCCalc: CRC-32, polynomial 0x04C11DB7, MSB
// first, start 0) over the bytes in the file's order, so they are checked in
// the order the file comes in and made anew in the order it goes to.
#include "re4_card_files.h"
#include "re4_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct Re4SaveRun {
    unsigned short offset;
    unsigned char size;
    unsigned short count;
};
#include "save_layout.inc"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;

const u32 kSaveHeader = 0x2000, kSaveGlobalAt = 0x2034, kSaveItemAt = 0x572C, kSaveRoomAt = 0x6930,
          kSaveRoomEnd = 0xE7D0, kSaveSscrnAt = 0xE7D0, kSaveMerchantAt = 0xE7D4, kSaveCrc = 0xEAF8,
          kSaveBytes = 0xEAFC, kSaveVersion = 0x116;
const u32 kSystemWork = 0x1E40, kSystemCrc = 0x1E78, kSystemBytes = 0x1E7C;
const char kGame[] = "G4BE", kCompany[] = "08";
// The European release's saves (G4BP08): its makeSaveData / cGameSave::save (main.dol 0x80009bac,
// 0x8011a064) write version 0x116 with the same parts at the same places and sizes; its GLOBAL_WK
// keeps the saved block 0xC bytes later (0x4F8C), but the block inside is the same (every access
// of pG in the block lies 0xC further, 30.10.2026). Taken over as G4BE08, the id the game sees with
// every disc (dvd.cpp).
const char kGameEu[] = "G4BP";
// the save header: CRC, version, OSCalendarTime (10 words), mode
const Re4SaveRun kSaveHeaderRuns[] = {{0, 4, 13}};

static_assert(kSaveGlobalSize == 0x36F8, "GLOBAL_WK save block (game.h GameSaveBlock)");
static_assert(kSaveGlobalAt + kSaveGlobalSize == kSaveItemAt, "SAVE_GAME + SAVE_GAME_SIZE");
static_assert(kSaveItemSize == 0x1204, "SAVE_DATA2_SIZE");
static_assert(kSaveRoomSize == 0xD8 && kSaveRoomHeaderSize == 0x10, "room records");
static_assert(kSaveMerchantAt + kSaveMerchantSize <= kSaveCrc, "merchant before the CRC");
static_assert(kSaveSystemSize == 0x38 && kSystemWork + kSaveSystemSize == kSystemCrc, "SYS_WORK .. SYS_CRC");

u32 gCrcTable[256];

// CRCInit / CRCCalc of src/game/card.cpp
u32 crc(const u8* p, u32 bytes)
{
    if (!gCrcTable[1]) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i << 24;
            for (int j = 0; j < 8; j++) {
                c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : c << 1;
            }
            gCrcTable[i] = c;
        }
    }
    u32 v = 0;
    for (u32 i = 0; i < bytes; i++) {
        v = (v << 8) ^ gCrcTable[(v >> 24) ^ p[i]];
    }
    return v;
}

u32 le32(const u8* p)
{
    return (u32) p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24;
}

u32 be32(const u8* p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

void putLe32(u8* p, u32 v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (u8) (v >> (8 * i));
    }
}

void putBe32(u8* p, u32 v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (u8) (v >> (24 - 8 * i));
    }
}

u16 le16(const u8* p)
{
    return (u16) (p[0] | p[1] << 8);
}

void turn(u8* p, unsigned size)
{
    for (unsigned i = 0; i < size / 2; i++) {
        u8 t = p[i];
        p[i] = p[size - 1 - i];
        p[size - 1 - i] = t;
    }
}

template <size_t N> void turnRuns(u8* base, const Re4SaveRun (&runs)[N])
{
    for (const Re4SaveRun& r : runs) {
        for (unsigned k = 0; k < r.count; k++) {
            turn(base + r.offset + k * r.size, r.size);
        }
    }
}

template <size_t N> unsigned unknownBytes(const u8* base, const Re4SaveRun (&runs)[N])
{
    unsigned n = 0;
    for (const Re4SaveRun& r : runs) {
        for (unsigned k = 0; k < r.count; k++) {
            n += base[r.offset + k] != 0;
        }
    }
    return n;
}

void say(char* note, int size, const char* fmt, ...)
{
    if (!note || size <= 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(note, (size_t) size, fmt, ap);
    va_end(ap);
}

bool isSave(const char* name)
{
    return strncmp(name, "bh4_data", 8) == 0 && name[8] >= '0' && name[8] <= '9' && name[9] >= '0' && name[9] <= '9'
           && name[10] == 0;
}

bool isSystem(const char* name)
{
    return strcmp(name, "bh4_system") == 0;
}

// The directory entry (CARDDir, 64 bytes) from one order to the other:
// time, iconAddr, iconFormat, iconSpeed, startBlock, length, _padding1,
// commentAddr; the names and the single bytes stay.
void turnEntry(u8* e)
{
    turn(e + 0x28, 4);
    turn(e + 0x2C, 4);
    turn(e + 0x30, 2);
    turn(e + 0x32, 2);
    turn(e + 0x36, 2);
    turn(e + 0x38, 2);
    turn(e + 0x3A, 2);
    turn(e + 0x3C, 4);
}

}  // namespace

extern "C" {

int re4_gci_convert(const char* name, unsigned char* data, unsigned bytes, int toGameCube, char* note, int noteSize)
{
    // read and write the words of the file's own order
    u32 (*get)(const u8*) = toGameCube ? le32 : be32;
    void (*put)(u8*, u32) = toGameCube ? putBe32 : putLe32;
    const char* from = toGameCube ? "port" : "GameCube";
    if (isSystem(name)) {
        if (bytes < kSystemBytes) {
            say(note, noteSize, "%s: %u bytes, the system file has 0x%X", name, bytes, kSystemBytes);
            return RE4_CARD_BAD;
        }
        if (crc(data, kSystemCrc) != get(data + kSystemCrc)) {
            say(note, noteSize, "%s: CRC does not hold in %s byte order (broken, or not this game's)", name, from);
            return RE4_CARD_BAD;
        }
        turnRuns(data + kSystemWork, kSaveSystem);
        put(data + kSystemCrc, crc(data, kSystemCrc));
        say(note, noteSize, "%s: system file turned", name);
        return RE4_CARD_OK;
    }
    if (!isSave(name)) {
        say(note, noteSize, "%s: not a file of this game (bh4_system, bh4_data00..)", name);
        return RE4_CARD_BAD;
    }
    if (bytes < kSaveBytes) {
        say(note, noteSize, "%s: %u bytes, a save has 0x%X", name, bytes, kSaveBytes);
        return RE4_CARD_BAD;
    }
    bool header = crc(data + kSaveHeader + 4, 0x1FC) == get(data + kSaveHeader);
    bool whole = crc(data, kSaveCrc) == get(data + kSaveCrc);
    if (!header || !whole) {
        say(note, noteSize, "%s: %s CRC does not hold in %s byte order (broken, or not this game's)", name,
            header ? "file" : "header", from);
        return RE4_CARD_BAD;
    }
    u32 version = get(data + kSaveHeader + 4);
    if (version != kSaveVersion) {
        say(note, noteSize, "%s: save version 0x%X, this game writes 0x%X (another release of the game)", name,
            version, kSaveVersion);
        return RE4_CARD_BAD;
    }
    u32 rooms = get(data + kSaveRoomAt + 4);
    if (kSaveRoomAt + kSaveRoomHeaderSize + rooms * kSaveRoomSize > kSaveRoomEnd) {
        say(note, noteSize, "%s: %u room records do not fit", name, rooms);
        return RE4_CARD_BAD;
    }
    unsigned unknown = unknownBytes(data + kSaveGlobalAt, kSaveGlobalUnknown)
                       + unknownBytes(data + kSaveItemAt, kSaveItemUnknown)
                       + unknownBytes(data + kSaveRoomAt, kSaveRoomHeaderUnknown);
    turnRuns(data + kSaveHeader, kSaveHeaderRuns);
    turnRuns(data + kSaveGlobalAt, kSaveGlobal);
    turnRuns(data + kSaveItemAt, kSaveItem);
    turnRuns(data + kSaveRoomAt, kSaveRoomHeader);
    for (u32 i = 0; i < rooms; i++) {
        turnRuns(data + kSaveRoomAt + kSaveRoomHeaderSize + i * kSaveRoomSize, kSaveRoom);
    }
    turnRuns(data + kSaveSscrnAt, kSaveSscrn);
    turnRuns(data + kSaveMerchantAt, kSaveMerchant);
    put(data + kSaveHeader, crc(data + kSaveHeader + 4, 0x1FC));
    put(data + kSaveCrc, crc(data, kSaveCrc));
    if (unknown) {
        // bytes whose size is not known went over as bytes; say so (the
        // headers name them pad_*): a save of a later point in the game
        // may use them
        re4_log("gci: %s: %u bytes of unknown meaning are not 0 (taken over as bytes)", name, unknown);
    }
    say(note, noteSize, "%s: save turned (%u rooms%s)", name, rooms, unknown ? ", unknown bytes not 0" : "");
    return RE4_CARD_OK;
}

// The game's header of a save (port order) for the menu's list.
void re4_gci_describe(Re4CardFile* f, const unsigned char* data, unsigned bytes)
{
    if (!isSave(f->name) || bytes < kSaveBytes) {
        f->intact = isSystem(f->name) && bytes >= kSystemBytes && crc(data, kSystemCrc) == le32(data + kSystemCrc);
        return;
    }
    const u8* h = data + kSaveHeader;
    f->save = 1;
    f->intact = crc(h + 4, 0x1FC) == le32(h) && crc(data, kSaveCrc) == le32(data + kSaveCrc) && le32(h + 4) == kSaveVersion;
    // OSCalendarTime: sec, min, hour, mday, mon (0-11), year
    f->minute = (int) le32(h + 0x0C);
    f->hour = (int) le32(h + 0x10);
    f->day = (int) le32(h + 0x14);
    f->month = (int) le32(h + 0x18) + 1;
    f->year = (int) le32(h + 0x1C);
    f->chapter = h[0x3E];
    f->saveCount = le16(h + 0x40);
    f->playTime = le32(h + 0x48);
    f->room = le16(h + 0x50);
}

int re4_gci_file_name(int index, char* out, int size)
{
    u8 dir[64];
    int got = re4_card_read(index, dir, 0, 0);
    if (got < 0) {
        return got;
    }
    snprintf(out, (size_t) size, "%.2s-%.4s-%.32s.gci", (const char*) dir + 4, (const char*) dir, (const char*) dir + 8);
    return RE4_CARD_OK;
}

int re4_gci_export(int index, unsigned char* out, unsigned capacity, char* note, int noteSize)
{
    if (capacity < RE4_GCI_HEADER) {
        return RE4_CARD_NOSPACE;
    }
    int bytes = re4_card_read(index, out, out + RE4_GCI_HEADER, capacity - RE4_GCI_HEADER);
    if (bytes < 0) {
        say(note, noteSize, "file %d cannot be read (%d)", index, bytes);
        return bytes;
    }
    char name[33];
    memcpy(name, out + 8, 32);
    name[32] = 0;
    if (memcmp(out, kGame, 4) != 0 || memcmp(out + 4, kCompany, 2) != 0) {
        say(note, noteSize, "%s: of another game (%.4s%.2s)", name, (const char*) out, (const char*) out + 4);
        return RE4_CARD_BAD;
    }
    int r = re4_gci_convert(name, out + RE4_GCI_HEADER, (unsigned) bytes, 1, note, noteSize);
    if (r != RE4_CARD_OK) {
        return r;
    }
    turnEntry(out);
    re4_log("gci: %s exported (%d bytes)", name, bytes + (int) RE4_GCI_HEADER);
    return bytes + (int) RE4_GCI_HEADER;
}

int re4_gci_import(const unsigned char* gci, unsigned size, int replace, char* note, int noteSize)
{
    if (size < RE4_GCI_HEADER + RE4_CARD_BLOCK) {
        say(note, noteSize, "%u bytes: too short for a .gci", size);
        return RE4_CARD_BAD;
    }
    u8 entry[64];
    memcpy(entry, gci, 64);
    turnEntry(entry);
    char name[33];
    memcpy(name, entry + 8, 32);
    name[32] = 0;
    u32 blocks = le16(entry + 0x38);
    if (blocks == 0 || RE4_GCI_HEADER + blocks * RE4_CARD_BLOCK > size) {
        say(note, noteSize, "%s: says %u blocks, the file holds %u bytes", name, blocks, size - RE4_GCI_HEADER);
        return RE4_CARD_BAD;
    }
    bool european = memcmp(entry, kGameEu, 4) == 0;
    if ((memcmp(entry, kGame, 4) != 0 && !european) || memcmp(entry + 4, kCompany, 2) != 0) {
        say(note, noteSize, "%s: a file of another game (%.4s%.2s, this one is %s%s or %s%s)", name,
            (const char*) entry, (const char*) entry + 4, kGame, kCompany, kGameEu, kCompany);
        return RE4_CARD_BAD;
    }
    if (european) {
        memcpy(entry, kGame, 4);
    }
    u8* data = (u8*) malloc(blocks * RE4_CARD_BLOCK);
    if (!data) {
        return RE4_CARD_NOSPACE;
    }
    memcpy(data, gci + RE4_GCI_HEADER, blocks * RE4_CARD_BLOCK);
    int r = re4_gci_convert(name, data, blocks * RE4_CARD_BLOCK, 0, note, noteSize);
    if (r == RE4_CARD_OK) {
        r = re4_card_write(entry, data, replace);
        if (r == RE4_CARD_EXISTS) {
            say(note, noteSize, "%s: is on the card already", name);
        } else if (r == RE4_CARD_NOSPACE) {
            say(note, noteSize, "%s: not enough space on the card (%u blocks needed, %d free)", name, blocks,
                re4_card_free_blocks());
        } else if (r != RE4_CARD_OK) {
            say(note, noteSize, "%s: writing failed (%d)", name, r);
        } else {
            re4_log("gci: %s imported (%u blocks%s)", name, blocks, european ? ", European G4BP08 as G4BE08" : "");
        }
    }
    free(data);
    return r;
}

}  // extern "C"
