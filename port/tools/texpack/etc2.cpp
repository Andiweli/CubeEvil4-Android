// ETC2 / EAC encoder and decoder (see etc2.h). Pixel j of a block is x*4+y (the formats' column order).
#include "etc2.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace etc2 {
namespace {

using u64 = std::uint64_t;
using i64 = std::int64_t;

const int kTable[8][2] = {{2, 8}, {5, 17}, {9, 29}, {13, 42}, {18, 60}, {24, 80}, {33, 106}, {47, 183}};
const int kDistance[8] = {3, 6, 11, 16, 23, 32, 41, 64};
const int kAlpha[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14}, {-3, -7, -10, -13, 2, 6, 9, 12}, {-2, -5, -8, -13, 1, 4, 7, 12},
    {-2, -4, -6, -13, 1, 3, 5, 12}, {-3, -6, -8, -12, 2, 5, 7, 11}, {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10}, {-3, -5, -8, -11, 2, 4, 7, 10}, {-2, -6, -8, -10, 1, 5, 7, 9},
    {-2, -5, -8, -10, 1, 4, 7, 9},  {-2, -4, -8, -10, 1, 3, 7, 9},  {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},  {-1, -2, -3, -10, 0, 1, 2, 9},  {-4, -6, -8, -9, 3, 5, 7, 8},
    {-3, -5, -7, -9, 2, 4, 6, 8}};

// colour error weights (about 0.3 / 0.59 / 0.12, sum 128)
const int kWr = 38, kWg = 75, kWb = 15;

inline int Clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

inline int Modifier(int table, int idx) {
  const int a = kTable[table][0], b = kTable[table][1];
  return idx == 0 ? a : idx == 1 ? b : idx == 2 ? -a : -b;
}

inline int Expand(int q, int bits) {
  switch (bits) {
    case 4: return (q << 4) | q;
    case 5: return (q << 3) | (q >> 2);
    case 6: return (q << 2) | (q >> 4);
    default: return (q << 1) | (q >> 6);  // 7
  }
}

// the code whose expansion is nearest to v
int Quant(int v, int bits) {
  const int maxc = (1 << bits) - 1;
  int q = (Clamp255(v) * maxc + 127) / 255;
  int best = q, bestErr = 1 << 30;
  for (int c = std::max(0, q - 1); c <= std::min(maxc, q + 1); c++) {
    const int e = std::abs(Expand(c, bits) - v);
    if (e < bestErr) {
      bestErr = e;
      best = c;
    }
  }
  return best;
}

inline int Sext3(int v) { return v & 4 ? v - 8 : v; }

struct Block {
  int r[16], g[16], b[16], a[16];  // by j = x*4+y
};

// gathers the 4x4 block at (bx, by), edge pixels repeated past the image
void Gather(const std::uint8_t *rgba, unsigned w, unsigned h, unsigned bx, unsigned by, Block &blk) {
  for (int x = 0; x < 4; x++) {
    const unsigned sx = std::min(w - 1, bx * 4 + x);
    for (int y = 0; y < 4; y++) {
      const unsigned sy = std::min(h - 1, by * 4 + y);
      const std::uint8_t *p = rgba + (static_cast<std::size_t>(sy) * w + sx) * 4;
      const int j = x * 4 + y;
      blk.r[j] = p[0];
      blk.g[j] = p[1];
      blk.b[j] = p[2];
      blk.a[j] = p[3];
    }
  }
}

inline bool InSub(int j, int flip, int sub) {
  // flip 0: two 2x4 halves side by side (x < 2 left); flip 1: two 4x2 halves (y < 2 top)
  return flip ? (((j & 3) >= 2) == (sub == 1)) : ((j >= 8) == (sub == 1));
}

// A half block: its pixels, which are transparent (punch-through), which modifier set
struct Sub {
  int js[8];
  int n = 0;
  const Block *blk = nullptr;
  bool transparent[16] = {};
  bool nonOpaque = false;  // punch-through block with transparent pixels: modifiers 0, +b, -b; index 2 transparent
};

struct Choice {
  int q[3] = {0, 0, 0};  // quantized base colour
  int table = 0;
  i64 err = 0;
  std::uint8_t idx[16] = {};
};

