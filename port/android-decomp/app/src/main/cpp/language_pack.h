// Language packs of the player (fan translations of the European discs; port/docs/sprachpakete.md).
//
// A pack is a set of replacement disc files (texts, subtitles, menu word pictures) a fan translation made for the
// European discs. CubeEvil4 brings none: the player imports one from a folder or a ZIP. The import keeps only the
// files that differ from the player's own imported discs, and only when they are text or menu files that change
// nothing else: rooms and cutscenes may change their message tables only, the menu files their messages, their
// pictures and their screen layouts. What it keeps is the overlay the core reads instead of the disc's files
// (port/whole/runtime/dvd.cpp, re4_language_pack):
//
//   <pack>/overlay.tsv      "V\t1", "L\t<game language>", then "F\t<disc 1|2>\t<path on the disc>\t<size>\t<file>"
//   <pack>/pack.properties  name, language, counts and sizes for the app's menu
//   <pack>/files/...        the kept files (one copy when both discs carry the same file)
//   <pack>/disc2/...        a file of disc 2 that differs from disc 1's of the same path
//
// The same code runs in the app (language_pack_jni.cpp) and in the PC tool (tools/langpack_tool.cpp).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace re4lang {

// Why an import stopped (Result::reason); the app shows a text of its own for each.
enum Reason {
  kOk = 0,
  kNoDiscs = 1,        // no imported discs
  kNotEuropean = 2,    // the imported discs are not the European ones (G4BP08)
  kProgramFile = 3,    // the pack changes a program file (main.dol, a .rel module, the disc's tables)
  kNotText = 4,        // the pack changes a game file that is not a text or menu file
  kMoreThanText = 5,   // a text or menu file whose other parts differ from the player's disc
  kNotOnDisc = 6,      // a game file the player's disc does not have (another release)
  kDamaged = 7,        // a file of the pack that does not read as what it should be
  kNoChanges = 8,      // nothing in the pack differs from the player's discs
  kNoSpace = 9,        // the device has no room for the overlay
  kReadFailed = 10,    // the pack could not be read
  kNotPack = 11,       // no files of the game at all
  kCancelled = 12,
  kTwoCopies = 13,     // two different files for the same file of a disc
  kUnsafePath = 14,    // a name with ".." in it
  kNoLanguage = 15,    // which language the pack replaces could not be told
  kBadZip = 16,        // the ZIP is damaged, encrypted or uses a method other than store/deflate
};

struct Result {
  int reason = kOk;
  std::string file;    // the pack's file the refusal is about
  std::string detail;  // English, for the log and the report
  int replaced = 0;    // disc files the overlay replaces (a file of both discs counts twice)
  int kept = 0;        // files kept in the pack's folder
  int identical = 0;   // files of the pack equal to the player's discs (dropped)
  int ignored = 0;     // files outside the game's folders (read-me files, pictures of the translators ...)
  uint64_t bytes = 0;  // size of the kept files
  int language = 0;    // the game language whose texts the pack replaces (2 English .. 6 Italian)
};

// One import. Use: begin, then for every file of the pack want() and, when it says kRead, add(); then finish().
class Importer {
 public:
  enum Want { kSkip = 0, kRead = 1, kStop = 2 };

  // gameRoot: the folder holding the imported discs (disc1, disc2 in the app; disc0, disc1 on the PC)
  // dest: the pack's folder; the import builds dest + ".work" and replaces dest at finish
  Importer(std::string gameRoot, std::string dest);
  ~Importer();

  bool begin(Result& r);
  // What to do with a file of the pack (its path inside the pack, '/' or '\\'): read it, skip it (not a game
  // file), or stop (r says why).
  Want want(const std::string& path, Result& r);
  bool add(const std::string& path, const std::vector<uint8_t>& data, Result& r);
  bool finish(const std::string& name, const std::string& source, Result& r);
  // Removes the work folder (an import that stopped or was cancelled).
  void abort();

