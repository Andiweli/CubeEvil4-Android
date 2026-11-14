// Tests of the texture tool's parts: CRC, ETC2/EAC encoder and decoder, BC and DDS reader, inflate and PNG reader,
// filters. texpack_tests [PNG fixture folder (make_png_fixtures.py) [BC7/JPG fixture folder (make_bc7_jpeg_fixtures.py)]]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "c4tex.h"
#include "etc2.h"
#include "fileio.h"
#include "image.h"

namespace {

int gFailures = 0, gChecks = 0;

#define CHECK(c)                                                       \
  do {                                                                 \
    gChecks++;                                                         \
    if (!(c)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);         \
      gFailures++;                                                     \
    }                                                                  \
  } while (0)

img::Image Make(unsigned w, unsigned h, unsigned seed, int pattern) {
  img::Image im;
  im.Resize(w, h);
  std::mt19937 rng(seed);
  for (unsigned y = 0; y < h; y++)
    for (unsigned x = 0; x < w; x++) {
      std::uint8_t *p = im.px.data() + (static_cast<std::size_t>(y) * w + x) * 4;
      switch (pattern) {
        case 0:  // flat
          p[0] = 200; p[1] = 120; p[2] = 40; p[3] = 255;
          break;
        case 1:  // smooth gradient
          p[0] = static_cast<std::uint8_t>(x * 255 / (w - 1));
          p[1] = static_cast<std::uint8_t>(y * 255 / (h - 1));
          p[2] = static_cast<std::uint8_t>((x + y) * 255 / (w + h - 2));
          p[3] = 255;
          break;
        case 2:  // noise
          p[0] = static_cast<std::uint8_t>(rng());
          p[1] = static_cast<std::uint8_t>(rng());
          p[2] = static_cast<std::uint8_t>(rng());
          p[3] = 255;
          break;
        case 3: {  // squares of two colours
          const bool a = ((x / 8) + (y / 8)) & 1;
          p[0] = a ? 230 : 30; p[1] = a ? 210 : 60; p[2] = a ? 40 : 120; p[3] = 255;
          break;
        }
        case 4: {  // texture-like: gradient plus some noise
          const int n = static_cast<int>(rng() % 21) - 10;
          p[0] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(x * 2) + n + 40, 0, 255));
          p[1] = static_cast<std::uint8_t>(std::clamp(static_cast<int>(y * 2) + n + 30, 0, 255));
          p[2] = static_cast<std::uint8_t>(std::clamp(90 + n, 0, 255));
          p[3] = 255;
          break;
        }
      }
    }
  return im;
}

double Psnr(const img::Image &a, const img::Image &b, int channels) {
  double se = 0;
  const std::size_t n = static_cast<std::size_t>(a.w) * a.h;
  for (std::size_t i = 0; i < n; i++)
    for (int c = 0; c < channels; c++) {
      const double d = static_cast<double>(a.px[i * 4 + c]) - b.px[i * 4 + c];
      se += d * d;
    }
  const double mse = se / (n * channels);
  return mse < 1e-9 ? 99 : 10 * std::log10(255.0 * 255.0 / mse);
}

img::Image RoundTrip(etc2::Kind kind, const img::Image &src, int effort, std::vector<std::uint8_t> *data = nullptr) {
  std::vector<std::uint8_t> enc;
  etc2::EncodeLevel(kind, src.px.data(), src.w, src.h, effort, enc);
  CHECK(enc.size() == c4tex::LevelBytes(kind, src.w, src.h));
  img::Image back;
  back.Resize(src.w, src.h);
  etc2::DecodeLevel(kind, enc.data(), src.w, src.h, back.px.data());
  if (data) *data = enc;
  return back;
}

void TestCrc() {
  const char *s = "123456789";
  CHECK(c4tex::Crc32(s, 9) == 0xCBF43926u);
  CHECK(c4tex::Crc32(s + 4, 5, c4tex::Crc32(s, 4)) == 0xCBF43926u);
}

