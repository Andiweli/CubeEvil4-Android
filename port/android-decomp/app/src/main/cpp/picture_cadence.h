// The pictures' cadence on the screen (0.99.9, user suggestion "frame pacing", 11.11.2026): how evenly new pictures
// reach the screen. Each new picture (a game picture or an in-between picture of 60 FPS) is noted at the vsync it is
// drawn for; every 10 s one line for the log: the median interval, the share of intervals more than 2 ms off it,
// the longest one, and the intervals counted in vsyncs (1, 2, 3, 4, 5 or more). Even at 30 pictures a second on a
// 60 Hz screen: all of them 2 vsyncs, 0 % off. Uneven (a stutter a player feels while the average looks fine):
// a share off the median, a longest interval far above it, more than one count of vsyncs. Gaps of a second or
// more (a pause, a room's loading) start over. Independent of Android so the host tests can check it
// (tools/test_gx_gl.cpp TestPictureCadence).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace re4gx {

struct PictureCadence {
  static constexpr long long kWindowNs = 10000000000LL;  // a line every 10 s
  static constexpr long long kGapNs = 1000000000LL;      // a gap this long starts over
  long long last = 0, windowStart = 0;
  std::vector<double> intervals;  // ms
  int vsyncs[6] = {0, 0, 0, 0, 0, 0};  // [1..4], [5]: 5 or more; [0]: no vsync period known

  // The time of the vsync `now` belongs to (`at`: the latest vsync, `period`: the display's; 0: not known): the
  // picture drawn at the render thread's own time would carry its scheduling into the intervals.
  static long long Snap(long long now, long long at, long long period) {
    if (at <= 0 || period <= 0)
      return now;
    const long long k = static_cast<long long>(std::llround(static_cast<double>(now - at) / static_cast<double>(period)));
    return at + k * period;
  }

  // A new picture at `ns` (Snap), the display's vsync period `period` (0: not known). True with `line` set once a
  // window of 10 s is full.
  bool Note(long long ns, long long period, double hz, std::string *line) {
    if (last == 0 || ns - last >= kGapNs || ns < last) {
      last = ns;
      Reset(ns);
      return false;
    }
    const long long d = ns - last;
    last = ns;
    if (d <= 0)
      return false;  // two pictures for the same vsync: the screen shows the second
    intervals.push_back(static_cast<double>(d) / 1e6);
    if (period > 0) {
      const long long n = std::max(1LL, (d + period / 2) / period);
      vsyncs[n >= 5 ? 5 : n]++;
    } else {
      vsyncs[0]++;
    }
    if (ns - windowStart < kWindowNs || intervals.size() < 2)
      return false;
    std::vector<double> sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2], longest = sorted.back();
    std::size_t off = 0;
    for (double v : intervals)
      off += std::fabs(v - median) > 2.0 ? 1 : 0;
    char text[320];
    std::snprintf(text, sizeof(text),
                  "picture pacing: %zu new pictures in %.0f s, interval median %.1f ms, %.0f %% more than 2 ms off it, "
                  "longest %.1f ms; in vsyncs of the display (%.1f Hz) 1: %d, 2: %d, 3: %d, 4: %d, 5+: %d",
                  intervals.size() + 1, static_cast<double>(ns - windowStart) / 1e9, median,
                  100.0 * static_cast<double>(off) / static_cast<double>(intervals.size()), longest, hz, vsyncs[1],
                  vsyncs[2], vsyncs[3], vsyncs[4], vsyncs[5]);
    *line = text;
    Reset(ns);
    return true;
  }

  void Reset(long long ns) {
    windowStart = ns;
    intervals.clear();
    std::fill(std::begin(vsyncs), std::end(vsyncs), 0);
  }
};

