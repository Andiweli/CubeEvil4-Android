// re4_host --core-test: the embedding interface of the core (runtime/re4_core.h)
// without disc data, the way the Android app uses it. A process of its own:
// the game thread runs the Dolphin OS emulation, which the other runtime tests
// also start.
//
//   bridge  the format converter's notes give the disc source of an archive
//           entry; one snapshot of a fake game state (tests/core_probe.cpp):
//           room, step, camera, the enemy work with its parts' world matrices,
//           bind positions and its BIN/TPL sources
//   thread  re4_core_reserve / re4_core_start_entry: the boot thread runs on a
//           thread of its own, the controller set with re4_core_pad arrives in
//           PADRead, one snapshot per frame, one reader at a time, the state
//           ends with the entry's return value, the log tail is readable
//   pad     the SDK's PADClamp turns the raw controller values into the
//           game's ranges (dead zones, octagon, trigger threshold); the
//           rumble motor per frame (re4_core_rumble)
//   card    the SDK's memory card library on the port's card file
//           (tests/card_probe.cpp): format, files, a new start, no card;
//           .gci export and import
//   rel     a REL module read as a file of its own (rel/st2_1.rel): its
//           header in host order right after the load, before OSLink
//           (the game allocates bssSize first; r206 crashed on it)
//   pause   the app's menu (M11): re4_core_pause stops the game in its next
//           PADRead and lets it go again; the menu's card functions only
//           while it stands; the debug menu request is taken once
//   event   a room's event bundle (EVS): an asset one event only carries and
//           another event's packet uses as light data is converted as light
//           data in both events (the game takes the first event's copy); a
//           message table cut off after its first block word (events without
//           text) neither stops nor keeps the light data behind it (r215 /
//           r227 crashed in cLightMgr::setEnv)
//   morph   a model whose two shapes share one delta list: the list comes
//           out in host order once, not turned back (vertex indices
//           big-endian again sent the codec's face shapes far outside the
//           vertices; r229)
#include "re4_card_files.h"
#include "re4_core.h"
#include "re4_runtime.h"

#include <pthread.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern "C" void re4probe_core_setup(void* mem, void* bin, void* tpl);
extern "C" void re4probe_core_restore(void);
extern "C" void re4probe_core_display(void* mem, int what);
extern "C" void re4probe_scroll_setup(void* mem, void* bin, void* other);
extern "C" void re4probe_scroll_restore(void);
extern "C" void re4probe_shape_setup(void* mem);
extern "C" void re4probe_shape_restore(void);
extern "C" int re4probe_ax_test(void);
extern "C" int re4probe_axfx_test(void);
extern "C" int re4probe_movie_test(void);
extern "C" int re4_formats_entry_status(const void* entry);
extern "C" int re4probe_card_test(void);
extern "C" int re4probe_instant_game_test(void);

namespace {

int gFailures;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            re4_log("core-test: %s:%d: %s", __FILE__, __LINE__, #cond); \
            gFailures++;                                               \
            ok = false;                                                \
        }                                                              \
    } while (0)

void report(const char* name, bool ok)
{
    re4_log("core-test %s: %s", name, ok ? "ok" : "FAILED");
}

struct Header {  // DvdHeader (include/dvd.h)
    unsigned type, size, dest, ofs, sndType, sndArg, sndNo, x1C;
};

