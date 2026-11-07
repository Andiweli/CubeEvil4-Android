// The GX shaders for Vulkan (port/docs/vulkan-bauplan.md, E2): the GLSL ES 3.00 text the generator writes for GLES
// (gx_gl_shader.cpp, unchanged: the GL programs and their caches stay as they are) rewritten as GLSL 450 for Vulkan,
// line by line: the version, the uniform blocks at set 0 (bindings as in GL: XFM 0, PX 1, XFP 2, XFL 3, XFMM 4),
// the eight textures at set 1 (binding = map), uMtx and uWiden as push constants, fixed locations for the values
// between the stages, gl_VertexIndex, and the depth from 0 to 1 (Vulkan's clip space) instead of -1 to 1. The EFB's
// rows lie in memory as in GL (row 0 = NDC y -1), so nothing else changes. Any thread.
#pragma once

#include <cstdint>
#include <string>

namespace re4gx {

// Push constants of the GX vertex shaders (both stages see the same range; only the vertex shader reads it).
struct VkDrawConstants {
  std::uint32_t mtx[3];  // uMtx: the draw's matrix indices (not for merged draw calls)
  float widen;           // uWiden: the flat draws' widening ("4:3", GxRenderer::SetWiden)
};
static_assert(sizeof(VkDrawConstants) == 16, "uvec3 + float");

constexpr int kVkSetBlocks = 0;    // the uniform blocks (dynamic offsets)
constexpr int kVkSetTextures = 1;  // the eight combined image samplers

// A shader of the generator (GxVertexShader, GxMergedVertexShader, GxFragmentShader) for Vulkan.
std::string VulkanGlsl(const std::string &glslEs, bool vertex);

} // namespace re4gx
