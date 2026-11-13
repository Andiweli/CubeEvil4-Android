// Image readers, writer and filters of the texture tool (see image.h).
#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "bc7.h"
#include "c4tex.h"
#include "fileio.h"

namespace img {
namespace {

inline std::uint32_t Le32(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
         static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
inline std::uint32_t Be32(const std::uint8_t *p) {
  return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
         static_cast<std::uint32_t>(p[2]) << 8 | static_cast<std::uint32_t>(p[3]);
}
inline int Clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// ---------------- DDS ----------------

enum class Dds { kBc1, kBc2, kBc3, kBc4, kBc5, kBc7, kRaw, kUnknown };

struct RawFormat {
  unsigned bits = 0;
  std::uint32_t mask[4] = {};  // r g b a
  bool luminance = false;
};

void Rgb565(unsigned c, int out[3]) {
  const int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  out[0] = (r << 3) | (r >> 2);
  out[1] = (g << 2) | (g >> 4);
  out[2] = (b << 3) | (b >> 2);
}

// colour part of BC1-BC3; four colours always for BC2/BC3
void ColourBlock(const std::uint8_t *b, bool alwaysFour, std::uint8_t out[64]) {
  const unsigned c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
  int pal[4][4];
  Rgb565(c0, pal[0]);
  Rgb565(c1, pal[1]);
  pal[0][3] = pal[1][3] = 255;
  if (c0 > c1 || alwaysFour) {
    for (int c = 0; c < 3; c++) {
      pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3;
      pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3;
    }
    pal[2][3] = pal[3][3] = 255;
  } else {
    for (int c = 0; c < 3; c++) {
      pal[2][c] = (pal[0][c] + pal[1][c]) / 2;
      pal[3][c] = 0;
    }
    pal[2][3] = 255;
    pal[3][3] = 0;
  }
  const std::uint32_t idx = Le32(b + 4);
  for (int i = 0; i < 16; i++) {
    const int k = (idx >> (2 * i)) & 3;
    for (int c = 0; c < 4; c++) out[i * 4 + c] = static_cast<std::uint8_t>(pal[k][c]);
  }
}

// the 8-byte BC3 alpha / BC4 block: 16 values
void ValueBlock(const std::uint8_t *b, std::uint8_t values[16]) {
  const int a0 = b[0], a1 = b[1];
  int v[8];
  v[0] = a0;
  v[1] = a1;
  if (a0 > a1) {
    for (int i = 1; i <= 6; i++) v[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (int i = 1; i <= 4; i++) v[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    v[6] = 0;
    v[7] = 255;
  }
  std::uint64_t bits = 0;
  for (int i = 0; i < 6; i++) bits |= static_cast<std::uint64_t>(b[2 + i]) << (8 * i);
  for (int i = 0; i < 16; i++) values[i] = static_cast<std::uint8_t>(v[(bits >> (3 * i)) & 7]);
}

std::size_t BlockBytes(Dds f) { return f == Dds::kBc1 || f == Dds::kBc4 ? 8 : 16; }

std::size_t DdsLevelBytes(Dds f, const RawFormat &raw, unsigned w, unsigned h) {
  if (f == Dds::kRaw) return static_cast<std::size_t>(w) * h * (raw.bits / 8);
  return static_cast<std::size_t>((w + 3) / 4) * ((h + 3) / 4) * BlockBytes(f);
}

unsigned MaskShift(std::uint32_t m) {
  unsigned s = 0;
  while (m && !(m & 1)) {
    m >>= 1;
    s++;
  }
  return s;
}

int MaskValue(std::uint32_t pixel, std::uint32_t mask) {
  if (!mask) return -1;
  const unsigned s = MaskShift(mask);
  const std::uint32_t m = mask >> s;
  const std::uint32_t v = (pixel & mask) >> s;
  return static_cast<int>((v * 255 + m / 2) / m);
}

void DecodeDdsLevel(Dds f, const RawFormat &raw, const std::uint8_t *d, unsigned w, unsigned h, Image &out) {
  out.Resize(w, h);
  if (f == Dds::kRaw) {
    const unsigned bpp = raw.bits / 8;
    for (unsigned y = 0; y < h; y++)
      for (unsigned x = 0; x < w; x++) {
        const std::uint8_t *p = d + (static_cast<std::size_t>(y) * w + x) * bpp;
        std::uint32_t v = 0;
        for (unsigned i = 0; i < bpp; i++) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
        std::uint8_t *o = out.px.data() + (static_cast<std::size_t>(y) * w + x) * 4;
        if (raw.luminance) {
          const int l = MaskValue(v, raw.mask[0]);
          o[0] = o[1] = o[2] = static_cast<std::uint8_t>(l < 0 ? 0 : l);
        } else {
          for (int c = 0; c < 3; c++) {
            const int value = MaskValue(v, raw.mask[c]);
            o[c] = static_cast<std::uint8_t>(value < 0 ? 0 : value);
          }
        }
        const int a = MaskValue(v, raw.mask[3]);
        o[3] = static_cast<std::uint8_t>(a < 0 ? 255 : a);
      }
    return;
  }
  const unsigned bw = (w + 3) / 4, bh = (h + 3) / 4;
  const std::size_t bytes = BlockBytes(f);
  std::uint8_t px[64];
  for (unsigned by = 0; by < bh; by++)
    for (unsigned bx = 0; bx < bw; bx++) {
      const std::uint8_t *b = d + (static_cast<std::size_t>(by) * bw + bx) * bytes;
      switch (f) {
        case Dds::kBc1: ColourBlock(b, false, px); break;
        case Dds::kBc2: {
          ColourBlock(b + 8, true, px);
          for (int i = 0; i < 16; i++) {
            const int a = (b[i / 2] >> ((i & 1) * 4)) & 15;
            px[i * 4 + 3] = static_cast<std::uint8_t>(a * 17);
          }
          break;
        }
        case Dds::kBc3: {
          ColourBlock(b + 8, true, px);
          std::uint8_t a[16];
          ValueBlock(b, a);
          for (int i = 0; i < 16; i++) px[i * 4 + 3] = a[i];
          break;
        }
        case Dds::kBc4: {
          std::uint8_t v[16];
          ValueBlock(b, v);
          for (int i = 0; i < 16; i++) {
            px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v[i];
            px[i * 4 + 3] = 255;
          }
          break;
        }
        case Dds::kBc7: DecodeBc7Block(b, px); break;
        default: {  // BC5: two channels
          std::uint8_t r[16], g[16];
          ValueBlock(b, r);
          ValueBlock(b + 8, g);
          for (int i = 0; i < 16; i++) {
            px[i * 4] = r[i];
            px[i * 4 + 1] = g[i];
            px[i * 4 + 2] = 0;
            px[i * 4 + 3] = 255;
          }
          break;
        }
      }
      for (unsigned y = 0; y < 4; y++) {
        const unsigned sy = by * 4 + y;
        if (sy >= h) break;
        for (unsigned x = 0; x < 4; x++) {
          const unsigned sx = bx * 4 + x;
          if (sx >= w) break;
          std::memcpy(out.px.data() + (static_cast<std::size_t>(sy) * w + sx) * 4, px + (y * 4 + x) * 4, 4);
        }
      }
    }
}

struct DdsInfo {
  Dds format = Dds::kUnknown;
  RawFormat raw;
  unsigned w = 0, h = 0, levels = 1;
  std::size_t dataStart = 128;
  std::string name;
};

bool ParseDds(const std::uint8_t *d, std::size_t n, DdsInfo &info, std::string &error) {
  if (n < 128 || std::memcmp(d, "DDS ", 4) != 0 || Le32(d + 4) != 124) {
    error = "not a DDS file";
    return false;
  }
  info.h = Le32(d + 12);
  info.w = Le32(d + 16);
  info.levels = std::max<std::uint32_t>(1, Le32(d + 28));
  const std::uint32_t pfFlags = Le32(d + 80);
  const std::uint8_t *four = d + 84;
  if (info.w == 0 || info.h == 0 || info.w > 16384 || info.h > 16384) {
    error = "DDS size out of range";
    return false;
  }
  if (pfFlags & 0x4) {  // DDPF_FOURCC
    if (!std::memcmp(four, "DXT1", 4)) info.format = Dds::kBc1;
    else if (!std::memcmp(four, "DXT2", 4) || !std::memcmp(four, "DXT3", 4)) info.format = Dds::kBc2;
    else if (!std::memcmp(four, "DXT4", 4) || !std::memcmp(four, "DXT5", 4)) info.format = Dds::kBc3;
    else if (!std::memcmp(four, "ATI1", 4) || !std::memcmp(four, "BC4U", 4)) info.format = Dds::kBc4;
    else if (!std::memcmp(four, "ATI2", 4) || !std::memcmp(four, "BC5U", 4)) info.format = Dds::kBc5;
    else if (!std::memcmp(four, "DX10", 4)) {
      if (n < 148) {
        error = "DDS DX10 header cut short";
        return false;
      }
      info.dataStart = 148;
      const std::uint32_t dxgi = Le32(d + 128);
      switch (dxgi) {
        case 70: case 71: case 72: info.format = Dds::kBc1; break;
        case 73: case 74: case 75: info.format = Dds::kBc2; break;
        case 76: case 77: case 78: info.format = Dds::kBc3; break;
        case 79: case 80: case 81: info.format = Dds::kBc4; break;
        case 82: case 83: case 84: info.format = Dds::kBc5; break;
        case 97: case 98: case 99: info.format = Dds::kBc7; break;
        case 94: case 95: case 96:
          error = "DDS format BC6H is not supported (a format for HDR pictures; texture packs use BC1, BC3 or BC7)";
          return false;
        case 27: case 28: case 29:  // R8G8B8A8
          info.format = Dds::kRaw;
          info.raw.bits = 32;
          info.raw.mask[0] = 0xFF;
          info.raw.mask[1] = 0xFF00;
          info.raw.mask[2] = 0xFF0000;
          info.raw.mask[3] = 0xFF000000u;
          break;
        case 87: case 88: case 90: case 91:  // B8G8R8A8 / X8
          info.format = Dds::kRaw;
          info.raw.bits = 32;
          info.raw.mask[0] = 0xFF0000;
          info.raw.mask[1] = 0xFF00;
          info.raw.mask[2] = 0xFF;
          info.raw.mask[3] = (dxgi == 87 || dxgi == 91) ? 0xFF000000u : 0;
          break;
        default:
          error = "DDS DXGI format " + std::to_string(dxgi) + " is not supported";
          return false;
      }
    } else {
      error = "DDS format " + std::string(reinterpret_cast<const char *>(four), 4) + " is not supported";
      return false;
    }
  } else if (pfFlags & (0x40 | 0x20000 | 0x2)) {  // RGB, LUMINANCE, ALPHA
    info.format = Dds::kRaw;
    info.raw.bits = Le32(d + 88);
    for (int c = 0; c < 4; c++) info.raw.mask[c] = Le32(d + 92 + 4 * c);
    if (!(pfFlags & 0x1) && !(pfFlags & 0x2)) info.raw.mask[3] = 0;  // no ALPHAPIXELS
    info.raw.luminance = (pfFlags & 0x20000) != 0;
    if (info.raw.bits != 8 && info.raw.bits != 16 && info.raw.bits != 24 && info.raw.bits != 32) {
      error = "DDS with " + std::to_string(info.raw.bits) + " bits a pixel is not supported";
      return false;
    }
  } else {
    error = "DDS pixel format not recognised";
    return false;
  }
  static const char *names[] = {"BC1", "BC2", "BC3", "BC4", "BC5", "BC7", "RGBA"};
  info.name = names[static_cast<int>(info.format)];
  // as many levels as the file really holds
  info.levels = std::min(info.levels, c4tex::FullChain(info.w, info.h));
  std::size_t at = info.dataStart;
  unsigned w = info.w, h = info.h, have = 0;
  for (unsigned l = 0; l < info.levels; l++) {
    const std::size_t bytes = DdsLevelBytes(info.format, info.raw, w, h);
    if (at + bytes > n) break;
    at += bytes;
    have++;
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }
  if (have == 0) {
    error = "DDS data cut short";
    return false;
  }
  info.levels = have;
  return true;
}

// ---------------- inflate ----------------

struct BitReader {
  const std::uint8_t *p;
  std::size_t n, pos = 0;
  std::uint64_t buf = 0;
  int cnt = 0;
  int overrun = 0;
  void Fill(int k) {
    while (cnt < k) {
      std::uint64_t byte = 0;
      if (pos < n)
        byte = p[pos++];
      else
        overrun++;
      buf |= byte << cnt;
      cnt += 8;
    }
  }
  std::uint32_t Get(int k) {
    if (k == 0) return 0;
    Fill(k);
    const std::uint32_t v = static_cast<std::uint32_t>(buf & ((1ull << k) - 1));
    buf >>= k;
    cnt -= k;
    return v;
  }
};

constexpr int kFastBits = 10;

struct Huffman {
  std::uint16_t fast[1 << kFastBits];  // symbol << 4 | length, 0: longer code
  std::int16_t count[16];
  std::uint16_t symbols[288];
};

bool BuildHuffman(const std::uint8_t *lengths, int n, Huffman &h) {
  std::memset(h.count, 0, sizeof(h.count));
  std::memset(h.fast, 0, sizeof(h.fast));
  for (int s = 0; s < n; s++) h.count[lengths[s]]++;
  h.count[0] = 0;
  int left = 1;
  for (int len = 1; len < 16; len++) {
    left <<= 1;
    left -= h.count[len];
    if (left < 0) return false;  // over-subscribed
  }
  int offs[16];
  offs[1] = 0;
  for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
  for (int s = 0; s < n; s++)
    if (lengths[s]) h.symbols[offs[lengths[s]]++] = static_cast<std::uint16_t>(s);
  // the fast table: canonical codes, bit-reversed (the stream gives a code's first bit first)
  int code = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    for (int i = 0; i < h.count[len]; i++, index++, code++) {
      if (len > kFastBits) continue;
      int rev = 0;
      for (int b = 0; b < len; b++) rev |= ((code >> b) & 1) << (len - 1 - b);
      for (int r = rev; r < (1 << kFastBits); r += 1 << len)
        h.fast[r] = static_cast<std::uint16_t>(h.symbols[index] << 4 | len);
    }
    code <<= 1;
  }
  return true;
}

int Decode(BitReader &br, const Huffman &h) {
  br.Fill(16);
  const std::uint16_t e = h.fast[br.buf & ((1 << kFastBits) - 1)];
  if (e) {
    const int len = e & 15;
    br.buf >>= len;
    br.cnt -= len;
    return e >> 4;
  }
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    code |= static_cast<int>(br.Get(1));
    const int count = h.count[len];
    if (code - count < first) return h.symbols[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

const std::uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                     193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace

namespace {

// a deflate stream; partial: out holds only the stream's first bytes, it ends when out is full
bool InflateStream(const std::uint8_t *data, std::size_t n, std::vector<std::uint8_t> &out, std::string &error,
                   bool partial) {
  BitReader br{data, n};
  std::size_t at = 0;
  const std::size_t limit = out.size();
  static Huffman fixedLit, fixedDist;
  static bool fixedReady = [] {
    std::uint8_t l[288];
    for (int i = 0; i < 144; i++) l[i] = 8;
    for (int i = 144; i < 256; i++) l[i] = 9;
    for (int i = 256; i < 280; i++) l[i] = 7;
    for (int i = 280; i < 288; i++) l[i] = 8;
    BuildHuffman(l, 288, fixedLit);
    std::uint8_t d[30];
    for (int i = 0; i < 30; i++) d[i] = 5;
    BuildHuffman(d, 30, fixedDist);
    return true;
  }();
  (void)fixedReady;
  Huffman lit, dist;
  bool final = false;
  while (!final) {
    final = br.Get(1) != 0;
    const unsigned type = br.Get(2);
    if (type == 0) {
      br.Get(br.cnt & 7);
      const unsigned len = br.Get(16), nlen = br.Get(16);
      if ((len ^ 0xFFFF) != nlen) {
        error = "bad stored block";
        return false;
      }
      if (at + len > limit) {
        if (partial) {
          while (at < limit) out[at++] = static_cast<std::uint8_t>(br.Get(8));
          return true;
        }
        error = "more data than expected";
        return false;
      }
      for (unsigned i = 0; i < len; i++) out[at++] = static_cast<std::uint8_t>(br.Get(8));
    } else if (type == 1 || type == 2) {
      const Huffman *pl = &fixedLit, *pd = &fixedDist;
      if (type == 2) {
        const int hlit = static_cast<int>(br.Get(5)) + 257, hdist = static_cast<int>(br.Get(5)) + 1,
                  hclen = static_cast<int>(br.Get(4)) + 4;
        static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        std::uint8_t cl[19] = {};
        for (int i = 0; i < hclen; i++) cl[order[i]] = static_cast<std::uint8_t>(br.Get(3));
        Huffman clh;
        if (hlit > 286 || hdist > 30 || !BuildHuffman(cl, 19, clh)) {
          error = "bad code lengths";
          return false;
        }
        std::uint8_t lengths[320] = {};
        int i = 0;
        while (i < hlit + hdist) {
          const int sym = Decode(br, clh);
          if (sym < 0) {
            error = "bad code length code";
            return false;
          }
          if (sym < 16) {
            lengths[i++] = static_cast<std::uint8_t>(sym);
          } else {
            int repeat = 0, value = 0;
            if (sym == 16) {
              if (i == 0) {
                error = "repeat without a length";
                return false;
              }
              value = lengths[i - 1];
              repeat = 3 + static_cast<int>(br.Get(2));
            } else if (sym == 17) {
              repeat = 3 + static_cast<int>(br.Get(3));
            } else {
              repeat = 11 + static_cast<int>(br.Get(7));
            }
            if (i + repeat > hlit + hdist) {
              error = "too many code lengths";
              return false;
            }
            while (repeat--) lengths[i++] = static_cast<std::uint8_t>(value);
          }
        }
        if (!BuildHuffman(lengths, hlit, lit) || !BuildHuffman(lengths + hlit, hdist, dist)) {
          error = "bad Huffman table";
          return false;
        }
        pl = &lit;
        pd = &dist;
      }
      for (;;) {
        const int sym = Decode(br, *pl);
        if (sym < 0 || br.overrun > 8) {
          error = "bad data";
          return false;
        }
        if (sym < 256) {
          if (at >= limit) {
            if (partial) return true;
            error = "more data than expected";
            return false;
          }
          out[at++] = static_cast<std::uint8_t>(sym);
        } else if (sym == 256) {
          break;
        } else {
          const int li = sym - 257;
          if (li >= 29) {
            error = "bad length";
            return false;
          }
          const std::size_t len = kLenBase[li] + br.Get(kLenExtra[li]);
          const int ds = Decode(br, *pd);
          if (ds < 0 || ds >= 30) {
            error = "bad distance";
            return false;
          }
          const std::size_t d = kDistBase[ds] + br.Get(kDistExtra[ds]);
          if (d <= at && at + len > limit && partial) {
            for (; at < limit; at++) out[at] = out[at - d];
            return true;
          }
          if (d > at || at + len > limit) {
            error = d > at ? "distance too far" : "more data than expected";
            return false;
          }
          for (std::size_t k = 0; k < len; k++, at++) out[at] = out[at - d];
        }
      }
    } else {
      error = "bad block type";
      return false;
    }
    if (br.overrun > 8) {
      error = "data cut short";
      return false;
    }
  }
  if (at != limit) {
    error = "less data than expected";
    return false;
  }
  return true;
}

}  // namespace

bool Inflate(const std::uint8_t *data, std::size_t n, std::vector<std::uint8_t> &out, std::string &error) {
  if (n < 2 || (data[0] & 15) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20)) {
    error = "bad zlib header";
    return false;
  }
  return InflateStream(data + 2, n - 2, out, error, false);
}

bool InflateRaw(const std::uint8_t *data, std::size_t n, std::vector<std::uint8_t> &out, std::string &error, bool partial) {
  return InflateStream(data, n, out, error, partial);
}

namespace {

// ---------------- PNG ----------------

int Paeth(int a, int b, int c) {
  const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

bool Unfilter(std::uint8_t *rows, unsigned height, std::size_t stride, unsigned bpp, std::string &error) {
  // rows: height * (1 + stride) bytes, filtered in place to the raw bytes (filter bytes kept)
  std::vector<std::uint8_t> zero(stride, 0);
  const std::uint8_t *prior = zero.data();
  for (unsigned y = 0; y < height; y++) {
    std::uint8_t *row = rows + y * (stride + 1);
    const int filter = row[0];
    std::uint8_t *cur = row + 1;
    switch (filter) {
      case 0: break;
      case 1:
        for (std::size_t i = bpp; i < stride; i++) cur[i] = static_cast<std::uint8_t>(cur[i] + cur[i - bpp]);
        break;
      case 2:
        for (std::size_t i = 0; i < stride; i++) cur[i] = static_cast<std::uint8_t>(cur[i] + prior[i]);
        break;
      case 3:
        for (std::size_t i = 0; i < stride; i++) {
          const int left = i >= bpp ? cur[i - bpp] : 0;
          cur[i] = static_cast<std::uint8_t>(cur[i] + ((left + prior[i]) >> 1));
        }
        break;
      case 4:
        for (std::size_t i = 0; i < stride; i++) {
          const int left = i >= bpp ? cur[i - bpp] : 0, up = prior[i], ul = i >= bpp ? prior[i - bpp] : 0;
          cur[i] = static_cast<std::uint8_t>(cur[i] + Paeth(left, up, ul));
        }
        break;
      default:
        error = "bad PNG filter";
        return false;
    }
    prior = cur;
  }
  return true;
}

}  // namespace

bool LoadPng(const std::uint8_t *d, std::size_t n, Loaded &out, std::string &error) {
  static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (n < 8 || std::memcmp(d, sig, 8) != 0) {
    error = "not a PNG file";
    return false;
  }
  unsigned w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
  std::vector<std::uint8_t> idat, plte, trns;
  bool haveHeader = false;
  std::size_t at = 8;
  while (at + 12 <= n) {
    const std::uint32_t len = Be32(d + at);
    const std::uint8_t *type = d + at + 4;
    if (len > n - at - 12) {
      error = "PNG chunk cut short";
      return false;
    }
    const std::uint8_t *body = d + at + 8;
    if (!std::memcmp(type, "IHDR", 4) && len >= 13) {
      w = Be32(body);
      h = Be32(body + 4);
      depth = body[8];
      ctype = body[9];
      interlace = body[12];
      haveHeader = true;
      if (body[10] != 0 || body[11] != 0 || interlace > 1) {
        error = "PNG compression, filter or interlace method unknown";
        return false;
      }
    } else if (!std::memcmp(type, "PLTE", 4)) {
      plte.assign(body, body + len);
    } else if (!std::memcmp(type, "tRNS", 4)) {
      trns.assign(body, body + len);
    } else if (!std::memcmp(type, "IDAT", 4)) {
      idat.insert(idat.end(), body, body + len);
    } else if (!std::memcmp(type, "IEND", 4)) {
      break;
    }
    at += 12 + len;
  }
  if (!haveHeader || w == 0 || h == 0 || w > 16384 || h > 16384) {
    error = "PNG header missing or size out of range";
    return false;
  }
  unsigned channels = 0;
  switch (ctype) {
    case 0: channels = 1; break;
    case 2: channels = 3; break;
    case 3: channels = 1; break;
    case 4: channels = 2; break;
    case 6: channels = 4; break;
    default: error = "PNG colour type unknown"; return false;
  }
  const bool depthOk = (ctype == 0 && (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) ||
                       (ctype == 3 && (depth == 1 || depth == 2 || depth == 4 || depth == 8)) ||
                       ((ctype == 2 || ctype == 4 || ctype == 6) && (depth == 8 || depth == 16));
  if (!depthOk || (ctype == 3 && plte.size() < 3)) {
    error = "PNG bit depth or palette invalid";
    return false;
  }
  const unsigned bitsPerPixel = channels * depth, bpp = std::max(1u, bitsPerPixel / 8);
  struct Pass {
    unsigned x0, y0, dx, dy;
  };
  static const Pass adam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
  static const Pass whole[1] = {{0, 0, 1, 1}};
  const Pass *passes = interlace ? adam7 : whole;
  const int passCount = interlace ? 7 : 1;
  std::size_t total = 0;
  for (int p = 0; p < passCount; p++) {
    const unsigned pw = w > passes[p].x0 ? (w - passes[p].x0 + passes[p].dx - 1) / passes[p].dx : 0;
    const unsigned ph = h > passes[p].y0 ? (h - passes[p].y0 + passes[p].dy - 1) / passes[p].dy : 0;
    if (pw && ph) total += static_cast<std::size_t>(ph) * (1 + (static_cast<std::size_t>(pw) * bitsPerPixel + 7) / 8);
  }
  std::vector<std::uint8_t> raw(total);
  if (!Inflate(idat.data(), idat.size(), raw, error)) {
    error = "PNG data: " + error;
    return false;
  }
  Image image;
  image.Resize(w, h);
  // palette with alpha
  std::uint8_t pal[256][4] = {};
  if (ctype == 3)
    for (unsigned i = 0; i < 256; i++) {
      if (i * 3 + 2 < plte.size()) {
        pal[i][0] = plte[i * 3];
        pal[i][1] = plte[i * 3 + 1];
        pal[i][2] = plte[i * 3 + 2];
      }
      pal[i][3] = i < trns.size() ? trns[i] : 255;
    }
  const int keyGray = (ctype == 0 && trns.size() >= 2) ? (trns[0] << 8 | trns[1]) : -1;
  const int keyR = (ctype == 2 && trns.size() >= 6) ? (trns[0] << 8 | trns[1]) : -1;
  const int keyG = keyR >= 0 ? (trns[2] << 8 | trns[3]) : -1, keyB = keyR >= 0 ? (trns[4] << 8 | trns[5]) : -1;
  std::size_t off = 0;
  for (int p = 0; p < passCount; p++) {
    const Pass &ps = passes[p];
    const unsigned pw = w > ps.x0 ? (w - ps.x0 + ps.dx - 1) / ps.dx : 0;
    const unsigned ph = h > ps.y0 ? (h - ps.y0 + ps.dy - 1) / ps.dy : 0;
    if (!pw || !ph) continue;
    const std::size_t stride = (static_cast<std::size_t>(pw) * bitsPerPixel + 7) / 8;
    std::uint8_t *rows = raw.data() + off;
    if (!Unfilter(rows, ph, stride, bpp, error)) return false;
    off += static_cast<std::size_t>(ph) * (stride + 1);
    for (unsigned y = 0; y < ph; y++) {
      const std::uint8_t *row = rows + y * (stride + 1) + 1;
      for (unsigned x = 0; x < pw; x++) {
        // a sample at the file's depth
        auto sample = [&](unsigned c) -> int {
          if (depth == 8) return row[x * channels + c];
          if (depth == 16) return row[(x * channels + c) * 2] << 8 | row[(x * channels + c) * 2 + 1];
          const unsigned bit = (x * channels + c) * depth;
          return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
        };
        auto to8 = [&](int v) -> int {
          if (depth == 16) return v >> 8;
          if (depth == 8) return v;
          return v * 255 / ((1 << depth) - 1);
        };
        std::uint8_t *o = image.px.data() + (static_cast<std::size_t>(ps.y0 + y * ps.dy) * w + ps.x0 + x * ps.dx) * 4;
        switch (ctype) {
          case 0: {
            const int s = sample(0);
            o[0] = o[1] = o[2] = static_cast<std::uint8_t>(to8(s));
            o[3] = s == keyGray ? 0 : 255;
            break;
          }
          case 2: {
            const int r = sample(0), g = sample(1), b = sample(2);
            o[0] = static_cast<std::uint8_t>(to8(r));
            o[1] = static_cast<std::uint8_t>(to8(g));
            o[2] = static_cast<std::uint8_t>(to8(b));
            o[3] = (r == keyR && g == keyG && b == keyB) ? 0 : 255;
            break;
          }
          case 3: std::memcpy(o, pal[sample(0) & 255], 4); break;
          case 4:
            o[0] = o[1] = o[2] = static_cast<std::uint8_t>(to8(sample(0)));
            o[3] = static_cast<std::uint8_t>(to8(sample(1)));
            break;
          default:
            for (unsigned c = 0; c < 4; c++) o[c] = static_cast<std::uint8_t>(to8(sample(c)));
            break;
        }
      }
    }
  }
  out = Loaded{};
  out.fileWidth = w;
  out.fileHeight = h;
  out.fileLevels = 1;
  out.format = "PNG";
  out.alpha = AlphaOf(image);
  out.levels.push_back(std::move(image));
  return true;
}

namespace {

// the last level that is still at least maxSide on its longer side: its number, size and file offset
void FirstLevel(const DdsInfo &info, unsigned maxSide, unsigned &first, unsigned &w, unsigned &h, std::size_t &at) {
  first = 0;
  w = info.w;
  h = info.h;
  at = info.dataStart;
  while (maxSide && first + 1 < info.levels && std::max(std::max(1u, w / 2), std::max(1u, h / 2)) >= maxSide) {
    at += DdsLevelBytes(info.format, info.raw, w, h);
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
    first++;
  }
}

// data: the file's bytes from the first wanted level on
void DecodeFrom(const DdsInfo &info, unsigned first, unsigned w, unsigned h, const std::uint8_t *data, bool allLevels,
                Loaded &out) {
  out = Loaded{};
  out.fileWidth = info.w;
  out.fileHeight = info.h;
  out.fileLevels = info.levels;
  out.format = info.name;
  std::size_t at = 0;
  for (unsigned l = first; l < info.levels; l++) {
    Image level;
    DecodeDdsLevel(info.format, info.raw, data + at, w, h, level);
    out.levels.push_back(std::move(level));
    if (!allLevels) break;
    at += DdsLevelBytes(info.format, info.raw, w, h);
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }
  out.alpha = AlphaOf(out.levels[0]);
}

}  // namespace

bool LoadDds(const std::uint8_t *d, std::size_t n, unsigned maxSide, bool allLevels, Loaded &out, std::string &error) {
  DdsInfo info;
  if (!ParseDds(d, n, info, error)) return false;
  unsigned first, w, h;
  std::size_t at;
  FirstLevel(info, maxSide, first, w, h, at);
  DecodeFrom(info, first, w, h, d + at, allLevels, out);
  return true;
}

bool Load(const std::string &path, unsigned maxSide, bool allLevels, Loaded &out, std::string &error) {
  FILE *f = fio::Open(path, "rb");
  if (!f) {
    error = "cannot open the file";
    return false;
  }
  const std::uint64_t size = fio::Size(f);
  std::vector<std::uint8_t> head;
  if (size < 8 || size >= (1ull << 31) ||
      !fio::ReadRange(f, 0, static_cast<std::size_t>(std::min<std::uint64_t>(size, 148)), head)) {
    std::fclose(f);
    error = size >= (1ull << 31) ? "file too large" : "cannot read the file";
    return false;
  }
  bool ok = false;
  if (head.size() >= 4 && !std::memcmp(head.data(), "DDS ", 4)) {
    // the header gives the levels; only the part from the wanted level on is read
    DdsInfo info;
    head.resize(148, 0);
    if (ParseDds(head.data(), static_cast<std::size_t>(size), info, error)) {
      unsigned first, w, h;
      std::size_t at;
      FirstLevel(info, maxSide, first, w, h, at);
      std::vector<std::uint8_t> data;
      if (fio::ReadRange(f, at, static_cast<std::size_t>(size - at), data)) {
        DecodeFrom(info, first, w, h, data.data(), allLevels, out);
        ok = true;
      } else {
        error = "cannot read the file";
      }
    }
  } else {
    std::vector<std::uint8_t> all;
    if (!fio::ReadRange(f, 0, static_cast<std::size_t>(size), all))
      error = "cannot read the file";
    else if (all.size() >= 3 && all[0] == 0xFF && all[1] == 0xD8 && all[2] == 0xFF)
      ok = LoadJpeg(all.data(), all.size(), out, error);
    else
      ok = LoadPng(all.data(), all.size(), out, error);
  }
  std::fclose(f);
  return ok;
}

bool ProbeData(const std::uint8_t *head, std::size_t headBytes, std::uint64_t fileBytes, unsigned maxSide, unsigned &w,
               unsigned &h, int *bitsPerTexel) {
  if (headBytes < 24) return false;
  if (!std::memcmp(head, "DDS ", 4)) {
    std::vector<std::uint8_t> h148(head, head + std::min<std::size_t>(headBytes, 148));
    h148.resize(148, 0);
    DdsInfo info;
    std::string error;
    if (!ParseDds(h148.data(), static_cast<std::size_t>(std::min<std::uint64_t>(fileBytes, 1ull << 31)), info, error))
      return false;
    unsigned first;
    std::size_t at;
    FirstLevel(info, maxSide, first, w, h, at);
    if (bitsPerTexel) {
      // what ETC2 needs at most: 4 bits without soft alpha (BC1's is cut-out), 8 with
      const bool soft = info.format == Dds::kBc2 || info.format == Dds::kBc3 || info.format == Dds::kBc7 ||
                        (info.format == Dds::kRaw && info.raw.mask[3]);
      *bitsPerTexel = soft ? 8 : 4;
    }
    return true;
  }
  if (head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF) {
    if (bitsPerTexel) *bitsPerTexel = 4;
    return JpegSize(head, headBytes, w, h);
  }
  if (head[1] == 'P' && head[2] == 'N' && head[3] == 'G') {
    w = Be32(head + 16);
    h = Be32(head + 20);
    if (bitsPerTexel) *bitsPerTexel = (head[25] == 4 || head[25] == 6 || head[25] == 3) ? 8 : 4;
    return w && h && w <= 16384 && h <= 16384;
  }
  return false;
}

bool Probe(const std::string &path, unsigned maxSide, unsigned &w, unsigned &h) {
  FILE *f = fio::Open(path, "rb");
  if (!f) return false;
  const std::uint64_t size = fio::Size(f);
  std::vector<std::uint8_t> head;
  bool read = fio::ReadRange(f, 0, static_cast<std::size_t>(std::min<std::uint64_t>(size, 148)), head);
  // a JPEG's frame header can come after big APP segments: the start of the file
  if (read && head.size() >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF)
    read = fio::ReadRange(f, 0, static_cast<std::size_t>(std::min<std::uint64_t>(size, 1u << 20)), head);
  std::fclose(f);
  return read && ProbeData(head.data(), head.size(), size, maxSide, w, h, nullptr);
}

bool LoadData(const std::uint8_t *data, std::size_t n, unsigned maxSide, bool allLevels, Loaded &out, std::string &error) {
  if (n >= 4 && !std::memcmp(data, "DDS ", 4)) return LoadDds(data, n, maxSide, allLevels, out, error);
  if (n >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return LoadJpeg(data, n, out, error);
  return LoadPng(data, n, out, error);
}

// ---------------- PNG writer ----------------

std::vector<std::uint8_t> EncodePng(const Image &image) {
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(image.h) * (image.w * 4 + 1));
  for (unsigned y = 0; y < image.h; y++) {
    raw.push_back(0);
    const std::uint8_t *row = image.px.data() + static_cast<std::size_t>(y) * image.w * 4;
    raw.insert(raw.end(), row, row + image.w * 4);
  }
  std::vector<std::uint8_t> z = {0x78, 0x01};
  std::size_t at = 0;
  do {
    const std::size_t len = std::min<std::size_t>(65535, raw.size() - at);
    z.push_back(at + len == raw.size() ? 1 : 0);
    z.push_back(static_cast<std::uint8_t>(len));
    z.push_back(static_cast<std::uint8_t>(len >> 8));
    z.push_back(static_cast<std::uint8_t>(~len));
    z.push_back(static_cast<std::uint8_t>(~len >> 8));
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + len));
    at += len;
  } while (at < raw.size());
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  const std::uint32_t adler = b << 16 | a;
  for (int i = 3; i >= 0; i--) z.push_back(static_cast<std::uint8_t>(adler >> (8 * i)));
  std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  auto chunk = [&](const char *type, const std::vector<std::uint8_t> &body) {
    const std::uint32_t len = static_cast<std::uint32_t>(body.size());
    for (int i = 3; i >= 0; i--) png.push_back(static_cast<std::uint8_t>(len >> (8 * i)));
    std::vector<std::uint8_t> crcData(type, type + 4);
    crcData.insert(crcData.end(), body.begin(), body.end());
    png.insert(png.end(), crcData.begin(), crcData.end());
    const std::uint32_t crc = c4tex::Crc32(crcData.data(), crcData.size());
    for (int i = 3; i >= 0; i--) png.push_back(static_cast<std::uint8_t>(crc >> (8 * i)));
  };
  std::vector<std::uint8_t> ihdr(13, 0);
  for (int i = 0; i < 4; i++) {
    ihdr[i] = static_cast<std::uint8_t>(image.w >> (24 - 8 * i));
    ihdr[4 + i] = static_cast<std::uint8_t>(image.h >> (24 - 8 * i));
  }
  ihdr[8] = 8;
  ihdr[9] = 6;
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  return png;
}

bool SavePng(const std::string &path, const Image &image) { return fio::WriteFile(path, EncodePng(image)); }

// ---------------- filters ----------------

namespace {

double Mitchell(double x) {
  const double B = 1.0 / 3.0, C = 1.0 / 3.0;
  x = std::fabs(x);
  if (x < 1.0) return ((12 - 9 * B - 6 * C) * x * x * x + (-18 + 12 * B + 6 * C) * x * x + (6 - 2 * B)) / 6.0;
  if (x < 2.0)
    return ((-B - 6 * C) * x * x * x + (6 * B + 30 * C) * x * x + (-12 * B - 48 * C) * x + (8 * B + 24 * C)) / 6.0;
  return 0.0;
}

struct Taps {
  std::vector<int> first;
  std::vector<int> count;
  std::vector<float> weight;  // count[i] weights for output i, one after the other
  std::vector<int> start;
};

Taps MakeTaps(unsigned src, unsigned dst) {
  Taps t;
  const double scale = static_cast<double>(src) / dst;
  const double support = 2.0 * std::max(1.0, scale);
  const double filterScale = std::max(1.0, scale);
  t.first.resize(dst);
  t.count.resize(dst);
  t.start.resize(dst);
  for (unsigned i = 0; i < dst; i++) {
    const double centre = (i + 0.5) * scale;
    int lo = static_cast<int>(std::floor(centre - support));
    int hi = static_cast<int>(std::ceil(centre + support));
    std::vector<float> w;
    double sum = 0;
    for (int s = lo; s <= hi; s++) {
      const double v = Mitchell((s + 0.5 - centre) / filterScale);
      w.push_back(static_cast<float>(v));
      sum += v;
    }
    if (sum == 0) sum = 1;
    t.first[i] = lo;
    t.count[i] = hi - lo + 1;
    t.start[i] = static_cast<int>(t.weight.size());
    for (float v : w) t.weight.push_back(static_cast<float>(v / sum));
  }
  return t;
}

}  // namespace

Image Downscale(const Image &src, unsigned nw, unsigned nh) {
  nw = std::max(1u, nw);
  nh = std::max(1u, nh);
  if (nw == src.w && nh == src.h) return src;
  const Taps tx = MakeTaps(src.w, nw), ty = MakeTaps(src.h, nh);
  // per pixel: premultiplied r g b, alpha, straight r g b
  constexpr int kC = 7;
  std::vector<float> mid(static_cast<std::size_t>(nw) * src.h * kC);
  for (unsigned y = 0; y < src.h; y++) {
    const std::uint8_t *row = src.px.data() + static_cast<std::size_t>(y) * src.w * 4;
    for (unsigned x = 0; x < nw; x++) {
      float acc[kC] = {};
      for (int k = 0; k < tx.count[x]; k++) {
        const int sx = std::clamp(tx.first[x] + k, 0, static_cast<int>(src.w) - 1);
        const float wgt = tx.weight[tx.start[x] + k];
        const std::uint8_t *p = row + sx * 4;
        const float a = p[3] / 255.0f;
        acc[0] += wgt * p[0] * a;
        acc[1] += wgt * p[1] * a;
        acc[2] += wgt * p[2] * a;
        acc[3] += wgt * a;
        acc[4] += wgt * p[0];
        acc[5] += wgt * p[1];
        acc[6] += wgt * p[2];
      }
      std::memcpy(&mid[(static_cast<std::size_t>(y) * nw + x) * kC], acc, sizeof(acc));
    }
  }
  Image out;
  out.Resize(nw, nh);
  for (unsigned y = 0; y < nh; y++)
    for (unsigned x = 0; x < nw; x++) {
      float acc[kC] = {};
      for (int k = 0; k < ty.count[y]; k++) {
        const int sy = std::clamp(ty.first[y] + k, 0, static_cast<int>(src.h) - 1);
        const float wgt = ty.weight[ty.start[y] + k];
        const float *p = &mid[(static_cast<std::size_t>(sy) * nw + x) * kC];
        for (int c = 0; c < kC; c++) acc[c] += wgt * p[c];
      }
      std::uint8_t *o = out.px.data() + (static_cast<std::size_t>(y) * nw + x) * 4;
      const float a = acc[3];
      for (int c = 0; c < 3; c++) {
        const float v = a > 1.0f / 512 ? acc[c] / a : acc[4 + c];
        o[c] = static_cast<std::uint8_t>(Clamp255(static_cast<int>(std::lround(v))));
      }
      o[3] = static_cast<std::uint8_t>(Clamp255(static_cast<int>(std::lround(a * 255.0f))));
    }
  return out;
}

Image HalfSize(const Image &src) {
  const unsigned nw = std::max(1u, src.w / 2), nh = std::max(1u, src.h / 2);
  const bool box = (src.w % 2 == 0 || src.w == 1) && (src.h % 2 == 0 || src.h == 1);
  if (!box) return Downscale(src, nw, nh);
  Image out;
  out.Resize(nw, nh);
  const unsigned fx = src.w > 1 ? 2 : 1, fy = src.h > 1 ? 2 : 1;
  for (unsigned y = 0; y < nh; y++)
    for (unsigned x = 0; x < nw; x++) {
      int sumA = 0, pre[3] = {}, plain[3] = {}, n = 0;
      for (unsigned dy = 0; dy < fy; dy++)
        for (unsigned dx = 0; dx < fx; dx++) {
          const std::uint8_t *p = src.px.data() + (static_cast<std::size_t>(y * fy + dy) * src.w + x * fx + dx) * 4;
          for (int c = 0; c < 3; c++) {
            pre[c] += p[c] * p[3];
            plain[c] += p[c];
          }
          sumA += p[3];
          n++;
        }
      std::uint8_t *o = out.px.data() + (static_cast<std::size_t>(y) * nw + x) * 4;
      for (int c = 0; c < 3; c++) o[c] = static_cast<std::uint8_t>(sumA ? (pre[c] + sumA / 2) / sumA : (plain[c] + n / 2) / n);
      o[3] = static_cast<std::uint8_t>((sumA + n / 2) / n);
    }
  return out;
}

double Coverage(const Image &image, int threshold) {
  const std::size_t n = static_cast<std::size_t>(image.w) * image.h;
  if (!n) return 0;
  std::size_t c = 0;
  for (std::size_t i = 0; i < n; i++) c += image.px[i * 4 + 3] >= threshold;
  return static_cast<double>(c) / n;
}

void KeepCoverage(Image &image, int threshold, double target) {
  const std::size_t n = static_cast<std::size_t>(image.w) * image.h;
  auto coverageAt = [&](double scale) {
    std::size_t c = 0;
    for (std::size_t i = 0; i < n; i++) c += std::min(255.0, image.px[i * 4 + 3] * scale) >= threshold;
    return static_cast<double>(c) / n;
  };
  double lo = 0, hi = 8, best = 1, bestDiff = std::fabs(coverageAt(1) - target);
  for (int i = 0; i < 14; i++) {
    const double mid = (lo + hi) / 2, c = coverageAt(mid);
    if (std::fabs(c - target) < bestDiff) {
      bestDiff = std::fabs(c - target);
      best = mid;
    }
    if (c < target)
      lo = mid;
    else
      hi = mid;
  }
  if (best == 1) return;
  for (std::size_t i = 0; i < n; i++)
    image.px[i * 4 + 3] = static_cast<std::uint8_t>(std::min(255.0, std::round(image.px[i * 4 + 3] * best)));
}

Alpha AlphaOf(const Image &image) {
  bool any = false;
  const std::size_t n = static_cast<std::size_t>(image.w) * image.h;
  for (std::size_t i = 0; i < n; i++) {
    const int a = image.px[i * 4 + 3];
    if (a != 255) {
      if (a != 0) return Alpha::kSoft;
      any = true;
    }
  }
  return any ? Alpha::kBinary : Alpha::kNone;
}

}  // namespace img
