// Shader generation of the OUYA's GX renderer (gx_gl2.cpp): GLSL ES 1.00 from the GX state that
// shapes the code, for GLES 2 devices like the OUYA's Tegra 3 (port/docs/ouya-machbarkeit.md,
// stage O2). It follows the main app's GLES 3 generator (port/android-decomp/.../gx_gl_shader.cpp)
// with what GLES 2 lacks worked around:
//   - no integers in the shaders: the TEV computes with floats holding the same whole numbers
//     (mediump on the Tegra 3 keeps them exactly up to 2^14: the lerp's rounding is approximate
//     there, exact where the device has highp, as Mesa does)
//   - no uniform blocks: plain uniform arrays, only those a shader reads (the Tegra 3 has 252
//     vertex vec4 in practice; the lights and post-transform rows a shader uses are packed)
//   - no integer attributes: matrix indices arrive as unsigned bytes, the compact vertex
//     (RE4_GX_V_COMPACT) as the GX types themselves (GL converts, the shader scales by the
//     fraction bits)
//   - texture coordinates read by one map only are scaled to it in the vertex shader (no
//     arithmetic in the fragment shader before the read); with indirect stages the fixed-point
//     path of the GLES 3 renderer in floats
#pragma once

#include <cstdint>
#include <string>

namespace re4gx2 {

// Everything that changes the generated code. Unused fields stay zero so equal states compare
// equal byte for byte.
struct ShaderKey {
  // vertex
  std::uint32_t layout;       // Re4GxLayout bits of the draw (with RE4_GX_V_COMPACT)
  std::uint32_t mtxSlots;     // matrix slots the vertex carries (bit 0 position, 1 + n texture n)
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
  std::uint32_t fastTev;      // 1: the TEV in plain fractions 0..1, not to the step (GxOptions::fastTev)
  // both
  std::uint32_t texelCoords;  // 1: every coordinate in 1/128 texels, as the GLES 3 renderer (tests)
};

// Vertex inputs (attribute sources), in the order the generator gives them locations.
enum AttrSource {
  kAttrPos = 0,
  kAttrNrm,
  kAttrBin,
  kAttrTan,
  kAttrClr0,
  kAttrClr1,
  kAttrTex0, // + n
  kAttrMtx0 = kAttrTex0 + 8, // matrix slots 0-3 as four unsigned bytes, then 4-7, then 8
  kAttrSources = kAttrMtx0 + 3
};

// What the renderer needs to know of a shader besides its code.
struct ShaderInfo {
  int attribs[kAttrSources]; // the sources the vertex shader reads, location = position here
  int attribCount;
  std::uint32_t lights;      // the lights it reads (bit per light), packed into uLight in that order
  int postRows[24];          // the post-transform rows it reads (XF 0x500 row numbers), packed into uPost
  int postCount;
  // per texture coordinate: the map whose size the vertex shader scales it to (the only map that
  // reads it, no indirect stages), or -1: in 1/128 texels (the fixed-point path), or -2: unused
  int coordMap[8];
  bool projective[8];        // the coordinate has a q (projected texgen or dual transform)
  bool normalMatrix;         // reads uNrm
  bool mtxDefaults;          // reads uMtxDef (slots the vertex does not carry)
  bool compact;              // reads uDequant
  bool fixedPoint;           // indirect stages: coordinates in 1/128 texels, reads uMapScale
  int vertexUniforms;        // vec4 the vertex shader declares
  int varyingComponents;     // floats it passes on
};

ShaderInfo GxShaderInfo(const ShaderKey &key);
// `lean`: without the GameCube's special case for texture coordinates with q = 0 (rare), for
// devices whose vertex shader would be too long otherwise (the Tegra 3 takes 256 instructions)
std::string GxVertexShader(const ShaderKey &key, const ShaderInfo &info, bool lean = false);
// highp where the device has it in fragment shaders (GL_FRAGMENT_PRECISION_HIGH), else mediump
std::string GxFragmentShader(const ShaderKey &key, const ShaderInfo &info);
// The attribute name of a source ("aPos", ...).
const char *GxAttributeName(int source);

} // namespace re4gx2
