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

// The display copy's filters (moved here unchanged from GlRenderer::MakeFxaa, MakeSharpen and MakePsx,
// for the Vulkan renderer too, 08.11.2026)

// the menu's "Sharpen" (GxRenderer::SetSharpen): how far a texel moves away from its neighbours' mean ("Light",
// "Strong"; RE4_GX_SHARPEN_AMOUNT=A,B for the PC's comparisons)
constexpr float kSharpLight = 0.6f, kSharpStrong = 1.2f;

constexpr char kFxaaVs[] = "#version 300 es\n"
                          "out vec2 vUv;\n"
                          "void main() {\n"
                          "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                          "  vUv = p;\n"
                          "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                          "}\n";

// Edge smoothing along the luma gradient: the direction of the edge from the four corners'
// luma, two taps along it (each between samples) and a wider pair; the wider mix is dropped when
// it leaves the range of the neighbourhood's luma (a thin line stays a line).
constexpr char kFxaaFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D src;\nuniform vec2 texel;\nin vec2 vUv;\nout vec4 o;\n"
    "const vec3 kLuma = vec3(0.299, 0.587, 0.114);\n"
    "void main() {\n"
    "  vec3 nw = texture(src, vUv + vec2(-1.0, -1.0) * texel).rgb;\n"
    "  vec3 ne = texture(src, vUv + vec2(1.0, -1.0) * texel).rgb;\n"
    "  vec3 sw = texture(src, vUv + vec2(-1.0, 1.0) * texel).rgb;\n"
    "  vec3 se = texture(src, vUv + vec2(1.0, 1.0) * texel).rgb;\n"
    "  vec3 m = texture(src, vUv).rgb;\n"
    "  float lnw = dot(nw, kLuma), lne = dot(ne, kLuma), lsw = dot(sw, kLuma), lse = dot(se, kLuma), lm = dot(m, kLuma);\n"
    "  float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));\n"
    "  float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));\n"
    "  vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));\n"
    "  float reduce = max((lnw + lne + lsw + lse) * 0.25 * 0.125, 1.0 / 128.0);\n"
    "  dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-8.0), vec2(8.0)) * texel;\n"
    "  vec3 a = 0.5 * (texture(src, vUv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(src, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
    "  vec3 b = a * 0.5 + 0.25 * (texture(src, vUv + dir * -0.5).rgb + texture(src, vUv + dir * 0.5).rgb);\n"
    "  float lb = dot(b, kLuma);\n"
    "  o = vec4((lb < lmin || lb > lmax) ? a : b, 1.0);\n"
    "}\n";

constexpr char kSharpenVs[] = "#version 300 es\n"
                          "void main() {\n"
                          "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                          "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                          "}\n";

