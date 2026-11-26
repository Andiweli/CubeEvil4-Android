// Language packs: the import and its checks (language_pack.h, port/docs/sprachpakete.md).
#include "language_pack.h"

#include "ports/yz2.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace re4lang {

namespace {

// ---- small helpers ------------------------------------------------------------------------------------------------

uint32_t be32(const Bytes& b, size_t o) {
  if (o > b.size() || b.size() - o < 4) return 0;
  return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3];
}

std::string lower(std::string s) {
  for (auto& c : s) c = char(tolower((unsigned char) c));
  return s;
}

bool endsWith(const std::string& s, const char* tail) {
  size_t n = strlen(tail);
  return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

bool isDir(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool readFile(const std::string& path, Bytes& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  bool ok = fseek(f, 0, SEEK_END) == 0;
  long n = ok ? ftell(f) : -1;
  ok = ok && n >= 0 && fseek(f, 0, SEEK_SET) == 0;
  if (ok) {
    out.resize(size_t(n));
    ok = n == 0 || fread(out.data(), 1, size_t(n), f) == size_t(n);
  }
  fclose(f);
  return ok;
}

bool mkdirs(const std::string& path) {
  if (path.empty() || isDir(path)) return true;
  size_t slash = path.find_last_of('/');
  if (slash != std::string::npos && slash > 0 && !mkdirs(path.substr(0, slash))) return false;
  return mkdir(path.c_str(), 0775) == 0 || errno == EEXIST;
}

// 0 written, else errno
int writeFile(const std::string& path, const uint8_t* data, size_t n) {
  size_t slash = path.find_last_of('/');
  if (slash != std::string::npos && !mkdirs(path.substr(0, slash))) return errno ? errno : EIO;
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return errno ? errno : EIO;
  size_t put = n ? fwrite(data, 1, n, f) : 0;
  int err = put == n ? 0 : (errno ? errno : EIO);
  if (fflush(f) != 0 && !err) err = errno ? errno : EIO;
  if (fclose(f) != 0 && !err) err = errno ? errno : EIO;
  return err;
}

void removeTree(const std::string& path) {
  DIR* d = opendir(path.c_str());
  if (!d) {
    unlink(path.c_str());
    return;
  }
  while (dirent* e = readdir(d)) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    std::string p = path + "/" + e->d_name;
    if (isDir(p)) {
      removeTree(p);
    } else {
      unlink(p.c_str());
    }
  }
  closedir(d);
  rmdir(path.c_str());
}

// Every file below `root` (paths relative to it, '/'), sorted.
void listFiles(const std::string& root, const std::string& rel, std::vector<std::string>& out) {
  DIR* d = opendir((root + (rel.empty() ? "" : "/" + rel)).c_str());
  if (!d) return;
  std::vector<std::string> names;
  while (dirent* e = readdir(d)) {
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());
  for (const auto& n : names) {
    std::string r = rel.empty() ? n : rel + "/" + n;
    if (isDir(root + "/" + r)) {
      listFiles(root, r, out);
    } else {
      out.push_back(r);
    }
  }
}

// Which language a slot of the six-language tables is (EU order: Japanese, English, French, German, Italian,
// Spanish) as the game's language number (2 English, 3 German, 4 French, 5 Spanish, 6 Italian; 0 Japanese).
const int kSlotLanguage[6] = {0, 2, 4, 3, 6, 5};

// ---- message tables (MDT) -------------------------------------------------------------------------------------

// One message block {x0, count, offsets[count]} at `bo` inside [.., end): the offsets inside, u16 aligned.
bool validBlock(const Bytes& b, size_t bo, size_t end) {
  if (bo + 8 > end) return false;
  uint32_t count = be32(b, bo + 4);
  if (count > 20000 || bo + 8 + size_t(count) * 4 > end) return false;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t o = be32(b, bo + 8 + i * 4);
    if (o < 8 + count * 4 || bo + o >= end || (bo + o) % 2) return false;
  }
  return true;
}

bool sixLanguages(const Bytes& b, size_t off) { return be32(b, off) == 6 && be32(b, off + 4) == 0x1C; }

// A message table in [off, end): six languages (6, 0x1C, then six block offsets; patch 0122) or one block.
bool validMdt(const Bytes& b, size_t off, size_t end) {
  if (end > b.size() || off >= end) return false;
  if (sixLanguages(b, off)) {
    for (int i = 0; i < 6; i++) {
      uint32_t bo = be32(b, off + 4 + 4 * i);
      if (!validBlock(b, off + bo, end)) return false;
    }
    return true;
  }
  return validBlock(b, off, end);
}

// The messages of block `slot` that differ (both tables six languages): adds them to slots[slot].
void tallyMdt(const Bytes& a, size_t ao, size_t ae, const Bytes& p, size_t po, size_t pe, int64_t* slots) {
  if (!slots || !sixLanguages(a, ao) || !sixLanguages(p, po)) return;
  for (int s = 0; s < 6; s++) {
    size_t ab = ao + be32(a, ao + 4 + 4 * s), pb = po + be32(p, po + 4 + 4 * s);
    size_t aEnd = s < 5 ? ao + be32(a, ao + 8 + 4 * s) : ae, pEnd = s < 5 ? po + be32(p, po + 8 + 4 * s) : pe;
    if (aEnd <= ab || pEnd <= pb) aEnd = ae, pEnd = pe;
    uint32_t na = be32(a, ab + 4), np = be32(p, pb + 4);
    uint32_t n = std::min(na, np);
    int64_t changed = int64_t(na > np ? na - np : np - na);
    for (uint32_t i = 0; i < n; i++) {
      size_t as = ab + be32(a, ab + 8 + 4 * i), ps = pb + be32(p, pb + 8 + 4 * i);
      size_t ax = i + 1 < na ? ab + be32(a, ab + 12 + 4 * i) : aEnd;
      size_t px = i + 1 < np ? pb + be32(p, pb + 12 + 4 * i) : pEnd;
      if (ax < as || px < ps || ax > a.size() || px > p.size()) continue;
      if (ax - as != px - ps || memcmp(a.data() + as, p.data() + ps, ax - as) != 0) changed++;
    }
    slots[s] += changed;
  }
}

