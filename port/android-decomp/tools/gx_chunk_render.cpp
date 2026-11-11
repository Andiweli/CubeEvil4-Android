// GX frames of the game core (RE4_GX_DUMP, re4_gx_frame_io.h) drawn with the
// app's GX renderer (gx_gl.cpp) on a headless GLES3 context: the local check
// with disc data (port/tools/whole_r100_check.sh) shows what the core mode
// of the app shows.
//
//   gx_chunk_render OUTPUT_DIR gx-*.bin
//
// Writes OUTPUT_DIR/<name>.ppm (the display copy, 640 wide) and one line per
// frame: size, draws, triangles, textures, copies, shaders, problems.
//
// A chunk that directly follows the one before (next sequence number and next
// frame, the app's R3 series gx-geraet-NN-KK.bin) runs on the same renderer as
// in the app: EFB, copies and textures stay, a held picture (RE4_GX_CHUNK_HOLD)
// leaves the picture before on screen. Any other chunk must start over
// (RE4_GX_CHUNK_RESET) and gets a fresh renderer.
//
// RE4_GX_SERIES=1: dumps of the core up to 4 frames apart (RE4_GX_DUMP_AT="7141,7142,..."), each starting over, run
// on the same renderer as one series (the lens flares, which fade in over pictures).
//
// RE4_GX_INVALIDATE_POISON=1: what the renderer lets the GPU forget (copy targets, the blur and
// the focus of the autofocus; GPU step b) is painted magenta instead: the pictures must not
// change (whole_r100_check.sh compares them).
//
// RE4_GX_SAMPLES=2 (or 4): with MSAA, on the tiles where the GPU can (GPU step d;
// RE4_GX_MSAA_TILES=0: in own buffers); a line on stderr says which.
//
// RE4_GX_INTERP=1 (60 FPS, experimental): before each chunk that follows the one before, the
// in-between picture of the two (GxRenderer::Interpolate, RE4_GX_INTERP_T, default 0.5) as
// OUTPUT_DIR/<name>-zw.ppm, with a line: shown or a cut, the 3D draws and how many were matched.
// The real pictures must stay the same as without it. A second line counts the draws without a
// counterpart by kind (particles, figures, room, 2D), those found by their object (owner tags,
// patch 0137) and the texture matrices / TEV colours blended (60fps-plan.md S0-S3).
// RE4_GX_NO_OWNERS=1: the owner tags left out (the counter-check: the in-between pictures as before).
//
// RE4_GX_SHARPEN=1 (light) or 2 (strong), RE4_GX_FXAA=1, RE4_GX_TV_FILTER=1, RE4_GX_FLARES=1, RE4_GX_DOF_STRONG=1:
// the app menu's switches (GxRenderer::SetSharpen, SetFxaa, SetTvFilter, SetFlares, SetDofStrong;
// RE4_GX_SHARPEN_AMOUNT=A,B the strengths of light and strong).
// A movie's pictures (RE4_GX_CHUNK_MOVIE) say "Film" in their line; RE4_GX_SHARPEN_MOVIES=1 drops that flag (the
// check whether sharpening would make the blocks of the movie's compression stand out).
//
// RE4_GX_SCREEN=WxH:FORMAT (the sharpening's comparisons, 08.11.2026): each picture also as the app's screen of
// W x H pixels shows it in the menu's picture format (0 "16:9", 1 "4:3 (GC)", 2 "Stretched", 3 "4:3", 5 "Native";
// the rows shown and the boxes of core_mode.cpp drawGx, minimap_view.h) as OUTPUT_DIR/<name>-screen.ppm. Native
// wider than 16:9 needs the core's wide pictures (RE4_VIEW_ASPECT=0.75 x W/H, e.g. 1.6667 for 20:9).
//
// RE4_GX_WORK=1: the GL work of each picture and the render thread's time by kind; with RE4_GX_MERGE=1
// draws in a row that differ only in their matrices are merged into one draw call (GxRenderer::SetMerge):
// the pictures must stay the same.
#include "gx_gl.h"
#include "minimap_view.h"
#include "re4_gx_frame_io.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

bool Context() {
  auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
  EGLDisplay display = getDisplay ? getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
                                  : eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major{}, minor{};
  if (!eglInitialize(display, &major, &minor))
    return false;
  const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
  EGLConfig config{};
  EGLint count{};
  eglChooseConfig(display, attributes, &config, 1, &count);
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
  EGLContext context = eglCreateContext(display, count ? config : nullptr, EGL_NO_CONTEXT, contextAttributes);
  return context != EGL_NO_CONTEXT && eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
}