// Contrast-adaptive sharpening of the finished picture (written for the port, 08.11.2026, after the idea of
// AMD's CAS/RCAS, no code of theirs): a texel and its four neighbours, five reads. The texel moves away from
// the neighbours' mean by `amount`: not where the neighbourhood's contrast is within a few steps of 8-bit
// colour (fog, gradients, the noise of dark areas stay as they are), half as much at hard edges (outlines,
// text: they are sharp already), less for a lone texel (sparkle), and never beyond the neighbourhood's own
// darkest and brightest colour (no bright or dark seams at edges, letters and the HUD). lim: the last column,
// the first and the last row shown (GxRenderer::SetShownRows): the rows outside are copied as they are (the
// screenshot shows them; the screen shows at most the one next to the edge, through its bilinear filter), the
// reads of the rows inside stay inside (the game's black bars do not darken or lighten the edge rows).
constexpr char kSharpenFs[] =
    "#version 300 es\nprecision mediump float;\n"
    "uniform mediump sampler2D src;\nuniform float amount;\nuniform ivec3 lim;\nout vec4 o;\n"
    "const vec3 kLuma = vec3(0.299, 0.587, 0.114);\n"
    "void main() {\n"
    "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "  vec3 c = texelFetch(src, p, 0).rgb;\n"
    "  if (p.y < lim.y || p.y > lim.z) { o = vec4(c, 1.0); return; }\n"
    "  vec3 n = texelFetch(src, ivec2(p.x, max(p.y - 1, lim.y)), 0).rgb;\n"
    "  vec3 s = texelFetch(src, ivec2(p.x, min(p.y + 1, lim.z)), 0).rgb;\n"
    "  vec3 w = texelFetch(src, ivec2(max(p.x - 1, 0), p.y), 0).rgb;\n"
    "  vec3 e = texelFetch(src, ivec2(min(p.x + 1, lim.x), p.y), 0).rgb;\n"
    "  vec3 lo = min(c, min(min(n, s), min(w, e)));\n"
    "  vec3 hi = max(c, max(max(n, s), max(w, e)));\n"
    "  vec3 mean = 0.25 * (n + s + w + e);\n"
    "  float range = dot(hi - lo, kLuma);\n"
    "  float k = amount * smoothstep(0.012, 0.05, range) * (1.0 - 0.5 * smoothstep(0.3, 0.8, range));\n"
    "  float lone = abs(dot(c - mean, kLuma)) / max(range, 0.004);\n"
    "  k *= 1.0 - 0.5 * lone * lone;\n"
    "  o = vec4(clamp(c + k * (c - mean), lo, hi), 1.0);\n"
    "}\n";

constexpr char kPsxVs[] = "#version 300 es\n"
                          "out vec2 vUv;\n"
                          "void main() {\n"
                          "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                          "  vUv = p;\n"
                          "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                          "}\n";

// One colour per cell of the grid: the mean of 4 x 4 points across it (one point alone made fine textures on
// the ground flicker as the camera moved, user report 03.11.2026; four still let fine planks flicker at the
// higher picture levels, the owner's report the same day), 8 bits to 5 per channel after the PlayStation's
// ordered dither (a 4 x 4 table of -4..3 added to the 8-bit value, by the cell's place). A cell keeps the
// level it had in the picture before while its new value stays within kHold of a level of that one (the
// owner's dumps of 04.11.2026, castle carpets and walls: cells near the edge of two levels changed level
// from picture to picture as the textures below shimmered, with the camera still or moving slowly; a
// value that leaves that band, as when the camera turns or the light changes, takes its own level)
constexpr char kPsxFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D src;\nuniform sampler2D prev;\nuniform float usePrev;\nuniform vec2 grid;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "const float kHold = 1.0;\n"
    "const float kDither[16] = float[16](-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0,\n"
    "                                    -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0);\n"
    "void main() {\n"
    "  vec2 cell = min(floor(vUv * grid), grid - 1.0);\n"
    "  vec3 c = vec3(0.0);\n"
    "  for (int y = 0; y < 4; y++)\n"
    "    for (int x = 0; x < 4; x++)\n"
    "      c += texture(src, (cell + (vec2(x, y) + 0.5) * 0.25) / grid).rgb;\n"
    "  c *= 255.0 / 16.0;\n"
    "  ivec2 d = ivec2(mod(cell, 4.0));\n"
    "  vec3 v = clamp(c + kDither[d.y * 4 + d.x], 0.0, 255.0) / 8.0;\n"
    "  c = floor(v);\n"
    "  if (usePrev > 0.5) {\n"
    "    vec3 p = floor(texture(prev, (cell + 0.5) / grid).rgb * 31.0 + 0.5);\n"
    "    if (all(greaterThanEqual(v, p - kHold)) && all(lessThan(v, p + 1.0 + kHold)))\n"
    "      c = p;\n"
    "  }\n"
    "  o = vec4(c * (255.0 / 31.0) / 255.0, 1.0);\n"
    "}\n";

} // namespace fixed
} // namespace re4gx
