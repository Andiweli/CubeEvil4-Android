// GLSL ES 3.00 from the GX state (gx_gl_shader.h). The arithmetic follows the
// hardware as Dolphin's shader generators describe it: TEV in 8-bit integer
// math with the hardware's lerp rounding, signed 11-bit registers, indirect
// texturing in fixed-point texel coordinates (1/128 texel), lighting and
// texture coordinate generation as the XF unit does them.
#include "gx_gl_shader.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace re4gx {
namespace {

void Add(std::string &out, const char *format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  out += buffer;
}

// The XF block in three parts (kXfParts, gx_gl_shader.h): a draw uploads only the parts that
// changed (the matrices of each model; not the post matrices and lights every time).
const char kUniformBlocks[] =
    "layout(std140) uniform XFM {\n"
    "  vec4 posTex[64];\n"
    "  vec4 nrmMtx[32];\n"
    "};\n"
    "layout(std140) uniform XFP {\n"
    "  vec4 postMtx[64];\n"
    "};\n"
    "layout(std140) uniform XFL {\n"
    "  vec4 lights[40];\n"
    "  vec4 matAmb[4];\n"
    "  vec4 vp[2];\n"
    "  vec4 proj[2];\n"
    "};\n";

const char kPixelBlock[] =
    "layout(std140) uniform PX {\n"
    "  ivec4 colors[4];\n"
    "  ivec4 konst[4];\n"
    "  ivec4 alphaRef;\n"
    "  vec4 fogParam[2];\n"
    "  vec4 coordSize[8];\n"
    "  vec4 mapSize[8];\n"
    "  ivec4 indScale[4];\n"
    "  ivec4 indMtx[6];\n"
    "};\n";

bool Has(const ShaderKey &k, std::uint32_t bit) { return (k.layout & bit) != 0; }

// One lighting channel: `dest` (vec3 or float) from control word `ctrl`.
void Light(std::string &out, const ShaderKey &k, int chan, bool alpha) {
  const std::uint32_t c = k.chan[chan + (alpha ? 2 : 0)];
  const char *sw = alpha ? "a" : "rgb";
  const char *type = alpha ? "float" : "vec3";
  const bool hasColor = Has(k, chan == 0 ? 4u /*COLOR0*/ : 8u /*COLOR1*/);
  Add(out, "  {\n");
  if ((c & 1) && hasColor)
    Add(out, "    %s mat = aClr%d.%s;\n", type, chan, sw);
  else if (c & 1)
    Add(out, "    %s mat = %s(1.0);\n", type, type);
  else
    Add(out, "    %s mat = matAmb[%d].%s;\n", type, chan, sw);
  if (c & 2) {
    if ((c & 64) && hasColor)
      Add(out, "    %s lacc = aClr%d.%s;\n", type, chan, sw);
    else if (c & 64)
      Add(out, "    %s lacc = %s(1.0);\n", type, type);
    else
      Add(out, "    %s lacc = matAmb[%d].%s;\n", type, 2 + chan, sw);
    const unsigned mask = ((c >> 2) & 15) | (((c >> 11) & 15) << 4);
    const unsigned diffuse = (c >> 7) & 3, attn = (c >> 9) & 3;
    for (int i = 0; i < 8; i++) {
      if (!(mask & (1u << i)))
        continue;
      const int b = i * 5; // colour, cos att, dist att, position, direction
      Add(out, "    {\n");
      if (attn == 1) { // specular
        Add(out, "      vec3 ldir = normalize(lights[%d].xyz - pos);\n", b + 3);
        Add(out, "      float at = dot(nrm, ldir) >= 0.0 ? max(0.0, dot(nrm, lights[%d].xyz)) : 0.0;\n", b + 4);
        if (diffuse == 0)
          Add(out, "      vec3 dist = lights[%d].xyz;\n", b + 2);
        else
          Add(out, "      vec3 dist = normalize(lights[%d].xyz);\n", b + 2);
        Add(out,
            "      at = max(0.0, dot(lights[%d].xyz, vec3(1.0, at, at * at))) / dot(dist, vec3(1.0, at, at * "
            "at));\n",
            b + 1);
      } else if (attn == 3) { // spot
        Add(out, "      vec3 ldir = lights[%d].xyz - pos;\n", b + 3);
        Add(out, "      float dist2 = dot(ldir, ldir);\n      float dist = sqrt(dist2);\n");
        Add(out, "      ldir = ldir / dist;\n");
        Add(out, "      float at = max(0.0, dot(ldir, lights[%d].xyz));\n", b + 4);
        Add(out,
            "      at = max(0.0, lights[%d].x + lights[%d].y * at + lights[%d].z * at * at) / "
            "dot(lights[%d].xyz, vec3(1.0, dist, dist2));\n",
            b + 1, b + 1, b + 1, b + 2);
      } else { // none / directional
        Add(out, "      vec3 ldir = normalize(lights[%d].xyz - pos);\n", b + 3);
        Add(out, "      float at = 1.0;\n");
      }
      if (diffuse == 0)
        Add(out, "      lacc += at * lights[%d].%s;\n", b, sw);
      else if (diffuse == 1)
        Add(out, "      lacc += at * dot(ldir, nrm) * lights[%d].%s;\n", b, sw);
      else
        Add(out, "      lacc += at * max(0.0, dot(ldir, nrm)) * lights[%d].%s;\n", b, sw);
      Add(out, "    }\n");
    }
    Add(out, "    col%d.%s = mat * clamp(lacc, 0.0, 1.0);\n", chan, sw);
  } else {
    Add(out, "    col%d.%s = mat;\n", chan, sw);
  }
  Add(out, "  }\n");
}

} // namespace