// error of one half with an 8-bit base colour and a table; best index per pixel in idx
i64 EvalSub(const Sub &s, int br, int bg, int bb, int table, std::uint8_t *idx) {
  int cr[4], cg[4], cb[4];
  bool usable[4] = {true, true, true, true};
  for (int i = 0; i < 4; i++) {
    int m = Modifier(table, i);
    if (s.nonOpaque && i == 0) m = 0;
    cr[i] = Clamp255(br + m);
    cg[i] = Clamp255(bg + m);
    cb[i] = Clamp255(bb + m);
  }
  if (s.nonOpaque) usable[2] = false;
  i64 total = 0;
  const Block &k = *s.blk;
  for (int n = 0; n < s.n; n++) {
    const int j = s.js[n];
    if (s.nonOpaque && s.transparent[j]) {
      idx[j] = 2;
      continue;
    }
    int best = 0, bestErr = 1 << 30;
    for (int i = 0; i < 4; i++) {
      if (!usable[i]) continue;
      const int dr = k.r[j] - cr[i], dg = k.g[j] - cg[i], db = k.b[j] - cb[i];
      const int e = kWr * dr * dr + kWg * dg * dg + kWb * db * db;
      if (e < bestErr) {
        bestErr = e;
        best = i;
      }
    }
    idx[j] = static_cast<std::uint8_t>(best);
    total += bestErr;
  }
  return total;
}

int ModifierFor(const Sub &s, int table, int idx) {
  if (s.nonOpaque && idx == 0) return 0;
  return Modifier(table, idx);
}

// the mean of the half's (opaque) pixels minus their modifiers, quantized
void MeanQuant(const Sub &s, const std::uint8_t *idx, int table, int bits, int q[3]) {
  int sr = 0, sg = 0, sb = 0, n = 0;
  const Block &k = *s.blk;
  for (int i = 0; i < s.n; i++) {
    const int j = s.js[i];
    if (s.nonOpaque && s.transparent[j]) continue;
    const int m = idx ? ModifierFor(s, table, idx[j]) : 0;
    sr += k.r[j] - m;
    sg += k.g[j] - m;
    sb += k.b[j] - m;
    n++;
  }
  if (n == 0) {
    q[0] = q[1] = q[2] = 0;
    return;
  }
  q[0] = Quant((sr + n / 2) / n, bits);
  q[1] = Quant((sg + n / 2) / n, bits);
  q[2] = Quant((sb + n / 2) / n, bits);
}

void Try(const Sub &s, const int q[3], int table, int bits, Choice &best) {
  std::uint8_t idx[16];
  const i64 e = EvalSub(s, Expand(q[0], bits), Expand(q[1], bits), Expand(q[2], bits), table, idx);
  if (e < best.err) {
    best.err = e;
    best.table = table;
    std::memcpy(best.q, q, sizeof(best.q));
    for (int i = 0; i < s.n; i++) best.idx[s.js[i]] = idx[s.js[i]];
  }
}

// best base colour and table for a half (bits 4: individual mode, 5: differential)
Choice SearchSub(const Sub &s, int bits, int effort) {
  Choice best;
  best.err = INT64_MAX;
  int q0[3];
  MeanQuant(s, nullptr, 0, bits, q0);
  for (int t = 0; t < 8; t++) {
    std::uint8_t idx[16];
    EvalSub(s, Expand(q0[0], bits), Expand(q0[1], bits), Expand(q0[2], bits), t, idx);
    Try(s, q0, t, bits, best);
    int q1[3];
    MeanQuant(s, idx, t, bits, q1);
    if (q1[0] != q0[0] || q1[1] != q0[1] || q1[2] != q0[2]) Try(s, q1, t, bits, best);
  }
  if (effort <= 0 || best.err == 0) return best;
  const int maxc = (1 << bits) - 1;
  const int t0 = best.table;
  int centre[3];
  std::memcpy(centre, best.q, sizeof(centre));
  for (int t = 0; t < 8; t++) {
    if (effort < 2 && std::abs(t - t0) > 1) continue;
    for (int dr = -1; dr <= 1; dr++)
      for (int dg = -1; dg <= 1; dg++)
        for (int db = -1; db <= 1; db++) {
          if (!dr && !dg && !db) continue;
          // effort 1: the axis steps and the two grey steps
          if (effort < 2 && !((dr == dg && dg == db) || (!!dr + !!dg + !!db == 1))) continue;
          const int q[3] = {centre[0] + dr, centre[1] + dg, centre[2] + db};
          if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[0] > maxc || q[1] > maxc || q[2] > maxc) continue;
          Try(s, q, t, bits, best);
        }
  }
  return best;
}

