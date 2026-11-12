// CubeEvil4 Texture Packer: turns a Dolphin texture pack folder (tex1_<W>x<H>[_m]_<hash>[_<tlut>]_<fmt>.dds/.png)
// into one CubeEvil4 texture pack file (.c4tex): shrunk to 512 or 1024, new mip levels, ETC2 for phones.
// Plan: port/docs/textur-lader-plan.md; file layout: c4tex.h, port/docs/textur-paket-format.md.
// It only reads the pack folder and never writes into it. Our own code (CC0).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "c4tex.h"
#include "etc2.h"
#include "fileio.h"
#include "image.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

const char *kToolName = "CubeEvil4 Texture Packer";
const char *kToolVersion = "1.0";

// ---------------- console ----------------

bool gInteractive = false;

void Print(const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  std::fputs(buf, stdout);
  std::fflush(stdout);
}

bool StdoutIsConsole() {
#ifdef _WIN32
  DWORD mode;
  return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
#else
  return isatty(1);
#endif
}

std::string ReadLine() {
#ifdef _WIN32
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  if (GetConsoleMode(in, &mode)) {
    std::wstring w;
    wchar_t buf[512];
    DWORD got = 0;
    for (;;) {
      if (!ReadConsoleW(in, buf, 512, &got, nullptr) || got == 0) break;
      w.append(buf, got);
      if (!w.empty() && w.back() == L'\n') break;
    }
    while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r')) w.pop_back();
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
  }
#endif
  std::string s;
  int c;
  while ((c = std::fgetc(stdin)) != EOF && c != '\n') s.push_back(static_cast<char>(c));
  while (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '"')) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '"')) i++;
  s = s.substr(i);
  // PowerShell's drag and drop gives & 'path'
  if (s.size() > 2 && s[0] == '&' && s[1] == ' ') s = Trim(s.substr(2));
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') s = s.substr(1, s.size() - 2);
  return s;
}

std::string Lower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

int AskNumber(const char *prompt, int lo, int hi, int deflt) {
  for (;;) {
    Print("%s [%d]: ", prompt, deflt);
    const std::string s = Trim(ReadLine());
    if (s.empty()) return deflt;
    char *end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end && *end == '\0' && v >= lo && v <= hi) return static_cast<int>(v);
    Print("Please type a number from %d to %d.\n", lo, hi);
  }
}

bool AskYes(const char *prompt) {
  Print("%s (y/n): ", prompt);
  const std::string s = Lower(Trim(ReadLine()));
  return s == "y" || s == "yes" || s == "j" || s == "ja";
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

std::string DesktopFolder() {
#ifdef _WIN32
  PWSTR path = nullptr;
  std::string out;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path))) out = fio::Utf8(fs::path(path));
  CoTaskMemFree(path);
  if (!out.empty()) return out;
#endif
  const char *home = std::getenv("HOME");
  return home ? home : ".";
}

std::uint64_t PhysicalMemory() {
#ifdef _WIN32
  MEMORYSTATUSEX m;
  m.dwLength = sizeof(m);
  if (GlobalMemoryStatusEx(&m)) return m.ullTotalPhys;
  return 8ull << 30;
#else
  const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
  return pages > 0 && size > 0 ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(size) : 8ull << 30;
#endif
}

// ---------------- names ----------------

struct Key {
  std::uint64_t tex = 0, tlut = 0;
  unsigned w = 0, h = 0, fmt = 0;
  bool hasTlut = false, tlutAny = false;
  bool mip = false;  // "_m": a texture can be there twice, with and without (Dolphin keeps them apart)
  bool operator<(const Key &o) const {
    if (tex != o.tex) return tex < o.tex;
    if (tlut != o.tlut) return tlut < o.tlut;
    if (w != o.w) return w < o.w;
    if (h != o.h) return h < o.h;
    if (fmt != o.fmt) return fmt < o.fmt;
    if (mip != o.mip) return mip < o.mip;
    return tlutAny < o.tlutAny;
  }
};

struct Name {
  Key key;
  bool mip = false, arb = false, mipFile = false;
  bool dds = false;
};

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

