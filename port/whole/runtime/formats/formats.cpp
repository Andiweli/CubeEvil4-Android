// Formatumwandler: entry points and dispatch (see formats.h).
//
//   re4_formats_part_loaded  the game's DVD queue finished one part of a file
//                            (src/game/dvd.cpp cDvdQueue::readMain, patch 0015)
//   re4_formats_unpacked     yz2Decode_Decode unpacked a stream (runtime/yz2_decode.cpp)
//
// A part is chosen by its DvdHeader type first (sound blocks and samples have
// their own types), then by file name. Unknown files are logged once and left
// as they are.
#include "formats.h"

#include "dvd.h"
#include "re4_core.h"
#include "re4_runtime.h"

#include <ctype.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

namespace re4fmt {

Blob blob(void* data, u32 size, const char* name)
{
    Blob b;
    b.data = (u8*) data;
    b.size = size;
    b.name = name;
    b.ok = true;
    return b;
}

bool fits(Blob& b, u32 off, u32 len, const char* what)
{
    if (off <= b.size && len <= b.size - off) {
        return true;
    }
    if (b.ok) {
        re4_log("formats: %s: %s at 0x%x (+0x%x) outside the %u-byte block, conversion stopped", b.name, what,
                (unsigned) off, (unsigned) len, (unsigned) b.size);
    }
    b.ok = false;
    return false;
}

u32 be32(Blob& b, u32 off)
{
    if (!fits(b, off, 4, "32-bit read")) {
        return 0;
    }
    const u8* p = b.data + off;
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3];
}

u16 be16(Blob& b, u32 off)
{
    if (!fits(b, off, 2, "16-bit read")) {
        return 0;
    }
    const u8* p = b.data + off;
    return (u16) ((p[0] << 8) | p[1]);
}

u32 rd32(Blob& b, u32 off)
{
    u32 v = 0;
    if (fits(b, off, 4, "32-bit read")) {
        memcpy(&v, b.data + off, 4);
    }
    return v;
}

u16 rd16(Blob& b, u32 off)
{
    u16 v = 0;
    if (fits(b, off, 2, "16-bit read")) {
        memcpy(&v, b.data + off, 2);
    }
    return v;
}

void swap16(Blob& b, u32 off, u32 count)
{
    u32 len = count > b.size / 2 ? 0xFFFFFFFFu : count * 2;
    if (count != 0 && fits(b, off, len, "16-bit field")) {
        re4_swap16_words(b.data + off, len);
    }
}

void swap32(Blob& b, u32 off, u32 count)
{
    u32 len = count > b.size / 4 ? 0xFFFFFFFFu : count * 4;
    if (count != 0 && fits(b, off, len, "32-bit field")) {
        re4_swap32_words(b.data + off, len);
    }
}

void swapField(Blob& b, u32 base, u32 off, u32 size, u32 elem)
{
    if (elem == 2) {
        swap16(b, base + off, size / 2);
    } else if (elem == 4) {
        swap32(b, base + off, size / 4);
    }
}

bool Seen::first(u32 o)
{
    for (u32 i = 0; i < n; i++) {
        if (off[i] == o) {
            return false;
        }
    }
    if (n < (u32) kMax) {
        off[n++] = o;
    } else if (!full) {
        full = true;
        re4_log("formats: more than %d shared structures in one block", (int) kMax);
    }
    return true;
}

void convertWords(Blob& b, const Part&)
{
    swap32(b, 0, b.size / 4);
}

// etc/sizetbl.dat (cDvd::SizeTableRead; the game keeps the pointer but never
// reads it): u32 count, then per file char name[8], u32 size, u32 pad.
void convertSizeTbl(Blob& b, const Part&)
{
    u32 count = be32(b, 0);
    if (!fits(b, 4, count * 16, "size table")) {
        return;
    }
    swap32(b, 0);
    for (u32 i = 0; i < count; i++) {
        swap32(b, 4 + i * 16 + 8, 2);
    }
}

