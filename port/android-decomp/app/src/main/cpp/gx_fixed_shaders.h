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
// filter, then the conversion of the copy format. `box` (0.99.8): above 0.5 the
// half copy's box reaches that many EFB texels from its centre (one GameCube
// pixel at the scale: the 2x2 box of the GameCube's own pixels, 16 linear taps),
// and the line above / below of the copy filter (taps) is such a box as well
// (the game's depth of field, GxCore::EfbCopy). 0 or 0.5: the 2x2 texels.
constexpr char kCopyFs[] =
    "#version 300 es\nprecision highp float;\nprecision highp int;\n"
    "uniform sampler2D src;\nuniform int mode;\nuniform vec4 rect;\nuniform vec2 size;\nuniform int halfScale;\n"
    "uniform float gain;\nuniform vec2 limitY;\nuniform vec3 taps;\nuniform float lineStep;\nuniform float box;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "vec4 fetch(vec2 p) { return texture(src, p / size); }\n"
    "vec4 row(vec2 p, float y, float d) {\n"
    "  return fetch(p + vec2(-3.0 * d, y)) + fetch(p + vec2(-d, y)) + fetch(p + vec2(d, y)) + fetch(p + vec2(3.0 * d, "
    "y));\n"
    "}\n"
    "vec4 boxed(vec2 p) {\n"
    "  if (halfScale == 0) return fetch(p);\n"
    "  if (box <= 0.5) return 0.25 * (fetch(p + vec2(-0.5, -0.5)) + fetch(p + vec2(0.5, -0.5)) + fetch(p + "
    "vec2(-0.5, 0.5)) + fetch(p + vec2(0.5, 0.5)));\n"
    "  float d = 0.25 * box;\n"
    "  return (row(p, -3.0 * d, d) + row(p, -d, d) + row(p, d, d) + row(p, 3.0 * d, d)) / 16.0;\n"
    "}\n"
    "void main() {\n"
    "  vec2 p = rect.xy + vUv * rect.zw;\n"
    "  p.y = clamp(p.y, limitY.x + 1.0, limitY.y - 1.0);\n"
    "  vec4 c = boxed(p);\n"
    "  if (taps.y >= 0.0) {\n"
    "    float ya = max(p.y - lineStep, limitY.x + 1.0), yb = min(p.y + lineStep, limitY.y - 1.0);\n"
    "    c.rgb = min(taps.x * boxed(vec2(p.x, ya)).rgb + taps.y * c.rgb + taps.z * boxed(vec2(p.x, yb)).rgb, "
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

// The lens flares (moved here unchanged from GlRenderer::MakeFlares, for the Vulkan renderer too, 08.11.2026)

constexpr char kFlareQuadVs[] = "#version 300 es\n"
                          "void main() {\n"
                          "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                          "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                          "}\n";

// texel (i, 0): slot i's followed visibility (a) and colour; texel (i, 1): where its light really is in its box,
// the brightness-weighted middle of the bright open texels (x, y: -1..1 of its radius as 0..1, b 1 = known),
// followed a third of the way a picture (a group's box centre can lie beside the flames). The slot: x, y in EFB
// texels, depth, state 0 free, 1 seen, 2 seen and new, 3 lost, 4 waiting, plus 8 x its radius in EFB texels.
constexpr char kFlareVisFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform highp sampler2D depths;\nuniform sampler2D colors;\nuniform sampler2D before;\n"
    "uniform vec4 slot[32];\nuniform vec4 size;\nout vec4 o;\n"
    "float L(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "bool inside(vec2 t) { return t.x >= 0.0 && t.y >= 0.0 && t.x < size.x && t.y < size.y; }\n"
    "void main() {\n"
    "  int i = int(gl_FragCoord.x), row = int(gl_FragCoord.y);\n"
    "  vec4 s = slot[i];\n"
    "  float st = mod(s.w, 8.0);\n"
    "  bool fresh = st > 1.5 && st < 2.5;\n"
    "  vec4 old = fresh ? vec4(0.0) : texelFetch(before, ivec2(i, 0), 0);\n"
    "  vec4 oldAt = fresh ? vec4(0.0) : texelFetch(before, ivec2(i, 1), 0);\n"
    "  if (st < 0.5) { o = vec4(0.0); return; }\n"
    "  float R = max(2.0, floor(s.w / 8.0));\n"
    "  vec2 oldOff = oldAt.b > 0.5 ? oldAt.xy * 2.0 - 1.0 : vec2(0.0);\n"
    "  if (row == 1) {\n"
    "    o = oldAt;\n"
    "    if (st > 1.5 && st < 2.5 || st < 1.5 || st > 3.5) {\n"
    "      vec2 sum = vec2(0.0);\n"
    "      float wsum = 0.0;\n"
    "      for (int y = -3; y <= 3; y++) {\n"
    "        for (int x = -3; x <= 3; x++) {\n"
    "          vec2 d = vec2(float(x), float(y)) / 3.0;\n"
    "          vec2 t = s.xy + d * R;\n"
    "          if (!inside(t)) continue;\n"
    "          if (texelFetch(depths, ivec2(t), 0).r < s.z - 0.0005) continue;\n"
    "          float w = max(L(texelFetch(colors, ivec2(t), 0).rgb) - 0.55, 0.0);\n"
    "          sum += d * w * w;\n"
    "          wsum += w * w;\n"
    "        }\n"
    "      }\n"
    "      if (wsum > 0.0) {\n"
    "        vec2 off = clamp(sum / wsum, -1.0, 1.0);\n"
    "        off = oldAt.b > 0.5 ? mix(oldOff, off, 0.75) : off;\n"
    "        o = vec4(off * 0.5 + 0.5, 1.0, 1.0);\n"
    "      }\n"
    "    }\n"
    "    return;\n"
    "  }\n"
    "  vec2 c = s.xy + oldOff * R;\n"
    "  float seen = 0.0, contrast = 0.0, core = 1.0;\n"
    "  vec3 col = vec3(0.0);\n"
    "  if (st < 1.5) {\n"
    "    float r = max(size.z, R * 0.6);\n"
    "    for (int k = 0; k < 9; k++) {\n"
    "      vec2 off = k == 0 ? vec2(0.0) : r * vec2(cos(float(k) * 0.785398), sin(float(k) * 0.785398)) * "
    "(k % 2 == 0 ? 1.0 : 0.5);\n"
    "      vec2 t = c + off;\n"
    "      if (!inside(t)) continue;\n"
    "      bool open = texelFetch(depths, ivec2(t), 0).r >= s.z - 0.0005;\n"
    "      seen += open ? 1.0 : 0.0;\n"
    "      vec3 cc = texelFetch(colors, ivec2(t), 0).rgb;\n"
    "      if (open && L(cc) > L(col)) col = cc;\n"
    "    }\n"
    "    seen = min(1.0, seen / 9.0 * 2.0);  // half the taps open: in full view (a candle's holder before its glow)\n"
    "    float ring = 0.0, rn = 0.0;\n"
    "    float far = max(3.0, R * 1.6);\n"
    "    for (int k = 0; k < 8; k++) {\n"
    "      vec2 t = c + far * vec2(cos(float(k) * 0.785398 + 0.3927), sin(float(k) * 0.785398 + 0.3927));\n"
    "      if (!inside(t)) continue;\n"
    "      ring += L(texelFetch(colors, ivec2(t), 0).rgb);\n"
    "      rn += 1.0;\n"
    "    }\n"
    "    contrast = L(col) - (rn > 0.0 ? ring / rn : 1.0);\n"
    "    core = L(texelFetch(colors, ivec2(clamp(s.xy, vec2(0.0), size.xy - 1.0)), 0).rgb);\n"
    "  }\n"
    "  float lum = L(col);\n"
    "  float target = seen * smoothstep(0.65, 0.9, lum) * smoothstep(0.15, 0.35, contrast) * smoothstep(0.3, 0.5, core);\n"
    "  float a = old.a + (target - old.a) * (target > old.a ? 0.1 : 0.14);\n"
    // a lost light (its source not drawn in this picture) gone in about three pictures: its star stayed where the
    // light had been and faded there for some ten pictures (0.99.8, stars wandering along while the camera turned)
    "  if (st > 2.5 && st < 3.5) a = old.a * 0.45;\n"
    "  if (a < 0.004 && target < 0.004) a = 0.0;\n"
    "  vec3 tint = st < 1.5 && lum > 0.05 ? mix(col / max(max(col.r, col.g), col.b), vec3(1.0), 0.45) : old.rgb;\n"
    "  o = vec4(old.a > 0.003 && dot(old.rgb, vec3(1.0)) > 0.01 ? mix(old.rgb, tint, 0.12) : tint, a);\n"
    "  if (size.w > 0.5) o = vec4(seen, lum, 0.0, a);  // (tests: RE4_GX_FLARE_LOG=2)\n"
    "}\n";

