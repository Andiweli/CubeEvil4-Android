// CubeEvil4 Texture Packer, the PC's console front end: turns a Dolphin texture pack (a folder or a ZIP with
// tex1_<W>x<H>[_m]_<hash>[_<tlut>]_<fmt>.dds/.png/.jpg) into one CubeEvil4 texture pack file (.c4tex) with the core in
// packer.cpp (the Android app uses the same). Plan: port/docs/textur-lader-plan.md; file layout: c4tex.h,
// port/docs/textur-paket-format.md. It only reads the pack and never writes into it. Our own code (CC0).
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
#include "packer.h"
#include "zip.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using pack::Bytes;
using pack::Duration;
using pack::Key;
using pack::KeyName;
using pack::kToolName;
using pack::kToolVersion;
using pack::Lower;
using pack::Thousands;

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

// ---------------- packing (the console front end of packer.h) ----------------

struct Options {
  std::string in, out;
  unsigned size = 0;
  std::vector<std::string> variants;  // "Group=Variant"
  int threads = 0, effort = 1, limit = 0;
  bool yes = false;
  std::string check, png, key;
};

void Usage() {
  Print("%s %s\n\n", pack::kToolName, pack::kToolVersion);
  Print("Converts a texture pack in the Dolphin format into one file for CubeEvil4.\n");
  Print("Start it without anything to be asked step by step, or:\n\n");
  Print("  texpack -in <pack folder or .zip> -size 256|512|1024 [-out <folder or file.c4tex>]\n");
  Print("          [-variant \"<group>=<variant>\"] ... [-threads N] [-effort 0|1|2]\n");
  Print("  texpack -check <file.c4tex> [-png <folder>] [-limit N] [-key <texture hash>]\n\n");
  Print("The pack is only read, never changed. An existing output file is replaced.\n");
  Print("CubeEvil4 includes no textures; use only packs you obtained yourself and may use.\n");
}

// the pack file on disk, written through stdio
class FileSink : public pack::Sink {
 public:
  FileSink(FILE *f, fs::path dir) : f_(f), dir_(std::move(dir)) {}
  bool Write(const void *data, std::size_t n) override { return n == 0 || std::fwrite(data, 1, n, f_) == n; }
  bool WriteAt(std::uint64_t offset, const void *data, std::size_t n) override {
    return fio::Seek(f_, offset) && Write(data, n);
  }
  std::uint64_t Free() override {
    std::error_code ec;
    const fs::space_info sp = fs::space(dir_, ec);
    return ec ? ~0ull : sp.available;
  }

 private:
  FILE *f_;
  fs::path dir_;
};

