// HD texture packs (see tex_pack.h).
#include "tex_pack.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

namespace re4gx {
namespace {

constexpr std::uint64_t kP1 = 0x9E3779B185EBCA87ull, kP2 = 0xC2B2AE3D27D4EB4Full, kP3 = 0x165667B19E3779F9ull,
                        kP4 = 0x85EBCA77C2B2AE63ull, kP5 = 0x27D4EB2F165667C5ull;

inline std::uint64_t Rotl(std::uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }

inline std::uint64_t Read64(const std::uint8_t *p) {
  std::uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
  return v;
}

inline std::uint32_t Read32(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

inline std::uint64_t Round(std::uint64_t acc, std::uint64_t input) {
  acc += input * kP2;
  acc = Rotl(acc, 31);
  return acc * kP1;
}

inline std::uint64_t Merge(std::uint64_t acc, std::uint64_t v) {
  acc ^= Round(0, v);
  return acc * kP1 + kP4;
}

bool PreadAll(int fd, void *buf, std::size_t n, std::uint64_t offset) {
  std::uint8_t *p = static_cast<std::uint8_t *>(buf);
  while (n) {
#if defined(__ANDROID__) || defined(__linux__)
    const ssize_t got = pread64(fd, p, n, static_cast<off64_t>(offset));
#else
    const ssize_t got = pread(fd, p, n, static_cast<off_t>(offset));
#endif
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) return false;
    p += got;
    n -= static_cast<std::size_t>(got);
    offset += static_cast<std::uint64_t>(got);
  }
  return true;
}

}  // namespace

std::uint64_t Xxh64(const void *data, std::size_t n, std::uint64_t seed) {
  const std::uint8_t *p = static_cast<const std::uint8_t *>(data);
  const std::uint8_t *end = p + n;
  std::uint64_t h;
  if (n >= 32) {
    std::uint64_t v1 = seed + kP1 + kP2, v2 = seed + kP2, v3 = seed, v4 = seed - kP1;
    const std::uint8_t *limit = end - 32;
    do {
      v1 = Round(v1, Read64(p));
      v2 = Round(v2, Read64(p + 8));
      v3 = Round(v3, Read64(p + 16));
      v4 = Round(v4, Read64(p + 24));
      p += 32;
    } while (p <= limit);
    h = Rotl(v1, 1) + Rotl(v2, 7) + Rotl(v3, 12) + Rotl(v4, 18);
    h = Merge(h, v1);
    h = Merge(h, v2);
    h = Merge(h, v3);
    h = Merge(h, v4);
  } else {
    h = seed + kP5;
  }
  h += static_cast<std::uint64_t>(n);
  while (p + 8 <= end) {
    h ^= Round(0, Read64(p));
    h = Rotl(h, 27) * kP1 + kP4;
    p += 8;
  }
  if (p + 4 <= end) {
    h ^= static_cast<std::uint64_t>(Read32(p)) * kP1;
    h = Rotl(h, 23) * kP2 + kP3;
    p += 4;
  }
  while (p < end) {
    h ^= static_cast<std::uint64_t>(*p) * kP5;
    h = Rotl(h, 11) * kP1;
    p++;
  }
  h ^= h >> 33;
  h *= kP2;
  h ^= h >> 29;
  h *= kP3;
  h ^= h >> 32;
  return h;
}

TexKey DolphinKey(unsigned format, unsigned width, unsigned height, bool mip, const std::uint8_t *level0,
                  std::size_t level0Bytes, const std::uint8_t *palette, std::size_t paletteBytes) {
  TexKey k;
  k.width = width;
  k.height = height;
  k.format = format;
  k.mip = mip;
  k.tex = Xxh64(level0, level0Bytes, 0);
  if (format == 8 || format == 9 || format == 10) {
    // the used range of the palette, as Dolphin hashes it (C14X2: its own reading of the first byte)
    unsigned lo = ~0u, hi = 0;
    if (format == 8) {
      for (std::size_t i = 0; i < level0Bytes; i++) {
        const unsigned a = level0[i] & 15, b = level0[i] >> 4;
        lo = std::min(lo, std::min(a, b));
        hi = std::max(hi, std::max(a, b));
      }
    } else if (format == 9) {
      for (std::size_t i = 0; i < level0Bytes; i++) {
        lo = std::min<unsigned>(lo, level0[i]);
        hi = std::max<unsigned>(hi, level0[i]);
      }
    } else {
      for (std::size_t i = 0; i + 1 < level0Bytes; i += 2) {
        const unsigned v = (static_cast<unsigned>(level0[i]) << 8) & 0x3FFF;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
    }
    if (lo <= hi && palette) {
      const std::size_t from = static_cast<std::size_t>(lo) * 2, to = (static_cast<std::size_t>(hi) + 1) * 2;
      if (to <= paletteBytes) {
        k.tlut = Xxh64(palette + from, to - from, 0);
        k.hasTlut = true;
      }
    }
  }
  return k;
}

TexPack::~TexPack() { Close(); }

void TexPack::Close() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  entries_.clear();
  info_.clear();
  header_ = c4tex::Header{};
  dropped_ = 0;
}

bool TexPack::Open(const std::string &path, std::string *error) {
  Close();
  auto fail = [&](const std::string &why) {
    if (error) *error = why;
    Close();
    return false;
  };
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_LARGEFILE);
  if (fd < 0) return fail(std::string("cannot open: ") + std::strerror(errno));
  fd_ = fd;
#if defined(__ANDROID__) || defined(__linux__)
  struct stat64 st;
  if (fstat64(fd, &st) != 0) return fail("cannot read the file's size");
#else
  struct stat st;
  if (fstat(fd, &st) != 0) return fail("cannot read the file's size");
#endif
  const std::uint64_t size = static_cast<std::uint64_t>(st.st_size);
  if (size < c4tex::kHeaderBytes || !PreadAll(fd, &header_, sizeof(header_), 0)) return fail("too short");
  if (std::memcmp(header_.magic, c4tex::kMagic, 8) != 0) return fail("not a CubeEvil4 texture pack");
  if (header_.version != c4tex::kVersion) return fail("a pack of another version (" + std::to_string(header_.version) + ")");
  if (header_.headerCrc != c4tex::HeaderCrc(header_)) return fail("the header is damaged");
  if (header_.fileBytes != size) return fail("incomplete (the copy was cut short?)");
  if (header_.entryBytes != c4tex::kEntryBytes || header_.entryCount > c4tex::kMaxEntries ||
      header_.indexOffset > size || static_cast<std::uint64_t>(header_.entryCount) * c4tex::kEntryBytes > size - header_.indexOffset ||
      header_.dataOffset > header_.indexOffset)
    return fail("the index lies outside the file");
  std::vector<c4tex::Entry> all(header_.entryCount);
  if (!all.empty() && !PreadAll(fd, all.data(), all.size() * sizeof(c4tex::Entry), header_.indexOffset))
    return fail("cannot read the index");
  if (c4tex::Crc32(all.data(), all.size() * sizeof(c4tex::Entry)) != header_.indexCrc) return fail("the index is damaged");
  entries_.reserve(all.size());
  for (const c4tex::Entry &e : all) {
    if (c4tex::EntryValid(e, size, header_.dataOffset, header_.indexOffset))
      entries_.push_back(e);
    else
      dropped_++;
  }
  // sorted as the tool wrote it; sorted again in case, the lookup needs it
  if (!std::is_sorted(entries_.begin(), entries_.end(), c4tex::EntryLess))
    std::sort(entries_.begin(), entries_.end(), c4tex::EntryLess);
  if (header_.infoBytes && header_.infoBytes <= c4tex::kMaxInfoBytes && header_.infoOffset + header_.infoBytes <= size) {
    std::string text(header_.infoBytes, '\0');
    if (PreadAll(fd, text.data(), text.size(), header_.infoOffset)) {
      std::size_t at = 0;
      while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(at, nl - at);
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) {
          std::string v = line.substr(eq + 1);
          while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
          info_[line.substr(0, eq)] = v;
        }
        at = nl + 1;
      }
    }
  }
  path_ = path;
  return true;
}

