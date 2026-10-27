// The GX renderer of the core mode (gx_gl.cpp, gx_gl_shader.cpp, gx_texture.cpp)
// on a headless GLES3 context: no disc data, runs in the cloud.
//
//   - the GX texture decoder against the app's checked TPL reader for every
//     format (I4 I8 IA4 IA8 RGB565 RGB5A3 RGBA8 C4 C8 C14X2 CMPR, sizes that
//     are not whole tiles)
//   - chunks as the core's decoder writes them (re4_gx_frame.h), with the
//     register encodings of the SDK: clear by display copy, vertex colours,
//     orientation (GameCube row 0 = EFB row 0), culling, a texture through
//     the TEV with nearest sampling, konstant colours, alpha test, blending,
//     depth test, a directional light, texture coordinates generated from the
//     position, an EFB copy used as a texture, a paletted texture, linear fog
//   - shader generation: random TEV setups (inputs, compare modes, scales,
//     swap tables, konst selections, indirect stages) all compile
#include "gx_auto_level.h"
#include "gx_chunk_builder.h"
#include "gx_gl.h"
#include "gx_gl_shader.h"
#include "gx_selfcheck.h"
#include "gx_texture.h"
#include "re4_gx_frame.h"
#include "texture.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <dirent.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <array>
#include <thread>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    gFailures++;
  }
}

void Require(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FATAL: %s\n", what);
    std::exit(1);
  }
}

EGLDisplay gDisplay = EGL_NO_DISPLAY;
EGLConfig gConfig{};
EGLint gConfigs = 0;

// A GLES3 context of its own (each thread that draws needs one).
EGLContext NewContext() {
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  return eglCreateContext(gDisplay, gConfigs ? gConfig : nullptr, EGL_NO_CONTEXT, contextAttributes);
}

void Context() {
  auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  gDisplay = getDisplay ? getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                        : eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major{}, minor{};
  Require(eglInitialize(gDisplay, &major, &minor), "EGL initialise");
  const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
  eglChooseConfig(gDisplay, attributes, &gConfig, 1, &gConfigs);
  EGLContext context = NewContext();
  Require(context != EGL_NO_CONTEXT, "EGL context");
  Require(eglMakeCurrent(gDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, context), "EGL current");
}

// --- texture decoder against the TPL reader ---------------------------------------

void Be16(std::vector<std::uint8_t> &b, std::size_t at, unsigned v) {
  b[at] = static_cast<std::uint8_t>(v >> 8);
  b[at + 1] = static_cast<std::uint8_t>(v);
}
void Be32(std::vector<std::uint8_t> &b, std::size_t at, std::uint32_t v) {
  Be16(b, at, v >> 16);
  Be16(b, at + 2, v & 0xFFFF);
}

void TestDecoder() {
  std::mt19937 rng(7);
  const unsigned formats[] = {0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 14};
  for (unsigned format : formats) {
    const unsigned w = 13, h = 9;
    const std::size_t bytes = re4gx::GxTextureLevelBytes(format, w, h);
    const bool paletted = re4gx::GxIsPaletted(format);
    const unsigned entries = paletted ? (format == 10 ? 64 : re4gx::GxPaletteEntries(format)) : 0;
    // TPL: header (12), descriptor (8), texture header (36), palette header (12), palette, texels
    const std::size_t texHeader = 0x20, palHeader = 0x60, palData = 0x80;
    const std::size_t texData = (palData + entries * 2 + 31) / 32 * 32;
    std::vector<std::uint8_t> tpl(texData + bytes);
    Be32(tpl, 0, 0x0020AF30);
    Be32(tpl, 4, 1);
    Be32(tpl, 8, 12);
    Be32(tpl, 12, static_cast<std::uint32_t>(texHeader));
    Be32(tpl, 16, paletted ? static_cast<std::uint32_t>(palHeader) : 0);
    Be16(tpl, texHeader, h);
    Be16(tpl, texHeader + 2, w);
    Be32(tpl, texHeader + 4, format);
    Be32(tpl, texHeader + 8, static_cast<std::uint32_t>(texData));
    if (paletted) {
      Be16(tpl, palHeader, entries);
      Be32(tpl, palHeader + 4, 2); // RGB5A3
      Be32(tpl, palHeader + 8, static_cast<std::uint32_t>(palData));
      for (unsigned i = 0; i < entries; i++)
        Be16(tpl, palData + i * 2, rng() & 0xFFFF);
    }
    for (std::size_t i = 0; i < bytes; i++)
      tpl[texData + i] = static_cast<std::uint8_t>(rng());
    if (format == 10) // indices inside the 64-entry palette
      for (std::size_t i = 0; i < bytes; i += 2) {
        tpl[texData + i] &= 0xC0;
        tpl[texData + i + 1] &= 63;
      }
    re4::Texture ref = re4::DecodeTpl(tpl, 0, 0);
    std::vector<std::uint8_t> mine;
    const bool ok = re4gx::GxDecodeTexture(tpl.data() + texData, bytes, format, w, h,
                                           paletted ? tpl.data() + palData : nullptr, entries, 2, mine);
    char what[96];
    std::snprintf(what, sizeof(what), "decoder format %u equals the TPL reader", format);
    Check(ok && mine == ref.rgba, what);
  }
}

// --- chunks ------------------------------------------------------------------------

using namespace re4gx::chunk;

GLuint gReadFbo = 0;

// RGBA at EFB pixel (x, y) (GameCube coordinates, scale 1).
std::vector<std::uint8_t> EfbPixel(re4gx::GxRenderer &r, int x, int y) {
  glBindFramebuffer(GL_FRAMEBUFFER, gReadFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.EfbColor(), 0);
  std::vector<std::uint8_t> p(4);
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}

std::vector<std::uint8_t> TexPixel(GLuint tex, int x, int y) {
  glBindFramebuffer(GL_FRAMEBUFFER, gReadFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  std::vector<std::uint8_t> p(4);
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}

bool Near(const std::vector<std::uint8_t> &p, int r, int g, int b, int tolerance = 2) {
  return std::abs(p[0] - r) <= tolerance && std::abs(p[1] - g) <= tolerance && std::abs(p[2] - b) <= tolerance;
}

void Print(const char *what, const std::vector<std::uint8_t> &p) {
  std::fprintf(stderr, "  %s: %d %d %d %d\n", what, p[0], p[1], p[2], p[3]);
}

const std::uint8_t kRed[4] = {255, 0, 0, 255};
const std::uint8_t kGreen[4] = {0, 255, 0, 255};
const std::uint8_t kBlue[4] = {0, 0, 255, 255};
const std::uint8_t kBlack[4] = {0, 0, 0, 255};
const std::uint8_t kWhite[4] = {255, 255, 255, 255};

void TestBasics(re4gx::GxRenderer &r) {
  // frame 1: clear to blue by a display copy, then a green quad top left, a
  // red triangle bottom right, display copy
  {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    Quad(b, 10, 20, 110, 70, 0, kGreen);
    std::vector<std::uint8_t> v;
    V(v, 500, 400, 0, kRed);
    V(v, 600, 400, 0, kRed);
    V(v, 500, 500, 0, kRed);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 3);
    DisplayCopy(b, false, kBlue);
    r.Run(b.Chunk());
  }
  auto inside = EfbPixel(r, 50, 40), outside = EfbPixel(r, 300, 300), tri = EfbPixel(r, 520, 420);
  Check(Near(inside, 0, 255, 0), "vertex colour: the green quad at GameCube rows 20..70 (EFB rows 20..70)");
  Check(Near(outside, 0, 0, 255), "clear by display copy: blue elsewhere");
  Check(Near(tri, 255, 0, 0), "a triangle with clockwise screen winding is drawn (no culling)");
  if (!Near(inside, 0, 255, 0) || !Near(outside, 0, 0, 255))
    Print("inside", inside), Print("outside", outside);
  unsigned w = 0, h = 0;
  GLuint xfb = r.Xfb(&w, &h);
  Check(xfb != 0 && w == 640 && h == 528, "display copy: an XFB of the copy size");
  Check(xfb && Near(TexPixel(xfb, 50, 40), 0, 255, 0), "display copy: the XFB holds the EFB (row 0 = top)");
  Check(r.Stats().draws == 2 && r.Stats().displayCopies == 2, "two draws, two display copies");

  // a held picture (RE4_GX_CHUNK_HOLD: the VI stays on the XFB before, as
  // with SYS_SCREEN_STOP): drawn and cleared, the XFB keeps the last one
  {
    Builder b;
    Baseline(b);
    b.flags |= RE4_GX_CHUNK_XFB | RE4_GX_CHUNK_HOLD;
    Quad(b, 10, 20, 110, 70, 0, kRed);
    DisplayCopy(b, true, kBlack);
    r.Run(b.Chunk());
  }
  Check(Near(TexPixel(r.Xfb(nullptr, nullptr), 50, 40), 0, 255, 0) && r.Stats().heldCopies == 1,
        "held picture: the XFB keeps the picture before");
  Check(Near(EfbPixel(r, 50, 40), 0, 0, 0), "held picture: the display copy still clears the EFB");

  // VISetBlack (RE4_GX_CHUNK_BLACK): the chunk runs, the screen shows black
  // until a chunk without the flag
  {
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    auto shown = [&](re4gx::GxRenderer &rr) {
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
      glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      rr.Present(0, 0, 64, 64);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      std::uint8_t px[4] = {0, 0, 0, 0};
      glReadPixels(5, 58, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);  // near the top left: the green quad
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return std::array<int, 3>{px[0], px[1], px[2]};
    };
    for (int black = 1; black >= 0; black--) {
      Builder b;
      Baseline(b);
      b.flags |= RE4_GX_CHUNK_XFB | (black ? RE4_GX_CHUNK_BLACK : 0);
      Quad(b, 0, 0, 640, 448, 0, kGreen);
      DisplayCopy(b, true, kBlack);
      r.Run(b.Chunk());
      auto px = shown(r);
      if (black) {
        Check(px[0] == 0 && px[1] == 0 && px[2] == 0, "VISetBlack: the screen shows black");
        Check(Near(TexPixel(r.Xfb(nullptr, nullptr), 50, 40), 0, 255, 0), "VISetBlack: the XFB holds the picture");
      } else {
        Check(px[1] > 200 && px[0] < 40, "black lifted: the picture shows again");
      }
      if ((black && (px[0] || px[1] || px[2])) || (!black && px[1] <= 200))
        std::fprintf(stderr, "  shown: %d %d %d\n", px[0], px[1], px[2]);
    }
    // every present counts (the GPU measurement, RE4_GPU_BENCH); with the timer extension its
    // GPU time arrives some presents later
    {
      const unsigned before = r.Stats().presents;
      for (int i = 0; i < 3; i++) {
        shown(r);
        glFinish();
      }
      Check(r.Stats().presents == before + 3, "every present counted");
      const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
      if (ext && std::strstr(ext, "GL_EXT_disjoint_timer_query")) {
        Check(r.Stats().presentTimed > 0, "the presents' GPU time measured");
        Check(r.Stats().gpuTimed > 0, "the chunks' GPU time measured");
        std::fprintf(stderr, "  present: %u timed, %.3f ms each\n", r.Stats().presentTimed,
                     r.Stats().presentTimed ? r.Stats().presentMicros / 1000.0 / r.Stats().presentTimed : 0.0);
      } else {
        std::fprintf(stderr, "  present: no timer extension here, GPU time not measured\n");
      }
      // every chunk is checked against the GPU (fences); after glFinish it has caught up
      unsigned counted = 0;
      for (unsigned n : r.Stats().behindCount)
        counted += n;
      Check(r.Stats().behindChecks == r.Stats().chunks && counted == r.Stats().behindChecks,
            "every chunk checked against the GPU");
      Builder b;
      Baseline(b);
      Quad(b, 0, 0, 640, 448, 0, kGreen);
      const unsigned long long sum = r.Stats().behindSum;
      const unsigned zero = r.Stats().behindCount[0];
      r.Run(b.Chunk());
      Check(r.Stats().behindSum == sum && r.Stats().behindCount[0] == zero + 1,
            "after glFinish the GPU is behind by no picture");
    }
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
  }

  // culling: hardware mode 1 culls back faces; GX front faces are clockwise
  // on screen (y down)
  {
    Builder b;
    Baseline(b);
    b.Bp(0x00, (1u << 4) | 1u << 14);
    DisplayCopy(b, true, kBlack);
    std::vector<std::uint8_t> cw, ccw;
    V(cw, 100, 100, 0, kRed); // clockwise on screen: right, then down
    V(cw, 200, 100, 0, kRed);
    V(cw, 100, 200, 0, kRed);
    V(ccw, 400, 100, 0, kGreen); // counter-clockwise
    V(ccw, 400, 200, 0, kGreen);
    V(ccw, 500, 100, 0, kGreen);
    b.Draw(RE4_GX_V_COLOR0, 16, cw, 3);
    b.Draw(RE4_GX_V_COLOR0, 16, ccw, 3);
    r.Run(b.Chunk());
  }
  Check(Near(EfbPixel(r, 120, 120), 255, 0, 0), "culling back faces keeps a clockwise (front) triangle");
  Check(Near(EfbPixel(r, 420, 120), 0, 0, 0), "culling back faces drops a counter-clockwise triangle");

  // depth: the nearer quad wins in either order (LEQUAL, depth 0 near)
  {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    Quad(b, 0, 0, 100, 100, -0.25f, kRed);  // nearer (depth 0.25)
    Quad(b, 0, 0, 100, 100, -0.75f, kGreen); // farther, drawn later
    Quad(b, 200, 0, 300, 100, -0.75f, kGreen);
    Quad(b, 200, 0, 300, 100, -0.25f, kRed);
    r.Run(b.Chunk());
  }
  Check(Near(EfbPixel(r, 50, 50), 255, 0, 0) && Near(EfbPixel(r, 250, 50), 255, 0, 0),
        "depth test: the nearer quad wins in either order");

  // blending: src alpha over dst; alpha test discards below the reference
  {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    Quad(b, 0, 0, 100, 100, 0, kBlue);
    const std::uint8_t half[4] = {255, 0, 0, 128};
    b.Bp(0x41, 1 | 1 << 3 | 1 << 4 | 5 << 5 | 4 << 8); // SRCALPHA, INVSRCALPHA
    Quad(b, 0, 0, 100, 100, 0, half);
    b.Bp(0x41, 1 << 3 | 1 << 4);
    b.Bp(0xF3, 100 | 6u << 16 | 0u << 19 | 0u << 22); // alpha >= 100 AND never -> discard all
    const std::uint8_t low[4] = {0, 255, 0, 50};
    Quad(b, 200, 0, 300, 100, 0, low);
    b.Bp(0xF3, 40 | 6u << 16 | 7u << 19); // alpha >= 40 AND always
    Quad(b, 400, 0, 500, 100, 0, low);
    r.Run(b.Chunk());
  }
  auto blended = EfbPixel(r, 50, 50);
  Check(Near(blended, 128, 0, 127, 3), "blending: half red over blue");
  if (!Near(blended, 128, 0, 127, 3))
    Print("blended", blended);
  Check(Near(EfbPixel(r, 250, 50), 0, 0, 0), "alpha test: a failing compare discards");
  Check(Near(EfbPixel(r, 450, 50), 0, 255, 0), "alpha test: a passing compare draws");
}

