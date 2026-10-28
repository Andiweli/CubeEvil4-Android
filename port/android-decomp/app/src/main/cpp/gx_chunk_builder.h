// Chunks as the core's decoder writes them (re4_gx_frame.h), built by hand: the GX renderer's
// check program (tools/test_gx_gl.cpp) and the renderer's check on the device (gx_selfcheck.cpp).
#pragma once

#include "re4_gx_frame.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace re4gx {
namespace chunk {

// A chunk as the core's decoder writes it.
struct Builder {
  std::vector<std::uint32_t> ops;
  std::vector<std::uint8_t> vertices;
  std::vector<std::uint16_t> indices;
  std::vector<std::uint8_t> blob;
  std::uint32_t bp[256] = {};
  std::uint32_t flags = 0;
  Re4GxChunk view{};

  void Op(std::uint32_t op, const std::uint32_t *p, std::uint32_t n) {
    ops.push_back(op | n << 8);
    ops.insert(ops.end(), p, p + n);
  }
  void Bp(unsigned reg, std::uint32_t value) {
    bp[reg] = value & 0xFFFFFF;
    const std::uint32_t w = reg << 24 | (value & 0xFFFFFF);
    Op(RE4_GX_OP_BP, &w, 1);
  }
  void Xf(std::uint32_t address, std::vector<std::uint32_t> words) {
    words.insert(words.begin(), address);
    Op(RE4_GX_OP_XF, words.data(), static_cast<std::uint32_t>(words.size()));
  }
  void XfF(std::uint32_t address, std::vector<float> values) {
    std::vector<std::uint32_t> w(values.size());
    std::memcpy(w.data(), values.data(), values.size() * 4);
    Xf(address, w);
  }
  std::uint32_t Blob(const void *data, std::size_t n) {
    std::uint32_t at = static_cast<std::uint32_t>(blob.size());
    blob.insert(blob.end(), static_cast<const std::uint8_t *>(data), static_cast<const std::uint8_t *>(data) + n);
    while (blob.size() & 31)
      blob.push_back(0);
    return at;
  }
  // the owner of the draws that follow (RE4_GX_OP_OWNER, patch 0137)
  void Owner(std::uint32_t object, std::uint32_t function, std::uint32_t table, std::uint32_t life = ~0u) {
    const std::uint32_t w[4] = {object, function, table, life};
    Op(RE4_GX_OP_OWNER, w, 4);
  }
  // vertices: floats/bytes as the layout says; one triangle list or quads (`tex0Row`: the XF row of
  // texture coordinate 0's matrix, 60 the identity)
  void Draw(std::uint32_t layout, std::uint32_t stride, const std::vector<std::uint8_t> &v, unsigned count,
            unsigned gxPrimitive = 0x90, std::uint32_t primitive = RE4_GX_TRIANGLES, std::uint32_t tex0Row = 60) {
    Re4GxDraw d{};
    d.primitive = primitive | gxPrimitive << 8;
    d.layout = layout;
    d.stride = stride;
    d.vertexOffset = static_cast<std::uint32_t>(vertices.size());
    d.vertexCount = count;
    d.indexOffset = static_cast<std::uint32_t>(indices.size());
    vertices.insert(vertices.end(), v.begin(), v.end());
    if (gxPrimitive == 0x80) {
      for (unsigned q = 0; q + 3 < count; q += 4) {
        const std::uint16_t t[6] = {static_cast<std::uint16_t>(q), static_cast<std::uint16_t>(q + 1),
                                    static_cast<std::uint16_t>(q + 2), static_cast<std::uint16_t>(q),
                                    static_cast<std::uint16_t>(q + 2), static_cast<std::uint16_t>(q + 3)};
        indices.insert(indices.end(), t, t + 6);
      }
    } else {
      for (unsigned i = 0; i < count; i++)
        indices.push_back(static_cast<std::uint16_t>(i));
    }
    d.indexCount = static_cast<std::uint32_t>(indices.size()) - d.indexOffset;
    d.matrices[0] = 0 | tex0Row << 8 | 60u << 16 | 60u << 24; // PNMTX0, identity texture matrices
    d.matrices[1] = 60u | 60u << 8 | 60u << 16 | 60u << 24;
    d.matrices[2] = 60u;
    Op(RE4_GX_OP_DRAW, reinterpret_cast<const std::uint32_t *>(&d), sizeof(d) / 4);
  }
  void Texture(unsigned map, std::uint64_t key, std::uint32_t flags, std::uint32_t address, unsigned w, unsigned h,
               unsigned format, const std::vector<std::uint8_t> &data, std::uint32_t tlut = 0) {
    Re4GxTexture t{};
    t.map = map;
    t.keyLo = static_cast<std::uint32_t>(key);
    t.keyHi = static_cast<std::uint32_t>(key >> 32);
    t.flags = flags;
    t.address = address;
    t.width = w;
    t.height = h;
    t.format = format;
    t.levels = 1;
    t.tlut = tlut;
    if (!data.empty()) {
      t.dataOffset = Blob(data.data(), data.size());
      t.dataBytes = static_cast<std::uint32_t>(data.size());
    }
    Op(RE4_GX_OP_TEXTURE, reinterpret_cast<const std::uint32_t *>(&t), sizeof(t) / 4);
  }
  const Re4GxChunk &Chunk() {
    view = Re4GxChunk{};
    view.version = RE4_GX_FRAME_VERSION;
    view.flags = flags;
    view.ops = ops.data();
    view.opWords = static_cast<std::uint32_t>(ops.size());
    view.vertices = vertices.data();
    view.vertexBytes = static_cast<std::uint32_t>(vertices.size());
    view.indices = indices.data();
    view.indexCount = static_cast<std::uint32_t>(indices.size());
    view.blob = blob.data();
    view.blobBytes = static_cast<std::uint32_t>(blob.size());
    return view;
  }
};

inline std::uint32_t Bits(float f) {
  std::uint32_t v;
  std::memcpy(&v, &f, 4);
  return v;
}

// The state GXInit leaves (the parts these tests need) plus a 2D setup:
// orthographic projection over the EFB, identity position matrix, one colour
// channel from the vertex colour, one TEV stage passing the rasterized colour.
inline void Baseline(Builder &b) {
  std::vector<std::uint32_t> state(256, 0);
  b.flags = RE4_GX_CHUNK_RESET;
  // genmode: 1 colour channel, 1 TEV stage, no texture coordinates
  state[0x00] = 1u << 4;
  // TEV stage 0: colour = rasterized colour, alpha = rasterized alpha, clamped
  state[0xC0] = 10u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19;
  state[0xC1] = 5u << 4 | 7u << 7 | 7u << 10 | 7u << 13 | 1u << 19;
  state[0x28] = 0x3F7u << 12 | 0u; // stage 0: map 0, coord 0, no texture, colour channel 0
  // swap table 0 identity (r g b a), the others like GXInit
  state[0xF6] = 0 | 1 << 2;
  state[0xF7] = 2 | 3 << 2;
  for (int t = 1; t < 4; t++) {
    state[0xF6 + 2 * t] = 0 | 1 << 2;
    state[0xF7 + 2 * t] = 2 | 3 << 2;
  }
  state[0xF3] = 7u << 16 | 7u << 19; // alpha test always
  state[0x40] = 1 | 3 << 1 | 1 << 4; // depth on, LEQUAL, update
  state[0x41] = 1 << 3 | 1 << 4;     // colour and alpha update
  state[0x43] = 1;                   // RGBA6_Z24 (alpha kept)
  state[0x20] = 342 | 342u << 12;    // scissor 0,0 .. 639,527
  state[0x21] = (342 + 527) | (342u + 639) << 12;
  state[0x59] = 171 | 171u << 10;
  for (int i = 0; i < 8; i++) {
    state[0x30 + 2 * i] = 63; // rasterizer size 64 x 64
    state[0x31 + 2 * i] = 63;
  }
  state[0x49] = 0;
  state[0x4A] = 639 | 527u << 10; // copy rectangle: the whole EFB
  for (int i = 0; i < 256; i++)
    b.bp[i] = state[i];
  b.Op(RE4_GX_OP_BP_STATE, state.data(), 256);
  // XF: identity PNMTX0 (rows 0-2) and identity texture matrix 60 (rows 60-62)
  b.XfF(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  b.XfF(60 * 4, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  b.XfF(0x400, {1, 0, 0, 0, 1, 0, 0, 0, 1}); // normal matrix 0
  b.XfF(0x500 + 61 * 4, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  b.Xf(0x1009, {1});                // one colour channel
  b.Xf(0x100E, {1});                // colour 0 from the vertex, no lighting
  b.Xf(0x1010, {1});                // alpha 0 likewise
  b.Xf(0x103F, {0});                // no texture coordinates
  // viewport over the EFB, depth 0..1
  b.XfF(0x101A, {320.0f, -264.0f, 16777215.0f, 342.0f + 320.0f, 342.0f + 264.0f, 16777215.0f});
  // orthographic projection: x 0..640, y 0..528 (top down), z 0 .. -1 as depth 0..1
  b.XfF(0x1020, {1.0f / 320.0f, -1.0f, -2.0f / 528.0f, 1.0f, -1.0f, -1.0f});
  b.Xf(0x1026, {1});
}

// a vertex: position, colour
inline void V(std::vector<std::uint8_t> &v, float x, float y, float z, const std::uint8_t c[4]) {
  const float p[3] = {x, y, z};
  const auto *pb = reinterpret_cast<const std::uint8_t *>(p);
  v.insert(v.end(), pb, pb + 12);
  v.insert(v.end(), c, c + 4);
}

// a vertex: position, colour, s, t
inline void VT(std::vector<std::uint8_t> &v, float x, float y, float z, const std::uint8_t c[4], float s, float t) {
  V(v, x, y, z, c);
  const float st[2] = {s, t};
  const auto *pb = reinterpret_cast<const std::uint8_t *>(st);
  v.insert(v.end(), pb, pb + 8);
}

inline void Quad(Builder &b, float x0, float y0, float x1, float y1, float z, const std::uint8_t c[4]) {
  std::vector<std::uint8_t> v;
  V(v, x0, y0, z, c);
  V(v, x1, y0, z, c);
  V(v, x1, y1, z, c);
  V(v, x0, y1, z, c);
  b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
}

inline void DisplayCopy(Builder &b, bool clear, const std::uint8_t color[4]) {
  b.Bp(0x4F, color[0] | color[3] << 8); // clear colour A R
  b.Bp(0x50, color[2] | color[1] << 8); // G B
  b.Bp(0x51, 0xFFFFFF);
  b.Bp(0x4B, 0x80460000u >> 5 & 0x1FFFFF);
  b.Bp(0x52, (clear ? 1u << 11 : 0) | 1u << 14);
}

} // namespace chunk
} // namespace re4gx
