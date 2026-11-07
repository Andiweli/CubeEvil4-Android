// The OUYA's GX renderer (port/ouya/app/src/main/cpp/gx_gl2.cpp, GLES 2) against the main app's
// (port/android-decomp/app/src/main/cpp/gx_gl.cpp, GLES 3) on Mesa: the same synthetic scenes,
// built as the core's decoder writes chunks, drawn by both; the pictures must match.
//
//   gx_gl2_check [OUTPUT_DIR]
//
// The GLES 3 renderer draws the float vertex in a child process (Mesa's version override holds
// for a whole process); the GLES 2 one draws the same scenes with compact vertices
// (RE4_GX_V_COMPACT, each attribute in a GX type that holds its values exactly, chosen at random:
// every type, fraction and component count comes up) on a context forced to GLES 2.0
// (MESA_GLES_VERSION_OVERRIDE=2.0), once as Mesa offers it and once as the OUYA's Tegra 3 does
// (GxOptions::tegra3: 16-bit depth, no depth textures, NPOT only clamped, the Tegra's limits).
// The EFB (and the XFB where a scene makes a display copy) of every scene is compared; with
// OUTPUT_DIR the pictures of scenes that differ are written there (PPM: reference, GLES 2,
// difference).
#include "gx_chunk_builder.h"
#include "gx_gl2.h"
#include "gx_test_common.h"
#include "gx_texture.h"
#include "re4_gx_frame.h"

#include <GLES2/gl2.h>

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace re4gx::chunk;
using ouyatest::Picture;

namespace {

const std::uint8_t kRed[4] = {255, 0, 0, 255};
const std::uint8_t kGreen[4] = {0, 255, 0, 255};
const std::uint8_t kBlue[4] = {0, 0, 255, 255};
const std::uint8_t kBlack[4] = {0, 0, 0, 255};
const std::uint8_t kWhite[4] = {255, 255, 255, 255};

// --- scene building ----------------------------------------------------------------------------

void Append(std::vector<std::uint8_t> &v, const void *p, std::size_t n) {
  v.insert(v.end(), static_cast<const std::uint8_t *>(p), static_cast<const std::uint8_t *>(p) + n);
}
void AppendF(std::vector<std::uint8_t> &v, std::initializer_list<float> f) {
  for (float x : f)
    Append(v, &x, 4);
}

// A texture with `levels` mipmap levels of random texels (GX tiled), bound to `map`; paletted
// formats get a random palette at TMEM 0x200 << 9 + map * 0x40 << 9.
void RandomTexture(Builder &b, std::mt19937 &rng, unsigned map, std::uint64_t key, unsigned format, unsigned w,
                   unsigned h, unsigned levels) {
  std::vector<std::uint8_t> data;
  unsigned lw = w, lh = h;
  for (unsigned l = 0; l < levels; l++) {
    const std::size_t bytes = re4gx::GxTextureLevelBytes(format, lw, lh);
    for (std::size_t i = 0; i < bytes; i++)
      data.push_back(static_cast<std::uint8_t>(rng()));
    lw = std::max(1u, lw / 2);
    lh = std::max(1u, lh / 2);
  }
  std::uint32_t tlut = 0;
  if (re4gx::GxIsPaletted(format)) {
    const unsigned entries = format == re4gx::kTexC14X2 ? 16384 : re4gx::GxPaletteEntries(format);
    std::vector<std::uint8_t> palette(entries * 2);
    for (auto &p : palette)
      p = static_cast<std::uint8_t>(rng());
    const unsigned tmem = 0x200 + map * 0x40;
    const std::uint32_t at = b.Blob(palette.data(), palette.size());
    const std::uint32_t op[3] = {tmem << 9, static_cast<std::uint32_t>(palette.size()), at};
    b.Op(RE4_GX_OP_TLUT, op, 3);
    tlut = tmem | (rng() % 3) << 10;
  }
  Re4GxTexture t{};
  t.map = map;
  t.keyLo = static_cast<std::uint32_t>(key);
  t.keyHi = static_cast<std::uint32_t>(key >> 32);
  t.address = 0x100000 + map * 0x10000;
  t.width = w;
  t.height = h;
  t.format = format;
  t.levels = levels;
  t.tlut = tlut;
  t.dataOffset = b.Blob(data.data(), data.size());
  t.dataBytes = static_cast<std::uint32_t>(data.size());
  b.Op(RE4_GX_OP_TEXTURE, reinterpret_cast<const std::uint32_t *>(&t), sizeof(t) / 4);
  // the coordinate size of coordinate `map` to the texture (the tests pair them)
  b.Bp(0x30 + 2 * map, w - 1);
  b.Bp(0x31 + 2 * map, h - 1);
}

// texture map m's sampling (BP 0x80/0xA0 + m): wrap s, t (0 clamp, 1 repeat, 2 mirror), filters
void Sampling(Builder &b, unsigned m, unsigned wrapS, unsigned wrapT, bool linear, unsigned minFilter,
              int lodBias = 0) {
  b.Bp((m < 4 ? 0x80 : 0xA0) + (m & 3),
       wrapS | wrapT << 2 | (linear ? 1u : 0u) << 4 | minFilter << 5 | (static_cast<std::uint32_t>(lodBias) & 0xFF) << 9);
  b.Bp((m < 4 ? 0x84 : 0xA4) + (m & 3), 0 | 160u << 8); // min LOD 0, max LOD 10
}

// One TEV stage taking the texture of map/coord 0 times the rasterized colour.
void TextureStage(Builder &b) {
  b.Bp(0x00, 1u | 1u << 4);                                       // one texture coordinate, one colour channel
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);        // stage 0: map 0, coord 0, colour 0
  b.Bp(0xC0, 15u | 8u << 4 | 10u << 8 | 15u << 12 | 1u << 19);    // colour = ras * tex
  b.Bp(0xC1, 7u << 4 | 4u << 7 | 5u << 10 | 7u << 13 | 1u << 19); // alpha = ras * tex
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7}); // texgen 0: regular, from TEX0, ST
}

// a quad of position, colour, s, t over (x0, y0)..(x1, y1) with coordinates s0..s1, t0..t1
void TexQuad(Builder &b, float x0, float y0, float x1, float y1, float s0, float t0, float s1, float t1,
             const std::uint8_t c[4] = kWhite) {
  std::vector<std::uint8_t> v;
  VT(v, x0, y0, 0, c, s0, t0);
  VT(v, x1, y0, 0, c, s1, t0);
  VT(v, x1, y1, 0, c, s1, t1);
  VT(v, x0, y1, 0, c, s0, t1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
}

using Scene = std::function<void(std::vector<Builder> &, std::mt19937 &)>;

Builder &NewChunk(std::vector<Builder> &chunks) {
  chunks.emplace_back();
  Baseline(chunks.back());
  return chunks.back();
}

void SceneBasics(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlue);
  Quad(b, 10, 20, 110, 70, 0, kGreen);
  std::vector<std::uint8_t> v;
  V(v, 500, 400, 0, kRed);
  V(v, 600, 400, 0, kRed);
  V(v, 500, 500, 0, kRed);
  b.Draw(RE4_GX_V_COLOR0, 16, v, 3);
  // a colour gradient over a big triangle (interpolation)
  std::vector<std::uint8_t> g;
  V(g, 150, 100, 0, kRed);
  V(g, 450, 120, 0, kGreen);
  V(g, 260, 380, 0, kBlue);
  b.Draw(RE4_GX_V_COLOR0, 16, g, 3);
  DisplayCopy(b, false, kBlue);
}

void SceneCulling(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  b.Bp(0x00, (1u << 4) | 1u << 14);
  DisplayCopy(b, true, kBlack);
  std::vector<std::uint8_t> cw, ccw;
  V(cw, 100, 100, 0, kRed);
  V(cw, 200, 100, 0, kRed);
  V(cw, 100, 200, 0, kRed);
  V(ccw, 400, 100, 0, kGreen);
  V(ccw, 400, 200, 0, kGreen);
  V(ccw, 500, 100, 0, kGreen);
  b.Draw(RE4_GX_V_COLOR0, 16, cw, 3);
  b.Draw(RE4_GX_V_COLOR0, 16, ccw, 3);
  b.Bp(0x00, (1u << 4) | 2u << 14);
  std::vector<std::uint8_t> cw2, ccw2;
  V(cw2, 100, 300, 0, kRed);
  V(cw2, 200, 300, 0, kRed);
  V(cw2, 100, 400, 0, kRed);
  V(ccw2, 400, 300, 0, kGreen);
  V(ccw2, 400, 400, 0, kGreen);
  V(ccw2, 500, 300, 0, kGreen);
  b.Draw(RE4_GX_V_COLOR0, 16, cw2, 3);
  b.Draw(RE4_GX_V_COLOR0, 16, ccw2, 3);
}

