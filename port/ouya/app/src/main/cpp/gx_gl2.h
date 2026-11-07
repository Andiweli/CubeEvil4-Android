// The GX renderer of the OUYA app (stage O2 of port/docs/ouya-machbarkeit.md): executes the
// chunks the game core's emulated graphics processor decodes (port/whole/runtime/re4_gx_frame.h)
// with OpenGL ES 2.0, as the OUYA's Tegra 3 offers it. It is the main app's GLES 3 renderer
// (port/android-decomp/app/src/main/cpp/gx_gl.cpp) rebuilt for GLES 2; the main app keeps its
// own, this one is a copy (decision of the project owner, 22.10.2026).
//
// Like the GLES 3 renderer it keeps the register state the chunks change (BP, XF, the palette
// part of TMEM), draws into its own embedded frame buffer (EFB, 640x528, colour and depth), turns
// EFB copies into textures (GXCopyTex) and display copies into the picture to show (GXCopyDisp,
// the XFB), and generates its shaders from the state (gx_gl2_shader.cpp). It reads both vertex
// forms: the float vertex and the compact one (RE4_GX_V_COMPACT: the GX types as the game sent
// them, scaled in the vertex shader), which the core writes for the OUYA (RE4_GX_COMPACT=1).
//
// Left out against the GLES 3 renderer: multisampling, the gameplay autofocus (depth of field),
// the uniform stream, GPU timers and fences, program binaries (the Tegra 3 has none), base
// vertices. What the Tegra 3 lacks besides (measured, port/scripts/ouya-gpu-info.ps1,
// 22.10.2026): 16-bit depth only, no depth textures (depth copies read the depth back with
// NV_read_depth, or are lost), non-power-of-two textures only clamped, mipmaps only complete
// (the levels a GX texture leaves out are made here), 9 varying vec4, 252 vertex vec4, fp20 in
// fragment shaders, at most 256 instructions per shader (measured 22.10.2026).
//
// Orientation as in the GLES 3 renderer: the EFB keeps GameCube rows (row 0 = top) at GL row 0;
// Present flips the XFB.
#pragma once

#include <GLES2/gl2.h>

#include <cstdint>
#include <memory>
#include <string>

struct Re4GxChunk;

namespace re4gx2 {

struct GxStats {
  unsigned chunks = 0, draws = 0, triangles = 0;
  unsigned programs = 0;         // shaders built
  unsigned textures = 0;         // textures decoded
  unsigned textureCopies = 0;    // EFB copies to a texture
  unsigned displayCopies = 0;    // EFB copies to the XFB
  unsigned heldCopies = 0;       // of them not shown: the VI kept the picture before (RE4_GX_CHUNK_HOLD)
  unsigned resets = 0;
  unsigned missingTextures = 0;  // bound keys the renderer did not have
  unsigned shaderErrors = 0;
  unsigned leanShaders = 0;      // vertex shaders too long for the device, built again without the
                                 // q = 0 special case of the texture coordinates
  unsigned dstAlphaApprox = 0;   // draws whose constant destination alpha is not 0 and not exact
  unsigned depthCopies = 0;      // EFB depth copies to a texture
  unsigned depthReadBacks = 0;   // of them made from the depth read back (no depth textures)
  unsigned depthCopiesLost = 0;  // of them neither way possible (the copy reads as far away)
  unsigned npotClamped = 0;      // non-power-of-two textures the game repeats or mirrors, clamped here
  unsigned mipmapsMade = 0;      // levels made here because a GX texture stops before 1x1
  unsigned autofocusMarks = 0;   // BP 0x5A markers (the main app's depth of field), ignored here
  unsigned skippedDraws = 0;     // draws left out: no vertex format, or too many attributes
  unsigned compactDraws = 0;     // draws with the compact vertex
  unsigned textures16 = 0;       // texture levels uploaded with 16 bits (GxOptions::tex16)
  unsigned skippedChunks = 0;    // chunks run without drawing (Skip): state and textures only
  unsigned texturesDeferred = 0; // textures of skipped chunks kept as they came, not decoded (a movie's)
  unsigned deferredUploaded = 0; // of them decoded later: a drawn chunk bound them
  unsigned decodeMicros = 0;     // time decoding texels (GX formats to RGBA)
  unsigned uploadMicros = 0;     // time in glTexImage2D
  // GL work per draw
  unsigned uniformUploads = 0, programSwitches = 0, textureBinds = 0, stateChanges = 0, attributeSetups = 0;
  unsigned presents = 0;
};

// What the device offers, as Init found it (and the options left of it).
struct GxCaps {
  bool depthTexture = false;    // OES_depth_texture: depth copies on the GPU
  bool depth24 = false;         // OES_depth24
  bool depthNonlinear = false;  // NV_depth_nonlinear (the Tegra's 16-bit depth, spread like 24-bit)
  bool readDepth = false;       // NV_read_depth: depth copies through the CPU without depth textures
  bool npotRepeat = false;      // non-power-of-two textures repeat and mirror (OES_texture_npot)
  bool npotMipmap = false;      // ... have mipmaps (OES_texture_npot, NV_texture_npot_2D_mipmap)
  bool maxLevel = false;        // APPLE_texture_max_level: mipmap chains may stop early, as GX's
                                // (else the missing levels are made; not on the Tegra 3)
  bool fragmentHigh = false;    // highp in fragment shaders
  int depthBits = 0;            // the EFB's
  int maxAttribs = 0, maxVertexUniforms = 0, maxVaryings = 0, maxTextureSize = 0;
  std::string renderer;         // GL_RENDERER, GL_VERSION
};

struct GxOptions {
  // tests: behave as on the OUYA's Tegra 3 where the device can do more (Mesa): no depth
  // textures (depth copies through NV_read_depth, which the Tegra 3 has), 16-bit depth,
  // non-power-of-two textures only clamped, the Tegra's limits on attributes, vertex uniforms
  // and varyings (RE4_GX2_TEGRA3=1 does the same)
  bool tegra3 = false;
  // tests: texture coordinates rounded to 1/128 texel in the fragment shader as the GLES 3
  // renderer does (its integers), so both draw the same texels exactly; otherwise the vertex
  // shader scales them and the read takes them as they are (no arithmetic before it)
  bool texelCoords = false;
  // measurements (RE4_GX2_PROBE, a comma list; the picture is wrong then): "flat" fragment
  // shaders that only pass the colour on (what the TEV costs), "nocopy" EFB copies left out but
  // their clear (what the copies cost), "nodraw" no glDrawElements (what the GL calls around
  // the draws cost), "noupload" the vertices and indices of a chunk only uploaded the first time,
  // "noattrib" attribute pointers only set when the format changes (not at each draw's first
  // vertex), "nouniform" uniforms only the first time a program is used, "simplevs" vertex shaders
  // without lighting and texture coordinates (with "flat")
  bool probeFlat = false, probeNoCopy = false, probeNoDraw = false, probeNoUpload = false;
  bool probeNoAttrib = false, probeNoUniform = false, probeSimpleVs = false;
  // "half": the draws into a quarter of the EFB's pixels (what a lower resolution would save)
  bool probeHalf = false;
  // the EFB's resolution in eighths of the GameCube's 640x528 (0: Init's scale; 6: 0.75x,
  // 480x396; 2..32). RE4_GX2_SCALE=0.75 does the same. Copies and the display copy follow it.
  int resolutionEighths = 0;
  // the TEV in plain fractions 0..1 (RE4_GX2_FAST_TEV=1): no whole numbers 0..255, no rounding
  // and wrapping to 8 bits, the lerp as mix(); compare stages and the alpha test still on the
  // 255 steps. Close to the GameCube, not to the step: far fewer instructions per pixel (the TEV
  // was 50-80 % of a frame on the OUYA)
  bool fastTev = false;
  // at most this many lights per vertex (RE4_GX2_LIGHTS=n; 0: all, as the GameCube): for each draw
  // the strongest at the model's origin (its default position matrix), the others added as a
  // constant (their light at the origin, half of it where the diffuse function reads the normal).
  // The two strongest give 92-100 % of the light there in r100 and r101 (07.11.2026)
  int maxLights = 0;
  // 16-bit colour (wish of the project owner, 07.11.2026): textures (RE4_GX2_TEX16=1) as RGB565
  // when opaque, RGBA5551 when their alpha is only 0 and 255 (cut-outs), else as they are (a soft
  // alpha in 4 bits would band); the EFB (RE4_GX2_EFB16=1) as RGB565 with the GPU's dithering
  // (no destination alpha: it reads as 1)
  bool tex16 = false, efb16 = false;
};

class GxRenderer {
public:
  GxRenderer();
  ~GxRenderer();
  GxRenderer(const GxRenderer &) = delete;
  GxRenderer &operator=(const GxRenderer &) = delete;