// tables only, for a fixed base (a differential half moved into the other's reach)
Choice SearchTable(const Sub &s, const int q[3], int bits) {
  Choice best;
  best.err = INT64_MAX;
  for (int t = 0; t < 8; t++) Try(s, q, t, bits, best);
  return best;
}

Sub MakeSub(const Block &blk, int flip, int sub, bool nonOpaque) {
  Sub s;
  s.blk = &blk;
  s.nonOpaque = nonOpaque;
  for (int j = 0; j < 16; j++) {
    if (InSub(j, flip, sub)) s.js[s.n++] = j;
    s.transparent[j] = blk.a[j] < kAlphaThreshold;
  }
  return s;
}

void PutIndices(u64 &w, const std::uint8_t *idx) {
  for (int j = 0; j < 16; j++) {
    w |= static_cast<u64>((idx[j] >> 1) & 1) << (16 + j);
    w |= static_cast<u64>(idx[j] & 1) << j;
  }
}

struct Candidate {
  i64 err = INT64_MAX;
  u64 word = 0;
};

void Individual(const Block &blk, int effort, Candidate &out) {
  for (int flip = 0; flip < 2; flip++) {
    const Sub s0 = MakeSub(blk, flip, 0, false), s1 = MakeSub(blk, flip, 1, false);
    const Choice c0 = SearchSub(s0, 4, effort), c1 = SearchSub(s1, 4, effort);
    const i64 e = c0.err + c1.err;
    if (e >= out.err) continue;
    u64 w = 0;
    w |= static_cast<u64>(c0.q[0]) << 60 | static_cast<u64>(c1.q[0]) << 56;
    w |= static_cast<u64>(c0.q[1]) << 52 | static_cast<u64>(c1.q[1]) << 48;
    w |= static_cast<u64>(c0.q[2]) << 44 | static_cast<u64>(c1.q[2]) << 40;
    w |= static_cast<u64>(c0.table) << 37 | static_cast<u64>(c1.table) << 34;
    w |= static_cast<u64>(flip) << 32;  // diff bit 33 = 0
    std::uint8_t idx[16];
    for (int j = 0; j < 16; j++) idx[j] = InSub(j, flip, 0) ? c0.idx[j] : c1.idx[j];
    PutIndices(w, idx);
    out.err = e;
    out.word = w;
  }
}

u64 PackDifferential(const Choice &c0, const Choice &c1, int flip, int bit33) {
  u64 w = 0;
  const int dr = c1.q[0] - c0.q[0], dg = c1.q[1] - c0.q[1], db = c1.q[2] - c0.q[2];
  w |= static_cast<u64>(c0.q[0]) << 59 | static_cast<u64>(dr & 7) << 56;
  w |= static_cast<u64>(c0.q[1]) << 51 | static_cast<u64>(dg & 7) << 48;
  w |= static_cast<u64>(c0.q[2]) << 43 | static_cast<u64>(db & 7) << 40;
  w |= static_cast<u64>(c0.table) << 37 | static_cast<u64>(c1.table) << 34;
  w |= static_cast<u64>(bit33) << 33 | static_cast<u64>(flip) << 32;
  std::uint8_t idx[16];
  for (int j = 0; j < 16; j++) idx[j] = InSub(j, flip, 0) ? c0.idx[j] : c1.idx[j];
  PutIndices(w, idx);
  return w;
}

inline bool DeltaOk(const Choice &a, const Choice &b) {
  for (int c = 0; c < 3; c++) {
    const int d = b.q[c] - a.q[c];
    if (d < -4 || d > 3) return false;
  }
  return true;
}