void SceneDepth(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  Quad(b, 0, 0, 100, 100, -0.25f, kRed);
  Quad(b, 0, 0, 100, 100, -0.75f, kGreen);
  Quad(b, 200, 0, 300, 100, -0.75f, kGreen);
  Quad(b, 200, 0, 300, 100, -0.25f, kRed);
  // interpenetrating triangles (depth across a triangle)
  std::vector<std::uint8_t> v1, v2;
  V(v1, 100, 200, -0.1f, kBlue);
  V(v1, 500, 200, -0.9f, kBlue);
  V(v1, 300, 500, -0.5f, kBlue);
  V(v2, 100, 300, -0.9f, kRed);
  V(v2, 500, 300, -0.1f, kRed);
  V(v2, 300, 520, -0.5f, kRed);
  b.Draw(RE4_GX_V_COLOR0, 16, v1, 3);
  b.Draw(RE4_GX_V_COLOR0, 16, v2, 3);
  // GREATER and no depth update
  b.Bp(0x40, 1 | 4 << 1);
  Quad(b, 400, 0, 500, 100, -0.5f, kWhite);
  b.Bp(0x40, 1 | 3 << 1 | 1 << 4);
}

void SceneBlend(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  Quad(b, 0, 0, 100, 100, 0, kBlue);
  const std::uint8_t half[4] = {255, 0, 0, 128};
  b.Bp(0x41, 1 | 1 << 3 | 1 << 4 | 5 << 5 | 4 << 8);
  Quad(b, 0, 0, 100, 100, 0, half);
  // additive, dst colour
  b.Bp(0x41, 1 | 1 << 3 | 1 << 4 | 1 << 5 | 1 << 8);
  Quad(b, 50, 50, 150, 150, 0, half);
  b.Bp(0x41, 1 | 1 << 3 | 1 << 4 | 0 << 5 | 2 << 8);
  Quad(b, 100, 100, 200, 200, 0, kGreen);
  b.Bp(0x41, 1 << 3 | 1 << 4);
  b.Bp(0xF3, 100 | 6u << 16 | 0u << 19 | 0u << 22);
  const std::uint8_t low[4] = {0, 255, 0, 50};
  Quad(b, 200, 0, 300, 100, 0, low);
  b.Bp(0xF3, 40 | 6u << 16 | 7u << 19);
  Quad(b, 400, 0, 500, 100, 0, low);
  // alpha test with two compares (OR, XOR)
  b.Bp(0xF3, 60 | 200u << 8 | 1u << 16 | 4u << 19 | 1u << 22);
  std::vector<std::uint8_t> ramp;
  const std::uint8_t a0[4] = {255, 255, 0, 0}, a1[4] = {255, 255, 0, 255};
  V(ramp, 0, 300, 0, a0);
  V(ramp, 640, 300, 0, a1);
  V(ramp, 640, 340, 0, a1);
  V(ramp, 0, 340, 0, a0);
  b.Draw(RE4_GX_V_COLOR0, 16, ramp, 4, 0x80);
  b.Bp(0xF3, 60 | 200u << 8 | 4u << 16 | 1u << 19 | 2u << 22);
  std::vector<std::uint8_t> ramp2;
  for (int i = 0; i < 4; i++) {
    const std::uint32_t at = static_cast<std::uint32_t>(i * 16);
    float y;
    std::memcpy(&y, &ramp[at + 4], 4);
    ramp2.insert(ramp2.end(), ramp.begin() + at, ramp.begin() + at + 16);
    const float ny = y + 60;
    std::memcpy(&ramp2[at + 4], &ny, 4);
  }
  b.Draw(RE4_GX_V_COLOR0, 16, ramp2, 4, 0x80);
  b.Bp(0xF3, 7u << 16 | 7u << 19);
}

void SceneTexture(std::vector<Builder> &c, std::mt19937 &) {
  std::vector<std::uint8_t> tex(64);
  for (int i = 0; i < 16; i++) {
    const int x = i % 4, y = i / 4;
    tex[i * 2] = 255;
    tex[i * 2 + 1] = static_cast<std::uint8_t>(x * 60);
    tex[32 + i * 2] = static_cast<std::uint8_t>(y * 60);
    tex[32 + i * 2 + 1] = 200;
  }
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  b.Bp(0x00, 1u | 1u << 4);
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7});
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  b.Bp(0x80, 0);
  b.Bp(0x84, 0);
  b.Bp(0x30, 3);
  b.Bp(0x31, 3);
  b.Texture(0, 0x1234, 0, 0x100000, 4, 4, 6, tex);
  TexQuad(b, 100, 100, 164, 164, 0, 0, 1, 1);
  b.Texture(0, 0x1234, 0, 0x100000, 4, 4, 6, {});
  TexQuad(b, 300, 100, 364, 164, 0, 0, 1, 1);
  // linear filtering, magnified
  b.Bp(0x80, 1 << 4 | 4 << 5);
  TexQuad(b, 100, 250, 356, 506, 0, 0, 1, 1);
}

void SceneTev(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  b.Bp(0xE2, 10 | 40u << 12 | 1u << 23);
  b.Bp(0xE3, 30 | 20u << 12 | 1u << 23);
  b.Bp(0xF6, (0 | 1 << 2) | 0x0Du << 4);
  b.Bp(0xC0, 14u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  Quad(b, 0, 0, 50, 50, 0, kWhite);
  b.Bp(0xE2, 0 | 255u << 12);
  b.Bp(0xE3, 200 | 0u << 12);
  b.Bp(0xC0, 15u | 13u << 4 | 2u << 8 | 10u << 12 | 1u << 19);
  Quad(b, 100, 0, 150, 50, 0, kRed);
  b.Bp(0xC0, 15u | 12u << 4 | 13u << 8 | 10u << 12 | 3u << 16 | 0u << 18 | 3u << 20 | 1u << 19);
  Quad(b, 200, 0, 250, 50, 0, kRed);
  const std::uint8_t grey[4] = {100, 100, 100, 255};
  b.Bp(0xC0, 10u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 18 | 1u << 20 | 1u << 19);
  Quad(b, 300, 0, 350, 50, 0, grey);
  // lerp across a gradient with every scale and both ops (the rounding)
  for (unsigned scale = 0; scale < 4; scale++)
    for (unsigned op = 0; op < 2; op++)
      for (unsigned bias = 0; bias < 3; bias++) {
        b.Bp(0xE4, 77 | 200u << 12);                        // C1 RA
        b.Bp(0xE5, 33 | 140u << 12);                        // C1 BG
        b.Bp(0xC0, 4u | 10u << 4 | 2u << 8 | 4u << 12 | bias << 16 | op << 18 | scale << 20 | 1u << 19);
        b.Bp(0xC1, 2u | 5u << 7 | 1u << 10 | 2u << 13 | bias << 16 | op << 18 | scale << 20 | 1u << 19);
        const float x = static_cast<float>(scale * 160), y = static_cast<float>(80 + (op * 3 + bias) * 70);
        std::vector<std::uint8_t> v;
        const std::uint8_t c0[4] = {0, 0, 0, 0}, c1[4] = {255, 255, 255, 255};
        V(v, x, y, 0, c0);
        V(v, x + 150, y, 0, c1);
        V(v, x + 150, y + 60, 0, c1);
        V(v, x, y + 60, 0, c0);
        b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
      }
}

void SceneLighting(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  b.Xf(0x100A, {0x202020FFu});
  b.Xf(0x100E, {1u | 1u << 1 | 1u << 2 | 2u << 7 | 2u << 9});
  std::vector<std::uint32_t> light(16, 0);
  light[3] = 0xFFFFFFFFu;
  light[4] = Bits(1.0f);
  light[7] = Bits(1.0f);
  light[12] = Bits(100000.0f);
  b.Xf(0x600, light);
  // a second, spot light (attenuation 3) at the left, coloured
  std::vector<std::uint32_t> spot(16, 0);
  spot[3] = 0xFF8040FFu;
  spot[4] = Bits(0.5f);
  spot[5] = Bits(0.5f);
  spot[7] = Bits(1.0f);
  spot[8] = Bits(0.001f);
  spot[10] = Bits(100.0f);
  spot[11] = Bits(300.0f);
  spot[12] = Bits(200.0f);
  spot[13] = Bits(0.0f);
  spot[14] = Bits(0.0f);
  spot[15] = Bits(-1.0f);
  b.Xf(0x610, spot);
  auto quad = [&](float x0, float y0, float nx, float nz, std::uint32_t ctrl) {
    b.Xf(0x100E, {ctrl});
    std::vector<std::uint8_t> v;
    for (int i = 0; i < 4; i++) {
      AppendF(v, {i == 1 || i == 2 ? x0 + 140 : x0, i >= 2 ? y0 + 140 : y0, 0.0f});
      const float len = std::sqrt(nx * nx + nz * nz);
      AppendF(v, {nx / len, 0.0f, nz / len});
      Append(v, kWhite, 4);
    }
    b.Draw(RE4_GX_V_NORMAL | RE4_GX_V_COLOR0, 28, v, 4, 0x80);
  };
  quad(0, 0, 0, 1, 1u | 1u << 1 | 1u << 2 | 2u << 7 | 2u << 9);
  quad(160, 0, 0, -1, 1u | 1u << 1 | 1u << 2 | 2u << 7 | 2u << 9);
  quad(320, 0, 0.5f, 1, 1u | 1u << 1 | 3u << 2 | 2u << 7 | 3u << 9);
  quad(480, 0, -0.5f, 1, 1u | 1u << 1 | 2u << 2 | 1u << 7 | 3u << 9);
  quad(0, 200, 0.3f, 1, 0u | 1u << 1 | 1u << 2 | 2u << 7 | 1u << 9);
  quad(160, 200, 0.3f, 1, 1u | 1u << 1 | 1u << 6 | 3u << 2 | 0u << 7 | 2u << 9);
}

void SceneCopyPalette(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  Quad(b, 0, 0, 32, 32, 0, kRed);
  Quad(b, 8, 8, 24, 24, 0, kGreen);
  b.Bp(0x49, 0);
  b.Bp(0x4A, 31 | 31u << 10);
  b.Bp(0x4D, 32);
  b.Bp(0x4B, 0x200000 >> 5);
  b.Bp(0x52, 6u << 4 | 2u << 15);
  b.Bp(0x4A, 639 | 527u << 10);
  b.Bp(0x00, 1u | 1u << 4);
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7});
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  b.Bp(0x30, 31);
  b.Bp(0x31, 31);
  b.Texture(0, 0x200000ull | 1ull << 32, RE4_GX_TEX_COPY, 0x200000, 32, 32, 6, {});
  TexQuad(b, 300, 300, 364, 364, 0, 0, 1, 1);
  std::vector<std::uint8_t> palette(512);
  for (int i = 0; i < 256; i++) {
    palette[i * 2] = i == 5 ? 0x07 : 0xF8;
    palette[i * 2 + 1] = i == 5 ? 0xE0 : 0x00;
  }
  const std::uint32_t tlutAt = 0x200 << 9;
  {
    const std::uint32_t at = b.Blob(palette.data(), palette.size());
    const std::uint32_t p[3] = {tlutAt, 512, at};
    b.Op(RE4_GX_OP_TLUT, p, 3);
  }
  std::vector<std::uint8_t> c8(32, 5);
  b.Bp(0x30, 7);
  b.Bp(0x31, 3);
  b.Texture(0, 0x5555, 0, 0x300000, 8, 4, 9, c8, 0x200 | 1u << 10);
  TexQuad(b, 400, 300, 440, 340, 0, 0, 1, 1);
  // the copy read through a palette (an I8 copy as C8)
  b.Bp(0x4A, 31 | 31u << 10);
  b.Bp(0x4B, 0x210000 >> 5);
  b.Bp(0x52, 1u << 4 | 1u << 15);
  b.Bp(0x4A, 639 | 527u << 10);
  std::vector<std::uint8_t> ramp(512);
  for (int i = 0; i < 256; i++) {
    const unsigned v565 = (i >> 3) << 11 | (255 - i) >> 2 << 5 | (i & 31);
    ramp[i * 2] = static_cast<std::uint8_t>(v565 >> 8);
    ramp[i * 2 + 1] = static_cast<std::uint8_t>(v565);
  }
  {
    const std::uint32_t at = b.Blob(ramp.data(), ramp.size());
    const std::uint32_t p[3] = {0x240 << 9, 512, at};
    b.Op(RE4_GX_OP_TLUT, p, 3);
  }
  b.Bp(0x30, 31);
  b.Bp(0x31, 31);
  b.Texture(0, 0x210000ull | 2ull << 32, RE4_GX_TEX_COPY, 0x210000, 32, 32, 9, {}, 0x240 | 1u << 10);
  TexQuad(b, 500, 300, 564, 364, 0, 0, 1, 1);
  b.Bp(0x49, 0);
  b.Bp(0x4A, 639 | 527u << 10);
  DisplayCopy(b, false, kBlack);
}

