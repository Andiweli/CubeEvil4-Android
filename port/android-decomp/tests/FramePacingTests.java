package com.ast.re4decomp;

/**
 * GPU step c (FramePacing): the display's rate from its vsyncs, and at which
 * vsyncs the picture is drawn: at each on 60 Hz, at every second on 120 Hz,
 * changed only after two measurements in a row agree, late callbacks ignored.
 */
public final class FramePacingTests {
  static int checks;

  static void check(boolean value) {
    checks++;
    if (!value)
      throw new AssertionError("check " + checks);
  }

  /** `n` vsyncs of a display of `hz` from `at` (ns); returns how often every() changed. */
  static int run(FramePacing p, double hz, int n, long[] at) {
    int changes = 0;
    final double period = 1e9 / hz;
    for (int i = 0; i < n; i++) {
      at[0] += Math.round(period);
      if (p.vsync(at[0])) changes++;
    }
    return changes;
  }

  /** A pacing on 120 Hz with every() 2, the vsyncs and the beat measured (vsync and due at each). */
  static FramePacing at120(long[] at) {
    FramePacing p = new FramePacing();
    for (int i = 0; i < 3 * FramePacing.WINDOW; i++) {
      at[0] += Math.round(1e9 / 120);
      p.vsync(at[0]);
      p.due(at[0]);
    }
    check(p.every() == 2 && Math.abs(p.hz() - 120) < 0.5);
    p.takeCounts();
    return p;
  }

  /**
   * The beat (due, 0.98.5): the vsyncs counted by their times. `missEvery`: of each that many
   * vsyncs the callback of the last does not come (0: all come); `jitterNs`: the times off by up to that much.
   * Returns the pictures requested in `seconds` at 120 Hz and checks that no two fall in one
   * slot of two vsyncs.
   */
  static int beatRun(FramePacing p, long[] at, int seconds, int missEvery, long jitterNs, int[] oldCount) {
    final double period = 1e9 / 120;
    final long start = at[0];
    int pictures = 0, callbacks = 0;
    long lastSlot = -1;
    for (int i = 1; i <= 120 * seconds; i++) {
      if (missEvery > 0 && i % missEvery == missEvery - 1) continue;
      final long jitter = jitterNs == 0 ? 0 : (i * 7919L % (2 * jitterNs + 1)) - jitterNs;
      final long t = start + Math.round(i * period) + jitter;
      p.vsync(t);
      callbacks++;
      if (p.due(t)) {
        pictures++;
        final long slot = i / 2;
        check(slot != lastSlot);
        lastSlot = slot;
      }
    }
    at[0] = start + Math.round(120 * seconds * period);
    // until 0.98.4: every second callback, whatever its time
    if (oldCount != null) oldCount[0] = callbacks / 2;
    return pictures;
  }

  static void beat(long[] at) {
    // on time: every second vsync, 60 a second
    FramePacing p = at120(at);
    check(beatRun(p, at, 10, 0, 0, null) == 600);
    int[] c = p.takeCounts();
    check(c[0] == 1200 && c[1] == 0 && c[2] == 0);

    // the UI thread busy (the tester's Xiaomi with the touch controls, report 06.11.2026): every
    // fifth callback missing, the next one a vsync late; counted by callbacks until 0.98.4 only
    // 48 pictures a second, by the vsyncs' times still 60
    p = at120(at);
    int[] old = {0};
    check(beatRun(p, at, 10, 5, 0, old) == 600);
    check(old[0] == 480);
    c = p.takeCounts();
    check(c[0] == 960 && c[1] == 240 && c[2] == 240);

    // every fourth missing: each slot of two vsyncs still has a callback, 60 a second (with every
    // second one late the median would take the display for 60 Hz, WINDOW)
    p = at120(at);
    check(beatRun(p, at, 10, 4, 0, old) == 600);
    check(old[0] == 450 && p.every() == 2);

    // the vsyncs' times a little off (up to 1 ms of 8.3) count the same
    p = at120(at);
    check(beatRun(p, at, 10, 0, 1_000_000L, null) == 600);
    check(p.takeCounts()[1] == 0);
    p = at120(at);
    check(beatRun(p, at, 10, 5, 1_000_000L, null) == 600);

    // the same vsync twice or an older one: nothing counted, no picture
    p = at120(at);
    at[0] += Math.round(1e9 / 120);
    p.vsync(at[0]);
    p.due(at[0]);
    check(p.step(at[0]) == 0 && !p.due(at[0]) && !p.due(at[0] - 1_000_000L));

    // a stall of 100 ms: one picture at once, then the beat goes on
    long t = at[0] + 100_000_000L;
    p.vsync(t);
    check(p.due(t));
    c = p.takeCounts();
    check(c[1] == 1 && c[2] == 11);

    // after a pause the first callback counts as one vsync
    p.restart();
    at[0] += 3_000_000_000L;
    check(p.step(at[0]) == 1);

    // 60 Hz: drawn at each vsync by GLSurfaceView itself, the beat requests nothing
    p = new FramePacing();
    int requested = 0;
    for (int i = 0; i < 600; i++) {
      at[0] += Math.round(1e9 / 60);
      p.vsync(at[0]);
      if (p.due(at[0])) requested++;
    }
    check(p.every() == 1 && requested == 0);

    // step alone (the UI thread watched): late callbacks and the vsyncs they missed
    p = new FramePacing();
    for (int i = 0; i < 3 * FramePacing.WINDOW; i++) {
      at[0] += Math.round(1e9 / 120);
      p.vsync(at[0]);
    }
    p.takeCounts();
    at[0] += Math.round(1e9 / 120);
    check(p.step(at[0]) == 1);
    at[0] += Math.round(3e9 / 120);
    check(p.step(at[0]) == 3);
    c = p.takeCounts();
    check(c[0] == 2 && c[1] == 1 && c[2] == 2);
    c = p.takeCounts();
    check(c[0] == 0 && c[1] == 0 && c[2] == 0);
  }

