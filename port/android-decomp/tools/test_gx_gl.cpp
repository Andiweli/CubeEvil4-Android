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
//   - merged draw calls (RE4_GX_MERGE): draws that differ only in their
//     matrices and material colours in few calls, the picture unchanged
#include "gx_bench_steps.h"
#include "gx_auto_level.h"
#include "picture_cadence.h"
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

#include <chrono>
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

// The console's pixel centre (7/12 of a pixel, Dolphin's correction; the rifle scope's horizontal line
// at 1x, 29.10.2026): a line 0.94 pixels thick on the edge between rows 223 and 224 (and columns 319
// and 320) covers row 223 (column 319) only, as the GameCube draws it; a quad whose left edge is at
// 10.55 covers column 10 (its sample at 10.583), one at 50.65 not column 50.
void TestPixelCentre(re4gx::GxRenderer &r) {
  Builder b;
  Baseline(b);
  b.flags |= RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  Quad(b, 100, 223.53f, 300, 224.47f, 0, kGreen);
  Quad(b, 319.53f, 300, 320.47f, 400, 0, kRed);
  Quad(b, 10.55f, 440, 40, 460, 0, kBlue);
  Quad(b, 50.65f, 440, 80, 460, 0, kWhite);
  DisplayCopy(b, false, kBlack);
  r.Run(b.Chunk());
  const auto row = EfbPixel(r, 200, 223), above = EfbPixel(r, 200, 222), below = EfbPixel(r, 200, 224);
  Check(Near(row, 0, 255, 0) && Near(above, 0, 0, 0) && Near(below, 0, 0, 0),
        "pixel centre: a horizontal line thinner than a pixel on a row edge drawn in one row (the scope)");
  if (!Near(row, 0, 255, 0))
    Print("row 223", row), Print("row 224", below);
  Check(Near(EfbPixel(r, 319, 350), 255, 0, 0) && Near(EfbPixel(r, 320, 350), 0, 0, 0),
        "pixel centre: a vertical line thinner than a pixel on a column edge drawn in one column");
  Check(Near(EfbPixel(r, 10, 450), 0, 0, 255), "pixel centre: an edge at 10.55 covers column 10 (sample at 10.583)");
  Check(Near(EfbPixel(r, 50, 450), 0, 0, 0) && Near(EfbPixel(r, 51, 450), 255, 255, 255),
        "pixel centre: an edge at 50.65 leaves column 50");
}

// "4:3 (exact)" (GxRenderer::SetWiden, 29.10.2026): a flat draw over the whole screen is drawn 15/14
// wider around the screen's centre (columns 200..300 of 640: 191.4..298.6); a flat draw into a part
// of the EFB (a viewport 200 wide, a picture for a texture) stays as sent, and so does everything at 1.
void TestWiden(re4gx::GxRenderer &r) {
  auto run = [&](float widen, bool part) {
    r.SetWiden(widen);
    Builder b;
    Baseline(b);
    b.flags |= RE4_GX_CHUNK_XFB;
    DisplayCopy(b, true, kBlack);
    if (part)
      b.XfF(0x101A, {100.0f, -264.0f, 16777215.0f, 342.0f + 100.0f, 342.0f + 264.0f, 16777215.0f});
    Quad(b, 200, 100, 300, 150, 0, kGreen);
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
  };
  run(15.0f / 14.0f, false);
  Check(Near(EfbPixel(r, 192, 120), 0, 255, 0) && Near(EfbPixel(r, 190, 120), 0, 0, 0) &&
            Near(EfbPixel(r, 297, 120), 0, 255, 0) && Near(EfbPixel(r, 300, 120), 0, 0, 0),
        "4:3 exact: a flat draw over the whole screen is 15/14 wider around the centre");
  run(1.0f, false);
  Check(Near(EfbPixel(r, 195, 120), 0, 0, 0) && Near(EfbPixel(r, 200, 120), 0, 255, 0) &&
            Near(EfbPixel(r, 299, 120), 0, 255, 0) && Near(EfbPixel(r, 300, 120), 0, 0, 0),
        "4:3 exact off: the flat draw as sent");
  run(1.0f, true);
  std::vector<std::vector<std::uint8_t>> asSent;
  for (int x = 0; x < 640; x += 4)
    asSent.push_back(EfbPixel(r, x, 120));
  run(15.0f / 14.0f, true);
  bool same = true;
  for (int x = 0, i = 0; x < 640; x += 4, i++)
    same = same && EfbPixel(r, x, 120) == asSent[static_cast<std::size_t>(i)];
  Check(same, "4:3 exact: a flat draw into a part of the EFB stays as sent");
  r.SetWiden(1.0f);
}

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

#ifdef RE4_GX_VULKAN
// The Vulkan renderer (port/docs/vulkan-bauplan.md E4): the rescale and the copies handed over, read through
// ReadXfb (rows from the top, pixels of the renderer's scale). They run where a Vulkan device starts (lavapipe:
// port/tools/vk_tools_linux.sh, ~/vk-tools/env.sh); elsewhere they are left out.
std::vector<std::uint8_t> XfbPixel(re4gx::GxRenderer &r, int x, int y, unsigned *width = nullptr,
                                   unsigned *height = nullptr) {
  std::vector<std::uint8_t> rgba;
  unsigned w = 0, h = 0;
  const bool read = r.ReadXfb(&rgba, &w, &h);
  if (width)
    *width = w;
  if (height)
    *height = h;
  if (!read || x < 0 || y < 0 || static_cast<unsigned>(x) >= w || static_cast<unsigned>(y) >= h)
    return {0, 0, 0, 0};
  const std::size_t at = (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
  return {rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

bool VulkanStarts() {
  re4gx::GxRenderer probe(re4gx::GxApi::kVulkan);
  std::string error = "not in this build";
  if (probe.Api() != re4gx::GxApi::kVulkan || !probe.Init(1, &error)) {
    std::fprintf(stderr, "Vulkan checks left out: %s\n", error.c_str());
    return false;
  }
  probe.Release();
  return true;
}

void TestVkRescale() {
  re4gx::GxRenderer r(re4gx::GxApi::kVulkan);
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
  Check(Near(XfbPixel(r, 640, 640), 255, 0, 0), "Vulkan rescale: the copy drawn at scale 2");
  // whole levels and the menu's in-between ones (in halves: 2 1x, 3 1.5x, 5 2.5x, 6 3x)
  for (int halves : {2, 6, 3, 5}) {
    r.RescaleHalves(halves);
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    textured(b, false);
    r.Run(b.Chunk());
    unsigned w = 0, h = 0;
    const auto corner = XfbPixel(r, 640 * halves / 2 - 4, 528 * halves / 2 - 4, &w, &h);
    const std::string at = "Vulkan rescale to " + std::to_string(halves) + " halves";
    Check(r.Halves() == halves, (at + ": the renderer's level").c_str());
    Check(w == static_cast<unsigned>((640 * halves + 1) / 2) && h == static_cast<unsigned>((528 * halves + 1) / 2) &&
              Near(corner, 0, 0, 255),
          (at + ": the display copy has the new size").c_str());
    Check(Near(XfbPixel(r, 320 * halves / 2, 320 * halves / 2), 255, 0, 0),
          (at + ": the copy still bound, scaled along").c_str());
    Check(Near(XfbPixel(r, 100 * halves / 2, 400 * halves / 2), 0, 0, 255), (at + ": the clear colour elsewhere").c_str());
  }
  Check(r.Stats().rescales == 4, "Vulkan rescale: four rescales counted");
  r.Release();
}

// New picture options make a new renderer (core_mode.cpp): on the device both share, the copies go over at the new
// renderer's scale (as TestHandOverCopies for GL)
void TestVkHandOverCopies() {
  std::string error;
  re4gx::GxRenderer a(re4gx::GxApi::kVulkan), b(re4gx::GxApi::kVulkan);
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
  Check(b.Stats().handedCopies == 1, "Vulkan hand over: the copy goes to the new renderer");
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
  Check(b.Stats().missingTextures == 0, "Vulkan hand over: the copy is not missing in the new renderer");
  Check(Near(XfbPixel(b, 100 * 2, 100 * 2), 255, 0, 0), "Vulkan hand over: the copy drawn at the new scale (red)");
  Check(Near(XfbPixel(b, 400 * 2, 400 * 2), 0, 0, 0), "Vulkan hand over: black beside it");
  b.Release();
}

// The overlay (GxRenderer::SetOverlay: the app's minimap under Vulkan) over the presented picture: its square at
// (x, y) from the bottom left as GL's viewport, its upper half red (y up in its clip space), the picture around it
void TestVkOverlay() {
  re4gx::GxRenderer r(re4gx::GxApi::kVulkan);
  std::string error;
  Require(r.Init(1, &error), error.c_str());
  Builder b;
  Baseline(b);
  DisplayCopy(b, true, kBlue);
  DisplayCopy(b, false, kBlue);
  r.Run(b.Chunk());
  re4gx::GxOverlay o;
  auto put = [&](float x, float y) { o.verts.insert(o.verts.end(), {x, y, 1.0f, 0.0f, 0.0f, 1.0f}); };
  put(-1, 0), put(1, 0), put(1, 1), put(-1, 0), put(1, 1), put(-1, 1);  // the upper half
  o.parts = {{false, 0, 6}};
  o.x = 10;
  o.y = 20;
  o.size = 50;
  r.SetOverlay(o);
  std::vector<std::uint8_t> rgba;
  const int W = 200, H = 100;
  Require(r.ReadPresented(W, H, 0, 0, W, H, 0, 0, &rgba), "Vulkan overlay: the picture read back");
  auto at = [&](int x, int y) {  // from the top
    const std::size_t i = (static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)) * 4;
    return std::vector<std::uint8_t>{rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
  };
  // the square: columns 10..59, rows 30..79 from the top; its upper half rows 30..54
  Check(Near(at(35, 40), 255, 0, 0), "Vulkan overlay: the upper half of its square red");
  Check(Near(at(35, 70), 0, 0, 255), "Vulkan overlay: its lower half the picture (y up in the square)");
  Check(Near(at(5, 40), 0, 0, 255) && Near(at(65, 40), 0, 0, 255), "Vulkan overlay: the picture beside it");
  Check(Near(at(35, 25), 0, 0, 255), "Vulkan overlay: the picture above it (x, y from the bottom left)");
  r.SetOverlay(re4gx::GxOverlay());
  Require(r.ReadPresented(W, H, 0, 0, W, H, 0, 0, &rgba), "Vulkan overlay: read again");
  Check(Near(at(35, 40), 0, 0, 255), "Vulkan overlay: gone when the app sets none");
  r.Release();
}
#endif

// Merged draw calls (SetMerge, RE4_GX_MERGE, 30.10.2026): 24 squares, each moved by its own position
// matrix (every second one in PNMTX1, another uMtx) and coloured by its own material colour, with a
// blend mode set and back and owner tags between them as the game sends them; after square 11 a
// register stays changed (the dither bit: the picture stays, the run must end). With merging the
// squares go in a few draw calls (once Prewarm made the programs), the picture the same as without.
void TestMerge() {
  const auto build = [](Builder &b) {
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    b.Xf(0x100E, {0});  // colour 0 from the material register, no lighting
    for (unsigned i = 0; i < 24; i++) {
      const float x = static_cast<float>(i % 6) * 40.0f, y = static_cast<float>(i / 6) * 40.0f;
      const unsigned row = (i & 1) ? 3 : 0;
      b.XfF(row * 4, {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, 0});
      b.Xf(0x100C, {(40u + i * 8) << 24 | (255u - i * 9) << 16 | ((i * 37) & 255) << 8 | 255u});
      b.Bp(0x41, b.bp[0x41] | 1u << 0);  // blending on and off again (the game between two objects)
      b.Bp(0x41, b.bp[0x41] & ~1u);
      if (i % 5 == 0)
        b.Owner(0x80001000u + i, 0x80002000u, 0x80003000u);
      Quad(b, 10, 10, 30, 30, 0, kWhite);
      Re4GxDraw d;
      std::memcpy(&d, &b.ops[b.ops.size() - sizeof(d) / 4], sizeof(d));
      d.matrices[0] = (d.matrices[0] & ~0xFFu) | row;
      std::memcpy(&b.ops[b.ops.size() - sizeof(d) / 4], &d, sizeof(d));
      if (i == 11)
        b.Bp(0x41, b.bp[0x41] | 1u << 2);  // dither on: stays
    }
    b.Bp(0x41, b.bp[0x41] & ~(1u << 2));
    DisplayCopy(b, false, kBlack);
  };
  std::string error;
  re4gx::GxRenderer single, merged;
  Require(single.Init(1, &error), error.c_str());
  Require(merged.Init(1, &error), error.c_str());
  merged.SetMerge(true);
  Builder a, b, c;
  build(a);
  single.Run(a.Chunk());
  build(b);
  merged.Run(b.Chunk());
  const unsigned before = merged.Stats().mergeBatches;
  Check(merged.Prewarm(1e9) == false, "merged draws: Prewarm makes the programs the chunk wanted");
  build(c);
  const re4gx::GxStats was = merged.Stats();
  merged.Run(c.Chunk());
  const re4gx::GxStats &s = merged.Stats();
  const unsigned draws = s.draws - was.draws, taken = s.mergeTaken - was.mergeTaken,
                 calls = s.mergeBatches - was.mergeBatches;
  char what[160];
  std::snprintf(what, sizeof(what), "merged draws: none before the programs are made (%u)", before);
  Check(before == 0, what);
  std::snprintf(what, sizeof(what), "merged draws: 24 squares in %u draw calls (%u merged in %u)",
                draws - taken + calls, taken, calls);
  // runs of up to 10: 0-9, 10-11 (the dither stays after 11), 12-21, 22-23
  Check(draws == 24 && taken == 24 && calls == 4, what);
  bool same = true, right = true;
  for (unsigned i = 0; i < 24; i++) {
    const int x = static_cast<int>(i % 6) * 40 + 20, y = static_cast<int>(i / 6) * 40 + 20;
    const auto p = TexPixel(single.Xfb(nullptr, nullptr), x, y), q = TexPixel(merged.Xfb(nullptr, nullptr), x, y);
    same = same && p == q;
    right = right && Near(q, static_cast<int>(40 + i * 8), static_cast<int>(255 - i * 9), static_cast<int>((i * 37) & 255));
    for (const int dx : {-12, 12})
      same = same && TexPixel(single.Xfb(nullptr, nullptr), x + dx, y) == TexPixel(merged.Xfb(nullptr, nullptr), x + dx, y);
  }
  Check(right, "merged draws: each square at its matrix's place in its material colour");
  Check(same, "merged draws: the picture as with single draw calls");
  // off again: one by one
  merged.SetMerge(false);
  Builder e;
  build(e);
  const unsigned callsBefore = merged.Stats().mergeBatches;
  merged.Run(e.Chunk());
  Check(merged.Stats().mergeBatches == callsBefore, "merged draws: off, every draw on its own");
  Check(glGetError() == GL_NO_ERROR, "merged draws: no GL error");
  single.Release();
  merged.Release();
}

// The vertex ring (GxOptions::vertexRing, V0b of port/docs/vulkan-plan.md, 31.10.2026): 48 chunks of 300
// squares each, their places and colours different in every chunk, so the ring goes round its quarters
// many times; every 12th chunk three large draws more (180 000 vertices, 2.9 MB: more than a quarter of
// 2 MB), so it grows. Every picture as a renderer without the ring draws it (a buffer per chunk).
void TestVertexRing() {
  std::string error;
  re4gx::GxRenderer ring, plain;
  re4gx::GxOptions on, off;
  on.vertexRing = true;  // off by default (the app menu's switch)
  off.vertexRing = false;
  ring.SetOptions(on);
  plain.SetOptions(off);
  Require(ring.Init(1, &error), error.c_str());
  Require(plain.Init(1, &error), error.c_str());
  const auto build = [](Builder &b, unsigned n) {
    if (n == 0)
      Baseline(b);
    else
      b.flags = 0;
    DisplayCopy(b, true, kBlack);
    if (n % 12 == 11) {
      // three draws of 60 000 vertices of triangles without area, drawn below the squares
      std::vector<std::uint8_t> v;
      for (unsigned i = 0; i < 60000; i++)
        V(v, static_cast<float>(i % 600), static_cast<float>(i / 600), 0, kWhite);
      for (int k = 0; k < 3; k++)
        b.Draw(RE4_GX_V_COLOR0, 16, v, 60000, 0x90);
    }
    for (unsigned i = 0; i < 300; i++) {
      const float x = static_cast<float>((i * 7 + n * 13) % 40) * 16.0f, y = static_cast<float>((i + n) % 33) * 16.0f;
      const std::uint8_t c[4] = {static_cast<std::uint8_t>(i * 5 + n), static_cast<std::uint8_t>(255 - i),
                                 static_cast<std::uint8_t>(n * 9), 255};
      Quad(b, x + 2, y + 2, x + 14, y + 14, 0, c);
    }
    DisplayCopy(b, false, kBlack);
  };
  const re4gx::GxStats before = ring.Stats();
  bool same = true;
  unsigned chunks = 0;
  for (unsigned n = 0; n < 48; n++) {
    Builder a, b;
    build(a, n);
    build(b, n);
    ring.Run(a.Chunk());
    plain.Run(b.Chunk());
    chunks++;
    unsigned w = 0, h = 0;
    const GLuint ra = ring.Xfb(&w, &h), pa = plain.Xfb(nullptr, nullptr);
    std::vector<std::uint8_t> p(static_cast<std::size_t>(w) * h * 4), q(p.size());
    glBindFramebuffer(GL_FRAMEBUFFER, gReadFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ra, 0);
    glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, p.data());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, pa, 0);
    glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, q.data());
    same = same && w && p == q;
  }
  const re4gx::GxStats &s = ring.Stats();
  char what[160];
  std::snprintf(what, sizeof(what), "vertex ring: %u chunks went into it (%u), it grew for the large ones (%u), "
                "%u waits", chunks, s.ringChunks - before.ringChunks, s.ringGrows - before.ringGrows,
                s.ringWaits - before.ringWaits);
  std::fprintf(stderr, "  %s\n", what);
  // (RE4_GX_VERTEX_RING=0: all of gx_gl_check without the ring; then only the pictures are compared)
  const char *forcedOff = std::getenv("RE4_GX_VERTEX_RING");
  if (!(forcedOff && *forcedOff == '0'))
    Check(s.ringChunks - before.ringChunks == chunks && s.ringGrows - before.ringGrows >= 1, what);
  Check(plain.Stats().ringChunks == 0, "vertex ring: off, a buffer per chunk");
  Check(same, "vertex ring: every picture as with a buffer per chunk");
  Check(glGetError() == GL_NO_ERROR, "vertex ring: no GL error");
  ring.Release();
  plain.Release();
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

// Original TV filter (the app menu's switch): the display copy takes the copy filter's vertical taps (the
// NTSC deflicker filter 8 8 10 12 10 8 8: the line above 16/64, the line 32/64, the line below 16/64), one
// original line apart at any scale. A red line one original line tall on blue: switched off it stays one
// line, switched on its neighbours get a quarter of its red and it keeps half.
void TestTvFilter(re4gx::GxRenderer &r) {
  auto scene = [&]() {
    Builder b;
    Baseline(b);
    b.Bp(0x53, 8u | 8u << 6 | 10u << 12 | 12u << 18);
    b.Bp(0x54, 10u | 8u << 6 | 8u << 12);
    DisplayCopy(b, true, kBlue);
    std::vector<std::uint8_t> v;
    V(v, 100, 200, 0, kRed);
    V(v, 300, 200, 0, kRed);
    V(v, 100, 201, 0, kRed);
    V(v, 300, 200, 0, kRed);
    V(v, 300, 201, 0, kRed);
    V(v, 100, 201, 0, kRed);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 6);
    DisplayCopy(b, false, kBlue);
    r.Run(b.Chunk());
    return r.Xfb(nullptr, nullptr);
  };
  const int f = r.Scale();
  const auto red = [&](GLuint xfb, int y) { return static_cast<int>(TexPixel(xfb, 200 * f, y * f)[0]); };
  GLuint xfb = scene();
  const int offAbove = red(xfb, 199), offAt = red(xfb, 200), offBelow = red(xfb, 201);
  r.SetTvFilter(true);
  xfb = scene();
  const int onAbove = red(xfb, 199), onAt = red(xfb, 200), onBelow = red(xfb, 201);
  r.SetTvFilter(false);
  std::fprintf(stderr, "  TV filter: red above/at/below off %d %d %d, on %d %d %d\n", offAbove, offAt, offBelow, onAbove,
               onAt, onBelow);
  Check(offAbove < 10 && offAt > 245 && offBelow < 10, "TV filter: off, the line stays one line");
  Check(onAbove > 50 && onAbove < 80 && onAt > 115 && onAt < 140 && onBelow > 50 && onBelow < 80,
        "TV filter: on, a quarter above and below, half on the line (the taps 16 32 16 of 64)");
  xfb = scene();
  Check(red(xfb, 199) < 10, "TV filter: off again, the line is sharp");
}