// ---- part archives ----------------------------------------------------------------------------------------------

// u32 count, then at 0x10 the parts' offsets and after them their tags ("MDT", "EFF", "UWF", ... or zeros for an
// empty slot). Each part ends where the next one (by offset) starts.
struct Part {
  char tag[4];
  size_t off, end;
  bool empty;
};

bool parseParts(const Bytes& b, std::vector<Part>& out) {
  uint32_t n = be32(b, 0);
  if (n == 0 || n > 4096 || 0x10 + size_t(n) * 8 > b.size()) return false;
  std::vector<size_t> starts;
  out.clear();
  for (uint32_t i = 0; i < n; i++) {
    Part p;
    memcpy(p.tag, b.data() + 0x10 + n * 4 + i * 4, 4);
    p.empty = !p.tag[0] && !p.tag[1] && !p.tag[2] && !p.tag[3];
    p.off = be32(b, 0x10 + i * 4);
    if (!p.empty) {
      for (int k = 0; k < 3; k++) {
        if (!isalnum((unsigned char) p.tag[k])) return false;
      }
      if (p.off > b.size()) return false;
      starts.push_back(p.off);
    }
    out.push_back(p);
  }
  starts.push_back(b.size());
  std::sort(starts.begin(), starts.end());
  for (auto& p : out) {
    if (p.empty) continue;
    p.end = *std::upper_bound(starts.begin(), starts.end(), p.off);
    if (p.end < p.off) p.end = b.size();
  }
  return true;
}

std::string tagOf(const Part& p) { return std::string(p.tag, strnlen(p.tag, 3)); }

// [as, ae) of a and [ps, pe) of p equal once trailing zeros are left off.
bool sameTrimmed(const Bytes& a, size_t as, size_t ae, const Bytes& p, size_t ps, size_t pe) {
  while (ae > as && a[ae - 1] == 0) ae--;
  while (pe > ps && p[pe - 1] == 0) pe--;
  return ae - as == pe - ps && memcmp(a.data() + as, p.data() + ps, ae - as) == 0;
}

// Two offset tables (u32 count, u32 offsets from the table's start; a record runs to the next record by place or
// to the table's end) with the same count and the same records.
bool sameRecords(const Bytes& a, size_t as, size_t ae, const Bytes& p, size_t ps, size_t pe) {
  uint32_t n = be32(a, as);
  if (n != be32(p, ps) || n > 100000 || as + 4 + size_t(n) * 4 > ae || ps + 4 + size_t(n) * 4 > pe) return false;
  auto records = [n](const Bytes& b, size_t s, size_t e, std::vector<std::pair<size_t, size_t>>& out) {
    std::vector<size_t> pts{e - s};
    for (uint32_t i = 0; i < n; i++) pts.push_back(be32(b, s + 4 + 4 * i));
    std::sort(pts.begin(), pts.end());
    for (uint32_t i = 0; i < n; i++) {
      size_t o = be32(b, s + 4 + 4 * i);
      if (o > e - s) return false;
      size_t x = *std::upper_bound(pts.begin(), pts.end(), o);
      if (x > e - s) x = e - s;
      out.emplace_back(s + o, s + x);
    }
    return true;
  };
  std::vector<std::pair<size_t, size_t>> ra, rp;
  if (!records(a, as, ae, ra) || !records(p, ps, pe, rp)) return false;
  for (uint32_t i = 0; i < n; i++) {
    if (!sameTrimmed(a, ra[i].first, ra[i].second, p, rp[i].first, rp[i].second)) return false;
  }
  return true;
}

// Effect data (eff_sys.cpp ESP_EFF_HEADER: version 0xB, eleven table offsets): the picture tables (ids and TPLs)
// may change; every other table as on the disc, or (a table the disc has not) empty.
bool checkEffPictures(const Bytes& a, size_t ao, size_t ae, const Bytes& p, size_t po, size_t pe,
                      std::string& why) {
  const int kTplIds = 0, kTpls = 5;
  if (ae - ao < 0x30 || pe - po < 0x30 || be32(a, ao) != 0xB || be32(p, po) != 0xB) {
    why = "effect data of another version";
    return false;
  }
  auto regions = [](const Bytes& b, size_t o, size_t e, size_t* start, size_t* end) {
    std::vector<size_t> pts{e - o};
    for (int k = 0; k < 11; k++) {
      start[k] = be32(b, o + 4 + 4 * k);
      if (start[k]) pts.push_back(start[k]);
    }
    std::sort(pts.begin(), pts.end());
    for (int k = 0; k < 11; k++) {
      end[k] = start[k] ? *std::upper_bound(pts.begin(), pts.end(), start[k]) : 0;
    }
  };
  size_t as[11], aEnd[11], ps[11], pEnd[11];
  regions(a, ao, ae, as, aEnd);
  regions(p, po, pe, ps, pEnd);
  static const char* const names[11] = {"picture ids", "sequence ids", "sprite ids", "path ids", "object ids",
                                        "pictures", "animations", "sequences", "sprites", "paths", "objects"};
  for (int k = 0; k < 11; k++) {
    if (ps[k] && (ps[k] > pe - po || pEnd[k] < ps[k])) {
      why = std::string("effect table ") + names[k] + " outside the data";
      return false;
    }
    if (k == kTplIds || k == kTpls) continue;
    if (as[k]) {
      // the same table: the id tables byte for byte, the offset tables (u32 count, u32 offsets from the table's
      // start) record by record; trailing zeros are alignment (a translator's tool lays the tables out anew)
      bool same = ps[k] != 0 && (k < kTpls ? sameTrimmed(a, ao + as[k], ao + aEnd[k], p, po + ps[k], po + pEnd[k])
                                           : sameRecords(a, ao + as[k], ao + aEnd[k], p, po + ps[k], po + pEnd[k]));
      if (!same) {
        why = std::string("effect table ") + names[k] + " differs from the disc's";
        return false;
      }
    } else if (ps[k] && be32(p, po + ps[k]) != 0) {
      why = std::string("effect table ") + names[k] + " added";
      return false;
    }
  }
  return true;
}

