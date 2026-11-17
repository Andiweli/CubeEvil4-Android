package com.ast.re4decomp;

import com.ast.retrotouch.RetroTouchButtonShape;
import com.ast.retrotouch.RetroTouchButtonStyle;
import com.ast.retrotouch.RetroTouchColors;
import com.ast.retrotouch.RetroTouchIcon;

/**
 * The four looks of the touch controls, all in one place (wish of the project owner, 17.11.2026; designs in
 * builds\retrotouch-styles): RE (symbols for what each button does, the default since 0.99.7), GC (the GameCube's
 * buttons; it replaces the GC look of 0.99.6 and earlier), PS2 and XBOX (those controllers' buttons, mapped as
 * CoreInput maps a controller: Z = R1/RB, L = L2/LT, R = R2/RT, MENU = SELECT/View).
 *
 * All four are drawn in RetroTouch's ICONS style (1.0.0-beta.12): each action's colours (normal, pressed, latched),
 * its icon or, without one, its label, its shape and width over height; the D-pad and the move stick take the
 * default colours. RetroTouch knows two styles only, so TouchControls keeps it in ICONS and the look is the app's own
 * setting (TouchLook); RetroTouch's LABELS style is no longer shown.
 *
 * Centring (the project owner, 17.11.2026: labels centred horizontally and vertically): RetroTouch centres a label
 * of one character by its ink, one of more characters by its width and the font's height (within about a pixel),
 * and an icon by its 24 x 24 box; the own icons below are drawn with their ink centred in that box.
 *
 * The icons and labels follow what a button does on the touch controls (CoreTouch.gcButton: a GameCube button
 * each); the menu's "Alternate Controls" change the controller's keys only, so the looks stay as they are with them.
 */
final class TouchStyle {
  private TouchStyle() {}

  /** RetroTouch's style: always ICONS, which draws all four looks. */
  static final RetroTouchButtonStyle DEFAULT_BUTTON_STYLE = RetroTouchButtonStyle.ICONS;

  /** The looks, in the order of the app menu and of the editor's style button. */
  static final int RE = 0, GC = 1, PS2 = 2, XBOX = 3, LOOKS = 4;
  /** Their names: the same in every language (the menu's buttons, the editor's style button). */
  static final String[] NAMES = {"RE", "GC", "PS2", "XBOX"};
  /** A new player's look. */
  static final int DEFAULT_LOOK = RE;

  /** The look after `look` (the editor's style button goes round). */
  static int next(int look) { return (look + 1) % LOOKS; }

  /** A look number as it is, else the default (a setting from elsewhere). */
  static int valid(int look) { return look >= 0 && look < LOOKS ? look : DEFAULT_LOOK; }

  // ---------------------------------------------------------------- RE (the project owner's start values, 09.11.2026)
  // RetroTouch takes the transparency from each control's opacity (the editor's ALPHA), not from these colours
  static final int FILL = 0xFF2E2B25, BORDER = 0xFFB58B55, ICON = 0xFFF0DFB8;
  static final int PRESSED_FILL = 0xFF4A4338, PRESSED_BORDER = 0xFFD9A86A, PRESSED_ICON = 0xFFFFF1D0;
  // a latched L, R or B: RetroTouch adds the outer ring in the border's colour
  static final int LATCHED_FILL = 0xFF3A3326, LATCHED_BORDER = 0xFFE8C07A, LATCHED_ICON = 0xFFFFF1D0;

  /**
   * Every button, the D-pad and the sticks of RE. The border is RetroTouch's own width (7 % of the shorter side).
   */
  static final RetroTouchColors COLOURS = colours(FILL, BORDER, ICON, PRESSED_FILL, PRESSED_BORDER, PRESSED_ICON,
      LATCHED_FILL, LATCHED_BORDER, LATCHED_ICON);

  static RetroTouchColors colours(int fill, int border, int text, int pressedFill, int pressedBorder, int pressedText,
                                  int latchedFill, int latchedBorder, int latchedText) {
    return new RetroTouchColors(fill, pressedFill, border, text)
        .withPressedColors(pressedFill, pressedBorder, pressedText)
        .withLatchedColors(latchedFill, latchedBorder, latchedText);
  }

