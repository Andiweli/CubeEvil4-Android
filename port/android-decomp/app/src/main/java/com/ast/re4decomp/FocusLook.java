package com.ast.re4decomp;

import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.widget.Button;

/**
 * The controller's focus in the app's own windows: one golden look on every phone (0.99.11, a user's report with a
 * Xiaomi on Android 11: moving through the app menu with a controller showed only a faint dark frame; the Retroid
 * showed the button golden).
 *
 * The cause: Android's dark mode for apps ("force dark"; MIUI turns it on for apps by itself) recolours what a window
 * draws as backgrounds. A colour lighter than L 55 (CIE Lab lightness, 0 black to 100 white) becomes L 110 - L, darker
 * ones stay. The menu's white focus frame (L 94) came out at L 16, almost the panel's colour: the user's picture shows
 * exactly that colour (37 36 30 there, the frame's colour recoloured so: 41 40 33, both pictures at 90 %).
 *
 * Now (1) the app's windows say no to it: the app's theme (`forceDarkAllowed` false, Android 10 and later) and each of
 * its dialogs' and the game's window (`View.setForceDarkAllowed(false)` on the window's top view, which holds for all
 * views in it); (2) the focus is a golden fill (L 44) with the menu's gold as a 2 dp edge: the fill is under L 55, which
 * that recolouring leaves as it is, should a phone not listen to the no (the edge would then only be darker). A
 * selected choice keeps its look (the dark olive fill, a 1 dp golden edge, golden text), so in a row of choices the
 * selected one and the focused one stay apart. Every button of the app menu, its pages, its questions, the
 * benchmark's popups and the game's messages has it, the coloured ones (update, benchmark, warning) too; the sliders
 * show it as the golden box around them.
 */
final class FocusLook {
  /** The focus: the golden fill (L 44, left alone by force dark; the menu's ink on it 4.8 : 1) and the edge, the menu's
   *  gold (FocusLookTest). */
  static final int FILL = 0xff7d661c, EDGE = 0xffd7bc7d;
  /** The edge's width and the corners as the menu's buttons. */
  static final int EDGE_DP = 2, RADIUS_DP = 6;
  /** The text on a focused button (the menu's ink). */
  static final int TEXT = 0xfff0eee5;

  private FocusLook() {}

  private static int dp(Context context, int n) {
    return Math.round(n * context.getResources().getDisplayMetrics().density);
  }

  /** The focused look of a button or a slider. */
  static GradientDrawable focused(Context context) {
    GradientDrawable d = new GradientDrawable();
    d.setColor(FILL);
    d.setCornerRadius(dp(context, RADIUS_DP));
    d.setStroke(dp(context, EDGE_DP), EDGE);
    return d;
  }

  /** A button's text colours: the focus's text while focused, otherwise `others` (enabled and disabled as there). */
  static ColorStateList text(ColorStateList others) {
    int normal = others != null ? others.getDefaultColor() : TEXT;
    int disabled = others != null ? others.getColorForState(new int[] {-android.R.attr.state_enabled}, normal) : normal;
    return new ColorStateList(new int[][] {new int[] {-android.R.attr.state_enabled},
        new int[] {android.R.attr.state_focused}, new int[0]}, new int[] {disabled, TEXT, normal});
  }

  /** `window` and all views in it: no force dark (Android 10 and later; earlier phones have none). */
  static void noForceDark(Window window) {
    if (window == null || Build.VERSION.SDK_INT < 29) return;
    window.getDecorView().setForceDarkAllowed(false);
  }

  /** Before `dialog` shows: no force dark in its window. */
  static void beforeShow(Dialog dialog) { noForceDark(dialog.getWindow()); }

  /** After `dialog` showed: Android's own buttons of an alert get the focus look (their look otherwise unchanged). */
  static void afterShow(Dialog dialog) {
    if (!(dialog instanceof AlertDialog)) return;
    AlertDialog alert = (AlertDialog) dialog;
    for (int which : new int[] {AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEGATIVE, AlertDialog.BUTTON_NEUTRAL}) {
      Button b = alert.getButton(which);
      if (b != null) button(b);
    }
  }

  /** One of Android's buttons: the focus look over its own background (kept for all other states), the focus's text. */
  static void button(Button b) {
    if (over(b)) b.setTextColor(text(b.getTextColors()));
  }

  /** `v` (a button or a slider): the focus look over its own background, which stays for all other states; false if it
   *  had it already. */
  static boolean over(View v) {
    Drawable own = v.getBackground();
    if (own instanceof Marked) return false;
    Marked look = new Marked();
    look.addState(new int[] {android.R.attr.state_focused}, focused(v.getContext()));
    look.addState(new int[0], own != null ? own : new GradientDrawable());
    v.setBackground(look);
    return true;
  }

  /** The focus look this class put on a button (so it is not wrapped twice). */
  private static final class Marked extends StateListDrawable {}
}
