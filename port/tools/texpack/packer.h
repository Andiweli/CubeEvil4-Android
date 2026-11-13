// The CubeEvil4 Texture Packer's core: a Dolphin texture pack (its files from a folder on the PC, a ZIP, an Android
// folder) to one .c4tex file. The PC tool (texpack.cpp) and the Android app (port/android-decomp/packer) are front ends
// around it, so both make the same file. Our own code (CC0); plans: port/docs/textur-lader-plan.md,
// port/docs/textur-packer-android-plan.md.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "c4tex.h"
#include "zip.h"

namespace pack {

extern const char *const kToolName;
extern const char *const kToolVersion;

std::string Lower(std::string s);
std::string Thousands(std::uint64_t v);
std::string Bytes(std::uint64_t b);
std::string Duration(double seconds);

// ---- Dolphin names: tex1_<W>x<H>[_m]_<hash>[_<tlut hash>|_$]_<fmt>[_mip<N>][_arb].(dds|png|jpg|jpeg) ----

struct Key {
  std::uint64_t tex = 0, tlut = 0;
  unsigned w = 0, h = 0, fmt = 0;
  bool hasTlut = false, tlutAny = false;
  bool mip = false;  // "_m": a texture can be there twice, with and without (Dolphin keeps them apart)
  bool operator<(const Key &o) const;
};

struct Name {
  Key key;
  bool mip = false, arb = false, mipFile = false;
  int kind = 0;  // 0 DDS, 1 PNG, 2 JPG: the same texture in several files, the lowest is used
};

bool ParseName(const std::string &fileName, Name &n);
const char *KindName(int kind);
std::string KeyName(const Key &k);
bool IsTextureFile(const std::string &fileName);

// ---- the pack's files ----

// one file of the pack, wherever it lies
class Reader {
 public:
  virtual ~Reader() = default;
  virtual bool ReadAll(std::vector<std::uint8_t> &out, std::string &error) = 0;
  // at least the file's first n bytes (all of a smaller one)
  virtual bool ReadHead(std::size_t n, std::vector<std::uint8_t> &out);
  // a plain file on disk (the image reader then reads a DDS only from its wanted level on); "" when not
  virtual std::string DiskPath() const { return std::string(); }
};

struct InputFile {
  std::string rel;  // from the pack's root, '/' between folders
  std::uint64_t bytes = 0;
  std::shared_ptr<Reader> reader;
};

// every file of a folder on disk (PC)
std::vector<InputFile> ListFolder(const std::string &root);
// every file of a ZIP archive, read from it when needed; a single top folder of all files is left out of their paths
// and becomes the pack's name (else the ZIP's name without .zip)
bool ListZip(std::shared_ptr<zipr::RandomAccess> access, const std::string &zipName, std::vector<InputFile> &files,
             std::string &packName, std::string &error);

struct Source {
  InputFile file;
  Name name;
};

struct Skipped {
  std::string path, reason;
};

struct Variant {
  std::string name;
  std::string prefix;  // its folder in the pack, ending with '/'
  bool sameAsMain = false;
  std::vector<Source> files;
};

struct Group {
  std::string name;
  std::vector<Variant> variants;  // one "add-on" variant when the group holds files directly
  bool addOn = false;
  int chosen = -1;  // -1: as in the main folder (an add-on: not added)
};

struct Pack {
  std::string name;              // the pack's folder (or ZIP) name
  std::vector<Source> files;     // the main images, in name order
  std::vector<Skipped> skipped;  // files that are no images of the pack
  std::vector<Skipped> duplicates;
  std::map<Key, const Source *> byKey;  // one per key, after the duplicates
  std::vector<Group> groups;
  std::uint64_t bytes = 0;
};

// files: every file with its path from the pack's root. A game-ID folder (G4BE08 ...) is the pack, as in Dolphin; then
// the other top folders, else Extra/Extras/Optional, hold groups of choices. `pack` must not move afterwards.
void Prepare(std::vector<InputFile> files, const std::string &name, Pack &pack);

// "group=variant" (an add-on: "group=on"); false: one matches nothing, it is in `bad`
bool Choose(Pack &pack, const std::vector<std::string> &specs, std::string &bad);

struct Settings {
  unsigned size = 512;  // 256, 512, 1024
  int effort = 1;
  unsigned threads = 0;                 // 0: every core
  std::uint64_t memoryBudget = 1ull << 30;
  int limit = 0;                        // tests: only the first N images
  std::string date;                     // "2026-11-13"
  std::string outName;                  // the file's name, for the report
};

// the images to convert after the choices (key order), and the choices as text
std::vector<const Source *> Todo(const Pack &pack, const Settings &settings, std::string &choices);

// the pack file's size at most (from the images' headers: their size after the rule, 8 bits a texel with soft alpha)
std::uint64_t Estimate(const std::vector<const Source *> &todo, unsigned size);
// the same for several sizes, reading each image's header once
std::vector<std::uint64_t> EstimateSizes(const std::vector<const Source *> &todo, const std::vector<unsigned> &sizes);

// where the pack file goes (a file on the PC, a file descriptor on Android)
class Sink {
 public:
  virtual ~Sink() = default;
  virtual bool Write(const void *data, std::size_t n) = 0;
  virtual bool WriteAt(std::uint64_t offset, const void *data, std::size_t n) = 0;
  virtual std::uint64_t Free() { return ~0ull; }  // free space where it is written
};

struct Progress {
  std::size_t done = 0, total = 0;
  std::uint64_t written = 0;
  double seconds = 0;
};

struct Outcome {
  bool ok = false, cancelled = false, diskFull = false;
  std::string error;
  std::size_t entries = 0, failed = 0, skipped = 0;
  std::size_t counts[4] = {};
  std::uint64_t fileBytes = 0, dataBytes = 0;
  double seconds = 0;
  unsigned threads = 0;
  std::string report;  // the text for the .txt next to the pack file
};

// converts `todo` into `sink` (the whole file: header, info, images, index); cancel stops it (the caller deletes the
// unfinished file)
Outcome Convert(const Pack &pack, const std::vector<const Source *> &todo, const std::string &choices,
                const Settings &settings, Sink &sink, const std::function<void(const Progress &)> &progress,
                const std::atomic<bool> *cancel);

}  // namespace pack
