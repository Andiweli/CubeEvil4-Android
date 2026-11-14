// The Texture Packer's core (see packer.h).
#include "packer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>

#include "etc2.h"
#include "fileio.h"
#include "image.h"

namespace fs = std::filesystem;

namespace pack {

const char *const kToolName = "CubeEvil4 Texture Packer";
const char *const kToolVersion = "1.3";

std::string Lower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

std::string Thousands(std::uint64_t v) {
  std::string s = std::to_string(v);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
  return s;
}

std::string Bytes(std::uint64_t b) {
  char buf[64];
  if (b >= 1000ull * 1000 * 1000)
    std::snprintf(buf, sizeof(buf), "%.2f GB", b / 1e9);
  else
    std::snprintf(buf, sizeof(buf), "%.0f MB", b / 1e6);
  return buf;
}

std::string Duration(double seconds) {
  char buf[64];
  const int s = static_cast<int>(seconds + 0.5);
  if (s >= 3600)
    std::snprintf(buf, sizeof(buf), "%d h %d min", s / 3600, (s / 60) % 60);
  else if (s >= 60)
    std::snprintf(buf, sizeof(buf), "%d min %d s", s / 60, s % 60);
  else
    std::snprintf(buf, sizeof(buf), "%d s", s);
  return buf;
}

// ---------------- names ----------------

bool Key::operator<(const Key &o) const {
  if (tex != o.tex) return tex < o.tex;
  if (tlut != o.tlut) return tlut < o.tlut;
  if (w != o.w) return w < o.w;
  if (h != o.h) return h < o.h;
  if (fmt != o.fmt) return fmt < o.fmt;
  if (mip != o.mip) return mip < o.mip;
  return tlutAny < o.tlutAny;
}

namespace {

bool ParseHex16(const std::string &s, std::size_t at, std::uint64_t &v) {
  if (at + 16 > s.size()) return false;
  v = 0;
  for (std::size_t i = at; i < at + 16; i++) {
    const char c = s[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else return false;
    v = v << 4 | static_cast<unsigned>(d);
  }
  return true;
}

bool ParseNumber(const std::string &s, std::size_t &at, unsigned &v) {
  const std::size_t start = at;
  v = 0;
  while (at < s.size() && s[at] >= '0' && s[at] <= '9' && at - start < 6) v = v * 10 + static_cast<unsigned>(s[at++] - '0');
  return at > start;
}

}  // namespace

bool ParseName(const std::string &fileName, Name &n) {
  std::string s = Lower(fileName);
  const auto ends = [&](const char *ext) {
    const std::size_t e = std::strlen(ext);
    return s.size() > e && s.compare(s.size() - e, e, ext) == 0;
  };
  if (ends(".dds")) n.kind = 0;
  else if (ends(".png")) n.kind = 1;
  else if (ends(".jpg") || ends(".jpeg")) n.kind = 2;
  else return false;
  s.resize(s.rfind('.'));
  if (s.compare(0, 5, "tex1_") != 0) return false;
  std::size_t at = 5;
  if (!ParseNumber(s, at, n.key.w) || at >= s.size() || s[at++] != 'x' || !ParseNumber(s, at, n.key.h)) return false;
  if (s.compare(at, 2, "_m") == 0 && at + 2 < s.size() && s[at + 2] == '_') {
    n.mip = true;
    at += 2;
  }
  if (at >= s.size() || s[at++] != '_' || !ParseHex16(s, at, n.key.tex)) return false;
  at += 16;
  if (at < s.size() && s[at] == '_') {
    std::uint64_t t;
    if (ParseHex16(s, at + 1, t) && (at + 17 == s.size() || s[at + 17] == '_')) {
      n.key.tlut = t;
      n.key.hasTlut = true;
      at += 17;
    } else if (s.compare(at, 2, "_$") == 0) {
      n.key.hasTlut = true;
      n.key.tlutAny = true;
      at += 2;
    }
  }
  if (at >= s.size() || s[at++] != '_' || !ParseNumber(s, at, n.key.fmt)) return false;
  if (s.compare(at, 4, "_mip") == 0) {
    unsigned level;
    at += 4;
    if (!ParseNumber(s, at, level)) return false;
    n.mipFile = true;
  }
  if (s.compare(at, 4, "_arb") == 0) {
    n.arb = true;
    at += 4;
  }
  if (at != s.size()) return false;
  if (n.key.w == 0 || n.key.h == 0 || n.key.w > 1024 || n.key.h > 1024 || n.key.fmt > 14) return false;
  n.key.mip = n.mip;
  return true;
}

const char *KindName(int kind) { return kind == 0 ? "DDS" : kind == 1 ? "PNG" : "JPG"; }

std::string KeyName(const Key &k) {
  char buf[128];
  const char *m = k.mip ? "_m" : "";
  if (k.hasTlut && k.tlutAny)
    std::snprintf(buf, sizeof(buf), "tex1_%ux%u%s_%016llx_$_%u", k.w, k.h, m, static_cast<unsigned long long>(k.tex), k.fmt);
  else if (k.hasTlut)
    std::snprintf(buf, sizeof(buf), "tex1_%ux%u%s_%016llx_%016llx_%u", k.w, k.h, m, static_cast<unsigned long long>(k.tex),
                  static_cast<unsigned long long>(k.tlut), k.fmt);
  else
    std::snprintf(buf, sizeof(buf), "tex1_%ux%u%s_%016llx_%u", k.w, k.h, m, static_cast<unsigned long long>(k.tex), k.fmt);
  return buf;
}

bool IsTextureFile(const std::string &fileName) {
  const std::size_t dot = fileName.rfind('.');
  if (dot == std::string::npos) return false;
  const std::string e = Lower(fileName.substr(dot));
  return e == ".dds" || e == ".png" || e == ".jpg" || e == ".jpeg";
}

// ---------------- files ----------------

bool Reader::ReadHead(std::size_t n, std::vector<std::uint8_t> &out) {
  std::string error;
  if (!ReadAll(out, error)) return false;
  if (out.size() > n) out.resize(n);
  return true;
}

namespace {

class DiskReader : public Reader {
 public:
  explicit DiskReader(std::string path) : path_(std::move(path)) {}
  bool ReadAll(std::vector<std::uint8_t> &out, std::string &error) override {
    if (fio::ReadFile(path_, out)) return true;
    error = "cannot read the file";
    return false;
  }
  bool ReadHead(std::size_t n, std::vector<std::uint8_t> &out) override {
    FILE *f = fio::Open(path_, "rb");
    if (!f) return false;
    const std::uint64_t size = fio::Size(f);
    const bool ok = fio::ReadRange(f, 0, static_cast<std::size_t>(std::min<std::uint64_t>(size, n)), out);
    std::fclose(f);
    return ok;
  }
  std::string DiskPath() const override { return path_; }

