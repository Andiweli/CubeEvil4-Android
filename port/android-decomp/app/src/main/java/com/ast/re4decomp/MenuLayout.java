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

  /** Equal-width buttons; long translations reduce the column count before their text wraps. */
  static final class Buttons extends GridLayout {
    private final int maxColumns;
    private int measuredColumns;

    Buttons(Context context, int maxColumns) {
      super(context);
      this.maxColumns = maxColumns;
      setAlignmentMode(ALIGN_MARGINS);
    }

    void addButton(View button) {
      LayoutParams lp = new LayoutParams();
      lp.width = 0;
      lp.height = LayoutParams.WRAP_CONTENT;
      lp.setMargins(dp(getContext(), 3), dp(getContext(), 4), dp(getContext(), 3), dp(getContext(), 4));
      addView(button, lp);
    }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
      int available = Math.max(1, MeasureSpec.getSize(widthSpec) - getPaddingLeft() - getPaddingRight());
      int needed = dp(getContext(), 64);
      for (int i = 0; i < getChildCount(); i++) {
        View child = getChildAt(i);
        child.measure(MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED),
            MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
        needed = Math.max(needed, child.getMeasuredWidth() + dp(getContext(), 6));
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

  /** Two columns when there is room, otherwise a vertical stack. Negative fixedWidth fixes the end. */
  static final class Columns extends LinearLayout {
    private final int fixedWidth, minimumWidth;
    private final boolean emptyCaption;
    private boolean fillEndHeight;

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