void TestTexture(re4gx::GxRenderer &r) {
  // a 4x4 RGBA8 texture: texel (x, y) = (x * 60, y * 60, 200), sampled nearest
  // over a 64x64 quad at (100, 100); the TEV takes the texture colour
  std::vector<std::uint8_t> tex(64);
  for (int i = 0; i < 16; i++) {
    const int x = i % 4, y = i / 4;
    tex[i * 2] = 255;                                   // A
    tex[i * 2 + 1] = static_cast<std::uint8_t>(x * 60); // R
    tex[32 + i * 2] = static_cast<std::uint8_t>(y * 60); // G
    tex[32 + i * 2 + 1] = 200;                           // B
  }
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  b.Bp(0x00, 1u | 1u << 4); // one texture coordinate
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7}); // texgen 0: regular, from TEX0, ST
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19); // colour = texture
  b.Bp(0x80, 0);                                           // clamp, nearest
  b.Bp(0x84, 0);
  b.Bp(0x88, 3 | 3 << 10 | 6u << 20);
  b.Bp(0x30, 3);
  b.Bp(0x31, 3);
  b.Texture(0, 0x1234, 0, 0x100000, 4, 4, 6, tex);
  std::vector<std::uint8_t> v;
  VT(v, 100, 100, 0, kWhite, 0, 0);
  VT(v, 164, 100, 0, kWhite, 1, 0);
  VT(v, 164, 164, 0, kWhite, 1, 1);
  VT(v, 100, 164, 0, kWhite, 0, 1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
  // the same texture again by key, without data, over another quad
  b.Texture(0, 0x1234, 0, 0x100000, 4, 4, 6, {});
  std::vector<std::uint8_t> v2;
  VT(v2, 300, 100, 0, kWhite, 0, 0);
  VT(v2, 364, 100, 0, kWhite, 1, 0);
  VT(v2, 364, 164, 0, kWhite, 1, 1);
  VT(v2, 300, 164, 0, kWhite, 0, 1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v2, 4, 0x80);
  r.Run(b.Chunk());
  auto t00 = EfbPixel(r, 104, 104), t30 = EfbPixel(r, 160, 104), t03 = EfbPixel(r, 104, 160);
  Check(Near(t00, 0, 0, 200) && Near(t30, 180, 0, 200) && Near(t03, 0, 180, 200),
        "texture: texel (0,0) top left, x to the right, y down");
  if (!Near(t00, 0, 0, 200) || !Near(t30, 180, 0, 200) || !Near(t03, 0, 180, 200))
    Print("t00", t00), Print("t30", t30), Print("t03", t03);
  Check(Near(EfbPixel(r, 360, 160), 180, 180, 200), "texture: bound again by key without data");
  Check(r.Stats().missingTextures == 0, "no missing textures");
}

void TestTev(re4gx::GxRenderer &r) {
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  // konstant colour K1 = (10, 20, 30, 40), stage 0 colour = KONST (kcsel 0x0D)
  b.Bp(0xE2, 10 | 40u << 12 | 1u << 23);  // K1 RA
  b.Bp(0xE3, 30 | 20u << 12 | 1u << 23);  // K1 BG
  b.Bp(0xF6, (0 | 1 << 2) | 0x0Du << 4);  // stage 0 kcsel K1
  b.Bp(0xC0, 14u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  Quad(b, 0, 0, 50, 50, 0, kWhite);
  // lerp: a = red vertex colour, b = C0 (0, 0, 200), c = HALF (128): ~half way
  b.Bp(0xE2, 0 | 255u << 12);       // C0 RA (register, not konst)
  b.Bp(0xE3, 200 | 0u << 12);       // C0 BG
  b.Bp(0xC0, 15u | 13u << 4 | 2u << 8 | 10u << 12 | 1u << 19);
  Quad(b, 100, 0, 150, 50, 0, kRed);
  // compare mode RGB8_GT: d = 0 + (a > b ? c : 0) per component; a = ras (255,0,0), b = HALF, c = ONE
  b.Bp(0xC0, 15u | 12u << 4 | 13u << 8 | 10u << 12 | 3u << 16 | 0u << 18 | 3u << 20 | 1u << 19);
  Quad(b, 200, 0, 250, 50, 0, kRed);
  // scale x2 and subtract: d (ras = 100,100,100) - a(0) ... (d - lerp(0, 0, x)) * 2 = 200
  const std::uint8_t grey[4] = {100, 100, 100, 255};
  b.Bp(0xC0, 10u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 18 | 1u << 20 | 1u << 19);
  Quad(b, 300, 0, 350, 50, 0, grey);
  r.Run(b.Chunk());
  auto konst = EfbPixel(r, 25, 25), lerp = EfbPixel(r, 125, 25), cmp = EfbPixel(r, 225, 25), scaled = EfbPixel(r, 325, 25);
  Check(Near(konst, 10, 20, 30, 0), "TEV: konstant colour K1");
  Check(Near(lerp, 128, 0, 100, 1), "TEV: lerp(a, b, half) with the hardware rounding");
  Check(Near(cmp, 255, 0, 0, 0), "TEV: compare RGB8_GT");
  Check(Near(scaled, 200, 200, 200, 0), "TEV: subtract with scale 2");
  if (!Near(lerp, 128, 0, 100, 1))
    Print("lerp", lerp);
  if (!Near(cmp, 255, 0, 0, 0))
    Print("compare", cmp);
}

void TestLighting(re4gx::GxRenderer &r) {
  // one light at +z far away (directional), white; material from the vertex
  // (white), ambient register dark grey; normals towards / away from the light
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  b.Xf(0x100A, {0x202020FFu});                                   // ambient 0
  b.Xf(0x100E, {1u | 1u << 1 | 1u << 2 | 2u << 7 | 2u << 9});   // vertex material, lighting, light 0, clamp, dir
  std::vector<std::uint32_t> light(16, 0);
  light[3] = 0xFFFFFFFFu;
  light[4] = Bits(1.0f);
  light[7] = Bits(1.0f);
  light[12] = Bits(100000.0f); // position far along +z
  b.Xf(0x600, light);
  auto quad = [&](float x0, float nz) {
    std::vector<std::uint8_t> v;
    for (int i = 0; i < 4; i++) {
      const float p[3] = {i == 1 || i == 2 ? x0 + 50 : x0, i >= 2 ? 50.0f : 0.0f, 0.0f};
      const float n[3] = {0, 0, nz};
      v.insert(v.end(), reinterpret_cast<const std::uint8_t *>(p), reinterpret_cast<const std::uint8_t *>(p) + 12);
      v.insert(v.end(), reinterpret_cast<const std::uint8_t *>(n), reinterpret_cast<const std::uint8_t *>(n) + 12);
      v.insert(v.end(), kWhite, kWhite + 4);
    }
    b.Draw(RE4_GX_V_NORMAL | RE4_GX_V_COLOR0, 28, v, 4, 0x80);
  };
  quad(0, 1.0f);
  quad(100, -1.0f);
  r.Run(b.Chunk());
  Check(Near(EfbPixel(r, 25, 25), 255, 255, 255), "lighting: a normal towards the light is lit fully");
  Check(Near(EfbPixel(r, 125, 25), 32, 32, 32), "lighting: a normal away from the light gets the ambient only");
}

// viaXfb: read the result from a display copy (a multisampled EFB has the
// picture in its samples until a copy resolves it)
void TestCopyAndPalette(re4gx::GxRenderer &r, bool viaXfb = false) {
  // draw red into the EFB, copy 32x32 at (0, 0) to a texture at 0x200000
  // (RGBA8), then draw a quad with that copy as texture map 0
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  Quad(b, 0, 0, 32, 32, 0, kRed);
  b.Bp(0x49, 0);
  b.Bp(0x4A, 31 | 31u << 10);
  b.Bp(0x4D, 32);
  b.Bp(0x4B, 0x200000 >> 5);
  b.Bp(0x52, 6u << 4 | 2u << 15); // RGBA8 to a texture
  b.Bp(0x4A, 639 | 527u << 10);
  b.Bp(0x00, 1u | 1u << 4);
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7});
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  b.Bp(0x88, 31 | 31 << 10 | 6u << 20);
  b.Bp(0x30, 31);
  b.Bp(0x31, 31);
  b.Texture(0, 0x200000ull | 1ull << 32, RE4_GX_TEX_COPY, 0x200000, 32, 32, 6, {});
  std::vector<std::uint8_t> v;
  VT(v, 300, 300, 0, kWhite, 0, 0);
  VT(v, 340, 300, 0, kWhite, 1, 0);
  VT(v, 340, 340, 0, kWhite, 1, 1);
  VT(v, 300, 340, 0, kWhite, 0, 1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
  // a C8 texture with an RGB565 palette: index i -> palette entry i
  std::vector<std::uint8_t> palette(512);
  for (int i = 0; i < 256; i++) {
    palette[i * 2] = i == 5 ? 0x07 : 0xF8; // entry 5: green (0x07E0), the rest red (0xF800)
    palette[i * 2 + 1] = i == 5 ? 0xE0 : 0x00;
  }
  const std::uint32_t tlutAt = 0x200 << 9;
  {
    const std::uint32_t at = b.Blob(palette.data(), palette.size());
    const std::uint32_t p[3] = {tlutAt, 512, at};
    b.Op(RE4_GX_OP_TLUT, p, 3);
  }
  std::vector<std::uint8_t> c8(32, 5); // 8x4 texels, all index 5
  b.Bp(0x88, 7 | 3 << 10 | 9u << 20);
  b.Bp(0x98, 0x200 | 1u << 10);
  b.Bp(0x30, 7);
  b.Bp(0x31, 3);
  b.Texture(0, 0x5555, 0, 0x300000, 8, 4, 9, c8, 0x200 | 1u << 10);
  std::vector<std::uint8_t> v2;
  VT(v2, 400, 300, 0, kWhite, 0, 0);
  VT(v2, 440, 300, 0, kWhite, 1, 0);
  VT(v2, 440, 340, 0, kWhite, 1, 1);
  VT(v2, 400, 340, 0, kWhite, 0, 1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v2, 4, 0x80);
  if (viaXfb) {
    b.Bp(0x49, 0);
    b.Bp(0x4A, 639 | 527u << 10);
    DisplayCopy(b, false, kBlack);
  }
  r.Run(b.Chunk());
  auto pixel = [&](int x, int y) { return viaXfb ? TexPixel(r.Xfb(nullptr, nullptr), x, y) : EfbPixel(r, x, y); };
  Check(r.Stats().textureCopies == 1, "one EFB copy to a texture");
  Check(Near(pixel(320, 320), 255, 0, 0), "EFB copy used as a texture");
  Check(Near(pixel(420, 320), 0, 255, 0), "paletted texture: C8 through an RGB565 palette");
}

// The game draws only the letterbox rows (56..391) and copies rows 56..447 at half size for water and
// heat haze; the rows below the picture must repeat the last drawn row, not read black (dark waves
// at the bottom edge). The copy is read back through a texture quad (bottom of the quad = last copy row).
void TestLetterboxCopy(re4gx::GxRenderer &r, bool viaXfb, bool letterbox) {
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  if (letterbox) {
    b.Bp(0x20, (342u << 12) | (342 + 56));
    b.Bp(0x21, ((342u + 639) << 12) | (342 + 391));
  }
  Quad(b, 0, letterbox ? 56 : 0, 640, letterbox ? 392 : 528, 0, kRed);
  b.Bp(0x20, 342u << 12 | 342);
  b.Bp(0x21, ((342u + 639) << 12) | (342 + 527));
  const unsigned before = r.Stats().limitedCopies;
  b.Bp(0x49, 0 | 56u << 10);
  b.Bp(0x4A, 511 | 391u << 10);
  b.Bp(0x4D, 32);
  b.Bp(0x4B, 0x400000 >> 5);
  b.Bp(0x52, 6u << 4 | 2u << 15 | 1u << 9); // RGBA8 to a texture, half size: 256 x 196
  b.Bp(0x4A, 639 | 527u << 10);
  b.Bp(0x00, 1u | 1u << 4);
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7});
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  b.Bp(0x88, 255 | 195 << 10 | 6u << 20);
  b.Bp(0x30, 255);
  b.Bp(0x31, 195);
  b.Texture(0, 0x400000ull | 1ull << 32, RE4_GX_TEX_COPY, 0x400000, 256, 196, 6, {});
  std::vector<std::uint8_t> v;
  VT(v, 0, 300, 0, kWhite, 0, 0);
  VT(v, 256, 300, 0, kWhite, 1, 0);
  VT(v, 256, 496, 0, kWhite, 1, 1);
  VT(v, 0, 496, 0, kWhite, 0, 1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
  if (viaXfb) {
    b.Bp(0x49, 0);
    b.Bp(0x4A, 639 | 527u << 10);
    DisplayCopy(b, false, kBlack);
  }
  r.Run(b.Chunk());
  auto pixel = [&](int x, int y) { return viaXfb ? TexPixel(r.Xfb(nullptr, nullptr), x, y) : EfbPixel(r, x, y); };
  const char *how = letterbox ? "letterbox copy" : "full picture copy";
  // copy row 20 (quad row 320): drawn in both; copy row 190 (quad row 490): below the letterbox picture
  Check(Near(pixel(128, 320), 255, 0, 0), (std::string(how) + ": the drawn rows are copied").c_str());
  Check(Near(pixel(128, 490), 255, 0, 0),
        (std::string(how) + ": rows below the drawn picture repeat the last drawn row, not black").c_str());
  Check((r.Stats().limitedCopies - before == 1) == letterbox,
        (std::string(how) + ": the row limit is used only for a letterbox picture").c_str());
}

// The shimmer of the logos at the start (IdShimmerTrans): the whole picture (rows 0..447) copied at
// half size and read whole, while a movie fills only rows 56..391 under a full scissor (no letterbox
// scissor: the rule above does not see the bars). In the 16:9 formats the app shows rows 56..391
// (SetShownRows): the copy repeats the shown edge rows, so a distortion near the edge reads picture,
// not the black bars (black waves at the bottom of the intro, 0.91.0 test of 26.10.2026). All rows
// shown (4:3): as on the GameCube, the bars stay black in the copy.
void TestShownRowsCopy(re4gx::GxRenderer &r, bool viaXfb, bool crop) {
  r.SetShownRows(crop ? 56 : 0, crop ? 392 : 1 << 16);
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  Quad(b, 0, 56, 640, 392, 0, kRed);
  const unsigned before = r.Stats().limitedCopies;
  b.Bp(0x49, 0);
  b.Bp(0x4A, 511 | 447u << 10);
  b.Bp(0x4D, 32);
  b.Bp(0x4B, 0x480000 >> 5);
  b.Bp(0x52, 6u << 4 | 2u << 15 | 1u << 9); // RGBA8 to a texture, half size: 256 x 224
  b.Bp(0x4A, 639 | 527u << 10);
  b.Bp(0x00, 1u | 1u << 4);
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7});
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  b.Bp(0x88, 255 | 223 << 10 | 6u << 20);
  b.Bp(0x30, 255);
  b.Bp(0x31, 223);
  b.Texture(0, 0x480000ull | 2ull << 32, RE4_GX_TEX_COPY, 0x480000, 256, 224, 6, {});
  std::vector<std::uint8_t> v;
  VT(v, 0, 300, 0, kWhite, 0, 0);
  VT(v, 256, 300, 0, kWhite, 1, 0);
  VT(v, 256, 524, 0, kWhite, 1, 1);
  VT(v, 0, 524, 0, kWhite, 0, 1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
  if (viaXfb) {
    b.Bp(0x49, 0);
    b.Bp(0x4A, 639 | 527u << 10);
    DisplayCopy(b, false, kBlack);
  }
  r.Run(b.Chunk());
  auto EfbPixel = [&](re4gx::GxRenderer &rr, int x, int y) {
    return viaXfb ? TexPixel(rr.Xfb(nullptr, nullptr), x, y) : ::EfbPixel(rr, x, y);
  };
  const char *how = crop ? "16:9 (rows 56..391 shown)" : "4:3 (all rows shown)";
  // quad row 400: copy row 100 (EFB 200, drawn); 310: copy row 10 (EFB 20, above the movie);
  // 500: copy row 200 (EFB 400, below it)
  Check(Near(EfbPixel(r, 128, 400), 255, 0, 0), (std::string("whole copy, ") + how + ": the drawn rows are copied").c_str());
  const bool topRed = Near(EfbPixel(r, 128, 310), 255, 0, 0), bottomRed = Near(EfbPixel(r, 128, 500), 255, 0, 0);
  Check(topRed == crop, (std::string("whole copy, ") + how + (crop ? ": rows above the shown part repeat its top row" : ": rows above the movie stay black")).c_str());
  Check(bottomRed == crop, (std::string("whole copy, ") + how + (crop ? ": rows below the shown part repeat its last row" : ": rows below the movie stay black")).c_str());
  Check((r.Stats().limitedCopies - before == 1) == crop, (std::string("whole copy, ") + how + ": the row limit only while cropped").c_str());
  r.SetShownRows(0, 1 << 16);
}

