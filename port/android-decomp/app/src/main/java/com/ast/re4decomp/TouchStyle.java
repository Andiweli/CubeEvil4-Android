package com.ast.re4decomp;

import com.ast.retrotouch.RetroTouchButtonShape;
import com.ast.retrotouch.RetroTouchButtonStyle;
import com.ast.retrotouch.RetroTouchColors;
import com.ast.retrotouch.RetroTouchDpadStyle;
import com.ast.retrotouch.RetroTouchIcon;
import com.ast.retrotouch.RetroTouchLook;
import com.ast.retrotouch.RetroTouchSkin;
import com.ast.retrotouch.RetroTouchStickGate;

/**
 * The four skins of the touch controls, all in one place (wish of the project owner, 17.11.2026; RetroTouch
 * 1.0.0-beta.13, the owner's reference picture builds\retrotouch-styles\owner-reference-beta13.png): RE (symbols for
 * what each button does, the default since 0.99.7), GC (the GameCube's buttons: bumpers L and R, beans X and Y, a
 * purple Z, a grey D-pad, START, MENU and move stick, a yellow C stick, octagonal gates; it replaces the GC style of 0.99.6 .. 1.0, RetroTouch's
 * LABELS), PS2 (black buttons, the four symbols in their colours, L2/R2 triggers, R1 bumper, a D-pad of four buttons)
 * and XBOX (dark buttons with coloured letters, LT/RT triggers, RB bumper). Mapped as CoreInput maps a controller and
 * as the HD texture packs' button sets: Z = R1/RB, L = L2/LT, R = R2/RT, MENU = SELECT/View, the C stick the right stick.
 *
 * RetroTouch keeps the player's choice (its key button_skin_cubeevil4, in the player's backup); its editor's style
 * button goes through the skins in this order. Each skin sets every action's content, colours, shape and width over
 * height, so nothing of one skin shows in another; the actions themselves carry RE's labels and icons (TouchControls).
 * RetroTouch centres labels and icons on their ink in skins (beta.13).
 *
 * The icons and labels follow what a button does on the touch controls (CoreTouch.gcButton: a GameCube button
 * each); the menu's "Alternate Controls" change the controller's keys only, so the skins stay as they are with them.
 */
final class TouchStyle {
  private TouchStyle() {}

  /** RetroTouch's style without skins (the base of RE). */
  static final RetroTouchButtonStyle DEFAULT_BUTTON_STYLE = RetroTouchButtonStyle.ICONS;

  /** The skins, in the order of the app menu and of the editor's style button. */
  static final int RE = 0, GC = 1, PS2 = 2, XBOX = 3, LOOKS = 4;
  /** Their IDs (RetroTouch saves the chosen one) and names (the menu's buttons, the editor's style button). */
  static final String[] IDS = {"re", "gc", "ps2", "xbox"};
  static final String[] NAMES = {"RE", "GC", "PS2", "XBOX"};
  /** A new player's skin. */
  static final int DEFAULT_LOOK = RE;

  /** The skin number of a skin ID, the default for an unknown one. */
  static int look(String id) {
    for (int i = 0; i < LOOKS; i++) if (IDS[i].equals(id)) return i;
    return DEFAULT_LOOK;
  }

  /** A skin number as it is, else the default. */
  static int valid(int look) { return look >= 0 && look < LOOKS ? look : DEFAULT_LOOK; }

  // ---------------------------------------------------------------- RE (the project owner's start values, 09.11.2026)
  // RetroTouch takes the transparency from each control's opacity (the editor's ALPHA), not from these colours
  static final int FILL = 0xFF2E2B25, BORDER = 0xFFB58B55, ICON = 0xFFF0DFB8;
  static final int PRESSED_FILL = 0xFF4A4338, PRESSED_BORDER = 0xFFD9A86A, PRESSED_ICON = 0xFFFFF1D0;
  // a latched L, R or B: RetroTouch adds the outer ring in the border's colour
  static final int LATCHED_FILL = 0xFF3A3326, LATCHED_BORDER = 0xFFE8C07A, LATCHED_ICON = 0xFFFFF1D0;

