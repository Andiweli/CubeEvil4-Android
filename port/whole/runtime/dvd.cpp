// Disc access (Dolphin DVD API) over a folder of extracted disc files.
//
// Layout of the data folder (what Dolphin's "Extract Entire Disc" writes, or
// port/tools/whole_disc_folder.py), one folder per disc:
//
//   <data>/disc0/sys/boot.bin   disc header (DVDDiskID in its first 32 bytes)
//   <data>/disc0/sys/fst.bin    file system table, big-endian as on the disc
//   <data>/disc0/files/...      the files
//   <data>/disc1/...            second disc
//
// A folder with sys/ and files/ directly inside is accepted as disc 0 alone.
// The import folder of the Android app counts from 1 (<data>/disc1 is the
// first disc, <data>/disc2 the second); without a disc0 folder that layout is
// used.
//
// As on the GameCube, the FST of the inserted disc is copied to the top of MEM1
// (DVDGetFSTLocation) and the disc id to 0x80000000. Entry numbers are FST
// indices. Reads complete immediately: the callback runs before
// DVDReadAsyncPrio returns, which the game's queue (dvd.cpp) handles like a
// very fast drive. A read started inside an interrupt handler (the sound
// driver's stream refill in the audio frame) completes when the handler has
// returned (os_thread.cpp re4_os_defer): the stream code marks the read busy
// only after starting it. At the start the queue does not wait a frame
// between its steps (patch 0085, re4_dvd_boot_fast below).
#include "re4_runtime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

