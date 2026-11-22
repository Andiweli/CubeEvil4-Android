package com.ast.re4decomp;

/**
 * When the touch controls show (TouchControls), by the menu's setting "Touch Controls" (decision
 * of the project owner, 21.10.2026):
 * <ul>
 * <li>AUTO (the default): while no controller is connected; with one, after a tap on the screen
 * until the controller's next button or stick (variant A of 21.10.2026: a Redmi 15 5G reported a
 * controller nobody had connected, the controls stayed hidden and with them the menu).</li>
 * <li>FORCE: always (until 21.10.2026 the switch "Use Touch Controls").</li>
 * <li>SUPPRESS: not while a controller is connected, a tap does not bring them back (taking
 * screenshots with a controller in the hand). Without any controller they show, and a tap shows
 * them as long as no controller was used since the app started: a device that only reports a
 * controller must not leave the player without any way to play or to open the menu.</li>
 * </ul>
 * For SUPPRESS a keyboard or a mouse counts as a controller too, and so does a PC or a Chromebook (1.1.1, the project
 * owner: "Auf einem Chromebook mit Touchscreen wird die Touchsteuerung nicht mit 'Unterdrücken' ausgeblendet": a
 * Chromebook has its keyboard and touchpad but rarely a gamepad, so the controls stayed). AUTO keeps counting gamepads
 * only (a phone with a Bluetooth keyboard as before).
 * Pure Java (tests/TouchShowRuleTests.java).
 */
final class TouchShowRule {
  static final int AUTO = 0, FORCE = 1, SUPPRESS = 2;

  /** A stick, trigger or hat moved this far (of 1) is a deliberate use, not a resting stick's noise. */
  static final float MOVED = 0.5f;

  private int mode = AUTO;
  // touched: the screen since the controller's last use; used: a controller since the app started
  private boolean touched, used;

  /** The menu's setting (AUTO, FORCE, SUPPRESS; anything else counts as AUTO). */
  void setMode(int mode) {
    this.mode = mode == FORCE || mode == SUPPRESS ? mode : AUTO;
  }

  int mode() { return mode; }

  /** What counts as a connected controller for shown(): a gamepad; with SUPPRESS also a keyboard or a mouse
   *  (`keyboardOrMouse`) or a PC / Chromebook (`pc`, ChromeOS: its built-in keyboard and touchpad). */
  static boolean counts(int mode, boolean gamepad, boolean keyboardOrMouse, boolean pc) {
    return gamepad || (mode == SUPPRESS && (keyboardOrMouse || pc));
  }

  /** Shown now, with a controller connected or not. */
  boolean shown(boolean controller) {
    if (mode == FORCE || !controller) return true;
    return touched && (mode == AUTO || !used);
  }

  /** The screen was touched while the controls were hidden; true when that changes the rule. */
  boolean touch() {
    if (mode == SUPPRESS && used) return false;
    boolean was = touched;
    touched = true;
    return !was;
  }

  /** A controller's button or stick was used; true when that changes the rule. */
  boolean controller() {
    boolean was = touched;
    touched = false;
    used = true;
    return was;
  }

  /** The screen was touched since the controller's last use. */
  boolean touched() { return touched; }

  /** The menu's name of a setting, for the log. */
  static String name(int mode) {
    return mode == FORCE ? "force" : mode == SUPPRESS ? "suppress" : "auto";
  }

  /** Any of the axes (sticks and hat -1..1, triggers 0..1) moved deliberately. */
  static boolean moved(float... axes) {
    for (float a : axes) {
      if (Math.abs(a) > MOVED) return true;
    }
    return false;
  }
}
