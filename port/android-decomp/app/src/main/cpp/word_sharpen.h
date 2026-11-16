// "Sharper menu text" (1.0.0, decision of the project owner, 16.11.2026; port/docs/textur-lader-plan.md): the menus'
// word pictures (tex_pack_words.h) drawn sharper at high resolutions. Each word is enlarged 2x (4x for small words at
// 3x and more) with a contour snap: the picture enlarged smoothly (Catmull-Rom), then in every channel the value
// between the darkest and brightest of the four source pixels around is read like a distance field, its middle drawn
// as an edge about one pixel wide; only where those four differ enough (letters, outlines), so wood grain and soft
// gradients stay as they are. The work runs on a thread of its own (WordSharpener); GxCore shows the game's own
// picture first and the sharper one from a later chunk on (GxCore::ApplyWords), as the HD replacements of T4.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace re4gx {

// whether a texture of this format and size can be a menu word at all (a quick test before its texels' hash)
bool MaybeMenuWord(unsigned format, unsigned width, unsigned height);

// the enlargement of a w x h word at the EFB scale `halves` (2: 1x ... 8: 4x): 0 below 1.5x (nothing to gain), 4 for
// small words (at most 64 rows and 32768 texels) from 3x on, else 2 (large text pages stay at 2x)
unsigned WordSharpenFactor(unsigned width, unsigned height, int halves);

// src: w x h RGBA8 (straight alpha). levels: the (w*f) x (h*f) picture, then each half down to w x h (the colour
// weighted by its alpha), one after the other; *levelCount their number (log2(f) + 1)
void SharpenWord(const std::uint8_t *src, unsigned w, unsigned h, unsigned f, std::vector<std::uint8_t> &levels,
                 unsigned *levelCount);

// the thread that sharpens the words GxCore asks for, below the game and the renderer. At most `maxReady` bytes wait
// finished and not taken (it waits then).
class WordSharpener {
 public:
  struct Job {
    std::uint64_t key = 0, ticket = 0;  // the game texture's key, GxCore's ticket for this request
    unsigned width = 0, height = 0, factor = 2;
    std::vector<std::uint8_t> rgba;  // the game's picture, RGBA8
  };
  struct Done {
    Job job;
    std::vector<std::uint8_t> levels;
    unsigned levelCount = 0;
    std::uint64_t us = 0;  // the work's time on the thread
  };
  explicit WordSharpener(std::size_t maxReady);
  ~WordSharpener();
  WordSharpener(const WordSharpener &) = delete;
  WordSharpener &operator=(const WordSharpener &) = delete;

  void Add(Job job);
  bool Take(Done &out);  // the oldest finished; false: none yet
  void Cancel();         // queued and finished ones go (one under way is dropped when it ends)
  std::size_t Waiting() const;

 private:
  void Loop();
  std::size_t maxReady_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::deque<Job> jobs_;
  std::deque<Done> ready_;
  std::size_t readyBytes_ = 0;
  std::uint64_t epoch_ = 0;
  bool working_ = false, stop_ = false;
  std::thread thread_;
};

}  // namespace re4gx