// The parts of `p` against the disc's `a`: same count and tags; a part that differs must be a message table, the
// pictures of effect data (pictures) or a screen layout (layouts).
bool compareParts(const Bytes& a, const Bytes& p, bool pictures, bool layouts, std::string& why, int64_t* slots) {
  std::vector<Part> pa, pp;
  if (!parseParts(a, pa)) {
    why = "the disc's file has no part table";
    return false;
  }
  if (!parseParts(p, pp)) {
    why = "no valid part table";
    return false;
  }
  if (pa.size() != pp.size()) {
    why = "has " + std::to_string(pp.size()) + " parts, the disc's " + std::to_string(pa.size());
    return false;
  }
  for (size_t i = 0; i < pa.size(); i++) {
    const Part& x = pa[i];
    const Part& y = pp[i];
    if (x.empty != y.empty || (!x.empty && memcmp(x.tag, y.tag, 4) != 0)) {
      why = "part " + std::to_string(i) + " is " + (y.empty ? "empty" : tagOf(y)) + ", on the disc " +
            (x.empty ? "empty" : tagOf(x));
      return false;
    }
    if (x.empty) continue;
    if (x.end - x.off == y.end - y.off && memcmp(a.data() + x.off, p.data() + y.off, x.end - x.off) == 0) continue;
    std::string tag = tagOf(x);
    if (tag == "MDT") {
      if (!validMdt(p, y.off, y.end)) {
        why = "message table (part " + std::to_string(i) + ") damaged";
        return false;
      }
      tallyMdt(a, x.off, x.end, p, y.off, y.end, slots);
    } else if (tag == "EFF" && pictures) {
      std::string w;
      if (!checkEffPictures(a, x.off, x.end, p, y.off, y.end, w)) {
        why = "part " + std::to_string(i) + ": " + w;
        return false;
      }
    } else if (tag == "UWF" && layouts) {
      if (y.end - y.off < 4) {
        why = "screen layout (part " + std::to_string(i) + ") empty";
        return false;
      }
    } else {
      why = "part " + std::to_string(i) + " (" + tag + ") differs from the disc's";
      return false;
    }
  }
  return true;
}

// ---- DAS archives (the disc's files with a part table: 0x20 bytes CAB6BE20, then 0x20-byte records
// {type, size, -, offset} up to 0xFFFFFFFF; 0xFFFFFFFE a marker of no size, dvd.cpp re4_dvd_header_check) -----

struct DasEntry {
  uint32_t type, size, off;
};

bool isDas(const Bytes& b) { return b.size() >= 0x400 && be32(b, 0) == 0xCAB6BE20u; }

bool parseDas(const Bytes& b, std::vector<DasEntry>& out) {
  out.clear();
  if (!isDas(b)) return false;
  for (size_t o = 0x20; o + 0x20 <= 0x400; o += 0x20) {
    uint32_t t = be32(b, o);
    if (t == 0xFFFFFFFFu) return true;
    if (t == 0xFFFFFFFEu) continue;
    if (t > 4) return false;
    DasEntry e{t, be32(b, o + 4), be32(b, o + 12)};
    if (e.off > b.size() || e.size > b.size() - e.off) return false;
    out.push_back(e);
  }
  return false;
}

bool isYz2(const Bytes& b, size_t off, size_t n) {
  if (n < 0x40) return false;
  bool tab = false;
  for (size_t i = 0; i < 0x20; i++) {
    char c = char(b[off + i]);
    if (c == '\t') tab = true;
    if (c == '\n') return tab && isxdigit((unsigned char) b[off]);
    if (!isxdigit((unsigned char) c) && c != '\t') return false;
  }
  return false;
}

bool unyz2(const Bytes& b, size_t off, size_t n, Bytes& out, std::string& why) {
  try {
    Bytes in(b.begin() + off, b.begin() + off + n);
    out = re4::DecodeYz2(in, 64u << 20).bytes;
    return true;
  } catch (const std::exception& e) {
    why = std::string("packed data damaged (") + e.what() + ")";
    return false;
  }
}

