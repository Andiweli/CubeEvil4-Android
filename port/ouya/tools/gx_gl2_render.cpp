// GX frames of the game core (RE4_GX_DUMP, re4_gx_frame_io.h) drawn by the OUYA's GLES 2 renderer
// (gx_gl2.cpp) alone, on any GLES 2 device: the pictures and how long the GPU takes. Built for
// the OUYA (port/scripts/ouya-gx-render.ps1, NDK r23c) and for the PC.
//
//   gx_gl2_render OUTPUT_DIR gx-*.bin
//
// Writes OUTPUT_DIR/<name>-<tag>.ppm (the display copy; tag: RE4_GX2_TAG, default "ouya") and one
// line per frame: draws, shaders, the first run (textures decoded and uploaded, shaders compiled)
// and, run again without new texture data as the game's next frames would be, the time to hand
// the frame to the driver and the time until the GPU is done (glFinish), the median of
// RE4_GX2_REPEAT runs (default 5). A frame that directly follows the one before runs on the same
// renderer, any other starts over, as in gx_chunk_render.
#include "gx_gl2.h"
#include "re4_gx_frame.h"
#include "re4_gx_frame_io.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

bool Context() {
  EGLDisplay display = EGL_NO_DISPLAY;
#ifndef __ANDROID__
  auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (getDisplay)
    display = getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#endif
  if (display == EGL_NO_DISPLAY)
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major = 0, minor = 0;
  if (!eglInitialize(display, &major, &minor))
    return false;
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint pbufferConfig[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                  EGL_RED_SIZE,        8,                  EGL_GREEN_SIZE,   8,
                                  EGL_BLUE_SIZE,       8,                  EGL_NONE};
  const EGLint anyConfig[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  EGLSurface surface = EGL_NO_SURFACE;
  if (eglChooseConfig(display, pbufferConfig, &config, 1, &count) && count > 0) {
    const EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    surface = eglCreatePbufferSurface(display, config, size);
  }
  if (surface == EGL_NO_SURFACE) {
    count = 0;
    eglChooseConfig(display, anyConfig, &config, 1, &count);
  }
  const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  EGLContext context = eglCreateContext(display, count ? config : nullptr, EGL_NO_CONTEXT, contextAttributes);
  return context != EGL_NO_CONTEXT && eglMakeCurrent(display, surface, surface, context);
}

std::string Stem(const std::string &path) {
  const auto slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const auto dot = name.rfind('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

double Ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// the process's processor time (all threads, the driver's too) in ms
double CpuMs() {
  timespec t{};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
  return static_cast<double>(t.tv_sec) * 1000.0 + static_cast<double>(t.tv_nsec) / 1e6;
}

double Median(std::vector<double> v) {
  if (v.empty())
    return 0.0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

// The chunk as the game's next frames send it: the same draws, textures by key only (the
// renderer has them), no start over.
void Steady(const Re4GxChunk &in, std::vector<std::uint32_t> &ops, Re4GxChunk &out) {
  ops.assign(in.ops, in.ops + in.opWords);
  for (std::uint32_t at = 0; at < in.opWords;) {
    const std::uint32_t op = ops[at] & 0xFF, n = ops[at] >> 8;
    if (op == RE4_GX_OP_TEXTURE && n * 4 >= sizeof(Re4GxTexture)) {
      Re4GxTexture t;
      std::memcpy(&t, &ops[at + 1], sizeof(t));
      if (!(t.flags & RE4_GX_TEX_COPY))
        t.dataBytes = 0;
      std::memcpy(&ops[at + 1], &t, sizeof(t));
    }
    at += 1 + n;
  }
  out = in;
  out.ops = ops.data();
  out.flags &= ~static_cast<std::uint32_t>(RE4_GX_CHUNK_RESET);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: gx_gl2_render OUTPUT_DIR gx-*.bin\n");
    return 2;
  }
  if (!Context()) {
    std::fprintf(stderr, "no EGL/GLES 2 context\n");
    return 1;
  }
  // the renderer's log lines (a shader that failed) with the rest of the output, not only in logcat
  re4gx2::SetLogCopy([](const char *line) {
    std::printf("%s\n", line);
    std::fflush(stdout);
  });
  if (const char *probe = std::getenv("RE4_GX2_PROBE"))
    std::printf("measurement: RE4_GX2_PROBE=%s (the pictures are wrong)\n", probe);
  const std::string out = argv[1];
  const char *tagText = std::getenv("RE4_GX2_TAG");
  const std::string tag = tagText && *tagText ? tagText : "ouya";
  const char *repeatText = std::getenv("RE4_GX2_REPEAT");
  const int repeat = repeatText ? std::max(0, std::atoi(repeatText)) : 5;
  std::printf("GPU: %s, %s\n", reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
              reinterpret_cast<const char *>(glGetString(GL_VERSION)));
  std::unique_ptr<re4gx2::GxRenderer> renderer;
  std::uint32_t lastSequence = 0, lastFrame = 0;
  int failures = 0;
  std::vector<double> allSteady;
  GLuint fbo = 0;
  for (int i = 2; i < argc; i++) {
    const std::string path = argv[i];
    re4gxio::Loaded loaded;
    FILE *f = std::fopen(path.c_str(), "rb");
    const bool ok = f && re4gxio::Read(f, loaded);
    if (f)
      std::fclose(f);
    if (!ok) {
      std::printf("%s: not readable\n", Stem(path).c_str());
      failures++;
      continue;
    }
    Re4GxChunk chunk = loaded.chunk;
    const bool follows = renderer && chunk.sequence == lastSequence + 1 && loaded.frame == lastFrame + 1;
    if (!follows) {
      if (!(chunk.flags & RE4_GX_CHUNK_RESET)) {
        std::printf("%s: follows a frame that is not there, left out\n", Stem(path).c_str());
        continue;
      }
      if (renderer)
        renderer->Release();
      renderer = std::make_unique<re4gx2::GxRenderer>();
      std::string error;
      if (!renderer->Init(1, &error)) {
        std::printf("%s: renderer: %s\n", Stem(path).c_str(), error.c_str());
        return 1;
      }
      if (!fbo)
        glGenFramebuffers(1, &fbo);
      chunk.flags &= ~static_cast<std::uint32_t>(RE4_GX_CHUNK_HOLD);
    }
    lastSequence = chunk.sequence;
    lastFrame = loaded.frame;
    const re4gx2::GxStats before = renderer->Stats();
    auto t0 = std::chrono::steady_clock::now();
    renderer->Run(chunk);
    auto t1 = std::chrono::steady_clock::now();
    glFinish();
    auto t2 = std::chrono::steady_clock::now();
    const re4gx2::GxStats first = renderer->Stats();
    // the picture of the first run
    unsigned w = 0, h = 0;
    const GLuint xfb = renderer->Xfb(&w, &h);
    if (xfb && w && h) {
      std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * h * 4);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xfb, 0);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      const std::string file = out + "/" + Stem(path) + "-" + tag + ".ppm";
      if (FILE *p = std::fopen(file.c_str(), "wb")) {
        std::fprintf(p, "P6\n%u %u\n255\n", w, h);
        for (unsigned y = 0; y < h; y++)
          for (unsigned x = 0; x < w; x++)
            std::fwrite(&rgba[(static_cast<std::size_t>(y) * w + x) * 4], 1, 3, p);
        std::fclose(p);
      }
      // RE4_GX2_PRESENT=WxH: the picture as the screen shows it (Present: scaled up linearly into
      // W x H, e.g. 1440x1080, the 4:3 of a 1080p television), to judge a lower resolution
      unsigned pw = 0, ph = 0;
      if (const char *present = std::getenv("RE4_GX2_PRESENT"))
        std::sscanf(present, "%ux%u", &pw, &ph);
      if (pw && ph) {
        GLuint tv = 0, tvFbo = 0;
        glGenTextures(1, &tv);
        glBindTexture(GL_TEXTURE_2D, tv);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(pw), static_cast<GLsizei>(ph), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &tvFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, tvFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tv, 0);
        renderer->Present(0, 0, static_cast<int>(pw), static_cast<int>(ph));
        std::vector<unsigned char> big(static_cast<std::size_t>(pw) * ph * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, static_cast<GLsizei>(pw), static_cast<GLsizei>(ph), GL_RGBA, GL_UNSIGNED_BYTE, big.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &tvFbo);
        glDeleteTextures(1, &tv);
        const std::string file = out + "/" + Stem(path) + "-" + tag + "-tv.ppm";
        if (FILE *p = std::fopen(file.c_str(), "wb")) {
          // Present draws upright (GL row 0 at the bottom): rows from the top
          std::fprintf(p, "P6\n%u %u\n255\n", pw, ph);
          for (unsigned y = ph; y-- > 0;)
            for (unsigned x = 0; x < pw; x++)
              std::fwrite(&big[(static_cast<std::size_t>(y) * pw + x) * 4], 1, 3, p);
          std::fclose(p);
        }
      }
    }
    // again, as the next frames of the game would come
    std::vector<std::uint32_t> ops;
    Re4GxChunk steady;
    Steady(loaded.chunk, ops, steady);
    std::vector<double> submit, total, cpu;
    for (int r = 0; r < repeat; r++) {
      const double c0 = CpuMs();
      auto s0 = std::chrono::steady_clock::now();
      renderer->Run(steady);
      auto s1 = std::chrono::steady_clock::now();
      glFinish();
      auto s2 = std::chrono::steady_clock::now();
      submit.push_back(Ms(s0, s1));
      total.push_back(Ms(s0, s2));
      cpu.push_back(CpuMs() - c0);
    }
    const re4gx2::GxStats &s = renderer->Stats();
    if (repeat)
      allSteady.push_back(Median(total));
    const unsigned runs = static_cast<unsigned>(repeat) + 1;
    std::printf("%s (frame %u%s): %u draws (%u compact), %u triangles, %u textures (%u levels made), %u copies, %u "
                "shaders (%u new)%s%s%s; first run %.1f ms (%.1f to hand over); again %.1f ms to hand over, %.1f ms "
                "with the GPU, %.1f ms processor; per run %u uniform uploads, %u program switches, %u texture binds\n",
                Stem(path).c_str(), loaded.frame, follows ? ", follows" : "", first.draws - before.draws,
                first.compactDraws - before.compactDraws, first.triangles - before.triangles,
                first.textures - before.textures, first.mipmapsMade - before.mipmapsMade,
                first.textureCopies - before.textureCopies, s.programs, first.programs - before.programs,
                first.shaderErrors > before.shaderErrors ? ", SHADER ERRORS" : "",
                first.leanShaders > before.leanShaders ? ", lean vertex shaders" : "",
                first.missingTextures > before.missingTextures ? ", missing textures" : "", Ms(t0, t2), Ms(t0, t1),
                Median(submit), Median(total), Median(cpu), (s.uniformUploads - before.uniformUploads) / runs,
                (s.programSwitches - before.programSwitches) / runs, (s.textureBinds - before.textureBinds) / runs);
    std::fflush(stdout);
    if (first.shaderErrors > before.shaderErrors)
      failures++;
  }
  if (!allSteady.empty())
    std::printf("in all: %zu frames, GPU time per frame (median of the frames) %.1f ms, the slowest %.1f ms\n",
                allSteady.size(), Median(allSteady), *std::max_element(allSteady.begin(), allSteady.end()));
  if (renderer) {
    const re4gx2::GxCaps &c = renderer->Caps();
    const int e = renderer->ResolutionEighths();
    std::printf("device: %s; EFB depth %d bits%s; depth copies %s; EFB %dx%d (%.3gx)\n", c.renderer.c_str(),
                c.depthBits, c.depthNonlinear ? " (nonlinear)" : "",
                c.depthTexture ? "on the GPU" : c.readDepth ? "read back" : "lost", (640 * e + 4) / 8,
                (528 * e + 4) / 8, e / 8.0);
    renderer->Release();
  }
  std::printf(failures ? "gx_gl2_render: %d FAILED\n" : "gx_gl2_render: done\n", failures);
  return failures ? 1 : 0;
}
