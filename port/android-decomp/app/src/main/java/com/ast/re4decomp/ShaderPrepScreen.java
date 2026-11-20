package com.ast.re4decomp;

import android.content.Context;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.util.Locale;

/**
 * "Prepare shaders at start" (1.1.0, the project owner's design of 20.11.2026: an optional switch under Management next
 * to "Clear shader cache"): while the shaders are made ahead (GLES: the programs of the app's list and the device's own
 * cache, WarmProgramCache; Vulkan: the app's pipeline list, AheadThread) this screen covers the picture: a title, a bar,
 * "n / total", one line. The game stands meanwhile (CoreActivity). Nothing skips it (the project owner, 20.11.2026: "nicht
 * mit einem Tastendruck oder Touch-Berührung abbrechen"): it stays until the making ahead is through (or has not begun
 * after 20 s); buttons, keys, taps, the mouse and Back are taken and dropped meanwhile (CoreActivity).
 */
final class ShaderPrepScreen {
  private final FrameLayout root;
  private final ProgressBar bar;
  private final TextView count;

  ShaderPrepScreen(Context c) {
    root = new FrameLayout(c);
    root.setBackgroundColor(Color.BLACK);
    root.setClickable(true);
    float d = c.getResources().getDisplayMetrics().density;
    LinearLayout column = new LinearLayout(c);
    column.setOrientation(LinearLayout.VERTICAL);
    column.setGravity(Gravity.CENTER_HORIZONTAL);
    TextView title = new TextView(c);
    title.setText(R.string.ingame_shader_prep_title);
    title.setTextColor(Color.WHITE);
    title.setTextSize(22);
    title.setGravity(Gravity.CENTER);
    column.addView(title);
    bar = new ProgressBar(c, null, android.R.attr.progressBarStyleHorizontal);
    bar.setMax(1000);
    bar.setIndeterminate(true);
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(Math.round(420 * d), Math.round(18 * d));
    lp.topMargin = Math.round(18 * d);
    column.addView(bar, lp);
    count = new TextView(c);
    count.setTextColor(0xFFE0E0E0);
    count.setTextSize(16);
    count.setGravity(Gravity.CENTER);
    count.setPadding(0, Math.round(10 * d), 0, 0);
    column.addView(count);
    TextView line = new TextView(c);
    line.setText(R.string.ingame_shader_prep_line);
    line.setTextColor(0xFFB0B0B0);
    line.setTextSize(14);
    line.setGravity(Gravity.CENTER);
    line.setPadding(Math.round(24 * d), Math.round(18 * d), Math.round(24 * d), 0);
    column.addView(line);
    root.addView(column, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER));
  }

  View view() { return root; }

  /** The progress: `total` 0 while nothing is known yet. */
  void update(int done, int total) {
    if (total <= 0) {
      bar.setIndeterminate(true);
      count.setText("");
      return;
    }
    bar.setIndeterminate(false);
    bar.setProgress((int) Math.min(1000L, done * 1000L / total));
    count.setText(String.format(Locale.getDefault(), "%d / %d", Math.min(done, total), total));
  }
}
