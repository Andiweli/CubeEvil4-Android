// Shader generation of the GX renderer (gx_gl.cpp): GLSL ES 3.00 from the GX
// state that shapes the code. Constants (matrices, lights, colours, fog
// parameters, alpha references) live in two uniform blocks the renderer fills.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace re4gx {

// Everything that changes the generated code. Unused fields stay zero so
// equal states compare equal byte for byte.
struct ShaderKey {
  // vertex
  std::uint32_t layout;       // Re4GxLayout bits of the draw
  std::uint32_t numChans;     // XF 0x1009
  std::uint32_t numTexGens;   // XF 0x103F
  std::uint32_t chan[4];      // XF 0x100E-0x1011: colour 0/1, alpha 0/1 control
  std::uint32_t texGen[8];    // XF 0x1040+
  std::uint32_t postGen[8];   // XF 0x1050+ (with dual texture transform)
  std::uint32_t dualTex;      // XF 0x1012 bit 0
  std::uint32_t orthographic; // XF 0x1026
  // fragment
  std::uint32_t numStages;    // BP genmode bits 10-13, + 1
  std::uint32_t numIndStages; // BP genmode bits 16-18
  std::uint32_t colorEnv[16]; // BP 0xC0 + 2s
  std::uint32_t alphaEnv[16]; // BP 0xC1 + 2s
  std::uint32_t order[16];    // per stage: map | coord << 3 | enable << 6 | channel << 7
  std::uint32_t konst[16];    // per stage: kcsel | kasel << 8
  std::uint32_t swap[4];      // swap tables: r | g << 2 | b << 4 | a << 6
  std::uint32_t indCmd[16];   // BP 0x10 + s
  std::uint32_t indRef;       // BP 0x27 (with indirect stages)
  std::uint32_t alphaTest;    // BP 0xF3 bits 16-23: comp0, comp1, logic
  std::uint32_t fog;          // BP 0xF1 bits 20-23: projection, function
  std::uint32_t texMaps;      // maps the stages sample (bit per map)
  // both
  std::uint32_t centroid;     // 1: the varyings at a covered sample (MSAA, GxOptions::centroid)
};

std::string GxVertexShader(const ShaderKey &key);
std::string GxFragmentShader(const ShaderKey &key);
// The vertex shader of a merged draw call (RE4_GX_MERGE, gx_gl.cpp): draws that differ only in
// the XFM part, the material colours and uMtx in one call. Each of kMergeSlots slots holds a
// draw's (MergeBlock, binding kMergeBinding); a vertex finds its slot by gl_VertexID (the slots'
// first vertices ascend).
std::string GxMergedVertexShader(const ShaderKey &key);

// How the fragment shaders write the TEV's lerp ((a << 8) + (b - a) * c, rounded, divided by 256)
// and the fog's mix, for every program generated from then on (any thread; the GPU check of the
// movie colours, gx_selfcheck.cpp, picks the first one the GPU computes right):
//  - kTevMathShift: as the hardware does it, a shifted by 8;
//  - kTevMathNoShift: a + ((b - a) * c, rounded) >> 8, the same for every input (the division
//    by 256 rounds down, a * 256 is a multiple of it); the fog likewise. The Galaxy A36 (Adreno
//    710, driver 0800.58) computed a << 8 in 16 bits: a above 127 gave 0, the movies green
//    (report 22.10.2026 19:30, the check's six wrong pixels exactly so);
//  - kTevMathFloat: the lerp in floats (exact: whole numbers below 2^24, a division by 256);
//  - kTevMathBroken16: tests only, a << 8 wrapped to 16 bits like that A36.
enum TevMath { kTevMathShift = 0, kTevMathNoShift = 1, kTevMathFloat = 2, kTevMathBroken16 = 3 };
void SetTevMath(int mode);
int GetTevMath();
const char *TevMathName(int mode);

// Uniform blocks (std140, vec4/ivec4 only), shared by all programs. The XF block goes to the GPU
// in three parts (kXfParts), each at a binding of its own, and a draw uploads only the parts that
// changed: XFM the position, texture and normal matrices (each model), XFP the post-transform
// matrices (rarely), XFL lights, colours, viewport and projection. (The 3.3 KB of the whole block
// per change were 2.4 MB per picture in r10b, R3 series of the Retroid 22.10.2026; in parts 1.3 MB.)
constexpr int kXfBinding = 0;
constexpr int kPixelBinding = 1;
constexpr int kXfParts = 3;
constexpr int kXfPartBinding[kXfParts] = {kXfBinding, 2, 3};
constexpr const char *kXfPartName[kXfParts] = {"XFM", "XFP", "XFL"};
constexpr int kXfPartRow[kXfParts + 1] = {0, 96, 160, 208}; // first rows (vec4) of the parts in XfBlock

struct XfBlock {
  float posTex[64][4];  // XF 0x000-0x0FF: position and texture matrices by row
  float normal[32][4];  // XF 0x400-0x45F: normal matrices, 3 floats a row
  float post[64][4];    // XF 0x500-0x5FF: post-transform matrices by row
  float light[8][5][4]; // colour, cos attenuation, distance attenuation, position, direction
  float matAmb[4][4];   // material 0/1, ambient 0/1 (0..1)
  float viewport[2][4]; // sx sy sz ox | oy oz efbWidth efbHeight
  float proj[2][4];     // p0 p1 p2 p3 | p4 p5 type pointSize
};

// A merged draw call's XFM parts (GxMergedVertexShader): per slot uMtx and the slot's first vertex
// (gl_VertexID; ~0 for slots not used), then per slot posTex, normal and matAmb as in XfBlock (the
// game sets the material colours per object). 10 slots fit the 16 KB every GLES 3.0 driver allows
// for a uniform block (many Mali drivers no more).
constexpr int kMergeSlots = 10;
constexpr int kMergeBinding = 4;
constexpr int kMergeRows = 100;  // vec4 per slot: posTex 64, normal 32, matAmb 4
struct MergeBlock {
  std::uint32_t slot[kMergeSlots][4];
  float xf[kMergeSlots][kMergeRows][4];
};
static_assert(sizeof(MergeBlock) <= 16384, "a uniform block of GLES 3.0");

struct PixelBlock {
  std::int32_t colors[4][4];   // TEV registers prev, c0, c1, c2 (signed 11 bit)
  std::int32_t konst[4][4];    // konstant colours
  std::int32_t alphaRef[4];    // ref0, ref1
  float fog[2][4];             // A, C, b magnitude, b shift | colour r g b (0..255)
  float coordSize[8][4];       // per texture coordinate: rasterizer size s, t (texels, BP 0x30+)
  float mapSize[8][4];         // per texture map: width, height, LOD bias
  std::int32_t indScale[4][4]; // per indirect stage: s, t shift
  std::int32_t indMtx[6][4];   // indirect matrices, two rows each: m0 m1 m2 (s1.10), shift
};

} // namespace re4gx
