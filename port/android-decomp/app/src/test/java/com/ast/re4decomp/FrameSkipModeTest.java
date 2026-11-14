package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * Frame skip as one setting (CoreMenu.frameSkipMode, 0.99.8; wish of the project owner, 10.11.2026): Off, Normal (the
 * renderer's), Aggressive (the game's own); the two switches of before ("frameskip", "cpuSkip") taken over, both on:
 * Aggressive.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class FrameSkipModeTest {
  private SharedPreferences g;

  @Before public void clean() {
    g = RuntimeEnvironment.getApplication().getSharedPreferences("graphics-test", Context.MODE_PRIVATE);
    g.edit().clear().commit();
  }

  @Test public void nothingSavedIsOff() {
    assertEquals(0, CoreMenu.frameSkipMode(g));
  }

  @Test public void theOldSwitchesTakenOver() {
    g.edit().putBoolean("frameskip", true).commit();
    assertEquals(1, CoreMenu.frameSkipMode(g));
    g.edit().putBoolean("cpuSkip", true).commit();
    assertEquals(2, CoreMenu.frameSkipMode(g));  // both on: Aggressive
    g.edit().putBoolean("frameskip", false).commit();
    assertEquals(2, CoreMenu.frameSkipMode(g));
  }

  @Test public void storedReplacesTheOldSwitches() {
    g.edit().putBoolean("frameskip", true).putBoolean("cpuSkip", true).commit();
    CoreMenu.storeFrameSkipMode(g, 1);
    assertEquals(1, CoreMenu.frameSkipMode(g));
    assertFalse(g.contains("frameskip"));
    assertFalse(g.contains("cpuSkip"));
    CoreMenu.storeFrameSkipMode(g, 7);
    assertEquals(2, CoreMenu.frameSkipMode(g));
  }

  @Test public void eachPressMovesOnAndBackToOff() {
    int mode = 0;
    int[] seen = new int[4];
    for (int i = 0; i < 4; i++) {
      mode = (CoreMenu.frameSkipMode(g) + 1) % 3;
      CoreMenu.storeFrameSkipMode(g, mode);
      seen[i] = CoreMenu.frameSkipMode(g);
    }
    assertArrayEquals(new int[] {1, 2, 0, 1}, seen);
  }
}
