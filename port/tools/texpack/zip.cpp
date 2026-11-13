// ZIP reader (see zip.h). After the PKWARE APPNOTE: end of central directory, ZIP64 locator and record, central
// directory with its ZIP64 extra field, local header; methods 0 (stored) and 8 (deflate).
#include "zip.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "c4tex.h"
#include "fileio.h"
#include "image.h"

namespace zipr {
namespace {

inline std::uint16_t Le16(const std::uint8_t *p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
inline std::uint32_t Le32(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 |
         static_cast<std::uint32_t>(p[3]) << 24;
}
inline std::uint64_t Le64(const std::uint8_t *p) { return Le32(p) | static_cast<std::uint64_t>(Le32(p + 4)) << 32; }

class FileAccess : public RandomAccess {
 public:
  explicit FileAccess(FILE *f) : f_(f), size_(fio::Size(f)) {}
  ~FileAccess() override { std::fclose(f_); }
  bool ReadAt(std::uint64_t offset, void *data, std::size_t n) override {
    std::lock_guard<std::mutex> lock(mu_);
    return fio::Seek(f_, offset) && (n == 0 || std::fread(data, 1, n, f_) == n);
  }
  std::uint64_t Size() override { return size_; }

 private:
  FILE *f_;
  std::uint64_t size_;
  std::mutex mu_;
};

}  // namespace

std::shared_ptr<RandomAccess> OpenFile(const std::string &path, std::string &error) {
  FILE *f = fio::Open(path, "rb");
  if (!f) {
    error = "cannot open the file";
    return nullptr;
  }
  return std::make_shared<FileAccess>(f);
}

bool Archive::Open(std::shared_ptr<RandomAccess> file, std::string &error) {
  file_ = std::move(file);
  entries_.clear();
  const std::uint64_t size = file_->Size();
  if (size < 22) {
    error = "not a ZIP file";
    return false;
  }
  // the end of central directory record: within the last 64 KB + 22 bytes (its comment)
  const std::size_t tail = static_cast<std::size_t>(std::min<std::uint64_t>(size, 65557));
  std::vector<std::uint8_t> t(tail);
  if (!file_->ReadAt(size - tail, t.data(), tail)) {
    error = "cannot read the file";
    return false;
  }
  std::size_t eocd = SIZE_MAX;
  for (std::size_t i = tail - 22 + 1; i-- > 0;)
    if (Le32(&t[i]) == 0x06054b50) {
      eocd = i;
      break;
    }
  if (eocd == SIZE_MAX) {
    error = "not a ZIP file (no central directory)";
    return false;
  }
  std::uint64_t count = Le16(&t[eocd + 10]), cdBytes = Le32(&t[eocd + 12]), cdOffset = Le32(&t[eocd + 16]);
  // ZIP64: the locator right before it points to the ZIP64 end of central directory record
  if (eocd >= 20 && Le32(&t[eocd - 20]) == 0x07064b50) {
    const std::uint64_t recordAt = Le64(&t[eocd - 20 + 8]);
    std::uint8_t r[56];
    if (recordAt + 56 > size || !file_->ReadAt(recordAt, r, 56) || Le32(r) != 0x06064b50) {
      error = "damaged ZIP64 directory";
      return false;
    }
    count = Le64(r + 32);
    cdBytes = Le64(r + 40);
    cdOffset = Le64(r + 48);
  }
  if (cdOffset > size || cdBytes > size - cdOffset || cdBytes > (1ull << 31) || count > (1u << 24)) {
    error = "damaged ZIP directory";
    return false;
  }
  std::vector<std::uint8_t> cd(static_cast<std::size_t>(cdBytes));
  if (!file_->ReadAt(cdOffset, cd.data(), cd.size())) {
    error = "cannot read the ZIP directory";
    return false;
  }
  std::size_t at = 0;
  for (std::uint64_t i = 0; i < count; i++) {
    if (at + 46 > cd.size() || Le32(&cd[at]) != 0x02014b50) {
      error = "damaged ZIP directory";
      return false;
    }
    const std::uint8_t *h = &cd[at];
    Entry e;
    e.flags = Le16(h + 8);
    e.method = Le16(h + 10);
    e.crc = Le32(h + 16);
    e.compressed = Le32(h + 20);
    e.bytes = Le32(h + 24);
    const std::size_t nameLen = Le16(h + 28), extraLen = Le16(h + 30), commentLen = Le16(h + 32);
    e.localOffset = Le32(h + 42);
    if (at + 46 + nameLen + extraLen + commentLen > cd.size()) {
      error = "damaged ZIP directory";
      return false;
    }
    e.name.assign(reinterpret_cast<const char *>(h + 46), nameLen);
    std::replace(e.name.begin(), e.name.end(), '\\', '/');
    // ZIP64 extra field: the 64-bit values of the fields set to 0xFFFFFFFF, in this order
    const std::uint8_t *x = h + 46 + nameLen, *xend = x + extraLen;
    while (x + 4 <= xend) {
      const std::uint16_t id = Le16(x), len = Le16(x + 2);
      if (x + 4 + len > xend) break;
      if (id == 0x0001) {
        const std::uint8_t *v = x + 4, *vend = v + len;
        if (e.bytes == 0xFFFFFFFFu && v + 8 <= vend) {
          e.bytes = Le64(v);
          v += 8;
        }
        if (e.compressed == 0xFFFFFFFFu && v + 8 <= vend) {
          e.compressed = Le64(v);
          v += 8;
        }
        if (e.localOffset == 0xFFFFFFFFu && v + 8 <= vend) e.localOffset = Le64(v);
      }
      x += 4 + len;
    }
    entries_.push_back(std::move(e));
    at += 46 + nameLen + extraLen + commentLen;
  }
  return true;
}

bool Archive::Read(const Entry &e, std::vector<std::uint8_t> &out, std::string &error, std::size_t limit) const {
  if (e.flags & 1) {
    error = "the ZIP entry is encrypted";
    return false;
  }
  if (e.method != 0 && e.method != 8) {
    error = "the ZIP entry is packed with method " + std::to_string(e.method) + " (only stored and Deflate are supported)";
    return false;
  }
  if (e.bytes > (1ull << 31) || e.compressed > (1ull << 31)) {
    error = "the ZIP entry is too large";
    return false;
  }
  std::uint8_t lh[30];
  if (!file_->ReadAt(e.localOffset, lh, 30) || Le32(lh) != 0x04034b50) {
    error = "damaged ZIP entry";
    return false;
  }
  const std::uint64_t dataAt = e.localOffset + 30 + Le16(lh + 26) + Le16(lh + 28);
  const bool partial = limit < e.bytes;
  const std::size_t want = static_cast<std::size_t>(partial ? limit : e.bytes);
  if (e.method == 0) {
    out.resize(want);
    if (!file_->ReadAt(dataAt, out.data(), want)) {
      error = "cannot read the ZIP entry";
      return false;
    }
  } else {
    // a part of a deflated entry: what is read of the packed data is a guess, more is read when it was too little
    std::size_t take = static_cast<std::size_t>(e.compressed);
    if (partial) take = std::min<std::size_t>(take, std::max<std::size_t>(want + 4096, 65536));
    for (;;) {
      std::vector<std::uint8_t> packed(take);
      if (!file_->ReadAt(dataAt, packed.data(), take)) {
        error = "cannot read the ZIP entry";
        return false;
      }
      out.assign(want, 0);
      std::string why;
      if (img::InflateRaw(packed.data(), packed.size(), out, why, partial)) break;
      if (!partial || take >= e.compressed) {
        error = "damaged ZIP entry (" + why + ")";
        return false;
      }
      take = static_cast<std::size_t>(std::min<std::uint64_t>(e.compressed, static_cast<std::uint64_t>(take) * 4));
    }
  }
  if (!partial && c4tex::Crc32(out.data(), out.size()) != e.crc) {
    error = "damaged ZIP entry (checksum)";
    return false;
  }
  return true;
}

}  // namespace zipr
