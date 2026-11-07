#include "gx_gl2.h"

#include "gx_gl2_shader.h"
#include "gx_texture.h"
#include "re4_gx_frame.h"

#include <GLES2/gl2ext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifndef GL_DEPTH_COMPONENT16_NONLINEAR_NV
#define GL_DEPTH_COMPONENT16_NONLINEAR_NV 0x8E2C
#endif
#ifndef GL_DEPTH_COMPONENT24_OES
#define GL_DEPTH_COMPONENT24_OES 0x81A6
#endif

#define GXLOG(...) re4gx2::LogTo("RE4GX2", __VA_ARGS__)

namespace re4gx2 {

namespace {
std::atomic<void (*)(const char *)> gLogCopy{nullptr};
} // namespace

void SetLogCopy(void (*copy)(const char *line)) { gLogCopy = copy; }

void LogTo(const char *tag, const char *fmt, ...) {
  // long lines whole (a shader that failed); logcat cuts its own at about 4 KB
  std::vector<char> text(4096);
  va_list ap;
  va_start(ap, fmt);
  va_list again;
  va_copy(again, ap);
  const int n = std::vsnprintf(text.data(), text.size(), fmt, ap);
  va_end(ap);
  if (n >= static_cast<int>(text.size())) {
    text.resize(static_cast<std::size_t>(n) + 1);
    std::vsnprintf(text.data(), text.size(), fmt, again);
  }
  va_end(again);
#ifdef __ANDROID__
  __android_log_write(ANDROID_LOG_INFO, tag, text.data());
#else
  std::fprintf(stderr, "[%s] %s\n", tag, text.data());
#endif
  if (auto copy = gLogCopy.load()) {
    const std::string line = std::string(tag) + ": " + text.data();
    copy(line.c_str());
  }
}

namespace {

// `name` in the extension string `ext` as a whole word
bool HasExtension(const char *ext, const char *name) {
  if (!ext)
    return false;
  const std::size_t n = std::strlen(name);
  for (const char *at = std::strstr(ext, name); at; at = std::strstr(at + n, name))
    if ((at == ext || at[-1] == ' ') && (at[n] == ' ' || at[n] == '\0'))
      return true;
  return false;
}

constexpr int kEfbWidth = 640;
constexpr int kEfbHeight = 528;
constexpr unsigned kTlutBytes = 0x80000;

float AsFloat(std::uint32_t v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

std::uint64_t Fnv(const void *data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
  const auto *p = static_cast<const std::uint8_t *>(data);
  for (std::size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

int Signed11(std::uint32_t v) { return (v & 0x400) ? static_cast<int>(v & 0x7FF) - 2048 : static_cast<int>(v & 0x7FF); }

bool PowerOfTwo(unsigned v) { return v && (v & (v - 1)) == 0; }

GLuint Compile(GLenum type, const std::string &source, std::string *log) {
  GLuint s = glCreateShader(type);
  const char *text = source.c_str();
  glShaderSource(s, 1, &text, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buffer[4096];
    glGetShaderInfoLog(s, sizeof(buffer), nullptr, buffer);
    if (log)
      *log = std::string(type == GL_VERTEX_SHADER ? "vertex shader: " : "fragment shader: ") + buffer;
    glDeleteShader(s);
    return 0;
  }
  return s;
}

// `attributes`: names in location order (nullptr ends)
GLuint Link(const std::string &vs, const std::string &fs, const char *const *attributes, int count, std::string *log) {
  GLuint v = Compile(GL_VERTEX_SHADER, vs, log);
  if (!v)
    return 0;
  GLuint f = Compile(GL_FRAGMENT_SHADER, fs, log);
  if (!f) {
    glDeleteShader(v);
    return 0;
  }
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  for (int i = 0; i < count; i++)
    glBindAttribLocation(p, static_cast<GLuint>(i), attributes[i]);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buffer[4096];
    glGetProgramInfoLog(p, sizeof(buffer), nullptr, buffer);
    if (log)
      *log = std::string("link: ") + buffer;
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

// The renderer's own passes: a quad over the viewport from attribute 0 (0..1 in x and y).
const char kQuadVs[] = "attribute vec2 aQuad;\nvarying vec2 vUv;\n"
                       "void main() {\n"
                       "  vUv = aQuad;\n"
                       "  gl_Position = vec4(aQuad * 2.0 - 1.0, 0.0, 1.0);\n"
                       "}\n";
const char kFragmentPrecision[] = "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\nprecision highp sampler2D;\n"
                                  "#else\nprecision mediump float;\nprecision mediump sampler2D;\n#endif\n";

// A generated program, its uniforms, and how far it has the state (uniforms are per program in
// GLES 2: each keeps the values it was given; `seen` is the state stamp it has caught up with).
struct Program {
  GLuint id = 0;
  ShaderInfo info{};
  GLint posTex[64] = {}, nrm[32] = {}, post[24] = {}, light[8] = {};
  GLint matAmb = -1, vp = -1, proj = -1, mtxDef = -1, dequant = -1, tcScale = -1;
  GLint colors = -1, konst = -1, alphaRef = -1, fog = -1, lodBias = -1, mapScale = -1, indScale = -1, indMtx = -1;
  GLint colorsW = -1, konstW = -1;
  GLint fold = -1;
  std::uint64_t seen = 0;
  // ShaderKey::lightFold: the lights in its slots and the light state then, the fold sent
  int lightSel[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  std::uint64_t lightSelSeen = 0;
  float foldSent[8] = {};
  float mtxDefSent[12] = {};
  bool mtxDefValid = false;
  float tcScaleSent[16] = {};
};

struct KeyHash {
  std::size_t operator()(const ShaderKey &k) const { return static_cast<std::size_t>(Fnv(&k, sizeof(k))); }
};
struct KeyEqual {
  bool operator()(const ShaderKey &a, const ShaderKey &b) const { return std::memcmp(&a, &b, sizeof(a)) == 0; }
};

// A texture as the renderer keeps it: decoded RGBA8 with its levels (a full chain or one).
struct Texture {
  GLuint id = 0;
  unsigned width = 0, height = 0, levels = 1;
};

// An EFB copy to a texture (gx_gl.cpp Copy): the texels as the matching texture format reads them,
// for paletted reads the raw value.
struct Copy {
  GLuint id = 0;
  unsigned width = 0, height = 0;
  unsigned serial = 0;
  unsigned kind = 0; // 0 colour, 1 one channel (value in .r), 2 two channels (.a high, .r low)
};

// Sampling parameters as last set on a texture object (GLES 2 has no sampler objects).
struct TexParams {
  GLint wrapS = -1, wrapT = -1, minFilter = -1, magFilter = -1;
  bool npotCounted = false;
};

// One vertex input of the draw's layout: where and as what GL reads it.
struct AttrFormat {
  bool present = false;
  GLint size = 0;
  GLenum type = GL_FLOAT;
  GLboolean normalized = GL_FALSE;
  std::uint32_t offset = 0;
};

} // namespace

struct GxRenderer::Impl {
  // the EFB's resolution in eighths of the GameCube's 640x528 (8: 1x; 6: 0.75x, 480x396; Init's
  // scale times 8, or GxOptions::resolutionEighths, RE4_GX2_SCALE)
  int eighths = 8;
  // a length in GameCube pixels in EFB pixels (rounded; edges through S, sizes as differences)
  int S(int v) const { return (v * eighths + 4) / 8; }
  GxStats stats;
  GxCaps caps;
  GxOptions options;

  // register state
  std::uint32_t bp[256] = {};
  std::uint32_t xfMem[0x800] = {};
  std::uint32_t xfReg[0x58] = {};
  std::vector<std::uint8_t> tlut = std::vector<std::uint8_t>(kTlutBytes);
  int tevColor[4][4] = {};
  int tevKonst[4][4] = {};
  std::uint32_t compactWords[RE4_GX_COMPACT_ATTRS] = {};
  bool compactValid = false;

  // --- the uniforms' values and when they changed --------------------------------------------------
  // XF memory and registers as the GLES 3 renderer's XF block, the BP state as its PX block; each
  // part carries the stamp of its last change (stamp counts every change), a program uploads what
  // changed after the stamp it has seen.
  float posTex[64][4] = {}, normal[32][4] = {}, post[64][4] = {}, light[8][5][4] = {};
  float matAmb[4][4] = {}, viewport[2][4] = {}, proj[2][4] = {};
  float colors[4][4] = {}, konst[4][4] = {}, alphaRef[4] = {0, 0, -1, 0}, fog[2][4] = {};
  float colorsW[4][4] = {}, konstW[4][4] = {};
  float shaderAlpha = -1.0f; // the destination alpha the shader writes (ApplyState; -1: none)
  float lodBias[8] = {}, mapScale[8][4] = {}, coordSize[8][2] = {}, indScale[4][4] = {}, indMtx[6][4] = {};
  float dequant[12] = {};
  std::uint64_t stamp = 1;
  std::uint64_t posTexStamp[64], normalStamp[32], postStamp[64], lightStamp[8];
  std::uint64_t posTexNewest = 1, normalNewest = 1, postNewest = 1, lightNewest = 1;
  std::uint64_t xfRegStamp = 1, pixelStamp = 1, texStamp = 1, indStamp = 1, dequantStamp = 1;
  bool xfRegsDirty = true, pxDirty = true, texDirty = true, indDirty = true;

  // GL objects
  GLuint efbFbo = 0, efbColor = 0, efbDepthTex = 0, efbDepthRb = 0;
  GLuint xfbTex = 0;
  unsigned xfbWidth = 0, xfbHeight = 0;
  bool haveXfb = false;
  bool holdXfb = false;  // this chunk's display copy is not shown (RE4_GX_CHUNK_HOLD)
  bool viBlack = false;  // the VI shows black (RE4_GX_CHUNK_BLACK): Present draws black
  GLuint vbo = 0, ibo = 0, quadVbo = 0;
  GLuint presentProgram = 0, paletteProgram = 0;
  std::unordered_map<GLuint, GLuint> targetFbos;  // by the texture they draw into
  GLint presentRows = -1;
  GLint paletteSize = -1, paletteKind = -1, paletteFormat = -1;
  GLuint whiteTex = 0;
  std::unordered_map<ShaderKey, Program, KeyHash, KeyEqual> programs;
  std::unordered_map<std::uint64_t, Texture> textures;
  // Textures a skipped chunk brought (Skip): their texels as they came, decoded when a drawn chunk
  // binds them (a movie brings two new ones a picture; decoding those of the pictures left out cost
  // the OUYA about 100 ms a picture, its films ran at 1 picture a second, 07.11.2026)
  struct Deferred {
    Re4GxTexture t;
    std::vector<std::uint8_t> texels;
  };
  std::unordered_map<std::uint64_t, Deferred> deferred;
  std::unordered_map<std::uint32_t, Copy> copies;       // by destination address
  std::unordered_map<std::uint64_t, Texture> paletted;  // copies read through a palette
  std::unordered_map<GLuint, TexParams> texParams;      // by GL texture
  std::uint32_t copySerial = 0;

  // --- what GL already has ---------------------------------------------------------------------
  bool keyDirty = true;
  // GxOptions::maxLights: the lights of the lit channels of the current key (all the same), its
  // diffuse and attenuation functions
  std::uint32_t foldMask = 0, foldDiffuse = 0, foldAttn = 0;
  std::uint32_t keyLayout = ~0u;
  ShaderKey key = {};
  Program *keyProgram = nullptr;
  Program *current = nullptr;
  std::uint32_t stateSig[10] = {};
  bool stateValid = false;
  GLuint unitTex[8] = {};
  unsigned activeUnit = ~0u;
  bool unitsValid = false;
  std::uint32_t attribMask = 0;  // enabled vertex attribute arrays (by location)
  AttrFormat attr[kAttrSources];
  std::uint32_t attrLayout = ~0u;  // the layout `attr` describes
  std::uint32_t probeStride = 0;   // the "noattrib" measurement: the pointers' stride and program
  const Program *probeProgram = nullptr;
  bool attrDirty = true;           // the compact format changed

  struct Bound {
    GLuint id = 0;
    unsigned width = 1, height = 1, levels = 1;
    std::uint32_t copy = 0; // an EFB copy: its address (CanSkip)
  } bound[8];
  bool skipDraws = false; // Skip: this chunk's draws and copies are left out

  // Other passes changed GL state.
  void Touched() {
    stateValid = false;
    unitsValid = false;
    current = nullptr;
  }

  std::uint64_t Next() { return ++stamp; }

  // --- state -------------------------------------------------------------------------------------

  // The copy filter's gain (gx_gl.cpp CopyFilterGain): the seven vertical taps' sum against 64.
  float CopyFilterGain() const {
    const std::uint32_t c0 = bp[0x53], c1 = bp[0x54];
    unsigned sum = 0;
    for (unsigned i = 0; i < 4; i++)
      sum += (c0 >> (6 * i)) & 63;
    for (unsigned i = 0; i < 3; i++)
      sum += (c1 >> (6 * i)) & 63;
    return sum == 0 ? 1.0f : static_cast<float>(sum) / 64.0f;
  }

  void BpWrite(std::uint32_t word, bool action) {
    const unsigned reg = word >> 24;
    const std::uint32_t value = word & 0xFFFFFF;
    if (bp[reg] != value || !action) {
      // registers the generated shader depends on (MakeKey)
      if (reg == 0x00 || (reg >= 0x10 && reg <= 0x1F) || (reg >= 0x27 && reg <= 0x2F) ||
          (reg >= 0xC0 && reg <= 0xDF) || reg == 0xF1 || reg == 0xF3 || (reg >= 0xF6 && reg <= 0xFD))
        keyDirty = true;
      if ((reg >= 0xE0 && reg <= 0xE7) || (reg >= 0xEE && reg <= 0xF3))
        pxDirty = true;
      if ((reg >= 0x30 && reg <= 0x3F) || (reg >= 0x80 && reg <= 0x87) || (reg >= 0xA0 && reg <= 0xA7))
        texDirty = true;
      if ((reg >= 0x06 && reg <= 0x0E) || reg == 0x25 || reg == 0x26)
        indDirty = true;
    }
    bp[reg] = value;
    if (reg >= 0xE0 && reg <= 0xE7) {
      const unsigned i = (reg - 0xE0) / 2;
      int *dst = (value >> 23) & 1 ? tevKonst[i] : tevColor[i];
      if ((reg & 1) == 0) { // RA
        dst[0] = Signed11(value);
        dst[3] = Signed11(value >> 12);
      } else { // BG
        dst[2] = Signed11(value);
        dst[1] = Signed11(value >> 12);
      }
    }
    if (action && reg == 0x52) {
      if (!skipDraws) {
        EfbCopy(value);
      } else if ((value >> 14) & 1) {
        // a skipped picture: nothing to show, only the clear of the EFB the next picture needs
        stats.skippedChunks++;
        Touched();
        PlainState();
        glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
        ClearAfterCopy(value, static_cast<int>(bp[0x49] & 0x3FF), static_cast<int>((bp[0x49] >> 10) & 0x3FF),
                       static_cast<int>(bp[0x4A] & 0x3FF) + 1, static_cast<int>((bp[0x4A] >> 10) & 0x3FF) + 1);
      } // a copy for a texture: CanSkip said the next chunk copies there first, nobody reads this one
    }
    if (action && reg == 0x5A)
      stats.autofocusMarks++;
  }

  void XfWrite(std::uint32_t address, const std::uint32_t *words, std::uint32_t n) {
    const std::uint64_t now = Next();
    for (std::uint32_t i = 0; i < n; i++) {
      const std::uint32_t a = address + i;
      if (a < 0x800) {
        xfMem[a] = words[i];
        XfMemToArrays(a, now);
      } else if (a >= 0x1000 && a < 0x1058) {
        const std::uint32_t r = a - 0x1000;
        xfReg[r] = words[i];
        if (r == 0x09 || (r >= 0x0E && r <= 0x12) || r == 0x26 || r >= 0x3F)
          keyDirty = true;
        xfRegsDirty = true;
      }
    }
  }

  // One word of XF memory into the uniform arrays.
  void XfMemToArrays(std::uint32_t a, std::uint64_t now) {
    if (a < 0x100) {
      posTex[a / 4][a % 4] = AsFloat(xfMem[a]);
      posTexStamp[a / 4] = posTexNewest = now;
    } else if (a >= 0x400 && a < 0x460) {
      normal[(a - 0x400) / 3][(a - 0x400) % 3] = AsFloat(xfMem[a]);
      normalStamp[(a - 0x400) / 3] = normalNewest = now;
    } else if (a >= 0x500 && a < 0x600) {
      post[(a - 0x500) / 4][(a - 0x500) % 4] = AsFloat(xfMem[a]);
      postStamp[(a - 0x500) / 4] = postNewest = now;
    } else if (a >= 0x600 && a < 0x680) {
      const unsigned l = (a - 0x600) / 16, w = (a - 0x600) % 16;
      if (w == 3) {
        for (int c = 0; c < 4; c++)
          light[l][0][c] = static_cast<float>((xfMem[a] >> (24 - 8 * c)) & 255) / 255.0f;
      } else if (w >= 4) {
        light[l][1 + (w - 4) / 3][(w - 4) % 3] = AsFloat(xfMem[a]);
      }
      lightStamp[l] = lightNewest = now;
    }
  }

  // Material and ambient colours, viewport, projection (gx_gl.cpp FillXfRegs).
  void FillXfRegs() {
    const std::uint32_t colorRegs[4] = {xfReg[0x0C], xfReg[0x0D], xfReg[0x0A], xfReg[0x0B]};
    for (int i = 0; i < 4; i++)
      for (int c = 0; c < 4; c++)
        matAmb[i][c] = static_cast<float>((colorRegs[i] >> (24 - 8 * c)) & 255) / 255.0f;
    // the GX viewport (EFB pixels + 342) in x and y as factors on the clip coordinates (the vertex
    // shader's gl_Position), worked out here, not per vertex (the Tegra 3 counts each
    // instruction); depth as the registers say: the shader rounds it as the GLES 3 renderer does,
    // pictures at the far end (z = w) stay in
    // The console samples a pixel at 7/12 of its width and height, GLES at 1/2 (Dolphin's pixel
    // centre correction, the GLES 3 renderer's since 29.10.2026): everything 1/12 of a GameCube pixel
    // up and left, so a line thinner than a pixel on a pixel's edge (the rifle scope's at 1x) is drawn
    const double w = kEfbWidth, h = kEfbHeight;
    viewport[0][0] = static_cast<float>(AsFloat(xfReg[0x1A]) * 2.0 / w);
    viewport[0][1] = static_cast<float>((AsFloat(xfReg[0x1D]) - 342.0) * 2.0 / w - 1.0 - 1.0 / (6.0 * w));
    viewport[0][2] = static_cast<float>(AsFloat(xfReg[0x1B]) * 2.0 / h);
    viewport[0][3] = static_cast<float>((AsFloat(xfReg[0x1E]) - 342.0) * 2.0 / h - 1.0 - 1.0 / (6.0 * h));
    viewport[1][0] = AsFloat(xfReg[0x1C]);
    viewport[1][1] = AsFloat(xfReg[0x1F]);
    viewport[1][2] = viewport[1][3] = 0.0f;
    for (int i = 0; i < 6; i++)
      proj[i / 4][i % 4] = AsFloat(xfReg[0x20 + i]);
    proj[1][2] = static_cast<float>(xfReg[0x26]);
    xfRegsDirty = false;
    xfRegStamp = Next();
  }

  // TEV registers, konstants, alpha references, fog (gx_gl.cpp FillPixel); a new stamp when
  // anything differs.
  void FillPixel() {
    pxDirty = false;
    float c[4][4], k[4][4], ref[4], f[2][4];
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++) {
        c[i][j] = static_cast<float>(tevColor[i][j]);
        k[i][j] = static_cast<float>(tevKonst[i][j]);
      }
    ref[0] = static_cast<float>(bp[0xF3] & 0xFF);
    ref[1] = static_cast<float>((bp[0xF3] >> 8) & 0xFF);
    ref[2] = shaderAlpha;
    ref[3] = 0.0f;
    const auto fogFloat = [](std::uint32_t v) {
      const std::uint32_t bits = ((v >> 19) & 1) << 31 | ((v >> 11) & 0xFF) << 23 | (v & 0x7FF) << 12;
      return AsFloat(bits);
    };
    f[0][0] = fogFloat(bp[0xEE]);
    f[0][1] = fogFloat(bp[0xF1]);
    f[0][2] = static_cast<float>(bp[0xEF] & 0xFFFFFF) / 16777216.0f;
    f[0][3] = std::ldexp(16777215.0f / 16777216.0f, -static_cast<int>(bp[0xF0] & 31));
    f[1][0] = static_cast<float>((bp[0xF2] >> 16) & 255);
    f[1][1] = static_cast<float>((bp[0xF2] >> 8) & 255);
    f[1][2] = static_cast<float>(bp[0xF2] & 255);
    f[1][3] = 0.0f;
    if (std::memcmp(c, colors, sizeof(c)) || std::memcmp(k, konst, sizeof(k)) || std::memcmp(ref, alphaRef, sizeof(ref)) ||
        std::memcmp(f, fog, sizeof(f))) {
      std::memcpy(colors, c, sizeof(c));
      std::memcpy(konst, k, sizeof(k));
      std::memcpy(alphaRef, ref, sizeof(ref));
      std::memcpy(fog, f, sizeof(f));
      // & 255: the TEV's inputs a, b, c read registers and konstants so (the shader then wraps
      // only what its stages wrote)
      for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
          colorsW[i][j] = static_cast<float>(tevColor[i][j] & 255);
          konstW[i][j] = static_cast<float>(tevKonst[i][j] & 255);
        }
      pixelStamp = Next();
    }
  }

  // Coordinate sizes (BP 0x30+), map sizes (the bound textures), LOD biases.
  void FillTex() {
    texDirty = false;
    float cs[8][2], ms[8][4], lb[8];
    for (int i = 0; i < 8; i++) {
      cs[i][0] = static_cast<float>((bp[0x30 + 2 * i] & 0xFFFF) + 1);
      cs[i][1] = static_cast<float>((bp[0x31 + 2 * i] & 0xFFFF) + 1);
      const unsigned mode0 = bp[(i < 4 ? 0x80 : 0xA0) + (i & 3)];
      int bias = static_cast<int>((mode0 >> 9) & 0xFF);
      if (bias & 0x80)
        bias -= 256;
      lb[i] = static_cast<float>(bias) / 32.0f;
      ms[i][0] = 1.0f / (128.0f * static_cast<float>(bound[i].width));
      ms[i][1] = 1.0f / (128.0f * static_cast<float>(bound[i].height));
      ms[i][2] = static_cast<float>(bound[i].width);
      ms[i][3] = static_cast<float>(bound[i].height);
    }
    if (std::memcmp(cs, coordSize, sizeof(cs)) || std::memcmp(ms, mapScale, sizeof(ms)) ||
        std::memcmp(lb, lodBias, sizeof(lb))) {
      std::memcpy(coordSize, cs, sizeof(cs));
      std::memcpy(mapScale, ms, sizeof(ms));
      std::memcpy(lodBias, lb, sizeof(lb));
      texStamp = Next();
    }
  }

  // Indirect scales and matrices (gx_gl.cpp FillPixel): factors 2^-shift, matrices with their shift.
  void FillInd() {
    indDirty = false;
    float sc[4][4], mt[6][4];
    for (int s = 0; s < 4; s++) {
      const std::uint32_t r = bp[0x25 + s / 2] >> ((s & 1) ? 8 : 0);
      sc[s][0] = std::ldexp(1.0f, -static_cast<int>(r & 15));
      sc[s][1] = std::ldexp(1.0f, -static_cast<int>((r >> 4) & 15));
      sc[s][2] = sc[s][3] = 0.0f;
    }
    for (int m = 0; m < 3; m++) {
      const std::uint32_t a = bp[0x06 + 3 * m], b = bp[0x07 + 3 * m], c = bp[0x08 + 3 * m];
      const int shift = static_cast<int>(((a >> 22) & 3) | ((b >> 22) & 3) << 2 | ((c >> 22) & 3) << 4);
      mt[2 * m][0] = static_cast<float>(Signed11(a));
      mt[2 * m][1] = static_cast<float>(Signed11(b));
      mt[2 * m][2] = static_cast<float>(Signed11(c));
      mt[2 * m][3] = static_cast<float>(17 - shift);
      mt[2 * m + 1][0] = static_cast<float>(Signed11(a >> 11));
      mt[2 * m + 1][1] = static_cast<float>(Signed11(b >> 11));
      mt[2 * m + 1][2] = static_cast<float>(Signed11(c >> 11));
      mt[2 * m + 1][3] = static_cast<float>(17 - shift);
    }
    if (std::memcmp(sc, indScale, sizeof(sc)) || std::memcmp(mt, indMtx, sizeof(mt))) {
      std::memcpy(indScale, sc, sizeof(sc));
      std::memcpy(indMtx, mt, sizeof(mt));
      indStamp = Next();
    }
  }

  // The compact vertex format (RE4_GX_OP_COMPACT_FORMAT): attribute pointers and the scales of
  // the fraction bits.
  void CompactFormat(const std::uint32_t *words) {
    if (std::memcmp(words, compactWords, sizeof(compactWords)) != 0 || !compactValid) {
      std::memcpy(compactWords, words, sizeof(compactWords));
      keyDirty = true;  // the matrix slots the vertex carries shape the shader
      attrDirty = true;
    }
    compactValid = true;
    float dq[12] = {};
    const int words12[10] = {RE4_GX_CA_POSITION, RE4_GX_CA_NORMAL, RE4_GX_CA_TEX0,     RE4_GX_CA_TEX0 + 1,
                             RE4_GX_CA_TEX0 + 2, RE4_GX_CA_TEX0 + 3, RE4_GX_CA_TEX0 + 4, RE4_GX_CA_TEX0 + 5,
                             RE4_GX_CA_TEX0 + 6, RE4_GX_CA_TEX0 + 7};
    for (int i = 0; i < 10; i++) {
      const std::uint32_t w = words[words12[i]];
      const unsigned type = (w >> 8) & 15, fraction = (w >> 16) & 31;
      dq[i] = (w & RE4_GX_CA_PRESENT) && type != RE4_GX_CT_F32 ? std::ldexp(1.0f, -static_cast<int>(fraction)) : 1.0f;
    }
    if (std::memcmp(dq, dequant, sizeof(dq)) != 0) {
      std::memcpy(dequant, dq, sizeof(dq));
      dequantStamp = Next();
    }
  }

  // --- textures ----------------------------------------------------------------------------------

  // A whole mipmap chain for GLES 2 (no TEXTURE_MAX_LEVEL): the levels below the last one GX gave,
  // each the 2x2 average of the one before.
  // One texel of each 2x2, not their mean: GX samples its last level on in the distance, so the
  // colours and the alpha a test cuts at stay those of that level (a mean turns cut-out foliage
  // half transparent, and the alpha test then thins or thickens it far away; the r100 frames on
  // the PC differed by up to 123 levels there).
  static void NextLevel(const std::vector<std::uint8_t> &in, unsigned w, unsigned h, std::vector<std::uint8_t> &out) {
    const unsigned ow = std::max(1u, w / 2), oh = std::max(1u, h / 2);
    out.assign(static_cast<std::size_t>(ow) * oh * 4, 0);
    for (unsigned y = 0; y < oh; y++)
      for (unsigned x = 0; x < ow; x++) {
        const unsigned sx = std::min(w - 1, x * 2), sy = std::min(h - 1, y * 2);
        std::memcpy(&out[(static_cast<std::size_t>(y) * ow + x) * 4], &in[(static_cast<std::size_t>(sy) * w + sx) * 4], 4);
      }
  }

  // GxOptions::tex16: how a texture goes to the GPU, from its first level: 0 RGBA8, 1 RGB565
  // (opaque), 2 RGBA5551 (alpha only 0 and 255)
  int TextureForm(const std::vector<std::uint8_t> &rgba) const {
    if (!options.tex16)
      return 0;
    bool opaque = true, cut = true;
    for (std::size_t i = 3; i < rgba.size() && cut; i += 4) {
      opaque = opaque && rgba[i] == 255;
      cut = rgba[i] == 0 || rgba[i] == 255;
    }
    return opaque ? 1 : cut ? 2 : 0;
  }

  // One level in the form TextureForm chose
  void TexImage(GLint level, unsigned w, unsigned h, const std::vector<std::uint8_t> &rgba, int form) {
    if (form == 0) {
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, rgba.data());
      return;
    }
    const std::size_t n = static_cast<std::size_t>(w) * h;
    texels16.resize(n);
    for (std::size_t i = 0; i < n; i++) {
      const std::uint8_t *p = &rgba[i * 4];
      const unsigned r5 = (p[0] * 31u + 127u) / 255u, b5 = (p[2] * 31u + 127u) / 255u;
      if (form == 1)
        texels16[i] = static_cast<std::uint16_t>(r5 << 11 | ((p[1] * 63u + 127u) / 255u) << 5 | b5);
      else
        texels16[i] = static_cast<std::uint16_t>(r5 << 11 | ((p[1] * 31u + 127u) / 255u) << 6 | b5 << 1 | (p[3] ? 1u : 0u));
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    if (form == 1)
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGB,
                   GL_UNSIGNED_SHORT_5_6_5, texels16.data());
    else
      glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA,
                   GL_UNSIGNED_SHORT_5_5_5_1, texels16.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    stats.textures16++;
  }
  std::vector<std::uint16_t> texels16;

  void Upload(const Re4GxTexture &t, const Re4GxChunk &chunk) {
    const std::uint64_t key = static_cast<std::uint64_t>(t.keyHi) << 32 | t.keyLo;
    if (t.dataBytes == 0 || t.dataOffset + t.dataBytes > chunk.blobBytes)
      return;
    Touched(); // binds the new texture
    Texture tex;
    tex.width = t.width;
    tex.height = t.height;
    const bool pot = PowerOfTwo(t.width) && PowerOfTwo(t.height);
    unsigned want = t.levels ? t.levels : 1;
    if (!pot && !caps.npotMipmap)
      want = 1;
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const std::uint8_t *data = chunk.blob + t.dataOffset;
    std::size_t left = t.dataBytes;
    const std::uint8_t *palette = nullptr;
    unsigned entries = 0;
    if (re4gx::GxIsPaletted(t.format)) {
      const unsigned offset = (t.tlut & 0x3FF) << 9;
      palette = tlut.data() + offset;
      entries = std::min<unsigned>(re4gx::GxPaletteEntries(t.format), (kTlutBytes - offset) / 2);
    }
    unsigned w = t.width, h = t.height, levels = 0, lw = w, lh = h;
    std::vector<std::uint8_t> rgba, smaller;
    int form = 0; // TextureForm of level 0: every level the same (else the texture is incomplete)
    for (unsigned l = 0; l < want; l++) {
      const std::size_t bytes = re4gx::GxTextureLevelBytes(t.format, w, h);
      if (bytes > left)
        break;
      const auto decodeStart = std::chrono::steady_clock::now();
      re4gx::GxDecodeTexture(data, bytes, t.format, w, h, palette, entries, (t.tlut >> 10) & 3, rgba);
      if (l == 0)
        form = TextureForm(rgba);
      const auto uploadStart = std::chrono::steady_clock::now();
      TexImage(static_cast<GLint>(l), w, h, rgba, form);
      const auto uploadEnd = std::chrono::steady_clock::now();
      stats.decodeMicros += static_cast<unsigned>(
          std::chrono::duration_cast<std::chrono::microseconds>(uploadStart - decodeStart).count());
      stats.uploadMicros += static_cast<unsigned>(
          std::chrono::duration_cast<std::chrono::microseconds>(uploadEnd - uploadStart).count());
      levels++;
      lw = w;
      lh = h;
      data += bytes;
      left -= bytes;
      if (w == 1 && h == 1)
        break;
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
    }
    if (levels > 1 && caps.maxLevel) {
      // the chain ends where GX's does (APPLE_texture_max_level): sampled as the GameCube does
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL_APPLE, static_cast<GLint>(levels - 1));
    } else if (levels > 1) {
      // a mipmapped texture whose chain stops before 1x1 (lw x lh, the last level in rgba): the
      // rest from the last level, or GLES 2 samples it black (GX stays at its last level: the
      // distance gets a little softer here)
      while (lw > 1 || lh > 1) {
        NextLevel(rgba, lw, lh, smaller);
        rgba.swap(smaller);
        lw = std::max(1u, lw / 2);
        lh = std::max(1u, lh / 2);
        TexImage(static_cast<GLint>(levels), lw, lh, rgba, form);
        levels++;
        stats.mipmapsMade++;
      }
    }
    tex.levels = std::max(1u, levels);
    texParams[tex.id] = TexParams{};
    auto old = textures.find(key);
    if (old != textures.end())
      DeleteTexture(old->second.id);
    textures[key] = tex;
    stats.textures++;
  }

  void DeleteTexture(GLuint id) {
    auto fbo = targetFbos.find(id);
    if (fbo != targetFbos.end()) {
      glDeleteFramebuffers(1, &fbo->second);
      targetFbos.erase(fbo);
    }
    glDeleteTextures(1, &id);
    texParams.erase(id);
  }

  // The framebuffer that draws into texture `target` (an EFB copy's or the XFB), made once.
  GLuint TargetFbo(GLuint target) {
    auto it = targetFbos.find(target);
    if (it != targetFbos.end())
      return it->second;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    targetFbos[target] = fbo;
    return fbo;
  }

  // A texture copy read through a palette (gx_gl.cpp PalettedCopy).
  GLuint PalettedCopy(const Copy &copy, const Re4GxTexture &t) {
    Touched(); // its own pass
    const unsigned offset = (t.tlut & 0x3FF) << 9;
    const unsigned entries = std::min<unsigned>(re4gx::GxPaletteEntries(t.format), (kTlutBytes - offset) / 2);
    const std::uint64_t key =
        Fnv(tlut.data() + offset, entries * 2, (static_cast<std::uint64_t>(copy.serial) << 32) ^ t.address ^ t.tlut);
    auto it = paletted.find(key);
    if (it != paletted.end())
      return it->second.id;
    // the palette as a 256 x (entries / 256) RGBA texture, decoded through the C8 path
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(entries) * 4);
    for (unsigned i = 0; i < entries; i++) {
      std::vector<std::uint8_t> one;
      std::uint8_t tile[32] = {};
      re4gx::GxDecodeTexture(tile, 32, re4gx::kTexC8, 1, 1, tlut.data() + offset + i * 2, 1, (t.tlut >> 10) & 3, one);
      std::memcpy(rgba.data() + i * 4, one.data(), 4);
    }
    const unsigned pw = std::min(entries, 256u), ph = std::max(1u, entries / 256);
    GLuint pal = 0;
    glGenTextures(1, &pal);
    glBindTexture(GL_TEXTURE_2D, pal);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(pw), static_cast<GLsizei>(ph), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    PlainParams(GL_NEAREST);
    Texture out;
    out.width = copy.width;
    out.height = copy.height;
    glGenTextures(1, &out.id);
    glBindTexture(GL_TEXTURE_2D, out.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S(static_cast<int>(copy.width)), S(static_cast<int>(copy.height)), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    PlainParams(GL_NEAREST);
    texParams[out.id] = TexParams{GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, false};
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.id, 0);
    PlainState();
    glViewport(0, 0, S(static_cast<int>(copy.width)), S(static_cast<int>(copy.height)));
    glUseProgram(paletteProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, copy.id);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pal);
    glUniform2f(paletteSize, static_cast<float>(pw), static_cast<float>(ph));
    glUniform1f(paletteKind, static_cast<float>(copy.kind));
    glUniform1f(paletteFormat, static_cast<float>(t.format));
    DrawQuad();
    glActiveTexture(GL_TEXTURE0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &pal);
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    paletted[key] = out;
    return out.id;
  }

