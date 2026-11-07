package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** InputDelay: the controller's delay from Android to the app, one line per 30 seconds. */
public class InputDelayTest {
  @Test
  public void linePerPeriodAndAnew() {
    InputDelay d = new InputDelay();
    d.button(1000, 1004);
    d.button(2000, 2010);
    d.stick(3000, 3000, 3002);
    d.stick(4000, 3990, 4001);  // a batch: its oldest sample 11 ms back
    assertNull(d.due(1004 + InputDelay.PERIOD_MS - 1, "pad"));
    String line = d.due(1004 + InputDelay.PERIOD_MS, "pad");
    assertTrue(line, line.contains("buttons 7.0 ms on average, at most 10 ms (2)"));
    assertTrue(line, line.contains("sticks 1.5 ms on average, at most 2 ms (2)"));
    assertTrue(line, line.contains("1 stick events held back for the next refresh (their oldest sample 11.0 ms"));
    // counting starts anew
    assertNull(d.due(1004 + 2 * InputDelay.PERIOD_MS, "pad"));
    d.stick(50000, 50000, 50003);
    line = d.due(50003 + InputDelay.PERIOD_MS, "pad");
    assertTrue(line, line.contains("buttons none, sticks 3.0 ms on average"));
    assertEquals(false, line.contains("held back"));
  }
}