// New picture options make a new renderer (core_mode.cpp): the EFB copies of the renderer before go
// over to it at its scale (HandOverCopies). A copy the game made once (a room's start) is read on
// without a new copy: before 26.10.2026 it was missing (white) until the next room.
void TestHandOverCopies() {
  std::string error;
  re4gx::GxRenderer a, b;
  Require(a.Init(1, &error), error.c_str());
  {
    Builder c;
    Baseline(c);
    DisplayCopy(c, true, kBlack);
    Quad(c, 0, 0, 640, 528, 0, kRed);
    c.Bp(0x49, 0);
    c.Bp(0x4A, 255 | 255u << 10);
    c.Bp(0x4D, 16);
    c.Bp(0x4B, 0x500000 >> 5);
    c.Bp(0x52, 6u << 4 | 1u << 9); // RGBA8 to a texture, 256 x 256
    DisplayCopy(c, false, kBlack);
    a.Run(c.Chunk());
  }
  Require(b.Init(2, &error), error.c_str());
  a.HandOverCopies(b);
  a.Release();
  Check(b.Stats().handedCopies == 1, "hand over: the copy goes to the new renderer");
  Builder d;
  Baseline(d);
  DisplayCopy(d, true, kBlack);
  d.Bp(0x00, 1u | 1u << 4);
  d.Xf(0x103F, {1});
  d.Xf(0x1040, {5u << 7});
  d.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  d.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  d.Bp(0x88, 255 | 255 << 10 | 6u << 20);
  d.Bp(0x30, 255);
  d.Bp(0x31, 255);
  d.Texture(0, 0x500000ull | 3ull << 32, RE4_GX_TEX_COPY, 0x500000, 256, 256, 6, {});
  std::vector<std::uint8_t> v;
  VT(v, 0, 0, 0, kWhite, 0, 0);
  VT(v, 256, 0, 0, kWhite, 1, 0);
  VT(v, 256, 256, 0, kWhite, 1, 1);
  VT(v, 0, 256, 0, kWhite, 0, 1);
  d.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
  d.Bp(0x49, 0);
  d.Bp(0x4A, 639 | 527u << 10);
  DisplayCopy(d, false, kBlack);
  b.Run(d.Chunk());
  Check(b.Stats().missingTextures == 0, "hand over: the copy is not missing in the new renderer");
  Check(Near(TexPixel(b.Xfb(nullptr, nullptr), 100 * 2, 100 * 2), 255, 0, 0),
        "hand over: the copy drawn at the new scale (red)");
  Check(glGetError() == GL_NO_ERROR, "hand over: no GL error");
  b.Release();
}

// The game's GXPeekZ (a torch's glow hides itself behind a wall by reading the depth around it): the
// core marks the chunk with BP 0x5B; at its display copy the depth goes, half the resolution, to the
// sink once the GPU is through (the next chunk). A quad at depth 0.5 on the left, cleared (far) elsewhere.
void TestDepthMap(re4gx::GxRenderer &r, bool viaDeferred) {
  (void) viaDeferred;
  std::vector<std::uint32_t> got;
  std::uint32_t serial = 0;
  r.SetDepthSink([&](const std::uint32_t *cells, std::uint32_t s) {
    got.assign(cells, cells + re4gx::GxRenderer::kDepthWidth * re4gx::GxRenderer::kDepthHeight);
    serial = s;
  });
  const unsigned before = r.Stats().depthSnapshots;
  {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    Quad(b, 0, 0, 320, 528, -0.5f, kRed);
    b.Bp(0x5B, 4242);  // the marker with the number of the picture
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
  }
  Check(r.Stats().depthSnapshots == before + 1, "depth map: the marked picture is taken");
  for (int i = 0; i < 5 && got.empty(); i++) { // the next chunks hand it over once the GPU is through
    Builder b;
    Baseline(b);
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
  }
  Check(!got.empty() && serial == 4242, "depth map: delivered to the sink with the number of the picture that peeked");
  if (!got.empty()) {
    const std::uint32_t half = 8388607, far = 16777215;
    const std::uint32_t left = got[60 * re4gx::GxRenderer::kDepthWidth + 50];
    const std::uint32_t right = got[60 * re4gx::GxRenderer::kDepthWidth + 250];
    Check(left + 64 >= half && left <= half + 64, "depth map: the quad's depth (about 0.5) at the cell");
    Check(right == far, "depth map: cleared depth reads far (0xFFFFFF)");
  }
  // an unmarked picture takes none
  const unsigned snapshots = r.Stats().depthSnapshots;
  {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    Quad(b, 0, 0, 320, 528, -0.5f, kRed);
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
  }
  Check(r.Stats().depthSnapshots == snapshots, "depth map: a picture without the marker takes none");
  r.SetDepthSink(nullptr);
}

// FXAA (the app menu's switch): the display copy goes through one more pass. A red triangle on blue has
// a hard slanted edge; with FXAA the shown picture has in-between colours along it, flat areas and the
// EFB stay as they are, and switched off the picture is the plain copy again.
void TestFxaa(re4gx::GxRenderer &r) {
  auto scene = [&]() {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    std::vector<std::uint8_t> v;
    V(v, 200, 100, 0, kRed);
    V(v, 300, 160, 0, kRed);
    V(v, 200, 160, 0, kRed);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 3);
    DisplayCopy(b, false, kBlue);
    r.Run(b.Chunk());
  };
  auto between = [&](GLuint xfb) {
    int n = 0;
    const int f = r.Scale();
    for (int x = 200; x < 300; x++) {
      const int y = 100 + (x - 200) * 60 / 100; // on the edge
      for (int dy = -1; dy <= 1; dy++) {
        auto px = TexPixel(xfb, x * f, (y + dy) * f);
        n += px[0] > 20 && px[0] < 235 && px[2] > 20 && px[2] < 235;
      }
    }
    return n;
  };
  scene();
  GLuint plain = r.Xfb(nullptr, nullptr);
  const int hard = between(plain);
  const unsigned before = r.Stats().fxaaPasses;
  r.SetFxaa(true);
  scene();
  GLuint smooth = r.Xfb(nullptr, nullptr);
  Check(r.Stats().fxaaPasses == before + 2, "FXAA: one pass per display copy (the scene has two)");
  Check(smooth != plain && smooth != 0, "FXAA: the shown picture is the smoothed copy");
  Check(between(smooth) > hard + 20, "FXAA: the slanted edge gets in-between colours");
  Check(Near(TexPixel(smooth, 100 * r.Scale(), 300 * r.Scale()), 0, 0, 255), "FXAA: a flat area stays as it is");
  Check(Near(TexPixel(smooth, 220 * r.Scale(), 155 * r.Scale()), 255, 0, 0), "FXAA: inside the shape stays red");
  r.SetFxaa(false);
  scene();
  Check(r.Xfb(nullptr, nullptr) == plain, "FXAA: off, the plain display copy is shown again");
}

// Frame skip: a chunk that is not drawn (the renderer is behind and the next chunk is there). The state
// it carries is applied, the EFB cleared as its display copy would, the shown picture stays; the rules
// for when it may be skipped.
void TestFrameSkip(re4gx::GxRenderer &r) {
  // the picture before: red on the EFB, which stays (no clear)
  {
    Builder z;
    Baseline(z);
    Quad(z, 0, 0, 320, 264, 0, kRed);
    DisplayCopy(z, false, kBlack);
    r.Run(z.Chunk());
  }
  GLuint xfb = r.Xfb(nullptr, nullptr);
  Check(Near(TexPixel(xfb, 100, 100), 255, 0, 0), "frame skip: the picture before is red");
  Builder a, b;
  a.flags = RE4_GX_CHUNK_XFB;
  Quad(a, 0, 0, 640, 528, 0, kGreen);
  DisplayCopy(a, true, kBlack);
  b.flags = RE4_GX_CHUNK_XFB;
  Quad(b, 400, 300, 500, 400, 0, kBlue);
  DisplayCopy(b, true, kBlack);
  Check(r.CanSkip(a.Chunk(), b.Chunk()), "frame skip: a plain chunk may be skipped");
  const unsigned skipped = r.Stats().skippedChunks, shown = r.Stats().displayCopies;
  r.Skip(a.Chunk());
  Check(r.Stats().skippedChunks == skipped + 1 && r.Stats().displayCopies == shown, "frame skip: no display copy made");
  Check(Near(TexPixel(r.Xfb(nullptr, nullptr), 100, 100), 255, 0, 0), "frame skip: the picture shown stays");
  r.Run(b.Chunk());
  xfb = r.Xfb(nullptr, nullptr);
  Check(Near(TexPixel(xfb, 450, 350), 0, 0, 255), "frame skip: the next chunk draws as usual");
  Check(Near(TexPixel(xfb, 600, 500), 0, 0, 0), "frame skip: the skipped chunk's draws are left out");
  Check(Near(TexPixel(xfb, 100, 100), 0, 0, 0), "frame skip: the EFB was cleared (the red before is gone)");
  // the rules
  Builder reset;
  Baseline(reset);
  DisplayCopy(reset, true, kBlack);
  Check(!r.CanSkip(reset.Chunk(), b.Chunk()), "frame skip: a chunk that starts over is not skipped");
  Builder held;
  held.flags = RE4_GX_CHUNK_XFB | RE4_GX_CHUNK_HOLD;
  DisplayCopy(held, true, kBlack);
  Check(!r.CanSkip(held.Chunk(), b.Chunk()), "frame skip: a held picture is not skipped");
  Builder noCopy;
  noCopy.flags = 0; // not a chunk that ends with a display copy
  Quad(noCopy, 0, 0, 10, 10, 0, kRed);
  Check(!r.CanSkip(noCopy.Chunk(), b.Chunk()), "frame skip: a chunk without a display copy is not skipped");
  // an EFB copy for a texture in `a`
  auto copyTo = [](Builder &c, unsigned dest) {
    c.Bp(0x49, 0);
    c.Bp(0x4A, 31 | 31u << 10);
    c.Bp(0x4D, 32);
    c.Bp(0x4B, dest >> 5);
    c.Bp(0x52, 6u << 4 | 2u << 15);
    c.Bp(0x4A, 639 | 527u << 10);
  };
  Builder withCopy;
  withCopy.flags = RE4_GX_CHUNK_XFB;
  copyTo(withCopy, 0x300000);
  DisplayCopy(withCopy, true, kBlack);
  Check(!r.CanSkip(withCopy.Chunk(), b.Chunk()), "frame skip: a texture copy the next chunk may read is not skipped");
  Builder overwrites;
  overwrites.flags = RE4_GX_CHUNK_XFB;
  copyTo(overwrites, 0x300000);
  Quad(overwrites, 0, 0, 10, 10, 0, kRed);
  DisplayCopy(overwrites, true, kBlack);
  Check(r.CanSkip(withCopy.Chunk(), overwrites.Chunk()), "frame skip: the next chunk copies there before it draws");
  Builder readsFirst;
  readsFirst.flags = RE4_GX_CHUNK_XFB;
  readsFirst.Texture(0, 0x300000ull | 1ull << 32, RE4_GX_TEX_COPY, 0x300000, 32, 32, 6, {});
  copyTo(readsFirst, 0x300000);
  DisplayCopy(readsFirst, true, kBlack);
  Check(!r.CanSkip(withCopy.Chunk(), readsFirst.Chunk()), "frame skip: the next chunk reads the copy first");
  // as in a room: the next chunk draws its scene (no map holds the copy) and copies afterwards (the
  // shadows, the depth of field): skipped (until 26.10.2026 any draw before the copy refused it)
  Builder drawsThenCopies;
  drawsThenCopies.flags = RE4_GX_CHUNK_XFB;
  Quad(drawsThenCopies, 0, 0, 10, 10, 0, kRed);
  copyTo(drawsThenCopies, 0x300000);
  DisplayCopy(drawsThenCopies, true, kBlack);
  Check(r.CanSkip(withCopy.Chunk(), drawsThenCopies.Chunk()),
        "frame skip: the next chunk draws without the copy, then copies there: skipped");
  // `a` binds its copy to map 0 and draws with it; `b` draws with map 0 sampled (still the copy) first
  Builder usesCopy;
  usesCopy.flags = RE4_GX_CHUNK_XFB;
  copyTo(usesCopy, 0x300000);
  usesCopy.Texture(0, 0x300000ull | 1ull << 32, RE4_GX_TEX_COPY, 0x300000, 32, 32, 6, {});
  DisplayCopy(usesCopy, true, kBlack);
  Builder samplesMap0;
  samplesMap0.flags = RE4_GX_CHUNK_XFB;
  samplesMap0.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);  // stage 0 samples map 0
  Quad(samplesMap0, 0, 0, 10, 10, 0, kRed);
  copyTo(samplesMap0, 0x300000);
  DisplayCopy(samplesMap0, true, kBlack);
  Check(!r.CanSkip(usesCopy.Chunk(), samplesMap0.Chunk()),
        "frame skip: the next chunk samples a map that still holds the copy: not skipped");
  Builder neverCopies;
  neverCopies.flags = RE4_GX_CHUNK_XFB;
  Quad(neverCopies, 0, 0, 10, 10, 0, kRed);
  DisplayCopy(neverCopies, true, kBlack);
  Check(!r.CanSkip(withCopy.Chunk(), neverCopies.Chunk()),
        "frame skip: the next chunk does not copy there again (a later one may read it): not skipped");
}

// Auto in the app menu: the EFB scale changes between two Runs (GxRenderer::Rescale). At scale 2 a
// red EFB copy is made and drawn as a texture; at scale 1 and then 3 a chunk draws with map 0 still
// bound to that copy (no new texture load): the copy came along, scaled, and the display copy has
// the new size.
void TestRescale() {
  re4gx::GxRenderer r;
  std::string error;
  Require(r.Init(2, &error), error.c_str());
  auto textured = [](Builder &b, bool load) {
    b.Bp(0x00, 1u | 1u << 4);
    b.Xf(0x103F, {1});
    b.Xf(0x1040, {5u << 7});
    b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
    b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
    b.Bp(0x88, 31 | 31 << 10 | 6u << 20);
    b.Bp(0x30, 31);
    b.Bp(0x31, 31);
    if (load)
      b.Texture(0, 0x200000ull | 1ull << 32, RE4_GX_TEX_COPY, 0x200000, 32, 32, 6, {});
    std::vector<std::uint8_t> v;
    VT(v, 300, 300, 0, kWhite, 0, 0);
    VT(v, 340, 300, 0, kWhite, 1, 0);
    VT(v, 340, 340, 0, kWhite, 1, 1);
    VT(v, 300, 340, 0, kWhite, 0, 1);
    b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
    b.Bp(0x49, 0);
    b.Bp(0x4A, 639 | 527u << 10);
    DisplayCopy(b, false, kBlack);
  };
  {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    Quad(b, 0, 0, 32, 32, 0, kRed);
    b.Bp(0x49, 0);
    b.Bp(0x4A, 31 | 31u << 10);
    b.Bp(0x4D, 32);
    b.Bp(0x4B, 0x200000 >> 5);
    b.Bp(0x52, 6u << 4 | 2u << 15); // RGBA8 to a texture
    b.Bp(0x4A, 639 | 527u << 10);
    textured(b, true);
    r.Run(b.Chunk());
  }
  Check(Near(TexPixel(r.Xfb(nullptr, nullptr), 640, 640), 255, 0, 0), "rescale: the copy drawn at scale 2");
  for (int scale : {1, 3}) {
    r.Rescale(scale);
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    textured(b, false);
    r.Run(b.Chunk());
    unsigned w = 0, h = 0;
    const GLuint xfb = r.Xfb(&w, &h);
    const std::string at = "rescale to " + std::to_string(scale);
    Check(r.Scale() == scale, (at + ": the renderer's scale").c_str());
    // (outside a smaller texture the read gives no blue)
    Check(w == 640 && h == 528 && Near(TexPixel(xfb, static_cast<int>(w) * scale - 4, static_cast<int>(h) * scale - 4), 0, 0, 255),
          (at + ": the display copy has the new size").c_str());
    Check(Near(TexPixel(xfb, 320 * scale, 320 * scale), 255, 0, 0), (at + ": the copy still bound, scaled along").c_str());
    Check(Near(TexPixel(xfb, 100 * scale, 400 * scale), 0, 0, 255), (at + ": the clear colour elsewhere").c_str());
  }
  // the menu's in-between levels (1.5x, 2.5x: in halves 3, 5): the EFB 960 x 792 and 1600 x 1320
  for (int halves : {3, 5}) {
    r.RescaleHalves(halves);
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    textured(b, false);
    r.Run(b.Chunk());
    unsigned w = 0, h = 0;
    const GLuint xfb = r.Xfb(&w, &h);
    const std::string at = "rescale to " + std::to_string(halves) + " halves";
    Check(r.Halves() == halves && r.Scale() == halves / 2, (at + ": the renderer's level").c_str());
    Check(w == 640 && h == 528 &&
              Near(TexPixel(xfb, static_cast<int>(w) * halves / 2 - 4, static_cast<int>(h) * halves / 2 - 4), 0, 0, 255),
          (at + ": the display copy has the new size").c_str());
    Check(Near(TexPixel(xfb, 320 * halves / 2, 320 * halves / 2), 255, 0, 0),
          (at + ": the copy still bound, scaled along").c_str());
    Check(Near(TexPixel(xfb, 100 * halves / 2, 400 * halves / 2), 0, 0, 255), (at + ": the clear colour elsewhere").c_str());
  }
  {
    // at 1.5x a copy at an odd place and of an odd size (its texture Hi(w) pixels, the source read exactly)
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    Quad(b, 0, 0, 640, 528, 0, kRed);
    b.Bp(0x49, 33 | 17u << 10);
    b.Bp(0x4A, 30 | 30u << 10);  // 31 x 31 at (33, 17)
    b.Bp(0x4D, 32);
    b.Bp(0x4B, 0x210000 >> 5);
    b.Bp(0x52, 6u << 4 | 2u << 15);
    b.Bp(0x49, 0);
    b.Bp(0x4A, 639 | 527u << 10);
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
    Check(Near(TexPixel(r.Xfb(nullptr, nullptr), 480, 396), 255, 0, 0), "1.5x: an odd copy, the picture still drawn");
  }
  Check(r.Stats().rescales == 4, "rescale: counted");
  Check(glGetError() == GL_NO_ERROR, "rescale: no GL error");
  r.Release();
}

