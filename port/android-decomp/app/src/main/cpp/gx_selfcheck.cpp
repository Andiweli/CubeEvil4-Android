// The GPU checks itself once per run (gx_selfcheck.h).
#include "gx_selfcheck.h"

#include "gx_chunk_builder.h"
#include "gx_gl.h"
#include "gx_gl_shader.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdio>

namespace re4gx {
namespace {

using namespace chunk;

struct Yuv {
  int y, u, v;
};

// black, greys, white, a red, a blue and a light colour with both colour parts high
const Yuv kColours[8] = {{16, 128, 128}, {60, 128, 128},  {128, 128, 128}, {200, 128, 128},
                         {235, 128, 128}, {180, 90, 200}, {100, 200, 60},  {235, 240, 240}};

// the movie's registers (sofdec.cpp setTevPrm, the konstant colours with patch 0054's byte order)
const int kC0[4] = {-111, 0, -138, 68};
const int kKonst[4][4] = {
    {0x66, 0x00, 0xFF, 0x32}, {0x94, 0x00, 0x94, 0x94}, {0xCB, 0x00, 0x05, 0xCF}, {0x00, 0xFF, 0x00, 0x00}};

// One TEV operation as gx_gl_shader.cpp computes it (Regular: a, b, c taken as bytes, c widened to
// 0..256, the rounding bias before the division by 256, the doubling scale inside).
int Tev(int a, int b, int c, int d, int op, int scale, bool clamp) {
  a &= 255;
  b &= 255;
  c &= 255;
  const int factor = scale == 1 ? 2 : scale == 2 ? 4 : 1;
  const int dd = d * factor;
  const int lerp = (a * 256 + (b - a) * (c + (c >> 7))) * factor; // never negative
  const int rounded = (lerp + (op ? 127 : 128)) / 256;
  const int v = op ? dd - rounded : dd + rounded;
  return clamp ? std::min(255, std::max(0, v)) : std::min(1023, std::max(-1024, v));
}

// The output after `stages` stages: PREV wrapped to bytes as the shader writes it.
void Expected(const Yuv &p, int stages, int out[4]) {
  int prev[4] = {0, 0, 0, 0};
  const int uv0[4] = {p.v, p.u, p.u, p.u}; // IA8 (I = V, A = U) through swap table 1 (R A A A)
  const int uv2[4] = {p.v, p.v, p.u, p.v}; // swap table 2 (R R A R)
  for (int s = 0; s < stages; s++) {
    int next[4];
    for (int ch = 0; ch < 4; ch++) {
      const bool alpha = ch == 3;
      switch (s) {
      case 0: // C0 + UV x K0 (alpha: A0 - U x K0.a)
        next[ch] = Tev(0, uv0[ch], kKonst[0][ch], kC0[ch], alpha, 0, false);
        break;
      case 1: // (PREV + Y x K1) x 2
        next[ch] = Tev(0, p.y, kKonst[1][ch], prev[ch], 0, 1, false);
        break;
      case 2: // PREV + UV x K2 (alpha: PREV - V x K2.a), clamped
        next[ch] = Tev(0, uv2[ch], kKonst[2][ch], prev[ch], alpha, 0, true);
        break;
      default: // colour: lerp(CPREV, APREV, K3), alpha: 0
        next[ch] = alpha ? 0 : Tev(prev[ch], prev[3], kKonst[3][ch], 0, 0, 0, true);
        break;
      }
    }
    std::copy(next, next + 4, prev);
  }
  for (int ch = 0; ch < 4; ch++)
    out[ch] = prev[ch] & 255;
}

std::uint32_t Reg(int lo, int hi, bool konst) {
  return (static_cast<std::uint32_t>(lo) & 0x7FF) | (static_cast<std::uint32_t>(hi) & 0x7FF) << 12 |
         (konst ? 1u << 23 : 0u);
}

// TEV order of a stage: texture map, coordinate, enabled, no colour channel
std::uint32_t Order(unsigned map, unsigned coord, bool on) { return map | coord << 3 | (on ? 1u : 0u) << 6 | 7u << 7; }

const int kTop[4] = {16, 96, 176, 256}; // the quads of 1, 2, 3 and 4 stages: 64 x 64 at x 0, these rows

void Build(Builder &b) {
  Baseline(b);
  const std::uint8_t black[4] = {0, 0, 0, 255};
  DisplayCopy(b, true, black);
  b.Xf(0x1009, {0});       // no colour channels
  b.Xf(0x103F, {2});       // two texture coordinates, both from TEX0
  b.Xf(0x1040, {5u << 7});
  b.Xf(0x1041, {5u << 7});
  for (unsigned r = 0x30; r <= 0x33; r++)
    b.Bp(r, 7); // coordinates 0 and 1: 8 x 8
  // map 0: Y (I8), map 1: U and V (IA8); clamped, nearest
  b.Bp(0x80, 0);
  b.Bp(0x84, 0);
  b.Bp(0x88, 7 | 7u << 10 | 1u << 20);
  b.Bp(0x81, 0);
  b.Bp(0x85, 0);
  b.Bp(0x89, 7 | 7u << 10 | 3u << 20);
  std::vector<std::uint8_t> y(64), uv(128);
  for (int row = 0; row < 8; row++)
    for (int x = 0; x < 8; x++) {
      const Yuv &p = kColours[x];
      y[(row / 4) * 32 + (row % 4) * 8 + x] = static_cast<std::uint8_t>(p.y); // tiles of 8 x 4
      const int at = ((row / 4) * 2 + x / 4) * 32 + ((row % 4) * 4 + x % 4) * 2; // tiles of 4 x 4
      uv[at] = static_cast<std::uint8_t>(p.u);                                      // A
      uv[at + 1] = static_cast<std::uint8_t>(p.v);                                  // I
    }
  b.Texture(0, 0x5E1FC4EC00000001ull, 0, 0x100000, 8, 8, 1, y);
  b.Texture(1, 0x5E1FC4EC00000002ull, 0, 0x100800, 8, 8, 3, uv);
  // setTevPrm: stages 0 and 2 read U and V (map 1, coordinate 0), stage 1 Y (map 0, coordinate 1)
  b.Bp(0x28, Order(1, 0, true) | Order(0, 1, true) << 12);
  b.Bp(0x29, Order(1, 0, true) | Order(7, 7, false) << 12);
  // colour stages: d | c << 4 | b << 8 | a << 12 | bias << 16 | op << 18 | clamp << 19 | scale << 20
  b.Bp(0xC0, 2u | 14u << 4 | 8u << 8 | 15u << 12);                 // C0 + TEXC x KONST
  b.Bp(0xC2, 0u | 14u << 4 | 8u << 8 | 15u << 12 | 1u << 20);      // (CPREV + TEXC x KONST) x 2
  b.Bp(0xC4, 0u | 14u << 4 | 8u << 8 | 15u << 12 | 1u << 19);      // CPREV + TEXC x KONST, clamped
  b.Bp(0xC6, 15u | 14u << 4 | 1u << 8 | 0u << 12 | 1u << 19);      // lerp(CPREV, APREV, KONST)
  // alpha stages: rswap | tswap << 2 | d << 4 | c << 7 | b << 10 | a << 13 | op << 18 | clamp << 19
  b.Bp(0xC1, 0u | 1u << 2 | 1u << 4 | 6u << 7 | 4u << 10 | 7u << 13 | 1u << 18);
  b.Bp(0xC3, 0u | 0u << 2 | 0u << 4 | 6u << 7 | 4u << 10 | 7u << 13 | 1u << 20);
  b.Bp(0xC5, 0u | 2u << 2 | 0u << 4 | 6u << 7 | 4u << 10 | 7u << 13 | 1u << 18 | 1u << 19);
  b.Bp(0xC7, 0u | 0u << 2 | 7u << 4 | 7u << 7 | 7u << 10 | 7u << 13 | 1u << 19);
  // swap tables 0 (R G B A), 1 (R A A A), 2 (R R A R), 3 (R G B A) with the konstant selections
  b.Bp(0xF6, 0u | 1u << 2 | 0x0Cu << 4 | 0x1Cu << 9 | 0x0Du << 14 | 0x1Du << 19);
  b.Bp(0xF7, 2u | 3u << 2 | 0x0Eu << 4 | 0x1Eu << 9 | 0x0Fu << 14);
  b.Bp(0xF8, 0u | 3u << 2);
  b.Bp(0xF9, 3u | 3u << 2);
  b.Bp(0xFA, 0u | 0u << 2);
  b.Bp(0xFB, 3u | 0u << 2);
  b.Bp(0xFC, 0u | 1u << 2);
  b.Bp(0xFD, 2u | 3u << 2);
  // registers: PREV 0, C0 (-111, 0, -138, 68); konstant colours K0..K3
  b.Bp(0xE0, Reg(0, 0, false));
  b.Bp(0xE1, Reg(0, 0, false));
  b.Bp(0xE2, Reg(kC0[0], kC0[3], false));
  b.Bp(0xE3, Reg(kC0[2], kC0[1], false));
  for (int k = 0; k < 4; k++) {
    b.Bp(0xE0 + 2 * k, Reg(kKonst[k][0], kKonst[k][3], true));
    b.Bp(0xE1 + 2 * k, Reg(kKonst[k][2], kKonst[k][1], true));
  }
  const std::uint8_t white[4] = {255, 255, 255, 255};
  for (int stages = 1; stages <= 4; stages++) {
    b.Bp(0x00, 2u | static_cast<std::uint32_t>(stages - 1) << 10); // 2 coordinates, 0 channels
    const float top = static_cast<float>(kTop[stages - 1]);
    std::vector<std::uint8_t> v;
    VT(v, 0, top, 0, white, 0, 0);
    VT(v, 64, top, 0, white, 1, 0);
    VT(v, 64, top + 64, 0, white, 1, 1);
    VT(v, 0, top + 64, 0, white, 0, 1);
    b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
  }
}

void DefaultState() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glUseProgram(0);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  for (GLuint unit = 0; unit < 8; unit++) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindSampler(unit, 0);
  }
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDepthMask(GL_TRUE);
}

} // namespace

