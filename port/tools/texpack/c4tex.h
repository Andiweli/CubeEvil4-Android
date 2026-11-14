// CubeEvil4 texture pack file (.c4tex): the layout shared by the PC tool (texpack) and the app's loader.
// Description for people: port/docs/textur-paket-format.md. All numbers little-endian; the image data is
// GL's compressed layout (ETC2 / EAC blocks, each 64-bit block word stored most significant byte first).
//
//   header (128 bytes) | info text (UTF-8 "key=value" lines) | image data (each entry 16-byte aligned) | index
//
// The index is sorted by (texHash, tlutHash, origWidth, origHeight, gxFormat, "_m"); a reader checks the header CRC,
// the index CRC and every entry's bounds before it trusts anything.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace c4tex {

constexpr char kMagic[8] = {'C', '4', 'T', 'E', 'X', 'P', 'A', 'K'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kHeaderBytes = 128;
constexpr std::uint32_t kEntryBytes = 48;
constexpr std::uint32_t kMaxSide = 4096;      // no entry is larger (the tool makes 512 or 1024)
constexpr std::uint32_t kMaxLevels = 13;      // 4096 down to 1
constexpr std::uint32_t kMaxEntries = 1u << 20;
constexpr std::uint32_t kMaxInfoBytes = 64u * 1024;
constexpr std::uint32_t kAlign = 16;

// how the replacement's texels are stored
enum Encoding : std::uint8_t {
  kEtc2Rgb = 1,      // GL_COMPRESSED_RGB8_ETC2, opaque, 8 bytes a block
  kEtc2RgbA1 = 2,    // GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2, 8 bytes a block
  kEtc2RgbaEac = 3,  // GL_COMPRESSED_RGBA8_ETC2_EAC, 16 bytes a block (EAC alpha, then ETC2 colour)
};

enum EntryFlags : std::uint8_t {
  kFlagMipName = 1u << 0,    // the Dolphin name had "_m" (the game's texture has mipmaps)
  kFlagTlut = 1u << 1,       // the name had a palette part (tlutHash is valid)
  kFlagTlutAny = 1u << 2,    // the palette part was "$": any palette
  kFlagArbitrary = 1u << 3,  // "_arb": the pack's own mip levels, kept as they were
};

struct Header {                // 128 bytes
  char magic[8];
  std::uint32_t version;
  std::uint32_t headerBytes;   // 128
  std::uint32_t entryCount;
  std::uint32_t entryBytes;    // 48
  std::uint64_t indexOffset;
  std::uint64_t dataOffset;    // first byte of image data
  std::uint64_t fileBytes;     // the whole file: a shorter file is incomplete
  std::uint32_t maxSide;       // the tool's size (512, 1024)
  std::uint32_t indexCrc;      // CRC-32 of the index
  std::uint32_t infoOffset;    // 128
  std::uint32_t infoBytes;
  std::uint32_t headerCrc;     // CRC-32 of bytes 0..63 (this field and the rest zero while computing)
  std::uint8_t reserved[60];
};
static_assert(sizeof(Header) == 128, "header layout");

struct Entry {                 // 48 bytes
  std::uint64_t texHash;       // Dolphin's XXH64 of the first level in whole tiles
  std::uint64_t tlutHash;      // ... of the palette's used range (kFlagTlut), else 0
  std::uint16_t origWidth;     // the game's texture size from the name
  std::uint16_t origHeight;
  std::uint8_t gxFormat;       // GX texture format from the name (0..14)
  std::uint8_t flags;          // EntryFlags
  std::uint8_t encoding;       // Encoding
  std::uint8_t levels;         // mip levels stored, largest first
  std::uint16_t width;         // the replacement's first level
  std::uint16_t height;
  std::uint32_t dataCrc;       // CRC-32 of the entry's data
  std::uint64_t dataOffset;    // from the start of the file
  std::uint32_t dataBytes;     // all levels
  std::uint32_t reserved;
};
static_assert(sizeof(Entry) == 48, "entry layout");

inline std::size_t BlockBytes(unsigned encoding) { return encoding == kEtc2RgbaEac ? 16 : 8; }

inline std::size_t LevelBytes(unsigned encoding, unsigned w, unsigned h) {
  return static_cast<std::size_t>((w + 3) / 4) * ((h + 3) / 4) * BlockBytes(encoding);
}

inline std::size_t ImageBytes(unsigned encoding, unsigned w, unsigned h, unsigned levels) {
  std::size_t n = 0;
  for (unsigned l = 0; l < levels; l++) {
    n += LevelBytes(encoding, w, h);
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
  }
  return n;
}

inline unsigned FullChain(unsigned w, unsigned h) {
  unsigned n = 1;
  while (w > 1 || h > 1) {
    w = w > 1 ? w / 2 : 1;
    h = h > 1 ? h / 2 : 1;
    n++;
  }
  return n;
}

// CRC-32 (the zlib/PNG polynomial)
inline std::uint32_t Crc32(const void *data, std::size_t n, std::uint32_t crc = 0) {
  static std::uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (std::uint32_t i = 0; i < 256; i++) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; k++)
        c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  const std::uint8_t *p = static_cast<const std::uint8_t *>(data);
  crc = ~crc;
  for (std::size_t i = 0; i < n; i++)
    crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
  return ~crc;
}

inline std::uint32_t HeaderCrc(const Header &h) {
  Header copy = h;
  copy.headerCrc = 0;
  std::memset(copy.reserved, 0, sizeof(copy.reserved));
  return Crc32(&copy, 64);
}

// sort order of the index (and the lookup's). A pack may hold a texture twice, with and without "_m" (Dolphin
// looks up "_m" for a texture the game binds with mipmaps): the flag is the last part of the key.
inline bool EntryLess(const Entry &a, const Entry &b) {
  if (a.texHash != b.texHash) return a.texHash < b.texHash;
  if (a.tlutHash != b.tlutHash) return a.tlutHash < b.tlutHash;
  if (a.origWidth != b.origWidth) return a.origWidth < b.origWidth;
  if (a.origHeight != b.origHeight) return a.origHeight < b.origHeight;
  if (a.gxFormat != b.gxFormat) return a.gxFormat < b.gxFormat;
  return (a.flags & (kFlagMipName | kFlagTlutAny)) < (b.flags & (kFlagMipName | kFlagTlutAny));
}

// Everything an entry promises, checked against the file's size (a reader drops an entry that fails)
inline bool EntryValid(const Entry &e, std::uint64_t fileBytes, std::uint64_t dataOffset, std::uint64_t indexOffset) {
  if (e.encoding < kEtc2Rgb || e.encoding > kEtc2RgbaEac) return false;
  if (e.width == 0 || e.height == 0 || e.width > kMaxSide || e.height > kMaxSide) return false;
  if (e.levels == 0 || e.levels > kMaxLevels || e.levels > FullChain(e.width, e.height)) return false;
  if (e.origWidth == 0 || e.origHeight == 0 || e.origWidth > 1024 || e.origHeight > 1024) return false;
  if (e.gxFormat > 14) return false;
  if (e.dataBytes != ImageBytes(e.encoding, e.width, e.height, e.levels)) return false;
  if (e.dataOffset < dataOffset || e.dataOffset > indexOffset || e.dataBytes > indexOffset - e.dataOffset) return false;
  return e.dataOffset + e.dataBytes <= fileBytes;
}

}  // namespace c4tex
