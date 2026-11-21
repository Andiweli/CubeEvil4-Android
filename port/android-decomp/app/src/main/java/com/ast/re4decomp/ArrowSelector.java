package com.ast.re4decomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.widget.Button;

/**
 * A choice of more than two values in one cell (1.1.0, the project owner's design of 21.11.2026: "Das neue Screendesign
 * passt sehr gut"): ‹ on the left and › on the right, each beyond a thin line, the choice's name small on top, its
 * value big below and a dot for each step under it, the current one light. A tap on ‹ goes back, on › or the middle
 * forward, after the last step back to the first. With a controller or a keyboard the cell takes the focus as any
 * button (the golden frame) and the D-pad's left and right change the value, up and down move on as before. The
 * value is golden while it is not the default. Steps that are not allowed (the graphics chip's limits) are passed
 * over; a selector without any other allowed step does not change. A Button, so it sits in the menu's grids, takes
 * their look and focus and is found by its text as the buttons were.
 */
final class ArrowSelector extends Button {
  interface Listener { void changed(int index); }

  private static final int LINE = 0xff414b41, DOT = 0xff5d675c;
  private final String name;
  private final String[] values;
  private final boolean[] allowed;
  private final int defaultIndex;
  private int index;
  private Listener listener;
  private float touchX = -1f;
  private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);

  /** `name` may be null or empty: then only the value and the dots (a choice under a heading of its own). */
  ArrowSelector(Context context, String name, String[] values, int index, int defaultIndex) {
    super(context);
    this.name = name == null ? "" : name;
    this.values = values.clone();
    this.allowed = new boolean[values.length];
    java.util.Arrays.fill(allowed, true);
    this.defaultIndex = Math.max(0, Math.min(values.length - 1, defaultIndex));
    this.index = Math.max(0, Math.min(values.length - 1, index));
  }

  /** After the menu's style (its padding and height): the arrows' room on both sides, the text in the middle. */
  void finishLook() {
    int arrows = arrowWidth();
    setPadding(arrows, dp(5), arrows, dp(13));
    setMinHeight(dp(name.isEmpty() ? 56 : 64));
    setMinimumHeight(dp(name.isEmpty() ? 56 : 64));
    setAllCaps(false);
    setOnClickListener(v -> { });  // (the step is performClick's; a listener as the menu's buttons have one)
    refresh();
  }

  void setListener(Listener listener) { this.listener = listener; }

  int index() { return index; }

  int count() { return values.length; }

  boolean isDefault() { return index == defaultIndex; }

  /** "Selected" as the menu's buttons were lit: a value other than the default (the look itself stays the cell's,
   *  only the value turns golden; the drawable state is not touched). */
  @Override public boolean isSelected() { return values != null && index != defaultIndex; }

  /** Which steps may be chosen (a step above the graphics chip's limit: not); the current one stays as it is. */
  void setAllowed(int step, boolean ok) {
    if (step >= 0 && step < allowed.length) allowed[step] = ok;
    invalidate();
  }

  /** Shows step `i` (no listener call): the menu set it back or from elsewhere. */
  void setIndex(int i) {
    if (i < 0 || i >= values.length) return;
    index = i;
    refresh();
  }

  /** One step back (-1) or forward (+1), over the steps not allowed, round after the last; false when it stays. */
  boolean step(int direction) {
    if (!isEnabled() || values.length < 2) return false;
    int at = index;
    for (int n = 0; n < values.length; n++) {
      at = (at + direction + values.length) % values.length;
      if (allowed[at]) break;
    }
    if (at == index || !allowed[at]) return false;
    index = at;
    refresh();
    if (listener != null) listener.changed(index);
    return true;
  }

  private void refresh() {
    SpannableStringBuilder t = new SpannableStringBuilder();
    if (!name.isEmpty()) {
      t.append(name);
      t.setSpan(new RelativeSizeSpan(.78f), 0, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      t.setSpan(new StyleSpan(Typeface.NORMAL), 0, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      t.setSpan(new ForegroundColorSpan(isEnabled() ? CoreMenu.MUTED : 0xff70796f), 0, t.length(),
          Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      t.append('\n');
    }
    int from = t.length();
    t.append(values[index]);
    t.setSpan(new StyleSpan(Typeface.BOLD), from, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    if (isEnabled() && index != defaultIndex) {
      t.setSpan(new ForegroundColorSpan(CoreMenu.GOLD), from, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    }
    setText(t);
    setContentDescription((name.isEmpty() ? "" : name + ", ") + values[index]);
    invalidate();
  }

  @Override public void setEnabled(boolean enabled) {
    super.setEnabled(enabled);
    if (values != null) refresh();
  }

  @Override public boolean onTouchEvent(MotionEvent event) {
    if (event.getActionMasked() == MotionEvent.ACTION_DOWN || event.getActionMasked() == MotionEvent.ACTION_UP) {
      touchX = event.getX();
    }
    return super.onTouchEvent(event);
  }

  /** A tap (or the A button, Enter): ‹ back, › and the middle forward. */
  @Override public boolean performClick() {
    boolean back = touchX >= 0f && touchX < arrowWidth();
    touchX = -1f;
    super.performClick();
    step(back ? -1 : 1);
    return true;
  }

  @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
    if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) { step(-1); return true; }
    if (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) { step(1); return true; }
    return super.onKeyDown(keyCode, event);
  }

  @Override public boolean onKeyUp(int keyCode, KeyEvent event) {
    if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT || keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) return true;
    return super.onKeyUp(keyCode, event);
  }

  @Override protected void onDraw(Canvas canvas) {
    super.onDraw(canvas);
    int w = getWidth(), h = getHeight(), a = arrowWidth();
    // the thin lines beside the arrows
    paint.setStyle(Paint.Style.FILL);
    paint.setColor(LINE);
    float line = Math.max(1f, dp(1));
    canvas.drawRect(a - line, dp(6), a, h - dp(6), paint);
    canvas.drawRect(w - a, dp(6), w - a + line, h - dp(6), paint);
    // the arrows
    boolean can = isEnabled() && canMove();
    paint.setColor(can ? CoreMenu.INK : 0xff70796f);
    paint.setTextSize(dp(26));
    paint.setTextAlign(Paint.Align.CENTER);
    paint.setTypeface(Typeface.DEFAULT_BOLD);
    Paint.FontMetrics fm = paint.getFontMetrics();
    float base = h / 2f - (fm.ascent + fm.descent) / 2f;
    canvas.drawText("‹", a / 2f, base, paint);
    canvas.drawText("›", w - a / 2f, base, paint);
    // the dots: one a step, the current one light
    float r = dp(2) + .5f, gap = dp(9);
    float y = h - dp(9);
    float x = w / 2f - (values.length - 1) * gap / 2f;
    for (int i = 0; i < values.length; i++, x += gap) {
      paint.setColor(i == index ? (isEnabled() ? CoreMenu.INK : 0xff8a9388) : DOT);
      canvas.drawCircle(x, y, r, paint);
    }
  }

  private boolean canMove() {
    for (int i = 0; i < allowed.length; i++) if (i != index && allowed[i]) return true;
    return false;
  }

  private int arrowWidth() { return dp(44); }

  private int dp(int n) { return Math.round(n * getResources().getDisplayMetrics().density); }

  // for the tests
  String valueText() { return values[index]; }
  String nameText() { return name; }
}
