// GLSL ES 1.00 from the GX state (gx_gl2_shader.h). The arithmetic is the main app's
// (gx_gl_shader.cpp, after Dolphin's shader generators) in floats: every TEV value is a whole
// number held in a float, `x & 255` becomes x - 256 floor(x / 256), shifts become products and
// floors, the lerp splits into a + (b - a) c' / 256 so the operands stay small. With highp the
// results equal the GLES 3 renderer's; with the Tegra 3's mediump (13 bits) the lerp's rounding
// and the fog mix are approximate.
#include "gx_gl2_shader.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace re4gx2 {
namespace {

// Re4GxLayout (re4_gx_frame.h)
constexpr std::uint32_t kNormal = 1u << 0, kNbt = 1u << 1, kColor0 = 1u << 2, kColor1 = 1u << 3, kTex0 = 1u << 4,
                        kCompact = 1u << 13;

void Add(std::string &out, const char *format, ...) __attribute__((format(printf, 2, 3)));
void Add(std::string &out, const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  out += buffer;
}

bool Has(const ShaderKey &k, std::uint32_t bit) { return (k.layout & bit) != 0; }

const char kXyzw[] = "xyzw";

unsigned Popcount(std::uint32_t v) {
  unsigned n = 0;
  for (; v; v &= v - 1)
    n++;
  return n;
}

// texgen i: type 0 regular (and 4..7 as regular), 1 emboss, 2/3 colour
unsigned GenType(std::uint32_t g) { return (g >> 4) & 7; }
bool Regular(std::uint32_t g) { return GenType(g) != 1 && GenType(g) != 2 && GenType(g) != 3; }
unsigned GenRow(std::uint32_t g) { return (g >> 7) & 31; }

// lighting channel c (0/1) colour or alpha: lit
bool Lit(const ShaderKey &k, unsigned c, bool alpha) {
  return c < k.numChans && c < 2 && (k.chan[c + (alpha ? 2 : 0)] & 2);
}

std::uint32_t LightMask(std::uint32_t c) { return ((c >> 2) & 15) | (((c >> 11) & 15) << 4); }

// The expression of matrix slot `slot` (0 position, 1 + n texture n): from the vertex or the
// draw's defaults.
std::string Slot(const ShaderKey &k, unsigned slot) {
  char buffer[32];
  if (k.mtxSlots & (1u << slot)) {
    const unsigned j = Popcount(k.mtxSlots & ((1u << slot) - 1));
    std::snprintf(buffer, sizeof(buffer), "aMtx%u.%c", j / 4, kXyzw[j % 4]);
  } else {
    std::snprintf(buffer, sizeof(buffer), "uMtxDef[%u].%c", slot / 4, kXyzw[slot % 4]);
  }
  return buffer;
}

// the dequantisation factor of a compact attribute (uDequant: pos, nrm, tex0, tex1 | tex2-5 | tex6, 7)
std::string Dequant(int source) {
  int i = source == kAttrPos ? 0 : (source == kAttrNrm || source == kAttrBin || source == kAttrTan) ? 1
                                                                                                  : 2 + (source - kAttrTex0);
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "uDequant[%d].%c", i / 4, kXyzw[i % 4]);
  return buffer;
}

} // namespace

const char *GxAttributeName(int source) {
  static const char *names[kAttrSources] = {"aPos",  "aNrm",  "aBin",  "aTan",  "aClr0", "aClr1", "aTex0",
                                            "aTex1", "aTex2", "aTex3", "aTex4", "aTex5", "aTex6", "aTex7",
                                            "aMtx0", "aMtx1", "aMtx2"};
  return source >= 0 && source < kAttrSources ? names[source] : "?";
}

ShaderInfo GxShaderInfo(const ShaderKey &k) {
  ShaderInfo s{};
  s.compact = Has(k, kCompact);
  const bool hasNrm = Has(k, kNormal), hasNbt = hasNrm && Has(k, kNbt);
  bool lit = false;
  for (unsigned c = 0; c < 2; c++)
    lit = lit || Lit(k, c, false) || Lit(k, c, true);
  bool needNrm = hasNrm && lit, needNbt = false, needClr[2] = {false, false}, needTex[8] = {};
  bool normalMatrix = hasNrm && lit;
  std::uint32_t slots = 1; // the position matrix
  for (unsigned i = 0; i < k.numTexGens; i++) {
    const std::uint32_t g = k.texGen[i];
    if (GenType(g) == 1) {
      const unsigned source = (g >> 12) & 7, light = (g >> 15) & 7;
      if (source < i && hasNbt) {
        needNbt = true;
        normalMatrix = true;
        s.lights |= 1u << light;
      }
      continue;
    }
    if (!Regular(g))
      continue;
    slots |= 2u << i;
    const unsigned row = GenRow(g);
    if (row == 1 && hasNrm)
      needNrm = true;
    else if ((row == 3 || row == 4) && hasNbt)
      needNbt = true;
    else if (row >= 5 && row <= 12 && Has(k, kTex0 << (row - 5)))
      needTex[row - 5] = true;
  }
  // colours: what the lighting channels read of the vertex (gx_gl_shader.cpp Light), and the
  // channels taken as they are
  for (unsigned c = 0; c < 2; c++) {
    if (!Has(k, c == 0 ? kColor0 : kColor1))
      continue;
    if (c >= k.numChans) {
      needClr[c] = true;
      continue;
    }
    for (int alpha = 0; alpha < 2; alpha++) {
      const std::uint32_t ctrl = k.chan[c + 2 * alpha];
      if ((ctrl & 1) || ((ctrl & 2) && (ctrl & 64)))
        needClr[c] = true;
    }
  }
  for (unsigned c = 0; c < 2; c++)
    for (int alpha = 0; alpha < 2; alpha++)
      if (Lit(k, c, alpha))
        s.lights |= LightMask(k.chan[c + 2 * alpha]);
  // attributes, in location order
  s.attribs[s.attribCount++] = kAttrPos;
  if (needNrm || normalMatrix)
    s.attribs[s.attribCount++] = kAttrNrm;
  if (needNbt) {
    s.attribs[s.attribCount++] = kAttrBin;
    s.attribs[s.attribCount++] = kAttrTan;
  }
  for (unsigned c = 0; c < 2; c++)
    if (needClr[c])
      s.attribs[s.attribCount++] = kAttrClr0 + static_cast<int>(c);
  for (unsigned t = 0; t < 8; t++)
    if (needTex[t])
      s.attribs[s.attribCount++] = kAttrTex0 + static_cast<int>(t);
  unsigned mtxWords = 0;
  for (unsigned slot = 0; slot < 9; slot++) {
    if (!(slots & (1u << slot)))
      continue;
    if (k.mtxSlots & (1u << slot)) {
      const unsigned j = Popcount(k.mtxSlots & ((1u << slot) - 1));
      mtxWords |= 1u << (j / 4);
    } else {
      s.mtxDefaults = true;
    }
  }
  for (unsigned w = 0; w < 3; w++)
    if (mtxWords & (1u << w))
      s.attribs[s.attribCount++] = kAttrMtx0 + static_cast<int>(w);
  s.normalMatrix = normalMatrix;
  // post-transform rows, packed
  if (k.dualTex)
    for (unsigned i = 0; i < k.numTexGens; i++) {
      if (!Regular(k.texGen[i]))
        continue;
      const int pr = static_cast<int>(k.postGen[i] & 63);
      for (int r = pr; r < pr + 3; r++) {
        bool known = false;
        for (int j = 0; j < s.postCount; j++)
          known = known || s.postRows[j] == r;
        if (!known && s.postCount < 24)
          s.postRows[s.postCount++] = r;
      }
    }
  // texture coordinates: which maps read each, projected or not
  s.fixedPoint = k.numIndStages > 0;
  for (int i = 0; i < 8; i++)
    s.coordMap[i] = -2;
  // 0x100: the coordinate in 1/128 texels for the indirect stages: their lookups, the stages
  // themselves (wrap, matrix), and a stage whose coordinate an indirect stage after it adds to
  // (the other coordinates of such a shader read as without indirect stages: on the Tegra 3 a
  // shader with five such coordinates passed the 256 instructions)
  std::uint32_t readers[8] = {};
  auto indirectStage = [&](unsigned n) {
    const std::uint32_t ind = n < k.numStages ? k.indCmd[n] : 0;
    return ind != 0 && (ind & 3) < k.numIndStages;
  };
  for (unsigned n = 0; n < k.numStages; n++) {
    const std::uint32_t order = k.order[n];
    const unsigned map = order & 7, coord = (order >> 3) & 7, enable = (order >> 6) & 1;
    if (coord < k.numTexGens && enable && (k.texMaps & (1u << map)))
      readers[coord] |= 1u << map;
    const bool addedTo = indirectStage(n + 1) && ((k.indCmd[n + 1] >> 20) & 1);
    if (coord < k.numTexGens && (indirectStage(n) || addedTo))
      readers[coord] |= 0x100;
  }
  if (s.fixedPoint)
    for (unsigned st = 0; st < k.numIndStages && st < 4; st++) {
      const unsigned coord = (k.indRef >> (6 * st + 3)) & 7;
      if (coord < k.numTexGens)
        readers[coord] |= 0x100;
    }
  for (unsigned i = 0; i < k.numTexGens; i++) {
    const std::uint32_t r = readers[i] & 0xFF;
    if (!readers[i])
      continue;
    if (!(readers[i] & 0x100) && !k.texelCoords && r && (r & (r - 1)) == 0) {
      int m = 0;
      while (!(r & (1u << m)))
        m++;
      s.coordMap[i] = m;
    } else {
      s.coordMap[i] = -1;
    }
  }
  for (unsigned i = 0; i < k.numTexGens; i++) {
    const std::uint32_t g = k.texGen[i];
    if (Regular(g))
      s.projective[i] = ((g >> 1) & 1) || k.dualTex;
    else if (GenType(g) == 1) {
      const unsigned source = (g >> 12) & 7;
      s.projective[i] = source < i && s.projective[source];
    }
  }
  // sizes: vertex uniforms (vec4) and varying floats
  s.vertexUniforms = 64 + (s.normalMatrix ? 32 : 0) + s.postCount + 5 * static_cast<int>(Popcount(s.lights)) + 4 + 4 +
                     (s.mtxDefaults ? 3 : 0) + (s.compact ? 3 : 0) + 4;
  s.varyingComponents = 8;
  for (unsigned i = 0; i < k.numTexGens; i++)
    if (s.coordMap[i] != -2)
      s.varyingComponents += s.projective[i] ? 3 : 2;
  return s;
}