// differential mode; bit33: 1 for RGB8 and opaque punch-through blocks, 0 for non-opaque punch-through
void Differential(const Block &blk, int effort, bool nonOpaque, int bit33, Candidate &out) {
  for (int flip = 0; flip < 2; flip++) {
    const Sub s0 = MakeSub(blk, flip, 0, nonOpaque), s1 = MakeSub(blk, flip, 1, nonOpaque);
    Choice c0 = SearchSub(s0, 5, effort), c1 = SearchSub(s1, 5, effort);
    if (!DeltaOk(c0, c1)) {
      // move one half's base into the other's reach, keep the better of the two
      Choice a1;
      {
        int q[3];
        for (int c = 0; c < 3; c++) q[c] = std::clamp(c1.q[c], c0.q[c] - 4, c0.q[c] + 3);
        a1 = SearchTable(s1, q, 5);
      }
      Choice b0;
      {
        int q[3];
        for (int c = 0; c < 3; c++) q[c] = std::clamp(c0.q[c], c1.q[c] - 3, c1.q[c] + 4);
        b0 = SearchTable(s0, q, 5);
      }
      if (c0.err + a1.err <= b0.err + c1.err)
        c1 = a1;
      else
        c0 = b0;
    }
    const i64 e = c0.err + c1.err;
    if (e >= out.err) continue;
    out.err = e;
    out.word = PackDifferential(c0, c1, flip, bit33);
  }
}

// ---- planar mode ----

struct PlanarFit {
  float m[3][16];  // O, H, V = m * values
};

const PlanarFit &Fit() {
  static PlanarFit fit = [] {
    PlanarFit f{};
    // c(x,y) = O (1 - x/4 - y/4) + H x/4 + V y/4; least squares over the 16 pixels
    double ata[3][3] = {};
    double rows[16][3];
    for (int x = 0; x < 4; x++)
      for (int y = 0; y < 4; y++) {
        const int j = x * 4 + y;
        rows[j][0] = 1.0 - x / 4.0 - y / 4.0;
        rows[j][1] = x / 4.0;
        rows[j][2] = y / 4.0;
      }
    for (int j = 0; j < 16; j++)
      for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) ata[a][b] += rows[j][a] * rows[j][b];
    // inverse of the 3x3 matrix
    double inv[3][3];
    const double det = ata[0][0] * (ata[1][1] * ata[2][2] - ata[1][2] * ata[2][1]) -
                       ata[0][1] * (ata[1][0] * ata[2][2] - ata[1][2] * ata[2][0]) +
                       ata[0][2] * (ata[1][0] * ata[2][1] - ata[1][1] * ata[2][0]);
    inv[0][0] = (ata[1][1] * ata[2][2] - ata[1][2] * ata[2][1]) / det;
    inv[0][1] = (ata[0][2] * ata[2][1] - ata[0][1] * ata[2][2]) / det;
    inv[0][2] = (ata[0][1] * ata[1][2] - ata[0][2] * ata[1][1]) / det;
    inv[1][0] = (ata[1][2] * ata[2][0] - ata[1][0] * ata[2][2]) / det;
    inv[1][1] = (ata[0][0] * ata[2][2] - ata[0][2] * ata[2][0]) / det;
    inv[1][2] = (ata[0][2] * ata[1][0] - ata[0][0] * ata[1][2]) / det;
    inv[2][0] = (ata[1][0] * ata[2][1] - ata[1][1] * ata[2][0]) / det;
    inv[2][1] = (ata[0][1] * ata[2][0] - ata[0][0] * ata[2][1]) / det;
    inv[2][2] = (ata[0][0] * ata[1][1] - ata[0][1] * ata[1][0]) / det;
    for (int a = 0; a < 3; a++)
      for (int j = 0; j < 16; j++) {
        double v = 0;
        for (int b = 0; b < 3; b++) v += inv[a][b] * rows[j][b];
        f.m[a][j] = static_cast<float>(v);
      }
    return f;
  }();
  return fit;
}

inline int PlanarValue(int o, int h, int v, int x, int y) {
  return Clamp255((x * (h - o) + y * (v - o) + 4 * o + 2) >> 2);
}