 private:
  struct Disc;
  struct Target {
    int disc;          // 1 or 2
    std::string rel;   // the path on that disc, its own spelling
  };
  bool place(const std::string& path, int& disc, std::string& rel, bool& programName, Result& r);
  bool keep(const Target& t, const std::vector<uint8_t>& data, Result& r);

  std::string gameRoot_, dest_, work_;
  std::vector<Disc*> discs_;  // [0] disc 1, [1] disc 2 (null when not imported)
  std::map<std::string, std::string> seen_;  // "disc\trel" -> the pack's path of the file taken for it
  std::map<std::string, uint32_t> sizes_;    // "disc\trel" -> kept size
  std::map<std::string, std::string> files_; // "disc\trel" -> file in the pack's folder
  std::map<std::string, std::vector<uint8_t>> shared_;  // rel (lower case) -> bytes kept under files/
  int64_t slotScore_[7] = {};
  bool begun_ = false;
};

// ---- the formats (exposed for the PC tool's checks) ---------------------------------------------------------

using Bytes = std::vector<uint8_t>;
// Every part of `pack` that differs from `disc` is a message table: rooms (St*/r*.das, yz2 inside).
bool CheckRoom(const Bytes& disc, const Bytes& pack, std::string& why, int64_t* slots);
// Header, packets, names and every asset but the .mdt ones as on the disc: cutscenes (Evd/*.evd).
bool CheckEvent(const Bytes& disc, const Bytes& pack, std::string& why, int64_t* slots);
// Archives of etc/, op/ and ss/: message tables, the pictures of effect data, and (menus) screen layouts.
bool CheckArchive(const Bytes& disc, const Bytes& pack, bool layouts, std::string& why, int64_t* slots);
// A TPL picture file with as many pictures as the disc's.
bool CheckTpl(const Bytes& disc, const Bytes& pack, std::string& why);
// The PC tool's check of the room rule on a real room (the import cannot pack yz2): unpacks `room`, changes one byte
// of its first part tagged `tag` (SAT the collision, MDT the texts) and says what the room check says of it.
std::string SelfTestRoom(const Bytes& room, const char* tag);

// ---- ZIP (store and deflate, ZIP64) ---------------------------------------------------------------------------

struct ZipEntry {
  std::string name;
  uint64_t size = 0, packed = 0, local = 0;
  uint32_t crc = 0;
  uint16_t method = 0, flags = 0;
};
// The ZIP's directory; read(at, buf, n) reads n bytes of the ZIP file at `at` (false when it cannot).
using ZipRead = std::function<bool(uint64_t at, void* buf, size_t n)>;
bool ZipList(const ZipRead& read, uint64_t zipSize, std::vector<ZipEntry>& out, std::string& why);
bool ZipExtract(const ZipRead& read, const ZipEntry& e, Bytes& out, std::string& why);
// Raw DEFLATE (RFC 1951) into `out` (expected: the size the ZIP names); false when damaged.
bool Inflate(const uint8_t* in, size_t inSize, Bytes& out, size_t expected);
uint32_t Crc32(const uint8_t* p, size_t n);

// A whole import from a folder or a ZIP on this device (the PC tool; the app feeds the Importer itself from
// the player's chosen folder or file). progress(done, total) in bytes of the pack.
bool ImportFolder(const std::string& gameRoot, const std::string& dest, const std::string& folder,
                  const std::string& name, Result& r, const std::function<void(uint64_t, uint64_t)>& progress,
                  const std::atomic<bool>* cancel = nullptr);
bool ImportZip(const std::string& gameRoot, const std::string& dest, const ZipRead& read, uint64_t zipSize,
               const std::string& name, const std::string& source, Result& r,
               const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>* cancel = nullptr);

// "reason\tfile\tdetail\treplaced\tkept\tidentical\tignored\tbytes\tlanguage" (the JNI's answer)
std::string Describe(const Result& r);

}  // namespace re4lang