namespace {

// The packed place of light `light` in uLight (five vec4 each).
int LightSlot(const ShaderInfo &s, unsigned light) { return static_cast<int>(Popcount(s.lights & ((1u << light) - 1))); }

int PostSlot(const ShaderInfo &s, int row) {
  for (int j = 0; j < s.postCount; j++)
    if (s.postRows[j] == row)
      return j;
  return 0;
}

// One lighting channel (gx_gl_shader.cpp Light).
void Light(std::string &out, const ShaderKey &k, const ShaderInfo &s, int chan, bool alpha) {
  const std::uint32_t c = k.chan[chan + (alpha ? 2 : 0)];
  const char *sw = alpha ? "a" : "rgb";
  const char *type = alpha ? "float" : "vec3";
  bool hasColor = false;
  for (int a = 0; a < s.attribCount; a++)
    hasColor = hasColor || s.attribs[a] == kAttrClr0 + chan;
  Add(out, "  {\n");
  if ((c & 1) && hasColor)
    Add(out, "    %s mat = aClr%d.%s;\n", type, chan, sw);
  else if (c & 1)
    Add(out, "    %s mat = %s(1.0);\n", type, type);
  else
    Add(out, "    %s mat = uMatAmb[%d].%s;\n", type, chan, sw);
  if (c & 2) {
    if ((c & 64) && hasColor)
      Add(out, "    %s lacc = aClr%d.%s;\n", type, chan, sw);
    else if (c & 64)
      Add(out, "    %s lacc = %s(1.0);\n", type, type);
    else
      Add(out, "    %s lacc = uMatAmb[%d].%s;\n", type, 2 + chan, sw);
    const unsigned mask = LightMask(c);
    const unsigned diffuse = (c >> 7) & 3, attn = (c >> 9) & 3;
    for (unsigned i = 0; i < 8; i++) {
      if (!(mask & (1u << i)))
        continue;
      const int b = LightSlot(s, i) * 5; // colour, cos att, dist att, position, direction
      Add(out, "    {\n");
      if (attn == 1) { // specular
        Add(out, "      vec3 ldir = normalize(uLight[%d].xyz - pos);\n", b + 3);
        Add(out, "      float at = dot(nrm, ldir) >= 0.0 ? max(0.0, dot(nrm, uLight[%d].xyz)) : 0.0;\n", b + 4);
        if (diffuse == 0)
          Add(out, "      vec3 dist = uLight[%d].xyz;\n", b + 2);
        else
          Add(out, "      vec3 dist = normalize(uLight[%d].xyz);\n", b + 2);
        Add(out, "      at = max(0.0, dot(uLight[%d].xyz, vec3(1.0, at, at * at))) / dot(dist, vec3(1.0, at, at * at));\n",
            b + 1);
      } else if (attn == 3) { // spot
        Add(out, "      vec3 ldir = uLight[%d].xyz - pos;\n", b + 3);
        // one inverse square root for the distance and the direction (sqrt and two divisions cost
        // the Tegra 3 more instructions)
        Add(out, "      float dist2 = dot(ldir, ldir);\n      float idist = inversesqrt(dist2);\n");
        Add(out, "      float dist = dist2 * idist;\n      ldir *= idist;\n");
        Add(out, "      float at = max(0.0, dot(ldir, uLight[%d].xyz));\n", b + 4);
        Add(out,
            "      at = max(0.0, uLight[%d].x + uLight[%d].y * at + uLight[%d].z * at * at) / "
            "dot(uLight[%d].xyz, vec3(1.0, dist, dist2));\n",
            b + 1, b + 1, b + 1, b + 2);
      } else { // none / directional
        Add(out, "      vec3 ldir = normalize(uLight[%d].xyz - pos);\n", b + 3);
        Add(out, "      float at = 1.0;\n");
      }
      if (diffuse == 0)
        Add(out, "      lacc += at * uLight[%d].%s;\n", b, sw);
      else if (diffuse == 1)
        Add(out, "      lacc += at * dot(ldir, nrm) * uLight[%d].%s;\n", b, sw);
      else
        Add(out, "      lacc += at * max(0.0, dot(ldir, nrm)) * uLight[%d].%s;\n", b, sw);
      Add(out, "    }\n");
    }
    Add(out, "    col%d.%s = mat * clamp(lacc, 0.0, 1.0);\n", chan, sw);
  } else {
    Add(out, "    col%d.%s = mat;\n", chan, sw);
  }
  Add(out, "  }\n");
}

bool Reads(const ShaderInfo &s, int source) {
  for (int a = 0; a < s.attribCount; a++)
    if (s.attribs[a] == source)
      return true;
  return false;
}

} // namespace

