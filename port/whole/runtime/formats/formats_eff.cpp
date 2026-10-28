// Formatumwandler: effect data (EFF, src/game/eff_sys.cpp EspDataLoad, version
// 0xB) and the small tables next to it in the archives: texture sets (TEX,
// texture.cpp cTexSys::DataLoad, version 3) and rumble patterns (VIB,
// pad.cpp VibSetData).
#include "formats.h"

#include "esp.h"
#include "espgen.h"
#include "pad.h"
#include "path.h"
#include "re4_runtime.h"
#include "texture.h"

using namespace re4fmt;

static_assert(sizeof(cEspSeqTbl) == 0x12C, "cEspSeqTbl layout");
static_assert(offsetof(cEspSeqHead, SeqTbl) == 0x30, "cEspSeqHead layout");
static_assert(offsetof(cAnm, Frame_cnt) == 0x10, "cAnm layout");
static_assert(sizeof(PATH_UNIT) == 0x28, "PATH_UNIT layout");
static_assert(sizeof(VIB_DATA) == 8, "VIB_DATA layout");
static_assert(sizeof(TexIdEnt) == 8, "TexIdEnt layout");

namespace re4fmt {

namespace {

// eff_sys.cpp's ESP_EFF_HEADER: version 0xB, then eleven offsets from the
// file start (0 = no such table).
enum {
    kTplIds = 1, kEstIds, kSstIds, kPathIds, kObjIds,
    kTpls, kAnms, kEsts, kSsts, kPaths, kObjs,
    kEffWords = 12
};

// Table of u32 count + `entry`-byte records whose first u16 and the u32 at +4
// are numbers (EffIdTbl, SstList, TexIdTbl: u16 id/no, u16 type, u32 flags).
void convertIdList(Blob& b, u32 o)
{
    if (o == 0) {
        return;
    }
    u32 n = be32(b, o);
    swap32(b, o);
    if (!fits(b, o + 4, n * 8, "id list")) {
        return;
    }
    for (u32 i = 0; i < n; i++) {
        swap16(b, o + 4 + i * 8, 2);
        swap32(b, o + 4 + i * 8 + 4);
    }
}

// u32 count + u32 offsets from the table start; returns the count and fills
// `offs` (up to `max`) with the offsets in disc order.
u32 convertOfsTableAt(Blob& b, u32 o, u32* offs, u32 max)
{
    u32 n = be32(b, o);
    if (!fits(b, o + 4, n * 4, "offset table")) {
        return 0;
    }
    for (u32 i = 0; i < n && i < max; i++) {
        offs[i] = be32(b, o + 4 + i * 4);
    }
    swap32(b, o, n + 1);
    return n < max ? n : max;
}

// The same for a table a header points at (offset 0 = no table).
u32 convertOfsTable(Blob& b, u32 o, u32* offs, u32 max)
{
    return o ? convertOfsTableAt(b, o, offs, max) : 0;
}

void convertAnm(Blob& b, u32 o)
{
    if (!fits(b, o, offsetof(cAnm, Xn), "cAnm")) {
        return;
    }
    RE4_SWAP(b, o, cAnm, Width);
    RE4_SWAP(b, o, cAnm, Height);
    RE4_SWAP(b, o, cAnm, Cx);
    RE4_SWAP(b, o, cAnm, Cy);
    RE4_SWAP(b, o, cAnm, Frames);  // the pattern table after it is bytes
}

void convertSeqRecord(Blob& b, u32 o)
{
    typedef cEspSeqTbl T;
    RE4_SWAP(b, o, T, Set_time);
    RE4_SWAP(b, o, T, Tool_flg);
    // Pos .. D_size_plus: 36 floats.
    swap32(b, o + offsetof(T, Pos), (offsetof(T, Col_start_r) - offsetof(T, Pos)) / 4);
    RE4_SWAP(b, o, T, Col_d_r);
    RE4_SWAP(b, o, T, Col_d_g);
    RE4_SWAP(b, o, T, Col_d_b);
    RE4_SWAP(b, o, T, Col_d_a);
    RE4_SWAP(b, o, T, Col_max_cnt);
    RE4_SWAP(b, o, T, Col_start_cnt);
    RE4_SWAP(b, o, T, Pos_start_cnt);
    RE4_SWAP(b, o, T, Size_start_cnt);
    RE4_SWAP(b, o, T, Life_max);
    RE4_SWAP(b, o, T, Life_time);
    RE4_SWAP(b, o, T, Anm_cnt);
    // EspGenPrm: two words; the narrow views read their low parts
    // (whole_prepare.py rewrites them to casts of the words).
    swap32(b, o + offsetof(T, prm), 2);
    RE4_SWAP(b, o, T, xD4);
    swap32(b, o + offsetof(T, Vec0), 9);
    RE4_SWAP_ARRAY(b, o, T, Espgen_work16);
    swap32(b, o + offsetof(T, Espgen_vec0), 3);
}

// cEspSeqHead: header, then `num` records.
void convertSeqHead(Blob& b, u32 o)
{
    typedef cEspSeqHead H;
    if (!fits(b, o, offsetof(H, SeqTbl), "cEspSeqHead")) {
        return;
    }
    u16 n = be16(b, o + offsetof(H, num));
    RE4_SWAP(b, o, H, num);
    RE4_SWAP(b, o, H, flags);
    swap32(b, o + offsetof(H, pos), 3);
    swap32(b, o + offsetof(H, rot), 3);
    RE4_SWAP(b, o, H, Core_flg);
    RE4_SWAP_ARRAY(b, o, H, pad1);
    if (!fits(b, o + offsetof(H, SeqTbl), n * sizeof(cEspSeqTbl), "effect records")) {
        return;
    }
    for (u32 i = 0; i < n; i++) {
        convertSeqRecord(b, o + offsetof(H, SeqTbl) + i * sizeof(cEspSeqTbl));
    }
}

// SstData: u32 count + offsets (from the table) of cEspSeqHead blocks.
void convertSeqTable(Blob& b, u32 o)
{
    static u32 offs[1024];
    u32 n = convertOfsTable(b, o, offs, 1024);
    Seen seen;
    for (u32 i = 0; i < n && b.ok; i++) {
        if (seen.first(offs[i])) {
            convertSeqHead(b, o + offs[i]);
        }
    }
}

void reportOnce(bool& done, const char* what, const char* where)
{
    if (!done) {
        done = true;
        re4_log("formats: %s (first in %s): no converter yet, left big-endian", what, where);
    }
}

}  // namespace

// path.cpp Path: u16 count, then PATH_UNIT records (floats and bytes).
void convertPath(Blob& b, u32 o)
{
    u16 n = be16(b, o);
    swap16(b, o);
    if (!fits(b, o + offsetof(Path, vtx), n * sizeof(PATH_UNIT), "path vertices")) {
        return;
    }
    for (u32 i = 0; i < n; i++) {
        u32 v = o + offsetof(Path, vtx) + i * sizeof(PATH_UNIT);
        swap32(b, v + offsetof(PATH_UNIT, pos), 3);
        swap32(b, v + offsetof(PATH_UNIT, normal), 3);
        RE4_SWAP(b, v, PATH_UNIT, Length);
    }
}

// PTH, a room archive entry: one Path (r333's jet ski rail, pl0e.cpp cPl0e::setRail; em36's).
// Without it the ride read big-endian floats: the ski and the camera at (0 0 0), then at 2e28,
// most of the ending's picture gone (SCV40 report 23.10.2026 10:31).
void convertPathEntry(Blob& b, const Part&)
{
    convertPath(b, 0);
}

void convertEffect(Blob& b, const Part&)
{
    if (be32(b, 0) != 0xB) {
        fits(b, 0, 0xFFFFFFFFu, "EFF version");
        return;
    }
    u32 h[kEffWords];
    for (int i = 0; i < kEffWords; i++) {
        h[i] = be32(b, i * 4);
    }
    swap32(b, 0, kEffWords);
    convertIdList(b, h[kTplIds]);
    convertIdList(b, h[kEstIds]);
    convertIdList(b, h[kSstIds]);
    convertIdList(b, h[kPathIds]);
    convertIdList(b, h[kObjIds]);
    static u32 offs[1024];
    u32 n = convertOfsTable(b, h[kTpls], offs, 1024);
    Seen seen;
    for (u32 i = 0; i < n && b.ok; i++) {
        if (seen.first(h[kTpls] + offs[i])) {
            convertTplAt(b, h[kTpls] + offs[i]);
        }
    }
    n = convertOfsTable(b, h[kAnms], offs, 1024);
    for (u32 i = 0; i < n && b.ok; i++) {
        if (seen.first(h[kAnms] + offs[i])) {
            convertAnm(b, h[kAnms] + offs[i]);
        }
    }
    convertSeqTable(b, h[kEsts]);
    convertSeqTable(b, h[kSsts]);
    n = convertOfsTable(b, h[kPaths], offs, 1024);
    for (u32 i = 0; i < n && b.ok; i++) {
        if (seen.first(h[kPaths] + offs[i])) {
            convertPath(b, h[kPaths] + offs[i]);
        }
    }
    // Effect models (eff_sys.cpp EffEfmEnt: x0, offsets of the BIN model, the
    // TPL, the motion table (EspEfmMotTbl: u32 count, offsets of MotionData
    // from the table) and the shape header, from the entry).
    n = convertOfsTable(b, h[kObjs], offs, 1024);
    static bool shapeReported;
    u32 starts[4096];
    u32 nStarts = 0;
    for (u32 i = 0; i < kEffWords; i++) {
        starts[nStarts++] = h[i];
    }
    for (u32 i = 0; i < n; i++) {
        u32 e = h[kObjs] + offs[i];
        if (fits(b, e, 20, "EffEfmEnt") && nStarts + 5 <= 4096) {
            starts[nStarts++] = e;
            for (u32 k = 1; k < 5; k++) {
                u32 o = be32(b, e + k * 4);
                if (o) {
                    starts[nStarts++] = e + o;
                }
            }
        }
    }
    for (u32 i = 0; i < n && b.ok; i++) {
        u32 e = h[kObjs] + offs[i];
        if (!seen.first(e) || !fits(b, e, sizeof(u32) * 5, "EffEfmEnt")) {
            continue;
        }
        u32 model = be32(b, e + 4);
        u32 tpl = be32(b, e + 8);
        u32 mot = be32(b, e + 12);
        u32 shape = be32(b, e + 16);
        swap32(b, e, 5);
        if (model && seen.first(e + model)) {
            u32 end = b.size;
            for (u32 k = 0; k < nStarts; k++) {
                if (starts[k] > e + model && starts[k] < end) {
                    end = starts[k];
                }
            }
            convertBinAt(b, e + model, end);
        }
        if (tpl && seen.first(e + tpl)) {
            convertTplAt(b, e + tpl);
        }
        if (mot && seen.first(e + mot) && fits(b, e + mot, 4, "EspEfmMotTbl")) {
            u32 t = e + mot;
            u32 count = be32(b, t);
            if (fits(b, t + 4, count * 4, "EspEfmMotTbl offsets")) {
                u32 textures = 0;
                for (u32 k = 0; k < count && b.ok; k++) {
                    u32 m = t + be32(b, t + 4 + k * 4);
                    if (!seen.first(m)) {
                        continue;
                    }
                    // a texture in the motion slot (TPL magic 0x0020AF30): r10b's effect model 0x7c
                    // has three 128x128 CMPR textures there whose zero bytes are stored as 0x20
                    // (st1/r10b.das #7; original data). The game reads a motion only for an
                    // ESP_EFM04 effect with Tool_flg 8 (esp_efm.cpp), and these would be no motion
                    // on the GameCube either: left as they are
                    u32 magic = fits(b, m, 4, "EspEfmMotTbl motion") ? be32(b, m) : 0;
                    if (magic == 0x0020AF30u || magic == 0x2020AF30u) {
                        textures++;
                        continue;
                    }
                    convertMotionAt(b, m);
                }
                if (textures) {
                    re4_log("formats: %s: effect model at 0x%x: %u of the %u motions are textures (TPL magic %08x, "
                            "original data), left as they are",
                            b.name, (unsigned) e, (unsigned) textures, (unsigned) count,
                            (unsigned) be32(b, t + be32(b, t + 4)));
                }
                swap32(b, t, 1 + count);
            }
        }
        if (shape) {
            reportOnce(shapeReported, "effect model shape header", b.name);
        }
    }
}

// TEX (cTexSys::DataLoad, version 3): offsets of the id table, the TPL table
// and the animation table.
void convertTexSet(Blob& b, const Part&)
{
    if (be32(b, 0) != 3) {
        fits(b, 0, 0xFFFFFFFFu, "TEX version");
        return;
    }
    u32 ids = be32(b, offsetof(TexData, ofsId));
    u32 tpls = be32(b, offsetof(TexData, ofsTpl));
    u32 anms = be32(b, offsetof(TexData, ofsAnm));
    swap32(b, 0, sizeof(TexData) / 4);
    convertIdList(b, ids);
    static u32 offs[1024];
    Seen seen;
    u32 n = convertOfsTable(b, tpls, offs, 1024);
    for (u32 i = 0; i < n && b.ok; i++) {
        if (seen.first(tpls + offs[i])) {
            convertTplAt(b, tpls + offs[i]);
        }
    }
    n = convertOfsTable(b, anms, offs, 1024);
    for (u32 i = 0; i < n && b.ok; i++) {
        if (seen.first(anms + offs[i])) {
            convertAnm(b, anms + offs[i]);
        }
    }
}

// VIB (VibDataTbl): u32 count, offsets (from the table, 0 = none) of VIB_INFO
// blocks: u32 count + VIB_DATA records.
void convertVibration(Blob& b, const Part&)
{
    static u32 offs[1024];
    u32 n = convertOfsTableAt(b, 0, offs, 1024);
    Seen seen;
    for (u32 i = 0; i < n && b.ok; i++) {
        u32 o = offs[i];
        if (o == 0 || !seen.first(o)) {
            continue;
        }
        u32 count = be32(b, o);
        swap32(b, o);
        if (!fits(b, o + 4, count * sizeof(VIB_DATA), "VIB_DATA")) {
            return;
        }
        for (u32 k = 0; k < count; k++) {
            u32 d = o + 4 + k * sizeof(VIB_DATA);
            RE4_SWAP(b, d, VIB_DATA, flag);
            RE4_SWAP(b, d, VIB_DATA, delay);
            RE4_SWAP(b, d, VIB_DATA, time);
        }
    }
}

}  // namespace re4fmt
