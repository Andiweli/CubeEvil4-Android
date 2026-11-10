package com.ast.re4decomp;

import com.ast.retrotouch.RetroTouchButtonShape;
import com.ast.retrotouch.RetroTouchButtonStyle;
import com.ast.retrotouch.RetroTouchColors;
import com.ast.retrotouch.RetroTouchIcon;

/**
 * The look of the touch controls in RetroTouch's two button styles (RetroTouch 1.0.0-beta.10, wish of
 * the project owner, 09.11.2026; named "RE" for ICONS and "GC" for LABELS in the editor and the app menu,
 * his wish of 10.11.2026), all in one place: the style a new player gets, the icons, the shapes
 * and the colours of the ICONS style. The LABELS style is the look of 0.99.6 and earlier, unchanged:
 * the GameCube's colours (CoreTouch.COLOURS) and RetroTouch's shapes chosen from the labels.
 *
 * Shapes and colours count for both styles in RetroTouch, so TouchControls.applyStyle sets the ones
 * below only while ICONS is shown and takes them back for LABELS. Positions and sizes are those of the
 * default layout (CoreTouch.LAYOUT) in both styles; the player's saved layout keeps its own.
 *
 * The icons follow what a button does on the touch controls (CoreTouch.gcButton: a GameCube button
 * each). The menu's "Alternate Controls" change the controller's keys only (CoreInput: "the touch
 * controls keep their GameCube buttons"), so the icons stay as they are with them too.
 */
final class TouchStyle {
  private TouchStyle() {}

  /** The style while the player has chosen none (in the editor or the app menu). */
  static final RetroTouchButtonStyle DEFAULT_BUTTON_STYLE = RetroTouchButtonStyle.ICONS;

  // the ICONS style's colours (the project owner's start values, 09.11.2026); RetroTouch takes the
  // transparency from each control's opacity (the editor's ALPHA), not from these colours
  static final int FILL = 0xFF2E2B25, BORDER = 0xFFB58B55, ICON = 0xFFF0DFB8;
  static final int PRESSED_FILL = 0xFF4A4338, PRESSED_BORDER = 0xFFD9A86A, PRESSED_ICON = 0xFFFFF1D0;
  // a latched L, R or B: RetroTouch adds the outer ring in the border's colour
  static final int LATCHED_FILL = 0xFF3A3326, LATCHED_BORDER = 0xFFE8C07A, LATCHED_ICON = 0xFFFFF1D0;

  /**
   * Every button, the D-pad and the sticks in the ICONS style. The border is RetroTouch's own width (7 %
   * of the shorter side).
   */
  static final RetroTouchColors COLOURS = new RetroTouchColors(FILL, PRESSED_FILL, BORDER, ICON)
      .withPressedColors(PRESSED_FILL, PRESSED_BORDER, PRESSED_ICON)
      .withLatchedColors(LATCHED_FILL, LATCHED_BORDER, LATCHED_ICON);

  /** Width over height of L and R as rounded rectangles (where the screen has room, see shoulderAspect). */
  static final float SHOULDER_ASPECT = 1.95f;
  /** Width over height of MENU and START as capsules. */
  static final float CAPSULE_ASPECT = 2.1f;

  /**
   * The icon of an action, null for none (R3 and the C stick keep their labels). A: interact, confirm, fire
   * (with R held); B: run, cancel; X: Ashley (wait / follow); Y: the inventory (attache case); Z: the map;
   * L: ready the knife; R: ready the weapon (aim); START: the game's menu; MENU: the app's menu (SELECT).
   */
  static RetroTouchIcon icon(String action) {
    switch (action) {
      case CoreTouch.A: return RetroTouchIcon.HAND;
      case CoreTouch.B: return RetroTouchIcon.FOOTPRINTS;
      case CoreTouch.X: return RetroTouchIcon.USER;
      case CoreTouch.Y: return RetroTouchIcon.BRIEFCASE;
      case CoreTouch.Z: return RetroTouchIcon.MAP;
      case CoreTouch.L: return RetroTouchIcon.KNIFE_HORIZONTAL;
      case CoreTouch.R: return RetroTouchIcon.CROSSHAIR;
      case CoreTouch.START: return RetroTouchIcon.CIRCLE_PAUSE;
      case CoreTouch.MENU: return RetroTouchIcon.MENU;
      default: return null;
    }
  }

  /** The shape of an action's buttons in the ICONS style; AUTO (RetroTouch's choice by the label: round) for the others. */
  static RetroTouchButtonShape iconShape(String action) {
    switch (action) {
      case CoreTouch.L:
      case CoreTouch.R: return RetroTouchButtonShape.ROUNDED_RECT;
      case CoreTouch.START:
      case CoreTouch.MENU: return RetroTouchButtonShape.CAPSULE;
      default: return RetroTouchButtonShape.AUTO;
    }
  }

  /** Width over height of an action's button of the default layout (1 where the shape has no aspect). */
  static float aspect(String action, float shoulder) {
    switch (action) {
      case CoreTouch.L:
      case CoreTouch.R: return shoulder;
      case CoreTouch.START:
      case CoreTouch.MENU: return CAPSULE_ASPECT;
      default: return 1f;
    }
  }

  /**
   * L's and R's aspect on a screen of width x height pixels (`density` pixels a dp): SHOULDER_ASPECT where
   * R of the default layout (CoreTouch.LAYOUT), the outer ring of a latched R included, stays left of
   * RetroTouch's gear at the top right, else as
   * wide as fits there, but never narrower than 1.2. Narrow screens (4:3) need it: R is centred at 84 % of
   * the width and the gear is a square of 0.105 of the shorter side (38..52 dp) times 0.75, 10 dp from the
   * edges (RetroTouchView.gearRect with its default place and size).
   */
  static float shoulderAspect(int width, int height, float density) {
    if (width <= 0 || height <= 0 || density <= 0) return SHOULDER_ASPECT;
    float shorter = Math.min(width, height);
    float gear = Math.max(38f * density, Math.min(52f * density, shorter * 0.105f)) * 0.75f;
    float gearLeft = width - 10f * density - gear;
    CoreTouch.Control r = null;
    for (CoreTouch.Control c : CoreTouch.LAYOUT) if (CoreTouch.R.equals(c.action)) r = c;
    if (r == null) return SHOULDER_ASPECT;
    float tall = Math.min(r.width * width, r.height * height);  // its height (RetroTouch: the size times the shorter side)
    // a latched button's outer ring: 20 % of the half height away, at least 4 dp (RetroTouchView.drawIconButton), and 2 dp air
    float ring = Math.max(tall * 0.1f, 4f * density) + 2f * density;
    float half = gearLeft - ring - r.x * width;  // the half width R may take
    float aspect = 2f * half / tall;
    // in steps of 0.05, so the layout is built again only when the room really changed
    aspect = (float) Math.floor(aspect * 20f) / 20f;
    return Math.max(1.2f, Math.min(SHOULDER_ASPECT, aspect));
  }
}
