// GLSL to SPIR-V with glslang (vk_shader.h).
#include "vk_shader.h"

#include <algorithm>

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <glslang/build_info.h>

#include <cstddef>
#include <mutex>

namespace re4vk {

namespace {

std::once_flag gInit;

// The limits glslang checks the shaders against: all generous (the device's own limits are Vulkan's business, and
// the generator stays within GLES 3.0's: 8 textures, 16 KB uniform blocks). TBuiltInResource is ints, then the
// bools of TLimits.
TBuiltInResource Resources() {
  TBuiltInResource r;
  static_assert(offsetof(TBuiltInResource, limits) % sizeof(int) == 0, "ints before the limits");
  int *ints = reinterpret_cast<int *>(&r);
  for (std::size_t i = 0; i < offsetof(TBuiltInResource, limits) / sizeof(int); i++)
    ints[i] = 256;
  r.maxDrawBuffers = 8;
  r.maxClipDistances = 8;
  r.maxCullDistances = 8;
  r.maxCombinedClipAndCullDistances = 8;
  r.maxSamples = 16;
  r.maxViewports = 16;
  r.minProgramTexelOffset = -8;
  r.maxProgramTexelOffset = 7;
  r.maxComputeWorkGroupSizeX = 1024;
  r.maxComputeWorkGroupSizeY = 1024;
  r.maxComputeWorkGroupSizeZ = 64;
  r.maxComputeWorkGroupCountX = r.maxComputeWorkGroupCountY = r.maxComputeWorkGroupCountZ = 65535;
  r.limits.nonInductiveForLoops = true;
  r.limits.whileLoops = true;
  r.limits.doWhileLoops = true;
  r.limits.generalUniformIndexing = true;
  r.limits.generalAttributeMatrixVectorIndexing = true;
  r.limits.generalVaryingIndexing = true;
  r.limits.generalSamplerIndexing = true;
  r.limits.generalVariableIndexing = true;
  r.limits.generalConstantMatrixVectorIndexing = true;
  return r;
}

// The debug instructions out of a SPIR-V module (OpSource*, OpName, OpMemberName, OpString, OpLine, OpNoLine,
// OpModuleProcessed): the spec makes them optional, the program stays the same; the GX shaders' names of their
// blocks' members were most of their size
void StripDebug(std::vector<std::uint32_t> *words) {
  std::vector<std::uint32_t> &w = *words;
  if (w.size() < 5)
    return;
  std::size_t out = 5, at = 5;  // (the header)
  while (at < w.size()) {
    const std::uint32_t count = w[at] >> 16, op = w[at] & 0xFFFF;
    if (count == 0 || at + count > w.size())
      return;  // not well formed: left as it is
    const bool debug = (op >= 2 && op <= 8) || op == 317 || op == 330;
    if (!debug) {
      if (out != at)
        std::copy(w.begin() + static_cast<std::ptrdiff_t>(at), w.begin() + static_cast<std::ptrdiff_t>(at + count),
                  w.begin() + static_cast<std::ptrdiff_t>(out));
      out += count;
    }
    at += count;
  }
  w.resize(out);
}

} // namespace

bool CompileGlsl(ShaderStage stage, const std::string &source, std::vector<std::uint32_t> *spirv, std::string *log) {
  std::call_once(gInit, [] { glslang::InitializeProcess(); });
  static const TBuiltInResource resources = Resources();
  const EShLanguage language = stage == ShaderStage::kVertex ? EShLangVertex : EShLangFragment;
  glslang::TShader shader(language);
  const char *text = source.c_str();
  const int length = static_cast<int>(source.size());
  shader.setStringsWithLengths(&text, &length, 1);
  // Vulkan 1.0 semantics and SPIR-V 1.0: every Vulkan device takes them
  shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
  if (!shader.parse(&resources, 450, ECoreProfile, false, false, messages)) {
    if (log)
      *log = std::string(shader.getInfoLog()) + shader.getInfoDebugLog();
    return false;
  }
  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(messages)) {
    if (log)
      *log = std::string(program.getInfoLog()) + program.getInfoDebugLog();
    return false;
  }
  glslang::SpvOptions options;
  options.generateDebugInfo = false;
  options.disableOptimizer = true;
  options.validate = false;
  spv::SpvBuildLogger logger;
  spirv->clear();
  glslang::GlslangToSpv(*program.getIntermediate(language), *spirv, &logger, &options);
  if (log)
    *log = logger.getAllMessages();
  StripDebug(spirv);
  return !spirv->empty();
}

const char *CompilerName() {
  static const std::string name = "glslang " + std::to_string(GLSLANG_VERSION_MAJOR) + "." +
                                  std::to_string(GLSLANG_VERSION_MINOR) + "." + std::to_string(GLSLANG_VERSION_PATCH);
  return name.c_str();
}

} // namespace re4vk