std::string GxVertexShader(const ShaderKey &k, const ShaderInfo &s, bool lean) {
  std::string out = "precision highp float;\n";
  for (int a = 0; a < s.attribCount; a++) {
    const int src = s.attribs[a];
    const char *type = src == kAttrPos || src == kAttrNrm || src == kAttrBin || src == kAttrTan ? "vec3"
                       : src >= kAttrTex0 && src < kAttrMtx0                                    ? "vec2"
                                                                                                 : "vec4";
    Add(out, "attribute %s %s;\n", type, GxAttributeName(src));
  }
  out += "uniform vec4 uPosTex[64];\n";
  if (s.normalMatrix)
    out += "uniform vec4 uNrm[32];\n";
  if (s.postCount)
    Add(out, "uniform vec4 uPost[%d];\n", s.postCount);
  if (s.lights)
    Add(out, "uniform vec4 uLight[%u];\n", 5 * Popcount(s.lights));
  out += "uniform vec4 uMatAmb[4];\nuniform vec4 uVp[2];\nuniform vec4 uProj[2];\n";
  if (s.mtxDefaults)
    out += "uniform vec4 uMtxDef[3];\n";
  if (s.compact)
    out += "uniform vec4 uDequant[3];\n";
  out += "uniform vec4 uTcScale[4];\n";
  out += "varying vec4 vColor0;\nvarying vec4 vColor1;\n";
  for (unsigned i = 0; i < k.numTexGens; i++)
    if (s.coordMap[i] != -2)
      Add(out, "varying %s vTex%u;\n", s.projective[i] ? "vec3" : "vec2", i);
  out += "void main() {\n";
  // the attributes as numbers (the compact vertex scaled by its fraction bits)
  for (int a = 0; a < s.attribCount; a++) {
    const int src = s.attribs[a];
    if (src >= kAttrClr0 && src <= kAttrClr1)
      continue;
    if (src >= kAttrMtx0)
      continue;
    const char *type = src >= kAttrTex0 ? "vec2" : "vec3";
    static const char *local[kAttrMtx0] = {"lPos",  "lNrm",  "lBin",  "lTan",  "",      "",      "lTex0",
                                           "lTex1", "lTex2", "lTex3", "lTex4", "lTex5", "lTex6", "lTex7"};
    if (s.compact)
      Add(out, "  %s %s = %s * %s;\n", type, local[src], GxAttributeName(src), Dequant(src).c_str());
    else
      Add(out, "  %s %s = %s;\n", type, local[src], GxAttributeName(src));
  }
  Add(out, "  float slot0 = %s;\n", Slot(k, 0).c_str());
  out += "  int pi = int(slot0);\n";
  out += "  vec4 p4 = vec4(lPos, 1.0);\n";
  out += "  vec3 pos = vec3(dot(uPosTex[pi], p4), dot(uPosTex[pi + 1], p4), dot(uPosTex[pi + 2], p4));\n";
  out += "  vec3 nrm = vec3(0.0, 0.0, 1.0);\n  vec3 vbin = vec3(0.0);\n  vec3 vtan = vec3(0.0);\n";
  if (s.normalMatrix) {
    out += "  int ni = int(mod(slot0, 32.0));\n";
    out += "  nrm = vec3(dot(uNrm[ni].xyz, lNrm), dot(uNrm[ni + 1].xyz, lNrm), dot(uNrm[ni + 2].xyz, lNrm));\n";
    out += "  if (dot(nrm, nrm) > 0.0) nrm = normalize(nrm);\n";
    if (Reads(s, kAttrBin)) {
      out += "  vbin = vec3(dot(uNrm[ni].xyz, lBin), dot(uNrm[ni + 1].xyz, lBin), dot(uNrm[ni + 2].xyz, lBin));\n";
      out += "  vtan = vec3(dot(uNrm[ni].xyz, lTan), dot(uNrm[ni + 1].xyz, lTan), dot(uNrm[ni + 2].xyz, lTan));\n";
    }
  }
  if (k.orthographic)
    out += "  vec4 clip = vec4(uProj[0].x * pos.x + uProj[0].y, uProj[0].z * pos.y + uProj[0].w, uProj[1].x * pos.z "
           "+ uProj[1].y, 1.0);\n";
  else
    out += "  vec4 clip = vec4(uProj[0].x * pos.x + uProj[0].y * pos.z, uProj[0].z * pos.y + uProj[0].w * pos.z, "
           "uProj[1].x * pos.z + uProj[1].y, -pos.z);\n";
  // GX viewport (EFB pixels + 342) to the GL viewport over the whole EFB; no flip (uVp: the
  // factors gx_gl2.cpp FillXfRegs works out)
  out += "  gl_Position = vec4(uVp[0].x * clip.x + uVp[0].y * clip.w, uVp[0].z * clip.y + uVp[0].w * clip.w,\n"
         "                     2.0 * (uVp[1].x * clip.z + uVp[1].y * clip.w) / 16777215.0 - clip.w, clip.w);\n"
         "  gl_PointSize = max(1.0, uProj[1].w);\n";
  // lighting channels
  out += "  vec4 col0 = vec4(1.0);\n  vec4 col1 = vec4(1.0);\n";
  for (unsigned c = 0; c < 2 && c < k.numChans; c++) {
    Light(out, k, s, static_cast<int>(c), false);
    Light(out, k, s, static_cast<int>(c), true);
  }
  if (k.numChans == 0)
    out += Reads(s, kAttrClr0) ? "  col0 = aClr0;\n" : "  col0 = vec4(1.0);\n";
  if (k.numChans < 2)
    out += Reads(s, kAttrClr1) ? "  col1 = aClr1;\n" : "  col1 = col0;\n";
  out += "  vColor0 = col0;\n  vColor1 = col1;\n";
  // texture coordinate generation: t0..t7 as GX makes them, then scaled for the fragment shader
  for (unsigned i = 0; i < k.numTexGens; i++) {
    const std::uint32_t g = k.texGen[i];
    const unsigned proj = (g >> 1) & 1, form = (g >> 2) & 1, type = GenType(g), row = GenRow(g);
    Add(out, "  vec3 t%u = vec3(0.0, 0.0, 1.0);\n", i);
    if (type == 1) { // emboss: the source coordinate moved along the light in tangent space
      const unsigned source = (g >> 12) & 7, light = (g >> 15) & 7;
      if (source < i && Reads(s, kAttrBin)) {
        Add(out, "  {\n    vec3 ldir = normalize(uLight[%d].xyz - pos);\n", LightSlot(s, light) * 5 + 3);
        Add(out, "    t%u = t%u + vec3(dot(ldir, vtan), dot(ldir, vbin), 0.0);\n  }\n", i, source);
      } else if (source < i) {
        Add(out, "  t%u = t%u;\n", i, source);
      }
      continue;
    }
    if (type == 2 || type == 3) {
      Add(out, "  t%u = vec3(col%u.xy, 1.0);\n", i, type - 2);
      continue;
    }
    out += "  {\n    vec4 src = vec4(0.0, 0.0, 1.0, 1.0);\n";
    if (row == 0)
      out += "    src = vec4(lPos, 1.0);\n";
    else if (row == 1 && Reads(s, kAttrNrm))
      out += "    src = vec4(lNrm, 1.0);\n";
    else if (row == 2)
      out += "    src = col0;\n";
    else if (row == 3 && Reads(s, kAttrTan))
      out += "    src = vec4(lTan, 1.0);\n";
    else if (row == 4 && Reads(s, kAttrBin))
      out += "    src = vec4(lBin, 1.0);\n";
    else if (row >= 5 && row <= 12 && Reads(s, kAttrTex0 + static_cast<int>(row - 5)))
      Add(out, "    src = vec4(lTex%u, 1.0, 1.0);\n", row - 5);
    if (form == 0)
      out += "    src.z = 1.0;\n";
    Add(out, "    int ti = int(%s);\n", Slot(k, i + 1).c_str());
    if (proj)
      out += "    vec3 t = vec3(dot(src, uPosTex[ti]), dot(src, uPosTex[ti + 1]), dot(src, uPosTex[ti + 2]));\n";
    else
      out += "    vec3 t = vec3(dot(src, uPosTex[ti]), dot(src, uPosTex[ti + 1]), 1.0);\n";
    if (k.dualTex) {
      const std::uint32_t post = k.postGen[i];
      if (post & 0x100)
        out += "    if (dot(t, t) > 0.0) t = normalize(t);\n";
      const int pr = static_cast<int>(post & 63);
      const int r0 = PostSlot(s, pr), r1 = PostSlot(s, pr + 1), r2 = PostSlot(s, pr + 2);
      Add(out, "    vec4 th = vec4(t, 1.0);\n");
      Add(out, "    t = vec3(dot(uPost[%d], th), dot(uPost[%d], th), dot(uPost[%d], th));\n", r0, r1, r2);
    }
    // q = 0: the GameCube's special case; z = 1 then, so the fragment shader's division keeps it
    if (!lean)
      out += "    if (t.z == 0.0) { t.xy = clamp(t.xy / 2.0, vec2(-1.0), vec2(1.0)); t.z = 1.0; }\n";
    Add(out, "    t%u = t;\n  }\n", i);
  }
  for (unsigned i = 0; i < k.numTexGens; i++) {
    if (s.coordMap[i] == -2)
      continue;
    const char *scale = (i & 1) ? "zw" : "xy";
    if (s.projective[i])
      Add(out, "  vTex%u = vec3(t%u.xy * uTcScale[%u].%s, t%u.z);\n", i, i, i / 2, scale, i);
    else
      Add(out, "  vTex%u = t%u.xy * uTcScale[%u].%s;\n", i, i, i / 2, scale);
  }
  out += "}\n";
  return out;
}