  // nearest or linear, clamped, no mipmaps: the bound texture
  static void PlainParams(GLenum filter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  void BindTexture(const Re4GxTexture &t, const Re4GxChunk &chunk) {
    const unsigned map = t.map & 7;
    texDirty = true; // the map's size is a uniform
    Bound &b = bound[map];
    b.id = whiteTex;
    b.width = t.width ? t.width : 1;
    b.height = t.height ? t.height : 1;
    b.levels = 1;
    b.copy = (t.flags & RE4_GX_TEX_COPY) ? t.address : 0;
    if (t.flags & RE4_GX_TEX_COPY) {
      auto it = copies.find(t.address);
      if (it == copies.end()) {
        stats.missingTextures++;
        return;
      }
      b.id = re4gx::GxIsPaletted(t.format) ? PalettedCopy(it->second, t) : it->second.id;
      // a texture of the copy's width but another height reads the copy's rows one to one
      // (gx_gl.cpp BindTexture: the heat haze over the fires of r101)
      if (it->second.width == b.width && it->second.height)
        b.height = it->second.height;
      return;
    }
    const std::uint64_t key = static_cast<std::uint64_t>(t.keyHi) << 32 | t.keyLo;
    if (key == 0)
      return;
    if (t.dataBytes && skipDraws && !re4gx::GxIsPaletted(t.format) && t.dataOffset + t.dataBytes <= chunk.blobBytes) {
      // a skipped chunk draws nothing: its texels kept for later (a paletted texture needs the TLUT
      // of now, it is decoded at once)
      Deferred &d = deferred[key];
      d.t = t;
      d.t.dataOffset = 0;
      d.texels.assign(chunk.blob + t.dataOffset, chunk.blob + t.dataOffset + t.dataBytes);
      stats.texturesDeferred++;
      return;
    }
    if (t.dataBytes) {
      deferred.erase(key);
      Upload(t, chunk);
    } else {
      auto later = deferred.find(key);
      if (later != deferred.end() && !skipDraws) {
        Re4GxChunk from = chunk;
        from.blob = later->second.texels.data();
        from.blobBytes = static_cast<std::uint32_t>(later->second.texels.size());
        Re4GxTexture lt = later->second.t;
        lt.map = t.map;
        Upload(lt, from);
        deferred.erase(later);
        stats.deferredUploaded++;
      } else if (later != deferred.end()) {
        return;  // still not drawn
      }
    }
    auto it = textures.find(key);
    if (it == textures.end()) {
      stats.missingTextures++;
      return;
    }
    b.id = it->second.id;
    b.levels = it->second.levels;
  }

  void Forget(std::uint64_t key) {
    deferred.erase(key);
    Touched(); // a deleted texture is unbound
    auto it = textures.find(key);
    if (it != textures.end()) {
      DeleteTexture(it->second.id);
      textures.erase(it);
    }
  }

  void DropTextures() {
    deferred.clear();
    Touched();
    for (auto &t : textures)
      DeleteTexture(t.second.id);
    textures.clear();
    for (auto &t : paletted)
      DeleteTexture(t.second.id);
    paletted.clear();
  }

  // Texture map `m` on unit m: the texture bound, sampled as BP mode0 says (wrap, filters).
  void BindMap(unsigned m) {
    const Bound &b = bound[m];
    const GLuint id = b.id ? b.id : whiteTex;
    auto it = texParams.find(id);
    if (it == texParams.end())
      it = texParams.emplace(id, TexParams{}).first;
    TexParams &p = it->second;
    const std::uint32_t mode0 = bp[(m < 4 ? 0x80 : 0xA0) + (m & 3)];
    static const GLint wrap[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    GLint ws = wrap[mode0 & 3], wt = wrap[(mode0 >> 2) & 3];
    if (!caps.npotRepeat && !(PowerOfTwo(b.width) && PowerOfTwo(b.height)) &&
        (ws != GL_CLAMP_TO_EDGE || wt != GL_CLAMP_TO_EDGE)) {
      // GLES 2 without OES_texture_npot: such a texture would read black
      ws = wt = GL_CLAMP_TO_EDGE;
      if (!p.npotCounted) {
        p.npotCounted = true;
        stats.npotClamped++;
      }
    }
    const GLint mag = (mode0 >> 4) & 1 ? GL_LINEAR : GL_NEAREST;
    const unsigned minFilter = (mode0 >> 5) & 7;
    static const GLint minTable[8] = {GL_NEAREST,
                                      GL_NEAREST_MIPMAP_NEAREST,
                                      GL_NEAREST_MIPMAP_LINEAR,
                                      GL_NEAREST,
                                      GL_LINEAR,
                                      GL_LINEAR_MIPMAP_NEAREST,
                                      GL_LINEAR_MIPMAP_LINEAR,
                                      GL_LINEAR};
    GLint min = minTable[minFilter];
    if (b.levels <= 1)
      min = (minFilter >= 4) ? GL_LINEAR : GL_NEAREST;
    if (unitTex[m] == id && p.wrapS == ws && p.wrapT == wt && p.magFilter == mag && p.minFilter == min)
      return;
    if (activeUnit != m) {
      glActiveTexture(GL_TEXTURE0 + m);
      activeUnit = m;
    }
    if (unitTex[m] != id) {
      glBindTexture(GL_TEXTURE_2D, id);
      unitTex[m] = id;
      stats.textureBinds++;
    }
    if (p.wrapS != ws) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ws);
      p.wrapS = ws;
    }
    if (p.wrapT != wt) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wt);
      p.wrapT = wt;
    }
    if (p.magFilter != mag) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
      p.magFilter = mag;
    }
    if (p.minFilter != min) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
      p.minFilter = min;
    }
  }

  // --- GL state ----------------------------------------------------------------------------------

  // GL state for the renderer's own passes (copies, palettes, presenting)
  void PlainState() {
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    Touched();
  }

  void DrawQuad() {
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    for (GLuint a = 0; a < 16; a++)
      if (attribMask & (1u << a))
        glDisableVertexAttribArray(a);
    attribMask = 1;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }

  // Scissor, depth, blending, culling, line width (gx_gl.cpp ApplyState).
  void ApplyState(unsigned primitive) {
    const std::uint32_t sig[10] = {bp[0x20],     bp[0x21],     bp[0x59],       bp[0x40] & 0x1F, bp[0x41] & 0xFFFF,
                                   bp[0x43] & 7, (bp[0x00] >> 14) & 3, bp[0x22] & 0xFF, primitive, bp[0x42] & 0x1FF};
    if (stateValid && std::memcmp(sig, stateSig, sizeof(sig)) == 0)
      return;
    std::memcpy(stateSig, sig, sizeof(sig));
    stateValid = true;
    stats.stateChanges++;
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    // (the "half" measurement: a quarter of the pixels)
    const int div = options.probeHalf ? 2 : 1;
    glViewport(0, 0, S(kEfbWidth) / div, S(kEfbHeight) / div);
    // scissor: registers in EFB pixels + 342, minus the scissor offset (BP 0x59, in 2 pixels)
    const int xoff = static_cast<int>(bp[0x59] & 0x3FF) * 2, yoff = static_cast<int>((bp[0x59] >> 10) & 0x3FF) * 2;
    const int x0 = std::max(0, static_cast<int>((bp[0x20] >> 12) & 0x7FF) - xoff);
    const int y0 = std::max(0, static_cast<int>(bp[0x20] & 0x7FF) - yoff);
    const int x1 = std::min(kEfbWidth, static_cast<int>((bp[0x21] >> 12) & 0x7FF) - xoff + 1);
    const int y1 = std::min(kEfbHeight, static_cast<int>(bp[0x21] & 0x7FF) - yoff + 1);
    glEnable(GL_SCISSOR_TEST);
    glScissor(S(x0) / div, S(y0) / div, std::max(0, S(x1) - S(x0)) / div, std::max(0, S(y1) - S(y0)) / div);
    const std::uint32_t z = bp[0x40];
    if (z & 1) {
      static const GLenum func[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(func[(z >> 1) & 7]);
      glDepthMask((z >> 4) & 1 ? GL_TRUE : GL_FALSE);
    } else {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
    }
    const std::uint32_t c = bp[0x41];
    const bool alphaFormat = (bp[0x43] & 7) == 1; // RGBA6_Z24 keeps alpha
    const bool alphaWrite = ((c >> 4) & 1) && alphaFormat, colorWrite = (c >> 3) & 1;
    glColorMask(colorWrite, colorWrite, colorWrite, alphaWrite);
    // destination alpha (GXSetDstAlpha, BP 0x42), as gx_gl.cpp: the shader writes it, or the
    // alpha blend factors make it (0 only)
    const bool dstAlpha = alphaWrite && ((bp[0x42] >> 8) & 1);
    const int dstValue = static_cast<int>(bp[0x42] & 0xFF);
    int shaderAlpha = -1;
    if ((c >> 11) & 1) { // subtract: dst - src
      glEnable(GL_BLEND);
      glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, dstAlpha ? GL_FUNC_ADD : GL_FUNC_REVERSE_SUBTRACT);
      glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, dstAlpha ? GL_ZERO : GL_ONE);
      if (dstAlpha)
        shaderAlpha = dstValue;
    } else if (c & 1) {
      static const GLenum src[8] = {GL_ZERO,      GL_ONE,           GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
      static const GLenum dst[8] = {GL_ZERO,      GL_ONE,           GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
      const unsigned sf = (c >> 8) & 7, df = (c >> 5) & 7;
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      if (!dstAlpha) {
        glBlendFunc(src[sf], dst[df]);
      } else if (!colorWrite || (sf != 4 && sf != 5 && df != 4 && df != 5)) {
        glBlendFuncSeparate(src[sf], dst[df], GL_ONE, GL_ZERO);
        shaderAlpha = dstValue;
      } else {
        glBlendFuncSeparate(src[sf], dst[df], GL_ZERO, GL_ZERO);
        if (dstValue != 0)
          stats.dstAlphaApprox++;
      }
    } else if ((c >> 1) & 1) { // logic operations: the common ones
      const unsigned op = (c >> 12) & 15;
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      if (op == 0)
        glBlendFunc(GL_ZERO, GL_ZERO);
      else if (op == 5)
        glBlendFunc(GL_ZERO, GL_ONE);
      else if (op == 15)
        glBlendFunc(GL_ONE, GL_ONE);
      else
        glBlendFunc(GL_ONE, GL_ZERO);
      if (dstAlpha && op != 0 && op != 5 && op != 15)
        shaderAlpha = dstValue;
    } else {
      glDisable(GL_BLEND);
      if (dstAlpha)
        shaderAlpha = dstValue;
    }
    if (this->shaderAlpha != static_cast<float>(shaderAlpha)) {
      this->shaderAlpha = static_cast<float>(shaderAlpha);
      pxDirty = true;
    }
    const unsigned cull = (bp[0x00] >> 14) & 3;
    if (primitive != RE4_GX_TRIANGLES || cull == 0) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(cull == 1 ? GL_BACK : cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK);
    }
    const float lineWidth = static_cast<float>(bp[0x22] & 0xFF) / 6.0f * static_cast<float>(eighths) / 8.0f;
    glLineWidth(std::max(1.0f, lineWidth));
  }

  // --- draws -------------------------------------------------------------------------------------

  // The matrix slots the draw's vertices carry (bit 0 position, 1 + n texture n).
  std::uint32_t MatrixSlots(const Re4GxDraw &d) const {
    if (!(d.layout & RE4_GX_V_MATRIX))
      return 0;
    if (!(d.layout & RE4_GX_V_COMPACT))
      return 0x1FF; // the float vertex has all nine (the missing ones filled in by the core)
    return (compactWords[RE4_GX_CA_MATRIX] >> 16) & 0x1FF;
  }

  ShaderKey MakeKey(const Re4GxDraw &d) {
    ShaderKey k;
    std::memset(&k, 0, sizeof(k));
    k.layout = d.layout;
    k.mtxSlots = MatrixSlots(d);
    k.numChans = xfReg[0x09] & 3;
    k.numTexGens = std::min(8u, xfReg[0x3F] & 15);
    for (int i = 0; i < 4; i++)
      k.chan[i] = i % 2 < static_cast<int>(k.numChans) ? xfReg[0x0E + i] & 0x7FFF : 0;
    k.dualTex = xfReg[0x12] & 1;
    for (unsigned i = 0; i < k.numTexGens; i++) {
      k.texGen[i] = xfReg[0x40 + i] & 0x3FFFF;
      k.postGen[i] = k.dualTex ? xfReg[0x50 + i] & 0x13F : 0;
    }
    k.orthographic = xfReg[0x26] & 1;
    const std::uint32_t gen = bp[0x00];
    k.numStages = ((gen >> 10) & 15) + 1;
    k.numIndStages = (gen >> 16) & 7;
    for (unsigned s = 0; s < k.numStages; s++) {
      k.colorEnv[s] = bp[0xC0 + 2 * s] & 0xFFFFFF;
      k.alphaEnv[s] = bp[0xC1 + 2 * s] & 0xFFFFFF;
      const std::uint32_t o = bp[0x28 + s / 2] >> ((s & 1) ? 12 : 0);
      k.order[s] = (o & 7) | ((o >> 3) & 7) << 3 | ((o >> 6) & 1) << 6 | ((o >> 7) & 7) << 7;
      const std::uint32_t ks = bp[0xF6 + s / 2];
      k.konst[s] = ((s & 1) ? (ks >> 14) & 31 : (ks >> 4) & 31) | ((s & 1) ? (ks >> 19) & 31 : (ks >> 9) & 31) << 8;
      k.indCmd[s] = k.numIndStages ? bp[0x10 + s] & 0x1FFFFF : 0;
      if ((o >> 6) & 1)
        k.texMaps |= 1u << (o & 7);
    }
    for (int t = 0; t < 4; t++)
      k.swap[t] = (bp[0xF6 + 2 * t] & 3) | ((bp[0xF6 + 2 * t] >> 2) & 3) << 2 | (bp[0xF7 + 2 * t] & 3) << 4 |
                  ((bp[0xF7 + 2 * t] >> 2) & 3) << 6;
    if (k.numIndStages) {
      k.indRef = bp[0x27] & 0xFFFFFF;
      for (unsigned s = 0; s < k.numIndStages && s < 4; s++)
        k.texMaps |= 1u << ((k.indRef >> (6 * s)) & 7);
    }
    // what no stage reads stays out of the key (the code is the same; on the OUYA each such
    // difference built a shader of its own, 150-300 ms, 07.11.2026): the swap tables of neither a
    // stage's texture nor its rasterized colour, konstant selections of a stage without a KONST input
    std::uint32_t swapsRead = 0;
    for (unsigned s = 0; s < k.numStages; s++) {
      const std::uint32_t cc = k.colorEnv[s], ac = k.alphaEnv[s], o = k.order[s];
      const unsigned channel = (o >> 7) & 7;
      if (((o >> 6) & 1) && (k.texMaps & (1u << (o & 7))))
        swapsRead |= 1u << ((ac >> 2) & 3);
      if (channel == 0 || channel == 1)
        swapsRead |= 1u << (ac & 3);
      bool colorKonst = false, alphaKonst = false;
      for (unsigned shift = 0; shift < 16; shift += 4)
        colorKonst = colorKonst || ((cc >> shift) & 15) == 14;  // GX_CC_KONST
      for (unsigned shift = 4; shift < 16; shift += 3)
        alphaKonst = alphaKonst || ((ac >> shift) & 7) == 6;  // GX_CA_KONST
      if (!colorKonst)
        k.konst[s] &= ~0xFFu;
      if (!alphaKonst)
        k.konst[s] &= ~0xFF00u;
    }
    for (int t = 0; t < 4; t++)
      if (!(swapsRead & (1u << t)))
        k.swap[t] = 0;
    k.alphaTest = (bp[0xF3] >> 16) & 0xFF;
    k.fog = (bp[0xF1] >> 20) & 15;
    k.texelCoords = options.texelCoords ? 1 : 0;
    k.fastTev = options.fastTev ? 1 : 0;
    // at most maxLights lights per vertex: where every lit channel with lights has the same ones
    // and there are more, the channels get the first slots; Uniforms puts the strongest there
    foldMask = 0;
    if (options.maxLights > 0) {
      std::uint32_t common = 0, diffuse = 0, attn = 0;
      bool same = true;
      for (int i = 0; i < 4; i++) {
        const std::uint32_t c = k.chan[i];
        const std::uint32_t m = ((c >> 2) & 15) | (((c >> 11) & 15) << 4);
        if (!(c & 2) || !m)
          continue;
        if (!common) {
          common = m;
          diffuse = (c >> 7) & 3;
          attn = (c >> 9) & 3;
        } else if (m != common) {
          same = false;
        }
      }
      // also with no more lights than that: the lights in the first slots (lights 2 and 5 the same
      // shader as 0 and 1; each set of lights built a shader of its own, on the OUYA 150 ms each)
      if (same && common) {
        const unsigned count = std::min(Popcount8(common), static_cast<unsigned>(options.maxLights));
        const std::uint32_t slots = (1u << count) - 1;
        for (int i = 0; i < 4; i++) {
          std::uint32_t &c = k.chan[i];
          const std::uint32_t m = ((c >> 2) & 15) | (((c >> 11) & 15) << 4);
          if ((c & 2) && m)
            c = (c & ~(15u << 2 | 15u << 11)) | (slots & 15) << 2 | ((slots >> 4) & 15) << 11;
        }
        k.lightFold = 1;
        foldMask = common;
        foldDiffuse = diffuse;
        foldAttn = attn;
      }
    }
    return k;
  }

  // Diagnosis (the OUYA's stalls, 07.11.2026: a shader costs it about 150 ms): for the first new
  // shaders, how the key differs from the nearest one built before (field[index] old -> new)
  void LogKeyDifference(const ShaderKey &k) {
    if (stats.programs > 400 || programs.empty())
      return;
    struct Field {
      const char *name;
      std::size_t offset, words;
    };
#define RE4_FIELD(f) {#f, offsetof(ShaderKey, f), sizeof(ShaderKey::f) / 4}
    static const Field fields[] = {RE4_FIELD(layout),      RE4_FIELD(mtxSlots),  RE4_FIELD(numChans),
                                   RE4_FIELD(numTexGens),  RE4_FIELD(chan),      RE4_FIELD(texGen),
                                   RE4_FIELD(postGen),     RE4_FIELD(dualTex),   RE4_FIELD(orthographic),
                                   RE4_FIELD(numStages),   RE4_FIELD(numIndStages), RE4_FIELD(colorEnv),
                                   RE4_FIELD(alphaEnv),    RE4_FIELD(order),     RE4_FIELD(konst),
                                   RE4_FIELD(swap),        RE4_FIELD(indCmd),    RE4_FIELD(indRef),
                                   RE4_FIELD(alphaTest),   RE4_FIELD(fog),       RE4_FIELD(texMaps),
                                   RE4_FIELD(fastTev),     RE4_FIELD(texelCoords), RE4_FIELD(lightFold)};
#undef RE4_FIELD
    const std::uint32_t *now = reinterpret_cast<const std::uint32_t *>(&k);
    const std::size_t words = sizeof(ShaderKey) / 4;
    const ShaderKey *best = nullptr;
    std::size_t bestDiff = words + 1;
    for (const auto &e : programs) {
      const std::uint32_t *w = reinterpret_cast<const std::uint32_t *>(&e.first);
      std::size_t d = 0;
      for (std::size_t i = 0; i < words && d < bestDiff; i++)
        d += w[i] != now[i];
      if (d < bestDiff) {
        bestDiff = d;
        best = &e.first;
      }
    }
    const std::uint32_t *was = reinterpret_cast<const std::uint32_t *>(best);
    std::string text;
    for (const Field &f : fields)
      for (std::size_t i = 0; i < f.words; i++) {
        const std::size_t at = f.offset / 4 + i;
        if (was[at] == now[at])
          continue;
        char part[96];
        if (f.words > 1)
          snprintf(part, sizeof(part), " %s[%zu] %x->%x", f.name, i, was[at], now[at]);
        else
          snprintf(part, sizeof(part), " %s %x->%x", f.name, was[at], now[at]);
        text += part;
      }
    GXLOG("new shader %u: %zu words from the nearest:%s", stats.programs + 1, bestDiff, text.c_str());
  }

  Program *ProgramFor(const ShaderKey &k) {
    auto it = programs.find(k);
    if (it != programs.end())
      return it->second.id ? &it->second : nullptr;
    LogKeyDifference(k);
    Program p;
    ShaderKey vk = k;
    if (options.probeSimpleVs) {
      // the position only: no lighting channels, no texture coordinates
      vk.numChans = vk.numTexGens = vk.dualTex = 0;
      std::memset(vk.chan, 0, sizeof(vk.chan));
    }
    p.info = GxShaderInfo(vk);
    std::string log;
    std::string vs = GxVertexShader(vk, p.info);
    const std::string fs = options.probeFlat
                               ? "precision mediump float;\nvarying vec4 vColor0;\nvoid main() { gl_FragColor = vColor0; }\n"
                               : GxFragmentShader(k, p.info);
    const char *names[kAttrSources];
    for (int a = 0; a < p.info.attribCount; a++)
      names[a] = GxAttributeName(p.info.attribs[a]);
    if (p.info.attribCount > caps.maxAttribs) {
      log = "more vertex attributes than the device has";
    } else {
      p.id = Link(vs, fs, names, p.info.attribCount, &log);
      // too many instructions for the device (Tegra 3: "C6002: Instruction limit of 256
      // exceeded"): once more without what rarely matters
      if (!p.id && log.compare(0, 14, "vertex shader:") == 0 && log.find("C6002") != std::string::npos) {
        vs = GxVertexShader(vk, p.info, true);
        p.id = Link(vs, fs, names, p.info.attribCount, &log);
        if (p.id)
          stats.leanShaders++;
      }
    }
    static const bool logShaders = [] {
      const char *v = std::getenv("RE4_GX2_LOG_SHADERS");
      return v && *v == '1';
    }();
    if (!p.id) {
      stats.shaderErrors++;
      if (stats.shaderErrors <= 4)
        GXLOG("shader failed (%d vertex vec4, %d varying floats): %s\n--- vertex ---\n%s\n--- fragment ---\n%s",
              p.info.vertexUniforms, p.info.varyingComponents, log.c_str(), vs.c_str(), fs.c_str());
    } else if (logShaders) {
      GXLOG("shader %u (%d vertex vec4, %d varying floats)\n--- vertex ---\n%s\n--- fragment ---\n%s", stats.programs,
            p.info.vertexUniforms, p.info.varyingComponents, vs.c_str(), fs.c_str());
    }
    if (p.id) {
      stats.programs++;
      Locations(p);
      current = nullptr;
    }
    auto &slot = programs[k];
    slot = p;
    return slot.id ? &slot : nullptr;
  }

  static GLint Location(GLuint id, const char *name, int index = -1) {
    char buffer[32];
    if (index >= 0) {
      std::snprintf(buffer, sizeof(buffer), "%s[%d]", name, index);
      name = buffer;
    }
    return glGetUniformLocation(id, name);
  }

  void Locations(Program &p) {
    const GLuint id = p.id;
    for (int r = 0; r < 64; r++)
      p.posTex[r] = Location(id, "uPosTex", r);
    for (int r = 0; r < 32; r++)
      p.nrm[r] = p.info.normalMatrix ? Location(id, "uNrm", r) : -1;
    for (int r = 0; r < p.info.postCount; r++)
      p.post[r] = Location(id, "uPost", r);
    int slot = 0;
    for (unsigned l = 0; l < 8; l++)
      p.light[l] = (p.info.lights & (1u << l)) ? Location(id, "uLight", 5 * slot++) : -1;
    p.matAmb = Location(id, "uMatAmb");
    p.vp = Location(id, "uVp");
    p.proj = Location(id, "uProj");
    p.mtxDef = Location(id, "uMtxDef");
    p.dequant = Location(id, "uDequant");
    p.tcScale = Location(id, "uTcScale");
    p.colors = Location(id, "uColors");
    p.konst = Location(id, "uKonst");
    p.colorsW = Location(id, "uColorsW");
    p.fold = Location(id, "uFold");
    p.konstW = Location(id, "uKonstW");
    p.alphaRef = Location(id, "uAlphaRef");
    p.fog = Location(id, "uFog");
    p.lodBias = Location(id, "uLodBias");
    p.mapScale = Location(id, "uMapScale");
    p.indScale = Location(id, "uIndScale");
    p.indMtx = Location(id, "uIndMtx");
    glUseProgram(id);
    for (int m = 0; m < 8; m++) {
      char name[8];
      std::snprintf(name, sizeof(name), "s%d", m);
      const GLint loc = glGetUniformLocation(id, name);
      if (loc >= 0)
        glUniform1i(loc, m);
    }
  }

  // Rows of an array that changed after `seen`, as runs, to the program.
  template <int N>
  void UploadRows(const GLint (&loc)[N], const float (*rows)[4], const std::uint64_t *rowStamp, int count,
                  std::uint64_t seen) {
    for (int r = 0; r < count;) {
      if (rowStamp[r] <= seen || loc[r] < 0) {
        r++;
        continue;
      }
      int end = r + 1;
      while (end < count && rowStamp[end] > seen && loc[end] >= 0)
        end++;
      glUniform4fv(loc[r], end - r, rows[r]);
      stats.uniformUploads++;
      r = end;
    }
  }

  // GxOptions::maxLights: the strongest lights of foldMask at the draw's origin (the translation of
  // its default position matrix, view space as the lights) into the program's light slots, the
  // others as uFold (their light there; half of it where the diffuse function reads the normal,
  // whose angle to them is not known here)
  void FoldLights(Program &p, const Re4GxDraw &d) {
    const unsigned row = std::min(61u, d.matrices[0] & 0xFF);
    const float ox = posTex[row][3], oy = posTex[row + 1][3], oz = posTex[row + 2][3];
    float strength[8] = {};
    for (unsigned l = 0; l < 8; l++) {
      if (!(foldMask & (1u << l)))
        continue;
      const float *c = light[l][0], *a = light[l][1], *k = light[l][2], *pos = light[l][3], *dir = light[l][4];
      float at = 1.0f;
      if (foldAttn == 3) {  // spot: the angle and the distance (gx_gl2_shader.cpp Light)
        const float dx = pos[0] - ox, dy = pos[1] - oy, dz = pos[2] - oz;
        const float d2 = dx * dx + dy * dy + dz * dz, dist = std::sqrt(d2);
        const float cosv = dist > 0.0f ? std::max(0.0f, (dx * dir[0] + dy * dir[1] + dz * dir[2]) / dist) : 1.0f;
        const float den = k[0] + k[1] * dist + k[2] * d2;
        at = std::max(0.0f, a[0] + a[1] * cosv + a[2] * cosv * cosv) / (den > 1e-6f ? den : 1e-6f);
      }
      strength[l] = at;
      (void) c;
    }
    // the strongest maxLights (in light order, so a draw keeps its slots while the order holds)
    int sel[8], n = 0;
    std::uint32_t chosen = 0;
    for (int j = 0; j < options.maxLights; j++) {
      int best = -1;
      float bestValue = -1.0f;
      for (unsigned l = 0; l < 8; l++) {
        if (!(foldMask & (1u << l)) || (chosen & (1u << l)))
          continue;
        const float *c = light[l][0];
        const float v = strength[l] * (c[0] + c[1] + c[2] + 1e-3f);
        if (v > bestValue) {
          bestValue = v;
          best = static_cast<int>(l);
        }
      }
      if (best >= 0)
        chosen |= 1u << best;
    }
    for (unsigned l = 0; l < 8; l++)
      if (chosen & (1u << l))
        sel[n++] = static_cast<int>(l);
    float fold[8] = {};
    const float share = foldDiffuse == 0 ? 1.0f : 0.5f;
    for (unsigned l = 0; l < 8; l++)
      if ((foldMask & (1u << l)) && !(chosen & (1u << l)))
        for (int c = 0; c < 4; c++)
          fold[c] += strength[l] * share * light[l][0][c];
    for (int c = 0; c < 4; c++)
      fold[4 + c] = fold[c];
    bool resend = lightNewest > p.lightSelSeen;
    for (int j = 0; j < n; j++)
      resend = resend || p.lightSel[j] != sel[j];
    if (resend) {
      for (int j = 0; j < n; j++) {
        if (p.light[j] >= 0)
          glUniform4fv(p.light[j], 5, light[sel[j]][0]);
        p.lightSel[j] = sel[j];
        stats.uniformUploads++;
      }
      p.lightSelSeen = lightNewest;
    }
    if (p.fold >= 0 && std::memcmp(fold, p.foldSent, sizeof(fold)) != 0) {
      glUniform4fv(p.fold, 2, fold);
      std::memcpy(p.foldSent, fold, sizeof(fold));
      stats.uniformUploads++;
    }
  }

  static unsigned Popcount8(std::uint32_t v) {
    unsigned n = 0;
    for (; v; v &= v - 1)
      n++;
    return n;
  }

  // What the program does not have yet of the state.
  void Uniforms(Program &p, const Re4GxDraw &d) {
    const std::uint64_t seen = p.seen;
    if (posTexNewest > seen)
      UploadRows(p.posTex, posTex, posTexStamp, 64, seen);
    if (p.info.normalMatrix && normalNewest > seen)
      UploadRows(p.nrm, normal, normalStamp, 32, seen);
    if (p.info.postCount && postNewest > seen)
      for (int j = 0; j < p.info.postCount; j++) {
        const int row = p.info.postRows[j];
        if (postStamp[row] > seen && p.post[j] >= 0) {
          glUniform4fv(p.post[j], 1, post[row]);
          stats.uniformUploads++;
        }
      }
    if (p.info.lights && key.lightFold)
      FoldLights(p, d);
    else if (p.info.lights && lightNewest > seen)
      for (unsigned l = 0; l < 8; l++)
        if (p.light[l] >= 0 && lightStamp[l] > seen) {
          glUniform4fv(p.light[l], 5, light[l][0]);
          stats.uniformUploads++;
        }
    if (xfRegStamp > seen) {
      if (p.matAmb >= 0)
        glUniform4fv(p.matAmb, 4, matAmb[0]);
      glUniform4fv(p.vp, 2, viewport[0]);
      glUniform4fv(p.proj, 2, proj[0]);
      stats.uniformUploads += 3;
    }
    if (p.info.compact && dequantStamp > seen) {
      glUniform4fv(p.dequant, 3, dequant);
      stats.uniformUploads++;
    }
    if (pixelStamp > seen) {
      glUniform4fv(p.colors, 4, colors[0]);
      glUniform4fv(p.konst, 4, konst[0]);
      if (p.colorsW >= 0)
        glUniform4fv(p.colorsW, 4, colorsW[0]);
      if (p.konstW >= 0)
        glUniform4fv(p.konstW, 4, konstW[0]);
      glUniform4fv(p.alphaRef, 1, alphaRef);
      glUniform4fv(p.fog, 2, fog[0]);
      stats.uniformUploads += 4;
    }
    if (texStamp > seen) {
      // per coordinate: to the one map that reads it, or to 1/128 texels (gx_gl2_shader.h)
      float tc[16] = {};
      for (int i = 0; i < 8; i++) {
        const int m = p.info.coordMap[i];
        if (m >= 0) {
          tc[i * 2] = coordSize[i][0] / mapScale[m][2];
          tc[i * 2 + 1] = coordSize[i][1] / mapScale[m][3];
        } else if (m == -1) {
          tc[i * 2] = coordSize[i][0] * 128.0f;
          tc[i * 2 + 1] = coordSize[i][1] * 128.0f;
        }
      }
      if (std::memcmp(tc, p.tcScaleSent, sizeof(tc)) != 0 || seen == 0) {
        glUniform4fv(p.tcScale, 4, tc);
        std::memcpy(p.tcScaleSent, tc, sizeof(tc));
        stats.uniformUploads++;
      }
      glUniform4fv(p.lodBias, 2, lodBias);
      if (p.mapScale >= 0)
        glUniform4fv(p.mapScale, 8, mapScale[0]);
      stats.uniformUploads += 2;
    }
    if (p.info.fixedPoint && indStamp > seen) {
      glUniform4fv(p.indScale, 4, indScale[0]);
      glUniform4fv(p.indMtx, 6, indMtx[0]);
      stats.uniformUploads += 2;
    }
    p.seen = stamp;
    // the draw's default matrix slots
    if (p.info.mtxDefaults) {
      float m[12] = {};
      for (int slot = 0; slot < 9; slot++)
        m[slot] = static_cast<float>((d.matrices[slot / 4] >> (8 * (slot % 4))) & 0xFF);
      if (!p.mtxDefValid || std::memcmp(m, p.mtxDefSent, sizeof(m)) != 0) {
        glUniform4fv(p.mtxDef, 3, m);
        std::memcpy(p.mtxDefSent, m, sizeof(m));
        p.mtxDefValid = true;
        stats.uniformUploads++;
      }
    }
  }

  // Where GL reads each vertex input of a layout: the float vertex (re4_gx_frame.h) or the
  // compact one (the last RE4_GX_OP_COMPACT_FORMAT).
  void AttrFormats(std::uint32_t layout) {
    attrLayout = layout;
    attrDirty = false;
    for (auto &a : attr)
      a = AttrFormat{};
    if (!(layout & RE4_GX_V_COMPACT)) {
      std::uint32_t off = 0;
      attr[kAttrPos] = {true, 3, GL_FLOAT, GL_FALSE, off};
      off += 12;
      if (layout & RE4_GX_V_NORMAL) {
        attr[kAttrNrm] = {true, 3, GL_FLOAT, GL_FALSE, off};
        off += 12;
        if (layout & RE4_GX_V_BINORMAL_TANGENT) {
          attr[kAttrBin] = {true, 3, GL_FLOAT, GL_FALSE, off};
          attr[kAttrTan] = {true, 3, GL_FLOAT, GL_FALSE, off + 12};
          off += 24;
        }
      }
      for (int c = 0; c < 2; c++)
        if (layout & (c == 0 ? RE4_GX_V_COLOR0 : RE4_GX_V_COLOR1)) {
          attr[kAttrClr0 + c] = {true, 4, GL_UNSIGNED_BYTE, GL_TRUE, off};
          off += 4;
        }
      for (int t = 0; t < 8; t++)
        if (layout & (RE4_GX_V_TEX0 << t)) {
          attr[kAttrTex0 + t] = {true, 2, GL_FLOAT, GL_FALSE, off};
          off += 8;
        }
      if (layout & RE4_GX_V_MATRIX)
        for (int w = 0; w < 3; w++)
          attr[kAttrMtx0 + w] = {true, 4, GL_UNSIGNED_BYTE, GL_FALSE, off + 4u * static_cast<std::uint32_t>(w)};
      return;
    }
    static const GLenum types[5] = {GL_UNSIGNED_BYTE, GL_BYTE, GL_UNSIGNED_SHORT, GL_SHORT, GL_FLOAT};
    static const std::uint32_t sizes[5] = {1, 1, 2, 2, 4};
    auto from = [&](std::uint32_t w, GLint comps, std::uint32_t skip) {
      const unsigned type = std::min(4u, (w >> 8) & 15);
      return AttrFormat{true, comps, types[type], GL_FALSE, (w & 0xFF) + skip * sizes[type]};
    };
    const std::uint32_t pos = compactWords[RE4_GX_CA_POSITION];
    if (pos & RE4_GX_CA_PRESENT)
      attr[kAttrPos] = from(pos, static_cast<GLint>(std::max(1u, std::min(3u, (pos >> 12) & 15))), 0);
    const std::uint32_t nrm = compactWords[RE4_GX_CA_NORMAL];
    if ((layout & RE4_GX_V_NORMAL) && (nrm & RE4_GX_CA_PRESENT)) {
      attr[kAttrNrm] = from(nrm, 3, 0);
      if ((layout & RE4_GX_V_BINORMAL_TANGENT) && ((nrm >> 12) & 15) >= 9) {
        attr[kAttrBin] = from(nrm, 3, 3);
        attr[kAttrTan] = from(nrm, 3, 6);
      }
    }
    for (int c = 0; c < 2; c++) {
      const std::uint32_t w = compactWords[RE4_GX_CA_COLOR0 + c];
      if ((layout & (c == 0 ? RE4_GX_V_COLOR0 : RE4_GX_V_COLOR1)) && (w & RE4_GX_CA_PRESENT))
        attr[kAttrClr0 + c] = {true, 4, GL_UNSIGNED_BYTE, GL_TRUE, w & 0xFF};
    }
    for (int t = 0; t < 8; t++) {
      const std::uint32_t w = compactWords[RE4_GX_CA_TEX0 + t];
      if ((layout & (RE4_GX_V_TEX0 << t)) && (w & RE4_GX_CA_PRESENT))
        attr[kAttrTex0 + t] = from(w, static_cast<GLint>(std::max(1u, std::min(2u, (w >> 12) & 15))), 0);
    }
    const std::uint32_t mtx = compactWords[RE4_GX_CA_MATRIX];
    if ((layout & RE4_GX_V_MATRIX) && (mtx & RE4_GX_CA_PRESENT)) {
      const unsigned count = (mtx >> 12) & 15;
      for (unsigned w = 0; w * 4 < count && w < 3; w++)
        attr[kAttrMtx0 + w] = {true, static_cast<GLint>(std::min(4u, count - 4 * w)), GL_UNSIGNED_BYTE, GL_FALSE,
                               (mtx & 0xFF) + 4 * w};
    }
  }

  void Draw(const Re4GxDraw &d, const Re4GxChunk &chunk) {
    if (skipDraws)
      return;
    if (d.indexCount == 0 || d.vertexOffset + static_cast<std::uint64_t>(d.vertexCount) * d.stride > chunk.vertexBytes ||
        d.indexOffset + d.indexCount > chunk.indexCount)
      return;
    const bool compact = (d.layout & RE4_GX_V_COMPACT) != 0;
    if (compact && !compactValid) {
      stats.skippedDraws++;
      return;
    }
    if (keyDirty || d.layout != keyLayout) {
      key = MakeKey(d);
      keyProgram = ProgramFor(key);
      keyLayout = d.layout;
      keyDirty = false;
    }
    Program *p = keyProgram;
    if (!p) {
      stats.skippedDraws++;
      return;
    }
    const unsigned primitive = d.primitive & 0xFF;
    ApplyState(primitive);
    const float pointSize = static_cast<float>((bp[0x22] >> 8) & 0xFF) / 6.0f * static_cast<float>(eighths) / 8.0f;
    if (xfRegsDirty)
      FillXfRegs();
    if (proj[1][3] != pointSize) {
      proj[1][3] = pointSize;
      xfRegStamp = Next();
    }
    if (pxDirty)
      FillPixel();
    if (texDirty)
      FillTex();
    if (indDirty)
      FillInd();
    if (current != p) {
      glUseProgram(p->id);
      current = p;
      stats.programSwitches++;
    }
    if (!options.probeNoUniform || p->seen == 0)
      Uniforms(*p, d);
    if (!unitsValid) {
      for (auto &u : unitTex)
        u = ~0u;
      activeUnit = ~0u;
      unitsValid = true;
    }
    for (unsigned m = 0; m < 8; m++)
      if (key.texMaps & (1u << m))
        BindMap(m);
    // vertex attributes: pointers at this draw's first vertex
    // (the "noattrib" measurement: from the start of the buffer, set again only with a new format)
    const bool keep = options.probeNoAttrib && !attrDirty && d.layout == attrLayout && d.stride == probeStride &&
                      p == probeProgram;
    const std::uint32_t base = options.probeNoAttrib ? 0 : d.vertexOffset;
    probeStride = d.stride;
    probeProgram = p;
    if (attrDirty || d.layout != attrLayout)
      AttrFormats(d.layout);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    std::uint32_t mask = keep ? attribMask : 0;
    for (int a = 0; !keep && a < p->info.attribCount; a++) {
      const AttrFormat &f = attr[p->info.attribs[a]];
      if (!f.present) {
        // the shader reads what the layout lacks: a constant 0
        glVertexAttrib4f(static_cast<GLuint>(a), 0.0f, 0.0f, 0.0f, 1.0f);
        continue;
      }
      mask |= 1u << a;
      glVertexAttribPointer(static_cast<GLuint>(a), f.size, f.type, f.normalized, static_cast<GLsizei>(d.stride),
                            reinterpret_cast<const void *>(static_cast<std::uintptr_t>(base + f.offset)));
    }
    for (GLuint a = 0; a < 16; a++) {
      const std::uint32_t bit = 1u << a;
      if ((mask & bit) && !(attribMask & bit))
        glEnableVertexAttribArray(a);
      else if (!(mask & bit) && (attribMask & bit))
        glDisableVertexAttribArray(a);
    }
    attribMask = mask;
    stats.attributeSetups++;
    const GLenum mode = primitive == RE4_GX_LINES ? GL_LINES : primitive == RE4_GX_POINTS ? GL_POINTS : GL_TRIANGLES;
    if (!options.probeNoDraw)
      glDrawElements(mode, static_cast<GLsizei>(d.indexCount), GL_UNSIGNED_SHORT,
                     reinterpret_cast<const void *>(static_cast<std::uintptr_t>(d.indexOffset) * 2));
    stats.draws++;
    if (compact)
      stats.compactDraws++;
    if (mode == GL_TRIANGLES)
      stats.triangles += d.indexCount / 3;
  }

  // --- EFB copies ----------------------------------------------------------------------------------

  // Copy format and conversion (gx_gl.cpp CopyMode): mode for the copy shader (0 RGBA8, 1 RGB565,
  // 2 RGB5A3, 3 I, 4 IA, 5 R, 6 G, 7 B, 8 A, 9 RG, 10 GB, 11 RA, 12 depth high byte, 13 middle,
  // 14 low, 15 depth 16 bit, 16 depth 24 bit) and the kind of the result.
  static void CopyMode(std::uint32_t control, bool depth, int *mode, unsigned *kind) {
    const unsigned format = ((control >> 4) & 7) | ((control >> 3) & 1) << 3;
    const bool intensity = (control >> 15) & 1;
    *kind = 0;
    if (depth) {
      switch (format) {
      case 0x9: *mode = 13; *kind = 1; return;
      case 0xA: *mode = 14; *kind = 1; return;
      case 0xB:
      case 0x3:
      case 0xC: *mode = 15; *kind = 2; return;
      case 0x6: *mode = 16; *kind = 0; return;
      default: *mode = 12; *kind = 1; return;
      }
    }
    if (intensity) {
      if (format == 2 || format == 3) {
        *mode = 4;
        *kind = 2;
      } else {
        *mode = 3;
        *kind = 1;
      }
      return;
    }
    switch (format) {
    case 0x0:
    case 0x1:
    case 0x8: *mode = 5; *kind = 1; return;
    case 0x2:
    case 0x3: *mode = 11; *kind = 2; return;
    case 0x4: *mode = 1; return;
    case 0x5: *mode = 2; return;
    case 0x7: *mode = 8; *kind = 1; return;
    case 0x9: *mode = 6; *kind = 1; return;
    case 0xA: *mode = 7; *kind = 1; return;
    case 0xB: *mode = 9; *kind = 2; return;
    case 0xC: *mode = 10; *kind = 2; return;
    default: *mode = 0; return;
    }
  }

  // A depth copy without depth textures: the EFB's depth read back (NV_read_depth) and converted
  // as the copy shader would (modes 12-16), into the bound target of tw x th texels.
  bool DepthReadBack(int x, int y, int w, int h, bool half, int mode, int tw, int th) {
    if (!caps.readDepth)
      return false;
    std::vector<std::uint16_t> depth(static_cast<std::size_t>(w) * h);
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 2);
    while (glGetError() != GL_NO_ERROR) {
    }
    glReadPixels(x, y, w, h, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, depth.data());
    if (glGetError() != GL_NO_ERROR)
      return false;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(tw) * th * 4);
    for (int j = 0; j < th; j++)
      for (int i = 0; i < tw; i++) {
        double z = 0.0;
        if (half) {
          int n = 0;
          for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++) {
              const int sx = std::min(w - 1, i * 2 + dx), sy = std::min(h - 1, j * 2 + dy);
              z += depth[static_cast<std::size_t>(sy) * w + sx];
              n++;
            }
          z /= n;
        } else {
          z = depth[static_cast<std::size_t>(std::min(h - 1, j)) * w + std::min(w - 1, i)];
        }
        const std::uint32_t z24 = static_cast<std::uint32_t>(z / 65535.0 * 16777215.0);
        const std::uint8_t zh = static_cast<std::uint8_t>(z24 >> 16), zm = static_cast<std::uint8_t>(z24 >> 8),
                           zl = static_cast<std::uint8_t>(z24);
        std::uint8_t *o = &rgba[(static_cast<std::size_t>(j) * tw + i) * 4];
        const std::uint8_t px[5][4] = {{zh, zh, zh, zh}, {zm, zm, zm, zm}, {zl, zl, zl, zl}, {zm, zm, zm, zh}, {zh, zm, zl, 255}};
        std::memcpy(o, px[std::min(4, std::max(0, mode - 12))], 4);
      }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return true;
  }

  void EfbCopy(std::uint32_t control) {
    Touched(); // its own pass
    const int x = static_cast<int>(bp[0x49] & 0x3FF), y = static_cast<int>((bp[0x49] >> 10) & 0x3FF);
    const int w = static_cast<int>(bp[0x4A] & 0x3FF) + 1, h = static_cast<int>((bp[0x4A] >> 10) & 0x3FF) + 1;
    const std::uint32_t dest = (bp[0x4B] & 0x1FFFFF) << 5;
    const bool toXfb = (control >> 14) & 1;
    const bool half = !toXfb && ((control >> 9) & 1);
    const bool depth = !toXfb && (bp[0x43] & 7) == 3;
    int mode = 0;
    unsigned kind = 0;
    if (!toXfb)
      CopyMode(control, depth, &mode, &kind);
    const int ow = half ? std::max(1, w / 2) : w, oh = half ? std::max(1, h / 2) : h;
    if ((toXfb && holdXfb) || options.probeNoCopy) {
      // the VI stays on the picture before (RE4_GX_CHUNK_HOLD): only the clear of the EFB
      if (toXfb && holdXfb)
        stats.heldCopies++;
      PlainState();
      glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
      ClearAfterCopy(control, x, y, w, h);
      return;
    }
    GLuint target = 0;
    if (toXfb) {
      if (!xfbTex || xfbWidth != static_cast<unsigned>(w) || xfbHeight != static_cast<unsigned>(h)) {
        if (xfbTex)
          DeleteTexture(xfbTex);
        glGenTextures(1, &xfbTex);
        glBindTexture(GL_TEXTURE_2D, xfbTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S(w), S(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        PlainParams(GL_LINEAR);
        xfbWidth = static_cast<unsigned>(w);
        xfbHeight = static_cast<unsigned>(h);
      }
      target = xfbTex;
      haveXfb = true;
      stats.displayCopies++;
    } else {
      Copy &c = copies[dest];
      if (!c.id || c.width != static_cast<unsigned>(ow) || c.height != static_cast<unsigned>(oh)) {
        if (c.id)
          DeleteTexture(c.id);
        glGenTextures(1, &c.id);
        glBindTexture(GL_TEXTURE_2D, c.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S(ow), S(oh), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        PlainParams(GL_LINEAR);
        texParams[c.id] = TexParams{GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false};
        c.width = static_cast<unsigned>(ow);
        c.height = static_cast<unsigned>(oh);
      }
      c.serial = ++copySerial;
      c.kind = kind;
      target = c.id;
      stats.textureCopies++;
      for (auto it = paletted.begin(); it != paletted.end();) {
        DeleteTexture(it->second.id);
        it = paletted.erase(it);
      }
    }
    const int tw = toXfb ? w : ow, th = toXfb ? h : oh;
    PlainState();
    if (depth) {
      stats.depthCopies++;
      if (!efbDepthTex) {
        glBindTexture(GL_TEXTURE_2D, target);
        if (DepthReadBack(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y), half, mode, S(tw), S(th))) {
          stats.depthReadBacks++;
        } else {
          // neither way: far away everywhere
          std::vector<std::uint8_t> white(static_cast<std::size_t>(S(tw)) * S(th) * 4, 255);
          glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, S(tw), S(th), GL_RGBA, GL_UNSIGNED_BYTE, white.data());
          stats.depthCopiesLost++;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
        ClearAfterCopy(control, x, y, w, h);
        return;
      }
    }
    // render the EFB region into the target (each target keeps a framebuffer of its own: a new
    // attachment makes the driver check the framebuffer again)
    const float gain = depth ? 1.0f : CopyFilterGain();
    const CopyProgram *program = CopyProgramFor(toXfb ? 0 : mode, half, gain != 1.0f);
    if (!program) {
      glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
      ClearAfterCopy(control, x, y, w, h);
      return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, TargetFbo(target));
    glViewport(0, 0, S(tw), S(th));
    glUseProgram(program->id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depth ? efbDepthTex : efbColor);
    // the region as a share of the EFB (the same at any resolution)
    const float fw = static_cast<float>(S(kEfbWidth)), fh = static_cast<float>(S(kEfbHeight));
    glUniform4f(program->rect, static_cast<float>(x) / kEfbWidth, static_cast<float>(y) / kEfbHeight,
                static_cast<float>(w) / kEfbWidth, static_cast<float>(h) / kEfbHeight);
    if (program->texel >= 0)
      glUniform2f(program->texel, 0.5f / fw, 0.5f / fh);
    if (program->gain >= 0)
      glUniform1f(program->gain, gain);
    DrawQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    ClearAfterCopy(control, x, y, w, h);
  }

  // clear the copied region: colour if colour/alpha update is on, depth if depth update is on
  void ClearAfterCopy(std::uint32_t control, int x, int y, int w, int h) {
    if ((control >> 11) & 1) {
      const std::uint32_t c = bp[0x41];
      const bool color = (c >> 3) & 1, alpha = (c >> 4) & 1, zupdate = (bp[0x40] >> 4) & 1;
      glEnable(GL_SCISSOR_TEST);
      glScissor(S(x), S(y), S(x + w) - S(x), S(y + h) - S(y));
      glColorMask(color, color, color, alpha);
      glDepthMask(zupdate ? GL_TRUE : GL_FALSE);
      const float r = static_cast<float>(bp[0x4F] & 255) / 255.0f, a = static_cast<float>((bp[0x4F] >> 8) & 255) / 255.0f;
      const float b = static_cast<float>(bp[0x50] & 255) / 255.0f, g = static_cast<float>((bp[0x50] >> 8) & 255) / 255.0f;
      glClearColor(r, g, b, a);
      glClearDepthf(static_cast<float>(bp[0x51] & 0xFFFFFF) / 16777215.0f);
      GLbitfield mask = 0;
      if (color || alpha)
        mask |= GL_COLOR_BUFFER_BIT;
      if (zupdate)
        mask |= GL_DEPTH_BUFFER_BIT;
      if (mask)
        glClear(mask);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDepthMask(GL_TRUE);
    }
    Touched();
  }

  // --- programs of the renderer itself -------------------------------------------------------

  // EFB to a copy (gx_gl.cpp kCopyFs), one program per conversion instead of a branch for each
  // (on the Tegra 3 a copy costs its pixels times the shader's length: about 9 ms each with the
  // branches): the source rectangle (the vertex shader maps it, normalised), optionally the 2x2
  // box filter, the copy filter's gain, the copy format's conversion (CopyMode).
  struct CopyProgram {
    GLuint id = 0;
    GLint rect = -1, texel = -1, gain = -1;
  };
  std::unordered_map<unsigned, CopyProgram> copyPrograms;

  const CopyProgram *CopyProgramFor(int mode, bool half, bool gain) {
    const unsigned key = static_cast<unsigned>(mode) | (half ? 1u : 0u) << 5 | (gain && mode < 12 ? 1u : 0u) << 6;
    auto it = copyPrograms.find(key);
    if (it != copyPrograms.end())
      return it->second.id ? &it->second : nullptr;
    static const char kCopyVs[] = "attribute vec2 aQuad;\nuniform vec4 rect;\nvarying vec2 vUv;\n"
                                  "void main() {\n"
                                  "  vUv = rect.xy + aQuad * rect.zw;\n"
                                  "  gl_Position = vec4(aQuad * 2.0 - 1.0, 0.0, 1.0);\n"
                                  "}\n";
    std::string fs = std::string(kFragmentPrecision) +
                     "uniform sampler2D src;\nuniform vec2 texel;\nuniform float gain;\nvarying vec2 vUv;\n"
                     "void main() {\n";
    fs += half ? "  vec4 c = 0.25 * (texture2D(src, vUv - texel) + texture2D(src, vUv + vec2(texel.x, -texel.y)) + "
                 "texture2D(src, vUv + vec2(-texel.x, texel.y)) + texture2D(src, vUv + texel));\n"
               : "  vec4 c = texture2D(src, vUv);\n";
    if (key & 64)
      fs += "  c.rgb = min(c.rgb * gain, vec3(1.0));\n";
    if (mode == 3 || mode == 4)
      fs += "  float y = clamp(0.257 * c.r + 0.504 * c.g + 0.098 * c.b + 16.0 / 255.0, 0.0, 1.0);\n";
    if (mode >= 12)
      fs += "  float z = floor(c.r * 16777215.0);\n"
            "  float zh = floor(z / 65536.0) / 255.0, zm = mod(floor(z / 256.0), 256.0) / 255.0, "
            "zl = mod(z, 256.0) / 255.0;\n";
    static const char *conversion[17] = {
        "c",
        "vec4(floor(c.rgb * vec3(31.0, 63.0, 31.0) + 0.5) / vec3(31.0, 63.0, 31.0), 1.0)",
        // NOLINTNEXTLINE: one entry
        ("c.a >= 0.999 ? vec4(floor(c.rgb * 31.0 + 0.5) / 31.0, 1.0) : "
         "vec4(floor(c * vec4(15.0, 15.0, 15.0, 7.0) + 0.5) / vec4(15.0, 15.0, 15.0, 7.0))"),
        "vec4(y)",
        "vec4(y, y, y, c.a)",
        "vec4(c.r)",
        "vec4(c.g)",
        "vec4(c.b)",
        "vec4(c.a)",
        "vec4(c.g, c.g, c.g, c.r)",
        "vec4(c.b, c.b, c.b, c.g)",
        "vec4(c.r, c.r, c.r, c.a)",
        "vec4(zh)",
        "vec4(zm)",
        "vec4(zl)",
        "vec4(zm, zm, zm, zh)",
        "vec4(zh, zm, zl, 1.0)"};
    fs += std::string("  gl_FragColor = ") + conversion[std::max(0, std::min(16, mode))] + ";\n}\n";
    const char *quad[] = {"aQuad"};
    std::string log;
    CopyProgram p;
    p.id = Link(kCopyVs, fs, quad, 1, &log);
    if (!p.id) {
      GXLOG("copy shader (mode %d) failed: %s", mode, log.c_str());
    } else {
      glUseProgram(p.id);
      glUniform1i(glGetUniformLocation(p.id, "src"), 0);
      p.rect = glGetUniformLocation(p.id, "rect");
      p.texel = glGetUniformLocation(p.id, "texel");
      p.gain = glGetUniformLocation(p.id, "gain");
      current = nullptr;
    }
    auto &slot = copyPrograms[key];
    slot = p;
    return slot.id ? &slot : nullptr;
  }

  bool MakeUtilityPrograms(std::string *error) {
    const char *quad[] = {"aQuad"};
    // a copy read through a palette (gx_gl.cpp kPaletteFs): one channel .r, two .a high .r low
    const std::string paletteFs =
        std::string(kFragmentPrecision) +
        "uniform sampler2D src;\nuniform sampler2D pal;\nuniform vec2 palSize;\nuniform float kind;\n"
        "uniform float format;\nvarying vec2 vUv;\n"
        "void main() {\n"
        "  vec4 c = texture2D(src, vUv);\n"
        "  float index = kind > 1.5 ? mod(floor(c.a * 255.0 + 0.5) * 256.0 + floor(c.r * 255.0 + 0.5), 16384.0) : "
        "floor(c.r * 255.0 + 0.5);\n"
        "  if (format > 7.5 && format < 8.5) index = floor(index / 16.0);\n"
        "  vec2 at = vec2(mod(index, palSize.x), floor(index / palSize.x));\n"
        "  gl_FragColor = texture2D(pal, (at + 0.5) / palSize);\n"
        "}\n";
    // rows: XFB texture rows shown (offset, span; 0..1), row 0 at the top
    const std::string presentFs = std::string(kFragmentPrecision) +
                                  "uniform sampler2D src;\nuniform vec2 rows;\nvarying vec2 vUv;\n"
                                  "void main() { gl_FragColor = vec4(texture2D(src, vec2(vUv.x, rows.x + (1.0 - vUv.y) * "
                                  "rows.y)).rgb, 1.0); }\n";
    std::string log;
    if (!CopyProgramFor(0, false, false)) {
      *error = "copy shader";
      return false;
    }
    paletteProgram = Link(kQuadVs, paletteFs, quad, 1, &log);
    if (!paletteProgram) {
      *error = "palette shader: " + log;
      return false;
    }
    glUseProgram(paletteProgram);
    glUniform1i(glGetUniformLocation(paletteProgram, "src"), 0);
    glUniform1i(glGetUniformLocation(paletteProgram, "pal"), 1);
    paletteSize = glGetUniformLocation(paletteProgram, "palSize");
    paletteKind = glGetUniformLocation(paletteProgram, "kind");
    paletteFormat = glGetUniformLocation(paletteProgram, "format");
    presentProgram = Link(kQuadVs, presentFs, quad, 1, &log);
    if (!presentProgram) {
      *error = "present shader: " + log;
      return false;
    }
    glUseProgram(presentProgram);
    glUniform1i(glGetUniformLocation(presentProgram, "src"), 0);
    presentRows = glGetUniformLocation(presentProgram, "rows");
    return true;
  }

  // The EFB's depth: a texture where depth copies can sample it, else a renderbuffer, the most
  // bits the device takes. False: no depth buffer works.
  bool MakeEfbDepth(int w, int h) {
    while (glGetError() != GL_NO_ERROR) {
    }
    if (caps.depthTexture) {
      // 24 bits like the GameCube's where the device packs depth with stencil, else its own
      const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
      for (int packed = HasExtension(ext, "GL_OES_packed_depth_stencil") ? 1 : 0; packed >= 0; packed--) {
        while (glGetError() != GL_NO_ERROR) {
        }
        glGenTextures(1, &efbDepthTex);
        glBindTexture(GL_TEXTURE_2D, efbDepthTex);
        if (packed)
          glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL_OES, w, h, 0, GL_DEPTH_STENCIL_OES, GL_UNSIGNED_INT_24_8_OES,
                       nullptr);
        else
          glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        PlainParams(GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, efbDepthTex, 0);
        if (glGetError() == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
          return true;
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glDeleteTextures(1, &efbDepthTex);
        efbDepthTex = 0;
      }
      caps.depthTexture = false;
    }
    std::vector<GLenum> formats;
    if (caps.depth24)
      formats.push_back(GL_DEPTH_COMPONENT24_OES);
    if (caps.depthNonlinear)
      formats.push_back(GL_DEPTH_COMPONENT16_NONLINEAR_NV);
    formats.push_back(GL_DEPTH_COMPONENT16);
    for (GLenum format : formats) {
      while (glGetError() != GL_NO_ERROR) {
      }
      glGenRenderbuffers(1, &efbDepthRb);
      glBindRenderbuffer(GL_RENDERBUFFER, efbDepthRb);
      glRenderbufferStorage(GL_RENDERBUFFER, format, w, h);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, efbDepthRb);
      if (glGetError() == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        caps.depth24 = format == GL_DEPTH_COMPONENT24_OES;
        caps.depthNonlinear = format == GL_DEPTH_COMPONENT16_NONLINEAR_NV;
        return true;
      }
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
      glDeleteRenderbuffers(1, &efbDepthRb);
      efbDepthRb = 0;
    }
    return false;
  }

  void Capabilities() {
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    caps.renderer = std::string(renderer ? renderer : "?") + ", " + (version ? version : "?");
    const bool tegra3 = options.tegra3;
    caps.depthTexture = !tegra3 && HasExtension(ext, "GL_OES_depth_texture");
    caps.depth24 = !tegra3 && HasExtension(ext, "GL_OES_depth24");
    caps.depthNonlinear = HasExtension(ext, "GL_NV_depth_nonlinear");
    caps.readDepth = HasExtension(ext, "GL_NV_read_depth");
    caps.npotRepeat = !tegra3 && HasExtension(ext, "GL_OES_texture_npot");
    caps.npotMipmap = HasExtension(ext, "GL_OES_texture_npot") || HasExtension(ext, "GL_NV_texture_npot_2D_mipmap");
    // RE4_GX2_MAKE_LEVELS=1 (tests): the missing mipmap levels made, as on the Tegra 3
    const char *makeLevels = std::getenv("RE4_GX2_MAKE_LEVELS");
    caps.maxLevel = !tegra3 && !(makeLevels && *makeLevels == '1') && HasExtension(ext, "GL_APPLE_texture_max_level");
    GLint range[2] = {}, precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    caps.fragmentHigh = precision > 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &caps.maxAttribs);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &caps.maxVertexUniforms);
    glGetIntegerv(GL_MAX_VARYING_VECTORS, &caps.maxVaryings);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps.maxTextureSize);
    if (tegra3) {
      caps.maxAttribs = std::min(caps.maxAttribs, 16);
      caps.maxVertexUniforms = std::min(caps.maxVertexUniforms, 252);
      caps.maxVaryings = std::min(caps.maxVaryings, 9);
    }
  }
};

