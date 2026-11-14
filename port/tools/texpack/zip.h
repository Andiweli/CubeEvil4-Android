// ZIP archives for the texture tool (our own code, CC0): the central directory (also ZIP64, over 4 GB and 65535 files)
// and one entry at a time, stored or deflated, read without unpacking the archive.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zipr {

// the archive's bytes (a file on the PC, a file descriptor on Android); ReadAt may be called from several threads
class RandomAccess {
 public:
  virtual ~RandomAccess() = default;
  virtual bool ReadAt(std::uint64_t offset, void *data, std::size_t n) = 0;
  virtual std::uint64_t Size() = 0;
};

// a file on disk, read under a lock (PC)
std::shared_ptr<RandomAccess> OpenFile(const std::string &path, std::string &error);

struct Entry {
  std::string name;  // as stored ('/' between folders); a folder ends with '/'
  std::uint64_t compressed = 0, bytes = 0, localOffset = 0;
  std::uint16_t method = 0, flags = 0;
  std::uint32_t crc = 0;
};

class Archive {
 public:
  bool Open(std::shared_ptr<RandomAccess> file, std::string &error);
  const std::vector<Entry> &Entries() const { return entries_; }
  // the entry's data (all of it; with limit only its first `limit` bytes), checked against its CRC when whole
  bool Read(const Entry &e, std::vector<std::uint8_t> &out, std::string &error, std::size_t limit = SIZE_MAX) const;

 private:
  std::shared_ptr<RandomAccess> file_;
  std::vector<Entry> entries_;
};

}  // namespace zipr
