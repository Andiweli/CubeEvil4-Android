// "Sharper menu text" (see word_sharpen.h).
#include "word_sharpen.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <pthread.h>
#include <sys/resource.h>

#include "tex_pack.h"
#include "tex_pack_words.h"

namespace re4gx {
namespace {

std::uint64_t SizeKey(unsigned format, unsigned width, unsigned height) {
  return static_cast<std::uint64_t>(width) << 24 | static_cast<std::uint64_t>(height) << 8 | format;
}

const std::vector<std::uint64_t> &WordSizes() {
  static const std::vector<std::uint64_t> sizes = [] {
    std::vector<std::uint64_t> v;
    for (const KeptTexture &k : kMenuWords) v.push_back(SizeKey(k.format, k.width, k.height));
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
  }();
  return sizes;
}

float CatmullRom(float x) {
  x = std::fabs(x);
  if (x < 1.0f) return 1.5f * x * x * x - 2.5f * x * x + 1.0f;
  if (x < 2.0f) return -0.5f * x * x * x + 2.5f * x * x - 4.0f * x + 2.0f;
  return 0.0f;
}

// the four taps of each output column (or row) enlarged f times from n: indices clamped to the edge, weights summing up
// to 1
struct Taps {
  int idx[4];
  float wt[4];
};
std::vector<Taps> MakeTaps(unsigned n, unsigned f) {
  std::vector<Taps> taps(n * f);
  for (unsigned o = 0; o < n * f; o++) {
    const float s = (o + 0.5f) / f - 0.5f;
    const int c = static_cast<int>(std::floor(s));
    float sum = 0.0f;
    for (int k = 0; k < 4; k++) {
      const int i = c - 1 + k;
      taps[o].idx[k] = std::clamp(i, 0, static_cast<int>(n) - 1);
      taps[o].wt[k] = CatmullRom(s - i);
      sum += taps[o].wt[k];
    }
    for (float &w : taps[o].wt) w /= sum;
  }
  return taps;
}

}  // namespace

bool MaybeMenuWord(unsigned format, unsigned width, unsigned height) {
  const std::vector<std::uint64_t> &sizes = WordSizes();
  return std::binary_search(sizes.begin(), sizes.end(), SizeKey(format, width, height)) ||
         IsTestMenuWordSize(format, width, height);
}

unsigned WordSharpenFactor(unsigned width, unsigned height, int halves) {
  if (halves < 3) return 0;
  const bool small = height <= 64 && static_cast<std::uint64_t>(width) * height <= 32768;
  return halves >= 6 && small ? 4 : 2;
}

void SharpenWord(const std::uint8_t *src, unsigned w, unsigned h, unsigned f, std::vector<std::uint8_t> &levels,
                 unsigned *levelCount) {
  const unsigned W = w * f, H = h * f;
  const std::size_t n = static_cast<std::size_t>(w) * h;
  // premultiplied floats 0..1
  std::vector<float> p(n * 4);
  for (std::size_t i = 0; i < n; i++) {
    const float a = src[i * 4 + 3] / 255.0f;
    for (int c = 0; c < 3; c++) p[i * 4 + c] = src[i * 4 + c] / 255.0f * a;
    p[i * 4 + 3] = a;
  }
  // enlarged smoothly: rows first, then columns
  const std::vector<Taps> tx = MakeTaps(w, f), ty = MakeTaps(h, f);
  std::vector<float> rows(static_cast<std::size_t>(W) * h * 4), up(static_cast<std::size_t>(W) * H * 4);
  for (unsigned y = 0; y < h; y++)
    for (unsigned x = 0; x < W; x++) {
      const Taps &t = tx[x];
      float *o = &rows[(static_cast<std::size_t>(y) * W + x) * 4];
      for (int c = 0; c < 4; c++) {
        float v = 0.0f;
        for (int k = 0; k < 4; k++) v += t.wt[k] * p[(static_cast<std::size_t>(y) * w + t.idx[k]) * 4 + c];
        o[c] = v;
      }
    }
  for (unsigned y = 0; y < H; y++) {
    const Taps &t = ty[y];
    for (unsigned x = 0; x < W; x++) {
      float *o = &up[(static_cast<std::size_t>(y) * W + x) * 4];
      for (int c = 0; c < 4; c++) {
        float v = 0.0f;
        for (int k = 0; k < 4; k++) v += t.wt[k] * rows[(static_cast<std::size_t>(t.idx[k]) * W + x) * 4 + c];
        o[c] = v;
      }
    }
  }
  // the contour snap; out in straight RGBA8
  levels.resize(static_cast<std::size_t>(W) * H * 4);
  auto srcAt = [&](int x, int y) { return &p[(static_cast<std::size_t>(y) * w + x) * 4]; };
  auto upAt = [&](unsigned x, unsigned y) { return &up[(static_cast<std::size_t>(y) * W + x) * 4]; };
  for (unsigned y = 0; y < H; y++) {
    const int y0 = std::max(0, static_cast<int>(std::floor((y + 0.5f) / f - 0.5f)));
    const int y1 = std::min(static_cast<int>(h) - 1, y0 + 1);
    const unsigned ym = y ? y - 1 : 0, yp = y + 1 < H ? y + 1 : y;
    for (unsigned x = 0; x < W; x++) {
      const int x0 = std::max(0, static_cast<int>(std::floor((x + 0.5f) / f - 0.5f)));
      const int x1 = std::min(static_cast<int>(w) - 1, x0 + 1);
      const unsigned xm = x ? x - 1 : 0, xp = x + 1 < W ? x + 1 : x;
      const float *s00 = srcAt(x0, y0), *s01 = srcAt(x1, y0), *s10 = srcAt(x0, y1), *s11 = srcAt(x1, y1);
      const float *left = upAt(xm, y), *right = upAt(xp, y), *above = upAt(x, ym), *below = upAt(x, yp);
      float v[4];
      for (int c = 0; c < 4; c++) {
        const float u = upAt(x, y)[c];
        const float mn = std::min(std::min(s00[c], s01[c]), std::min(s10[c], s11[c]));
        const float mx = std::max(std::max(s00[c], s01[c]), std::max(s10[c], s11[c]));
        const float range = mx - mn;
        v[c] = u;
        if (range > 0.04f) {
          const float gx = 0.5f * (right[c] - left[c]), gy = 0.5f * (below[c] - above[c]);
          const float g = std::sqrt(gx * gx + gy * gy);
          const float dist = (u - 0.5f * (mn + mx)) / std::max(g, 1e-4f);  // pixels here to the edge
          const float snapped = mn + range * std::clamp(0.5f + dist, 0.0f, 1.0f);
          const float t = std::clamp((range - 0.12f) / 0.18f, 0.0f, 1.0f);
          v[c] = u + (snapped - u) * t * t * (3.0f - 2.0f * t);
        }
      }
      const float a = std::clamp(v[3], 0.0f, 1.0f);
      std::uint8_t *o = &levels[(static_cast<std::size_t>(y) * W + x) * 4];
      for (int c = 0; c < 3; c++)
        o[c] = static_cast<std::uint8_t>(std::lround(a > 1e-4f ? std::clamp(v[c], 0.0f, a) / a * 255.0f : 0.0f));
      o[3] = static_cast<std::uint8_t>(std::lround(a * 255.0f));
    }
  }
  // the smaller levels down to the game's size: 2 x 2 averaged, the colour weighted by its alpha
  unsigned count = 1, lw = W, lh = H;
  std::size_t at = 0;
  while (lw > w || lh > h) {
    const unsigned nw = std::max(w, lw / 2), nh = std::max(h, lh / 2);
    const std::size_t from = at, to = levels.size();
    levels.resize(to + static_cast<std::size_t>(nw) * nh * 4);
    for (unsigned y = 0; y < nh; y++)
      for (unsigned x = 0; x < nw; x++) {
        unsigned a = 0, c[3] = {0, 0, 0};
        for (unsigned dy = 0; dy < 2; dy++)
          for (unsigned dx = 0; dx < 2; dx++) {
            const std::size_t sy = std::min(y * 2 + dy, lh - 1), sx = std::min(x * 2 + dx, lw - 1);
            const std::uint8_t *s = &levels[from + (sy * lw + sx) * 4];
            a += s[3];
            for (int i = 0; i < 3; i++) c[i] += s[i] * s[3];
          }
        std::uint8_t *o = &levels[to + (static_cast<std::size_t>(y) * nw + x) * 4];
        for (int i = 0; i < 3; i++) o[i] = static_cast<std::uint8_t>(a ? (c[i] + a / 2) / a : 0);
        o[3] = static_cast<std::uint8_t>((a + 2) / 4);
      }
    at = to;
    lw = nw;
    lh = nh;
    count++;
  }
  *levelCount = count;
}

WordSharpener::WordSharpener(std::size_t maxReady) : maxReady_(maxReady ? maxReady : 1) {
  thread_ = std::thread([this] { Loop(); });
}

WordSharpener::~WordSharpener() {
  {
    std::lock_guard<std::mutex> lock(m_);
    stop_ = true;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void WordSharpener::Add(Job job) {
  {
    std::lock_guard<std::mutex> lock(m_);
    jobs_.push_back(std::move(job));
  }
  cv_.notify_all();
}

bool WordSharpener::Take(Done &out) {
  {
    std::lock_guard<std::mutex> lock(m_);
    if (ready_.empty()) return false;
    out = std::move(ready_.front());
    ready_.pop_front();
    readyBytes_ -= std::min(readyBytes_, out.levels.size());
  }
  cv_.notify_all();
  return true;
}

void WordSharpener::Cancel() {
  {
    std::lock_guard<std::mutex> lock(m_);
    jobs_.clear();
    ready_.clear();
    readyBytes_ = 0;
    epoch_++;
  }
  cv_.notify_all();
}

std::size_t WordSharpener::Waiting() const {
  std::lock_guard<std::mutex> lock(m_);
  return jobs_.size() + ready_.size() + (working_ ? 1 : 0);
}

void WordSharpener::Loop() {
#ifdef __linux__
  pthread_setname_np(pthread_self(), "re4-word-sharp");
  setpriority(PRIO_PROCESS, 0, 10);  // (this thread: below the game and the renderer)
#endif
  std::unique_lock<std::mutex> lock(m_);
  for (;;) {
    cv_.wait(lock, [this] { return stop_ || (!jobs_.empty() && readyBytes_ < maxReady_); });
    if (stop_) return;
    Done d;
    d.job = std::move(jobs_.front());
    jobs_.pop_front();
    const std::uint64_t epoch = epoch_;
    working_ = true;
    lock.unlock();
    const auto start = std::chrono::steady_clock::now();
    SharpenWord(d.job.rgba.data(), d.job.width, d.job.height, d.job.factor, d.levels, &d.levelCount);
    d.us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    d.job.rgba.clear();
    d.job.rgba.shrink_to_fit();
    lock.lock();
    working_ = false;
    if (epoch == epoch_ && !stop_) {
      readyBytes_ += d.levels.size();
      ready_.push_back(std::move(d));
    }
  }
}

}  // namespace re4gx