std::uint32_t FogWord(float f) {
  const std::uint32_t v = Bits(f);
  return ((v >> 12) & 0x7FF) | ((v >> 23) & 0xFF) << 11 | (v >> 31) << 19;
}

void SceneFog(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  b.Bp(0xEE, FogWord(1.0f));
  b.Bp(0xF1, FogWord(0.0f) | 1u << 20 | 2u << 21);
  b.Bp(0xF2, 0xFFFFFF);
  Quad(b, 0, 0, 50, 50, -0.5f, kBlack);
  // a ramp in depth through every fog function
  for (unsigned f = 2; f < 8; f++) {
    b.Bp(0xF1, FogWord(0.1f) | 1u << 20 | f << 21);
    b.Bp(0xF2, 0x4080C0);
    std::vector<std::uint8_t> v;
    const float y = 60.0f + static_cast<float>(f - 2) * 75.0f;
    V(v, 0, y, 0.0f, kRed);
    V(v, 640, y, -1.0f, kRed);
    V(v, 640, y + 70, -1.0f, kGreen);
    V(v, 0, y + 70, 0.0f, kGreen);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  }
}

void SceneCopyFilter(std::vector<Builder> &c, std::mt19937 &) {
  const unsigned taps[3][7] = {{10, 10, 12, 15, 12, 10, 10}, {6, 6, 8, 9, 8, 6, 6}, {20, 20, 25, 30, 25, 20, 20}};
  const std::uint8_t grey[4] = {100, 100, 100, 255};
  for (const auto &t : taps) {
    Builder &b = NewChunk(c);
    b.Bp(0x53, t[0] | t[1] << 6 | t[2] << 12 | t[3] << 18);
    b.Bp(0x54, t[4] | t[5] << 6 | t[6] << 12);
    DisplayCopy(b, true, kBlack);
    Quad(b, 10, 20, 110, 70, 0, grey);
    Quad(b, 200, 20, 300, 70, 0, kWhite);
    DisplayCopy(b, false, kBlack);
  }
  Builder &b = NewChunk(c);
  b.Bp(0x53, 8 | 8 << 6 | 10 << 12 | 12 << 18);
  b.Bp(0x54, 10 | 8 << 6 | 8 << 12);
  DisplayCopy(b, true, kBlack);
}

// Random TEV setups (as gx_gl_check's shader variety), each drawn into a cell of an 8x8 grid
// over textured, coloured quads; `indirect`: with indirect stages. RE4_GX2_CHECK_CELL=n draws
// only cell n (the same setups; to look at one with RE4_GX2_LOG_SHADERS=1).
void SceneRandomTev(std::vector<Builder> &c, std::mt19937 &rng, bool indirect) {
  const char *onlyCell = std::getenv("RE4_GX2_CHECK_CELL");
  const int only = onlyCell ? std::atoi(onlyCell) : -1;
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  b.Bp(0x40, 0); // no depth
  for (unsigned m = 0; m < 8; m++) {
    RandomTexture(b, rng, m, 0xA000 + m + (indirect ? 0x100 : 0), re4gx::kTexRGBA8, 16, 16, 1);
    Sampling(b, m, rng() % 3, rng() % 3, rng() & 1, 4);
  }
  for (int cell = 0; cell < 64; cell++) {
    const unsigned stages = 1 + rng() % 16, ind = indirect ? 1 + rng() % 3 : 0;
    b.Bp(0x00, 2u | 2u << 4 | (stages - 1) << 10 | ind << 16);
    b.Xf(0x1009, {2});
    b.Xf(0x103F, {2});
    b.Xf(0x1040, {static_cast<std::uint32_t>(5u << 7 | (rng() & 2))});
    b.Xf(0x1041, {static_cast<std::uint32_t>(6u << 7)});
    b.Xf(0x100E, {1});
    b.Xf(0x100F, {1});
    b.Xf(0x1010, {1});
    b.Xf(0x1011, {1});
    for (unsigned s = 0; s < stages; s++) {
      b.Bp(0xC0 + 2 * s, rng() & 0xFFFFFF);
      b.Bp(0xC1 + 2 * s, rng() & 0xFFFFFF);
      b.Bp(0x10 + s, ind ? (rng() & 0x1FFFFF) : 0);
    }
    for (int i = 0; i < 8; i++) {
      b.Bp(0x28 + i, rng() & 0xFFFFFF);
      b.Bp(0xF6 + i, rng() & 0xFFFFFF);
    }
    for (int r = 0; r < 4; r++) {
      b.Bp(0xE0 + 2 * r, (rng() & 0x7FF) | (rng() & 0x7FF) << 12 | (rng() & 1) << 23);
      b.Bp(0xE1 + 2 * r, (rng() & 0x7FF) | (rng() & 0x7FF) << 12 | (rng() & 1) << 23);
    }
    if (ind) {
      b.Bp(0x27, rng() & 0xFFFFFF);
      b.Bp(0x25, rng() & 0xFFFF);
      b.Bp(0x26, rng() & 0xFFFF);
      for (int r = 0; r < 9; r++)
        b.Bp(0x06 + r, rng() & 0xFFFFFF);
    }
    b.Bp(0xF3, (rng() & 0xFFFF) | (rng() & 0xFF) << 16);
    const float x = static_cast<float>((cell % 8) * 80), y = static_cast<float>((cell / 8) * 66);
    std::vector<std::uint8_t> v;
    for (int i = 0; i < 4; i++) {
      AppendF(v, {i == 1 || i == 2 ? x + 78 : x, i >= 2 ? y + 64 : y, 0.0f});
      std::uint8_t c0[4], c1[4];
      for (int k = 0; k < 4; k++) {
        c0[k] = static_cast<std::uint8_t>(rng());
        c1[k] = static_cast<std::uint8_t>(rng());
      }
      Append(v, c0, 4);
      Append(v, c1, 4);
      const float s = i == 1 || i == 2 ? 1.5f : -0.25f, t = i >= 2 ? 1.25f : -0.5f;
      AppendF(v, {s, t, t * 0.5f, s * 2.0f});
    }
    if (only >= 0 && cell != only)
      continue;
    b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_COLOR1 | RE4_GX_V_TEX0 | (RE4_GX_V_TEX0 << 1), 36, v, 4, 0x80);
  }
}