// A steady rhythm at 30 game pictures a second on a screen near 60 Hz (0.99.12; a user's Helio G85 phone: the game's
// thread used 76 % of a picture's time, its pictures came done a little earlier or later around a vsync, and the
// screen showed them 1 and 3 vsyncs in turn, 46 % at 2; felt as small frame drops). A new game picture ready one
// vsync after the last one waits for the next vsync: after a late picture the rhythm keeps two vsyncs a picture, a
// sixtieth of a second later, instead of making up at once. Shown(): the vsync each new picture was drawn for (Snap);
// Wait(): should the picture ready at the vsync `vsync` wait one? Only on a screen of 54..66 Hz and when this vsync is
// exactly the one after the last new picture (a render thread that is late itself never waits). The caller holds it
// only once (core_mode.cpp drawGx: skipNext) and not at 60 game pictures a second or with 60 FPS. Independent of
// Android (tools/test_gx_gl.cpp TestPictureHold).
struct PictureHold {
  long long last = 0;  // the vsync of the last new picture
  unsigned held = 0;   // pictures held back one vsync so far (the log)

  void Shown(long long vsync) { last = vsync; }

  bool Wait(long long vsync, long long period) {
    if (period < 15150000LL || period > 18520000LL || last <= 0 || vsync <= last)
      return false;
    const long long n = (vsync - last + period / 2) / period;
    if (n != 1)
      return false;
    held++;
    return true;
  }
};

// A steady rhythm of 60 pictures a second on a screen that stays faster than 60 Hz (0.99.13; a user's nubia NX709S
// at 120 Hz, 60 FPS on: the app draws at every second vsync, but each picture reached the screen at the first vsync
// after the GPU was done with it, 2 or 3 vsyncs after the drawing began; in turn that gave 1 and 3 vsyncs a picture,
// "1: 200, 2: 180, 3: 200", felt as unevenness although 60 came a second). The app asks Android to show each picture
// at a vsync of its own (eglPresentationTimeANDROID): `latency` vsyncs after the vsync its drawing began at, the
// drawings kept on their grid of `every` vsyncs (FramePacing: 2 at 120 Hz). A picture done in time then waits for its
// vsync instead of coming one early, so every picture shows for `every` vsyncs. The latency starts at every + 1 (3 at
// 120 Hz: 8 ms more than a picture done within 2 vsyncs needed) and follows what the screen did (Outcome: when the GPU
// was done and when the picture was shown, EGL_ANDROID_get_frame_timestamps): one more while more than 1 in 20
// pictures came late, one less after 5 s without a late one and with a vsync to spare, between every and every + 2.
// It steps aside for 30 s (no times asked for, the screen as before) when it cannot help: more than 1 in 4 pictures
// late even at every + 2, or the render thread behind the game 3 times within 2 s (Behind): it never holds a picture
// back while the device does not keep up. Only for screens whose `every` vsyncs are a 60th of a second (120, 180,
// 240 Hz; 60 pictures on 90 Hz cannot be even: 1 and 2 vsyncs by turns); never else (Target 0: no time asked for).
// Independent of Android (tools/test_gx_gl.cpp TestPresentPacer).
struct PresentPacer {
  static constexpr int kWindow = 60;          // pictures judged at a time
  static constexpr long long kMarginNs = 2000000LL;  // the GPU done this long before its vsync at least (the latch)
  static constexpr long long kAsideNs = 30000000000LL;  // stepped aside this long
  int latency = 0;   // vsyncs from the drawing's vsync to the one asked for (0: not active)
  int every = 0;     // the drawings' grid in vsyncs
  long long period = 0;
  long long lastBase = 0, lastTarget = 0;
  int frames = 0, late = 0, quietWindows = 0;
  long long minSpare = -1;  // the least time between the GPU done and its vsync in the window (-1: none known)
  long long asideUntil = 0, behindFrom = 0;
  int behindCount = 0;
  unsigned asked = 0, ups = 0, downs = 0, lateAll = 0, judged = 0, asides = 0;

  bool Aside() const { return asideUntil > 0; }

  // No times asked for during the next 30 s from `vsync` on.
  void StepAside(long long vsync) {
    asideUntil = vsync + kAsideNs;
    asides++;
    lastBase = lastTarget = 0;
    frames = late = quietWindows = behindCount = 0;
    behindFrom = 0;
    minSpare = -1;
  }

