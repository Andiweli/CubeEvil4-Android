package com.ast.re4decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * The warning of an unofficial build (OfficialBuild; the project owner, 16.11.2026: "als popup das man mit rotem button
 * bestätigen muss bei jedem start"): the dark red popup of the app menu's "Delete All Data" question, with one red
 * button. It cannot be closed with Back, B or a tap beside it; touch, A or Start confirm it (the focus starts on the
 * button where the screen is not in touch mode). Texts: strings_ingame.xml (the phone's language, English as the
 * fallback).
 */
final class UnofficialWarning {
  private UnofficialWarning() {}

  /** Shows the warning over `activity`; `confirmed` runs once the player confirmed it (OfficialBuild.confirm). */
  static AlertDialog show(Activity activity, Runnable confirmed) {
    float density = activity.getResources().getDisplayMetrics().density;
    LinearLayout box = new LinearLayout(activity);
    box.setOrientation(LinearLayout.VERTICAL);
    box.setPadding(dp(density, 24), dp(density, 20), dp(density, 24), dp(density, 18));
    TextView title = new TextView(activity);
    title.setText(R.string.ingame_unofficial_title);
    title.setTextSize(20); title.setTypeface(Typeface.DEFAULT, Typeface.BOLD); title.setTextColor(CoreMenu.INK);
    title.setGravity(Gravity.CENTER); title.setTextAlignment(View.TEXT_ALIGNMENT_GRAVITY);
    box.addView(title, new LinearLayout.LayoutParams(-1, -2));
    TextView message = new TextView(activity);
    message.setText(R.string.ingame_unofficial_text);
    message.setTextSize(16); message.setTextColor(CoreMenu.INK); message.setLineSpacing(0, 1.1f);
    message.setGravity(Gravity.CENTER_HORIZONTAL); message.setTextAlignment(View.TEXT_ALIGNMENT_GRAVITY);
    ScrollView scroller = new ScrollView(activity);
    scroller.addView(message, new ViewGroup.LayoutParams(-1, -2));
    LinearLayout.LayoutParams between = new LinearLayout.LayoutParams(-1, -2, 1);
    between.setMargins(0, dp(density, 12), 0, dp(density, 18));
    box.addView(scroller, between);
    final AlertDialog[] shown = new AlertDialog[1];
    Button ok = new Button(activity);
    ok.setText(R.string.ingame_unofficial_ok);
    ok.setAllCaps(false); ok.setTextSize(16); ok.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    ok.setMinHeight(dp(density, 48)); ok.setMinimumHeight(dp(density, 48));
    ok.setPadding(dp(density, 24), dp(density, 8), dp(density, 24), dp(density, 8));
    StateListDrawable look = new StateListDrawable();
    look.addState(new int[] {android.R.attr.state_focused}, FocusLook.focused(activity));
    look.addState(new int[] {android.R.attr.state_pressed}, shape(density, CoreMenu.UPDATE_RED, CoreMenu.INK, 6));
    look.addState(new int[0], shape(density, CoreMenu.UPDATE_RED, CoreMenu.UPDATE_RED_EDGE, 6));
    ok.setBackground(look);
    ok.setTextColor(FocusLook.text(ColorStateList.valueOf(CoreMenu.INK)));
    ok.setOnClickListener(v -> {
      OfficialBuild.confirm(activity);
      shown[0].dismiss();
      if (confirmed != null) confirmed.run();
    });
    LinearLayout.LayoutParams button = new LinearLayout.LayoutParams(-2, -2);
    button.gravity = Gravity.CENTER_HORIZONTAL;
    box.addView(ok, button);
    AlertDialog dialog = new AlertDialog.Builder(activity).setView(box).setCancelable(false).create();
    shown[0] = dialog;
    dialog.setCanceledOnTouchOutside(false);
    GradientDrawable frame = shape(density, CoreMenu.WARN_RED, CoreMenu.UPDATE_RED_EDGE, 10);
    frame.setStroke(dp(density, 2), CoreMenu.UPDATE_RED_EDGE);
    dialog.getWindow().setBackgroundDrawable(frame);
    dialog.setOnKeyListener((d, key, event) -> {
      // A and Start: the one button, also while a touch screen still holds no focus (Android gives it with the first
      // key); Back, B and the other buttons: nothing
      if (key == KeyEvent.KEYCODE_BUTTON_A || key == KeyEvent.KEYCODE_BUTTON_START) {
        if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) ok.performClick();
        return true;
      }
      return key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_ESCAPE || KeyEvent.isGamepadButton(key);
    });
    dialog.setOnShowListener(d -> ok.requestFocus());
    FocusLook.beforeShow(dialog);
    dialog.show();
    return dialog;
  }

  private static int dp(float density, int n) {
    return Math.round(n * density);
  }

  private static GradientDrawable shape(float density, int fill, int stroke, int radius) {
    GradientDrawable d = new GradientDrawable();
    d.setColor(fill);
    d.setCornerRadius(dp(density, radius));
    d.setStroke(dp(density, 1), stroke);
    return d;
  }
}
