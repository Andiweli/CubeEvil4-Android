// HD texture packs of the player (port/docs/textur-lader-plan.md): the .c4tex file the PC tool makes
// (port/tools/texpack, layout c4tex.h), its index in memory, and Dolphin's texture key that finds a replacement.
// The file is only read (pread); the texels go to the GPU as they are (ETC2 / EAC).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "c4tex.h"

namespace re4gx {

// XXH64 (Yann Collet's algorithm, our own code), as Dolphin names its textures with it
std::uint64_t Xxh64(const void *data, std::size_t n, std::uint64_t seed);

// Dolphin's name of a game texture: the first level's bytes in whole tiles, and for a texture with a palette the
// palette's used range (TMEM as the game loaded it)
struct TexKey {
  std::uint64_t tex = 0, tlut = 0;
  bool hasTlut = false;
  unsigned width = 0, height = 0, format = 0;
  bool mip = false;  // the game binds it with mip levels ("_m")
};

// level0: the first level (GxTextureLevelBytes bytes); palette: TMEM at the texture's palette (CI formats), its bytes
TexKey DolphinKey(unsigned format, unsigned width, unsigned height, bool mip, const std::uint8_t *level0,
                  std::size_t level0Bytes, const std::uint8_t *palette, std::size_t paletteBytes);

class TexPack {
 public:
  TexPack() = default;
  ~TexPack();
  TexPack(const TexPack &) = delete;
  TexPack &operator=(const TexPack &) = delete;

  // checks the header, the index's checksum and every entry; false with a reason for the player's log
  bool Open(const std::string &path, std::string *error);
  void Close();
  bool IsOpen() const { return fd_ >= 0; }

  // the replacement of a game texture: the entry with the same "_m" first, else the other one; nullptr: none
  const c4tex::Entry *Find(const TexKey &key) const;
  // the entry's texels from `firstLevel` on (Sharpness Half: 1); false when the file cannot be read
  bool Read(const c4tex::Entry &e, unsigned firstLevel, std::vector<std::uint8_t> &out) const;

  std::size_t Count() const { return entries_.size(); }
  std::uint64_t FileBytes() const { return header_.fileBytes; }
  unsigned MaxSide() const { return header_.maxSide; }
  std::string Info(const std::string &key) const;  // a line of the info text ("name", "created", ...)
  const std::string &Path() const { return path_; }
  std::size_t Dropped() const { return dropped_; }  // entries that failed the checks

 private:
  int fd_ = -1;
  std::string path_;
  c4tex::Header header_{};
  std::vector<c4tex::Entry> entries_;  // sorted (c4tex::EntryLess)
  std::map<std::string, std::string> info_;
  std::size_t dropped_ = 0;
};

// bytes of an entry's levels from `firstLevel` on, and the first of them's size
std::size_t LevelsBytes(const c4tex::Entry &e, unsigned firstLevel, unsigned *width = nullptr, unsigned *height = nullptr);

}  // namespace re4gx