// per slot six quads (36 vertices): 0 the glow and starburst at the light, 1..5 its ghosts on the line from the
// light through the picture's centre; nothing for a dark slot. slot.z: the radius in EFB texels, ghost[i]: the
// slot's share of the ghosts; size.z 1: the ghosts only, no glow and star at the light ("Lens flares", 0.99.6).
constexpr char kFlareVs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D vis;\nuniform vec4 slot[32];\nuniform float ghost[32];\nuniform float crowd[32];\n"
    "uniform vec4 size;\n"
    "out vec2 vUv;\nout vec4 vCol;\nflat out int vKind;\n"
    "const vec4 kGhost[5] = vec4[5](vec4(0.42, 0.030, 0.18, 1.0), vec4(0.18, 0.016, 0.22, 2.0),\n"
    "  vec4(-0.30, 0.055, 0.13, 3.0), vec4(-0.62, 0.024, 0.20, 4.0), vec4(-1.05, 0.080, 0.09, 5.0));\n"
    "const vec3 kGhostCol[5] = vec3[5](vec3(0.35, 1.0, 0.55), vec3(1.0, 0.72, 0.35), vec3(0.72, 0.42, 1.0),\n"
    "  vec3(0.35, 0.62, 1.0), vec3(0.40, 0.95, 0.85));\n"
    "void main() {\n"
    "  int q6 = gl_VertexID / 6, c = gl_VertexID % 6;\n"
    "  int i = q6 / 6, k = q6 % 6;\n"
    "  vec4 v = texelFetch(vis, ivec2(i, 0), 0);\n"
    "  vec4 s = slot[i];\n"
    "  vec4 at = texelFetch(vis, ivec2(i, 1), 0);\n"
    "  if (at.b > 0.5) s.xy += (at.xy * 2.0 - 1.0) * max(2.0, floor(s.w / 8.0));\n"
    "  vec2 q = vec2(c == 1 || c == 4 || c == 5 ? 1.0 : -1.0, c == 2 || c == 3 || c == 5 ? 1.0 : -1.0);\n"
    "  float a = v.a * size.w;\n"
    "  vec2 centre;\n"
    "  float rad;\n"
    "  if (k == 0) {\n"
    "    centre = s.xy;\n"
    "    rad = min(s.z, 10.0 * size.x / 640.0) * 6.0;\n"
    "    a *= size.z > 0.5 ? 0.0 : 1.0;\n"
    "    vCol = vec4(v.rgb, a * crowd[i] * 0.9);\n"
    "    vKind = 0;\n"
    "  } else {\n"
    "    vec4 g = kGhost[k - 1];\n"
    "    vec2 mid = size.xy * 0.5;\n"
    "    centre = mid + (s.xy - mid) * g.x;\n"
    "    rad = g.y * size.y * (0.8 + 0.4 * length(s.xy - mid) / size.y);\n"
    "    a *= g.z * ghost[i];\n"
    "    vCol = vec4(mix(kGhostCol[k - 1], v.rgb, 0.25), a);\n"
    "    vKind = int(g.w);\n"
    "  }\n"
    "  vUv = q;\n"
    "  gl_Position = a < 0.003 || s.w < 0.5 ? vec4(2.0, 2.0, 2.0, 1.0)\n"
    "                : vec4((centre + q * rad) / size.xy * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

constexpr char kFlareFs[] =
    "#version 300 es\nprecision mediump float;\n"
    "in vec2 vUv;\nin vec4 vCol;\nflat in int vKind;\nout vec4 o;\n"
    "void main() {\n"
    "  float d = length(vUv);\n"
    "  if (d >= 1.0) discard;\n"
    "  float f;\n"
    "  if (vKind == 0) {\n"
    "    float e = d * 6.0;\n"
    "    float glow = exp(-e * e * 1.4) * 0.9 + exp(-e * e * 0.18) * 0.22;\n"
    "    float ang = atan(vUv.y, vUv.x);\n"
    "    float rays = pow(abs(cos(ang * 3.0)), 28.0) + 0.45 * pow(abs(cos(ang * 3.0 + 0.5236)), 56.0);\n"
    "    f = glow + rays * (1.0 - d) * (1.0 - d) * exp(-d * 1.8) * 0.75;\n"
    "  } else if (vKind == 3 || vKind == 5) {\n"
    "    vec2 h = abs(vUv);\n"
    "    float hex = max(h.x * 0.866 + h.y * 0.5, h.y);\n"
    "    f = smoothstep(0.98, 0.84, hex) * 0.55 + smoothstep(0.74, 0.88, hex) * smoothstep(0.98, 0.88, hex) * 0.8;\n"
    "  } else if (vKind == 4) {\n"
    "    f = smoothstep(1.0, 0.86, d) * smoothstep(0.62, 0.84, d) + (1.0 - d) * 0.25;\n"
    "  } else {\n"
    "    f = smoothstep(1.0, 0.2, d);\n"
    "  }\n"
    "  o = vec4(vCol.rgb * f * vCol.a, 0.0);\n"
    "}\n";

// The gameplay autofocus (moved here unchanged from GlRenderer::MakeAutofocus, for the Vulkan renderer too, 08.11.2026)

constexpr char kDofQuadVs[] = "#version 300 es\n"
                              "out vec2 vUv;\n"
                              "void main() {\n"
                              "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                              "  vUv = p;\n"
                              "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                              "}\n";


// region: the picture in EFB texels (x, y, w, h); size: the EFB texture;
// zmap: projection A, B, viewport zRange, zFar
constexpr char kDofDistance[] =
    "uniform highp sampler2D depths;\nuniform vec4 region;\nuniform vec2 size;\nuniform vec4 zmap;\n"
    "float distanceAt(vec2 t) {\n"
    "  float q = (texture(depths, t / size).r * 16777215.0 - zmap.w) / zmap.z + zmap.x;\n"
    "  return q < 0.0 ? zmap.y / q : 1.0e9;\n"
    "}\n";


// the first lines of the autofocus's fragment shaders (before kDofDistance and their body)
constexpr char kDofHead[] = "#version 300 es\nprecision highp float;\nprecision highp int;\n";

