// Our ETC2/EAC decoder against a GL driver's (Mesa in WSL/Linux): random blocks of every mode and the encoder's
// blocks of test images go to glCompressedTexImage2D, a pass with texelFetch reads them back, and every texel is
// compared with etc2::DecodeLevel. A difference means our reading (and so our writing) of the format is wrong.
// Run: EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 ./etc2_gl_check
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "bc7.h"
#include "etc2.h"

#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#define GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 0x9276
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif

namespace {

const unsigned kW = 256, kH = 256;

GLuint Shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
    std::printf("shader: %s\n", log);
  }
  return s;
}

// 0 individual, 1 differential, 2 T, 3 H, 4 planar (for the report)
int Mode(const std::uint8_t *b, bool punchthrough) {
  std::uint64_t w = 0;
  for (int i = 0; i < 8; i++) w = w << 8 | b[i];
  if (!punchthrough && !((w >> 33) & 1)) return 0;
  auto sext = [](int v) { return v & 4 ? v - 8 : v; };
  const int r = (w >> 59) & 31, g = (w >> 51) & 31, bb = (w >> 43) & 31;
  const int r2 = r + sext((w >> 56) & 7), g2 = g + sext((w >> 48) & 7), b2 = bb + sext((w >> 40) & 7);
  if (r2 < 0 || r2 > 31) return 2;
  if (g2 < 0 || g2 > 31) return 3;
  if (b2 < 0 || b2 > 31) return 4;
  return 1;
}

int Compare(GLuint program, etc2::Kind kind, GLenum glFormat, const std::vector<std::uint8_t> &data, const char *what) {
  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  glCompressedTexImage2D(GL_TEXTURE_2D, 0, glFormat, kW, kH, 0, static_cast<GLsizei>(data.size()), data.data());
  if (glGetError() != GL_NO_ERROR) {
    std::printf("%s: upload failed\n", what);
    return 1;
  }
  glUseProgram(program);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::vector<std::uint8_t> gl(kW * kH * 4), ours(kW * kH * 4);
  glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, gl.data());
  etc2::DecodeLevel(kind, data.data(), kW, kH, ours.data());
  glDeleteTextures(1, &tex);
  const std::size_t blockBytes = etc2::BlockBytes(kind);
  int badBlocks = 0, modeBad[5] = {}, modeAll[5] = {}, mulZeroBad = 0;
  for (unsigned by = 0; by < kH / 4; by++)
    for (unsigned bx = 0; bx < kW / 4; bx++) {
      const std::uint8_t *blk = data.data() + (by * (kW / 4) + bx) * blockBytes;
      const int mode = Mode(blk + (kind == etc2::kRgbaEac ? 8 : 0), kind == etc2::kRgbA1);
      modeAll[mode]++;
      bool bad = false;
      for (unsigned y = 0; y < 4 && !bad; y++)
        for (unsigned x = 0; x < 4 && !bad; x++) {
          const std::size_t i = ((by * 4 + y) * kW + bx * 4 + x) * 4;
          // a transparent texel's colour is not compared (both must say alpha 0)
          const int channels = (kind == etc2::kRgbA1 && ours[i + 3] == 0 && gl[i + 3] == 0) ? 0 : 4;
          for (int c = 0; c < channels; c++)
            if (gl[i + c] != ours[i + c]) bad = true;
          if (kind == etc2::kRgbA1 && (ours[i + 3] == 0) != (gl[i + 3] == 0)) bad = true;
        }
      if (bad) {
        if (badBlocks < 3) {
          std::printf("  %s: block %u,%u differs (mode %d)", what, bx, by, mode);
          for (std::size_t k = 0; k < blockBytes; k++) std::printf("%s%02x", k ? "" : " ", blk[k]);
          const std::size_t i = ((by * 4) * kW + bx * 4) * 4;
          std::printf("  gl %d,%d,%d,%d ours %d,%d,%d,%d\n", gl[i], gl[i + 1], gl[i + 2], gl[i + 3], ours[i], ours[i + 1],
                      ours[i + 2], ours[i + 3]);
        }
        badBlocks++;
        modeBad[mode]++;
        if (kind == etc2::kRgbaEac && ((blk[1] >> 4) & 15) == 0) mulZeroBad++;
      }
    }
  std::printf("%-34s %5d of %d blocks differ  (modes individual/diff/T/H/planar: %d/%d/%d/%d/%d, differing %d/%d/%d/%d/%d)%s\n",
              what, badBlocks, (kW / 4) * (kH / 4), modeAll[0], modeAll[1], modeAll[2], modeAll[3], modeAll[4], modeBad[0],
              modeBad[1], modeBad[2], modeBad[3], modeBad[4],
              mulZeroBad ? (" (" + std::to_string(mulZeroBad) + " with EAC multiplier 0)").c_str() : "");
  return badBlocks ? 1 : 0;
}

}  // namespace