  public static void main(String[] args) {
    // the rule: the most vsyncs per picture that still give 59.94 pictures a second
    check(FramePacing.everyFor(59.94) == 1 && FramePacing.everyFor(60) == 1 && FramePacing.everyFor(50) == 1);
    check(FramePacing.everyFor(90) == 1);  // 45 a second would be too few
    check(FramePacing.everyFor(119.88) == 2 && FramePacing.everyFor(120) == 2 && FramePacing.everyFor(144) == 2);
    check(FramePacing.everyFor(165) == 2 && FramePacing.everyFor(180) == 3 && FramePacing.everyFor(240) == 4);
    check(FramePacing.everyFor(0) == 1);
    // 0.99.9: 60 FPS uneven while 30 is even: 90 Hz (and the A24's measured 89.4 Hz); not 60, 60.5, 120, 144, 75
    check(FramePacing.sixtyUneven(90) && FramePacing.sixtyUneven(89.4));
    check(!FramePacing.sixtyUneven(60) && !FramePacing.sixtyUneven(60.5) && !FramePacing.sixtyUneven(59.94));
    check(!FramePacing.sixtyUneven(120) && !FramePacing.sixtyUneven(144) && !FramePacing.sixtyUneven(75));
    check(!FramePacing.sixtyUneven(0));

    // the Retroid: 60 Hz, nothing changes
    long[] at = {1_000_000_000L};
    FramePacing p = new FramePacing();
    check(p.hz() == 0 && p.every() == 1);
    check(run(p, 60, 200, at) == 0 && p.every() == 1 && Math.abs(p.hz() - 60) < 0.5);

    // 120 Hz: every second vsync, after two measurements (the first vsync starts the first interval)
    p = new FramePacing();
    at[0] = 5_000_000_000L;
    run(p, 120, FramePacing.WINDOW, at);
    check(p.hz() == 0);
    run(p, 120, 1, at);
    check(p.every() == 1 && Math.abs(p.hz() - 120) < 0.5);
    check(run(p, 120, FramePacing.WINDOW, at) == 1 && p.every() == 2);
    check(run(p, 120, 500, at) == 0 && p.every() == 2);

    // late callbacks (a vsync missed now and then: twice the interval) do not move the median
    for (int i = 0; i < 400; i++) {
      at[0] += Math.round(1e9 / 120 * (i % 5 == 0 ? 2 : 1));
      check(!p.vsync(at[0]));
    }
    check(p.every() == 2 && Math.abs(p.hz() - 120) < 0.5);

    // the screen goes to 60 Hz (the request took effect): back to each vsync after two measurements
    run(p, 60, FramePacing.WINDOW, at);
    check(p.every() == 2);
    check(run(p, 60, FramePacing.WINDOW, at) == 1 && p.every() == 1);

    // one odd measurement between agreeing ones changes nothing
    run(p, 120, FramePacing.WINDOW, at);
    run(p, 60, FramePacing.WINDOW, at);
    run(p, 120, FramePacing.WINDOW, at);
    run(p, 60, FramePacing.WINDOW, at);
    check(p.every() == 1);

    // pause: the time away is no interval; a clock that goes back is skipped
    p = new FramePacing();
    at[0] = 0;
    run(p, 120, 10, at);
    p.restart();
    at[0] += 3_000_000_000L;
    run(p, 120, 2 * FramePacing.WINDOW + 1, at);
    check(p.every() == 2 && Math.abs(p.hz() - 120) < 0.5);
    check(!p.vsync(at[0] - 5_000_000L));
    run(p, 120, 2 * FramePacing.WINDOW, at);
    check(p.every() == 2 && Math.abs(p.hz() - 120) < 0.5);

    // 144 Hz: every second (72 a second), 240 Hz: every fourth
    p = new FramePacing();
    run(p, 144, 3 * FramePacing.WINDOW, at);
    check(p.every() == 2);
    p = new FramePacing();
    run(p, 240, 3 * FramePacing.WINDOW, at);
    check(p.every() == 4);

    beat(at);
    System.out.println("FramePacingTests: " + checks + " checks passed");
  }
}
