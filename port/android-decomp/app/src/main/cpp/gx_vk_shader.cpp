// The GX shaders for Vulkan (gx_vk_shader.h).
#include "gx_vk_shader.h"

#include <cstdio>
#include <cstdlib>
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

namespace {

struct UniformLine {
  std::string type, name;
  std::uint32_t count = 1;
};

// `uniform [highp|mediump|lowp] TYPE NAME;` or `... NAME[N];`
bool ParseUniform(const std::string &line, UniformLine *u) {
  if (!Starts(line, "uniform ") || line.empty() || line.back() != ';')
    return false;
  std::string r = line.substr(8);
  for (const char *p : {"highp ", "mediump ", "lowp "})
    if (Starts(r, p))
      r = r.substr(std::strlen(p));
  const std::size_t sp = r.find(' ');
  if (sp == std::string::npos)
    return false;
  u->type = r.substr(0, sp);
  std::string n = r.substr(sp + 1, r.size() - sp - 2);
  u->count = 1;
  const std::size_t br = n.find('[');
  if (br != std::string::npos) {
    u->count = static_cast<std::uint32_t>(std::strtoul(n.c_str() + br + 1, nullptr, 10));
    n = n.substr(0, br);
  }
  u->name = n;
  return true;
}

// std430's size and alignment of a scalar or vector (std140 differs for arrays only, below)
void TypeSize(const std::string &t, std::uint32_t *size, std::uint32_t *align) {
  const char last = t.empty() ? ' ' : t.back();
  const bool vec = t.find("vec") != std::string::npos;
  const int n = vec && last >= '2' && last <= '4' ? last - '0' : 1;
  *size = 4u * static_cast<std::uint32_t>(n);
  *align = n == 3 ? 16u : *size;
}

// `[flat |centroid |smooth ]in|out TYPE NAME;`: the name and whether it is an output; false for other lines
bool ParseVarying(const std::string &line, std::string *name, bool *out) {
  std::string q = line;
  for (const char *p : {"flat ", "centroid ", "smooth "})
    if (Starts(q, p))
      q = q.substr(std::strlen(p));
  const bool isOut = Starts(q, "out "), isIn = Starts(q, "in ");
  if ((!isOut && !isIn) || q.empty() || q.back() != ';')
    return false;
  const std::size_t sp = q.rfind(' ');
  *name = q.substr(sp + 1, q.size() - sp - 2);
  *out = isOut;
  return true;
}

} // namespace

const VkFixedLayout::Constant *VkFixedLayout::Find(const char *name) const {
  for (const Constant &c : constants)
    if (c.name == name)
      return &c;
  return nullptr;
}

VkFixedLayout VulkanFixedProgram(const std::string &vsEs, const std::string &fsEs, std::string *vs, std::string *fs) {
  VkFixedLayout l;
  std::vector<UniformLine> plain;
  auto lines = [](const std::string &s) {
    std::vector<std::string> v;
    std::size_t at = 0;
    while (at < s.size()) {
      std::size_t end = s.find('\n', at);
      if (end == std::string::npos)
        end = s.size();
      v.push_back(s.substr(at, end - at));
      at = end + 1;
    }
    return v;
  };
  const std::vector<std::string> vl = lines(vsEs), fl = lines(fsEs);
  for (const std::vector<std::string> *src : {&vl, &fl})
    for (const std::string &line : *src) {
      UniformLine u;
      if (!ParseUniform(line, &u))
        continue;
      if (Starts(u.type, "sampler")) {
        bool known = false;
        for (const std::string &s : l.samplers)
          known = known || s == u.name;
        if (!known)
          l.samplers.push_back(u.name);
        continue;
      }
      bool known = false;
      for (const UniformLine &p : plain)
        known = known || p.name == u.name;
      if (!known)
        plain.push_back(u);
    }
  // push constants (std430) while they fit 128 bytes and hold no array, else a uniform block (std140)
  auto layOut = [&](bool std140) {
    l.constants.clear();
    std::uint32_t bytes = 0;
    for (const UniformLine &u : plain) {
      std::uint32_t size = 4, align = 4;
      TypeSize(u.type, &size, &align);
      std::uint32_t stride = (size + align - 1) / align * align;
      if (std140 && u.count > 1) {
        align = 16;  // an array's elements on 16 bytes
        stride = (size + 15) / 16 * 16;
      }
      const std::uint32_t offset = (bytes + align - 1) / align * align;
      l.constants.push_back({u.name, offset, size, u.count, stride});
      bytes = offset + (u.count > 1 ? stride * u.count : size);
    }
    l.bytes = (bytes + 15) / 16 * 16;
  };
  bool arrays = false;
  for (const UniformLine &u : plain)
    arrays = arrays || u.count > 1;
  layOut(false);
  l.ubo = arrays || l.bytes > 128;
  if (l.ubo)
    layOut(true);
  std::string block;
  if (!plain.empty()) {
    char head[96];
    if (l.ubo)
      std::snprintf(head, sizeof(head), "layout(std140, set = 0, binding = %zu) uniform U {\n", l.samplers.size());
    else
      std::snprintf(head, sizeof(head), "layout(push_constant) uniform P {\n");
    block = head;
    for (const UniformLine &u : plain)
      block += "  " + u.type + " " + u.name + (u.count > 1 ? "[" + std::to_string(u.count) + "]" : "") + ";\n";
    block += "};\n";
  }
  std::vector<std::string> varyings;  // the vertex shader's outputs, in order
  auto emit = [&](const std::vector<std::string> &src, bool vertex) {
    std::string out = "#version 450\n" + block;
    for (const std::string &line : src) {
      if (Starts(line, "#version ") || Starts(line, "precision "))
        continue;
      UniformLine u;
      if (ParseUniform(line, &u)) {
        if (Starts(u.type, "sampler")) {
          std::size_t b = 0;
          while (b < l.samplers.size() && l.samplers[b] != u.name)
            b++;
          char decl[160];
          std::snprintf(decl, sizeof(decl), "layout(set = 0, binding = %zu) uniform %s %s;\n", b, u.type.c_str(),
                        u.name.c_str());
          out += decl;
        }
        continue;  // (a plain one is in the block)
      }
      std::string name;
      bool isOut = false;
      if (ParseVarying(line, &name, &isOut)) {
        std::size_t loc = 0;
        if (vertex || !isOut) {
          while (loc < varyings.size() && varyings[loc] != name)
            loc++;
          if (loc == varyings.size())
            varyings.push_back(name);
        }
        out += "layout(location = " + std::to_string(loc) + ") " + line + "\n";
        continue;
      }
      out += line + "\n";
    }
    ReplaceAll(out, "gl_VertexID", "gl_VertexIndex");
    return out;
  };
  *vs = emit(vl, true);
  *fs = emit(fl, false);
  return l;
}

} // namespace re4gx