// The game's depth of field (filter01.cpp) copies the picture at half size with the copy filter 32 0 0 0 0 0 32
// (only the lines above and below) and draws the copy back over the scene. On the GameCube that copy is soft;
// until 0.99.7 it stayed sharp at a higher scale (its 2x2 box was the app's own texels, the filter left out) and
// showed as a second, shifted picture ("ghosting", 0.99.8). A red stripe (columns 200..209) on blue, copied that
// way and drawn back with linear filtering: at scale 1 and 4 the column left of the stripe gets some of its red
// (at 4 within reach of the GameCube's two pixels, not only the app's last texel), columns further out stay blue.
void TestBlurCopy() {
  for (int scale : {1, 4}) {
    re4gx::GxRenderer r;
    std::string error;
    Require(r.Init(scale, &error), error.c_str());
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    Quad(b, 200, 0, 210, 448, 0, kRed);
    b.Bp(0x53, 32u);          // copy filter: the line above (tap 0)
    b.Bp(0x54, 32u << 12);    // and the line below (tap 6), nothing on the line itself
    b.Bp(0x49, 0);
    b.Bp(0x4A, 511 | 447u << 10);
    b.Bp(0x4D, 32);
    b.Bp(0x4B, 0x400000 >> 5);
    b.Bp(0x52, 6u << 4 | 2u << 15 | 1u << 9);  // RGBA8 to a texture, half size: 256 x 224
    b.Bp(0x53, 0);
    b.Bp(0x54, 0);
    b.Bp(0x4A, 639 | 527u << 10);
    b.Bp(0x00, 1u | 1u << 4);
    b.Xf(0x103F, {1});
    b.Xf(0x1040, {5u << 7});
    b.Bp(0x28, 0 | 0 << 3 | 1 << 6 | 0 << 7 | 0x3F7u << 12);
    b.Bp(0xC0, 8u | 15u << 4 | 15u << 8 | 15u << 12 | 1u << 19);
    b.Bp(0x80, 1u << 4 | 1u << 5);  // clamp, linear (GXInitTexObjLOD 1, 1 of the game)
    b.Bp(0x84, 0);
    b.Bp(0x88, 255 | 223 << 10 | 6u << 20);
    b.Bp(0x30, 255);
    b.Bp(0x31, 223);
    b.Texture(0, 0x400000ull | 1ull << 32, RE4_GX_TEX_COPY, 0x400000, 256, 224, 6, {});
    std::vector<std::uint8_t> v;
    VT(v, 0, 0, 0, kWhite, 0, 0);
    VT(v, 512, 0, 0, kWhite, 1, 0);
    VT(v, 512, 448, 0, kWhite, 1, 1);
    VT(v, 0, 448, 0, kWhite, 0, 1);
    b.Draw(RE4_GX_V_COLOR0 | RE4_GX_V_TEX0, 24, v, 4, 0x80);
    r.Run(b.Chunk());
    // the device pixel two texels (at scale 4: half a GameCube pixel) left of the stripe, one inside it, one far out
    const int y = 200 * scale, edge = 200 * scale;
    const int beside = EfbPixel(r, edge - (scale > 1 ? 2 : 1), y)[0], inside = EfbPixel(r, 205 * scale, y)[0];
    const int far = EfbPixel(r, 190 * scale, y)[0];
    std::fprintf(stderr, "  blur copy at scale %d: red beside the stripe %d, inside %d, far out %d\n", scale, beside,
                 inside, far);
    const std::string at = " (scale " + std::to_string(scale) + ")";
    Check(beside > 40 && beside < 200, ("blur copy: the stripe's edge soft as on the GameCube" + at).c_str());
    Check(inside > 150, ("blur copy: inside the stripe red" + at).c_str());
    Check(far < 10, ("blur copy: columns further out stay blue" + at).c_str());
    Check(glGetError() == GL_NO_ERROR, ("blur copy: no GL error" + at).c_str());
    r.Release();
  }
}

// Sharpen (the app menu's filter, 08.11.2026): a grey band (160) on grey (100), softened by the TV filter (its rows
// above and below get in-between greys). Strong: the in-between rows move away from the middle (the edge gets
// steeper), every texel stays within the two greys (no seams), flat parts stay as they are, one pass per display
// copy. A movie's chunk (RE4_GX_CHUNK_MOVIE) and the rows outside the shown ones (SetShownRows) stay as they are.
// Off again: the plain copy.
void TestSharpen(re4gx::GxRenderer &r) {
  const std::uint8_t kDark[4] = {100, 100, 100, 255}, kLight[4] = {160, 160, 160, 255};
  auto scene = [&](std::uint32_t flags) {
    Builder b;
    Baseline(b);
    b.flags |= flags;
    b.Bp(0x53, 8u | 8u << 6 | 10u << 12 | 12u << 18);
    b.Bp(0x54, 10u | 8u << 6 | 8u << 12);
    DisplayCopy(b, true, kDark);
    Quad(b, 100, 200, 300, 260, 0, kLight);
    Quad(b, 100, 20, 300, 40, 0, kLight);  // above the rows shown in the 16:9 formats (56..392)
    DisplayCopy(b, false, kDark);
    r.Run(b.Chunk());
    return r.Xfb(nullptr, nullptr);
  };
  const int f = r.Scale();
  const auto grey = [&](GLuint xfb, int y) { return static_cast<int>(TexPixel(xfb, 200 * f, y)[0]); };
  // how far the texels around an edge (rows from..to of the game) are from the middle grey, and whether all stay
  // within the two greys
  const auto steep = [&](GLuint xfb, int from, int to, bool *inside) {
    int sum = 0;
    for (int y = from * f; y < to * f; y++) {
      const int v = grey(xfb, y);
      sum += std::abs(v - 130);
      if (v < 99 || v > 161)
        *inside = false;
    }
    return sum;
  };
  r.SetTvFilter(true);
  GLuint xfb = scene(0);
  const GLuint plain = xfb;
  bool inside = true;
  const int offEdge = steep(xfb, 196, 204, &inside), offTop = grey(xfb, 19 * f);
  const unsigned before = r.Stats().sharpenPasses;
  r.SetSharpen(2);
  r.SetShownRows(56, 392);
  xfb = scene(0);
  const int onEdge = steep(xfb, 196, 204, &inside), onTop = grey(xfb, 19 * f);
  std::fprintf(stderr, "  sharpen: the band's edge %d -> %d from the middle grey; row 19 (not shown) %d -> %d\n",
               offEdge, onEdge, offTop, onTop);
  Check(r.Stats().sharpenPasses == before + 2, "sharpen: one pass per display copy (the scene has two)");
  Check(xfb != plain && xfb != 0, "sharpen: the shown picture is the sharpened copy");
  Check(onEdge > offEdge + 2, "sharpen: the softened edge gets steeper");
  Check(inside, "sharpen: no texel beyond the two greys (no seams)");
  Check(Near(TexPixel(xfb, 100 * f, 300 * f), 100, 100, 100, 1) && Near(TexPixel(xfb, 200 * f, 230 * f), 160, 160, 160, 1),
        "sharpen: flat parts stay as they are");
  Check(onTop == offTop, "sharpen: the rows outside the shown ones are copied as they are");
  xfb = scene(RE4_GX_CHUNK_MOVIE);
  Check(r.Stats().sharpenPasses == before + 2 && xfb == plain, "sharpen: a movie's pictures stay as they are");
  r.SetSharpen(0);
  r.SetShownRows(0, 1 << 16);
  xfb = scene(0);
  Check(xfb == plain && steep(xfb, 196, 204, &inside) == offEdge, "sharpen: off again, the plain display copy");
  r.SetTvFilter(false);
}

// Colour and CRT (0.99.9): grey around an orange band and a black square. Colour "Warm film, Strong": one pass per
// display copy, the orange warmer (red up, blue down), black stays black; "Survival horror, Strong": the orange
// paler; off again the plain copy. CRT "Strong": the XFB stays as it is, the CRT's pictures once per display copy;
// the screen (three screen pixels a game line) has dark gaps between the lines in the grey, its corner is black
// (rounded); off again the plain screen, byte for byte.
void TestCrtColour(re4gx::GxRenderer &r) {
  const std::uint8_t kGrey[4] = {128, 128, 128, 255}, kOrange[4] = {200, 120, 40, 255}, kBlack[4] = {0, 0, 0, 255};
  auto scene = [&]() {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kGrey);
    Quad(b, 100, 200, 300, 260, 0, kOrange);
    Quad(b, 400, 300, 500, 350, 0, kBlack);
    DisplayCopy(b, false, kGrey);
    r.Run(b.Chunk());
    return r.Xfb(nullptr, nullptr);
  };
  const int f = r.Scale();
  const GLuint plain = scene();
  const std::vector<std::uint8_t> orange = TexPixel(plain, 200 * f, 230 * f);
  const int W = 1792, H = 1344;  // 448 lines: three screen pixels each
  std::vector<std::uint8_t> plainScreen, screen;
  Require(r.ReadPresented(W, H, 0, 0, W, H, 0, 0, &plainScreen), "CRT: the plain screen read back");
  // colour
  unsigned before = r.Stats().colourPasses;
  r.SetColour(1, 2);
  GLuint xfb = scene();
  const std::vector<std::uint8_t> warm = TexPixel(xfb, 200 * f, 230 * f), warmGrey = TexPixel(xfb, 50 * f, 50 * f);
  std::fprintf(stderr, "  colour: orange %d,%d,%d -> warm film %d,%d,%d; grey -> %d,%d,%d\n", orange[0], orange[1],
               orange[2], warm[0], warm[1], warm[2], warmGrey[0], warmGrey[1], warmGrey[2]);
  Check(r.Stats().colourPasses == before + 2 && xfb != plain, "colour: one pass per display copy, shown");
  Check(warm[0] > orange[0] && warm[2] < orange[2], "colour: warm film makes the orange warmer");
  Check(warmGrey[0] > warmGrey[2], "colour: warm film warms the grey");
  Check(Near(TexPixel(xfb, 450 * f, 320 * f), 0, 0, 0, 1), "colour: black stays black");
  r.SetColour(2, 2);
  xfb = scene();
  const std::vector<std::uint8_t> pale = TexPixel(xfb, 200 * f, 230 * f);
  std::fprintf(stderr, "  colour: survival horror %d,%d,%d\n", pale[0], pale[1], pale[2]);
  Check(pale[0] - pale[2] < (orange[0] - orange[2]) / 2, "colour: survival horror makes the orange paler");
  Check(Near(TexPixel(xfb, 450 * f, 320 * f), 0, 0, 0, 1), "colour: black stays black (survival horror)");
  r.SetColour(0, 1);
  xfb = scene();
  Check(xfb == plain, "colour: off again, the plain display copy");
  // CRT
  before = r.Stats().crtPasses;
  r.SetCrt(2);
  xfb = scene();
  Check(r.Stats().crtPasses == before + 2 && xfb == plain, "CRT: its pictures once per display copy, the XFB as it is");
  Require(r.ReadPresented(W, H, 0, 0, W, H, 0, 0, &screen), "CRT: the screen read back");
  // the grey at game x 128, around game line 112: the brightest and darkest of the screen rows of three lines
  int lo = 255, hi = 0;
  for (int y = 112 * 3 - 4; y < 112 * 3 + 5; y++) {
    const int v = screen[(static_cast<std::size_t>(y) * W + W / 5) * 4 + 1];
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  std::fprintf(stderr, "  CRT: the grey between %d and %d across three lines; the corner %d\n", lo, hi, screen[1]);
  Check(hi - lo > 30, "CRT: dark gaps between the game lines");
  Check(screen[0] == 0 && screen[1] == 0 && screen[2] == 0, "CRT: the corner black (rounded, curved)");
  Check(screen != plainScreen, "CRT: the screen differs");
  r.SetCrt(0);
  scene();
  Require(r.ReadPresented(W, H, 0, 0, W, H, 0, 0, &screen), "CRT: the screen read back again");
  Check(screen == plainScreen, "CRT: off again, the plain screen");
}