// error of one channel's plane (codes of `bits`), weighted
i64 PlaneErr(const int *vals, int o, int h, int v, int bits, int weight) {
  const int eo = Expand(o, bits), eh = Expand(h, bits), ev = Expand(v, bits);
  i64 e = 0;
  for (int x = 0; x < 4; x++)
    for (int y = 0; y < 4; y++) {
      const int d = vals[x * 4 + y] - PlanarValue(eo, eh, ev, x, y);
      e += static_cast<i64>(weight) * d * d;
    }
  return e;
}

void FitChannel(const int *vals, int bits, int weight, int effort, int code[3], i64 &err) {
  const PlanarFit &f = Fit();
  for (int a = 0; a < 3; a++) {
    float s = 0;
    for (int j = 0; j < 16; j++) s += f.m[a][j] * vals[j];
    code[a] = Quant(static_cast<int>(std::lround(s)), bits);
  }
  err = PlaneErr(vals, code[0], code[1], code[2], bits, weight);
  const int maxc = (1 << bits) - 1;
  for (int pass = 0; pass < (effort >= 2 ? 3 : effort); pass++) {
    bool better = false;
    for (int a = 0; a < 3; a++)
      for (int d = -1; d <= 1; d += 2) {
        int c[3] = {code[0], code[1], code[2]};
        c[a] += d;
        if (c[a] < 0 || c[a] > maxc) continue;
        const i64 e = PlaneErr(vals, c[0], c[1], c[2], bits, weight);
        if (e < err) {
          err = e;
          std::memcpy(code, c, sizeof(c));
          better = true;
        }
      }
    if (!better) break;
  }
}

void Planar(const Block &blk, int effort, Candidate &out) {
  int r[3], g[3], b[3];
  i64 er, eg, eb;
  FitChannel(blk.r, 6, kWr, effort, r, er);
  FitChannel(blk.g, 7, kWg, effort, g, eg);
  FitChannel(blk.b, 6, kWb, effort, b, eb);
  const i64 e = er + eg + eb;
  if (e >= out.err) return;
  const int ro = r[0], go = g[0], bo = b[0], rh = r[1], gh = g[1], bh = b[1], rv = r[2], gv = g[2], bv = b[2];
  u64 w = 0;
  w |= static_cast<u64>(ro) << 57;
  w |= static_cast<u64>((go >> 6) & 1) << 56 | static_cast<u64>(go & 63) << 49;
  w |= static_cast<u64>((bo >> 5) & 1) << 48 | static_cast<u64>((bo >> 3) & 3) << 43 | static_cast<u64>(bo & 7) << 39;
  w |= static_cast<u64>((rh >> 1) & 31) << 34 | static_cast<u64>(rh & 1) << 32;
  w |= static_cast<u64>(gh) << 25 | static_cast<u64>(bh) << 19;
  w |= static_cast<u64>(rv) << 13 | static_cast<u64>(gv) << 6 | static_cast<u64>(bv);
  w |= 1ull << 33;
  // red and green must not overflow (else T or H mode), blue must
  const int dr = Sext3(static_cast<int>((w >> 56) & 7));
  if (dr < 0) w |= 1ull << 63;
  const int dg = Sext3(static_cast<int>((w >> 48) & 7));
  if (dg < 0) w |= 1ull << 55;
  const int s1 = (bo >> 3) & 3, s2 = (bo >> 1) & 3;
  if (s1 + s2 >= 4)
    w |= 7ull << 45;  // B = 28 + s1, dB = s2 >= 0: past 31
  else
    w |= 1ull << 42;  // B = s1, dB = s2 - 4: below 0
  out.err = e;
  out.word = w;
}

void PutWord(u64 w, std::uint8_t *dst) {
  for (int i = 0; i < 8; i++) dst[i] = static_cast<std::uint8_t>(w >> (56 - 8 * i));
}

u64 GetWord(const std::uint8_t *src) {
  u64 w = 0;
  for (int i = 0; i < 8; i++) w = w << 8 | src[i];
  return w;
}