// The entries of two DAS files: same entries; a body that differs goes to compareParts (unpacked when yz2).
bool compareDas(const Bytes& a, const Bytes& p, bool pictures, bool layouts, std::string& why, int64_t* slots) {
  std::vector<DasEntry> ea, ep;
  if (!parseDas(a, ea)) {
    why = "the disc's file has no valid part list";
    return false;
  }
  if (!parseDas(p, ep)) {
    why = "no valid part list";
    return false;
  }
  if (ea.size() != ep.size()) {
    why = "has " + std::to_string(ep.size()) + " blocks, the disc's " + std::to_string(ea.size());
    return false;
  }
  for (size_t i = 0; i < ea.size(); i++) {
    const DasEntry& x = ea[i];
    const DasEntry& y = ep[i];
    if (x.type != y.type) {
      why = "block " + std::to_string(i) + " of another kind";
      return false;
    }
    if (x.size == y.size && memcmp(a.data() + x.off, p.data() + y.off, x.size) == 0) continue;
    Bytes ba, bp;
    bool za = isYz2(a, x.off, x.size), zp = isYz2(p, y.off, y.size);
    if (za != zp) {
      why = "block " + std::to_string(i) + " packed differently";
      return false;
    }
    if (za) {
      std::string w;
      if (!unyz2(a, x.off, a.size() - x.off, ba, w)) {
        why = "the disc's block " + std::to_string(i) + ": " + w;
        return false;
      }
      if (!unyz2(p, y.off, p.size() - y.off, bp, w)) {
        why = "block " + std::to_string(i) + ": " + w;
        return false;
      }
    } else {
      ba.assign(a.begin() + x.off, a.begin() + x.off + x.size);
      bp.assign(p.begin() + y.off, p.begin() + y.off + y.size);
    }
    std::string w;
    if (!compareParts(ba, bp, pictures, layouts, w, slots)) {
      why = "block " + std::to_string(i) + ": " + w;
      return false;
    }
  }
  return true;
}

// ---- TPL ------------------------------------------------------------------------------------------------------

bool tplOk(const Bytes& b, uint32_t& count, std::string& why) {
  count = be32(b, 4);
  if (be32(b, 0) != 0x0020AF30u || be32(b, 8) != 0xC || count == 0 || count > 1024 ||
      0xC + size_t(count) * 8 > b.size()) {
    why = "not a TPL picture file";
    return false;
  }
  for (uint32_t i = 0; i < count; i++) {
    uint32_t h = be32(b, 0xC + i * 8);
    if (h == 0 || size_t(h) + 0x24 > b.size() || be32(b, h + 8) >= b.size()) {
      why = "picture " + std::to_string(i) + " outside the file";
      return false;
    }
  }
  return true;
}

// ---- where a file of the pack belongs ---------------------------------------------------------------------------

// "Disc 1", "disc2", "DISCO 2", "CD_1", "dvd-2": the disc's number, else 0.
int discFolder(const std::string& name) {
  std::string s;
  for (char c : lower(name)) {
    if (c != ' ' && c != '_' && c != '-' && c != '.') s += c;
  }
  for (const char* prefix : {"disco", "disc", "disk", "dvd", "cd"}) {
    size_t n = strlen(prefix);
    if (s.size() == n + 1 && s.compare(0, n, prefix) == 0 && (s[n] == '1' || s[n] == '2')) return s[n] - '0';
  }
  return 0;
}

bool programExtension(const std::string& lowerName) {
  return endsWith(lowerName, ".dol") || endsWith(lowerName, ".rel") || endsWith(lowerName, ".elf");
}

}  // namespace

// ---- the player's imported discs -------------------------------------------------------------------------------

struct Importer::Disc {
  int number = 0;
  std::string root;                          // holds sys/ and files/
  std::map<std::string, std::string> files;  // lower-case path below files/ -> its own spelling
  std::set<std::string> tops;                // lower-case top folders of files/
  std::string release;
};

Importer::Importer(std::string gameRoot, std::string dest)
    : gameRoot_(std::move(gameRoot)), dest_(std::move(dest)), work_(dest_ + ".work") {}

Importer::~Importer() {
  for (Disc* d : discs_) delete d;
}

bool Importer::begin(Result& r) {
  r = Result();
  int first = isDir(gameRoot_ + "/disc0") ? 0 : 1;  // the PC's data folder: disc0, disc1; the app's disc1, disc2
  for (int n = 1; n <= 2; n++) {
    std::string root = gameRoot_ + "/disc" + std::to_string(n - 1 + first);
    Disc* d = nullptr;
    if (isDir(root + "/files") && isDir(root + "/sys")) {
      d = new Disc;
      d->number = n;
      d->root = root;
      Bytes boot;
      if (readFile(root + "/sys/boot.bin", boot) && boot.size() >= 6) d->release.assign((const char*) boot.data(), 6);
      std::vector<std::string> list;
      listFiles(root + "/files", "", list);
      for (const auto& f : list) {
        d->files[lower(f)] = f;
        d->tops.insert(lower(f.substr(0, f.find('/'))));
      }
    }
    discs_.push_back(d);
  }
  if (!discs_[0]) {
    r.reason = kNoDiscs;
    r.detail = "no imported disc 1 in " + gameRoot_;
    return false;
  }
  for (Disc* d : discs_) {
    if (d && d->release != "G4BP08") {
      r.reason = kNotEuropean;
      r.detail = "disc " + std::to_string(d->number) + " is " + (d->release.empty() ? "unknown" : d->release) +
                 ", language packs are for the European discs (G4BP08)";
      return false;
    }
  }
  removeTree(work_);
  if (!mkdirs(work_)) {
    r.reason = kNoSpace;
    r.detail = "cannot make " + work_ + " (" + strerror(errno) + ")";
    return false;
  }
  begun_ = true;
  return true;
}

