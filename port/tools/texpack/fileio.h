// Files with UTF-8 names (Windows: wide API) and 64-bit offsets, for the texture tool.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fio {

inline std::filesystem::path Path(const std::string &utf8) { return std::filesystem::u8path(utf8); }

inline std::string Utf8(const std::filesystem::path &p) {
  const auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

inline FILE *Open(const std::string &utf8, const char *mode) {
#ifdef _WIN32
  std::wstring wm(mode, mode + std::strlen(mode));
  return _wfopen(Path(utf8).c_str(), wm.c_str());
#else
  return std::fopen(utf8.c_str(), mode);
#endif
}

inline bool Seek(FILE *f, std::uint64_t offset) {
#ifdef _WIN32
  return _fseeki64(f, static_cast<long long>(offset), SEEK_SET) == 0;
#else
  return fseeko(f, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

inline std::uint64_t Size(FILE *f) {
#ifdef _WIN32
  const long long here = _ftelli64(f);
  _fseeki64(f, 0, SEEK_END);
  const long long size = _ftelli64(f);
  _fseeki64(f, here, SEEK_SET);
#else
  const off_t here = ftello(f);
  fseeko(f, 0, SEEK_END);
  const off_t size = ftello(f);
  fseeko(f, here, SEEK_SET);
#endif
  return size < 0 ? 0 : static_cast<std::uint64_t>(size);
}

// bytes [offset, offset + n) of a file
inline bool ReadRange(FILE *f, std::uint64_t offset, std::size_t n, std::vector<std::uint8_t> &out) {
  out.resize(n);
  if (!Seek(f, offset)) return false;
  return n == 0 || std::fread(out.data(), 1, n, f) == n;
}

inline bool ReadFile(const std::string &path, std::vector<std::uint8_t> &out) {
  FILE *f = Open(path, "rb");
  if (!f) return false;
  const std::uint64_t n = Size(f);
  const bool ok = n < (1ull << 31) && ReadRange(f, 0, static_cast<std::size_t>(n), out);
  std::fclose(f);
  return ok;
}

inline bool WriteFile(const std::string &path, const std::vector<std::uint8_t> &data) {
  FILE *f = Open(path, "wb");
  if (!f) return false;
  const bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace fio
