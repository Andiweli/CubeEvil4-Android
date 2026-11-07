package com.ast.re4decomp;

import static org.junit.Assert.*;

import org.junit.Test;

/** StickWatch: the SDK's clamp, the game's direction bits and the 30 s line (the stutter report of 07.11.2026). */
public class StickWatchTest {
  @Test public void theClampAsTheSdk() {
    assertArrayEquals(new int[] {0, 72}, StickWatch.clamp(0, 101));     // full up: the octagon's top
    assertArrayEquals(new int[] {0, -72}, StickWatch.clamp(0, -101));
    assertArrayEquals(new int[] {40, 40}, StickWatch.clamp(71, 71));    // full diagonal: its corner
    assertArrayEquals(new int[] {0, 0}, StickWatch.clamp(15, -15));     // the dead zone
    assertArrayEquals(new int[] {0, 30}, StickWatch.clamp(0, 45));      // 45 raw: 30 after the dead zone
  }

  @Test public void theGamesDirections() {
    assertEquals(StickWatch.UP, StickWatch.bits(0, 72));
    assertEquals(0, StickWatch.bits(0, 30));                            // not past 30
    assertEquals(StickWatch.UP, StickWatch.bits(0, 31));
    assertEquals(StickWatch.DOWN, StickWatch.bits(0, -72));
    // 45 degrees: up and a side, both ways
    assertEquals(StickWatch.UP | StickWatch.SIDE_A, StickWatch.bits(40, 40));
    assertEquals(StickWatch.UP | StickWatch.SIDE_B, StickWatch.bits(-40, 40));
    // 20 degrees beside up: up alone (the sides start at 30)
    assertEquals(StickWatch.UP, StickWatch.bits(25, 68));
  }

  @Test public void aSteadyStickGivesOneChange() {
    StickWatch w = new StickWatch();
    String line = null;
    for (long t = 0; t <= 31000; t += 10) {
      String l = w.sample(t, 0, 101);
      if (l != null) line = l;
    }
    assertNotNull(line);
    assertTrue(line, line.contains("30 s past its threshold"));
    assertTrue(line, line.contains("at most 72 of 72 (raw 101 of 101"));
    assertTrue(line, line.contains("1 direction changes"));
    assertTrue(line, line.contains("less than 150 ms 0 times"));
    assertTrue(line, line.endsWith("the app's stick dead zone 0 %, range 100 %"));
  }

  @Test public void theLineNamesTheMenusSettings() {
    StickWatch w = new StickWatch();
    w.setShape(10, 85);
    String line = null;
    for (long t = 0; t <= 31000; t += 10) {
      String l = w.sample(t, 0, 101);
      if (l != null) line = l;
    }
    assertNotNull(line);
    assertTrue(line, line.endsWith("the app's stick dead zone 10 %, range 85 %"));
  }

  @Test public void aStickAtTheThresholdShowsItsDropOuts() {
    StickWatch w = new StickWatch();
    String line = null;
    for (long t = 0; t <= 31000; t += 10) {
      // hovering at the threshold: every second "up" drops out for 50 ms (raw 44 = 29 after the dead zone)
      int y = t % 1000 < 50 ? 44 : 47;
      String l = w.sample(t, 0, y);
      if (l != null) line = l;
    }
    assertNotNull(line);
    // one drop-out a second, the first window from 1 s to 29 s
    assertTrue(line, line.contains("less than 150 ms 29 times"));
    assertTrue(line, line.contains("at most 32 of 72"));
  }

  @Test public void noLineWithoutUse() {
    StickWatch w = new StickWatch();
    for (long t = 0; t <= 61000; t += 10) assertNull(w.sample(t, 0, 0));
  }
}
