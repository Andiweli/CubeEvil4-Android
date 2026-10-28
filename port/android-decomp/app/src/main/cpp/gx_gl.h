// The GX renderer of the core mode: executes the chunks the game core's
// emulated graphics processor decodes (port/whole/runtime/re4_gx_frame.h)
// with GLES 3.0. Plan: port/docs/gx-plan.md, step G3.
//
// It keeps the register state the chunks change (BP, XF, the palette part of
// TMEM), draws into its own embedded frame buffer (EFB, 640x528 like the
// GameCube's, colour and depth), turns EFB copies into textures (GXCopyTex)
// and display copies into the picture to show (GXCopyDisp, the XFB). Shaders
// are generated from the state that shapes them (vertex: XF transform,
// lighting channels, texture coordinate generation; fragment: TEV stages,
// alpha test, fog) and cached.
//
// Orientation: the EFB keeps GameCube rows (row 0 = top) at GL row 0, so
// copies, scissor and viewport need no flips; a display shows the XFB upside
// down in GL terms (Present flips it).
#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

struct Re4GxChunk;

namespace re4gx {

struct GxStats {
  unsigned chunks = 0, draws = 0, triangles = 0;
  unsigned programs = 0;         // shaders built
  unsigned programsLoaded = 0;   // of them taken from the program cache
  unsigned programsAhead = 0;    // of them made before their first draw (Prewarm)
  unsigned textures = 0;         // textures decoded
  unsigned textureCopies = 0;    // EFB copies to a texture
  unsigned displayCopies = 0;    // EFB copies to the XFB
  unsigned heldCopies = 0;       // of them not shown: the VI kept the picture before (RE4_GX_CHUNK_HOLD)
  unsigned skippedChunks = 0;    // chunks run without drawing (Frame skip): state and textures only
  unsigned fxaaPasses = 0;       // display copies smoothed by FXAA
  unsigned depthSnapshots = 0;   // depth maps taken for the game's GXPeekZ (BP 0x5B, the core's marker)
  unsigned depthDelivered = 0;   // of them handed to the core
  unsigned handedCopies = 0;     // EFB copies the renderer before handed over (HandOverCopies)
  unsigned limitedCopies = 0;    // texture copies that repeat the last drawn row below the picture (letterbox)
  unsigned resets = 0;
  unsigned rescales = 0;          // EFB scale changed while running (Rescale: Auto in the app menu)
  unsigned missingTextures = 0;  // bound keys the renderer did not have
  unsigned shaderErrors = 0;
  unsigned dstAlphaApprox = 0;   // draws whose constant destination alpha is not 0 and not exact
  unsigned autofocus = 0;        // blur passes of the gameplay autofocus (BP 0x5A, the core's marker)
  unsigned efbRestores = 0;      // MSAA on the tiles: the EFB drawn back after a pass ended (GPU step d2)
  // GL work per draw (what costs driver time): uniform block uploads and their
  // bytes, program switches, texture and sampler binds, render state changes,
  // vertex attribute setups
  unsigned uniformUploads = 0, uniformBytes = 0, programSwitches = 0, textureBinds = 0, stateChanges = 0,
           attributeSetups = 0;
  unsigned streamWaits = 0;  // the uniform stream waited for the GPU before reusing a quarter
  // the uniform stream moved on to its next quarter: in all, and inside a chunk (a chunk larger
  // than the room left; between chunks otherwise, gx_gl.cpp)
  unsigned streamSwitches = 0, streamSplits = 0;
  // GPU time of the chunks (EXT_disjoint_timer_query; 0 when the driver has none)
  unsigned long long gpuMicros = 0;
  unsigned gpuTimed = 0;
  // the pictures put on the screen (Present, once per display refresh) and their GPU time
  unsigned presents = 0;
  unsigned long long presentMicros = 0;
  unsigned presentTimed = 0;
  // how far the GPU is behind, without its timer: a fence after every chunk; after the next
  // chunk the fences it has not reached are chunks it still works on. Summed over the checks,
  // and how many checks found 0, 1, ... 8 behind (8: kFences of gx_gl.cpp, or more)
  unsigned long long behindSum = 0;
  unsigned behindChecks = 0;
  unsigned behindCount[9] = {};
  // in-between pictures (60 FPS, Interpolate): shown, refused as a cut; their perspective draws,
  // of them matched to a draw of the picture before, and draws with blended vertices
  unsigned interpPictures = 0, interpCuts = 0, interpDraws = 0, interpMatched = 0, interpVertexBlends = 0;
  // (60fps-plan.md S0-S3) draws of the in-between pass by kind (0 particles, 1 figures, 2 room and
  // other 3D, 3 2D) and of them without a counterpart; draws found by their object (owner tags), of
  // them a slot the game used anew; texture matrices blended, left out as a jump over the edge; TEV
  // colours and light colours blended
  unsigned interpKind[4] = {}, interpKindUnmatched[4] = {};
  unsigned interpOwned = 0, interpOwnedNew = 0, interpTexBlends = 0, interpTexWraps = 0, interpTevBlends = 0,
           interpLightBlends = 0;
};

// Picture options of the app's menu (M11), applied by Init: multisampling of
// the EFB (resolved before every copy out of it) and anisotropic filtering of
// the game's mipmapped, linearly filtered textures. Init keeps what the device
// can do (Samples(), Anisotropy()).
struct GxOptions {
  int samples = 1;          // 1 (off), 2, 4
  float anisotropy = 1.0f;  // 1 (off) .. 16
  // the GPU may forget what a copy's target, the blur and the focus of the autofocus held before
  // they are drawn over completely (glInvalidateFramebuffer, GPU step b, 19.10.2026); poison
  // (tests, RE4_GX_INVALIDATE_POISON=1 in gx_chunk_render): they are painted magenta instead, so
  // a picture that still needed the old content shows it
  bool invalidate = false;  // on the Retroid it saved nothing, at scale 3 alone it cost about 7 %
  bool poison = false;
  // MSAA on the tiles (GPU step d, 19.10.2026; d2 since 19.10.2026): the EFB's own textures take
  // the samples (EXT_multisampled_render_to_texture2); off, or without the extension: separate
  // multisampled buffers, resolved with a blit before every copy. The extension leaves the
  // samples undefined after a pass ends (the Retroid's Adreno 650 kept the colour, lost the depth:
  // Leon's hair, fog, whole objects missing), so d2 draws the EFB back itself at the start of
  // every new pass, from a second set of textures. Off: with d2 too the Retroid lost Leon's hair
  // and parts of textures although its self-test passed (decision of the project owner,
  // 19.10.2026: MSAA in own buffers)
  bool tiledMsaa = false;
  // tests (RE4_GX_LOSE_TILES in gx_chunk_render): 1, every new pass on the tiles starts with
  // magenta and depth 0 before the EFB is drawn back, as a GPU that loses everything would; 2,
  // besides, the resolved depth never reaches the texture (0 there), as the Adreno 650 seems to do:
  // the self-test at Init finds it and takes own buffers
  int loseTiles = 0;
  // the depth of field's blur at half the resolution, mixed into the picture at the full one
  // (GPU step e, 19.10.2026); off: the blur and the copy back at the full resolution
  bool halfDof = true;
  // MSAA: the shaders' texture coordinates and colours at a sample the triangle covers
  // (centroid), not at the pixel's centre. At the edge of a thin cut-out (branches: an alpha
  // test) the centre can lie outside the triangle; its coordinate then points past the cut-out
  // and the alpha test drops or keeps the edge from picture to picture (the Retroid, 20.10.2026:
  // thin branches flickered with 2x and 4x MSAA only). Without MSAA it changes nothing
  bool centroid = true;
  // MSAA in own buffers: every resolve of the whole EFB, not only of the region a copy reads
  // (a test of the device replay)
  bool fullResolve = false;
};

class GxRenderer {
public:
  GxRenderer();
  ~GxRenderer();
  GxRenderer(const GxRenderer &) = delete;
  GxRenderer &operator=(const GxRenderer &) = delete;