// PSone demake: a square of an in-between colour from x 101 on. Off: x 100 is the blue around it, x 101 and 102
// the square. On: one colour per cell of two game pixels, the mean of its game pixels: x 100 and 101 share the
// cell of x 100 (half blue, half the square), x 102 and 103 the square's; each channel one of the 32 levels of
// 15-bit colour. Off again, the square from x 101 as before.
void TestPsx(re4gx::GxRenderer &r) {
  const std::uint8_t kMid[4] = {100, 150, 200, 255};
  auto scene = [&]() {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlue);
    Quad(b, 101, 100, 300, 200, 0, kMid);
    DisplayCopy(b, false, kBlue);
    r.Run(b.Chunk());
    return r.Xfb(nullptr, nullptr);
  };
  const int f = r.Scale();
  auto at = [&](GLuint xfb, int x) { return TexPixel(xfb, x * f, 150 * f); };
  auto level = [](int v) { return std::abs(std::lround(v * 31.0 / 255.0) * 255.0 / 31.0 - v) <= 1.0; };
  auto same = [](const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
  };
  GLuint xfb = scene();
  const auto off100 = at(xfb, 100), off101 = at(xfb, 101);
  r.SetPsx(true);
  xfb = scene();
  const auto on100 = at(xfb, 100), on101 = at(xfb, 101), on102 = at(xfb, 102), on103 = at(xfb, 103);
  r.SetPsx(false);
  Check(Near(off100, 0, 0, 255) && Near(off101, 100, 150, 200), "PSone demake: off, the square from x 101");
  const bool cells = same(on100, on101) && Near(on100, 50, 75, 228, 12) && same(on102, on103) &&
                     Near(on102, 100, 150, 200, 8);
  Check(cells, "PSone demake: on, cells of two game pixels, the mean of their pixels");
  const bool levels = level(on102[0]) && level(on102[1]) && level(on102[2]);
  Check(levels, "PSone demake: on, 15-bit colour levels");
  if (!cells || !levels)
    Print("100", on100), Print("101", on101), Print("102", on102), Print("103", on103);
  xfb = scene();
  Check(Near(at(xfb, 101), 100, 150, 200), "PSone demake: off again, the picture as before");
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

// The pictures' cadence (picture_cadence.h): 30 pictures a second on 60 Hz, all 2 vsyncs, 0 % off; on 89.4 Hz with
// the game's own clock (59.94 / 2 a second) mostly 3 vsyncs and now and then 2 (the picture early by a vsync);
// 60 FPS on 90 Hz: 1 and 2 vsyncs by turns, half of them off; a pause starts over.
// 0.99.12: the steady rhythm (picture_cadence.h PictureHold). The report's phone at 30 game pictures a second on
// 60.1 Hz: each picture done 1.6 .. 2.4 vsyncs after its frame began (the game's thread at 76 %, the GPU after it),
// so its first vsync is 2 or 3 after the frame's start and the screen shows 1, 2 and 3 vsyncs a picture. The render
// thread draws at every vsync: without the hold the newest picture at once; with it a picture one vsync after the
// last waits one more (and is never held twice). Returns the line of PictureCadence and the pictures held.
static std::string HoldRun(bool hold, double from, double to, unsigned *held, unsigned *shownOut, double *lagOut) {
  const long long period = 16638935;  // 60.1 Hz
  const long long start = 1000000000LL;
  re4gx::PictureCadence cadence;
  re4gx::PictureHold steady;
  unsigned seed = 12345, shown = 0;
  double lag = 0.0;  // vsyncs a picture waited after it was ready, summed
  std::vector<long long> ready;
  for (int k = 0; k < 330; k++) {
    seed = seed * 1103515245u + 12345u;
    const double r = from + (to - from) * ((seed >> 8) & 0xFFFF) / 65535.0;
    ready.push_back(start + 2LL * k * period + static_cast<long long>(r * period));
  }
  std::size_t next = 0;
  bool pending = false;  // a picture held back, drawn at the next vsync
  std::string line, last;
  for (long long v = start; v < start + 680LL * period && next < ready.size(); v += period) {
    bool draw = false;
    if (pending) {
      draw = true;
      pending = false;
    } else if (ready[next] <= v) {
      if (hold && steady.Wait(v, period))
        pending = true;
      else
        draw = true;
    }
    if (draw) {
      lag += static_cast<double>(v - ready[next]) / period;
      next++;
      shown++;
      steady.Shown(v);
      if (cadence.Note(v, period, 60.1, &line)) last = line;
    }
  }
  *held = steady.held;
  *shownOut = shown;
  *lagOut = shown ? lag / shown : 0.0;
  return last;
}

void TestPictureHold() {
  unsigned held = 0, shown = 0;
  double lag = 0.0;
  // the report's spread (1.6 .. 2.4 vsyncs from the frame's start to done): without the hold 1, 2 and 3 in turn
  const std::string before = HoldRun(false, 1.6, 2.4, &held, &shown, &lag);
  std::fprintf(stderr, "  hold off: %s (%u shown, waited %.2f vsyncs on average)\n", before.c_str(), shown, lag);
  Check(before.find("1: 0,") == std::string::npos && before.find("3: 0,") == std::string::npos,
        "picture hold: without it the late and early pictures show 1 and 3 vsyncs (the report's pattern)");
  const unsigned shownBefore = shown;
  const std::string after = HoldRun(true, 1.6, 2.4, &held, &shown, &lag);
  std::fprintf(stderr, "  hold on:  %s (%u shown, %u held one vsync, waited %.2f vsyncs on average)\n", after.c_str(),
               shown, held, lag);
  Check(after.find("1: 0,") != std::string::npos, "picture hold: no picture shows for one vsync only");
  Check(shown == shownBefore, "picture hold: every game picture is shown (30 a second as before)");
  Check(held > 0 && lag < 1.0, "picture hold: some pictures wait one vsync, on average less than a vsync");
  // 2 vsyncs plus a little (a steady device, the Retroid): nothing to hold, the line as before
  const std::string even = HoldRun(true, 1.2, 1.5, &held, &shown, &lag);
  std::fprintf(stderr, "  steady:   %s (%u held)\n", even.c_str(), held);
  Check(held == 0 && even.find("0 % more than 2 ms") != std::string::npos, "picture hold: a steady device: nothing held");
  // not on a 120 Hz screen (the render thread draws every second vsync there) nor without a known period
  re4gx::PictureHold h;
  h.Shown(1000000000LL);
  Check(!h.Wait(1000000000LL + 8333333LL, 8333333LL), "picture hold: not at 120 Hz");
  Check(!h.Wait(1000000000LL + 16666667LL, 0), "picture hold: not without the screen's period");
  Check(!h.Wait(1000000000LL + 2 * 16666667LL, 16666667LL), "picture hold: two vsyncs after the last: drawn");
  Check(h.Wait(1000000000LL + 16666667LL, 16666667LL), "picture hold: one vsync after the last: waits");
}

// 0.99.13: the steady rhythm of 60 pictures a second on a screen that stays at 120 Hz (picture_cadence.h
// PresentPacer). The report's phone (nubia NX709S): the app draws at every second vsync; a picture is done (GPU)
// `from` .. `to` vsyncs after its drawing began, and the screen shows it at the first vsync 2 ms after that and not
// before the time asked for; a picture due at the same vsync as a later one is dropped. Some drawings begin a vsync
// late (the render thread woke late). Returns PictureCadence's line of the pictures as shown, the pictures shown and
// dropped, the average vsyncs from the drawing's vsync to the screen, the pacer's latency at the end, its steps and
// the times it stepped aside.
struct PresentRun {
  std::string line;
  unsigned shown = 0, dropped = 0;
  double delay = 0.0;
  int latency = 0;
  unsigned ups = 0, downs = 0, asides = 0, asked = 0;
  int twoShare = 0;  // % of the intervals of 2 vsyncs
};

static PresentRun PresentSim(bool pace, double from, double to, int pictures, unsigned seed = 4711) {
  const long long period = 8302889;  // 120.44 Hz, the report's
  const long long start = 2000000000LL;
  re4gx::PresentPacer pacer;
  struct Shot {
    long long began, target, done, at;
  };
  std::vector<Shot> shots;
  for (int k = 0; k < pictures; k++) {
    seed = seed * 1103515245u + 12345u;
    const double r = from + (to - from) * ((seed >> 8) & 0xFFFF) / 65535.0;
    const long long grid = start + 2LL * k * period;
    // now and then the drawing begins a vsync late: the latest vsync it sees is the one after its own
    const long long began = (seed >> 24) % 23 == 0 ? grid + period : grid;
    const long long done = grid + static_cast<long long>(r * period);
    long long asked = pace ? pacer.Target(began, period, 2) : 0;
    long long earliest = done + 2000000LL;
    if (asked > 0)
      earliest = std::max(earliest, asked);
    const long long at = start + ((earliest - start + period - 1) / period) * period;
    shots.push_back({began, asked > 0 ? asked + period / 2 : 0, done, at});
    // what the screen reports some swaps later: here at once
    if (pace)
      pacer.Outcome(shots.back().target, done, at);
  }
  PresentRun out;
  // the screen: at each vsync the newest picture due; an older one due at the same vsync or later is dropped
  std::vector<long long> shownAt;
  long long lastAt = 0;
  double delay = 0.0;
  for (std::size_t i = 0; i < shots.size(); i++) {
    const Shot &s = shots[i];
    const bool replacedByNext = i + 1 < shots.size() && shots[i + 1].at <= s.at;
    if (replacedByNext || s.at <= lastAt) {
      out.dropped++;
      continue;
    }
    shownAt.push_back(s.at);
    lastAt = s.at;
    delay += static_cast<double>(s.at - (start + 2LL * static_cast<long long>(i) * period)) / period;
  }
  re4gx::PictureCadence cadence;
  unsigned two = 0, intervals = 0;
  for (std::size_t i = 0; i < shownAt.size(); i++) {
    std::string line;
    if (cadence.Note(shownAt[i], period, 120.44, &line))
      out.line = line;
    if (i > 0) {
      intervals++;
      two += (shownAt[i] - shownAt[i - 1] + period / 2) / period == 2 ? 1 : 0;
    }
  }
  out.shown = static_cast<unsigned>(shownAt.size());
  out.delay = out.shown ? delay / out.shown : 0.0;
  out.latency = pacer.latency;
  out.ups = pacer.ups;
  out.downs = pacer.downs;
  out.asides = pacer.asides;
  out.asked = pacer.asked;
  out.twoShare = intervals ? static_cast<int>(100 * two / intervals) : 0;
  return out;
}

void TestPresentPacer() {
  // the report's phone: done 1.3 .. 2.3 vsyncs after the drawing began (render thread 8-9 ms, the GPU after it)
  const PresentRun before = PresentSim(false, 1.3, 2.3, 1300);
  std::fprintf(stderr, "  present times off: %s (%u shown, %u dropped, %d %% at 2 vsyncs, %.2f vsyncs to the screen)\n",
               before.line.c_str(), before.shown, before.dropped, before.twoShare, before.delay);
  Check(before.line.find("1: 0,") == std::string::npos && before.line.find("3: 0,") == std::string::npos &&
            before.twoShare < 70,
        "present pacer: without it the pictures show 1 and 3 vsyncs in turn (the report's pattern)");
  const PresentRun after = PresentSim(true, 1.3, 2.3, 1300);
  std::fprintf(stderr, "  present times on:  %s (%u shown, %u dropped, %d %% at 2 vsyncs, %.2f vsyncs to the screen, "
               "latency %d)\n", after.line.c_str(), after.shown, after.dropped, after.twoShare, after.delay,
               after.latency);
  Check(after.twoShare >= 97, "present pacer: at least 97 % of the pictures show for 2 vsyncs");
  Check(after.dropped == 0 && after.shown == 1300, "present pacer: every picture is shown (60 a second as before)");
  Check(after.latency == 3 && after.delay < 3.1, "present pacer: 3 vsyncs from the drawing to the screen (8 ms more)");
  // a slower GPU (done 2.5 .. 3.4 vsyncs after): the latency rises to 4, then the rhythm holds again
  const PresentRun slow = PresentSim(true, 2.5, 3.4, 1300);
  std::fprintf(stderr, "  slower GPU:        %s (%d %% at 2 vsyncs, latency %d, %u up)\n", slow.line.c_str(),
               slow.twoShare, slow.latency, slow.ups);
  Check(slow.latency == 4 && slow.ups == 1 && slow.twoShare >= 90 && slow.dropped == 0,
        "present pacer: a slower GPU: one vsync more, the rhythm holds");
  // a fast GPU (done within 1.2 vsyncs): after 5 s without a late picture back to 2 vsyncs
  const PresentRun fast = PresentSim(true, 0.6, 1.2, 1300);
  std::fprintf(stderr, "  fast GPU:          %s (%d %% at 2 vsyncs, latency %d, %u down)\n", fast.line.c_str(),
               fast.twoShare, fast.latency, fast.downs);
  Check(fast.latency == 2 && fast.downs == 1 && fast.twoShare >= 99, "present pacer: a fast GPU: back to 2 vsyncs");
  // a device that cannot keep up (done 3.6 .. 5 vsyncs after): even 4 vsyncs do not help, the times step aside and the
  // pictures come as without them (no latency added, no more lost), for 30 s, then the pacer tries again
  const PresentRun behind = PresentSim(true, 3.6, 5.0, 1300);
  const PresentRun behindOff = PresentSim(false, 3.6, 5.0, 1300);
  std::fprintf(stderr, "  too slow:          %s (%d %% at 2 vsyncs, %u stepped aside, %u asked of 1300, %.2f vsyncs to "
               "the screen, without: %.2f)\n", behind.line.c_str(), behind.twoShare, behind.asides, behind.asked,
               behind.delay, behindOff.delay);
  Check(behind.asides == 1 && behind.asked < 200 && behind.shown + behind.dropped == 1300 &&
            behind.dropped <= behindOff.dropped + 2,
        "present pacer: a device that does not keep up: the times step aside, no more pictures lost than without");
  Check(behind.delay < behindOff.delay + 0.2, "present pacer: stepped aside, no latency added");
  const PresentRun longer = PresentSim(true, 3.6, 5.0, 1800 + 600);
  Check(longer.asides == 2, "present pacer: after 30 s it tries again (and steps aside again)");
  // the render thread behind the game three times within 2 s: aside at once
  {
    re4gx::PresentPacer b;
    const long long per = 8333333LL;
    long long v = 1000000000LL;
    Check(b.Target(v, per, 2) > 0, "present pacer: asks at 120 Hz");
    b.Behind(v);
    b.Behind(v + 20 * per);
    Check(!b.Aside(), "present pacer: twice behind: still asking");
    b.Behind(v + 40 * per);
    Check(b.Aside() && b.Target(v + 42 * per, per, 2) == 0, "present pacer: three times behind within 2 s: aside");
    Check(b.Target(v + 42 * per + 30000000000LL, per, 2) > 0 && b.latency == 3,
          "present pacer: 30 s later: asking again, from 3 vsyncs");
    re4gx::PresentPacer c;
    c.Target(v, per, 2);
    c.Behind(v);
    c.Behind(v + 300 * per);
    c.Behind(v + 600 * per);
    Check(!c.Aside(), "present pacer: behind now and then (2.5 s apart): still asking");
  }
  // only where every n-th vsync is a 60th of a second
  re4gx::PresentPacer p;
  Check(p.Target(1000000000LL, 16666667LL, 1) == 0, "present pacer: not at 60 Hz");
  Check(p.Target(1000000000LL, 11111111LL, 1) == 0, "present pacer: not at 90 Hz");
  Check(p.Target(1000000000LL, 6944444LL, 2) == 0, "present pacer: not at 144 Hz (2 vsyncs are 13.9 ms)");
  Check(p.Target(1000000000LL, 8333333LL, 2) == 1000000000LL + 3 * 8333333LL - 8333333LL / 2,
        "present pacer: 120 Hz: asked for 3 vsyncs after, half a vsync before it");
  Check(p.Target(1000000000LL, 4166667LL, 4) > 0, "present pacer: 240 Hz, every 4th vsync");
  Check(p.Target(1000000000LL, 0, 2) == 0 && p.latency == 0, "present pacer: no period known: off");
  // the grid: a drawing that began a vsync late keeps its place
  re4gx::PresentPacer g;
  const long long t0 = g.Target(1000000000LL, 8333333LL, 2);
  const long long t1 = g.Target(1000000000LL + 3 * 8333333LL, 8333333LL, 2);
  Check(t1 - t0 == 2 * 8333333LL, "present pacer: a drawing a vsync late stays on the grid");
}