  // ---------------------------------------------------------------- GC: the GameCube's colours; light letters on A, B
  // and Z, dark ones on the grey buttons (dark letters on A and B were hard to read through the controls' transparency
  // on the emulator, 17.11.2026)
  static final RetroTouchColors GC_GREEN = colours(0xFF1FAE73, 0xFF0A5A3A, 0xFFF2FFF8, 0xFF49D497, 0xFF0D7A4F, 0xFFFFFFFF,
      0xFF2DBF82, 0xFF0F8A59, 0xFFFFFFFF);
  static final RetroTouchColors GC_RED = colours(0xFFE34A3E, 0xFF771A13, 0xFFFFF1EF, 0xFFFF7468, 0xFF9A241B, 0xFFFFFFFF,
      0xFFF05A4E, 0xFFA8291F, 0xFFFFFFFF);
  static final RetroTouchColors GC_GREY = colours(0xFFCBCBD3, 0xFF5A5A66, 0xFF32323A, 0xFFEDEDF3, 0xFF74747F, 0xFF26262C,
      0xFFDCDCE3, 0xFF85858F, 0xFF26262C);
  static final RetroTouchColors GC_PURPLE = colours(0xFF7B6BC6, 0xFF3B2F78, 0xFFF3F0FF, 0xFF9D8FE6, 0xFF4D3F99, 0xFFFFFFFF,
      0xFF9D8FE6, 0xFF4D3F99, 0xFFFFFFFF);
  static final RetroTouchColors GC_DARK = colours(0xFF3C3C44, 0xFFBDBDC6, 0xFFEEEEF4, 0xFF55555F, 0xFFE0E0E8, 0xFFFFFFFF,
      0xFF484850, 0xFFF0F0F6, 0xFFFFFFFF);
  static final RetroTouchColors GC_YELLOW = colours(0xFFF2C21B, 0xFF7A5C00, 0xFF4A3800, 0xFFFFD84D, 0xFF9A7400, 0xFF3A2C00,
      0xFFFFD84D, 0xFF9A7400, 0xFF3A2C00);

  // ---------------------------------------------------------------- PS2: black buttons, the symbols in their colours
  private static RetroTouchColors ps2(int symbol, int pressedSymbol) {
    return colours(0xFF1D1E24, 0xFF737689, symbol, 0xFF33353F, 0xFFA5A8BC, pressedSymbol, 0xFF2A2C34, 0xFFC3C6D8,
        pressedSymbol);
  }
  static final RetroTouchColors PS2_BLUE = ps2(0xFF8DB6FF, 0xFFB9D1FF), PS2_RED = ps2(0xFFFF6E6E, 0xFFFF9E9E),
      PS2_PINK = ps2(0xFFFF8FD9, 0xFFFFB9E8), PS2_GREEN = ps2(0xFF52D9AA, 0xFF8AE9C8), PS2_TEXT = ps2(0xFFE7E8EF, 0xFFFFFFFF);
  static final RetroTouchColors PS2_DARK = colours(0xFF24252C, 0xFF737689, 0xFFE7E8EF, 0xFF383A44, 0xFFA5A8BC, 0xFFFFFFFF,
      0xFF2E3038, 0xFFC3C6D8, 0xFFFFFFFF);

  // ---------------------------------------------------------------- XBOX: coloured caps with a dark ring
  private static RetroTouchColors xbox(int fill, int pressedFill, int letter) {
    return colours(fill, 0xFF16171B, letter, pressedFill, 0xFF3A3C44, letter, pressedFill, 0xFF5A5D68, letter);
  }
  static final RetroTouchColors XBOX_GREEN = xbox(0xFF4DAE2E, 0xFF6FCB4E, 0xFFFFFFFF),
      XBOX_RED = xbox(0xFFD33B32, 0xFFF05A50, 0xFFFFFFFF), XBOX_BLUE = xbox(0xFF2E6AD8, 0xFF5089F0, 0xFFFFFFFF),
      XBOX_YELLOW = xbox(0xFFEDB80F, 0xFFFFD03D, 0xFF2B2200);
  static final RetroTouchColors XBOX_WHITE = colours(0xFFECECEF, 0xFF16171B, 0xFF16171B, 0xFFFFFFFF, 0xFF3A3C44, 0xFF000000,
      0xFFF6F6F8, 0xFF5A5D68, 0xFF000000);
  static final RetroTouchColors XBOX_BUMPER = colours(0xFF3A3C44, 0xFF16171B, 0xFFF2F2F5, 0xFF50535D, 0xFF3A3C44, 0xFFFFFFFF,
      0xFF454852, 0xFF6B6E79, 0xFFFFFFFF);
  static final RetroTouchColors XBOX_DARK = colours(0xFF2A2C32, 0xFF6B6E79, 0xFFF2F2F5, 0xFF3E414A, 0xFF9A9DA8, 0xFFFFFFFF,
      0xFF33363D, 0xFFB5B8C2, 0xFFFFFFFF);

