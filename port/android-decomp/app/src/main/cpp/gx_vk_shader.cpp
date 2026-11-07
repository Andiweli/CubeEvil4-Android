// The GX shaders for Vulkan (gx_vk_shader.h).
#include "gx_vk_shader.h"

#include <cstdio>
#include <cstring>

namespace re4gx {

namespace {

bool Starts(const std::string &s, const char *p) { return s.compare(0, std::strlen(p), p) == 0; }

// the binding of a uniform block in set 0 (as GL's kXfPartBinding / kPixelBinding / kMergeBinding)
int BlockBinding(const std::string &name) {
  if (name == "XFM")
    return 0;
  if (name == "PX")
    return 1;
  if (name == "XFP")
    return 2;
  if (name == "XFL")
    return 3;
  if (name == "XFMM")
    return 4;
  return -1;
}

// the location of a value between the stages, the same in both
int VaryingLocation(const std::string &name) {
  if (name == "vColor0")
    return 0;
  if (name == "vColor1")
    return 1;
  if (name.size() == 5 && Starts(name, "vTex") && name[4] >= '0' && name[4] <= '7')
    return 2 + (name[4] - '0');
  if (name == "vPsxPos")
    return 10;
  if (name == "vPsxClip")
    return 11;
  return -1;
}

void ReplaceAll(std::string &s, const std::string &from, const std::string &to) {
  for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    s.replace(at, from.size(), to);
}

} // namespace

std::string VulkanGlsl(const std::string &es, bool vertex, bool glDepth) {
  std::string out;
  out.reserve(es.size() + 512);
  bool constants = false;
  std::size_t at = 0;
  while (at < es.size()) {
    std::size_t end = es.find('\n', at);
    if (end == std::string::npos)
      end = es.size();
    std::string line = es.substr(at, end - at);
    at = end + 1;
    if (line == "#version 300 es") {
      out += "#version 450\n";
      continue;
    }
    if (Starts(line, "precision "))
      continue;
    if (Starts(line, "layout(std140) uniform ")) {
      const std::string rest = line.substr(std::strlen("layout(std140) uniform "));
      const std::string name = rest.substr(0, rest.find(' '));
      const int b = BlockBinding(name);
      if (b >= 0) {
        char head[96];
        std::snprintf(head, sizeof(head), "layout(std140, set = %d, binding = %d) uniform ", kVkSetBlocks, b);
        out += head + rest + "\n";
        continue;
      }
    }
    if (Starts(line, "uniform sampler2D s") && line.size() == 21 && line[20] == ';') {
      const int map = line[19] - '0';
      char decl[96];
      std::snprintf(decl, sizeof(decl), "layout(set = %d, binding = %d) uniform sampler2D s%d;", kVkSetTextures, map,
                    map);
      out += std::string(decl) + "\n";
      continue;
    }
    if (line == "uniform uvec3 uMtx;" || line == "uniform float uWiden;") {
      if (!constants) {
        constants = true;
        out += "layout(push_constant) uniform PC {\n  uvec3 uMtx;\n  float uWiden;\n};\n";
      }
      continue;
    }
    // values between the stages (not the vertex attributes, which have their locations already)
    {
      std::string q = line;
      std::string prefix;
      if (Starts(q, "centroid ")) {
        prefix = "centroid ";
        q = q.substr(9);
      }
      const bool isOut = Starts(q, "out "), isIn = Starts(q, "in ");
      if (isOut || isIn) {
        const std::size_t sp = q.find(' ', isOut ? 4 : 3);
        if (sp != std::string::npos && q.back() == ';') {
          const std::string name = q.substr(sp + 1, q.size() - sp - 2);
          int loc = VaryingLocation(name);
          if (!vertex && isOut && name == "oColor")
            loc = 0;
          if (loc >= 0) {
            char head[48];
            std::snprintf(head, sizeof(head), "layout(location = %d) ", loc);
            out += head + prefix + q + "\n";
            continue;
          }
        }
      }
    }
    out += line + "\n";
  }
  if (vertex) {
    ReplaceAll(out, "gl_VertexID", "gl_VertexIndex");
    // GL's clip z runs from -w to w, Vulkan's from 0 to w: the same window depth without the GL mapping
    if (!glDepth)
      ReplaceAll(out, "2.0 * (vp[0].z * clip.z + vp[1].y * clip.w) / 16777215.0 - clip.w",
               "(vp[0].z * clip.z + vp[1].y * clip.w) / 16777215.0");
  }
  return out;
}

const VkFixedLayout::Constant *VkFixedLayout::Find(const char *name) const {
  for (const Constant &c : constants)
    if (c.name == name)
      return &c;
  return nullptr;
}

std::string VulkanFixedGlsl(const std::string &es, bool vertex, VkFixedLayout *layout) {
  VkFixedLayout l;
  std::string decls, rest;
  std::size_t at = 0;
  while (at < es.size()) {
    std::size_t end = es.find('\n', at);
    if (end == std::string::npos)
      end = es.size();
    std::string line = es.substr(at, end - at);
    at = end + 1;
    if (line == "#version 300 es" || Starts(line, "precision "))
      continue;
    if (Starts(line, "uniform ")) {
      std::string d = line.substr(8);
      if (Starts(d, "highp ") || Starts(d, "mediump ") || Starts(d, "lowp "))
        d = d.substr(d.find(' ') + 1);
      const std::size_t sp = d.find(' ');
      const std::string type = d.substr(0, sp), name = d.substr(sp + 1, d.size() - sp - 2);
      if (type == "sampler2D") {
        char b[128];
        std::snprintf(b, sizeof(b), "layout(set = 0, binding = %zu) uniform sampler2D %s;\n", l.samplers.size(),
                      name.c_str());
        decls += b;
        l.samplers.push_back(name);
        continue;
      }
      // std430: scalars 4, vec2 8, vec3 and vec4 16 aligned
      std::uint32_t size = 4, align = 4;
      if (type == "vec2" || type == "ivec2" || type == "uvec2")
        size = align = 8;
      else if (type == "vec3" || type == "ivec3" || type == "uvec3")
        size = 12, align = 16;
      else if (type == "vec4" || type == "ivec4" || type == "uvec4")
        size = align = 16;
      const std::uint32_t offset = (l.bytes + align - 1) / align * align;
      l.constants.push_back({name, offset, size});
      l.bytes = offset + size;
      rest += "  " + type + " " + name + ";\n";
      continue;
    }
    if (line == "out vec2 vUv;" || line == "in vec2 vUv;" || (!vertex && Starts(line, "out vec4 "))) {
      decls += "layout(location = 0) " + line + "\n";
      continue;
    }
    decls += line + "\n";
  }
  std::string out = "#version 450\n";
  if (!l.constants.empty())
    out += "layout(push_constant) uniform P {\n" + rest + "};\n";
  out += decls;
  ReplaceAll(out, "gl_VertexID", "gl_VertexIndex");
  l.bytes = (l.bytes + 15) / 16 * 16;
  if (layout)
    *layout = l;
  return out;
}

} // namespace re4gx
