package com.ast.re4decomp;

import java.util.Locale;

/**
 * How long a controller's input took from Android's event time (when the system took it from the
 * device driver) to the app (the Bluetooth controller lag report of 05.11.2026): buttons and sticks
 * apart, and for sticks the oldest sample Android held back in a batch. Once every 30 seconds with
 * input, one line for the error log. What Bluetooth itself takes before Android has the event is not
 * in it. No Android types: the host tests run it.
 */
final class InputDelay {
  static final long PERIOD_MS = 30_000;

  private long since = -1;
  private int buttons, sticks;
  private long buttonSum, buttonMax, stickSum, stickMax, batchSum, batchMax;
  private int batched;

  /** A button pressed or released at `eventMs`, handled at `nowMs` (both SystemClock.uptimeMillis). */
  synchronized void button(long eventMs, long nowMs) {
    start(nowMs);
    final long d = Math.max(0, nowMs - eventMs);
    buttons++;
    buttonSum += d;
    buttonMax = Math.max(buttonMax, d);
  }

  /**
   * A stick, trigger or hat event of `eventMs` handled at `nowMs`; `oldestMs` the oldest sample in its
   * batch (the event time when Android sent it alone).
   */
  synchronized void stick(long eventMs, long oldestMs, long nowMs) {
    start(nowMs);
    final long d = Math.max(0, nowMs - eventMs);
    sticks++;
    stickSum += d;
    stickMax = Math.max(stickMax, d);
    if (oldestMs < eventMs) {
      final long b = Math.max(0, nowMs - oldestMs);
      batched++;
      batchSum += b;
      batchMax = Math.max(batchMax, b);
    }
  }

  private void start(long nowMs) {
    if (since < 0) since = nowMs;
  }

  /** The line of the last period when it is over (null otherwise); counting starts anew. */
  synchronized String due(long nowMs, String device) {
    if (since < 0 || nowMs - since < PERIOD_MS) return null;
    final String line = String.format(Locale.ROOT,
        "input delay (Android to the app, %s, %d s): buttons %s, sticks %s%s", device, (nowMs - since) / 1000,
        part(buttons, buttonSum, buttonMax), part(sticks, stickSum, stickMax),
        batched > 0 ? String.format(Locale.ROOT, ", %d stick events held back for the next refresh (their oldest sample %s)",
            batched, part(batched, batchSum, batchMax)) : "");
    since = -1;
    buttons = sticks = batched = 0;
    buttonSum = buttonMax = stickSum = stickMax = batchSum = batchMax = 0;
    return line;
  }

  private static String part(int n, long sum, long max) {
    if (n == 0) return "none";
    return String.format(Locale.ROOT, "%.1f ms on average, at most %d ms (%d)", (double) sum / n, max, n);
  }
}