namespace {

const char *kDest[4] = {"prev", "c0", "c1", "c2"};
const int kKonstValue[8] = {255, 223, 191, 159, 128, 96, 64, 32};
const char kChannel[4] = {'r', 'g', 'b', 'a'};

std::string Swizzle(std::uint32_t table) {
  std::string s;
  for (int i = 0; i < 4; i++)
    s += kChannel[(table >> (2 * i)) & 3];
  return s;
}

// A value of a TEV stage: its GLSL expression (vec3 for colour, float for alpha), the range it
// can take, and the constant it is, if one. The stage code leaves out what the range makes
// needless: the wrap to 8 bits, the lerp with c = 0 or 255, the rounding of whole numbers, the
// clamp (the Tegra 3 allows 256 instructions in a fragment shader, and the TEV was most of the
// time of a frame there).
struct Term {
  std::string e;
  double lo = -1024.0, hi = 1023.0;
  int constant = -1;
  bool whole = true;
};

std::string Number(double v) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.10g", v);
  std::string s = buffer;
  if (s.find_first_of(".e") == std::string::npos)
    s += ".0";
  return s;
}

Term Constant(int v, bool vec) {
  Term t;
  t.e = vec ? "vec3(" + Number(v) + ")" : Number(v);
  t.lo = t.hi = v;
  t.constant = v;
  return t;
}

Term Variable(const std::string &e, double lo, double hi) {
  Term t;
  t.e = e;
  t.lo = lo;
  t.hi = hi;
  return t;
}

bool In8(const Term &t) { return t.lo >= 0.0 && t.hi <= 255.0; }

// The value & 255 (inputs a, b, c): the value itself where its range allows, else `wrapped` (a
// uniform the processor wrapped) or wrap8().
Term Wrap(const Term &t, const std::string &wrapped) {
  if (In8(t))
    return t;
  if (!wrapped.empty())
    return Variable(wrapped, 0.0, 255.0);
  return Variable("wrap8(" + t.e + ")", 0.0, 255.0);
}

// (d + bias) op lerp(a, b, c), scaled, with the hardware's rounding (gx_gl_shader.cpp Regular):
// c widened to 0..256 (c' = c + (c >= 128)), a scale above one inside the lerp, a rounding bias
// before the division by 256 (not with the halving scale). lerp / 256 = a + (b - a) c' / 256.
Term RegularOp(const Term &a, const Term &b, const Term &c, const Term &d, unsigned bias, unsigned op,
               unsigned scale) {
  const double mul = scale == 1 ? 2.0 : scale == 2 ? 4.0 : 1.0;
  // the lerp, in the inputs' units: whole where it is a or b
  Term lerp;
  lerp.lo = std::min(a.lo, b.lo);
  lerp.hi = std::max(a.hi, b.hi);
  if (c.constant >= 0) {
    const int cw = c.constant + (c.constant >= 128 ? 1 : 0);
    const std::string f = Number(cw / 256.0);
    if (cw == 0 || a.e == b.e) {
      lerp = a;
    } else if (cw == 256) {
      lerp = b;
    } else if (a.constant == 0) {
      lerp.e = "(" + b.e + " * " + f + ")";
      lerp.whole = false;
    } else if (b.constant == 0) {
      lerp.e = "(" + a.e + " * " + Number(1.0 - cw / 256.0) + ")";
      lerp.whole = false;
    } else {
      lerp.e = "(" + a.e + " + (" + b.e + " - " + a.e + ") * " + f + ")";
      lerp.whole = false;
    }
  } else if (a.e == b.e) {
    lerp = a;
  } else {
    const std::string f = "((" + c.e + " + step(128.0, " + c.e + ")) * 0.00390625)";
    if (a.constant == 0)
      lerp.e = "(" + b.e + " * " + f + ")";
    else if (b.constant == 0)
      lerp.e = "(" + a.e + " - " + a.e + " * " + f + ")";
    else
      lerp.e = "(" + a.e + " + (" + b.e + " - " + a.e + ") * " + f + ")";
    lerp.whole = false;
  }
  if (a.constant == 0 && b.constant == 0)
    lerp = Constant(0, false), lerp.e = a.e;
  // rounded: floor(lerp * mul + rounding); a whole lerp stays as it is
  Term rounded;
  rounded.lo = lerp.lo * mul;
  rounded.hi = lerp.hi * mul;
  if (lerp.constant == 0) {
    rounded = lerp;
  } else {
    const std::string scaled = mul == 1.0 ? lerp.e : "(" + lerp.e + " * " + Number(mul) + ")";
    if (lerp.whole)
      rounded.e = scaled;
    else
      rounded.e = "floor(" + scaled + (scale != 3 ? (op ? " + 0.49609375" : " + 0.5") : "") + ")";
  }
  // D = (d + bias) * mul (not with the halving scale)
  const double add = bias == 1 ? 128.0 : bias == 2 ? -128.0 : 0.0;
  Term dd;
  dd.lo = (d.lo + add) * mul;
  dd.hi = (d.hi + add) * mul;
  if (d.constant == 0 && add == 0.0) {
    dd = d;
  } else {
    dd.e = add == 0.0 ? d.e : "(" + d.e + (add > 0 ? " + 128.0" : " - 128.0") + ")";
    if (mul != 1.0)
      dd.e = "(" + dd.e + " * " + Number(mul) + ")";
    dd.constant = d.constant >= 0 ? static_cast<int>((d.constant + add) * mul) : -1;
  }
  Term v;
  if (rounded.constant == 0) {
    v = dd;
  } else if (dd.constant == 0) {
    v = rounded;
    if (op) {
      v.e = "-" + rounded.e;
      v.lo = -rounded.hi;
      v.hi = -rounded.lo;
      v.constant = -1;
    }
  } else {
    v.e = "(" + dd.e + (op ? " - " : " + ") + rounded.e + ")";
    v.lo = op ? dd.lo - rounded.hi : dd.lo + rounded.lo;
    v.hi = op ? dd.hi - rounded.lo : dd.hi + rounded.hi;
    v.constant = -1;
  }
  v.whole = true;
  if (scale == 3) {
    v.e = "floor(" + v.e + " * 0.5)";
    v.lo = std::floor(v.lo * 0.5);
    v.hi = std::floor(v.hi * 0.5);
    v.constant = -1;
  }
  return v;
}

// The clamp of a stage's result where its range needs it.
Term Clamp(Term v, bool to8, bool vec) {
  const double lo = to8 ? 0.0 : -1024.0, hi = to8 ? 255.0 : 1023.0;
  if (v.lo >= lo && v.hi <= hi)
    return v;
  if (vec)
    v.e = "clamp(" + v.e + ", vec3(" + Number(lo) + "), vec3(" + Number(hi) + "))";
  else
    v.e = "clamp(" + v.e + ", " + Number(lo) + ", " + Number(hi) + ")";
  v.lo = std::max(v.lo, lo);
  v.hi = std::min(v.hi, hi);
  v.constant = -1;
  return v;
}