// A file inside a container (etc archive, event assets) by its extension.
void convertNamedFile(Blob& b, const Part& part, const char* name)
{
    struct Ext {
        const char* ext;
        Converter convert;
    };
    static const Ext kExt[] = {
        { ".bin", convertBin }, { ".tpl", convertTpl }, { ".fcv", convertMotion }, { ".seq", convertSeq },
        { ".eff", convertEffect }, { ".lit", convertLit }, { ".mdt", convertMdt }, { ".sat", convertSat },
        { ".eat", convertSat }, { ".esl", convertEmList }, { ".evd", convertEvd }, { ".cam", convertCamera },
    };
    const char* dot = strrchr(name, '.');
    for (const Ext& e : kExt) {
        if (dot && strcasecmp(dot, e.ext) == 0) {
            e.convert(b, part);
            return;
        }
    }
    static char seen[64][8];
    static u32 n;
    char ext[8] = "";
    snprintf(ext, sizeof(ext), "%s", dot ? dot : "(none)");
    for (u32 i = 0; i < n; i++) {
        if (strcmp(seen[i], ext) == 0) {
            return;
        }
    }
    if (n < 64) {
        memcpy(seen[n++], ext, sizeof(ext));
    }
    re4_log("formats: %s: no converter for \"%s\" files, left big-endian", b.name, ext);
}

// ---- coverage: what every archive entry became ----------------------------------

