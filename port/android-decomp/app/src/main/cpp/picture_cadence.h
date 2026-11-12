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

} // namespace re4gx
