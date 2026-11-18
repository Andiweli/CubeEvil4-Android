// What the PC's two front ends of the Texture Packer share: the console (texpack.cpp,
// CubeEvil4-TexturePacker-Console.exe) and the window (gui.cpp, CubeEvil4-TexturePacker.exe).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

#include "fileio.h"
#include "packer.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pc {

// the pack file on disk, written through stdio
class FileSink : public pack::Sink {
 public:
  FileSink(FILE *f, std::filesystem::path dir) : f_(f), dir_(std::move(dir)) {}
  bool Write(const void *data, std::size_t n) override { return n == 0 || std::fwrite(data, 1, n, f_) == n; }
  bool WriteAt(std::uint64_t offset, const void *data, std::size_t n) override {
    return fio::Seek(f_, offset) && Write(data, n);
  }
  std::uint64_t Free() override {
    std::error_code ec;
    const std::filesystem::space_info sp = std::filesystem::space(dir_, ec);
    return ec ? ~0ull : sp.available;
  }

 private:
  FILE *f_;
  std::filesystem::path dir_;
};

inline std::uint64_t PhysicalMemory() {
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

// memory for the images in work: a third of the RAM, 1 to 6 GB
inline std::uint64_t MemoryBudget() {
  return std::clamp<std::uint64_t>(PhysicalMemory() / 3, 1ull << 30, 6ull << 30);
}

// "2026-11-14"
inline std::string Today() {
  char date[32];
  const std::time_t now = std::time(nullptr);
  std::strftime(date, sizeof(date), "%Y-%m-%d", std::localtime(&now));
  return date;
}

}  // namespace pc