void TestPictureCadence() {
  auto run = [](double hz, double fps, int seconds, std::string *line) {
    re4gx::PictureCadence c;
    const long long period = static_cast<long long>(1e9 / hz);
    const long long at = 1000000000LL;
    bool got = false;
    for (int i = 0; i <= static_cast<int>(fps * seconds) && !got; i++) {
      const long long t = at + static_cast<long long>(i * 1e9 / fps) + 300000;  // drawn just after a vsync
      // the vsync the picture waits for: the next one at or after its time
      const long long v = at + ((t - at + period - 1) / period) * period;
      got = c.Note(re4gx::PictureCadence::Snap(v, at, period), period, hz, line);
    }
    return got;
  };
  std::string line;
  Check(run(60.0, 30.0, 11, &line), "cadence: a line after 10 s");
  std::fprintf(stderr, "  %s\n", line.c_str());
  Check(line.find("0 % more than 2 ms") != std::string::npos && line.find("2: 301,") != std::string::npos,
        "cadence: 30 pictures on 60 Hz, all 2 vsyncs");
  Check(run(89.4, 59.94 / 2, 11, &line), "cadence: 89.4 Hz");
  std::fprintf(stderr, "  %s\n", line.c_str());
  Check(line.find("2: 0,") == std::string::npos && line.find("3: ") != std::string::npos,
        "cadence: the game's own clock on 89.4 Hz shows pictures of 2 vsyncs among the 3");
  Check(run(90.0, 60.0, 11, &line), "cadence: 60 FPS on 90 Hz");
  std::fprintf(stderr, "  %s\n", line.c_str());
  Check(line.find("1: 0,") == std::string::npos && line.find("2: 0,") == std::string::npos,
        "cadence: 60 FPS on 90 Hz, 1 and 2 vsyncs by turns");
  re4gx::PictureCadence c;
  std::string none;
  c.Note(1000000000LL, 16666667, 60.0, &none);
  c.Note(1033333333LL, 16666667, 60.0, &none);
  c.Note(5000000000LL, 16666667, 60.0, &none);  // a pause
  Check(c.intervals.empty(), "cadence: a pause starts over");
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
  // A device whose GPU needs `ms2x` ms a picture at 2x and the pixels' share of that at the other levels (in
  // halves: 1.5x 56 %, 1x 25 %): 30 pictures a second while it fits into 33 ms, else the GPU's pace with the
  // fence behind and the render thread waiting. Returns the times of the level changes.
  void PlayGpu(double seconds, double ms2x, std::vector<double> *downs = nullptr, std::vector<double> *ups = nullptr) {
    const double end = t + seconds;
    while (t < end) {
      const double share = a.Level() / 4.0;
      const double gpu = ms2x * share * share;
      const bool over = gpu > 1000.0 / 30.0 + 0.1;
      t += (over ? gpu : 1000.0 / 30.0) / 1000.0;
      sum += over ? 1 : 0;
      checks++;
      waited += over ? gpu - 1000.0 / 30.0 : 0.0;
      const int was = a.Level();
      std::string why;
      a.Chunk(t, sum, checks, true, &why, dropped, waited);
      if (!why.empty())
        lines.push_back(why);
      if (a.Level() < was && downs)
        downs->push_back(t);
      if (a.Level() > was && ups)
        ups->push_back(t);
    }
  }
};

// The benchmark's steps (gx_bench_steps.h) with simulated devices: `game(level, sixty)` the game pictures a second
// at a level, `shown(level, sixty)` those shown; one call a second. The log lines go to stderr, so the whole
// sequence 4x -> ... -> result is visible.
template <typename Game, typename Shown>
re4gx::BenchSteps RunBench(const char *name, Game game, Shown shown, double tick = 1.0) {
  re4gx::BenchSteps b;
  b.Start(0.0);
  std::fprintf(stderr, "benchmark simulation: %s\n", name);
  double t = 0.0;
  for (int i = 0; i < 400 && !b.Finished(); i++) {
    t += tick;
    const bool sixty = b.Pass() == 1;
    std::string line;
    const re4gx::BenchSteps::Step step = b.Second(t, game(b.Level(), sixty), shown(b.Level(), sixty), 0, &line);
    if (step != re4gx::BenchSteps::kNone) std::fprintf(stderr, "  %5.1f s  %s\n", t, line.c_str());
  }
  std::fprintf(stderr, "  result: %s (%s) with 30, %s (%s) with 60 after %.0f s\n", re4gx::BenchSteps::Name(b.Result(0)).c_str(),
               b.Held(0) ? "stable" : "not stable", re4gx::BenchSteps::Name(b.Result(1)).c_str(),
               b.Held(1) ? "stable" : "not stable", t);
  return b;
}