const char *kColorCompare[8] = {"(tevin_a.r > tevin_b.r ? tevin_c.rgb : vec3(0.0))",
                                "(tevin_a.r == tevin_b.r ? tevin_c.rgb : vec3(0.0))",
                                "(gt16(tevin_a.rgb, tevin_b.rgb) ? tevin_c.rgb : vec3(0.0))",
                                "(eq16(tevin_a.rgb, tevin_b.rgb) ? tevin_c.rgb : vec3(0.0))",
                                "(gt24(tevin_a.rgb, tevin_b.rgb) ? tevin_c.rgb : vec3(0.0))",
                                "(eq24(tevin_a.rgb, tevin_b.rgb) ? tevin_c.rgb : vec3(0.0))",
                                "(vec3(greaterThan(tevin_a.rgb, tevin_b.rgb)) * tevin_c.rgb)",
                                "(vec3(equal(tevin_a.rgb, tevin_b.rgb)) * tevin_c.rgb)"};
const char *kAlphaCompare[8] = {"(tevin_a.r > tevin_b.r ? tevin_c.a : 0.0)",
                                "(tevin_a.r == tevin_b.r ? tevin_c.a : 0.0)",
                                "(gt16(tevin_a.rgb, tevin_b.rgb) ? tevin_c.a : 0.0)",
                                "(eq16(tevin_a.rgb, tevin_b.rgb) ? tevin_c.a : 0.0)",
                                "(gt24(tevin_a.rgb, tevin_b.rgb) ? tevin_c.a : 0.0)",
                                "(eq24(tevin_a.rgb, tevin_b.rgb) ? tevin_c.a : 0.0)",
                                "(tevin_a.a > tevin_b.a ? tevin_c.a : 0.0)",
                                "(tevin_a.a == tevin_b.a ? tevin_c.a : 0.0)"};

std::string AlphaFunc(unsigned f, const char *ref) {
  static const char *op[8] = {"false", "<", "==", "<=", ">", "!=", ">=", "true"};
  if (f == 0 || f == 7)
    return op[f];
  return std::string("(prev.a ") + op[f] + " " + ref + ")";
}

// uLodBias / uMapScale entries of map m
std::string Bias(unsigned m) { return "uLodBias[" + std::to_string(m / 4) + "]." + kXyzw[m % 4]; }

// The fast TEV (ShaderKey::fastTev): a value in fractions 0..1 (255 steps = 1) and the constant it
// is (in steps), if one.
struct Frac {
  std::string e;
  int constant = -1;
};

Frac FracConstant(int steps, bool vec) {
  Frac f;
  const std::string v = steps == 0 ? "0.0" : steps == 255 ? "1.0" : Number(steps / 255.0);
  f.e = vec ? "vec3(" + v + ")" : v;
  f.constant = steps;
  return f;
}

// (d + bias) op mix(a, b, c), scaled: GX's arithmetic without its 8-bit steps (c' = c + c / 255
// stands for the hardware's c + (c >= 128); no rounding)
std::string FastOp(const Frac &a, const Frac &b, const Frac &c, const Frac &d, unsigned bias, unsigned op,
                   unsigned scale, bool vec) {
  std::string lerp;
  if (c.constant == 0 || a.e == b.e)
    lerp = a.e;
  else if (c.constant == 255)
    lerp = b.e;
  else if (a.constant == 0)
    lerp = "(" + b.e + " * " + c.e + ")";
  else if (b.constant == 0)
    lerp = "(" + a.e + " * (1.0 - " + c.e + "))";
  else
    lerp = "mix(" + a.e + ", " + b.e + ", " + c.e + ")";
  const bool noLerp = (a.constant == 0 && b.constant == 0) || (c.constant == 0 && a.constant == 0) ||
                      (c.constant == 255 && b.constant == 0);
  std::string v = d.constant == 0 ? "" : d.e;
  if (bias == 1)
    v = v.empty() ? (vec ? "vec3(0.50196)" : "0.50196") : "(" + v + " + 0.50196)";
  else if (bias == 2)
    v = v.empty() ? (vec ? "vec3(-0.50196)" : "-0.50196") : "(" + v + " - 0.50196)";
  if (!noLerp)
    v = v.empty() ? (op ? "-" + lerp : lerp) : "(" + v + (op ? " - " : " + ") + lerp + ")";
  if (v.empty())
    v = vec ? "vec3(0.0)" : "0.0";
  static const char *mul[4] = {"", " * 2.0", " * 4.0", " * 0.5"};
  if (scale)
    v = "(" + v + mul[scale] + ")";
  return v;
}

} // namespace

