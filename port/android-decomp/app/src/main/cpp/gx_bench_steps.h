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
// Retroid, which plays at 2x fairly steadily, was given 1x.
// Since 0.99.14 a level also needs a reserve where the GPU's time is measured (Vulkan's timestamps, GL's timer
// queries): on average at most kGpuReserve of a picture's time (16.7 ms with 60, 33.4 with 30). The project owner's
// Retroid (report 09914-benchmark-dynres, 60 FPS, HD textures) held 3x at the dock with 0-9 in-between pictures left
// out in 10 s and the GPU at 14.4-16.3 ms of 16.7 (86-98 %); in heavier rooms (r333) 3x did not hold, and with FXAA
// and 16x filtering, the profile's extras, less so. At 2.5x the GPU took 12-12.8 ms (72-77 %): the result now.
// "Dynamic" goes up to the result and no further (gx_auto_level.h), so the result is a level that leaves room.
// Fed once a second with that second's figures (Second), on a clock of its own: the
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
  static constexpr double kGpuReserve = 0.85;     // the GPU's time a picture at most, share of a picture's time

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
  // 0.99.12 (a user's report: a Helio G85 phone rated Medium, small frame drops in the village): the core thread's
  // processor time a game picture at the 30 pass's result, in percent of a picture's 33.37 ms (-1: not known, a core
  // without Re4GxChunk::coreCpuUs). The level search sees the graphics chip; this shows how much of the frame the
  // game's own thread needs (the Retroid at the dock about 27 %, that phone 76 %). PerfProfile decides by it.
  int CpuLoad() const { return cpuLoad_; }
  static constexpr double kFrameMs = 1000.0 / (60000.0 / 1001.0) * 2.0;  // a game picture at 30 a second (NTSC)
  // 0.99.14: the GPU's time a picture shown at the last judged level of a pass (ms; negative: not measured)
  double GpuMs(int pass) const { return gpuMs_[pass]; }

  // One second's figures at `t`: game pictures, pictures shown, in-between pictures left out, the core thread's
  // processor time a game picture in ms (`cpuMs`, negative: not known), the GPU's time a picture shown in ms (`gpuMs`,
  // negative: not measured). kLevel: set Level() now; kPassDone: the 60 pass starts (set Level() and 60 FPS); kDone:
  // both results are there.
  Step Second(double t, int chunks, int shown, unsigned dropped, std::string *log, double cpuMs = -1.0,
              double gpuMs = -1.0) {
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
      if (cpuMs >= 0.0) {
        cpuSum_ += cpuMs;
        cpuSamples_++;
      }
      if (gpuMs >= 0.0) {
        gpuSum_ += gpuMs;
        gpuSamples_++;
      }
    }
    const bool overTime = t - passAt_ >= kPassLimit;
    if (t - levelAt_ < kHold && !overTime) return kNone;
    const double avgChunks = samples_ ? static_cast<double>(sumChunks_) / samples_ : 0.0;
    const double avgShown = samples_ ? static_cast<double>(sumShown_) / samples_ : 0.0;
    const bool steady = samples_ >= kMinSamples && avgChunks >= kAvgChunks && weak_ <= kWeakSeconds &&
                        (!sixty || (avgShown >= kAvgShown && dropped_ * 100 <= static_cast<unsigned>(sumChunks_) * kDroppedPercent));
    // the GPU's reserve (0.99.14): a picture's time at the pass's rate
    const double picture = sixty ? kFrameMs / 2.0 : kFrameMs;
    const double gpu = gpuSamples_ >= kMinSamples ? gpuSum_ / gpuSamples_ : -1.0;
    const bool reserve = gpu < 0.0 || gpu <= kGpuReserve * picture;
    const bool holds = steady && reserve;
    gpuMs_[pass_] = gpu;
    const double cpu = cpuSamples_ ? cpuSum_ / cpuSamples_ : -1.0;
    char cpuText[96] = "";
    if (cpu >= 0.0)
      std::snprintf(cpuText, sizeof(cpuText), "; the game's thread %.1f ms of processor time a picture (%.0f %%)", cpu,
                    cpu * 100.0 / kFrameMs);
    char gpuText[96] = "; the GPU's time not measured";
    if (gpu >= 0.0)
      std::snprintf(gpuText, sizeof(gpuText), "; the GPU %.1f ms a picture (%.0f %% of %.1f ms)", gpu,
                    gpu * 100.0 / picture, picture);
    char verdict[96];
    if (holds)
      std::snprintf(verdict, sizeof(verdict), "stable");
    else if (overTime)
      std::snprintf(verdict, sizeof(verdict), "not stable, the pass's time is up");
    else if (steady)
      std::snprintf(verdict, sizeof(verdict), "smooth, but without a reserve (the GPU over %.0f %%): not stable",
                    kGpuReserve * 100.0);
    else
      std::snprintf(verdict, sizeof(verdict), "not stable");
    char line[560];
    std::snprintf(line, sizeof(line),
                  "benchmark: %d FPS pass, level %s for %.1f s: game %.1f pictures a second on average (at least %d), "
                  "shown %.1f (at least %d), %d weak seconds, %u in-between left out: %s%s%s",
                  sixty ? 60 : 30, Name(level_).c_str(), t - levelAt_, avgChunks, samples_ ? minChunks_ : 0, avgShown,
                  samples_ ? minShown_ : 0, weak_, dropped_, verdict, gpuText, cpuText);
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
      cpuLoad_ = cpu >= 0.0 ? static_cast<int>(cpu * 100.0 / kFrameMs + 0.5) : -1;
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
    cpuSum_ = 0.0;
    cpuSamples_ = 0;
    gpuSum_ = 0.0;
    gpuSamples_ = 0;
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
  double cpuSum_ = 0.0;                  // the level's processor time a picture (ms), summed over its counted seconds
  int cpuSamples_ = 0;
  int cpuLoad_ = -1;
  double gpuSum_ = 0.0;                  // the level's GPU time a picture (ms), summed over its counted seconds
  int gpuSamples_ = 0;
  double gpuMs_[2] = {-1.0, -1.0};
};

}  // namespace re4gx