// The rule of Auto (gx_auto_level.h) with made-up measurements: `seconds` of chunks at `rate` a
// second, each fence check finding `behind` chunks unfinished.
struct AutoRun {
  re4gx::AutoLevel a;
  double t = 0.0;
  unsigned long long sum = 0;
  unsigned checks = 0;
  unsigned long long dropped = 0;  // 60 FPS: in-between pictures left out
  double waited = 0.0;             // ms the render thread waited for the GPU in its Runs
  std::vector<std::string> lines;
  // `droppedEvery`: one in-between picture left out each so many chunks (0: none); `wait`: ms the
  // render thread waits in each Run
  void Play(double seconds, double rate, unsigned behind, bool mayChange = true, unsigned droppedEvery = 0,
            double wait = 0.0) {
    const double end = t + seconds;
    while (t < end) {
      t += 1.0 / rate;
      sum += behind;
      checks++;
      waited += wait;
      if (droppedEvery && checks % droppedEvery == 0)
        dropped++;
      std::string why;
      a.Chunk(t, sum, checks, mayChange, &why, dropped, waited);
      if (!why.empty())
        lines.push_back(why);
    }
  }
};

void TestAutoLevel() {
  {
    AutoRun r;
    r.a.Start(2, 0.0);
    r.Play(20.0, 30.0, 0);
    Check(r.a.Level() == 2 && r.lines.empty(), "auto level: 30 pictures/s with the GPU keeping up: stays at the top");
    r.Play(20.0, 25.0, 0);
    Check(r.a.Level() == 2, "auto level: slow with the GPU idle (the processor): stays");
    r.Play(4.1, 20.0, 1, false);
    Check(r.a.Level() == 2, "auto level: no change while it may not (menu, R3 series)");
    r.Play(4.0, 30.0, 0);
    r.Play(1.5, 20.0, 1);
    r.Play(4.0, 30.0, 0);
    Check(r.a.Level() == 2, "auto level: a short hitch (one slow window at most): stays");
    r.Play(6.5, 20.0, 1);
    Check(r.a.Level() == 1 && r.a.Downs() == 1, "auto level: two slow windows in a row, the GPU behind: one level down");
    Check(!r.lines.empty() && r.lines.back().find("level 2 -> 1") != std::string::npos, "auto level: logged");
    r.Play(10.0, 20.0, 1);
    Check(r.a.Level() == 1, "auto level: never below 1");
    r.Play(15.0, 30.0, 0);  // at most 26 s after the step down (it came 4-6.5 s into the slow part)
    Check(r.a.Level() == 1, "auto level: not up before 30 s after the step down");
    r.Play(12.0, 30.0, 0);
    Check(r.a.Level() == 2 && r.a.Ups() == 1, "auto level: up again after 30 s, 10 s keeping up");
    // straight down again: the next wait doubles (60 s)
    r.Play(6.5, 20.0, 2);
    Check(r.a.Level() == 2 || r.a.Hold() == 60.0, "auto level: down again soon: the next wait 60 s");
  }
  {
    // the bounce right after a step up
    AutoRun r;
    r.a.Start(2, 0.0);
    r.Play(6.5, 20.0, 1);
    Check(r.a.Level() == 1, "auto level: down");
    while (r.a.Ups() == 0 && r.t < 200.0)
      r.Play(0.5, 30.0, 0);
    Check(r.a.Ups() == 1, "auto level: up");
    r.Play(6.5, 20.0, 2);
    Check(r.a.Level() == 1 && r.a.Hold() == 60.0, "auto level: down again within 15 s of the step up: the next wait 60 s");
    const double downAt = r.t;
    while (r.a.Ups() == 1 && r.t < 400.0)
      r.Play(0.5, 30.0, 0);
    Check(r.t - downAt >= 55.0, "auto level: the longer wait holds");  // the step down came during the slow part
    r.Play(100.0, 30.0, 0);
    Check(r.a.Level() == 2, "auto level: never above the screen's level");
  }
  {
    AutoRun r;
    r.a.Start(3, 0.0);
    r.Play(6.5, 20.0, 1);
    Check(r.a.Level() == 2, "auto level: one level down after two slow windows");
    r.Play(6.5, 20.0, 1);
    Check(r.a.Level() == 1, "auto level: down one level each two windows while the GPU stays behind");
  }
  {
    // 60 FPS: the game keeps 30 pictures a second because the renderer leaves in-between pictures out
    AutoRun r;
    r.a.Start(2, 0.0);
    r.Play(6.5, 30.0, 1, true, 2);
    Check(r.a.Level() == 1 && r.a.Downs() == 1,
          "auto level: 60 FPS, half the in-between pictures left out, the GPU behind: one level down");
    Check(!r.lines.empty() && r.lines.back().find("% without their in-between picture") != std::string::npos,
          "auto level: the share left out logged");
    AutoRun idle;
    idle.a.Start(2, 0.0);
    idle.Play(20.0, 30.0, 0, true, 2);
    Check(idle.a.Level() == 2, "auto level: 60 FPS, pictures left out with the GPU idle (the processor): stays");
    AutoRun few;
    few.a.Start(2, 0.0);
    few.Play(6.5, 20.0, 1);
    while (few.a.Ups() == 0 && few.t < 200.0)
      few.Play(0.5, 30.0, 0, true, 5);
    Check(few.a.Ups() == 0, "auto level: 60 FPS, not up again while a fifth of the in-between pictures is left out");
    while (few.a.Ups() == 0 && few.t < 400.0)
      few.Play(0.5, 30.0, 0);
    Check(few.a.Ups() == 1, "auto level: 60 FPS, up again once (almost) none is left out");
  }
  {
    // the render thread waits for the GPU inside the driver: no fence behind, the game still slow
    // (Retroid 27.10.2026: the lake at level 2, 24 pictures a second, 35 ms waiting a chunk)
    AutoRun r;
    r.a.Start(2, 0.0);
    r.Play(20.0, 30.0, 0, true, 0, 8.0);
    Check(r.a.Level() == 2, "auto level: 30 pictures/s with the render thread waiting a little: stays");
    r.Play(20.0, 25.0, 0, true, 0, 3.0);
    Check(r.a.Level() == 2, "auto level: slow without waiting for the GPU (the processor): stays");
    r.Play(6.5, 24.0, 0, true, 0, 30.0);
    Check(r.a.Level() == 1 && r.a.Downs() == 1, "auto level: slow, the render thread waiting for the GPU: one level down");
    Check(!r.lines.empty() && r.lines.back().find("waiting 30.0 ms") != std::string::npos,
          "auto level: the waiting logged");
    while (r.a.Ups() == 0 && r.t < 200.0)
      r.Play(0.5, 30.0, 0, true, 0, 7.0);
    Check(r.a.Ups() == 0, "auto level: not up again while the render thread still waits 7 ms a chunk");
    while (r.a.Ups() == 0 && r.t < 400.0)
      r.Play(0.5, 30.0, 0, true, 0, 2.0);
    Check(r.a.Ups() == 1, "auto level: up again once it hardly waits");
  }
  {
    // in halves (the app): from 2x (4) half a level at a time, never below 1x (2), named "1.5x"
    AutoRun r;
    r.a.Start(4, 0.0, 2, true);
    r.Play(6.5, 20.0, 1);
    Check(r.a.Level() == 3 && !r.lines.empty() && r.lines.back().find("level 2x -> 1.5x") != std::string::npos,
          "auto level in halves: 2x -> 1.5x");
    r.Play(6.5, 20.0, 1);
    Check(r.a.Level() == 2, "auto level in halves: 1.5x -> 1x");
    r.Play(10.0, 20.0, 1);
    Check(r.a.Level() == 2, "auto level in halves: never below 1x");
  }
}

void TestFog(re4gx::GxRenderer &r) {
  // linear fog, orthographic: ze = A * depth - C; A = 1, C = 0 -> factor = depth
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  const auto fogWord = [](float f) {
    const std::uint32_t v = Bits(f);
    return ((v >> 12) & 0x7FF) | ((v >> 23) & 0xFF) << 11 | (v >> 31) << 19;
  };
  b.Bp(0xEE, fogWord(1.0f));
  b.Bp(0xF1, fogWord(0.0f) | 1u << 20 | 2u << 21); // orthographic, linear
  b.Bp(0xF2, 0xFFFFFF);                              // white fog
  Quad(b, 0, 0, 50, 50, -0.5f, kBlack);              // depth 0.5 -> half fog
  r.Run(b.Chunk());
  auto p = EfbPixel(r, 25, 25);
  Check(Near(p, 128, 128, 128, 2), "fog: linear, half way at depth 0.5");
  if (!Near(p, 128, 128, 128, 2))
    Print("fog", p);
}

// Random TEV setups: every generated shader compiles and links.
void TestShaderVariety(re4gx::GxRenderer &r) {
  std::mt19937 rng(42);
  const unsigned before = r.Stats().shaderErrors;
  for (int round = 0; round < 60; round++) {
    Builder b;
    Baseline(b);
    const unsigned stages = 1 + rng() % 16, ind = rng() % 3;
    b.Bp(0x00, (2u + 0) | 2u << 4 | (stages - 1) << 10 | ind << 16 | (rng() % 3) << 14);
    b.Xf(0x103F, {2});
    b.Xf(0x1040, {static_cast<std::uint32_t>(5u << 7 | (rng() & 2))});
    b.Xf(0x1041, {static_cast<std::uint32_t>(6u << 7 | (rng() & 6))});
    b.Xf(0x1012, {static_cast<std::uint32_t>(rng() & 1)});
    b.Xf(0x1009, {2});
    b.Xf(0x100E, {static_cast<std::uint32_t>(rng() & 0x7FFF)});
    b.Xf(0x100F, {static_cast<std::uint32_t>(rng() & 0x7FFF)});
    b.Xf(0x1010, {static_cast<std::uint32_t>(rng() & 0x7FFF)});
    b.Xf(0x1011, {static_cast<std::uint32_t>(rng() & 0x7FFF)});
    for (unsigned s = 0; s < stages; s++) {
      b.Bp(0xC0 + 2 * s, rng() & 0xFFFFFF);
      b.Bp(0xC1 + 2 * s, rng() & 0xFFFFFF);
      b.Bp(0x10 + s, ind ? (rng() & 0x1FFFFF) : 0);
    }
    for (int i = 0; i < 8; i++) {
      b.Bp(0x28 + i, rng() & 0xFFFFFF);
      b.Bp(0xF6 + i, rng() & 0xFFFFFF);
    }
    b.Bp(0x27, rng() & 0xFFFFFF);
    b.Bp(0xF3, rng() & 0xFFFFFF);
    b.Bp(0xF1, rng() & 0xFFFFFF);
    std::vector<std::uint8_t> v;
    for (int i = 0; i < 3; i++) {
      const float p[3] = {static_cast<float>(i * 10), static_cast<float>(i * 7), 0};
      const float n[3] = {0, 0, 1};
      const float st[4] = {0.1f, 0.2f, 0.3f, 0.4f};
      v.insert(v.end(), reinterpret_cast<const std::uint8_t *>(p), reinterpret_cast<const std::uint8_t *>(p) + 12);
      v.insert(v.end(), reinterpret_cast<const std::uint8_t *>(n), reinterpret_cast<const std::uint8_t *>(n) + 12);
      v.insert(v.end(), kRed, kRed + 4);
      v.insert(v.end(), kGreen, kGreen + 4);
      v.insert(v.end(), reinterpret_cast<const std::uint8_t *>(st), reinterpret_cast<const std::uint8_t *>(st) + 16);
    }
    b.Draw(RE4_GX_V_NORMAL | RE4_GX_V_COLOR0 | RE4_GX_V_COLOR1 | RE4_GX_V_TEX0 | (RE4_GX_V_TEX0 << 1), 48, v, 3);
    r.Run(b.Chunk());
  }
  Check(r.Stats().shaderErrors == before, "random TEV setups: every shader compiles");
  std::fprintf(stderr, "  shaders built: %u\n", r.Stats().programs);
}

} // namespace

// The game's brightness option (main_sub.cpp Bg_brightness_set): the seven
// copy filter taps (BP 0x53/0x54, 1/64 each) of the display copy sum to 64 at
// the normal setting; more brightens, less darkens the copied picture.
void TestCopyFilter(re4gx::GxRenderer &r) {
  const std::uint8_t grey[4] = {100, 100, 100, 255};
  struct {
    unsigned taps[7];
    int expect;
    const char *what;
  } cases[] = {{{8, 8, 10, 12, 10, 8, 8}, 100, "copy filter: sum 64 (normal brightness) keeps the colour"},
               {{10, 10, 12, 15, 12, 10, 10}, 123, "copy filter: sum 79 brightens by 79/64"},
               {{6, 6, 8, 9, 8, 6, 6}, 77, "copy filter: sum 49 darkens by 49/64"},
               {{20, 20, 25, 30, 25, 20, 20}, 250, "copy filter: sum 160 brightens and stays in range"}};
  for (const auto &c : cases) {
    Builder b;
    Baseline(b);
    b.Bp(0x53, c.taps[0] | c.taps[1] << 6 | c.taps[2] << 12 | c.taps[3] << 18);
    b.Bp(0x54, c.taps[4] | c.taps[5] << 6 | c.taps[6] << 12);
    DisplayCopy(b, true, kBlack);
    Quad(b, 10, 20, 110, 70, 0, grey);
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
    auto px = TexPixel(r.Xfb(nullptr, nullptr), 50, 40);
    Check(Near(px, c.expect, c.expect, c.expect), c.what);
    if (!Near(px, c.expect, c.expect, c.expect))
      Print("xfb", px);
  }
  // back to the normal filter for the tests after
  Builder b;
  Baseline(b);
  b.Bp(0x53, 8 | 8 << 6 | 10 << 12 | 12 << 18);
  b.Bp(0x54, 10 | 8 << 6 | 8 << 12);
  DisplayCopy(b, true, kBlack);
  r.Run(b.Chunk());
}

