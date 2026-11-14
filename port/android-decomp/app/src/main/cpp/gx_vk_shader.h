// The GX shaders for Vulkan (port/docs/vulkan-bauplan.md, E2): the GLSL ES 3.00 text the generator writes for GLES
// (gx_gl_shader.cpp, unchanged: the GL programs and their caches stay as they are) rewritten as GLSL 450 for Vulkan,
// line by line: the version, the uniform blocks at set 0 (bindings as in GL: XFM 0, PX 1, XFP 2, XFL 3, XFMM 4),
// the eight textures at set 1 (binding = map), uMtx and uWiden as push constants, fixed locations for the values
// between the stages, gl_VertexIndex, and the depth from 0 to 1 (Vulkan's clip space) instead of -1 to 1. The EFB's
// rows lie in memory as in GL (row 0 = NDC y -1), so nothing else changes. Any thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace re4gx {

// Push constants of the GX vertex shaders (both stages see the same range; only the vertex shader reads it).
struct VkDrawConstants {
  std::uint32_t mtx[3];  // uMtx: the draw's matrix indices (not for merged draw calls)
  float widen;           // uWiden: the flat draws' widening ("4:3", GxRenderer::SetWiden)
};
static_assert(sizeof(VkDrawConstants) == 16, "uvec3 + float");

constexpr int kVkSetBlocks = 0;    // the uniform blocks (dynamic offsets)
constexpr int kVkSetTextures = 1;  // the eight combined image samplers

// A shader of the generator (GxVertexShader, GxMergedVertexShader, GxFragmentShader) for Vulkan. glDepth: the depth
// stays in GL's clip range (-w..w; VK_EXT_depth_clip_control), else it is mapped to Vulkan's (0..w).
std::string VulkanGlsl(const std::string &glslEs, bool vertex, bool glDepth = false);

// A fixed shader pair of the renderer (gx_fixed_shaders.h, the effects) for Vulkan, both stages together: the plain
// uniforms of both in one block, push constants (std430) or, with an array or more than 128 bytes, a uniform block
// (std140) at set 0 after the textures (`ubo`); `constants` gets each one's name, offset, size and, for an array, its
// length and stride, `bytes` the block's size. The textures of both stages go to set 0 in their order (`samplers`,
// seen by both stages); the values between the stages get locations in the vertex shader's order, the fragment
// shader's output location 0; gl_VertexID becomes gl_VertexIndex.
struct VkFixedLayout {
  struct Constant {
    std::string name;
    std::uint32_t offset, size;           // size: one element's
    std::uint32_t count = 1, stride = 0;  // an array: its length and the step from one element to the next
  };
  std::vector<Constant> constants;
  std::vector<std::string> samplers;
  std::uint32_t bytes = 0;
  bool ubo = false;
  const Constant *Find(const char *name) const;
};
VkFixedLayout VulkanFixedProgram(const std::string &vsEs, const std::string &fsEs, std::string *vs, std::string *fs);

} // namespace re4gx