void TestEtc2Quality() {
  for (int effort = 0; effort <= 2; effort++) {
    const img::Image flat = Make(16, 16, 1, 0), grad = Make(64, 64, 2, 1), noise = Make(32, 32, 3, 2),
                     squares = Make(32, 32, 4, 3), tex = Make(64, 64, 5, 4);
    const img::Image f = RoundTrip(etc2::kRgb, flat, effort);
    int maxErr = 0;
    for (std::size_t i = 0; i < f.px.size(); i++)
      if (i % 4 != 3) maxErr = std::max(maxErr, std::abs(f.px[i] - flat.px[i]));
    CHECK(maxErr <= 4);
    const double pg = Psnr(RoundTrip(etc2::kRgb, grad, effort), grad, 3);
    const double pn = Psnr(RoundTrip(etc2::kRgb, noise, effort), noise, 3);
    const double ps = Psnr(RoundTrip(etc2::kRgb, squares, effort), squares, 3);
    const double pt = Psnr(RoundTrip(etc2::kRgb, tex, effort), tex, 3);
    std::printf("  ETC2 RGB effort %d: gradient %.1f dB, noise %.1f dB, squares %.1f dB, texture %.1f dB\n", effort, pg, pn,
                ps, pt);
    CHECK(pg >= 38);
    CHECK(pn >= 10);  // random colour noise: ETC2 keeps one colour a half block
    CHECK(ps >= 34);
    CHECK(pt >= 30);
  }
}

void TestPlanarUsed() {
  // a smooth 4x4 gradient is best in planar mode: differential with blue past 31 and red, green inside
  img::Image g;
  g.Resize(4, 4);
  for (unsigned y = 0; y < 4; y++)
    for (unsigned x = 0; x < 4; x++) {
      std::uint8_t *p = g.px.data() + (y * 4 + x) * 4;
      p[0] = static_cast<std::uint8_t>(20 + 40 * x);
      p[1] = static_cast<std::uint8_t>(10 + 50 * y);
      p[2] = static_cast<std::uint8_t>(100 + 20 * x - 15 * y);
      p[3] = 255;
    }
  std::vector<std::uint8_t> enc;
  const img::Image back = RoundTrip(etc2::kRgb, g, 1, &enc);
  std::uint64_t w = 0;
  for (int i = 0; i < 8; i++) w = w << 8 | enc[i];
  const auto sext = [](int v) { return v & 4 ? v - 8 : v; };
  const int r = static_cast<int>(w >> 59) & 31, gg = static_cast<int>(w >> 51) & 31, b = static_cast<int>(w >> 43) & 31;
  const int r2 = r + sext(static_cast<int>(w >> 56) & 7), g2 = gg + sext(static_cast<int>(w >> 48) & 7),
            b2 = b + sext(static_cast<int>(w >> 40) & 7);
  CHECK((w >> 33) & 1);
  CHECK(r2 >= 0 && r2 <= 31 && g2 >= 0 && g2 <= 31);
  CHECK(b2 < 0 || b2 > 31);
  std::printf("  planar gradient: %.1f dB\n", Psnr(back, g, 3));
  CHECK(Psnr(back, g, 3) >= 40);
}

void TestPunchThrough() {
  img::Image im = Make(32, 32, 7, 4);
  for (unsigned y = 0; y < 32; y++)
    for (unsigned x = 0; x < 32; x++) {
      std::uint8_t *p = im.px.data() + (static_cast<std::size_t>(y) * 32 + x) * 4;
      p[3] = ((x * 7 + y * 3) % 11 < 4 || (x > 20 && y > 20)) ? 0 : 255;
    }
  const img::Image back = RoundTrip(etc2::kRgbA1, im, 1);
  int wrongAlpha = 0;
  img::Image a = im, b = back;
  for (std::size_t i = 0; i < im.px.size(); i += 4) {
    const int want = im.px[i + 3] >= 128 ? 255 : 0;
    if (back.px[i + 3] != want) wrongAlpha++;
    if (!want) {
      CHECK(back.px[i] == 0 && back.px[i + 1] == 0 && back.px[i + 2] == 0);
      std::memset(&a.px[i], 0, 4);
      std::memset(&b.px[i], 0, 4);
    }
  }
  CHECK(wrongAlpha == 0);
  const double p = Psnr(a, b, 3);
  std::printf("  ETC2 punch-through: colour %.1f dB, alpha exact\n", p);
  CHECK(p >= 28);
  // an opaque image in the punch-through format: no transparency appears
  const img::Image opaque = Make(16, 16, 8, 1);
  const img::Image ob = RoundTrip(etc2::kRgbA1, opaque, 1);
  bool allOpaque = true;
  for (std::size_t i = 3; i < ob.px.size(); i += 4) allOpaque = allOpaque && ob.px[i] == 255;
  CHECK(allOpaque);
  CHECK(Psnr(ob, opaque, 3) >= 36);
}

