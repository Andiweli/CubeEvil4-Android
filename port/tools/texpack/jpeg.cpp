// JPEG reader of the texture tool (our own code, CC0): baseline and extended sequential (Huffman) and progressive
// JPEG, 8 bits, grey (1 component) or YCbCr / RGB (3 components), any sampling factors, restart markers. Not: 12-bit,
// arithmetic coding, lossless, hierarchical, CMYK/YCCK (a clear message). After ITU-T T.81; checked against Pillow
// (libjpeg-turbo) with tests/make_bc7_jpeg_fixtures.py.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "image.h"

namespace img {
namespace {

const int kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                         41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                         30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct Huffman {
  bool defined = false;
  std::uint8_t fastLen[512] = {};  // 9-bit lookahead: code length (0: longer code)
  std::uint8_t fastSym[512] = {};
  int maxcode[18] = {};
  int valptr[17] = {}, mincode[17] = {};
  std::uint8_t values[256] = {};
};

bool BuildHuffman(const std::uint8_t counts[16], const std::uint8_t *values, int n, Huffman &h) {
  h = Huffman{};
  std::memcpy(h.values, values, static_cast<std::size_t>(n));
  int code = 0, k = 0;
  for (int len = 1; len <= 16; len++) {
    h.valptr[len] = k;
    h.mincode[len] = code;
    for (int i = 0; i < counts[len - 1]; i++, k++, code++) {
      if (code >= (1 << len)) return false;
      if (len <= 9) {
        const int shift = 9 - len;
        for (int f = 0; f < (1 << shift); f++) {
          h.fastLen[(code << shift) | f] = static_cast<std::uint8_t>(len);
          h.fastSym[(code << shift) | f] = values[k];
        }
      }
    }
    h.maxcode[len] = counts[len - 1] ? code - 1 : -1;
    code <<= 1;
  }
  h.maxcode[17] = 0x7FFFFFFF;
  h.defined = true;
  return k == n;
}

// the entropy-coded data: bits MSB first, 0xFF00 is 0xFF, a marker stops the bits (zeros follow)
struct Bits {
  const std::uint8_t *p = nullptr;
  std::size_t n = 0, pos = 0;
  std::uint32_t buf = 0;
  int cnt = 0;
  bool marker = false;
  void Fill() {
    while (cnt <= 24) {
      std::uint32_t b = 0;
      if (!marker && pos < n) {
        b = p[pos];
        if (b == 0xFF) {
          const std::uint8_t next = pos + 1 < n ? p[pos + 1] : 0xD9;
          if (next == 0x00) {
            pos += 2;
          } else {
            marker = true;
            b = 0;
          }
        } else {
          pos++;
        }
      }
      buf |= b << (24 - cnt);
      cnt += 8;
    }
  }
  int Get(int bits) {
    if (bits == 0) return 0;
    Fill();
    const int v = static_cast<int>(buf >> (32 - bits));
    buf <<= bits;
    cnt -= bits;
    return v;
  }
  int Decode(const Huffman &h) {
    Fill();
    const unsigned look = buf >> (32 - 9);
    if (h.fastLen[look]) {
      const int len = h.fastLen[look];
      buf <<= len;
      cnt -= len;
      return h.fastSym[look];
    }
    for (int len = 10; len <= 16; len++) {
      const int code = static_cast<int>(buf >> (32 - len));
      if (code <= h.maxcode[len]) {
        buf <<= len;
        cnt -= len;
        const int idx = h.valptr[len] + code - h.mincode[len];
        return idx >= 0 && idx < 256 ? h.values[idx] : 0;
      }
    }
    buf <<= 16;  // a code that does not exist: the data is damaged
    cnt -= 16;
    return 0;
  }
  // a restart: the rest of the byte goes, the RSTn marker is passed
  void Restart() {
    buf = 0;
    cnt = 0;
    if (!marker) {
      // find the marker (the padding bits of the last byte are skipped)
      while (pos + 1 < n && !(p[pos] == 0xFF && p[pos + 1] != 0x00 && p[pos + 1] != 0xFF)) pos++;
    }
    if (pos + 1 < n && p[pos] == 0xFF && p[pos + 1] >= 0xD0 && p[pos + 1] <= 0xD7) pos += 2;
    marker = false;
  }
};

inline int Extend(int v, int s) { return s && v < (1 << (s - 1)) ? v - (1 << s) + 1 : v; }

struct Component {
  int id = 0, h = 1, v = 1, tq = 0;
  int blocksW = 0, blocksH = 0;      // the coefficient grid (whole MCUs)
  int usedW = 0, usedH = 0;          // blocks with picture in them (a single-component scan's grid)
  int pixW = 0, pixH = 0;            // the component's samples (ceil(width * h / hmax) ...)
  std::vector<std::int16_t> coef;    // blocksW * blocksH blocks of 64, natural order
  int dcPred = 0;
  int dcTable = 0, acTable = 0;
};

struct Decoder {
  int width = 0, height = 0;
  int hmax = 1, vmax = 1, mcusX = 0, mcusY = 0;
  bool progressive = false;
  int adobeTransform = -1;
  int restartInterval = 0;
  std::vector<Component> comps;
  std::uint16_t qt[4][64] = {};      // natural order
  Huffman dc[4], ac[4];
  int eobrun = 0;
  std::string error;