namespace {

typedef unsigned u32;
typedef int s32;

struct DVDDiskID {
    char gameName[4];
    char company[2];
    unsigned char diskNumber;
    unsigned char gameVersion;
    unsigned char streaming;
    unsigned char streamingBufSize;
    unsigned char padding[22];
};

struct DVDCommandBlock;
typedef void (*DVDCBCallback)(s32 result, DVDCommandBlock* block);
struct DVDCommandBlock {
    DVDCommandBlock* next;
    DVDCommandBlock* prev;
    u32 command;
    s32 state;
    u32 offset;
    u32 length;
    void* addr;
    u32 currTransferSize;
    u32 transferredSize;
    DVDDiskID* id;
    DVDCBCallback callback;
    void* userData;
};

struct DVDFileInfo;
typedef void (*DVDCallback)(s32 result, DVDFileInfo* fileInfo);
struct DVDFileInfo {
    DVDCommandBlock cb;
    u32 startAddr;
    u32 length;
    DVDCallback callback;
};

struct DVDDir {
    u32 entryNum;
    u32 location;
    u32 next;
};

struct DVDDirEntry {
    u32 entryNum;
    int isDir;
    char* name;
};

static_assert(sizeof(DVDDiskID) == 0x20, "DVDDiskID layout");
// the SDK's form (include/dolphin/dvd.h), which the game allocates: the
// GameCube's on ILP32, pointers of 8 bytes on LP64 (milestone 12)
static_assert(sizeof(DVDCommandBlock) == (sizeof(void*) == 4 ? 0x30 : 0x48), "DVDCommandBlock layout");
static_assert(sizeof(DVDFileInfo) == (sizeof(void*) == 4 ? 0x3C : 0x58), "DVDFileInfo layout");

const int kStateEnd = 0;
const int kStateBusy = 1;
const int kStateCanceled = 10;
const int kResultCanceled = -3;
const int kCommandRead = 1;
const int kCommandChangeDisk = 3;

char gDataDir[1024] = "data";

struct Disc {
    bool loaded;
    char root[1100];          // folder holding sys/ and files/
    unsigned char id[0x20];   // first 32 bytes of boot.bin
    int release;              // RE4_DISC_DEBUG ... (releaseOf)
    unsigned char* fst;       // big-endian FST, host copy
    u32 fstSize;
    u32 count;                // FST entries
    u32* parent;              // per entry: parent directory index
};

Disc gDiscs[2];
int gCurrent = -1;

u32 be32(const unsigned char* p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

bool isDir(const Disc& d, u32 i)
{
    return d.fst[i * 12] != 0;
}

const char* entryName(const Disc& d, u32 i)
{
    return (const char*) d.fst + d.count * 12 + (be32(d.fst + i * 12) & 0xFFFFFF);
}

u32 word(const Disc& d, u32 i, int k)
{
    return be32(d.fst + i * 12 + k * 4);
}

unsigned char* readWhole(const char* path, u32* size)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = (unsigned char*) malloc(n > 0 ? n : 1);
    if (n < 0 || fread(buf, 1, n, f) != (size_t) n) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *size = (u32) n;
    return buf;
}

// opendir rather than stat: the i686 test sysroot lacks the kernel headers
// <sys/stat.h> pulls in.
bool isDirectory(const char* path)
{
    DIR* dir = opendir(path);
    if (dir) {
        closedir(dir);
    }
    return dir != 0;
}

// Validates the FST (bounds of names, directory ranges) and records parents.
bool parseFst(Disc& d)
{
    if (d.fstSize < 12 || d.fst[0] != 1) {
        return false;
    }
    d.count = word(d, 0, 2);
    if (d.count == 0 || d.count > d.fstSize / 12) {
        return false;
    }
    u32 namesSize = d.fstSize - d.count * 12;
    d.parent = (u32*) calloc(d.count, sizeof(u32));
    // Directory ranges nest: walk with an explicit stack of open directories.
    u32 stack[64];
    int depth = 0;
    stack[0] = 0;
    for (u32 i = 1; i < d.count; i++) {
        while (depth > 0 && i >= word(d, stack[depth], 2)) {
            depth--;
        }
        if ((be32(d.fst + i * 12) & 0xFFFFFF) >= namesSize) {
            return false;
        }
        d.parent[i] = stack[depth];
        if (isDir(d, i)) {
            u32 end = word(d, i, 2);
            if (end <= i || end > d.count || depth == 63) {
                return false;
            }
            stack[++depth] = i;
        }
    }
    return true;
}

// Which release a disc header belongs to (the rule of port/tools/disc_release.py): G4BE08 with
// the file system table at 0x28FE00 is the debug build of 2004-11-25 the port's code is (its
// main.dol is 2554432 bytes), G4BP08 the European release (port/docs/retail-eu-plan.md), G4BE08
// with the table elsewhere the US release (never tested). A header without the table's place
// (only the port's tests write one) counts as the debug build, as the port assumed until then.
int releaseOf(const unsigned char* boot, u32 size)
{
    if (size >= 6 && memcmp(boot, "G4BP08", 6) == 0) {
        return RE4_DISC_RETAIL_EU;
    }
    if (size < 6 || memcmp(boot, "G4BE08", 6) != 0) {
        return RE4_DISC_UNKNOWN;
    }
    if (size < 0x428) {
        return RE4_DISC_DEBUG;
    }
    return be32(boot + 0x424) == 0x28FE00 ? RE4_DISC_DEBUG : RE4_DISC_RETAIL_US;
}

const char* releaseName(int release)
{
    static const char* const names[] = {"debug build G4BE08", "European release G4BP08",
                                        "US release G4BE08 (untested)", "unknown game"};
    return names[release >= 0 && release <= RE4_DISC_UNKNOWN ? release : RE4_DISC_UNKNOWN];
}

bool loadDisc(int no)
{
    Disc& d = gDiscs[no];
    if (d.loaded) {
        return true;
    }
    char path[1200];
    snprintf(d.root, sizeof(d.root), "%s/disc0", gDataDir);
    int first = isDirectory(d.root) ? 0 : 1;  // the app's folders: disc1, disc2
    snprintf(d.root, sizeof(d.root), "%s/disc%d", gDataDir, no + first);
    if (!isDirectory(d.root)) {
        if (no != 0) {
            return false;
        }
        snprintf(d.root, sizeof(d.root), "%s", gDataDir);
    }
    snprintf(path, sizeof(path), "%s/sys/fst.bin", d.root);
    d.fst = readWhole(path, &d.fstSize);
    if (!d.fst) {
        return false;
    }
    if (!parseFst(d)) {
        re4_log("DVD: %s is not a valid file system table", path);
        free(d.fst);
        free(d.parent);
        d.fst = 0;
        d.parent = 0;
        return false;
    }
    u32 bootSize = 0;
    snprintf(path, sizeof(path), "%s/sys/boot.bin", d.root);
    unsigned char* boot = readWhole(path, &bootSize);
    memset(d.id, 0, sizeof(d.id));
    if (boot && bootSize >= sizeof(d.id)) {
        memcpy(d.id, boot, sizeof(d.id));
        d.release = releaseOf(boot, bootSize);
    } else {
        // No header: assume the debug build's id (G4BE08), disc `no`.
        memcpy(d.id, "G4BE08", 6);
        d.id[6] = (unsigned char) no;
        d.release = RE4_DISC_DEBUG;
    }
    free(boot);
    d.loaded = true;
    re4_log("DVD: disc %d: %s, %u FST entries, %s", no, d.root, d.count, releaseName(d.release));
    const Disc& other = gDiscs[no ^ 1];
    if (other.loaded && other.release != d.release) {
        re4_log("DVD: the discs are of different releases (%s, %s): import both discs of one release",
                releaseName(gDiscs[0].release), releaseName(gDiscs[1].release));
    }
    return true;
}

// Makes disc `no` the inserted one: disc id at 0x80000000, FST at the top of MEM1.
bool insertDisc(int no)
{
    if (!loadDisc(no)) {
        return false;
    }
    Disc& d = gDiscs[no];
    gCurrent = no;
    if (!re4_mem1_init()) {
        return true;
    }
    memcpy((void*) (unsigned long) RE4_MEM1_BASE, d.id, sizeof(d.id));
    if (d.release == RE4_DISC_RETAIL_EU) {
        // The European discs carry G4BP08. The game's code is the debug build's, which writes its
        // saves itself: the memory card ties each file to the game id of the disc (CARD __CARDAccess),
        // so with the European id the saves of the debug discs (and the .gci of the app's menu,
        // runtime/gci.cpp) would be another game's. cDvd::DiscChange asks for "G4BE" as well. The
        // game sees the debug build's id; disc number and version stay the disc's.
        memcpy((void*) (unsigned long) RE4_MEM1_BASE, "G4BE08", 6);
        static bool told;
        if (!told) {
            told = true;
            re4_log("DVD: European discs: the game sees the debug build's id G4BE08 (memory card, disc change)");
        }
    }
    // the top of retail memory (ILP32), on LP64 above the higher heap end
    u32 top = RE4_MEM1_FST_ADDR + RE4_MEM1_FST_MAX;
    u32 fstAddr = (top - d.fstSize) & ~31u;
    if (d.fstSize > RE4_MEM1_FST_MAX) {
        re4_log("DVD: FST of disc %d is %u bytes, overlaps the game heap", no, d.fstSize);
    }
    memcpy((void*) (unsigned long) fstAddr, d.fst, d.fstSize);
    *(volatile u32*) (unsigned long) (RE4_MEM1_BASE + 0x38) = fstAddr;
    *(volatile u32*) (unsigned long) (RE4_MEM1_BASE + 0x3C) = d.fstSize;
    return true;
}

Disc* current()
{
    if (gCurrent < 0) {
        insertDisc(0);
    }
    return gCurrent >= 0 ? &gDiscs[gCurrent] : 0;
}

// Full path of entry i below files/.
void entryPath(const Disc& d, u32 i, char* out, size_t outSize)
{
    u32 chain[64];
    int n = 0;
    for (u32 e = i; e != 0 && n < 64; e = d.parent[e]) {
        chain[n++] = e;
    }
    size_t len = (size_t) snprintf(out, outSize, "%s/files", d.root);
    while (n > 0 && len < outSize) {
        len += (size_t) snprintf(out + len, outSize - len, "/%s", entryName(d, chain[--n]));
    }
}

bool sameName(const char* a, size_t alen, const char* b)
{
    size_t i = 0;
    for (; i < alen && b[i]; i++) {
        if (tolower((unsigned char) a[i]) != tolower((unsigned char) b[i])) {
            return false;
        }
    }
    return i == alen && b[i] == 0;
}

// DVDConvertPathToEntrynum: '/'-separated, case-insensitive, from the root
// (the game never changes the current directory); "." and ".." allowed.
s32 lookup(const Disc& d, const char* path)
{
    u32 dir = 0;
    const char* p = path;
    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char* end = p;
        while (*end && *end != '/') {
            end++;
        }
        size_t len = (size_t) (end - p);
        if (len == 1 && p[0] == '.') {
            p = end;
            continue;
        }
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            dir = d.parent[dir];
            p = end;
            continue;
        }
        u32 found = 0;
        for (u32 i = dir + 1; i < word(d, dir, 2);) {
            if (sameName(p, len, entryName(d, i))) {
                found = i;
                break;
            }
            i = isDir(d, i) ? word(d, i, 2) : i + 1;
        }
        if (!found) {
            return -1;
        }
        if (*end && !isDir(d, found)) {
            return -1;
        }
        dir = found;
        p = end;
    }
    return (s32) dir;
}