// The pack's path -> disc (1, 2, or 0: every disc that has the file) and the path on the disc; programName: a
// program file outside the game's folders. False: not a game file (a read-me ...), or r.reason set.
bool Importer::place(const std::string& path, int& disc, std::string& rel, bool& programName, Result& r) {
  std::vector<std::string> parts;
  std::string cur;
  for (char c : path) {
    if (c == '/' || c == '\\') {
      if (!cur.empty()) parts.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  programName = false;
  disc = 0;
  rel.clear();
  for (const auto& p : parts) {
    if (p == "..") {
      r.reason = kUnsafePath;
      r.file = path;
      r.detail = "a name with \"..\" in it";
      return false;
    }
  }
  if (parts.empty()) return false;
  std::set<std::string> tops{"sys"};
  for (Disc* d : discs_) {
    if (d) tops.insert(d->tops.begin(), d->tops.end());
  }
  size_t start = 0;
  for (size_t i = 0; i + 1 < parts.size(); i++) {
    if (int n = discFolder(parts[i])) {
      disc = n;
      start = i + 1;
    }
  }
  size_t top = parts.size();
  for (size_t i = start; i + 1 < parts.size(); i++) {
    std::string l = lower(parts[i]);
    if (l == "files" && tops.count(lower(parts[i + 1]))) continue;  // Dolphin's "Extract Entire Disc": files/, sys/
    if (tops.count(l)) {
      top = i;
      break;
    }
  }
  if (top == parts.size()) {
    programName = programExtension(lower(parts.back()));
    return false;
  }
  for (size_t i = top; i < parts.size(); i++) rel += (i > top ? "/" : "") + parts[i];
  return true;
}

Importer::Want Importer::want(const std::string& path, Result& r) {
  int disc;
  std::string rel;
  bool program;
  if (place(path, disc, rel, program, r)) return kRead;
  if (r.reason) return kStop;
  if (program) {
    r.reason = kProgramFile;
    r.file = path;
    r.detail = "a program file (" + path + ")";
    return kStop;
  }
  r.ignored++;
  return kSkip;
}

bool Importer::add(const std::string& path, const std::vector<uint8_t>& data, Result& r) {
  int disc;
  std::string rel;
  bool program;
  if (!place(path, disc, rel, program, r)) {
    if (!r.reason) r.ignored++;
    return !r.reason;
  }
  std::string low = lower(rel);
  // sys/: the disc's own tables and program; equal is fine, anything else is not a language pack
  if (low.compare(0, 4, "sys/") == 0) {
    for (Disc* d : discs_) {
      if (!d || (disc && d->number != disc)) continue;
      Bytes mine;
      if (readFile(d->root + "/" + rel, mine) && mine == data) {
        r.identical++;
        return true;
      }
    }
    r.reason = kProgramFile;
    r.file = path;
    r.detail = rel + " differs from the imported disc's (the game's program or the disc's tables)";
    return false;
  }
  // the discs that carry this file
  std::vector<Disc*> targets;
  for (Disc* d : discs_) {
    if (d && (!disc || d->number == disc) && d->files.count(low)) targets.push_back(d);
  }
  if (targets.empty()) {
    if (disc == 2 && !discs_[1]) {  // disc 2 not imported: its files wait for it
      r.ignored++;
      return true;
    }
    r.reason = kNotOnDisc;
    r.file = path;
    r.detail = rel + " is not on the imported disc" + (disc ? " " + std::to_string(disc) : "s");
    return false;
  }
  bool counted = false;
  for (Disc* d : targets) {
    const std::string& own = d->files[low];
    Bytes mine;
    if (!readFile(d->root + "/files/" + own, mine)) {
      r.reason = kReadFailed;
      r.file = path;
      r.detail = "cannot read the imported disc's " + own;
      return false;
    }
    if (mine == data) {
      if (!counted) r.identical++;
      counted = true;
      continue;
    }
    // what may change in which file
    std::string why;
    int64_t slots[6] = {};
    bool ok;
    size_t slash = low.find('/');
    std::string topDir = low.substr(0, slash);
    std::string name = low.substr(low.find_last_of('/') + 1);
    if (topDir == "rel" || programExtension(low)) {
      r.reason = kProgramFile;
      r.file = path;
      r.detail = own + " is a program module";
      return false;
    } else if (topDir.size() >= 3 && topDir[0] == 's' && topDir[1] == 't' && isdigit((unsigned char) topDir[2]) &&
               name[0] == 'r' && endsWith(name, ".das") && low.find('/', slash + 1) == std::string::npos) {
      ok = CheckRoom(mine, data, why, slots);
    } else if (topDir == "evd" && endsWith(name, ".evd")) {
      ok = CheckEvent(mine, data, why, slots);
    } else if (low == "etc/core.das" || low == "etc/memcard.das" || low == "etc/movie.das" ||
               (topDir == "op" && endsWith(name, ".das"))) {
      ok = CheckArchive(mine, data, false, why, slots);
    } else if (topDir == "ss" && endsWith(name, ".dat")) {
      ok = CheckArchive(mine, data, true, why, slots);
    } else if (topDir == "ss" && endsWith(name, ".tpl")) {
      ok = CheckTpl(mine, data, why);
    } else if (topDir == "font" && endsWith(name, ".fnt")) {
      ok = data.size() >= 16 && data.size() <= mine.size() * 4 + (1u << 20) && memcmp(data.data(), mine.data(), 4) == 0;
      if (!ok) why = "not a font of the game";
    } else {
      r.reason = kNotText;
      r.file = path;
      r.detail = own + " is not a text or menu file";
      return false;
    }
    if (!ok) {
      r.reason = kMoreThanText;
      r.file = path;
      r.detail = own + ": " + why;
      return false;
    }
    for (int s = 0; s < 6; s++) slotScore_[kSlotLanguage[s]] += slots[s];
    // a menu folder of one language (ss/Esp ...) counts for that language
    if (topDir == "ss" && slash != std::string::npos) {
      std::string lang = low.substr(slash + 1, low.find('/', slash + 1) - slash - 1);
      static const char* const folders[] = {"eng", "ger", "fra", "esp", "ita"};
      static const int languages[] = {2, 3, 4, 5, 6};
      for (int k = 0; k < 5; k++) {
        if (lang == folders[k]) slotScore_[languages[k]] += 25;
      }
    }
    if (!keep(Target{d->number, own}, data, r)) return false;
  }
  return true;
}

// Writes the file for disc t.disc (one copy under files/ for both discs when they agree).
bool Importer::keep(const Target& t, const std::vector<uint8_t>& data, Result& r) {
  std::string key = std::to_string(t.disc) + "\t" + t.rel;
  auto seen = files_.find(key);
  if (seen != files_.end()) {
    Bytes before;
    if (readFile(work_ + "/" + seen->second, before) && before == data) return true;
    r.reason = kTwoCopies;
    r.file = t.rel;
    r.detail = "two different files for " + t.rel + " of disc " + std::to_string(t.disc);
    return false;
  }
  std::string low = lower(t.rel);
  std::string file = "files/" + t.rel;
  auto shared = shared_.find(low);
  if (shared != shared_.end()) {
    Bytes before;
    if (!(readFile(work_ + "/" + file, before) && before == data)) file = "disc" + std::to_string(t.disc) + "/" + t.rel;
  }
  if (shared == shared_.end() || file != "files/" + t.rel) {
    int err = writeFile(work_ + "/" + file, data.data(), data.size());
    if (err) {
      r.reason = err == ENOSPC ? kNoSpace : kReadFailed;
      r.file = t.rel;
      r.detail = "cannot write " + file + " (" + strerror(err) + ")";
      return false;
    }
    r.kept++;
    r.bytes += data.size();
    if (shared == shared_.end()) shared_[low] = Bytes();
  }
  files_[key] = file;
  sizes_[key] = uint32_t(data.size());
  r.replaced++;
  return true;
}

namespace {

// A Java .properties value: \ = : # ! escaped, anything beyond ASCII as \uXXXX (UTF-8 in).
std::string propertyValue(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    unsigned char c = (unsigned char) s[i];
    uint32_t cp = c;
    size_t n = 1;
    if (c >= 0xF0 && i + 3 < s.size()) {
      cp = (c & 7u) << 18 | (s[i + 1] & 0x3Fu) << 12 | (s[i + 2] & 0x3Fu) << 6 | (s[i + 3] & 0x3Fu);
      n = 4;
    } else if (c >= 0xE0 && i + 2 < s.size()) {
      cp = (c & 0xFu) << 12 | (s[i + 1] & 0x3Fu) << 6 | (s[i + 2] & 0x3Fu);
      n = 3;
    } else if (c >= 0xC0 && i + 1 < s.size()) {
      cp = (c & 0x1Fu) << 6 | (s[i + 1] & 0x3Fu);
      n = 2;
    }
    i += n;
    if (cp == '\\' || cp == '=' || cp == ':' || cp == '#' || cp == '!') {
      out += '\\';
      out += char(cp);
    } else if (cp < 0x20) {
      out += ' ';
    } else if (cp < 0x80) {
      out += char(cp);
    } else {
      auto unit = [&out](uint32_t u) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04X", unsigned(u));
        out += buf;
      };
      if (cp >= 0x10000) {
        cp -= 0x10000;
        unit(0xD800 + (cp >> 10));
        unit(0xDC00 + (cp & 0x3FF));
      } else {
        unit(cp);
      }
    }
  }
  return out;
}

}  // namespace