void put32be(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

bool near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

extern "C" int re4_formats_describe(const void* addr, char* out, int size);

extern "C" void ARStartDMA(unsigned type, unsigned mainAddr, unsigned aramAddr, unsigned length);
extern "C" unsigned char* re4_aram(void);

Re4CoreSnapshot gSnap;

// Codec archives (op/opNN.das): the second half of the entries are the radio
// call's sequence tables (SsTermMain::OpeMesSeq), whatever their tag says.
void testCodec()
{
    bool ok = true;
    unsigned char* file = (unsigned char*) (unsigned long) 0x82200000u;
    memset(file, 0, 0x800);
    unsigned char* arc = file + 0x400;
    put32be(arc + 0x00, 2);
    put32be(arc + 0x10, 0x20);
    put32be(arc + 0x14, 0x30);
    memcpy(arc + 0x18, "XMS\0SEQ\0", 8);  // a tag that names a motion sequence
    const unsigned rec[2][5] = {{1, 2, 0, 5, 30}, {0, 0, 300, 0xFFFFFFFFu, 0xFFFFFFFFu}};
    for (int r = 0; r < 2; r++) {
        unsigned char* p = arc + 0x30 + r * 16;
        p[0] = 0, p[1] = (unsigned char) rec[r][0];
        p[2] = 0, p[3] = (unsigned char) rec[r][1];
        put32be(p + 4, rec[r][2]);
        put32be(p + 8, rec[r][3]);
        put32be(p + 12, rec[r][4]);
    }
    Header h = { 0, 0x450, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("op/op01.das", 0, 0, &h, 0x82200000u);
    struct Seq {
        unsigned short be_flag;
        short no;
        int frame, mes, timer;
    } seq[2];
    memcpy(seq, arc + 0x30, sizeof(seq));
    CHECK(seq[0].be_flag == 1 && seq[0].no == 2 && seq[0].frame == 0 && seq[0].mes == 5 && seq[0].timer == 30);
    CHECK(seq[1].frame == 300 && seq[1].mes == -1 && seq[1].timer == -1);
    report("codec", ok);
}

// Files the game reads one by one outside an archive (formats.cpp): the
// chapter title of r101's bell event (etc/eng/id101.eff, id texture data in
// the EFF layout; etc/eng/event001.uwf, its screen layout) left big-endian
// crashed IDSystem::set on the Retroid (17.10.2026); the item model of the
// pickup screen (ss/cmn/itmXX.bin) hung it. Version word and an entry
// position come out in host order.
void testSingleFiles()
{
    bool ok = true;
    unsigned char* eff = (unsigned char*) (unsigned long) 0x82400000u;
    memset(eff, 0, 0x40);
    put32be(eff, 0xB);  // version, no tables
    Header h = { 0, 0x40, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("etc/eng/id101.eff", 0, 0, &h, 0x82400000u);
    unsigned version;
    memcpy(&version, eff, 4);
    CHECK(version == 0xB);
    unsigned char* uwf = (unsigned char*) (unsigned long) 0x82401000u;
    memset(uwf, 0, 0x100);
    memcpy(uwf, "1.00", 4);
    uwf[5] = 1;  // UnitNum: one ID_DATA_V1 at 8
    float x = 1.5f;
    unsigned bits;
    memcpy(&bits, &x, 4);
    put32be(uwf + 8 + 0x14, bits);  // pos.x
    Header hu = { 0, 8 + 0x88, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("etc/eng/event001.uwf", 0, 0, &hu, 0x82401000u);
    float got;
    memcpy(&got, uwf + 8 + 0x14, 4);
    CHECK(got == 1.5f);
    report("single files", ok);
}

// A REL read from disc as a file of its own: the header (version 3, bssSize
// with a low byte above 0x7F: read big-endian it is a negative size) comes
// out in host order; converting again changes nothing; a later part of the
// file (no header) is left alone.
void testRel()
{
    bool ok = true;
    CHECK(re4_mem1_init());
    unsigned char* rel = (unsigned char*) (unsigned long) 0x82300000u;
    memset(rel, 0, 0x100);
    put32be(rel + 0x00, 0x12);    // id
    put32be(rel + 0x0C, 4);       // numSections
    put32be(rel + 0x1C, 3);       // version
    put32be(rel + 0x20, 0x1ADC);  // bssSize
    put32be(rel + 0x24, 0x880);   // relOffset
    rel[0x30] = 1;                // prologSection (a byte, stays)
    Header h = { 0, 0x100, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("rel/st2_1.rel", 0, 0, &h, 0x82300000u);
    unsigned words[10];
    memcpy(words, rel, sizeof(words));
    CHECK(words[0] == 0x12 && words[3] == 4 && words[7] == 3 && words[8] == 0x1ADC && words[9] == 0x880);
    CHECK(rel[0x30] == 1);
    re4_formats_part_loaded("rel/st2_1.rel", 0, 0, &h, 0x82300000u);
    memcpy(words, rel, sizeof(words));
    CHECK(words[7] == 3 && words[8] == 0x1ADC);
    unsigned char* later = rel + 0x80;
    put32be(later + 0x1C, 3);
    Header hl = { 0, 0x40, 0, 0x2000, 0, 0, 0, 0 };
    re4_formats_part_loaded("rel/st2_1.rel", 0, 1, &hl, 0x82300080u);
    CHECK(later[0x1F] == 3 && later[0x1C] == 0);
    report("rel", ok);
}

void testBridge()
{
    bool ok = true;
    CHECK(re4_mem1_init());
    // a big-endian archive from "em/em99.drs" (part at file offset 0x400):
    // two entries with tags no converter knows, so their bytes stay as they are
    unsigned char* arc = (unsigned char*) (unsigned long) 0x82000000u;
    memset(arc, 0, 0x8000);
    put32be(arc + 0x00, 2);
    put32be(arc + 0x10, 0x20);
    put32be(arc + 0x14, 0x40);
    memcpy(arc + 0x18, "XBN\0XTP\0", 8);
    Header h = { 0, 0x60, 0, 0x400, 0, 0, 0, 0 };
    re4_formats_part_loaded("em/em99.drs", 0, 0, &h, 0x82000000u);
    Re4CoreSource src;
    CHECK(re4_formats_locate(arc + 0x40, &src));
    CHECK(strcmp(src.file, "em/em99.drs") == 0 && src.fileOffset == 0x400 && src.archiveSize == 0x60 &&
          src.entry == 1);
    // nested data (a BIN inside an SMD entry): the entry around it and the offset in it
    CHECK(re4_formats_locate(arc + 0x44, &src) && src.entry == 1 && src.innerOffset == 4 &&
          strcmp(src.file, "em/em99.drs") == 0);
    CHECK(re4_formats_locate(arc + 0x40, &src) && src.entry == 1 && src.innerOffset == 0);
    CHECK(!re4_formats_locate(arc + 0x9000, &src) && src.entry == -1 && src.file[0] == 0);
    // r100 loads far more archives than the first note table held (64): the
    // first one must still be found after 100 more
    for (unsigned k = 0; k < 100; k++) {
        unsigned char* more = (unsigned char*) (unsigned long) (0x82100000u + k * 0x100);
        memset(more, 0, 0x100);
        put32be(more + 0x00, 1);
        put32be(more + 0x10, 0x20);
        memcpy(more + 0x14, "XBN\0", 4);
        char name[32];
        snprintf(name, sizeof(name), "em/t%02u.drs", k);
        Header hk = { 0, 0x40, 0, 0x400, 0, 0, 0, 0 };
        re4_formats_part_loaded(name, 0, 0, &hk, 0x82100000u + k * 0x100);
    }
    CHECK(re4_formats_locate(arc + 0x40, &src) && strcmp(src.file, "em/em99.drs") == 0 && src.entry == 1);
    CHECK(re4_formats_locate((unsigned char*) (unsigned long) 0x82106320u, &src) &&
          strcmp(src.file, "em/t99.drs") == 0 && src.entry == 0);
    char where[160];
    re4_formats_describe(arc + 0x44, where, sizeof(where));
    CHECK(strstr(where, "inside") && strstr(where, "+0x44"));
    re4_formats_describe((void*) (unsigned long) 0x82F00000u, where, sizeof(where));
    CHECK(strstr(where, "not in a noted archive") != 0);
    // memory reused: another archive loaded over em/t98.drs makes its notes stale
    {
        unsigned char* reuse = (unsigned char*) (unsigned long) 0x82106200u;
        memset(reuse, 0, 0x100);
        put32be(reuse + 0x00, 1);
        put32be(reuse + 0x10, 0x40);
        memcpy(reuse + 0x14, "XBN\0", 4);
        Header hr = { 0, 0x60, 0, 0x800, 0, 0, 0, 0 };
        re4_formats_part_loaded("em/new.drs", 0, 0, &hr, 0x82106200u);
        CHECK(!re4_formats_locate(reuse + 0x20, &src));  // the old entry start
        CHECK(re4_formats_locate(reuse + 0x40, &src) && strcmp(src.file, "em/new.drs") == 0 && src.entry == 0);
        // and data loaded over an archive without a note of its own (its tags change)
        unsigned char* plain = (unsigned char*) (unsigned long) 0x82106000u;  // em/t96.drs
        CHECK(re4_formats_locate(plain + 0x24, &src) && strcmp(src.file, "em/t96.drs") == 0 &&
              src.innerOffset == 4);
        memset(plain, 0x5A, 0x40);
        // any new note makes the lookups run again
        unsigned char* other = (unsigned char*) (unsigned long) 0x82107000u;
        memset(other, 0, 0x40);
        put32be(other + 0x00, 1);
        put32be(other + 0x10, 0x20);
        memcpy(other + 0x14, "XBN\0", 4);
        Header hx = { 0, 0x40, 0, 0x400, 0, 0, 0, 0 };
        re4_formats_part_loaded("em/other.drs", 0, 0, &hx, 0x82107000u);
        CHECK(!re4_formats_locate(plain + 0x24, &src));
        // a later load over the middle of an archive: only that part is stale
        unsigned char* big = (unsigned char*) (unsigned long) 0x82108000u;
        memset(big, 0, 0x80);
        put32be(big + 0x00, 3);
        put32be(big + 0x10, 0x40);
        put32be(big + 0x14, 0x50);
        put32be(big + 0x18, 0x60);
        memcpy(big + 0x1C, "XBN\0XTP\0XBN\0", 12);
        Header hb = { 0, 0x80, 0, 0x400, 0, 0, 0, 0 };
        re4_formats_part_loaded("em/big.drs", 0, 0, &hb, 0x82108000u);
        CHECK(re4_formats_locate(big + 0x54, &src) && strcmp(src.file, "em/big.drs") == 0 && src.entry == 1 &&
              src.innerOffset == 4);
        unsigned char* middle = big + 0x50;
        memset(middle, 0, 0x40);  // reaches past the end of em/big.drs
        put32be(middle + 0x00, 1);
        put32be(middle + 0x10, 0x20);
        memcpy(middle + 0x14, "XBN\0", 4);
        Header hm = { 0, 0x40, 0, 0x800, 0, 0, 0, 0 };
        re4_formats_part_loaded("em/middle.drs", 0, 0, &hm, 0x82108050u);
        CHECK(re4_formats_locate(big + 0x40, &src) && strcmp(src.file, "em/big.drs") == 0 && src.entry == 0);
        CHECK(!re4_formats_locate(big + 0x54, &src));
        CHECK(re4_formats_locate(big + 0x70, &src) && strcmp(src.file, "em/middle.drs") == 0 && src.entry == 0);
        re4_formats_describe(big + 0x48, where, sizeof(where));
        CHECK(strstr(where, "no entry starts there") != 0);
        re4_formats_describe(big + 0x54, where, sizeof(where));
        CHECK(strstr(where, "overwritten by a later load") != 0);
        // an event file (EVD): its models are found by their offset in the file
        // (entry -2); a sub screen swapping the memory out suspends the note
        // until the data is back
        unsigned char* evd = (unsigned char*) (unsigned long) 0x8210A000u;
        memset(evd, 0, 0x200);
        memcpy(evd, "event", 5);
        memcpy(evd + 0x20, "r100", 4);
        memcpy(evd + 0x28, "s00", 3);
        put32be(evd + 0x40, 0x50);  // packets (none)
        put32be(evd + 0x4C, 0x50);  // bin table (no entries)
        Header he = { 0, 0x200, 0, 0x400, 0, 0, 0, 0 };
        re4_formats_part_loaded("Evd/r100s00.evd", 0, 0, &he, 0x8210A000u);
        CHECK(re4_formats_locate(evd + 0x123, &src) && strcmp(src.file, "evd/r100s00.evd") == 0 &&
              src.entry == -2 && src.innerOffset == 0x123 && src.fileOffset == 0x400 && src.archiveSize == 0x200);
        unsigned char saved[0x40];
        memcpy(saved, evd, sizeof(saved));
        memset(evd, 0x77, sizeof(saved));  // swapped out
        unsigned char* trigger = (unsigned char*) (unsigned long) 0x8210C000u;
        memset(trigger, 0, 0x40);
        put32be(trigger + 0x00, 1);
        put32be(trigger + 0x10, 0x20);
        memcpy(trigger + 0x14, "XBN\0", 4);
        Header ht = { 0, 0x40, 0, 0x400, 0, 0, 0, 0 };
        re4_formats_part_loaded("em/trigger.drs", 0, 0, &ht, 0x8210C000u);
        CHECK(!re4_formats_locate(evd + 0x123, &src));
        memcpy(evd, saved, sizeof(saved));  // back again
        memset(trigger, 0, 0x40);
        put32be(trigger + 0x00, 1);
        put32be(trigger + 0x10, 0x20);
        memcpy(trigger + 0x14, "XBN\0", 4);
        re4_formats_part_loaded("em/trigger2.drs", 0, 0, &ht, 0x8210C000u);
        CHECK(re4_formats_locate(evd + 0x123, &src) && src.entry == -2 && src.innerOffset == 0x123);
        // an event file loaded to ARAM (converted and noted there) and copied
        // to MEM1 when the event plays: found through the copy
        {
            unsigned aramAt = 0x00F00000u;  // high in ARAM, away from the game's stack
            unsigned char* inAram = re4_aram() + aramAt;
            memset(inAram, 0, 0x200);
            memcpy(inAram, "event", 5);
            memcpy(inAram + 0x20, "r100", 4);
            memcpy(inAram + 0x28, "s03", 3);
            put32be(inAram + 0x40, 0x50);
            put32be(inAram + 0x4C, 0x50);
            Header ha = { 3 /* TRANS_ARAM */, 0x200, 0, 0x400, 0, 0, 0, 0 };
            re4_formats_part_loaded("Evd/r100s03.evd", 0, 0, &ha, aramAt);
            unsigned char* inMem1 = (unsigned char*) (unsigned long) 0x8210E000u;
            ARStartDMA(1, 0x8210E000u, aramAt, 0x200);
            CHECK(memcmp(inMem1, "event", 5) == 0);
            CHECK(re4_formats_locate(inMem1 + 0x180, &src) && strcmp(src.file, "evd/r100s03.evd") == 0 &&
                  src.entry == -2 && src.innerOffset == 0x180);
            // an archive loaded there later wins over the copy
            memset(inMem1, 0, 0x60);
            put32be(inMem1 + 0x00, 1);
            put32be(inMem1 + 0x10, 0x20);
            memcpy(inMem1 + 0x14, "XBN\0", 4);
            Header hl = { 0, 0x60, 0, 0x400, 0, 0, 0, 0 };
            re4_formats_part_loaded("em/later.drs", 0, 0, &hl, 0x8210E000u);
            CHECK(re4_formats_locate(inMem1 + 0x20, &src) && strcmp(src.file, "em/later.drs") == 0 && src.entry == 0);
        }
        // an event borrows an enemy's archive memory (MemorySwap, patch 0028):
        // the event file goes from ARAM to the enemy's MRAM, the enemy's
        // archive to ARAM; the notes follow both ways and back
        {
            unsigned aramAt = 0x00E00000u;
            unsigned char* ev = re4_aram() + aramAt;
            memset(ev, 0, 0x200);
            memcpy(ev, "event", 5);
            memcpy(ev + 0x20, "r100", 4);
            memcpy(ev + 0x28, "s00", 3);
            put32be(ev + 0x40, 0x50);
            put32be(ev + 0x4C, 0x50);
            Header hv = { 3 /* TRANS_ARAM */, 0x200, 0, 0x400, 0, 0, 0, 0 };
            re4_formats_part_loaded("Evd/r100s00.evd", 0, 0, &hv, aramAt);
            unsigned char* enemy = (unsigned char*) (unsigned long) 0x82110000u;
            memset(enemy, 0, 0x200);
            put32be(enemy + 0x00, 1);
            put32be(enemy + 0x10, 0x40);
            memcpy(enemy + 0x14, "XBN\0", 4);
            Header hn = { 0, 0x200, 0, 0x400, 0, 0, 0, 0 };
            re4_formats_part_loaded("em/em12.drs", 0, 0, &hn, 0x82110000u);
            CHECK(re4_formats_locate(enemy + 0x40, &src) && strcmp(src.file, "em/em12.drs") == 0);
            unsigned char tmp[0x200];
            memcpy(tmp, enemy, 0x200);  // what MemorySwap does
            memcpy(enemy, ev, 0x200);
            memcpy(ev, tmp, 0x200);
            re4_formats_memory_swapped(enemy, aramAt, 0x200);
            CHECK(re4_formats_locate(enemy + 0x120, &src) && strcmp(src.file, "evd/r100s00.evd") == 0 &&
                  src.entry == -2 && src.innerOffset == 0x120);
            memcpy(tmp, enemy, 0x200);  // and back after the event
            memcpy(enemy, ev, 0x200);
            memcpy(ev, tmp, 0x200);
            re4_formats_memory_swapped(enemy, aramAt, 0x200);
            CHECK(re4_formats_locate(enemy + 0x40, &src) && strcmp(src.file, "em/em12.drs") == 0 && src.entry == 0);
        }
        // motion curves (FCV): a joint the game reads is converted, an unused
        // joint whose offset points into the header is left alone (weapon
        // motions have such joints) and does not stop the conversion
        unsigned char* fa = (unsigned char*) (unsigned long) 0x82109000u;
        memset(fa, 0, 0x60);
        put32be(fa + 0x00, 1);
        put32be(fa + 0x10, 0x20);
        memcpy(fa + 0x14, "FCV\0", 4);
        unsigned char* m = fa + 0x20;
        m[0] = 0; m[1] = 10;                 // maxFrame
        m[2] = 2;                            // joints
        m[3] = 0xF0; m[4] = 0x02;            // joint 0: key layout 15 (4 bytes), rotation
        m[5] = 0xF0; m[6] = 0x00;            // joint 1: kind 0, never read
        put32be(m + 12, 48);                 // size; offsets follow
        put32be(m + 16, 24);
        put32be(m + 20, 4);                  // points into the header
        for (int axis = 0; axis < 3; axis++) {
            unsigned char* a = m + 24 + axis * 8;
            a[1] = 1;                        // one key
            a[3] = (unsigned char) (5 + axis);  // at frame 5 + axis
            a[4] = 0x3F; a[5] = 0x80;        // key bytes stay big-endian
        }
        Header hf = { 0, 0x50, 0, 0x400, 0, 0, 0, 0 };
        re4_formats_part_loaded("em/fcv.drs", 0, 0, &hf, 0x82109000u);
        unsigned short v;
        memcpy(&v, m + 0, 2);
        CHECK(v == 10);
        memcpy(&v, m + 3, 2);
        CHECK(v == 0xF002);  // the kind words once in host order (joint 1 did not swap them back)
        memcpy(&v, m + 5, 2);
        CHECK(v == 0xF000);
        for (int axis = 0; axis < 3; axis++) {
            memcpy(&v, m + 24 + axis * 8, 2);
            CHECK(v == 1);
            memcpy(&v, m + 26 + axis * 8, 2);
            CHECK(v == 5 + axis);
            CHECK(m[28 + axis * 8] == 0x3F && m[29 + axis * 8] == 0x80);
        }
    }

    re4probe_core_setup(arc + 0x1000, arc + 0x20, arc + 0x40);
    // what the game leaves out (trans.cpp): held by an event unless it is
    // setNoSuspend, the player under DPF_PL, an enemy under DPF_EM
    {
        const int want[6] = {0, 1, 0, 1, 1, 1};
        for (int what = 1; what <= 5; what++) {
            re4probe_core_display(arc + 0x1000, what);
            re4_bridge_capture(&gSnap);
            CHECK(gSnap.nModels == 1 && (gSnap.models[0].flags & RE4_CORE_MODEL_HIDDEN) == want[what] &&
                  (gSnap.models[0].beFlag & 6) == 6);
        }
        re4probe_core_display(arc + 0x1000, 0);
    }
    re4_bridge_capture(&gSnap);
    re4probe_core_restore();
    CHECK(gSnap.models[0].flags == 0);
    // how the game draws it: depth test only, the first info added on with
    // half its material alpha, the others plain
    CHECK(near(gSnap.models[0].opacity, 1.0f) && gSnap.models[0].zMode == 1 && gSnap.models[0].cullMode == 0);
    CHECK(gSnap.infos[0].blendMode == 2 && gSnap.infos[0].color[3] == 0x80 && near(gSnap.infos[0].opacity, 1.0f));
    CHECK(gSnap.infos[1].blendMode == 0 && gSnap.infos[1].color[3] == 0xFF);
    CHECK(gSnap.apiVersion == RE4_CORE_API_VERSION);
    CHECK(gSnap.room == 0x100 && gSnap.rno[0] == 3);
    CHECK(gSnap.cameraValid && near(gSnap.cameraEye[2], 3.0f) && near(gSnap.cameraTarget[0], 4.0f) &&
          near(gSnap.cameraRoll, 0.25f) && near(gSnap.cameraFovy, 50.0f));
    CHECK(gSnap.nModels == 1 && gSnap.nParts == 2 && gSnap.droppedModels == 0);
    const Re4CoreModel& m = gSnap.models[0];
    CHECK(m.address == 0x82001000u && m.manager == 0 && m.id == 0x12 && m.type == 3 && (m.beFlag & 6) == 6);
    CHECK(m.firstMatrix == 0 && m.nParts == 2 && near(m.matrix[3], 100.0f) && near(m.matrix[11], -50.0f));
    for (int i = 0; i < 2; i++) {
        const Re4CorePart& p = gSnap.parts[i];
        CHECK(near(p.matrix[0], 1.0f) && near(p.matrix[7], 10.0f * (i + 1)));
        CHECK(near(p.bind[0], 0.0f) && near(p.bind[1], 10.0f * (i + 1)) && near(p.bind[2], 0.0f));
    }
    // all nine infos (Leon has nine; the hair is the sixth)
    CHECK(m.firstInfo == 0 && m.nInfos == 9 && gSnap.nInfos == 9 && gSnap.droppedInfos == 0);
    const Re4CoreModelInfo* mi = gSnap.infos + m.firstInfo;
    CHECK(mi[0].binAddress == 0x82000020u && mi[0].tplAddress == 0x82000040u);
    // how the game draws the infos: the first skinned, the second rigid on parts 1
    CHECK(mi[0].flags == 8 && mi[0].rigidPart == -1 && near(mi[0].matrix[0], 1.0f));
    CHECK(mi[1].flags == 8 && mi[1].rigidPart == 1 && near(mi[1].matrix[7], 5.0f));
    CHECK(strcmp(mi[0].bin.file, "em/em99.drs") == 0 && mi[0].bin.entry == 0 &&
          mi[0].tpl.entry == 1 && mi[0].tpl.fileOffset == 0x400);
    CHECK(mi[8].flags == 0x48 && mi[8].rigidPart == -1 && mi[8].binAddress == 0x82000020u &&
          strcmp(mi[8].tpl.file, "em/em99.drs") == 0);
    // scenery: which SMD work each scroll object shows (patch 0029)
    memset(arc + 0x4000, 0, 0x2000);
    re4probe_scroll_setup(arc + 0x4000, arc + 0x20, arc + 0x40);
    re4_bridge_capture(&gSnap);
    re4probe_scroll_restore();
    CHECK(gSnap.nModels == 3);
    if (gSnap.nModels == 3) {
        CHECK(gSnap.models[0].manager == 1 && gSnap.models[0].id == 2 && gSnap.models[0].scrollIndex == 5 &&
              gSnap.models[0].scrollBlock == -1);
        CHECK(gSnap.models[1].scrollIndex == RE4_CORE_NO_SCROLL);  // a room script's scenery
        CHECK(gSnap.models[2].scrollIndex == RE4_CORE_NO_SCROLL);  // the slot holds other data now
        CHECK(near(gSnap.models[1].matrix[3], 10.0f));
    }
    // a face with a running shape: the positions the game's shape code makes
    {
        unsigned char* mem = (unsigned char*) (unsigned long) 0x82500000u;
        memset(mem, 0, 0x4000);
        re4probe_shape_setup(mem);
        re4_bridge_capture(&gSnap);  // without the bridge view: no shapes
        for (unsigned i = 0; i < gSnap.nModels; i++) {
            CHECK(gSnap.models[i].address != 0x82500000u || gSnap.nMorph == 0);
        }
        re4_bridge_shapes(1);
        re4_bridge_capture(&gSnap);
        re4probe_shape_restore();
        bool found = false;
        for (unsigned i = 0; i < gSnap.nModels && !found; i++) {
            const Re4CoreModel& m = gSnap.models[i];
            if (m.address != 0x82500000u || m.nInfos != 1) {
                continue;
            }
            found = true;
            const Re4CoreModelInfo& mi = gSnap.infos[m.firstInfo];
            CHECK(mi.nMorph == 2 && gSnap.nMorph >= 2 && gSnap.droppedMorph == 0);
            if (mi.nMorph == 2) {
                const short* a = gSnap.morph[mi.firstMorph];
                const short* b = gSnap.morph[mi.firstMorph + 1];
                re4_log("core-test shape: (%d %d %d) (%d %d %d)", a[0], a[1], a[2], b[0], b[1], b[2]);
                CHECK(a[0] == 10 && a[1] == 20 && a[2] == 30);
                CHECK(b[0] == 44 && b[1] == 42 && b[2] == 72);
            }
        }
        CHECK(found);
        // the channel names entry 3 of a one-entry table (r229's codec face):
        // nothing is added, as on the GameCube (patch 0064)
        memset(mem, 0, 0x4000);
        re4probe_shape_setup(mem);
        mem[0x3800 + 5] = 3;  // ShapeData idx[0]
        // behind the table, bytes that read as an entry with one delta
        const unsigned behind[2] = { 0x10, 1 };
        memcpy(mem + 0x3600 + 3 * 8, behind, sizeof(behind));
        re4_bridge_capture(&gSnap);
        re4_bridge_shapes(0);
        re4probe_shape_restore();
        found = false;
        for (unsigned i = 0; i < gSnap.nModels && !found; i++) {
            const Re4CoreModel& m = gSnap.models[i];
            if (m.address != 0x82500000u || m.nInfos != 1) {
                continue;
            }
            found = true;
            const Re4CoreModelInfo& mi = gSnap.infos[m.firstInfo];
            CHECK(mi.nMorph == 2);
            if (mi.nMorph == 2) {
                const short* b = gSnap.morph[mi.firstMorph + 1];
                CHECK(b[0] == 40 && b[1] == 50 && b[2] == 60);
            }
        }
        CHECK(found);
    }
    report("bridge", ok);
}

void put16be(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char) (v >> 8);
    p[1] = (unsigned char) v;
}

unsigned get32(const unsigned char* p)
{
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

unsigned get16(const unsigned char* p)
{
    unsigned short v;
    memcpy(&v, p, 2);
    return v;
}

// A sound bank (DvdHeader type 1, an enemy's block) with a random SE table
// behind its wavetable: seRandomCheck reads header word 1, the table entry of
// the SIT's random group and the list it points to.
void testSound()
{
    bool ok = true;
    unsigned char* blk = (unsigned char*) (unsigned long) 0x82200000u;
    memset(blk, 0, 0x100);
    put32be(blk + 0x00, 0x20);  // ISS block
    put32be(blk + 0x04, 0xC0);  // random SE table
    put32be(blk + 0x20, 2);     // SITs
    put32be(blk + 0x24, 0x40);  // wavetable (at 0x60)
    put32be(blk + 0x28, 0x10);  // SIT array (at 0x30)
    put16be(blk + 0x30, 0x0102);           // SIT 0: note, no random group
    put16be(blk + 0x30 + 0x16, 0x0004);    //        flag
    put16be(blk + 0x48, 0x0304);           // SIT 1: note
    blk[0x48 + 0x13] = 2;                  //        random group 2
    put32be(blk + 0x60 + 4, 0x18);  // wavetable: melodic instruments at 0x78
    put32be(blk + 0x60 + 20, 0x20); // ADPCM table at 0x80 (one entry): ends at the random table
    put16be(blk + 0x78, 0x0005);
    put16be(blk + 0x80, 0x1234);
    put32be(blk + 0xC0 + 8, 0x10);  // group 2: list at 0xD0
    put16be(blk + 0xD0, 3);         // num
    put16be(blk + 0xD4, 5);
    put16be(blk + 0xD6, 6);
    put16be(blk + 0xD8, 7);
    put16be(blk + 0xDA, 0xBEEF);    // after the list: not touched
    Header h = { 1, 0xE0, 0, 0, 8, 0, 0, 0 };
    re4_formats_part_loaded("em/snd.dat", 0, 0, &h, 0x82200000u);
    CHECK(get32(blk + 0x00) == 0x20 && get32(blk + 0x04) == 0xC0);
    CHECK(get32(blk + 0x20) == 2 && get32(blk + 0x28) == 0x10);
    CHECK(get16(blk + 0x30) == 0x0102 && get16(blk + 0x48) == 0x0304 && blk[0x48 + 0x13] == 2);
    CHECK(get16(blk + 0x78) == 5 && get16(blk + 0x80) == 0x1234);
    // the table the game reads (seRandomCheck)
    unsigned char* tbl = blk + get32(blk + 4);
    CHECK(get32(tbl + 2 * 4) == 0x10);
    unsigned char* list = tbl + get32(tbl + 2 * 4);
    CHECK(get16(list) == 3 && get16(list + 2) == 0 && get16(list + 4) == 5 && get16(list + 6) == 6 &&
          get16(list + 8) == 7);
    CHECK(blk[0xDA] == 0xBE && blk[0xDB] == 0xEF);
    report("sound", ok);
}

void putf32be(unsigned char* p, float f)
{
    unsigned v;
    memcpy(&v, &f, 4);
    put32be(p, v);
}

float getf32(const unsigned char* p)
{
    float f;
    memcpy(&f, p, 4);
    return f;
}

// A room camera (CAM in an archive) as r100 has them: a shoulder camera cut
// (Id 8, CameraQuasiFPS::setAreaData) with nPoint 12, of which the game reads
// 24 positions (near and far points) and 12 targets, rolls and angles, and a
// pFrame the camera tool left pointing outside the file (Id 8 never reads it);
// then a motion cut (Id 6, HermiteExport: frame times and nPoint keys).
void testCamera()
{
    bool ok = true;
    unsigned char* arc = (unsigned char*) (unsigned long) 0x82300000u;
    memset(arc, 0, 0x400);
    put32be(arc + 0x00, 1);
    put32be(arc + 0x10, 0x20);
    memcpy(arc + 0x14, "CAM\0", 4);
    unsigned char* cam = arc + 0x20;
    memcpy(cam, "B404", 4);
    cam[4] = 2;  // cuts
    cam[5] = 1;  // areas
    put32be(cam + 0x10 + 8, 0x20);   // CUT_INFO: area
    put32be(cam + 0x10 + 12, 0x50);  //           cut
    cam[0x20] = 1;                    // AREA_DATA
    putf32be(cam + 0x20 + 0x20, 2000.0f);
    put32be(cam + 0x20 + 0x28, 1);    // one vertex
    put32be(cam + 0x20 + 0x2C, 0xB8);
    putf32be(cam + 0xB8, 7.0f);
    unsigned char* c = cam + 0x50;    // CAMERA_DATA 0: shoulder camera
    c[0] = 1;
    c[2] = 8;
    c[3] = 0x30;
    putf32be(c + 0x08, 1000.0f);  // offset.y
    put32be(c + 0x10, 0x818C3400u);  // pFrame: a stale pointer, never read
    putf32be(c + 0x14, 0.25f);    // floor_ratio
    put32be(c + 0x20, 12);        // nPoint
    put32be(c + 0x24, 0xC4);      // pCampos: 24
    put32be(c + 0x28, 0x1E4);     // pTarget: 12
    put32be(c + 0x2C, 0x274);     // pRoll: 12
    put32be(c + 0x30, 0x2A4);     // pFovy: 12
    for (int k = 0; k < 24; k++) {
        putf32be(cam + 0xC4 + k * 12 + 0, (float) k);
        putf32be(cam + 0xC4 + k * 12 + 4, 1000.0f + k);
        putf32be(cam + 0xC4 + k * 12 + 8, -2000.0f - k);
    }
    for (int k = 0; k < 12; k++) {
        putf32be(cam + 0x1E4 + k * 12 + 4, 1500.0f + k);
        putf32be(cam + 0x274 + k * 4, 0.5f);
        putf32be(cam + 0x2A4 + k * 4, 50.0f);
    }
    unsigned char* m = cam + 0x84;    // CAMERA_DATA 1: motion cut, two keys
    m[0] = 1;
    m[1] = 1;
    m[2] = 6;
    put32be(m + 0x10, 0x2D4);     // pFrame
    put32be(m + 0x20, 2);
    put32be(m + 0x24, 0x2D8);
    put32be(m + 0x28, 0x2F0);
    put32be(m + 0x2C, 0x308);
    put32be(m + 0x30, 0x310);
    put16be(cam + 0x2D4, 0);
    put16be(cam + 0x2D6, 30);
    for (int k = 0; k < 2; k++) {
        putf32be(cam + 0x2D8 + k * 12, 300.0f + k);
        putf32be(cam + 0x2F0 + k * 12 + 4, 400.0f + k);
        putf32be(cam + 0x308 + k * 4, 0.25f);
        putf32be(cam + 0x310 + k * 4, 45.0f);
    }
    put32be(cam + 0x318, 0xDEADBEEFu);  // after the arrays: not touched
    Header h = { 0, 0x340, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("em/cam.das", 0, 0, &h, 0x82300000u);
    CHECK(memcmp(cam, "B404", 4) == 0 && get32(cam + 0x50 + 0x20) == 12 && get32(cam + 0x50 + 0x24) == 0xC4);
    CHECK(get32(cam + 0x50 + 0x10) == 0x818C3400u && getf32(cam + 0xB8) == 7.0f);
    for (int k = 0; k < 24; k++) {
        CHECK(getf32(cam + 0xC4 + k * 12) == (float) k && getf32(cam + 0xC4 + k * 12 + 4) == 1000.0f + k &&
              getf32(cam + 0xC4 + k * 12 + 8) == -2000.0f - k);
    }
    for (int k = 0; k < 12; k++) {
        CHECK(getf32(cam + 0x1E4 + k * 12 + 4) == 1500.0f + k && getf32(cam + 0x274 + k * 4) == 0.5f &&
              getf32(cam + 0x2A4 + k * 4) == 50.0f);
    }
    // the motion cut after it is converted completely, frame times included
    CHECK(get16(cam + 0x2D4) == 0 && get16(cam + 0x2D6) == 30);
    CHECK(getf32(cam + 0x2D8 + 12) == 301.0f && getf32(cam + 0x2F0 + 12 + 4) == 401.0f &&
          getf32(cam + 0x308 + 4) == 0.25f && getf32(cam + 0x310 + 4) == 45.0f);
    CHECK(cam[0x318] == 0xDE && cam[0x31B] == 0xEF);
    report("camera", ok);
}

// A room archive with an event bundle (EVS) of two events. Event A carries
// the light data "r215_lit" (no extension) but no packet of its own uses
// it; event B's Lit packet names it and B has a copy of its own. The game
// registers A's copy first and gives it to B's packet (DatTbl::SetDat keeps
// a name once), so A's copy has to be converted as light data too. In B a
// 32-byte message table comes first, its block cut off after x0 (as in
// r215s02.evd): the light data behind it is still converted.
void putLit(unsigned char* lit)
{
    put16be(lit + 0x00, 1);       // CutNum
    lit[0x02] = 0x2C;             // Version
    lit[0x03] = 4;                // nMaxLight
    put32be(lit + 0x04, 0x10);    // cut 0
    unsigned char* env = lit + 0x10;
    put32be(env + 0x04, 0);       // nLight
    put32be(env + 0x08, 1);       // Fog.Type
    putf32be(env + 0x0C, 1000.0f);  // Fog.Start
    putf32be(env + 0x10, 8000.0f);  // Fog.End
}

void putEvd(unsigned char* e, unsigned pacSize, unsigned nBin, unsigned binOfs)
{
    memcpy(e, "event", 5);
    put32be(e + 0x40, 0x50);      // pacOfs
    put32be(e + 0x44, pacSize);
    put32be(e + 0x48, nBin);
    put32be(e + 0x4C, binOfs);
}

void putPacket(unsigned char* p, unsigned id, unsigned size)
{
    put32be(p + 0x00, id);
    put16be(p + 0x0C, (unsigned short) size);
}

bool litConverted(const unsigned char* lit)
{
    return get16(lit) == 1 && get32(lit + 4) == 0x10 && get32(lit + 0x10 + 8) == 1 &&
           getf32(lit + 0x10 + 0x0C) == 1000.0f && getf32(lit + 0x10 + 0x10) == 8000.0f;
}

void testEvent()
{
    bool ok = true;
    unsigned char* arc = (unsigned char*) (unsigned long) 0x82300000u;
    memset(arc, 0, 0x600);
    put32be(arc + 0x00, 1);
    put32be(arc + 0x10, 0x20);
    memcpy(arc + 0x14, "EVS\0", 4);
    unsigned char* evs = arc + 0x20;
    put32be(evs + 0x00, 2);       // events
    put32be(evs + 0x04, 8);       // table
    put32be(evs + 0x08, 0x20);    // event A
    put32be(evs + 0x10, 0x200);   // event B
    unsigned char* a = evs + 0x20;
    putEvd(a, 0x10, 1, 0x60);
    putPacket(a + 0x50, 27, 0x10);  // EndPac
    memcpy(a + 0x60, "r215_lit", 8);
    put32be(a + 0x60 + 0x30, 0xA0);
    putLit(a + 0xA0);
    unsigned char* b = evs + 0x200;
    putEvd(b, 0x30, 2, 0x80);
    putPacket(b + 0x50, 14, 0x20);  // Lit
    memcpy(b + 0x50 + 0x10, "r215_lit", 8);
    putPacket(b + 0x70, 27, 0x10);  // EndPac
    memcpy(b + 0x80, "r215s02.mdt", 11);
    put32be(b + 0x80 + 0x30, 0x100);
    memcpy(b + 0xC0, "r215_lit", 8);
    put32be(b + 0xC0 + 0x30, 0x120);
    unsigned char* mdt = b + 0x100;  // six languages, one block at 0x1C: x0 only
    put32be(mdt, 6);
    for (int k = 1; k <= 6; k++) {
        put32be(mdt + k * 4, 0x1C);
    }
    put32be(mdt + 0x1C, 0x11223344u);
    putLit(b + 0x120);
    Header h = { 0, 0x500, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("em/evs.das", 0, 0, &h, 0x82300000u);
    CHECK(get32(evs) == 2 && get32(evs + 4) == 8 && get32(evs + 0x10) == 0x200);
    CHECK(get32(a + 0x40) == 0x50 && get32(a + 0x60 + 0x30) == 0xA0);
    CHECK(get32(mdt) == 6 && get32(mdt + 4) == 0x1C && get32(mdt + 0x18) == 0x1C && get32(mdt + 0x1C) == 0x11223344u);
    CHECK(litConverted(b + 0x120));
    CHECK(litConverted(a + 0xA0));  // the copy the game uses
    report("event", ok);
}

// Empty tables the game never reads (the room tour, 22.10.2026): an event bundle without events
// whose table offset is the tools' fill 0xCDCDCDCD (EventMgr::SetEvs; r101, r111, r400 ...) and an
// item pack without items whose BIN and TPL table offsets are 0 (cItmSys::DataLoad; r215). The
// converters stopped at the tables and left both entries big-endian; both count as converted now.
void testEmptyTables()
{
    bool ok = true;
    unsigned char* arc = (unsigned char*) (unsigned long) 0x82340000u;
    memset(arc, 0, 0x100);
    put32be(arc + 0x00, 2);
    put32be(arc + 0x10, 0x20);
    put32be(arc + 0x14, 0x40);
    memcpy(arc + 0x18, "EVS\0", 4);
    memcpy(arc + 0x1C, "ITM\0", 4);
    put32be(arc + 0x24, 0xCDCDCDCDu);  // EVS: no events, the table offset is fill
    unsigned char* itm = arc + 0x40;
    put32be(itm + 0x00, 3);
    put32be(itm + 0x04, 0x10);  // the id table: no ids
    Header h = { 0, 0x80, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("em/empty.das", 0, 0, &h, 0x82340000u);
    CHECK(re4_formats_entry_status(arc + 0x20) == 1);
    CHECK(re4_formats_entry_status(itm) == 1);
    CHECK(get32(itm) == 3 && get32(itm + 4) == 0x10 && get32(itm + 8) == 0);
    report("empty tables", ok);
}

// An effect model whose motion slot holds a texture with its zero bytes stored as 0x20, as r10b's
// effect model 0x7c (st1/r10b.das #7, the room tour 22.10.2026): EFF version 0xB with one effect
// model (id 0x7c, motion table of one entry). The motion converter read 175 joints from it and
// stopped; the entry counts as converted now, the texture stays as it is.
void testEffectTextureMotion()
{
    bool ok = true;
    unsigned char* arc = (unsigned char*) (unsigned long) 0x82350000u;
    memset(arc, 0, 0x100);
    put32be(arc + 0x00, 1);
    put32be(arc + 0x10, 0x20);
    memcpy(arc + 0x14, "EFF", 4);
    unsigned char* eff = arc + 0x20;
    put32be(eff + 0x00, 0xB);
    put32be(eff + 0x14, 0x30);  // effect model ids
    put32be(eff + 0x2C, 0x40);  // effect models
    put32be(eff + 0x30, 1);
    put16be(eff + 0x34, 0x7C);
    put32be(eff + 0x40, 1);
    put32be(eff + 0x44, 0x10);  // the model entry at 0x50
    unsigned char* efm = eff + 0x50;
    put32be(efm + 0x00, 4);
    put32be(efm + 0x0C, 0x20);  // motion table at 0x70
    put32be(efm + 0x20, 1);
    put32be(efm + 0x24, 0x10);  // "motion" 0 at 0x80
    unsigned char* tex = efm + 0x30;
    memset(tex, 0x20, 0x40);
    put32be(tex + 0x00, 0x2020AF30u);  // TPL magic, zeros as 0x20
    put32be(tex + 0x04, 0x20202001u);
    put32be(tex + 0x08, 0x2020200Cu);
    Header h = { 0, 0xE0, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("em/efm.das", 0, 0, &h, 0x82350000u);
    CHECK(re4_formats_entry_status(eff) == 1);
    CHECK(get32(eff) == 0xB && get32(efm + 0x0C) == 0x20 && get32(efm + 0x20) == 1 && get32(efm + 0x24) == 0x10);
    CHECK(tex[0] == 0x20 && tex[1] == 0x20 && tex[2] == 0xAF && tex[3] == 0x30);
    report("effect texture motion", ok);
}

// A model (BIN, version 0x20010801) with four vertices and a shape table of
// two shapes that name the same delta list (vertex 2: +0x74, -1, +3).
void testMorph()
{
    bool ok = true;
    unsigned char* arc = (unsigned char*) (unsigned long) 0x82300000u;
    memset(arc, 0, 0x200);
    put32be(arc + 0x00, 1);
    put32be(arc + 0x10, 0x20);
    memcpy(arc + 0x14, "BIN\0", 4);
    unsigned char* bin = arc + 0x20;
    put32be(bin + 0x2C, 0x80);        // shapeOfs
    put32be(bin + 0x30, 0x60);        // vtxOrig
    put16be(bin + 0x38, 4);           // nVtx
    put32be(bin + 0x3C, 0x20010801);  // version
    for (int k = 0; k < 4; k++) {
        put16be(bin + 0x60 + k * 8, (unsigned) (100 * k));
    }
    put32be(bin + 0x80, 2);           // shapes
    put32be(bin + 0x84, 0x10);        // shape 0: list at entries + 0x10
    put32be(bin + 0x88, 1);
    put32be(bin + 0x8C, 0x10);        // shape 1: the same list
    put32be(bin + 0x90, 1);
    unsigned char* list = bin + 0x84 + 0x10;
    put16be(list + 0, 2);
    put16be(list + 2, 0x74);
    put16be(list + 4, 0xFFFF);
    put16be(list + 6, 3);
    Header h = { 0, 0x100, 0, 0, 0, 0, 0, 0 };
    re4_formats_part_loaded("em/shp.das", 0, 0, &h, 0x82300000u);
    CHECK(get32(bin + 0x2C) == 0x80 && get16(bin + 0x38) == 4 && get16(bin + 0x60 + 8) == 100);
    CHECK(get32(bin + 0x80) == 2 && get32(bin + 0x84) == 0x10 && get32(bin + 0x90) == 1);
    CHECK(get16(list) == 2 && get16(list + 2) == 0x74 && get16(list + 4) == 0xFFFF && get16(list + 6) == 3);
    report("morph", ok);
}

struct PadStatus {  // PADStatus
    unsigned short button;
    signed char stickX, stickY, substickX, substickY;
    unsigned char triggerLeft, triggerRight, analogA, analogB;
    signed char err;
};
extern "C" unsigned PADRead(PadStatus* status);

PadStatus gSeen[8];
volatile int gEntryFrames;

int entry()
{
    PadStatus pads[4];
    for (int i = 0; i < 6; i++) {
        PADRead(pads);
        gSeen[i] = pads[0];
        gEntryFrames = i + 1;
        usleep(2000);
    }
    return 7;
}

double now()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void testThread()
{
    bool ok = true;
    CHECK(re4_core_state() == RE4_CORE_IDLE);
    CHECK(re4_core_acquire() == 0);  // nothing published yet
    CHECK(re4_core_reserve("/nonexistent-re4-data", ""));
    Re4CorePad pad = { 0x0100, 50, -20, 0, 0, 0, 200 };
    re4_core_pad(&pad);
    CHECK(re4_core_start_entry(entry));
    double end = now() + 10.0;
    while (re4_core_state() == RE4_CORE_RUNNING && now() < end) {
        usleep(1000);
    }
    CHECK(re4_core_state() == RE4_CORE_ENDED);
    CHECK(strcmp(re4_core_message(), "re4_game_main returned 7") == 0);
    CHECK(gEntryFrames == 6);
    for (int i = 0; i < 6; i++) {
        CHECK(gSeen[i].button == 0x0100 && gSeen[i].stickX == 50 && gSeen[i].stickY == -20 &&
              gSeen[i].triggerRight == 200 && gSeen[i].analogA == 0xFF && gSeen[i].err == 0);
    }
    const Re4CoreSnapshot* s = re4_core_acquire();
    CHECK(s != 0);
    if (s) {
        CHECK(s->apiVersion == RE4_CORE_API_VERSION && s->serial == 6 && s->frame == 5);
        CHECK(re4_core_acquire() == 0);  // one reader at a time
        re4_core_release(s);
        const Re4CoreSnapshot* again = re4_core_acquire();
        CHECK(again == s);
        re4_core_release(again);
    }
    static char tail[4096];
    re4_core_log_tail(tail, sizeof(tail));
    CHECK(strstr(tail, "core: running") != 0 && strstr(tail, "returned 7") != 0);
    // a tap pressed and released between two frames reaches the game once
    Re4CorePad tap = { 0x0300, 0, 0, 0, 0, 0, 0 };
    Re4CorePad none = { 0, 0, 0, 0, 0, 0, 0 };
    re4_core_pad(&none);
    PadStatus pads[4];
    PADRead(pads);
    re4_core_pad(&tap);
    re4_core_pad(&none);
    PADRead(pads);
    CHECK(pads[0].button == 0x0300 && pads[0].analogA == 0xFF);
    PADRead(pads);
    CHECK(pads[0].button == 0);
    report("thread", ok);
}

extern "C" void PADClamp(PadStatus* status);
extern "C" void PADControlMotor(long chan, unsigned cmd);

// The raw values from the app become what the game saw on the GameCube once
// its PadRead calls the SDK's PADClamp (src/lib/Padclamp.c): dead zones, the
// octagon of the main stick (at most 72), the C stick up to 59 (cam_qfps.cpp
// divides by 59), triggers past 30 (PadRead turns any value into L/R).
void testPad()
{
    bool ok = true;
    struct Case {
        signed char x, y, cx, cy;
        unsigned char l, r;
        signed char ex, ey, ecx, ecy;
        unsigned char el, er;
    };
    const Case cases[] = {
        { 14, -15, 15, -10, 30, 0, 0, 0, 0, 0, 0, 0 },          // inside the dead zones
        { 101, 0, 92, 0, 255, 31, 72, 0, 59, 0, 150, 1 },       // pushed fully to the right
        { 0, -101, 0, -92, 180, 181, 0, -72, 0, -59, 150, 150 },
        { 71, 71, 65, -65, 100, 0, 40, 40, 31, -31, 70, 0 },    // diagonals: the octagon
        { 50, 0, -40, 0, 0, 0, 35, 0, -25, 0, 0, 0 },           // past the dead zone: linear
    };
    for (const Case& c : cases) {
        PadStatus pads[4];
        memset(pads, 0, sizeof(pads));
        pads[0].stickX = c.x;
        pads[0].stickY = c.y;
        pads[0].substickX = c.cx;
        pads[0].substickY = c.cy;
        pads[0].triggerLeft = c.l;
        pads[0].triggerRight = c.r;
        for (int i = 1; i < 4; i++) {
            pads[i] = pads[0];
            pads[i].err = -1;  // no controller: left alone
        }
        PADClamp(pads);
        CHECK(pads[0].stickX == c.ex && pads[0].stickY == c.ey && pads[0].substickX == c.ecx &&
              pads[0].substickY == c.ecy && pads[0].triggerLeft == c.el && pads[0].triggerRight == c.er);
        CHECK(pads[3].stickX == c.x && pads[3].triggerLeft == c.l);
    }
    // the rumble motor as the game switches it, frame by frame
    PadStatus pads[4];
    PADRead(pads);
    const unsigned start = re4_core_rumble() >> 8;
    PADControlMotor(0, 1);
    PADRead(pads);
    PADRead(pads);
    CHECK((re4_core_rumble() & 0xF) == 0x3 && (re4_core_rumble() >> 8) == start + 2);
    PADControlMotor(0, 2);  // PAD_MOTOR_STOP_HARD
    PADRead(pads);
    CHECK((re4_core_rumble() & 0xF) == 0x6);
    PADRead(pads);
    PADRead(pads);
    PADRead(pads);
    CHECK((re4_core_rumble() & 0xF) == 0);
    // the live controller (the app): every press reaches the game as a press of its own, also a
    // fast mash where the button is let go and pressed again between two reads (r226, 25.10.2026)
    {
        Re4CorePad live;
        memset(&live, 0, sizeof(live));
        auto read = [&]() {
            PadStatus p[4];
            PADRead(p);
            return p[0].button;
        };
        re4_core_pad(&live);
        read();
        live.buttons = 0x0100;
        re4_core_pad(&live);
        CHECK(read() == 0x0100);  // pressed
        live.buttons = 0;
        re4_core_pad(&live);
        live.buttons = 0x0100;
        re4_core_pad(&live);       // let go and pressed again before the next read
        CHECK(read() == 0);        // a frame off ...
        CHECK(read() == 0x0100);   // ... then the second press
        CHECK(read() == 0x0100);   // held
        live.buttons = 0;
        re4_core_pad(&live);
        live.buttons = 0x0100;
        re4_core_pad(&live);
        live.buttons = 0;
        re4_core_pad(&live);       // a tap shorter than a frame
        CHECK(read() == 0);        // (held at the read before: off first)
        CHECK(read() == 0x0100);   // the tap
        CHECK(read() == 0);
        live.triggerL = 200;
        live.buttons = 0x0040;
        re4_core_pad(&live);
        read();
        live.buttons = 0;
        live.triggerL = 0;
        re4_core_pad(&live);
        live.buttons = 0x0040;
        live.triggerL = 200;
        re4_core_pad(&live);
        PadStatus p[4];
        PADRead(p);
        CHECK(p[0].button == 0 && p[0].triggerLeft == 0);  // L off with its trigger value
        CHECK(read() == 0x0040);
        re4_core_pad(nullptr);
    }
    report("pad", ok);
}

volatile int gPauseFrames;

void* pauseGame(void*)
{
    PadStatus pads[4];
    for (int i = 0; i < 3; i++) {
        PADRead(pads);  // the frame hook: waits while the menu is open
        gPauseFrames++;
    }
    return 0;
}

extern "C" int re4_debug_menu_request(void);
extern "C" void re4_memcard_open(const char* path);

void testPause()
{
    bool ok = true;
    char path[64];
    snprintf(path, sizeof(path), "/tmp/re4-pause-card-%d.bin", (int) getpid());
    unlink(path);
    re4_memcard_open(path);  // a new card: formatted, empty
    Re4CardFile files[4];
    CHECK(re4_core_card_list(files, 4) == RE4_CARD_BUSY);  // the game does not stand
    CHECK(re4_core_pause(1, 0) == 0);
    gPauseFrames = 0;
    pthread_t game;
    CHECK(pthread_create(&game, 0, pauseGame, 0) == 0);
    double end = now() + 5.0;
    while (!re4_core_pause(1, 0) && now() < end) {
        usleep(1000);
    }
    CHECK(re4_core_pause(1, 0) == 1);
    usleep(30000);
    CHECK(gPauseFrames == 0);  // stands in its first PADRead
    CHECK(re4_core_card_list(files, 4) == 0);
    CHECK(re4_core_card_free_blocks() == 251);
    Re4InstantInfo instant = {};
    const int slot = re4_core_instant(RE4_INSTANT_STATUS, &instant);
    CHECK(slot == RE4_INSTANT_OK || slot == RE4_INSTANT_BAD || slot == RE4_INSTANT_IO);
    CHECK(instant.canSave == 0 && instant.canLoad == 0); // synthetic state is not playable
    CHECK(re4_core_instant(RE4_INSTANT_SAVE, &instant) == RE4_INSTANT_BUSY);
    CHECK(gPauseFrames == 0); // commands were serviced without advancing the game
    char note[128];
    unsigned char out[64];
    CHECK(re4_core_card_export(0, out, sizeof(out), note, sizeof(note)) == RE4_CARD_NOFILE);
    re4_core_pause(0, 0);  // the menu closes
    pthread_join(game, 0);
    CHECK(gPauseFrames == 3);
    CHECK(re4_core_pause(0, 0) == 0);
    CHECK(re4_core_card_free_blocks() == RE4_CARD_BUSY);
    CHECK(re4_core_instant(RE4_INSTANT_STATUS, &instant) == RE4_INSTANT_BUSY);
    // the menu's depth of field switch (patch 0065 reads it)
    CHECK(re4_dof_enabled() == 1);
    re4_core_set_dof(0);
    CHECK(re4_dof_enabled() == 0);
    re4_core_set_dof(1);
    CHECK(re4_dof_enabled() == 1);
    // the picture format "4:3 without bars" (patch 0105 reads it)
    CHECK(re4_letterbox_off() == 0);
    re4_core_set_letterbox(1);
    CHECK(re4_letterbox_off() == 1);
    re4_core_set_letterbox(0);
    CHECK(re4_letterbox_off() == 0);
    // SELECT held: the debug menu at the next frame, once
    CHECK(re4_debug_menu_request() == 0);
    re4_core_debug_menu();
    CHECK(re4_debug_menu_request() == 1);
    CHECK(re4_debug_menu_request() == 0);
    re4_memcard_open(0);
    unlink(path);
    report("pause", ok);
}

// GXPeekZ answers from the depth map the renderer hands over (runtime/gx_fifo.cpp): far without a
// map and when the renderer stays behind, else the cell of the pixel (half the EFB's resolution).
extern "C" unsigned int re4_gx_efb_peek(unsigned short x, unsigned short y, int z);

void testPeekDepth()
{
    bool ok = true;
    static unsigned cells[RE4_GX_DEPTH_W * RE4_GX_DEPTH_H];
    unsigned answered0, far0, marked0, answered, far, marked;
    re4_gx_peek_stats(&answered0, &far0, &marked0);
    re4_gx_peek_new_frame();
    CHECK(re4_gx_efb_peek(100, 60, 1) == 0x00FFFFFF);  // no map yet: far, as before
    CHECK(re4_gx_efb_peek(100, 60, 0) == 0);           // colour: black
    re4_gx_peek_stats(&answered, &far, &marked);
    CHECK(marked == marked0 + 1 && far == far0 + 1);   // the picture's first peek marked it, once
    CHECK(re4_gx_efb_peek(101, 61, 1) == 0x00FFFFFF);
    re4_gx_peek_stats(&answered, &far, &marked);
    CHECK(marked == marked0 + 1);
    cells[(60 >> 1) * RE4_GX_DEPTH_W + (100 >> 1)] = 0x123456;
    cells[(RE4_GX_DEPTH_H - 1) * RE4_GX_DEPTH_W + RE4_GX_DEPTH_W - 1] = 0xFF00ABCD;  // only 24 bits count
    re4_core_gx_depth(cells, 1);
    re4_gx_peek_new_frame();
    CHECK(re4_gx_efb_peek(100, 60, 1) == 0x123456);
    CHECK(re4_gx_efb_peek(101, 61, 1) == 0x123456);  // the 2 x 2 pixels of a cell
    CHECK(re4_gx_efb_peek(639, 527, 1) == 0x00ABCD);
    CHECK(re4_gx_efb_peek(2000, 2000, 1) == 0x00ABCD);  // beyond the EFB: the last cell
    CHECK(re4_gx_efb_peek(0, 0, 1) == 0);               // a cell the renderer drew nothing in (0)
    re4_gx_peek_stats(&answered, &far, &marked);
    CHECK(answered == answered0 + 5);
    // the renderer stays behind: the pictures that asked run more than 8 ahead of its maps
    for (int i = 0; i < 9; i++) {
        re4_gx_peek_new_frame();
        re4_gx_efb_peek(100, 60, 1);
    }
    CHECK(re4_gx_efb_peek(100, 60, 1) == 0x00FFFFFF);
    re4_core_gx_depth(cells, 100);  // it caught up
    CHECK(re4_gx_efb_peek(100, 60, 1) == 0x123456);
    report("peek depth", ok);
}

}  // namespace

int re4_core_tests()
{
    gFailures = 0;
    testBridge();
    testCodec();
    testRel();
    testSingleFiles();
    testThread();
    testPad();
    testSound();
    testCamera();
    testEvent();
    testEmptyTables();
    testEffectTextureMotion();
    testMorph();
    {
        int failed = re4probe_ax_test();
        gFailures += failed;
        report("ax", failed == 0);
    }
    {
        int failed = re4probe_axfx_test();
        gFailures += failed;
        report("axfx", failed == 0);
    }
    {
        int failed = re4probe_movie_test();
        if (failed) {
            re4_log("  movie checks failed: 0x%x", failed);
            gFailures++;
        }
        report("movie", failed == 0);
    }
    {
        // the memory card library on the port's card (tests/card_probe.cpp)
        int failed = re4probe_card_test();
        gFailures += failed;
        report("card", failed == 0);
    }
    {
        int failed = re4probe_instant_game_test();
        gFailures += failed;
        report("instant-game", failed == 0);
    }
    testPause();
    testPeekDepth();
    re4_log("core-test: %d failed", gFailures);
    return gFailures;
}