void TestBenchSteps() {
  // a fast device: 4x holds both
  re4gx::BenchSteps fast = RunBench("fast", [](int, bool) { return 30; }, [](int, bool sixty) { return sixty ? 60 : 30; });
  Check(fast.Finished() && fast.Result(0) == 8 && fast.Result(1) == 8 && fast.Held(0) && fast.Held(1),
        "benchmark: a fast device: 4x for 30 and 60");
  // a middle one: 30 pictures from 2x down, 60 shown from 1.5x down
  re4gx::BenchSteps mid = RunBench(
      "middle (30 up to 2x, 60 up to 1.5x)", [](int level, bool) { return level <= 4 ? 30 : 24; },
      [](int level, bool sixty) { return sixty ? (level <= 3 ? 60 : 47) : (level <= 4 ? 30 : 24); });
  Check(mid.Finished() && mid.Result(0) == 4 && mid.Result(1) == 3 && mid.Held(0) && mid.Held(1),
        "benchmark: steps 4x 3.5x 3x 2.5x 2x for 30, the 60 pass from 2x (the 30 result, 0.99.10) down to 1.5x");
  Check(!mid.CpuSign(), "benchmark: the middle device's graphics chip holds it back, not the processor");
  // a slow one: not even 1x
  re4gx::BenchSteps slow = RunBench("slow (not even 1x)", [](int, bool) { return 22; }, [](int, bool) { return 22; });
  Check(slow.Finished() && slow.Result(0) == 2 && slow.Result(1) == 2 && !slow.Held(0) && !slow.Held(1),
        "benchmark: 1x does not hold: 1x, marked not held, and it ends");
  // seconds that come late (a stalling game): the pass's time limit ends it all the same
  re4gx::BenchSteps late = RunBench("seconds 9 s apart", [](int, bool) { return 20; }, [](int, bool) { return 20; }, 9.0);
  Check(late.Finished(), "benchmark: ends with seconds far apart too");
  // one hitch at a level is allowed, two are not
  int calls = 0;
  re4gx::BenchSteps dip = RunBench("one bad second at 4x",
                                   [&calls](int, bool) { return ++calls == 3 ? 25 : 30; },
                                   [](int, bool sixty) { return sixty ? 60 : 30; });
  Check(dip.Result(0) == 8 && dip.Result(1) == 8, "benchmark: one bad second at 4x: still 4x");
  int calls2 = 0;
  re4gx::BenchSteps dip2 = RunBench("two bad seconds at 4x",
                                    [&calls2](int, bool) { ++calls2; return calls2 == 3 || calls2 == 6 ? 25 : 30; },
                                    [](int, bool sixty) { return sixty ? 60 : 30; });
  Check(dip2.Result(0) == 7 && dip2.Result(1) == 7,
        "benchmark: two bad seconds at 4x: 3.5x for 30, the 60 pass from 3.5x (0.99.10) holds it");
  // 0.99.10: a processor that holds the game back: about the same game pictures at every level, not even 1x
  re4gx::BenchSteps cpu = RunBench("processor-bound (22-24 at every level)",
                                   [](int level, bool) { return level <= 3 ? 24 : 22; }, [](int, bool) { return 22; });
  Check(cpu.Result(0) == 2 && !cpu.Held(0) && cpu.CpuSign(), "benchmark: no level helps: the processor sign");
  Check(!slow.CpuSign() || slow.Result(0) == 2, "benchmark: the slow device's sign is only set at 1x");
  // a graphics chip that gets much faster lower down, still not 30 at 1x: no processor sign
  re4gx::BenchSteps gpu = RunBench("graphics-bound (12 at 4x, 27 at 1x)",
                                   [](int level, bool) { return 33 - 3 * level; }, [](int, bool) { return 20; });
  Check(gpu.Result(0) == 2 && !gpu.Held(0) && !gpu.CpuSign(), "benchmark: lower levels help: no processor sign");
  // 0.99.10: the 30 pass from 2.5x (a weak graphics chip) after 15 s that do not count
  {
    re4gx::BenchSteps w;
    w.Start(0.0, 5, 15.0);
    double t = 0.0;
    int firstJudged = -1;
    for (int i = 0; i < 400 && !w.Finished(); i++) {
      t += 1.0;
      const int level = w.Level();
      std::string line;
      // the first 15 s at 2.5x as if the programs were made (8 a second), then 30 at 2x and below
      const int c = t <= 15.0 ? 8 : level <= 4 ? 30 : 24;
      const re4gx::BenchSteps::Step step = w.Second(t, c, w.Pass() ? (level <= 3 ? 60 : 40) : c, 0, &line);
      if (step != re4gx::BenchSteps::kNone && firstJudged < 0) firstJudged = static_cast<int>(t);
    }
    Check(w.Finished() && w.Result(0) == 4 && w.Result(1) == 3 && firstJudged == 25,
          "benchmark: from 2.5x after a 15 s warm-up (the first level judged at 25 s), 2x and 1.5x");
  }
  // fairly steady (the project owner's Retroid at 2x): 29-30 with a hitch now and then holds
  int calls3 = 0;
  re4gx::BenchSteps steady = RunBench(
      "fairly steady at 2x and below", [&calls3](int level, bool) { ++calls3; return level > 4 ? 24 : calls3 % 3 ? 30 : 29; },
      [](int level, bool sixty) { return sixty ? (level > 4 ? 48 : 58) : (level > 4 ? 24 : 30); });
  Check(steady.Result(0) == 4 && steady.Result(1) == 4 && steady.Held(0) && steady.Held(1),
        "benchmark: 29-30 a second at 2x holds 2x for 30 and 60");
  // 0.99.12: the game's thread at the place (report 09912-benchmark-too-good, a Helio G85 phone): 30 pictures a second
  // from 1.5x down (17 at 2.5x, 24.6 at 2x), 25.4 ms of processor time a picture: 76 % at the 30 pass's result; the
  // Retroid 9 ms (27 %); a core without the figure: not known
  auto cpuRun = [](const char *name, double cpuMs) {
    re4gx::BenchSteps b;
    b.Start(0.0, 5);
    std::fprintf(stderr, "benchmark simulation: %s\n", name);
    double t = 0.0;
    for (int i = 0; i < 400 && !b.Finished(); i++) {
      t += 1.0;
      const int level = b.Level();
      const bool sixty = b.Pass() == 1;
      const int c = level >= 5 ? 17 : level == 4 ? 25 : 30;
      std::string line;
      const re4gx::BenchSteps::Step step = b.Second(t, c, sixty ? 36 : c, 0, &line, cpuMs);
      if (step != re4gx::BenchSteps::kNone) std::fprintf(stderr, "  %5.1f s  %s\n", t, line.c_str());
    }
    return b;
  };
  re4gx::BenchSteps g85 = cpuRun("Helio G85 (25.4 ms a picture for the game's thread)", 25.4);
  Check(g85.Result(0) == 3 && g85.Held(0) && g85.CpuLoad() == 76 && !g85.CpuSign(),
        "benchmark: the Helio G85 holds 30 at 1.5x, its game's thread at 76 % (the old sign saw nothing)");
  re4gx::BenchSteps calm = cpuRun("the same levels, 9 ms a picture (Retroid)", 9.0);
  Check(calm.CpuLoad() == 27, "benchmark: 9 ms a picture is 27 %");
  re4gx::BenchSteps unknown = cpuRun("an older core (no processor time)", -1.0);
  Check(unknown.CpuLoad() == -1 && unknown.Result(0) == 3, "benchmark: without the figure the load is not known");
  // the 60 FPS pause does not carry over into the benchmark's next level (SmoothPacer::Restart)
  re4gx::SmoothPacer pacer;
  bool off = false;
  for (int i = 0; i < 200 && !off; i++) off = !pacer.Chunk(i / 30.0, true, nullptr) && pacer.Off();
  Check(off, "60 FPS pacer: behind again and again, the in-between pictures pause");
  pacer.Restart(7.0);
  Check(!pacer.Off() && pacer.Chunk(7.05, false, nullptr), "60 FPS pacer: Restart: in-between pictures at once");
  // a break with 4 s of grace: behind all the while within it, no pause
  re4gx::SmoothPacer loading;
  loading.Restart(0.0, 4.0);
  bool paused = false;
  for (int i = 1; i < 110; i++) paused |= !loading.Chunk(i / 30.0, true, nullptr) && loading.Off();
  Check(!paused, "60 FPS pacer: no pause within the grace after a break (a room loading)");
}

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
    r.Play(9.0, 30.0, 0);  // four windows keeping up
    Check(r.a.Level() == 1, "auto level: not up before 10 s keeping up");
    r.Play(3.0, 30.0, 0);
    Check(r.a.Level() == 2 && r.a.Ups() == 1, "auto level: up again after 10 s keeping up (the wait of 10 s over)");
    // straight down again: the next wait doubles (20 s)
    r.Play(6.5, 20.0, 2);
    Check(r.a.Level() == 1 && r.a.Hold() == 20.0, "auto level: down again soon: the next wait 20 s");
  }
  {
    // not before 10 s after a step down, even with every window keeping up
    AutoRun r;
    r.a.Start(2, 0.0);
    r.Play(4.1, 20.0, 1);
    Check(r.a.Level() == 1, "auto level: down after two slow windows");
    const double downAt = r.t;
    while (r.a.Ups() == 0 && r.t < 100.0)
      r.Play(0.1, 30.0, 0);
    Check(r.a.Ups() == 1 && r.t - downAt >= 9.9 && r.t - downAt < 11.0, "auto level: up 10 s after the step down");
  }
  {
    // heavy, light, heavy (the project owner, 06.11.2026: 30 s too long when the way goes on into a lighter area): a
    // GPU that needs 40 ms at 2x (22.5 ms at 1.5x) at the heavy spot, 24 ms at 2x in the light stretch
    AutoRun r;
    r.a.Start(4, 0.0, 2, true);
    std::vector<double> downs, ups;
    r.PlayGpu(40.0, 40.0, &downs, &ups);
    Check(!downs.empty() && downs.front() < 5.0, "auto level, heavy spot: half a level down within 5 s");
    const double light = r.t;
    const size_t upsBefore = ups.size(), downsBefore = downs.size();
    r.PlayGpu(120.0, 24.0, &downs, &ups);
    Check(r.a.Level() == 4 && downs.size() == downsBefore, "auto level, light stretch: 2x again, no step down there");
    Check(r.a.Level() == 4 && (ups.size() == upsBefore || ups.back() - light <= 25.0),
          "auto level, light stretch: back at 2x at most 25 s into it (until 0.98.4: 59 s)");
    Check(r.a.Hold() == re4gx::AutoLevel::kFirstHold, "auto level, light stretch: the wait back to 10 s once 2x held 60 s");
    // the next heavy spot starts with the short wait again: down, up after 10 s, down again (a bounce), then 20 s
    downs.clear();
    ups.clear();
    r.PlayGpu(40.0, 40.0, &downs, &ups);
    Check(downs.size() >= 1 && ups.size() >= 1 && ups.front() - downs.front() < 12.0,
          "auto level, the next heavy spot: the first try up after about 10 s");
    // heavy for 10 minutes: the bounces grow apart (10, 20, 40, 80, 160, 300 s), at most 7 steps down in all
    r.PlayGpu(600.0, 40.0, &downs, &ups);
    r.PlayGpu(5.0, 40.0, &downs, &ups);  // a try up in the last seconds is back down after two slow windows
    std::fprintf(stderr, "auto level, heavy for 10 minutes: %zu steps down, %zu up, level %s, wait %.0f s\n",
                 downs.size(), ups.size(), r.a.Name(r.a.Level()).c_str(), r.a.Hold());
    Check(downs.size() <= 7 && r.a.Level() == 3, "auto level, heavy for 10 minutes: few bounces, ends at 1.5x");
    Check(r.a.Hold() >= 160.0, "auto level, heavy for 10 minutes: the wait grown to minutes");
    for (size_t i = 2; i < ups.size(); i++)
      Check(ups[i] - ups[i - 1] > ups[i - 1] - ups[i - 2], "auto level, heavy for 10 minutes: each try up later than the last");
  }
  {
    // breaks (06.11.2026): the app menu (no chunks for 20 s), then a room's loading and its programs (3 s slow with
    // the GPU behind): no step down, no hold; slow on after the quiet seconds: down as before
    AutoRun r;
    r.a.Start(4, 0.0, 2, true);
    r.Play(1.5, 30.0, 0);             // a window open when the menu opens
    r.t += 20.0;                      // the menu: no chunks
    r.Play(3.0, 20.0, 1);             // the room loads, the renderer behind
    r.Play(10.0, 30.0, 0);
    Check(r.a.Level() == 4 && r.a.Downs() == 0 && r.lines.empty(), "auto level: menu and room loading: no step down");
    r.a.Restart(r.t);                 // a room starts (core_mode.cpp)
    r.Play(3.5, 20.0, 1);             // its loading within the quiet seconds
    Check(r.a.Level() == 4 && r.a.Downs() == 0, "auto level: the quiet seconds after a room start are not counted");
    r.Play(6.0, 20.0, 1);             // slow on after them: the GPU really behind
    Check(r.a.Level() == 3 && r.a.Downs() == 1, "auto level: slow after the quiet seconds: half a level down");
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
    Check(r.a.Level() == 1 && r.a.Hold() == 20.0, "auto level: down again within 15 s of the step up: the next wait 20 s");
    const double downAt = r.t;
    while (r.a.Ups() == 1 && r.t < 400.0)
      r.Play(0.5, 30.0, 0);
    Check(r.t - downAt >= 15.0, "auto level: the longer wait holds");  // the step down came during the slow part
    r.Play(59.0, 30.0, 0);
    Check(r.a.Hold() == 20.0, "auto level: the longer wait kept while the step up holds less than 60 s");
    r.Play(41.0, 30.0, 0);
    Check(r.a.Level() == 2, "auto level: never above the screen's level");
    Check(r.a.Hold() == re4gx::AutoLevel::kFirstHold, "auto level: a step up that held 60 s: the wait back to 10 s");
  }
  {
    // the benchmark's check (0.99.10): a renderer that starts below its top, at the benchmark's level, and still
    // goes up to the top where the GPU keeps up
    AutoRun r;
    r.a.Start(7, 0.0, 2, true, 6, 6);
    Check(r.a.Level() == 6 && r.a.Top() == 7, "auto level: starts at the level asked for, below the top");
    while (r.a.Ups() == 0 && r.t < 60.0)
      r.Play(0.5, 30.0, 0);
    Check(r.a.Level() == 7, "auto level: from the start below the top up to the top");
    AutoRun low;
    low.a.Start(4, 0.0, 2, true, 3, 1);
    Check(low.a.Level() == 2, "auto level: a start below the bottom takes the bottom");
    AutoRun high;
    high.a.Start(4, 0.0, 2, true, 3, 9);
    Check(high.a.Level() == 4, "auto level: a start above the top takes the top");
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
    // a fifth left out with the GPU behind (Retroid 28.10.2026, r226 at 2x with 4x MSAA): one level down
    AutoRun fifth;
    fifth.a.Start(2, 0.0);
    fifth.Play(6.5, 30.0, 1, true, 5);
    Check(fifth.a.Level() == 1 && fifth.a.Downs() == 1,
          "auto level: 60 FPS, a fifth of the in-between pictures left out, the GPU behind: one level down");
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
    // up to the benchmark's level (0.99.5, the project owner's Retroid report 08.11.2026: 60 FPS, down to 1x after a
    // hitch, never up again): with every in-between picture shown and the game at 30, a GPU a third behind (the fence of
    // the newest in-between picture still open at every third look) goes up to the benchmark's 1.5x, not above it
    auto play = [](AutoRun &r, double seconds, unsigned behindEvery) {
      const double end = r.t + seconds;
      while (r.t < end) {
        r.t += 1.0 / 30.0;
        r.checks++;
        r.sum += behindEvery && r.checks % behindEvery == 0 ? 1u : 0u;
        std::string why;
        r.a.Chunk(r.t, r.sum, r.checks, true, &why, r.dropped, r.waited);
        if (!why.empty())
          r.lines.push_back(why);
      }
    };
    AutoRun r;
    r.a.Start(4, 0.0, 2, true, 3);  // the benchmark's 1.5x, Dynamic up to 2x
    r.Play(6.5, 20.0, 1);
    r.Play(6.5, 20.0, 1);
    Check(r.a.Level() == 2, "auto level, benchmark 1.5x: down to 1x after a long hitch");
    play(r, 30.0, 3);
    Check(r.a.Level() == 3 && r.a.Ups() == 1, "auto level, benchmark 1.5x: up to 1.5x with the GPU a third behind");
    play(r, 120.0, 3);
    Check(r.a.Level() == 3 && r.a.Ups() == 1, "auto level, benchmark 1.5x: not above it unless the GPU keeps up");
    play(r, 30.0, 0);
    Check(r.a.Level() == 4 && r.a.Ups() == 2, "auto level, benchmark 1.5x: 2x once the GPU keeps up");
    AutoRun half;
    half.a.Start(4, 0.0, 2, true, 3);
    half.Play(13.0, 20.0, 1);
    play(half, 60.0, 2);
    Check(half.a.Level() == 2 && half.a.Ups() == 0, "auto level, benchmark 1.5x: not up with the GPU half behind");
    AutoRun none;
    none.a.Start(4, 0.0, 2, true);  // no benchmark: as before
    none.Play(13.0, 20.0, 1);
    play(none, 60.0, 3);
    Check(none.a.Level() == 2 && none.a.Ups() == 0, "auto level, no benchmark: not up with the GPU a third behind");
  }
  {
    // 60 FPS (0.99.5, the project owner's reports 08.11.2026, GLES and Vulkan): a heavy moment at 2x took it to 1.5x;
    // then he stood still facing a wall, 60 shown a second, no in-between picture left out, the fences 0.7 behind, and
    // it never went back up. Now: up again 10-20 s later; a level too heavy goes back within 5 s, and each next try
    // waits longer (no pendulum); below the top a status line every 10 s says why it stays.
    // `behindOf10`: of every 10 looks that many find the GPU behind; `droppedEvery`: one in-between picture of so many
    // chunks left out (0: none)
    auto play60 = [](AutoRun &r, double seconds, unsigned behindOf10, unsigned droppedEvery,
                     std::vector<std::string> *status) {
      const double end = r.t + seconds;
      while (r.t < end) {
        r.t += 1.0 / 30.0;
        r.checks++;
        r.sum += r.checks % 10 < behindOf10 ? 1u : 0u;
        if (droppedEvery && r.checks % droppedEvery == 0)
          r.dropped++;
        std::string why, line;
        r.a.Chunk(r.t, r.sum, r.checks, true, &why, r.dropped, r.waited, true, &line);
        if (!why.empty())
          r.lines.push_back(why);
        if (!line.empty() && status)
          status->push_back(line);
      }
    };
    AutoRun r;
    std::vector<std::string> status;
    r.a.Start(4, 0.0, 2, true, 3);  // the benchmark's 1.5x, Dynamic up to 2x (the owner's Retroid)
    play60(r, 6.5, 10, 4, &status);  // heavy: a quarter left out, the GPU behind
    Check(r.a.Level() == 3, "auto level, 60 FPS: a heavy moment takes 2x to 1.5x");
    const double downAt = r.t;
    while (r.a.Ups() == 0 && r.t < downAt + 120.0)
      play60(r, 0.5, 7, 0, &status);  // standing still: none left out, the fences 0.7 behind (as the owner's GLES log)
    Check(r.a.Level() == 4 && r.t - downAt >= 9.9 && r.t - downAt <= 20.5,
          "auto level, 60 FPS: steady at 1.5x (none left out, fences 0.7 behind): back at 2x within 10-20 s");
    std::fprintf(stderr, "  auto level, 60 FPS: back at 2x %.1f s after the step down\n", r.t - downAt);
    Check(!r.lines.empty() && r.lines.back().find("every in-between picture shown") != std::string::npos,
          "auto level, 60 FPS: the step up logged");
    bool told = false;
    for (const std::string &l : status)
      told = told || l.find("dynamic resolution: stays at 1.5x below 2x: ") != std::string::npos;
    Check(told, "auto level, 60 FPS: a status line says why it stays below the top");
    const unsigned downs = r.a.Downs();
    play60(r, 60.0, 9, 0, &status);  // steady at 2x: it stays
    Check(r.a.Level() == 4 && r.a.Downs() == downs, "auto level, 60 FPS: steady at 2x, it stays");
    // in-between pictures left out at 1.5x: no step up, the status says so
    AutoRun lost;
    std::vector<std::string> lostStatus;
    lost.a.Start(4, 0.0, 2, true, 3);
    play60(lost, 6.5, 10, 4, &lostStatus);
    play60(lost, 60.0, 7, 20, &lostStatus);  // a twentieth left out (5 %): not steady
    // (the window across the switch from heavy to light may take it half a level further down: no matter)
    Check(lost.a.Level() <= 3 && lost.a.Ups() == 0, "auto level, 60 FPS: not up while in-between pictures are left out");
    told = false;
    for (const std::string &l : lostStatus)
      told = told || l.find("in-between pictures left out") != std::string::npos;
    Check(told, "auto level, 60 FPS: the status names the in-between pictures left out");
    // too heavy at 2x, fine at 1.5x for 5 minutes: tries 2x again and again, each try later than the last
    AutoRun swing;
    swing.a.Start(4, 0.0, 2, true, 3);
    std::vector<double> ups;
    while (swing.t < 300.0) {
      const unsigned before = swing.a.Ups();
      if (swing.a.Level() == 4)
        play60(swing, 0.5, 10, 4, nullptr);
      else
        play60(swing, 0.5, 7, 0, nullptr);
      if (swing.a.Ups() != before)
        ups.push_back(swing.t);
    }
    std::fprintf(stderr, "  auto level, 60 FPS, 2x too heavy for 5 minutes: %u steps down, %zu up, the wait %.0f s\n",
                 swing.a.Downs(), ups.size(), swing.a.Hold());
    Check(swing.a.Downs() <= 6 && swing.a.Downs() >= 2, "auto level, 60 FPS: 2x too heavy: few tries in 5 minutes");
    Check(!ups.empty() && ups.front() <= 25.0, "auto level, 60 FPS: the first try within 25 s");
    for (size_t i = 2; i < ups.size(); i++)
      Check(ups[i] - ups[i - 1] > ups[i - 1] - ups[i - 2], "auto level, 60 FPS: each try later than the last");
    Check(swing.a.Hold() >= 80.0, "auto level, 60 FPS: the wait grown after the bounces");
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

// Bloom (0.99.13; GxCore::BloomPass): a dark grey room (3D, 20 m away), a white lamp (40 x 40 game pixels, 1 m),
// a mid-grey object below the threshold, then the HUD (2D after the scene: a white square). Off: nothing changes.
// Light: the lamp's surroundings brighten, the room away from it, the grey object and the HUD's surroundings stay;
// strong brightens more and farther. With the letterbox (the scene's scissor rows 56..391) the bar above stays black;
// a menu screen of the game (RE4_GX_CHUNK_STILL) gets none. Read through ReadXfb (rows from the top, the renderer's
// pixels): the same check for GL and Vulkan.
std::vector<std::uint8_t> ReadPixel(re4gx::GxRenderer &r, int x, int y) {
  std::vector<std::uint8_t> rgba;
  unsigned w = 0, h = 0;
  if (!r.ReadXfb(&rgba, &w, &h) || x < 0 || y < 0 || static_cast<unsigned>(x) >= w || static_cast<unsigned>(y) >= h)
    return {0, 0, 0, 0};
  const std::size_t at = (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
  return {rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

void TestBloom(re4gx::GxRenderer &r, const char *tag) {
  const std::uint8_t kRoom[4] = {40, 40, 40, 255}, kMid[4] = {120, 120, 120, 255};
  auto scene = [&](bool letterbox, bool still) {
    Builder b;
    Baseline(b);
    DisplayCopy(b, true, kBlack);
    if (letterbox) {
      b.Bp(0x20, (342 + 56) | 342u << 12);
      b.Bp(0x21, (342 + 391) | (342u + 639) << 12);
    }
    const float n = 100.0f, f = 1000000.0f;
    b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
    b.Xf(0x1026, {0});
    QuadAt(b, 0, 0, 640, 528, 20000.0f, kRoom);
    QuadAt(b, 200, letterbox ? 60 : 200, 240, letterbox ? 100 : 240, 1000.0f, kWhite);
    QuadAt(b, 400, 300, 440, 340, 1000.0f, kMid);
    // the HUD: orthographic after the scene
    b.XfF(0x1020, {1.0f / 320.0f, -1.0f, -2.0f / 528.0f, 1.0f, -1.0f, -1.0f});
    b.Xf(0x1026, {1});
    b.Bp(0x40, 0);  // no depth test for the HUD
    Quad(b, 500, 100, 540, 140, 0, kWhite);
    if (still)
      b.flags |= RE4_GX_CHUNK_STILL;
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
  };
  const int s = r.Scale();
  auto at = [&](int x, int y) { return ReadPixel(r, x * s, y * s); };
  char what[160];
  r.SetBloom(0);
  scene(false, false);
  const auto offBeside = at(250, 220), offFar = at(100, 450), offMid = at(420, 320), offHud = at(550, 120);
  std::snprintf(what, sizeof(what), "bloom (%s): off, the room as drawn", tag);
  Check(Near(offBeside, 40, 40, 40) && Near(offFar, 40, 40, 40) && Near(offMid, 120, 120, 120) &&
            Near(offHud, 40, 40, 40),
        what);
  const unsigned before = r.Stats().bloomPasses;
  r.SetBloom(1);
  scene(false, false);
  const auto light = at(250, 220), lightFar2 = at(262, 220);
  std::fprintf(stderr, "  bloom (%s) light: beside the lamp %d (off %d), 22 pixels away %d; far %d, the grey %d, beside the "
               "HUD %d\n", tag, light[1], offBeside[1], lightFar2[1], at(100, 450)[1], at(420, 320)[1], at(550, 120)[1]);
  std::snprintf(what, sizeof(what), "bloom (%s): light, one pass, the lamp's surroundings brighter", tag);
  Check(r.Stats().bloomPasses == before + 1 && light[1] >= offBeside[1] + 10, what);
  std::snprintf(what, sizeof(what), "bloom (%s): light, the room away from the lamp, the grey object and the HUD's "
                "surroundings stay", tag);
  Check(Near(at(100, 450), 40, 40, 40) && Near(at(420, 320), 120, 120, 120) && Near(at(550, 120), 40, 40, 40) &&
            Near(at(520, 120), 255, 255, 255),
        what);
  r.SetBloom(2);
  scene(false, false);
  const auto strong = at(250, 220), strongFar2 = at(262, 220);
  std::fprintf(stderr, "  bloom (%s) strong: beside the lamp %d, 22 pixels away %d\n", tag, strong[1], strongFar2[1]);
  std::snprintf(what, sizeof(what), "bloom (%s): strong, brighter and farther than light", tag);
  Check(strong[1] > light[1] && strongFar2[1] > lightFar2[1] && Near(at(550, 120), 40, 40, 40), what);
  scene(true, false);
  std::snprintf(what, sizeof(what), "bloom (%s): the letterbox bar above the lamp stays black", tag);
  Check(Near(at(220, 50), 0, 0, 0, 1) && at(250, 80)[1] > 45, what);
  const unsigned beforeStill = r.Stats().bloomPasses;
  scene(false, true);
  std::snprintf(what, sizeof(what), "bloom (%s): none on a menu screen of the game", tag);
  Check(r.Stats().bloomPasses == beforeStill && Near(at(250, 220), 40, 40, 40), what);
  r.SetBloom(0);
  scene(false, false);
  std::snprintf(what, sizeof(what), "bloom (%s): off again, the room as drawn", tag);
  Check(at(250, 220) == offBeside, what);
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
  // the render thread's split (GxStats::splitNs): the kinds add up to the Run's time, the draw calls,
  // uniform blocks and binds of a chunk of 2400 draws each take some of it
  {
    const re4gx::GxStats before = r.Stats();
    const auto from = std::chrono::steady_clock::now();
    const auto res = chunk(2400, false);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count() * 1e9;
    const re4gx::GxStats &s = r.Stats();
    double sum = 0;
    for (int k = 0; k < re4gx::GxStats::kSplitKinds; k++)
      sum += static_cast<double>(s.splitNs[k] - before.splitNs[k]);
    const auto part = [&](int k) { return s.splitNs[k] - before.splitNs[k]; };
    std::fprintf(stderr, "  %s split of 2400 draws: %.2f of %.2f ms (draw calls %.2f, uniform %.2f, binds %.2f, rest %.2f)\n",
                 tag, sum / 1e6, wall / 1e6, part(re4gx::GxStats::kSplitDraw) / 1e6,
                 part(re4gx::GxStats::kSplitUniform) / 1e6, part(re4gx::GxStats::kSplitBind) / 1e6,
                 part(re4gx::GxStats::kSplitRest) / 1e6);
    std::snprintf(what, sizeof(what), "%s: render thread split adds up to the Run (%.2f of %.2f ms)", tag, sum / 1e6,
                  wall / 1e6);
    Check(res.first && sum > 0 && sum <= wall && part(re4gx::GxStats::kSplitDraw) > 0 &&
              part(re4gx::GxStats::kSplitUniform) > 0 && part(re4gx::GxStats::kSplitBind) > 0,
          what);
  }
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
  TestTvFilter(r);
  TestSharpen(r);
  TestCrtColour(r);
  TestBloom(r, r.TiledMsaa() ? (lose ? "GL MSAA on the tiles, everything lost" : "GL MSAA on the tiles")
                             : "GL MSAA");
  TestPsx(r);
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
  // the new one: where it is in B (-7000) moved back by half the camera's move (1500, the room's pieces agree on
  // it), as it stands still in the world: -7750 (BlendByCamera, 04.11.2026; as in B it stood apart from the
  // room between the two); halfway to the dead first one's place it would be at -6000
  const auto fresh = TexPixel(xfb, px(-7750.0f), 264), between = TexPixel(xfb, px(-6000.0f), 264),
             asInB = TexPixel(xfb, px(-6800.0f), 264);
  Check(Near(fresh, 0, 255, 0) && Near(between, 0, 0, 0) && Near(asInB, 0, 0, 0),
        "particles: one that has just started moves with the camera from where it is in B");
  if (!Near(own, 0, 255, 0) || !Near(wrong, 0, 0, 0) || !Near(third, 0, 0, 0) || !Near(fresh, 0, 255, 0) ||
      !Near(between, 0, 0, 0))
    Print("own", own), Print("wrong", wrong), Print("third", third), Print("fresh", fresh), Print("between", between);
  Check(glGetError() == GL_NO_ERROR, "particles: no GL error");
  r.Release();
}

// Owner tags (60fps-plan.md S1/S2, patch 0137): two alike particles swap their order from A to B, each
// moved 1000 to the left; by look and order they would take each other's places (both halfway at
// -1500), by their objects each goes halfway along its own move (-500, -2500). A third one's slot is
// used anew in B (fewer life frames): it stands where it is in B (+6000), not halfway (+4500). With
// `tags` false the same scene without the tags (the counter-check: the old way).
void OwnerScene(Builder &b, std::uint32_t sequence, bool second, bool tags, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  auto particle = [&](std::uint32_t object, std::uint32_t life, float x) {
    if (tags)
      b.Owner(object, 0x81234560u, 0x12, life);
    b.XfF(0, {1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, -20000.0f});
    std::vector<std::uint8_t> v;
    V(v, -250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, 250.0f, 0.0f, kGreen);
    V(v, -250.0f, 250.0f, 0.0f, kGreen);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  };
  if (!second) {
    particle(0x80100000u, 0x00050010u, -2000.0f);
    particle(0x80100150u, 0x00050011u, 0.0f);
    particle(0x801002A0u, 0x00050028u, 3000.0f);
  } else {
    particle(0x80100150u, 0x00050012u, -1000.0f);
    particle(0x80100000u, 0x00050011u, -3000.0f);
    particle(0x801002A0u, 0x00050002u, 6000.0f);
  }
  if (tags)
    b.Owner(0, 0, 0xFF);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationOwners() {
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 20000.0f * 320.0f)); };
  for (int tags = 1; tags >= 0; tags--) {
    std::string error;
    re4gx::GxRenderer r;
    Require(r.Init(1, &error), error.c_str());
    r.SetRecording(true);
    Builder ba, bb;
    Re4GxChunk a, b;
    OwnerScene(ba, 1, false, tags != 0, &a);
    OwnerScene(bb, 2, true, tags != 0, &b);
    r.Run(a);
    Check(r.Interpolate(a, b, 0.5f), tags ? "owners: the in-between picture is shown" : "owners: (without tags) shown");
    const GLuint xfb = r.Xfb(nullptr, nullptr);
    const auto own1 = TexPixel(xfb, px(-500.0f), 264), own2 = TexPixel(xfb, px(-2500.0f), 264),
               swapped = TexPixel(xfb, px(-1500.0f), 264), fresh = TexPixel(xfb, px(6000.0f), 264),
               between = TexPixel(xfb, px(4500.0f), 264);
    const bool right = Near(own1, 0, 255, 0) && Near(own2, 0, 255, 0) && Near(swapped, 0, 0, 0) &&
                       Near(fresh, 0, 255, 0) && Near(between, 0, 0, 0);
    if (tags) {
      Check(right, "owners: alike particles keep their own objects, a slot used anew stands where it is in B");
      Check(r.Stats().interpOwned == 2 && r.Stats().interpOwnedNew == 1, "owners: two found by their object, one new");
      if (!right)
        Print("own1", own1), Print("own2", own2), Print("swapped", swapped), Print("fresh", fresh),
            Print("between", between);
    } else {
      // the counter-check: without the tags the old way takes the first alike one (both at -1500)
      Check(!right && Near(swapped, 0, 255, 0), "owners: without tags the alike particles swap (counter-check)");
    }
    Check(glGetError() == GL_NO_ERROR, "owners: no GL error");
    r.Release();
  }
}

// A weather particle that wraps around the camera (esp15.cpp, id 0x15; the project owner's fog in r227, GX series
// 08.11.2026: a sprite 7 m to the side): A +3500, B -3500, the same object and life. Halfway (0) it stood where it is
// in neither picture, a brighter fog bank in each such in-between picture; now it stands where it is in B. A second
// one drifts 1000 to the left (from -6000 to -7000) and still goes halfway (-6500).
void WrapScene(Builder &b, std::uint32_t sequence, bool second, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  auto particle = [&](std::uint32_t object, float x) {
    b.Owner(object, 0x81234560u, 0x15, 0x00150ed7u);
    b.XfF(0, {1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, -20000.0f});
    std::vector<std::uint8_t> v;
    V(v, -250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, 250.0f, 0.0f, kGreen);
    V(v, -250.0f, 250.0f, 0.0f, kGreen);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  };
  particle(0x80100000u, second ? -3500.0f : 3500.0f);
  particle(0x80100150u, second ? -7000.0f : -6000.0f);
  b.Owner(0, 0, 0xFF);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationWrap() {
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 20000.0f * 320.0f)); };
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb;
  Re4GxChunk a, b;
  WrapScene(ba, 1, false, &a);
  WrapScene(bb, 2, true, &b);
  r.Run(a);
  Check(r.Interpolate(a, b, 0.5f), "wrap: the in-between picture is shown");
  const GLuint xfb = r.Xfb(nullptr, nullptr);
  const auto asInB = TexPixel(xfb, px(-3500.0f), 264), halfway = TexPixel(xfb, px(0.0f), 264),
             asInA = TexPixel(xfb, px(3500.0f), 264), drift = TexPixel(xfb, px(-6500.0f), 264);
  const bool right = Near(asInB, 0, 255, 0) && Near(halfway, 0, 0, 0) && Near(asInA, 0, 0, 0) && Near(drift, 0, 255, 0);
  Check(right, "wrap: a particle that wrapped around stands where it is in B, a drifting one halfway");
  Check(r.Stats().interpOwnedJumps == 1 && r.Stats().interpOwned == 1, "wrap: one jumped, one found by its object");
  if (!right)
    Print("asInB", asInB), Print("halfway", halfway), Print("asInA", asInA), Print("drift", drift);
  Check(glGetError() == GL_NO_ERROR, "wrap: no GL error");
  r.Release();
}

