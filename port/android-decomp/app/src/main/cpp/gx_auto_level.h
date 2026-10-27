// "Dynamic" in the app menu's resolution (wish of the project owner, 23.10.2026; until then "Auto"):
// the EFB level follows the GPU. It starts at the screen's level (one below the level whose 336 rows
// come closest to the screen's rows, CoreMenu.autoScale) and never goes above it.
//
// Windows of at least 2 s and 20 game pictures: two in a row with fewer than 28.5 pictures a second
// while the GPU is behind (the fence after every chunk, GxStats::behind*: on average at least half a
// chunk not finished) take one level down; one such window alone is a hitch (a room loading, a new
// shader: the Retroid's report 23.10.2026 15:06 went down at the start of r10c and 4 s after a step
// up), not a GPU too slow. A game that is slow on its own (the processor, loading) leaves the GPU
// idle and the level as it is. Up again one level after 5 windows in a row (10 s) at 29.5
// pictures a second or more with the GPU keeping up (under 0.1 behind), at the earliest 30 s after
// the last step down; down again within 15 s of a step up doubles that wait (at most 5 minutes).
// With 60 FPS the in-between pictures the renderer left out because it was behind (core_mode.cpp)
// count as well: a window with more than a quarter of them left out while the GPU is behind is slow
// too (the game keeps its pace then, the GPU still does not keep up: Retroid 26.10.2026, level 2 with
// 60 FPS stayed at 17-19 pictures a second), and up again only with almost none left out.
// The GPU is also too slow when the render thread waits for it inside the driver (its Run takes much
// longer than its processor time): then the fences are all reached, the GPU is not "behind", and the
// game still waits for the renderer (Retroid 27.10.2026, the village and the lake at level 2 with 2x
// MSAA: 22-25 pictures a second, the render thread waiting 11-35 ms a chunk, never more than 8.4 ms
// in the smooth parts). A slow window with at least kWaitDown ms of such waiting on average counts as
// the GPU behind; up again only with less than kWaitUp.
// core_mode.cpp applies a new level between two Runs (GxRenderer::Rescale).
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>

namespace re4gx {

class AutoLevel {
public:
  static constexpr double kWindow = 2.0;      // seconds
  static constexpr unsigned kWindowChunks = 20;
  static constexpr double kSlow = 28.5;       // game pictures a second
  static constexpr double kFast = 29.5;
  static constexpr double kBehindDown = 0.5;  // chunks the GPU has not finished, on average
  static constexpr double kBehindUp = 0.1;
  static constexpr int kGoodWindows = 5;
  static constexpr int kBadWindows = 2;
  static constexpr double kFirstHold = 30.0;  // seconds after a step down before a step up
  static constexpr double kMostHold = 300.0;
  static constexpr double kBounce = 15.0;     // down again this soon after up: the wait doubles
  static constexpr double kDroppedDown = 0.25; // share of the chunks without their in-between picture
  static constexpr double kDroppedUp = 0.05;
  static constexpr double kWaitDown = 10.0;    // ms a chunk the render thread waits for the GPU
  static constexpr double kWaitUp = 5.0;

  // A renderer started at `top` (the screen's level, its highest) at `t` seconds.
  void Start(int top, double t) {
    *this = AutoLevel();
    top_ = level_ = std::max(1, top);
    since_ = t;
    started_ = true;
  }

  // After each real chunk: the time, the GPU's fences so far (GxStats::behindSum / behindChecks),
  // whether the level may change now (no menu open, no R3 series running), and the in-between
  // pictures left out so far (60 FPS; 0 without), the ms the render thread waited in its Runs so far
  // (run time minus processor time). The level to use from the next chunk on; on a change `why` says
  // why (a log line).
  int Chunk(double t, unsigned long long behindSum, unsigned behindChecks, bool mayChange, std::string *why,
            unsigned long long dropped = 0, double waitMs = 0.0) {
    if (!started_)
      return level_;
    if (!windowOpen_) {
      windowOpen_ = true;
      behindSum_ = behindSum;
      behindChecks_ = behindChecks;
      dropped_ = dropped;
      waitMs_ = waitMs;
    }
    chunks_++;
    const double seconds = t - since_;
    if (seconds < kWindow || chunks_ < kWindowChunks)
      return level_;
    const double rate = chunks_ / seconds;
    const unsigned checks = behindChecks - behindChecks_;
    const double behind = checks ? static_cast<double>(behindSum - behindSum_) / checks : 0.0;
    const double droppedShare = static_cast<double>(dropped - dropped_) / chunks_;
    const double wait = (waitMs - waitMs_) / chunks_;
    since_ = t;
    chunks_ = 0;
    behindSum_ = behindSum;
    behindChecks_ = behindChecks;
    dropped_ = dropped;
    waitMs_ = waitMs;
    if (!mayChange) {
      good_ = bad_ = 0;
      return level_;
    }
    char line[256];
    const bool slow =
        (rate < kSlow || droppedShare > kDroppedDown) && (behind >= kBehindDown || wait >= kWaitDown) && level_ > 1;
    bad_ = slow ? bad_ + 1 : 0;
    if (slow && bad_ < kBadWindows) {
      good_ = 0;
      return level_;
    }
    if (slow) {
      bad_ = 0;
      // straight down again after a step up: that level was too much, wait longer next time
      if (upAt_ >= 0.0 && t - upAt_ < kBounce)
        hold_ = std::min(kMostHold, hold_ * 2.0);
      upAt_ = -1.0;
      holdUntil_ = t + hold_;
      good_ = 0;
      std::snprintf(line, sizeof(line),
                    "dynamic resolution: level %d -> %d (%.1f game pictures/s, the GPU %.1f chunks behind, the render "
                    "thread waiting %.1f ms a chunk, %.0f %% without their in-between picture)",
                    level_, level_ - 1, rate, behind, wait, droppedShare * 100.0);
      level_--;
      downs_++;
      if (why)
        *why = line;
      return level_;
    }
    if (rate >= kFast && behind < kBehindUp && wait < kWaitUp && droppedShare < kDroppedUp && level_ < top_) {
      if (++good_ >= kGoodWindows && t >= holdUntil_) {
        good_ = 0;
        upAt_ = t;
        std::snprintf(line, sizeof(line),
                      "dynamic resolution: level %d -> %d (%.0f s at %.1f game pictures/s, the GPU keeping up)", level_,
                      level_ + 1, kGoodWindows * kWindow, rate);
        level_++;
        ups_++;
        if (why)
          *why = line;
      }
      return level_;
    }
    good_ = 0;
    return level_;
  }

  int Level() const { return level_; }
  int Top() const { return top_; }
  unsigned Downs() const { return downs_; }
  unsigned Ups() const { return ups_; }
  double Hold() const { return hold_; }

private:
  bool started_ = false, windowOpen_ = false;
  int top_ = 1, level_ = 1;
  double since_ = 0.0;
  unsigned chunks_ = 0;
  unsigned long long behindSum_ = 0, dropped_ = 0;
  double waitMs_ = 0.0;
  unsigned behindChecks_ = 0;
  int good_ = 0, bad_ = 0;
  double upAt_ = -1.0, holdUntil_ = 0.0, hold_ = kFirstHold;
  unsigned downs_ = 0, ups_ = 0;
};

} // namespace re4gx