  std::int16_t *Block(Component &c, int bx, int by) {
    return c.coef.data() + (static_cast<std::size_t>(by) * c.blocksW + bx) * 64;
  }

  void DecodeBlock(Bits &b, Component &c, std::int16_t *blk, int ss, int se, int ah, int al) {
    if (!progressive) {
      const int s = b.Decode(dc[c.dcTable]);
      c.dcPred += s ? Extend(b.Get(s), s) : 0;
      blk[0] = static_cast<std::int16_t>(c.dcPred);
      for (int k = 1; k < 64;) {
        const int rs = b.Decode(ac[c.acTable]);
        const int r = rs >> 4, sz = rs & 15;
        if (!sz) {
          if (r != 15) break;
          k += 16;
          continue;
        }
        k += r;
        if (k > 63) break;
        blk[kZigzag[k]] = static_cast<std::int16_t>(Extend(b.Get(sz), sz));
        k++;
      }
      return;
    }
    if (ss == 0) {
      // DC scans
      if (ah == 0) {
        const int s = b.Decode(dc[c.dcTable]);
        c.dcPred += s ? Extend(b.Get(s), s) : 0;
        blk[0] = static_cast<std::int16_t>(c.dcPred * (1 << al));
      } else if (b.Get(1)) {
        blk[0] = static_cast<std::int16_t>(blk[0] | (1 << al));
      }
      return;
    }
    if (ah == 0) {
      // AC first
      if (eobrun > 0) {
        eobrun--;
        return;
      }
      for (int k = ss; k <= se;) {
        const int rs = b.Decode(ac[c.acTable]);
        const int r = rs >> 4, sz = rs & 15;
        if (!sz) {
          if (r < 15) {
            eobrun = (1 << r) - 1;
            if (r) eobrun += b.Get(r);
            break;
          }
          k += 16;
          continue;
        }
        k += r;
        if (k > 63) break;
        blk[kZigzag[k]] = static_cast<std::int16_t>(Extend(b.Get(sz), sz) * (1 << al));
        k++;
      }
      return;
    }
    // AC refinement (as T.81 G.1.2.3; the order of libjpeg's decode_mcu_AC_refine)
    const int p1 = 1 << al, m1 = -(1 << al);
    int k = ss;
    if (eobrun <= 0) {
      for (; k <= se; k++) {
        const int rs = b.Decode(ac[c.acTable]);
        int r = rs >> 4, s = rs & 15;
        if (s) {
          s = b.Get(1) ? p1 : m1;
        } else if (r != 15) {
          eobrun = 1 << r;
          if (r) eobrun += b.Get(r);
          break;
        }
        do {
          std::int16_t &coef = blk[kZigzag[k]];
          if (coef != 0) {
            if (b.Get(1) && (coef & p1) == 0) coef = static_cast<std::int16_t>(coef >= 0 ? coef + p1 : coef + m1);
          } else {
            if (--r < 0) break;
          }
          k++;
        } while (k <= se);
        if (s && k <= 63) blk[kZigzag[k]] = static_cast<std::int16_t>(s);
      }
    }
    if (eobrun > 0) {
      for (; k <= se; k++) {
        std::int16_t &coef = blk[kZigzag[k]];
        if (coef != 0 && b.Get(1) && (coef & p1) == 0) coef = static_cast<std::int16_t>(coef >= 0 ? coef + p1 : coef + m1);
      }
      eobrun--;
    }
  }