// Every texture format, magnified and minified, linear and nearest, every wrap; mipmaps that
// stop early (GLES 2 needs the whole chain).
void SceneTextureFormats(std::vector<Builder> &c, std::mt19937 &rng) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  TextureStage(b);
  const unsigned formats[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 14};
  int cell = 0;
  for (unsigned format : formats) {
    for (int variant = 0; variant < 4; variant++, cell++) {
      const unsigned size = variant < 2 ? 16 : 64, levels = variant < 2 ? 1 : 3;
      RandomTexture(b, rng, 0, 0xF000 + cell, format, size, size, levels);
      const unsigned wrap = static_cast<unsigned>(variant) % 3;
      Sampling(b, 0, wrap, (wrap + 1) % 3, variant != 1, variant == 3 ? 6 : variant == 2 ? 5 : 4,
               variant == 3 ? 16 : 0);
      const float x = static_cast<float>((cell % 8) * 80), y = static_cast<float>((cell / 8) * 66);
      // minified up to LOD 2 with the bias: GX clamps at the last level the texture has (here 2),
      // GLES 2 goes on into the levels made here (both draw the same up to that)
      const float span = variant >= 2 ? 3.0f : 1.7f;
      TexQuad(b, x, y, x + 78, y + 64, -0.35f, -0.2f, -0.35f + span, -0.2f + span * 0.8f);
    }
  }
}

// Texture coordinate generation: from the position through a texture matrix, from the normal,
// projected, with the dual transform (post matrices, normalised), from a colour, emboss.
void SceneTexgen(std::vector<Builder> &c, std::mt19937 &rng) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  TextureStage(b);
  RandomTexture(b, rng, 0, 0xB000, re4gx::kTexRGBA8, 32, 32, 1);
  Sampling(b, 0, 1, 1, true, 4);
  // texture matrix 0 (rows 30-32): scale and move
  b.XfF(30 * 4, {1.0f / 64.0f, 0.01f, 0, 0.1f, -0.005f, 1.0f / 48.0f, 0, 0.3f, 0, 0, 1, 0});
  // texture matrix 1 (rows 33-35): projective
  b.XfF(33 * 4, {0.02f, 0, 0.3f, 0, 0, 0.02f, 0.2f, 0, 0.001f, 0.0005f, 1.0f, 0});
  // post matrices (rows 0-2, 3-5)
  b.XfF(0x500, {0.5f, 0.2f, 0, 0.25f, -0.1f, 0.7f, 0, 0.1f, 0, 0, 1, 0});
  b.XfF(0x500 + 12, {1.5f, 0, 0, 0, 0, 1.5f, 0, 0, 0.1f, 0.1f, 1.0f, 0.2f});
  struct Setup {
    std::uint32_t gen, post, dual, matrix;
  } setups[] = {
      {0u << 7 | 0u << 1, 0, 0, 30},                // from the position, ST, matrix 0
      {1u << 7, 0, 0, 30},                          // from the normal
      {0u << 7 | 1u << 1 | 1u << 2, 0, 0, 33},     // projected (STQ), from the position (xyz)
      {5u << 7, 0, 1, 30},                          // TEX0 with the dual transform
      {5u << 7 | 1u << 1, 0x100 | 3, 1, 33},        // projected, dual, normalised, post rows 3-5
      {2u << 4, 0, 0, 30},                          // from colour 0 (srtg)
  };
  int cell = 0;
  for (const auto &s : setups) {
    b.Xf(0x1012, {s.dual});
    b.Xf(0x1040, {s.gen});
    b.Xf(0x1050, {s.post});
    const float x = static_cast<float>((cell % 3) * 210), y = static_cast<float>((cell / 3) * 260);
    std::vector<std::uint8_t> v;
    for (int i = 0; i < 4; i++) {
      const float px = i == 1 || i == 2 ? x + 200 : x, py = i >= 2 ? y + 250 : y;
      AppendF(v, {px, py, 0.0f});
      AppendF(v, {0.3f * static_cast<float>(i), 0.5f, 0.8f});
      const std::uint8_t col[4] = {static_cast<std::uint8_t>(i * 70), static_cast<std::uint8_t>(255 - i * 60), 128, 255};
      Append(v, col, 4);
      AppendF(v, {static_cast<float>(i & 1) * 3.0f - 1.0f, static_cast<float>(i >> 1) * 2.0f + 0.5f});
    }
    Re4GxDraw d{};
    b.Draw(RE4_GX_V_NORMAL | RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 36, v, 4, 0x80);
    // the draw's texture matrix 0 default: the setup's
    std::memcpy(&d, &b.ops[b.ops.size() - sizeof(Re4GxDraw) / 4], sizeof(d));
    d.matrices[0] = 0 | s.matrix << 8 | 60u << 16 | 60u << 24;
    std::memcpy(&b.ops[b.ops.size() - sizeof(Re4GxDraw) / 4], &d, sizeof(d));
    cell++;
  }
  b.Xf(0x1012, {0});
}

// Emboss (bump) mapping: texgen 1 moved from texgen 0 along the light in tangent space.
void SceneEmboss(std::vector<Builder> &c, std::mt19937 &rng) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  RandomTexture(b, rng, 0, 0xB100, re4gx::kTexIA8, 32, 32, 1);
  Sampling(b, 0, 1, 1, true, 4);
  b.Bp(0x32, 31);
  b.Bp(0x33, 31);
  b.Bp(0x00, 2u | 1u << 4 | 1u << 10);
  b.Xf(0x103F, {2});
  b.Xf(0x1040, {5u << 7});
  b.Xf(0x1041, {1u << 4 | 0u << 12 | 0u << 15}); // emboss from coordinate 0, light 0
  std::vector<std::uint32_t> light(16, 0);
  light[3] = 0xFFFFFFFFu;
  light[10] = Bits(-300.0f);
  light[11] = Bits(-200.0f);
  light[12] = Bits(400.0f);
  b.Xf(0x600, light);
  // stage 0: texture at coordinate 0; stage 1: minus the texture at the moved coordinate
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 7 << 7 | (0 | 1 << 3 | 1 << 6 | 7 << 7) << 12);
  b.Bp(0xC0, 15u | 15u << 4 | 15u << 8 | 8u << 12 | 1u << 19);
  b.Bp(0xC2, 0u | 15u << 4 | 15u << 8 | 8u << 12 | 1u << 18 | 1u << 16 | 1u << 19);
  std::vector<std::uint8_t> v;
  for (int i = 0; i < 4; i++) {
    AppendF(v, {i == 1 || i == 2 ? 600.0f : 40.0f, i >= 2 ? 480.0f : 40.0f, 0.0f});
    AppendF(v, {0, 0, 1, 0, 1, 0, 1, 0, 0}); // normal, binormal, tangent
    Append(v, kWhite, 4);
    AppendF(v, {static_cast<float>(i == 1 || i == 2) * 4.0f, static_cast<float>(i >= 2) * 3.0f});
  }
  b.Draw(RE4_GX_V_NORMAL | RE4_GX_V_BINORMAL_TANGENT | RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 60, v, 4, 0x80);
}

