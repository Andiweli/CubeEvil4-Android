// GLSL to SPIR-V for the Vulkan renderer with glslang (port/docs/vulkan-bauplan.md, E0/E2). Vulkan takes only
// SPIR-V; the GX shaders are generated at run time (one per combination of TEV stages, lighting, texture
// coordinates), so they are translated on the device. Any thread.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace re4vk {

enum class ShaderStage { kVertex, kFragment };

// `source`: GLSL 450 for Vulkan. True with the SPIR-V words in *spirv; false with glslang's messages in *log.
bool CompileGlsl(ShaderStage stage, const std::string &source, std::vector<std::uint32_t> *spirv, std::string *log);

// "glslang 11.11.0"
const char *CompilerName();

} // namespace re4vk