  /** Every button, the D-pad and the sticks of RE. The border is RetroTouch's own width (7 % of the shorter side). */
  static final RetroTouchColors COLOURS = colours(FILL, BORDER, ICON, PRESSED_FILL, PRESSED_BORDER, PRESSED_ICON,
      LATCHED_FILL, LATCHED_BORDER, LATCHED_ICON);

  static RetroTouchColors colours(int fill, int border, int text, int pressedFill, int pressedBorder, int pressedText,
                                  int latchedFill, int latchedBorder, int latchedText) {
    return new RetroTouchColors(fill, pressedFill, border, text)
        .withPressedColors(pressedFill, pressedBorder, pressedText)
        .withLatchedColors(latchedFill, latchedBorder, latchedText);
  }

  // ---------------------------------------------------------------- GC
  static final RetroTouchColors GC_GREY = colours(0xFFB9BCC3, 0xFF6E717A, 0xFF3C3F47, 0xFFDCDEE3, 0xFF8A8D96, 0xFF2A2C32,
      0xFFC9CBD1, 0xFF9A9DA6, 0xFF2A2C32);
  static final RetroTouchColors GC_GREEN = colours(0xFF2FA57E, 0xFFCFEFE4, 0xFFF2FFF8, 0xFF4FC79D, 0xFFFFFFFF, 0xFFFFFFFF,
      0xFF3BB58C, 0xFFFFFFFF, 0xFFFFFFFF);
  static final RetroTouchColors GC_RED = colours(0xFFC9373A, 0xFF7A1A1C, 0xFFFFECEC, 0xFFE95A5C, 0xFFA42628, 0xFFFFFFFF,
      0xFFD9474A, 0xFFE8C07A, 0xFFFFFFFF);
  static final RetroTouchColors GC_PURPLE = colours(0xFF5B4FC0, 0xFF8F84E8, 0xFFEDEBFF, 0xFF7A6FE0, 0xFFB7AEFF, 0xFFFFFFFF,
      0xFF6A5FD0, 0xFFC9C1FF, 0xFFFFFFFF);
  // the move stick (the skin's default colours): the grey of L, R and the D-pad a little darker, so that its gate (drawn
  // at 55 %) stays visible over the game and below the knob (the project owner, 17.11.2026)
  static final RetroTouchColors GC_STICK = colours(0xFF9A9DA5, 0xFF5E6169, 0xFF2A2C32, 0xFFBFC2C9, 0xFF7A7D86, 0xFF1E2024,
      0xFFABAEB5, 0xFF8A8D96, 0xFF1E2024);
  static final RetroTouchColors GC_YELLOW = colours(0xFFD9B21C, 0xFF6E5800, 0xFF3A2C00, 0xFFF2CB33, 0xFF8E7200, 0xFF2A2000,
      0xFFE6BF28, 0xFF9A7C00, 0xFF2A2000);

  // ---------------------------------------------------------------- PS2: black buttons, the symbols in their colours
  static final RetroTouchColors PS2_BLACK = colours(0xFF1D1E24, 0xFF6E717D, 0xFFD0D2DA, 0xFF33353F, 0xFFA5A8BC, 0xFFFFFFFF,
      0xFF2A2C34, 0xFFC3C6D8, 0xFFFFFFFF);
  static final RetroTouchColors PS2_BLUE = PS2_BLACK.withIconColors(0xFF6E96E6, 0xFF9DBBFF, 0xFF9DBBFF),
      PS2_RED = PS2_BLACK.withIconColors(0xFFE2545A, 0xFFFF8A8E, 0xFFFF8A8E),
      PS2_PINK = PS2_BLACK.withIconColors(0xFFE27AC4, 0xFFFFA8E2, 0xFFFFA8E2),
      PS2_GREEN = PS2_BLACK.withIconColors(0xFF3FBF96, 0xFF7BE3C0, 0xFF7BE3C0);

