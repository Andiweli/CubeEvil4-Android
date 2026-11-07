package com.ast.re4decomp;

import java.util.Locale;

/**
 * The main stick as the game turns it into walking, for the error log (a user's report 0992, 07.11.2026: "my left
 * analog stick stutters when I'm walking or running, the D-pad is perfectly smooth"). The game moves Leon digitally:
 * game/pad.cpp PadRead takes the stick after the SDK's PADClamp (src/lib/Padclamp.c ClampStick: dead zone 15, the
 * octagon 72 / 40) and, once it is past 30, sets the direction bits "up" (within 70 degrees of up), "down" (beyond
 * 110) and the two sides (30 to 150); the D-pad sets the same bits directly. A stick that wanders around one of these
 * borders switches Leon between walking and standing or walking and turning, the D-pad cannot.
 *
 * <p>Fed with every controller state the app hands the core (CoreInput.pad: raw values, up positive, after the
 * menu's stick dead zone and range, which the line names), it writes one
 * line for each 30 s in which the stick was past the threshold for at least 2 s: how long, how far it reached, how often
 * the bits changed, how often "up" dropped out for less than BLIP_MS, and how many states lay within 6 of the threshold.
 * Pure Java (StickWatchTest).
 */
final class StickWatch {
  static final int MIN = 15, MAX = 72, XY = 40;
  static final float ON = 30f;
  static final int UP = 1, DOWN = 2, SIDE_A = 4, SIDE_B = 8;
  static final long WINDOW_MS = 30000, BLIP_MS = 150, MIN_USE_MS = 2000;
  static final float NEAR = 6f;

  private long windowStart = -1, lastAt = -1, upLostAt = -1;
  private int bits, changes, blips, samples, near;
  private long usedMs;
  private float maxMag;
  private int maxRaw;
  private int deadZone, range = 100;  // the menu's stick settings (CoreInput.setStickShape), for the line

  /** The menu's dead zone and range in percent, named in each line (0.99.2: what the next report was made with). */
  void setShape(int deadZonePercent, int rangePercent) {
    deadZone = deadZonePercent;
    range = rangePercent;
  }

  /** src/lib/Padclamp.c ClampStick with the main stick's region (15, 72, 40): {x, y}. */
  static int[] clamp(int px, int py) {
    int x = Math.abs(px), y = Math.abs(py);
    int sx = px < 0 ? -1 : 1, sy = py < 0 ? -1 : 1;
    x = x <= MIN ? 0 : x - MIN;
    y = y <= MIN ? 0 : y - MIN;
    if (x == 0 && y == 0) return new int[] {0, 0};
    int d = XY * y <= XY * x ? XY * x + (MAX - XY) * y : XY * y + (MAX - XY) * x;
    if (XY * MAX < d) {
      x = (byte) (XY * MAX * x / d);
      y = (byte) (XY * MAX * y / d);
    }
    return new int[] {sx * x, sy * y};
  }

  /** game/pad.cpp PadRead's direction bits of a clamped stick (y up positive). */
  static int bits(int x, int y) {
    if ((float) x * x + (float) y * y <= ON * ON) return 0;
    double ang = -Math.atan2(x, y);
    double deg = Math.toDegrees(ang);
    int b = 0;
    if (ang < 0) {
      if (deg > -70) b |= UP;
      if (deg < -110) b |= DOWN;
      if (deg < -30 && deg > -150) b |= SIDE_A;
    } else {
      if (deg < 70) b |= UP;
      if (deg > 110) b |= DOWN;
      if (deg > 30 && deg < 150) b |= SIDE_B;
    }
    return b;
  }

  /** A controller state at `now` (ms): raw main stick, up positive. A line when a window of use ends, else null. */
  String sample(long now, int rawX, int rawY) {
    String line = null;
    if (windowStart < 0) windowStart = now;
    if (lastAt >= 0 && bits != 0) usedMs += now - lastAt;
    if (now - windowStart >= WINDOW_MS) {
      if (usedMs >= MIN_USE_MS) line = line(now - windowStart);
      windowStart = now;
      usedMs = 0;
      changes = blips = samples = near = 0;
      maxMag = 0;
      maxRaw = 0;
    }
    int[] c = clamp(rawX, rawY);
    float mag = (float) Math.sqrt((double) c[0] * c[0] + (double) c[1] * c[1]);
    int b = bits(c[0], c[1]);
    if (b != 0 || mag > 0) {
      samples++;
      if (Math.abs(mag - ON) <= NEAR) near++;
      maxMag = Math.max(maxMag, mag);
      maxRaw = Math.max(maxRaw, Math.max(Math.abs(rawX), Math.abs(rawY)));
    }
    if (b != bits) {
      changes++;
      if ((bits & UP) != 0 && (b & UP) == 0) upLostAt = now;
      if ((bits & UP) == 0 && (b & UP) != 0 && upLostAt >= 0 && now - upLostAt < BLIP_MS) blips++;
      bits = b;
    }
    lastAt = now;
    return line;
  }

  private String line(long window) {
    return String.format(Locale.ROOT,
        "stick (as the game reads it for walking): %.0f s past its threshold in the last %.0f s, at most %.0f of 72 "
            + "(raw %d of 101; walking from 30), %d direction changes, \"up\" lost for less than %d ms %d times, "
            + "%d of %d states within %.0f of the threshold; the app's stick dead zone %d %%, range %d %%",
        usedMs / 1000.0, window / 1000.0, maxMag, maxRaw, changes, BLIP_MS, blips, near, samples, NEAR, deadZone,
        range);
  }
}