u64 EncodeColour(const Block &blk, Kind kind, int effort) {
  Candidate best;
  if (kind == kRgbA1) {
    bool anyTransparent = false, allTransparent = true;
    for (int j = 0; j < 16; j++) {
      if (blk.a[j] < kAlphaThreshold)
        anyTransparent = true;
      else
        allTransparent = false;
    }
    if (allTransparent) {
      // differential, opaque bit 0, every pixel index 2 (msb 1, lsb 0)
      return 0xFFFF0000ull;
    }
    if (anyTransparent) {
      Differential(blk, effort, true, 0, best);
      return best.word;
    }
    Differential(blk, effort, false, 1, best);
    Planar(blk, effort, best);
    return best.word;
  }
  Individual(blk, effort, best);
  if (best.err > 0) Differential(blk, effort, false, 1, best);
  if (best.err > 0) Planar(blk, effort, best);
  return best.word;
}

u64 EncodeAlpha(const Block &blk, int effort) {
  int amin = 255, amax = 0;
  for (int j = 0; j < 16; j++) {
    amin = std::min(amin, blk.a[j]);
    amax = std::max(amax, blk.a[j]);
  }
  int bestBase = amin, bestMul = 1, bestTable = 13;
  std::uint8_t bestIdx[16];
  std::fill(bestIdx, bestIdx + 16, 4);  // table 13, index 4: modifier 0
  if (amin != amax) {
    i64 bestErr = INT64_MAX;
    for (int t = 0; t < 16; t++) {
      const int tmin = kAlpha[t][3], tmax = kAlpha[t][7];
      const double ideal = static_cast<double>(amax - amin) / (tmax - tmin);
      const int m0 = static_cast<int>(std::floor(ideal));
      const int spread = effort >= 2 ? 2 : 1;
      for (int mul = m0 - (spread - 1); mul <= m0 + spread; mul++) {
        if (mul < 1 || mul > 15) continue;
        const int b0 = static_cast<int>(std::lround((amin + amax) / 2.0 - mul * (tmin + tmax) / 2.0));
        for (int base = b0 - spread; base <= b0 + spread; base++) {
          if (base < 0 || base > 255) continue;
          int values[8];
          for (int i = 0; i < 8; i++) values[i] = Clamp255(base + kAlpha[t][i] * mul);
          i64 e = 0;
          std::uint8_t idx[16];
          for (int j = 0; j < 16 && e < bestErr; j++) {
            int best = 0, be = 1 << 30;
            for (int i = 0; i < 8; i++) {
              const int d = blk.a[j] - values[i];
              if (d * d < be) {
                be = d * d;
                best = i;
              }
            }
            idx[j] = static_cast<std::uint8_t>(best);
            e += be;
          }
          if (e < bestErr) {
            bestErr = e;
            bestBase = base;
            bestMul = mul;
            bestTable = t;
            std::memcpy(bestIdx, idx, 16);
          }
        }
      }
    }
  }
  u64 w = static_cast<u64>(bestBase) << 56 | static_cast<u64>(bestMul) << 52 | static_cast<u64>(bestTable) << 48;
  for (int j = 0; j < 16; j++) w |= static_cast<u64>(bestIdx[j]) << (45 - 3 * j);
  return w;
}

// ---- decoder ----

void Put(std::uint8_t out[64], int j, int r, int g, int b, int a) {
  const int x = j >> 2, y = j & 3;
  std::uint8_t *p = out + (y * 4 + x) * 4;
  p[0] = static_cast<std::uint8_t>(r);
  p[1] = static_cast<std::uint8_t>(g);
  p[2] = static_cast<std::uint8_t>(b);
  p[3] = static_cast<std::uint8_t>(a);
}

inline int PixelIndex(u64 w, int j) { return static_cast<int>(((w >> (16 + j)) & 1) << 1 | ((w >> j) & 1)); }