bool Importer::finish(const std::string& name, const std::string& source, Result& r) {
  if (!begun_) return false;
  if (r.replaced == 0) {
    r.reason = r.identical ? kNoChanges : kNotPack;
    r.detail = r.identical ? "every file of the pack is as on the imported discs" : "no files of the game in it";
    abort();
    return false;
  }
  int language = 0;
  int64_t best = 0;
  for (int l = 2; l <= 6; l++) {
    if (slotScore_[l] > best) best = slotScore_[l], language = l;
  }
  if (!language) {
    r.reason = kNoLanguage;
    r.detail = "no texts of a language changed";
    abort();
    return false;
  }
  r.language = language;
  std::string overlay = "V\t1\nL\t" + std::to_string(language) + "\n";
  for (const auto& f : files_) {
    size_t tab = f.first.find('\t');
    overlay += "F\t" + f.first.substr(0, tab) + "\t" + f.first.substr(tab + 1) + "\t" +
               std::to_string(sizes_[f.first]) + "\t" + f.second + "\n";
  }
  char date[32];
  time_t now = time(nullptr);
  strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&now));
  std::string props = "# CubeEvil4 language pack (port/docs/sprachpakete.md)\nversion=1\n";
  props += "name=" + propertyValue(name) + "\n";
  props += "source=" + propertyValue(source) + "\n";
  props += "language=" + std::to_string(language) + "\n";
  props += "release=G4BP08\n";
  props += "replaced=" + std::to_string(r.replaced) + "\n";
  props += "files=" + std::to_string(r.kept) + "\n";
  props += "bytes=" + std::to_string(r.bytes) + "\n";
  props += "identical=" + std::to_string(r.identical) + "\n";
  props += "ignored=" + std::to_string(r.ignored) + "\n";
  props += std::string("imported=") + date + "\n";
  int err = writeFile(work_ + "/overlay.tsv", (const uint8_t*) overlay.data(), overlay.size());
  if (!err) err = writeFile(work_ + "/pack.properties", (const uint8_t*) props.data(), props.size());
  if (err) {
    r.reason = err == ENOSPC ? kNoSpace : kReadFailed;
    r.detail = std::string("cannot write the pack's list (") + strerror(err) + ")";
    abort();
    return false;
  }
  // the old pack out, the new one in (the old one only goes once the new one is in place)
  std::string old = dest_ + ".old";
  removeTree(old);
  if (isDir(dest_) && rename(dest_.c_str(), old.c_str()) != 0) {
    r.reason = kReadFailed;
    r.detail = std::string("cannot move the old pack aside (") + strerror(errno) + ")";
    abort();
    return false;
  }
  if (rename(work_.c_str(), dest_.c_str()) != 0) {
    int e = errno;
    if (isDir(old)) rename(old.c_str(), dest_.c_str());
    r.reason = kReadFailed;
    r.detail = std::string("cannot put the pack in place (") + strerror(e) + ")";
    abort();
    return false;
  }
  removeTree(old);
  begun_ = false;
  return true;
}

