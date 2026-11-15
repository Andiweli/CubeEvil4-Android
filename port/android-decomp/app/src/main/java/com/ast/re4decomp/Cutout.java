package com.ast.re4decomp;

import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;

/**
 * The camera's cutout of a phone (a punch hole, a notch) and the game's window (0.99.17; first in 0.99.15, taken back
 * in 0.99.16). In landscape Android keeps a window off the screen's short edge with the cutout unless the window asks
 * otherwise (LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT): a black bar on the camera's side (a user's Moto G22, 95 pixels).
 * The game's window reaches over it (extend) and the picture fills the screen as the picture format says; the touch
 * controls cover the whole window, so a button can go into the camera's strip too, and their layouts keep their spots
 * (TouchFrame). What must not lie under the camera keeps clear of its strip with the cutout's safe insets (safe): the
 * FPS display (CornerInset), the app menu (CoreMenu.resizeWindow), the touch editor's bar and the gear's default place
 * (RetroTouch 1.0.0-beta.11, TouchControls.setSafeInsets). The app's questions keep Android's default: their windows
 * stay off the cutout. Some phones (Xiaomi, Samsung, Motorola) keep an app off the cutout by a setting of their own per app
 * whatever the app asks; the error log says what the window got (describe).
 */
final class Cutout {
  private Cutout() {}

  /** The mode the game's window asks for on this Android: ALWAYS from 11, SHORT_EDGES on 9 and 10, -1 before 9. */
  static int mode(int sdk) {
    if (sdk < 28) return -1;
    return sdk >= 30 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
  }

  /** The window over the cutout's edge too (as StartupActivity's). */
  static void extend(Window window) {
    final int mode = mode(Build.VERSION.SDK_INT);
    if (window == null || mode < 0) return;
    WindowManager.LayoutParams p = window.getAttributes();
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

  /** Which side the camera is on by the safe insets: "left", "right", "top", "bottom", "none" (or two of them). */
  static String side(int[] safe) {
    StringBuilder s = new StringBuilder();
    final String[] names = {"left", "top", "right", "bottom"};
    for (int i = 0; i < 4; i++) {
      if (safe[i] <= 0) continue;
      if (s.length() > 0) s.append(" and ");
      s.append(names[i]);
    }
    return s.length() == 0 ? "none" : s.toString();
  }

  /** A window mode's name for the log. */
  static String modeName(int mode) {
    switch (mode) {
      case WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT: return "default";
      case WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES: return "short edges";
      case WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER: return "never";
      case WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS: return "always";
      default: return mode < 0 ? "none (before Android 9)" : "mode " + mode;
    }
  }

  /**
   * The error log's line about the cutout: the window's size and place on the screen, the screen's size, the cutout's
   * safe insets and side, the mode the window asked for and the one it has. The window "keeps off the cutout" when it
   * is smaller than the screen on a side with a cutout and has no safe inset there (the phone's own setting for the
   * app, or Android's default).
   */
  static String describe(int windowWidth, int windowHeight, int windowX, int windowY, int screenWidth,
      int screenHeight, int[] safe, int asked, int has) {
    StringBuilder s = new StringBuilder("camera cutout: window ").append(windowWidth).append('x').append(windowHeight)
        .append(" at ").append(windowX).append(',').append(windowY).append(" of the screen ").append(screenWidth)
        .append('x').append(screenHeight).append(", safe insets left ").append(safe[0]).append(" top ")
        .append(safe[1]).append(" right ").append(safe[2]).append(" bottom ").append(safe[3]).append(" (camera: ")
        .append(side(safe)).append("), mode asked ").append(modeName(asked)).append(", window has ")
        .append(modeName(has));
    final boolean smaller = screenWidth > 0 && screenHeight > 0
        && (windowWidth < screenWidth || windowHeight < screenHeight);
    final boolean insets = safe[0] > 0 || safe[1] > 0 || safe[2] > 0 || safe[3] > 0;
    if (insets) s.append("; the window reaches over the cutout");
    else if (smaller) s.append("; the window keeps off a strip of the screen (a cutout the phone keeps the app off?)");
    else s.append("; no cutout in this window");
    return s.toString();
  }
}