int main() {
  EGLDisplay dpy = EGL_NO_DISPLAY;
  auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (getPlatformDisplay) dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!eglInitialize(dpy, nullptr, nullptr)) {
    std::printf("no EGL display\n");
    return 2;
  }
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig cfg;
  EGLint n = 0;
  eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n);
  const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, n ? cfg : nullptr, EGL_NO_CONTEXT, ctxAttr);
  if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
    std::printf("no GLES 3 context\n");
    return 2;
  }
  std::printf("GL: %s, %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  GLuint rb, fb;
  glGenRenderbuffers(1, &rb);
  glBindRenderbuffer(GL_RENDERBUFFER, rb);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kW, kH);
  glGenFramebuffers(1, &fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
  glViewport(0, 0, kW, kH);
  const GLuint program = glCreateProgram();
  glAttachShader(program, Shader(GL_VERTEX_SHADER,
                                 "#version 300 es\nvoid main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));"
                                 "gl_Position=vec4(p*2.0-1.0,0.0,1.0);}"));
  glAttachShader(program, Shader(GL_FRAGMENT_SHADER,
                                 "#version 300 es\nprecision highp float;uniform highp sampler2D t;out vec4 o;"
                                 "void main(){o=texelFetch(t,ivec2(gl_FragCoord.xy),0);}"));
  glLinkProgram(program);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  int failures = 0;
  std::mt19937_64 rng(7);
  const etc2::Kind kinds[3] = {etc2::kRgb, etc2::kRgbA1, etc2::kRgbaEac};
  const GLenum formats[3] = {GL_COMPRESSED_RGB8_ETC2, GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
                             GL_COMPRESSED_RGBA8_ETC2_EAC};
  const char *names[3] = {"RGB8_ETC2", "RGB8_PUNCHTHROUGH_ALPHA1_ETC2", "RGBA8_ETC2_EAC"};
  for (int k = 0; k < 3; k++) {
    // random blocks: every mode, every bit
    std::vector<std::uint8_t> data((kW / 4) * (kH / 4) * etc2::BlockBytes(kinds[k]));
    for (auto &b : data) b = static_cast<std::uint8_t>(rng());
    failures += Compare(program, kinds[k], formats[k], data, (std::string(names[k]) + " random").c_str());
    // the encoder's blocks of a test image (gradients, edges, noise, transparency)
    std::vector<std::uint8_t> rgba(kW * kH * 4);
    for (unsigned y = 0; y < kH; y++)
      for (unsigned x = 0; x < kW; x++) {
        std::uint8_t *p = rgba.data() + (y * kW + x) * 4;
        const unsigned region = (x / 64) + 4 * (y / 64);
        const int noise = static_cast<int>(rng() % 31) - 15;
        auto c = [](int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); };
        switch (region % 4) {
          case 0: p[0] = c(x * 4); p[1] = c(y * 4 % 256); p[2] = c(128 + noise); break;
          case 1: p[0] = c(((x / 4 + y / 4) & 1) ? 220 : 30); p[1] = c(90 + noise); p[2] = c(x); break;
          case 2: p[0] = c(static_cast<int>(rng() % 256)); p[1] = c(static_cast<int>(rng() % 256)); p[2] = c(static_cast<int>(rng() % 256)); break;
          default: p[0] = c(100 + 3 * static_cast<int>(x % 64)); p[1] = c(60 + 2 * static_cast<int>(y % 64)); p[2] = c(40 + noise); break;
        }
        p[3] = kinds[k] == etc2::kRgbA1 ? (((x * 5 + y * 3) % 13) < 4 ? 0 : 255) : c(static_cast<int>((x + 2 * y) % 256));
      }
    std::vector<std::uint8_t> enc;
    etc2::EncodeLevel(kinds[k], rgba.data(), kW, kH, 1, enc);
    failures += Compare(program, kinds[k], formats[k], enc, (std::string(names[k]) + " encoder").c_str());
  }
  // BC7 against the driver's BPTC decoder (GL_EXT_texture_compression_bptc), random blocks of every mode
  const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
  if (ext && std::strstr(ext, "GL_EXT_texture_compression_bptc")) {
    for (int mode = 0; mode <= 8; mode++) {
      std::vector<std::uint8_t> data((kW / 4) * (kH / 4) * 16);
      for (std::size_t i = 0; i < data.size(); i++) data[i] = static_cast<std::uint8_t>(rng());
      for (std::size_t b = 0; b < data.size(); b += 16) {
        const int m = mode < 8 ? mode : static_cast<int>(rng() % 8);
        data[b] = static_cast<std::uint8_t>((data[b] << (m + 1)) | (1 << m));
      }
      GLuint tex;
      glGenTextures(1, &tex);
      glBindTexture(GL_TEXTURE_2D, tex);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glCompressedTexImage2D(GL_TEXTURE_2D, 0, 0x8E8C /* GL_COMPRESSED_RGBA_BPTC_UNORM_EXT */, kW, kH, 0,
                             static_cast<GLsizei>(data.size()), data.data());
      glUseProgram(program);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      std::vector<std::uint8_t> gl(kW * kH * 4);
      glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, gl.data());
      glDeleteTextures(1, &tex);
      int bad = 0;
      for (unsigned by = 0; by < kH / 4; by++)
        for (unsigned bx = 0; bx < kW / 4; bx++) {
          std::uint8_t px[64];
          img::DecodeBc7Block(data.data() + (by * (kW / 4) + bx) * 16, px);
          bool differs = false;
          for (unsigned y = 0; y < 4; y++)
            for (unsigned x = 0; x < 4; x++)
              if (std::memcmp(px + (y * 4 + x) * 4, gl.data() + ((by * 4 + y) * kW + bx * 4 + x) * 4, 4)) differs = true;
          bad += differs;
        }
      std::printf("BC7 %s %5d of %d blocks differ\n", mode < 8 ? ("mode " + std::to_string(mode)).c_str() : "mixed ", bad,
                  (kW / 4) * (kH / 4));
      failures += bad ? 1 : 0;
    }
  } else {
    std::printf("BC7: the driver has no GL_EXT_texture_compression_bptc, not compared\n");
  }
  std::printf(failures ? "DIFFERENCES FOUND\n" : "decoder and GL agree\n");
  return failures ? 1 : 0;
}