void Importer::abort() {
  removeTree(work_);
  begun_ = false;
}

// ---- the checks ------------------------------------------------------------------------------------------------

bool CheckRoom(const Bytes& disc, const Bytes& pack, std::string& why, int64_t* slots) {
  // rooms: only message tables (a room's other parts are its world: collision, enemies, items, cameras ...)
  return compareDas(disc, pack, false, false, why, slots);
}

std::string SelfTestRoom(const Bytes& room, const char* tag) {
  std::vector<DasEntry> e;
  Bytes body;
  std::string why;
  if (!parseDas(room, e) || e.empty() || !isYz2(room, e[0].off, e[0].size) ||
      !unyz2(room, e[0].off, room.size() - e[0].off, body, why))
    return "not a packed room: " + why;
  std::vector<Part> parts;
  if (!parseParts(body, parts)) return "no part table";
  for (size_t i = 0; i < parts.size(); i++) {
    if (parts[i].empty || tagOf(parts[i]) != tag || parts[i].end - parts[i].off < 64) continue;
    Bytes changed = body;
    changed[parts[i].off + (parts[i].end - parts[i].off) / 2] ^= 0x5A;
    int64_t slots[6] = {};
    bool ok = compareParts(body, changed, false, false, why, slots);
    return std::string("one byte of part ") + std::to_string(i) + " (" + tag + ") changed: " +
           (ok ? "accepted" : "refused (" + why + ")");
  }
  return std::string("no part ") + tag;
}

bool CheckArchive(const Bytes& disc, const Bytes& pack, bool layouts, std::string& why, int64_t* slots) {
  if (isDas(disc)) return compareDas(disc, pack, true, layouts, why, slots);
  std::vector<Part> parts;
  if (parseParts(disc, parts)) return compareParts(disc, pack, true, layouts, why, slots);
  why = "a file of a kind the import does not know";
  return false;
}

bool CheckTpl(const Bytes& disc, const Bytes& pack, std::string& why) {
  uint32_t a = 0, p = 0;
  std::string w;
  if (!tplOk(disc, a, w)) {
    why = "the disc's file: " + w;
    return false;
  }
  if (!tplOk(pack, p, why)) return false;
  if (a != p) {
    why = std::to_string(p) + " pictures, the disc's " + std::to_string(a);
    return false;
  }
  return true;
}

// An event (event.h EvtHeader 0x50 bytes: packets at pacOfs, nBin EvtBinEntry of 0x40 bytes at binOfs, each a
// name of 0x30 bytes and the offset of its data). Header, packets and names as on the disc; every asset but the
// message tables (".mdt") as on the disc at the same place; a message table may move (a longer one is put at the
// end and its offset changed).
bool CheckEvent(const Bytes& disc, const Bytes& pack, std::string& why, int64_t* slots) {
  if (disc.size() < 0x50 || pack.size() < 0x50) {
    why = "not an event file";
    return false;
  }
  if (memcmp(disc.data(), pack.data(), 0x50) != 0) {
    why = "the event's header differs";
    return false;
  }
  uint32_t pac = be32(disc, 0x40), pacSize = be32(disc, 0x44), n = be32(disc, 0x48), bins = be32(disc, 0x4C);
  if (pac > disc.size() || pacSize > disc.size() - pac || n > 4096 || bins > disc.size() ||
      size_t(n) * 0x40 > disc.size() - bins || pac + size_t(pacSize) > pack.size() ||
      bins + size_t(n) * 0x40 > pack.size()) {
    why = "the event's tables lie outside the file";
    return false;
  }
  if (memcmp(disc.data() + pac, pack.data() + pac, pacSize) != 0) {
    why = "the event's packets (what happens in it) differ";
    return false;
  }
  std::vector<size_t> da{disc.size()}, pa{pack.size()};
  for (uint32_t i = 0; i < n; i++) {
    da.push_back(be32(disc, bins + i * 0x40 + 0x30));
    pa.push_back(be32(pack, bins + i * 0x40 + 0x30));
  }
  std::sort(da.begin(), da.end());
  std::sort(pa.begin(), pa.end());
  for (uint32_t i = 0; i < n; i++) {
    size_t e = bins + i * 0x40;
    std::string name((const char*) disc.data() + e, strnlen((const char*) disc.data() + e, 0x30));
    bool message = endsWith(lower(name), ".mdt");
    // the name, and for anything but a message table its offset and size (+0x30, +0x34); the rest of the entry always
    if (memcmp(disc.data() + e, pack.data() + e, 0x30) != 0 ||
        memcmp(disc.data() + e + 0x38, pack.data() + e + 0x38, 8) != 0 ||
        (!message && memcmp(disc.data() + e + 0x30, pack.data() + e + 0x30, 8) != 0)) {
      why = "the event's asset list differs";
      return false;
    }
    size_t ao = be32(disc, e + 0x30), po = be32(pack, e + 0x30);
    size_t ae = *std::upper_bound(da.begin(), da.end(), ao), pe = *std::upper_bound(pa.begin(), pa.end(), po);
    if (ao > disc.size() || po > pack.size()) {
      why = name + " lies outside the file";
      return false;
    }
    if (endsWith(lower(name), ".mdt")) {
      if (!validMdt(pack, po, pe)) {
        why = "message table " + name + " damaged";
        return false;
      }
      tallyMdt(disc, ao, ae, pack, po, pe, slots);
      continue;
    }
    if (ao != po || ae > pack.size() || memcmp(disc.data() + ao, pack.data() + po, ae - ao) != 0) {
      why = name + " differs from the disc's (only message tables may change)";
      return false;
    }
  }
  return true;
}