// The measurement: the median of nine depths at the centre (the first port's), or of 35 around
// it when fewer than five lie behind the player; 65535 (far: no blur) when none does. The
// focus in use (texel 0) follows the median of the last three measurements by a quarter of the
// way each picture: one picture measured wrong (a twig or the gun at the centre for a moment,
// a GPU's depth of an edge; the Retroid with MSAA, 19.10.2026: thin branches went soft for
// one picture) changes nothing, a new distance is taken over in about a quarter of a second.
// Texels 1 and 2: the last two measurements. restart: no picture before (all three anew).
// (its body: kDofHead + kDofDistance + this)
constexpr char kDofFocusFs[] =
    "uniform float player;\nuniform highp sampler2D history;\nuniform float restart;\nout vec4 o;\n"
    "float measure() {\n"
    "  float z[35];\n"
    "  for (int i = 0; i < 35; ++i) z[i] = 0.0;\n"
    "  int n = 0;\n"
    "  vec2 c = region.xy + 0.5 * region.zw;\n"
    "  for (int y = -1; y <= 1; ++y)\n"
    "    for (int x = -1; x <= 1; ++x) {\n"
    "      float d = distanceAt(c + vec2(float(x), float(y)) * 0.006 * region.zw);\n"
    "      if (d > player) z[n++] = d;\n"
    "    }\n"
    "  if (n < 5) {\n"
    "    n = 0;\n"
    "    for (int y = -2; y <= 2; ++y)\n"
    "      for (int x = -3; x <= 3; ++x) {\n"
    "        float d = distanceAt(c + vec2(float(x) * 0.05, float(y) * 0.06) * region.zw);\n"
    "        if (d > player) z[n++] = d;\n"
    "      }\n"
    "  }\n"
    "  if (n == 0) return 65535.0;\n"
    "  for (int i = 0; i < 34; ++i)\n"
    "    for (int j = i + 1; j < 35; ++j)\n"
    "      if (j < n && z[j] < z[i]) { float t = z[i]; z[i] = z[j]; z[j] = t; }\n"
    "  return clamp(round(z[n / 2]), 0.0, 65535.0);\n"
    "}\n"
    "float stored(int i) {\n"
    "  vec4 t = texelFetch(history, ivec2(i, 0), 0);\n"
    "  return round(t.r * 255.0) * 256.0 + round(t.g * 255.0);\n"
    "}\n"
    "vec4 focus(float f) {\n"
    "  uint d = uint(clamp(round(f), 0.0, 65535.0));\n"
    "  return vec4(float(d >> 8u) / 255.0, float(d & 255u) / 255.0, 1.0 - smoothstep(3000.0, 6000.0, f), 1.0);\n"
    "}\n"
    "void main() {\n"
    "  int i = int(gl_FragCoord.x);\n"
    "  if (i == 2 && restart < 0.5) { o = texelFetch(history, ivec2(1, 0), 0); return; }\n"
    "  float m = measure();\n"
    "  if (i != 0 || restart > 0.5) { o = focus(m); return; }\n"
    "  float b = stored(1), c = stored(2);\n"
    "  float target = max(min(m, b), min(max(m, b), c));\n"
    "  float before = stored(0);\n"
    "  o = focus(abs(target - before) < 4.0 ? target : before + (target - before) * 0.25);\n"
    "}\n";


// (its body: kDofHead + kDofDistance + this)
constexpr char kDofBlurFs[] =
    "uniform sampler2D frame;\nuniform sampler2D focus;\nuniform float radius;\nuniform float strength;\nout vec4 o;\n"
    "void main() {\n"
    "  vec2 t = gl_FragCoord.xy;\n"
    "  vec4 original = texelFetch(frame, ivec2(t), 0);\n"
    "  vec4 f = texelFetch(focus, ivec2(0), 0);\n"
    "  float focal = round(f.r * 255.0) * 256.0 + round(f.g * 255.0);\n"
    "  float start = focal + max(800.0, focal * 0.25), end = focal + max(2500.0, focal);\n"
    "  float amount = strength * f.b * smoothstep(start, end, distanceAt(t));\n"
    "  if (amount <= 0.0) { o = original; return; }\n"
    "  vec3 sum = original.rgb * 2.0;\n"
    "  float weight = 2.0;\n"
    "  const vec2 taps[8] = vec2[8](vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1), vec2(-0.707, -0.707),\n"
    "                               vec2(0.707, 0.707), vec2(-0.707, 0.707), vec2(0.707, -0.707));\n"
    "  vec2 lo = region.xy + 0.5, hi = region.xy + region.zw - 0.5;\n"
    "  for (int i = 0; i < 8; ++i) {\n"
    "    vec2 p = clamp(t + taps[i] * radius, lo, hi);\n"
    "    if (distanceAt(p) >= start) { sum += texture(frame, p / size).rgb; weight += 1.0; }\n"
    "  }\n"
    "  o = vec4(mix(original.rgb, sum / weight, amount), original.a);\n"
    "}\n";


// GPU step e: the blur above at half the resolution, without the mix. Its centre is the four
// EFB texels under the texel (together weighted as the one above), the taps as above, and only
// what lies behind the focus counts, the centre too: the sharp foreground does not bleed into
// the background through the coarser texels. Alpha: something behind the focus was there
// (1, the colour as it is) or not (0, black), so the mix can leave the latter out.
// (its body: kDofHead + kDofDistance + this)
constexpr char kDofHalfFs[] =
    "uniform sampler2D frame;\nuniform sampler2D focus;\nuniform float radius;\nout vec4 o;\n"
    "void main() {\n"
    "  vec2 t = gl_FragCoord.xy * 2.0;\n"
    "  vec4 f = texelFetch(focus, ivec2(0), 0);\n"
    "  if (f.b <= 0.0) { o = vec4(0.0); return; }\n"
    "  float focal = round(f.r * 255.0) * 256.0 + round(f.g * 255.0);\n"
    "  float start = focal + max(800.0, focal * 0.25);\n"
    "  vec2 lo = region.xy + 0.5, hi = region.xy + region.zw - 0.5;\n"
    "  vec3 sum = vec3(0.0);\n"
    "  float weight = 0.0;\n"
    "  for (int i = 0; i < 4; ++i) {\n"
    "    vec2 p = clamp(t + vec2(float(i & 1) - 0.5, float(i >> 1) - 0.5), lo, hi);\n"
    "    if (distanceAt(p) >= start) { sum += texture(frame, p / size).rgb * 0.5; weight += 0.5; }\n"
    "  }\n"
    "  const vec2 taps[8] = vec2[8](vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1), vec2(-0.707, -0.707),\n"
    "                               vec2(0.707, 0.707), vec2(-0.707, 0.707), vec2(0.707, -0.707));\n"
    "  for (int i = 0; i < 8; ++i) {\n"
    "    vec2 p = clamp(t + taps[i] * radius, lo, hi);\n"
    "    if (distanceAt(p) >= start) { sum += texture(frame, p / size).rgb; weight += 1.0; }\n"
    "  }\n"
    "  o = weight > 0.0 ? vec4(sum / weight, 1.0) : vec4(0.0);\n"
    "}\n";


// ... and the mix at the full resolution: each pixel's strength from its own distance, the
// blur from halfTex (linear, inside the region; its colour divided by its alpha leaves the
// texels without a background out), blended over the picture by the strength, less where
// the texels around are mostly foreground
// (its body: kDofHead + kDofDistance + this)
constexpr char kDofMixFs[] =
    "uniform sampler2D blurred;\nuniform sampler2D focus;\nuniform float strength;\nout vec4 o;\n"
    "void main() {\n"
    "  vec2 t = gl_FragCoord.xy;\n"
    "  vec4 f = texelFetch(focus, ivec2(0), 0);\n"
    "  if (f.b <= 0.0) discard;\n"
    "  float focal = round(f.r * 255.0) * 256.0 + round(f.g * 255.0);\n"
    "  float start = focal + max(800.0, focal * 0.25), end = focal + max(2500.0, focal);\n"
    "  float amount = strength * f.b * smoothstep(start, end, distanceAt(t));\n"
    "  if (amount <= 0.0) discard;\n"
    "  vec2 lo = region.xy + 1.0, hi = region.xy + region.zw - 1.0;\n"
    "  vec4 b = texture(blurred, clamp(t, lo, hi) / size);\n"
    "  if (b.a <= 0.0) discard;\n"
    "  o = vec4(b.rgb / b.a, amount * min(1.0, b.a * 4.0));\n"
    "}\n";

// ---- Colour and CRT (wishes of the project owner, 11.11.2026; the mock-ups of 0.99.9 in numpy, the same maths) ----