// tex1_<W>x<H>[_m]_<16 hex>[_<16 hex>|_$]_<fmt>[_mip<N>][_arb].(dds|png), any case
bool ParseName(const std::string &fileName, Name &n) {
  std::string s = Lower(fileName);
  if (s.size() > 4 && s.compare(s.size() - 4, 4, ".dds") == 0)
    n.dds = true;
  else if (!(s.size() > 4 && s.compare(s.size() - 4, 4, ".png") == 0))
    return false;
  s.resize(s.size() - 4);
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

// ---------------- the pack folder ----------------

struct Source {
  std::string path;  // UTF-8
  Name name;
  std::uint64_t bytes = 0;
};

struct Skipped {
  std::string path, reason;
};

struct Scan {
  std::vector<Source> files;
  std::vector<Skipped> skipped;
  std::uint64_t bytes = 0;
};

bool IsTextureFile(const fs::path &p) {
  const std::string e = Lower(fio::Utf8(p.extension()));
  return e == ".dds" || e == ".png";
}

void ScanFolder(const fs::path &dir, const std::set<fs::path> &exclude, Scan &out, const fs::path &root) {
  std::error_code ec;
  std::vector<fs::path> entries;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
  std::sort(entries.begin(), entries.end());
  for (const fs::path &p : entries) {
    std::error_code e2;
    if (fs::is_directory(p, e2)) {
      if (!exclude.count(p)) ScanFolder(p, exclude, out, root);
      continue;
    }
    const std::string rel = fio::Utf8(fs::relative(p, root, e2));
    if (!IsTextureFile(p)) {
      const std::string fn = Lower(fio::Utf8(p.filename()));
      if (fn != "desktop.ini" && fn != "thumbs.db") out.skipped.push_back({rel, "not a DDS or PNG file"});
      continue;
    }
    Source s;
    s.path = fio::Utf8(p);
    if (!ParseName(fio::Utf8(p.filename()), s.name)) {
      out.skipped.push_back({rel, "the name is not a Dolphin texture name (a copy such as \"(1)\" or \"- Copy\"?)"});
      continue;
    }
    if (s.name.mipFile) {
      out.skipped.push_back({rel, "a separate mip level file (the tool makes the levels itself)"});
      continue;
    }
    s.bytes = fs::file_size(p, e2);
    out.bytes += s.bytes;
    out.files.push_back(std::move(s));
  }
}

bool HasTextures(const fs::path &dir) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_regular_file(ec) && IsTextureFile(it->path())) return true;
  return false;
}

bool IsGameId(const std::string &name) {
  if (name.size() != 6) return false;
  for (char c : name)
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  return true;
}

struct Variant {
  std::string name;
  fs::path dir;
  bool sameAsMain = false;
};

struct Group {
  std::string name;
  std::vector<Variant> variants;  // one "add-on" variant when the group holds files directly
  bool addOn = false;
  int chosen = -1;                // -1: as in the main folder
};

struct Layout {
  std::vector<fs::path> main;
  std::vector<Group> groups;
  std::set<fs::path> optional;  // excluded from the main scan
};

std::vector<fs::path> SubDirs(const fs::path &dir) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_directory(ec)) out.push_back(it->path());
  std::sort(out.begin(), out.end());
  return out;
}

void AddGroups(const fs::path &dir, Layout &layout) {
  for (const fs::path &g : SubDirs(dir)) {
    Group group;
    group.name = fio::Utf8(g.filename());
    for (const fs::path &v : SubDirs(g))
      if (HasTextures(v)) group.variants.push_back({fio::Utf8(v.filename()), v, false});
    if (group.variants.empty() && HasTextures(g)) {
      group.addOn = true;
      group.variants.push_back({group.name, g, false});
    }
    if (!group.variants.empty()) layout.groups.push_back(std::move(group));
  }
}

Layout FindLayout(const fs::path &root) {
  Layout layout;
  std::vector<fs::path> others;
  for (const fs::path &d : SubDirs(root)) {
    if (IsGameId(fio::Utf8(d.filename())))
      layout.main.push_back(d);
    else
      others.push_back(d);
  }
  if (!layout.main.empty()) {
    // as Dolphin: the game's folder is the pack, the other folders hold choices (HD Project: "Extra")
    for (const fs::path &o : others) {
      layout.optional.insert(o);
      AddGroups(o, layout);
    }
    return layout;
  }
  layout.main.push_back(root);
  for (const fs::path &o : others) {
    const std::string n = Lower(fio::Utf8(o.filename()));
    if (n == "extra" || n == "extras" || n == "optional") {
      layout.optional.insert(o);
      AddGroups(o, layout);
    }
  }
  return layout;
}