// The gameplay autofocus (BP 0x5A, the core's marker; runtime/dof.cpp): a
// perspective scene (near 100, far 1,000,000 as the game's): far behind, a
// red / blue edge at x = 160 (20 m away); at the centre a grey card 1 m
// away (the focus); right of it a near green / white edge at 1.5 m. The
// marker softens the far edge, the near one stays sharp; with nothing near at
// the centre (looking into the distance) the picture stays as it is.
void QuadAt(Builder &b, float sx0, float sy0, float sx1, float sy1, float distance, const std::uint8_t c[4]) {
  // screen pixels through the viewport of Baseline (centre 320, 264; half 320 x 264)
  auto x = [&](float sx) { return (sx - 320.0f) / 320.0f * distance; };
  auto y = [&](float sy) { return (264.0f - sy) / 264.0f * distance; };
  std::vector<std::uint8_t> v;
  V(v, x(sx0), y(sy0), -distance, c);
  V(v, x(sx1), y(sy0), -distance, c);
  V(v, x(sx1), y(sy1), -distance, c);
  V(v, x(sx0), y(sy1), -distance, c);
  b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
}

void AutofocusScene(Builder &b, bool nearCentre, bool mark) {
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  const std::uint8_t red[4] = {255, 0, 0, 128}, blue[4] = {0, 0, 255, 128}, grey[4] = {128, 128, 128, 255};
  QuadAt(b, 0, 0, 160, 528, 20000.0f, red);
  QuadAt(b, 160, 0, 640, 528, 20000.0f, blue);
  if (nearCentre)
    QuadAt(b, 280, 224, 360, 304, 1000.0f, grey);
  QuadAt(b, 440, 400, 480, 480, 1500.0f, kGreen);
  QuadAt(b, 480, 400, 520, 480, 1500.0f, kWhite);
  if (mark)
    b.Bp(0x5A, 1);
  DisplayCopy(b, false, kBlack);
}

void TestAutofocus(re4gx::GxRenderer &r, bool multisampled = false) {
  char tag[64];
  std::snprintf(tag, sizeof(tag), "autofocus%s%s", multisampled ? " (multisampled)" : "",
                r.HalfDof() ? "" : " (full resolution)");
  auto run = [&](bool nearCentre, bool mark) {
    Builder b;
    AutofocusScene(b, nearCentre, mark);
    r.Run(b.Chunk());
    return r.Xfb(nullptr, nullptr);
  };
  char what[160];
  const unsigned before = r.Stats().autofocus;
  GLuint xfb = run(true, false);
  auto sharpL = TexPixel(xfb, 159, 100), sharpR = TexPixel(xfb, 160, 100);
  std::snprintf(what, sizeof(what), "%s: without the marker the far edge is sharp", tag);
  Check(Near(sharpL, 255, 0, 0) && Near(sharpR, 0, 0, 255), what);
  xfb = run(true, true);
  std::snprintf(what, sizeof(what), "%s: one blur pass for the marker", tag);
  Check(r.Stats().autofocus == before + 1, what);
  auto l = TexPixel(xfb, 159, 100), rr = TexPixel(xfb, 160, 100);
  std::snprintf(what, sizeof(what), "%s: the far edge behind the focus gets soft", tag);
  Check(l[2] > 20 && l[0] < 240 && rr[0] > 20 && rr[2] < 240, what);
  if (!(l[2] > 20 && l[0] < 240 && rr[0] > 20 && rr[2] < 240)) {
    Print("left of the edge", l);
    Print("right of the edge", rr);
  }
  std::snprintf(what, sizeof(what), "%s: away from the edge the background keeps its colour", tag);
  Check(Near(TexPixel(xfb, 80, 100), 255, 0, 0) && Near(TexPixel(xfb, 600, 100), 0, 0, 255), what);
  std::snprintf(what, sizeof(what), "%s: the focused card stays as it is", tag);
  Check(Near(TexPixel(xfb, 320, 264), 128, 128, 128), what);
  auto g = TexPixel(xfb, 479, 440), w = TexPixel(xfb, 480, 440);
  std::snprintf(what, sizeof(what), "%s: the near edge in front of the focus stays sharp", tag);
  Check(Near(g, 0, 255, 0) && Near(w, 255, 255, 255), what);
  // the blurred background right next to it hardly takes its colour (llvmpipe: 0 at the full
  // resolution, 8 at the half one, 20 when the half one's centre texels in front counted too)
  auto beside = TexPixel(xfb, 520, 440), below = TexPixel(xfb, 500, 480);
  std::snprintf(what, sizeof(what), "%s: the background next to the sharp near edge keeps its colour", tag);
  Check(Near(beside, 0, 0, 255, 12) && Near(below, 0, 0, 255, 12), what);
  if (!(Near(beside, 0, 0, 255, 12) && Near(below, 0, 0, 255, 12))) {
    Print("beside the white card", beside);
    Print("below the white card", below);
  }
  if (!multisampled) {
    std::snprintf(what, sizeof(what), "%s: the EFB alpha stays", tag);
    Check(EfbPixel(r, 159, 100)[3] == 128, what);
  }
  // the focus follows its measurements step by step (the Retroid with MSAA, 19.10.2026: thin
  // branches went soft for one picture): one picture without the card at the centre (measured
  // far) changes nothing, the far edge stays soft; after three the focus is far, the picture sharp
  xfb = run(false, true);
  l = TexPixel(xfb, 159, 100);
  rr = TexPixel(xfb, 160, 100);
  std::snprintf(what, sizeof(what), "%s: one picture measured far leaves the focus (the far edge stays soft)", tag);
  Check(l[2] > 20 && l[0] < 240 && rr[0] > 20 && rr[2] < 240, what);
  run(false, true);
  xfb = run(false, true);
  l = TexPixel(xfb, 159, 100);
  rr = TexPixel(xfb, 160, 100);
  std::snprintf(what, sizeof(what), "%s: looking into the distance for three pictures leaves the picture sharp", tag);
  Check(Near(l, 255, 0, 0) && Near(rr, 0, 0, 255), what);
  if (!(Near(l, 255, 0, 0) && Near(rr, 0, 0, 255))) {
    Print("left of the edge", l);
    Print("right of the edge", rr);
  }
  // a picture without the marker in between: the next autofocus starts anew, at once
  xfb = run(false, true);
  run(true, false);
  xfb = run(true, true);
  l = TexPixel(xfb, 159, 100);
  rr = TexPixel(xfb, 160, 100);
  std::snprintf(what, sizeof(what), "%s: after a picture without the autofocus the focus is measured anew", tag);
  Check(l[2] > 20 && l[0] < 240 && rr[0] > 20 && rr[2] < 240, what);
  // standing (Retroid, 17.10.2026): Leon (a card 1 m away) covers the centre,
  // behind him a green / white wall 3 m away, the far edge at 20 m. Without
  // his depth in the marker he is the focus and the wall goes soft; with it
  // (1.5 m) the focus is the wall around him: the wall stays sharp, the far
  // edge gets soft, Leon stays as he is
  auto standing = [&](std::uint32_t player) {
    run(true, false);  // each case on its own: the autofocus starts anew
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    const float n = 100.0f, f = 1000000.0f;
    b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
    b.Xf(0x1026, {0});
    const std::uint8_t red[4] = {255, 0, 0, 255}, blue[4] = {0, 0, 255, 255}, grey[4] = {128, 128, 128, 255};
    QuadAt(b, 0, 0, 160, 528, 20000.0f, red);
    QuadAt(b, 160, 0, 640, 528, 20000.0f, blue);
    QuadAt(b, 200, 120, 260, 408, 3000.0f, kGreen);
    QuadAt(b, 260, 120, 440, 408, 3000.0f, kWhite);
    QuadAt(b, 280, 224, 360, 304, 1000.0f, grey);
    b.Bp(0x5A, player);
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
    return r.Xfb(nullptr, nullptr);
  };
  xfb = standing(1);
  g = TexPixel(xfb, 259, 150);
  w = TexPixel(xfb, 260, 150);
  std::snprintf(what, sizeof(what), "%s: Leon as the focus softens the wall behind him (the problem)", tag);
  Check(!(Near(g, 0, 255, 0) && Near(w, 255, 255, 255)), what);
  xfb = standing(1500);
  g = TexPixel(xfb, 259, 150);
  w = TexPixel(xfb, 260, 150);
  std::snprintf(what, sizeof(what), "%s: with Leon's depth the wall around him is the focus and stays sharp", tag);
  Check(Near(g, 0, 255, 0) && Near(w, 255, 255, 255), what);
  if (!(Near(g, 0, 255, 0) && Near(w, 255, 255, 255))) {
    Print("wall green", g);
    Print("wall white", w);
  }
  l = TexPixel(xfb, 159, 100);
  rr = TexPixel(xfb, 160, 100);
  std::snprintf(what, sizeof(what), "%s: with Leon's depth the far edge behind the wall gets soft", tag);
  Check(l[2] > 20 && l[0] < 240 && rr[0] > 20 && rr[2] < 240, what);
  std::snprintf(what, sizeof(what), "%s: Leon himself stays sharp", tag);
  Check(Near(TexPixel(xfb, 320, 264), 128, 128, 128), what);
  // all the picture is Leon or nearer: no focus, no blur
  xfb = standing(30000);
  l = TexPixel(xfb, 159, 100);
  rr = TexPixel(xfb, 160, 100);
  std::snprintf(what, sizeof(what), "%s: nothing behind Leon: the picture stays sharp", tag);
  Check(Near(l, 255, 0, 0) && Near(rr, 0, 0, 255), what);
  // back to the orthographic projection of the other tests
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  r.Run(b.Chunk());
}

// The game draws on after an EFB copy (GPU step d2: on the tiles the copy ends the GPU's pass,
// the EFB is drawn back after it): what lies in front before the copy still hides what is drawn
// behind it after, a draw at exactly the same depth with EQUAL still passes, and the colours
// drawn before stay.
void TestPassBreak(re4gx::GxRenderer &r, const char *tag) {
  char what[160];
  Builder b;
  Baseline(b);
  DisplayCopy(b, false, kBlack);
  const unsigned restores = r.Stats().efbRestores;
  Quad(b, 0, 0, 640, 528, -1.0f, kBlue);     // background, depth 1
  Quad(b, 100, 100, 300, 300, -0.5f, kGreen);  // in front, depth 0.5
  // a copy of 32x32 at (0, 0) to a texture: the pass ends
  b.Bp(0x49, 0);
  b.Bp(0x4A, 31 | 31u << 10);
  b.Bp(0x4D, 32);
  b.Bp(0x4B, 0x200000 >> 5);
  b.Bp(0x52, 6u << 4 | 2u << 15);
  b.Bp(0x4A, 639 | 527u << 10);
  Quad(b, 200, 200, 400, 400, -0.75f, kRed);  // behind the green one, depth 0.75
  b.Bp(0x40, 1 | 2 << 1 | 1 << 4);            // EQUAL
  Quad(b, 100, 100, 150, 150, -0.5f, kWhite);  // the green one's depth exactly
  b.Bp(0x40, 1 | 3 << 1 | 1 << 4);
  DisplayCopy(b, false, kBlack);
  r.Run(b.Chunk());
  GLuint xfb = r.Xfb(nullptr, nullptr);
  auto front = TexPixel(xfb, 250, 250), behind = TexPixel(xfb, 350, 350), equal = TexPixel(xfb, 120, 120),
       before = TexPixel(xfb, 50, 450);
  std::snprintf(what, sizeof(what), "%s: after a copy, the depth drawn before still hides what lies behind", tag);
  Check(Near(front, 0, 255, 0), what);
  std::snprintf(what, sizeof(what), "%s: after a copy, what lies in front of the background is drawn", tag);
  Check(Near(behind, 255, 0, 0), what);
  std::snprintf(what, sizeof(what), "%s: after a copy, a draw at exactly the same depth passes EQUAL", tag);
  Check(Near(equal, 255, 255, 255), what);
  std::snprintf(what, sizeof(what), "%s: after a copy, the colour drawn before stays", tag);
  Check(Near(before, 0, 0, 255), what);
  if (!(Near(front, 0, 255, 0) && Near(behind, 255, 0, 0) && Near(equal, 255, 255, 255) && Near(before, 0, 0, 255))) {
    Print("green in front", front);
    Print("red behind", behind);
    Print("white EQUAL", equal);
    Print("blue before", before);
  }
  if (r.TiledMsaa()) {
    // the copy (and the display copy) end passes: drawn back before the draws after them
    std::snprintf(what, sizeof(what), "%s: the EFB drawn back after the copy", tag);
    Check(r.Stats().efbRestores >= restores + 1, what);
  }
}

// The uniform stream moves on to its next quarter only between chunks: a fence set while a
// picture was drawn changed the multisampled pixels around it on the Adreno 650 (thin branches
// paler for one picture, 20.10.2026). Chunks of 700 draws, each with its own position matrix
// (one XF block in the stream per draw, about 2.3 MB a chunk), switch before they start; one of
// 1400 draws (more than a quarter) has to switch inside. Every draw lands where its matrix says.
void TestUniformStream(re4gx::GxRenderer &r, const char *tag) {
  char what[160];
  auto chunk = [&](unsigned draws, bool reset) {
    Builder b;
    if (reset)
      Baseline(b);
    else
      b.flags = 0;
    DisplayCopy(b, true, kBlack);
    for (unsigned i = 0; i < draws; i++) {
      // a 4x4 square on a grid of 40 x 33 places, the matrix moves it there
      const float x = static_cast<float>(i % 40) * 16.0f, y = static_cast<float>(i / 40 % 33) * 16.0f;
      b.XfF(0, {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, 0});
      Quad(b, 4, 4, 8, 8, 0, (i & 1) ? kGreen : kRed);
    }
    b.XfF(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
    DisplayCopy(b, false, kBlack);
    const re4gx::GxStats before = r.Stats();
    r.Run(b.Chunk());
    const re4gx::GxStats &s = r.Stats();
    // the first and the last square of the chunk, and the black between
    const GLuint xfb = r.Xfb(nullptr, nullptr);
    const unsigned last = draws - 1;
    const int lx = static_cast<int>(last % 40) * 16 + 6, ly = static_cast<int>(last / 40 % 33) * 16 + 6;
    const bool ok = xfb && Near(TexPixel(xfb, 6, 6), 255, 0, 0) && Near(TexPixel(xfb, 2, 2), 0, 0, 0) &&
                    Near(TexPixel(xfb, lx, ly), (last & 1) ? 0 : 255, (last & 1) ? 255 : 0, 0);
    return std::make_pair(ok, std::make_pair(s.streamSwitches - before.streamSwitches,
                                             s.streamSplits - before.streamSplits));
  };
  unsigned switches = 0, splits = 0;
  bool right = true;
  // a draw moves the square with its position matrix: one part of the XF block (XFM, 1 KB) goes to
  // the stream per draw (until 22.10.2026 the whole block, 3.3 KB; then 700 and 1400 draws)
  for (int i = 0; i < 10; i++) {
    const auto res = chunk(2400, i == 0);
    right = right && res.first;
    switches += res.second.first;
    splits += res.second.second;
  }
  std::snprintf(what, sizeof(what), "%s: uniform stream, chunks of 2400 draws drawn right", tag);
  Check(right, what);
  std::snprintf(what, sizeof(what), "%s: uniform stream switches between chunks (%u), none inside (%u)", tag,
                switches, splits);
  Check(switches >= 8 && splits == 0, what);
  const auto big = chunk(5000, false);
  std::snprintf(what, sizeof(what), "%s: uniform stream, a chunk larger than a quarter drawn right", tag);
  Check(big.first, what);
  std::snprintf(what, sizeof(what), "%s: uniform stream, a chunk larger than a quarter switches inside (%u)", tag,
                big.second.second);
  Check(big.second.second >= 1, what);
  // and back to chunks that fit: from a quarter's start again, none inside
  const auto after = chunk(2400, false);
  std::snprintf(what, sizeof(what), "%s: uniform stream, after the large chunk one of 2400 draws right, none inside",
                tag);
  Check(after.first && after.second.first == 1 && after.second.second == 0, what);
}

// The menu's picture options (M11): a multisampled EFB is resolved before
// every copy out of it (display copy, texture copy); a slanted edge comes out
// between the two colours; anisotropic filtering does not change a picture
// without mipmaps.
void TestOptions(bool poison, bool tiled, bool halfDof = true, int lose = 0) {
  int maxSamples = 1;
  float maxAnisotropy = 1.0f;
  re4gx::GxRenderer::Limits(&maxSamples, &maxAnisotropy);
  std::fprintf(stderr, "  options: up to %d samples, anisotropy up to %.0f, MSAA %s%s\n", maxSamples, maxAnisotropy,
               tiled ? "on the tiles where the GPU can" : "in own buffers",
               lose == 2   ? ", everything lost at a pass's end, the depth never stored"
               : lose == 1 ? ", everything lost at a pass's end"
                           : "");
  re4gx::GxRenderer r;
  re4gx::GxOptions o;
  o.samples = 4;
  o.anisotropy = 8.0f;
  o.invalidate = poison;
  o.poison = poison;
  o.tiledMsaa = tiled;
  o.halfDof = halfDof;
  o.loseTiles = lose;
  r.SetOptions(o);
  std::string error;
  Require(r.Init(1, &error), error.c_str());
  // MSAA on the tiles exactly where the GPU has both extensions (GPU step d)
  const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
  const bool can = ext && std::strstr(ext, "GL_EXT_multisampled_render_to_texture2") && maxSamples > 1;
  // (d2: not when the device never stores the resolved depth; the self-test at Init finds it)
  Check(r.TiledMsaa() == (tiled && can && lose < 2), "options: MSAA on the tiles where asked for and the GPU can");
  if (tiled && can && lose == 2)
    Check(r.Samples() > 1, "options: the depth never stored on the tiles: own buffers instead");
  Check(r.Samples() == (maxSamples >= 4 ? 4 : 1) || (r.Samples() > 1 && r.Samples() <= maxSamples),
        "options: the samples asked for, as far as the device goes");
  Check(r.Anisotropy() == std::min(8.0f, maxAnisotropy), "options: the anisotropy, as far as the device goes");
  {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    Quad(b, 10, 20, 110, 70, 0, kGreen);
    std::vector<std::uint8_t> v;  // a slanted edge from (200, 100) to (300, 160)
    V(v, 200, 100, 0, kRed);
    V(v, 300, 160, 0, kRed);
    V(v, 200, 160, 0, kRed);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 3);
    DisplayCopy(b, false, kBlue);
    r.Run(b.Chunk());
  }
  GLuint xfb = r.Xfb(nullptr, nullptr);
  Check(xfb && Near(TexPixel(xfb, 50, 40), 0, 255, 0), "multisampled: the display copy holds the picture");
  Check(xfb && Near(TexPixel(xfb, 300, 300), 0, 0, 255), "multisampled: the clear colour elsewhere");
  if (r.Samples() > 1 && xfb) {
    int between = 0;
    for (int x = 200; x < 300; x++) {
      const int y = 100 + (x - 200) * 60 / 100;  // on the edge
      auto px = TexPixel(xfb, x, y);
      between += px[0] > 20 && px[0] < 235 && px[2] > 20 && px[2] < 235;
    }
    Check(between > 20, "multisampled: the slanted edge is smoothed");
  }
  TestCopyAndPalette(r, true);  // EFB copy to a texture (resolved first), palette
  TestLetterboxCopy(r, true, true);
  TestShownRowsCopy(r, true, true);
  TestShownRowsCopy(r, true, false);
  TestDepthMap(r, true);
  TestFxaa(r);
  TestFrameSkip(r);
  Check(r.HalfDof() == halfDof, "options: the depth of field's resolution as asked for");
  TestAutofocus(r, r.Samples() > 1);
  TestPassBreak(r, r.TiledMsaa() ? (lose ? "on the tiles, everything lost" : "on the tiles") : "multisampled");
  if (!poison && !lose)
    TestUniformStream(r, r.TiledMsaa() ? "on the tiles" : "multisampled");
  Check(glGetError() == GL_NO_ERROR, "options: no GL error");
  r.Release();
}