int Pack(Options &o) {
  std::error_code ec;
  fs::path root = fio::Path(o.in);
  const bool zip = fs::is_regular_file(root, ec) && Lower(fio::Utf8(root.extension())) == ".zip";
  if (!zip && !fs::is_directory(root, ec)) {
    Print("The folder or ZIP file \"%s\" was not found.\n", o.in.c_str());
    return 2;
  }
  root = fs::weakly_canonical(root, ec);
  Print("Looking at %s ...\n", fio::Utf8(root).c_str());
  std::vector<pack::InputFile> files;
  std::string name;
  if (zip) {
    std::string error;
    auto access = zipr::OpenFile(fio::Utf8(root), error);
    if (!access || !pack::ListZip(access, fio::Utf8(root.filename()), files, name, error)) {
      Print("The ZIP file cannot be read: %s.\n", error.c_str());
      return 2;
    }
  } else {
    files = pack::ListFolder(fio::Utf8(root));
    name = fio::Utf8(root.filename());
  }
  pack::Pack p;
  pack::Prepare(std::move(files), name, p);
  if (p.byKey.empty()) {
    Print("No textures with Dolphin names (tex1_...) found in this %s.\n", zip ? "ZIP file" : "folder");
    return 2;
  }
  Print("Found %s images (%s)", Thousands(p.byKey.size()).c_str(), Bytes(p.bytes).c_str());
  if (!p.groups.empty()) Print(" and %zu group(s) of choices", p.groups.size());
  Print(".\n");
  // the choices
  std::string bad;
  if (!pack::Choose(p, o.variants, bad)) {
    Print("The choice \"%s\" matches no group and variant of this pack.\n", bad.c_str());
    return 2;
  }
  if (gInteractive)
    for (pack::Group &g : p.groups) {
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
  // the size
  if (o.size == 0) {
    Print("\nSize on the phone (the longer side of the images; never smaller than the game's own):\n");
    Print("  1 = 256   (saves memory and space on weak phones; big textures are not sharper than the game's)\n");
    Print("  2 = 512   (recommended, for every device)\n");
    Print("  3 = 1024  (for strong devices with enough space)\n");
    static const unsigned sizes[3] = {256, 512, 1024};
    o.size = sizes[AskNumber("Choose", 1, 3, 2) - 1];
  }
  if (o.size != 256 && o.size != 512 && o.size != 1024) {
    Print("The size must be 256, 512 or 1024.\n");
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
  if (!zip) {
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
  pack::Settings settings;
  settings.size = o.size;
  settings.effort = o.effort;
  settings.threads = o.threads > 0 ? static_cast<unsigned>(o.threads) : 0;
  settings.memoryBudget = std::clamp<std::uint64_t>(PhysicalMemory() / 3, 1ull << 30, 6ull << 30);
  settings.limit = o.limit;
  {
    char date[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(date, sizeof(date), "%Y-%m-%d", std::localtime(&now));
    settings.date = date;
  }
  settings.outName = fio::Utf8(out.filename());
  std::string choices;
  const std::vector<const pack::Source *> todo = pack::Todo(p, settings, choices);
  // free space: the size the file will have at most
  {
    const std::uint64_t estimate = pack::Estimate(todo, o.size);
    const fs::space_info sp = fs::space(out.parent_path(), ec);
    Print("\nThe pack file will need about %s", Bytes(estimate).c_str());
    if (!ec) Print("; %s are free there", Bytes(sp.available).c_str());
    Print(".\n");
    if (!ec && sp.available < estimate + (64ull << 20)) {
      Print("That may not be enough.\n");
      if (gInteractive && !AskYes("Try anyway")) return 1;
    }
  }
  const fs::path part = fio::Path(fio::Utf8(out) + ".part");
  FILE *f = fio::Open(fio::Utf8(part), "wb");
  if (!f) {
    Print("Cannot write %s.\n", fio::Utf8(part).c_str());
    return 2;
  }
  FileSink sink(f, out.parent_path());
  const unsigned threads = settings.threads ? settings.threads : std::max(1u, std::thread::hardware_concurrency());
  Print("\nConverting %s images to %u with %u threads ...\n", Thousands(todo.size()).c_str(), o.size, threads);
  const bool console = StdoutIsConsole();
  auto lastPrint = std::chrono::steady_clock::now();
  int lastPercent = -1;
  const pack::Outcome result = pack::Convert(p, todo, choices, settings, sink, [&](const pack::Progress &pr) {
    const auto now = std::chrono::steady_clock::now();
    const int percent = static_cast<int>(pr.done * 100 / pr.total);
    if (console ? (now - lastPrint > std::chrono::milliseconds(500) || pr.done == pr.total) : percent / 10 != lastPercent / 10) {
      lastPrint = now;
      lastPercent = percent;
      const double left = pr.done < pr.total ? pr.seconds / pr.done * (pr.total - pr.done) : 0;
      Print("%s  %s of %s (%d %%), %s written, about %s left      %s", console ? "\r" : "", Thousands(pr.done).c_str(),
            Thousands(pr.total).c_str(), percent, Bytes(pr.written).c_str(), Duration(left).c_str(), console ? "" : "\n");
    }
  }, nullptr);
  if (console) Print("\n");
  const bool closed = std::fclose(f) == 0;
  if (!result.ok || !closed) {
    fs::remove(part, ec);
    if (result.diskFull) Print("\nThe disk is full. Stopped.\n");
    Print("%s\n", result.entries == 0 && result.error == "no image could be converted"
                      ? "No image could be converted; nothing written."
                      : "Writing the file failed; nothing written.");
    return 2;
  }
  fs::remove(out, ec);
  fs::rename(part, out, ec);
  if (ec) {
    Print("Cannot rename %s: %s\n", fio::Utf8(part).c_str(), ec.message().c_str());
    return 2;
  }
  fs::path reportPath = out;
  reportPath.replace_extension(".txt");
  fio::WriteFile(fio::Utf8(reportPath), std::vector<std::uint8_t>(result.report.begin(), result.report.end()));
  Print("\nDone: %s (%s) in %s.\n", fio::Utf8(out).c_str(), Bytes(result.fileBytes).c_str(), Duration(result.seconds).c_str());
  Print("%s images converted", Thousands(result.entries).c_str());
  if (result.failed) Print(", %zu could not be read", result.failed);
  if (result.skipped) Print(", %zu files skipped", result.skipped);
  Print(". Details: %s\n", fio::Utf8(reportPath.filename()).c_str());
  Print("\nCopy %s to your phone, then in CubeEvil4: Menu, Cheats & Mods, Import texture pack.\n",
        fio::Utf8(out.filename()).c_str());
  return result.failed ? 3 : 0;
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
    Print("Drag your texture pack folder (or its ZIP file) into this window and press Enter:\n> ");
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
