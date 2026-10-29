package com.ast.re4decomp;

import java.util.HashSet;
import java.util.Set;

/**
 * The touch controls of the core mode (RetroTouch, decision of the project
 * owner, 19.10.2026): its callbacks become GameCube controller 0, a device of
 * CoreInput of its own. Independent of Android and RetroTouch so the host
 * tests can check it (tests/CoreTouchTests.java); TouchControls puts RetroTouch
 * on the screen and calls this class.
 *
 * One button per controller function with its label (A, B, X, Y, Z, L, R,
 * START), the D-pad (also for the game's menus), the move stick as the control
 * stick and the right look zone as the C stick (decision of the project owner,
 * 19.10.2026); MENU is the app's SELECT (pressed: the app's menu, held for a
 * second: the game's debug menu). L and R are held while the finger is on
 * them, as real buttons; with the menu's "Latch L and R" (setLatch) a tap keeps
 * them pressed until the next tap: one thumb cannot hold R (ready the weapon)
 * and tap A (fire). Latching was the only way until 20.10.2026; since then a
 * switch, off unless chosen (decision of the project owner). "Latch B"
 * (setLatchB, wish of the project owner, 22.10.2026) does the same for B: a
 * tap keeps B (run) pressed, so Leon runs without a finger on B.
 *
 * The look zone reports how far a finger moved, not where it is. The C stick
 * is the way the finger went since it went down (LOOK_RANGE of the screen's
 * shorter side tilts it fully; a longer way moves the centre along) and goes
 * back when the finger lifts. RetroTouch does not say which finger looks, so
 * TouchControls names the fingers that went down inside the look zone without
 * pressing a control (fingerDown).
 *
 * The C stick can also be an analog stick of its own (wish of the project
 * owner, 26.10.2026; until then four buttons C-up .. C-right): the editor's
 * ADD, then ACTION to "C" (RetroTouch's stick action, 1.0.0-beta.7), in the
 * GameCube's yellow. It does what the look zone does; the further tilt wins.
 */
final class CoreTouch {
  // the actions registered with RetroTouch (the layout's contract: lower case, never renamed)
  static final String A = "a", B = "b", X = "x", Y = "y", Z = "z", L = "l", R = "r",
      START = "start", MENU = "menu", R3 = "r3",
      // the C stick as an analog stick the editor adds (RetroTouch's stick action, registerStickAction)
      C_STICK = "cstick";
  // the D-pad's directions (RetroTouchNavigation)
  static final String UP = "nav_up", DOWN = "nav_down", LEFT = "nav_left", RIGHT = "nav_right";
  /** This way of a looking finger (share of the screen's shorter side) tilts the C stick fully. */
  static final float LOOK_RANGE = 0.15f;

  /**
   * The buttons in the editor's order: action, label (short: it has to fit the button), colour (0:
   * RetroTouch's grey). R3 (wish of the project owner, 24.10.2026) is the right stick pressed: the
   * graphics capture for error reports, as on a controller; not in the default layout, the editor
   * adds it.
   */
  static final String[] ACTIONS = {A, B, X, Y, Z, L, R, START, MENU, R3, C_STICK};
  static final String[] LABELS = {"A", "B", "X", "Y", "Z", "L", "R", "START", "MENU", "R3", "C"};
  // the GameCube's official colours (the project owner's palette, 19.10.2026): A green, B red,
  // Z purple, the C stick yellow, the others grey (RetroTouch writes dark labels on the light grey
  // and the yellow)
  static final int GC_GREEN = rgb(0x10, 0x9B, 0x61), GC_RED = rgb(0xF6, 0x39, 0x2C), GC_PURPLE = rgb(0x6B, 0x5C, 0xB1),
      GC_GREY = rgb(0xDD, 0xDD, 0xDD), GC_YELLOW = rgb(0xF7, 0xC8, 0x1E);
  static final int[] COLOURS = {GC_GREEN, GC_RED, GC_GREY, GC_GREY, GC_PURPLE, GC_GREY, GC_GREY, GC_GREY, GC_GREY, GC_GREY,
      GC_YELLOW};

  /** Whether an action is an analog stick (RetroTouch's stick action), not a button. */
  static boolean isStick(String action) {
    return C_STICK.equals(action);
  }

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

  /**
   * The default layout, for landscape screens of 4:3 to 20:9: left the D-pad
   * above the move stick and L; right the face buttons as on the GameCube (A
   * large, B below left, X right, Y above), Z and R above them; MENU and START
   * at the top centre (the subtitles are at the bottom). The look zone is the
   * right half; the buttons in it come first. RetroTouch's gear (top right)
   * opens its editor, where the player moves, resizes and saves the layout.
   */
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

  interface Host {
    /** MENU went down or up: the app's SELECT. */
    void select(boolean down);
    /** A tap latched L, R or B or let it go (RetroTouch shows a latched button stronger). */
    void latched(String action, boolean on);
    /** R3 went down or up: the app's graphics capture. */
    void r3(boolean down);
  }

  private final CoreInput input;
  private final Host host;
  private boolean l, r;  // latched (only with latch)
  private boolean latch;
  private boolean b;     // latched (only with latchB)
  private boolean latchB;
  // the way the looking fingers went since the first of them went down (shares of the shorter side)
  private float lookX, lookY;
  private final Set<Integer> lookFingers = new HashSet<>();
  // a control answered the finger of the touch event being dispatched (beginEvent .. fingerDown)
  private boolean answered;

  CoreTouch(CoreInput input, Host host) {
    this.input = input;
    this.host = host;
  }

  static int rgb(int red, int green, int blue) {
    return 0xFF000000 | red << 16 | green << 8 | blue;
  }