  // one scan from `at` (after the SOS header); returns where the data ends
  std::size_t Scan(const std::uint8_t *d, std::size_t n, std::size_t at, const std::vector<int> &scomps, int ss, int se,
                   int ah, int al) {
    Bits b;
    b.p = d;
    b.n = n;
    b.pos = at;
    for (int ci : scomps) comps[static_cast<std::size_t>(ci)].dcPred = 0;
    eobrun = 0;
    int todo = restartInterval;
    auto restart = [&] {
      if (!restartInterval) return;
      if (--todo == 0) {
        b.Restart();
        for (int ci : scomps) comps[static_cast<std::size_t>(ci)].dcPred = 0;
        eobrun = 0;
        todo = restartInterval;
      }
    };
    if (scomps.size() == 1) {
      Component &c = comps[static_cast<std::size_t>(scomps[0])];
      for (int by = 0; by < c.usedH; by++)
        for (int bx = 0; bx < c.usedW; bx++) {
          DecodeBlock(b, c, Block(c, bx, by), ss, se, ah, al);
          restart();
        }
    } else {
      for (int my = 0; my < mcusY; my++)
        for (int mx = 0; mx < mcusX; mx++) {
          for (int ci : scomps) {
            Component &c = comps[static_cast<std::size_t>(ci)];
            for (int y = 0; y < c.v; y++)
              for (int x = 0; x < c.h; x++) DecodeBlock(b, c, Block(c, mx * c.h + x, my * c.v + y), ss, se, ah, al);
          }
          restart();
        }
    }
    // the data ends at the next marker that is not a restart
    std::size_t end = b.pos;
    while (end + 1 < n && !(d[end] == 0xFF && d[end + 1] != 0x00 && d[end + 1] != 0xFF &&
                            !(d[end + 1] >= 0xD0 && d[end + 1] <= 0xD7)))
      end++;
    return end;
  }

  void Idct(const std::int16_t *in, const std::uint16_t *q, std::uint8_t *out, int stride) {
    static float cosTable[8][8];
    static bool ready = false;
    if (!ready) {
      for (int x = 0; x < 8; x++)
        for (int u = 0; u < 8; u++)
          cosTable[x][u] = static_cast<float>((u == 0 ? std::sqrt(0.5) : 1.0) * std::cos((2 * x + 1) * u * 3.14159265358979323846 / 16));
      ready = true;
    }
    float f[64], tmp[64];
    for (int i = 0; i < 64; i++) f[i] = static_cast<float>(in[i] * q[i]);
    for (int v = 0; v < 8; v++)
      for (int x = 0; x < 8; x++) {
        float s = 0;
        for (int u = 0; u < 8; u++) s += cosTable[x][u] * f[v * 8 + u];
        tmp[v * 8 + x] = s / 2;
      }
    for (int x = 0; x < 8; x++)
      for (int y = 0; y < 8; y++) {
        float s = 0;
        for (int v = 0; v < 8; v++) s += cosTable[y][v] * tmp[v * 8 + x];
        const int value = static_cast<int>(std::lround(s / 2 + 128));
        out[y * stride + x] = static_cast<std::uint8_t>(value < 0 ? 0 : value > 255 ? 255 : value);
      }
  }

