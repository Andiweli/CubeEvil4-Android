package com.ast.re4decomp;

import android.os.Build;
import android.view.DisplayCutout;
import android.view.RoundedCorner;
import android.view.WindowInsets;

/**
 * How far the FPS display keeps from the window's corner or edge (since 05.11.2026 at one of four places, the
 * menu's choice; before at the bottom left) (tester report, 05.11.2026: on screens with
 * rounded corners the corner cut off "30 FPS (Game)"). The display is a box flush in that corner; it stays whole
 * when its own corner lies inside the screen's rounded corner of radius r, i.e. at least r * (1 - 1/sqrt 2) from
 * both edges (the arc's point at 45 degrees), plus a little air. Screens that report no rounded corner (before
 * Android 12, or square ones) keep a fixed margin. A display cutout or a system bar that stays on that side adds
 * its width; in a car the window's content is already kept clear of the car's bars (CarDisplay), so they are
 * not added again. "Bottom left" is the window's own corner: in either landscape orientation the screen reports
 * the corner that is there.
 */
final class CornerInset {
  /** The margin when the screen reports no rounded corner (dp). */
  static final int MIN_DP = 12;
  /** The air between the box and the rounded corner's arc (dp). */
  static final int AIR_DP = 4;

  private CornerInset() {}

  /**
   * The distance (pixels) from one edge: `edgePx` what lies on that side (a cutout, a bar that stays), `radiusPx`
   * the radius of the screen's rounded corner there (0: none reported).
   */
  static int inset(int edgePx, int radiusPx, float density) {
    final int min = Math.round(MIN_DP * density);
    int corner = 0;
    if (radiusPx > 0) {
      corner = (int) Math.ceil(radiusPx * (1.0 - Math.sqrt(0.5))) + Math.round(AIR_DP * density);
    }
    return Math.max(0, edgePx) + Math.max(min, corner);
  }

  /**
   * The distance (pixels) of the FPS display from the window's left edge and from its bottom (or top) edge. In
   * the corners the rounded corner there counts; centred on that edge no rounded corner reaches (the left
   * distance is then of no use). Since 0.98.4 the display touches the top or bottom edge (wish of the project
   * owner, 05.11.2026): the second value is 0 in all four places; the distance from the left keeps the corner,
   * the cutout and the bars as before (a box of a text line at the edge stays inside the arc that far in).
   */
  static int[] place(WindowInsets insets, float density, boolean car, boolean top, boolean centre) {
    int left = 0;
    if (Build.VERSION.SDK_INT >= 28) {
      final DisplayCutout cutout = insets.getDisplayCutout();
      if (cutout != null) {
        left = cutout.getSafeInsetLeft();
      }
    }
    if (!car) {
      if (Build.VERSION.SDK_INT >= 30) {
        final android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
        left = Math.max(left, bars.left);
      } else {
        left = Math.max(left, insets.getSystemWindowInsetLeft());
      }
    }
    int radius = 0;
    if (Build.VERSION.SDK_INT >= 31) {
      final RoundedCorner corner = insets.getRoundedCorner(top ? RoundedCorner.POSITION_TOP_LEFT : RoundedCorner.POSITION_BOTTOM_LEFT);
      if (corner != null) radius = corner.getRadius();
    }
    return new int[] {inset(left, radius, density), 0};
  }

}