// Matrices by vertex (skinning): position matrices 0..3 and texture matrices by index in the
// vertex.
void SceneSkinning(std::vector<Builder> &c, std::mt19937 &rng) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  TextureStage(b);
  RandomTexture(b, rng, 0, 0xB200, re4gx::kTexRGB565, 32, 32, 1);
  Sampling(b, 0, 1, 1, true, 4);
  for (int m = 0; m < 4; m++) {
    const float a = 0.2f * static_cast<float>(m), s = 1.0f - 0.1f * static_cast<float>(m);
    b.XfF(static_cast<std::uint32_t>(m * 3 * 4),
          {s * std::cos(a), -s * std::sin(a), 0, 60.0f + 140.0f * static_cast<float>(m), s * std::sin(a),
           s * std::cos(a), 0, 80.0f + 30.0f * static_cast<float>(m), 0, 0, 1, 0});
    b.XfF(static_cast<std::uint32_t>((30 + m * 3) * 4),
          {1.0f + 0.5f * static_cast<float>(m), 0, 0, 0.1f * static_cast<float>(m), 0, 1.0f, 0, 0, 0, 0, 1, 0});
  }
  std::vector<std::uint8_t> v;
  unsigned count = 0;
  for (int quad = 0; quad < 6; quad++)
    for (int i = 0; i < 4; i++) {
      const float px = static_cast<float>((i == 1 || i == 2) * 100), py = static_cast<float>((i >= 2) * 90 + quad * 60);
      AppendF(v, {px, py, 0.0f});
      const std::uint8_t col[4] = {255, static_cast<std::uint8_t>(quad * 40), static_cast<std::uint8_t>(i * 60), 255};
      Append(v, col, 4);
      AppendF(v, {static_cast<float>(i == 1 || i == 2), static_cast<float>(i >= 2)});
      const std::uint8_t pos = static_cast<std::uint8_t>(3 * ((quad + (i >> 1)) % 4));
      const std::uint8_t tex = static_cast<std::uint8_t>(30 + 3 * (quad % 4));
      const std::uint8_t mtx[12] = {pos, tex, 60, 60, 60, 60, 60, 60, 60, 0, 0, 0};
      Append(v, mtx, 12);
      count++;
    }
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0 | RE4_GX_V_MATRIX, 36, v, count, 0x80);
}

// A perspective scene: quads at distances, overlapping in depth, exponential fog in perspective.
void ScenePerspective(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  // perspective projection (near 10, far 10000, 60 degrees): depth 0 at the near plane
  const float n = 10.0f, f = 10000.0f, t = std::tan(30.0f * 3.14159265f / 180.0f), aspect = 640.0f / 528.0f;
  b.XfF(0x1020, {1.0f / (t * aspect), 0.0f, 1.0f / t, 0.0f, -n / (f - n), -(f * n) / (f - n)});
  b.Xf(0x1026, {0});
  // perspective fog: ze = A 2^24 / (B - (z24 >> 1)) with B = 2^24 - 1 stays between A and 2A
  b.Bp(0xEE, FogWord(1.5f));
  b.Bp(0xEF, 0xFFFFFF);
  b.Bp(0xF0, 1);
  b.Bp(0xF1, FogWord(1.2f) | 0u << 20 | 4u << 21);
  b.Bp(0xF2, 0x8090A0);
  // screen rectangle (fractions) at view depth z, tilted in depth
  auto quad = [&](float sx0, float sy0, float sx1, float sy1, float z, float tilt, const std::uint8_t col[4]) {
    std::vector<std::uint8_t> v;
    auto at = [&](float sx, float sy, float zz) {
      V(v, (2.0f * sx - 1.0f) * t * aspect * -zz, (1.0f - 2.0f * sy) * t * -zz, zz, col);
    };
    at(sx0, sy0, z);
    at(sx1, sy0, z);
    at(sx1, sy1, z - tilt);
    at(sx0, sy1, z - tilt);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  };
  for (int q = 0; q < 12; q++) {
    const std::uint8_t col[4] = {static_cast<std::uint8_t>(255 - q * 20), static_cast<std::uint8_t>(q * 20), 60, 255};
    const float sx = 0.03f + 0.078f * static_cast<float>(q), sy = 0.1f + 0.06f * static_cast<float>(q % 5);
    quad(sx, sy, sx + 0.12f, sy + 0.4f, -20.0f - 400.0f * static_cast<float>(q), 300.0f, col);
  }
  quad(0.1f, 0.6f, 0.9f, 0.95f, -500.0f, -400.0f, kWhite);
}

// EFB copies in every colour format (and half size), drawn back as textures.
void SceneCopyFormats(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  // a colour gradient in the corner to copy
  std::vector<std::uint8_t> v;
  const std::uint8_t c0[4] = {255, 20, 60, 30}, c1[4] = {20, 230, 90, 250}, c2[4] = {90, 60, 240, 140};
  V(v, 0, 0, 0, c0);
  V(v, 64, 0, 0, c1);
  V(v, 64, 64, 0, c2);
  V(v, 0, 64, 0, c1);
  b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  struct Fmt {
    std::uint32_t control;
    unsigned texFormat;
  } fmts[] = {{6u << 4, 6},          {4u << 4, 4},          {5u << 4, 5},          {1u << 4 | 1u << 15, 1},
              {3u << 4 | 1u << 15, 3}, {1u << 3 | 0u << 4, 1}, {2u << 4, 3},          {7u << 4, 1},
              {1u << 3 | 1u << 4, 1},  {1u << 3 | 2u << 4, 1}, {1u << 3 | 3u << 4, 3}, {1u << 3 | 4u << 4, 3},
              {6u << 4 | 1u << 9, 6},  {1u << 4 | 1u << 15 | 1u << 9, 1}};
  for (unsigned i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
    const bool half = (fmts[i].control >> 9) & 1;
    b.Bp(0x49, 0);
    b.Bp(0x4A, 63 | 63u << 10);
    b.Bp(0x4B, (0x400000 + i * 0x4000) >> 5);
    b.Bp(0x52, fmts[i].control);
    const unsigned size = half ? 32 : 64;
    b.Bp(0x00, 1u | 1u << 4);
    b.Xf(0x103F, {1});
    b.Xf(0x1040, {5u << 7});
    b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
    b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
    b.Bp(0xC1, 4u << 4 | 7u << 7 | 7u << 10 | 7u << 13 | 1u << 19);
    b.Bp(0x30, size - 1);
    b.Bp(0x31, size - 1);
    b.Bp(0x80, 0);
    b.Texture(0, (0x400000ull + i * 0x4000) | (10ull + i) << 32, RE4_GX_TEX_COPY, 0x400000 + i * 0x4000, size, size,
              fmts[i].texFormat, {});
    const float x = static_cast<float>((i % 7) * 90), y = static_cast<float>(100 + (i / 7) * 90);
    TexQuad(b, x, y, x + 80, y + 80, 0, 0, 1, 1);
    // back to one colour channel for the next gradient draws
    b.Bp(0x00, 1u << 4);
    b.Xf(0x103F, {0});
    b.Bp(0x28, 0x3F7u << 12 | 0x3F7u);
    b.Bp(0xC0, 10u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
    b.Bp(0xC1, 5u << 4 | 7u << 7 | 7u << 10 | 7u << 13 | 1u << 19);
  }
  b.Bp(0x4A, 639 | 527u << 10);
}

// Depth copies (EFB pixel format Z24), the high byte (Z8; full and half size), drawn back as
// textures. RE4 itself never sets the Z24 pixel format (main_sub.cpp: RGB8, RGBA6, RGB565), so
// the lower bytes, in which the renderers' depth buffers differ, are left out. On the Tegra 3
// they go through a read-back of the 16-bit depth.
void SceneDepthCopy(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  std::vector<std::uint8_t> v;
  V(v, 0, 0, -0.1f, kWhite);
  V(v, 64, 0, -0.9f, kWhite);
  V(v, 64, 64, -0.5f, kWhite);
  V(v, 0, 64, -0.3f, kWhite);
  b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  for (unsigned i = 0; i < 2; i++) {
    const unsigned size = i ? 32 : 64;
    b.Bp(0x43, 3); // Z24 pixel format: copies read the depth
    b.Bp(0x49, 0);
    b.Bp(0x4A, 63 | 63u << 10);
    b.Bp(0x4B, (0x500000 + i * 0x4000) >> 5);
    b.Bp(0x52, 1u << 4 | i << 9);
    b.Bp(0x43, 1);
    b.Bp(0x00, 1u | 1u << 4);
    b.Xf(0x103F, {1});
    b.Xf(0x1040, {5u << 7});
    b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
    b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
    b.Bp(0x30, size - 1);
    b.Bp(0x31, size - 1);
    b.Bp(0x80, 0);
    b.Texture(0, (0x500000ull + i * 0x4000) | (40ull + i) << 32, RE4_GX_TEX_COPY, 0x500000 + i * 0x4000, size, size, 1,
              {});
    const float x = static_cast<float>(i * 120), y = 200;
    TexQuad(b, x, y, x + 100, y + 100, 0, 0, 1, 1);
  }
  b.Bp(0x4A, 639 | 527u << 10);
}

void ScenePointsLines(std::vector<Builder> &c, std::mt19937 &rng) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  for (int w = 0; w < 4; w++) {
    b.Bp(0x22, static_cast<std::uint32_t>(6 + 12 * w) | static_cast<std::uint32_t>(6 + 18 * w) << 8);
    std::vector<std::uint8_t> lines, points;
    for (int i = 0; i < 8; i++) {
      const std::uint8_t col[4] = {static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()), 200, 255};
      V(lines, static_cast<float>(20 + w * 150), static_cast<float>(20 + i * 30), 0, col);
      V(lines, static_cast<float>(140 + w * 150), static_cast<float>(40 + i * 30), 0, col);
      V(points, static_cast<float>(30 + w * 150 + i * 14), static_cast<float>(330 + (i & 1) * 40), 0, col);
    }
    b.Draw(RE4_GX_V_COLOR0, 16, lines, 16, 0xA8, RE4_GX_LINES);
    b.Draw(RE4_GX_V_COLOR0, 16, points, 8, 0xB8, RE4_GX_POINTS);
  }
}