  /** The label of an action, "" for none. */
  static String label(String action) {
    for (int i = 0; i < ACTIONS.length; i++) if (ACTIONS[i].equals(action)) return LABELS[i];
    return "";
  }

  /** The GameCube button of an action (the D-pad's directions too), 0 for none. */
  static int gcButton(String action) {
    switch (action) {
      case A: return CoreInput.GC_A;
      case B: return CoreInput.GC_B;
      case X: return CoreInput.GC_X;
      case Y: return CoreInput.GC_Y;
      case Z: return CoreInput.GC_Z;
      case L: return CoreInput.GC_L;
      case R: return CoreInput.GC_R;
      case START: return CoreInput.GC_START;
      case UP: return CoreInput.GC_UP;
      case DOWN: return CoreInput.GC_DOWN;
      case LEFT: return CoreInput.GC_LEFT;
      case RIGHT: return CoreInput.GC_RIGHT;
      default: return 0;
    }
  }

  /** RetroTouch's onAction: a button or a direction of the D-pad went down or up. */
  void action(String action, boolean down) {
    if (down) answered = true;
    if (MENU.equals(action)) {
      host.select(down);
    } else if (R3.equals(action)) {
      host.r3(down);
    } else if (latch && (L.equals(action) || R.equals(action))) {
      if (!down) return;
      boolean on = L.equals(action) ? (l = !l) : (r = !r);
      input.touchButton(gcButton(action), on);
      host.latched(action, on);
    } else if (latchB && B.equals(action)) {
      if (!down) return;
      b = !b;
      input.touchButton(CoreInput.GC_B, b);
      host.latched(B, b);
    } else {
      int bit = gcButton(action);
      if (bit != 0) input.touchButton(bit, down);
    }
  }

  /** RetroTouch's onMove: the move stick, -1..1 with y down. */
  void move(float x, float y) {
    answered = true;
    input.touchStick(false, x, y);
  }

  /** RetroTouch's onLook: a looking finger moved by dx/dy (shares of the shorter side). */
  void look(float dx, float dy) {
    if (lookFingers.isEmpty()) return;  // not a finger of the look zone (see fingerDown)
    // aiming with "Touch Area": the swipe turns the aim (CoreInput.touchSwipe); the view stays as it was
    if (input.touchSwipe(dx, dy)) {
      if (lookX != 0 || lookY != 0) centreLook();
      return;
    }
    lookX += dx;
    lookY += dy;
    float way = (float) Math.sqrt(lookX * lookX + lookY * lookY);
    if (way > LOOK_RANGE) {
      lookX *= LOOK_RANGE / way;
      lookY *= LOOK_RANGE / way;
    }
    updateC();
  }

  // the C stick of the editor (RetroTouch's stick action), -1..1 with y down
  private float cx, cy;

  /** RetroTouch's onStick: the C stick the editor added, -1..1 with y down (0, 0: let go). */
  void stick(String action, float x, float y) {
    if (!C_STICK.equals(action)) return;
    answered = true;  // a finger on the C stick does not look (fingerDown), even in its centre
    cx = x;
    cy = y;
    updateC();
  }

  /**
   * The C stick: the C stick control or the looking finger, whichever tilts further (CoreInput.touchLook;
   * with touch aiming one of them aims while L or R is held).
   */
  private void updateC() {
    float lx = lookFingers.isEmpty() ? 0 : lookX / LOOK_RANGE, ly = lookFingers.isEmpty() ? 0 : lookY / LOOK_RANGE;
    input.touchLook(lx, ly, cx, cy);
  }

  /** Before RetroTouch gets a touch event. */
  void beginEvent() { answered = false; }

  /**
   * After RetroTouch had the event in which `finger` went down: a finger
   * inside the look zone that no control answered looks.
   */
  void fingerDown(int finger, boolean inLookZone) {
    if (inLookZone && !answered) lookFingers.add(finger);
  }

  /** A finger lifted: the C stick goes back when it was the last looking one. */
  void fingerUp(int finger) {
    if (lookFingers.remove(finger) && lookFingers.isEmpty()) centreLook();
  }

  /** Android took the fingers away (another window): nobody looks any more. */
  void cancel() {
    lookFingers.clear();
    centreLook();
  }

  boolean looking() { return !lookFingers.isEmpty(); }

  boolean latched(String action) {
    return L.equals(action) ? l : R.equals(action) ? r : B.equals(action) && b;
  }

  /** The menu's switch: L and R latch by a tap (on) or are held while the finger is on them. */
  void setLatch(boolean on) {
    if (on == latch) return;
    latch = on;
    // what either way held goes
    releaseLR();
  }

  boolean latch() { return latch; }

  /** The menu's switch "Latch B": B latches by a tap (on) or is held while the finger is on it. */
  void setLatchB(boolean on) {
    if (on == latchB) return;
    latchB = on;
    releaseB();
  }

  boolean latchB() { return latchB; }

  private void releaseB() {
    if (b) host.latched(B, false);
    b = false;
    input.touchButton(CoreInput.GC_B, false);
  }

  private void releaseLR() {
    if (l) host.latched(L, false);
    if (r) host.latched(R, false);
    l = false;
    r = false;
    input.touchButton(CoreInput.GC_L, false);
    input.touchButton(CoreInput.GC_R, false);
  }

  private void centreLook() {
    lookX = 0;
    lookY = 0;
    updateC();
  }

  /** Everything let go (pause, the controls hidden, the editor), latched L, R and B too. */
  void reset() {
    lookFingers.clear();
    lookX = 0;
    lookY = 0;
    cx = 0;
    cy = 0;
    if (l) host.latched(L, false);
    if (r) host.latched(R, false);
    if (b) host.latched(B, false);
    l = false;
    r = false;
    b = false;
    input.touchClear();
  }
}
