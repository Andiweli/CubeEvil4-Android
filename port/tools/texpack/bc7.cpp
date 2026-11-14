// BC7 block decoder (see bc7.h). Checked against Pillow's and Mesa's decoders (tests/make_bc7_jpeg_fixtures.py,
// tests/etc2_gl_check.cpp).
#include "bc7.h"

#include <cstring>
#include <utility>

namespace img {
namespace {

struct Mode {
  int subsets, partitionBits, rotationBits, indexSelectionBits, colorBits, alphaBits, endpointPBits, sharedPBits,
      indexBits, indexBits2;
};

const Mode kModes[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 0}, {2, 6, 0, 0, 6, 0, 0, 1, 3, 0}, {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 0}, {1, 0, 2, 1, 5, 6, 0, 0, 2, 3}, {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 0}, {2, 6, 0, 0, 5, 5, 1, 0, 2, 0}};

// two subsets: bit i set = pixel i in subset 1
const std::uint16_t kPartition2[64] = {
    0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80, 0xE800, 0xFFE8,
    0xFF00, 0xFFF0, 0xF000, 0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE, 0x088C, 0x3110,
    0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C, 0xAAAA, 0xF0F0, 0x5A5A, 0x33CC, 0x3C3C, 0x55AA, 0x9696,
    0xA55A, 0x73CE, 0x13C8, 0x324C, 0x3BDC, 0x6996, 0xC33C, 0x9966, 0x0660, 0x0272, 0x04E4, 0x4E40, 0x2720,
    0xC936, 0x936C, 0x39C6, 0x639C, 0x9336, 0x9CC6, 0x817E, 0xE718, 0xCCF0, 0x0FCC, 0x7744, 0xEE22};

const std::uint8_t kPartition3[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2}, {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2},
    {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2}, {0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2}, {0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2}, {0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2}, {0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2}, {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0}, {0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0}, {0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1},
    {0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2}, {0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2},
    {0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1}, {0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2}, {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2}, {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0}, {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0}, {0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1}, {0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1}, {0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1}, {0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1}, {0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2}, {0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1},
    {0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2}, {0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2}, {0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2},
    {0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2},
    {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1}, {0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2}, {0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0}};

// the second subset's anchor (two subsets), the second and third subsets' anchors (three subsets)
const std::uint8_t kAnchor2[64] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
                                   15, 2,  8,  2,  2,  8,  8,  15, 2,  8,  2,  2,  8,  8,  2,  2,
                                   15, 15, 6,  8,  2,  8,  15, 15, 2,  8,  2,  2,  2,  15, 15, 6,
                                   6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2,  15};
const std::uint8_t kAnchor3a[64] = {3, 3,  15, 15, 8,  3,  15, 15, 8,  8,  6,  6,  6,  5,  3,  3,
                                    3, 3,  8,  15, 3,  3,  6,  10, 5,  8,  8,  6,  8,  5,  15, 15,
                                    8, 15, 3,  5,  6,  10, 8,  15, 15, 3,  15, 5,  15, 15, 15, 15,
                                    3, 15, 5,  5,  5,  8,  5,  10, 5,  10, 8,  13, 15, 12, 3,  3};
const std::uint8_t kAnchor3b[64] = {15, 8,  8,  3,  15, 15, 3,  8,  15, 15, 15, 15, 15, 15, 15, 8,
                                    15, 8,  15, 3,  15, 8,  15, 8,  3,  15, 6,  10, 15, 15, 10, 8,
                                    15, 3,  15, 10, 10, 8,  9,  10, 6,  15, 8,  15, 3,  6,  6,  8,
                                    15, 3,  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8};

const int kWeights2[4] = {0, 21, 43, 64};
const int kWeights3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
const int kWeights4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

const int *Weights(int bits) { return bits == 2 ? kWeights2 : bits == 3 ? kWeights3 : kWeights4; }

struct Bits {
  const std::uint8_t *p;
  unsigned pos = 0;
  unsigned Get(int n) {
    unsigned v = 0;
    for (int i = 0; i < n; i++, pos++)
      if (pos < 128) v |= static_cast<unsigned>((p[pos >> 3] >> (pos & 7)) & 1) << i;
    return v;
  }
};

inline int Unquantize(int v, int bits) {
  v <<= 8 - bits;
  return v | (v >> bits);
}