namespace {

struct EntryNote {
    const void* addr;
    char tag[4];
    int status;
    const void* archive;  // noteArchive address, 0 = not in an archive
    int entry;
    int archiveSlot;      // index in g_archives, -1 none
    u32 archiveGen;       // the archive's generation at the time (a reload makes the note stale)
};

struct ArchiveNote {
    const void* addr;
    u32 size;
    u32 gen;          // 0 = free
    char name[80];
    char file[52];    // path below files/
    u32 fileOfs;
    u32* offs;        // entry start offsets (by entry number), malloc'd
    u32 nOffs;
    u32 sig;          // hash of the entry count and tags: memory reused without a new note?
    u32 holeLo[4], holeHi[4];  // parts later loads overwrote (offsets in the archive)
    u32 nHoles;
    bool plain;       // a file without entry table (an EVD): data found by offset (entry -2)
    u32 seq;          // when it was noted (g_seq), against copies
    bool suspended;   // signature differs right now (memory swapped out, sub screen)
};

// r100 alone loads well over 64 archives (enemies, player, weapons, room):
// the bridge to the app looks every drawn model up here (re4_formats_locate)
const u32 kNotes = 65536;
const u32 kArchives = 1024;
EntryNote g_notes[kNotes];
ArchiveNote g_archives[kArchives];
u32 g_nextArchive;
u32 g_lastArchive;  // the archive noted last (its entries follow it)
u32 g_gen;          // +1 per noted archive or copy: cached lookups are redone
u32 g_seq;          // order of notes and copies

// ARAM -> MEM1 copies: data the converter noted in ARAM now lies in MEM1 too
struct CopyNote {
    const u8* dst;
    const u8* src;
    u32 len;
    u32 seq;
};
const u32 kCopies = 512;
CopyNote g_copies[kCopies];
u32 g_nextCopy;

int archiveSlot(const void* archive)
{
    if (!archive) {
        return -1;
    }
    if (g_archives[g_lastArchive].addr == archive) {
        return (int) g_lastArchive;
    }
    for (u32 i = 0; i < kArchives; i++) {
        if (g_archives[i].addr == archive) {
            return (int) i;
        }
    }
    return -1;
}

void forget(ArchiveNote& a)
{
    free(a.offs);
    memset(&a, 0, sizeof(a));
}

// The game never rewrites an archive's entry count and tags (an EVD's tag
// and names); other data over the archive (without a note of its own)
// changes them.
u32 signature(const ArchiveNote& a)
{
    const u8* p = (const u8*) a.addr;
    u32 h = 2166136261u;
    if (a.plain) {
        for (u32 i = 0; i < 0x34 && i < a.size; i++) {
            h = (h ^ p[i]) * 16777619u;
        }
        return h;
    }
    u32 n = a.nOffs;
    u32 len = 0x10 + n * 8;
    if (len > a.size) {
        len = a.size;
    }
    for (u32 i = 0; i < 4 && i < len; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    for (u32 i = 0x10 + n * 4; i < len; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

// The data is there right now. A sub screen swaps parts of MEM1 out to ARAM
// and back (SubScreenAramRead): the note stays, lookups wait until the
// archive is back.
bool intact(ArchiveNote& a)
{
    if (!a.gen) {
        return false;
    }
    bool same = signature(a) == a.sig;
    if (same == a.suspended) {
        static u32 reported;
        if (reported < 40) {
            reported++;
            re4_log("formats: archive %s (0x%08lx..0x%08lx): %s", a.name, (unsigned long) a.addr,
                    (unsigned long) a.addr + a.size,
                    same ? "back again, notes valid" : "other data there now (swapped out?), notes suspended");
        }
        a.suspended = !same;
    }
    return same;
}

bool inHole(const ArchiveNote& a, u32 rel)
{
    for (u32 i = 0; i < a.nHoles; i++) {
        if (rel >= a.holeLo[i] && rel < a.holeHi[i]) {
            return true;
        }
    }
    return false;
}

bool current(const EntryNote& e)
{
    if (e.archiveSlot < 0) {
        return false;
    }
    const ArchiveNote& a = g_archives[e.archiveSlot];
    if (a.gen == 0 || a.addr != e.archive || a.gen != e.archiveGen) {
        return false;
    }
    u32 rel = (u32) ((const char*) e.addr - (const char*) a.addr);
    return rel < a.size && !inHole(a, rel);
}

u32 slotOf(const void* addr)
{
    u32 h = (u32) (unsigned long) addr;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    return (h ^ (h >> 15)) % kNotes;
}

}  // namespace

void noteEntry(const void* addr, const char* tag, int status, const void* archive, int entry)
{
    u32 s = slotOf(addr);
    for (u32 i = 0; i < kNotes; i++) {
        EntryNote& e = g_notes[(s + i) % kNotes];
        if (e.addr == 0 || e.addr == addr) {
            e.addr = addr;
            memcpy(e.tag, tag, 4);
            e.status = status;
            e.archive = archive;
            e.entry = entry;
            e.archiveSlot = archiveSlot(archive);
            e.archiveGen = e.archiveSlot >= 0 ? g_archives[e.archiveSlot].gen : 0;
            return;
        }
    }
}

int entryStatus(const void* addr, char* tag)
{
    u32 s = slotOf(addr);
    for (u32 i = 0; i < kNotes; i++) {
        const EntryNote& e = g_notes[(s + i) % kNotes];
        if (e.addr == 0) {
            break;
        }
        if (e.addr == addr) {
            if (tag) {
                memcpy(tag, e.tag, 4);
            }
            return e.status;
        }
    }
    return 0;
}

void noteArchive(const void* addr, unsigned size, const char* name, const Part& part, const u32* offs, u32 count,
                 bool plain)
{
    // memory reused: the part of an older archive this one covers is stale
    // now. The same range again (a reload) or its header covered: the whole
    // note goes. An archive that encloses this one (a container around an
    // inner archive) stays as it is.
    static u32 reported;
    const char* lo = (const char*) addr;
    const char* hi = lo + size;
    for (u32 i = 0; i < kArchives; i++) {
        ArchiveNote& o = g_archives[i];
        const char* b = (const char*) o.addr;
        const char* e = b + o.size;
        if (!o.gen || !(b < hi && lo < e)) {
            continue;
        }
        if (b <= lo && hi <= e && (b != lo || e != hi)) {
            continue;  // encloses the new one
        }
        const char* from = lo > b ? lo : b;
        const char* to = hi < e ? hi : e;
        u32 header = 0x10 + o.nOffs * 8;
        bool whole = (b == lo && e == hi) || from < b + header || o.nHoles == 4;
        if (reported < 40) {
            reported++;
            re4_log("formats: archive %s (0x%08lx..0x%08lx) overlaps %s (0x%08lx..0x%08lx): %s", name,
                    (unsigned long) lo, (unsigned long) hi, o.name, (unsigned long) b, (unsigned long) e,
                    whole ? "its notes are dropped" : "that part is marked stale");
        }
        if (whole) {
            forget(o);
        } else {
            o.holeLo[o.nHoles] = (u32) (from - b);
            o.holeHi[o.nHoles] = (u32) (to - b);
            o.nHoles++;
        }
    }
    ArchiveNote* a = &g_archives[g_nextArchive++ % kArchives];
    forget(*a);
    g_lastArchive = (u32) (a - g_archives);
    a->addr = addr;
    a->size = size;
    a->gen = ++g_gen;
    a->seq = ++g_seq;
    snprintf(a->name, sizeof(a->name), "%s", name);
    snprintf(a->file, sizeof(a->file), "%s", part.name ? part.name : "");
    a->fileOfs = part.fileOfs;
    a->plain = plain;
    if (offs && count) {
        a->offs = (u32*) malloc(count * sizeof(u32));
        if (a->offs) {
            memcpy(a->offs, offs, count * sizeof(u32));
            a->nOffs = count;
        }
    }
    a->sig = signature(*a);
}

// The archive entry that contains `addr` (nested data: a BIN inside an SMD,
// a model inside a room block file): the entry with the highest start at or
// below it; of entries sharing a start, the last one owns the data
// (convertArchive). -1 = not inside a noted archive.
int containingEntry(const void* addr, int* slot, u32* inner)
{
    const char* p = (const char*) addr;
    int found = -1;  // the innermost archive around it
    for (u32 k = 0; k < kArchives; k++) {
        ArchiveNote& a = g_archives[k];
        const char* base = (const char*) a.addr;
        if (a.gen && (a.offs || a.plain) && p >= base && p < base + a.size && !inHole(a, (u32) (p - base)) &&
            (found < 0 || a.size < g_archives[found].size) && intact(a)) {
            found = (int) k;
        }
    }
    if (found < 0) {
        return -1;
    }
    const ArchiveNote& a = g_archives[found];
    u32 rel = (u32) (p - (const char*) a.addr);
    if (a.plain) {
        *slot = found;
        *inner = rel;
        return -2;  // data of the file itself
    }
    int best = -1;
    for (u32 i = 0; i < a.nOffs; i++) {
        if (a.offs[i] <= rel && (best < 0 || a.offs[i] >= a.offs[best])) {
            best = (int) i;
        }
    }
    if (best >= 0) {
        *slot = found;
        *inner = rel - a.offs[best];
    }
    return best;
}

}  // namespace re4fmt

using namespace re4fmt;

// GetDataExt (read.cpp, patch 0025) reports every lookup: the tag, the entry
// it found and whether the converter turned that entry into host order. Each
// entry is reported once per load; RE4_COVERAGE_LOG=FILE also appends a
// tab-separated line (archive, tag, number, address, status).
// What the converter did with an archive entry (EntryStatus; 0 unknown): GetDataExt's coverage
// line below, and the core tests.
extern "C" int re4_formats_entry_status(const void* entry)
{
    u32 s = slotOf(entry);
    for (u32 i = 0; i < kNotes; i++) {
        const EntryNote& e = g_notes[(s + i) % kNotes];
        if (e.addr == 0) {
            break;
        }
        if (e.addr == entry) {
            return e.status;
        }
    }
    return 0;
}

extern "C" void re4_formats_get_data(const void* arc, const char* tag, int no, const void* entry)
{
    if (entry == 0) {
        return;  // not in this archive: nothing to convert
    }
    static const void* reported[4096];
    static u32 nReported;
    for (u32 i = 0; i < nReported; i++) {
        if (reported[i] == entry) {
            return;
        }
    }
    if (nReported < 4096) {
        reported[nReported++] = entry;
    }
    int e = re4_formats_entry_status(entry);
    const char* status = e == kEntryConverted    ? "ja"
                         : e == kEntryIncomplete ? "nein (unvollständig)"
                         : e == kEntryNoConverter ? "nein (kein Umwandler)"
                                                  : "nein (unbekannter Eintrag)";
    const char* name = "?";
    int slot = archiveSlot(arc);
    if (slot >= 0) {
        name = g_archives[slot].name;
    }
    re4_log("coverage: GetDataExt %.3s #%d in %s -> %p: umgewandelt %s", tag, no, name, entry, status);
    if (const char* path = getenv("RE4_COVERAGE_LOG")) {
        if (FILE* f = fopen(path, "a")) {
            fprintf(f, "%s\t%.3s\t%d\t%p\t%s\n", name, tag, no, entry, status);
            fclose(f);
        }
    }
}

namespace {

// Plain text: nothing to convert.
void text(Blob&, const Part&)
{
}

// Yz2-packed: converted after unpacking (re4_formats_unpacked).
void packed(Blob&, const Part&)
{
}

// A REL module read as a file of its own (rel/st2_1.rel ...): its header to
// host order at once, as convertArchive does for the RELs inside archives.
// The game reads the header before OSLink: cRoomData::linkRelData and
// sscrn.cpp allocate bssSize bytes first (still big-endian, a huge size:
// OSAllocFromHeap crashed when r206 loaded st2_1.rel). OSLink finds the
// header converted (re4_rel_header_to_host leaves a host-order one alone).
void convertRel(Blob& b, const Part& part)
{
    if (part.fileOfs != 0 || !fits(b, 0, 0x40, "REL header")) {
        return;
    }
    if (re4_rel_header_to_host(b.data) < 0) {
        re4_log("formats: %s: no REL module header (version 0x%08x)", b.name, (unsigned) be32(b, 0x1C));
    }
}

struct Rule {
    const char* pattern;  // fnmatch pattern on the lower-case path
    Converter convert;
    const char* what;
};

const Rule kRules[] = {
    { "bgm/bio4str.hed", convertStrHed, "stream block headers" },
    { "bgm/bio4midi.hed", convertWords, "BGM file offsets" },
    { "bgm/doorse.hed", convertDoorseHed, "door SE file table" },
    { "bgm/bgmtbl.dat", convertBgmTbl, "room BGM table" },
    { "etc/sizetbl.dat", convertSizeTbl, "file size table" },
    { "font/*.fnt", convertFont, "message font" },
    { "*.tpl", convertTpl, "texture palette" },
    // files the game reads one by one, outside an archive: the item models of
    // the pickup screen (sce_at.cpp, ss/cmn/itmXX.bin) and the inventory's
    // examine view (ss_main.cpp, ss/item/idmXXX.bin, capXX.bin), the costume
    // models of r300 (etc/pl0*.bin), the chapter title over the bell event of
    // r101 (etc/<lang>/id101.eff: id texture data, the EFF layout;
    // event001.uwf: its screen layout)
    { "ss/cmn/itm*.bin", convertBin, "item model" },
    { "ss/item/*.bin", convertBin, "item model" },
    { "etc/*.bin", convertBin, "model" },
    { "etc/*.eff", convertEffect, "id texture data" },
    { "etc/*.uwf", convertIdLayout, "screen layout (id system)" },
    { "st*/r*.das", packed, "room archive (Yz2)" },
    { "st*/r*_*.dat", convertArchive, "room block archive" },
    { "etc/*.esl", convertEmList, "enemy list" },
    { "*.evd", convertEvdFile, "event" },
    { "op/op*.das", convertCodecArchive, "codec messages (archive at +0x400)" },
    { "*.das", convertArchive, "archive" },
    { "*.drs", convertArchive, "archive" },
    { "ss/*.dat", convertArchive, "archive (screens)" },
    { "debug/*.txt", text, "text" },
    { "rel/*.rel", convertRel, "REL module (header to host order)" },
    { "debug/roominfo.dat", convertRoomInfo, "room jump table" },
};

// Logs a file once (hash table of names already reported).
bool firstReport(const char* name)
{
    static u32 seen[1024];
    u32 h = 2166136261u;
    for (const char* p = name; *p; p++) {
        h = (h ^ (u8) *p) * 16777619u;
    }
    h |= 1;
    for (u32 i = 0; i < 1024; i++) {
        u32 slot = (h + i) & 1023;
        if (seen[slot] == h) {
            return false;
        }
        if (seen[slot] == 0) {
            seen[slot] = h;
            return true;
        }
    }
    return false;
}

void normalise(const char* in, char* out, size_t size)
{
    size_t n = 0;
    for (const char* p = in; *p && n + 1 < size; p++) {
        char c = *p == '\\' ? '/' : (char) tolower((unsigned char) *p);
        out[n++] = c;
    }
    out[n] = 0;
    // SN host reads prefix the PC folder (cDvdQueue::Initialize).
    const char* host = "d:/bio4/data/";
    size_t hl = strlen(host);
    if (strncmp(out, host, hl) == 0) {
        memmove(out, out + hl, n - hl + 1);
    }
    while (out[0] == '/') {
        memmove(out, out + 1, strlen(out));
    }
}

char g_packedName[64];

// Archives the European release orders differently (port/docs/retail-eu-plan.md, R2): the same
// entries, a few at other places. The game's code (the debug build's) takes entries by index
// (PL_ARC, SS_ARC_PTR); the table of such an archive is put back into the debug build's order
// before anything reads it. Found by comparing the European code with the debug build's:
//  - em/pl0a.drs (Krauser): the texture the debug build has at entry 7 is the European entry 12,
//    the five models behind it moved up by one (cPlKlauser::setModel / setHead, the sub screen's
//    klauserModelInit / wep36Init read 0xA/0x10, 0xD, 0xE/0xF, 0xB there);
//  - ss/cmn/ss_wep40.dat (Ada's semi-auto rifle): the European file has a second rifle model at
//    entry 2 (wep40Init picks entry 1 or 2), hand and motion follow at 3 and 4; the second rifle
//    goes to the end, where the debug build's wep40Init does not look.
// Each rule checks the European order first (count and the tags of the moved entries), so a
// debug disc, or a table already put back, is left alone.
struct RetailOrder {
    const char* name;
    u32 count;
    u32 first;        // the moved range: entries first .. first + n - 1
    u32 n;
    const char* euTags;  // their tags in the European order, 4 bytes each
    u8 from[8];       // debug entry first + i is the European entry first + from[i]
};

const RetailOrder kRetailOrder[] = {
    { "em/pl0a.drs", 146, 6, 8, "BIN\0BIN\0BIN\0BIN\0BIN\0TPL\0TPL\0TPL\0", { 0, 6, 1, 2, 3, 4, 5, 7 } },
    { "ss/cmn/ss_wep40.dat", 5, 1, 4, "BIN\0BIN\0BIN\0FCV\0", { 0, 2, 3, 1 } },
};

bool retailOrder(Blob& b, const char* name)
{
    for (const RetailOrder& r : kRetailOrder) {
        if (strcmp(name, r.name) != 0) {
            continue;
        }
        if (b.size < 0x10 + r.count * 8 || be32(b, 0) != r.count) {
            return false;
        }
        u8* offs = b.data + 0x10 + r.first * 4;
        u8* tags = b.data + 0x10 + r.count * 4 + r.first * 4;
        if (memcmp(tags, r.euTags, r.n * 4) != 0) {
            return false;  // not the European order: the debug build's, or already put back
        }
        u8 o[32], t[32];
        memcpy(o, offs, r.n * 4);
        memcpy(t, tags, r.n * 4);
        for (u32 i = 0; i < r.n; i++) {
            memcpy(offs + i * 4, o + r.from[i] * 4, 4);
            memcpy(tags + i * 4, t + r.from[i] * 4, 4);
        }
        char key[96];
        snprintf(key, sizeof(key), "retail order %s", name);
        if (firstReport(key)) {
            re4_log("formats: %s: entries %u..%u put into the debug build's order (retail release)", name,
                    (unsigned) r.first, (unsigned) (r.first + r.n - 1));
        }
        return true;
    }
    return false;
}

}  // namespace

// The step above alone, on a big-endian archive body (the runtime tests): 1 when reordered.
extern "C" int re4_formats_retail_order(void* data, unsigned size, const char* name)
{
    Blob b = blob(data, size, name);
    return retailOrder(b, name) ? 1 : 0;
}

extern "C" void re4_formats_part_loaded(const char* rawName, int nest, int index, const void* header, unsigned addr)
{
    const DvdHeader* h = (const DvdHeader*) header;
    char name[96];
    normalise(rawName, name, sizeof(name));
    Part part = { name, nest, index, h->type, h->sndType, h->ofs };
    if (h->size == 0) {
        return;
    }
    bool aram = h->type == TRANS_SND_PCM || h->type == TRANS_ARAM;
    u8* data = aram ? re4_aram() + addr : (u8*) addr;
    char label[128];
    snprintf(label, sizeof(label), "%s part %d.%d", name, nest, index);
    Blob b = blob(data, h->size, label);

    if (h->type == TRANS_SND_PCM) {
        return;  // DSP-ADPCM samples: read by the audio layer (M8)
    }
    if (h->type == TRANS_SND_BLK) {
        convertSoundBlock(b, part);
        return;
    }
    if (re4_disc_retail()) {  // the European, US and Japanese releases order them alike
        retailOrder(b, name);
    }
    for (const Rule& r : kRules) {
        if (fnmatch(r.pattern, name, 0) == 0) {
            if (r.convert == packed) {
                snprintf(g_packedName, sizeof(g_packedName), "%s", name);
            }
            timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            r.convert(b, part);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            // a slow conversion stalls the game's thread (the user reports of a stutter in r101 with
            // evd/r101s21.evd, 30.10.2026): the slow ones into the log, at most 50 lines
            const long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
            static int slowLines;
            if (ms >= 50 && slowLines < 50) {
                slowLines++;
                re4_log("formats: %s (%u bytes) took %ld ms to convert", label, (unsigned) h->size, ms);
            }
            if (firstReport(name)) {
                re4_log("formats: %s: %s%s", label, r.what, b.ok ? "" : " (incomplete)");
            }
            return;
        }
    }
    if (firstReport(name)) {
        re4_log("formats: %s (type %u, %u bytes at %s 0x%08x): no converter, left big-endian", label,
                (unsigned) h->type, (unsigned) h->size, aram ? "ARAM" : "MRAM", (unsigned) addr);
    }
}

// Room archives are the only Yz2 user (read.cpp decodeData); they unpack to
// an archive with a table of contents.
extern "C" void re4_formats_unpacked(void* dst, unsigned size)
{
    static char label[96];
    snprintf(label, sizeof(label), "%s (unpacked)", g_packedName[0] ? g_packedName : "yz2 stream");
    Blob b = blob(dst, size, label);
    Part part = { g_packedName[0] ? g_packedName : label, 0, 0, TRANS_MRAM, 0, 0xFFFFFFFFu };
    convertArchive(b, part);
    re4_log("formats: %s: archive, %u bytes%s", label, size, b.ok ? "" : " (incomplete)");
}

// Core bridge (runtime/re4_core.h): the disc source of the archive entry at
// `addr`, from the notes of the converter. The notes are written while the
// game loads (its own thread); the bridge reads them on the same thread.
namespace {

// The notes alone: an archive entry starting at addr, or the entry (or plain
// file) around it. Returns the entry (-1 none), *slot the archive.
int lookupNotes(const void* addr, int* slot, u32* inner)
{
    *slot = -1;
    *inner = 0;
    u32 s = slotOf(addr);
    for (u32 i = 0; i < kNotes; i++) {
        const EntryNote& e = g_notes[(s + i) % kNotes];
        if (e.addr == 0) {
            break;
        }
        if (e.addr == addr) {
            if (current(e) && intact(g_archives[e.archiveSlot])) {
                *slot = e.archiveSlot;
                return e.entry;
            }
            break;
        }
    }
    // nested inside an entry (a BIN in an SMD, a model in a block file) or
    // inside a file without entries (an EVD: entry -2)
    return containingEntry(addr, slot, inner);
}

// The newest ARAM copy that put data at addr, 0 if none.
const CopyNote* copyAt(const void* addr)
{
    const u8* p = (const u8*) addr;
    for (u32 k = 0; k < kCopies && k < g_nextCopy; k++) {
        const CopyNote& c = g_copies[(g_nextCopy - 1 - k) % kCopies];
        if (p >= c.dst && p < c.dst + c.len) {
            return &c;
        }
    }
    return 0;
}

}  // namespace

// MemorySwap (game/dvd.cpp, patch 0028) exchanged `size` bytes between MRAM
// and ARAM: events borrow an enemy's archive memory, sub screens the heap.
// The notes of what lay on either side move to the other; an archive cut by
// an edge of the swapped range is dropped.
extern "C" void re4_formats_memory_swapped(void* mram, unsigned aram, unsigned size)
{
    if (!size || aram >= 0x1000000u || size > 0x1000000u - aram) {
        return;
    }
    const u8* a = (const u8*) mram;
    const u8* b = re4_aram() + aram;
    auto moved = [&](const void* p) -> const void* {
        const u8* q = (const u8*) p;
        if (q >= a && q < a + size) {
            return b + (q - a);
        }
        if (q >= b && q < b + size) {
            return a + (q - b);
        }
        return p;
    };
    for (u32 i = 0; i < kArchives; i++) {
        ArchiveNote& o = g_archives[i];
        if (!o.gen) {
            continue;
        }
        const u8* lo = (const u8*) o.addr;
        const u8* hi = lo + o.size;
        bool inA = lo >= a && hi <= a + size;
        bool inB = lo >= b && hi <= b + size;
        bool cut = (lo < a + size && a < hi) || (lo < b + size && b < hi);
        if (inA || inB) {
            o.addr = moved(o.addr);
        } else if (cut) {
            forget(o);
        }
    }
    // entry notes are hashed by address: insert them anew
    static EntryNote old[kNotes];
    memcpy(old, g_notes, sizeof(g_notes));
    memset(g_notes, 0, sizeof(g_notes));
    for (u32 i = 0; i < kNotes; i++) {
        EntryNote e = old[i];
        if (!e.addr) {
            continue;
        }
        e.addr = moved(e.addr);
        e.archive = moved(e.archive);
        u32 s = slotOf(e.addr);
        for (u32 k = 0; k < kNotes; k++) {
            EntryNote& t = g_notes[(s + k) % kNotes];
            if (t.addr == 0) {
                t = e;
                break;
            }
        }
    }
    // copies into either side are history now
    for (u32 k = 0; k < kCopies; k++) {
        CopyNote& c = g_copies[k];
        if (c.len && ((c.dst < a + size && a < c.dst + c.len) || (c.dst < b + size && b < c.dst + c.len))) {
            c.len = 0;
        }
    }
    ++g_seq;
    ++g_gen;
}

extern "C" void re4_formats_aram_copied(void* dst, const void* src, unsigned length)
{
    if (!length) {
        return;
    }
    CopyNote& c = g_copies[g_nextCopy++ % kCopies];
    c.dst = (const u8*) dst;
    c.src = (const u8*) src;
    c.len = length;
    c.seq = ++g_seq;
    ++g_gen;
}

extern "C" int re4_formats_locate(const void* addr, Re4CoreSource* out)
{
    memset(out, 0, sizeof(*out));
    out->entry = -1;
    if (!addr) {
        return 0;
    }
    // remembered per address until the next archive or copy is noted: the
    // bridge asks for the same models every frame
    struct Cached {
        const void* addr;
        u32 gen;
        int slot;
        int entry;
        u32 inner;
    };
    static Cached cache[4096];
    Cached& c = cache[slotOf(addr) % 4096];
    if (c.addr != addr || c.gen != g_gen) {
        c.addr = addr;
        c.gen = g_gen;
        int slot;
        u32 inner;
        int entry = lookupNotes(addr, &slot, &inner);
        // data copied from ARAM after the archive here was noted: the source
        // is what the converter saw in ARAM
        const CopyNote* copy = copyAt(addr);
        if (copy && (slot < 0 || copy->seq > g_archives[slot].seq)) {
            int cslot;
            u32 cinner;
            int centry = lookupNotes(copy->src + ((const u8*) addr - copy->dst), &cslot, &cinner);
            if (cslot >= 0 && (centry >= 0 || centry == -2)) {
                slot = cslot;
                inner = cinner;
                entry = centry;
            }
        }
        c.slot = slot;
        c.entry = entry;
        c.inner = inner;
    }
    if ((c.entry < 0 && c.entry != -2) || c.slot < 0 || !g_archives[c.slot].gen) {
        return 0;
    }
    const ArchiveNote& a = g_archives[c.slot];
    snprintf(out->file, sizeof(out->file), "%s", a.file);
    out->fileOffset = a.fileOfs;
    out->archiveSize = a.size;
    out->entry = c.entry;
    out->innerOffset = c.inner;
    return out->file[0] != 0;
}

// For the reports: where an address lies that re4_formats_locate does not
// know -- inside a noted archive (no entry starts there) or nowhere.
extern "C" int re4_formats_describe(const void* addr, char* out, int size)
{
    if (!out || size <= 0) {
        return 0;
    }
    out[0] = 0;
    if (!addr) {
        return snprintf(out, size, "null");
    }
    u32 s = slotOf(addr);
    for (u32 i = 0; i < kNotes; i++) {
        const EntryNote& e = g_notes[(s + i) % kNotes];
        if (e.addr == 0) {
            break;
        }
        if (e.addr == addr) {
            return snprintf(out, size, "%p entry #%d %.4s of %s", addr, e.entry, e.tag,
                            current(e) ? g_archives[e.archiveSlot].name : "a replaced archive");
        }
    }
    const char* a = (const char*) addr;
    for (u32 k = 0; k < kArchives; k++) {
        const ArchiveNote& n = g_archives[k];
        const char* base = (const char*) n.addr;
        if (n.gen && a >= base && a < base + n.size) {
            return snprintf(out, size, "%p inside %s at +0x%x (%s)", addr, n.name, (unsigned) (a - base),
                            inHole(n, (u32) (a - base)) ? "overwritten by a later load" : "no entry starts there");
        }
    }
    return snprintf(out, size, "%p not in a noted archive", addr);
}