std::string GxVertexShader(const ShaderKey &k) {
  std::string out = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
  out += kUniformBlocks;
  out += "uniform uvec3 uMtx;\n";
  out += "layout(location = 0) in vec3 aPos;\n";
  if (Has(k, 1u))
    out += "layout(location = 1) in vec3 aNrm;\n";
  if (Has(k, 2u))
    out += "layout(location = 2) in vec3 aBin;\nlayout(location = 3) in vec3 aTan;\n";
  if (Has(k, 4u))
    out += "layout(location = 4) in vec4 aClr0;\n";
  if (Has(k, 8u))
    out += "layout(location = 5) in vec4 aClr1;\n";
  for (int i = 0; i < 8; i++)
    if (Has(k, 16u << i))
      Add(out, "layout(location = %d) in vec2 aTex%d;\n", 6 + i, i);
  if (Has(k, 1u << 12))
    out += "layout(location = 14) in uvec3 aMtx;\n";
  const char *outQ = k.centroid ? "centroid out" : "out";
  Add(out, "%s vec4 vColor0;\n%s vec4 vColor1;\n", outQ, outQ);
  for (unsigned i = 0; i < k.numTexGens; i++)
    Add(out, "%s vec3 vTex%u;\n", outQ, i);
  out += "int mtxByte(uvec3 m, int n) {\n"
         "  uint w = n < 4 ? m.x : n < 8 ? m.y : m.z;\n"
         "  return int((w >> uint(8 * (n & 3))) & 63u);\n"
         "}\n";
  out += "void main() {\n";
  out += Has(k, 1u << 12) ? "  uvec3 m = aMtx;\n" : "  uvec3 m = uMtx;\n";
  out += "  int pi = mtxByte(m, 0);\n";
  out += "  vec4 p4 = vec4(aPos, 1.0);\n";
  out += "  vec3 pos = vec3(dot(posTex[pi], p4), dot(posTex[pi + 1], p4), dot(posTex[pi + 2], p4));\n";
  out += "  vec3 nrm = vec3(0.0, 0.0, 1.0);\n  vec3 vbin = vec3(0.0);\n  vec3 vtan = vec3(0.0);\n";
  if (Has(k, 1u)) {
    out += "  int ni = pi & 31;\n";
    out += "  nrm = vec3(dot(nrmMtx[ni].xyz, aNrm), dot(nrmMtx[ni + 1].xyz, aNrm), dot(nrmMtx[ni + 2].xyz, "
           "aNrm));\n";
    out += "  if (dot(nrm, nrm) > 0.0) nrm = normalize(nrm);\n";
    if (Has(k, 2u)) {
      out += "  vbin = vec3(dot(nrmMtx[ni].xyz, aBin), dot(nrmMtx[ni + 1].xyz, aBin), dot(nrmMtx[ni + 2].xyz, "
             "aBin));\n";
      out += "  vtan = vec3(dot(nrmMtx[ni].xyz, aTan), dot(nrmMtx[ni + 1].xyz, aTan), dot(nrmMtx[ni + 2].xyz, "
             "aTan));\n";
    }
  }
  if (k.orthographic)
    out += "  vec4 clip = vec4(proj[0].x * pos.x + proj[0].y, proj[0].z * pos.y + proj[0].w, proj[1].x * pos.z "
           "+ proj[1].y, 1.0);\n";
  else
    out += "  vec4 clip = vec4(proj[0].x * pos.x + proj[0].y * pos.z, proj[0].z * pos.y + proj[0].w * pos.z, "
           "proj[1].x * pos.z + proj[1].y, -pos.z);\n";
  // GX viewport (EFB pixels + 342) to the GL viewport over the whole EFB; no flip
  out += "  float W = vp[1].z, H = vp[1].w;\n";
  out += "  gl_Position = vec4(((vp[0].w - 342.0) * 2.0 / W - 1.0) * clip.w + vp[0].x * 2.0 / W * clip.x,\n"
         "                     ((vp[1].x - 342.0) * 2.0 / H - 1.0) * clip.w + vp[0].y * 2.0 / H * clip.y,\n"
         "                     2.0 * (vp[0].z * clip.z + vp[1].y * clip.w) / 16777215.0 - clip.w, clip.w);\n"
         "  gl_PointSize = max(1.0, proj[1].w);\n";
  // lighting channels
  out += "  vec4 col0 = vec4(1.0);\n  vec4 col1 = vec4(1.0);\n";
  for (unsigned c = 0; c < 2 && c < k.numChans; c++) {
    Light(out, k, static_cast<int>(c), false);
    Light(out, k, static_cast<int>(c), true);
  }
  if (k.numChans == 0)
    out += Has(k, 4u) ? "  col0 = aClr0;\n" : "  col0 = vec4(1.0);\n";
  if (k.numChans < 2)
    out += Has(k, 8u) ? "  col1 = aClr1;\n" : "  col1 = col0;\n";
  out += "  vColor0 = col0;\n  vColor1 = col1;\n";
  // texture coordinate generation
  for (unsigned i = 0; i < k.numTexGens; i++) {
    const std::uint32_t g = k.texGen[i];
    const unsigned proj = (g >> 1) & 1, form = (g >> 2) & 1, type = (g >> 4) & 7, row = (g >> 7) & 31;
    Add(out, "  {\n    vec4 src = vec4(0.0, 0.0, 1.0, 1.0);\n");
    if (row == 0)
      out += "    src = vec4(aPos, 1.0);\n";
    else if (row == 1 && Has(k, 1u))
      out += "    src = vec4(aNrm, 1.0);\n";
    else if (row == 2)
      out += "    src = col0;\n";
    else if (row == 3 && Has(k, 2u))
      out += "    src = vec4(aTan, 1.0);\n";
    else if (row == 4 && Has(k, 2u))
      out += "    src = vec4(aBin, 1.0);\n";
    else if (row >= 5 && row <= 12 && Has(k, 16u << (row - 5)))
      Add(out, "    src = vec4(aTex%u, 1.0, 1.0);\n", row - 5);
    if (form == 0)
      out += "    src.z = 1.0;\n";
    if (type == 1) { // emboss: the source coordinate moved along the light in tangent space
      const unsigned source = (g >> 12) & 7, light = (g >> 15) & 7;
      if (source < i && Has(k, 2u)) {
        Add(out, "    vec3 ldir = normalize(lights[%u].xyz - pos);\n", light * 5 + 3);
        Add(out, "    vTex%u = vTex%u + vec3(dot(ldir, vtan), dot(ldir, vbin), 0.0);\n", i, source);
      } else if (source < i) {
        Add(out, "    vTex%u = vTex%u;\n", i, source);
      } else {
        Add(out, "    vTex%u = vec3(0.0, 0.0, 1.0);\n", i);
      }
    } else if (type == 2 || type == 3) {
      Add(out, "    vTex%u = vec3(col%u.xy, 1.0);\n", i, type - 2);
    } else {
      Add(out, "    int ti = mtxByte(m, %u);\n", i + 1);
      if (proj)
        Add(out, "    vec3 t = vec3(dot(src, posTex[ti]), dot(src, posTex[ti + 1]), dot(src, posTex[ti + 2]));\n");
      else
        Add(out, "    vec3 t = vec3(dot(src, posTex[ti]), dot(src, posTex[ti + 1]), 1.0);\n");
      if (k.dualTex) {
        const std::uint32_t post = k.postGen[i];
        if (post & 0x100)
          out += "    if (dot(t, t) > 0.0) t = normalize(t);\n";
        const unsigned pr = post & 63;
        Add(out,
            "    t = vec3(dot(postMtx[%u].xyz, t) + postMtx[%u].w, dot(postMtx[%u].xyz, t) + postMtx[%u].w, "
            "dot(postMtx[%u].xyz, t) + postMtx[%u].w);\n",
            pr, pr, pr + 1, pr + 1, pr + 2, pr + 2);
      }
      out += "    if (t.z == 0.0) t.xy = clamp(t.xy / 2.0, vec2(-1.0), vec2(1.0));\n";
      Add(out, "    vTex%u = t;\n", i);
    }
    out += "  }\n";
  }
  out += "}\n";
  return out;
}