 private:
  std::string path_;
};

class ZipReader : public Reader {
 public:
  ZipReader(std::shared_ptr<zipr::Archive> archive, std::size_t index) : archive_(std::move(archive)), index_(index) {}
  bool ReadAll(std::vector<std::uint8_t> &out, std::string &error) override {
    return archive_->Read(archive_->Entries()[index_], out, error);
  }
  bool ReadHead(std::size_t n, std::vector<std::uint8_t> &out) override {
    std::string error;
    return archive_->Read(archive_->Entries()[index_], out, error, n);
  }

 private:
  std::shared_ptr<zipr::Archive> archive_;
  std::size_t index_;
};

bool IsGameId(const std::string &name) {
  if (name.size() != 6) return false;
  for (char c : name)
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  return true;
}

std::vector<std::string> Parts(const std::string &rel) {
  std::vector<std::string> out;
  std::size_t at = 0;
  for (;;) {
    const std::size_t slash = rel.find('/', at);
    out.push_back(rel.substr(at, slash == std::string::npos ? std::string::npos : slash - at));
    if (slash == std::string::npos) break;
    at = slash + 1;
  }
  return out;
}

// the order a folder walk takes: folder by folder, names sorted
void SortFiles(std::vector<InputFile> &files) {
  std::stable_sort(files.begin(), files.end(),
                   [](const InputFile &a, const InputFile &b) { return Parts(a.rel) < Parts(b.rel); });
}

void Walk(const fs::path &dir, const std::string &prefix, std::vector<InputFile> &out) {
  std::error_code ec;
  std::vector<fs::path> entries;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
  for (const fs::path &p : entries) {
    std::error_code e2;
    const std::string name = fio::Utf8(p.filename());
    if (fs::is_directory(p, e2)) {
      Walk(p, prefix + name + "/", out);
      continue;
    }
    InputFile f;
    f.rel = prefix + name;
    f.bytes = fs::file_size(p, e2);
    f.reader = std::make_shared<DiskReader>(fio::Utf8(p));
    out.push_back(std::move(f));
  }
}

}  // namespace

std::vector<InputFile> ListFolder(const std::string &root) {
  std::vector<InputFile> files;
  Walk(fio::Path(root), "", files);
  SortFiles(files);
  return files;
}

bool ListZip(std::shared_ptr<zipr::RandomAccess> access, const std::string &zipName, std::vector<InputFile> &files,
             std::string &packName, std::string &error) {
  auto archive = std::make_shared<zipr::Archive>();
  if (!archive->Open(std::move(access), error)) return false;
  files.clear();
  const std::vector<zipr::Entry> &entries = archive->Entries();
  for (std::size_t i = 0; i < entries.size(); i++) {
    const zipr::Entry &e = entries[i];
    if (e.name.empty() || e.name.back() == '/' || e.name.rfind("__MACOSX/", 0) == 0) continue;
    InputFile f;
    f.rel = e.name;
    f.bytes = e.bytes;
    f.reader = std::make_shared<ZipReader>(archive, i);
    files.push_back(std::move(f));
  }
  // one top folder for all: the pack's folder
  std::string top;
  bool common = !files.empty();
  for (const InputFile &f : files) {
    const std::size_t slash = f.rel.find('/');
    if (slash == std::string::npos) {
      common = false;
      break;
    }
    const std::string first = f.rel.substr(0, slash);
    if (top.empty()) top = first;
    else if (top != first) {
      common = false;
      break;
    }
  }
  if (common && !IsGameId(top)) {
    for (InputFile &f : files) f.rel = f.rel.substr(top.size() + 1);
    packName = top;
  } else {
    packName = zipName;
    const std::string low = Lower(packName);
    if (low.size() > 4 && low.compare(low.size() - 4, 4, ".zip") == 0) packName.resize(packName.size() - 4);
  }
  SortFiles(files);
  return true;
}



// ---------------- the pack ----------------

namespace {

bool Classify(const InputFile &f, const std::string &rel, Source &s, std::vector<Skipped> *skipped) {
  const std::string fileName = rel.substr(rel.rfind('/') == std::string::npos ? 0 : rel.rfind('/') + 1);
  if (!IsTextureFile(fileName)) {
    const std::string fn = Lower(fileName);
    if (skipped && fn != "desktop.ini" && fn != "thumbs.db") skipped->push_back({rel, "not a DDS, PNG or JPG file"});
    return false;
  }
  s.file = f;
  s.name = Name{};
  if (!ParseName(fileName, s.name)) {
    if (skipped) skipped->push_back({rel, "the name is not a Dolphin texture name (a copy such as \"(1)\" or \"- Copy\"?)"});
    return false;
  }
  if (s.name.mipFile) {
    if (skipped) skipped->push_back({rel, "a separate mip level file (the tool makes the levels itself)"});
    return false;
  }
  return true;
}

bool SameAsMain(const Variant &variant, const std::map<Key, const Source *> &main) {
  if (variant.files.empty()) return false;
  for (const Source &s : variant.files) {
    auto it = main.find(s.name.key);
    if (it == main.end() || it->second->file.bytes != s.file.bytes) return false;
    std::vector<std::uint8_t> a, b;
    std::string e;
    if (!s.file.reader->ReadAll(a, e) || !it->second->file.reader->ReadAll(b, e) || a != b) return false;
  }
  return true;
}

}  // namespace

void Prepare(std::vector<InputFile> files, const std::string &name, Pack &pack) {
  pack = Pack{};
  pack.name = name;
  SortFiles(files);
  // the top folders: game IDs are the pack, the others choices (else only Extra/Extras/Optional are choices)
  std::vector<std::string> tops;
  for (const InputFile &f : files) {
    const std::size_t slash = f.rel.find('/');
    if (slash != std::string::npos) {
      const std::string t = f.rel.substr(0, slash);
      if (tops.empty() || tops.back() != t) tops.push_back(t);
    }
  }
  std::set<std::string> gameDirs, optional;
  for (const std::string &t : tops)
    if (IsGameId(t)) gameDirs.insert(t);
  for (const std::string &t : tops) {
    const std::string low = Lower(t);
    if (gameDirs.empty() ? (low == "extra" || low == "extras" || low == "optional") : !gameDirs.count(t)) optional.insert(t);
  }
  for (const InputFile &f : files) {
    const std::size_t slash = f.rel.find('/');
    const std::string top = slash == std::string::npos ? std::string() : f.rel.substr(0, slash);
    const bool isMain = gameDirs.empty() ? !optional.count(top) : gameDirs.count(top) != 0;
    if (!isMain) continue;
    Source s;
    if (!Classify(f, f.rel, s, &pack.skipped)) continue;
    pack.bytes += f.bytes;
    pack.files.push_back(std::move(s));
  }
  // duplicates: a DDS before a PNG before a JPG, else the first in name order
  for (const Source &s : pack.files) {
    auto it = pack.byKey.find(s.name.key);
    if (it == pack.byKey.end()) {
      pack.byKey[s.name.key] = &s;
    } else if (s.name.kind < it->second->name.kind) {
      pack.duplicates.push_back({it->second->file.rel, std::string("the same texture again (its ") + KindName(s.name.kind) + " is used)"});
      it->second = &s;
    } else {
      pack.duplicates.push_back({s.file.rel, "the same texture again"});
    }
  }
  // groups: a folder in a choice folder; its sub-folders with images are the variants, else it is an add-on
  for (const std::string &o : optional) {
    std::vector<std::string> groupNames;
    for (const InputFile &f : files) {
      const std::vector<std::string> parts = Parts(f.rel);
      if (parts.size() >= 3 && parts[0] == o && (groupNames.empty() || groupNames.back() != parts[1]))
        groupNames.push_back(parts[1]);
    }
    for (const std::string &g : groupNames) {
      Group group;
      group.name = g;
      std::vector<std::string> variantNames;
      bool direct = false;
      for (const InputFile &f : files) {
        const std::vector<std::string> parts = Parts(f.rel);
        if (parts.size() < 3 || parts[0] != o || parts[1] != g || !IsTextureFile(parts.back())) continue;
        if (parts.size() >= 4) {
          if (variantNames.empty() || variantNames.back() != parts[2]) variantNames.push_back(parts[2]);
        } else {
          direct = true;
        }
      }
      for (const std::string &v : variantNames) group.variants.push_back({v, o + "/" + g + "/" + v + "/", false, {}});
      if (group.variants.empty() && direct) {
        group.addOn = true;
        group.variants.push_back({g, o + "/" + g + "/", false, {}});
      }
      if (group.variants.empty()) continue;
      for (Variant &v : group.variants) {
        for (const InputFile &f : files) {
          if (f.rel.compare(0, v.prefix.size(), v.prefix) != 0) continue;
          Source s;
          if (Classify(f, f.rel, s, nullptr)) v.files.push_back(std::move(s));
        }
        v.sameAsMain = SameAsMain(v, pack.byKey);
      }
      pack.groups.push_back(std::move(group));
    }
  }
}

bool Choose(Pack &pack, const std::vector<std::string> &specs, std::string &bad) {
  for (const std::string &spec : specs) {
    const std::size_t eq = spec.find('=');
    auto trim = [](std::string t) {
      while (!t.empty() && t.back() == ' ') t.pop_back();
      while (!t.empty() && t.front() == ' ') t.erase(0, 1);
      return t;
    };
    const std::string gname = Lower(trim(spec.substr(0, eq))),
                      vname = eq == std::string::npos ? "" : Lower(trim(spec.substr(eq + 1)));
    bool found = false;
    for (Group &g : pack.groups) {
      if (Lower(g.name).rfind(gname, 0) != 0) continue;
      for (std::size_t v = 0; v < g.variants.size(); v++)
        if (g.addOn ? (vname == "on" || vname == "yes" || vname.empty()) : Lower(g.variants[v].name) == vname) {
          g.chosen = static_cast<int>(v);
          found = true;
        }
    }
    if (!found) {
      bad = spec;
      return false;
    }
  }
  return true;
}

std::vector<const Source *> Todo(const Pack &pack, const Settings &settings, std::string &choices) {
  std::map<Key, const Source *> byKey = pack.byKey;
  choices.clear();
  for (const Group &g : pack.groups) {
    if (!choices.empty()) choices += "; ";
    choices += g.name + ": " +
               (g.chosen < 0 ? std::string(g.addOn ? "no" : "as in the main folder")
                             : g.addOn ? std::string("yes") : g.variants[static_cast<std::size_t>(g.chosen)].name);
    if (g.chosen < 0) continue;
    for (const Source &s : g.variants[static_cast<std::size_t>(g.chosen)].files) byKey[s.name.key] = &s;
  }
  std::vector<const Source *> todo;
  for (auto &kv : byKey) todo.push_back(kv.second);
  if (settings.limit > 0 && todo.size() > static_cast<std::size_t>(settings.limit))
    todo.resize(static_cast<std::size_t>(settings.limit));
  return todo;
}

// ---------------- converting ----------------

namespace {

constexpr std::size_t kHeadBytes = 1u << 20;  // enough for a JPEG's frame header behind big APP segments

// the image file's size at its level `maxSide` (0: the first) and what a texel needs
bool ProbeSource(const Source &src, unsigned maxSide, unsigned &w, unsigned &h, int *bits) {
  const std::string path = src.file.reader->DiskPath();
  if (!path.empty() && !bits) return img::Probe(path, maxSide, w, h);
  // a DDS or PNG needs its first 148 bytes, a JPEG maybe more
  std::vector<std::uint8_t> head;
  if (!src.file.reader->ReadHead(148, head)) return false;
  if (head.size() >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF && src.file.bytes > head.size() &&
      !src.file.reader->ReadHead(kHeadBytes, head))
    return false;
  return img::ProbeData(head.data(), head.size(), src.file.bytes, maxSide, w, h, bits);
}

struct Target {
  unsigned w = 0, h = 0, load = 0;
};

// The size an image is brought to: its longer side at most maxSide, but never below the game's own texture (the size
// in the Dolphin name) in either direction (decision of the project owner, 12.11.2026); an HD image already smaller
// than the original keeps its size. Also the longer side to ask the DDS reader for (its last mip level at least that big).
Target TargetFor(const Source &src, unsigned maxSide, unsigned fw, unsigned fh) {
  Target t;
  if (!fw || !fh) {
    t.load = std::max(maxSide, std::max(src.name.key.w, src.name.key.h));
    return t;
  }
  double scale = std::max(fw, fh) > maxSide ? static_cast<double>(maxSide) / std::max(fw, fh) : 1.0;
  scale = std::max(scale, std::max(static_cast<double>(src.name.key.w) / fw, static_cast<double>(src.name.key.h) / fh));
  scale = std::min(scale, 1.0);
  t.w = std::max(1u, static_cast<unsigned>(std::lround(fw * scale)));
  t.h = std::max(1u, static_cast<unsigned>(std::lround(fh * scale)));
  t.load = std::max(t.w, t.h);
  return t;
}

Target TargetFor(const Source &src, unsigned maxSide) {
  unsigned fw = 0, fh = 0;
  if (!ProbeSource(src, 0, fw, fh, nullptr)) fw = fh = 0;
  return TargetFor(src, maxSide, fw, fh);
}

struct Result {
  bool ok = false;
  std::string error;
  c4tex::Entry entry{};
  std::vector<std::uint8_t> data;
  double psnr = 0;
};

double Psnr(const img::Image &a, const img::Image &b, bool alpha) {
  double se = 0;
  const std::size_t n = static_cast<std::size_t>(a.w) * a.h;
  const int channels = alpha ? 4 : 3;
  for (std::size_t i = 0; i < n; i++)
    for (int c = 0; c < channels; c++) {
      const double d = static_cast<double>(a.px[i * 4 + c]) - b.px[i * 4 + c];
      se += d * d;
    }
  const double mse = se / (static_cast<double>(n) * channels);
  return mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

Result ConvertOne(const Source &src, unsigned maxSide, int effort) {
  Result r;
  img::Loaded loaded;
  const Target target = TargetFor(src, maxSide);
  const std::string path = src.file.reader->DiskPath();
  if (!path.empty()) {
    if (!img::Load(path, target.load, src.name.arb, loaded, r.error)) return r;
  } else {
    std::vector<std::uint8_t> data;
    if (!src.file.reader->ReadAll(data, r.error)) return r;
    if (!img::LoadData(data.data(), data.size(), target.load, src.name.arb, loaded, r.error)) return r;
  }
  img::Image top = std::move(loaded.levels[0]);
  unsigned w = top.w, h = top.h;
  // never larger than the level read (no enlarging)
  const unsigned tw = target.w ? std::min(target.w, w) : w, th = target.h ? std::min(target.h, h) : h;
  if (tw != w || th != h) {
    w = tw;
    h = th;
    top = img::Downscale(top, w, h);
  }
  etc2::Kind kind = etc2::kRgb;
  if (loaded.alpha == img::Alpha::kBinary) kind = etc2::kRgbA1;
  if (loaded.alpha == img::Alpha::kSoft) kind = etc2::kRgbaEac;
  std::vector<img::Image> levels;
  levels.push_back(std::move(top));
  if (src.name.arb) {
    // the pack's own levels, each brought to the chain's size
    for (std::size_t i = 1; i < loaded.levels.size(); i++) {
      const unsigned lw = std::max(1u, w >> i), lh = std::max(1u, h >> i);
      img::Image &l = loaded.levels[i];
      levels.push_back(l.w == lw && l.h == lh ? std::move(l) : img::Downscale(l, lw, lh));
      if (lw == 1 && lh == 1) break;
    }
  } else {
    const double coverage = kind == etc2::kRgbA1 ? img::Coverage(levels[0], etc2::kAlphaThreshold) : 0;
    while (levels.back().w > 1 || levels.back().h > 1) {
      img::Image next = img::HalfSize(levels.back());
      if (kind == etc2::kRgbA1) img::KeepCoverage(next, etc2::kAlphaThreshold, coverage);
      levels.push_back(std::move(next));
    }
  }
  for (const img::Image &l : levels) etc2::EncodeLevel(kind, l.px.data(), l.w, l.h, effort, r.data);
  // quality of the first level as the phone will show it
  {
    img::Image back;
    back.Resize(levels[0].w, levels[0].h);
    etc2::DecodeLevel(kind, r.data.data(), back.w, back.h, back.px.data());
    img::Image want = levels[0];
    if (kind == etc2::kRgbA1)
      for (std::size_t i = 0; i < want.px.size(); i += 4) {
        if (want.px[i + 3] < etc2::kAlphaThreshold) {
          want.px[i] = want.px[i + 1] = want.px[i + 2] = want.px[i + 3] = 0;
        } else {
          want.px[i + 3] = 255;
        }
      }
    if (kind == etc2::kRgb)
      for (std::size_t i = 0; i < want.px.size(); i += 4) want.px[i + 3] = 255;
    r.psnr = Psnr(want, back, kind != etc2::kRgb);
  }
  c4tex::Entry &e = r.entry;
  e.texHash = src.name.key.tex;
  e.tlutHash = src.name.key.tlut;
  e.origWidth = static_cast<std::uint16_t>(src.name.key.w);
  e.origHeight = static_cast<std::uint16_t>(src.name.key.h);
  e.gxFormat = static_cast<std::uint8_t>(src.name.key.fmt);
  e.flags = static_cast<std::uint8_t>((src.name.mip ? c4tex::kFlagMipName : 0) |
                                      (src.name.key.hasTlut ? c4tex::kFlagTlut : 0) |
                                      (src.name.key.tlutAny ? c4tex::kFlagTlutAny : 0) |
                                      (src.name.arb ? c4tex::kFlagArbitrary : 0));
  e.encoding = static_cast<std::uint8_t>(kind);
  e.levels = static_cast<std::uint8_t>(levels.size());
  e.width = static_cast<std::uint16_t>(w);
  e.height = static_cast<std::uint16_t>(h);
  e.dataBytes = static_cast<std::uint32_t>(r.data.size());
  e.dataCrc = c4tex::Crc32(r.data.data(), r.data.size());
  if (r.data.size() != c4tex::ImageBytes(kind, w, h, e.levels)) {
    r.error = "internal: data size";
    return r;
  }
  r.ok = true;
  return r;
}

// memory one conversion needs at most, from the decoded level's size (and a whole file in memory when not on disk)
std::uint64_t MemoryFor(const Source &s, unsigned maxSide) {
  const Target t = TargetFor(s, maxSide);
  unsigned w = 0, h = 0;
  if (!ProbeSource(s, t.load, w, h, nullptr)) return 64ull << 20;
  const std::uint64_t px = static_cast<std::uint64_t>(w) * h;
  const std::uint64_t out = std::max<std::uint64_t>(t.load, 1);
  const std::uint64_t file = s.file.reader->DiskPath().empty() ? s.file.bytes * 2 : 0;
  return px * 4 * 2 + std::min<std::uint64_t>(w, out) * h * 28 + 4ull * out * out * 3 + file + (8ull << 20);
}

}  // namespace

std::vector<std::uint64_t> EstimateSizes(const std::vector<const Source *> &todo, const std::vector<unsigned> &sizes) {
  std::vector<std::vector<std::uint64_t>> each(todo.size(), std::vector<std::uint64_t>(sizes.size(), 0));
  std::atomic<std::size_t> next{0};
  auto work = [&] {
    for (;;) {
      const std::size_t i = next++;
      if (i >= todo.size()) return;
      const Source &s = *todo[i];
      unsigned fw = 0, fh = 0;
      int bits = 8;
      const bool known = ProbeSource(s, 0, fw, fh, &bits);
      for (std::size_t k = 0; k < sizes.size(); k++) {
        unsigned w = std::min(sizes[k], 1024u), h = w;
        if (known) {
          const Target t = TargetFor(s, sizes[k], fw, fh);
          w = t.w ? t.w : fw;
          h = t.h ? t.h : fh;
        }
        const unsigned encoding = !known || bits > 4 ? c4tex::kEtc2RgbaEac : c4tex::kEtc2Rgb;
        each[i][k] = c4tex::ImageBytes(encoding, w, h, c4tex::FullChain(w, h)) + c4tex::kAlign + c4tex::kEntryBytes;
      }
    }
  };
  std::vector<std::thread> pool;
  const unsigned threads = std::min<unsigned>(8, std::max(1u, std::thread::hardware_concurrency()));
  for (unsigned t = 0; t < threads; t++) pool.emplace_back(work);
  for (std::thread &t : pool) t.join();
  std::vector<std::uint64_t> total(sizes.size(), 4096);
  for (const auto &v : each)
    for (std::size_t k = 0; k < sizes.size(); k++) total[k] += v[k];
  return total;
}

std::uint64_t Estimate(const std::vector<const Source *> &todo, unsigned size) { return EstimateSizes(todo, {size})[0]; }

Outcome Convert(const Pack &pack, const std::vector<const Source *> &todo, const std::string &choices,
                const Settings &o, Sink &sink, const std::function<void(const Progress &)> &progress,
                const std::atomic<bool> *cancel) {
  Outcome out;
  const auto start = std::chrono::steady_clock::now();
  const std::string baseName = "CubeEvil4-textures-" + std::to_string(o.size);
  // header and info first (the header again at the end)
  std::string info;
  info += "name=" + baseName + "\n";
  info += std::string("tool=") + kToolName + " " + kToolVersion + "\n";
  info += "created=" + o.date + "\n";
  info += "size=" + std::to_string(o.size) + "\n";
  info += "source=" + pack.name + "\n";
  if (!choices.empty()) info += "choices=" + choices + "\n";
  const std::uint64_t infoOffset = c4tex::kHeaderBytes;
  // images=... comes last, written when known: its room reserved
  info += "images=          \n";
  std::uint64_t at = infoOffset + info.size();
  const std::uint64_t dataOffset = (at + c4tex::kAlign - 1) / c4tex::kAlign * c4tex::kAlign;
  std::vector<std::uint8_t> zeros(c4tex::kAlign, 0);
  c4tex::Header header{};
  bool ok = sink.Write(&header, sizeof(header)) && sink.Write(info.data(), info.size()) &&
            sink.Write(zeros.data(), static_cast<std::size_t>(dataOffset - at));
  at = dataOffset;
  const unsigned threads = o.threads > 0 ? o.threads : std::max(1u, std::thread::hardware_concurrency());
  const std::uint64_t budget = std::max<std::uint64_t>(o.memoryBudget, 256ull << 20);
  std::mutex mu;
  std::condition_variable cv;
  std::size_t nextJob = 0, writerAt = 0;  // workers stay at most 512 images ahead of the writer
  std::uint64_t inUse = 0;
  std::map<std::size_t, Result> done;
  std::atomic<bool> stop{false};
  auto stopped = [&] { return stop.load() || (cancel && cancel->load()); };
  auto worker = [&] {
    for (;;) {
      std::size_t j;
      {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return nextJob < writerAt + 512 || stopped(); });
        if (nextJob >= todo.size() || stopped()) return;
        j = nextJob++;
      }
      const std::uint64_t need = std::min(MemoryFor(*todo[j], o.size), budget);
      {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return inUse + need <= budget || inUse == 0 || stopped(); });
        inUse += need;
      }
      Result r;
      if (!stopped()) r = ConvertOne(*todo[j], o.size, o.effort);
      {
        std::unique_lock<std::mutex> lock(mu);
        inUse -= need;
        done.emplace(j, std::move(r));
      }
      cv.notify_all();
    }
  };
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < threads; t++) pool.emplace_back(worker);
  std::vector<c4tex::Entry> entries;
  std::vector<Skipped> failed;
  std::vector<std::pair<double, std::string>> worst;
  double psnrSum = 0;
  std::size_t written = 0;
  for (std::size_t j = 0; j < todo.size() && ok; j++) {
    Result r;
    {
      std::unique_lock<std::mutex> lock(mu);
      cv.wait(lock, [&] { return done.count(j) != 0 || (cancel && cancel->load()); });
      if (!done.count(j)) break;
      r = std::move(done[j]);
      done.erase(j);
      writerAt = j + 1;
    }
    cv.notify_all();
    if (cancel && cancel->load()) break;
    const std::string &rel = todo[j]->file.rel;
    if (!r.ok) {
      failed.push_back({rel, r.error});
    } else {
      r.entry.dataOffset = at;
      ok = sink.Write(r.data.data(), r.data.size());
      at += r.data.size();
      const std::uint64_t pad = (c4tex::kAlign - at % c4tex::kAlign) % c4tex::kAlign;
      ok = ok && sink.Write(zeros.data(), static_cast<std::size_t>(pad));
      at += pad;
      out.dataBytes += r.data.size();
      entries.push_back(r.entry);
      out.counts[r.entry.encoding]++;
      psnrSum += r.psnr;
      worst.push_back({r.psnr, rel});
      written++;
      if (written % 256 == 0 && sink.Free() < (64ull << 20)) {
        out.diskFull = true;
        ok = false;
      }
    }
    if (progress) {
      Progress p;
      p.done = j + 1;
      p.total = todo.size();
      p.written = at;
      p.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      progress(p);
    }
  }
  stop = true;
  cv.notify_all();
  for (std::thread &t : pool) t.join();
  out.threads = threads;
  if (cancel && cancel->load()) {
    out.cancelled = true;
    out.error = "cancelled";
    return out;
  }
  // index, info and header
  std::sort(entries.begin(), entries.end(), c4tex::EntryLess);
  const std::uint64_t indexOffset = at;
  ok = ok && sink.Write(entries.data(), entries.size() * sizeof(c4tex::Entry));
  at += entries.size() * sizeof(c4tex::Entry);
  {
    std::string count = std::to_string(entries.size());
    count.resize(10, ' ');
    const std::size_t pos = info.find("images=          \n");
    info.replace(pos + 7, 10, count);
  }
  header = c4tex::Header{};
  std::memcpy(header.magic, c4tex::kMagic, 8);
  header.version = c4tex::kVersion;
  header.headerBytes = c4tex::kHeaderBytes;
  header.entryCount = static_cast<std::uint32_t>(entries.size());
  header.entryBytes = c4tex::kEntryBytes;
  header.indexOffset = indexOffset;
  header.dataOffset = dataOffset;
  header.fileBytes = at;
  header.maxSide = o.size;
  header.indexCrc = c4tex::Crc32(entries.data(), entries.size() * sizeof(c4tex::Entry));
  header.infoOffset = static_cast<std::uint32_t>(infoOffset);
  header.infoBytes = static_cast<std::uint32_t>(info.size());
  header.headerCrc = c4tex::HeaderCrc(header);
  ok = ok && sink.WriteAt(0, &header, sizeof(header)) && sink.WriteAt(infoOffset, info.data(), info.size());
  out.entries = entries.size();
  out.failed = failed.size();
  out.skipped = pack.skipped.size() + pack.duplicates.size();
  out.fileBytes = at;
  out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (!ok || entries.empty()) {
    out.error = out.diskFull ? "the disk is full" : entries.empty() ? "no image could be converted" : "writing the file failed";
    return out;
  }
  // the report next to the pack
  std::sort(worst.begin(), worst.end());
  std::string &report = out.report;
  char line[512];
  std::snprintf(line, sizeof(line), "%s %s, %s\nSource: %s\nChoices: %s\nSize: %u\n", kToolName, kToolVersion,
                o.date.c_str(), pack.name.c_str(), choices.empty() ? "-" : choices.c_str(), o.size);
  report += line;
  std::snprintf(line, sizeof(line),
                "Images: %zu converted (%zu opaque, %zu with cut-out transparency, %zu with soft transparency), %zu failed, %zu skipped\n",
                entries.size(), out.counts[1], out.counts[2], out.counts[3], failed.size(), out.skipped);
  report += line;
  std::snprintf(line, sizeof(line), "File: %s (%s), image data %s\nTime: %s, %u threads, effort %d\n",
                o.outName.c_str(), Bytes(at).c_str(), Bytes(out.dataBytes).c_str(), Duration(out.seconds).c_str(),
                threads, o.effort);
  report += line;
  std::snprintf(line, sizeof(line), "Quality of the first level after ETC2: average %.1f dB PSNR\n",
                written ? psnrSum / written : 0.0);
  report += line;
  report += "Lowest quality (dB, file):\n";
  for (std::size_t i = 0; i < std::min<std::size_t>(10, worst.size()); i++) {
    std::snprintf(line, sizeof(line), "  %.1f  %s\n", worst[i].first, worst[i].second.c_str());
    report += line;
  }
  for (const Skipped &s : failed) report += "Failed: " + s.path + ": " + s.reason + "\n";
  for (const Skipped &s : pack.duplicates) report += "Skipped: " + s.path + ": " + s.reason + "\n";
  for (const Skipped &s : pack.skipped) report += "Skipped: " + s.path + ": " + s.reason + "\n";
  out.ok = true;
  return out;
}

}  // namespace pack