// 60 FPS (experimental): the in-between picture of two chunks (GxRenderer::Interpolate). A green
// card 1000 units away (80 pixels wide) moved by its position matrix (dx), a red one moved by its
// vertices (as the game skins its figures), both in perspective; between A (dx -50: 16 pixels to
// the left, red at 430..470) and B (dx +50, red at 450..490) the in-between picture shows them
// halfway, Run(B) afterwards draws B as without it (the state put back), t = 1 draws B, and a
// camera that turns 90 degrees is a cut.
void InterpScene(Builder &b, std::uint32_t sequence, float dx, float red0, float turn, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  const float c = std::cos(turn), s = std::sin(turn);
  b.XfF(0, {c, 0, s, dx, 0, 1, 0, 0, -s, 0, c, 0});
  QuadAt(b, 280, 224, 360, 304, 1000.0f, kGreen);
  b.XfF(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
  QuadAt(b, red0, 380, red0 + 40, 420, 1000.0f, kRed);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

std::vector<std::uint8_t> XfbPixels(re4gx::GxRenderer &r) {
  unsigned w = 0, h = 0;
  const GLuint xfb = r.Xfb(&w, &h);
  std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
  if (!xfb)
    return px;
  glBindFramebuffer(GL_FRAMEBUFFER, gReadFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xfb, 0);
  glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  return px;
}

// Particles (60 FPS, Retroid report 26.10.2026, r224: the smoke and fire flickered): quads of
// the same look, each with a matrix of its own, 2500 units apart at 20000. From picture A to B the
// camera moves 1500 units (the two room pieces show it, not the flying object before them), every
// particle 500 units of its own; the
// first one dies, a new one starts far to the left. The in-between picture places each particle
// halfway along its own move (before: the second took the first's place; nearest without the
// camera's move: the third's), the new one as in B.
void ParticleScene(Builder &b, std::uint32_t sequence, bool second, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f, depth = 20000.0f, camera = second ? 1500.0f : 0.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  // large draws of a look of their own (64 to 76 vertices) in a band at the top: first an object of
  // two parts with one matrix that flies 5000 units (its parts agree with each other, but it is not
  // the camera: the merchant's series, 26.10.2026), then the room, two pieces with matrices of their
  // own that move with the camera
  const std::uint8_t blue[4] = {0, 0, 255, 255}, red[4] = {255, 0, 0, 255};
  auto piece = [&](unsigned quads, float y0, const std::uint8_t *c) {
    std::vector<std::uint8_t> v;
    for (unsigned q = 0; q < quads; q++) {
      const float x0 = -8000.0f + 900.0f * static_cast<float>(q);
      V(v, x0, y0, 0.0f, c);
      V(v, x0 + 700.0f, y0, 0.0f, c);
      V(v, x0 + 700.0f, y0 + 1500.0f, 0.0f, c);
      V(v, x0, y0 + 1500.0f, 0.0f, c);
    }
    b.Draw(RE4_GX_V_COLOR0, 16, v, quads * 4, 0x80);
  };
  b.XfF(0, {1, 0, 0, second ? 5000.0f : 0.0f, 0, 1, 0, 0, 0, 0, 1, -30000.0f});
  piece(16, 17000.0f, red);
  piece(17, 15000.0f, red);
  b.XfF(0, {1, 0, 0, camera - 4000.0f, 0, 1, 0, 0, 0, 0, 1, -30000.0f});
  piece(18, 17000.0f, blue);
  b.XfF(0, {1, 0, 0, camera + 4000.0f, 0, 1, 0, 0, 0, 0, 1, -32000.0f});
  piece(19, 15000.0f, blue);
  // the particles: a small quad each, placed by its matrix
  auto particle = [&](float x) {
    b.XfF(0, {1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, -depth});
    std::vector<std::uint8_t> v;
    V(v, -250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, 250.0f, 0.0f, kGreen);
    V(v, -250.0f, 250.0f, 0.0f, kGreen);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  };
  for (int i = second ? 2 : 1; i <= 5; i++)
    particle(-5000.0f + 2500.0f * static_cast<float>(i - 1) + (second ? camera + 500.0f : 0.0f));
  if (second)
    particle(-7000.0f);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationParticles() {
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb;
  Re4GxChunk a, b;
  ParticleScene(ba, 1, false, &a);
  ParticleScene(bb, 2, true, &b);
  r.Run(a);
  // screen x of a view-space x at 20000 (the viewport of Baseline: 320 + x / 20000 * 320)
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 20000.0f * 320.0f)); };
  const GLuint xfbA = r.Xfb(nullptr, nullptr);
  Check(Near(TexPixel(xfbA, px(-5000.0f), 264), 0, 255, 0) && Near(TexPixel(xfbA, px(-2500.0f), 264), 0, 255, 0),
        "particles: picture A (the first two particles)");
  Check(r.Interpolate(a, b, 0.5f), "particles: the in-between picture is shown");
  const GLuint xfb = r.Xfb(nullptr, nullptr);
  // the second particle: A -2500, B -500; halfway -1500. Taking the first's place (A -5000) it
  // would sit at -2750, taking the third's (A 0, nearest without the camera's move) at -250
  const auto own = TexPixel(xfb, px(-1500.0f), 264), wrong = TexPixel(xfb, px(-2750.0f), 264),
             third = TexPixel(xfb, px(-250.0f), 264);
  Check(Near(own, 0, 255, 0) && Near(wrong, 0, 0, 0) && Near(third, 0, 0, 0),
        "particles: each halfway along its own move");
  // the new one: as in B (-7000); halfway to the dead first one's place it would be at -6000
  const auto fresh = TexPixel(xfb, px(-7000.0f), 264), between = TexPixel(xfb, px(-6000.0f), 264);
  Check(Near(fresh, 0, 255, 0) && Near(between, 0, 0, 0), "particles: one that has just started stands where it is in B");
  if (!Near(own, 0, 255, 0) || !Near(wrong, 0, 0, 0) || !Near(third, 0, 0, 0) || !Near(fresh, 0, 255, 0) ||
      !Near(between, 0, 0, 0))
    Print("own", own), Print("wrong", wrong), Print("third", third), Print("fresh", fresh), Print("between", between);
  Check(glGetError() == GL_NO_ERROR, "particles: no GL error");
  r.Release();
}

// Larger draws of the same look keep the game's order (Retroid report 26.10.2026: with fast turns
// a long bar at the bottom of the picture; two alike parts of Leon had swapped places). Two draws of
// 20 vertices at 3000 units: X moves from 0 to 300, Y from 400 to 700. X lies nearer to Y's place
// before than to its own, but stays X: halfway at 150 and 550, not both at 350. And a particle's
// material colour (its fade) goes halfway too: red in A, blue in B, purple between.
void LooksScene(Builder &b, std::uint32_t sequence, bool second, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  auto part = [&](float x) {
    b.XfF(0, {1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, -3000.0f});
    std::vector<std::uint8_t> v;
    for (int q = 0; q < 5; q++) {
      const float y0 = -150.0f + 60.0f * static_cast<float>(q);
      V(v, -60.0f, y0, 0.0f, kGreen);
      V(v, 60.0f, y0, 0.0f, kGreen);
      V(v, 60.0f, y0 + 60.0f, 0.0f, kGreen);
      V(v, -60.0f, y0 + 60.0f, 0.0f, kGreen);
    }
    b.Draw(RE4_GX_V_COLOR0, 16, v, 20, 0x80);
  };
  part(second ? 300.0f : 0.0f);
  part(second ? 700.0f : 400.0f);
  // the particle: colour from the material register (XF 0x100C), not from its vertices
  b.Xf(0x100E, {0});
  b.Xf(0x1010, {0});
  b.Xf(0x100C, {second ? 0x0000C8FFu : 0xC80000FFu});
  b.XfF(0, {1, 0, 0, -1200.0f, 0, 1, 0, 0, 0, 0, 1, -3000.0f});
  std::vector<std::uint8_t> v;
  V(v, -100.0f, -100.0f, 0.0f, kWhite);
  V(v, 100.0f, -100.0f, 0.0f, kWhite);
  V(v, 100.0f, 100.0f, 0.0f, kWhite);
  V(v, -100.0f, 100.0f, 0.0f, kWhite);
  b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  b.Xf(0x100E, {1});
  b.Xf(0x1010, {1});
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationLooks() {
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb;
  Re4GxChunk a, b;
  LooksScene(ba, 1, false, &a);
  LooksScene(bb, 2, true, &b);
  r.Run(a);
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 3000.0f * 320.0f)); };
  const GLuint xfbA = r.Xfb(nullptr, nullptr);
  Check(Near(TexPixel(xfbA, px(-1200.0f), 264), 200, 0, 0, 3), "looks: picture A (the particle red)");
  Check(r.Interpolate(a, b, 0.5f), "looks: the in-between picture is shown");
  const GLuint xfb = r.Xfb(nullptr, nullptr);
  const auto x = TexPixel(xfb, px(150.0f), 264), y = TexPixel(xfb, px(550.0f), 264), swapped = TexPixel(xfb, px(350.0f), 264);
  Check(Near(x, 0, 255, 0) && Near(y, 0, 255, 0) && Near(swapped, 0, 0, 0),
        "looks: larger draws of the same look keep the game's order");
  const auto colour = TexPixel(xfb, px(-1200.0f), 264);
  Check(Near(colour, 100, 0, 100, 3), "looks: a particle's material colour goes halfway");
  if (!Near(x, 0, 255, 0) || !Near(y, 0, 255, 0) || !Near(swapped, 0, 0, 0) || !Near(colour, 100, 0, 100, 3))
    Print("x", x), Print("y", y), Print("swapped", swapped), Print("colour", colour);
  // the real picture after it: B's colour (blue), the material register put back
  r.Run(b);
  Check(Near(TexPixel(r.Xfb(nullptr, nullptr), px(-1200.0f), 264), 0, 0, 200, 3), "looks: picture B after it (blue)");
  Check(glGetError() == GL_NO_ERROR, "looks: no GL error");
  r.Release();
}

// Effects in the in-between picture (Retroid series 26.10.2026 at the merchant's brazier): a flame
// shows a new picture of its animation every game picture (another texture), and its glow is a 2D
// square the game lays over it in screen units. The flame moves 600 units at 3000 (texture red in
// A, blue in B): halfway, blue. The glow moves 60 screen units: halfway. A 2D square that jumps
// 220 units (more than a quarter of the picture), and one that grows from 60 to 100 units wide
// (turned or resized: r224's copy of the picture laid over itself), stand where they are in B.
std::vector<std::uint8_t> SolidRgba8(int r, int g, int b) {
  std::vector<std::uint8_t> tex(64);
  for (int i = 0; i < 16; i++) {
    tex[i * 2] = 255;
    tex[i * 2 + 1] = static_cast<std::uint8_t>(r);
    tex[32 + i * 2] = static_cast<std::uint8_t>(g);
    tex[32 + i * 2 + 1] = static_cast<std::uint8_t>(b);
  }
  return tex;
}