  // Creates the GL objects (a GLES 3.0 context is current). `scale`: EFB
  // resolution multiple (1 = the GameCube's 640x528).
  bool Init(int scale, std::string *error);
  // The same in halves of the GameCube's resolution (2: 1x, 3: 1.5x ... 8: 4x).
  bool InitHalves(int halves, std::string *error);
  // Between two Runs: the EFB at another scale (the app menu's Auto follows the GPU). The EFB,
  // its MSAA buffers, the autofocus's buffers, the display copy and every EFB copy anew at the new
  // size (the copies and the display copy drawn over, scaled); programs, textures and the GX state
  // stay. The EFB starts cleared, as after a display copy.
  void Rescale(int scale);
  void RescaleHalves(int halves);
  // Another renderer replaces this one (new picture options, the same GL context): this one's EFB
  // copies go over to `to` (after its Init), at `to`'s scale. The game copies some pictures once (a
  // room's start) and reads them on: a new renderer without them drew those textures white until
  // the next room (Retroid 26.10.2026: 306 missing after three changes of the resolution in r223).
  void HandOverCopies(GxRenderer &to);
  // Before Init: the picture options (default: none).
  void SetOptions(const GxOptions &options);
  // After Init: the multisampling and the filtering in use.
  int Samples() const;
  float Anisotropy() const;
  // the copy targets are forgotten before they are drawn over (GxOptions::invalidate)
  bool Invalidates() const;
  // the samples live in the GPU's tile memory (GxOptions::tiledMsaa and the device can)
  bool TiledMsaa() const;
  // the depth of field's blur at half the resolution (GxOptions::halfDof)
  bool HalfDof() const;
  // What the device can do (a GLES 3.0 context is current): the most EFB
  // samples and the strongest anisotropic filtering (1: none).
  static void Limits(int *maxSamples, float *maxAnisotropy);
  // Runs one chunk (in sequence order).
  void Run(const Re4GxChunk &chunk);
  // The depth of the picture for the game's GXPeekZ (the glow of a torch or the merchant's fire hides
  // itself behind a wall by reading the depth at points around it; Dolphin, a GameCube: hidden; before
  // 26.10.2026 the port answered "far" and the glow shone through walls). The core marks the chunk
  // that peeks (BP 0x5B); at that chunk's display copy the depth of the picture goes, at half the
  // resolution (kDepthWidth x kDepthHeight cells, row 0 = the EFB's row 0, GX's 24-bit values), to the
  // sink: a frame later, once the GPU is through (a fence, no stall), with the marker's value (the
  // core's number of the picture, by which it judges the map's age). The sink runs on the thread that
  // calls Run. None: no snapshots.
  static constexpr int kDepthWidth = 320, kDepthHeight = 264;
  using DepthSink = std::function<void(const std::uint32_t *cells, std::uint32_t serial)>;
  void SetDepthSink(DepthSink sink);
  // FXAA (the app menu, 26.10.2026): while on, every display copy goes through one more pass, an edge
  // smoothing of the finished picture (FXAA 3.x, luma edges, a few taps) into a texture of its own, and
  // Xfb() and Present show that. Works with or without MSAA and at any resolution; takes effect with
  // the next display copy. Costs one pass over the XFB per picture (the in-between pictures too).
  void SetFxaa(bool on);
  // The EFB rows the app shows (0..448 of the game's picture: all, or 56..392 in the 16:9 formats,
  // whose crop leaves the letterbox bars out). Full-width texture copies repeat the edge rows of the
  // shown part instead of reading the rows outside it (a distortion near the edge read the black bars:
  // black waves at the bottom of the intro and under water). Default: all rows. Any thread.
  void SetShownRows(int top, int bottom);
  // Frame skip (the app menu, 26.10.2026). The renderer is behind when the core has finished two
  // chunks: `a` need not be drawn when `b` (the next one) is drawn at once. Skip runs `a` without its
  // draws and its display copy: the register and texture state the game builds on (BP, XF, palettes,
  // texture uploads and bindings) is applied, vertices are not uploaded, nothing is drawn, the EFB is
  // cleared as the display copy would. The game itself is not slowed down: the chunk is taken out of
  // the queue and the next one drawn in the same refresh.
  // CanSkip: false when `a` starts over, is held or black, marked, ends without a display copy, or
  // makes an EFB copy for a texture that `b` could still read (unless `b` copies to the same place
  // before its first draw: then `a`'s copy is never seen). With the in-between pictures on
  // (SetRecording) a skipped chunk gets none: Interpolate refuses until the next real Run.
  bool CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const;
  void Skip(const Re4GxChunk &a);
  // 60 FPS (experimental, decision of the project owner 22.10.2026): on, every Run notes how each
  // of its draws was drawn (the position and normal matrices, the projection) for Interpolate.
  void SetRecording(bool on);
  // The picture between `prev`, the chunk run last (the caller still holds it: its vertices), and
  // `next` at t (0 prev .. 1 next), into the XFB: `next` once more, every draw that matches one
  // of `prev` with its matrices (and changed vertices, the figures the game skins on the CPU)
  // blended. The renderer's state, EFB copies and the autofocus stay as after `prev`: Run(next)
  // follows as without it. False when it is no picture to show (a cut of the camera, a chunk that
  // starts over, held or black, nothing noted): run `next` at once.
  bool Interpolate(const Re4GxChunk &prev, const Re4GxChunk &next, float t);
  // The latest display copy: an RGBA8 texture, GameCube row 0 at GL row 0;
  // 0 before the first display copy.
  GLuint Xfb(unsigned *width, unsigned *height) const;
  // Draws the latest display copy into the current framebuffer's viewport
  // (x, y, w, h), upright; rows > 0: only XFB rows top .. top + rows - 1
  // (the letterbox cut off). False when there is none yet.
  bool Present(int x, int y, int w, int h, unsigned top = 0, unsigned rows = 0);
  // EFB colour texture (tests): GameCube coordinates times the scale.
  GLuint EfbColor() const;
  int Scale() const;   // whole multiples (1.5x: 1)
  int Halves() const;  // in halves (1.5x: 3)
  const GxStats &Stats() const;
  // True when draws use glDrawElementsBaseVertex (one attribute setup per layout).
  bool BaseVertex() const;
  // Keeps the linked shader programs in `folder` (it exists) and takes them
  // from there next time instead of compiling (the stutter of a new effect);
  // false when the driver has no program binaries (nothing is kept then).
  bool SetProgramCache(const std::string &folder);
  // The programs this device made before (the cache keeps their keys) and this renderer has not:
  // takes them from the cache, or links them anew when the sources or the driver changed, until
  // `budgetMs` is spent (at least one). The app calls it on display refreshes without a new chunk,
  // so a new effect does not stop the picture while its programs are made (Retroid report
  // 22.10.2026 23:42: a chunk of up to 435 ms in r10b while 61 programs were made). False when
  // none are left.
  bool Prewarm(double budgetMs);
  // The app's list of programs (a key list file, WriteProgramKeyList): Prewarm takes them too, for
  // the MSAA setting in use. Before the first Prewarm.
  void SetProgramList(const std::string &path);
  // Frees the GL objects (context current).
  void Release();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Shader programs made ahead (decision of the project owner, 23.10.2026: a list in the app, so the
// first start makes them too). A key list file: "RE4L", the size of a key, the count, the keys
// (port/tools/gx_program_list.py merges the lists of error reports into the app's asset
// gx-programs.keys).
//
// The keys of the cache's key files into one list file (the error report): how many (0 when it
// could not be written).
unsigned WriteProgramKeyList(const std::string &cache, const std::string &path);

// What WarmProgramCache did.
struct GxWarmResult {
  unsigned keys = 0;    // different keys of the cache and the list, for the MSAA setting
  unsigned listed = 0;  // of them from the list
  unsigned linked = 0;  // linked now
  unsigned had = 0;     // with a binary for this driver already
  unsigned failed = 0;
  unsigned stale = 0;   // old files removed (other sources or driver)
  double ms = 0.0;
};

// On the calling thread, with a GL context of its own current (core_mode.cpp starts one below
// normal priority): every program of the key list `list` (may be missing) and of the cache's key
// files that has no binary for this driver yet is linked and kept in `cache`, for the MSAA setting
// `centroid` (GxOptions::centroid with MSAA), until `stop` is set; old files of the same keys go.
// GxRenderer::Prewarm then only loads binaries: no link on the render thread.
GxWarmResult WarmProgramCache(const std::string &cache, const std::string &list, std::uint32_t centroid,
                              const std::atomic<bool> *stop);
// true while a WarmProgramCache runs (Prewarm waits for its binaries)
bool ProgramCacheWarming();

// The renderer's and the core mode's log lines: to logcat under `tag` and, once SetLogCopy gave a
// place, also as "tag: text" there (core_mode.cpp: the core's log file; Android 10 and older give
// the app no logcat to record, the error report then has only that file).
void LogTo(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void SetLogCopy(void (*copy)(const char *line));

} // namespace re4gx
