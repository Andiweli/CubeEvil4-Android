package com.ast.re4decomp;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** The hang watchdog's rule (CoreActivity.hangCounts, 1.1.3): a standing game is a hang only while nothing holds it. */
public class HangRuleTest {
  @Test public void aStandingGameInPlayIsAHang() {
    assertTrue(CoreActivity.hangCounts(true, false, false, false, false, false));
  }

  @Test public void theAppsOwnPausesAreNot() {
    assertFalse("in the background", CoreActivity.hangCounts(false, false, false, false, false, false));
    assertFalse("the app menu", CoreActivity.hangCounts(true, true, false, false, false, false));
    assertFalse("the touch editor", CoreActivity.hangCounts(true, false, true, false, false, false));
    assertFalse("a benchmark popup", CoreActivity.hangCounts(true, false, false, true, false, false));
    assertFalse("shaders prepared at start (a user's false report)",
        CoreActivity.hangCounts(true, false, false, false, true, false));
    assertFalse("photo mode", CoreActivity.hangCounts(true, false, false, false, false, true));
  }
}