namespace {

const char *kColorIn[16] = {"prev.rgb",     "prev.aaa",     "c0.rgb",       "c0.aaa",
                            "c1.rgb",       "c1.aaa",       "c2.rgb",       "c2.aaa",
                            "textemp.rgb",  "textemp.aaa",  "rastemp.rgb",  "rastemp.aaa",
                            "ivec3(255)",   "ivec3(128)",   "konsttemp.rgb", "ivec3(0)"};
const char *kAlphaIn[8] = {"prev.a", "c0.a", "c1.a", "c2.a", "textemp.a", "rastemp.a", "konsttemp.a", "0"};
const char *kDest[4] = {"prev", "c0", "c1", "c2"};
const char *kKonstConst[8] = {"255", "223", "191", "159", "128", "96", "64", "32"};
const char kChannel[4] = {'r', 'g', 'b', 'a'};

std::string KonstColor(unsigned sel) {
  if (sel < 8)
    return std::string("ivec3(") + kKonstConst[sel] + ")";
  if (sel >= 0x0C && sel <= 0x0F)
    return "konst[" + std::to_string(sel - 0x0C) + "].rgb";
  if (sel >= 0x10)
    return "konst[" + std::to_string(sel & 3) + "]." + std::string(3, kChannel[(sel - 0x10) >> 2]);
  return "ivec3(0)";
}

std::string KonstAlpha(unsigned sel) {
  if (sel < 8)
    return kKonstConst[sel];
  if (sel >= 0x10)
    return "konst[" + std::to_string(sel & 3) + "]." + std::string(1, kChannel[(sel - 0x10) >> 2]);
  return "0";
}

std::string Swizzle(std::uint32_t table) {
  std::string s;
  for (int i = 0; i < 4; i++)
    s += kChannel[(table >> (2 * i)) & 3];
  return s;
}

std::atomic<int> gTevMath{kTevMathShift};

// (d + bias) op lerp(a, b, c), scaled, with the hardware's rounding: c is
// widened to 0..256, a scale above one moves inside the lerp, and a rounding
// bias is added before the division by 256 (not with the halving scale).
// Written as SetTevMath says; all of them give the same numbers.
std::string Regular(const char *comp, unsigned bias, unsigned op, unsigned scale) {
  static const char *biasStr[4] = {"", " + 128", " - 128", ""};
  static const char *left[4] = {"", " << 1", " << 2", ""};
  static const char *times[4] = {"", " * 2", " * 4", ""};
  static const char *timesF[4] = {"", " * 2.0", " * 4.0", ""};
  static const char *right[4] = {"", "", "", " >> 1"};
  const std::string c = comp;
  const bool vec = c.size() > 1;
  const std::string d = "((tevin_d." + c + biasStr[bias] + ")" + left[scale] + ")";
  const std::string a = "tevin_a." + c, diff = "(tevin_b." + c + " - tevin_a." + c + ")",
                    factor = "(tevin_c." + c + " + (tevin_c." + c + " >> 7))";
  const char *round = scale != 3 ? (op ? " + 127" : " + 128") : "";
  std::string rounded;
  switch (gTevMath.load()) {
  case kTevMathNoShift:
    rounded = "((" + a + times[scale] + ") + (((" + diff + " * " + factor + ")" + times[scale] + round + ") >> 8))";
    break;
  case kTevMathFloat: {
    const std::string f = vec ? "vec3" : "float", i = vec ? "ivec3" : "int";
    rounded = i + "(floor(((" + f + "(" + a + ") * 256.0 + " + f + "(" + diff + ") * " + f + "(" + factor + "))" +
              timesF[scale] + (scale != 3 ? (op ? " + 127.0" : " + 128.0") : "") + ") / 256.0))";
    break;
  }
  case kTevMathBroken16: {
    const std::string shifted = "(((" + a + " << 8) << 16) >> 16)";
    const std::string lerp = "((" + shifted + " + " + diff + " * " + factor + ")" + left[scale] + ")";
    rounded = "((" + lerp + round + ") >> 8)";
    break;
  }
  default: {
    const std::string lerp = "(((" + a + " << 8) + " + diff + " * " + factor + ")" + left[scale] + ")";
    rounded = "((" + lerp + round + ") >> 8)";
    break;
  }
  }
  return "((" + d + (op ? " - " : " + ") + rounded + ")" + right[scale] + ")";
}

const char *kColorCompare[8] = {
    "((tevin_a.r > tevin_b.r) ? tevin_c.rgb : ivec3(0))",
    "((tevin_a.r == tevin_b.r) ? tevin_c.rgb : ivec3(0))",
    "((idot(tevin_a.rgb, comp16) > idot(tevin_b.rgb, comp16)) ? tevin_c.rgb : ivec3(0))",
    "((idot(tevin_a.rgb, comp16) == idot(tevin_b.rgb, comp16)) ? tevin_c.rgb : ivec3(0))",
    "((idot(tevin_a.rgb, comp24) > idot(tevin_b.rgb, comp24)) ? tevin_c.rgb : ivec3(0))",
    "((idot(tevin_a.rgb, comp24) == idot(tevin_b.rgb, comp24)) ? tevin_c.rgb : ivec3(0))",
    "(ivec3(greaterThan(tevin_a.rgb, tevin_b.rgb)) * tevin_c.rgb)",
    "(ivec3(equal(tevin_a.rgb, tevin_b.rgb)) * tevin_c.rgb)"};
const char *kAlphaCompare[8] = {
    "((tevin_a.r > tevin_b.r) ? tevin_c.a : 0)",
    "((tevin_a.r == tevin_b.r) ? tevin_c.a : 0)",
    "((idot(tevin_a.rgb, comp16) > idot(tevin_b.rgb, comp16)) ? tevin_c.a : 0)",
    "((idot(tevin_a.rgb, comp16) == idot(tevin_b.rgb, comp16)) ? tevin_c.a : 0)",
    "((idot(tevin_a.rgb, comp24) > idot(tevin_b.rgb, comp24)) ? tevin_c.a : 0)",
    "((idot(tevin_a.rgb, comp24) == idot(tevin_b.rgb, comp24)) ? tevin_c.a : 0)",
    "((tevin_a.a > tevin_b.a) ? tevin_c.a : 0)",
    "((tevin_a.a == tevin_b.a) ? tevin_c.a : 0)"};

const char *AlphaFunc(unsigned f, const char *ref) {
  static char buffer[2][64];
  static int n;
  static const char *op[8] = {"false", "<", "==", "<=", ">", "!=", ">=", "true"};
  char *b = buffer[n++ & 1];
  if (f == 0 || f == 7)
    snprintf(b, 64, "%s", op[f]);
  else
    snprintf(b, 64, "(prev.a %s %s)", op[f], ref);
  return b;
}

} // namespace