void SceneDstAlphaLogic(std::vector<Builder> &c, std::mt19937 &) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  Quad(b, 0, 0, 640, 528, 0, kBlue);
  const std::uint8_t half[4] = {200, 100, 50, 128};
  // destination alpha 77 with plain colour
  b.Bp(0x42, 77 | 1u << 8);
  Quad(b, 10, 10, 110, 110, 0, half);
  // with blending that needs the source alpha (approximated: 0)
  b.Bp(0x41, 1 | 1 << 3 | 1 << 4 | 5 << 5 | 4 << 8);
  Quad(b, 120, 10, 220, 110, 0, half);
  b.Bp(0x42, 0);
  // subtract
  b.Bp(0x41, 1 << 11 | 1 << 3 | 1 << 4);
  Quad(b, 230, 10, 330, 110, 0, half);
  // logic operations: clear, noop, set, copy
  const unsigned ops[4] = {0, 5, 15, 3};
  for (int i = 0; i < 4; i++) {
    b.Bp(0x41, 1 << 1 | 1 << 3 | 1 << 4 | ops[i] << 12);
    Quad(b, static_cast<float>(10 + i * 110), 150, static_cast<float>(110 + i * 110), 250, 0, half);
  }
  b.Bp(0x41, 1 << 3 | 1 << 4);
  // alpha only, colour off
  b.Bp(0x41, 1 << 4);
  Quad(b, 10, 300, 110, 400, 0, half);
  b.Bp(0x41, 1 << 3 | 1 << 4);
  // RGB8 pixel format: alpha not written
  b.Bp(0x43, 0);
  Quad(b, 150, 300, 250, 400, 0, half);
  b.Bp(0x43, 1);
}

// Texture coordinates repeated far (the Tegra 3 loses precision here; on Mesa both match).
void SceneRepeats(std::vector<Builder> &c, std::mt19937 &rng) {
  Builder &b = NewChunk(c);
  DisplayCopy(b, true, kBlack);
  TextureStage(b);
  RandomTexture(b, rng, 0, 0xB300, re4gx::kTexRGBA8, 64, 64, 1);
  Sampling(b, 0, 1, 1, false, 0);
  const float offsets[4] = {0, 4, 16, 64};
  for (int i = 0; i < 4; i++)
    TexQuad(b, static_cast<float>(i * 160), 0, static_cast<float>(i * 160 + 150), 250, offsets[i], offsets[i],
            offsets[i] + 2, offsets[i] + 3);
}

// How a scene is compared, in each of the three runs (gx_gl2_check main): the share of the
// pictures' pixels allowed beyond the tolerance. "direct": as the renderer runs (texture
// coordinates scaled in the vertex shader: they differ from the GLES 3 renderer's 1/128-texel
// rounding where a texel edge falls in between, so a nearest read takes the next texel, a linear
// one a slightly other mix); "exact": with the GLES 3 renderer's rounding (GxOptions::texelCoords):
// the same arithmetic, the same texels; "tegra3": as the OUYA (16-bit depth, depth copies read
// back).
struct SceneEntry {
  const char *name;
  Scene build;
  int tolerance; // per channel
  double direct, exact, tegra3;
  bool xfb; // the scene ends with a display copy: its XFB is compared too
};

std::vector<SceneEntry> Scenes() {
  return {
      {"basics", SceneBasics, 1, 0, 0, 0, true},
      {"culling", SceneCulling, 1, 0, 0, 0, false},
      {"depth", SceneDepth, 1, 0.001, 0.001, 0.004, false},
      {"blend and alpha test", SceneBlend, 1, 0, 0, 0, false},
      {"texture", SceneTexture, 1, 0.001, 0, 0.001, false},
      {"tev", SceneTev, 0, 0, 0, 0, false},
      {"lighting", SceneLighting, 1, 0, 0, 0, false},
      {"copy and palette", SceneCopyPalette, 1, 0, 0, 0, true},
      {"fog", SceneFog, 1, 0, 0, 0, false},
      {"copy filter", SceneCopyFilter, 1, 0, 0, 0, true},
      {"random tev", [](std::vector<Builder> &c, std::mt19937 &r) { SceneRandomTev(c, r, false); }, 1, 0.01, 0, 0.01,
       false},
      // one cell reads a texel corner (coordinate 0, linear): a tie x.5 Mesa breaks one way in the
      // GLES 3 renderer, the other in this one, scaled by two in the stages after
      {"random tev, indirect", [](std::vector<Builder> &c, std::mt19937 &r) { SceneRandomTev(c, r, true); }, 1, 0.016,
       0.016, 0.016, false},
      // direct: coordinates as floats, the GLES 3 renderer's in 1/128 texels; with the pixel centre
      // at 7/12 (since 29.10.2026) more sample points of the nearest-read cells fall within 1/128 of
      // a texel edge (833 pixels at the cells' edge columns; exact: 0)
      {"texture formats", SceneTextureFormats, 2, 0.003, 0, 0.003, false},
      {"texgen", SceneTexgen, 2, 0.002, 0, 0.002, false},
      {"emboss", SceneEmboss, 2, 0.02, 0, 0.02, false},
      {"skinning", SceneSkinning, 2, 0.002, 0, 0.002, false},
      {"perspective and fog", ScenePerspective, 2, 0.002, 0.002, 0.004, false},
      {"copy formats", SceneCopyFormats, 2, 0.002, 0.002, 0.002, false},
      {"depth copies", SceneDepthCopy, 1, 0, 0, 0.01, false},
      {"points and lines", ScenePointsLines, 2, 0.01, 0.01, 0.01, false},
      {"destination alpha and logic", SceneDstAlphaLogic, 1, 0, 0, 0, false},
      {"repeats", SceneRepeats, 2, 0.002, 0, 0.002, false},
  };
}

// --- the compact vertex ----------------------------------------------------------------------

// A GX type and fraction for `values` that holds every one exactly (F32 always does), at random
// among those that do. normal: S8 with 6 or S16 with 14 fraction bits (GX normals).
std::uint32_t PickType(const std::vector<float> &values, bool normal, std::mt19937 &rng, std::uint32_t *fraction) {
  struct Candidate {
    std::uint32_t type, fraction;
  };
  std::vector<Candidate> fits{{RE4_GX_CT_F32, 0}};
  auto fitsAll = [&](std::uint32_t type, std::uint32_t frac) {
    static const double lo[4] = {0, -128, 0, -32768}, hi[4] = {255, 127, 65535, 32767};
    for (float v : values) {
      const double s = std::ldexp(static_cast<double>(v), static_cast<int>(frac));
      if (s != std::floor(s) || s < lo[type] || s > hi[type])
        return false;
    }
    return true;
  };
  if (normal) {
    if (fitsAll(RE4_GX_CT_S8, 6))
      fits.push_back({RE4_GX_CT_S8, 6});
    if (fitsAll(RE4_GX_CT_S16, 14))
      fits.push_back({RE4_GX_CT_S16, 14});
  } else {
    for (std::uint32_t type = RE4_GX_CT_U8; type <= RE4_GX_CT_S16; type++)
      for (std::uint32_t frac = 0; frac < (type < 2 ? 8u : 16u); frac++)
        if (fitsAll(type, frac))
          fits.push_back({type, frac});
  }
  const Candidate &c = fits[rng() % fits.size()];
  *fraction = c.fraction;
  return c.type;
}

void PutValue(std::uint8_t *at, std::uint32_t type, std::uint32_t fraction, float v) {
  const long s = std::lround(std::ldexp(static_cast<double>(v), static_cast<int>(fraction)));
  switch (type) {
  case RE4_GX_CT_U8:
  case RE4_GX_CT_S8:
    at[0] = static_cast<std::uint8_t>(s);
    break;
  case RE4_GX_CT_U16:
  case RE4_GX_CT_S16: {
    const std::uint16_t h = static_cast<std::uint16_t>(s);
    std::memcpy(at, &h, 2);
    break;
  }
  default:
    std::memcpy(at, &v, 4);
    break;
  }
}

std::uint32_t TypeSize(std::uint32_t type) { return type <= RE4_GX_CT_S8 ? 1 : type <= RE4_GX_CT_S16 ? 2 : 4; }