// a variant whose every file lies in the main folder with the same bytes
bool SameAsMain(const Scan &variant, const std::map<Key, const Source *> &main) {
  if (variant.files.empty()) return false;
  for (const Source &s : variant.files) {
    auto it = main.find(s.name.key);
    if (it == main.end() || it->second->bytes != s.bytes) return false;
    std::vector<std::uint8_t> a, b;
    if (!fio::ReadFile(s.path, a) || !fio::ReadFile(it->second->path, b) || a != b) return false;
  }
  return true;
}

// ---------------- converting ----------------

struct Job {
  const Source *source = nullptr;
};

struct Result {
  bool ok = false;
  std::string error;
  c4tex::Entry entry{};
  std::vector<std::uint8_t> data;
  double psnr = 0;
  std::string format;
  unsigned srcW = 0, srcH = 0;
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

Result Convert(const Source &src, unsigned maxSide, int effort) {
  Result r;
  img::Loaded loaded;
  if (!img::Load(src.path, maxSide, src.name.arb, loaded, r.error)) return r;
  r.format = loaded.format;
  r.srcW = loaded.fileWidth;
  r.srcH = loaded.fileHeight;
  img::Image top = std::move(loaded.levels[0]);
  unsigned w = top.w, h = top.h;
  if (std::max(w, h) > maxSide) {
    if (w >= h) {
      h = std::max(1u, static_cast<unsigned>(std::lround(static_cast<double>(h) * maxSide / w)));
      w = maxSide;
    } else {
      w = std::max(1u, static_cast<unsigned>(std::lround(static_cast<double>(w) * maxSide / h)));
      h = maxSide;
    }
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

// memory one conversion needs at most, from the decoded level's size
std::uint64_t MemoryFor(const Source &s, unsigned maxSide) {
  unsigned w = 0, h = 0;
  if (!img::Probe(s.path, maxSide, w, h)) return 64ull << 20;
  const std::uint64_t px = static_cast<std::uint64_t>(w) * h;
  const std::uint64_t outW = std::min<std::uint64_t>(w, maxSide);
  return px * 4 * 2 + outW * h * 28 + 4ull * maxSide * maxSide * 3 + (8ull << 20);
}

struct Options {
  std::string in, out;
  unsigned size = 0;
  std::vector<std::string> variants;  // "Group=Variant"
  int threads = 0, effort = 1, limit = 0;
  bool yes = false;
  std::string check, png, key;
};

void Usage() {
  Print("%s %s\n\n", kToolName, kToolVersion);
  Print("Converts a texture pack in the Dolphin format into one file for CubeEvil4.\n");
  Print("Start it without anything to be asked step by step, or:\n\n");
  Print("  texpack -in <pack folder> -size 512|1024 [-out <folder or file.c4tex>]\n");
  Print("          [-variant \"<group>=<variant>\"] ... [-threads N] [-effort 0|1|2]\n");
  Print("  texpack -check <file.c4tex> [-png <folder>] [-limit N] [-key <texture hash>]\n\n");
  Print("The pack folder is only read, never changed. An existing output file is replaced.\n");
  Print("CubeEvil4 includes no textures; use only packs you obtained yourself and may use.\n");
}

bool WriteAll(FILE *f, const void *data, std::size_t n) { return n == 0 || std::fwrite(data, 1, n, f) == n; }

int Pack(Options &o) {
  const auto start = std::chrono::steady_clock::now();
  fs::path root = fio::Path(o.in);
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    Print("The folder \"%s\" was not found.\n", o.in.c_str());
    return 2;
  }
  root = fs::weakly_canonical(root, ec);
  Print("Looking at %s ...\n", fio::Utf8(root).c_str());
  Layout layout = FindLayout(root);
  Scan scan;
  for (const fs::path &m : layout.main) ScanFolder(m, layout.optional, scan, root);
  // duplicates: a DDS before a PNG, else the first in name order
  std::map<Key, const Source *> byKey;
  std::vector<Skipped> duplicates;
  for (const Source &s : scan.files) {
    auto it = byKey.find(s.name.key);
    if (it == byKey.end()) {
      byKey[s.name.key] = &s;
    } else if (s.name.dds && !it->second->name.dds) {
      duplicates.push_back({fio::Utf8(fs::relative(fio::Path(it->second->path), root, ec)), "the same texture again (a DDS of it is used)"});
      it->second = &s;
    } else {
      duplicates.push_back({fio::Utf8(fs::relative(fio::Path(s.path), root, ec)), "the same texture again"});
    }
  }
  if (byKey.empty()) {
    Print("No textures with Dolphin names (tex1_...) found in this folder.\n");
    return 2;
  }
  Print("Found %s images (%s)", Thousands(byKey.size()).c_str(), Bytes(scan.bytes).c_str());
  if (!layout.groups.empty()) Print(" and %zu group(s) of choices", layout.groups.size());
  Print(".\n");
  // the choices
  std::vector<Scan> variantScans;
  std::map<std::pair<std::size_t, std::size_t>, std::size_t> scanOf;
  for (std::size_t g = 0; g < layout.groups.size(); g++)
    for (std::size_t v = 0; v < layout.groups[g].variants.size(); v++) {
      Scan vs;
      ScanFolder(layout.groups[g].variants[v].dir, {}, vs, root);
      layout.groups[g].variants[v].sameAsMain = SameAsMain(vs, byKey);
      scanOf[{g, v}] = variantScans.size();
      variantScans.push_back(std::move(vs));
    }
  for (const std::string &spec : o.variants) {
    const std::size_t eq = spec.find('=');
    const std::string gname = Lower(Trim(spec.substr(0, eq))), vname = eq == std::string::npos ? "" : Lower(Trim(spec.substr(eq + 1)));
    bool found = false;
    for (Group &g : layout.groups) {
      if (Lower(g.name).rfind(gname, 0) != 0) continue;
      for (std::size_t v = 0; v < g.variants.size(); v++)
        if (g.addOn ? (vname == "on" || vname == "yes" || vname.empty()) : Lower(g.variants[v].name) == vname) {
          g.chosen = static_cast<int>(v);
          found = true;
        }
    }
    if (!found) {
      Print("The choice \"%s\" matches no group and variant of this pack.\n", spec.c_str());
      return 2;
    }
  }
  if (gInteractive)
    for (Group &g : layout.groups) {
      Print("\n%s:\n", g.name.c_str());
      if (g.addOn) {
        g.chosen = AskYes("  Add these images") ? 0 : -1;
        continue;
      }
      Print("  0 = as in the main folder\n");
      for (std::size_t v = 0; v < g.variants.size(); v++)
        Print("  %zu = %s%s\n", v + 1, g.variants[v].name.c_str(), g.variants[v].sameAsMain ? " (the same as the main folder)" : "");
      g.chosen = AskNumber("  Choose", 0, static_cast<int>(g.variants.size()), 0) - 1;
    }
  std::string choices;
  for (std::size_t gi = 0; gi < layout.groups.size(); gi++) {
    const Group &g = layout.groups[gi];
    if (!choices.empty()) choices += "; ";
    choices += g.name + ": " + (g.chosen < 0 ? std::string(g.addOn ? "no" : "as in the main folder") : g.addOn ? std::string("yes") : g.variants[static_cast<std::size_t>(g.chosen)].name);
    if (g.chosen < 0) continue;
    for (const Source &s : variantScans[scanOf[{gi, static_cast<std::size_t>(g.chosen)}]].files) byKey[s.name.key] = &s;
  }
  // the size
  if (o.size == 0) {
    Print("\nSize on the phone (the longer side of the biggest images):\n");
    Print("  1 = 512   (recommended, about 0.7 GB for a full pack, for every device)\n");
    Print("  2 = 1024  (about 2.3 GB for a full pack, for strong devices)\n");
    o.size = AskNumber("Choose", 1, 2, 1) == 2 ? 1024 : 512;
  }
  if (o.size != 512 && o.size != 1024) {
    Print("The size must be 512 or 1024.\n");
    return 2;
  }
  // the output
  const std::string baseName = "CubeEvil4-textures-" + std::to_string(o.size);
  fs::path out = o.out.empty() ? fio::Path(DesktopFolder()) : fio::Path(o.out);
  if (fs::is_directory(out, ec) || o.out.empty() || Lower(fio::Utf8(out.extension())) != ".c4tex") {
    fs::create_directories(out, ec);
    out /= fio::Path(baseName + ".c4tex");
  }
  out = fs::weakly_canonical(out, ec);
  {
    const std::string r = fio::Utf8(root) + static_cast<char>(fs::path::preferred_separator);
    if (fio::Utf8(out).rfind(r, 0) == 0) {
      Print("The output file would be inside the texture pack folder. Please choose another folder.\n");
      return 2;
    }
  }
  if (fs::exists(out, ec) && gInteractive && !o.yes &&
      !AskYes(("\n" + fio::Utf8(out.filename()) + " is there already. Replace it").c_str())) {
    Print("Nothing changed.\n");
    return 1;
  }
  std::vector<const Source *> todo;
  for (auto &kv : byKey) todo.push_back(kv.second);
  if (o.limit > 0 && todo.size() > static_cast<std::size_t>(o.limit)) todo.resize(static_cast<std::size_t>(o.limit));
  // free space: a rough upper bound (8 bits a texel, with mips) of what is written
  {
    std::uint64_t estimate = 0;
    for (const Source *s : todo) estimate += std::min<std::uint64_t>(s->bytes, 4ull * o.size * o.size / 3);
    const fs::space_info sp = fs::space(out.parent_path(), ec);
    if (!ec && sp.available < estimate + (64ull << 20)) {
      Print("\nOnly %s free at %s; the pack may need up to %s.\n", Bytes(sp.available).c_str(),
            fio::Utf8(out.parent_path()).c_str(), Bytes(estimate).c_str());
      if (gInteractive && !AskYes("Try anyway")) return 1;
    }
  }
  const fs::path part = fio::Path(fio::Utf8(out) + ".part");
  FILE *f = fio::Open(fio::Utf8(part), "wb");
  if (!f) {
    Print("Cannot write %s.\n", fio::Utf8(part).c_str());
    return 2;
  }
  // header and info first (the header again at the end)
  char date[32];
  {
    const std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof(date), "%Y-%m-%d", std::localtime(&now));
  }
  std::string info;
  info += "name=" + baseName + "\n";
  info += std::string("tool=") + kToolName + " " + kToolVersion + "\n";
  info += std::string("created=") + date + "\n";
  info += "size=" + std::to_string(o.size) + "\n";
  info += "source=" + fio::Utf8(root.filename()) + "\n";
  if (!choices.empty()) info += "choices=" + choices + "\n";
  const std::uint64_t infoOffset = c4tex::kHeaderBytes;
  std::uint64_t at = infoOffset + info.size();
  // images=... comes last, written when known: reserve its room
  info += "images=          \n";
  at = infoOffset + info.size();
  const std::uint64_t dataOffset = (at + c4tex::kAlign - 1) / c4tex::kAlign * c4tex::kAlign;
  std::vector<std::uint8_t> zeros(c4tex::kAlign, 0);
  c4tex::Header header{};
  bool ok = WriteAll(f, &header, sizeof(header)) && WriteAll(f, info.data(), info.size()) &&
            WriteAll(f, zeros.data(), static_cast<std::size_t>(dataOffset - at));
  at = dataOffset;
  // the work
  const unsigned threads = o.threads > 0 ? static_cast<unsigned>(o.threads) : std::max(1u, std::thread::hardware_concurrency());
  const std::uint64_t budget = std::clamp<std::uint64_t>(PhysicalMemory() / 3, 1ull << 30, 6ull << 30);
  Print("\nConverting %s images to %u with %u threads ...\n", Thousands(todo.size()).c_str(), o.size, threads);
  std::mutex mu;
  std::condition_variable cv;
  std::size_t nextJob = 0, writerAt = 0;  // workers stay at most 512 images ahead of the writer
  std::uint64_t inUse = 0;
  std::map<std::size_t, Result> done;
  std::atomic<bool> stop{false};
  auto worker = [&] {
    for (;;) {
      std::size_t j;
      std::uint64_t need;
      {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return nextJob < writerAt + 512 || stop; });
        if (nextJob >= todo.size() || stop) return;
        j = nextJob++;
      }
      need = std::min(MemoryFor(*todo[j], o.size), budget);
      {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&] { return inUse + need <= budget || inUse == 0 || stop; });
        inUse += need;
      }
      Result r = Convert(*todo[j], o.size, o.effort);
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
  std::size_t written = 0, counts[4] = {};
  std::uint64_t dataBytes = 0;
  const bool console = StdoutIsConsole();
  auto lastPrint = std::chrono::steady_clock::now();
  int lastPercent = -1;
  for (std::size_t j = 0; j < todo.size() && ok; j++) {
    Result r;
    {
      std::unique_lock<std::mutex> lock(mu);
      cv.wait(lock, [&] { return done.count(j) != 0; });
      r = std::move(done[j]);
      done.erase(j);
      writerAt = j + 1;
    }
    cv.notify_all();
    const std::string rel = fio::Utf8(fs::relative(fio::Path(todo[j]->path), root, ec));
    if (!r.ok) {
      failed.push_back({rel, r.error});
    } else {
      r.entry.dataOffset = at;
      ok = WriteAll(f, r.data.data(), r.data.size());
      at += r.data.size();
      const std::uint64_t pad = (c4tex::kAlign - at % c4tex::kAlign) % c4tex::kAlign;
      ok = ok && WriteAll(f, zeros.data(), static_cast<std::size_t>(pad));
      at += pad;
      dataBytes += r.data.size();
      entries.push_back(r.entry);
      counts[r.entry.encoding]++;
      psnrSum += r.psnr;
      worst.push_back({r.psnr, rel});
      written++;
      if (written % 256 == 0) {
        const fs::space_info sp = fs::space(out.parent_path(), ec);
        if (!ec && sp.available < (64ull << 20)) {
          Print("\nThe disk is full. Stopped.\n");
          ok = false;
        }
      }
    }
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - start).count();
    const int percent = static_cast<int>((j + 1) * 100 / todo.size());
    if (console ? (now - lastPrint > std::chrono::milliseconds(500) || j + 1 == todo.size()) : percent / 10 != lastPercent / 10) {
      lastPrint = now;
      lastPercent = percent;
      const double left = j + 1 < todo.size() ? elapsed / (j + 1) * (todo.size() - j - 1) : 0;
      Print("%s  %s of %s (%d %%), %s written, about %s left      %s", console ? "\r" : "", Thousands(j + 1).c_str(),
            Thousands(todo.size()).c_str(), percent, Bytes(at).c_str(), Duration(left).c_str(), console ? "" : "\n");
    }
  }
  stop = true;
  cv.notify_all();
  for (std::thread &t : pool) t.join();
  if (console) Print("\n");
  // index, info and header
  std::sort(entries.begin(), entries.end(), c4tex::EntryLess);
  const std::uint64_t indexOffset = at;
  ok = ok && WriteAll(f, entries.data(), entries.size() * sizeof(c4tex::Entry));
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
  ok = ok && fio::Seek(f, 0) && WriteAll(f, &header, sizeof(header)) && WriteAll(f, info.data(), info.size());
  ok = std::fclose(f) == 0 && ok;
  if (!ok || entries.empty()) {
    fs::remove(part, ec);
    Print("%s\n", entries.empty() ? "No image could be converted; nothing written." : "Writing the file failed; nothing written.");
    return 2;
  }
  fs::remove(out, ec);
  fs::rename(part, out, ec);
  if (ec) {
    Print("Cannot rename %s: %s\n", fio::Utf8(part).c_str(), ec.message().c_str());
    return 2;
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  // the report next to the pack
  std::sort(worst.begin(), worst.end());
  std::string report;
  {
    char line[512];
    std::snprintf(line, sizeof(line), "%s %s, %s\nSource: %s\nChoices: %s\nSize: %u\n", kToolName, kToolVersion, date,
                  fio::Utf8(root.filename()).c_str(), choices.empty() ? "-" : choices.c_str(), o.size);
    report += line;
    std::snprintf(line, sizeof(line),
                  "Images: %zu converted (%zu opaque, %zu with cut-out transparency, %zu with soft transparency), %zu failed, %zu skipped\n",
                  entries.size(), counts[1], counts[2], counts[3], failed.size(), scan.skipped.size() + duplicates.size());
    report += line;
    std::snprintf(line, sizeof(line), "File: %s (%s), image data %s\nTime: %s, %u threads, effort %d\n",
                  fio::Utf8(out.filename()).c_str(), Bytes(at).c_str(), Bytes(dataBytes).c_str(), Duration(seconds).c_str(),
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
    for (const Skipped &s : duplicates) report += "Skipped: " + s.path + ": " + s.reason + "\n";
    for (const Skipped &s : scan.skipped) report += "Skipped: " + s.path + ": " + s.reason + "\n";
  }
  fs::path reportPath = out;
  reportPath.replace_extension(".txt");
  fio::WriteFile(fio::Utf8(reportPath), std::vector<std::uint8_t>(report.begin(), report.end()));
  Print("\nDone: %s (%s) in %s.\n", fio::Utf8(out).c_str(), Bytes(at).c_str(), Duration(seconds).c_str());
  Print("%s images converted", Thousands(entries.size()).c_str());
  if (!failed.empty()) Print(", %zu could not be read", failed.size());
  if (!scan.skipped.empty() || !duplicates.empty()) Print(", %zu files skipped", scan.skipped.size() + duplicates.size());
  Print(". Details: %s\n", fio::Utf8(reportPath.filename()).c_str());
  Print("\nCopy %s to your phone, then in CubeEvil4: Menu, Cheats & Mods, Import texture pack.\n",
        fio::Utf8(out.filename()).c_str());
  return failed.empty() ? 0 : 3;
}

// ---------------- check ----------------

int Check(const Options &o) {
  FILE *f = fio::Open(o.check, "rb");
  if (!f) {
    Print("Cannot open %s.\n", o.check.c_str());
    return 2;
  }
  const std::uint64_t size = fio::Size(f);
  c4tex::Header h{};
  std::vector<std::uint8_t> buf;
  if (!fio::ReadRange(f, 0, sizeof(h), buf) || buf.size() != sizeof(h)) {
    Print("The file is too short.\n");
    std::fclose(f);
    return 2;
  }
  std::memcpy(&h, buf.data(), sizeof(h));
  int problems = 0;
  auto problem = [&](const char *what) {
    Print("Problem: %s\n", what);
    problems++;
  };
  if (std::memcmp(h.magic, c4tex::kMagic, 8) != 0) {
    problem("not a CubeEvil4 texture pack");
    std::fclose(f);
    return 2;
  }
  if (h.version != c4tex::kVersion) problem("unknown version");
  if (h.headerCrc != c4tex::HeaderCrc(h)) problem("header checksum");
  if (h.fileBytes != size) problem("file size differs from the header (incomplete copy?)");
  if (h.entryBytes != c4tex::kEntryBytes || h.entryCount > c4tex::kMaxEntries ||
      h.indexOffset + static_cast<std::uint64_t>(h.entryCount) * c4tex::kEntryBytes > size) {
    problem("index out of the file");
    std::fclose(f);
    return 2;
  }
  std::vector<std::uint8_t> infoBytes;
  if (h.infoBytes <= c4tex::kMaxInfoBytes && fio::ReadRange(f, h.infoOffset, h.infoBytes, infoBytes))
    Print("%s", std::string(infoBytes.begin(), infoBytes.end()).c_str());
  std::vector<std::uint8_t> indexBytes;
  fio::ReadRange(f, h.indexOffset, static_cast<std::size_t>(h.entryCount) * c4tex::kEntryBytes, indexBytes);
  if (c4tex::Crc32(indexBytes.data(), indexBytes.size()) != h.indexCrc) problem("index checksum");
  std::vector<c4tex::Entry> entries(h.entryCount);
  if (h.entryCount) std::memcpy(entries.data(), indexBytes.data(), indexBytes.size());
  std::size_t bad = 0, badCrc = 0, unsorted = 0, counts[4] = {};
  std::uint64_t bytes = 0;
  unsigned largest = 0;
  for (std::size_t i = 0; i < entries.size(); i++) {
    const c4tex::Entry &e = entries[i];
    if (i && !c4tex::EntryLess(entries[i - 1], e)) unsorted++;
    if (!c4tex::EntryValid(e, size, h.dataOffset, h.indexOffset)) {
      bad++;
      continue;
    }
    std::vector<std::uint8_t> data;
    if (!fio::ReadRange(f, e.dataOffset, e.dataBytes, data) || c4tex::Crc32(data.data(), data.size()) != e.dataCrc) badCrc++;
    counts[e.encoding]++;
    bytes += e.dataBytes;
    largest = std::max<unsigned>(largest, std::max(e.width, e.height));
  }
  Print("Entries: %u (%zu opaque, %zu cut-out, %zu soft alpha), image data %s, largest side %u\n", h.entryCount, counts[1],
        counts[2], counts[3], Bytes(bytes).c_str(), largest);
  if (bad) {
    Print("Problem: %zu entries out of range\n", bad);
    problems++;
  }
  if (badCrc) {
    Print("Problem: %zu entries with a wrong checksum\n", badCrc);
    problems++;
  }
  if (unsorted) {
    Print("Problem: the index is not sorted (%zu places)\n", unsorted);
    problems++;
  }
  if (!o.png.empty()) {
    std::error_code ec;
    fs::create_directories(fio::Path(o.png), ec);
    std::size_t exported = 0;
    for (const c4tex::Entry &e : entries) {
      if (!c4tex::EntryValid(e, size, h.dataOffset, h.indexOffset)) continue;
      Key k;
      k.tex = e.texHash;
      k.tlut = e.tlutHash;
      k.w = e.origWidth;
      k.h = e.origHeight;
      k.fmt = e.gxFormat;
      k.hasTlut = (e.flags & c4tex::kFlagTlut) != 0;
      k.tlutAny = (e.flags & c4tex::kFlagTlutAny) != 0;
      k.mip = (e.flags & c4tex::kFlagMipName) != 0;
      char hex[32];
      std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(e.texHash));
      if (!o.key.empty() && Lower(o.key) != hex) continue;
      if (o.key.empty() && o.limit > 0 && exported >= static_cast<std::size_t>(o.limit)) break;
      std::vector<std::uint8_t> data;
      fio::ReadRange(f, e.dataOffset, e.dataBytes, data);
      unsigned w = e.width, hh = e.height;
      std::size_t off = 0;
      for (unsigned l = 0; l < e.levels; l++) {
        img::Image im;
        im.Resize(w, hh);
        etc2::DecodeLevel(static_cast<etc2::Kind>(e.encoding), data.data() + off, w, hh, im.px.data());
        std::string name = KeyName(k) + (l ? "_mip" + std::to_string(l) : std::string()) + ".png";
        img::SavePng(fio::Utf8(fio::Path(o.png) / fio::Path(name)), im);
        off += c4tex::LevelBytes(e.encoding, w, hh);
        w = std::max(1u, w / 2);
        hh = std::max(1u, hh / 2);
        if (o.key.empty()) break;  // the first level only, unless one texture is asked for
      }
      exported++;
    }
    Print("Exported %zu image(s) as PNG to %s\n", exported, o.png.c_str());
  }
  std::fclose(f);
  Print(problems ? "The pack has problems.\n" : "The pack is fine.\n");
  return problems ? 1 : 0;
}