void DecodeColour(u64 w, bool punchthrough, std::uint8_t out[64]) {
  const int bit33 = static_cast<int>((w >> 33) & 1);
  const bool differential = punchthrough || bit33;
  const bool nonOpaque = punchthrough && !bit33;
  const int flip = static_cast<int>((w >> 32) & 1);
  if (!differential) {
    const int r1 = Expand((w >> 60) & 15, 4), r2 = Expand((w >> 56) & 15, 4);
    const int g1 = Expand((w >> 52) & 15, 4), g2 = Expand((w >> 48) & 15, 4);
    const int b1 = Expand((w >> 44) & 15, 4), b2 = Expand((w >> 40) & 15, 4);
    const int t1 = (w >> 37) & 7, t2 = (w >> 34) & 7;
    for (int j = 0; j < 16; j++) {
      const bool second = InSub(j, flip, 1);
      const int m = Modifier(second ? t2 : t1, PixelIndex(w, j));
      Put(out, j, Clamp255((second ? r2 : r1) + m), Clamp255((second ? g2 : g1) + m), Clamp255((second ? b2 : b1) + m),
          255);
    }
    return;
  }
  const int r = (w >> 59) & 31, g = (w >> 51) & 31, b = (w >> 43) & 31;
  const int r2 = r + Sext3((w >> 56) & 7), g2 = g + Sext3((w >> 48) & 7), b2 = b + Sext3((w >> 40) & 7);
  if (r2 < 0 || r2 > 31 || g2 < 0 || g2 > 31) {
    // T (red overflows) or H (green overflows) mode: four paint colours
    int c1[3], c2[3], d;
    const bool tMode = r2 < 0 || r2 > 31;
    if (tMode) {
      c1[0] = Expand(static_cast<int>(((w >> 59) & 3) << 2 | ((w >> 56) & 3)), 4);
      c1[1] = Expand((w >> 52) & 15, 4);
      c1[2] = Expand((w >> 48) & 15, 4);
      c2[0] = Expand((w >> 44) & 15, 4);
      c2[1] = Expand((w >> 40) & 15, 4);
      c2[2] = Expand((w >> 36) & 15, 4);
      d = kDistance[((w >> 34) & 3) << 1 | ((w >> 32) & 1)];
    } else {
      c1[0] = Expand((w >> 59) & 15, 4);
      c1[1] = Expand(static_cast<int>(((w >> 56) & 7) << 1 | ((w >> 52) & 1)), 4);
      c1[2] = Expand(static_cast<int>(((w >> 51) & 1) << 3 | ((w >> 47) & 7)), 4);
      c2[0] = Expand((w >> 43) & 15, 4);
      c2[1] = Expand((w >> 39) & 15, 4);
      c2[2] = Expand((w >> 35) & 15, 4);
      const int v1 = c1[0] << 16 | c1[1] << 8 | c1[2], v2 = c2[0] << 16 | c2[1] << 8 | c2[2];
      d = kDistance[((w >> 34) & 1) << 2 | ((w >> 32) & 1) << 1 | (v1 >= v2 ? 1 : 0)];
    }
    int paint[4][3];
    for (int c = 0; c < 3; c++) {
      if (tMode) {
        paint[0][c] = c1[c];
        paint[1][c] = Clamp255(c2[c] + d);
        paint[2][c] = c2[c];
        paint[3][c] = Clamp255(c2[c] - d);
      } else {
        paint[0][c] = Clamp255(c1[c] + d);
        paint[1][c] = Clamp255(c1[c] - d);
        paint[2][c] = Clamp255(c2[c] + d);
        paint[3][c] = Clamp255(c2[c] - d);
      }
    }
    for (int j = 0; j < 16; j++) {
      const int i = PixelIndex(w, j);
      if (nonOpaque && i == 2)
        Put(out, j, 0, 0, 0, 0);
      else
        Put(out, j, paint[i][0], paint[i][1], paint[i][2], 255);
    }
    return;
  }
  if (b2 < 0 || b2 > 31) {
    // planar
    const int ro = Expand((w >> 57) & 63, 6);
    const int go = Expand(static_cast<int>(((w >> 56) & 1) << 6 | ((w >> 49) & 63)), 7);
    const int bo = Expand(static_cast<int>(((w >> 48) & 1) << 5 | ((w >> 43) & 3) << 3 | ((w >> 39) & 7)), 6);
    const int rh = Expand(static_cast<int>(((w >> 34) & 31) << 1 | ((w >> 32) & 1)), 6);
    const int gh = Expand((w >> 25) & 127, 7), bh = Expand((w >> 19) & 63, 6);
    const int rv = Expand((w >> 13) & 63, 6), gv = Expand((w >> 6) & 127, 7), bv = Expand(w & 63, 6);
    for (int x = 0; x < 4; x++)
      for (int y = 0; y < 4; y++)
        Put(out, x * 4 + y, PlanarValue(ro, rh, rv, x, y), PlanarValue(go, gh, gv, x, y), PlanarValue(bo, bh, bv, x, y),
            255);
    return;
  }
  const int er1 = Expand(r, 5), eg1 = Expand(g, 5), eb1 = Expand(b, 5);
  const int er2 = Expand(r2, 5), eg2 = Expand(g2, 5), eb2 = Expand(b2, 5);
  const int t1 = (w >> 37) & 7, t2 = (w >> 34) & 7;
  for (int j = 0; j < 16; j++) {
    const bool second = InSub(j, flip, 1);
    const int i = PixelIndex(w, j);
    if (nonOpaque && i == 2) {
      Put(out, j, 0, 0, 0, 0);
      continue;
    }
    const int m = nonOpaque && i == 0 ? 0 : Modifier(second ? t2 : t1, i);
    Put(out, j, Clamp255((second ? er2 : er1) + m), Clamp255((second ? eg2 : eg1) + m),
        Clamp255((second ? eb2 : eb1) + m), 255);
  }
}