s32 entryForStart(const Disc& d, u32 startAddr, u32 length)
{
    for (u32 i = 1; i < d.count; i++) {
        if (!isDir(d, i) && word(d, i, 1) == startAddr && word(d, i, 2) == length) {
            return (s32) i;
        }
    }
    return -1;
}

// Reads `length` bytes at `offset` of the file; beyond its end the drive
// would return the next sectors of the disc, the port returns zeros.
s32 readFile(DVDFileInfo* fi, void* dst, s32 length, s32 offset)
{
    Disc* d = current();
    if (!d || length < 0 || offset < 0) {
        return -1;
    }
    s32 entry = entryForStart(*d, fi->startAddr, fi->length);
    if (entry < 0) {
        static int toldEntry;
        if (toldEntry++ < 8) {
            re4_log("DVD: read of 0x%x bytes at 0x%x refused: no file starts at 0x%x with %u bytes",
                    (unsigned) length, (unsigned) offset, (unsigned) fi->startAddr, (unsigned) fi->length);
        }
        return -1;
    }
    char path[2048];
    entryPath(*d, (u32) entry, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) {
        re4_log("DVD: cannot open %s", path);
        return -1;
    }
    size_t got = 0;
    if (fseek(f, offset, SEEK_SET) == 0) {
        got = fread(dst, 1, (size_t) length, f);
    }
    long fileSize = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        fileSize = ftell(f);
    }
    fclose(f);
    if (length == 0x400) {
        // a part table that came back as zeros (patch 0113 reads it again): say where it came from
        static int toldZero;
        bool zeros = got > 0;
        for (size_t i = 0; i < got && zeros; i++) {
            zeros = ((const unsigned char*) dst)[i] == 0;
        }
        if (zeros && toldZero++ < 8) {
            re4_log("DVD: %s: 0x400 bytes at 0x%x read as zeros (got %u; the file is %ld bytes on the device, "
                    "%u on the disc)", path, (unsigned) offset, (unsigned) got, fileSize, (unsigned) fi->length);
        }
    }
    if (got < (size_t) length) {
        memset((char*) dst + got, 0, (size_t) length - got);
        // inside the file's length on the disc, the file on the device is shorter: an import
        // that did not finish, or a file changed since (Xiaomi report 23.10.2026: r325 read a part
        // table of zeros and the game read parts of size 0 for ever); a few lines at most
        static int told;
        if ((u32) offset + got < fi->length && told < 8) {
            told++;
            re4_log("DVD: %s: only %u of %u bytes at 0x%x on the device (the file has %u on the disc): "
                    "import the disc again",
                    path, (unsigned) got, (unsigned) length, (unsigned) offset, (unsigned) fi->length);
        }
    }
    return length;
}

}  // namespace