  // ---------------------------------------------------------------- own icons (24 x 24, the ink centred in the box)
  static final RetroTouchIcon PLUS = RetroTouchIcon.fromPaths(24f, "M12 5v14", "M5 12h14");
  static final RetroTouchIcon MINUS = RetroTouchIcon.fromPaths(24f, "M5 12h14");
  static final RetroTouchIcon PS2_CROSS = RetroTouchIcon.fromPaths(24f, "M6.5 6.5l11 11", "M17.5 6.5l-11 11");
  static final RetroTouchIcon PS2_CIRCLE = RetroTouchIcon.fromPaths(24f, "M12 5.5a6.5 6.5 0 1 0 0 13a6.5 6.5 0 1 0 0-13Z");
  static final RetroTouchIcon PS2_SQUARE = RetroTouchIcon.fromPaths(24f, "M6.5 6.5h11v11h-11Z");
  // the triangle's box (5.74 .. 18.24) centred at 12
  static final RetroTouchIcon PS2_TRIANGLE = RetroTouchIcon.fromPaths(24f, "M12 5.74l7.2 12.5H4.8Z");
  // the Xbox's "view" key: two overlapping windows (no logo)
  static final RetroTouchIcon XBOX_VIEW = RetroTouchIcon.fromPaths(24f, "M4 9h10v10H4Z", "M8 9V5h12v10h-6");

  /** Width over height of L and R as rounded rectangles (where the screen has room, see shoulderAspect). */
  static final float SHOULDER_ASPECT = 1.95f;
  /** Width over height of MENU and START as capsules. */
  static final float CAPSULE_ASPECT = 2.1f;
  /** Width over height of Z in GC, PS2 and XBOX (a shoulder button there: Z, R1, RB). */
  static final float Z_ASPECT = 1.5f;

  /**
   * The icon of an action in RE, null for none (R3 and the C stick keep their labels). A: interact, confirm, fire
   * (with R held); B: run, cancel; X: Ashley (wait / follow); Y: the inventory (attache case); Z: the map;
   * L: ready the knife; R: ready the weapon (aim); START: the game's menu; MENU: the app's menu (SELECT). The aim
   * and knife sticks as R and L (RetroTouch 1.0.0-beta.12 draws added sticks with an icon in the ICONS style).
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
      case CoreTouch.AIM_STICK: return RetroTouchIcon.CROSSHAIR;
      case CoreTouch.KNIFE_STICK: return RetroTouchIcon.KNIFE_HORIZONTAL;
      default: return null;
    }
  }

  /** The icon of an action in a look, null for its label. */
  static RetroTouchIcon icon(int look, String action) {
    if (look == RE) return icon(action);
    switch (action) {
      case CoreTouch.AIM_STICK: return RetroTouchIcon.CROSSHAIR;
      case CoreTouch.KNIFE_STICK: return RetroTouchIcon.KNIFE_HORIZONTAL;
      case CoreTouch.ZOOM_IN: return PLUS;
      case CoreTouch.ZOOM_OUT: return MINUS;
      default: break;
    }
    if (look == GC) return CoreTouch.MENU.equals(action) ? RetroTouchIcon.MENU : null;
    if (look == PS2) {
      switch (action) {
        case CoreTouch.A: return PS2_CROSS;
        case CoreTouch.B: return PS2_CIRCLE;
        case CoreTouch.X: return PS2_SQUARE;
        case CoreTouch.Y: return PS2_TRIANGLE;
        default: return null;
      }
    }
    switch (action) {  // XBOX: START is its menu key (three lines), MENU its view key
      case CoreTouch.START: return RetroTouchIcon.MENU;
      case CoreTouch.MENU: return XBOX_VIEW;
      default: return null;
    }
  }