// The chunk with every float draw rewritten as the core writes compact draws.
void Compactify(const Builder &in, Builder &out, std::mt19937 &rng) {
  out.ops.clear();
  out.vertices.clear();
  out.indices = in.indices;
  out.blob = in.blob;
  out.flags = in.flags;
  std::memcpy(out.bp, in.bp, sizeof(in.bp));
  std::uint32_t last[RE4_GX_COMPACT_ATTRS] = {};
  bool sent = false;
  for (std::size_t at = 0; at < in.ops.size();) {
    const std::uint32_t head = in.ops[at], op = head & 0xFF, n = head >> 8;
    if (op != RE4_GX_OP_DRAW) {
      out.ops.insert(out.ops.end(), in.ops.begin() + static_cast<long>(at), in.ops.begin() + static_cast<long>(at + 1 + n));
      at += 1 + n;
      continue;
    }
    Re4GxDraw d;
    std::memcpy(&d, &in.ops[at + 1], sizeof(d));
    at += 1 + n;
    // the float vertex's parts
    const std::uint32_t L = d.layout;
    std::uint32_t off = 12, nrmOff = 0, clrOff[2] = {}, texOff[8] = {}, mtxOff = 0;
    unsigned nrmFloats = 0;
    if (L & RE4_GX_V_NORMAL) {
      nrmOff = off;
      nrmFloats = (L & RE4_GX_V_BINORMAL_TANGENT) ? 9 : 3;
      off += nrmFloats * 4;
    }
    for (int k = 0; k < 2; k++)
      if (L & (RE4_GX_V_COLOR0 << k)) {
        clrOff[k] = off;
        off += 4;
      }
    for (int t = 0; t < 8; t++)
      if (L & (RE4_GX_V_TEX0 << t)) {
        texOff[t] = off;
        off += 8;
      }
    if (L & RE4_GX_V_MATRIX)
      mtxOff = off;
    const std::uint8_t *src = in.vertices.data() + d.vertexOffset;
    auto floatAt = [&](std::uint32_t i, std::uint32_t o) {
      float f;
      std::memcpy(&f, src + static_cast<std::size_t>(i) * d.stride + o, 4);
      return f;
    };
    // types
    std::uint32_t words[RE4_GX_COMPACT_ATTRS] = {};
    std::uint32_t outAt = 0;
    auto word = [&](std::uint32_t type, std::uint32_t comps, std::uint32_t fraction) {
      return RE4_GX_CA_PRESENT | outAt | type << 8 | comps << 12 | fraction << 16;
    };
    {
      std::vector<float> all;
      bool flat = true;
      for (std::uint32_t i = 0; i < d.vertexCount; i++)
        for (std::uint32_t k = 0; k < 3; k++) {
          all.push_back(floatAt(i, 4 * k));
          if (k == 2 && floatAt(i, 8) != 0.0f)
            flat = false;
        }
      const std::uint32_t comps = flat && (rng() & 1) ? 2 : 3;
      std::uint32_t fraction = 0;
      const std::uint32_t type = PickType(all, false, rng, &fraction);
      words[RE4_GX_CA_POSITION] = word(type, comps, fraction);
      outAt += (comps * TypeSize(type) + 3) & ~3u;
    }
    if (nrmFloats) {
      std::vector<float> all;
      for (std::uint32_t i = 0; i < d.vertexCount; i++)
        for (std::uint32_t k = 0; k < nrmFloats; k++)
          all.push_back(floatAt(i, nrmOff + 4 * k));
      std::uint32_t fraction = 0;
      const std::uint32_t type = PickType(all, true, rng, &fraction);
      words[RE4_GX_CA_NORMAL] = word(type, nrmFloats, fraction);
      outAt += (nrmFloats * TypeSize(type) + 3) & ~3u;
    }
    for (int k = 0; k < 2; k++)
      if (L & (RE4_GX_V_COLOR0 << k)) {
        words[RE4_GX_CA_COLOR0 + k] = word(RE4_GX_CT_RGBA8, 4, 0);
        outAt += 4;
      }
    for (int t = 0; t < 8; t++)
      if (L & (RE4_GX_V_TEX0 << t)) {
        std::vector<float> all;
        bool flat = true;
        for (std::uint32_t i = 0; i < d.vertexCount; i++) {
          all.push_back(floatAt(i, texOff[t]));
          all.push_back(floatAt(i, texOff[t] + 4));
          flat = flat && floatAt(i, texOff[t] + 4) == 0.0f;
        }
        const std::uint32_t comps = flat && (rng() & 1) ? 1 : 2;
        std::uint32_t fraction = 0;
        const std::uint32_t type = PickType(all, false, rng, &fraction);
        words[RE4_GX_CA_TEX0 + t] = word(type, comps, fraction);
        outAt += (comps * TypeSize(type) + 3) & ~3u;
      }
    std::uint32_t slots = 0, slotCount = 0;
    if (L & RE4_GX_V_MATRIX) {
      // the slots that differ from the draw's defaults, and some others at random
      for (std::uint32_t slot = 0; slot < 9; slot++) {
        const std::uint8_t def = static_cast<std::uint8_t>(d.matrices[slot / 4] >> (8 * (slot % 4)));
        bool differs = false;
        for (std::uint32_t i = 0; i < d.vertexCount; i++)
          differs = differs || src[static_cast<std::size_t>(i) * d.stride + mtxOff + slot] != def;
        if (differs || (rng() % 4) == 0) {
          slots |= 1u << slot;
          slotCount++;
        }
      }
      if (slotCount) {
        words[RE4_GX_CA_MATRIX] = RE4_GX_CA_PRESENT | outAt | RE4_GX_CT_MATRIX << 8 | slotCount << 12 | slots << 16;
        outAt += (slotCount + 3) & ~3u;
      }
    }
    const std::uint32_t stride = outAt ? outAt : 4;
    if (!sent || std::memcmp(last, words, sizeof(words)) != 0) {
      out.Op(RE4_GX_OP_COMPACT_FORMAT, words, RE4_GX_COMPACT_ATTRS);
      std::memcpy(last, words, sizeof(words));
      sent = true;
    }
    // the vertices
    const std::uint32_t start = static_cast<std::uint32_t>(out.vertices.size());
    out.vertices.resize(start + static_cast<std::size_t>(stride) * d.vertexCount, 0);
    for (std::uint32_t i = 0; i < d.vertexCount; i++) {
      std::uint8_t *dst = out.vertices.data() + start + static_cast<std::size_t>(i) * stride;
      auto put = [&](std::uint32_t w, std::uint32_t floatOff, std::uint32_t count) {
        const std::uint32_t type = (w >> 8) & 15, comps = (w >> 12) & 15, fraction = (w >> 16) & 31;
        for (std::uint32_t k = 0; k < comps && k < count; k++)
          PutValue(dst + (w & 0xFF) + k * TypeSize(type), type, fraction, floatAt(i, floatOff + 4 * k));
      };
      put(words[RE4_GX_CA_POSITION], 0, 3);
      if (nrmFloats)
        put(words[RE4_GX_CA_NORMAL], nrmOff, nrmFloats);
      for (int k = 0; k < 2; k++)
        if (L & (RE4_GX_V_COLOR0 << k))
          std::memcpy(dst + (words[RE4_GX_CA_COLOR0 + k] & 0xFF), src + static_cast<std::size_t>(i) * d.stride + clrOff[k], 4);
      for (int t = 0; t < 8; t++)
        if (L & (RE4_GX_V_TEX0 << t))
          put(words[RE4_GX_CA_TEX0 + t], texOff[t], 2);
      if (slotCount) {
        std::uint8_t *m = dst + (words[RE4_GX_CA_MATRIX] & 0xFF);
        for (std::uint32_t slot = 0, j = 0; slot < 9; slot++)
          if (slots & (1u << slot))
            m[j++] = src[static_cast<std::size_t>(i) * d.stride + mtxOff + slot];
      }
    }
    d.layout = L | RE4_GX_V_COMPACT;
    if (!slotCount)
      d.layout &= ~static_cast<std::uint32_t>(RE4_GX_V_MATRIX);
    d.stride = stride;
    d.vertexOffset = start;
    out.Op(RE4_GX_OP_DRAW, reinterpret_cast<const std::uint32_t *>(&d), sizeof(d) / 4);
  }
}

// --- pictures of the GLES 2 renderer ------------------------------------------------------------