extern "C" {

// A part table (0x400 bytes, host byte order, 32 entries of 8 words) must end in TRANS_EOF and may
// only hold known part types; entry 0 is the file's own record (patch 0113, cDvdQueue::readInit).
// 0 = good, 1 = read it again, 2 = give up. The counter belongs to the one table in flight: the
// game's header_buff is shared by all queues.
int re4_dvd_header_check(const void* table, const char* name, unsigned offset, unsigned length, int depth)
{
    static int retries;
    static unsigned retryOffset;  // the table the retries count for (a read cancelled meanwhile starts over)
    static char retryName[64];
    const u32* w = (const u32*) table;
    bool eof = false, known = true;
    for (int i = 1; i < 32 && !eof; i++) {
        u32 type = w[i * 8];
        if (type == 0xFFFFFFFFu) {
            eof = true;
        } else if (type > 4 && type != 0xFFFFFFFEu) {
            known = false;
        }
    }
    if (eof && known) {
        retries = 0;
        return 0;
    }
    if (retries && (retryOffset != offset || strncmp(retryName, name ? name : "", sizeof(retryName) - 1) != 0)) {
        retries = 0;
    }
    retryOffset = offset;
    snprintf(retryName, sizeof(retryName), "%s", name ? name : "");
    bool zeros = true;
    for (int i = 0; i < 256 && zeros; i++) {
        zeros = w[i] == 0;
    }
    char head[3 * 32 + 1];
    for (int i = 0; i < 32; i++) {
        snprintf(head + i * 3, 4, "%02x ", ((const unsigned char*) table)[i]);
    }
    re4_log("DVD: %s (depth %d, at 0x%x, file length %u): the part table is %s (%s); first bytes %s(try %d)",
            name ? name : "?", depth, (unsigned) offset, (unsigned) length,
            zeros ? "all zeros" : "no good", eof ? "unknown part type" : "no end marker", head, retries + 1);
    if (++retries < 3) {
        return 1;
    }
    retries = 0;
    re4_log("DVD: %s: giving up; import the disc again (the file on the device is damaged or unreadable)",
            name ? name : "?");
    return 2;
}

void re4_set_data_dir(const char* dir)
{
    snprintf(gDataDir, sizeof(gDataDir), "%s", dir);
    for (Disc& d : gDiscs) {
        free(d.fst);
        free(d.parent);
        memset(&d, 0, sizeof(d));
    }
    gCurrent = -1;
}

const char* re4_data_dir(void)
{
    return gDataDir;
}

int re4_dvd_inserted_disc(void)
{
    return current() ? gCurrent : -1;
}

int re4_disc_release(void)
{
    Disc* d = current();
    return d ? d->release : RE4_DISC_UNKNOWN;
}

int re4_disc_retail_eu(void)
{
    return re4_disc_release() == RE4_DISC_RETAIL_EU;
}

// The place of the core archive (patch 0117, read.cpp CoreDataRead): the debug build reads
// etc/core.das to the fixed 0x80578000 with room for CORE_DATA_MAX (0x234000) bytes, up to the
// option data at 0x807AC000. The European release's core is 0x23AC40 bytes: there it overran
// the option data ("CORE_DATA IS TOO LARGE", 26.10.2026). A larger core goes to a range of the
// development half of MEM1 nothing else uses (RE4_MEM1_CORE_ALT_ADDR); 0 keeps the fixed place,
// so the debug build's core stays where it was. The size is the body's (part 0 of the part
// table, unpacked size of a yz2 body), what the game's read writes to memory.
void* re4_core_data_place(const char* name, unsigned fixedMax)
{
    Disc* d = current();
    s32 entry = d ? lookup(*d, name) : -1;
    if (entry < 0 || isDir(*d, (u32) entry)) {
        return 0;
    }
    char path[1400];
    entryPath(*d, (u32) entry, path, sizeof(path));
    unsigned char head[0x440];
    FILE* f = fopen(path, "rb");
    size_t got = f ? fread(head, 1, sizeof(head), f) : 0;
    if (f) {
        fclose(f);
    }
    if (got < 0x440 || be32(head + 0x20) != 0) {
        return 0;  // no part table with the body first: the game's read reports it
    }
    u32 size = be32(head + 0x24);
    unsigned long packed = 0, unpacked = 0;
    char text[0x40];
    memcpy(text, head + 0x400, sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    if (isxdigit((unsigned char) text[0]) && strchr(text, '\t') && strchr(text, '\n') &&
        sscanf(text, "%lx\t%lx", &packed, &unpacked) == 2 && unpacked > 0) {
        size = (u32) unpacked;  // a yz2 body: its header names the unpacked size
    }
    if (size <= fixedMax) {
        return 0;
    }
    if (size > RE4_MEM1_CORE_ALT_MAX) {
        re4_log("DVD: %s: core of %u bytes, larger than the %u bytes of either place", name, size,
                RE4_MEM1_CORE_ALT_MAX);
        return 0;
    }
    re4_log("DVD: %s: core of %u bytes (fixed place %u, %s), read to 0x%08x", name, size, fixedMax,
            releaseName(d->release), RE4_MEM1_CORE_ALT_ADDR);
    return (void*) (unsigned long) RE4_MEM1_CORE_ALT_ADDR;
}

// The place of the option menu's data (patch 0121, read.cpp OptionDataRead): the debug build reads
// SS/<lang>/option.dat to the fixed 0x807AC000 with room for OPTION_DATA_MAX (0x40000) bytes, up to
// the player data at 0x807EC000. On the European discs only the English one fits (0x3FBA0 bytes);
// German, French, Spanish and Italian are 0x44000 to 0x4C2C0 (the European OptionDataRead reads to
// 0x80784000 and has no limit). A larger one goes behind the core's second place
// (RE4_MEM1_OPTION_ALT_ADDR); 0 keeps the fixed place. The file is read whole.
void* re4_option_data_place(const char* name, unsigned fixedMax)
{
    Disc* d = current();
    s32 entry = d ? lookup(*d, name) : -1;
    if (entry < 0 || isDir(*d, (u32) entry)) {
        return 0;
    }
    u32 size = word(*d, (u32) entry, 2);
    if (size <= fixedMax) {
        return 0;
    }
    if (size > RE4_MEM1_OPTION_ALT_MAX) {
        re4_log("DVD: %s: option data of %u bytes, larger than the %u bytes of either place", name, size,
                RE4_MEM1_OPTION_ALT_MAX);
        return 0;
    }
    re4_log("DVD: %s: option data of %u bytes (fixed place %u, %s), read to 0x%08x", name, size, fixedMax,
            releaseName(d->release), RE4_MEM1_OPTION_ALT_ADDR);
    return (void*) (unsigned long) RE4_MEM1_OPTION_ALT_ADDR;
}

// The start without the frame pace (patch 0085, cDvd::readProcMain; wish of
// the project owner, 19.10.2026): until the first SS/<lang>/title.dat is in,
// the game's read queue takes its next step at once instead of the next
// frame, with one frame's wait after every 64 steps (the other tasks, and a
// read that keeps failing). RE4_DVD_BOOT_FAST=0: the game's pace throughout.
static int gBootFast = -1;  // -1: not asked yet, 1: the start, 0: done or off
static unsigned gBootSteps, gBootWaits;

int re4_dvd_boot_fast(void)
{
    if (gBootFast < 0) {
        const char* v = getenv("RE4_DVD_BOOT_FAST");
        gBootFast = v && *v == '0' ? 0 : 1;
        re4_log(gBootFast ? "DVD: the start's reads without the frame pace (until title.dat)"
                          : "DVD: RE4_DVD_BOOT_FAST=0, the game's read pace throughout");
    }
    if (!gBootFast) {
        return 0;
    }
    if (++gBootSteps % 64 == 0) {
        gBootWaits++;
        return 0;
    }
    return 1;
}

// The queue finished a read (patch 0085): the first title.dat ends the start.
void re4_dvd_read_done(const char* name)
{
    static const char kTitle[] = "/title.dat";
    if (gBootFast != 1 || !name) {
        return;
    }
    size_t n = strlen(name), k = sizeof(kTitle) - 1;
    if (n < k || strcasecmp(name + n - k, kTitle) != 0) {
        return;
    }
    gBootFast = 0;
    re4_log("DVD: start reads done (%s): %u steps without a frame's wait, %u with; the game's pace from here",
            name, gBootSteps - gBootWaits, gBootWaits);
}

s32 DVDConvertPathToEntrynum(const char* path)
{
    Disc* d = current();
    return d ? lookup(*d, path) : -1;
}

int DVDFastOpen(s32 entrynum, DVDFileInfo* fi)
{
    Disc* d = current();
    if (!d || entrynum <= 0 || (u32) entrynum >= d->count || isDir(*d, (u32) entrynum)) {
        return 0;
    }
    fi->startAddr = word(*d, (u32) entrynum, 1);
    fi->length = word(*d, (u32) entrynum, 2);
    fi->callback = 0;
    fi->cb.state = kStateEnd;
    fi->cb.transferredSize = 0;
    return 1;
}

int DVDOpen(const char* path, DVDFileInfo* fi)
{
    s32 entry = DVDConvertPathToEntrynum(path);
    if (entry < 0) {
        re4_log("DVD: DVDOpen: %s not on disc", path);
        return 0;
    }
    return DVDFastOpen(entry, fi);
}

int DVDClose(DVDFileInfo* fi)
{
    (void) fi;
    return 1;
}

// The end of a read: state and callback (result -3: cancelled).
static void finishRead(void* object, int result)
{
    DVDFileInfo* fi = (DVDFileInfo*) object;
    fi->cb.state = result == kResultCanceled ? kStateCanceled : result < 0 ? -1 : kStateEnd;
    if (fi->callback) {
        fi->callback(result, fi);
    }
}

int DVDReadAsyncPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio)
{
    (void) prio;
    fi->cb.command = kCommandRead;
    fi->cb.addr = addr;
    fi->cb.length = (u32) length;
    fi->cb.offset = (u32) offset;
    fi->callback = callback;
    fi->cb.state = kStateBusy;
    s32 result = readFile(fi, addr, length, offset);
    fi->cb.transferredSize = result > 0 ? (u32) result : 0;
    if (re4_os_in_interrupt() && re4_os_defer(finishRead, fi, result)) {
        return 1;  // busy until the interrupt handler has returned
    }
    finishRead(fi, result);
    return 1;
}

int DVDReadAsync(DVDFileInfo* fi, void* addr, s32 length, s32 offset, DVDCallback callback)
{
    return DVDReadAsyncPrio(fi, addr, length, offset, callback, 2);
}

s32 DVDReadPrio(DVDFileInfo* fi, void* addr, s32 length, s32 offset, s32 prio)
{
    (void) prio;
    s32 result = readFile(fi, addr, length, offset);
    fi->cb.transferredSize = result > 0 ? (u32) result : 0;
    fi->cb.state = result < 0 ? -1 : kStateEnd;
    return result;
}

s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block)
{
    if (block->state == kStateBusy || block->state == kStateCanceled) {
        return block->state;
    }
    return block->state < 0 ? -1 : kStateEnd;
}