std::string GxFragmentShader(const ShaderKey &k, const ShaderInfo &s) {
  // samplers too: texture reads have the sampler's precision, lowp by default (the Tegra 3's lowp
  // is fixed point in 1/256, which turns some of the 255 steps of a texel into the next)
  std::string out = "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\nprecision highp sampler2D;\n#else\n"
                    "precision mediump float;\nprecision mediump sampler2D;\n#endif\n";
  for (int m = 0; m < 8; m++)
    if (k.texMaps & (1u << m))
      Add(out, "uniform sampler2D s%d;\n", m);
  // TEV registers and konstants, as they are and & 255 (wrapped by the processor)
  out += "uniform vec4 uColors[4];\nuniform vec4 uColorsW[4];\nuniform vec4 uKonst[4];\nuniform vec4 uKonstW[4];\n"
         "uniform vec4 uAlphaRef;\nuniform vec4 uFog[2];\nuniform vec4 uLodBias[2];\n";
  bool texel = false;
  for (unsigned i = 0; i < k.numTexGens; i++)
    texel = texel || s.coordMap[i] == -1;
  if (texel || s.fixedPoint)
    out += "uniform vec4 uMapScale[8];\n";
  if (s.fixedPoint)
    out += "uniform vec4 uIndScale[4];\nuniform vec4 uIndMtx[6];\n";
  out += "varying vec4 vColor0;\nvarying vec4 vColor1;\n";
  for (unsigned i = 0; i < k.numTexGens; i++)
    if (s.coordMap[i] != -2)
      Add(out, "varying %s vTex%u;\n", s.projective[i] ? "vec3" : "vec2", i);
  out += "vec3 wrap8(vec3 v) { return v - 256.0 * floor(v * 0.00390625); }\n"
         "float wrap8(float v) { return v - 256.0 * floor(v * 0.00390625); }\n"
         "vec2 trunc2(vec2 v) { return sign(v) * floor(abs(v)); }\n"
         "bool gt16(vec3 a, vec3 b) { return a.g > b.g || (a.g == b.g && a.r > b.r); }\n"
         "bool eq16(vec3 a, vec3 b) { return a.r == b.r && a.g == b.g; }\n"
         "bool gt24(vec3 a, vec3 b) { return a.b > b.b || (a.b == b.b && gt16(a, b)); }\n"
         "bool eq24(vec3 a, vec3 b) { return eq16(a, b) && a.b == b.b; }\n"
         "vec2 shiftTrans(vec2 t, float w) { return w >= 0.0 ? floor(t * exp2(-w)) : t * exp2(-w); }\n";
  // 0..1 to 0..255; with texelCoords as the GLES 3 renderer's round() on Mesa (ties to even: a
  // linear read at a texel corner gives x.5), else half up (cheaper)
  if (k.texelCoords)
    out += "vec4 to8(vec4 v) {\n  vec4 x = v * 255.0;\n  vec4 r = floor(x + 0.5);\n"
           "  return r - vec4(equal(r - x, vec4(0.5))) * mod(r, 2.0);\n}\n";
  else
    out += "vec4 to8(vec4 v) { return floor(v * 255.0 + 0.5); }\n";
  // a map read at a coordinate in 1/128 texels
  if (texel || s.fixedPoint)
    for (int m = 0; m < 8; m++)
      if (k.texMaps & (1u << m))
        Add(out,
            "vec4 texmap%d(vec2 c) {\n"
            "  return to8(texture2D(s%d, c * uMapScale[%d].xy, %s));\n"
            "}\n",
            m, m, m, Bias(static_cast<unsigned>(m)).c_str());
  const bool fast = k.fastTev != 0;
  out += "void main() {\n";
  if (fast)
    out += "  const float k255 = 1.0 / 255.0;\n  vec4 prev = uColors[0] * k255;\n  vec4 c0 = uColors[1] * k255;\n"
           "  vec4 c1 = uColors[2] * k255;\n  vec4 c2 = uColors[3] * k255;\n";
  else
    out += "  vec4 prev = uColors[0];\n  vec4 c0 = uColors[1];\n  vec4 c1 = uColors[2];\n  vec4 c2 = uColors[3];\n";
  out += "  vec4 rastemp = vec4(0.0);\n  vec4 textemp = vec4(0.0);\n";
  // what the stages leave in the registers (prev, c0, c1, c2; colour, alpha): range, written
  double regLo[4][2], regHi[4][2];
  bool regWritten[4][2] = {};
  for (int r = 0; r < 4; r++)
    for (int part = 0; part < 2; part++) {
      regLo[r][part] = -1024.0;
      regHi[r][part] = 1023.0;
    }
  out += "  vec2 tevcoord = vec2(0.0);\n  float alphabump = 0.0;\n";
  if (fast) {
    out += "  vec4 ras0 = clamp(vColor0, 0.0, 1.0);\n";
    out += "  vec4 ras1 = clamp(vColor1, 0.0, 1.0);\n";
  } else {
    out += "  vec4 ras0 = to8(clamp(vColor0, 0.0, 1.0));\n";
    out += "  vec4 ras1 = to8(clamp(vColor1, 0.0, 1.0));\n";
  }
  // coordinates in 1/128 texels: truncated like the GLES 3 renderer's integers
  for (unsigned i = 0; i < k.numTexGens; i++)
    if (s.coordMap[i] == -1) {
      if (s.projective[i])
        Add(out, "  vec2 fixuv%u = trunc2(vTex%u.xy / vTex%u.z);\n", i, i, i);
      else
        Add(out, "  vec2 fixuv%u = trunc2(vTex%u);\n", i, i);
    }
  // indirect texture lookups
  for (unsigned st = 0; st < k.numIndStages && st < 4; st++) {
    const unsigned map = (k.indRef >> (6 * st)) & 7, coord = (k.indRef >> (6 * st + 3)) & 7;
    if (coord < k.numTexGens && (k.texMaps & (1u << map)))
      Add(out, "  vec3 iindtex%u = texmap%u(floor(fixuv%u * uIndScale[%u].xy)).abg;\n", st, map, coord, st);
    else
      Add(out, "  vec3 iindtex%u = vec3(0.0);\n", st);
  }
  for (unsigned n = 0; n < k.numStages; n++) {
    const std::uint32_t cc = k.colorEnv[n], ac = k.alphaEnv[n], order = k.order[n], ind = k.indCmd[n];
    const unsigned map = order & 7, coord = (order >> 3) & 7, enable = (order >> 6) & 1, chan = (order >> 7) & 7;
    const bool hasCoord = coord < k.numTexGens;
    Add(out, "  // stage %u\n  {\n", n);
    const unsigned bt = ind & 3, fmt = (ind >> 2) & 3, bias = (ind >> 4) & 7, bs = (ind >> 7) & 3,
                   mid = (ind >> 9) & 15, sw = (ind >> 13) & 7, tw = (ind >> 16) & 7, addprev = (ind >> 20) & 1;
    const bool indirect = ind != 0 && bt < k.numIndStages;
    if (indirect) {
      static const char *fmtMod[4] = {"", "32.0", "16.0", "8.0"};
      static const char *alphaMod[4] = {"8.0", "32.0", "16.0", "8.0"};
      static const char *biasField[8] = {"", "x", "y", "xy", "z", "xz", "yz", "xyz"};
      static const char *biasAdd[4] = {"-128.0", "1.0", "1.0", "1.0"};
      if (bs) {
        const char comp = "xxyz"[bs];
        Add(out, "    alphabump = iindtex%u.%c - mod(iindtex%u.%c, %s);\n", bt, comp, bt, comp, alphaMod[fmt]);
      }
      if (fmt)
        Add(out, "    vec3 crd = mod(iindtex%u, %s);\n", bt, fmtMod[fmt]);
      else
        Add(out, "    vec3 crd = iindtex%u;\n", bt);
      if (bias)
        Add(out, "    crd.%s += %s;\n", biasField[bias], biasAdd[fmt]);
      out += "    vec2 trans = vec2(0.0);\n";
      if (mid >= 1 && mid <= 3) {
        const unsigned r = 2 * (mid - 1);
        Add(out, "    trans = floor(vec2(dot(uIndMtx[%u].xyz, crd), dot(uIndMtx[%u].xyz, crd)) / 8.0);\n", r, r + 1);
        Add(out, "    trans = shiftTrans(trans, uIndMtx[%u].w);\n", r);
      } else if (mid >= 5 && mid <= 7 && hasCoord) {
        const unsigned r = 2 * (mid - 5);
        Add(out, "    trans = shiftTrans(floor(fixuv%u * crd.xx / 256.0), uIndMtx[%u].w);\n", coord, r);
      } else if (mid >= 9 && mid <= 11 && hasCoord) {
        const unsigned r = 2 * (mid - 9);
        Add(out, "    trans = shiftTrans(floor(fixuv%u * crd.yy / 256.0), uIndMtx[%u].w);\n", coord, r);
      }
      // wrap 1-5: 256 .. 16 texels (in 1/128), 6: zero, 7: none (the GLES 3 renderer's & -1)
      static const char *wrap[8] = {"", "32768.0", "16384.0", "8192.0", "4096.0", "2048.0", "", ""};
      if (hasCoord) {
        Add(out, "    vec2 wrapped = fixuv%u;\n", coord);
        if (sw == 6)
          out += "    wrapped.x = 0.0;\n";
        else if (sw && sw < 6)
          Add(out, "    wrapped.x = mod(wrapped.x, %s);\n", wrap[sw]);
        if (tw == 6)
          out += "    wrapped.y = 0.0;\n";
        else if (tw && tw < 6)
          Add(out, "    wrapped.y = mod(wrapped.y, %s);\n", wrap[tw]);
      } else {
        out += "    vec2 wrapped = vec2(0.0);\n";
      }
      out += addprev ? "    tevcoord += wrapped + trans;\n" : "    tevcoord = wrapped + trans;\n";
    } else if (s.fixedPoint) {
      if (hasCoord && s.coordMap[coord] == -1)
        Add(out, "    tevcoord = fixuv%u;\n", coord);
      else
        out += "    tevcoord = vec2(0.0);\n";
    }
    const std::string rswap = Swizzle(k.swap[ac & 3]);
    const std::string tswap = Swizzle(k.swap[(ac >> 2) & 3]);
    // fast: the texel as it comes (0..1); from the fixed-point coordinates in steps, scaled
    const char *to8 = fast ? "" : "to8";
    const char *steps = fast ? " * k255" : "";
    if (enable && (k.texMaps & (1u << map))) {
      if (indirect) {
        Add(out, "    textemp = texmap%u(tevcoord).%s%s;\n", map, tswap.c_str(), steps);
      } else if (hasCoord && s.coordMap[coord] == static_cast<int>(map)) {
        // read as the vertex shader scaled it
        if (s.projective[coord])
          Add(out, "    textemp = %s(texture2DProj(s%u, vTex%u, %s)).%s;\n", to8, map, coord,
              Bias(map).c_str(), tswap.c_str());
        else
          Add(out, "    textemp = %s(texture2D(s%u, vTex%u, %s)).%s;\n", to8, map, coord, Bias(map).c_str(),
              tswap.c_str());
      } else if (hasCoord && s.coordMap[coord] == -1) {
        Add(out, "    textemp = texmap%u(fixuv%u).%s%s;\n", map, coord, tswap.c_str(), steps);
      } else {
        // no coordinate: texel 0 as the GLES 3 renderer reads it
        Add(out, "    textemp = %s(texture2D(s%u, vec2(0.0), %s)).%s;\n", to8, map, Bias(map).c_str(),
            tswap.c_str());
      }
    } else {
      out += fast ? "    textemp = vec4(1.0);\n" : "    textemp = vec4(255.0);\n";
    }
    switch (chan) {
    case 0:
      Add(out, "    rastemp = ras0.%s;\n", rswap.c_str());
      break;
    case 1:
      Add(out, "    rastemp = ras1.%s;\n", rswap.c_str());
      break;
    case 5:
      out += fast ? "    rastemp = vec4(alphabump * k255);\n" : "    rastemp = vec4(alphabump);\n";
      break;
    case 6:
      out += fast ? "    rastemp = vec4((alphabump + floor(alphabump / 32.0)) * k255);\n"
                  : "    rastemp = vec4(alphabump + floor(alphabump / 32.0));\n";
      break;
    default:
      out += "    rastemp = vec4(0.0);\n";
      break;
    }
    // the stage's inputs with what is known of them (Term): textures and rasterized colours are
    // 0..255, registers what the stages before left (the processor wraps their first values,
    // uColorsW), konstants unknown (uKonstW wrapped)
    const unsigned kc = k.konst[n] & 0xFF, ka = (k.konst[n] >> 8) & 0xFF;
    const bool texOn = enable && (k.texMaps & (1u << map));
    const bool rasOn = chan == 0 || chan == 1 || chan == 5 || chan == 6;
    auto reg = [&](unsigned r, int part, const char *swz, bool wrapped) {
      Term t = Variable(std::string(kDest[r]) + "." + swz, regLo[r][part], regHi[r][part]);
      return wrapped ? Wrap(t, regWritten[r][part] ? "" : "uColorsW[" + std::to_string(r) + "]." + swz) : t;
    };
    auto konst = [&](unsigned sel, bool vec, bool wrapped) {
      if (sel < 8)
        return Constant(kKonstValue[sel], vec);
      if (vec && sel >= 0x0C && sel <= 0x0F) {
        const std::string i = std::to_string(sel - 0x0C);
        return wrapped ? Variable("uKonstW[" + i + "].rgb", 0.0, 255.0) : Variable("uKonst[" + i + "].rgb", -1024.0, 1023.0);
      }
      if (sel >= 0x10) {
        const std::string i = std::to_string(sel & 3), c(vec ? 3 : 1, kChannel[(sel - 0x10) >> 2]);
        return wrapped ? Variable("uKonstW[" + i + "]." + c, 0.0, 255.0) : Variable("uKonst[" + i + "]." + c, -1024.0, 1023.0);
      }
      return Constant(0, vec);
    };
    auto colorIn = [&](unsigned sel, bool wrapped) {
      if (sel < 8)
        return reg(sel / 2, sel & 1, sel & 1 ? "aaa" : "rgb", wrapped);
      switch (sel) {
      case 8: return texOn ? Variable("textemp.rgb", 0.0, 255.0) : Constant(255, true);
      case 9: return texOn ? Variable("textemp.aaa", 0.0, 255.0) : Constant(255, true);
      case 10: return rasOn ? Variable("rastemp.rgb", 0.0, 255.0) : Constant(0, true);
      case 11: return rasOn ? Variable("rastemp.aaa", 0.0, 255.0) : Constant(0, true);
      case 12: return Constant(255, true);
      case 13: return Constant(128, true);
      case 14: return konst(kc, true, wrapped);
      default: return Constant(0, true);
      }
    };
    auto alphaIn = [&](unsigned sel, bool wrapped) {
      if (sel < 4)
        return reg(sel, 1, "a", wrapped);
      switch (sel) {
      case 4: return texOn ? Variable("textemp.a", 0.0, 255.0) : Constant(255, false);
      case 5: return rasOn ? Variable("rastemp.a", 0.0, 255.0) : Constant(0, false);
      case 6: return konst(ka, false, wrapped);
      default: return Constant(0, false);
      }
    };
    if (fast) {
      // inputs a, b, c are 8 bits on the GameCube (registers and konstants & 255; here clamped to
      // 0..1 where a stage before may have left more), d as it is
      auto fracReg = [&](unsigned r, int part, const char *swz, bool in8) {
        Frac f;
        if (!regWritten[r][part])
          f.e = std::string(in8 ? "uColorsW[" : "uColors[") + std::to_string(r) + "]." + swz + " * k255";
        else if (in8 && !(regLo[r][part] >= 0.0 && regHi[r][part] <= 255.0))
          f.e = std::string("clamp(") + kDest[r] + "." + swz + ", 0.0, 1.0)";
        else
          f.e = std::string(kDest[r]) + "." + swz;
        f.e = "(" + f.e + ")";
        return f;
      };
      auto fracKonst = [&](unsigned sel, bool vec) {
        if (sel < 8)
          return FracConstant(kKonstValue[sel], vec);
        Frac f;
        if (vec && sel >= 0x0C && sel <= 0x0F)
          f.e = "(uKonstW[" + std::to_string(sel - 0x0C) + "].rgb * k255)";
        else if (sel >= 0x10)
          f.e = "(uKonstW[" + std::to_string(sel & 3) + "]." + std::string(vec ? 3 : 1, kChannel[(sel - 0x10) >> 2]) +
                " * k255)";
        else
          return FracConstant(0, vec);
        return f;
      };
      auto fracColor = [&](unsigned sel, bool in8) {
        if (sel < 8)
          return fracReg(sel / 2, sel & 1, sel & 1 ? "aaa" : "rgb", in8);
        Frac f;
        switch (sel) {
        case 8: if (!texOn) return FracConstant(255, true); f.e = "textemp.rgb"; return f;
        case 9: if (!texOn) return FracConstant(255, true); f.e = "textemp.aaa"; return f;
        case 10: if (!rasOn) return FracConstant(0, true); f.e = "rastemp.rgb"; return f;
        case 11: if (!rasOn) return FracConstant(0, true); f.e = "rastemp.aaa"; return f;
        case 12: return FracConstant(255, true);
        case 13: return FracConstant(128, true);
        case 14: return fracKonst(kc, true);
        default: return FracConstant(0, true);
        }
      };
      auto fracAlpha = [&](unsigned sel, bool in8) {
        if (sel < 4)
          return fracReg(sel, 1, "a", in8);
        Frac f;
        switch (sel) {
        case 4: if (!texOn) return FracConstant(255, false); f.e = "textemp.a"; return f;
        case 5: if (!rasOn) return FracConstant(0, false); f.e = "rastemp.a"; return f;
        case 6: return fracKonst(ka, false);
        default: return FracConstant(0, false);
        }
      };
      const Frac fa = fracColor((cc >> 12) & 15, true), fb = fracColor((cc >> 8) & 15, true),
                 fc = fracColor((cc >> 4) & 15, true), fd = fracColor(cc & 15, false);
      const Frac ga = fracAlpha((ac >> 13) & 7, true), gb = fracAlpha((ac >> 10) & 7, true),
                 gc = fracAlpha((ac >> 7) & 7, true), gd = fracAlpha((ac >> 4) & 7, false);
      const bool colorCompare = ((cc >> 16) & 3) == 3, alphaCompare = ((ac >> 16) & 3) == 3;
      if (colorCompare || alphaCompare) {
        // compares on the 255 steps, as the exact TEV (the helpers take whole numbers)
        Add(out, "    vec4 tevin_a = floor(vec4(%s, %s) * 255.0 + 0.5);\n", fa.e.c_str(), ga.e.c_str());
        Add(out, "    vec4 tevin_b = floor(vec4(%s, %s) * 255.0 + 0.5);\n", fb.e.c_str(), gb.e.c_str());
        Add(out, "    vec4 tevin_c = vec4(%s, %s);\n", fc.e.c_str(), gc.e.c_str());
      }
      for (int part = 0; part < 2; part++) {
        const std::uint32_t env = part ? ac : cc;
        const unsigned b = (env >> 16) & 3, op = (env >> 18) & 1, clampOut = (env >> 19) & 1,
                       scale = (env >> 20) & 3, dest = (env >> 22) & 3;
        std::string v;
        if (b == 3)
          v = "(" + (part ? gd.e : fd.e) + " + " + (part ? kAlphaCompare : kColorCompare)[op | scale << 1] + ")";
        else
          v = part ? FastOp(ga, gb, gc, gd, b, op, scale, false) : FastOp(fa, fb, fc, fd, b, op, scale, true);
        if (clampOut)
          v = part ? "clamp(" + v + ", 0.0, 1.0)" : "clamp(" + v + ", vec3(0.0), vec3(1.0))";
        Add(out, "    %s.%s = %s;\n", kDest[dest], part ? "a" : "rgb", v.c_str());
        // the range in steps, for the inputs of the stages after (clamped: 0..255)
        regLo[dest][part] = clampOut ? 0.0 : -1024.0;
        regHi[dest][part] = clampOut ? 255.0 : 1023.0;
        regWritten[dest][part] = true;
      }
      out += "  }\n";
      continue;
    }
    const Term ca = colorIn((cc >> 12) & 15, true), cb = colorIn((cc >> 8) & 15, true),
               cc3 = colorIn((cc >> 4) & 15, true), cd = colorIn(cc & 15, false);
    const Term aa = alphaIn((ac >> 13) & 7, true), ab = alphaIn((ac >> 10) & 7, true),
               ac3 = alphaIn((ac >> 7) & 7, true), ad = alphaIn((ac >> 4) & 7, false);
    const bool colorCompare = ((cc >> 16) & 3) == 3, alphaCompare = ((ac >> 16) & 3) == 3;
    if (colorCompare || alphaCompare) {
      Add(out, "    vec4 tevin_a = vec4(%s, %s);\n", ca.e.c_str(), aa.e.c_str());
      Add(out, "    vec4 tevin_b = vec4(%s, %s);\n", cb.e.c_str(), ab.e.c_str());
      Add(out, "    vec4 tevin_c = vec4(%s, %s);\n", cc3.e.c_str(), ac3.e.c_str());
    }
    {
      const unsigned b = (cc >> 16) & 3, op = (cc >> 18) & 1, clampOut = (cc >> 19) & 1, scale = (cc >> 20) & 3,
                     dest = (cc >> 22) & 3;
      Term v;
      if (b == 3) {
        v = Variable("(" + cd.e + " + " + kColorCompare[op | scale << 1] + ")", cd.lo, cd.hi + 255.0);
      } else {
        v = RegularOp(ca, cb, cc3, cd, b, op, scale);
      }
      v = Clamp(v, clampOut, true);
      Add(out, "    %s.rgb = %s;\n", kDest[dest], v.e.c_str());
      regLo[dest][0] = v.lo;
      regHi[dest][0] = v.hi;
      regWritten[dest][0] = true;
    }
    {
      const unsigned b = (ac >> 16) & 3, op = (ac >> 18) & 1, clampOut = (ac >> 19) & 1, scale = (ac >> 20) & 3,
                     dest = (ac >> 22) & 3;
      Term v;
      if (b == 3) {
        v = Variable("(" + ad.e + " + " + kAlphaCompare[op | scale << 1] + ")", ad.lo, ad.hi + 255.0);
      } else {
        v = RegularOp(aa, ab, ac3, ad, b, op, scale);
      }
      v = Clamp(v, clampOut, false);
      Add(out, "    %s.a = %s;\n", kDest[dest], v.e.c_str());
      regLo[dest][1] = v.lo;
      regHi[dest][1] = v.hi;
      regWritten[dest][1] = true;
    }
    out += "  }\n";
  }
  if (k.numStages) {
    const unsigned cd = (k.colorEnv[k.numStages - 1] >> 22) & 3, ad = (k.alphaEnv[k.numStages - 1] >> 22) & 3;
    if (cd) {
      Add(out, "  prev.rgb = %s.rgb;\n", kDest[cd]);
      regLo[0][0] = regLo[cd][0];
      regHi[0][0] = regHi[cd][0];
    }
    if (ad) {
      Add(out, "  prev.a = %s.a;\n", kDest[ad]);
      regLo[0][1] = regLo[ad][1];
      regHi[0][1] = regHi[ad][1];
    }
  }
  // prev & 255, where it can be outside (fast: clamped, by the framebuffer; the alpha test reads
  // the steps of the clamped alpha)
  if (fast) {
    out += "  float pa = floor(clamp(prev.a, 0.0, 1.0) * 255.0 + 0.5);\n";
  } else {
    if (!(regLo[0][0] >= 0.0 && regHi[0][0] <= 255.0))
      out += "  prev.rgb = wrap8(prev.rgb);\n";
    if (!(regLo[0][1] >= 0.0 && regHi[0][1] <= 255.0))
      out += "  prev.a = wrap8(prev.a);\n";
  }
  // alpha test
  {
    const unsigned f0 = k.alphaTest & 7, f1 = (k.alphaTest >> 3) & 7, logic = (k.alphaTest >> 6) & 3;
    static const char *logicOp[4] = {"&&", "||", "!=", "=="};
    const bool always = (f0 == 7 && f1 == 7) || (logic == 1 && (f0 == 7 || f1 == 7));
    if (!always) {
      std::string t0 = AlphaFunc(f0, "uAlphaRef.x"), t1 = AlphaFunc(f1, "uAlphaRef.y");
      if (fast) {
        for (std::string *t : {&t0, &t1})
          if (t->compare(0, 7, "(prev.a") == 0)
            t->replace(1, 6, "pa");
      }
      Add(out, "  if (!(%s %s %s)) discard;\n", t0.c_str(), logicOp[logic], t1.c_str());
    }
  }
  // fog: uFog[0] = A, C, B magnitude / 2^24, 2^-shift (z * (2^24 - 1) >> shift as a fraction of 2^24)
  {
    const unsigned proj = k.fog & 1, fsel = (k.fog >> 1) & 7;
    if (fsel) {
      if (proj == 0)
        out += "  float ze = uFog[0].x / (uFog[0].z - gl_FragCoord.z * uFog[0].w);\n";
      else
        out += "  float ze = uFog[0].x * gl_FragCoord.z * 0.99999994;\n";
      out += "  float fog = clamp(ze - uFog[0].y, 0.0, 1.0);\n";
      if (fsel == 4)
        out += "  fog = 1.0 - exp2(-8.0 * fog);\n";
      else if (fsel == 5)
        out += "  fog = 1.0 - exp2(-8.0 * fog * fog);\n";
      else if (fsel == 6)
        out += "  fog = exp2(-8.0 * (1.0 - fog));\n";
      else if (fsel == 7)
        out += "  fog = 1.0 - fog;\n  fog = exp2(-8.0 * fog * fog);\n";
      if (fast) {
        out += "  prev.rgb = mix(prev.rgb, uFog[1].rgb * k255, fog);\n";
      } else {
        out += "  float ifog = floor(fog * 256.0 + 0.5);\n";
        out += "  prev.rgb = prev.rgb + floor((uFog[1].rgb - prev.rgb) * (ifog / 256.0));\n";
      }
    }
  }
  out += fast ? "  gl_FragColor = prev;\n" : "  gl_FragColor = prev / 255.0;\n";
  // destination alpha (GXSetDstAlpha) when the renderer lets the shader write it
  out += "  if (uAlphaRef.z >= 0.0) gl_FragColor.a = uAlphaRef.z / 255.0;\n}\n";
  return out;
}

} // namespace re4gx2