std::string Stem(const std::string &path) {
  const auto slash = path.find_last_of('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const auto dot = name.rfind('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: gx_chunk_render OUTPUT_DIR gx-*.bin\n");
    return 2;
  }
  // RE4_GX_API=vulkan: the Vulkan renderer (port/docs/vulkan-bauplan.md), on the PC Mesa's lavapipe
  // (. ~/vk-tools/env.sh; RE4_VK_VALIDATION=1 with the Khronos validation layer, 2 with its synchronization checks)
  const char *apiText = std::getenv("RE4_GX_API");
  const bool vulkan = apiText && std::string(apiText) == "vulkan";
  const auto Finish = [vulkan]() {
    if (!vulkan)
      glFinish();
  };
  if (!vulkan && !Context()) {
    std::fprintf(stderr, "no EGL/GLES3 context (Mesa packages: port/tools/whole_setup_linux.sh)\n");
    return 1;
  }
  const std::string out = argv[1];
  int failures = 0;
  std::unique_ptr<re4gx::GxRenderer> renderer;
  uint32_t lastSequence = 0, lastFrame = 0;
  const char *interpText = std::getenv("RE4_GX_INTERP");
  const bool interp = interpText && *interpText == '1';
  const char *interpTText = std::getenv("RE4_GX_INTERP_T");
  const float interpT = interpTText ? static_cast<float>(std::atof(interpTText)) : 0.5f;
  // the chunk run last (its vertices for the in-between picture)
  std::unique_ptr<re4gxio::Loaded> previous;
  // RE4_GX_SCALE: the EFB resolution of the app (-Scale; 1 = the GameCube's);
  // the pictures then have that many times the pixels
  const char *scaleText = std::getenv("RE4_GX_SCALE");
  int scale = scaleText && std::atoi(scaleText) >= 1 && std::atoi(scaleText) <= 4 ? std::atoi(scaleText) : 1;
  // RE4_GX_HALVES=N: the app menu's in-between levels in halves (3: 1.5x, 5: 2.5x ...); wins over RE4_GX_SCALE
  const char *halvesText = std::getenv("RE4_GX_HALVES");
  const int halves = halvesText && std::atoi(halvesText) >= 2 && std::atoi(halvesText) <= 8 ? std::atoi(halvesText)
                                                                                           : 2 * scale;
  int shownHalves = halves;  // the running renderer's level (after RE4_GX_RESCALE)
  // RE4_GX_RESCALE=K:S: before the K-th file (from 0) the running renderer goes to scale S
  // (GxRenderer::Rescale, the app menu's Auto); the pictures from there have S times the pixels.
  // RE4_GX_RESCALE=K:hN: to N halves (GxRenderer::RescaleHalves, a step of "Dynamic": 3 is 1.5x)
  int rescaleAt = -1, rescaleTo = 0;
  if (const char *rescale = std::getenv("RE4_GX_RESCALE")) {
    int n = 0;
    if (std::sscanf(rescale, "%d:h%d", &rescaleAt, &n) == 2 && n >= 2 && n <= 8)
      rescaleTo = n;
    else if (std::sscanf(rescale, "%d:%d", &rescaleAt, &rescaleTo) == 2 && rescaleTo >= 1 && rescaleTo <= 4)
      rescaleTo *= 2;
    else
      rescaleAt = -1;
  }
  // RE4_GX_TIME=1: the wall time of each picture (Run with glFinish) and of a rescale, in ms (the cost of a step
  // of "Dynamic", 06.11.2026)
  const bool timed = std::getenv("RE4_GX_TIME") && *std::getenv("RE4_GX_TIME") == '1';
  int screenW = 0, screenH = 0, screenFormat = 0;
  if (const char *screen = std::getenv("RE4_GX_SCREEN"))
    if (std::sscanf(screen, "%dx%d:%d", &screenW, &screenH, &screenFormat) != 3 || screenW < 16 || screenH < 16)
      screenW = screenH = 0;
  for (int i = 2; i < argc; i++) {
    const std::string path = argv[i];
    auto loadedHere = std::make_unique<re4gxio::Loaded>();
    re4gxio::Loaded &loaded = *loadedHere;
    FILE *f = std::fopen(path.c_str(), "rb");
    const bool ok = f && re4gxio::Read(f, loaded);
    if (f)
      std::fclose(f);
    if (!ok) {
      std::printf("%s: nicht lesbar\n", Stem(path).c_str());
      failures++;
      continue;
    }
    static const bool noOwners = std::getenv("RE4_GX_NO_OWNERS") && *std::getenv("RE4_GX_NO_OWNERS") == '1';
    if (noOwners) {
      std::vector<uint32_t> kept;
      for (std::size_t at = 0; at < loaded.ops.size();) {
        const uint32_t n = 1 + (loaded.ops[at] >> 8);
        if ((loaded.ops[at] & 0xFF) != RE4_GX_OP_OWNER)
          kept.insert(kept.end(), loaded.ops.begin() + at, loaded.ops.begin() + std::min(loaded.ops.size(), at + n));
        at += n;
      }
      loaded.ops.swap(kept);
      loaded.chunk.ops = loaded.ops.data();
      loaded.chunk.opWords = static_cast<uint32_t>(loaded.ops.size());
    }
    Re4GxChunk chunk = loaded.chunk;
    // RE4_GX_SERIES=1 (09.11.2026): the core's dumps a few frames apart (RE4_GX_DUMP_AT with consecutive frame numbers:
    // each one starts over with RE4_GX_CHUNK_RESET) run on the same renderer, as the app's chunks do after a dump; what
    // follows from picture to picture (the lens flares fading in, their slots) is seen then
    static const bool series = std::getenv("RE4_GX_SERIES") && *std::getenv("RE4_GX_SERIES") == '1';
    const bool follows = renderer && ((chunk.sequence == lastSequence + 1 && loaded.frame == lastFrame + 1) ||
                                      (series && (chunk.flags & RE4_GX_CHUNK_RESET) && loaded.frame > lastFrame &&
                                       loaded.frame - lastFrame <= 4));
    if (!follows) {
      if (!(chunk.flags & RE4_GX_CHUNK_RESET)) {
        std::printf("%s: Folgebild ohne das Bild davor (Bildstück %u), übersprungen\n", Stem(path).c_str(),
                    chunk.sequence);
        failures++;
        continue;
      }
      if (renderer)
        renderer->Release();
      renderer = std::make_unique<re4gx::GxRenderer>(vulkan ? re4gx::GxApi::kVulkan : re4gx::GxApi::kGl);
      const char *poison = std::getenv("RE4_GX_INVALIDATE_POISON");
      re4gx::GxOptions options;
      options.poison = poison && *poison == '1';
      options.invalidate = options.poison;
      const char *samples = std::getenv("RE4_GX_SAMPLES");
      options.samples = samples ? std::max(1, std::min(4, std::atoi(samples))) : 1;
      // RE4_GX_LOSE_TILES=1: MSAA on the tiles as on a GPU that loses everything at a pass's end
      // (magenta, depth 0) before the EFB is drawn back (GPU step d2); 2: besides, the resolved
      // depth never stored (the self-test takes own buffers then)
      const char *lose = std::getenv("RE4_GX_LOSE_TILES");
      options.loseTiles = lose ? std::max(0, std::min(2, std::atoi(lose))) : 0;
      // RE4_GX_CENTROID=0: MSAA without centroid (before 20.10.2026); RE4_GX_FULL_RESOLVE=1: the whole
      // EFB resolved each time
      const char *centroid = std::getenv("RE4_GX_CENTROID");
      options.centroid = !(centroid && *centroid == '0');
      const char *full = std::getenv("RE4_GX_FULL_RESOLVE");
      options.fullResolve = full && *full == '1';
      // RE4_GX_DOF_FULL=1: the depth of field's blur at the full resolution (GxOptions::halfDof off)
      const char *dofFull = std::getenv("RE4_GX_DOF_FULL");
      options.halfDof = !(dofFull && *dofFull == '1');
      renderer->SetOptions(options);
      // RE4_GX_PSX=1: the PSone demake as the app's switch sets it (GxRenderer::SetPsx)
      const char *psx = std::getenv("RE4_GX_PSX");
      renderer->SetPsx(psx && *psx == '1');
      const char *sharpen = std::getenv("RE4_GX_SHARPEN");
      renderer->SetSharpen(sharpen ? std::atoi(sharpen) : 0);
      // RE4_GX_CRT=1|2, RE4_GX_COLOUR=MODE,LEVEL: the menu's CRT and colour (SetCrt, SetColour; 0.99.9)
      const char *crt = std::getenv("RE4_GX_CRT");
      renderer->SetCrt(crt ? std::atoi(crt) : 0);
      int colourMode = 0, colourLevel = 1;
      if (const char *colour = std::getenv("RE4_GX_COLOUR"))
        std::sscanf(colour, "%d,%d", &colourMode, &colourLevel);
      renderer->SetColour(colourMode, colourLevel);
      const char *fxaa = std::getenv("RE4_GX_FXAA");
      renderer->SetFxaa(fxaa && *fxaa == '1');
      const char *tv = std::getenv("RE4_GX_TV_FILTER");
      renderer->SetTvFilter(tv && *tv == '1');
      // RE4_GX_FLARES=1, RE4_GX_DOF_STRONG=1: the menu's lens flares and depth of field "Strong" (SetFlares,
      // SetDofStrong; the pictures of every switch for the comparisons of the Vulkan renderer, vulkan-bauplan.md)
      const char *flares = std::getenv("RE4_GX_FLARES");
      renderer->SetFlares(flares && *flares == '1');
      const char *dofStrong = std::getenv("RE4_GX_DOF_STRONG");
      renderer->SetDofStrong(dofStrong && *dofStrong == '1');
      // the rows the format shows and its widening, as drawGx sets them before each picture
      if (screenW > 0) {
        if (screenFormat == 5) {
          const re4mm::NativeView v = re4mm::Native(screenW, screenH, false);
          renderer->SetShownRows(v.top, v.top + v.rows);
        } else {
          const bool crop = screenFormat == 0 || screenFormat == 2;
          renderer->SetShownRows(crop ? 56 : 0, crop ? 392 : 1 << 16);
        }
        // "4:3" draws everything in the picture's shape since 0.99.10 (core_mode.cpp drawGx); RE4_GX_WIDEN_43=1
        // gives the flat draws the 15/14 of 0.95..0.99.9 again (the comparison)
        const char *oldWiden = std::getenv("RE4_GX_WIDEN_43");
        renderer->SetWiden(screenFormat == 3 && oldWiden && *oldWiden == '1' ? 15.0f / 14.0f : 1.0f);
      }
      renderer->SetRecording(interp);
      std::string error;
      if (!renderer->InitHalves(halves, &error)) {
        std::printf("%s: Renderer: %s\n", Stem(path).c_str(), error.c_str());
        renderer.reset();
        failures++;
        continue;
      }
      if (options.samples > 1)
        std::fprintf(stderr, "MSAA: %d samples %s\n", renderer->Samples(),
                     renderer->TiledMsaa() ? "on the tiles" : "in own buffers");
      // RE4_GX_PROGRAM_CACHE: the programs kept there (binary and key) as in the app; the keys of
      // device captures for the app's list (port/tools/gx_program_list.py)
      if (const char *cache = std::getenv("RE4_GX_PROGRAM_CACHE"))
        if (*cache && !renderer->SetProgramCache(cache))
          std::fprintf(stderr, "program cache: the driver keeps no binaries\n");
      // RE4_GX_ROOM=0xNNN: the room, as core_mode sets it (the rooms' program and pipeline lists, Prewarm)
      if (const char *room = std::getenv("RE4_GX_ROOM"))
        renderer->SetRoom(static_cast<unsigned>(std::strtoul(room, nullptr, 0)));
      // RE4_GX_PEEK_AT=x,y (GameCube pixels, as the game's GXPeekZ): the depth map the core would answer from
      // (GxRenderer::SetDepthSink), the 7 x 7 cells around (x, y) of each map the renderer hands over, 24-bit
      // depths in hex (the light through Leon's head, r102, 06.11.2026)
      // RE4_GX_PEEK_DUMP=DIR (with RE4_GX_PEEK_AT): each map also as DIR/depth-<picture>.bin, kDepthWidth x
      // kDepthHeight little-endian 32-bit cells (the game's 12-point glow test re-run on them, 06.11.2026)
      if (const char *at = std::getenv("RE4_GX_PEEK_AT")) {
        int px = 0, py = 0;
        const char *dumpDir = std::getenv("RE4_GX_PEEK_DUMP");
        const std::string peekDump = dumpDir ? dumpDir : "";
        if (std::sscanf(at, "%d,%d", &px, &py) == 2)
          renderer->SetDepthSink([px, py, peekDump](const std::uint32_t *cells, std::uint32_t serial) {
            if (!peekDump.empty()) {
              const std::string name = peekDump + "/depth-" + std::to_string(serial) + ".bin";
              if (FILE *f = std::fopen(name.c_str(), "wb")) {
                std::fwrite(cells, sizeof(std::uint32_t),
                            re4gx::GxRenderer::kDepthWidth * re4gx::GxRenderer::kDepthHeight, f);
                std::fclose(f);
              }
            }
            const int cx = px / 2, cy = py / 2;
            std::printf("depth map of picture %u around (%d, %d), cell (%d, %d) in the middle:\n", serial, px, py, cx, cy);
            for (int y = cy - 3; y <= cy + 3; y++) {
              std::printf("  ");
              for (int x = cx - 3; x <= cx + 3; x++) {
                const bool in = x >= 0 && y >= 0 && x < re4gx::GxRenderer::kDepthWidth &&
                                y < re4gx::GxRenderer::kDepthHeight;
                std::printf(x == cx && y == cy ? "[%06x]" : " %06x ",
                            in ? cells[y * re4gx::GxRenderer::kDepthWidth + x] & 0xFFFFFFu : 0u);
              }
              std::printf("\n");
            }
          });
      }
    }
    lastSequence = chunk.sequence;
    lastFrame = loaded.frame;
    // a held picture (RE4_GX_CHUNK_HOLD) was not on screen; a lone one is drawn
    // anyway, in a series the picture before stays (as in the app)
    const bool held = (chunk.flags & RE4_GX_CHUNK_HOLD) != 0;
    if (!follows)
      chunk.flags &= ~RE4_GX_CHUNK_HOLD;
    const bool movie = (chunk.flags & RE4_GX_CHUNK_MOVIE) != 0;
    if (const char *sharpenMovies = std::getenv("RE4_GX_SHARPEN_MOVIES"))
      if (*sharpenMovies == '1')
        chunk.flags &= ~RE4_GX_CHUNK_MOVIE;
    if (interp && follows && previous) {
      const re4gx::GxStats was = renderer->Stats();
      const bool shown = renderer->Interpolate(previous->chunk, chunk, interpT);
      const re4gx::GxStats &now = renderer->Stats();
      std::printf("%s: Zwischenbild %s, %u 3D-Zeichenaufträge, %u zugeordnet, %u mit gemischten Ecken\n",
                  Stem(path).c_str(), shown ? "gezeigt" : "nicht gezeigt (Schnitt)", now.interpDraws - was.interpDraws,
                  now.interpMatched - was.interpMatched, now.interpVertexBlends - was.interpVertexBlends);
      std::printf("%s: ohne Gegenstück Teilchen %u/%u, Figuren %u/%u, Raum %u/%u, 2D %u/%u (%u mit der Kamera "
                  "bewegt); über das Objekt %u "
                  "(%u neu belegt, %u gesprungen, %u Einbild-Effekte wiedererkannt); Texturmatrizen %u (%u über den "
                  "Rand), TEV-Farben %u, Lichtfarben %u\n",
                  Stem(path).c_str(), now.interpKindUnmatched[0] - was.interpKindUnmatched[0],
                  now.interpKind[0] - was.interpKind[0], now.interpKindUnmatched[1] - was.interpKindUnmatched[1],
                  now.interpKind[1] - was.interpKind[1], now.interpKindUnmatched[2] - was.interpKindUnmatched[2],
                  now.interpKind[2] - was.interpKind[2], now.interpKindUnmatched[3] - was.interpKindUnmatched[3],
                  now.interpKind[3] - was.interpKind[3], now.interpCameraMoved - was.interpCameraMoved,
                  now.interpOwned - was.interpOwned,
                  now.interpOwnedNew - was.interpOwnedNew, now.interpOwnedJumps - was.interpOwnedJumps,
                  now.interpOneFrame - was.interpOneFrame,
                  now.interpTexBlends - was.interpTexBlends,
                  now.interpTexWraps - was.interpTexWraps, now.interpTevBlends - was.interpTevBlends,
                  now.interpLightBlends - was.interpLightBlends);
      unsigned iw = 0, ih = 0;
      std::vector<std::uint8_t> rgba;
      if (shown && renderer->ReadXfb(&rgba, &iw, &ih) && iw && ih) {
        const std::string file = out + "/" + Stem(path) + "-zw.ppm";
        if (FILE *p = std::fopen(file.c_str(), "wb")) {
          std::fprintf(p, "P6\n%u %u\n255\n", iw, ih);
          for (unsigned y = 0; y < ih; y++)
            for (unsigned x = 0; x < iw; x++)
              std::fwrite(&rgba[(static_cast<std::size_t>(y) * iw + x) * 4], 1, 3, p);
          std::fclose(p);
        }
      }
    }
    if (i - 2 == rescaleAt && renderer) {
      Finish();
      const auto t0 = std::chrono::steady_clock::now();
      renderer->RescaleHalves(rescaleTo);
      if (timed)
        Finish();
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      scale = renderer->Scale();
      shownHalves = renderer->Halves();
      if (timed)
        std::printf("%s: EFB-Stufe %d/2 (Rescale), %.2f ms\n", Stem(path).c_str(), shownHalves, ms);
      else
        std::printf("%s: EFB-Stufe %d/2 (Rescale)\n", Stem(path).c_str(), shownHalves);
      // with RE4_GX_SCREEN: the screen right after the step, before the next picture's display copy, as the app's
      // refreshes in between show it (OUTPUT_DIR/<name>-rescaled-screen.ppm; 0.99.10: the Vulkan renderer showed the
      // plain XFB there, without the CRT's glass and its black border, the colour or the PSone filter)
      unsigned rxw = 0, rxh = 0;
      if (screenW > 0 && renderer->Xfb(&rxw, &rxh) && rxh) {
        re4mm::Rect rhud, rpic;
        re4mm::PictureRects(screenFormat, screenW, screenH, static_cast<int>(rxh), &rhud, &rpic, renderer->XfbWide());
        std::vector<std::uint8_t> shot;
        bool read = false;
        if (screenFormat == 5) {
          const re4mm::NativeView v = re4mm::Native(screenW, screenH, renderer->XfbWide());
          read = renderer->ReadPresented(screenW, screenH, rpic.x, rpic.y, rpic.w, rpic.h,
                                         rxh * static_cast<unsigned>(v.top) / 448, rxh * static_cast<unsigned>(v.rows) / 448,
                                         &shot);
        } else if (screenFormat == 0 || screenFormat == 2) {
          read = renderer->ReadPresented(screenW, screenH, rpic.x, rpic.y, rpic.w, rpic.h, rxh * 56 / 448, rxh * 336 / 448,
                                         &shot);
        } else {
          read = renderer->ReadPresented(screenW, screenH, rpic.x, rpic.y, rpic.w, rpic.h, 0, 0, &shot);
        }
        const std::string file = out + "/" + Stem(path) + "-rescaled-screen.ppm";
        if (read && shot.size() >= static_cast<std::size_t>(screenW) * screenH * 4) {
          if (FILE *p = std::fopen(file.c_str(), "wb")) {
            std::fprintf(p, "P6\n%d %d\n255\n", screenW, screenH);
            for (int y = 0; y < screenH; y++)
              for (int x = 0; x < screenW; x++)
                std::fwrite(&shot[(static_cast<std::size_t>(y) * screenW + x) * 4], 1, 3, p);
            std::fclose(p);
          }
        }
      }
    }
    const re4gx::GxStats before = renderer->Stats();
    if (timed)
      Finish();
    const auto runStart = std::chrono::steady_clock::now();
    renderer->Run(chunk);
    if (timed) {
      Finish();
      std::printf("%s: Run %.2f ms (mit glFinish)\n", Stem(path).c_str(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - runStart).count());
    }
    // the merged draw calls' programs this chunk wanted, made as the app makes them between two chunks;
    // RE4_GX_PREWARM=1: the room's programs and pipelines ahead as well (RE4_GX_ROOM, RE4_GX_PROGRAM_CACHE)
    static const bool prewarm = std::getenv("RE4_GX_PREWARM") && *std::getenv("RE4_GX_PREWARM") == '1';
    if (renderer->Merging() || prewarm)
      renderer->Prewarm(1e9);
    if (held)
      std::printf("%s: Bild war angehalten (nicht auf dem Bildschirm)%s\n", Stem(path).c_str(),
                  follows ? ", gezeigt bleibt das Bild davor" : "");
    unsigned w = 0, h = 0;
    const GLuint xfb = renderer->Xfb(&w, &h);
    const re4gx::GxStats &s = renderer->Stats();
    std::printf("%s (Bild %u%s%s): %ux%u, %u Zeichenaufträge, %u Dreiecke, %u Texturen, %u Kopien, %u Shader%s%s%s%s\n",
                Stem(path).c_str(), loaded.frame, follows ? ", Folgebild" : "", movie ? ", Film" : "", w, h, s.draws - before.draws,
                s.triangles - before.triangles, s.textures - before.textures, s.textureCopies - before.textureCopies,
                s.programs, s.autofocus > before.autofocus ? ", Autofokus" : "",
                s.efbRestores > before.efbRestores ? (", EFB " + std::to_string(s.efbRestores - before.efbRestores) +
                                                      "x zurückgemalt").c_str() : "",
                s.missingTextures > before.missingTextures ? ", fehlende Texturen" : "",
                s.shaderErrors > before.shaderErrors ? ", FEHLER: Shader" : "");
    if (std::getenv("RE4_GX_WORK"))
      std::printf("  GL-Arbeit: %u Uniform-Uploads (%u KB, %u gleiche ausgelassen), %u Programmwechsel, %u Textur-/Sampler-Bindungen, "
                  "%u Zustandswechsel, %u Attribut-Einrichtungen, %u Uniform-Ringwechsel (%u im Bild), "
                  "Eckdaten-Ring %s (%u gewartet); "
                  "zusammenfassbar: %u ohne jede Änderung, %u nur mit anderen Uniforms (%u davon nur Matrizen/Licht)\n",
                  s.uniformUploads - before.uniformUploads, (s.uniformBytes - before.uniformBytes) / 1024,
                  s.uniformSame - before.uniformSame,
                  s.programSwitches - before.programSwitches, s.textureBinds - before.textureBinds,
                  s.stateChanges - before.stateChanges, s.attributeSetups - before.attributeSetups,
                  s.streamSwitches - before.streamSwitches, s.streamSplits - before.streamSplits,
                  s.ringChunks > before.ringChunks ? "an" : "aus", s.ringWaits - before.ringWaits,
                  s.mergeSame - before.mergeSame, s.mergeUniform - before.mergeUniform,
                  s.mergeMatrixOnly - before.mergeMatrixOnly);
    // RE4_GX_MERGE=1: the merged draw calls; and the render thread's time by kind of work (GxStats::splitNs)
    if (std::getenv("RE4_GX_WORK")) {
      const unsigned draws = s.draws - before.draws, taken = s.mergeTaken - before.mergeTaken,
                     batches = s.mergeBatches - before.mergeBatches;
      static const char *kinds[re4gx::GxStats::kSplitKinds] = {"Zeichenaufrufe", "Uniforms", "Bindungen", "Zustand",
                                                               "Programme", "Texturen", "BP", "XF", "Ecken", "Rest"};
      std::string split;
      double all = 0.0;
      for (int k = 0; k < re4gx::GxStats::kSplitKinds; k++) {
        const double ms = (s.splitNs[k] - before.splitNs[k]) / 1e6;
        all += ms;
        char part[64];
        std::snprintf(part, sizeof(part), "%s%s %.2f", k ? ", " : "", kinds[k], ms);
        split += part;
      }
      std::printf("  GL-Zeichenaufrufe: %u (%u Zeichenaufträge in %u zusammengefasst); Render-Thread %.2f ms: %s\n",
                  draws - taken + batches, taken, batches, all, split.c_str());
    }
    // the EFB copies of this picture and why merged runs ended (measurements, 31.10.2026)
    if (std::getenv("RE4_GX_WORK")) {
      static const char *copyKinds[re4gx::GxStats::kCopyKinds] = {"ganz", "halb", "Tiefe", "klein", "Anzeige",
                                                                     "Autofokus"};
      std::string part;
      for (int k = 0; k < re4gx::GxStats::kCopyKinds; k++) {
        char one[96];
        std::snprintf(one, sizeof(one), "%s%s %u (%llu Texel, %.2f ms)", k ? ", " : "", copyKinds[k],
                      s.copyCount[k] - before.copyCount[k], s.copyTexels[k] - before.copyTexels[k],
                      (s.copyNs[k] - before.copyNs[k]) / 1e6);
        part += one;
      }
      std::printf("  EFB-Kopien: %s; gelesen %u, ungelesen überschrieben %u\n", part.c_str(),
                  s.copyRead - before.copyRead, s.copyOverwrittenUnread - before.copyOverwrittenUnread);
      static const char *planKinds[re4gx::GxStats::kPlanKinds] = {"Kopie/Marke", "Textur mit Daten", "Kopie-Textur", "andere Op", "Layout",
                                                                  "Schrittweite", "Primitive", "Ecken", "Indizes"};
      static const char *runKinds[re4gx::GxStats::kRunKinds] = {"Plätze voll", "Kettenende", "kein Programm",
                                                                "BP-Marke", "TEV-Farbe", "BP-Wert", "XF sonst", "XF Texturmatrix",
                                                                "XF Licht", "XF Register", "Textur",
                                                                "andere Op"};
      std::string plan, run;
      for (int k = 0; k < re4gx::GxStats::kPlanKinds; k++)
        plan += std::string(k ? ", " : "") + planKinds[k] + " " +
                std::to_string(s.mergePlanBreak[k] - before.mergePlanBreak[k]);
      for (int k = 0; k < re4gx::GxStats::kRunKinds; k++)
        run += std::string(k ? ", " : "") + runKinds[k] + " " +
               std::to_string(s.mergeRunBreak[k] - before.mergeRunBreak[k]);
      std::printf("  Ketten-Abbruch (Plan): %s; Lauf-Ende: %s\n", plan.c_str(), run.c_str());
    }
    if (s.shaderErrors > before.shaderErrors)
      failures++;
    std::vector<std::uint8_t> rgba;
    if (xfb && w && h && renderer->ReadXfb(&rgba, &w, &h)) {
      // (the picture's pixels: the scaled size)
      // XFB row 0 is the picture's top row
      const std::string file = out + "/" + Stem(path) + ".ppm";
      if (FILE *p = std::fopen(file.c_str(), "wb")) {
        std::fprintf(p, "P6\n%u %u\n255\n", w, h);
        for (unsigned y = 0; y < h; y++)
          for (unsigned x = 0; x < w; x++)
            std::fwrite(&rgba[(static_cast<std::size_t>(y) * w + x) * 4], 1, 3, p);
        std::fclose(p);
      }
    }
    // the screen as drawGx (core_mode.cpp) fills it: the box of the format, the rows it shows
    if (xfb && screenW > 0 && !held) {
      unsigned xw = 0, xh = 0;
      renderer->Xfb(&xw, &xh);
      re4mm::Rect hud, pic;
      re4mm::PictureRects(screenFormat, screenW, screenH, static_cast<int>(xh), &hud, &pic, renderer->XfbWide());
      auto present = [&]() {
        if (screenFormat == 5) {
          const re4mm::NativeView v = re4mm::Native(screenW, screenH, renderer->XfbWide());
          renderer->ReadPresented(screenW, screenH, pic.x, pic.y, pic.w, pic.h, xh * static_cast<unsigned>(v.top) / 448,
                                  xh * static_cast<unsigned>(v.rows) / 448, &rgba);
        } else if (screenFormat == 0 || screenFormat == 2) {
          renderer->ReadPresented(screenW, screenH, pic.x, pic.y, pic.w, pic.h, xh * 56 / 448, xh * 336 / 448, &rgba);
        } else {
          renderer->ReadPresented(screenW, screenH, pic.x, pic.y, pic.w, pic.h, 0, 0, &rgba);
        }
      };
      present();
      // RE4_GX_PRESENT_REPEAT=N (the CRT's cost, 0.99.9): the screen drawn and read back N times more, the mean time
      if (const char *repeat = std::getenv("RE4_GX_PRESENT_REPEAT")) {
        const int n = std::max(1, std::atoi(repeat));
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; i++)
          present();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
        std::printf("%s: screen %.2f ms (with the read-back, mean of %d)\n", Stem(path).c_str(), ms, n);
      }
      const std::string file = out + "/" + Stem(path) + "-screen.ppm";
      if (FILE *p = std::fopen(file.c_str(), "wb")) {
        std::fprintf(p, "P6\n%d %d\n255\n", screenW, screenH);
        for (int y = 0; y < screenH; y++)  // ReadPresented: row 0 at the top
          for (int x = 0; x < screenW; x++)
            std::fwrite(&rgba[(static_cast<std::size_t>(y) * screenW + x) * 4], 1, 3, p);
        std::fclose(p);
      }
    }
    previous = std::move(loadedHere);
  }
  if (renderer && std::getenv("RE4_GX_WORK")) {
    Finish();
    const re4gx::GxStats &s = renderer->Stats();
    if (s.gpuTimed)
      std::printf("GPU-Zeit (Timer-Abfrage): %.1f ms je Bildstück aus %u Messungen\n",
                  s.gpuMicros / 1000.0 / s.gpuTimed, s.gpuTimed);
  }
  // the Vulkan renderer's waits, shaders and pipelines (TimingLine; "" for GL)
  if (renderer) {
    const std::string timing = renderer->TimingLine();
    if (!timing.empty())
      std::fprintf(stderr, "%s\n", timing.c_str());
  }
  if (renderer)
    renderer->Release();
  return failures ? 1 : 0;
}