s32 DVDGetDriveStatus(void)
{
    return kStateEnd;
}

u32 DVDGetTransferredSize(DVDFileInfo* fi)
{
    return fi->cb.transferredSize;
}

int DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback)
{
    // a read still waiting for its completion ends as cancelled (its callback
    // gets -3, as from the drive)
    re4_os_deferred_set(finishRead, block, kResultCanceled);
    if (callback) {
        callback(0, block);
    }
    return 1;
}

// The synchronous form (gcci.c, the CRI file interface): as DVDCancelAsync.
s32 DVDCancel(volatile DVDCommandBlock* block)
{
    re4_os_deferred_set(finishRead, (void*) block, kResultCanceled);
    return 0;
}

s32 DVDCancelAll(void)
{
    return 0;
}

DVDDiskID* DVDGetCurrentDiskID(void)
{
    current();
    re4_mem1_init();
    return (DVDDiskID*) (unsigned long) RE4_MEM1_BASE;
}

void* DVDGetFSTLocation(void)
{
    current();
    re4_mem1_init();
    return (void*) (unsigned long) *(volatile u32*) (unsigned long) (RE4_MEM1_BASE + 0x38);
}

int DVDCompareDiskID(const DVDDiskID* a, const DVDDiskID* b)
{
    if (memcmp(a->gameName, b->gameName, 4) != 0 || memcmp(a->company, b->company, 2) != 0) {
        return 0;
    }
    if (a->diskNumber != 0xFF && b->diskNumber != 0xFF && a->diskNumber != b->diskNumber) {
        return 0;
    }
    if (a->gameVersion != 0xFF && b->gameVersion != 0xFF && a->gameVersion != b->gameVersion) {
        return 0;
    }
    return 1;
}

