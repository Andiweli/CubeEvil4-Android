package com.ast.re4decomp;

/**
 * The captured mouse's camera look, independent of how often the mouse reports (1.1.3, a player's report: "Turn Leon
 * with mouse / look area" very slow on some devices). The camera takes the mouse as a C stick (CoreInput.mouseLook):
 * until 1.1.2 each event's counts times the sensitivity, and Android hands a captured mouse's events over one by one
 * (SOURCE_MOUSE_RELATIVE is not batched per frame like the touch screen's): a mouse reporting 1000 times a second gave
 * an eighth of the counts per event of a usual one at 125 a second, so the camera moved an eighth as fast (and slow
 * movements stayed under the game's dead zone). Now the counts of the last WINDOW_MS, as counts per REPORT_MS (a 125 Hz
 * mouse's report): a 125 Hz mouse looks as before, a faster one as fast. The aim and Leon's turn take the counts
 * themselves (radians a count, summed up to the game's frame) and were never slower.
 */
final class MouseLookRate {
  /** A usual mouse's report (USB's default 125 Hz) and the window the counts are taken over, ms. */
  static final long REPORT_MS = 8, WINDOW_MS = 32;
  /** The mouse at rest after this long without an event (as CoreActivity's mouseRest), ms. */
  static final long REST_MS = 60;
  private static final int MAX = 512;  // events in the window (an 8000 Hz mouse's 256 with room)

  private final long[] times = new long[MAX];
  private final float[] xs = new float[MAX], ys = new float[MAX];
  private int head, count;  // the oldest at head
  private long start, last;
  private boolean moving;

  /** The mouse moved by dx, dy counts at `ms` (its event's time): {x, y}, the look as counts per REPORT_MS. */
  float[] add(long ms, float dx, float dy) {
    if (!moving || ms - last > REST_MS || ms < last) {
      count = 0;
      start = ms;
      moving = true;
    }
    last = ms;
    if (count == MAX) {
      head = (head + 1) % MAX;
      count--;
    }
    int at = (head + count) % MAX;
    times[at] = ms;
    xs[at] = dx;
    ys[at] = dy;
    count++;
    while (count > 0 && ms - times[head] >= WINDOW_MS) {
      head = (head + 1) % MAX;
      count--;
    }
    float sx = 0, sy = 0;
    for (int i = 0; i < count; i++) {
      int k = (head + i) % MAX;
      sx += xs[k];
      sy += ys[k];
    }
    // the time the counts cover: one report at the movement's start, up to the window
    long span = Math.max(REPORT_MS, Math.min(WINDOW_MS, ms - start + REPORT_MS));
    float k = (float) REPORT_MS / span;
    return new float[] {sx * k, sy * k};
  }

  /** The mouse rests (no event for REST_MS): the next movement starts afresh. */
  void rest() {
    moving = false;
    count = 0;
  }
}
