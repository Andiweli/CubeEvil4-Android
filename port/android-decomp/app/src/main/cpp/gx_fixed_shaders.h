// The fixed shaders of the GX renderer (GLSL ES 3.00 as GLES takes them; the Vulkan renderer rewrites them,
// gx_vk_shader.h): the full-screen quad, the EFB copy, the palette read of a copy, the display, the EFB drawn
// back (MSAA on the tiles). Moved here unchanged from GlRenderer::MakeUtilityPrograms (gx_gl.cpp).
#pragma once

namespace re4gx {
namespace fixed {

constexpr char kQuadVs[] = "#version 300 es\n"
                              "out vec2 vUv;\n"
                              "void main() {\n"
                              "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                              "  vUv = p;\n"
                              "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                              "}\n";
// EFB to a copy: the source rectangle (EFB texels), optionally the 2x2 box
// filter, then the conversion of the copy format
constexpr char kCopyFs[] =
    "#version 300 es\nprecision highp float;\nprecision highp int;\n"
    "uniform sampler2D src;\nuniform int mode;\nuniform vec4 rect;\nuniform vec2 size;\nuniform int halfScale;\n"
    "uniform float gain;\nuniform vec2 limitY;\nuniform vec3 taps;\nuniform float lineStep;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "vec4 fetch(vec2 p) { return texture(src, p / size); }\n"
    "void main() {\n"
    "  vec2 p = rect.xy + vUv * rect.zw;\n"
    "  p.y = clamp(p.y, limitY.x + 1.0, limitY.y - 1.0);\n"
    "  vec4 c = halfScale != 0 ? 0.25 * (fetch(p + vec2(-0.5, -0.5)) + fetch(p + vec2(0.5, -0.5)) + fetch(p + "
    "vec2(-0.5, 0.5)) + fetch(p + vec2(0.5, 0.5))) : fetch(p);\n"
    "  if (taps.y >= 0.0) {\n"
    "    float ya = max(p.y - lineStep, limitY.x + 1.0), yb = min(p.y + lineStep, limitY.y - 1.0);\n"
    "    c.rgb = min(taps.x * fetch(vec2(p.x, ya)).rgb + taps.y * c.rgb + taps.z * fetch(vec2(p.x, yb)).rgb, "
    "vec3(1.0));\n"
    "  } else if (mode < 12) c.rgb = min(c.rgb * gain, vec3(1.0));\n"
    "  float y = clamp(0.257 * c.r + 0.504 * c.g + 0.098 * c.b + 16.0 / 255.0, 0.0, 1.0);\n"
    "  uint z = uint(c.r * 16777215.0);\n"
    "  float zh = float((z >> 16) & 255u) / 255.0, zm = float((z >> 8) & 255u) / 255.0, zl = float(z & 255u) / "
    "255.0;\n"
    "  if (mode == 0) o = c;\n"
    "  else if (mode == 1) o = vec4(floor(c.rgb * vec3(31.0, 63.0, 31.0) + 0.5) / vec3(31.0, 63.0, 31.0), 1.0);\n"
    "  else if (mode == 2) o = c.a >= 0.999 ? vec4(floor(c.rgb * 31.0 + 0.5) / 31.0, 1.0) : "
    "vec4(floor(c * vec4(15.0, 15.0, 15.0, 7.0) + 0.5) / vec4(15.0, 15.0, 15.0, 7.0));\n"
    "  else if (mode == 3) o = vec4(y);\n"
    "  else if (mode == 4) o = vec4(y, y, y, c.a);\n"
    "  else if (mode == 5) o = vec4(c.r);\n"
    "  else if (mode == 6) o = vec4(c.g);\n"
    "  else if (mode == 7) o = vec4(c.b);\n"
    "  else if (mode == 8) o = vec4(c.a);\n"
    "  else if (mode == 9) o = vec4(c.g, c.g, c.g, c.r);\n"
    "  else if (mode == 10) o = vec4(c.b, c.b, c.b, c.g);\n"
    "  else if (mode == 11) o = vec4(c.r, c.r, c.r, c.a);\n"
    "  else if (mode == 12) o = vec4(zh);\n"
    "  else if (mode == 13) o = vec4(zm);\n"
    "  else if (mode == 14) o = vec4(zl);\n"
    "  else if (mode == 15) o = vec4(zm, zm, zm, zh);\n"
    "  else o = vec4(zh, zm, zl, 1.0);\n"
    "}\n";
// a copy read through a palette: the copy's value (one channel: .r; two
// channels: .a high byte, .r low byte) indexes the palette texture
constexpr char kPaletteFs[] =
    "#version 300 es\nprecision highp float;\nprecision highp int;\n"
    "uniform sampler2D src;\nuniform sampler2D pal;\nuniform vec2 palSize;\nuniform int kind;\nuniform int "
    "format;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "void main() {\n"
    "  vec4 c = texture(src, vUv);\n"
    "  int index = kind == 2 ? (int(round(c.a * 255.0)) << 8 | int(round(c.r * 255.0))) & 16383 : "
    "int(round(c.r * 255.0));\n"
    "  if (format == 8) index = index >> 4;\n"
    "  int w = int(palSize.x);\n"
    "  o = texelFetch(pal, ivec2(index % w, index / w), 0);\n"
    "}\n";
// rows: XFB texture rows shown (offset, span; 0..1), row 0 at the top
constexpr char kPresentFs[] = "#version 300 es\nprecision highp float;\n"
                                 "uniform sampler2D src;\nuniform vec2 rows;\nin vec2 vUv;\nout vec4 o;\n"
                                 "void main() { o = vec4(texture(src, vec2(vUv.x, rows.x + (1.0 - vUv.y) * rows.y)).rgb, 1.0); }\n";

// d2: the EFB drawn back from the other set of textures, colour and depth texel for texel
// (the depth as sampled, no arithmetic: the 24 bits come back as they were)
constexpr char kRestoreFs[] = "#version 300 es\nprecision highp float;\n"
                                 "uniform sampler2D color;\nuniform highp sampler2D depth;\nout vec4 o;\n"
                                 "void main() {\n"
                                 "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
                                 "  o = texelFetch(color, p, 0);\n"
                                 "  gl_FragDepth = texelFetch(depth, p, 0).r;\n"
                                 "}\n";

// the depth map for the game's GXPeekZ (GlRenderer::MakePeek): 2 x 2 EFB depths, the nearest, as 24 bits in RGB
constexpr char kPeekVs[] = "#version 300 es\n"
                          "void main() {\n"
                          "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                          "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                          "}\n";
// cell (x, y) of kDepthWidth x kDepthHeight: the nearest depth of the EFB pixels (2x .. 2x + 1, 2y .. 2y + 1)
// at their centres (the game peeks single pixels, a cell stands for four), as GX's 24 bits in r (low byte), g, b
// (a 0: a cell is the 24-bit value). Until 06.11.2026 the depth at one point of the block: a lantern swinging just
// behind the edge of Leon's head in r102 peeked a cell that saw the wall behind him, its glow (esp09/esp0e) shone
// through his head (user report with GX series, 4x). Nearest: anything in front within the cell hides the glow.
constexpr char kPeekFs[] =
    "#version 300 es\nprecision highp float;\nprecision highp int;\n"
    "uniform highp sampler2D depths;\nuniform vec2 size;\nuniform float scale;\nout vec4 o;\n"
    "float at(vec2 p) { return texelFetch(depths, ivec2(min(p * scale, size - 1.0)), 0).r; }\n"
    "void main() {\n"
    "  vec2 c = floor(gl_FragCoord.xy) * 2.0;\n"
    "  float d = min(min(at(c + vec2(0.5, 0.5)), at(c + vec2(1.5, 0.5))), min(at(c + vec2(0.5, 1.5)), at(c + vec2(1.5, 1.5))));\n"
    "  uint z = min(uint(d * 16777215.0 + 0.5), 16777215u);\n"
    "  o = vec4(float(z & 255u), float((z >> 8) & 255u), float((z >> 16) & 255u), 0.0) / 255.0;\n"
    "}\n";

} // namespace fixed
} // namespace re4gx
