// RVZ of a GameCube disc to the plain disc image (disc_rvz.h). The format as Dolphin writes it
// (Source/Core/DiscIO/WIABlob.h, docs/WiaAndRvz.md): big-endian headers, a table of raw data
// entries and a table of groups (both compressed like the groups), each group one chunk of the disc,
// compressed with zstd or stored, and "packed": runs of data and runs of the disc's junk padding,
// which Nintendo's mastering filled with a lagged Fibonacci generator (only its seed is stored).
#define _FILE_OFFSET_BITS 64

#include "disc_rvz.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "third_party/zstd/zstd.h"

namespace re4 {
namespace {

constexpr std::uint64_t kBlock = 0x8000;  // Dolphin's VolumeWii::BLOCK_TOTAL_SIZE: groups start on it
constexpr std::uint32_t kNone = 0, kZstd = 5;
constexpr std::uint32_t kHeader1 = 0x48, kHeader2 = 0xDC;

std::uint32_t Be32(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
         static_cast<std::uint32_t>(p[2]) << 8 | p[3];
}
std::uint64_t Be64(const std::uint8_t *p) { return static_cast<std::uint64_t>(Be32(p)) << 32 | Be32(p + 4); }

struct Fd {
  int fd = -1;
  ~Fd() {
    if (fd >= 0)
      close(fd);
  }
};

bool ReadAt(int fd, std::uint64_t at, void *out, std::size_t n) {
  auto *p = static_cast<std::uint8_t *>(out);
  while (n) {
    const ssize_t r = pread(fd, p, n, static_cast<off_t>(at));
    if (r <= 0)
      return false;
    p += r;
    at += static_cast<std::uint64_t>(r);
    n -= static_cast<std::size_t>(r);
  }
  return true;
}

bool WriteAt(int fd, std::uint64_t at, const void *data, std::size_t n) {
  auto *p = static_cast<const std::uint8_t *>(data);
  while (n) {
    const ssize_t r = pwrite(fd, p, n, static_cast<off_t>(at));
    if (r <= 0)
      return false;
    p += r;
    at += static_cast<std::uint64_t>(r);
    n -= static_cast<std::size_t>(r);
  }
  return true;
}

// The junk of GameCube and Wii discs (Dolphin's LaggedFibonacciGenerator): 17 seed words, a lag of
// 521/32; the bytes come out big-endian with bits 18-23 moved down by 2.
class Lfg {
public:
  static constexpr std::size_t kK = 521, kJ = 32, kSeedWords = 17, kBytes = kK * 4;
  void Seed(const std::uint8_t *seed) {
    pos_ = 0;
    for (std::size_t i = 0; i < kSeedWords; i++)
      buf_[i] = Be32(seed + i * 4);
    for (std::size_t i = kSeedWords; i < kK; i++)
      buf_[i] = (buf_[i - 17] << 23) ^ (buf_[i - 16] >> 9) ^ buf_[i - 1];
    for (std::uint32_t &x : buf_)
      x = (x & 0xFF00FFFFu) | ((x >> 2) & 0x00FF0000u);
    for (int i = 0; i < 4; i++)
      Step();
  }
  void Forward(std::size_t bytes) {
    pos_ += bytes;
    while (pos_ >= kBytes) {
      Step();
      pos_ -= kBytes;
    }
  }
  void Bytes(std::uint8_t *out, std::size_t n) {
    while (n--) {
      *out++ = static_cast<std::uint8_t>(buf_[pos_ / 4] >> (24 - 8 * (pos_ % 4)));
      if (++pos_ == kBytes) {
        Step();
        pos_ = 0;
      }
    }
  }

private:
  void Step() {
    for (std::size_t i = 0; i < kJ; i++)
      buf_[i] ^= buf_[i + kK - kJ];
    for (std::size_t i = kJ; i < kK; i++)
      buf_[i] ^= buf_[i - kJ];
  }
  std::uint32_t buf_[kK] = {};
  std::size_t pos_ = 0;
};

// A packed group (rvz_packed_size != 0): u32 size, bit 31 junk; junk: 68 bytes of seed, else the data.
bool Unpack(const std::uint8_t *in, std::size_t n, std::uint64_t discOffset, std::uint8_t *out, std::size_t outN) {
  std::size_t i = 0, o = 0;
  Lfg lfg;
  while (o < outN) {
    if (i + 4 > n)
      return false;
    std::uint32_t size = Be32(in + i);
    i += 4;
    const bool junk = size & 0x80000000u;
    size &= 0x7FFFFFFFu;
    if (size > outN - o)
      return false;
    if (junk) {
      if (i + Lfg::kSeedWords * 4 > n)
        return false;
      lfg.Seed(in + i);
      i += Lfg::kSeedWords * 4;
      lfg.Forward(static_cast<std::size_t>(discOffset % kBlock));
      lfg.Bytes(out + o, size);
    } else {
      if (size > n - i)
        return false;
      std::memcpy(out + o, in + i, size);
      i += size;
    }
    o += size;
    discOffset += size;
  }
  return true;
}

struct Rvz {
  Fd file;
  RvzInfo info;
  std::uint32_t compression = 0, chunkSize = 0;
  std::uint8_t discHeader[0x80] = {};
  std::uint32_t rawCount = 0, rawSize = 0, groupCount = 0, groupSize = 0;
  std::uint64_t rawOffset = 0, groupOffset = 0;
};

// Reads `size` bytes at `at` and makes `expected` bytes of them (decompressed with zstd).
bool Inflate(const Rvz &r, std::uint64_t at, std::uint32_t size, bool compressed, std::vector<std::uint8_t> *out,
             std::size_t expected, std::vector<std::uint8_t> *scratch) {
  if (!compressed) {
    if (size != expected)
      return false;
    out->resize(expected);
    return ReadAt(r.file.fd, at, out->data(), size);
  }
  scratch->resize(size);
  if (!ReadAt(r.file.fd, at, scratch->data(), size))
    return false;
  out->resize(expected);
  const std::size_t got = ZSTD_decompress(out->data(), expected, scratch->data(), size);
  return !ZSTD_isError(got) && got == expected;
}

bool Open(const char *path, Rvz *r, std::string *error) {
  r->file.fd = open(path, O_RDONLY);
  if (r->file.fd < 0) {
    *error = "Could not open the RVZ file";
    return false;
  }
  struct stat st {};
  if (fstat(r->file.fd, &st) != 0) {
    *error = "Could not open the RVZ file";
    return false;
  }
  r->info.fileSize = static_cast<std::uint64_t>(st.st_size);
  std::uint8_t h1[kHeader1];
  if (!ReadAt(r->file.fd, 0, h1, sizeof(h1)) || std::memcmp(h1, "RVZ\x01", 4) != 0) {
    *error = "The file is not an RVZ disc image";
    return false;
  }
  const std::uint32_t h2Size = Be32(h1 + 0x0C);
  if (h2Size < kHeader2 || h2Size > 0x10000) {
    *error = "The RVZ file is damaged (header)";
    return false;
  }
  std::vector<std::uint8_t> h2(h2Size);
  if (!ReadAt(r->file.fd, kHeader1, h2.data(), h2.size())) {
    *error = "The RVZ file is damaged (header)";
    return false;
  }
  r->info.discSize = Be64(h1 + 0x24);
  const std::uint32_t discType = Be32(h2.data());
  r->compression = Be32(h2.data() + 0x04);
  r->chunkSize = Be32(h2.data() + 0x0C);
  std::memcpy(r->discHeader, h2.data() + 0x10, sizeof(r->discHeader));
  r->rawCount = Be32(h2.data() + 0xB4);
  r->rawOffset = Be64(h2.data() + 0xB8);
  r->rawSize = Be32(h2.data() + 0xC0);
  r->groupCount = Be32(h2.data() + 0xC4);
  r->groupOffset = Be64(h2.data() + 0xC8);
  r->groupSize = Be32(h2.data() + 0xD0);
  if (discType != 1) {
    *error = "The RVZ file holds a Wii disc, not a GameCube disc";
    return false;
  }
  if (r->compression != kNone && r->compression != kZstd) {
    *error = "This RVZ file is compressed with bzip2 or LZMA, which the app cannot unpack. Convert it again in "
             "Dolphin with Zstandard, or import an ISO/GCM file";
    return false;
  }
  if (r->chunkSize < kBlock || r->chunkSize > (64u << 20) || r->chunkSize % kBlock || r->info.discSize < 0x80 ||
      r->info.discSize > (8ull << 30) || r->rawCount > 0x10000 || r->groupCount > (1u << 24)) {
    *error = "The RVZ file is damaged (header)";
    return false;
  }
  return true;
}

} // namespace

bool IsRvz(const char *path) {
  Fd f;
  f.fd = open(path, O_RDONLY);
  std::uint8_t magic[4];
  return f.fd >= 0 && ReadAt(f.fd, 0, magic, 4) && std::memcmp(magic, "RVZ\x01", 4) == 0;
}

bool RvzInfoOf(const char *path, RvzInfo *info, std::string *error) {
  Rvz r;
  if (!Open(path, &r, error))
    return false;
  *info = r.info;
  return true;
}

bool RvzToIso(const char *in, const char *out, const std::function<bool(std::uint64_t, std::uint64_t)> &progress,
              std::string *error) {
  Rvz r;
  if (!Open(in, &r, error))
    return false;
  const bool zstd = r.compression == kZstd;
  std::vector<std::uint8_t> scratch, rawTable, groupTable;
  if (!Inflate(r, r.rawOffset, r.rawSize, zstd, &rawTable, std::size_t(r.rawCount) * 24, &scratch) ||
      !Inflate(r, r.groupOffset, r.groupSize, zstd, &groupTable, std::size_t(r.groupCount) * 12, &scratch)) {
    *error = "The RVZ file is damaged (tables)";
    return false;
  }
  Fd o;
  o.fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (o.fd < 0 || ftruncate(o.fd, static_cast<off_t>(r.info.discSize)) != 0) {
    *error = "Could not write the unpacked disc image";
    return false;
  }
  std::uint64_t total = 0, done = 0;
  for (std::uint32_t e = 0; e < r.rawCount; e++) {
    const std::uint8_t *p = rawTable.data() + std::size_t(e) * 24;
    total += Be64(p + 8);
  }
  std::vector<std::uint8_t> group, data;
  for (std::uint32_t e = 0; e < r.rawCount; e++) {
    const std::uint8_t *p = rawTable.data() + std::size_t(e) * 24;
    const std::uint64_t offset = Be64(p), size = Be64(p + 8);
    const std::uint32_t first = Be32(p + 16), count = Be32(p + 20);
    const std::uint64_t start = offset - offset % kBlock, end = offset + size;
    if (end < offset || end > r.info.discSize || first > r.groupCount || count > r.groupCount - first) {
      *error = "The RVZ file is damaged (data entries)";
      return false;
    }
    for (std::uint32_t g = 0; g < count; g++) {
      const std::uint64_t from = start + std::uint64_t(g) * r.chunkSize;
      if (from >= end)
        break;
      const std::size_t length = static_cast<std::size_t>(std::min<std::uint64_t>(r.chunkSize, end - from));
      const std::uint8_t *q = groupTable.data() + std::size_t(first + g) * 12;
      const std::uint64_t at = std::uint64_t(Be32(q)) << 2;
      const std::uint32_t field = Be32(q + 4), packed = Be32(q + 8);
      const std::uint32_t stored = field & 0x7FFFFFFFu;
      bool ok = true;
      if (stored == 0) {
        group.assign(length, 0);
      } else if (packed) {
        ok = Inflate(r, at, stored, field & 0x80000000u, &data, packed, &scratch);
        group.resize(length);
        ok = ok && Unpack(data.data(), data.size(), from, group.data(), length);
      } else {
        ok = Inflate(r, at, stored, field & 0x80000000u, &group, length, &scratch);
      }
      if (!ok || at + stored > r.info.fileSize) {
        *error = "The RVZ file is damaged (group " + std::to_string(first + g) + ")";
        return false;
      }
      // the part before the entry's offset belongs to what lies before it (the disc header)
      const std::uint64_t keep = from < offset ? offset - from : 0;
      if (keep < length && !WriteAt(o.fd, from + keep, group.data() + keep, length - keep)) {
        *error = "Could not write the unpacked disc image (storage full?)";
        return false;
      }
      done += length - std::min<std::uint64_t>(keep, length);
      if (progress && !progress(done, total)) {
        *error = "Import canceled; existing data has been preserved.";
        return false;
      }
    }
  }
  if (!WriteAt(o.fd, 0, r.discHeader, std::min<std::uint64_t>(sizeof(r.discHeader), r.info.discSize)) ||
      fsync(o.fd) != 0) {
    *error = "Could not write the unpacked disc image (storage full?)";
    return false;
  }
  return true;
}

} // namespace re4