  bool Decode(const std::uint8_t *d, std::size_t n, Image &image);
};

bool Decoder::Decode(const std::uint8_t *d, std::size_t n, Image &image) {
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) {
    error = "not a JPEG file";
    return false;
  }
  std::size_t at = 2;
  bool frame = false, any = false;
  while (at + 4 <= n) {
    if (d[at] != 0xFF) {
      at++;
      continue;
    }
    const int marker = d[at + 1];
    if (marker == 0xFF) {
      at++;
      continue;
    }
    at += 2;
    if (marker == 0xD9) break;                          // EOI
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
    if (at + 2 > n) break;
    const std::size_t len = static_cast<std::size_t>(d[at] << 8 | d[at + 1]);
    if (len < 2 || at + len > n) {
      error = "JPEG segment cut short";
      return false;
    }
    const std::uint8_t *s = d + at + 2;
    const std::size_t sl = len - 2;
    switch (marker) {
      case 0xC0: case 0xC1: case 0xC2: {
        if (sl < 6 || s[0] != 8) {
          error = sl >= 1 && s[0] != 8 ? "JPEG with " + std::to_string(s[0]) + " bits a sample is not supported"
                                        : "bad JPEG frame header";
          return false;
        }
        progressive = marker == 0xC2;
        height = s[1] << 8 | s[2];
        width = s[3] << 8 | s[4];
        const int nc = s[5];
        if (width == 0 || height == 0 || width > 16384 || height > 16384) {
          error = "JPEG size out of range";
          return false;
        }
        if (nc == 4) {
          error = "CMYK JPEG pictures are not supported (save the picture as RGB first)";
          return false;
        }
        if ((nc != 1 && nc != 3) || sl < 6u + 3u * static_cast<unsigned>(nc)) {
          error = "JPEG with " + std::to_string(nc) + " components is not supported";
          return false;
        }
        comps.resize(static_cast<std::size_t>(nc));
        for (int i = 0; i < nc; i++) {
          Component &c = comps[static_cast<std::size_t>(i)];
          c.id = s[6 + 3 * i];
          c.h = s[7 + 3 * i] >> 4;
          c.v = s[7 + 3 * i] & 15;
          c.tq = s[8 + 3 * i] & 3;
          if (c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4) {
            error = "bad JPEG sampling factors";
            return false;
          }
          hmax = std::max(hmax, c.h);
          vmax = std::max(vmax, c.v);
        }
        mcusX = (width + 8 * hmax - 1) / (8 * hmax);
        mcusY = (height + 8 * vmax - 1) / (8 * vmax);
        for (Component &c : comps) {
          if (hmax % c.h || vmax % c.v) {
            error = "JPEG sampling factors that do not divide are not supported";
            return false;
          }
          c.blocksW = mcusX * c.h;
          c.blocksH = mcusY * c.v;
          c.pixW = (width * c.h + hmax - 1) / hmax;
          c.pixH = (height * c.v + vmax - 1) / vmax;
          c.usedW = (c.pixW + 7) / 8;
          c.usedH = (c.pixH + 7) / 8;
          c.coef.assign(static_cast<std::size_t>(c.blocksW) * c.blocksH * 64, 0);
        }
        frame = true;
        break;
      }
      case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
        error = "this kind of JPEG (lossless, hierarchical or arithmetic coding) is not supported";
        return false;
      case 0xC4: {  // DHT
        std::size_t p = 0;
        while (p + 17 <= sl) {
          const int tc = s[p] >> 4, th = s[p] & 15;
          int total = 0;
          for (int i = 0; i < 16; i++) total += s[p + 1 + i];
          if (th > 3 || tc > 1 || total > 256 || p + 17 + static_cast<std::size_t>(total) > sl) {
            error = "bad JPEG Huffman table";
            return false;
          }
          if (!BuildHuffman(s + p + 1, s + p + 17, total, tc ? ac[th] : dc[th])) {
            error = "bad JPEG Huffman table";
            return false;
          }
          p += 17 + static_cast<std::size_t>(total);
        }
        break;
      }
      case 0xDB: {  // DQT
        std::size_t p = 0;
        while (p < sl) {
          const int pq = s[p] >> 4, tq = s[p] & 3;
          const std::size_t bytes = pq ? 128 : 64;
          if (p + 1 + bytes > sl) {
            error = "bad JPEG quantization table";
            return false;
          }
          for (int k = 0; k < 64; k++)
            qt[tq][kZigzag[k]] = static_cast<std::uint16_t>(pq ? (s[p + 1 + 2 * k] << 8 | s[p + 2 + 2 * k]) : s[p + 1 + k]);
          p += 1 + bytes;
        }
        break;
      }
      case 0xDD:  // DRI
        if (sl >= 2) restartInterval = s[0] << 8 | s[1];
        break;
      case 0xEE:  // APP14 Adobe: the colour transform
        if (sl >= 12 && !std::memcmp(s, "Adobe", 5)) adobeTransform = s[11];
        break;
      case 0xDA: {  // SOS
        if (!frame || sl < 1) {
          error = "JPEG scan before the frame header";
          return false;
        }
        const int ns = s[0];
        if (ns < 1 || ns > 4 || sl < 4u + 2u * static_cast<unsigned>(ns)) {
          error = "bad JPEG scan header";
          return false;
        }
        std::vector<int> scomps;
        for (int i = 0; i < ns; i++) {
          const int id = s[1 + 2 * i];
          int found = -1;
          for (std::size_t c = 0; c < comps.size(); c++)
            if (comps[c].id == id) found = static_cast<int>(c);
          if (found < 0) {
            error = "JPEG scan names an unknown component";
            return false;
          }
          comps[static_cast<std::size_t>(found)].dcTable = s[2 + 2 * i] >> 4 & 3;
          comps[static_cast<std::size_t>(found)].acTable = s[2 + 2 * i] & 3;
          scomps.push_back(found);
        }
        const int ss = s[1 + 2 * ns], se = s[2 + 2 * ns], ah = s[3 + 2 * ns] >> 4, al = s[3 + 2 * ns] & 15;
        if (progressive ? (ss > se || se > 63 || (ss == 0 && se != 0) || (ss > 0 && ns != 1) || al > 13)
                        : (ss != 0 || se != 63)) {
          error = "bad JPEG scan parameters";
          return false;
        }
        for (int ci : scomps) {
          const Component &c = comps[static_cast<std::size_t>(ci)];
          if ((ss == 0 && ah == 0 && !dc[c.dcTable].defined) || (se > 0 && !ac[c.acTable].defined)) {
            error = "JPEG scan without its Huffman table";
            return false;
          }
        }
        at = Scan(d, n, at + len, scomps, ss, se, ah, al);
        any = true;
        continue;
      }
      default:
        break;  // APPn, COM and the like
    }
    at += len;
  }
  if (!frame || !any) {
    error = "JPEG without a picture";
    return false;
  }
  // coefficients to samples, then to RGBA at full size
  std::vector<std::vector<std::uint8_t>> planes(comps.size());
  for (std::size_t ci = 0; ci < comps.size(); ci++) {
    Component &c = comps[ci];
    const int pw = c.blocksW * 8;
    planes[ci].assign(static_cast<std::size_t>(pw) * c.blocksH * 8, 0);
    for (int by = 0; by < c.blocksH; by++)
      for (int bx = 0; bx < c.blocksW; bx++)
        Idct(Block(c, bx, by), qt[c.tq], planes[ci].data() + (static_cast<std::size_t>(by) * 8 * pw + bx * 8), pw);
  }
  image.Resize(static_cast<unsigned>(width), static_cast<unsigned>(height));
  // a component's sample at the picture's (x, y): linear between the samples' centres (libjpeg's "fancy" 2:1 filter)
  std::vector<std::vector<std::uint8_t>> full(comps.size());
  for (std::size_t ci = 0; ci < comps.size(); ci++) {
    const Component &c = comps[ci];
    const int pw = c.blocksW * 8, sx = hmax / c.h, sy = vmax / c.v;
    std::vector<std::uint8_t> &out = full[ci];
    out.resize(static_cast<std::size_t>(width) * height);
    const std::uint8_t *in = planes[ci].data();
    if (sx == 1 && sy == 1) {
      for (int y = 0; y < height; y++) std::memcpy(&out[static_cast<std::size_t>(y) * width], in + static_cast<std::size_t>(y) * pw, static_cast<std::size_t>(width));
      continue;
    }
    std::vector<float> row(static_cast<std::size_t>(width) * c.pixH);
    for (int y = 0; y < c.pixH; y++)
      for (int x = 0; x < width; x++) {
        const float fx = (x + 0.5f) / sx - 0.5f;
        int x0 = static_cast<int>(std::floor(fx));
        const float t = fx - x0;
        const int a = std::clamp(x0, 0, c.pixW - 1), b = std::clamp(x0 + 1, 0, c.pixW - 1);
        row[static_cast<std::size_t>(y) * width + x] = in[static_cast<std::size_t>(y) * pw + a] * (1 - t) + in[static_cast<std::size_t>(y) * pw + b] * t;
      }
    for (int y = 0; y < height; y++) {
      const float fy = (y + 0.5f) / sy - 0.5f;
      int y0 = static_cast<int>(std::floor(fy));
      const float t = fy - y0;
      const int a = std::clamp(y0, 0, c.pixH - 1), b = std::clamp(y0 + 1, 0, c.pixH - 1);
      for (int x = 0; x < width; x++) {
        const float v = row[static_cast<std::size_t>(a) * width + x] * (1 - t) + row[static_cast<std::size_t>(b) * width + x] * t;
        out[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f)));
      }
    }
  }
  const bool rgb = comps.size() == 3 &&
                   (adobeTransform == 0 || (comps[0].id == 'R' && comps[1].id == 'G' && comps[2].id == 'B'));
  for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; i++) {
    std::uint8_t *o = image.px.data() + i * 4;
    if (comps.size() == 1) {
      o[0] = o[1] = o[2] = full[0][i];
    } else if (rgb) {
      o[0] = full[0][i];
      o[1] = full[1][i];
      o[2] = full[2][i];
    } else {
      const float y = full[0][i], cb = full[1][i] - 128.0f, cr = full[2][i] - 128.0f;
      const auto c8 = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f))); };
      o[0] = c8(y + 1.402f * cr);
      o[1] = c8(y - 0.344136f * cb - 0.714136f * cr);
      o[2] = c8(y + 1.772f * cb);
    }
    o[3] = 255;
  }
  return true;
}

}  // namespace

bool LoadJpeg(const std::uint8_t *data, std::size_t n, Loaded &out, std::string &error) {
  Decoder dec;
  Image image;
  if (!dec.Decode(data, n, image)) {
    error = dec.error;
    return false;
  }
  out = Loaded{};
  out.fileWidth = image.w;
  out.fileHeight = image.h;
  out.fileLevels = 1;
  out.format = "JPG";
  out.alpha = Alpha::kNone;
  out.levels.push_back(std::move(image));
  return true;
}

bool JpegSize(const std::uint8_t *d, std::size_t n, unsigned &w, unsigned &h) {
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
  std::size_t at = 2;
  while (at + 9 < n) {
    if (d[at] != 0xFF) return false;
    const int marker = d[at + 1];
    const std::size_t len = static_cast<std::size_t>(d[at + 2] << 8 | d[at + 3]);
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      h = static_cast<unsigned>(d[at + 5] << 8 | d[at + 6]);
      w = static_cast<unsigned>(d[at + 7] << 8 | d[at + 8]);
      return w && h;
    }
    at += 2 + len;
  }
  return false;
}

}  // namespace img