Picture Read(GLuint texture, unsigned width, unsigned height) {
  Picture p;
  if (!texture)
    return p;
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  p.width = width;
  p.height = height;
  p.rgba.resize(static_cast<std::size_t>(width) * height * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, p.rgba.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  return p;
}

// --- the reference, in a child process -------------------------------------------------------------

constexpr std::size_t kPictureBytes = 640 * 528 * 4;

struct Shared {
  int ok;
  unsigned shaderErrors;
  unsigned xfbWidth[64], xfbHeight[64];
  std::uint8_t pictures[1]; // per scene: EFB, XFB
};

bool RunReference(Shared *shared, const std::vector<SceneEntry> &scenes) {
  std::string error;
  if (!ouyatest::MakeContext(3, &error)) {
    std::fprintf(stderr, "reference: %s\n", error.c_str());
    return false;
  }
  ouyatest::Reference reference;
  if (!reference.Init(&error)) {
    std::fprintf(stderr, "reference: %s\n", error.c_str());
    return false;
  }
  for (std::size_t s = 0; s < scenes.size(); s++) {
    std::mt19937 rng(static_cast<unsigned>(1000 + s));
    std::vector<Builder> chunks;
    scenes[s].build(chunks, rng);
    for (auto &b : chunks)
      reference.Run(b.Chunk());
    const Picture efb = reference.Efb(), xfb = reference.Xfb();
    std::uint8_t *at = shared->pictures + s * 2 * kPictureBytes;
    std::memcpy(at, efb.rgba.data(), std::min(kPictureBytes, efb.rgba.size()));
    if (!xfb.rgba.empty() && xfb.rgba.size() <= kPictureBytes) {
      std::memcpy(at + kPictureBytes, xfb.rgba.data(), xfb.rgba.size());
      shared->xfbWidth[s] = xfb.width;
      shared->xfbHeight[s] = xfb.height;
    }
  }
  shared->shaderErrors = reference.ShaderErrors();
  return true;
}

} // namespace

int main(int argc, char **argv) {
  const std::string outDir = argc > 1 ? argv[1] : "";
  const std::vector<SceneEntry> scenes = Scenes();
  const std::size_t bytes = sizeof(Shared) + scenes.size() * 2 * kPictureBytes;
  auto *shared = static_cast<Shared *>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (shared == MAP_FAILED) {
    std::fprintf(stderr, "FATAL: no shared memory\n");
    return 1;
  }
  std::memset(shared, 0, sizeof(Shared));
  // the GLES 3 renderer in a child (no version override there)
  const pid_t child = fork();
  if (child == 0) {
    unsetenv("MESA_GLES_VERSION_OVERRIDE");
    shared->ok = RunReference(shared, scenes) ? 1 : 0;
    std::fflush(stdout);
    std::fflush(stderr);
    _exit(0);
  }
  int status = 0;
  waitpid(child, &status, 0);
  if (!shared->ok) {
    std::fprintf(stderr, "FATAL: the GLES 3 reference did not run (status %d)\n", status);
    return 1;
  }
  setenv("MESA_GLES_VERSION_OVERRIDE", "2.0", 1);
  std::string error;
  if (!ouyatest::MakeContext(2, &error)) {
    std::fprintf(stderr, "FATAL: %s\n", error.c_str());
    return 1;
  }
  const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
  std::fprintf(stderr, "GLES 2 context: %s\n", version ? version : "?");
  if (!version || std::strncmp(version, "OpenGL ES 2.0", 13) != 0) {
    std::fprintf(stderr, "FATAL: not a GLES 2.0 context\n");
    return 1;
  }
  int failures = 0;
  if (shared->shaderErrors) {
    std::fprintf(stderr, "FAIL: the GLES 3 reference had %u shader errors\n", shared->shaderErrors);
    failures++;
  }
  // the three runs (SceneEntry)
  const struct {
    const char *name, *tag;
    bool tegra3, texelCoords;
  } runs[] = {{"direct", "direct", false, false},
              {"exact (1/128-texel coordinates as GLES 3)", "exact", false, true},
              {"as the Tegra 3", "tegra3", true, false}};
  for (int run = 0; run < 3; run++) {
    re4gx2::GxRenderer renderer;
    re4gx2::GxOptions options;
    options.tegra3 = runs[run].tegra3;
    options.texelCoords = runs[run].texelCoords;
    renderer.SetOptions(options);
    if (!renderer.Init(1, &error)) {
      std::fprintf(stderr, "FATAL: GLES 2 renderer: %s\n", error.c_str());
      return 1;
    }
    std::fprintf(stderr, "--- GLES 2 renderer, %s ---\n", runs[run].name);
    for (std::size_t s = 0; s < scenes.size(); s++) {
      const SceneEntry &scene = scenes[s];
      std::mt19937 rng(static_cast<unsigned>(1000 + s));
      std::vector<Builder> chunks;
      scene.build(chunks, rng);
      std::mt19937 typeRng(static_cast<unsigned>(77 + s + 1000 * run));
      std::vector<Builder> compact(chunks.size());
      for (std::size_t i = 0; i < chunks.size(); i++) {
        Compactify(chunks[i], compact[i], typeRng);
        renderer.Run(compact[i].Chunk());
      }
      Picture refEfb;
      refEfb.width = 640;
      refEfb.height = 528;
      const std::uint8_t *at = shared->pictures + s * 2 * kPictureBytes;
      refEfb.rgba.assign(at, at + kPictureBytes);
      const Picture efb = Read(renderer.EfbColor(), 640, 528);
      // RE4_GX2_CHECK_STRICT=1: no tolerance (to find where a difference of one starts)
      const char *strict = std::getenv("RE4_GX2_CHECK_STRICT");
      const int tolerance = strict && *strict == '1' ? 0 : scene.tolerance;
      const auto diff = ouyatest::Compare(refEfb, efb, tolerance);
      ouyatest::Difference xdiff;
      Picture refXfb, xfb;
      if (scene.xfb && shared->xfbWidth[s]) {
        unsigned xw = 0, xh = 0;
        const GLuint xfbTex = renderer.Xfb(&xw, &xh);
        refXfb.width = shared->xfbWidth[s];
        refXfb.height = shared->xfbHeight[s];
        refXfb.rgba.assign(at + kPictureBytes,
                           at + kPictureBytes + static_cast<std::size_t>(refXfb.width) * refXfb.height * 4);
        xfb = Read(xfbTex, xw, xh);
        xdiff = ouyatest::Compare(refXfb, xfb, tolerance, false);
      }
      const double share = run == 0 ? scene.direct : run == 1 ? scene.exact : scene.tegra3;
      const double allowed = share * 640.0 * 528.0;
      const bool ok = !diff.sizeDiffers && diff.pixels <= allowed && !xdiff.sizeDiffers && xdiff.pixels <= allowed;
      std::fprintf(stderr, "%s %-28s EFB: %6u pixels off (worst %3u)%s", ok ? "ok  " : "FAIL", scene.name, diff.pixels,
                   diff.worst, diff.sizeDiffers ? " SIZE" : "");
      if (!xfb.rgba.empty() || !refXfb.rgba.empty())
        std::fprintf(stderr, "  XFB: %6u off (worst %3u)%s", xdiff.pixels, xdiff.worst, xdiff.sizeDiffers ? " SIZE" : "");
      std::fprintf(stderr, "\n");
      if (!ok) {
        failures++;
        if (!outDir.empty()) {
          char name[160];
          std::snprintf(name, sizeof(name), "%s/%02zu-%s", outDir.c_str(), s, runs[run].tag);
          ouyatest::WritePpm(std::string(name) + "-efb-gles3.ppm", refEfb);
          ouyatest::WritePpm(std::string(name) + "-efb-gles2.ppm", efb);
          ouyatest::WritePpm(std::string(name) + "-efb-diff.ppm", ouyatest::DifferencePicture(refEfb, efb));
          // raw RGBA as well (the alpha, which the PPMs leave out)
          auto raw = [&](const char *suffix, const Picture &pic) {
            if (FILE *f = std::fopen((std::string(name) + suffix).c_str(), "wb")) {
              std::fwrite(pic.rgba.data(), 1, pic.rgba.size(), f);
              std::fclose(f);
            }
          };
          raw("-efb-gles3.rgba", refEfb);
          raw("-efb-gles2.rgba", efb);
          if (!refXfb.rgba.empty()) {
            ouyatest::WritePpm(std::string(name) + "-xfb-gles3.ppm", refXfb);
            ouyatest::WritePpm(std::string(name) + "-xfb-gles2.ppm", xfb);
          }
        }
      }
    }
    const re4gx2::GxStats &st = renderer.Stats();
    std::fprintf(stderr,
                 "stats: %u chunks, %u draws (%u compact), %u programs, %u shader errors, %u textures, %u mipmap levels "
                 "made, %u depth copies (%u read back, %u lost), %u NPOT clamped, %u skipped\n",
                 st.chunks, st.draws, st.compactDraws, st.programs, st.shaderErrors, st.textures, st.mipmapsMade,
                 st.depthCopies, st.depthReadBacks, st.depthCopiesLost, st.npotClamped, st.skippedDraws);
    if (st.shaderErrors) {
      std::fprintf(stderr, "FAIL: shader errors\n");
      failures++;
    }
    renderer.Release();
  }
  munmap(shared, bytes);
  std::fprintf(stderr, failures ? "gx_gl2_check: %d FAILED\n" : "gx_gl2_check: all passed\n", failures);
  return failures ? 1 : 0;
}
