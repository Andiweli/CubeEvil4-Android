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
// RE4_GX_WORK=1: the GL work of each picture and the render thread's time by kind; with RE4_GX_MERGE=1
// draws in a row that differ only in their matrices are merged into one draw call (GxRenderer::SetMerge):
// the pictures must stay the same.
#include "gx_gl.h"
#include "re4_gx_frame_io.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <algorithm>
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
  if (!Context()) {
    std::fprintf(stderr, "no EGL/GLES3 context (Mesa packages: port/tools/whole_setup_linux.sh)\n");
    return 1;
  }
  const std::string out = argv[1];
  int failures = 0;
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
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
  const auto scaled = [&](unsigned v) { return (v * static_cast<unsigned>(halves) + 1) / 2; };
  // RE4_GX_RESCALE=K:S: before the K-th file (from 0) the running renderer goes to scale S
  // (GxRenderer::Rescale, the app menu's Auto); the pictures from there have S times the pixels
  int rescaleAt = -1, rescaleTo = 0;
  if (const char *rescale = std::getenv("RE4_GX_RESCALE"))
    if (std::sscanf(rescale, "%d:%d", &rescaleAt, &rescaleTo) != 2 || rescaleTo < 1 || rescaleTo > 4)
      rescaleAt = -1;
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
    const bool follows = renderer && chunk.sequence == lastSequence + 1 && loaded.frame == lastFrame + 1;
    if (!follows) {
      if (!(chunk.flags & RE4_GX_CHUNK_RESET)) {
        std::printf("%s: Folgebild ohne das Bild davor (Bildstück %u), übersprungen\n", Stem(path).c_str(),
                    chunk.sequence);
        failures++;
        continue;
      }
      if (renderer)
        renderer->Release();
      renderer = std::make_unique<re4gx::GxRenderer>();
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
      renderer->SetOptions(options);
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
    }
    lastSequence = chunk.sequence;
    lastFrame = loaded.frame;
    // a held picture (RE4_GX_CHUNK_HOLD) was not on screen; a lone one is drawn
    // anyway, in a series the picture before stays (as in the app)
    const bool held = (chunk.flags & RE4_GX_CHUNK_HOLD) != 0;
    if (!follows)
      chunk.flags &= ~RE4_GX_CHUNK_HOLD;
    if (interp && follows && previous) {
      const re4gx::GxStats was = renderer->Stats();
      const bool shown = renderer->Interpolate(previous->chunk, chunk, interpT);
      const re4gx::GxStats &now = renderer->Stats();
      std::printf("%s: Zwischenbild %s, %u 3D-Zeichenaufträge, %u zugeordnet, %u mit gemischten Ecken\n",
                  Stem(path).c_str(), shown ? "gezeigt" : "nicht gezeigt (Schnitt)", now.interpDraws - was.interpDraws,
                  now.interpMatched - was.interpMatched, now.interpVertexBlends - was.interpVertexBlends);
      std::printf("%s: ohne Gegenstück Teilchen %u/%u, Figuren %u/%u, Raum %u/%u, 2D %u/%u; über das Objekt %u "
                  "(%u neu belegt); Texturmatrizen %u (%u über den Rand), TEV-Farben %u, Lichtfarben %u\n",
                  Stem(path).c_str(), now.interpKindUnmatched[0] - was.interpKindUnmatched[0],
                  now.interpKind[0] - was.interpKind[0], now.interpKindUnmatched[1] - was.interpKindUnmatched[1],
                  now.interpKind[1] - was.interpKind[1], now.interpKindUnmatched[2] - was.interpKindUnmatched[2],
                  now.interpKind[2] - was.interpKind[2], now.interpKindUnmatched[3] - was.interpKindUnmatched[3],
                  now.interpKind[3] - was.interpKind[3], now.interpOwned - was.interpOwned,
                  now.interpOwnedNew - was.interpOwnedNew, now.interpTexBlends - was.interpTexBlends,
                  now.interpTexWraps - was.interpTexWraps, now.interpTevBlends - was.interpTevBlends,
                  now.interpLightBlends - was.interpLightBlends);
      unsigned iw = 0, ih = 0;
      const GLuint between = renderer->Xfb(&iw, &ih);
      if (shown && between && iw && ih) {
        iw = scaled(iw);
        ih = scaled(ih);
        std::vector<unsigned char> rgba(static_cast<std::size_t>(iw) * ih * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, between, 0);
        glReadPixels(0, 0, static_cast<GLsizei>(iw), static_cast<GLsizei>(ih), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
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
      renderer->Rescale(rescaleTo);
      scale = renderer->Scale();
      std::printf("%s: EFB-Stufe %d (Rescale)\n", Stem(path).c_str(), scale);
    }
    const re4gx::GxStats before = renderer->Stats();
    renderer->Run(chunk);
    // the merged draw calls' programs this chunk wanted, made as the app makes them between two chunks
    if (renderer->Merging())
      renderer->Prewarm(1e9);
    if (held)
      std::printf("%s: Bild war angehalten (nicht auf dem Bildschirm)%s\n", Stem(path).c_str(),
                  follows ? ", gezeigt bleibt das Bild davor" : "");
    unsigned w = 0, h = 0;
    const GLuint xfb = renderer->Xfb(&w, &h);
    const re4gx::GxStats &s = renderer->Stats();
    std::printf("%s (Bild %u%s): %ux%u, %u Zeichenaufträge, %u Dreiecke, %u Texturen, %u Kopien, %u Shader%s%s%s%s\n",
                Stem(path).c_str(), loaded.frame, follows ? ", Folgebild" : "", w, h, s.draws - before.draws,
                s.triangles - before.triangles, s.textures - before.textures, s.textureCopies - before.textureCopies,
                s.programs, s.autofocus > before.autofocus ? ", Autofokus" : "",
                s.efbRestores > before.efbRestores ? (", EFB " + std::to_string(s.efbRestores - before.efbRestores) +
                                                      "x zurückgemalt").c_str() : "",
                s.missingTextures > before.missingTextures ? ", fehlende Texturen" : "",
                s.shaderErrors > before.shaderErrors ? ", FEHLER: Shader" : "");
    if (std::getenv("RE4_GX_WORK"))
      std::printf("  GL-Arbeit: %u Uniform-Uploads (%u KB, %u gleiche ausgelassen), %u Programmwechsel, %u Textur-/Sampler-Bindungen, "
                  "%u Zustandswechsel, %u Attribut-Einrichtungen, %u Uniform-Ringwechsel (%u im Bild); "
                  "zusammenfassbar: %u ohne jede Änderung, %u nur mit anderen Uniforms (%u davon nur Matrizen/Licht)\n",
                  s.uniformUploads - before.uniformUploads, (s.uniformBytes - before.uniformBytes) / 1024,
                  s.uniformSame - before.uniformSame,
                  s.programSwitches - before.programSwitches, s.textureBinds - before.textureBinds,
                  s.stateChanges - before.stateChanges, s.attributeSetups - before.attributeSetups,
                  s.streamSwitches - before.streamSwitches, s.streamSplits - before.streamSplits,
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
    if (xfb && w && h) {
      w = scaled(w);  // the texture holds the scaled picture
      h = scaled(h);
      std::vector<unsigned char> rgba(static_cast<std::size_t>(w) * h * 4);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xfb, 0);
      glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
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
    previous = std::move(loadedHere);
  }
  if (renderer && std::getenv("RE4_GX_WORK")) {
    glFinish();
    const re4gx::GxStats &s = renderer->Stats();
    if (s.gpuTimed)
      std::printf("GPU-Zeit (Timer-Abfrage): %.1f ms je Bildstück aus %u Messungen\n",
                  s.gpuMicros / 1000.0 / s.gpuTimed, s.gpuTimed);
  }
  if (renderer)
    renderer->Release();
  return failures ? 1 : 0;
}