// ---- whole imports (PC tool) -------------------------------------------------------------------------------------

bool ImportFolder(const std::string& gameRoot, const std::string& dest, const std::string& folder,
                  const std::string& name, Result& r, const std::function<void(uint64_t, uint64_t)>& progress,
                  const std::atomic<bool>* cancel) {
  Importer imp(gameRoot, dest);
  if (!imp.begin(r)) return false;
  std::vector<std::string> list;
  listFiles(folder, "", list);
  uint64_t total = 0, done = 0;
  std::vector<uint64_t> sizes;
  for (const auto& f : list) {
    struct stat st;
    sizes.push_back(stat((folder + "/" + f).c_str(), &st) == 0 ? uint64_t(st.st_size) : 0);
    total += sizes.back();
  }
  for (size_t i = 0; i < list.size(); i++) {
    if (cancel && cancel->load()) {
      r.reason = kCancelled;
      imp.abort();
      return false;
    }
    Importer::Want w = imp.want(list[i], r);
    if (w == Importer::kStop) {
      imp.abort();
      return false;
    }
    if (w == Importer::kRead) {
      Bytes data;
      if (!readFile(folder + "/" + list[i], data)) {
        r.reason = kReadFailed;
        r.file = list[i];
        r.detail = "cannot read " + list[i];
        imp.abort();
        return false;
      }
      if (!imp.add(list[i], data, r)) {
        imp.abort();
        return false;
      }
    }
    done += sizes[i];
    if (progress) progress(done, total);
  }
  size_t slash = folder.find_last_of('/');
  return imp.finish(name, slash == std::string::npos ? folder : folder.substr(slash + 1), r);
}

bool ImportZip(const std::string& gameRoot, const std::string& dest, const ZipRead& read, uint64_t zipSize,
               const std::string& name, const std::string& source, Result& r,
               const std::function<void(uint64_t, uint64_t)>& progress, const std::atomic<bool>* cancel) {
  Importer imp(gameRoot, dest);
  if (!imp.begin(r)) return false;
  std::vector<ZipEntry> entries;
  std::string why;
  if (!ZipList(read, zipSize, entries, why)) {
    r.reason = kBadZip;
    r.detail = why;
    imp.abort();
    return false;
  }
  uint64_t total = 0, done = 0;
  for (const auto& e : entries) total += e.size;
  for (const auto& e : entries) {
    if (cancel && cancel->load()) {
      r.reason = kCancelled;
      imp.abort();
      return false;
    }
    if (!e.name.empty() && e.name.back() != '/') {
      Importer::Want w = imp.want(e.name, r);
      if (w == Importer::kStop) {
        imp.abort();
        return false;
      }
      if (w == Importer::kRead) {
        Bytes data;
        if (e.size > (512u << 20)) {
          r.reason = kReadFailed;
          r.file = e.name;
          r.detail = e.name + " is too large for a game file";
          imp.abort();
          return false;
        }
        if (!ZipExtract(read, e, data, why)) {
          r.reason = kBadZip;
          r.file = e.name;
          r.detail = why;
          imp.abort();
          return false;
        }
        if (!imp.add(e.name, data, r)) {
          imp.abort();
          return false;
        }
      }
    }
    done += e.size;
    if (progress) progress(done, total);
  }
  return imp.finish(name, source, r);
}

std::string Describe(const Result& r) {
  auto clean = [](std::string s) {
    for (auto& c : s) {
      if (c == '\t' || c == '\n') c = ' ';
    }
    return s;
  };
  return std::to_string(r.reason) + "\t" + clean(r.file) + "\t" + clean(r.detail) + "\t" + std::to_string(r.replaced) +
         "\t" + std::to_string(r.kept) + "\t" + std::to_string(r.identical) + "\t" + std::to_string(r.ignored) + "\t" +
         std::to_string(r.bytes) + "\t" + std::to_string(r.language);
}

}  // namespace re4lang