// A room part of 24 vertices (not a particle) whose texture matrix index changes from A (identity) to B
// (row 36), as a house door in r101 did (Retroid series 01.11.2026), moved 2000 to the right by the camera:
// by its object it goes halfway (-1000), not where it is in B (0). With `tags` false the counter-check: its
// look differs, it stands where it is in B.
void RoomPartScene(Builder &b, std::uint32_t sequence, bool second, bool tags, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  if (tags)
    b.Owner(0x80200000u, 0x81234570u, 0x10);
  b.XfF(0, {1, 0, 0, second ? 0.0f : -2000.0f, 0, 1, 0, 0, 0, 0, 1, -20000.0f});
  std::vector<std::uint8_t> v;
  for (int q = 0; q < 4; q++) {
    V(v, -250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, 250.0f, 0.0f, kGreen);
    V(v, -250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, 250.0f, 0.0f, kGreen);
    V(v, -250.0f, 250.0f, 0.0f, kGreen);
  }
  b.Draw(RE4_GX_V_COLOR0, 16, v, 24, 0x90, RE4_GX_TRIANGLES, second ? 36 : 60);
  if (tags)
    b.Owner(0, 0, 0xFF);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationRoomPart() {
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 20000.0f * 320.0f)); };
  for (int tags = 1; tags >= 0; tags--) {
    std::string error;
    re4gx::GxRenderer r;
    Require(r.Init(1, &error), error.c_str());
    r.SetRecording(true);
    Builder ba, bb;
    Re4GxChunk a, b;
    RoomPartScene(ba, 1, false, tags != 0, &a);
    RoomPartScene(bb, 2, true, tags != 0, &b);
    r.Run(a);
    // without the tags nothing is matched: no in-between picture (as a cut), B stays
    const bool shown = r.Interpolate(a, b, 0.5f);
    if (tags)
      Check(shown, "room part: the in-between picture is shown");
    const GLuint xfb = r.Xfb(nullptr, nullptr);
    const auto half = TexPixel(xfb, px(-1000.0f), 264), next = TexPixel(xfb, px(200.0f), 264);
    if (tags) {
      Check(Near(half, 0, 255, 0) && Near(next, 0, 0, 0),
            "room part: another texture matrix index, by its object halfway along its move");
      if (!Near(half, 0, 255, 0) || !Near(next, 0, 0, 0))
        Print("half", half), Print("next", next);
    } else {
      Check(Near(half, 0, 0, 0) && Near(next, 0, 255, 0), "room part: without tags as in B (counter-check)");
    }
    Check(glGetError() == GL_NO_ERROR, "room part: no GL error");
    r.Release();
  }
}

// A new particle in B (a slot that drew nothing in A, the sparkles of a turn of the r307 puzzle) drawn
// before three alike ones that stand still: it stands where it is in B (-2000), and the three keep their
// own places. Guessed, it took the first one's place (halfway at -3000), the first then the second's
// (-2000) and so on down the line: ghosts between the puzzle's fields (30.10.2026).
void SparkleScene(Builder &b, std::uint32_t sequence, bool second, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  auto particle = [&](std::uint32_t object, std::uint32_t life, float x) {
    b.Owner(object, 0x81234560u, 0x12, life);
    b.XfF(0, {1, 0, 0, x, 0, 1, 0, 0, 0, 0, 1, -20000.0f});
    std::vector<std::uint8_t> v;
    V(v, -250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, 250.0f, 0.0f, kGreen);
    V(v, -250.0f, 250.0f, 0.0f, kGreen);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  };
  if (second)
    particle(0x80100540u, 0x00070001u, -2000.0f);
  particle(0x80100000u, second ? 0x00050011u : 0x00050010u, -4000.0f);
  particle(0x80100150u, second ? 0x00050011u : 0x00050010u, 0.0f);
  particle(0x801002A0u, second ? 0x00050011u : 0x00050010u, 4000.0f);
  b.Owner(0, 0, 0xFF);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationSparkle() {
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 20000.0f * 320.0f)); };
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb;
  Re4GxChunk a, b;
  SparkleScene(ba, 1, false, &a);
  SparkleScene(bb, 2, true, &b);
  r.Run(a);
  Check(r.Interpolate(a, b, 0.5f), "sparkle: the in-between picture is shown");
  const GLuint xfb = r.Xfb(nullptr, nullptr);
  const auto fresh = TexPixel(xfb, px(-2000.0f), 264), between = TexPixel(xfb, px(-3000.0f), 264),
             p1 = TexPixel(xfb, px(-4000.0f), 264), p2 = TexPixel(xfb, px(0.0f), 264),
             p3 = TexPixel(xfb, px(4000.0f), 264), off = TexPixel(xfb, px(2000.0f), 264);
  const bool right = Near(fresh, 0, 255, 0) && Near(between, 0, 0, 0) && Near(p1, 0, 255, 0) &&
                     Near(p2, 0, 255, 0) && Near(p3, 0, 255, 0) && Near(off, 0, 0, 0);
  Check(right, "sparkle: a new particle stands where it is in B, the others keep their places");
  Check(r.Stats().interpOwned == 3 && r.Stats().interpOwnedNew == 1, "sparkle: three found by their object, one new");
  if (!right)
    Print("fresh", fresh), Print("between", between), Print("p1", p1), Print("p2", p2), Print("p3", p3),
        Print("off", off);
  Check(glGetError() == GL_NO_ERROR, "sparkle: no GL error");
  r.Release();
}

