package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.graphics.Insets;
import android.view.DisplayCutout;
import android.view.WindowInsets;
import android.view.WindowManager;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;

/** Cutout (0.99.17, a user's Moto G22): the game's window over the camera's cutout, the safe insets, the log's line. */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class CutoutTest {
  @Test public void theWindowReachesOverTheCutout() {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      assertEquals(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT,
          activity.getWindow().getAttributes().layoutInDisplayCutoutMode);
      Cutout.extend(activity.getWindow());
      assertEquals("Android 11 and later: always", WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS,
          activity.getWindow().getAttributes().layoutInDisplayCutoutMode);
    }
  }

  @Config(sdk = 29)
  @Test public void androidTenTakesTheShortEdges() {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      Cutout.extend(activity.getWindow());
      assertEquals(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES,
          activity.getWindow().getAttributes().layoutInDisplayCutoutMode);
    }
  }

  @Test public void theModeByAndroidsVersion() {
    assertEquals(-1, Cutout.mode(27));
    assertEquals(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES, Cutout.mode(28));
    assertEquals(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES, Cutout.mode(29));
    assertEquals(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS, Cutout.mode(30));
    assertEquals(WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS, Cutout.mode(35));
  }

  @Test public void theSafeInsetsOnEitherSide() {
    // the Moto G22 in landscape: the camera's strip 95 pixels on the left, after turning by 180 degrees on the right
    WindowInsets left = new WindowInsets.Builder()
        .setDisplayCutout(new DisplayCutout(Insets.of(95, 0, 0, 0), null, null, null, null)).build();
    assertArrayEquals(new int[] {95, 0, 0, 0}, Cutout.safe(left));
    assertEquals("left", Cutout.side(Cutout.safe(left)));
    WindowInsets right = new WindowInsets.Builder()
        .setDisplayCutout(new DisplayCutout(Insets.of(0, 0, 95, 0), null, null, null, null)).build();
    assertArrayEquals(new int[] {0, 0, 95, 0}, Cutout.safe(right));
    assertEquals("right", Cutout.side(Cutout.safe(right)));
    assertArrayEquals("no cutout", new int[4], Cutout.safe(new WindowInsets.Builder().build()));
    assertArrayEquals(new int[4], Cutout.safe((WindowInsets) null));
    assertEquals("none", Cutout.side(new int[4]));
    assertEquals("left and right", Cutout.side(new int[] {80, 0, 80, 0}));
  }

  @Test public void theLogsLine() {
    final int always = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
    final int dflt = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
    // over the cutout: the whole screen, the strip as a safe inset
    String over = Cutout.describe(1600, 720, 0, 0, 1600, 720, new int[] {95, 0, 0, 0}, always, always);
    assertEquals("camera cutout: window 1600x720 at 0,0 of the screen 1600x720, safe insets left 95 top 0 right 0"
        + " bottom 0 (camera: left), mode asked always, window has always; the window reaches over the cutout", over);
    // the phone's own setting keeps the app off the cutout: a smaller window without insets
    String kept = Cutout.describe(1505, 720, 95, 0, 1600, 720, new int[4], always, always);
    assertTrue(kept, kept.endsWith("; the window keeps off a strip of the screen (a cutout the phone keeps the app off?)"));
    assertTrue(kept, kept.contains("window 1505x720 at 95,0 of the screen 1600x720"));
    // a phone without a cutout
    String none = Cutout.describe(1920, 1080, 0, 0, 1920, 1080, new int[4], always, always);
    assertTrue(none, none.endsWith("(camera: none), mode asked always, window has always; no cutout in this window"));
    // before Android 9 and the default mode by name
    assertTrue(Cutout.describe(1280, 720, 0, 0, 1280, 720, new int[4], -1, dflt)
        .contains("mode asked none (before Android 9), window has default"));
  }
}