int Run(std::vector<std::string> args) {
  Options o;
  for (std::size_t i = 0; i < args.size(); i++) {
    const std::string a = Lower(args[i]);
    auto value = [&](std::string &v) {
      if (i + 1 < args.size()) v = args[++i];
    };
    std::string v;
    if (a == "-in" || a == "--in") value(o.in);
    else if (a == "-out" || a == "--out") value(o.out);
    else if (a == "-size" || a == "--size") { value(v); o.size = static_cast<unsigned>(std::atoi(v.c_str())); }
    else if (a == "-variant" || a == "--variant") { value(v); o.variants.push_back(v); }
    else if (a == "-threads" || a == "--threads") { value(v); o.threads = std::atoi(v.c_str()); }
    else if (a == "-effort" || a == "--effort") { value(v); o.effort = std::clamp(std::atoi(v.c_str()), 0, 2); }
    else if (a == "-limit" || a == "--limit") { value(v); o.limit = std::atoi(v.c_str()); }
    else if (a == "-yes" || a == "--yes") o.yes = true;
    else if (a == "-check" || a == "--check") value(o.check);
    else if (a == "-png" || a == "--png") value(o.png);
    else if (a == "-key" || a == "--key") value(o.key);
    else if (a == "-help" || a == "--help" || a == "-h" || a == "/?") { Usage(); return 0; }
    else if (o.in.empty() && a.size() && a[0] != '-') o.in = args[i];  // a folder dropped on the program
    else { Print("Unknown option %s\n\n", args[i].c_str()); Usage(); return 2; }
  }
  if (!o.check.empty()) return Check(o);
  if (args.empty() || (o.in.size() && o.size == 0)) gInteractive = true;
  Print("%s %s\n", kToolName, kToolVersion);
  Print("CubeEvil4 includes no textures. Use only packs you obtained yourself and may use.\n\n");
  if (o.in.empty()) {
    Print("Drag your texture pack folder into this window and press Enter:\n> ");
    o.in = Trim(ReadLine());
    if (o.in.empty()) return 1;
  }
  return Pack(o);
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
  SetConsoleOutputCP(CP_UTF8);
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) args.push_back(fio::Utf8(fs::path(argv[i])));
  const int rc = Run(args);
  if (gInteractive) {
    Print("\nPress Enter to close.");
    ReadLine();
  }
  return rc;
}
#else
int main(int argc, char **argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  return Run(args);
}
#endif
