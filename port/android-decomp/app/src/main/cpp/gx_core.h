// The inside of GxRenderer (gx_gl.h): one class per graphics API behind the same calls (port/docs/vulkan-bauplan.md,
// E1). GxRenderer forwards every call to its GxCore: GlRenderer (gx_gl.cpp, GLES 3) or, later, the Vulkan renderer.
// Step by step the GX emulation itself (registers, shader keys, in-between pictures, merged draws, the effects'
// logic) moves into this class, once for both APIs, and the API classes keep only what talks to the GPU.
#pragma once

#include "gx_gl.h"

#include <string>

struct Re4GxChunk;

namespace re4gx {

struct GxCore {
  explicit GxCore(GxApi a) : api(a) {}
  virtual ~GxCore() = default;
  GxCore(const GxCore &) = delete;
  GxCore &operator=(const GxCore &) = delete;

  const GxApi api;

  // GxRenderer's calls, with the same meaning (see gx_gl.h)
  virtual bool InitHalves(int halves, std::string *error) = 0;
  virtual void RescaleHalves(int halves) = 0;
  // `to`: a renderer of the same API (copies stay on their API's GPU objects); else nothing is handed over
  virtual void HandOverCopies(GxCore &to) = 0;
  virtual void SetOptions(const GxOptions &options) = 0;
  virtual int Samples() const = 0;
  virtual float Anisotropy() const = 0;
  virtual bool Invalidates() const = 0;
  virtual bool TiledMsaa() const = 0;
  virtual bool HalfDof() const = 0;
  virtual void Run(const Re4GxChunk &chunk) = 0;
  virtual void SetDepthSink(GxRenderer::DepthSink sink) = 0;
  virtual void SetFxaa(bool on) = 0;
  virtual void SetSharpen(int level) = 0;
  virtual void SetTvFilter(bool on) = 0;
  virtual void SetPsx(bool on) = 0;
  virtual void SetFlares(bool on) = 0;
  virtual void SetDofStrong(bool on) = 0;
  virtual void SetShownRows(int top, int bottom) = 0;
  virtual void SetWiden(float factor) = 0;
  virtual bool CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const = 0;
  virtual void Skip(const Re4GxChunk &a) = 0;
  virtual void SetRecording(bool on) = 0;
  virtual void SetMerge(bool on) = 0;
  virtual bool Merging() const = 0;
  virtual bool Interpolate(const Re4GxChunk &prev, const Re4GxChunk &next, float t) = 0;
  virtual unsigned Xfb(unsigned *width, unsigned *height) const = 0;
  virtual bool XfbWide() const = 0;
  virtual bool Present(int x, int y, int w, int h, unsigned top, unsigned rows) = 0;
  virtual unsigned EfbColor() const = 0;
  virtual int Scale() const = 0;
  virtual int Halves() const = 0;
  virtual const GxStats &Stats() const = 0;
  virtual bool BaseVertex() const = 0;
  virtual bool SetProgramCache(const std::string &folder) = 0;
  virtual bool Prewarm(double budgetMs) = 0;
  virtual void SetProgramList(const std::string &path) = 0;
  virtual void SetRoom(unsigned room) = 0;
  virtual void Release() = 0;
  // the start-up switches of the tests (RE4_GX_TV_FILTER, RE4_GX_PSX, RE4_GX_FLARES, RE4_GX_MAX_PROGRAMS), applied
  // by GxRenderer's constructor
  virtual void ApplyTestEnvironment() = 0;
};

// A renderer's inside for `api` (GlRenderer in gx_gl.cpp ...); null when this build has none for it.
std::unique_ptr<GxCore> MakeGlCore();
std::unique_ptr<GxCore> MakeCore(GxApi api);

} // namespace re4gx