  // The render thread was behind the game at the drawing of `vsync` (core_mode.cpp: the next chunk was there already).
  void Behind(long long vsync) {
    if (!latency || Aside() || vsync <= 0)
      return;
    if (behindFrom == 0 || vsync - behindFrom > 2000000000LL) {
      behindFrom = vsync;
      behindCount = 0;
    }
    if (++behindCount >= 3)
      StepAside(vsync);
  }

  // `every` vsyncs of `period` ns a 60th of a second (59.94 .. 60.5 pictures a second)?
  static bool Fits(long long period, int every) {
    if (period <= 0 || every < 2)
      return false;
    const long long frame = period * every;
    return frame >= 16529000LL && frame <= 16695000LL;
  }

  void Reset() {
    latency = every = 0;
    period = lastBase = lastTarget = 0;
    frames = late = quietWindows = behindCount = 0;
    minSpare = -1;
    asideUntil = behindFrom = 0;
  }

  // The time to ask for (ns, the clock of the vsyncs) for the picture drawn now: `vsync` the latest vsync when its
  // drawing began; 0: none (the screen does not fit, or a gap: the grid starts anew at the next picture).
  long long Target(long long vsync, long long periodNs, int everyNow) {
    if (!Fits(periodNs, everyNow) || vsync <= 0) {
      if (latency)
        Reset();
      return 0;
    }
    if (everyNow != every || std::llabs(periodNs - period) > period / 50) {
      // another screen rate: anew
      const int keep = everyNow == every ? latency : 0;
      Reset();
      every = everyNow;
      latency = keep ? keep : everyNow + 1;
    }
    period = periodNs;
    if (Aside()) {
      if (vsync < asideUntil)
        return 0;
      asideUntil = 0;  // again, from the start
      latency = every + 1;
    }
    const long long step = period * every;
    long long base = vsync;
    if (lastBase > 0 && vsync > lastBase - period / 2 && vsync - lastBase < 4 * step) {
      // on the grid of the drawings (a drawing that began a vsync late belongs to its own vsync of the grid)
      const long long k = std::max(1LL, (vsync - lastBase + period / 2) / step);
      base = lastBase + k * step;
    }
    long long target = base + latency * period;
    if (lastTarget > 0 && target < lastTarget + period)
      target = lastTarget + period;
    lastBase = base;
    lastTarget = target;
    asked++;
    // asked for half a vsync before: the screen takes it at the first vsync from then on, the target
    return target - period / 2;
  }

  // A picture's result: the vsync it was asked for (`target`: Target's value + period / 2), when the GPU was done
  // (`done`, 0: not known) and when the screen showed it (`shown`, 0: not known).
  void Outcome(long long target, long long done, long long shown) {
    if (!latency || Aside() || target <= 0 || (done <= 0 && shown <= 0))
      return;
    judged++;
    const bool wasLate = shown > 0 ? shown > target + period / 2 : done > target - kMarginNs;
    if (wasLate) {
      late++;
      lateAll++;
    }
    if (done > 0) {
      const long long spare = target - kMarginNs - done;
      minSpare = minSpare < 0 ? std::max(0LL, spare) : std::min(minSpare, std::max(0LL, spare));
    }
    if (++frames < kWindow)
      return;
    if (late * 4 > frames && latency >= every + 2) {
      StepAside(target);  // even the most it waits does not help: the screen as before
      return;
    }
    if (late * 20 > frames) {
      if (latency < every + 2) {
        latency++;
        ups++;
      }
      quietWindows = 0;
    } else if (late == 0 && minSpare > period) {
      if (++quietWindows >= 5) {
        if (latency > every) {
          latency--;
          downs++;
        }
        quietWindows = 0;
      }
    } else {
      quietWindows = 0;
    }
    frames = late = 0;
    minSpare = -1;
  }
};

} // namespace re4gx