DVDDiskID* DVDGenerateDiskID(DVDDiskID* id, const char* game, const char* company, unsigned char diskNo,
                             unsigned char version)
{
    memset(id, 0, sizeof(*id));
    if (game) {
        memcpy(id->gameName, game, 4);
    }
    if (company) {
        memcpy(id->company, company, 2);
    }
    id->diskNumber = diskNo;
    id->gameVersion = version;
    return id;
}

// The disc swap of the two-disc game: the folder of the requested disc
// becomes the inserted one.
int DVDChangeDiskAsync(DVDCommandBlock* block, DVDDiskID* id, DVDCBCallback callback)
{
    int no = id->diskNumber == 0xFF ? 0 : id->diskNumber;
    block->command = kCommandChangeDisk;
    block->id = id;
    bool ok = no < 2 && insertDisc(no);
    if (!ok) {
        re4_log("DVD: disc %d requested, but %s/disc%d is missing", no + 1, gDataDir, no);
    }
    block->state = ok ? kStateEnd : -1;
    if (callback) {
        callback(ok ? 0 : -1, block);
    }
    return 1;
}

int DVDOpenDir(const char* path, DVDDir* dir)
{
    Disc* d = current();
    s32 entry = d ? lookup(*d, path) : -1;
    if (entry < 0 || !isDir(*d, (u32) entry)) {
        return 0;
    }
    dir->entryNum = (u32) entry;
    dir->location = (u32) entry + 1;
    dir->next = word(*d, (u32) entry, 2);
    return 1;
}

int DVDReadDir(DVDDir* dir, DVDDirEntry* out)
{
    Disc* d = current();
    if (!d || dir->location <= dir->entryNum || dir->location >= dir->next) {
        return 0;
    }
    u32 i = dir->location;
    out->entryNum = i;
    out->isDir = isDir(*d, i);
    out->name = (char*) entryName(*d, i);
    dir->location = out->isDir ? word(*d, i, 2) : i + 1;
    return 1;
}

int DVDCloseDir(DVDDir* dir)
{
    (void) dir;
    return 1;
}

}  // extern "C"
