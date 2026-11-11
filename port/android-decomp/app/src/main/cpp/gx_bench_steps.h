// The benchmark's steps (app menu Display -> GRAPHICS -> Benchmark; core_mode.cpp LevelBench leads it on the
// render thread): two passes, 30 then 60 pictures a second. The 30 pass starts at 4x, whatever the menu's
// resolution (since 0.99.10 at a lower level the app gives for a weak graphics chip, after a warm-up of some seconds
// that does not count), holds every level 10 seconds and goes down half a level (4 -> 3.5 -> 3 -> 2.5 -> 2 -> 1.5 -> 1)
// until a level holds; that level is the pass's result; the 60 pass starts at the 30 pass's result (0.99.10: 60 never
// holds where 30 does not). When even 1x does not hold, the result is 1x, marked as not held.
// A pass ends after kPassLimit seconds at the latest (the result then the level it stood at, not held), so the
// benchmark never runs on (the project owner's device test 06.11.2026: once only a moment at 1x, once without an
// end at 3.5x: "Dynamic" set its own level over the benchmark's on every chunk).
// A level holds when, in its seconds after the first kSettle (a new EFB, new shader programs), the game made 29
// pictures a second on average with at most one weak second (below 27); for 60 also 57 shown on average (a weak
// second: below 54) and at most 3 % of the in-between pictures left out. Until 06.11.2026 every second needed 29
// (58 shown) and not one in-between picture could be missing: one hitch failed a level, and the project owner's
// Retroid, which plays at 2x fairly steadily, was given 1x. Fed once a second with that second's figures (Second), on a clock of its own: the
// renderer's statistics start over with a new renderer, these do not. No GL here: the host test runs it
// (tools/test_gx_gl.cpp TestBenchSteps) with slow and fast devices.
#pragma once

#include <algorithm>
#include <cstdio>
#include <string>

namespace re4gx {

class BenchSteps {
public:
  static constexpr int kTop = 8, kBottom = 2;  // halves: 4x, 1x
  static constexpr double kHold = 10.0;        // seconds a level (the project owner, 06.11.2026: 5 s was too short)
  static constexpr double kSettle = 2.5;       // its first seconds do not count (a new EFB, in-between pictures on again)
  static constexpr double kPassLimit = 100.0;  // seconds a pass at most (7 levels: 70 s)
  static constexpr int kMinSamples = 5;        // counted seconds a level needs
  static constexpr double kAvgChunks = 29.0;   // game pictures a second on average
  static constexpr double kAvgShown = 57.0;    // with 60: pictures shown a second on average
  static constexpr int kWeakChunks = 27;       // a second below this (or below kWeakShown with 60) is weak
  static constexpr int kWeakShown = 54;
  static constexpr int kWeakSeconds = 1;       // weak seconds a level may have (a hitch: a sound, a shader)
  static constexpr unsigned kDroppedPercent = 3;  // with 60: in-between pictures left out, % of the game's

  enum Step { kNone, kLevel, kPassDone, kDone };

  // `top`: the 30 pass's first level (halves; 0.99.10: a weak graphics chip starts at 2.5x); `warmup`: seconds at it
  // before anything counts (0.99.10: on the very first start the scene's shader programs are made then)
  void Start(double t, int top = kTop, double warmup = 0.0) {
    *this = BenchSteps();
    top_ = top >= kBottom && top <= kTop ? top : kTop;
    StartPass(0, t + (warmup > 0.0 ? warmup : 0.0), top_);
  }

  int Pass() const { return pass_; }           // 0: 30 pictures a second, 1: 60
  int Level() const { return level_; }
  int Result(int pass) const { return result_[pass]; }
  bool Held(int pass) const { return held_[pass]; }
  bool Finished() const { return finished_; }
  int Rate10() const { return rate10_; }       // the last second's pictures a second * 10 (game or shown)
  // 0.99.10: the 30 pass did not hold even 1x and the lower levels hardly helped (under 10 % more game pictures at 1x
  // than at its first level): the processor holds the game back, not the graphics chip
  bool CpuSign() const { return cpuSign_; }

