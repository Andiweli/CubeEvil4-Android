// The upscalers' shader text and constants (gx_upscale.h). The third-party files come in unchanged, embedded by
// CMakeLists.txt as raw string literals (embed/*.inc).
#include "gx_upscale.h"

#include <cstdint>
#include <cstring>

// FSR1's constants on the processor: the header's own CPU code (A_CPU); its many helpers unused here
#include <math.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define A_CPU 1
#include "third_party/fidelityfx-fsr/ffx_a.h"
#include "third_party/fidelityfx-fsr/ffx_fsr1.h"
#pragma clang diagnostic pop

namespace re4gx {
namespace {

const char kFfxA[] =
#include "ffx_a.inc"
    ;
const char kFfxFsr1[] =
#include "ffx_fsr1.inc"
    ;
const char kSgsr1[] =
#include "sgsr1.inc"
    ;

void Replace(std::string &s, const std::string &from, const std::string &to) {
  const std::size_t at = s.find(from);
  if (at != std::string::npos)
    s.replace(at, from.size(), to);
}

void ReplaceAll(std::string &s, const std::string &from, const std::string &to) {
  for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
    s.replace(at, from.size(), to);
}

// the text from `from` up to (not including) `until` taken out
void Cut(std::string &s, const std::string &from, const std::string &until) {
  const std::size_t a = s.find(from);
  const std::size_t b = a == std::string::npos ? a : s.find(until, a);
  if (a != std::string::npos && b != std::string::npos)
    s.erase(a, b - a);
}

// ffx_fsr1.h for the shaders: without its constant setup (FsrEasuCon, FsrEasuConOffset, FsrRcasCon, made on the
// processor here), whose int-to-uint assignments GLSL ES does not allow
std::string FsrGpu() {
  std::string f = kFfxFsr1;
  Cut(f, "A_STATIC void FsrEasuCon(", "#if defined(A_GPU)&&defined(FSR_EASU_F)");
  Cut(f, "A_STATIC void FsrRcasCon(", "#if defined(A_GPU)&&defined(FSR_RCAS_F)");
  return f;
}

// the push constant block of a program (the same in both stages)
std::string Block(int program) {
  if (program == 1)
    return "layout(push_constant) uniform UpscalePc { highp vec4 ViewportInfo[1]; highp vec2 rows; } pc_;\n";
  if (program == 2)
    return "layout(push_constant) uniform UpscalePc { highp uvec4 con0; highp uvec4 con1; highp uvec4 con2; "
           "highp uvec4 con3; highp vec2 rows; } pc_;\n";
  return "layout(push_constant) uniform UpscalePc { highp uvec4 con; highp vec2 rows; } pc_;\n";
}

}  // namespace

UpscaleSources UpscaleShaders(bool vulkan, int program) {
  UpscaleSources s;
  const std::string head = vulkan ? "#version 450\n" : "#version 310 es\n";
  // the quad: in_TEXCOORD0 the source's texture coordinates of the output's place (row 0 of the output = the region's
  // top, as the XFB's row 0 is the picture's top)
  s.vs = head + (vulkan ? Block(program) : std::string("uniform highp vec2 rows;\n")) +
         "layout(location = 0) out highp vec4 in_TEXCOORD0;\n"
         "void main() {\n"
         "  vec2 p = vec2(float(" + std::string(vulkan ? "gl_VertexIndex" : "gl_VertexID") + " & 1), float(" +
         std::string(vulkan ? "gl_VertexIndex" : "gl_VertexID") + " >> 1));\n"
         "  highp vec2 r = " + std::string(vulkan ? "pc_.rows" : "rows") + ";\n"
         "  in_TEXCOORD0 = vec4(p.x, r.x + p.y * r.y, 0.0, 0.0);\n"
         "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
         "}\n";
  if (program == 1) {
    std::string f = kSgsr1;
    Replace(f, "#version 300 es", vulkan ? "#version 450" : "#version 310 es");
    Replace(f, "int mode = OperationMode;", "const int mode = OperationMode;");
    // "highp vec2(...)": a precision on a constructor, which Qualcomm's compiler takes and GLSL does not
    ReplaceAll(f, "highp vec2(", "vec2(");
    if (vulkan) {
      // its uniforms: the sampler at binding 0, ViewportInfo in the push constants
      const std::size_t a = f.find("#if defined(UseUniformBlock)");
      const std::size_t b = a == std::string::npos ? a : f.find("#endif", a);
      if (a != std::string::npos && b != std::string::npos)
        f.replace(a, b + 6 - a, "layout(set = 0, binding = 0) uniform mediump sampler2D ps0;\n" + Block(1) +
                                    "#define ViewportInfo pc_.ViewportInfo\n");
    }
    s.sgsr = f;
    return s;
  }
  const std::string common =
      head + "precision highp float;\nprecision highp int;\n" +
      (vulkan ? "layout(set = 0, binding = 0) uniform highp sampler2D src;\n" + Block(program)
              : std::string("uniform highp sampler2D src;\n") +
                    (program == 2 ? "uniform highp uvec4 con0;\nuniform highp uvec4 con1;\nuniform highp uvec4 con2;\n"
                                    "uniform highp uvec4 con3;\n"
                                  : "uniform highp uvec4 con;\n")) +
      "layout(location = 0) out vec4 o;\n#define A_GPU 1\n#define A_GLSL 1\n" + kFfxA + "\n";
  if (program == 2) {
    s.easu = common + "#define FSR_EASU_F 1\n"
                      "AF4 FsrEasuRF(AF2 p) { return textureGather(src, p, 0); }\n"
                      "AF4 FsrEasuGF(AF2 p) { return textureGather(src, p, 1); }\n"
                      "AF4 FsrEasuBF(AF2 p) { return textureGather(src, p, 2); }\n" +
             FsrGpu() +
             "\nvoid main() {\n  AF3 c;\n" +
             (vulkan ? "  FsrEasuF(c, AU2(gl_FragCoord.xy), pc_.con0, pc_.con1, pc_.con2, pc_.con3);\n"
                     : "  FsrEasuF(c, AU2(gl_FragCoord.xy), con0, con1, con2, con3);\n") +
             "  o = vec4(c, 1.0);\n}\n";
  } else {
    s.rcas = common + "#define FSR_RCAS_F 1\n"
                      "AF4 FsrRcasLoadF(ASU2 p) { return texelFetch(src, ASU2(p), 0); }\n"
                      "void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}\n" +
             FsrGpu() +
             "\nvoid main() {\n  AF3 c;\n" +
             (vulkan ? "  FsrRcasF(c.r, c.g, c.b, AU2(gl_FragCoord.xy), pc_.con);\n"
                     : "  FsrRcasF(c.r, c.g, c.b, AU2(gl_FragCoord.xy), con);\n") +
             "  o = vec4(c, 1.0);\n}\n";
  }
  return s;
}

void EasuConstants(std::uint32_t con[16], float inW, float inH, float texW, float texH, float outW, float outH,
                   float offY) {
  varAU4(c0);
  varAU4(c1);
  varAU4(c2);
  varAU4(c3);
  FsrEasuConOffset(c0, c1, c2, c3, inW, inH, texW, texH, outW, outH, 0.0f, offY);
  std::memcpy(con, c0, 16);
  std::memcpy(con + 4, c1, 16);
  std::memcpy(con + 8, c2, 16);
  std::memcpy(con + 12, c3, 16);
}

void RcasConstants(std::uint32_t con[4], float stops) {
  varAU4(c);
  FsrRcasCon(c, stops);
  std::memcpy(con, c, 16);
}

}  // namespace re4gx
