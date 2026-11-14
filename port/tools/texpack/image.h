// Images for the texture tool: DDS (BC1-BC5, BC7, uncompressed), PNG and JPEG readers, a PNG writer, resizing and mip
// levels.
// Our own code (CC0), no libraries.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace img {

struct Image {
  unsigned w = 0, h = 0;
  std::vector<std::uint8_t> px;  // RGBA8, row-major
  void Resize(unsigned nw, unsigned nh) {
    w = nw;
    h = nh;
    px.assign(static_cast<std::size_t>(nw) * nh * 4, 0);
  }
};

// What a source file holds about alpha
enum class Alpha { kNone, kBinary, kSoft };

struct Loaded {
  std::vector<Image> levels;  // the chosen level first, then (with allLevels) the smaller ones of the file
  unsigned fileWidth = 0, fileHeight = 0, fileLevels = 1;
  Alpha alpha = Alpha::kNone;
  std::string format;         // "BC1", "BC3", "RGBA8", "PNG" ... for the report
};

// Reads a DDS or PNG file. With maxSide, a DDS with mip levels gives its last level whose longer side is still at
// least maxSide (so that a 8192 texture is not decoded whole for 512); allLevels keeps the levels below it too.
bool Load(const std::string &path, unsigned maxSide, bool allLevels, Loaded &out, std::string &error);

// The size of the level Load would decode (without reading the image), false if not a DDS or PNG file
bool Probe(const std::string &path, unsigned maxSide, unsigned &w, unsigned &h);

// The same from memory (tests)
bool LoadDds(const std::uint8_t *data, std::size_t n, unsigned maxSide, bool allLevels, Loaded &out, std::string &error);
bool LoadPng(const std::uint8_t *data, std::size_t n, Loaded &out, std::string &error);
// JPEG (jpeg.cpp): baseline and progressive, 8 bits, grey or YCbCr/RGB; its size without decoding
bool LoadJpeg(const std::uint8_t *data, std::size_t n, Loaded &out, std::string &error);
bool JpegSize(const std::uint8_t *data, std::size_t n, unsigned &w, unsigned &h);

// zlib stream (2-byte header, deflate, Adler-32) to `out`, which has the expected size
bool Inflate(const std::uint8_t *data, std::size_t n, std::vector<std::uint8_t> &out, std::string &error);
// a raw deflate stream (ZIP); partial: out holds only the first out.size() bytes of it
bool InflateRaw(const std::uint8_t *data, std::size_t n, std::vector<std::uint8_t> &out, std::string &error, bool partial);

// From memory: the size of the level Load would decode (head: the file's first bytes, 1 MB for a JPEG's frame header;
// fileBytes: its whole size) and the bits a texel ETC2 needs at most (4, or 8 with soft alpha); a whole file decoded
bool ProbeData(const std::uint8_t *head, std::size_t headBytes, std::uint64_t fileBytes, unsigned maxSide, unsigned &w,
               unsigned &h, int *bitsPerTexel);
bool LoadData(const std::uint8_t *data, std::size_t n, unsigned maxSide, bool allLevels, Loaded &out, std::string &error);

// PNG (RGBA8, stored deflate blocks)
std::vector<std::uint8_t> EncodePng(const Image &image);
bool SavePng(const std::string &path, const Image &image);

// Scale down to nw x nh (Mitchell filter, alpha-weighted colours; colours of transparent pixels kept)
Image Downscale(const Image &src, unsigned nw, unsigned nh);

// The next mip level (half size, at least 1)
Image HalfSize(const Image &src);

// The share of pixels with alpha >= threshold
double Coverage(const Image &image, int threshold);

// Scales alpha so that the coverage at threshold comes near `target` (alpha-tested textures far away)
void KeepCoverage(Image &image, int threshold, double target);

// Alpha of an image: none (all 255), binary (only 0 and 255) or soft
Alpha AlphaOf(const Image &image);

}  // namespace img
