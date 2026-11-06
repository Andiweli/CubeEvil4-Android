package com.ast.re4decomp;

import java.util.Arrays;

/**
 * GPU step c (19.10.2026): the game makes at most 60 pictures a second (59.94
 * fields); a screen of 90 Hz or more had each of them drawn anew at every
 * vsync (the tester's POCO X7 Pro: 120 presents a second). CoreActivity asks
 * Android for 60 Hz; where the screen still runs faster, the picture is drawn
 * at every n-th vsync only (`every()`: 120 Hz 2, 144 Hz 2, 240 Hz 4), still
 * at least 60 times a second. The rate is the display's, measured from its
 * vsyncs (Choreographer), not from the pictures drawn: a slow GPU does not
 * change it. Independent of Android so the host tests can check it
 * (tests/FramePacingTests.java).
 *
 * The beat (due(), 0.98.5): the vsyncs are counted from their times on the
 * display's grid, not from the callbacks. Choreographer delivers a callback
 * that comes a vsync late or more once, for the newest vsync; counted as one,
 * it moved the beat and left a picture out each time. The tester's Xiaomi
 * (120 Hz, every second vsync) lost up to 8 pictures a second while the touch
 * controls were shown (report 06.11.2026, a controller connected; the cause
 * most likely the UI thread, where the beat ran until then and where
 * RetroTouch redraws the controls at each vsync while a finger moves, with or
 * without a controller). The beat now runs on a thread of its own
 * (CoreActivity), the count by time covers what is still late there. step() alone counts for a thread that only watches (the UI
 * thread's lateness, for the log).
 */
final class FramePacing {
  /** Vsync intervals per measurement; their median is the display's period (a late callback does not count). */
  static final int WINDOW = 32;

  private final long[] deltas = new long[WINDOW];
  private int count;
  private long last = Long.MIN_VALUE;
  // written by the beat's thread, read by the UI thread (the menu's line, the FPS display)
  private volatile int every = 1;
  private int candidate = 1;
  private volatile double hz;
  // the beat: the vsync of the last step, the vsyncs counted since the start; the counts for the log
  private long stepAt = Long.MIN_VALUE;
  private long beat;
  private int callbacks, late, missed;

  /** The pictures for a display of `hz`: every n-th vsync, n the most that still gives 59.94 a second. */
  static int everyFor(double hz) {
    return Math.max(1, (int) Math.floor(hz / 59.94 + 0.03));
  }

  /**
   * A vsync of the display at `nanos` (Choreographer's frame time). True when
   * every() changed: the same new value from two measurements in a row (a
   * screen that changes its rate settles first).
   */
  synchronized boolean vsync(long nanos) {
    final long previous = last;
    last = nanos;
    if (previous == Long.MIN_VALUE || nanos <= previous) return false;
    deltas[count++] = nanos - previous;
    if (count < WINDOW) return false;
    count = 0;
    final long[] sorted = deltas.clone();
    Arrays.sort(sorted);
    hz = 1e9 / sorted[WINDOW / 2];
    final int want = everyFor(hz);
    if (want == every) {
      candidate = want;
      return false;
    }
    if (want != candidate) {
      candidate = want;
      return false;
    }
    every = want;
    return true;
  }

  /** Draw at every n-th vsync (1: at each). */
  int every() { return every; }

  /** The display's rate from the last measurement, 0 before the first. */
  double hz() { return hz; }

  /**
   * The callback of the vsync at `nanos`: the vsyncs since the one before (at least 1), from
   * their times with the measured rate (1 a callback until the first measurement). A callback
   * for the same vsync again or an older one is 0.
   */
  synchronized int step(long nanos) {
    final long before = stepAt;
    if (before != Long.MIN_VALUE && nanos <= before) return 0;
    stepAt = nanos;
    final double rate = hz;
    int n = 1;
    if (before != Long.MIN_VALUE && rate > 0) {
      final long counted = Math.round((nanos - before) * rate / 1e9);
      n = (int) Math.max(1, Math.min(1000, counted));
    }
    beat += n;
    callbacks++;
    if (n > 1) {
      late++;
      missed += n - 1;
    }
    return n;
  }

  /**
   * After vsync(nanos), with the same time: the picture is due at this vsync (every() > 1),
   * once each every() vsyncs counted by step(). A late callback whose slot has not had its
   * picture yet still requests it, at once.
   */
  synchronized boolean due(long nanos) {
    final long before = beat;
    if (step(nanos) == 0) return false;
    final int e = every;
    return e > 1 && beat / e != before / e;
  }

  /**
   * The counts since the last call, then zero: callbacks, those late by a vsync or more, the
   * vsyncs they missed.
   */
  synchronized int[] takeCounts() {
    final int[] out = {callbacks, late, missed};
    callbacks = late = missed = 0;
    return out;
  }

  /** The app went away (pause): the next vsync starts a new interval. */
  synchronized void restart() {
    last = Long.MIN_VALUE;
    count = 0;
    stepAt = Long.MIN_VALUE;
  }
}