  // ---------------------------------------------------------------- XBOX: dark buttons, coloured letters
  static final RetroTouchColors XBOX_DARK = colours(0xFF23262C, 0xFF8A8D96, 0xFFE6E7EC, 0xFF3A3D45, 0xFFC0C3CC, 0xFFFFFFFF,
      0xFF2E3138, 0xFFD0D3DC, 0xFFFFFFFF);
  private static RetroTouchColors letter(int colour, int pressed) {
    return colours(0xFF23262C, 0xFFB8BBC4, colour, 0xFF3A3D45, 0xFFE0E2E8, pressed, 0xFF2E3138, 0xFFE8C07A, pressed);
  }
  static final RetroTouchColors XBOX_GREEN = letter(0xFF6FCB4E, 0xFF96E87A), XBOX_RED = letter(0xFFF0605A, 0xFFFF8E88),
      XBOX_BLUE = letter(0xFF5B8FF0, 0xFF8DB2FF), XBOX_YELLOW = letter(0xFFF2C94C, 0xFFFFDD80);
  static final RetroTouchColors XBOX_DPAD = colours(0xFF3A3D45, 0xFF8A8D96, 0xFFE6E7EC, 0xFF52555E, 0xFFC0C3CC, 0xFFFFFFFF,
      0xFF454850, 0xFFD0D3DC, 0xFFFFFFFF);

  // ---------------------------------------------------------------- own icons (24 x 24, the ink centred in the box)
  static final RetroTouchIcon PLUS = RetroTouchIcon.fromPaths(24f, "M12 5v14", "M5 12h14");
  static final RetroTouchIcon MINUS = RetroTouchIcon.fromPaths(24f, "M5 12h14");
  // the Xbox's "view" key: two overlapping windows (no logo)
  static final RetroTouchIcon XBOX_VIEW = RetroTouchIcon.fromPaths(24f, "M4 9h10v10H4Z", "M8 9V5h12v10h-6");

  /** Width over height of L and R as rounded rectangles and bumpers (where the screen has room, see shoulderAspect). */
  static final float SHOULDER_ASPECT = 1.95f;
  /** Width over height of MENU and START as capsules. */
  static final float CAPSULE_ASPECT = 2.1f;
  /** Width over height of Z in GC, PS2 and XBOX (Z, R1, RB). */
  static final float Z_ASPECT = 1.6f;
  /** Width over height of the triggers (L2/R2, LT/RT) and of GC's beans (RetroTouch's defaults). */
  static final float TRIGGER_ASPECT = 0.8f, KIDNEY_ASPECT = 1.6f;
  /** GC's beans turned (clockwise, RetroTouch's KIDNEY is hollow on top) so that their hollow faces A. */
  static final float Y_TURN = 145f, X_TURN = 225f;

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

  /**
   * Width over height of an action's button of the default layout (1 where the shape has no aspect; 0 for Z: none
   * of its own, so its skin's counts).
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

  private static RetroTouchLook look() { return new RetroTouchLook(); }

  private static RetroTouchLook label(String text, RetroTouchColors colours) {
    return look().withLabel(text).withColors(colours).withShape(RetroTouchButtonShape.ROUND);
  }

  private static RetroTouchLook icon(RetroTouchIcon icon, RetroTouchColors colours) {
    return look().withIcon(icon).withColors(colours).withShape(RetroTouchButtonShape.ROUND);
  }

  private static RetroTouchLook capsule(RetroTouchLook look) {
    return look.withShape(RetroTouchButtonShape.CAPSULE).withAspect(CAPSULE_ASPECT);
  }

  /** The four skins for L's and R's width over height on this screen (shoulderAspect). */
  static RetroTouchSkin[] skins(float shoulder) {
    return new RetroTouchSkin[] {re(shoulder), gc(shoulder), ps2(), xbox()};
  }