  /** The label of an action in a look (drawn where it has no icon, and the editor's ACTION shows it). */
  static String label(int look, String action) {
    if (look == PS2) {
      switch (action) {
        case CoreTouch.Z: return "R1";
        case CoreTouch.L: return "L2";
        case CoreTouch.R: return "R2";
        case CoreTouch.MENU: return "SELECT";
        case CoreTouch.C_STICK: return "R";
        default: break;
      }
    } else if (look == XBOX) {
      switch (action) {
        case CoreTouch.Z: return "RB";
        case CoreTouch.L: return "LT";
        case CoreTouch.R: return "RT";
        case CoreTouch.C_STICK: return "RS";
        default: break;
      }
    }
    return CoreTouch.label(action);
  }

  /** An action's colours in a look; null for the look's default colours (all of RE). */
  static RetroTouchColors colours(int look, String action) {
    switch (look) {
      case GC:
        switch (action) {
          case CoreTouch.A: return GC_GREEN;
          case CoreTouch.B: return GC_RED;
          case CoreTouch.Z: return GC_PURPLE;
          case CoreTouch.X: case CoreTouch.Y: case CoreTouch.L: case CoreTouch.R: case CoreTouch.START: return GC_GREY;
          case CoreTouch.C_STICK: return GC_YELLOW;
          default: return GC_DARK;
        }
      case PS2:
        switch (action) {
          case CoreTouch.A: return PS2_BLUE;
          case CoreTouch.B: return PS2_RED;
          case CoreTouch.X: return PS2_PINK;
          case CoreTouch.Y: return PS2_GREEN;
          case CoreTouch.C_STICK: case CoreTouch.AIM_STICK: case CoreTouch.KNIFE_STICK: return PS2_DARK;
          default: return PS2_TEXT;
        }
      case XBOX:
        switch (action) {
          case CoreTouch.A: return XBOX_GREEN;
          case CoreTouch.B: return XBOX_RED;
          case CoreTouch.X: return XBOX_BLUE;
          case CoreTouch.Y: return XBOX_YELLOW;
          case CoreTouch.L: case CoreTouch.R: return XBOX_WHITE;
          case CoreTouch.Z: return XBOX_BUMPER;
          default: return XBOX_DARK;
        }
      default:
        return null;
    }
  }

  /** The colours of a look's D-pad, move stick and every action without colours of its own. */
  static RetroTouchColors defaults(int look) {
    switch (look) {
      case GC: return GC_DARK;
      case PS2: return PS2_DARK;
      case XBOX: return XBOX_DARK;
      default: return COLOURS;
    }
  }

  /** The shape of an action's buttons in RE; AUTO (RetroTouch's choice by the label: round) for the others. */
  static RetroTouchButtonShape iconShape(String action) {
    switch (action) {
      case CoreTouch.L:
      case CoreTouch.R: return RetroTouchButtonShape.ROUNDED_RECT;
      case CoreTouch.START:
      case CoreTouch.MENU: return RetroTouchButtonShape.CAPSULE;
      default: return RetroTouchButtonShape.AUTO;
    }
  }

  /** The shape of an action's buttons in a look: in GC, PS2 and XBOX also Z a shoulder button, the others round. */
  static RetroTouchButtonShape shape(int look, String action) {
    if (look == RE) return iconShape(action);
    switch (action) {
      case CoreTouch.L:
      case CoreTouch.R:
      case CoreTouch.Z: return RetroTouchButtonShape.ROUNDED_RECT;
      case CoreTouch.START:
      case CoreTouch.MENU: return RetroTouchButtonShape.CAPSULE;
      default: return RetroTouchButtonShape.ROUND;
    }
  }

  /**
   * Width over height of an action's button of the default layout (1 where the shape has no aspect; 0 for Z, whose
   * width comes from its look, zAspect, also on the default layout's Z).
   */
  static float aspect(String action, float shoulder) {
    switch (action) {
      case CoreTouch.L:
      case CoreTouch.R: return shoulder;
      case CoreTouch.START:
      case CoreTouch.MENU: return CAPSULE_ASPECT;
      case CoreTouch.Z: return 0f;
      default: return 1f;
    }
  }

  /** Width over height of every button of an action in a look (RetroTouch's setButtonAspect; 1 = round). */
  static float aspect(int look, String action, float shoulder) {
    if (CoreTouch.Z.equals(action)) return look == RE ? 1f : Z_ASPECT;
    float a = aspect(action, shoulder);
    return a == 0f ? 1f : a;
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