void EffectsScene(Builder &b, std::uint32_t sequence, bool second, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  // 2D (the orthographic projection of Baseline): the glow, unit square scaled by its matrix
  auto square = [&](float x, float w, unsigned layers) {
    b.XfF(0, {w, 0, 0, x, 0, 40.0f, 0, 244.0f, 0, 0, 1, 0});
    std::vector<std::uint8_t> v;
    for (unsigned l = 0; l < layers; l++) {
      V(v, 0, 0, -0.5f, kGreen);
      V(v, 1, 0, -0.5f, kGreen);
      V(v, 1, 1, -0.5f, kGreen);
      V(v, 0, 1, -0.5f, kGreen);
    }
    b.Draw(RE4_GX_V_COLOR0, 16, v, layers * 4, 0x80);
  };
  square(second ? 160.0f : 100.0f, 60.0f, 1);
  square(second ? 520.0f : 300.0f, 60.0f, 2);
  square(20.0f, second ? 100.0f : 60.0f, 3);
  // 3D: the flame, a textured square with a matrix of its own
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  b.Bp(0x00, 1u | 1u << 4);
  b.Xf(0x103F, {1});
  b.Xf(0x1040, {5u << 7});
  b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
  b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
  b.Bp(0x80, 0);
  b.Bp(0x84, 0);
  b.Bp(0x88, 3 | 3 << 10 | 6u << 20);
  b.Bp(0x30, 3);
  b.Bp(0x31, 3);
  if (second)
    b.Texture(0, 0x2222, 0, 0x120000, 4, 4, 6, SolidRgba8(0, 0, 200));
  else
    b.Texture(0, 0x1111, 0, 0x110000, 4, 4, 6, SolidRgba8(200, 0, 0));
  // (on screen row 400, below the 2D squares of row 264)
  b.XfF(0, {1, 0, 0, second ? 600.0f : 0.0f, 0, 1, 0, (264.0f - 400.0f) / 264.0f * 3000.0f, 0, 0, 1, -3000.0f});
  std::vector<std::uint8_t> v;
  VT(v, -100.0f, -100.0f, 0.0f, kWhite, 0, 0);
  VT(v, 100.0f, -100.0f, 0.0f, kWhite, 1, 0);
  VT(v, 100.0f, 100.0f, 0.0f, kWhite, 1, 1);
  VT(v, -100.0f, 100.0f, 0.0f, kWhite, 0, 1);
  b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationEffects() {
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb;
  Re4GxChunk a, b;
  EffectsScene(ba, 1, false, &a);
  EffectsScene(bb, 2, true, &b);
  r.Run(a);
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 3000.0f * 320.0f)); };
  const GLuint xfbA = r.Xfb(nullptr, nullptr);
  const auto flameA = TexPixel(xfbA, px(0.0f), 400), glowA = TexPixel(xfbA, 105, 264);
  Check(Near(flameA, 200, 0, 0, 3) && Near(glowA, 0, 255, 0), "effects: picture A (the flame red, the glow at 100)");
  if (!Near(flameA, 200, 0, 0, 3) || !Near(glowA, 0, 255, 0))
    Print("flameA", flameA), Print("glowA", glowA);
  Check(r.Interpolate(a, b, 0.5f), "effects: the in-between picture is shown");
  const GLuint xfb = r.Xfb(nullptr, nullptr);
  // the flame: halfway (300), with B's picture of the animation; not where it is in B (600)
  const auto flame = TexPixel(xfb, px(300.0f), 400), flameB = TexPixel(xfb, px(600.0f), 400);
  Check(Near(flame, 0, 0, 200, 3) && Near(flameB, 0, 0, 0), "effects: a flame with a new texture goes halfway");
  // the glow: 130..190 between 100..160 and 160..220
  const auto glow = TexPixel(xfb, 135, 264), glowB = TexPixel(xfb, 215, 264);
  Check(Near(glow, 0, 255, 0) && Near(glowB, 0, 0, 0), "effects: a 2D glow that moved a little goes halfway");
  // the jump of 220: where it is in B (520..580), not halfway (410..470)
  const auto jump = TexPixel(xfb, 550, 264), jumpMid = TexPixel(xfb, 440, 264);
  Check(Near(jump, 0, 255, 0) && Near(jumpMid, 0, 0, 0), "effects: a 2D square that jumped stands where it is in B");
  // the one that grew: 20..120 as in B, not 20..100
  const auto grown = TexPixel(xfb, 110, 264);
  Check(Near(grown, 0, 255, 0), "effects: a 2D square that changed its size stands as in B");
  if (!Near(grown, 0, 255, 0))
    Print("grown", grown);
  if (!Near(flame, 0, 0, 200, 3) || !Near(flameB, 0, 0, 0) || !Near(glow, 0, 255, 0) || !Near(glowB, 0, 0, 0) ||
      !Near(jump, 0, 255, 0) || !Near(jumpMid, 0, 0, 0))
    Print("flame", flame), Print("flameB", flameB), Print("glow", glow), Print("glowB", glowB), Print("jump", jump),
        Print("jumpMid", jumpMid);
  Check(glGetError() == GL_NO_ERROR, "effects: no GL error");
  r.Release();
}

void TestInterpolation() {
  std::string error;
  re4gx::GxRenderer r, plain;
  Require(r.Init(1, &error), error.c_str());
  Require(plain.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb, bc;
  Re4GxChunk a, b, c;
  InterpScene(ba, 1, -50.0f, 430.0f, 0.0f, &a);
  InterpScene(bb, 2, 50.0f, 450.0f, 0.0f, &b);
  InterpScene(bc, 3, 50.0f, 450.0f, 3.14159265f / 2, &c);
  r.Run(a);
  const GLuint xfbA = r.Xfb(nullptr, nullptr);
  Check(Near(TexPixel(xfbA, 272, 264), 0, 255, 0) && Near(TexPixel(xfbA, 435, 400), 255, 0, 0),
        "interpolation: picture A (green card at 264..344, red card at 430..470)");
  const bool shown = r.Interpolate(a, b, 0.5f);
  Check(shown, "interpolation: the in-between picture of A and B is shown");
  const GLuint xfb = r.Xfb(nullptr, nullptr);
  // A covers 264..344, B 296..376, the in-between picture 280..360
  auto mid = TexPixel(xfb, 320, 264), left = TexPixel(xfb, 272, 264), right = TexPixel(xfb, 368, 264);
  Check(Near(mid, 0, 255, 0) && Near(left, 0, 0, 0) && Near(right, 0, 0, 0),
        "interpolation: the card moved by its matrix halfway (280..360)");
  // red: A 430..470, B 450..490, in between 440..480
  auto red = TexPixel(xfb, 460, 400), redA = TexPixel(xfb, 435, 400), redB = TexPixel(xfb, 485, 400);
  Check(Near(red, 255, 0, 0) && Near(redA, 0, 0, 0) && Near(redB, 0, 0, 0),
        "interpolation: the card moved by its vertices halfway (440..480)");
  if (!Near(mid, 0, 255, 0) || !Near(red, 255, 0, 0))
    Print("middle", mid), Print("left", left), Print("right", right), Print("red", red);
  const auto &st = r.Stats();
  Check(st.interpPictures == 1 && st.interpMatched == 2 && st.interpVertexBlends == 1,
        "interpolation: both draws matched, one with blended vertices");
  if (st.interpMatched != 2 || st.interpVertexBlends != 1)
    std::fprintf(stderr, "  in-between: %u pictures, %u draws, %u matched, %u vertex blends\n", st.interpPictures,
                 st.interpDraws, st.interpMatched, st.interpVertexBlends);
  // Run(B) after it: the same picture as without the in-between one
  r.Run(b);
  plain.Run(a);
  plain.Run(b);
  const auto withPx = XfbPixels(r), withoutPx = XfbPixels(plain);
  Check(withPx == withoutPx && !withPx.empty(), "interpolation: Run(B) afterwards draws B as without it");
  Check(Near(TexPixel(r.Xfb(nullptr, nullptr), 368, 264), 0, 255, 0), "interpolation: picture B (green card at 296..376)");
  // a camera that turns by 90 degrees: a cut, no in-between picture
  Check(!r.Interpolate(b, c, 0.5f) && r.Stats().interpCuts == 1, "interpolation: a turn of 90 degrees is a cut");
  r.Run(c);
  // t = 1: the in-between picture is B (within rounding)
  re4gx::GxRenderer one;
  Require(one.Init(1, &error), error.c_str());
  one.SetRecording(true);
  one.Run(a);
  Check(one.Interpolate(a, b, 1.0f), "interpolation at t = 1: shown");
  const auto onePx = XfbPixels(one);
  unsigned differ = 0;
  for (std::size_t i = 0; i < onePx.size() && i < withoutPx.size(); i++)
    differ += std::abs(static_cast<int>(onePx[i]) - static_cast<int>(withoutPx[i])) > 2;
  Check(onePx.size() == withoutPx.size() && differ == 0, "interpolation at t = 1: the picture of B");
  // Frame skip with 60 FPS (26.10.2026): a chunk may be left out while the draws are noted; the
  // chunk after it gets no in-between picture
  {
    re4gx::GxRenderer sk;
    Require(sk.Init(1, &error), error.c_str());
    sk.SetRecording(true);
    Builder bd;
    Re4GxChunk d;
    InterpScene(bd, 3, 50.0f, 450.0f, 0.0f, &d);
    sk.Run(a);
    Check(sk.CanSkip(b, d), "frame skip with 60 FPS: a chunk may be left out while the draws are noted");
    sk.Skip(b);
    Check(!sk.Interpolate(b, d, 0.5f), "frame skip with 60 FPS: no in-between picture after a chunk left out");
    sk.Run(d);
    Check(sk.Interpolate(d, d, 0.5f) == false && glGetError() == GL_NO_ERROR, "frame skip with 60 FPS: no GL error");
    sk.Release();
  }
  // A menu screen of the game (RE4_GX_CHUNK_STILL, 26.10.2026: the load screen at the typewriter
  // flickered while its list scrolled): no in-between picture into it, within it or out of it
  {
    re4gx::GxRenderer st;
    Require(st.Init(1, &error), error.c_str());
    st.SetRecording(true);
    Re4GxChunk menuB = b, menuC = b, out = b;
    menuB.flags |= RE4_GX_CHUNK_STILL;
    menuC.flags |= RE4_GX_CHUNK_STILL;
    menuC.sequence = 3;
    out.sequence = 4;
    st.Run(a);
    Check(!st.Interpolate(a, menuB, 0.5f), "menu screen: no in-between picture into it");
    st.Run(menuB);
    Check(!st.Interpolate(menuB, menuC, 0.5f), "menu screen: no in-between picture within it");
    st.Run(menuC);
    Check(!st.Interpolate(menuC, out, 0.5f), "menu screen: no in-between picture out of it");
    st.Run(out);
    Builder be;
    Re4GxChunk e;
    InterpScene(be, 5, 50.0f, 450.0f, 0.0f, &e);
    Check(st.Interpolate(out, e, 0.5f) && st.Stats().interpPictures == 1,
          "menu screen: in-between pictures again after it");
    Check(glGetError() == GL_NO_ERROR, "menu screen: no GL error");
    st.Release();
  }
  // not recorded, or not the next chunk: nothing drawn
  Check(!plain.Interpolate(a, b, 0.5f), "interpolation: nothing without notes of the picture before");
  Check(!one.Interpolate(a, c, 0.5f), "interpolation: nothing between chunks that do not follow each other");
  Check(glGetError() == GL_NO_ERROR, "interpolation: no GL error");
  r.Release();
  plain.Release();
  one.Release();
}

// 60 FPS: the lights in the in-between picture move with the camera (tester's report 24.10.2026,
// light pools that swam). A white card 1000 units away with its normal to the camera, lit by one
// light 100 units in front of it and 100 to the right (diffuse, no attenuation: cos 45 degrees at
// the card's centre). The camera moves by dx -50 (A) and +50 (B); the game loads the light in view
// space, so it moves by dx too. In between (dx 0) the card's centre is lit as in A and B, not with
// B's light 50 units further right (cos 56 degrees: about a fifth darker).
void LitScene(Builder &b, std::uint32_t sequence, float dx, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  b.Xf(0x100A, {0x000000FFu});                                 // ambient 0: black
  b.Xf(0x100E, {1u | 1u << 1 | 1u << 2 | 2u << 7 | 2u << 9}); // vertex material, lighting, light 0, clamp
  std::vector<std::uint32_t> light(16, 0);
  light[3] = 0xFFFFFFFFu;
  light[4] = Bits(1.0f);
  light[7] = Bits(1.0f);
  light[10] = Bits(100.0f + dx);
  light[12] = Bits(-900.0f);
  b.Xf(0x600, light);
  b.XfF(0, {1, 0, 0, dx, 0, 1, 0, 0, 0, 0, 1, 0});
  std::vector<std::uint8_t> v;
  for (int i = 0; i < 4; i++) {
    const float p[3] = {i == 1 || i == 2 ? 125.0f : -125.0f, i >= 2 ? -125.0f : 125.0f, -1000.0f};
    const float nrm[3] = {0, 0, 1};
    v.insert(v.end(), reinterpret_cast<const std::uint8_t *>(p), reinterpret_cast<const std::uint8_t *>(p) + 12);
    v.insert(v.end(), reinterpret_cast<const std::uint8_t *>(nrm), reinterpret_cast<const std::uint8_t *>(nrm) + 12);
    v.insert(v.end(), kWhite, kWhite + 4);
  }
  b.Draw(RE4_GX_V_NORMAL | RE4_GX_V_COLOR0, 28, v, 4, 0x80);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolatedLights() {
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb;
  Re4GxChunk a, b;
  LitScene(ba, 1, -50.0f, &a);
  LitScene(bb, 2, 50.0f, &b);
  r.Run(a);
  // dx -50: 16 pixels left of the centre
  const auto litA = TexPixel(r.Xfb(nullptr, nullptr), 304, 264);
  const int g = litA.empty() ? 0 : litA[1];
  Check(g > 60 && g < 220, "interpolated lights: picture A half lit at the card's centre");
  Check(r.Interpolate(a, b, 0.5f), "interpolated lights: the in-between picture is shown");
  const auto litMid = TexPixel(r.Xfb(nullptr, nullptr), 320, 264);
  Check(Near(litMid, g, g, g, 3), "interpolated lights: the light moved with the camera (lit as in A)");
  if (!Near(litMid, g, g, g, 3))
    Print("A", litA), Print("in between", litMid);
  // the next picture keeps its own lights
  r.Run(b);
  Check(Near(TexPixel(r.Xfb(nullptr, nullptr), 336, 264), g, g, g, 3),
        "interpolated lights: picture B afterwards with its own light");
  Check(glGetError() == GL_NO_ERROR, "interpolated lights: no GL error");
  r.Release();
}

// The whole EFB (scale 1) as RGBA bytes.
std::vector<std::uint8_t> Efb(re4gx::GxRenderer &r) {
  glBindFramebuffer(GL_FRAMEBUFFER, gReadFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.EfbColor(), 0);
  std::vector<std::uint8_t> p(640 * 528 * 4);
  glReadPixels(0, 0, 640, 528, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}

// 60 quads, each with a random TEV setup of one to four stages on registers, konstant and
// rasterized colours (no textures): inputs above 127, every bias, scale, subtraction, clamping
// and compare mode; every other row with linear fog.
std::vector<std::uint8_t> TevMathScene(std::uint32_t seed, unsigned *shaderErrors) {
  re4gx::GxRenderer r;
  std::string error;
  Require(r.Init(1, &error), error.c_str());
  std::mt19937 rng(seed);
  const auto reg = [](int lo, int hi, bool konst) {
    return (static_cast<std::uint32_t>(lo) & 0x7FF) | (static_cast<std::uint32_t>(hi) & 0x7FF) << 12 |
           (konst ? 1u << 23 : 0u);
  };
  const auto fogWord = [](float f) {
    const std::uint32_t v = Bits(f);
    return ((v >> 12) & 0x7FF) | ((v >> 23) & 0xFF) << 11 | (v >> 31) << 19;
  };
  static const unsigned colorIn[14] = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15};
  static const unsigned alphaIn[7] = {0, 1, 2, 3, 5, 6, 7};
  static const unsigned konstSel[20] = {0, 1, 2, 3, 4, 5, 6, 7, 0x0C, 0x0D, 0x0E, 0x0F,
                                        0x10, 0x11, 0x12, 0x13, 0x14, 0x19, 0x1E, 0x1F};
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlack);
  for (int q = 0; q < 60; q++) {
    const unsigned stages = 1 + rng() % 4;
    b.Bp(0x00, 1u << 4 | (stages - 1) << 10);
    for (unsigned st = 0; st < stages; st++) {
      const auto ci = [&] { return colorIn[rng() % 14]; };
      const auto ai = [&] { return alphaIn[rng() % 7]; };
      const unsigned common = (rng() % 4) << 16 | (rng() % 2) << 18 | (rng() % 2) << 19 | (rng() % 4) << 20 |
                              (st + 1 == stages ? 0u : (rng() % 4) << 22);
      b.Bp(0xC0 + 2 * st, ci() | ci() << 4 | ci() << 8 | ci() << 12 | common);
      const unsigned commonA = (rng() % 4) << 16 | (rng() % 2) << 18 | (rng() % 2) << 19 | (rng() % 4) << 20 |
                               (st + 1 == stages ? 0u : (rng() % 4) << 22);
      b.Bp(0xC1 + 2 * st, ai() << 4 | ai() << 7 | ai() << 10 | ai() << 13 | commonA);
    }
    for (unsigned i = 0; i < 4; i++) {
      b.Bp(0x28 + i, 0); // no textures, colour channel 0
      const unsigned k0 = konstSel[rng() % 20], k0a = konstSel[rng() % 20], k1 = konstSel[rng() % 20],
                     k1a = konstSel[rng() % 20];
      b.Bp(0xF6 + 2 * i, (0 | 1 << 2) | k0 << 4 | k0a << 9 | k1 << 14 | k1a << 19);
      b.Bp(0xF7 + 2 * i, (2 | 3 << 2) | (konstSel[rng() % 20]) << 4 | (konstSel[rng() % 20]) << 9 |
                             (konstSel[rng() % 20]) << 14 | (konstSel[rng() % 20]) << 19);
    }
    for (int k = 0; k < 4; k++) {
      // registers PREV, C0-C2 signed (-1024..1023), konstant colours bytes
      b.Bp(0xE0 + 2 * k, reg(static_cast<int>(rng() % 2048) - 1024, static_cast<int>(rng() % 2048) - 1024, false));
      b.Bp(0xE1 + 2 * k, reg(static_cast<int>(rng() % 2048) - 1024, static_cast<int>(rng() % 2048) - 1024, false));
      b.Bp(0xE0 + 2 * k, reg(rng() % 256, rng() % 256, true));
      b.Bp(0xE1 + 2 * k, reg(rng() % 256, rng() % 256, true));
    }
    if ((q / 10) % 2) {
      b.Bp(0xEE, fogWord(1.0f));
      b.Bp(0xF1, fogWord(0.0f) | 1u << 20 | 2u << 21); // orthographic, linear
      b.Bp(0xF2, rng() & 0xFFFFFF);
    } else {
      b.Bp(0xF1, 0);
    }
    const std::uint8_t c[4] = {static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()),
                               static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng())};
    const float x = static_cast<float>((q % 10) * 64), y = static_cast<float>((q / 10) * 64);
    Quad(b, x, y, x + 60, y + 60, -0.1f - 0.013f * static_cast<float>(q), c);
  }
  r.Run(b.Chunk());
  std::vector<std::uint8_t> p = Efb(r);
  *shaderErrors += r.Stats().shaderErrors;
  r.Release();
  return p;
}