std::string GxFragmentShader(const ShaderKey &k) {
  std::string out = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
  out += kPixelBlock;
  for (int m = 0; m < 8; m++)
    if (k.texMaps & (1u << m))
      Add(out, "uniform sampler2D s%d;\n", m);
  const char *inQ = k.centroid ? "centroid in" : "in";
  Add(out, "%s vec4 vColor0;\n%s vec4 vColor1;\n", inQ, inQ);
  for (unsigned i = 0; i < k.numTexGens; i++)
    Add(out, "%s vec3 vTex%u;\n", inQ, i);
  out += "out vec4 oColor;\n";
  out += "int idot(ivec3 a, ivec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }\n";
  for (int m = 0; m < 8; m++)
    if (k.texMaps & (1u << m))
      Add(out,
          "ivec4 texmap%d(ivec2 c) {\n"
          "  return ivec4(round(texture(s%d, vec2(c) / (128.0 * mapSize[%d].xy), mapSize[%d].z) * 255.0));\n"
          "}\n",
          m, m, m, m);
  out += "void main() {\n";
  out += "  ivec4 prev = colors[0];\n  ivec4 c0 = colors[1];\n  ivec4 c1 = colors[2];\n  ivec4 c2 = colors[3];\n";
  out += "  ivec4 rastemp = ivec4(0);\n  ivec4 textemp = ivec4(0);\n  ivec4 konsttemp = ivec4(0);\n";
  out += "  ivec4 tevin_a, tevin_b, tevin_c, tevin_d;\n";
  out += "  ivec2 tevcoord = ivec2(0);\n  int alphabump = 0;\n";
  out += "  ivec3 comp16 = ivec3(1, 256, 0);\n  ivec3 comp24 = ivec3(1, 256, 65536);\n";
  out += "  ivec4 ras0 = ivec4(round(clamp(vColor0, 0.0, 1.0) * 255.0));\n";
  out += "  ivec4 ras1 = ivec4(round(clamp(vColor1, 0.0, 1.0) * 255.0));\n";
  for (unsigned i = 0; i < k.numTexGens; i++)
    Add(out,
        "  ivec2 fixuv%u = ivec2((vTex%u.z == 0.0 ? vTex%u.xy : vTex%u.xy / vTex%u.z) * coordSize[%u].xy * "
        "128.0);\n",
        i, i, i, i, i, i);
  // indirect texture lookups
  for (unsigned s = 0; s < k.numIndStages && s < 4; s++) {
    const unsigned map = (k.indRef >> (6 * s)) & 7, coord = (k.indRef >> (6 * s + 3)) & 7;
    if (coord < k.numTexGens && (k.texMaps & (1u << map)))
      Add(out, "  ivec3 iindtex%u = texmap%u(fixuv%u >> indScale[%u].xy).abg;\n", s, map, coord, s);
    else
      Add(out, "  ivec3 iindtex%u = ivec3(0);\n", s);
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
      static const char *fmtMask[4] = {"255", "31", "15", "7"};
      static const char *alphaMask[4] = {"248", "224", "240", "248"};
      static const char *biasField[8] = {"", "x", "y", "xy", "z", "xz", "yz", "xyz"};
      static const char *biasAdd[4] = {"-128", "1", "1", "1"};
      if (bs)
        Add(out, "    alphabump = iindtex%u.%c & %s;\n", bt, "xxyz"[bs], alphaMask[fmt]);
      Add(out, "    ivec3 crd = iindtex%u & %s;\n", bt, fmtMask[fmt]);
      if (bias)
        Add(out, "    crd.%s += %s;\n", biasField[bias], biasAdd[fmt]);
      out += "    ivec2 trans = ivec2(0);\n";
      if (mid >= 1 && mid <= 3) {
        const unsigned r = 2 * (mid - 1);
        Add(out, "    trans = ivec2(idot(indMtx[%u].xyz, crd), idot(indMtx[%u].xyz, crd)) >> 3;\n", r, r + 1);
        Add(out, "    if (indMtx[%u].w >= 0) trans = trans >> indMtx[%u].w; else trans = trans << -indMtx[%u].w;\n", r,
            r, r);
      } else if (mid >= 5 && mid <= 7 && hasCoord) {
        const unsigned r = 2 * (mid - 5);
        Add(out, "    trans = (fixuv%u * crd.xx) >> 8;\n", coord);
        Add(out, "    if (indMtx[%u].w >= 0) trans = trans >> indMtx[%u].w; else trans = trans << -indMtx[%u].w;\n", r,
            r, r);
      } else if (mid >= 9 && mid <= 11 && hasCoord) {
        const unsigned r = 2 * (mid - 9);
        Add(out, "    trans = (fixuv%u * crd.yy) >> 8;\n", coord);
        Add(out, "    if (indMtx[%u].w >= 0) trans = trans >> indMtx[%u].w; else trans = trans << -indMtx[%u].w;\n", r,
            r, r);
      }
      static const char *wrap[8] = {"", "(256 << 7)", "(128 << 7)", "(64 << 7)", "(32 << 7)", "(16 << 7)", "0", "0"};
      if (hasCoord) {
        Add(out, "    ivec2 wrapped = fixuv%u;\n", coord);
        if (sw == 6)
          out += "    wrapped.x = 0;\n";
        else if (sw)
          Add(out, "    wrapped.x = wrapped.x & (%s - 1);\n", wrap[sw]);
        if (tw == 6)
          out += "    wrapped.y = 0;\n";
        else if (tw)
          Add(out, "    wrapped.y = wrapped.y & (%s - 1);\n", wrap[tw]);
      } else {
        out += "    ivec2 wrapped = ivec2(0);\n";
      }
      out += addprev ? "    tevcoord += wrapped + trans;\n" : "    tevcoord = wrapped + trans;\n";
    } else if (hasCoord) {
      Add(out, "    tevcoord = fixuv%u;\n", coord);
    } else {
      out += "    tevcoord = ivec2(0);\n";
    }
    const std::string rswap = Swizzle(k.swap[ac & 3]);
    const std::string tswap = Swizzle(k.swap[(ac >> 2) & 3]);
    if (enable && (k.texMaps & (1u << map)))
      Add(out, "    textemp = texmap%u(tevcoord).%s;\n", map, tswap.c_str());
    else
      out += "    textemp = ivec4(255);\n";
    switch (chan) {
    case 0:
      Add(out, "    rastemp = ras0.%s;\n", rswap.c_str());
      break;
    case 1:
      Add(out, "    rastemp = ras1.%s;\n", rswap.c_str());
      break;
    case 5:
      out += "    rastemp = ivec4(alphabump);\n";
      break;
    case 6:
      out += "    rastemp = ivec4(alphabump | (alphabump >> 5));\n";
      break;
    default:
      out += "    rastemp = ivec4(0);\n";
      break;
    }
    const unsigned kc = k.konst[n] & 0xFF, ka = (k.konst[n] >> 8) & 0xFF;
    Add(out, "    konsttemp = ivec4(%s, %s);\n", KonstColor(kc).c_str(), KonstAlpha(ka).c_str());
    Add(out, "    tevin_a = ivec4(%s, %s) & 255;\n", kColorIn[(cc >> 12) & 15], kAlphaIn[(ac >> 13) & 7]);
    Add(out, "    tevin_b = ivec4(%s, %s) & 255;\n", kColorIn[(cc >> 8) & 15], kAlphaIn[(ac >> 10) & 7]);
    Add(out, "    tevin_c = ivec4(%s, %s) & 255;\n", kColorIn[(cc >> 4) & 15], kAlphaIn[(ac >> 7) & 7]);
    Add(out, "    tevin_d = ivec4(%s, %s);\n", kColorIn[cc & 15], kAlphaIn[(ac >> 4) & 7]);
    // colour
    {
      const unsigned b = (cc >> 16) & 3, op = (cc >> 18) & 1, clampOut = (cc >> 19) & 1, scale = (cc >> 20) & 3,
                     dest = (cc >> 22) & 3;
      std::string v;
      if (b == 3)
        v = std::string("(tevin_d.rgb + ") + kColorCompare[op | scale << 1] + ")";
      else
        v = Regular("rgb", b, op, scale);
      Add(out, "    %s.rgb = clamp(%s, %s);\n", kDest[dest], v.c_str(),
          clampOut ? "ivec3(0), ivec3(255)" : "ivec3(-1024), ivec3(1023)");
    }
    // alpha
    {
      const unsigned b = (ac >> 16) & 3, op = (ac >> 18) & 1, clampOut = (ac >> 19) & 1, scale = (ac >> 20) & 3,
                     dest = (ac >> 22) & 3;
      std::string v;
      if (b == 3)
        v = std::string("(tevin_d.a + ") + kAlphaCompare[op | scale << 1] + ")";
      else
        v = Regular("a", b, op, scale);
      Add(out, "    %s.a = clamp(%s, %s);\n", kDest[dest], v.c_str(), clampOut ? "0, 255" : "-1024, 1023");
    }
    out += "  }\n";
  }
  if (k.numStages) {
    const unsigned cd = (k.colorEnv[k.numStages - 1] >> 22) & 3, ad = (k.alphaEnv[k.numStages - 1] >> 22) & 3;
    if (cd)
      Add(out, "  prev.rgb = %s.rgb;\n", kDest[cd]);
    if (ad)
      Add(out, "  prev.a = %s.a;\n", kDest[ad]);
  }
  out += "  prev = prev & 255;\n";
  // alpha test
  {
    const unsigned f0 = k.alphaTest & 7, f1 = (k.alphaTest >> 3) & 7, logic = (k.alphaTest >> 6) & 3;
    static const char *logicOp[4] = {"&&", "||", "!=", "=="};
    const bool always = (f0 == 7 && f1 == 7) || (logic == 1 && (f0 == 7 || f1 == 7));
    if (!always)
      Add(out, "  if (!(%s %s %s)) discard;\n", AlphaFunc(f0, "alphaRef.x"), logicOp[logic],
          AlphaFunc(f1, "alphaRef.y"));
  }
  // fog
  {
    const unsigned proj = k.fog & 1, fsel = (k.fog >> 1) & 7;
    if (fsel) {
      out += "  int zc = int(gl_FragCoord.z * 16777215.0);\n";
      if (proj == 0)
        out += "  float ze = (fogParam[0].x * 16777216.0) / (fogParam[0].z - float(zc >> int(fogParam[0].w)));\n";
      else
        out += "  float ze = fogParam[0].x * float(zc) / 16777216.0;\n";
      out += "  float fog = clamp(ze - fogParam[0].y, 0.0, 1.0);\n";
      if (fsel == 4)
        out += "  fog = 1.0 - exp2(-8.0 * fog);\n";
      else if (fsel == 5)
        out += "  fog = 1.0 - exp2(-8.0 * fog * fog);\n";
      else if (fsel == 6)
        out += "  fog = exp2(-8.0 * (1.0 - fog));\n";
      else if (fsel == 7)
        out += "  fog = 1.0 - fog;\n  fog = exp2(-8.0 * fog * fog);\n";
      out += "  int ifog = int(round(fog * 256.0));\n";
      if (gTevMath.load() == kTevMathShift || gTevMath.load() == kTevMathBroken16)
        out += "  prev.rgb = (prev.rgb * (256 - ifog) + ivec3(fogParam[1].xyz) * ifog) >> 8;\n";
      else // the same numbers without prev * 256 (SetTevMath)
        out += "  prev.rgb += ((ivec3(fogParam[1].xyz) - prev.rgb) * ifog) >> 8;\n";
    }
  }
  out += "  oColor = vec4(prev) / 255.0;\n";
  // destination alpha (GXSetDstAlpha) when the renderer lets the shader write it
  out += "  if (alphaRef.z >= 0) oColor.a = float(alphaRef.z) / 255.0;\n}\n";
  return out;
}

void SetTevMath(int mode) { gTevMath = mode >= kTevMathShift && mode <= kTevMathBroken16 ? mode : kTevMathShift; }

int GetTevMath() { return gTevMath.load(); }

const char *TevMathName(int mode) {
  switch (mode) {
  case kTevMathShift:
    return "the lerp as the hardware computes it";
  case kTevMathNoShift:
    return "the lerp without the shift by 8";
  case kTevMathFloat:
    return "the lerp in floats";
  case kTevMathBroken16:
    return "the lerp broken to 16 bits (test)";
  default:
    return "?";
  }
}

} // namespace re4gx
