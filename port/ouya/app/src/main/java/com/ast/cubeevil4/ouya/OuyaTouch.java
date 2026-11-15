package com.ast.cubeevil4.ouya;

import java.util.HashSet;
import java.util.Set;

/**
 * RetroTouch's callbacks as the GameCube controller (wish of the project owner, 15.11.2026: RetroTouch in the OUYA
 * app, for the APK on a phone or tablet with Android 4.x; the OUYA itself has no touch screen). The main app's
 * CoreTouch (port/android-decomp) without its touch aiming, R3 and the zoom buttons: one button per GameCube button
 * (A, B, X, Y, Z, L, R, START), the D-pad, the move stick as the control stick, the right half as the C stick (the
 * way a finger went since it went down) or a C stick the editor adds; MENU opens the app's menu (the OUYA's L3).
 * "Latch L and R" and "Latch B": a tap keeps the button pressed until the next tap.
 */
final class OuyaTouch {
  // the actions registered with RetroTouch (the layout's contract, the main app's names: a layout exported there
  // fits here)
  static final String A = "a", B = "b", X = "x", Y = "y", Z = "z", L = "l", R = "r", START = "start", MENU = "menu",
      C_STICK = "cstick";
  // the D-pad's directions (RetroTouchNavigation)
  static final String UP = "nav_up", DOWN = "nav_down", LEFT = "nav_left", RIGHT = "nav_right";
  /** This way of a looking finger (share of the screen's shorter side) tilts the C stick fully. */
  static final float LOOK_RANGE = 0.15f;

  static final String[] ACTIONS = {A, B, X, Y, Z, L, R, START, MENU, C_STICK};
  static final String[] LABELS = {"A", "B", "X", "Y", "Z", "L", "R", "START", "MENU", "C"};
  // the GameCube's colours (the main app's): A green, B red, Z purple, the C stick yellow, the others grey
  static final int GC_GREEN = rgb(0x10, 0x9B, 0x61), GC_RED = rgb(0xF6, 0x39, 0x2C), GC_PURPLE = rgb(0x6B, 0x5C, 0xB1),
      GC_GREY = rgb(0xDD, 0xDD, 0xDD), GC_YELLOW = rgb(0xF7, 0xC8, 0x1E);
  static final int BORDER = 0xFF3E3C29;
  static final int[] COLOURS = {GC_GREEN, GC_RED, GC_GREY, GC_GREY, GC_PURPLE, GC_GREY, GC_GREY, GC_GREY, GC_GREY,
      GC_YELLOW};

  static boolean isStick(String action) { return C_STICK.equals(action); }

  // the kinds of RetroTouchControl
  static final int BUTTON = 0, STICK = 1, LOOK = 2, DPAD = 3;

  /** A control of the default layout: centre and size in shares of the screen (RetroTouchControl). */
  static final class Control {
    final int kind;
    final String id, action;
    final float x, y, width, height;

    Control(int kind, String id, String action, float x, float y, float width, float height) {
      this.kind = kind;
      this.id = id;
      this.action = action;
      this.x = x;
      this.y = y;
      this.width = width;
      this.height = height;
    }

    static Control button(String action, float x, float y, float size) {
      return new Control(BUTTON, action, action, x, y, size, size);
    }
  }

  /** The main app's default layout (CoreTouch.LAYOUT). */
  static final Control[] LAYOUT = {
      new Control(LOOK, "look", "", 0.75f, 0.50f, 0.50f, 1.00f),
      new Control(STICK, "stick", "", 0.14f, 0.70f, 0.30f, 0.30f),
      new Control(DPAD, "dpad", "", 0.11f, 0.36f, 0.20f, 0.20f),
      Control.button(L, 0.14f, 0.12f, 0.13f),
      Control.button(R, 0.84f, 0.12f, 0.13f),
      Control.button(Z, 0.93f, 0.30f, 0.09f),
      Control.button(Y, 0.80f, 0.46f, 0.11f),
      Control.button(X, 0.955f, 0.50f, 0.11f),
      Control.button(A, 0.875f, 0.68f, 0.17f),
      Control.button(B, 0.76f, 0.86f, 0.11f),
      Control.button(MENU, 0.42f, 0.08f, 0.085f),
      Control.button(START, 0.58f, 0.08f, 0.085f),
  };

  /** Where the touch controls' controller goes (OuyaActivity: beside the real controller's keys and sticks). */
  interface Input {
    void touchButton(int gcBit, boolean on);
    /** The move stick, -1..1 with y down. */
    void touchStick(float x, float y);
    /** The C stick: the looking finger (lx, ly) and the C stick control (cx, cy), -1..1 with y down. */
    void touchLook(float lx, float ly, float cx, float cy);
    void touchClear();
  }