// Colour: the display copy as shown so far graded (mode 1 "Warm film": browns and golds stronger, cold colours a
// little weaker, a warm white balance in the middle and the highlights, a gentle S-curve; mode 2 "Survival horror":
// 40 % of the saturation, deeper blacks, a harder S-curve, a cold green-grey in the middle tones), mixed with the
// picture by `strength` (0.5 "Light", 1 "Strong"). Black stays black.
constexpr char kColourFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D src;\n"
    "uniform int mode;\n"
    "uniform float strength;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "float lum(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "vec3 scurve(vec3 c, float a) { return c + a * (c * c * (3.0 - 2.0 * c) - c); }\n"
    "void main() {\n"
    "  vec4 t = texture(src, vUv);\n"
    "  vec3 c = t.rgb, g;\n"
    "  float l = lum(c);\n"
    "  if (mode == 1) {\n"
    "    float warm = clamp((c.r - c.b) * 3.0, 0.0, 1.0);\n"
    "    g = vec3(l) + (c - vec3(l)) * (1.0 + 0.30 * warm - 0.15 * (1.0 - warm));\n"
    "    g *= vec3(1.0) + vec3(0.06, 0.0, -0.14) * clamp(l * 1.6, 0.0, 1.0);\n"
    "    g = clamp(scurve(clamp(g, 0.0, 1.0), 0.25), 0.0, 1.0);\n"
    "  } else {\n"
    "    g = vec3(l) + (c - vec3(l)) * 0.40;\n"
    "    g = scurve(clamp((g - 0.03) / 0.97, 0.0, 1.0), 0.45);\n"
    "    float lg = lum(g);\n"
    "    g = clamp(g + lg * (1.0 - lg) * 4.0 * vec3(-0.02, 0.015, 0.005), 0.0, 1.0);\n"
    "  }\n"
    "  o = vec4(mix(c, g, strength), t.a);\n"
    "}\n";

// CRT, first pass: the picture's game lines (one row each; at most 1280 wide), the colour of a composite signal
// bled sideways (the chroma through five taps `bleed` apart, a Gaussian of that width; the luma stays sharp)
constexpr char kCrtLineFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D src;\n"
    "uniform float bleed;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "void main() {\n"
    "  const vec3 Y = vec3(0.299, 0.587, 0.114);\n"
    "  vec3 c = texture(src, vUv).rgb;\n"
    "  vec2 d = vec2(bleed, 0.0);\n"
    "  vec3 s = 0.4 * c + 0.24 * (texture(src, vUv - d).rgb + texture(src, vUv + d).rgb) +\n"
    "           0.06 * (texture(src, vUv - 2.0 * d).rgb + texture(src, vUv + 2.0 * d).rgb);\n"
    "  o = vec4(clamp(vec3(dot(c, Y)) + s - vec3(dot(s, Y)), 0.0, 1.0), 1.0);\n"
    "}\n";

// CRT, second pass: the glow of the bright parts, a quarter of the lines each way (3 x 3 linear taps a glow texel
// apart: about eight game pixels each way), the light above 0.35 (of 0.65 at most, as its square root)
constexpr char kCrtGlowFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D src;\n"
    "uniform vec2 glowStep;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "vec3 at(vec2 d) { return max(pow(texture(src, vUv + d * glowStep).rgb, vec3(2.2)) - 0.35, 0.0); }\n"
    "void main() {\n"
    "  vec3 g = 4.0 * at(vec2(0.0)) + 2.0 * (at(vec2(-1.0, 0.0)) + at(vec2(1.0, 0.0)) + at(vec2(0.0, -1.0)) +\n"
    "           at(vec2(0.0, 1.0))) + at(vec2(-1.0)) + at(vec2(1.0)) + at(vec2(-1.0, 1.0)) + at(vec2(1.0, -1.0));\n"
    "  o = vec4(sqrt(clamp(g / (16.0 * 0.65), 0.0, 1.0)), 1.0);\n"
    "}\n";

// Bloom (0.99.13, the project owner: "Ja, einbauen"; GxCore::BloomPass): the bright parts of the scene, after its
// last 3D draw and before the HUD, glow softly over their surroundings. Passes into pictures of their own of one texel
// per 4 x 4 game pixels (`size.z` EFB texels each way: the same look and cost at every resolution), then laid over
// the EFB's colour.
// 0.99.14 (the project owner: "Am Titelbild am hellen Himmel ist er merklich sichtbar. Aber im Spiel fällt es kaum
// auf"): the game's light sources are small and the nights dark (at a fixed 0.72 only 0.03-0.6 % of a night room's
// pixels passed, 13-53 % of the title's), and the mean over 4 x 4 pixels and the blur spread a candle or a torch thin.
// Now, first, the scene's key once a picture (kBloomKeyFs, a 1 x 1 picture): its mean brightness over a 16 x 12 grid
// sets the threshold (pick: x + y * the key, clamped to z..w: a little lower in the dark, higher in bright scenes),
// and the share of the grid above it (each tap's weight, below) a budget: more than keep.y of the scene at full weight
// (the title's sky, a bright fade) scales the bloom down to keep.y / that share, not below keep.z. Out: r the
// threshold, g the scale.
constexpr char kBloomKeyFs[] =
    "#version 300 es\nprecision highp float;\nprecision highp int;\n"
    "uniform sampler2D src;\n"
    "uniform vec4 region;\n"
    "uniform vec4 pick;\n"
    "uniform vec4 keep;\n"
    "out vec4 o;\n"
    "float lum(ivec2 q) { vec3 t = texelFetch(src, q, 0).rgb; return max(t.r, max(t.g, t.b)); }\n"
    "void main() {\n"
    "  ivec2 lo = ivec2(region.xy), hi = ivec2(region.xy + region.zw) - 1;\n"
    "  vec2 cell = region.zw / vec2(16.0, 12.0);\n"
    "  float key = 0.0;\n"
    "  for (int j = 0; j < 12; j++)\n"
    "    for (int i = 0; i < 16; i++)\n"
    "      key += lum(clamp(ivec2(region.xy + (vec2(float(i), float(j)) + 0.5) * cell), lo, hi));\n"
    "  key /= 192.0;\n"
    "  float th = clamp(pick.x + pick.y * key, pick.z, pick.w);\n"
    "  float cover = 0.0;\n"
    "  for (int j = 0; j < 12; j++)\n"
    "    for (int i = 0; i < 16; i++) {\n"
    "      float w = clamp((lum(clamp(ivec2(region.xy + (vec2(float(i), float(j)) + 0.5) * cell), lo, hi)) - th) /\n"
    "                      (1.0 - th), 0.0, 1.0);\n"
    "      cover += pow(w, keep.w);\n"
    "    }\n"
    "  cover /= 192.0;\n"
    "  float scale = cover > keep.y ? max(keep.z, keep.y / cover) : 1.0;\n"
    "  o = vec4(th, scale, key, 1.0);\n"
    "}\n";

// Then each bloom texel from its size.z x size.z EFB texels (size.w x size.w taps spread over them, one a game pixel,
// from `region`, the scene's part of the EFB in its texels): what lies above the key's threshold (by the brightest of
// the three channels, from 0 at it to the colour itself at white as the way there to the power keep.w (0.99.13: the
// square; since 0.99.14 the fourth: a flame near white glows fully, a lit plank, hair or fog a little above it hardly),
// each tap on its own; the texel takes keep.x of its brightest tap and the rest of their mean (0.99.14: a candle of one
// or two game pixels keeps more than a sixteenth of its light; a large bright area is the same either way), times the
// key's scale. Where the lens flares draw
// a halo at a light (halo: x, y, radius in EFB texels, w 1 while it is there) the light gives no bloom: the two never
// glow at the same place. GL's and Vulkan's texels and fragments run the same way in each (row 0 = the EFB's row 0).
constexpr char kBloomPickFs[] =
    "#version 300 es\nprecision highp float;\nprecision highp int;\n"
    "uniform sampler2D src;\n"
    "uniform sampler2D key;\n"
    "uniform vec4 region;\n"
    "uniform vec4 size;\n"
    "uniform vec4 keep;\n"
    "uniform vec4 halo[8];\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec4 k = texelFetch(key, ivec2(0), 0);\n"
    "  float th = k.r, inv = 1.0 / max(1.0 - th, 0.02);\n"
    "  vec2 p = region.xy + floor(gl_FragCoord.xy) * size.z;\n"
    "  ivec2 lo = ivec2(region.xy), hi = ivec2(region.xy + region.zw) - 1;\n"
    "  int n = int(size.w);\n"
    "  float d = size.z / size.w;\n"
    "  vec3 c = vec3(0.0), m = vec3(0.0);\n"
    "  for (int j = 0; j < 4; j++) {\n"
    "    if (j >= n) break;\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "      if (i >= n) break;\n"
    "      ivec2 q = clamp(ivec2(p + (vec2(float(i), float(j)) + 0.5) * d), lo, hi);\n"
    "      vec3 t = texelFetch(src, q, 0).rgb;\n"
    "      float w = pow(clamp((max(t.r, max(t.g, t.b)) - th) * inv, 0.0, 1.0), keep.w);\n"
    "      c += t * w;\n"
    "      m = max(m, t * w);\n"
    "    }\n"
    "  }\n"
    "  vec3 g = mix(c / float(n * n), m, keep.x) * k.g;\n"
    "  vec2 at = p + 0.5 * size.z;\n"
    "  for (int h = 0; h < 8; h++) {\n"
    "    if (halo[h].w > 0.5) g *= smoothstep(0.55, 1.0, length(at - halo[h].xy) / max(halo[h].z, 1.0));\n"
    "  }\n"
    "  o = vec4(g, 1.0);\n"
    "}\n";

// Bloom, second: a soft blur along `dir.xy` (a texel of the bloom picture in its coordinates, times the spread), 9
// texels of a Gaussian in 5 linear taps; `dir.zw` the picture's size.
constexpr char kBloomBlurFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D src;\n"
    "uniform vec4 dir;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec2 uv = gl_FragCoord.xy / dir.zw;\n"
    "  vec3 c = texture(src, uv).rgb * 0.2270270270;\n"
    "  c += (texture(src, uv + dir.xy * 1.3846153846).rgb + texture(src, uv - dir.xy * 1.3846153846).rgb) * 0.3162162162;\n"
    "  c += (texture(src, uv + dir.xy * 3.2307692308).rgb + texture(src, uv - dir.xy * 3.2307692308).rgb) * 0.0702702703;\n"
    "  o = vec4(c, 1.0);\n"
    "}\n";

// Bloom, third: the glow laid over the EFB (the colour only) over the scene's part (`region`, its texels; the scissor
// keeps the rest, the letterbox bars stay black); since 0.99.14 as a screen (one + one minus the glow: what is
// already bright gains little, a flame keeps its drawing instead of turning into a white blot; until then added one +
// one): the wide glow (`bloom`, after all blurs) times
// size.w and, since 0.99.14, the narrow one (`core`, after the first blur: near a small light it is still bright)
// times core.x; size.xy the bloom picture's texels, size.z the EFB texels a bloom texel spans.
constexpr char kBloomAddFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D bloom;\n"
    "uniform sampler2D core;\n"
    "uniform vec4 region;\n"
    "uniform vec4 size;\n"
    "uniform vec4 glow;\n"
    "out vec4 o;\n"
    "void main() {\n"
    "  vec2 t = (gl_FragCoord.xy - region.xy) / size.z / size.xy;\n"
    "  o = vec4(texture(bloom, t).rgb * size.w + texture(core, t).rgb * glow.x, 0.0);\n"
    "}\n";

// --- real-time shadows of the figures and objects (GxCore::ShadowMark) ----------------------------------------------
// The casters' triangles (view space, three floats a corner) from the light: only their depth (the map's clip space:
// the rows lx, ly, lz of the light's affine matrix; as rows, since the Vulkan renderer's constants have no mat4).
// glz.x: 1 GL's clip depth (-w..w), 0 Vulkan's (0..w); the map holds the same 0..1 depth either way.
// (stage 6: four rows, lw the clip w: a point light's map is a perspective one; GL's clip depth -w..w, Vulkan's 0..w)
constexpr char kShadowDepthVs[] = "#version 300 es\n"
                                  "layout(location = 0) in vec3 aPos;\n"
                                  "uniform vec4 lx;\n"
                                  "uniform vec4 ly;\n"
                                  "uniform vec4 lz;\n"
                                  "uniform vec4 lw;\n"
                                  "uniform vec4 glz;\n"
                                  "void main() {\n"
                                  "  vec4 p = vec4(aPos, 1.0);\n"
                                  "  vec4 c = vec4(dot(lx, p), dot(ly, p), dot(lz, p), dot(lw, p));\n"
                                  "  c.z = mix(0.5 * c.z + 0.5 * c.w, c.z, glz.x);\n"
                                  "  gl_Position = c;\n"
                                  "}\n";
constexpr char kShadowDepthFs[] = "#version 300 es\nprecision mediump float;\nvoid main() {}\n";
// The same triangles with the scene's camera, as the GX vertex shader places them (gx_gl_shader.cpp: XF viewport and
// projection, the pixel centre's twelfth), tested against the EFB's depth: `mark` where they are the visible surface
// (red the figures, green the objects; blended by the larger, so both stay): kShadowMaskFs leaves a figure's own
// pixels as they are and gives an object's only the figures' shadows, so that nothing stripes itself.
constexpr char kShadowMarkVs[] =
    "#version 300 es\n"
    "layout(location = 0) in vec3 aPos;\n"
    "uniform vec4 vp0;\n"  // xs ys zs ox
    "uniform vec4 vp1;\n"  // oy zo W H
    "uniform vec4 pr0;\n"  // p0 p1 p2 p3
    "uniform vec4 pr1;\n"  // p4 p5
    "uniform vec4 glz;\n"  // x: 1 GL's clip depth, 0 Vulkan's
    "void main() {\n"
    "  vec4 clip = vec4(pr0.x * aPos.x + pr0.y * aPos.z, pr0.z * aPos.y + pr0.w * aPos.z, pr1.x * aPos.z + pr1.y, -aPos.z);\n"
    "  float W = vp1.z, H = vp1.w;\n"
    "  gl_Position = vec4(((vp0.w - 342.0) * 2.0 / W - 1.0) * clip.w + vp0.x * 2.0 / W * clip.x,\n"
    "                     ((vp1.x - 342.0) * 2.0 / H - 1.0) * clip.w + vp0.y * 2.0 / H * clip.y,\n"
    "                     0.0, clip.w);\n"
    "  float z01 = (vp0.z * clip.z + vp1.y * clip.w) / 16777215.0;\n"
    "  gl_Position.z = mix(z01, 2.0 * z01 - clip.w, glz.x);\n"
    "  gl_Position.xy -= gl_Position.w * vec2(1.0 / (6.0 * W), 1.0 / (6.0 * H));\n"
    "}\n";
constexpr char kShadowMarkFs[] = "#version 300 es\nprecision mediump float;\nuniform vec4 mark;\nout vec4 o;\n"
                                 "void main() { o = mark; }\n";
// One tile's darkening (0 lit .. 1 in full shadow) at the view-space surface e with its normal n (stage 5: the maps
// share one depth atlas of 2 x 2 tiles; T.xy the tile's corner in the atlas, 0 or 0.5). Stage 6: a tile is a parallel
// map or a point light's perspective one (TP.z 1; rows LX LY LZ LW, TP near, far along its axis, a texel's size per
// unit of distance). e moved along n (B.z texels) into the tile's map, compared at 8 taps (B.w texels apart; each a 2 x 2
// comparison of the hardware), every tap kept inside the tile, the depth bias B.x + B.y slope texels. The shadow
// reaches T.z view units behind the caster the map holds there (raw: the atlas read without comparing): the room
// itself casts nothing, so a shadow must not fall through a wall onto what lies far behind it. (A macro: no sampler
// goes through a function's parameters, the Vulkan renderer's converter binds them by their declarations.)
#define RE4_SHADOW_OCC(NAME, LX, LY, LZ, LW, B, T, TP)                                                              \
  "float " NAME "(vec3 e, vec3 n, float slope, inout float rat) {\n"                                                \
  "  bool persp = " TP ".z > 0.5;\n"                                                                                \
  "  float w0 = dot(" LW ", vec4(e, 1.0));\n"                                                                       \
  "  if (persp && w0 <= " TP ".x) return 0.0;\n"                                                                    \
  "  float texel = persp ? w0 * " TP ".w : " TP ".w;\n"                                                             \
  "  vec4 p = vec4(e + n * (" B ".z * texel), 1.0);\n"                                                              \
  "  vec4 c4 = vec4(dot(" LX ", p), dot(" LY ", p), dot(" LZ ", p), dot(" LW ", p));\n"                             \
  "  if (c4.w <= 1e-3) return 0.0;\n"                                                                               \
  "  vec3 c = c4.xyz / c4.w * 0.5 + 0.5;\n"                                                                         \
  "  if (any(lessThan(c, vec3(0.0))) || any(greaterThan(c, vec3(1.0)))) return 0.0;\n"                              \
  "  vec2 lo = " T ".xy + vec2(look.w), hi = " T ".xy + vec2(0.5 - look.w);\n"                                    \
  "  vec2 a = " T ".xy + c.xy * 0.5;\n"                                                                           \
  "  float zn = " TP ".x, zf = " TP ".y;\n"                                                                         \
  "  float raw0 = texture(raw, clamp(a, lo, hi)).r;\n"                                                              \
  "  float dRec = persp ? c4.w : zn + c.z * (zf - zn);\n"                                                           \
  "  float dCas = persp ? zn * zf / (zf - raw0 * (zf - zn)) : zn + raw0 * (zf - zn);\n"                             \
  "  float reach = 1.0 - smoothstep(0.75 * " T ".z, " T ".z, dRec - dCas);\n"                                       \
  "  if (reach <= 0.0) return 0.0;\n"                                                                             \
  "  float perUnit = persp ? zn * zf / ((zf - zn) * dRec * dRec) : 1.0 / (zf - zn);\n"                              \
  "  float ref = c.z - (" B ".x + " B ".y * slope) * texel * perUnit;\n"                                            \
  "  vec2 r = vec2(" B ".w * 2.0 * look.w);\n"                                                                    \
  "  float lit = texture(shd, vec3(clamp(a + vec2(-0.326, -0.406) * r, lo, hi), ref));\n"                          \
  "  lit += texture(shd, vec3(clamp(a + vec2(-0.840, -0.074) * r, lo, hi), ref));\n"                               \
  "  lit += texture(shd, vec3(clamp(a + vec2(-0.696, 0.457) * r, lo, hi), ref));\n"                                \
  "  lit += texture(shd, vec3(clamp(a + vec2(-0.203, 0.621) * r, lo, hi), ref));\n"                                \
  "  lit += texture(shd, vec3(clamp(a + vec2(0.962, -0.195) * r, lo, hi), ref));\n"                                \
  "  lit += texture(shd, vec3(clamp(a + vec2(0.473, -0.480) * r, lo, hi), ref));\n"                                \
  "  lit += texture(shd, vec3(clamp(a + vec2(0.519, 0.767) * r, lo, hi), ref));\n"                                 \
  "  lit += texture(shd, vec3(clamp(a + vec2(0.185, -0.893) * r, lo, hi), ref));\n"                                \
  "  float o = (1.0 - lit * 0.125) * reach;\n"                                                                      \
  "  if (persp && o > 0.0) {\n"                                                                                     \
  "    float rawNear = min(raw0, min(min(texture(raw, clamp(a + vec2(-0.326, -0.406) * r, lo, hi)).r,"              \
  "                                   texture(raw, clamp(a + vec2(0.962, -0.195) * r, lo, hi)).r),"                 \
  "                               min(texture(raw, clamp(a + vec2(-0.203, 0.621) * r, lo, hi)).r,"                  \
  "                                   texture(raw, clamp(a + vec2(0.473, -0.480) * r, lo, hi)).r)));\n"             \
  "    rat = min(rat, zn * zf / (zf - rawNear * (zf - zn)) / max(dRec, 1.0));\n"                                    \
  "  }\n"                                                                                                           \
  "  return o;\n"                                                                                                   \
  "}\n"
// One light's darkening at e: its weight (W; its light C here, all but the distance fall-off KEEP, the distance
// DIST, the attenuation LB), its tile(s) where the surface faces it (TOL: towards the light); a point light's or a
// spot's weight again with its fall-off at the caster (DIST times rat), and less where its shadow falls more than
// 1.7 times as large as the caster (none from 3.3 times: a real lamp is no point; stage 7)
#define RE4_SHADOW_LIGHT(W, TOL, C, KEEP, DIST, LB, OCC_EXPR)                                                       \
  "  f = dot(n, " TOL ");\n"                                                                                        \
  "  if ((" W " > 0.0 || " KEEP " > 0.0) && f > 0.0) {\n"                                                           \
  "    float s = sqrt(max(0.0, 1.0 - f * f)) / max(f, 0.1);\n"                                                     \
  "    float occ = 0.0, rat = 1.0;\n" OCC_EXPR                                                                      \
  "    float w = " W ";\n"                                                                                          \
  "    if (occ > 0.0 && rat < 1.0 && " DIST " > 0.0) {\n"                                                           \
  "      float dc = " DIST " * rat;\n"                                                                              \
  "      float cc = " KEEP " / (1.0 + " LB ".x * dc + " LB ".y * dc * dc);\n"                                       \
  "      float tc = total - max(" C ", 0.0) + cc;\n"                                                                \
  "      w = min(amb.y, cc / max(tc, 1e-4)) * clamp((tc - 0.15) / 0.75, 0.0, 1.0);\n"                                \
  "    }\n"                                                                                                         \
  "    if (" DIST " > 0.0) w *= smoothstep(0.3, 0.6, rat);\n"                                                       \
  "    dark += w * occ * smoothstep(0.0, 0.3, f);\n"                                                                \
  "  }\n"
// The shadow mask, a texel per `grid` EFB texels of the scene's part (region): each texel's surface from the EFB
// depth back into view space (the XF viewport and projection as the vertex shader uses them; vp1.z texels per
// GameCube pixel), its normal from the nearer neighbours. Up to three lights (from the game core): lpN the place of
// a point or spot light or the direction a parallel one travels (view space; w the kind: 0 parallel, 1 point, 2
// spot), laN its colour's luminance (negative: a fixed weight, the marks of stages 1 to 5), strength, full up to and
// gone from (view units), lbN GX attenuation k1, k2 and a spot's cut-off cosine, lcN a spot's axis. Stage 6
// (16.11.2026): how much light each one gives (LightAt, trans_lit.cpp's fall-off) against all of it here (amb.x the
// light that is the same everywhere) is how much its shadow takes away here, less in the dark; nothing depends on
// where the player stands. A point light's or a spot's fall-off counts at the caster (its distance from the light
// along the same ray, from the map: Occ's rat), the fade at its reach and a spot's cone here: so a figure's shadow
// is as dark as the light on it, as at the player in stage 5, also where it falls further away. Light 0 casts from
// the figures' tile t0 and the objects' t1, lights 1 and 2 from the figures' t2, t3. The darkening adds up (where
// two lights are blocked the shadow is darker: the core shadow; where one, the half shadow), up to look.x. It fades
// from look.y to look.z view units away. Where a figure is the visible surface (fig.r) nothing darkens: the game
// lights its figures with lights of their own, and a figure shading itself striped it (r30f). Where an object is
// (fig.g) only the figures' shadows fall. 1 where lit; the sky (depth 1) stays lit. Stage 7 (18.11.2026, the owner's
// device test: some shadows too dark, others flickering wildly): the game's lights darken at most amb.y (0.6) each
// and look.x (0.7) together; a lamp's shadow fades where it falls 1.7 to 3.3 times as large as its caster (a real
// lamp is no point: a figure beside a candle lamp threw a giant shadow that swam with every breath); the caster's
// distance is the nearest of five of the map's samples, so that an edge does not switch between the caster's light
// and the floor's.
constexpr char kShadowMaskFs[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp sampler2DShadow;\n"
    "uniform highp sampler2D src;\n"
    "uniform highp sampler2DShadow shd;\n"
    "uniform highp sampler2D fig;\n"
    "uniform highp sampler2D raw;\n"
    "uniform vec4 region;\n"   // x y w h (EFB texels)
    "uniform vec4 grid;\n"     // EFB texels per mask texel (x, y)
    "uniform vec4 vp0;\n"
    "uniform vec4 vp1;\n"
    "uniform vec4 pr0;\n"
    "uniform vec4 pr1;\n"
    "uniform vec4 look;\n"     // the darkest the mask gets (1 - x), fade start, fade end (view units), half an atlas texel
    "uniform vec4 amb;\n"      // the figures' ambient (luminance)
    "uniform vec4 lx0;\n"
    "uniform vec4 ly0;\n"
    "uniform vec4 lz0;\n"
    "uniform vec4 lw0;\n"
    "uniform vec4 b0;\n"
    "uniform vec4 t0;\n"
    "uniform vec4 tp0;\n"
    "uniform vec4 lx1;\n"
    "uniform vec4 ly1;\n"
    "uniform vec4 lz1;\n"
    "uniform vec4 lw1;\n"
    "uniform vec4 b1;\n"
    "uniform vec4 t1;\n"
    "uniform vec4 tp1;\n"
    "uniform vec4 lx2;\n"
    "uniform vec4 ly2;\n"
    "uniform vec4 lz2;\n"
    "uniform vec4 lw2;\n"
    "uniform vec4 b2;\n"
    "uniform vec4 t2;\n"
    "uniform vec4 tp2;\n"
    "uniform vec4 lx3;\n"
    "uniform vec4 ly3;\n"
    "uniform vec4 lz3;\n"
    "uniform vec4 lw3;\n"
    "uniform vec4 b3;\n"
    "uniform vec4 t3;\n"
    "uniform vec4 tp3;\n"
    "uniform vec4 lp0;\n"
    "uniform vec4 la0;\n"
    "uniform vec4 lb0;\n"
    "uniform vec4 lc0;\n"
    "uniform vec4 lp1;\n"
    "uniform vec4 la1;\n"
    "uniform vec4 lb1;\n"
    "uniform vec4 lc1;\n"
    "uniform vec4 lp2;\n"
    "uniform vec4 la2;\n"
    "uniform vec4 lb2;\n"
    "uniform vec4 lc2;\n"
    "out vec4 o;\n"
    "vec3 ViewAt(vec2 fc, out float d) {\n"
    "  d = texelFetch(src, ivec2(fc), 0).r;\n"
    "  vec2 gc = fc / vp1.z;\n"
    "  float zz = (d * 16777215.0 - vp1.y) / vp0.z;\n"
    "  float dist = pr1.y / (zz + pr1.x);\n"
    "  vec2 ndc = vec2((gc.x - (vp0.w - 342.0)) / vp0.x, (gc.y - (vp1.x - 342.0)) / vp0.y);\n"
    "  return vec3((ndc.x + pr0.y) * dist / pr0.x, (ndc.y + pr0.w) * dist / pr0.z, -dist);\n"
    "}\n"
    // the light one light gives at e (its colour's luminance times its fall-off there), the way towards it, its
    // distance (0: parallel) and the light without the distance fall-off (the fade at its reach, a spot's cone)
    "float LightAt(vec4 lp, vec4 la, vec4 lb, vec4 lc, vec3 e, out vec3 toL, out float dist, out float keep) {\n"
    "  if (lp.w < 0.5) { toL = -lp.xyz; dist = 0.0; keep = la.x * la.y; return keep; }\n"
    "  vec3 v = lp.xyz - e;\n"
    "  float d = length(v);\n"
    "  toL = v / max(d, 1.0);\n"
    "  dist = d;\n"
    "  float fade = d <= la.z ? 1.0 : (d >= la.w ? 0.0 : (la.w - d) / max(la.w - la.z, 1.0));\n"
    "  keep = la.y * fade;\n"
    "  if (lp.w > 1.5) keep *= clamp((dot(-toL, lc.xyz) - lb.z) / max(1.0 - lb.z, 1e-4), 0.0, 1.0);\n"
    "  keep = la.x * max(keep, 0.0);\n"
    "  return keep / (1.0 + lb.x * d + lb.y * d * d);\n"
    "}\n" RE4_SHADOW_OCC("Occ0", "lx0", "ly0", "lz0", "lw0", "b0", "t0", "tp0")
        RE4_SHADOW_OCC("Occ1", "lx1", "ly1", "lz1", "lw1", "b1", "t1", "tp1")
        RE4_SHADOW_OCC("Occ2", "lx2", "ly2", "lz2", "lw2", "b2", "t2", "tp2")
        RE4_SHADOW_OCC("Occ3", "lx3", "ly3", "lz3", "lw3", "b3", "t3", "tp3")
    "void main() {\n"
    "  o = vec4(1.0);\n"
    "  vec2 fc = region.xy + floor((gl_FragCoord.xy - 0.5) * grid.xy + 0.5 * grid.xy) + 0.5;\n"
    "  float d, d1, d2, d3, d4;\n"
    "  vec3 e = ViewAt(fc, d);\n"
    "  vec2 mk = texelFetch(fig, ivec2(fc), 0).rg;\n"
    "  if (d >= 0.9999995 || mk.r > 0.5) return;\n"
    "  float fade = 1.0 - smoothstep(look.y, look.z, -e.z);\n"
    "  if (fade <= 0.0) return;\n"
    "  vec3 ex1 = ViewAt(fc + vec2(1.0, 0.0), d1), ex0 = ViewAt(fc - vec2(1.0, 0.0), d2);\n"
    "  vec3 ey1 = ViewAt(fc + vec2(0.0, 1.0), d3), ey0 = ViewAt(fc - vec2(0.0, 1.0), d4);\n"
    "  vec3 dx = abs(ex1.z - e.z) < abs(e.z - ex0.z) ? ex1 - e : e - ex0;\n"
    "  vec3 dy = abs(ey1.z - e.z) < abs(e.z - ey0.z) ? ey1 - e : e - ey0;\n"
    "  vec3 n = cross(dx, dy);\n"
    "  if (dot(n, n) < 1e-12) return;\n"
    "  n = normalize(n);\n"
    "  if (dot(n, e) > 0.0) n = -n;\n"
    // each light's share of the light here (all of it: the ambient and the three), less in the dark (none below 0.15
    // of a white light, full from 0.9)
    "  vec3 to0, to1, to2;\n"
    "  float dl0, dl1, dl2, kl0, kl1, kl2;\n"
    "  float c0 = LightAt(lp0, la0, lb0, lc0, e, to0, dl0, kl0), c1 = LightAt(lp1, la1, lb1, lc1, e, to1, dl1, kl1);\n"
    "  float c2 = LightAt(lp2, la2, lb2, lc2, e, to2, dl2, kl2);\n"
    "  float total = amb.x + max(c0, 0.0) + max(c1, 0.0) + max(c2, 0.0);\n"
    "  float lit = clamp((total - 0.15) / 0.75, 0.0, 1.0);\n"
    "  float w0 = la0.x < 0.0 ? -la0.x : min(amb.y, c0 / max(total, 1e-4)) * lit;\n"
    "  float w1 = la1.x < 0.0 ? -la1.x : min(amb.y, c1 / max(total, 1e-4)) * lit;\n"
    "  float w2 = la2.x < 0.0 ? -la2.x : min(amb.y, c2 / max(total, 1e-4)) * lit;\n"
    "  float dark = 0.0, f;\n"
    RE4_SHADOW_LIGHT("w0", "to0", "c0", "kl0", "dl0", "lb0",
                     "    if (t0.w > 0.5) occ = Occ0(e, n, s, rat);\n"
                     "    if (t1.w > 0.5 && mk.g < 0.5) occ = max(occ, Occ1(e, n, s, rat));\n")
    RE4_SHADOW_LIGHT("w1", "to1", "c1", "kl1", "dl1", "lb1", "    if (t2.w > 0.5) occ = Occ2(e, n, s, rat);\n")
    RE4_SHADOW_LIGHT("w2", "to2", "c2", "kl2", "dl2", "lb2", "    if (t3.w > 0.5) occ = Occ3(e, n, s, rat);\n")
    "  o = vec4(vec3(1.0 - min(dark * fade, look.x)), 1.0);\n"
    "}\n";
#undef RE4_SHADOW_OCC
#undef RE4_SHADOW_LIGHT
// The mask times the EFB's colour (blending: destination colour, zero; alpha not written), smoothed from its texels.
constexpr char kShadowApplyFs[] = "#version 300 es\n"
                                  "precision mediump float;\n"
                                  "uniform sampler2D mask;\n"
                                  "uniform vec4 region;\n"
                                  "out vec4 o;\n"
                                  "void main() {\n"
                                  "  float m = texture(mask, (gl_FragCoord.xy - region.xy) / region.zw).r;\n"
                                  "  o = vec4(m, m, m, 1.0);\n"
                                  "}\n";

// CRT, the screen (instead of kPresentFs): the box's picture through a curved glass (shape.xy) with rounded
// corners (shape.z, in box heights) and darker edges (shape.w); each game line a beam (beam.xy: its width, wider
// where bright; beam.z: the brightness the gaps take), the glow on top (beam.w); the phosphors' mask in screen
// pixels (mask.x: 0 none, 1 stripes, 2 the shifted dots of a shadow mask; mask.y its strength). Below three screen
// pixels a line the lines fade out (a fine screen would show stripes). rows: the copy's rows shown (as kPresentFs),
// lines: its game lines, box: the box in screen pixels. Row 0 of the picture is at the top: "1.0 - vUv.y" is
// GL's turn (the Vulkan renderer takes vUv.y).
constexpr char kCrtFs[] =
    "#version 300 es\nprecision highp float;\n"
    "uniform sampler2D crtLine;\n"
    "uniform sampler2D crtGlow;\n"
    "uniform vec2 rows;\n"
    "uniform float lines;\n"
    "uniform vec2 box;\n"
    "uniform vec4 shape;\n"
    "uniform vec4 beam;\n"
    "uniform vec2 mask;\n"
    "in vec2 vUv;\nout vec4 o;\n"
    "vec3 lineAt(float x, float l) { return pow(texture(crtLine, vec2(x, clamp((l + 0.5) / lines, 0.0, 1.0))).rgb, vec3(2.2)); }\n"
    "vec3 beamOf(vec3 c, float d) {\n"
    "  float s = mix(beam.x, beam.y, sqrt(clamp(max(c.r, max(c.g, c.b)), 0.0, 1.0)));\n"
    "  return c * exp(-0.5 * (d / s) * (d / s));\n"
    "}\n"
    "void main() {\n"
    "  vec2 uv = vec2(vUv.x, 1.0 - vUv.y);\n"
    "  vec2 p = uv * 2.0 - 1.0;\n"
    "  vec2 w = (vec2(p.x * (1.0 + p.y * p.y * shape.x), p.y * (1.0 + p.x * p.x * shape.y)) + 1.0) * 0.5;\n"
    "  if (w.x < 0.0 || w.x > 1.0 || w.y < 0.0 || w.y > 1.0) { o = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
    "  vec2 a = vec2(box.x / box.y, 1.0);\n"
    "  vec2 e = max(abs(w - 0.5) * a - (a * 0.5 - shape.z), 0.0);\n"
    "  float corner = clamp(0.5 - (length(e) - shape.z) * box.y / 1.5, 0.0, 1.0);\n"
    "  float v = rows.x + w.y * rows.y;\n"
    "  float ly = v * lines - 0.5;\n"
    "  float l0 = floor(ly), f = ly - l0;\n"
    "  vec3 c0 = lineAt(w.x, l0), c1 = lineAt(w.x, l0 + 1.0);\n"
    "  vec3 scan = (beamOf(c0, f) + beamOf(c1, 1.0 - f)) * beam.z;\n"
    "  vec3 col = mix(mix(c0, c1, f), scan, clamp((box.y / (lines * rows.y) - 1.5) / 1.5, 0.0, 1.0));\n"
    "  vec3 g = texture(crtGlow, vec2(w.x, v)).rgb;\n"
    "  col += g * g * (0.65 * beam.w);\n"
    "  if (mask.x > 0.5) {\n"
    "    vec2 px = floor(uv * box);\n"
    "    float ch = mask.x > 1.5 ? mod(px.x + mod(floor(px.y * 0.5), 2.0) * 2.0, 3.0) : mod(px.x, 3.0);\n"
    "    vec3 m = vec3(1.0 - mask.y);\n"
    "    if (ch < 0.5) m.r = 1.0 + 0.6 * mask.y; else if (ch < 1.5) m.g = 1.0 + 0.6 * mask.y; else m.b = 1.0 + 0.6 * mask.y;\n"
    "    col *= m;\n"
    "  }\n"
    "  col *= pow(clamp(16.0 * w.x * (1.0 - w.x) * w.y * (1.0 - w.y), 0.0, 1.0), shape.w);\n"
    "  o = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2)) * corner, 1.0);\n"
    "}\n";

// the Vulkan screen's CRT (as kSwapVs): the box in clip space (y down) turned as the display
constexpr char kCrtSwapVs[] = "#version 300 es\n"
                              "uniform vec4 rect;\n"
                              "uniform vec4 rot;\n"
                              "out vec2 vUv;\n"
                              "void main() {\n"
                              "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                              "  vUv = p;\n"
                              "  vec2 q = mix(rect.xy, rect.zw, p);\n"
                              "  gl_Position = vec4(rot.x * q.x + rot.y * q.y, rot.z * q.x + rot.w * q.y, 0.0, 1.0);\n"
                              "}\n";

// the CRT's two levels (the menu's "Light" and "Strong"; the mock-ups' values): the glass's curve (x, y), the
// corners' radius (box heights), the edges' darkening, a beam's width (dark, bright; lines), the brightness for
// the gaps, the glow, the mask (0 none, 2 shadow mask) and its strength, the colour bleed (game pixels)
struct CrtPreset {
  float warpX, warpY, corner, vignette, sigmaDark, sigmaBright, boost, glow, maskKind, maskStrength, bleed;
};
constexpr CrtPreset kCrtPresets[2] = {
    {1.0f / 48.0f, 1.0f / 36.0f, 0.025f, 0.22f, 0.34f, 0.55f, 1.18f, 0.18f, 0.0f, 0.0f, 1.2f},
    {1.0f / 24.0f, 1.0f / 18.0f, 0.055f, 0.38f, 0.24f, 0.42f, 1.42f, 0.30f, 2.0f, 0.30f, 2.4f}};

} // namespace fixed
} // namespace re4gx
