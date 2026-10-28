// Formatumwandler: message tables (MDT, src/game/mes.cpp MessageData::getAddr).
#include "formats.h"

#include "re4_runtime.h"

using namespace re4fmt;

namespace re4fmt {

// Offsets in a header of 32-bit words: the header ends where the first block
// it points at begins. Returns the number of header words (at least `from`).
u32 headerWords(Blob& b, u32 from)
{
    u32 end = b.size;
    for (u32 i = from; i * 4 < end && b.ok; i++) {
        u32 v = be32(b, i * 4);
        if (v != 0 && v < end) {
            end = v;
        }
    }
    return end / 4;
}

// u32 word 0 (number of languages), u32 offset of each language block. A block
// is mes.cpp's MesTblBlock: u32 x0, u32 count, u32 ofs[count] (from the block),
// then the u16 message codes up to the next block. Events without text carry
// a 32-byte table whose block is cut off after x0 (r215s02, r227s02, entry 17
// of r215's room archive): the game would read the count from what follows
// and never asks for a message there. x0 is turned, the rest left alone.
// The European discs keep the sub screens' texts as one-language tables
// (ss/<lang>/*.dat, retail-eu-plan.md R7): the block alone, its first offset
// right after the offsets. Every six-language table starts 6, 0x1C (patch 0122).
void convertMdt(Blob& b, const Part&)
{
    u32 count = b.size >= 12 ? be32(b, 4) : 0;
    if (count != 0 && !(be32(b, 0) == 6 && count == 0x1C) && count < 0x2000 && 8 + count * 4 <= b.size &&
        be32(b, 8) == 8 + count * 4) {
        swap32(b, 0, 2 + count);
        u32 text = 8 + count * 4;
        swap16(b, text, (b.size - text) / 2);
        return;
    }
    u32 words = headerWords(b, 1);
    u32 blocks[64];
    u32 n = 0;
    for (u32 i = 1; i < words && n < 64 && b.ok; i++) {
        u32 v = be32(b, i * 4);
        bool dup = v == 0;
        for (u32 k = 0; k < n && !dup; k++) {
            dup = blocks[k] == v;
        }
        if (!dup) {
            blocks[n++] = v;
        }
    }
    swap32(b, 0, words);
    for (u32 k = 0; k < n && b.ok; k++) {
        u32 o = blocks[k];
        u32 end = b.size;
        for (u32 j = 0; j < n; j++) {
            if (blocks[j] > o && blocks[j] < end) {
                end = blocks[j];
            }
        }
        if (o < b.size && b.size - o < 8) {
            if (b.size - o >= 4) {
                swap32(b, o);
            }
            continue;
        }
        u32 count = be32(b, o + 4);
        if (!fits(b, o, 8 + count * 4, "message offsets") || 8 + count * 4 > end - o) {
            fits(b, o, 0xFFFFFFFFu, "message block");
            return;
        }
        swap32(b, o, 2 + count);
        u32 text = o + 8 + count * 4;
        swap16(b, text, (end - text) / 2);
    }
}

}  // namespace re4fmt