  // Before Init.
  void SetOptions(const GxOptions &options);
  // Creates the GL objects (a GLES 2.0 context is current). `scale`: EFB resolution multiple
  // (1 = the GameCube's 640x528; the OUYA's).
  bool Init(int scale, std::string *error);
  const GxCaps &Caps() const;
  // Runs one chunk (in sequence order).
  void Run(const Re4GxChunk &chunk);
  // The latest display copy: an RGBA texture, GameCube row 0 at GL row 0; 0 before the first.
  // `width`, `height`: its pixels (the GameCube's at resolution 1x, fewer below).
  GLuint Xfb(unsigned *width, unsigned *height) const;
  // Draws the latest display copy into the current framebuffer's viewport (x, y, w, h),
  // upright; rows > 0: only XFB rows top .. top + rows - 1. False when there is none yet.
  bool Present(int x, int y, int w, int h, unsigned top = 0, unsigned rows = 0);
  // Frame skip (the OUYA app; gx_gl.cpp's of the main app, 26.10.2026): the renderer is behind when
  // the core has finished two chunks, `a` need not be drawn when `b` (the next one) is drawn at once.
  // Skip runs `a` without its draws and copies: the register, palette and texture state the game
  // builds on is applied, nothing is drawn, the EFB is cleared as the display copy would. The game is
  // not slowed down by a renderer that cannot keep up. CanSkip: false when `a` starts over, is held,
  // black or marked, ends without a display copy, or makes an EFB copy for a texture that `b` could
  // still read (unless `b` copies to the same place first).
  bool CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const;
  // A texture copy of `a` that `b` reads before copying there again (CanSkip without its other rules;
  // the OUYA app checks the chunks the core left without draws with it)
  bool CopiesReadLater(const Re4GxChunk &a, const Re4GxChunk &b) const;
  void Skip(const Re4GxChunk &a);
  // EFB colour texture (tests): GameCube coordinates times the scale.
  GLuint EfbColor() const;
  int Scale() const;
  // the EFB's resolution in eighths (8: 640x528)
  int ResolutionEighths() const;
  const GxStats &Stats() const;
  // Frees the GL objects (context current).
  void Release();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// The renderer's log lines: to logcat under `tag` (stderr off Android) and, once SetLogCopy gave
// a place, also as "tag: text" there.
void LogTo(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void SetLogCopy(void (*copy)(const char *line));

} // namespace re4gx2