int MovieColourPixels(std::string *detail, std::string *error) {
  int bad = 0;
  {
    GxRenderer r;
    if (!r.Init(1, error)) {
      DefaultState();
      return -1;
    }
    Builder b;
    Build(b);
    r.Run(b.Chunk());
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.EfbColor(), 0);
    int shown = 0;
    for (int stages = 1; stages <= 4; stages++)
      for (int i = 0; i < 8; i++) {
        std::uint8_t got[4] = {};
        glReadPixels(i * 8 + 4, kTop[stages - 1] + 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, got);
        int want[4];
        Expected(kColours[i], stages, want);
        if (got[0] == want[0] && got[1] == want[1] && got[2] == want[2] && got[3] == want[3])
          continue;
        bad++;
        // every wrong pixel of the full movie shader, a few of the shorter ones
        if (detail && (stages == 4 || shown < 4)) {
          shown += stages == 4 ? 0 : 1;
          char line[160];
          std::snprintf(line, sizeof(line), "; %d stage%s, Y %d U %d V %d: %d %d %d %d, want %d %d %d %d", stages,
                        stages > 1 ? "s" : "", kColours[i].y, kColours[i].u, kColours[i].v, got[0], got[1], got[2],
                        got[3], want[0], want[1], want[2], want[3]);
          *detail += line;
        }
      }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    r.Release();
  }
  DefaultState();
  return bad;
}