GxRenderer::GxRenderer() : impl_(new Impl) {}

GxRenderer::~GxRenderer() = default;

void GxRenderer::SetOptions(const GxOptions &options) { impl_->options = options; }

bool GxRenderer::Init(int scale, std::string *error) {
  Impl &m = *impl_;
  std::string dummy;
  if (!error)
    error = &dummy;
  m.eighths = 8 * std::max(1, scale);
  if (const char *res = std::getenv("RE4_GX2_SCALE"))
    m.options.resolutionEighths = static_cast<int>(std::atof(res) * 8.0 + 0.5);
  if (m.options.resolutionEighths > 0)
    m.eighths = std::min(32, std::max(2, m.options.resolutionEighths));
  if (const char *probe = std::getenv("RE4_GX2_PROBE")) {
    const std::string p = std::string(",") + probe + ",";
    m.options.probeFlat = m.options.probeFlat || p.find(",flat,") != std::string::npos;
    m.options.probeNoCopy = m.options.probeNoCopy || p.find(",nocopy,") != std::string::npos;
    m.options.probeNoDraw = m.options.probeNoDraw || p.find(",nodraw,") != std::string::npos;
    m.options.probeNoUpload = m.options.probeNoUpload || p.find(",noupload,") != std::string::npos;
    m.options.probeNoAttrib = m.options.probeNoAttrib || p.find(",noattrib,") != std::string::npos;
    m.options.probeNoUniform = m.options.probeNoUniform || p.find(",nouniform,") != std::string::npos;
    m.options.probeSimpleVs = m.options.probeSimpleVs || p.find(",simplevs,") != std::string::npos;
    m.options.probeHalf = m.options.probeHalf || p.find(",half,") != std::string::npos;
  }
  if (const char *fast = std::getenv("RE4_GX2_FAST_TEV"))
    m.options.fastTev = *fast == '1';
  if (const char *tex16 = std::getenv("RE4_GX2_TEX16"))
    m.options.tex16 = *tex16 == '1';
  if (const char *efb16 = std::getenv("RE4_GX2_EFB16"))
    m.options.efb16 = *efb16 == '1';
  if (const char *lights = std::getenv("RE4_GX2_LIGHTS"))
    m.options.maxLights = std::min(7, std::max(0, std::atoi(lights)));
  const char *tegra3 = std::getenv("RE4_GX2_TEGRA3");
  if (tegra3 && *tegra3 == '1')
    m.options.tegra3 = true;
  for (auto &s : m.posTexStamp)
    s = 1;
  for (auto &s : m.normalStamp)
    s = 1;
  for (auto &s : m.postStamp)
    s = 1;
  for (auto &s : m.lightStamp)
    s = 1;
  m.Capabilities();
  const int w = m.S(kEfbWidth), h = m.S(kEfbHeight);
  glGenTextures(1, &m.efbColor);
  glBindTexture(GL_TEXTURE_2D, m.efbColor);
  if (m.options.efb16)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
  else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  // copies read whole pixels, as the GameCube's (the GLES 3 renderer filters linearly at the
  // pixel centres: a coordinate a little off mixes in the neighbour, with the Tegra's fp20
  // coordinates by up to a tenth of a pixel; the 2x2 box of half copies reads pixel centres too);
  // linearly with texelCoords, as the GLES 3 renderer (tests)
  Impl::PlainParams(m.options.texelCoords ? GL_LINEAR : GL_NEAREST);
  glGenFramebuffers(1, &m.efbFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, m.efbFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m.efbColor, 0);
  if (!m.MakeEfbDepth(w, h)) {
    *error = "EFB framebuffer incomplete (no depth buffer the device takes)";
    return false;
  }
  glGetIntegerv(GL_DEPTH_BITS, &m.caps.depthBits);
  // once per process: what the device offers
  static bool described = false;
  if (!described) {
    described = true;
    const GxCaps &c = m.caps;
    GXLOG("GPU: %s; EFB depth %d bits as %s%s; depth copies %s; NPOT repeat %s, mipmaps %s; fragment highp %s; "
          "attributes %d, vertex vec4 %d, varyings %d, textures up to %d%s",
          c.renderer.c_str(), c.depthBits, c.depthTexture ? "texture" : "renderbuffer",
          c.depthNonlinear ? " (nonlinear)" : "", c.depthTexture ? "on the GPU" : c.readDepth ? "read back" : "lost",
          c.npotRepeat ? "yes" : "no", c.npotMipmap ? "yes" : "no", c.fragmentHigh ? "yes" : "no (mediump)",
          c.maxAttribs, c.maxVertexUniforms, c.maxVaryings, c.maxTextureSize,
          m.options.tegra3 ? " (as the Tegra 3)" : "");
  }
  glViewport(0, 0, w, h);
  // the EFB as every display copy of the game leaves it: black with alpha 0 (gx_gl.cpp Init)
  glClearColor(0, 0, 0, 0);
  glClearDepthf(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glGenBuffers(1, &m.vbo);
  glGenBuffers(1, &m.ibo);
  glGenBuffers(1, &m.quadVbo);
  const float quad[8] = {0, 0, 1, 0, 0, 1, 1, 1};
  glBindBuffer(GL_ARRAY_BUFFER, m.quadVbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
  const std::uint8_t white[4] = {255, 255, 255, 255};
  glGenTextures(1, &m.whiteTex);
  glBindTexture(GL_TEXTURE_2D, m.whiteTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  Impl::PlainParams(GL_NEAREST);
  m.texParams[m.whiteTex] = TexParams{GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, false};
  for (auto &b : m.bound)
    b.id = m.whiteTex;
  if (!m.MakeUtilityPrograms(error))
    return false;
  glBindFramebuffer(GL_FRAMEBUFFER, m.efbFbo);
  return true;
}

const GxCaps &GxRenderer::Caps() const { return impl_->caps; }

void GxRenderer::Run(const Re4GxChunk &chunk) {
  Impl &m = *impl_;
  if (chunk.version != RE4_GX_FRAME_VERSION)
    return;
  m.stats.chunks++;
  m.Touched();
  m.holdXfb = (chunk.flags & RE4_GX_CHUNK_HOLD) != 0;
  if (chunk.flags & RE4_GX_CHUNK_XFB)
    m.viBlack = (chunk.flags & RE4_GX_CHUNK_BLACK) != 0;
  if (chunk.flags & RE4_GX_CHUNK_RESET) {
    m.stats.resets++;
    m.DropTextures();
    m.compactValid = false;
  }
  // this chunk's vertices and indices (a skipped chunk draws nothing)
  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  if (!m.skipDraws && (!m.options.probeNoUpload || m.stats.chunks == 1))
    glBufferData(GL_ARRAY_BUFFER, chunk.vertexBytes ? chunk.vertexBytes : 4, chunk.vertices, GL_STREAM_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
  if (!m.skipDraws && (!m.options.probeNoUpload || m.stats.chunks == 1))
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunk.indexCount ? chunk.indexCount * 2 : 4, chunk.indices, GL_STREAM_DRAW);
  glBindFramebuffer(GL_FRAMEBUFFER, m.efbFbo);
  // state other GL code of the app may have left
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  glDisable(GL_DITHER);
  glFrontFace(GL_CCW);
  for (GLuint a = 0; a < 16; a++)
    glDisableVertexAttribArray(a);
  m.attribMask = 0;
  m.attrLayout = ~0u;
  std::uint32_t at = 0;
  while (at < chunk.opWords) {
    const std::uint32_t head = chunk.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    const std::uint32_t *p = chunk.ops + at + 1;
    if (at + 1 + n > chunk.opWords)
      break;
    switch (op) {
    case RE4_GX_OP_BP:
      if (n >= 1)
        m.BpWrite(p[0], true);
      break;
    case RE4_GX_OP_BP_STATE:
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        m.BpWrite(r << 24 | (p[r] & 0xFFFFFF), false);
      break;
    case RE4_GX_OP_XF:
      if (n >= 1)
        m.XfWrite(p[0], p + 1, n - 1);
      break;
    case RE4_GX_OP_TLUT:
      if (n >= 3 && p[0] < kTlutBytes && p[2] + p[1] <= chunk.blobBytes)
        std::memcpy(m.tlut.data() + p[0], chunk.blob + p[2], std::min<std::uint32_t>(p[1], kTlutBytes - p[0]));
      break;
    case RE4_GX_OP_TEXTURE:
      if (n * 4 >= sizeof(Re4GxTexture)) {
        Re4GxTexture t;
        std::memcpy(&t, p, sizeof(t));
        m.BindTexture(t, chunk);
      }
      break;
    case RE4_GX_OP_FORGET:
      if (n >= 2)
        m.Forget(static_cast<std::uint64_t>(p[1]) << 32 | p[0]);
      break;
    case RE4_GX_OP_DRAW:
      if (n * 4 >= sizeof(Re4GxDraw)) {
        Re4GxDraw d;
        std::memcpy(&d, p, sizeof(d));
        m.Draw(d, chunk);
      }
      break;
    case RE4_GX_OP_COMPACT_FORMAT:
      if (n >= RE4_GX_COMPACT_ATTRS)
        m.CompactFormat(p);
      break;
    default:
      break;
    }
    at += 1 + n;
  }
  // leave GL as the app's other code expects it
  for (GLuint a = 0; a < 16; a++)
    if (m.attribMask & (1u << a))
      glDisableVertexAttribArray(a);
  m.attribMask = 0;
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(0);
  m.PlainState();
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GxRenderer::CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const {
  const Impl &m = *impl_;
  const std::uint32_t refuse = RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_HOLD | RE4_GX_CHUNK_BLACK | RE4_GX_CHUNK_MARK;
  if (a.version != RE4_GX_FRAME_VERSION || b.version != RE4_GX_FRAME_VERSION || (a.flags & refuse) ||
      !(a.flags & RE4_GX_CHUNK_XFB) || m.viBlack)
    return false;
  // the EFB copies for textures in `a` (their targets as the BP state decides them), and which copy
  // each texture map holds (0: none), from the chunk run last on
  std::vector<std::uint32_t> bp(m.bp, m.bp + 256), pending;
  std::uint32_t mapCopy[8];
  for (int i = 0; i < 8; i++)
    mapCopy[i] = m.bound[i].copy;
  auto texture = [&](const std::uint32_t *p, std::uint32_t n, Re4GxTexture *t) {
    if (n * 4 < sizeof(Re4GxTexture))
      return false;
    std::memcpy(t, p, sizeof(*t));
    mapCopy[t->map & 7] = (t->flags & RE4_GX_TEX_COPY) ? t->address : 0;
    return true;
  };
  for (std::uint32_t at = 0; at < a.opWords;) {
    const std::uint32_t head = a.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    if (at + 1 + n > a.opWords)
      return false;
    const std::uint32_t *p = a.ops + at + 1;
    Re4GxTexture t;
    if (op == RE4_GX_OP_BP && n >= 1) {
      const unsigned reg = p[0] >> 24;
      bp[reg] = p[0] & 0xFFFFFF;
      if (reg == 0x52 && !((bp[0x52] >> 14) & 1))
        pending.push_back((bp[0x4B] & 0x1FFFFF) << 5);
    } else if (op == RE4_GX_OP_BP_STATE) {
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        bp[r] = p[r] & 0xFFFFFF;
    } else if (op == RE4_GX_OP_TEXTURE) {
      texture(p, n, &t);
    }
    at += 1 + n;
  }
  if (pending.empty())
    return true;
  auto isPending = [&](std::uint32_t address) {
    return address && std::find(pending.begin(), pending.end(), address) != pending.end();
  };
  // `b` must copy to each of them again before it reads one (a texture op for it, or a draw whose
  // stages sample a map still holding it)
  for (std::uint32_t at = 0; at < b.opWords && !pending.empty();) {
    const std::uint32_t head = b.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    if (at + 1 + n > b.opWords)
      return false;
    const std::uint32_t *p = b.ops + at + 1;
    Re4GxTexture t;
    if (op == RE4_GX_OP_DRAW) {
      const std::uint32_t gen = bp[0x00];
      const unsigned stages = ((gen >> 10) & 15) + 1, indStages = (gen >> 16) & 7;
      for (unsigned st = 0; st < stages; st++) {
        const std::uint32_t o = bp[0x28 + st / 2] >> ((st & 1) ? 12 : 0);
        if (((o >> 6) & 1) && isPending(mapCopy[o & 7]))
          return false;
      }
      for (unsigned st = 0; st < indStages && st < 4; st++)
        if (isPending(mapCopy[(bp[0x27] >> (6 * st)) & 7]))
          return false;
    } else if (op == RE4_GX_OP_TEXTURE) {
      if (!texture(p, n, &t))
        return false;
      if ((t.flags & RE4_GX_TEX_COPY) && isPending(t.address))
        return false;
    } else if (op == RE4_GX_OP_BP && n >= 1) {
      const unsigned reg = p[0] >> 24;
      bp[reg] = p[0] & 0xFFFFFF;
      if (reg == 0x52 && !((bp[0x52] >> 14) & 1)) {
        const std::uint32_t dest = (bp[0x4B] & 0x1FFFFF) << 5;
        pending.erase(std::remove(pending.begin(), pending.end(), dest), pending.end());
      }
    } else if (op == RE4_GX_OP_BP_STATE) {
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        bp[r] = p[r] & 0xFFFFFF;
    }
    at += 1 + n;
  }
  return pending.empty();
}

void GxRenderer::Skip(const Re4GxChunk &a) {
  impl_->skipDraws = true;
  Run(a);
  impl_->skipDraws = false;
}

GLuint GxRenderer::Xfb(unsigned *width, unsigned *height) const {
  if (width)
    *width = static_cast<unsigned>(impl_->S(static_cast<int>(impl_->xfbWidth)));
  if (height)
    *height = static_cast<unsigned>(impl_->S(static_cast<int>(impl_->xfbHeight)));
  return impl_->haveXfb ? impl_->xfbTex : 0;
}

bool GxRenderer::Present(int x, int y, int w, int h, unsigned top, unsigned rows) {
  Impl &m = *impl_;
  if (!m.haveXfb)
    return false;
  m.stats.presents++;
  m.PlainState();
  if (m.viBlack) {
    // VISetBlack: the GameCube's video output is black, whatever the XFB holds
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    return true;
  }
  glViewport(x, y, w, h);
  glUseProgram(m.presentProgram);
  const float lines = static_cast<float>(m.xfbHeight ? m.xfbHeight : 1);
  if (rows == 0 || top + rows > m.xfbHeight)
    glUniform2f(m.presentRows, 0.0f, 1.0f);
  else
    glUniform2f(m.presentRows, static_cast<float>(top) / lines, static_cast<float>(rows) / lines);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m.xfbTex);
  m.DrawQuad();
  glDisableVertexAttribArray(0);
  m.attribMask = 0;
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  return true;
}

GLuint GxRenderer::EfbColor() const { return impl_->efbColor; }

int GxRenderer::Scale() const { return impl_->eighths / 8; }
int GxRenderer::ResolutionEighths() const { return impl_->eighths; }

const GxStats &GxRenderer::Stats() const { return impl_->stats; }

void GxRenderer::Release() {
  Impl &m = *impl_;
  m.DropTextures();
  for (auto &c : m.copies)
    glDeleteTextures(1, &c.second.id);
  m.copies.clear();
  for (auto &p : m.programs)
    if (p.second.id)
      glDeleteProgram(p.second.id);
  m.programs.clear();
  const GLuint textures[] = {m.efbColor, m.efbDepthTex, m.xfbTex, m.whiteTex};
  glDeleteTextures(4, textures);
  glDeleteRenderbuffers(1, &m.efbDepthRb);
  const GLuint fbos[] = {m.efbFbo};
  glDeleteFramebuffers(1, fbos);
  for (auto &f : m.targetFbos)
    glDeleteFramebuffers(1, &f.second);
  m.targetFbos.clear();
  const GLuint buffers[] = {m.vbo, m.ibo, m.quadVbo};
  glDeleteBuffers(3, buffers);
  for (auto &p : m.copyPrograms)
    if (p.second.id)
      glDeleteProgram(p.second.id);
  m.copyPrograms.clear();
  glDeleteProgram(m.paletteProgram);
  glDeleteProgram(m.presentProgram);
  const GxOptions options = m.options;
  impl_.reset(new Impl);
  impl_->options = options;
}

} // namespace re4gx2