inline int Interpolate(int e0, int e1, int w) { return ((64 - w) * e0 + w * e1 + 32) >> 6; }

}  // namespace

void DecodeBc7Block(const std::uint8_t *block, std::uint8_t out[64]) {
  int mode = 0;
  while (mode < 8 && !((block[0] >> mode) & 1)) mode++;
  if (mode == 8) {  // no mode bit: a reserved block, transparent black
    std::memset(out, 0, 64);
    return;
  }
  const Mode &m = kModes[mode];
  Bits bits{block};
  bits.pos = static_cast<unsigned>(mode) + 1;
  const int partition = static_cast<int>(bits.Get(m.partitionBits));
  const int rotation = static_cast<int>(bits.Get(m.rotationBits));
  const int indexSelection = static_cast<int>(bits.Get(m.indexSelectionBits));
  const int endpoints = m.subsets * 2;
  int ep[6][4] = {};
  for (int c = 0; c < 3; c++)
    for (int e = 0; e < endpoints; e++) ep[e][c] = static_cast<int>(bits.Get(m.colorBits));
  if (m.alphaBits)
    for (int e = 0; e < endpoints; e++) ep[e][3] = static_cast<int>(bits.Get(m.alphaBits));
  int pbits[6] = {};
  if (m.endpointPBits)
    for (int e = 0; e < endpoints; e++) pbits[e] = static_cast<int>(bits.Get(1));
  if (m.sharedPBits)
    for (int s = 0; s < m.subsets; s++) pbits[2 * s] = pbits[2 * s + 1] = static_cast<int>(bits.Get(1));
  const bool hasP = m.endpointPBits || m.sharedPBits;
  for (int e = 0; e < endpoints; e++) {
    for (int c = 0; c < 3; c++) {
      int v = ep[e][c], n = m.colorBits;
      if (hasP) {
        v = v << 1 | pbits[e];
        n++;
      }
      ep[e][c] = Unquantize(v, n);
    }
    if (m.alphaBits) {
      int v = ep[e][3], n = m.alphaBits;
      if (hasP) {
        v = v << 1 | pbits[e];
        n++;
      }
      ep[e][3] = Unquantize(v, n);
    } else {
      ep[e][3] = 255;
    }
  }
  int subset[16];
  int anchor[3] = {0, 0, 0};
  for (int i = 0; i < 16; i++) {
    if (m.subsets == 1) subset[i] = 0;
    else if (m.subsets == 2) subset[i] = (kPartition2[partition] >> i) & 1;
    else subset[i] = kPartition3[partition][i];
  }
  if (m.subsets == 2) anchor[1] = kAnchor2[partition];
  if (m.subsets == 3) {
    anchor[1] = kAnchor3a[partition];
    anchor[2] = kAnchor3b[partition];
  }
  int index[16], index2[16];
  for (int i = 0; i < 16; i++) {
    const bool isAnchor = i == anchor[subset[i]];
    index[i] = static_cast<int>(bits.Get(m.indexBits - (isAnchor ? 1 : 0)));
  }
  if (m.indexBits2)
    for (int i = 0; i < 16; i++) index2[i] = static_cast<int>(bits.Get(m.indexBits2 - (i == 0 ? 1 : 0)));
  for (int i = 0; i < 16; i++) {
    const int s = subset[i];
    const int *e0 = ep[2 * s], *e1 = ep[2 * s + 1];
    int colourIndex = index[i], colourBits = m.indexBits, alphaIndex = index[i], alphaBits = m.indexBits;
    if (m.indexBits2) {
      alphaIndex = index2[i];
      alphaBits = m.indexBits2;
      if (indexSelection) {
        std::swap(colourIndex, alphaIndex);
        std::swap(colourBits, alphaBits);
      }
    }
    const int *wc = Weights(colourBits), *wa = Weights(alphaBits);
    int px[4];
    for (int c = 0; c < 3; c++) px[c] = Interpolate(e0[c], e1[c], wc[colourIndex]);
    px[3] = m.alphaBits ? Interpolate(e0[3], e1[3], wa[alphaIndex]) : 255;
    if (rotation) std::swap(px[3], px[rotation - 1]);
    for (int c = 0; c < 4; c++) out[i * 4 + c] = static_cast<std::uint8_t>(px[c]);
  }
}

}  // namespace img