// The ways of writing the TEV's lerp (gx_gl_shader.h SetTevMath). The Galaxy A36 (Adreno 710)
// computed a << 8 in 16 bits: written so on purpose, the movie check gives exactly that phone's six
// wrong pixels (report 22.10.2026 19:30), and the check then picks the way without the shift. The
// three real ways give the same pictures for random TEV setups.
void TestTevMath() {
  std::string detail, error;
  re4gx::SetTevMath(re4gx::kTevMathBroken16);
  const int bad = re4gx::MovieColourPixels(&detail, &error);
  Check(bad == 6, "TEV math: a << 8 in 16 bits gives six wrong movie pixels, as on the Galaxy A36");
  static const char *a36[6] = {
      "4 stages, Y 128 U 128 V 128: 0 131 0 0, want 131 131 132 0",
      "4 stages, Y 200 U 128 V 128: 0 215 0 0, want 215 215 216 0",
      "4 stages, Y 235 U 128 V 128: 0 255 0 0, want 255 255 255 0",
      "4 stages, Y 180 U 90 V 200: 0 148 116 0, want 255 148 116 0",
      "4 stages, Y 100 U 200 V 60: 0 125 0 0, want 0 125 244 0",
      "4 stages, Y 235 U 240 V 240: 0 121 0 0, want 255 121 255 0"};
  bool same = true;
  for (const char *w : a36)
    same = same && detail.find(w) != std::string::npos;
  Check(same, "TEV math: the same values as the Galaxy A36's report");
  if (!same)
    std::fprintf(stderr, "  emulated: %s\n", detail.c_str());
  int wrong = -1;
  const std::string line = re4gx::MovieColourCheck(&wrong);
  std::fprintf(stderr, "  emulated A36: %s\n", line.c_str());
  Check(wrong == 0 && re4gx::GetTevMath() == re4gx::kTevMathNoShift &&
            line.find("in use: the lerp without the shift by 8 (the movies right)") != std::string::npos,
        "TEV math: the check picks the lerp without the shift");
  for (int mode : {re4gx::kTevMathShift, re4gx::kTevMathNoShift, re4gx::kTevMathFloat}) {
    re4gx::SetTevMath(mode);
    Check(re4gx::MovieColourPixels(nullptr, &error) == 0, "TEV math: the movie colours right in each way");
  }
  for (std::uint32_t seed : {1u, 2u, 3u, 4u}) {
    unsigned errors = 0;
    std::vector<std::uint8_t> picture[4];
    for (int mode = re4gx::kTevMathShift; mode <= re4gx::kTevMathBroken16; mode++) {
      re4gx::SetTevMath(mode);
      picture[mode] = TevMathScene(seed, &errors);
    }
    Check(errors == 0, "TEV math: every shader compiles in each way");
    // the scene draws, and the comparison would see the A36's way
    int lit = 0, broken = 0;
    for (std::size_t i = 0; i < picture[0].size(); i += 4) {
      lit += picture[0][i] | picture[0][i + 1] | picture[0][i + 2] ? 1 : 0;
      broken += std::memcmp(&picture[0][i], &picture[re4gx::kTevMathBroken16][i], 4) ? 1 : 0;
    }
    Check(lit > 60 * 60 * 20 && broken > 0, "TEV math: the random scene draws, the 16-bit way differs");
    if (seed == 1)
      std::fprintf(stderr, "  TEV math scene: %d lit pixels, %d differ with the A36's way\n", lit, broken);
    for (int mode = re4gx::kTevMathNoShift; mode <= re4gx::kTevMathFloat; mode++) {
      int differ = 0;
      for (std::size_t i = 0; i < picture[0].size(); i++)
        differ += picture[mode][i] != picture[0][i];
      if (differ)
        std::fprintf(stderr, "  seed %u, %s: %d bytes differ\n", seed, re4gx::TevMathName(mode), differ);
      Check(differ == 0, "TEV math: random TEV setups, the same picture in each way");
    }
  }
  re4gx::SetTevMath(re4gx::kTevMathShift);
}

// The programs ahead of their first draw (GxRenderer::Prewarm, Retroid report 22.10.2026 23:42):
// a renderer keeps each program's key next to its binary; a new renderer makes them all before any
// draw, and the same draws then make none. A key under an old name (the sources changed) is
// linked anew and its old files go.
namespace {

std::vector<std::string> CacheFiles(const std::string &dir, const char *ending) {
  std::vector<std::string> names;
  if (DIR *d = opendir(dir.c_str())) {
    while (const dirent *e = readdir(d)) {
      const std::string name = e->d_name;
      if (name.size() > 4 && name.compare(name.size() - 4, 4, ending) == 0)
        names.push_back(name);
    }
    closedir(d);
  }
  return names;
}

} // namespace

// The shader programs made ahead (decision of the project owner, 23.10.2026): the render thread's
// Prewarm only loads binaries; WarmProgramCache, on a thread with a context of its own, links what
// the cache's keys and the app's list (a key list file) still lack and removes old files.
void TestPrewarm() {
  char folder[] = "/tmp/re4-prewarm-XXXXXX";
  Require(mkdtemp(folder) != nullptr, "prewarm: a folder for the program cache");
  const std::string dir = folder;
  std::string error;
  unsigned made = 0;
  {
    re4gx::GxRenderer r;
    Require(r.Init(1, &error), error.c_str());
    Check(r.SetProgramCache(dir), "prewarm: the driver keeps program binaries");
    Check(!r.Prewarm(100.0), "prewarm: an empty cache, nothing to make");
    TestShaderVariety(r);
    made = r.Stats().programs;
    r.Release();
  }
  const auto bins = CacheFiles(dir, ".bin"), keys = CacheFiles(dir, ".key");
  Check(made > 10 && bins.size() == made && keys.size() == made, "prewarm: a binary and a key per program");
  std::fprintf(stderr, "  prewarm: %u programs, %zu binaries, %zu keys\n", made, bins.size(), keys.size());
  // a key under a name its sources no longer have (one of them, its alpha test changed), and the
  // app's list with a key this device never made (MSAA's centroid in it: the list's keys are for
  // every MSAA setting)
  re4gx::ShaderKey old{}, fresh{};
  {
    FILE *f = std::fopen((dir + "/" + keys[0]).c_str(), "rb");
    char head[8];
    Require(f && std::fread(head, 8, 1, f) == 1 && std::fread(&old, sizeof(old), 1, f) == 1, "prewarm: a key file");
    std::fclose(f);
    fresh = old;
    old.alphaTest ^= 0x40;  // the logic of the two comparisons
    f = std::fopen((dir + "/0123456789abcdef.key").c_str(), "wb");
    Require(f && std::fwrite(head, 8, 1, f) == 1 && std::fwrite(&old, sizeof(old), 1, f) == 1, "prewarm: a key file");
    std::fclose(f);
    f = std::fopen((dir + "/0123456789abcdef.bin").c_str(), "wb");
    Require(f && std::fwrite("RE4P", 4, 1, f) == 1, "prewarm: a binary");
    std::fclose(f);
    fresh.alphaTest ^= 0x80;
    fresh.centroid = 1;
    f = std::fopen((dir + ".keys").c_str(), "wb");
    const std::uint32_t listHead[3] = {0x4C344552u, static_cast<std::uint32_t>(sizeof(fresh)), 1};  // "RE4L"
    Require(f && std::fwrite(listHead, sizeof(listHead), 1, f) == 1 && std::fwrite(&fresh, sizeof(fresh), 1, f) == 1,
            "prewarm: a key list");
    std::fclose(f);
  }
  const std::string list = dir + ".keys";
  {
    // without WarmProgramCache: the cached ones only, nothing linked on the render thread
    re4gx::GxRenderer r;
    Require(r.Init(1, &error), error.c_str());
    r.SetProgramCache(dir);
    r.SetProgramList(list);
    int calls = 0;
    while (r.Prewarm(0.0) && calls < 10000)
      calls++;
    const re4gx::GxStats s = r.Stats();
    Check(s.programsAhead == made && s.programsLoaded == made && s.programs == made,
          "prewarm: the cached programs before the first draw, nothing linked on the render thread");
    Check(calls + 1 >= static_cast<int>(made), "prewarm: one program per call with no time to spend");
    r.Release();
  }
  {
    re4gx::GxWarmResult w;
    bool warmingSeen = true;
    std::thread([&] {
      EGLContext own = NewContext();
      if (own == EGL_NO_CONTEXT || !eglMakeCurrent(gDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, own))
        return;
      w = re4gx::WarmProgramCache(dir, list, 0, nullptr);
      warmingSeen = re4gx::ProgramCacheWarming();
      eglMakeCurrent(gDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      eglDestroyContext(gDisplay, own);
    }).join();
    std::fprintf(stderr, "  warm: %u keys (%u from the list), %u linked, %u had a binary, %u failed, %u old removed\n",
                 w.keys, w.listed, w.linked, w.had, w.failed, w.stale);
    Check(w.keys == made + 2 && w.listed == 1 && w.linked == 2 && w.had == made && w.failed == 0 && w.stale == 1,
          "warm: the old key and the list's key linked on a thread of its own, the rest had binaries");
    Check(!warmingSeen, "warm: not warming once it returned");
  }
  Check(access((dir + "/0123456789abcdef.key").c_str(), F_OK) != 0 &&
            access((dir + "/0123456789abcdef.bin").c_str(), F_OK) != 0,
        "warm: the old name's files removed");
  Check(CacheFiles(dir, ".bin").size() == made + 2 && CacheFiles(dir, ".key").size() == made + 2,
        "warm: the programs linked anew kept under their names");
  {
    re4gx::GxRenderer r;
    Require(r.Init(1, &error), error.c_str());
    r.SetProgramCache(dir);
    r.SetProgramList(list);
    while (r.Prewarm(1000.0)) {
    }
    const re4gx::GxStats s = r.Stats();
    Check(s.programsAhead == made + 2 && s.programsLoaded == made + 2 && s.programs == made + 2,
          "prewarm: after the warm every program from the cache, the list's one too");
    TestShaderVariety(r);
    Check(r.Stats().programs == made + 2, "prewarm: the same draws make no program");
    r.Release();
  }
  {
    // MSAA: the shaders sample at a covered spot (centroid), the single-sampled keys stay as they are
    re4gx::GxRenderer r;
    re4gx::GxOptions o;
    o.samples = 4;
    r.SetOptions(o);
    Require(r.Init(1, &error), error.c_str());
    r.SetProgramCache(dir);
    r.SetProgramList(list);
    while (r.Prewarm(1000.0)) {
    }
    Check(r.Samples() == 1 || r.Stats().programsAhead == 0, "prewarm: no single-sampled program for MSAA");
    r.Release();
  }
  // the error report's list: every key of the cache once
  Check(re4gx::WriteProgramKeyList(dir, dir + "-report.keys") == made + 2, "key list: the cache's keys");
  {
    FILE *f = std::fopen((dir + "-report.keys").c_str(), "rb");
    std::uint32_t head[3] = {};
    Check(f && std::fread(head, sizeof(head), 1, f) == 1 && head[0] == 0x4C344552u && head[1] == sizeof(fresh) &&
              head[2] == made + 2,
          "key list: RE4L, the key's size, the count");
    if (f)
      std::fclose(f);
  }
  std::remove(list.c_str());
  std::remove((dir + "-report.keys").c_str());
  for (const char *ending : {".bin", ".key"})
    for (const std::string &name : CacheFiles(dir, ending))
      std::remove((dir + "/" + name).c_str());
  rmdir(folder);
}

int main() {
  Context();
  glGenFramebuffers(1, &gReadFbo);
  TestDecoder();
  // the GPU check of the movie colours as the app runs it at the start (gx_selfcheck.cpp)
  {
    int wrong = -1;
    const std::string line = re4gx::MovieColourCheck(&wrong);
    std::fprintf(stderr, "%s\n", line.c_str());
    Check(wrong == 0, "GPU check: the movie colours as the GameCube computes them");
  }
  TestTevMath();
  TestPrewarm();
  TestRescale();
  TestAutoLevel();
  TestHandOverCopies();
  // twice: as the app runs it, and with the copy targets, the blur and the focus painted magenta
  // where the GPU may forget them (GPU step b): every picture must stay the same
  for (int poison = 0; poison < 2; poison++) {
    if (poison)
      std::fprintf(stderr, "again with forgotten contents painted magenta:\n");
    re4gx::GxRenderer renderer;
    re4gx::GxOptions o;
    o.invalidate = poison != 0;
    o.poison = poison != 0;
    renderer.SetOptions(o);
    std::string error;
    Require(renderer.Init(1, &error), error.c_str());
    TestBasics(renderer);
    TestTexture(renderer);
    TestTev(renderer);
    TestLighting(renderer);
    TestCopyAndPalette(renderer);
    TestLetterboxCopy(renderer, false, true);
    TestLetterboxCopy(renderer, false, false);
    TestShownRowsCopy(renderer, false, true);
    TestShownRowsCopy(renderer, false, false);
    TestDepthMap(renderer, false);
    TestFxaa(renderer);
    TestFrameSkip(renderer);
    TestFog(renderer);
    TestShaderVariety(renderer);
    TestCopyFilter(renderer);
    TestAutofocus(renderer);
    TestPassBreak(renderer, "single-sampled");
    if (!poison)
      TestUniformStream(renderer, "single-sampled");
    Check(glGetError() == GL_NO_ERROR, "no GL error");
    renderer.Release();
    {
      // the depth of field as before GPU step e: the blur at the full resolution
      re4gx::GxRenderer full;
      o.halfDof = false;
      full.SetOptions(o);
      Require(full.Init(1, &error), error.c_str());
      TestAutofocus(full);
      Check(glGetError() == GL_NO_ERROR, "full-resolution depth of field: no GL error");
      full.Release();
    }
    TestOptions(poison != 0, true);
  }
  TestInterpolation();
  TestInterpolationParticles();
  TestInterpolationLooks();
  TestInterpolationEffects();
  TestInterpolatedLights();
  // MSAA in its own buffers (the devices without the extensions, RE4_GX_MSAA_TILES=0)
  TestOptions(false, false);
  // the depth of field's blur at the full resolution as before GPU step e (GxOptions::halfDof)
  TestOptions(false, false, false);
  // MSAA on the tiles on a GPU that loses colour and depth at every pass's end (the Adreno 650
  // lost the depth): the EFB drawn back (GPU step d2) keeps every picture; both resolutions of
  // the depth of field (its full one draws back into the EFB on the tiles)
  TestOptions(false, true, true, 1);
  TestOptions(true, true, false, 1);
  // ... and one that never stores the resolved depth either (the Adreno 650, it seems): the
  // self-test at Init takes own buffers, every picture right
  TestOptions(false, true, true, 2);
  std::fprintf(stderr, "gx_gl_check: %d failed\n", gFailures);
  return gFailures ? 1 : 0;
}
