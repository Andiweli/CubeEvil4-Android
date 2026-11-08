// GxRenderer (gx_gl.h): every call goes to the renderer of its graphics API (gx_core.h; port/docs/vulkan-bauplan.md).
#include "gx_core.h"

#include <algorithm>

namespace re4gx {

std::unique_ptr<GxCore> MakeCore(GxApi api) {
  switch (api) {
  case GxApi::kGl:
    return MakeGlCore();
  case GxApi::kVulkan:
#ifdef RE4_GX_VULKAN
    return MakeVkCore();
#else
    break;
#endif
  }
  return nullptr;
}

GxRenderer::GxRenderer(GxApi api) : api_(api), impl_(MakeCore(api)) {
  if (!impl_) {
    // no renderer of that API in this build: GLES draws (the app's fallback)
    api_ = GxApi::kGl;
    impl_ = MakeCore(api_);
  }
  impl_->ApplyTestEnvironment();
}

GxRenderer::~GxRenderer() = default;

bool GxRenderer::Init(int scale, std::string *error) { return InitHalves(2 * std::max(1, scale), error); }
bool GxRenderer::InitHalves(int halves, std::string *error) { return impl_->InitHalves(halves, error); }
void GxRenderer::Rescale(int scale) { RescaleHalves(2 * std::max(1, scale)); }
void GxRenderer::RescaleHalves(int halves) { impl_->RescaleHalves(halves); }
void GxRenderer::HandOverCopies(GxRenderer &to) { impl_->HandOverCopies(*to.impl_); }
void GxRenderer::SetOptions(const GxOptions &options) { impl_->SetOptions(options); }
int GxRenderer::Samples() const { return impl_->Samples(); }
float GxRenderer::Anisotropy() const { return impl_->Anisotropy(); }
bool GxRenderer::Invalidates() const { return impl_->Invalidates(); }
bool GxRenderer::TiledMsaa() const { return impl_->TiledMsaa(); }
bool GxRenderer::HalfDof() const { return impl_->HalfDof(); }
void GxRenderer::Run(const Re4GxChunk &chunk) { impl_->Run(chunk); }
void GxRenderer::SetDepthSink(DepthSink sink) { impl_->SetDepthSink(std::move(sink)); }
void GxRenderer::SetFxaa(bool on) { impl_->SetFxaa(on); }
void GxRenderer::SetSharpen(int level) { impl_->SetSharpen(level); }
void GxRenderer::SetTvFilter(bool on) { impl_->SetTvFilter(on); }
void GxRenderer::SetPsx(bool on) { impl_->SetPsx(on); }
void GxRenderer::SetFlares(bool on) { impl_->SetFlares(on); }
void GxRenderer::SetDofStrong(bool on) { impl_->SetDofStrong(on); }
void GxRenderer::SetShownRows(int top, int bottom) { impl_->SetShownRows(top, bottom); }
void GxRenderer::SetWiden(float factor) { impl_->SetWiden(factor); }
bool GxRenderer::CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const { return impl_->CanSkip(a, b); }
void GxRenderer::Skip(const Re4GxChunk &a) { impl_->Skip(a); }
void GxRenderer::SetRecording(bool on) { impl_->SetRecording(on); }
void GxRenderer::SetMerge(bool on) { impl_->SetMerge(on); }
bool GxRenderer::Merging() const { return impl_->Merging(); }
bool GxRenderer::Interpolate(const Re4GxChunk &prev, const Re4GxChunk &next, float t) {
  return impl_->Interpolate(prev, next, t);
}
GLuint GxRenderer::Xfb(unsigned *width, unsigned *height) const { return impl_->Xfb(width, height); }
bool GxRenderer::XfbWide() const { return impl_->XfbWide(); }
bool GxRenderer::Present(int x, int y, int w, int h, unsigned top, unsigned rows) {
  return impl_->Present(x, y, w, h, top, rows);
}
GLuint GxRenderer::EfbColor() const { return impl_->EfbColor(); }
void GxRenderer::SetWindow(void *window, int width, int height) { impl_->SetWindow(window, width, height); }
void GxRenderer::SetOverlay(const GxOverlay &overlay) { impl_->SetOverlay(overlay); }
std::string GxRenderer::ApiInfo() const { return impl_->ApiInfo(); }
std::string GxRenderer::TimingLine() { return impl_->TimingLine(); }
bool GxRenderer::ReadXfb(std::vector<std::uint8_t> *rgba, unsigned *width, unsigned *height) {
  return impl_->ReadXfb(rgba, width, height);
}
bool GxRenderer::ReadPresented(int screenW, int screenH, int x, int y, int w, int h, unsigned top, unsigned rows,
                               std::vector<std::uint8_t> *rgba) {
  return impl_->ReadPresented(screenW, screenH, x, y, w, h, top, rows, rgba);
}
int GxRenderer::Scale() const { return impl_->Scale(); }
int GxRenderer::Halves() const { return impl_->Halves(); }
const GxStats &GxRenderer::Stats() const { return impl_->Stats(); }
bool GxRenderer::BaseVertex() const { return impl_->BaseVertex(); }
bool GxRenderer::SetProgramCache(const std::string &folder) { return impl_->SetProgramCache(folder); }
bool GxRenderer::Prewarm(double budgetMs) { return impl_->Prewarm(budgetMs); }
void GxRenderer::SetProgramList(const std::string &path) { impl_->SetProgramList(path); }
void GxRenderer::SetRoom(unsigned room) { impl_->SetRoom(room); }
void GxRenderer::Release() {
  impl_->Release();
  // as before: a fresh renderer of the same API afterwards (without the start-up switches of the tests)
  impl_ = MakeCore(api_);
}

} // namespace re4gx