  /** RE: as before (ICONS: the actions' icons and labels, one colour set, L and R and the capsules as wide as before). */
  static RetroTouchSkin re(float shoulder) {
    RetroTouchLook shoulderButton = look().withShape(RetroTouchButtonShape.ROUNDED_RECT).withAspect(shoulder);
    return new RetroTouchSkin(IDS[RE], NAMES[RE], RetroTouchButtonStyle.ICONS)
        .setDefaultColors(COLOURS)
        .setLook(CoreTouch.L, shoulderButton)
        .setLook(CoreTouch.R, shoulderButton)
        .setLook(CoreTouch.START, capsule(look()))
        .setLook(CoreTouch.MENU, capsule(look()));
  }

  /**
   * GC: the GameCube's buttons; the D-pad, START, MENU and the move stick grey like L, R, X and Y (the project owner,
   * 17.11.2026), Z and the aim and knife sticks purple, the C stick yellow.
   */
  static RetroTouchSkin gc(float shoulder) {
    RetroTouchLook bumper = look().withShape(RetroTouchButtonShape.BUMPER).withAspect(shoulder).withColors(GC_GREY);
    RetroTouchLook bean = look().withShape(RetroTouchButtonShape.KIDNEY).withAspect(KIDNEY_ASPECT).withColors(GC_GREY);
    return new RetroTouchSkin(IDS[GC], NAMES[GC], RetroTouchButtonStyle.LABELS)
        .setDefaultColors(GC_STICK)
        .setDpadColors(GC_GREY)
        .setStickGate(RetroTouchStickGate.OCTAGON)
        .setLook(CoreTouch.A, label("A", GC_GREEN))
        .setLook(CoreTouch.B, label("B", GC_RED))
        .setLook(CoreTouch.X, bean.withLabel("X").withRotation(X_TURN))
        .setLook(CoreTouch.Y, bean.withLabel("Y").withRotation(Y_TURN))
        .setLook(CoreTouch.Z, look().withLabel("Z").withColors(GC_PURPLE).withShape(RetroTouchButtonShape.CAPSULE)
            .withAspect(Z_ASPECT))
        .setLook(CoreTouch.L, bumper.withLabel("L"))
        .setLook(CoreTouch.R, bumper.withLabel("R"))
        .setLook(CoreTouch.START, capsule(look().withLabel("START").withColors(GC_GREY)))
        .setLook(CoreTouch.MENU, capsule(look().withIcon(RetroTouchIcon.MENU).withColors(GC_GREY)))
        .setLook(CoreTouch.R3, label("R3", GC_GREY))
        .setLook(CoreTouch.ZOOM_IN, icon(PLUS, GC_GREY))
        .setLook(CoreTouch.ZOOM_OUT, icon(MINUS, GC_GREY))
        .setLook(CoreTouch.C_STICK, look().withLabel("C").withColors(GC_YELLOW))
        .setLook(CoreTouch.AIM_STICK, look().withIcon(RetroTouchIcon.CROSSHAIR).withColors(GC_PURPLE))
        .setLook(CoreTouch.KNIFE_STICK, look().withIcon(RetroTouchIcon.KNIFE_HORIZONTAL).withColors(GC_PURPLE));
  }