const c4tex::Entry *TexPack::Find(const TexKey &key) const {
  if (entries_.empty()) return nullptr;
  auto lo = std::lower_bound(entries_.begin(), entries_.end(), key.tex,
                             [](const c4tex::Entry &e, std::uint64_t h) { return e.texHash < h; });
  const c4tex::Entry *same = nullptr, *other = nullptr;
  for (auto it = lo; it != entries_.end() && it->texHash == key.tex; ++it) {
    const c4tex::Entry &e = *it;
    if (e.origWidth != key.width || e.origHeight != key.height || e.gxFormat != key.format) continue;
    const bool tlutOk = !(e.flags & c4tex::kFlagTlut) ? !key.hasTlut
                        : (e.flags & c4tex::kFlagTlutAny) ? true
                                                          : key.hasTlut && e.tlutHash == key.tlut;
    if (!tlutOk) continue;
    const bool mip = (e.flags & c4tex::kFlagMipName) != 0;
    if (mip == key.mip) {
      if (!same || !(e.flags & c4tex::kFlagTlutAny)) same = &e;  // an exact palette before "$"
    } else if (!other) {
      other = &e;
    }
  }
  return same ? same : other;
}

std::size_t LevelsBytes(const c4tex::Entry &e, unsigned firstLevel, unsigned *width, unsigned *height) {
  unsigned w = e.width, h = e.height;
  std::size_t skip = 0;
  for (unsigned l = 0; l < firstLevel && l + 1 < e.levels; l++) {
    skip += c4tex::LevelBytes(e.encoding, w, h);
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }
  if (width) *width = w;
  if (height) *height = h;
  return e.dataBytes - skip;
}

bool TexPack::Read(const c4tex::Entry &e, unsigned firstLevel, std::vector<std::uint8_t> &out) const {
  if (fd_ < 0) return false;
  const std::size_t bytes = LevelsBytes(e, firstLevel);
  out.resize(bytes);
  return PreadAll(fd_, out.data(), bytes, e.dataOffset + (e.dataBytes - bytes));
}

std::string TexPack::Info(const std::string &key) const {
  auto it = info_.find(key);
  return it == info_.end() ? std::string() : it->second;
}

}  // namespace re4gx
