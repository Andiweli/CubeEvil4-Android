// Formatumwandler: archives with a table of contents (src/game/read.cpp
// GetDataExt, card.cpp CardArc): u32 count, u32 offset of an embedded REL
// module (0 = none), two pad words, u32 offset of every entry, then a 4-byte
// tag per entry ("TPL", "MDT", "BIN" ...). Each entry is converted by its tag.
#include "formats.h"

#include "re4_runtime.h"

#include <string.h>

using namespace re4fmt;

namespace re4fmt {

namespace {

struct TagRule {
    const char* tag;
    Converter convert;
};

const TagRule kTags[] = {
    { "TPL", convertTpl },
    { "MDT", convertMdt },
    { "UWF", convertIdLayout },
    { "EFF", convertEffect },
    { "TEX", convertTexSet },
    { "VIB", convertVibration },
    // a message font inside an archive (etc/movie.das of the European discs: the movie subtitles)
    { "FNT", convertFont },
    // models and motions
    { "BIN", convertBin },
    { "FCV", convertMotion },
    { "SEQ", convertSeq },
    // room data (game.cpp gameRoomInit, read.cpp ReadAreaData)
    { "CNS", convertCons },
    { "SMD", convertSmd },
    { "SMX", convertSmx },
    { "LIT", convertLit },
    { "SAT", convertSat },
    { "EAT", convertSat },
    { "AEV", convertSceAreas },
    { "ITA", convertSceItems },
    { "SHD", convertShd },
    { "EAR", convertEffArea },
    { "SAR", convertLightArea },
    { "ITM", convertItemModels },
    { "ETM", convertEtcArchive },
    { "ETS", convertEtcList },
    { "CAM", convertCamera },
    { "BLK", convertBlock },
    { "EVS", convertEvs },
    { "EVD", convertEvd },
    { "FSE", convertFloorSe },
    { "ESE", convertSeArea },
    { "DSE", convertDoorSe },
    { "STB", convertSoundInfo },
    { "RTP", convertRoute },
    { "PTH", convertPathEntry },
    { "OSD", convertUnread },
    { "EMI", convertEmi },
    // the map screen's rooms (SS/cmn/map_objNN.dat, Sscrn ss_map.cpp)
    { "MAP", convertMapRoom },
};

// Logs an unknown tag once.
void reportTag(const char* tag, const char* where)
{
    static char seen[256][4];
    static u32 n;
    for (u32 i = 0; i < n; i++) {
        if (memcmp(seen[i], tag, 4) == 0) {
            return;
        }
    }
    if (n < 256) {
        memcpy(seen[n++], tag, 4);
    }
    re4_log("formats: archive entry \"%.4s\" (first in %s): no converter, left big-endian", tag, where);
}

}  // namespace

bool looksLikeArchive(Blob& b)
{
    if (b.size < 0x20) {
        return false;
    }
    u32 count = be32(b, 0);
    u32 rel = be32(b, 4);
    if (count == 0 || count > 0x1000 || 0x10 + count * 8 > b.size || be32(b, 8) || be32(b, 12)) {
        return false;
    }
    if (rel != 0 && (rel > b.size || rel % 32)) {
        return false;
    }
    u32 first = be32(b, 0x10);
    return first >= 0x10 + count * 8 && first <= b.size;
}

namespace {

// A converter chosen by the entry's position instead of its tag (nullptr:
// by tag), for archives whose readers index the entries (codec archives).
typedef Converter (*PickConverter)(u32 index, u32 count);

void convertArchiveWith(Blob& b, const Part& part, PickConverter pick);

}  // namespace

void convertArchive(Blob& b, const Part& part)
{
    convertArchiveWith(b, part, nullptr);
}

namespace {

void convertArchiveWith(Blob& b, const Part& part, PickConverter pick)
{
    if (!looksLikeArchive(b)) {
        fits(b, 0, 0xFFFFFFFFu, "archive header");
        return;
    }
    u32 count = be32(b, 0);
    u32 rel = be32(b, 4);
    u32 end = rel ? rel : b.size;
    u32 offs[0x1000];
    for (u32 i = 0; i < count; i++) {
        offs[i] = be32(b, 0x10 + i * 4);
    }
    swap32(b, 0, 4 + count);
    noteArchive(b.data, b.size, b.name, part, offs, count);
    Seen seen;
    for (u32 i = 0; i < count && b.ok; i++) {
        u32 o = offs[i];
        const char* entryTag = (const char*) b.data + 0x10 + count * 4 + i * 4;
        // An entry whose offset a later entry repeats has no data of its own
        // (a motion without sequence, a model without texture ...): the data
        // there belongs to the last entry with that offset and is converted
        // by its tag only.
        bool empty = false;
        for (u32 k = i + 1; k < count && !empty; k++) {
            empty = offs[k] == o;
        }
        if (empty || !seen.first(o)) {
            continue;
        }
        u32 stop = end;
        for (u32 k = 0; k < count; k++) {
            if (offs[k] > o && offs[k] < stop) {
                stop = offs[k];
            }
        }
        if (o > stop || !fits(b, o, stop - o, "archive entry")) {
            return;
        }
        if (stop == o) {
            noteEntry(b.data + o, entryTag, kEntryConverted, b.data, (int) i);  // empty entry
            continue;
        }
        char tag[5];
        memcpy(tag, b.data + 0x10 + count * 4 + i * 4, 4);
        tag[4] = 0;
        char label[160];
        snprintf(label, sizeof(label), "%s #%u %s", b.name, (unsigned) i, tag);
        Blob e = blob(b.data + o, stop - o, label);
        Converter conv = pick ? pick(i, count) : 0;
        for (const TagRule& t : kTags) {
            if (!conv && strcmp(t.tag, tag) == 0) {
                conv = t.convert;
            }
        }
        if (conv) {
            conv(e, part);
            noteEntry(b.data + o, entryTag, e.ok ? kEntryConverted : kEntryIncomplete, b.data, (int) i);
            if (!e.ok) {
                // what bounds the entry: the next entry (its tag) or the archive end
                int next = -1;
                for (u32 k = 0; k < count; k++) {
                    if (offs[k] == stop) {
                        next = (int) k;
                    }
                }
                if (next >= 0) {
                    re4_log("formats: %s: conversion incomplete (entry 0x%x..0x%x, next entry #%d %.4s, archive "
                            "end 0x%x)",
                            label, (unsigned) o, (unsigned) stop, next,
                            (const char*) b.data + 0x10 + count * 4 + next * 4, (unsigned) end);
                } else {
                    re4_log("formats: %s: conversion incomplete (entry 0x%x..0x%x = archive end)", label,
                            (unsigned) o, (unsigned) stop);
                }
            }
        } else {
            reportTag(tag, b.name);
            noteEntry(b.data + o, entryTag, kEntryNoConverter, b.data, (int) i);
        }
    }
    // The embedded REL module: OSLink converts its header too, but the game
    // reads bssSize before linking (read.cpp).
    if (rel != 0 && b.ok) {
        re4_rel_header_to_host(b.data + rel);
    }
}

// SsTermMain::OpeMesSeq (include/ss_main.h): u16 be_flag, s16 No, int Frame,
// int NoMes, int Timer; the table ends with Timer -1.
void convertOpeMesSeq(Blob& b, const Part&)
{
    for (u32 o = 0; o + 16 <= b.size; o += 16) {
        swap16(b, o, 2);
        swap32(b, o + 4, 3);
        s32 timer;
        memcpy(&timer, b.data + o + 12, 4);
        if (timer == -1) {
            break;
        }
    }
}

// Codec archives (op/opNN.das): SsTermMain::OpeMesTblInit takes the first
// half of the entries as message data and the second half as sequence tables
// (SS_ARC_PTR(arc, 4 + n) is entry n): op01 13 + 13, op02 6 + 6, op03 5 + 5.
Converter pickCodecEntry(u32 index, u32 count)
{
    return count % 2 == 0 && index >= count / 2 ? convertOpeMesSeq : nullptr;
}

}  // namespace

// Codec messages (ss_term.cpp opArc): an archive 0x400 bytes into the file;
// the game reads nothing before it.
void convertCodecArchive(Blob& b, const Part& part)
{
    if (!fits(b, 0x400, 0x20, "codec archive")) {
        return;
    }
    Blob arc = blob(b.data + 0x400, b.size - 0x400, b.name);
    Part inner = part;
    inner.fileOfs = part.fileOfs + 0x400;
    if (looksLikeArchive(arc)) {
        // the entry tags, for the report (the halves: messages, sequences)
        u32 count = be32(arc, 0);
        char tags[2][64] = {{0}, {0}};
        for (u32 i = 0; i < count; i++) {
            char* list = tags[count % 2 == 0 && i >= count / 2];
            char tag[5] = {0};
            memcpy(tag, arc.data + 0x10 + count * 4 + i * 4, 4);
            if (!strstr(list, tag) && strlen(list) + 6 < sizeof(tags[0])) {
                strcat(list, list[0] ? " " : "");
                strcat(list, tag);
            }
        }
        re4_log("formats: %s: codec archive, %u entries: messages [%s], sequences [%s]", b.name,
                (unsigned) count, tags[0], tags[1]);
    }
    convertArchiveWith(arc, inner, pickCodecEntry);
    b.ok = arc.ok;
}

}  // namespace re4fmt
