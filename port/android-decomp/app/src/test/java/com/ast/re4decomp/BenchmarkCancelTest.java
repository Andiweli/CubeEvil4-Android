package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.view.KeyEvent;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

/**
 * Ending the benchmark (0.99.17, users could not end it with B): Back, Esc and every B the game would take, through the
 * controller's mapping, "Swap A/B", the keyboard's mapping and the touch controls (CoreInput.touchHeld).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class BenchmarkCancelTest {
  @Test public void backEscAndTheGamesB() {
    assertTrue(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BACK, 0));
    assertTrue(BenchmarkFlow.cancels(KeyEvent.KEYCODE_ESCAPE, 0));
    assertTrue(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_B, CoreInput.GC_B));
    assertTrue("a B the mapping leaves without a button", BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_B, 0));
    assertTrue("another key mapped to the game's B", BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_X, CoreInput.GC_B));
    assertFalse("Android's B code that presses A (Swap A/B)", BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_B, CoreInput.GC_A));
    assertFalse(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_A, CoreInput.GC_A));
    assertFalse(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_START, CoreInput.GC_START));
  }

  @Test public void aControllersB() {
    CoreInput input = new CoreInput();
    assertTrue(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_B, input.gcButton(KeyEvent.KEYCODE_BUTTON_B)));
    assertFalse(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_A, input.gcButton(KeyEvent.KEYCODE_BUTTON_A)));
    // a controller sending the other layout's codes: its B button sends Android's A
    input.setSwapFace(true);
    assertTrue(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_A, input.gcButton(KeyEvent.KEYCODE_BUTTON_A)));
    assertFalse(BenchmarkFlow.cancels(KeyEvent.KEYCODE_BUTTON_B, input.gcButton(KeyEvent.KEYCODE_BUTTON_B)));
  }

  @Test public void aKeyboardsB() {
    CoreInput input = new CoreInput();
    int found = 0;
    for (int code = 1; code < KeyEvent.getMaxKeyCode(); code++) {
      if ((input.keyboardButton(code) & CoreInput.GC_B) == 0) continue;
      found++;
      assertTrue(KeyEvent.keyCodeToString(code), BenchmarkFlow.cancels(code, input.keyboardButton(code)));
    }
    assertTrue("the keyboard's mapping has a key for the game's B", found > 0);
    assertFalse(BenchmarkFlow.cancels(KeyEvent.KEYCODE_W, input.keyboardButton(KeyEvent.KEYCODE_W)));
  }

  @Test public void theTouchControlsB() {
    CoreInput input = new CoreInput();
    assertFalse(input.touchHeld(CoreInput.GC_B));
    input.touchButton(CoreInput.GC_B, true);
    assertTrue(input.touchHeld(CoreInput.GC_B));
    assertFalse(input.touchHeld(CoreInput.GC_A));
    input.touchButton(CoreInput.GC_B, false);
    assertFalse(input.touchHeld(CoreInput.GC_B));
    // L and R are the triggers at their end stop
    input.touchButton(CoreInput.GC_L, true);
    assertTrue(input.touchHeld(CoreInput.GC_L));
    assertFalse(input.touchHeld(CoreInput.GC_B));
  }
}
