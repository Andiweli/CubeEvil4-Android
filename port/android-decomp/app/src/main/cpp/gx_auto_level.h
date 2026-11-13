// "Dynamic" in the app menu's resolution (wish of the project owner, 23.10.2026; until then "Auto"):
// the EFB level follows the GPU. Its top is the benchmark's level for the rate played (30 or 60 pictures a second,
// core_mode.cpp; without a benchmark the screen's level, one below the level whose 336 rows come closest to the
// screen's rows, CoreMenu.autoScale); it starts there and never goes above it. Until 0.99.13 the top was half a level
// above the benchmark's: the project owner's Retroid (0.99.13, 60 FPS, HD textures, Vulkan; report
// 09914-benchmark-dynres) measured 3x and went up to 3.5x again and again, each time back down within seconds with a
// fifth to a half of the in-between pictures missing (3.5 -> 3 -> 3.5 -> 3 -> 2.5 -> 3 -> 3.5 ...). Decision of the
// project owner, 13.11.2026: the top is the measured level, nothing above it.
//
// The level is judged in windows of at least 2 s and 20 game pictures (AutoLevel::Sample, after each real chunk):
// - Not judged (the counts of slow and steady windows start anew): a window with a tenth or more of its pictures in a
//   game menu (the sub screens, the option screen: 30 a second without in-between pictures by design) or at the game's
//   own 60 chunks a second (movies, the title): they say nothing about the GPU at play (0.99.13: the ending movie took
//   the Retroid from 2x to 3.5x in 30 s, "every in-between picture shown"); a window with kLoadMs or more of loading on
//   the render thread (HD textures read and uploaded, shaders and pipelines made: GxStats::loadUs): pictures late for
//   that are not the resolution's doing. A room's start, the app menu and a stall over 250 ms are breaks (Restart): the
//   next kQuiet seconds are not counted at all.
// - Slow: fewer than 28.5 game pictures a second, more than a tenth of the in-between pictures left out (60 FPS), or
//   the 60 FPS pacer paused its in-between pictures (SmoothPacer: the renderer behind with a quarter of the chunks;
//   a pause that started in the window or covers a quarter of it: the 5 s of a pause hid the slowness, and slow and
//   paused windows took turns, so a level too heavy stayed 8-10 s) - and the GPU at its limit: where its time is
//   measured (GxStats::gpuMicros: Vulkan's timestamps, GL's
//   timer queries; not on Mali GL) at kGpuSlow of a picture's time or more (16.7 ms at 60 FPS, 33.4 at 30), else by
//   the fences (on average half a chunk unfinished) or the render thread waiting for the GPU inside the driver (its
//   Run much longer than its processor time, kWaitDown). Pictures late with the GPU below that are the processor's or
//   the render thread's doing: the level stays and a line says so. Two slow windows in a row: half a level down.
//   (Until 0.99.13 with 60 FPS the fences alone decided, and they stand at 0.7-1.0 behind with 60 FPS whatever the
//   GPU does: every late picture counted against the resolution.)
// - Steady: 29.5 game pictures a second or more, the render thread hardly waiting, with 60 FPS (almost) every
//   in-between picture shown and no pause of them, without 60 FPS the fences keeping up; and, where the GPU's time is
//   measured, room for the next level: this level's time a picture times the next level's pixels over this one's at
//   most kGpuUp of a picture's time. The Retroid's report: at the dock 2.5x took 12.4 ms of 16.7, 3x 15.3 (92 %), 3.5x
//   21; 3x in r333 15.7-17.0. Until 0.99.13 "every in-between picture shown" was enough, which a level just at its
//   limit also shows: the next one was tried and failed (the GPU 1.0 behind and 86-98 % busy at 3x, 3.5x 36 % more
//   pixels).
// - Up: kGoodWindows steady windows in a row (16 s), at the earliest kFirstHold seconds after the last step down;
//   down again within kBounce seconds of a step up doubles that wait (at most 5 minutes), a step up that holds kSettle
//   seconds brings it back (until 0.99.13: 5 windows, 10 s, 15 s and 60 s). A level that did not hold in a room is
//   not tried again in that room for kRoomForget seconds (0.99.14, after the project owner's report: steady, no up
//   and down in the same place), also over a new renderer with the same top; in other rooms it is tried after the
//   wait. The wait stays short (the project owner, 06.11.2026: 30 s was too long when the way goes on into a lighter
//   area): the memory keeps a heavy room from bouncing, not a long wait everywhere.
// Below the top a status line every kStatusEvery seconds says why it stays (Chunk's `status`), each step says why
// it came, and Summary() (core_mode.cpp at each room's start) tells the level, the steps and the rooms remembered.
// core_mode.cpp applies a new level between two Runs (GxRenderer::RescaleHalves). It counts the levels in
// halves (Start's `halves`: 4 is 2x, 3 is 1.5x; the app menu's in-between levels, wish of the project
// owner, 27.10.2026: from 2x straight to 1x the picture lost too much), down to `bottom` (2: 1x).
#pragma once

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace re4gx {

class AutoLevel {
public:
  // What core_mode.cpp hands over after each real chunk: counters since the renderer started (a window takes their
  // differences; a counter that went back, a new renderer's, starts the window anew) and what this chunk is.
  struct Sample {
    double t = 0.0;                    // seconds
    unsigned long long behindSum = 0;  // the GPU's fences so far (GxStats::behindSum / behindChecks)
    unsigned behindChecks = 0;
    unsigned long long dropped = 0;    // in-between pictures left out so far (60 FPS; 0 without)
    double waitMs = 0.0;               // ms the render thread waited for the GPU in its Runs so far
    unsigned long long gpuUs = 0;      // the GPU's time of the pictures it measured (GxStats::gpuMicros)
    unsigned gpuPictures = 0;          // and their number (GxStats::gpuTimed; 0: not measured on this device)
    unsigned long long loadUs = 0;     // the render thread's loading so far (GxStats::loadUs)
    unsigned pauses = 0;               // the 60 FPS pacer's pauses so far (SmoothPacer::Offs)
    bool sixty = false;                // 60 FPS is on and not given up for this renderer (a picture's time 16.7 ms
                                       // instead of 33.4)
    bool smooth = false;               // 60 FPS makes in-between pictures now (not paused, not given up)
    bool calm = false;                 // this picture says nothing about the GPU: a game menu, a movie, the title
    int room = -1;                     // the game's room (the memory of levels that did not hold), -1 not known
  };

  static constexpr double kWindow = 2.0;      // seconds
  static constexpr unsigned kWindowChunks = 20;
  static constexpr double kSlow = 28.5;       // game pictures a second
  static constexpr double kFast = 29.5;
  static constexpr double kBehindDown = 0.5;  // chunks the GPU has not finished, on average
  static constexpr double kBehindUp = 0.1;
  static constexpr int kGoodWindows = 8;      // steady windows in a row before a step up (16 s; 5 until 0.99.13)
  static constexpr int kBadWindows = 2;
  static constexpr double kFirstHold = 20.0;  // seconds after a step down before a step up (10 from 0.98.5 to 0.99.13)
  static constexpr double kMostHold = 300.0;
  static constexpr double kBounce = 60.0;     // down again this soon after up: the wait doubles (15 until 0.99.13)
  static constexpr double kSettle = 120.0;    // a step up that holds this long: the wait back to kFirstHold (60)
  static constexpr double kDroppedDown = 0.10; // share of the chunks without their in-between picture
  static constexpr double kDroppedUp = 0.05;
  static constexpr double kWaitDown = 10.0;    // ms a chunk the render thread waits for the GPU
  static constexpr double kWaitUp = 5.0;
  static constexpr double kDroppedUpSmooth = 0.02;  // 60 FPS: a step up with at most this share left out
  static constexpr double kStatusEvery = 10.0; // seconds between the lines why it stays below the top
  static constexpr double kQuiet = 4.0;        // seconds not counted after a break (Restart)
  static constexpr double kGap = 1.0;          // seconds without a chunk that count as a break
  static constexpr double kCalmShare = 0.10;   // a window with this share of menu or movie pictures is not judged
  static constexpr double kPausedShare = 0.25; // 60 FPS: a window with this share paused is slow (the GPU at its limit)
  static constexpr double kLoadMs = 30.0;      // a window with this much loading on the render thread is not judged
  static constexpr double kGpuSlow = 0.85;     // the GPU at its limit: this share of a picture's time or more
  static constexpr double kGpuUp = 0.90;       // a step up: the next level's estimate at most this share
  static constexpr double kRoomForget = 600.0; // a level that did not hold in a room: not tried there for 10 minutes
  static constexpr unsigned kGpuMin = 10;      // pictures a window needs measured for the GPU's time to count
  static constexpr double kPicture60 = 1000.0 / (60000.0 / 1001.0);  // ms a picture with 60 FPS (16.68)
  static constexpr double kPicture30 = 2.0 * kPicture60;              // without (33.37)

  // A renderer started at `top` (its highest level: the benchmark's or the screen's) at `t` seconds; never below
  // `bottom`; `halves`: the levels are halves (only how the log line writes them). `proven`: the highest level the
  // app's benchmark measured as holding on this device (0: none); up to it a step up without 60 FPS only needs the
  // fences not clearly behind. `from`: the level to start at below the top (0: the top).
  // The memory of levels that did not hold in a room stays when the top is the same as the renderer's before (new
  // picture options, the app back from the background); another top starts it anew.
  void Start(int top, double t, int bottom = 1, bool halves = false, int proven = 0, int from = 0) {
    const bool keep = started_ && std::max(std::max(1, bottom), top) == top_ && halves == halves_;
    const std::array<Fail, kFails> fails = fails_;
    const int room = room_;  // (the game's, not the renderer's)
    *this = AutoLevel();
    room_ = room;
    bottom_ = std::max(1, bottom);
    top_ = level_ = std::max(bottom_, top);
    if (from > 0)
      level_ = std::max(bottom_, std::min(top_, from));
    halves_ = halves;
    proven_ = proven;
    since_ = t;
    started_ = true;
    if (keep)
      fails_ = fails;
  }

  // After each real chunk: the level to use from the next chunk on. `mayChange`: no app menu, no R3 series, no
  // benchmark measuring. On a change `why` says why (a log line); below the top, every kStatusEvery seconds, `status`
  // says why it stays, and late pictures that are not the GPU's doing are told there too (also at the top).
  int Chunk(const Sample &s, bool mayChange, std::string *why, std::string *status = nullptr) {
    if (!started_)
      return level_;
    const double t = s.t;
    // a gap without chunks (the app menu, a room loading, a long stall): a break as Restart
    if (lastChunk_ > 0.0 && t - lastChunk_ > kGap)
      Restart(t);
    lastChunk_ = t;
    room_ = s.room;
    if (t < quietUntil_) {
      windowOpen_ = false;
      chunks_ = 0;
      return level_;
    }
    // a counter that went back: a new renderer's statistics; the window starts anew from here
    if (windowOpen_ && (s.behindChecks < at_.behindChecks || s.gpuPictures < at_.gpuPictures || s.loadUs < at_.loadUs ||
                        s.dropped < at_.dropped || s.pauses < at_.pauses || s.waitMs < at_.waitMs))
      windowOpen_ = false;
    if (!windowOpen_) {
      windowOpen_ = true;
      since_ = t;
      at_ = s;
      chunks_ = calmChunks_ = pausedChunks_ = 0;
    }
    chunks_++;
    calmChunks_ += s.calm ? 1u : 0u;
    pausedChunks_ += s.sixty && !s.smooth ? 1u : 0u;
    const double seconds = t - since_;
    if (seconds < kWindow || chunks_ < kWindowChunks)
      return level_;
    // the window's figures
    Figures f;
    f.rate = chunks_ / seconds;
    const unsigned checks = s.behindChecks - at_.behindChecks;
    f.behind = checks ? static_cast<double>(s.behindSum - at_.behindSum) / checks : 0.0;
    f.dropped = static_cast<double>(s.dropped - at_.dropped) / chunks_;
    f.wait = (s.waitMs - at_.waitMs) / chunks_;
    f.budget = s.sixty ? kPicture60 : kPicture30;
    const unsigned gpuPictures = s.gpuPictures - at_.gpuPictures;
    if (gpuPictures >= kGpuMin) {
      f.gpu = static_cast<double>(s.gpuUs - at_.gpuUs) / 1000.0 / gpuPictures;
      // a GPU busier than the clock allows is a timer that cannot be trusted (GL's on the Retroid with MSAA read
      // 450 ms at 30 pictures a second, 19.10.2026): not measured
      if (f.gpu * gpuPictures > seconds * 1000.0 * 1.25)
        f.gpu = -1.0;
    }
    f.load = static_cast<double>(s.loadUs - at_.loadUs) / 1000.0;
    f.pauses = s.pauses - at_.pauses;
    f.sixty = s.sixty;
    const double calm = static_cast<double>(calmChunks_) / chunks_;
    const bool paused = pausedChunks_ > 0;
    const double pausedShare = static_cast<double>(pausedChunks_) / chunks_;
    f.paused = pausedShare;
    since_ = t;
    at_ = s;
    chunks_ = calmChunks_ = pausedChunks_ = 0;
    // a step up that held kSettle seconds: that level suits again, the next wait is the first one
    if (upAt_ >= 0.0 && t - upAt_ >= kSettle) {
      upAt_ = -1.0;
      hold_ = kFirstHold;
    }
    char line[900];
    // the line why it stays below the top (at most every kStatusEvery seconds); `anyLevel`: at the top too
    auto tell = [&](const char *reason, bool anyLevel = false) {
      if (!status || (level_ >= top_ && !anyLevel) || t - statusAt_ < kStatusEvery)
        return;
      statusAt_ = t;
      if (level_ < top_)
        std::snprintf(line, sizeof(line), "dynamic resolution: stays at %s below %s: %s (%s)", Name(level_).c_str(),
                      Name(top_).c_str(), reason, Describe(f, smoothNow(s)).c_str());
      else
        std::snprintf(line, sizeof(line), "dynamic resolution: at %s (the top): %s (%s)", Name(level_).c_str(), reason,
                      Describe(f, smoothNow(s)).c_str());
      *status = line;
    };
    if (!mayChange) {
      good_ = bad_ = 0;
      tell("no change now (the app menu, an R3 series, the benchmark)");
      return level_;
    }
    char reason[200];
    if (calm >= kCalmShare) {
      good_ = bad_ = 0;
      std::snprintf(reason, sizeof(reason), "not judged: %.0f %% of the window a game menu or a movie", calm * 100.0);
      tell(reason);
      return level_;
    }
    if (f.load >= kLoadMs) {
      good_ = bad_ = 0;
      std::snprintf(reason, sizeof(reason), "not judged: %.0f ms of loading on the render thread (HD textures, shaders)",
                    f.load);
      tell(reason);
      return level_;
    }
    const bool late = f.rate < kSlow || f.dropped > kDroppedDown || f.pauses > 0 || pausedShare >= kPausedShare;
    const bool gpuLimit = f.gpu >= 0.0 ? f.gpu >= kGpuSlow * f.budget : f.behind >= kBehindDown || f.wait >= kWaitDown;
    if (late && !gpuLimit) {
      // late pictures with the GPU not at its limit: the processor or the render thread, not the resolution
      good_ = bad_ = 0;
      tell("pictures late, but the GPU not at its limit: not the resolution's doing", true);
      return level_;
    }
    if (late && level_ > bottom_) {
      good_ = 0;
      if (++bad_ < kBadWindows)
        return level_;
      bad_ = 0;
      // straight down again after a step up: that level was too much, wait longer next time
      if (upAt_ >= 0.0 && t - upAt_ < kBounce)
        hold_ = std::min(kMostHold, hold_ * 2.0);
      upAt_ = -1.0;
      holdUntil_ = t + hold_;
      Remember(level_, t);
      char where[64] = "";
      if (room_ >= 0)
        std::snprintf(where, sizeof(where), "; %s not tried again in r%03x for %.0f min", Name(level_).c_str(),
                      static_cast<unsigned>(room_), kRoomForget / 60.0);
      std::snprintf(line, sizeof(line), "dynamic resolution: level %s -> %s (two windows of 2 s too slow: %s)%s; the next "
                    "step up in %.0f s at the earliest",
                    Name(level_).c_str(), Name(level_ - 1).c_str(), Describe(f, smoothNow(s)).c_str(), where, hold_);
      level_--;
      downs_++;
      if (why)
        *why = line;
      return level_;
    }
    if (late) {
      good_ = bad_ = 0;
      return level_;  // at the bottom: nothing below
    }
    bad_ = 0;
    // up to the benchmark's level (`proven_`, measured at one of the heaviest places): without 60 FPS the fences only
    // not clearly behind (under kBehindDown); above it, keeping up (under kBehindUp). With 60 FPS (almost) every
    // in-between picture shown and no pause instead (the fences of in-between pictures stand at 0.7-1.0 there)
    const double behindUp = level_ < proven_ ? kBehindDown : kBehindUp;
    const bool smooth = smoothNow(s);
    const char *no = nullptr;
    if (f.rate < kFast)
      no = "the game slower than 29.5 pictures a second";
    else if (f.wait >= kWaitUp)
      no = "the render thread waits for the GPU (5 ms a chunk or more)";
    else if (s.sixty && (paused || !smooth))
      no = "60 FPS paused its in-between pictures (the renderer behind)";
    else if (smooth && f.dropped > kDroppedUpSmooth)
      no = "in-between pictures left out (more than 2 %)";
    else if (!smooth && f.dropped >= kDroppedUp)
      no = "in-between pictures left out (5 % or more)";
    else if (!smooth && f.behind >= behindUp) {
      std::snprintf(reason, sizeof(reason), "the GPU behind (%.1f or more without 60 FPS)", behindUp);
      no = reason;
    } else if (f.gpu >= 0.0 && level_ < top_ && f.gpu * Pixels(level_ + 1) / Pixels(level_) > kGpuUp * f.budget) {
      std::snprintf(reason, sizeof(reason),
                    "no room for %s: about %.1f ms a picture there (%.1f here), more than %.0f %% of %.1f ms",
                    Name(level_ + 1).c_str(), f.gpu * Pixels(level_ + 1) / Pixels(level_), f.gpu, kGpuUp * 100.0,
                    f.budget);
      no = reason;
    }
    if (!no && level_ < top_) {
      const double blockedFor = Blocked(level_ + 1, t, room_);
      if (++good_ >= kGoodWindows && t >= holdUntil_ && blockedFor <= 0.0) {
        good_ = 0;
        upAt_ = t;
        std::snprintf(line, sizeof(line), "dynamic resolution: level %s -> %s (%.0f s steady, room for it: %s)",
                      Name(level_).c_str(), Name(level_ + 1).c_str(), kGoodWindows * kWindow,
                      Describe(f, smooth).c_str());
        level_++;
        ups_++;
        statusAt_ = t;
        if (why)
          *why = line;
        return level_;
      }
      if (blockedFor > 0.0)
        std::snprintf(reason, sizeof(reason), "steady, but %s did not hold in r%03x (tried there again in %.0f s)",
                      Name(level_ + 1).c_str(), static_cast<unsigned>(room_), blockedFor);
      else if (t < holdUntil_)
        std::snprintf(reason, sizeof(reason), "steady, the next try in %.0f s (a wait of %.0f s after a step down)",
                      holdUntil_ - t, hold_);
      else
        std::snprintf(reason, sizeof(reason), "steady for %d of %d windows of 2 s", std::min(good_, kGoodWindows),
                      kGoodWindows);
      tell(reason);
      return level_;
    }
    good_ = 0;
    if (no)
      tell(no);
    return level_;
  }

  // The rule as until 0.99.13 took it (the host tests' older cases): fences, waiting and in-between pictures only.
  int Chunk(double t, unsigned long long behindSum, unsigned behindChecks, bool mayChange, std::string *why,
            unsigned long long dropped = 0, double waitMs = 0.0, bool smooth = false, std::string *status = nullptr) {
    Sample s;
    s.t = t;
    s.behindSum = behindSum;
    s.behindChecks = behindChecks;
    s.dropped = dropped;
    s.waitMs = waitMs;
    s.sixty = s.smooth = smooth;
    return Chunk(s, mayChange, why, status);
  }

  // A break at `t`: a room starts, the core paused (the app menu) or the renderer stalled (its programs for a new
  // room). The window open now is dropped, the slow and good windows in a row start anew, and the next kQuiet
  // seconds are not counted. The project owner's log 06.11.2026: the window open when the menu opened spanned the
  // menu, the next one the room's loading: two "slow" windows, 2x -> 1.5x 1.6 s after the room started (and the
  // 30 s hold after it). Nothing else changes: the level, the hold after a real step down, the rooms remembered.
  void Restart(double t) {
    if (!started_)
      return;
    windowOpen_ = false;
    chunks_ = 0;
    good_ = bad_ = 0;
    quietUntil_ = std::max(quietUntil_, t + kQuiet);
  }
  bool Quiet(double t) const { return t < quietUntil_; }

  int Level() const { return level_; }
  int Top() const { return top_; }
  unsigned Downs() const { return downs_; }
  unsigned Ups() const { return ups_; }
  double Hold() const { return hold_; }
  // The level a new renderer starts at (core_mode.cpp, before it makes the renderer): `from` (0: the top), but below
  // a level that did not hold in `room` in the last kRoomForget seconds, as long as the memory stays (the same top as
  // the renderer before; Start). The replay of the project owner's log: a renderer made anew in r333 (the app menu, a
  // benchmark refused) began at the top again and failed there twice more.
  int StartLevel(int top, int from, double t, int room, int bottom = 1) const {
    int level = from > 0 ? std::min(from, top) : top;
    if (!started_ || top != top_)
      return level;
    while (level > std::max(1, bottom) && Blocked(level, t, room) > 0.0)
      level--;
    return level;
  }
  // seconds the level `level` is still not tried in `room` (0: it may be)
  double Blocked(int level, double t, int room) const {
    double left = 0.0;
    for (const Fail &f : fails_)
      if (f.room >= 0 && f.room == room && f.level <= level)
        left = std::max(left, kRoomForget - (t - f.at));
    return left;
  }
  // a line for the log: the level, the steps so far and the rooms remembered
  std::string Summary(double t) const {
    char s[400];
    int n = std::snprintf(s, sizeof(s), "dynamic resolution: %s of %s, %u steps down and %u up so far", Name(level_).c_str(),
                          Name(top_).c_str(), downs_, ups_);
    bool first = true;
    for (const Fail &f : fails_) {
      if (f.room < 0 || t - f.at >= kRoomForget || n >= static_cast<int>(sizeof(s)) - 48)
        continue;
      n += std::snprintf(s + n, sizeof(s) - n, "%s r%03x below %s (%.0f s ago)", first ? "; remembered:" : ",",
                         static_cast<unsigned>(f.room), Name(f.level).c_str(), t - f.at);
      first = false;
    }
    return s;
  }
  // a level as the log writes it: "2" or, in halves, "1.5x"
  std::string Name(int level) const {
    char s[16];
    if (halves_)
      std::snprintf(s, sizeof(s), level % 2 ? "%d.5x" : "%dx", level / 2);
    else
      std::snprintf(s, sizeof(s), "%d", level);
    return s;
  }

private:
  struct Figures {
    double rate = 0.0, behind = 0.0, dropped = 0.0, wait = 0.0, gpu = -1.0, budget = kPicture30, load = 0.0;
    unsigned pauses = 0;
    double paused = 0.0;  // the window's share with 60 FPS paused (or given up for this renderer)
    bool sixty = false;
  };
  // a room where a level did not hold: that level and those above it are not tried there for kRoomForget seconds
  struct Fail {
    int room = -1, level = 0;
    double at = 0.0;
  };
  static constexpr int kFails = 24;

  static bool smoothNow(const Sample &s) { return s.sixty && s.smooth; }
  // the pixels of a level, up to a factor (its scale squared)
  static double Pixels(int level) { return static_cast<double>(level) * level; }
  void Remember(int level, double t) {
    if (room_ < 0)
      return;
    Fail *slot = nullptr;
    for (Fail &f : fails_)
      if (f.room == room_)
        slot = &f;
    if (!slot) {
      slot = &fails_[0];
      for (Fail &f : fails_)
        if (f.room < 0 || f.at < slot->at)
          slot = &f;
      slot->level = level;
    } else if (t - slot->at >= kRoomForget || level < slot->level) {
      slot->level = level;
    }
    slot->room = room_;
    slot->at = t;
  }
  // the window's figures as the log lines write them
  std::string Describe(const Figures &f, bool smooth) const {
    char gpu[96];
    if (f.gpu >= 0.0)
      std::snprintf(gpu, sizeof(gpu), "the GPU %.1f ms a picture (%.0f %% of %.1f)", f.gpu, f.gpu * 100.0 / f.budget,
                    f.budget);
    else
      std::snprintf(gpu, sizeof(gpu), "the GPU's time not measured, its fences %.1f chunks behind", f.behind);
    char sixty[48];
    if (!f.sixty)
      std::snprintf(sixty, sizeof(sixty), "off");
    else if (f.paused > 0.0)
      std::snprintf(sixty, sizeof(sixty), "paused %.0f %% of the window", f.paused * 100.0);
    else
      std::snprintf(sixty, sizeof(sixty), "%s", smooth ? "on" : "paused");
    char s[360];
    std::snprintf(s, sizeof(s),
                  "%.1f game pictures/s, %.0f %% without their in-between picture%s, %s, the render thread waiting %.1f "
                  "ms a chunk, loading %.0f ms, 60 FPS %s",
                  f.rate, f.dropped * 100.0, f.pauses ? " and a pause of them" : "", gpu, f.wait, f.load, sixty);
    return s;
  }

  bool started_ = false, windowOpen_ = false;
  int top_ = 1, level_ = 1, bottom_ = 1, proven_ = 0;
  double statusAt_ = -1e9;  // the last status line
  bool halves_ = false;
  double since_ = 0.0;
  Sample at_;  // the counters at the window's start
  unsigned chunks_ = 0, calmChunks_ = 0, pausedChunks_ = 0;
  int good_ = 0, bad_ = 0;
  double upAt_ = -1.0, holdUntil_ = 0.0, hold_ = kFirstHold;
  unsigned downs_ = 0, ups_ = 0;
  double lastChunk_ = 0.0, quietUntil_ = 0.0;
  int room_ = -1;
  std::array<Fail, kFails> fails_{};
};

// 60 FPS at an even pace (port/docs/60fps-plan.md S4, decision of the project owner, 28.10.2026): an
// in-between picture the renderer leaves out because it is behind makes a stretch of 60 and 30
// pictures a second side by side, which looks like flicker (Xiaomi report 26.10.2026: 21 % left out).
// The dynamic resolution goes down first (AutoLevel counts the left-out ones); when the renderer is
// still behind with at least a quarter of the chunks of the last kSpan seconds (at least kMinChunks of
// them), the in-between pictures stop for kOff seconds: an even 30. Single ones and short bursts (a
// room loading, the core a picture ahead right after a pause) do not count: Retroid 28.10.2026, the
// first rule (3 within 2 s) paused again and again right after each pause and left 30 FPS almost all
// the time. After a pause the first kGrace seconds are not counted. Each change is a log line.
class SmoothPacer {
public:
  static constexpr double kSpan = 4.0;    // seconds
  static constexpr double kOff = 5.0;     // seconds
  static constexpr double kGrace = 2.0;   // seconds after a pause
  static constexpr unsigned kMinChunks = 60;
  static constexpr unsigned kRing = 256;

  // A chunk at `t` (seconds) that could get an in-between picture; `behind`: the renderer is behind
  // with it (its picture is left out anyway). False: no in-between picture now. `why`: a log line
  // when 60 FPS goes off or on again.
  bool Chunk(double t, bool behind, std::string *why) {
    if (offUntil_ >= 0.0) {
      if (t < offUntil_)
        return false;
      offUntil_ = -1.0;
      graceUntil_ = t + kGrace;
      if (why)
        *why = "60 FPS: in-between pictures on again";
    }
    if (t < graceUntil_)
      return !behind;
    times_[at_] = t;
    behind_[at_] = behind;
    at_ = (at_ + 1) % kRing;
    unsigned chunks = 0, drops = 0;
    for (unsigned i = 0; i < kRing; i++) {
      if (t - times_[i] <= kSpan) {
        chunks++;
        drops += behind_[i] ? 1u : 0u;
      }
    }
    if (chunks >= kMinChunks && drops * 4 >= chunks) {
      offUntil_ = t + kOff;
      offs_++;
      for (double &d : times_)
        d = -1e9;
      if (why) {
        char s[200];
        std::snprintf(s, sizeof(s),
                      "60 FPS: the renderer was behind with %u of %u chunks in %.0f s: no in-between pictures "
                      "for %.0f s (an even 30, %u times so far)",
                      drops, chunks, kSpan, kOff, offs_);
        *why = s;
      }
      return false;
    }
    return !behind;
  }
  bool Off() const { return offUntil_ >= 0.0; }
  unsigned Offs() const { return offs_; }
  // A new start at `t` (the benchmark's next level, gx_bench_steps.h): in-between pictures on at once, with
  // the grace of kGrace seconds; a pause from the level before does not carry over (the project owner's
  // log 06.11.2026: the pause from 2x ran 2.5 s into 1.5x, which then failed with 30 shown in those seconds).
  // Also at a break (a room starts, the app menu closes, a stall: core_mode.cpp, with AutoLevel::Restart) with the
  // grace `grace`: its span of kSpan seconds no longer reaches back over a room's loading.
  void Restart(double t, double grace = kGrace) {
    offUntil_ = -1.0;
    graceUntil_ = t + grace;
    for (double &d : times_)
      d = -1e9;
  }

private:
  double times_[kRing] = {};
  bool behind_[kRing] = {};
  unsigned at_ = 0, offs_ = 0;
  double offUntil_ = -1.0, graceUntil_ = -1.0;

public:
  SmoothPacer() {
    for (double &d : times_)
      d = -1e9;
  }
};

// The game's own pace for 60 FPS (core_mode.cpp): its movies and the title come at the screen's 60
// chunks a second, which leaves no room for in-between pictures (and is not the renderer behind).
// Note() each new chunk once; Sixty(): at least 45 in the last second.
class ChunkRate {
public:
  static constexpr unsigned kRing = 64;
  void Note(double t) {
    times_[at_] = t;
    at_ = (at_ + 1) % kRing;
  }
  bool Sixty(double t) const {
    unsigned n = 0;
    for (double c : times_)
      n += t - c < 1.0 ? 1u : 0u;
    return n >= 45;
  }

private:
  double times_[kRing] = {};
  unsigned at_ = 0;

public:
  ChunkRate() {
    for (double &d : times_)
      d = -1e9;
  }
};

} // namespace re4gx
