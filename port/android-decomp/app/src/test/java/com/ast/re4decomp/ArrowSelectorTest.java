package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.view.KeyEvent;
import java.lang.reflect.Method;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;

/** The arrow cells and the slider cells of the app menu (1.1.0, the project owner's feedback of 21.11.2026). */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class ArrowSelectorTest {
  private static KeyEvent down(int code) { return new KeyEvent(KeyEvent.ACTION_DOWN, code); }

  /** L1 / R1 (Q / E, Page up / down) step, the D-pad is left to the focus; lit unless the off value; the game
   *  language's confirm: a preview until the middle or A, back to the value in force when the cell loses the focus. */
  @Test public void keysLitAndConfirm() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      int[] got = {-1};
      ArrowSelector a = new ArrowSelector(activity, "Name", new String[] {"Off", "Light", "Strong"}, 0, 0);
      a.finishLook();
      a.setListener(i -> got[0] = i);
      a.setOffIndex(0);
      assertFalse("Off: not lit", a.isSelected());
      assertTrue(a.onKeyDown(KeyEvent.KEYCODE_BUTTON_R1, down(KeyEvent.KEYCODE_BUTTON_R1)));
      assertEquals(1, a.index()); assertEquals(1, got[0]); assertTrue("Light: lit", a.isSelected());
      assertTrue(a.onKeyDown(KeyEvent.KEYCODE_E, down(KeyEvent.KEYCODE_E)));
      assertEquals(2, a.index());
      assertTrue(a.onKeyDown(KeyEvent.KEYCODE_PAGE_DOWN, down(KeyEvent.KEYCODE_PAGE_DOWN)));
      assertEquals("round to the first", 0, a.index());
      assertTrue(a.onKeyDown(KeyEvent.KEYCODE_Q, down(KeyEvent.KEYCODE_Q)));
      assertEquals(2, a.index());
      assertTrue(a.onKeyDown(KeyEvent.KEYCODE_BUTTON_L1, down(KeyEvent.KEYCODE_BUTTON_L1)));
      assertEquals(1, a.index());
      assertFalse("the D-pad moves the focus", a.onKeyDown(KeyEvent.KEYCODE_DPAD_LEFT, down(KeyEvent.KEYCODE_DPAD_LEFT)));
      assertFalse(a.onKeyDown(KeyEvent.KEYCODE_DPAD_RIGHT, down(KeyEvent.KEYCODE_DPAD_RIGHT)));
      assertEquals(1, a.index());
      a.performClick();  // A / the middle: forward
      assertEquals(2, a.index()); assertEquals(2, got[0]);

      ArrowSelector lang = new ArrowSelector(activity, null, new String[] {"Device", "English", "Deutsch"}, 0, 0);
      lang.finishLook();
      lang.setConfirm(true);
      int[] taken = {-1};
      lang.setListener(i -> taken[0] = i);
      assertTrue("no Off: always lit", lang.isSelected());
      lang.onKeyDown(KeyEvent.KEYCODE_BUTTON_R1, down(KeyEvent.KEYCODE_BUTTON_R1));
      assertEquals(1, lang.index()); assertTrue(lang.isPending()); assertEquals("only a preview", -1, taken[0]);
      // (the project owner's third menu round: the plain name, no arrow before it)
      assertTrue(lang.getText().toString().endsWith("English")); assertFalse(lang.getText().toString().contains("→"));
      lang.performClick();  // the middle / A: taken
      assertEquals(1, taken[0]); assertFalse(lang.isPending());
      lang.onKeyDown(KeyEvent.KEYCODE_BUTTON_R1, down(KeyEvent.KEYCODE_BUTTON_R1));
      assertEquals(2, lang.index()); assertTrue(lang.isPending());
      Method focus = android.view.View.class.getDeclaredMethod("onFocusChanged", boolean.class, int.class,
          android.graphics.Rect.class);
      focus.setAccessible(true);
      focus.invoke(lang, false, 0, null);
      assertEquals("left without taking it: the language in force again", 1, lang.index());
      assertFalse(lang.isPending()); assertEquals(1, taken[0]);
      assertFalse("the same value taken again: nothing", lang.take());
    }
  }

  /** The slider cell: L1 / R1 step it (each a move of the player), not past its ends, not while its bar is off. */
  // (real font sizes for the heights)
  @org.robolectric.annotation.GraphicsMode(org.robolectric.annotation.GraphicsMode.Mode.NATIVE)
  @Test public void sliderCellSteps() {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      int[] moved = {-1};
      SliderCell c = new SliderCell(activity, "Range", null);
      c.setUp(4, 3, p -> (50 + p * 5) + " %", p -> moved[0] = p);
      assertEquals("65 %", c.value.getText().toString());
      assertTrue(c.onKeyDown(KeyEvent.KEYCODE_BUTTON_R1, down(KeyEvent.KEYCODE_BUTTON_R1)));
      assertEquals(4, c.bar.getProgress()); assertEquals(4, moved[0]); assertEquals("70 %", c.value.getText().toString());
      assertFalse("at the end", c.step(1));
      c.onKeyDown(KeyEvent.KEYCODE_Q, down(KeyEvent.KEYCODE_Q));
      assertEquals(3, c.bar.getProgress());
      c.bar.setEnabled(false);
      assertFalse("off: no step", c.step(-1));
      assertEquals(3, c.bar.getProgress());
      assertFalse("the bar takes no focus", c.bar.isFocusable());
      assertTrue("the cell does", c.isFocusable());
      // (the project owner's third menu round) exactly as high as an arrow cell: 64dp
      int width = android.view.View.MeasureSpec.makeMeasureSpec(600, android.view.View.MeasureSpec.EXACTLY);
      int free = android.view.View.MeasureSpec.makeMeasureSpec(0, android.view.View.MeasureSpec.UNSPECIFIED);
      ArrowSelector a = new ArrowSelector(activity, "Name", new String[] {"Off", "On", "More"}, 0, 0);
      a.setTextSize(16);
      a.finishLook();
      c.measure(width, free); a.measure(width, free);
      int dp64 = Math.round(64 * activity.getResources().getDisplayMetrics().density);
      assertEquals("the slider cell 64dp", dp64, c.getMeasuredHeight());
      assertEquals("as the arrow cell", a.getMeasuredHeight(), c.getMeasuredHeight());
    }
  }
}