// The laser sight: the game makes its beam (a GX line, effect id 0x19) and dot anew every picture (life 1, another
// object each time), moved by their vertices while the view stays. In the in-between picture each is the one of
// its id before, halfway between the two: the beam (x -4000 -> 0, more than the 1500 units a figure may move) at
// -2000, the dot (a quad, -1000 -> 0) at -500. Taken for new, both stood where they are in B (users 06.11.2026:
// the laser briefly doubled while it moved).
void LaserScene(Builder &b, std::uint32_t sequence, float beamX, float dotX, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  b.Owner(0x80400000u + sequence * 0x200u, 0x81234590u, 0x11, 0x00190001u);
  b.XfF(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -20000.0f});
  std::vector<std::uint8_t> beam;
  V(beam, beamX, -3000.0f, 0.0f, kGreen);
  V(beam, beamX, 3000.0f, 0.0f, kGreen);
  b.Draw(RE4_GX_V_COLOR0, 16, beam, 2, 0xA8, RE4_GX_LINES);
  b.Owner(0x80400100u + sequence * 0x200u, 0x812345A0u, 0x11, 0x00000001u);
  std::vector<std::uint8_t> dot;
  V(dot, dotX - 250.0f, -250.0f, 0.0f, kGreen);
  V(dot, dotX + 250.0f, -250.0f, 0.0f, kGreen);
  V(dot, dotX + 250.0f, 250.0f, 0.0f, kGreen);
  V(dot, dotX - 250.0f, 250.0f, 0.0f, kGreen);
  b.Draw(RE4_GX_V_COLOR0, 16, dot, 4, 0x80);
  b.Owner(0, 0, 0xFF);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationOneFrame() {
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 20000.0f * 320.0f)); };
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb;
  Re4GxChunk a, b;
  LaserScene(ba, 1, -4000.0f, -1000.0f, &a);
  LaserScene(bb, 2, 0.0f, 0.0f, &b);
  r.Run(a);
  Check(r.Interpolate(a, b, 0.5f), "laser: the in-between picture is shown");
  const GLuint xfb = r.Xfb(nullptr, nullptr);
  // the 1-pixel beam: green within a pixel of its place
  auto beamAt = [&](float x) {
    for (int dx = -1; dx <= 1; dx++)
      if (Near(TexPixel(xfb, px(x) + dx, 264), 0, 255, 0, 60))
        return true;
    return false;
  };
  const auto dotHalf = TexPixel(xfb, px(-500.0f), 264), dotB = TexPixel(xfb, px(200.0f), 264);
  const bool right = beamAt(-2000.0f) && !beamAt(0.0f) && !beamAt(-4000.0f) && Near(dotHalf, 0, 255, 0) &&
                     Near(dotB, 0, 0, 0);
  Check(right, "laser: beam and dot made anew each picture lie halfway between A and B");
  Check(r.Stats().interpOneFrame == 2, "laser: both found as the one-frame effects of their ids");
  if (!right)
    std::printf("  beam at -2000 %d, at 0 %d, at -4000 %d\n", beamAt(-2000.0f), beamAt(0.0f), beamAt(-4000.0f)),
        Print("dot halfway", dotHalf), Print("dot as in B", dotB);
  Check(glGetError() == GL_NO_ERROR, "laser: no GL error");
  r.Release();
}

// An object whose number of quads changes from one picture to the next (a flame's trail of sprites):
// not matched by "n-th draw of the object" (its sprites would take each other's places), the old way
// instead; with the same number as before by its object as usual.
void TrailScene(Builder &b, std::uint32_t sequence, unsigned quads, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  b.Owner(0x80300000u, 0x81234580u, 0x12, 0x00070000u | sequence);
  for (unsigned q = 0; q < quads; q++) {
    b.XfF(0, {1, 0, 0, -3000.0f + 2000.0f * static_cast<float>(q), 0, 1, 0, 0, 0, 0, 1, -20000.0f});
    std::vector<std::uint8_t> v;
    V(v, -250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, -250.0f, 0.0f, kGreen);
    V(v, 250.0f, 250.0f, 0.0f, kGreen);
    V(v, -250.0f, 250.0f, 0.0f, kGreen);
    b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80);
  }
  b.Owner(0, 0, 0xFF);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationTrail() {
  for (unsigned second = 2; second <= 3; second++) {
    std::string error;
    re4gx::GxRenderer r;
    Require(r.Init(1, &error), error.c_str());
    r.SetRecording(true);
    Builder ba, bb;
    Re4GxChunk a, b;
    TrailScene(ba, 1, 2, &a);
    TrailScene(bb, 2, second, &b);
    r.Run(a);
    r.Interpolate(a, b, 0.5f);
    const auto &st = r.Stats();
    if (second == 2)
      Check(st.interpOwned == 2 && st.interpOwnedRecount == 0, "trail: the same number of quads: matched by the object");
    else
      Check(st.interpOwned == 0 && st.interpOwnedRecount == 3,
            "trail: another number of quads: not by \"n-th draw\", the old way");
    Check(glGetError() == GL_NO_ERROR, "trail: no GL error");
    r.Release();
  }
}

// With the object sure (S3): a texture matrix between the two (0.1 -> 0.3: blended once), not over
// the texture's edge (0.9 -> 0.05: left out), and the TEV colours between the two; without the tags
// none of it (counter-check).
void ExtrasScene(Builder &b, std::uint32_t sequence, bool second, bool tags, float shift, Re4GxChunk *out) {
  Baseline(b);
  b.flags = sequence == 1 ? RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_XFB : RE4_GX_CHUNK_XFB;
  DisplayCopy(b, true, kBlack);
  const float n = 100.0f, f = 1000000.0f;
  b.XfF(0x1020, {1.0f, 0.0f, 1.0f, 0.0f, -n / (f - n), -f * n / (f - n)});
  b.Xf(0x1026, {0});
  if (tags)
    b.Owner(0x80200000u, 0x81234570u, 3);
  b.XfF(0, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -20000.0f});
  // texture matrix 0 (XF rows 30..32): a shift in s
  b.XfF(30 * 4, {1, 0, 0, shift, 0, 1, 0, 0, 0, 0, 1, 0});
  // TEV colour register c0 (BP 0xE2 RA, 0xE3 BG): red in A, blue in B
  b.Bp(0xE2, second ? 0x000000u : 0x0FF000u);
  b.Bp(0xE3, second ? 0x0FF000u : 0x000000u);
  // indirect matrix 0 (BP 0x06..0x08), the same scale: element 0 from 16 to 48
  b.Bp(0x06, second ? 0x000030u : 0x000010u);
  b.Bp(0x07, 0x000000u);
  b.Bp(0x08, 0x000000u);
  std::vector<std::uint8_t> v;
  V(v, -2000.0f, -2000.0f, 0.0f, kGreen);
  V(v, 2000.0f, -2000.0f, 0.0f, kGreen);
  V(v, 2000.0f, 2000.0f, 0.0f, kGreen);
  V(v, -2000.0f, 2000.0f, 0.0f, kGreen);
  b.Draw(RE4_GX_V_COLOR0, 16, v, 4, 0x80, RE4_GX_TRIANGLES, 30);
  DisplayCopy(b, false, kBlack);
  *out = b.Chunk();
  out->sequence = sequence;
}

void TestInterpolationExtras() {
  struct Case {
    bool tags;
    float a, b;
    unsigned blends, wraps;
    const char *what;
  } cases[] = {
      {true, 0.1f, 0.3f, 1, 0, "extras: a texture matrix of the same object between the two"},
      {true, 0.9f, 0.05f, 0, 1, "extras: a texture matrix over its edge left as in B"},
      {false, 0.1f, 0.3f, 0, 0, "extras: without tags no texture matrix blended (counter-check)"},
  };
  for (const Case &c : cases) {
    std::string error;
    re4gx::GxRenderer r;
    Require(r.Init(1, &error), error.c_str());
    r.SetRecording(true);
    Builder ba, bb;
    Re4GxChunk a, b;
    ExtrasScene(ba, 1, false, c.tags, c.a, &a);
    ExtrasScene(bb, 2, true, c.tags, c.b, &b);
    r.Run(a);
    r.Interpolate(a, b, 0.5f);
    const auto &st = r.Stats();
    Check(st.interpTexBlends == c.blends && st.interpTexWraps == c.wraps, c.what);
    Check(st.interpTevBlends == (c.tags ? 1u : 0u),
          c.tags ? "extras: the TEV colours of the same object between the two" : "extras: without tags the TEV colours as in B");
    Check(st.interpIndBlends == (c.tags ? 1u : 0u),
          c.tags ? "extras: the indirect matrix of the same object between the two"
                 : "extras: without tags the indirect matrix as in B");
    if (st.interpTexBlends != c.blends || st.interpTexWraps != c.wraps)
      std::printf("    texture matrices %u blended, %u over the edge\n", st.interpTexBlends, st.interpTexWraps);
    Check(glGetError() == GL_NO_ERROR, "extras: no GL error");
    r.Release();
  }
}

// 60 FPS at an even pace (S4): single left-out pictures and short bursts (the core a picture ahead
// after a pause) leave it on; a quarter of the chunks left out over 4 s stops the in-between pictures
// for 5 s, then they come back, and the first 2 s after it do not count.
// The game's own pace (ChunkRate): 30 chunks a second are not 60 (each noted once), 60 are.
void TestChunkRate() {
  re4gx::ChunkRate r;
  double t = 0.0;
  for (int i = 0; i < 90; i++, t += 1.0 / 30.0)
    r.Note(t);
  Check(!r.Sixty(t), "chunk rate: 30 game pictures a second are not the game's own 60");
  for (int i = 0; i < 90; i++, t += 1.0 / 60.0)
    r.Note(t);
  Check(r.Sixty(t), "chunk rate: 60 chunks a second (a movie, the title) are");
}