void DecodeAlpha(u64 w, std::uint8_t out[64]) {
  const int base = static_cast<int>(w >> 56) & 255, mul = static_cast<int>(w >> 52) & 15,
            table = static_cast<int>(w >> 48) & 15;
  for (int j = 0; j < 16; j++) {
    const int i = static_cast<int>(w >> (45 - 3 * j)) & 7;
    const int x = j >> 2, y = j & 3;
    out[(y * 4 + x) * 4 + 3] = static_cast<std::uint8_t>(Clamp255(base + kAlpha[table][i] * mul));
  }
}

}  // namespace

void DecodeBlock(Kind kind, const std::uint8_t *block, std::uint8_t out[64]) {
  if (kind == kRgbaEac) {
    DecodeColour(GetWord(block + 8), false, out);
    DecodeAlpha(GetWord(block), out);
  } else {
    DecodeColour(GetWord(block), kind == kRgbA1, out);
  }
}

void EncodeLevel(Kind kind, const std::uint8_t *rgba, unsigned w, unsigned h, int effort, std::vector<std::uint8_t> &out) {
  const unsigned bw = (w + 3) / 4, bh = (h + 3) / 4;
  const std::size_t bytes = BlockBytes(kind);
  const std::size_t start = out.size();
  out.resize(start + static_cast<std::size_t>(bw) * bh * bytes);
  std::uint8_t *dst = out.data() + start;
  Block blk;
  for (unsigned by = 0; by < bh; by++)
    for (unsigned bx = 0; bx < bw; bx++) {
      Gather(rgba, w, h, bx, by, blk);
      if (kind == kRgbaEac) {
        PutWord(EncodeAlpha(blk, effort), dst);
        PutWord(EncodeColour(blk, kRgb, effort), dst + 8);
      } else {
        if (kind == kRgb)
          for (int j = 0; j < 16; j++) blk.a[j] = 255;
        PutWord(EncodeColour(blk, kind, effort), dst);
      }
      dst += bytes;
    }
}

void DecodeLevel(Kind kind, const std::uint8_t *data, unsigned w, unsigned h, std::uint8_t *rgba) {
  const unsigned bw = (w + 3) / 4, bh = (h + 3) / 4;
  const std::size_t bytes = BlockBytes(kind);
  std::uint8_t px[64];
  for (unsigned by = 0; by < bh; by++)
    for (unsigned bx = 0; bx < bw; bx++) {
      DecodeBlock(kind, data, px);
      data += bytes;
      for (unsigned y = 0; y < 4; y++) {
        const unsigned sy = by * 4 + y;
        if (sy >= h) break;
        for (unsigned x = 0; x < 4; x++) {
          const unsigned sx = bx * 4 + x;
          if (sx >= w) break;
          std::memcpy(rgba + (static_cast<std::size_t>(sy) * w + sx) * 4, px + (y * 4 + x) * 4, 4);
        }
      }
    }
}

}  // namespace etc2
