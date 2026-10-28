#ifndef RE4_FORMATS_H
#define RE4_FORMATS_H

// Format converters: disc data is big-endian, the original code reads it in
// place as structures. Every loaded part is converted to host byte order once,
// right after the game's DVD queue finished it (patch 0015) or the Yz2
// decoder unpacked it. See port/docs/whole-program-status.md, "Formatumwandler".
//
// The converters are compiled with the game's flags and headers: structure
// sizes and field offsets come from the original code. Only what the CPU reads
// is converted; texel data, display lists and audio samples stay big-endian
// for the renderer (M6) and the audio layer (M8).

#include "types.h"

#include <stddef.h>

namespace re4fmt {

// A block of disc data being converted. Every access is checked against
// `size`; a structure that does not fit is logged and clears `ok`, the
// converter then stops instead of touching memory outside the block.
struct Blob {
    u8* data;
    u32 size;
    const char* name;
    bool ok;
};

Blob blob(void* data, u32 size, const char* name);
bool fits(Blob& b, u32 off, u32 len, const char* what);

// Big-endian reads (before the conversion); 0 and a log line when outside.
u32 be32(Blob& b, u32 off);
u16 be16(Blob& b, u32 off);
// Host-order reads (after the conversion).
u32 rd32(Blob& b, u32 off);
u16 rd16(Blob& b, u32 off);

void swap16(Blob& b, u32 off, u32 count = 1);
void swap32(Blob& b, u32 off, u32 count = 1);
// One structure field: `size` bytes made of `elem`-byte elements (1, 2, 4).
void swapField(Blob& b, u32 base, u32 off, u32 size, u32 elem);

// Where a part comes from; converters that need more than the bytes get it.
struct Part {
    const char* name;  // lower-case path on the disc
    int nest;          // nesting depth of the part table (0 = top)
    int index;         // part number at that depth
    u32 type;          // DvdHeader::type
    u32 sndType;       // DvdHeader::sndType
    u32 fileOfs;       // DvdHeader::ofs: offset of the part in the file
};

typedef void (*Converter)(Blob& b, const Part& part);

void convertWords(Blob& b, const Part& part);  // file made of 32-bit words only
void convertSizeTbl(Blob& b, const Part& part);

// formats_snd.cpp
void convertStrHed(Blob& b, const Part& part);
void convertDoorseHed(Blob& b, const Part& part);
void convertBgmTbl(Blob& b, const Part& part);
void convertSoundBlock(Blob& b, const Part& part);  // DvdHeader type 1 (TRANS_SND_BLK)

// formats_tex.cpp
void convertTplAt(Blob& b, u32 base);
void convertTpl(Blob& b, const Part& part);
void convertFont(Blob& b, const Part& part);

// formats_mes.cpp
u32 headerWords(Blob& b, u32 from);  // offset table ends at the first block
void convertMdt(Blob& b, const Part& part);

// formats_id.cpp
void convertIdLayout(Blob& b, const Part& part);  // UWF

// formats_eff.cpp
void convertEffect(Blob& b, const Part& part);     // EFF
void convertPath(Blob& b, u32 o);                  // path.cpp Path at o (EFF, PTH)
void convertPathEntry(Blob& b, const Part& part);  // PTH (an archive entry: one Path)
void convertTexSet(Blob& b, const Part& part);     // TEX
void convertVibration(Blob& b, const Part& part);  // VIB

// formats_dbg.cpp
void convertRoomInfo(Blob& b, const Part& part);  // debug/roomInfo.dat

// formats_arc.cpp
bool looksLikeArchive(Blob& b);
void convertArchive(Blob& b, const Part& part);
void convertCodecArchive(Blob& b, const Part& part);  // op/opNN.das: archive at +0x400

// formats.cpp: a file inside a container, chosen by its name's extension
void convertNamedFile(Blob& b, const Part& part, const char* name);

// formats_model.cpp
bool isBin(Blob& b, u32 base);
void convertBinAt(Blob& b, u32 base, u32 end);
void convertBin(Blob& b, const Part& part);       // BIN: model or (core) light path
void convertMotionAt(Blob& b, u32 base);         // MotionData (FCV, camera and shape curves)
void convertMotion(Blob& b, const Part& part);
void convertSeqAt(Blob& b, u32 base);
void convertSeq(Blob& b, const Part& part);

// formats_lit.cpp
void convertLitAt(Blob& b, u32 base);
void convertLit(Blob& b, const Part& part);

// formats_room.cpp
void convertArea(Blob& b, u32 off);  // AREA_HIT_DATA
void convertCons(Blob& b, const Part& part);         // CNS
void convertEtcList(Blob& b, const Part& part);      // ETS
void convertEmi(Blob& b, const Part& part);          // EMI
void convertLightArea(Blob& b, const Part& part);    // SAR
void convertEffArea(Blob& b, const Part& part);      // EAR
void convertFloorSe(Blob& b, const Part& part);      // FSE
void convertSeArea(Blob& b, const Part& part);       // ESE
void convertDoorSe(Blob& b, const Part& part);       // DSE
void convertSoundInfo(Blob& b, const Part& part);    // STB
void convertBlock(Blob& b, const Part& part);        // BLK
void convertRoute(Blob& b, const Part& part);        // RTP
void convertUnread(Blob& b, const Part& part);       // OSD
void convertSceAreas(Blob& b, const Part& part);     // AEV
void convertSceItems(Blob& b, const Part& part);     // ITA
void convertEmList(Blob& b, const Part& part);       // etc/*.esl

// formats_scene.cpp
void convertSmd(Blob& b, const Part& part);          // SMD
void convertSmx(Blob& b, const Part& part);          // SMX
void convertShd(Blob& b, const Part& part);          // SHD
void convertEtcArchive(Blob& b, const Part& part);   // ETM
void convertItemModels(Blob& b, const Part& part);   // ITM
void convertSat(Blob& b, const Part& part);          // SAT, EAT
void convertMapRoom(Blob& b, const Part& part);      // MAP (map screen rooms: models, floors)

// formats_event.cpp
void convertCameraAt(Blob& b, u32 base);
void convertCamera(Blob& b, const Part& part);       // CAM
struct EvdMarks;  // what the packets of an event bundle do with assets (formats_event.cpp)
void convertEvdAt(Blob& b, u32 base, u32 end, const EvdMarks* bundle);  // bundle: of an EVS, or 0
void convertEvd(Blob& b, const Part& part);          // EVD, event/evd/*.evd
void convertEvdFile(Blob& b, const Part& part);      // a top-level .evd: also noted for the bridge
void convertEvs(Blob& b, const Part& part);          // EVS

// Coverage (formats.cpp): what each archive entry became, for the
// GetDataExt log (patch 0025, re4_formats_get_data).
enum EntryStatus { kEntryConverted = 1, kEntryNoConverter = 2, kEntryIncomplete = 3 };
// `entry`: index in the archive at `archive` (noteArchive); the core bridge
// (re4_formats_locate) tells the app where a model's BIN/TPL came from.
void noteEntry(const void* addr, const char* tag, int status, const void* archive = 0, int entry = -1);
// An archive of `size` bytes from files/<part.name>, part.fileOfs bytes into
// the file (0xFFFFFFFF: Yz2-packed, the archive is the unpacked file).
// offs/count: the entry start offsets (re4_formats_locate finds nested data by them)
// plain: a file without an entry table (EVD); its data is found by offset
void noteArchive(const void* addr, unsigned size, const char* name, const Part& part, const u32* offs = 0,
                 u32 count = 0, bool plain = false);
int containingEntry(const void* addr, int* slot, u32* inner);
// what noteEntry recorded (0 = nothing); tag, if given, receives its tag (4 chars)
int entryStatus(const void* addr, char* tag = 0);

// Remembers converted offsets of one blob, so a structure that two tables
// point at is converted once. Fixed size: the converters run before the game
// heaps exist and must not allocate.
struct Seen {
    enum { kMax = 4096 };
    u32 off[kMax];
    u32 n;
    bool full;
    Seen() : n(0), full(false) {}
    bool first(u32 o);  // true the first time `o` is seen
};

}  // namespace re4fmt

// Field swaps by the original structure definition.
#define RE4_SWAP(blob, base, T, f) \
    re4fmt::swapField(blob, base, offsetof(T, f), sizeof(((T*) 0)->f), sizeof(((T*) 0)->f))
#define RE4_SWAP_ARRAY(blob, base, T, f) \
    re4fmt::swapField(blob, base, offsetof(T, f), sizeof(((T*) 0)->f), sizeof(((T*) 0)->f[0]))

#endif
