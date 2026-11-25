package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

/** The captured mouse's camera look at any report rate (MouseLookRate, 1.1.3). */
public class MouseLookRateTest {
  /** A steady movement of `perSecond` counts sideways from a mouse reporting every `everyMs`: the look at the end. */
  private static float steady(long everyMs, float perSecond, long forMs) {
    MouseLookRate rate = new MouseLookRate();
    float[] look = null;
    for (long t = 1000; t <= 1000 + forMs; t += everyMs) look = rate.add(t, perSecond * everyMs / 1000f, 0);
    return look[0];
  }

  @Test public void aUsualMouseLooksAsBefore() {
    // 125 Hz, 4 counts a report (500 counts a second): 4 as each event gave until 1.1.2
    assertEquals(4f, steady(8, 500, 200), 1e-4f);
  }

  @Test public void aFastMouseIsNoSlower() {
    // the same hand at 250, 500 and 1000 reports a second: until 1.1.2 2, 1 and 0.5 counts an event
    assertEquals(4f, steady(4, 500, 200), 1e-3f);
    assertEquals(4f, steady(2, 500, 200), 1e-3f);
    assertEquals(4f, steady(1, 500, 200), 1e-3f);
  }

  @Test public void theStartOfAMovement() {
    MouseLookRate rate = new MouseLookRate();
    // the first report of a 125 Hz mouse: its counts as they are (no slow start)
    assertEquals(4f, rate.add(1000, 4, 0)[0], 1e-6f);
    assertEquals(4f, rate.add(1008, 4, 0)[0], 1e-6f);
    // a 1000 Hz mouse's first ms: an eighth of that; it reaches its full value within the window (32 ms)
    MouseLookRate fast = new MouseLookRate();
    assertEquals(0.5f, fast.add(1000, 0.5f, 0)[0], 1e-6f);
    float[] look = null;
    for (long t = 1001; t <= 1008; t++) look = fast.add(t, 0.5f, 0);
    assertEquals(4.5f * 8 / 16, look[0], 1e-5f);
  }

  @Test public void afterARestAfresh() {
    MouseLookRate rate = new MouseLookRate();
    for (long t = 1000; t < 1100; t += 8) rate.add(t, 4, 2);
    // 200 ms later: the counts of before are gone
    float[] look = rate.add(1300, 1, -1);
    assertEquals(1f, look[0], 1e-6f);
    assertEquals(-1f, look[1], 1e-6f);
    rate.rest();
    assertEquals(2f, rate.add(1310, 2, 0)[0], 1e-6f);
  }

  @Test public void anEightThousandHertzMouse() {
    // more events in the window than its ring holds is fine (sub-ms reports have the same ms time)
    MouseLookRate rate = new MouseLookRate();
    float[] look = null;
    for (int i = 0; i < 2000; i++) look = rate.add(1000 + i / 8, 0.0625f, 0);
    assertEquals(true, look[0] > 0 && look[0] <= 4.01f);
  }
}