void TestEac() {
  img::Image im = Make(32, 32, 9, 1);
  for (unsigned y = 0; y < 32; y++)
    for (unsigned x = 0; x < 32; x++) im.px[(static_cast<std::size_t>(y) * 32 + x) * 4 + 3] = static_cast<std::uint8_t>(x * 8);
  const img::Image back = RoundTrip(etc2::kRgbaEac, im, 1);
  int maxA = 0;
  for (std::size_t i = 3; i < im.px.size(); i += 4) maxA = std::max(maxA, std::abs(im.px[i] - back.px[i]));
  std::printf("  EAC alpha gradient: largest error %d, colour %.1f dB\n", maxA, Psnr(back, im, 3));
  CHECK(maxA <= 6);
  CHECK(Psnr(back, im, 3) >= 38);
  // constant alpha comes back exactly, also 0 and 255
  for (int value : {0, 77, 255}) {
    img::Image c = Make(8, 8, 10, 0);
    for (std::size_t i = 3; i < c.px.size(); i += 4) c.px[i] = static_cast<std::uint8_t>(value);
    const img::Image cb = RoundTrip(etc2::kRgbaEac, c, 1);
    bool exact = true;
    for (std::size_t i = 3; i < c.px.size(); i += 4) exact = exact && cb.px[i] == value;
    CHECK(exact);
  }
}

void TestPartialBlocks() {
  const img::Image im = Make(5, 3, 11, 4);
  for (etc2::Kind k : {etc2::kRgb, etc2::kRgbA1, etc2::kRgbaEac}) {
    const img::Image back = RoundTrip(k, im, 1);
    std::printf("  5x3 image, kind %d: %.1f dB\n", static_cast<int>(k), Psnr(back, im, 3));
    CHECK(Psnr(back, im, 3) >= 30);
  }
}

