// The language pack import on the PC (port/docs/sprachpakete.md): the app's code (app/src/main/cpp/language_pack.cpp)
// from a folder or a ZIP into a pack folder the core reads with RE4_LANGUAGE_PACK.
//
//   langpack_tool import GAMEROOT DEST SOURCE [NAME]   SOURCE: a folder or a .zip; GAMEROOT holds disc0/disc1
//                                                     (PC) or disc1/disc2 (app)
//   langpack_tool selftest-room ROOM.das              the room rule on a real room: one byte of its collision (SAT),
//                                                     texts (MDT), items (ITM), enemy areas (EAT) changed
//
// Build (WSL): g++ -O2 -std=c++17 -I app/src/main/cpp tools/langpack_tool.cpp app/src/main/cpp/language_pack.cpp
//              app/src/main/cpp/language_pack_zip.cpp app/src/main/cpp/ports/yz2.cpp -o langpack_tool
#include "language_pack.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* reasonName(int r) {
  static const char* const names[] = {"ok", "no discs", "not the European discs", "program file", "not a text file",
                                      "more than text", "not on the disc", "damaged", "no changes", "no space",
                                      "read failed", "not a pack", "cancelled", "two copies", "unsafe path",
                                      "no language", "bad zip"};
  return r >= 0 && r <= 16 ? names[r] : "?";
}

int main(int argc, char** argv) {
  if (argc == 3 && strcmp(argv[1], "selftest-room") == 0) {
    FILE* f = fopen(argv[2], "rb");
    if (!f) return 1;
    re4lang::Bytes room;
    uint8_t buf[65536];
    for (size_t n; (n = fread(buf, 1, sizeof(buf), f)) > 0;) room.insert(room.end(), buf, buf + n);
    fclose(f);
    for (const char* tag : {"SAT", "MDT", "ITM", "EAT"}) printf("%s\n", re4lang::SelfTestRoom(room, tag).c_str());
    return 0;
  }
  if (argc < 5 || strcmp(argv[1], "import") != 0) {
    fprintf(stderr, "usage: langpack_tool import GAMEROOT DEST SOURCE [NAME]\n");
    return 2;
  }
  std::string game = argv[2], dest = argv[3], source = argv[4];
  while (source.size() > 1 && source.back() == '/') source.pop_back();
  std::string base = source.substr(source.find_last_of('/') + 1);
  std::string name = argc > 5 ? argv[5] : base;
  int lastPercent = -1;
  auto progress = [&lastPercent](uint64_t done, uint64_t total) {
    int p = total ? int(done * 100 / total) : 100;
    if (p / 10 != lastPercent / 10) {
      lastPercent = p;
      fprintf(stderr, "  %3d %%\n", p);
    }
  };
  re4lang::Result r;
  bool ok;
  struct stat st;
  if (stat(source.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    ok = re4lang::ImportFolder(game, dest, source, name, r, progress);
  } else {
    int fd = open(source.c_str(), O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "cannot open %s\n", source.c_str());
      return 1;
    }
    fstat(fd, &st);
    re4lang::ZipRead read = [fd](uint64_t at, void* buf, size_t n) {
      size_t got = 0;
      while (got < n) {
        ssize_t k = pread(fd, (char*) buf + got, n - got, off_t(at + got));
        if (k <= 0) return false;
        got += size_t(k);
      }
      return true;
    };
    ok = re4lang::ImportZip(game, dest, read, uint64_t(st.st_size), name, base, r, progress);
    close(fd);
  }
  printf("result: %s\n", re4lang::Describe(r).c_str());
  printf("%s: %s%s%s\n", ok ? "imported" : "refused", reasonName(r.reason), r.detail.empty() ? "" : " - ",
         r.detail.c_str());
  printf("replaced %d disc files with %d files (%.1f MB), %d identical, %d ignored, language %d\n", r.replaced, r.kept,
         r.bytes / 1048576.0, r.identical, r.ignored, r.language);
  return ok ? 0 : 1;
}
