package com.ast.re4decomp;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.SharedPreferences;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * "Move and aim" and the two "aim with" choices (1.1.6, the project owner): the switch always works; on, it puts the
 * aiming where no left stick aims (the touch area for the move stick, the right stick for the left) and keeps the
 * choices of before, which come back when it goes off (CoreActivity.moveAimSources).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = android.app.Application.class)
public class MoveAimSourcesTest {
  private SharedPreferences prefs;

  @Before public void setUp() {
    prefs = RuntimeEnvironment.getApplication().getSharedPreferences("moveAimTest", Context.MODE_PRIVATE);
    prefs.edit().clear().commit();
  }

  @Test public void onSwitchesTheLeftSticksOffAndOffGivesThemBack() {
    prefs.edit().putInt(CoreActivity.TOUCH_AIM, CoreInput.AIM_MOVE_STICK)
        .putInt(CoreActivity.CONTROLLER_AIM, CoreInput.CONTROLLER_AIM_LEFT).commit();
    assertArrayEquals(new int[] {CoreInput.AIM_LOOK, CoreInput.CONTROLLER_AIM_RIGHT},
        CoreActivity.moveAimSources(prefs, true));
    assertTrue(prefs.getBoolean(CoreActivity.MOVE_AIM, false));
    assertEquals(CoreInput.AIM_LOOK, prefs.getInt(CoreActivity.TOUCH_AIM, -1));
    assertArrayEquals(new int[] {CoreInput.AIM_MOVE_STICK, CoreInput.CONTROLLER_AIM_LEFT},
        CoreActivity.moveAimSources(prefs, false));
    assertFalse(prefs.getBoolean(CoreActivity.MOVE_AIM, true));
    assertFalse(prefs.contains(CoreActivity.TOUCH_AIM_BEFORE_MOVE));
  }

  @Test public void theTouchCStickStaysAndChoicesThatAlreadyWorkAreKept() {
    prefs.edit().putInt(CoreActivity.TOUCH_AIM, CoreInput.AIM_C_STICK)
        .putInt(CoreActivity.CONTROLLER_AIM, CoreInput.CONTROLLER_AIM_LEFT).commit();
    assertArrayEquals(new int[] {CoreInput.AIM_C_STICK, CoreInput.CONTROLLER_AIM_RIGHT},
        CoreActivity.moveAimSources(prefs, true));
    assertArrayEquals(new int[] {CoreInput.AIM_C_STICK, CoreInput.CONTROLLER_AIM_LEFT},
        CoreActivity.moveAimSources(prefs, false));
    // touch area and right stick already: nothing to put back
    prefs.edit().putInt(CoreActivity.TOUCH_AIM, CoreInput.AIM_LOOK)
        .putInt(CoreActivity.CONTROLLER_AIM, CoreInput.CONTROLLER_AIM_RIGHT).commit();
    CoreActivity.moveAimSources(prefs, true);
    assertFalse(prefs.contains(CoreActivity.TOUCH_AIM_BEFORE_MOVE));
    assertArrayEquals(new int[] {CoreInput.AIM_LOOK, CoreInput.CONTROLLER_AIM_RIGHT},
        CoreActivity.moveAimSources(prefs, false));
  }

  @Test public void onTwiceKeepsTheFirstChoicesOfBefore() {
    prefs.edit().putInt(CoreActivity.TOUCH_AIM, CoreInput.AIM_MOVE_STICK)
        .putInt(CoreActivity.CONTROLLER_AIM, CoreInput.CONTROLLER_AIM_LEFT).commit();
    CoreActivity.moveAimSources(prefs, true);
    CoreActivity.moveAimSources(prefs, true);  // (the start's check with the switch on)
    assertArrayEquals(new int[] {CoreInput.AIM_MOVE_STICK, CoreInput.CONTROLLER_AIM_LEFT},
        CoreActivity.moveAimSources(prefs, false));
  }
}