void TestBc() {
  // BC1: red and blue, four colours (c0 > c1), then three colours and transparent (c0 <= c1)
  std::uint8_t dds[128 + 16] = {};
  std::memcpy(dds, "DDS ", 4);
  auto put32 = [&](int at, std::uint32_t v) {
    for (int i = 0; i < 4; i++) dds[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
  };
  put32(4, 124);
  put32(8, 0x1007);
  put32(12, 4);   // height
  put32(16, 8);   // width
  put32(28, 1);
  put32(76, 32);
  put32(80, 4);
  std::memcpy(dds + 84, "DXT1", 4);
  // block 0: c0 = red 0xF800 > c1 = blue 0x001F, indices 0 1 2 3 on the first row
  dds[128] = 0x00; dds[129] = 0xF8; dds[130] = 0x1F; dds[131] = 0x00;
  dds[132] = 0xE4;  // 11 10 01 00
  // block 1: c0 = blue < c1 = red: index 3 is transparent
  dds[136] = 0x1F; dds[137] = 0x00; dds[138] = 0x00; dds[139] = 0xF8;
  dds[140] = 0xE4;
  img::Loaded l;
  std::string err;
  CHECK(img::LoadDds(dds, sizeof(dds), 0, false, l, err));
  if (l.levels.empty()) return;
  const img::Image &im = l.levels[0];
  CHECK(im.w == 8 && im.h == 4);
  auto px = [&](unsigned x, unsigned y, int c) { return im.px[(y * im.w + x) * 4 + c]; };
  CHECK(px(0, 0, 0) == 255 && px(0, 0, 2) == 0);
  CHECK(px(1, 0, 0) == 0 && px(1, 0, 2) == 255);
  CHECK(px(2, 0, 0) == 170 && px(2, 0, 2) == 85);
  CHECK(px(3, 0, 0) == 85 && px(3, 0, 2) == 170);
  CHECK(px(4, 0, 2) == 255 && px(5, 0, 0) == 255);
  CHECK(px(6, 0, 0) == 127 && px(6, 0, 2) == 127 && px(6, 0, 3) == 255);
  CHECK(px(7, 0, 3) == 0);
  CHECK(px(0, 1, 0) == 255 && px(0, 1, 3) == 255);  // index 0 elsewhere
  CHECK(l.alpha == img::Alpha::kBinary);
}

void TestDdsLevels() {
  // BC1 16x16 with 5 levels: maxSide 8 starts at the 8x8 level, maxSide 5 too (4 < 5)
  const unsigned levels = 5;
  std::size_t size = 128;
  for (unsigned l = 0, s = 16; l < levels; l++, s = std::max(1u, s / 2)) size += ((s + 3) / 4) * ((s + 3) / 4) * 8;
  std::vector<std::uint8_t> dds(size, 0);
  std::memcpy(dds.data(), "DDS ", 4);
  auto put32 = [&](int at, std::uint32_t v) {
    for (int i = 0; i < 4; i++) dds[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
  };
  put32(4, 124);
  put32(12, 16);
  put32(16, 16);
  put32(28, levels);
  put32(80, 4);
  std::memcpy(dds.data() + 84, "DXT1", 4);
  img::Loaded l;
  std::string err;
  CHECK(img::LoadDds(dds.data(), dds.size(), 8, false, l, err) && l.levels.size() == 1 && l.levels[0].w == 8);
  CHECK(img::LoadDds(dds.data(), dds.size(), 5, true, l, err) && l.levels.size() == 4 && l.levels[0].w == 8 &&
        l.levels[3].w == 1);
  CHECK(img::LoadDds(dds.data(), dds.size(), 16, false, l, err) && l.levels[0].w == 16);
  CHECK(img::LoadDds(dds.data(), dds.size(), 0, false, l, err) && l.levels[0].w == 16);
  // cut short: the levels that are there
  CHECK(img::LoadDds(dds.data(), 128 + 128 + 32, 0, true, l, err) && l.levels.size() == 2);
  CHECK(!img::LoadDds(dds.data(), 100, 0, false, l, err));
}

void TestPngRoundTrip() {
  const img::Image im = Make(70, 33, 12, 2);
  const std::vector<std::uint8_t> png = img::EncodePng(im);
  img::Loaded l;
  std::string err;
  CHECK(img::LoadPng(png.data(), png.size(), l, err));
  CHECK(!l.levels.empty() && l.levels[0].px == im.px);
  // broken files give an error, never a crash
  for (std::size_t cut : {std::size_t(0), std::size_t(8), std::size_t(30), png.size() / 2, png.size() - 13}) {
    img::Loaded b;
    CHECK(!img::LoadPng(png.data(), cut, b, err));
  }
  std::vector<std::uint8_t> bad = png;
  for (std::size_t i = 60; i < bad.size(); i += 97) bad[i] ^= 0x5A;
  img::Loaded b;
  img::LoadPng(bad.data(), bad.size(), b, err);  // anything but a crash
}

void TestPngFixtures(const std::string &dir) {
  std::error_code ec;
  int n = 0;
  for (const auto &e : std::filesystem::directory_iterator(fio::Path(dir), ec)) {
    const std::string p = fio::Utf8(e.path());
    if (e.path().extension() != ".png") continue;
    std::vector<std::uint8_t> png, want;
    std::string rgba = p.substr(0, p.size() - 4) + ".rgba";
    if (!fio::ReadFile(p, png) || !fio::ReadFile(rgba, want)) continue;
    img::Loaded l;
    std::string err;
    const bool ok = img::LoadPng(png.data(), png.size(), l, err);
    if (!ok || l.levels[0].px != want) {
      std::printf("FAIL png fixture %s: %s\n", p.c_str(), ok ? "pixels differ" : err.c_str());
      gFailures++;
    }
    gChecks++;
    n++;
  }
  std::printf("  PNG fixtures: %d files\n", n);
  CHECK(n > 0);
}

// make_bc7_jpeg_fixtures.py (Pillow's decoding as the reference): BC7 exactly, JPG within the rounding of two
// decoders (IDCT, the chroma's upsampling); the CMYK file refused with a clear message
void TestBc7JpegFixtures(const std::string &dir) {
  std::error_code ec;
  int bc7 = 0, jpg = 0;
  double worstMean = 0;
  std::string worstName;
  for (const auto &e : std::filesystem::directory_iterator(fio::Path(dir), ec)) {
    const std::string p = fio::Utf8(e.path());
    const std::string ext = fio::Utf8(e.path().extension());
    if (ext != ".dds" && ext != ".jpg") continue;
    std::vector<std::uint8_t> file, want;
    if (!fio::ReadFile(p, file)) continue;
    img::Loaded l;
    std::string err;
    if (p.find("cmyk_refuse") != std::string::npos) {
      CHECK(!img::LoadJpeg(file.data(), file.size(), l, err) && err.find("CMYK") != std::string::npos);
      continue;
    }
    if (!fio::ReadFile(p.substr(0, p.size() - 4) + ".rgba", want)) continue;
    const bool ok = ext == ".dds" ? img::LoadDds(file.data(), file.size(), 0, false, l, err)
                                  : img::LoadJpeg(file.data(), file.size(), l, err);
    gChecks++;
    if (!ok || l.levels[0].px.size() != want.size()) {
      std::printf("FAIL %s: %s\n", p.c_str(), ok ? "size differs" : err.c_str());
      gFailures++;
      continue;
    }
    const std::vector<std::uint8_t> &got = l.levels[0].px;
    if (ext == ".dds") {
      bc7++;
      std::size_t differ = 0, first = 0;
      for (std::size_t i = 0; i < got.size(); i++)
        if (got[i] != want[i] && !differ++) first = i;
      if (differ) {
        const std::size_t px = first / 4;
        std::printf("FAIL %s: %zu values differ, first at pixel %zu,%zu (ours %d,%d,%d,%d, Pillow's %d,%d,%d,%d)\n", p.c_str(),
                    differ, px % l.levels[0].w, px / l.levels[0].w, got[px * 4], got[px * 4 + 1], got[px * 4 + 2],
                    got[px * 4 + 3], want[px * 4], want[px * 4 + 1], want[px * 4 + 2], want[px * 4 + 3]);
        gFailures++;
      }
      continue;
    }
    jpg++;
    double sum = 0;
    int maxd = 0;
    for (std::size_t i = 0; i < got.size(); i++) {
      const int d = std::abs(got[i] - want[i]);
      sum += d;
      maxd = std::max(maxd, d);
    }
    const double mean = sum / got.size();
    if (mean > worstMean) {
      worstMean = mean;
      worstName = fio::Utf8(e.path().filename());
    }
    // the same picture: on average within a step, single values within the upsampling's rounding at sharp edges
    if (mean > 1.0 || maxd > 48) {
      std::printf("FAIL %s: mean difference %.2f, largest %d\n", p.c_str(), mean, maxd);
      gFailures++;
    }
  }
  std::printf("  BC7 fixtures: %d files exact; JPG fixtures: %d files, largest mean difference %.2f (%s)\n", bc7, jpg,
              worstMean, worstName.c_str());
  CHECK(bc7 >= 10 && jpg >= 40);
}

void TestFilters() {
  const img::Image flat = Make(64, 48, 13, 0);
  const img::Image small = img::Downscale(flat, 16, 12);
  CHECK(small.w == 16 && small.h == 12);
  bool same = true;
  for (std::size_t i = 0; i < small.px.size(); i += 4)
    same = same && small.px[i] == 200 && small.px[i + 1] == 120 && small.px[i + 2] == 40 && small.px[i + 3] == 255;
  CHECK(same);
  // a transparent half keeps its own colour (no black edges for soft alpha)
  img::Image half = flat;
  for (unsigned y = 0; y < 48; y++)
    for (unsigned x = 32; x < 64; x++) {
      std::uint8_t *p = half.px.data() + (static_cast<std::size_t>(y) * 64 + x) * 4;
      p[0] = 10; p[1] = 250; p[2] = 10; p[3] = 0;
    }
  const img::Image hs = img::Downscale(half, 16, 12);
  CHECK(hs.px[(5 * 16 + 14) * 4 + 1] == 250 && hs.px[(5 * 16 + 14) * 4 + 3] == 0);
  CHECK(hs.px[(5 * 16 + 1) * 4 + 0] == 200 && hs.px[(5 * 16 + 1) * 4 + 3] == 255);
  img::Image four;
  four.Resize(2, 2);
  const std::uint8_t v[16] = {0, 0, 0, 255, 100, 100, 100, 255, 200, 200, 200, 255, 100, 100, 100, 255};
  std::memcpy(four.px.data(), v, 16);
  const img::Image one = img::HalfSize(four);
  CHECK(one.w == 1 && one.h == 1 && one.px[0] == 100 && one.px[3] == 255);
  // keeping coverage: a level with half the pixels at alpha 100 is lifted to the target
  img::Image cov;
  cov.Resize(4, 4);
  for (std::size_t i = 0; i < 16; i++) cov.px[i * 4 + 3] = i < 8 ? 100 : 0;
  img::KeepCoverage(cov, 128, 0.5);
  CHECK(std::abs(img::Coverage(cov, 128) - 0.5) < 1e-9);
  CHECK(img::AlphaOf(flat) == img::Alpha::kNone && img::AlphaOf(half) == img::Alpha::kBinary);
}

void TestEntryValid() {
  c4tex::Entry e{};
  e.encoding = c4tex::kEtc2Rgb;
  e.width = 64;
  e.height = 32;
  e.levels = 7;
  e.origWidth = 16;
  e.origHeight = 8;
  e.gxFormat = 14;
  e.dataBytes = static_cast<std::uint32_t>(c4tex::ImageBytes(1, 64, 32, 7));
  e.dataOffset = 256;
  CHECK(c4tex::EntryValid(e, 100000, 256, 50000));
  c4tex::Entry b = e;
  b.levels = 8;  // longer than the chain
  CHECK(!c4tex::EntryValid(b, 100000, 256, 50000));
  b = e;
  b.dataOffset = 49990;  // into the index
  CHECK(!c4tex::EntryValid(b, 100000, 256, 50000));
  b = e;
  b.dataBytes -= 8;
  CHECK(!c4tex::EntryValid(b, 100000, 256, 50000));
  b = e;
  b.encoding = 9;
  CHECK(!c4tex::EntryValid(b, 100000, 256, 50000));
  b = e;
  b.dataOffset = ~0ull - 10;  // overflow
  CHECK(!c4tex::EntryValid(b, 100000, 256, 50000));
}

}  // namespace

int main(int argc, char **argv) {
  TestCrc();
  TestEtc2Quality();
  TestPlanarUsed();
  TestPunchThrough();
  TestEac();
  TestPartialBlocks();
  TestBc();
  TestDdsLevels();
  TestPngRoundTrip();
  if (argc > 1) TestPngFixtures(argv[1]);
  if (argc > 2) TestBc7JpegFixtures(argv[2]);
  TestFilters();
  TestEntryValid();
  std::printf("%d checks, %d failed\n", gChecks, gFailures);
  return gFailures ? 1 : 0;
}
