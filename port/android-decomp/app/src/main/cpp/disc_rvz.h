#pragma once

#include <cstdint>
#include <functional>
#include <string>

// RVZ, Dolphin's compressed disc image (its own WIA format with zstd and packed junk data), of a
// GameCube disc: unpacked once at the import into the plain disc image the import reads (ISO/GCM).
// Supported: compression none and zstd (Dolphin's default for RVZ); bzip2/LZMA and Wii discs not.
namespace re4 {

struct RvzInfo {
  std::uint64_t discSize = 0;   // bytes of the disc it holds (the ISO/GCM it unpacks to)
  std::uint64_t fileSize = 0;   // bytes of the RVZ file
};

// True when the file starts with the RVZ magic.
bool IsRvz(const char *path);

// Reads the headers: false (with the reason in *error) when it is no RVZ of a GameCube disc this
// reader can unpack.
bool RvzInfoOf(const char *path, RvzInfo *info, std::string *error);

// Unpacks `in` to the disc image `out`. `progress(done, total)` after each group of the image;
// false from it cancels. The reason of a failure in *error.
bool RvzToIso(const char *in, const char *out, const std::function<bool(std::uint64_t, std::uint64_t)> &progress,
              std::string *error);

} // namespace re4