  /** PS2: black buttons, the symbols in their colours, a D-pad of four buttons. */
  static RetroTouchSkin ps2() {
    RetroTouchLook trigger = look().withShape(RetroTouchButtonShape.TRIGGER).withAspect(TRIGGER_ASPECT).withColors(PS2_BLACK);
    return new RetroTouchSkin(IDS[PS2], NAMES[PS2], RetroTouchButtonStyle.ICONS)
        .setDefaultColors(PS2_BLACK)
        .setDpadColors(PS2_BLACK)
        .setDpadStyle(RetroTouchDpadStyle.FOUR_BUTTONS)
        .setDpadIcons(RetroTouchIcon.ARROW_UP, RetroTouchIcon.ARROW_DOWN, RetroTouchIcon.ARROW_LEFT, RetroTouchIcon.ARROW_RIGHT)
        .setMoveStickLook(look().withLabel("L3"))
        .setLook(CoreTouch.A, icon(RetroTouchIcon.SYMBOL_CROSS, PS2_BLUE))
        .setLook(CoreTouch.B, icon(RetroTouchIcon.SYMBOL_CIRCLE, PS2_RED))
        .setLook(CoreTouch.X, icon(RetroTouchIcon.SYMBOL_SQUARE, PS2_PINK))
        .setLook(CoreTouch.Y, icon(RetroTouchIcon.SYMBOL_TRIANGLE, PS2_GREEN))
        .setLook(CoreTouch.Z, look().withLabel("R1").withColors(PS2_BLACK).withShape(RetroTouchButtonShape.BUMPER)
            .withAspect(Z_ASPECT))
        .setLook(CoreTouch.L, trigger.withLabel("L2"))
        .setLook(CoreTouch.R, trigger.withLabel("R2"))
        .setLook(CoreTouch.START, capsule(look().withLabel("START").withColors(PS2_BLACK)))
        .setLook(CoreTouch.MENU, capsule(look().withLabel("SELECT").withColors(PS2_BLACK)))
        .setLook(CoreTouch.R3, label("R3", PS2_BLACK))
        .setLook(CoreTouch.ZOOM_IN, icon(PLUS, PS2_BLACK))
        .setLook(CoreTouch.ZOOM_OUT, icon(MINUS, PS2_BLACK))
        .setLook(CoreTouch.C_STICK, look().withLabel("R3").withColors(PS2_BLACK))
        .setLook(CoreTouch.AIM_STICK, look().withIcon(RetroTouchIcon.CROSSHAIR).withColors(PS2_BLACK))
        .setLook(CoreTouch.KNIFE_STICK, look().withIcon(RetroTouchIcon.KNIFE_HORIZONTAL).withColors(PS2_BLACK));
  }

  /** XBOX: dark buttons with coloured letters, triggers and a bumper. */
  static RetroTouchSkin xbox() {
    RetroTouchLook trigger = look().withShape(RetroTouchButtonShape.TRIGGER).withAspect(TRIGGER_ASPECT).withColors(XBOX_DARK);
    return new RetroTouchSkin(IDS[XBOX], NAMES[XBOX], RetroTouchButtonStyle.LABELS)
        .setDefaultColors(XBOX_DARK)
        .setDpadColors(XBOX_DPAD)
        .setDpadIcons(RetroTouchIcon.ARROW_UP, RetroTouchIcon.ARROW_DOWN, RetroTouchIcon.ARROW_LEFT, RetroTouchIcon.ARROW_RIGHT)
        .setMoveStickLook(look().withLabel("LS"))
        .setLook(CoreTouch.A, label("A", XBOX_GREEN))
        .setLook(CoreTouch.B, label("B", XBOX_RED))
        .setLook(CoreTouch.X, label("X", XBOX_BLUE))
        .setLook(CoreTouch.Y, label("Y", XBOX_YELLOW))
        .setLook(CoreTouch.Z, look().withLabel("RB").withColors(XBOX_DARK).withShape(RetroTouchButtonShape.BUMPER)
            .withAspect(Z_ASPECT))
        .setLook(CoreTouch.L, trigger.withLabel("LT"))
        .setLook(CoreTouch.R, trigger.withLabel("RT"))
        .setLook(CoreTouch.START, capsule(look().withLabel("START").withColors(XBOX_DARK)))
        .setLook(CoreTouch.MENU, capsule(look().withIcon(XBOX_VIEW).withColors(XBOX_DARK)))
        .setLook(CoreTouch.R3, label("R3", XBOX_DARK))
        .setLook(CoreTouch.ZOOM_IN, icon(PLUS, XBOX_DARK))
        .setLook(CoreTouch.ZOOM_OUT, icon(MINUS, XBOX_DARK))
        .setLook(CoreTouch.C_STICK, look().withLabel("RS").withColors(XBOX_DARK))
        .setLook(CoreTouch.AIM_STICK, look().withIcon(RetroTouchIcon.CROSSHAIR).withColors(XBOX_DARK))
        .setLook(CoreTouch.KNIFE_STICK, look().withIcon(RetroTouchIcon.KNIFE_HORIZONTAL).withColors(XBOX_DARK));
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