  interface Host {
    /** MENU went down or up: the app's menu. */
    void select(boolean down);
    /** A tap latched L, R or B or let it go (RetroTouch shows a latched button stronger). */
    void latched(String action, boolean on);
  }

  private final Input input;
  private final Host host;
  private boolean l, r, b;  // latched
  private boolean latch, latchB;
  private float lookX, lookY;
  private final Set<Integer> lookFingers = new HashSet<Integer>();
  private boolean answered;
  private float cx, cy;

  OuyaTouch(Input input, Host host) {
    this.input = input;
    this.host = host;
  }

  static int rgb(int red, int green, int blue) {
    return 0xFF000000 | red << 16 | green << 8 | blue;
  }

  static String label(String action) {
    for (int i = 0; i < ACTIONS.length; i++)
      if (ACTIONS[i].equals(action))
        return LABELS[i];
    return "";
  }

  static int gcButton(String action) {
    if (A.equals(action)) return OuyaActivity.GC_A;
    if (B.equals(action)) return OuyaActivity.GC_B;
    if (X.equals(action)) return OuyaActivity.GC_X;
    if (Y.equals(action)) return OuyaActivity.GC_Y;
    if (Z.equals(action)) return OuyaActivity.GC_Z;
    if (L.equals(action)) return OuyaActivity.GC_L;
    if (R.equals(action)) return OuyaActivity.GC_R;
    if (START.equals(action)) return OuyaActivity.GC_START;
    if (UP.equals(action)) return OuyaActivity.GC_UP;
    if (DOWN.equals(action)) return OuyaActivity.GC_DOWN;
    if (LEFT.equals(action)) return OuyaActivity.GC_LEFT;
    if (RIGHT.equals(action)) return OuyaActivity.GC_RIGHT;
    return 0;
  }

  void action(String action, boolean down) {
    if (down)
      answered = true;
    if (MENU.equals(action)) {
      host.select(down);
    } else if (latch && (L.equals(action) || R.equals(action))) {
      if (!down)
        return;
      boolean on = L.equals(action) ? (l = !l) : (r = !r);
      input.touchButton(gcButton(action), on);
      host.latched(action, on);
    } else if (latchB && B.equals(action)) {
      if (!down)
        return;
      b = !b;
      input.touchButton(OuyaActivity.GC_B, b);
      host.latched(B, b);
    } else {
      int bit = gcButton(action);
      if (bit != 0)
        input.touchButton(bit, down);
    }
  }

  void move(float x, float y) {
    answered = true;
    input.touchStick(x, y);
  }

  void look(float dx, float dy) {
    if (lookFingers.isEmpty())
      return;
    lookX += dx;
    lookY += dy;
    float way = (float) Math.sqrt(lookX * lookX + lookY * lookY);
    if (way > LOOK_RANGE) {
      lookX *= LOOK_RANGE / way;
      lookY *= LOOK_RANGE / way;
    }
    updateC();
  }

  void stick(String action, float x, float y) {
    if (!C_STICK.equals(action))
      return;
    answered = true;
    cx = x;
    cy = y;
    updateC();
  }

  private void updateC() {
    float lx = lookFingers.isEmpty() ? 0 : lookX / LOOK_RANGE, ly = lookFingers.isEmpty() ? 0 : lookY / LOOK_RANGE;
    input.touchLook(lx, ly, cx, cy);
  }

  void beginEvent() { answered = false; }

  void fingerDown(int finger, boolean inLookZone) {
    if (inLookZone && !answered)
      lookFingers.add(finger);
  }

  void fingerUp(int finger) {
    if (lookFingers.remove(finger) && lookFingers.isEmpty())
      centreLook();
  }

  void cancel() {
    lookFingers.clear();
    centreLook();
  }

  boolean looking() { return !lookFingers.isEmpty(); }

  void setLatch(boolean on) {
    if (on == latch)
      return;
    latch = on;
    if (l)
      host.latched(L, false);
    if (r)
      host.latched(R, false);
    l = r = false;
    input.touchButton(OuyaActivity.GC_L, false);
    input.touchButton(OuyaActivity.GC_R, false);
  }

  void setLatchB(boolean on) {
    if (on == latchB)
      return;
    latchB = on;
    if (b)
      host.latched(B, false);
    b = false;
    input.touchButton(OuyaActivity.GC_B, false);
  }

  private void centreLook() {
    lookX = 0;
    lookY = 0;
    updateC();
  }

  /** Everything let go (the menu, the controls hidden, the editor), latched L, R and B too. */
  void reset() {
    lookFingers.clear();
    lookX = lookY = cx = cy = 0;
    if (l)
      host.latched(L, false);
    if (r)
      host.latched(R, false);
    if (b)
      host.latched(B, false);
    l = r = b = false;
    input.touchClear();
  }
}
