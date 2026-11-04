package com.ast.re4decomp;

import android.content.Context;
import android.content.pm.PackageManager;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * Android Automotive (AAOS, the car's own Android; wish of the project owner, 04.11.2026, like his other
 * car ports UT99 and UE1). Phones, the Retroid and Android TV are not touched: every call here does
 * nothing there.
 *
 * In a car the maker's system bars (the status bar, the climate bar at the bottom or the sides) often stay,
 * whatever an app asks: the game's window asks to hide them as on a phone, and what stays visible is kept
 * clear (the window's content is moved inside it), so no bar lies over the picture, the touch controls or
 * the app menu. Where the car lets them go, the game fills the screen as on a phone.
 */
final class CarDisplay {
  private static final String TAG = "RE4Car";
  private static int car = -1;  // -1 not asked yet, 0 no, 1 a car

  private CarDisplay() {}

  /** Android Automotive (PackageManager.FEATURE_AUTOMOTIVE), asked once. */
  static boolean is(Context context) {
    if (car < 0) {
      boolean automotive = false;
      try {
        automotive = context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_AUTOMOTIVE);
      } catch (RuntimeException ignored) {
        // no package manager (the unit tests' plain contexts)
      }
      car = automotive ? 1 : 0;
    }
    return car == 1;
  }

  /**
   * In a car: the bars hidden with the newer call as well (Android 11 and later), and the window's content
   * kept clear of the bars that stay. Called with the window's other settings (onCreate, a dialog's onStart);
   * the content follows when the bars come or go.
   */
  static void fit(Context context, Window window) {
    if (window == null || !is(context)) return;
    hide(window);
    final View content = window.getDecorView().findViewById(android.R.id.content);
    if (content == null) return;
    content.setOnApplyWindowInsetsListener((v, insets) -> {
      final int[] clear = visibleBars(insets);
      if (v.getPaddingLeft() != clear[0] || v.getPaddingTop() != clear[1] || v.getPaddingRight() != clear[2]
          || v.getPaddingBottom() != clear[3]) {
        v.setPadding(clear[0], clear[1], clear[2], clear[3]);
        android.util.Log.i(TAG, "car: the game keeps clear of the system bars (left " + clear[0] + ", top " + clear[1]
            + ", right " + clear[2] + ", bottom " + clear[3] + " pixels)");
      }
      return insets;
    });
    content.requestApplyInsets();
  }

  /** In a car, after the window got the focus back (a panel of the car closed): the bars hidden again. */
  static void refocus(Context context, Window window) {
    if (window != null && is(context)) hide(window);
  }

  private static void hide(Window window) {
    if (Build.VERSION.SDK_INT < 30) return;  // the flags of setSystemUiVisibility do it there
    final WindowInsetsController controller = window.getInsetsController();
    if (controller == null) return;
    controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
    controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
  }

  /** The bars that are on the screen now (left, top, right, bottom); hidden ones count 0. */
  static int[] visibleBars(WindowInsets insets) {
    if (Build.VERSION.SDK_INT >= 30) {
      final android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
      return new int[] {bars.left, bars.top, bars.right, bars.bottom};
    }
    return new int[] {insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom()};
  }
}
