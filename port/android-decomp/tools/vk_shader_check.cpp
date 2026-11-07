// vk_shader_check: every shader key of a key list (the app's asset gx-programs.keys, or the cache's room lists) as
// Vulkan GLSL (gx_vk_shader.cpp) through glslang to SPIR-V (vk_shader.cpp): the vertex shader, the merged draw calls'
// vertex shader and the fragment shader of each key (port/docs/vulkan-bauplan.md, E2). Prints the failures with
// their source, how long glslang took, and with --spirv DIR writes the SPIR-V for spirv-val.
//
//   vk_shader_check [--spirv DIR] [--limit N] gx-programs.keys...
#include "gx_core.h"
#include "gx_gl_shader.h"
#include "gx_vk_shader.h"
#include "vk_shader.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool ReadKeys(const char *path, std::vector<re4gx::ShaderKey> *keys) {
  FILE *f = std::fopen(path, "rb");
  if (!f)
    return false;
  re4gx::KeyListHead head{};
  bool ok = std::fread(&head, sizeof(head), 1, f) == 1 && std::memcmp(head.magic, "RE4L", 4) == 0 &&
            head.keyBytes == sizeof(re4gx::ShaderKey) && head.count < 1000000;
  if (ok) {
    const std::size_t at = keys->size();
    keys->resize(at + head.count);
    ok = std::fread(keys->data() + at, sizeof(re4gx::ShaderKey), head.count, f) == head.count;
  }
  std::fclose(f);
  return ok;
}

} // namespace

int main(int argc, char **argv) {
  std::string spirvDir;
  std::size_t limit = ~std::size_t(0);
  std::vector<re4gx::ShaderKey> keys;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--spirv") == 0 && i + 1 < argc) {
      spirvDir = argv[++i];
    } else if (std::strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
      limit = std::strtoul(argv[++i], nullptr, 10);
    } else if (!ReadKeys(argv[i], &keys)) {
      std::fprintf(stderr, "%s: not a key list of this build (RE4L, %zu-byte keys)\n", argv[i], sizeof(re4gx::ShaderKey));
      return 2;
    }
  }
  if (keys.empty()) {
    std::fprintf(stderr, "usage: vk_shader_check [--spirv DIR] [--limit N] gx-programs.keys...\n");
    return 2;
  }
  if (keys.size() > limit)
    keys.resize(limit);
  std::printf("vk_shader_check: %zu keys, %s\n", keys.size(), re4vk::CompilerName());
  unsigned failed = 0, shaders = 0;
  double ms = 0.0, most = 0.0;
  std::size_t words = 0;
  for (std::size_t n = 0; n < keys.size(); n++) {
    const re4gx::ShaderKey &k = keys[n];
    const struct {
      const char *kind;
      bool vertex;
      std::string es;
    } parts[3] = {{"vertex", true, re4gx::GxVertexShader(k)},
                  {"merged vertex", true, re4gx::GxMergedVertexShader(k)},
                  {"fragment", false, re4gx::GxFragmentShader(k)}};
    for (int p = 0; p < 3; p++) {
      const std::string vk = re4gx::VulkanGlsl(parts[p].es, parts[p].vertex);
      std::vector<std::uint32_t> spirv;
      std::string log;
      const auto t0 = std::chrono::steady_clock::now();
      const bool ok = re4vk::CompileGlsl(parts[p].vertex ? re4vk::ShaderStage::kVertex : re4vk::ShaderStage::kFragment,
                                         vk, &spirv, &log);
      const double t = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      shaders++;
      if (n || p)  // (the first one builds glslang's tables)
        ms += t;
      most = std::max(most, n || p ? t : 0.0);
      words += spirv.size();
      if (!ok) {
        if (failed++ < 3)
          std::printf("FAIL key %zu, %s shader:\n%s\n--- source ---\n%s\n", n, parts[p].kind, log.c_str(), vk.c_str());
        continue;
      }
      if (!spirvDir.empty()) {
        char name[64];
        std::snprintf(name, sizeof(name), "/%05zu-%d.spv", n, p);
        if (FILE *f = std::fopen((spirvDir + name).c_str(), "wb")) {
          std::fwrite(spirv.data(), 4, spirv.size(), f);
          std::fclose(f);
        }
      }
    }
  }
  std::printf("vk_shader_check: %u shaders, %u failed; glslang %.2f ms per shader on average, %.1f ms the longest; "
              "%.1f KB of SPIR-V per shader\n",
              shaders, failed, shaders > 1 ? ms / (shaders - 1) : 0.0, most,
              shaders ? static_cast<double>(words) * 4.0 / 1024.0 / shaders : 0.0);
  return failed ? 1 : 0;
}
