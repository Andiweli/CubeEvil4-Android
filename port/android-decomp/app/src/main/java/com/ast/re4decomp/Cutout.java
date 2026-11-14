package com.ast.re4decomp;

import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;

/**
 * The camera's cutout of a phone (a punch hole, a notch) and the game's window (0.99.15; a user's Moto G22, 20:9 with a
 * punch hole, Android 12: "it doesn't go into full screen, the camera bar remains visible"). In landscape Android keeps
 * a window off the screen's short edge with the cutout unless the window asks otherwise (LAYOUT_IN_DISPLAY_CUTOUT_MODE
 * DEFAULT): the game stopped at the camera's strip (95 pixels there), a black bar on that side. Now the game's window
 * reaches over it (extend) and the picture fills the screen as the picture format says; what must not lie under the
 * camera keeps clear of its strip with the cutout's safe insets (safe): the touch controls (CoreActivity, where they
 * were before, so a saved layout stays as it was), the FPS display (CornerInset), the app menu (CoreMenu.resizeWindow).
 * The app's questions keep Android's default: their windows stay off the cutout.
 */
final class Cutout {
  private Cutout() {}

  /** The window over the cutout's edge too (as StartupActivity's): ALWAYS from Android 11, SHORT_EDGES on 9 and 10. */
  static void extend(Window window) {
    if (window == null || Build.VERSION.SDK_INT < 28) return;
    WindowManager.LayoutParams p = window.getAttributes();
    final int mode = Build.VERSION.SDK_INT >= 30 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
    if (p.layoutInDisplayCutoutMode != mode) {
      p.layoutInDisplayCutoutMode = mode;
      window.setAttributes(p);
    }
  }

  /** The cutout's safe insets {left, top, right, bottom} in pixels; zeros without one (or before Android 9). */
  static int[] safe(WindowInsets insets) {
    if (insets == null || Build.VERSION.SDK_INT < 28) return new int[4];
    final DisplayCutout cutout = insets.getDisplayCutout();
    if (cutout == null) return new int[4];
    return new int[] {cutout.getSafeInsetLeft(), cutout.getSafeInsetTop(), cutout.getSafeInsetRight(),
        cutout.getSafeInsetBottom()};
  }

  /** The same for the window `view` is in (its root's insets). */
  static int[] safe(View view) {
    if (view == null || Build.VERSION.SDK_INT < 28) return new int[4];
    return safe(view.getRootWindowInsets());
  }
}
