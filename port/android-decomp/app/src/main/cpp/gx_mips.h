// Mipmaps for the game's textures that come without (1.1.0, the project owner, 21.11.2026; research
// reports/textures-research: 3,076 room textures of 128 texels or more have no smaller levels and shimmer in the
// distance, on the GameCube too). GxCore::Upload makes the chain from level 0 at load, on the CPU, the same way for
// OpenGL ES and Vulkan: each level the 2x2 average of the one above, as Capcom's tool made the levels the game has
// (the research's test: 1,566 of 1,567 levels are box averages, in the stored values, not in linear light), so a
// texture with made levels looks like one with the game's own. Cut-outs (alpha only 0 or 255: foliage, fences, hair)
// average their colour by alpha (no dark fringe from the hidden texels) and keep their coverage: each level's alpha is
// scaled until as many texels pass 128 as in level 0 (trees do not thin out in the distance). Header only: the
// renderer and its tests (tools/test_gx_gl.cpp) use it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace re4gx {

// The levels a w x h texture has down to 1 x 1.
inline unsigned GxMipCount(unsigned w, unsigned h) {
  unsigned n = 1;
  while (w > 1 || h > 1) {
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
    n++;
  }
  return n;
}

// A texture the renderer makes levels for: the game's own has none, and it is big enough to be drawn smaller than it
// is (64 texels or more on its longer side, 8 or more on the shorter: a ramp or a lookup strip stays as it is).
inline bool GxWantsMips(unsigned gameLevels, unsigned w, unsigned h) {
  return gameLevels <= 1 && std::max(w, h) >= 64 && std::min(w, h) >= 8;
}

// A cut-out: every alpha 0 or 255, both there.
inline bool GxCutout(const std::uint8_t *rgba, unsigned w, unsigned h) {
  bool clear = false, solid = false;
  for (std::size_t i = 0, n = static_cast<std::size_t>(w) * h; i < n; i++) {
    const std::uint8_t a = rgba[i * 4 + 3];
    if (a == 0)
      clear = true;
    else if (a == 255)
      solid = true;
    else
      return false;
  }
  return clear && solid;
}

// The share of texels whose alpha is 128 or more (the alpha test's usual edge).
inline float GxCoverage(const std::uint8_t *rgba, unsigned w, unsigned h) {
  const std::size_t n = static_cast<std::size_t>(w) * h;
  std::size_t in = 0;
  for (std::size_t i = 0; i < n; i++)
    in += rgba[i * 4 + 3] >= 128;
  return n ? static_cast<float>(in) / static_cast<float>(n) : 0.0f;
}

// The next level of `src` (w x h, RGBA8) into `dst` (max(1, w/2) x max(1, h/2)): each texel the average of the 2x2
// above it (an odd last row or column takes its own texel twice); `cutout`: the colour weighted by alpha.
inline void GxMipDown(const std::uint8_t *src, unsigned w, unsigned h, bool cutout, std::vector<std::uint8_t> &dst) {
  const unsigned dw = std::max(1u, w / 2), dh = std::max(1u, h / 2);
  dst.resize(static_cast<std::size_t>(dw) * dh * 4);
  for (unsigned y = 0; y < dh; y++) {
    const unsigned y0 = std::min(h - 1, y * 2), y1 = std::min(h - 1, y * 2 + 1);
    for (unsigned x = 0; x < dw; x++) {
      const unsigned x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + 1);
      const std::uint8_t *p[4] = {src + (static_cast<std::size_t>(y0) * w + x0) * 4,
                                  src + (static_cast<std::size_t>(y0) * w + x1) * 4,
                                  src + (static_cast<std::size_t>(y1) * w + x0) * 4,
                                  src + (static_cast<std::size_t>(y1) * w + x1) * 4};
      std::uint8_t *o = dst.data() + (static_cast<std::size_t>(y) * dw + x) * 4;
      const unsigned a = p[0][3] + p[1][3] + p[2][3] + p[3][3];
      if (cutout && a > 0) {
        for (int c = 0; c < 3; c++) {
          const unsigned sum = p[0][c] * p[0][3] + p[1][c] * p[1][3] + p[2][c] * p[2][3] + p[3][c] * p[3][3];
          o[c] = static_cast<std::uint8_t>((sum + a / 2) / a);
        }
      } else {
        for (int c = 0; c < 3; c++)
          o[c] = static_cast<std::uint8_t>((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
      }
      o[3] = static_cast<std::uint8_t>((a + 2) / 4);
    }
  }
}

// A cut-out's level: its alpha scaled until the share of texels at 128 or more is `coverage` (level 0's), as close as
// the level's texels allow (a binary search over the scale, 0.5 .. 4).
inline void GxKeepCoverage(std::uint8_t *rgba, unsigned w, unsigned h, float coverage) {
  const std::size_t n = static_cast<std::size_t>(w) * h;
  if (n == 0)
    return;
  const auto share = [&](float scale) {
    std::size_t in = 0;
    for (std::size_t i = 0; i < n; i++)
      in += std::min(255.0f, rgba[i * 4 + 3] * scale) >= 128.0f;
    return static_cast<float>(in) / static_cast<float>(n);
  };
  float lo = 0.5f, hi = 4.0f;
  for (int step = 0; step < 12; step++) {
    const float mid = (lo + hi) * 0.5f;
    if (share(mid) < coverage)
      lo = mid;
    else
      hi = mid;
  }
  const float scale = std::abs(share(lo) - coverage) <= std::abs(share(hi) - coverage) ? lo : hi;
  for (std::size_t i = 0; i < n; i++)
    rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::min(255.0f, rgba[i * 4 + 3] * scale + 0.5f));
}

}  // namespace re4gx
