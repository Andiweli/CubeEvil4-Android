package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/** CornerInset: the FPS display stays inside a rounded screen corner. */
public class CornerInsetTest {
  @Test
  public void noCornerKeepsTheMargin() {
    assertEquals(12, CornerInset.inset(0, 0, 1f));
    assertEquals(33, CornerInset.inset(0, 0, 2.75f));
  }

  @Test
  public void edgeAddsItsWidth() {
    assertEquals(80 + 33, CornerInset.inset(80, 0, 2.75f));
  }

  @Test
  public void boxCornerLiesInsideTheArc() {
    // radii of phones with rounded corners (pixels), densities from 2 to 3.5
    for (int r : new int[] {40, 90, 120, 160, 220}) {
      for (float density : new float[] {2f, 2.75f, 3.5f}) {
        int d = CornerInset.inset(0, r, density);
        // the box's corner (d, d) from the screen's corner: inside the circle of radius r around (r, r)
        double dist = Math.hypot(r - d, r - d);
        assertTrue("r " + r + " d " + d, d >= r || dist <= r);
        assertTrue(d >= Math.round(CornerInset.MIN_DP * density));
      }
    }
  }
}
