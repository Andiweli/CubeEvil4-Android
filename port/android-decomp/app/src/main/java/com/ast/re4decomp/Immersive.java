package com.ast.re4decomp;

import android.app.Dialog;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

/**
 * The game's full screen in the app's own windows over it: the app menu with its pages, its questions and the
 * messages over the game (wish of the project owner, 11.11.2026, a Samsung in landscape: the navigation bar and
 * the status bar came with the app menu and went with it).
 *
 * A dialog is a window of its own, and Android shows the system bars for the window that has the focus unless that
 * window asks to hide them as the game's window does (CoreActivity.hideSystemUi). Here every such window asks the
 * same: the bars hidden, a swipe from the edge shows them for a moment (sticky). The window is not focusable while
 * it appears, so the bars do not flash in, and focusable right after for the keys and the controller. Whenever it
 * gets the focus back (a question over it closed, the notification shade went up) it asks again.
 *
 * Only the bars, not the layout: the windows keep their size and place (the menu inside the game's area, which a
 * car's bars that stay keep clear, CarDisplay).
 */
final class Immersive {
  /** The bars hidden, back after a swipe for a moment only. */
  static final int FLAGS = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
      | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;

  private Immersive() {}

  /** `window` asks for the bars hidden (before it shows, after it showed, when it gets the focus back). */
  static void apply(Window window) {
    if (window == null) return;
    View decor = window.getDecorView();
    if ((decor.getSystemUiVisibility() & FLAGS) != FLAGS) decor.setSystemUiVisibility(decor.getSystemUiVisibility() | FLAGS);
    if (Build.VERSION.SDK_INT >= 30) {
      WindowInsetsController controller = window.getInsetsController();
      if (controller != null) {
        controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
      }
    }
  }

  /** Shows `dialog` in the game's full screen (instead of dialog.show()) and with the app's focus look (FocusLook:
   *  no force dark, Android's own buttons of an alert golden while focused); returns it. */
  static <T extends Dialog> T show(T dialog) {
    final Window window = dialog.getWindow();
    if (window == null) { dialog.show(); FocusLook.afterShow(dialog); return dialog; }
    FocusLook.beforeShow(dialog);
    window.setFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
    apply(window);
    window.getDecorView().getViewTreeObserver().addOnWindowFocusChangeListener(focus -> { if (focus) apply(window); });
    dialog.show();
    FocusLook.afterShow(dialog);
    apply(window);
    window.clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
    return dialog;
  }
}
