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

/** Cutout (0.99.15, a user's Moto G22): the game's window over the camera's cutout, the safe insets read back. */
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

  @Test public void theSafeInsetsOnEitherSide() {
    // the Moto G22 in landscape: the camera's strip 95 pixels on the left, after turning by 180 degrees on the right
    WindowInsets left = new WindowInsets.Builder()
        .setDisplayCutout(new DisplayCutout(Insets.of(95, 0, 0, 0), null, null, null, null)).build();
    assertArrayEquals(new int[] {95, 0, 0, 0}, Cutout.safe(left));
    WindowInsets right = new WindowInsets.Builder()
        .setDisplayCutout(new DisplayCutout(Insets.of(0, 0, 95, 0), null, null, null, null)).build();
    assertArrayEquals(new int[] {0, 0, 95, 0}, Cutout.safe(right));
    assertArrayEquals("no cutout", new int[4], Cutout.safe(new WindowInsets.Builder().build()));
    assertArrayEquals(new int[4], Cutout.safe((WindowInsets) null));
  }
}