std::string MovieColourCheck(int *wrong) {
  // the lerp as the hardware computes it first; on a wrong pixel the other ways of writing it, the
  // first one this GPU computes right stays (SetTevMath), else the one with the fewest wrong pixels
  const int first = GetTevMath();
  std::string detail, error;
  const int bad = MovieColourPixels(&detail, &error);
  if (wrong)
    *wrong = bad;
  if (bad < 0)
    return "GPU check (movie colours): no renderer (" + error + ")";
  if (!bad)
    return "GPU check (movie colours): ok, 32 of 32 pixels as the GameCube computes them";
  char head[96];
  std::snprintf(head, sizeof(head), "GPU check (movie colours): %d of 32 pixels wrong", bad);
  std::string line = head + detail;
  int best = first, bestBad = bad;
  for (int mode : {kTevMathNoShift, kTevMathFloat}) {
    if (mode == first)
      continue;
    SetTevMath(mode);
    const int n = MovieColourPixels(nullptr, &error);
    char part[128];
    std::snprintf(part, sizeof(part), "; with %s: %d wrong", TevMathName(mode), n);
    line += part;
    if (n >= 0 && n < bestBad) {
      best = mode;
      bestBad = n;
    }
    if (n == 0)
      break;
  }
  SetTevMath(best);
  if (wrong)
    *wrong = bestBad;
  return line + "; in use: " + TevMathName(best) + (bestBad ? "" : " (the movies right)");
}

} // namespace re4gx