  // One second's figures at `t`: game pictures, pictures shown, in-between pictures left out. kLevel: set
  // Level() now; kPassDone: the 60 pass starts (set Level() and 60 FPS); kDone: both results are there.
  Step Second(double t, int chunks, int shown, unsigned dropped, std::string *log) {
    if (finished_) return kDone;
    const bool sixty = pass_ == 1;
    rate10_ = (sixty ? shown : chunks) * 10;
    if (t - levelAt_ > kSettle) {
      samples_++;
      sumChunks_ += chunks;
      sumShown_ += shown;
      minChunks_ = std::min(minChunks_, chunks);
      minShown_ = std::min(minShown_, shown);
      dropped_ += dropped;
      if (chunks < kWeakChunks || (sixty && shown < kWeakShown)) weak_++;
    }
    const bool overTime = t - passAt_ >= kPassLimit;
    if (t - levelAt_ < kHold && !overTime) return kNone;
    const double avgChunks = samples_ ? static_cast<double>(sumChunks_) / samples_ : 0.0;
    const double avgShown = samples_ ? static_cast<double>(sumShown_) / samples_ : 0.0;
    const bool holds = samples_ >= kMinSamples && avgChunks >= kAvgChunks && weak_ <= kWeakSeconds &&
                       (!sixty || (avgShown >= kAvgShown && dropped_ * 100 <= static_cast<unsigned>(sumChunks_) * kDroppedPercent));
    char line[360];
    std::snprintf(line, sizeof(line),
                  "benchmark: %d FPS pass, level %s for %.1f s: game %.1f pictures a second on average (at least %d), "
                  "shown %.1f (at least %d), %d weak seconds, %u in-between left out: %s",
                  sixty ? 60 : 30, Name(level_).c_str(), t - levelAt_, avgChunks, samples_ ? minChunks_ : 0, avgShown,
                  samples_ ? minShown_ : 0, weak_, dropped_,
                  holds ? "stable" : overTime ? "not stable, the pass's time is up" : "not stable");
    if (log) *log = line;
    if (pass_ == 0) {
      if (firstLevel_ < 0) {
        firstLevel_ = level_;
        firstAvg_ = avgChunks;
      }
      lastAvg_ = avgChunks;
    }
    if (!holds && level_ > kBottom && !overTime) {
      level_--;
      NewLevel(t);
      return kLevel;
    }
    result_[pass_] = level_;
    held_[pass_] = holds;
    if (pass_ == 0) {
      cpuSign_ = !holds && level_ == kBottom && firstLevel_ > kBottom && lastAvg_ < firstAvg_ * 1.10;
      // the 60 pass from the 30 pass's level (0.99.10): 60 pictures a second never hold where 30 do not
      StartPass(1, t, result_[0]);
      return kPassDone;
    }
    finished_ = true;
    return kDone;
  }

  static std::string Name(int halves) {
    char s[16];
    std::snprintf(s, sizeof(s), halves % 2 ? "%d.5x" : "%dx", halves / 2);
    return s;
  }

private:
  void StartPass(int pass, double t, int level) {
    pass_ = pass;
    passAt_ = t;
    level_ = level;
    NewLevel(t);
  }
  void NewLevel(double t) {
    levelAt_ = t;
    samples_ = sumChunks_ = sumShown_ = weak_ = 0;
    minChunks_ = minShown_ = 1000;
    dropped_ = 0;
  }

  int pass_ = 0, level_ = kTop;
  double passAt_ = 0.0, levelAt_ = 0.0;
  int samples_ = 0, sumChunks_ = 0, sumShown_ = 0, weak_ = 0, minChunks_ = 1000, minShown_ = 1000;
  unsigned dropped_ = 0;
  int result_[2] = {0, 0};
  bool held_[2] = {false, false};
  bool finished_ = false;
  int rate10_ = 0;
  int top_ = kTop;
  int firstLevel_ = -1;                  // the 30 pass's first level judged, and its game pictures a second
  double firstAvg_ = 0.0, lastAvg_ = 0.0;
  bool cpuSign_ = false;
};

}  // namespace re4gx
