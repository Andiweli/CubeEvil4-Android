// HD texture packs of the player (port/docs/textur-lader-plan.md): the .c4tex file the PC tool makes
// (port/tools/texpack, layout c4tex.h), its index in memory, and Dolphin's texture key that finds a replacement.
// The file is only read (pread); the texels go to the GPU as they are (ETC2 / EAC).
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

// a game texture no pack replaces: the menus' words keep the game's own picture (tex_pack_words.h, tex_pack.cpp says
// why)
bool KeepsGameTexture(const TexKey &key);

// The HD pack's groups the player can keep original (1.0.0, decision of the project owner, 17.11.2026; the Mods page's
// "Costumes" and "Weapons"): a game texture's groups (tex_pack_groups.h), 0 for any other texture
enum TextureGroup : unsigned { kGroupCostumes = 1, kGroupWeapons = 2 };
unsigned TextureGroups(const TexKey &key);
// gx_gl_check only: one more texture of a group, as if tex_pack_groups.h listed it
void AddTestGroupTexture(std::uint64_t tex, unsigned width, unsigned height, unsigned format, unsigned groups);
// gx_gl_check only: one more word, as if tex_pack_words.h listed it (KeepsGameTexture and MaybeMenuWord see it)
void AddTestMenuWord(std::uint64_t tex, unsigned width, unsigned height, unsigned format);
bool IsTestMenuWordSize(unsigned format, unsigned width, unsigned height);

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

  // the replacement of a game texture: the entry with the same "_m" first, else the other one; nullptr: none (also
  // for a texture KeepsGameTexture keeps)
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

// T4 (0.99.18, port/docs/textur-lader-plan.md "Nachladen ohne Ruckeln"): the replacements read on a thread of their
// own, so the render thread never waits for the pack file. GxCore::UploadHd asks for an entry (Add) and shows the
// game's own texture meanwhile; at the start of a chunk GxCore::ApplyHd takes the finished reads (Take) and puts them
// on the GPU in place of the game's. At most `maxReady` bytes wait read and not yet taken (the thread waits then):
// the memory a busy render thread could otherwise pile up on a weak device. The thread runs below the game and the
// renderer.
class TexLoader {
 public:
  struct Job {
    std::uint64_t key = 0, ticket = 0;  // the game texture's key, GxCore's ticket for this request
    c4tex::Entry entry{};
    unsigned first = 0;                 // the first level read (Sharpness: Half)
  };
  struct Done {
    Job job;
    std::vector<std::uint8_t> data;
    bool ok = false;
    std::uint64_t readUs = 0;  // the read's time on the loader thread
  };
  TexLoader(std::shared_ptr<const TexPack> pack, std::size_t maxReady);
  ~TexLoader();
  TexLoader(const TexLoader &) = delete;
  TexLoader &operator=(const TexLoader &) = delete;

  void Add(const Job &job);
  // the oldest finished read; false: none yet
  bool Take(Done &out);
  // queued and finished reads go (a read under way is dropped when it ends)
  void Cancel();
  // requests not yet taken (queued, being read, finished) and the bytes finished
  std::size_t Waiting() const;
  std::size_t ReadyBytes() const;

 private:
  void Loop();
  std::shared_ptr<const TexPack> pack_;
  std::size_t maxReady_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::deque<Job> jobs_;
  std::deque<Done> ready_;
  std::size_t readyBytes_ = 0;
  std::uint64_t epoch_ = 0;
  bool reading_ = false, stop_ = false;
  std::thread thread_;
};

}  // namespace re4gx