void TestSmoothPacer() {
  re4gx::SmoothPacer p;
  std::string why;
  double t = 0.0;
  bool all = true;
  for (int i = 0; i < 600; i++, t += 1.0 / 30.0) {
    const bool behind = i % 90 == 0 || (i % 150) < 3;  // one every 3 s, a burst of 3 every 5 s
    p.Chunk(t, behind, &why);
    all = all && !p.Off();
  }
  Check(all && p.Offs() == 0, "pacer: single ones and short bursts left out: 60 FPS stays on");
  why.clear();
  bool off = false;
  for (int i = 0; i < 150 && !off; i++, t += 1.0 / 30.0) {
    p.Chunk(t, i % 3 == 0, &why);  // a third of the chunks for up to 5 s
    off = p.Off();
  }
  Check(off && p.Offs() == 1 && why.find("no in-between pictures") != std::string::npos,
        "pacer: a third left out over 4 s: off, with a log line");
  Check(!p.Chunk(t + 3.0, false, nullptr), "pacer: still off 3 s later");
  why.clear();
  t += 6.0;
  Check(p.Chunk(t, false, &why) && !p.Off() && why.find("on again") != std::string::npos,
        "pacer: on again after 5 s, with a log line");
  // right after it the core is a picture ahead: those first chunks do not start the next pause
  for (int i = 0; i < 6; i++, t += 1.0 / 30.0)
    p.Chunk(t, true, nullptr);
  Check(!p.Off() && p.Offs() == 1, "pacer: the catch-up right after a pause does not count");
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

// reuse (with `second`): 1 = the core forgot A's flame texture (0x1111) and sends it again in this chunk,
// 2 = binds it by key alone (the renderer should still have it)
void EffectsScene(Builder &b, std::uint32_t sequence, bool second, Re4GxChunk *out, int reuse = 0) {
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
  if (second && reuse == 1) {
    const std::uint32_t forget[2] = {0x1111, 0};
    b.Op(RE4_GX_OP_FORGET, forget, 2);
    b.Texture(0, 0x1111, 0, 0x110000, 4, 4, 6, SolidRgba8(200, 0, 0));
  } else if (second && reuse == 2)
    b.Texture(0, 0x1111, 0, 0x110000, 4, 4, 6, {});
  else if (second)
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

// The core forgets a texture at the start of a chunk and sends it again in the same chunk (its budget,
// gx_decode.cpp). The in-between pass uploads it ahead and leaves the forget to the real Run; that Run
// must upload it again after the forget, else the texture is gone for good and draws white (itel P661N,
// 28.10.2026: Ganados with white weapons after some 20 minutes with 60 FPS).
void TestInterpolationForget() {
  std::string error;
  re4gx::GxRenderer r;
  Require(r.Init(1, &error), error.c_str());
  r.SetRecording(true);
  Builder ba, bb, bc;
  Re4GxChunk a, b, c;
  EffectsScene(ba, 1, false, &a);
  EffectsScene(bb, 2, true, &b, 1);
  EffectsScene(bc, 3, true, &c, 2);
  auto px = [](float x) { return static_cast<int>(std::lround(320.0f + x / 3000.0f * 320.0f)); };
  r.Run(a);
  r.Interpolate(a, b, 0.5f);
  r.Run(b);
  const auto flameB = TexPixel(r.Xfb(nullptr, nullptr), px(600.0f), 400);
  r.Interpolate(b, c, 0.5f);
  r.Run(c);
  const auto flameC = TexPixel(r.Xfb(nullptr, nullptr), px(600.0f), 400);
  Check(Near(flameB, 200, 0, 0, 3) && Near(flameC, 200, 0, 0, 3) && r.Stats().missingTextures == 0,
        "forget: a texture forgotten and sent again in one chunk stays after the in-between picture");
  if (!Near(flameB, 200, 0, 0, 3) || !Near(flameC, 200, 0, 0, 3) || r.Stats().missingTextures)
    Print("flameB", flameB), Print("flameC", flameC),
        std::printf("  missing textures: %u\n", r.Stats().missingTextures);
  Check(glGetError() == GL_NO_ERROR, "forget: no GL error");
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
// (api Vulkan: read through a display copy of the whole EFB, rows as GL's EFB)
std::vector<std::uint8_t> TevMathScene(std::uint32_t seed, unsigned *shaderErrors,
                                       re4gx::GxApi api = re4gx::GxApi::kGl) {
  re4gx::GxRenderer r(api);
  Require(r.Api() == api, "TEV math scene: the renderer asked for");
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
  std::vector<std::uint8_t> p;
  if (api == re4gx::GxApi::kGl) {
    r.Run(b.Chunk());
    p = Efb(r);
  } else {
    DisplayCopy(b, false, kBlack);
    r.Run(b.Chunk());
    unsigned w = 0, h = 0;
    Require(r.ReadXfb(&p, &w, &h) && w == 640 && h == 528, "TEV math scene: the display copy read back");
  }
  *shaderErrors += r.Stats().shaderErrors;
  r.Release();
  return p;
}

// the Galaxy A36's wrong movie pixels (report 22.10.2026 19:30), as the check writes them
const char *const kA36Pixels[6] = {
    "4 stages, Y 128 U 128 V 128: 0 131 0 0, want 131 131 132 0",
    "4 stages, Y 200 U 128 V 128: 0 215 0 0, want 215 215 216 0",
    "4 stages, Y 235 U 128 V 128: 0 255 0 0, want 255 255 255 0",
    "4 stages, Y 180 U 90 V 200: 0 148 116 0, want 255 148 116 0",
    "4 stages, Y 100 U 200 V 60: 0 125 0 0, want 0 125 244 0",
    "4 stages, Y 235 U 240 V 240: 0 121 0 0, want 255 121 255 0"};

// The ways of writing the TEV's lerp (gx_gl_shader.h SetTevMath). The Galaxy A36 (Adreno 710)
// computed a << 8 in 16 bits: written so on purpose, the movie check gives exactly that phone's six
// wrong pixels (report 22.10.2026 19:30), and the check then picks the way without the shift. The
// three real ways give the same pictures for random TEV setups.
void TestTevMath() {
  std::string detail, error;
  re4gx::SetTevMath(re4gx::kTevMathBroken16);
  const int bad = re4gx::MovieColourPixels(&detail, &error);
  Check(bad == 6, "TEV math: a << 8 in 16 bits gives six wrong movie pixels, as on the Galaxy A36");
  bool same = true;
  for (const char *w : kA36Pixels)
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
// a renderer keeps each program's key next to its binary and the keys a room drew with in the room's
// list (01.11.2026: before, every program of the cache; a Mi 11 ran out of mappings with 10242); a new
// renderer in that room makes them before any draw, and the same draws then make none. A key under an
// old name (the sources changed) is linked anew and its old files go.
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
// CanonicalShaderKey (0.99.5): what the shaders do not use set to 0, what they use kept
void TestCanonicalKey() {
  re4gx::ShaderKey a{};
  a.numStages = 2;
  a.colorEnv[0] = 0x0008FA;  // inputs a 0, b 8, c 15, d 10: none KONST (14)
  a.alphaEnv[0] = (1u << 13) | (2u << 10) | (3u << 7) | (4u << 4) | 0u;  // inputs 1..4 (KONST is 6), swap tables 0, 0
  a.colorEnv[1] = 14u << 12;  // input a: KONST colour
  a.alphaEnv[1] = (6u << 4) | 1u | (2u << 2);  // input d: KONST alpha, swap tables 1 and 2
  a.konst[0] = 0x1D0D;
  a.konst[1] = 0x1C0C;
  a.swap[0] = 0x11, a.swap[1] = 0x22, a.swap[2] = 0x33, a.swap[3] = 0x44;
  a.fog = 1;  // projection without fog
  const re4gx::ShaderKey c = re4gx::CanonicalShaderKey(a);
  Check(c.konst[0] == 0 && c.konst[1] == 0x1C0C, "canonical key: KONST kept only where an input takes it");
  Check(c.swap[0] == 0x11 && c.swap[1] == 0x22 && c.swap[2] == 0x33 && c.swap[3] == 0,
        "canonical key: the swap tables the stages select kept, the others 0");
  Check(c.fog == 0, "canonical key: no fog, no projection bit");
  re4gx::ShaderKey b = a;
  b.konst[0] = 0x0606;  // another unused selection: the same canonical key
  b.swap[3] = 0x55;
  const re4gx::ShaderKey cb = re4gx::CanonicalShaderKey(b);
  Check(std::memcmp(&c, &cb, sizeof(c)) == 0, "canonical key: keys that differ in unused fields alike");
  b.konst[1] = 0x1C0D;  // the KONST colour stage 1 takes: a different shader
  const re4gx::ShaderKey cd = re4gx::CanonicalShaderKey(b);
  Check(std::memcmp(&c, &cd, sizeof(c)) != 0, "canonical key: a used selection still tells keys apart");
  a.fog = 0xE;
  Check(re4gx::CanonicalShaderKey(a).fog == 0xE, "canonical key: fog kept");
}

#ifdef RE4_GX_VULKAN
// The app's pipeline list made ahead (0.99.5, gx_vk.cpp AheadThread): a Vulkan renderer draws the random TEV setups in
// room 0x227 and leaves its room list (room-227.vkpipes); that list as the app's list (RE4VKL01, as
// port/tools/gx_vkpipe_list.py writes it) goes through the thread of a second renderer on an empty cache folder while it
// draws: every pipeline made, the list marked; a third renderer on that folder leaves it out (the mark), one with merged
// The movie check under Vulkan (an Adreno 840, Xiaomi, report 13.11.2026: the movies green under Vulkan only, as on
// the Galaxy A36 under GLES; the check ran only under GLES). Through Vulkan's read-back: right with the hardware's
// way; the A36's 16-bit way gives the same six pixels and the check then picks the lerp without the shift; GL and
// Vulkan draw the random TEV setups byte for byte the same in each of the three real ways (and both see the 16-bit
// way).
void TestVkTevMath() {
  re4gx::SetTevMath(re4gx::kTevMathShift);
  int wrong = -1;
  std::string line = re4gx::MovieColourCheck(&wrong, re4gx::GxApi::kVulkan);
  std::fprintf(stderr, "%s\n", line.c_str());
  Check(wrong == 0 && re4gx::GetTevMath() == re4gx::kTevMathShift &&
            line == "GPU check (movie colours, Vulkan): ok, 32 of 32 pixels as the GameCube computes them",
        "Vulkan TEV math: the movie colours as the GameCube computes them");
  std::string detail, error;
  re4gx::SetTevMath(re4gx::kTevMathBroken16);
  const int bad = re4gx::MovieColourPixels(&detail, &error, re4gx::GxApi::kVulkan);
  bool same = bad == 6;
  for (const char *w : kA36Pixels)
    same = same && detail.find(w) != std::string::npos;
  Check(same, "Vulkan TEV math: a << 8 in 16 bits gives the Galaxy A36's six pixels");
  if (!same)
    std::fprintf(stderr, "  emulated (%d, %s): %s\n", bad, error.c_str(), detail.c_str());
  line = re4gx::MovieColourCheck(&wrong, re4gx::GxApi::kVulkan);
  std::fprintf(stderr, "  emulated A36 under Vulkan: %s\n", line.c_str());
  Check(wrong == 0 && re4gx::GetTevMath() == re4gx::kTevMathNoShift &&
            line.find("GPU check (movie colours, Vulkan): 6 of 32 pixels wrong") == 0 &&
            line.find("in use: the lerp without the shift by 8 (the movies right)") != std::string::npos,
        "Vulkan TEV math: the check picks the lerp without the shift");
  // a second check with the way the first one chose (GL after Vulkan, or the other way round)
  line = re4gx::MovieColourCheck(&wrong, re4gx::GxApi::kVulkan);
  Check(wrong == 0 && re4gx::GetTevMath() == re4gx::kTevMathNoShift &&
            line.find(": ok, 32 of 32 pixels as the GameCube computes them; in use: the lerp without the shift") !=
                std::string::npos,
        "Vulkan TEV math: a second check keeps the way chosen");
  for (int mode : {re4gx::kTevMathShift, re4gx::kTevMathNoShift, re4gx::kTevMathFloat}) {
    re4gx::SetTevMath(mode);
    Check(re4gx::MovieColourPixels(nullptr, &error, re4gx::GxApi::kVulkan) == 0,
          "Vulkan TEV math: the movie colours right in each way");
  }
  for (std::uint32_t seed : {1u, 2u}) {
    unsigned errors = 0;
    for (int mode = re4gx::kTevMathShift; mode <= re4gx::kTevMathBroken16; mode++) {
      re4gx::SetTevMath(mode);
      const std::vector<std::uint8_t> gl = TevMathScene(seed, &errors);
      const std::vector<std::uint8_t> vk = TevMathScene(seed, &errors, re4gx::GxApi::kVulkan);
      int differ = 0, lit = 0;
      for (std::size_t i = 0; i < gl.size() && i < vk.size(); i += 4) {
        differ += std::memcmp(&gl[i], &vk[i], 4) ? 1 : 0;
        lit += vk[i] | vk[i + 1] | vk[i + 2] ? 1 : 0;
      }
      if (differ || seed == 1)
        std::fprintf(stderr, "  TEV math scene %u, %s: GL and Vulkan %d pixels differ (%d lit)\n", seed,
                     re4gx::TevMathName(mode), differ, lit);
      Check(gl.size() == vk.size() && differ == 0 && lit > 60 * 60 * 20,
            "Vulkan TEV math: random TEV setups, GL and Vulkan the same picture");
    }
    Check(errors == 0, "Vulkan TEV math: every shader compiles in each way");
  }
  re4gx::SetTevMath(re4gx::kTevMathShift);
}

// draws on goes through it again (the mark names the setting).
void TestVkAhead() {
  char a[] = "/tmp/re4-vkahead-a-XXXXXX", b[] = "/tmp/re4-vkahead-b-XXXXXX";
  Require(mkdtemp(a) != nullptr && mkdtemp(b) != nullptr, "vk ahead: folders for the caches");
  const std::string dirA = a, dirB = b, list = dirB + ".keys";
  std::string error;
  {
    re4gx::GxRenderer r(re4gx::GxApi::kVulkan);
    Require(r.Init(1, &error), error.c_str());
    Check(r.SetProgramCache(dirA), "vk ahead: the cache folder");
    r.SetRoom(0x227);
    TestShaderVariety(r);
    r.Release();
  }
  std::vector<char> room;
  if (FILE *f = std::fopen((dirA + "/room-227.vkpipes").c_str(), "rb")) {
    char buffer[65536];
    for (std::size_t n; (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0;)
      room.insert(room.end(), buffer, buffer + n);
    std::fclose(f);
  }
  std::uint32_t head[2] = {};
  if (room.size() >= 16)
    std::memcpy(head, room.data() + 8, sizeof(head));
  const std::uint32_t count = head[1];
  Check(room.size() >= 16 && std::memcmp(room.data(), "RE4VKP01", 8) == 0 && count >= 10 &&
            room.size() == 16 + static_cast<std::size_t>(count) * (head[0] + 24),
        "vk ahead: the room's pipeline list written");
  {
    FILE *f = std::fopen(list.c_str(), "wb");
    Require(f != nullptr, "vk ahead: the app's list");
    const std::uint32_t h[3] = {head[0], count, 1};
    std::fwrite("RE4VKL01", 8, 1, f);
    std::fwrite(h, sizeof(h), 1, f);
    std::fwrite(room.data() + 16, 1, room.size() - 16, f);
    const std::uint32_t r[2] = {0x227, count};
    std::fwrite(r, sizeof(r), 1, f);
    for (std::uint32_t i = 0; i < count; i++)
      std::fwrite(&i, 4, 1, f);
    std::fclose(f);
  }
  setenv("RE4_GX_VK_PIPE_LIST", list.c_str(), 1);
  auto run = [&](bool merge, const char *what, bool psx = false) {
    re4gx::GxRenderer r(re4gx::GxApi::kVulkan);
    Require(r.Init(1, &error), error.c_str());
    Check(r.SetProgramCache(dirB), "vk ahead: the second cache folder");
    // (as core_mode: the menu's settings after SetProgramCache, before the first Run, where the thread starts)
    r.SetMerge(merge);
    r.SetPsx(psx);
    r.SetRoom(0x227);
    // drawing on meanwhile (the thread made its pipelines beside the render thread)
    const auto start = std::chrono::steady_clock::now();
    unsigned made = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(60)) {
      Builder bl;
      Baseline(bl);
      DisplayCopy(bl, true, kBlack);
      Quad(bl, 0, 0, 32, 32, 0, kRed);
      DisplayCopy(bl, false, kBlack);
      r.Run(bl.Chunk());
      made = r.Stats().pipelinesAhead;
      if (made >= count || (std::string(what) == "again" && std::chrono::steady_clock::now() - start >
                                                                 std::chrono::seconds(2)))
        break;
    }
    r.Release();
    std::fprintf(stderr, "  vk ahead (%s): %u of %u made ahead\n", what, made, count);
    return made;
  };
  Check(run(false, "first") == count, "vk ahead: every pipeline of the list made ahead");
  FILE *mark = std::fopen((dirB + "/vk-ahead.done").c_str(), "rb");
  Check(mark != nullptr, "vk ahead: the list marked as made");
  if (mark)
    std::fclose(mark);
  Check(run(false, "again") == 0, "vk ahead: a list made on this driver before is left out");
  Check(run(true, "merged draws on") == count, "vk ahead: another setting (merged draws) goes through it again");
  Check(run(true, "PSone demake on", true) == count, "vk ahead: the PSone demake's keys made too (another mark)");
  unsetenv("RE4_GX_VK_PIPE_LIST");
}
#endif

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
    r.SetRoom(0x101);
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
    r.SetRoom(0x101);
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
    r.SetRoom(0x101);
    while (r.Prewarm(1000.0)) {
    }
    const re4gx::GxStats s = r.Stats();
    Check(s.programsAhead == made && s.programsLoaded == made && s.programs == made,
          "prewarm: after the warm the room's programs from the cache (the list's one is no room's)");
    TestShaderVariety(r);
    Check(r.Stats().programs == made, "prewarm: the same draws make no program");
    // another room: its list is empty, nothing ahead
    r.SetRoom(0x102);
    const unsigned ahead = r.Stats().programsAhead;
    while (r.Prewarm(1000.0)) {
    }
    Check(r.Stats().programsAhead == ahead, "prewarm: a room without a list takes no program ahead");
    r.Release();
  }
  {
    // at most RE4_GX_MAX_PROGRAMS programs kept: those that went longest unused are deleted, a draw
    // takes its program again from the cache and draws the same
    setenv("RE4_GX_MAX_PROGRAMS", "8", 1);
    re4gx::GxRenderer r;
    unsetenv("RE4_GX_MAX_PROGRAMS");
    Require(r.Init(1, &error), error.c_str());
    r.SetProgramCache(dir);
    r.SetRoom(0x103);
    TestShaderVariety(r);
    const re4gx::GxStats s = r.Stats();
    std::fprintf(stderr, "  limit 8: %u programs made, %u kept, %u deleted\n", s.programs, s.programsLive,
                 s.programsDeleted);
    Check(s.programsLive <= 8 && s.programsDeleted > 0 && s.programs == s.programsLoaded,
          "limit: at most 8 programs kept, the others deleted and taken from the cache again");
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
  Check(access((dir + "/room-101.keys").c_str(), F_OK) == 0 && access((dir + "/room-103.keys").c_str(), F_OK) == 0,
        "prewarm: the rooms' lists written");
  std::remove((dir + "/room-101.keys").c_str());
  std::remove((dir + "/room-103.keys").c_str());
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
  TestCanonicalKey();
  TestPrewarm();
  TestRescale();
  TestBlurCopy();
  {
    // bloom with GL at 2x (the loop below runs it at 1x): a bloom texel stays 4 x 4 game pixels
    re4gx::GxRenderer gr;
    std::string error;
    Require(gr.Init(2, &error), error.c_str());
    TestBloom(gr, "GL 2x");
    gr.Release();
  }
  TestAutoLevel();
  TestBenchSteps();
  TestHandOverCopies();
#ifdef RE4_GX_VULKAN
  if (VulkanStarts()) {
    TestVkRescale();
    TestVkHandOverCopies();
    TestVkOverlay();
    TestVkAhead();
    TestVkTevMath();
    {
      // bloom with Vulkan at 2x: the same as GL (TestBloom)
      re4gx::GxRenderer vr(re4gx::GxApi::kVulkan);
      std::string error;
      Require(vr.Init(2, &error), error.c_str());
      TestBloom(vr, "Vulkan 2x");
      vr.Release();
    }
  }
#endif
  TestMerge();
  TestVertexRing();
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
    TestPixelCentre(renderer);
    TestWiden(renderer);
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
    TestSharpen(renderer);
    TestCrtColour(renderer);
    TestBloom(renderer, poison ? "GL, forgotten contents magenta" : "GL");
    TestPictureCadence();
    TestPictureHold();
    if (!poison)
      TestPresentPacer();
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
  TestInterpolationForget();
  TestInterpolationOwners();
  TestInterpolationWrap();
  TestInterpolationRoomPart();
  TestInterpolationSparkle();
  TestInterpolationOneFrame();
  TestInterpolationTrail();
  TestInterpolationExtras();
  TestSmoothPacer();
  TestChunkRate();
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
