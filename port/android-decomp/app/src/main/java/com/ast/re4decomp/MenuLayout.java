package com.ast.re4decomp;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.widget.GridLayout;
import android.widget.LinearLayout;

/** Menu rows respond to their measured space, including split windows and larger system text. */
final class MenuLayout {
  private MenuLayout() {}

  private static int dp(Context context, float value) {
    return Math.round(value * context.getResources().getDisplayMetrics().density);
  }

  /**
   * Equal-width buttons; long translations reduce the column count before their text needs more than
   * `lines` lines.
   */
  /**
   * Half the gap between two rows of buttons, in dp: each button's margin above and below (wish of the
   * project owner, 30.10.2026: one gap everywhere, as wide as around the rows with a slider, 14 dp).
   */
  static final int GAP = 7;

  static final class Buttons extends GridLayout {
    private final int maxColumns, lines;
    private int measuredColumns;

    Buttons(Context context, int maxColumns) {
      this(context, maxColumns, 1);
    }

    Buttons(Context context, int maxColumns, int lines) {
      super(context);
      this.maxColumns = maxColumns;
      this.lines = lines;
      setAlignmentMode(ALIGN_MARGINS);
    }

    void addButton(View button) {
      LayoutParams lp = new LayoutParams();
      lp.width = 0;
      lp.height = LayoutParams.WRAP_CONTENT;
      lp.setMargins(dp(getContext(), 3), dp(getContext(), GAP), dp(getContext(), 3), dp(getContext(), GAP));
      addView(button, lp);
    }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
      int available = Math.max(1, MeasureSpec.getSize(widthSpec) - getPaddingLeft() - getPaddingRight());
      int needed = dp(getContext(), 64);
      for (int i = 0; i < getChildCount(); i++) {
        View child = getChildAt(i);
        child.measure(MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED),
            MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
        int padding = child.getPaddingLeft() + child.getPaddingRight();
        int text = child.getMeasuredWidth() - padding;
        needed = Math.max(needed, (text + lines - 1) / lines + padding + dp(getContext(), 6));
      }
      int columns = Math.max(1, Math.min(Math.min(maxColumns, getChildCount()), available / needed));
      // Clear auto-assigned spans before changing column count (also supports resizing back up).
      if (measuredColumns != columns) {
        setColumnCount(UNDEFINED);
        setRowCount(UNDEFINED);
        for (int i = 0; i < getChildCount(); i++) {
          LayoutParams lp = (LayoutParams) getChildAt(i).getLayoutParams();
          lp.columnSpec = spec(i % columns, FILL, 1f);
          lp.rowSpec = spec(i / columns, FILL);
          getChildAt(i).setLayoutParams(lp);
        }
        setColumnCount(columns);
        setRowCount((getChildCount() + columns - 1) / columns);
        measuredColumns = columns;
      }
      super.onMeasure(widthSpec, heightSpec);
    }
  }

  /**
   * Buttons as a piano (the resolution, wish of the project owner, 27.10.2026): the first `whites`
   * children in a row of equal width, the others below it, each one as wide and centred on the line
   * between white `firstGap + k` and the next (black keys). Too narrow for a row of whites: all of
   * them in rows of equal-width cells, in the order `order` gives (child indices).
   */
  static final class Keys extends ViewGroup {
    private final int whites, firstGap;
    private final int[] order;
    private int columns, cell, rowHeight;

    Keys(Context context, int whites, int firstGap, int[] order) {
      super(context);
      this.whites = whites;
      this.firstGap = firstGap;
      this.order = order;
    }

    void addButton(View button) {
      MarginLayoutParams lp = new MarginLayoutParams(0, LayoutParams.WRAP_CONTENT);
      lp.setMargins(dp(getContext(), 3), dp(getContext(), GAP), dp(getContext(), 3), dp(getContext(), GAP));
      addView(button, lp);
    }

    private boolean piano() { return columns == whites; }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
      int width = Math.max(1, MeasureSpec.getSize(widthSpec) - getPaddingLeft() - getPaddingRight());
      int margins = dp(getContext(), 6);
      int needed = dp(getContext(), 64);
      for (int i = 0; i < getChildCount(); i++) {
        View child = getChildAt(i);
        child.measure(MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED),
            MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
        needed = Math.max(needed, child.getMeasuredWidth() + margins);
      }
      columns = Math.max(1, Math.min(whites, width / needed));
      cell = width / columns;
      rowHeight = 0;
      for (int i = 0; i < getChildCount(); i++) {
        View child = getChildAt(i);
        child.measure(MeasureSpec.makeMeasureSpec(Math.max(0, cell - margins), MeasureSpec.EXACTLY),
            MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
        rowHeight = Math.max(rowHeight, child.getMeasuredHeight() + dp(getContext(), 2 * GAP));
      }
      for (int i = 0; i < getChildCount(); i++) {  // one height for all, as the grid's rows
        View child = getChildAt(i);
        child.measure(MeasureSpec.makeMeasureSpec(Math.max(0, cell - margins), MeasureSpec.EXACTLY),
            MeasureSpec.makeMeasureSpec(rowHeight - dp(getContext(), 2 * GAP), MeasureSpec.EXACTLY));
      }
      int rows = piano() ? (getChildCount() > whites ? 2 : 1) : (getChildCount() + columns - 1) / columns;
      setMeasuredDimension(MeasureSpec.getSize(widthSpec), rows * rowHeight + getPaddingTop() + getPaddingBottom());
    }

    @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
      int left = getPaddingLeft(), top = getPaddingTop(), side = dp(getContext(), 3), up = dp(getContext(), GAP);
      for (int i = 0; i < getChildCount(); i++) {
        int x, y;
        if (piano()) {
          // a black key's left edge half a cell before the line it is centred on
          x = i < whites ? i * cell : (firstGap + i - whites + 1) * cell - cell / 2;
          y = i < whites ? 0 : rowHeight;
        } else {
          int at = 0;
          while (at < order.length && order[at] != i) at++;
          x = (at % columns) * cell;
          y = (at / columns) * rowHeight;
        }
        View child = getChildAt(i);
        child.layout(left + x + side, top + y + up, left + x + side + child.getMeasuredWidth(),
            top + y + up + child.getMeasuredHeight());
      }
    }
  }

  /** Two columns when there is room, otherwise a vertical stack. Negative fixedWidth fixes the end. */
  static final class Columns extends LinearLayout {
    private final int fixedWidth, minimumWidth;
    private final boolean emptyCaption;
    private boolean fillEndHeight;
    private float startWeight, endWeight;

    Columns(Context context, int fixedWidth, int minimumWidth, boolean emptyCaption) {
      super(context);
      this.fixedWidth = fixedWidth;
      this.minimumWidth = minimumWidth;
      this.emptyCaption = emptyCaption;
      setBaselineAligned(false);
    }

    /** In two-column mode the end column fills the height set by the taller content. */
    void setFillEndHeight(boolean fill) {
      fillEndHeight = fill;
      requestLayout();
    }

    /** In two-column mode the columns share the width start : end instead of a fixed start column. */
    void setWeights(float start, float end) {
      startWeight = start;
      endWeight = end;
      requestLayout();
    }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
      float textScale = Math.max(1f, getResources().getConfiguration().fontScale);
      boolean sideBySide = MeasureSpec.getSize(widthSpec) >= dp(getContext(), minimumWidth * textScale);
      setOrientation(sideBySide ? HORIZONTAL : VERTICAL);
      if (emptyCaption && getChildCount() > 0) getChildAt(0).setVisibility(sideBySide ? VISIBLE : GONE);
      for (int i = 0; i < getChildCount(); i++) {
        View child = getChildAt(i);
        LayoutParams lp = (LayoutParams) child.getLayoutParams();
        boolean fixed = fixedWidth > 0 ? i == 0 : fixedWidth < 0 && i == getChildCount() - 1;
        lp.width = sideBySide ? (fixed ? dp(getContext(), Math.abs(fixedWidth) * textScale) : 0)
            : LayoutParams.MATCH_PARENT;
        lp.height = sideBySide && fillEndHeight && i == getChildCount() - 1
            ? LayoutParams.MATCH_PARENT : LayoutParams.WRAP_CONTENT;
        lp.weight = sideBySide && !fixed ? 1 : 0;
        if (sideBySide && startWeight > 0) {
          lp.width = 0;
          lp.weight = i == 0 ? startWeight : endWeight;
        }
      }
      super.onMeasure(widthSpec, heightSpec);
    }
  }

  /** Back / title / resume in one row; narrow windows put the title above the two actions. */
  static final class Header extends ViewGroup {
    private boolean horizontal;
    private final View back, title, resume;

    Header(Context context, View back, View title, View resume) {
      super(context);
      this.back = back;
      this.title = title;
      this.resume = resume;
      addView(back); addView(title); addView(resume);
    }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
      int width = MeasureSpec.getSize(widthSpec), gap = dp(getContext(), 16);
      float textScale = Math.max(1f, getResources().getConfiguration().fontScale);
      horizontal = width >= dp(getContext(), 680 * textScale);
      int actionsWidth = Math.max(1, (width - gap) / 2);
      int actionSpec = MeasureSpec.makeMeasureSpec(actionsWidth, MeasureSpec.AT_MOST);
      int freeHeight = MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED);
      if (back.getVisibility() != GONE) back.measure(actionSpec, freeHeight);
      resume.measure(actionSpec, freeHeight);
      int backWidth = back.getVisibility() == GONE ? 0 : back.getMeasuredWidth() + gap;
      int titleWidth = horizontal ? Math.max(1, width - backWidth - resume.getMeasuredWidth() - gap) : width;
      title.measure(MeasureSpec.makeMeasureSpec(titleWidth, MeasureSpec.EXACTLY), freeHeight);
      int actionsHeight = Math.max(back.getVisibility() == GONE ? 0 : back.getMeasuredHeight(), resume.getMeasuredHeight());
      int height = horizontal ? Math.max(title.getMeasuredHeight(), actionsHeight)
          : title.getMeasuredHeight() + dp(getContext(), 8) + actionsHeight;
      setMeasuredDimension(width, resolveSize(height, heightSpec));
    }

    private void place(View child, int start, int top) {
      int left = getLayoutDirection() == LAYOUT_DIRECTION_RTL ? getWidth() - start - child.getMeasuredWidth() : start;
      child.layout(left, top, left + child.getMeasuredWidth(), top + child.getMeasuredHeight());
    }

    @Override protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
      int titleStart = back.getVisibility() == GONE ? 0 : back.getMeasuredWidth() + dp(getContext(), 16);
      int actionsTop = horizontal ? 0 : title.getMeasuredHeight() + dp(getContext(), 8);
      if (back.getVisibility() != GONE) place(back, 0, horizontal ? (getHeight() - back.getMeasuredHeight()) / 2 : actionsTop);
      place(title, horizontal ? titleStart : 0, horizontal ? (getHeight() - title.getMeasuredHeight()) / 2 : 0);
      place(resume, getWidth() - resume.getMeasuredWidth(), horizontal ? (getHeight() - resume.getMeasuredHeight()) / 2 : actionsTop);
    }
  }
}
