package com.ast.re4decomp;

import java.util.Arrays;

/**
 * The game's actions on keys (the menu's "Button Mapping", wish of the project owner, 29.10.2026):
 * every action of the game on a key of the controller or the keyboard, and an action the GameCube
 * does with two buttons (fire: R held and A) on two keys again, the first held, the second pressed
 * (decisions of the project owner, 29.10.2026: the game's menus keep the device's default, a key an
 * other single action had moves to that one's old key). Independent of Android so the host tests can
 * check it (tests/ActionMapTests.java).
 *
 * A single action presses its GameCube button while its key is held. A combination (fire, reload,
 * slash) presses the button of the action it holds first (aim: R, knife: L) while its first key is
 * held, and its own (A, B) while the second is held too; the second key alone does only what it does
 * as a single action, if anything. The quick turn (back and run) and the quick-time events (A+B, L+R)
 * follow from the keys of their actions. Moving (keyboard only; the controller's left stick) tilts the
 * control stick fully.
 */
public final class ActionMap {
  // the actions, in the order of the page
  public static final int MOVE_FORWARD = 0, MOVE_BACK = 1, MOVE_LEFT = 2, MOVE_RIGHT = 3, ACTION = 4, RUN = 5,
      AIM = 6, FIRE = 7, RELOAD = 8, KNIFE = 9, SLASH = 10, ASHLEY = 11, INVENTORY = 12, MAP = 13, OPTIONS = 14,
      APP_MENU = 15, ACTIONS = 16;
  public static final int KIND_CONTROLLER = 0, KIND_KEYBOARD = 1;

  /** The GameCube button of each action (the moves: the stick; the app's menu: none). */
  static final int[] OUT = {0, 0, 0, 0, CoreInput.GC_A, CoreInput.GC_B, CoreInput.GC_R, CoreInput.GC_A,
      CoreInput.GC_B, CoreInput.GC_L, CoreInput.GC_A, CoreInput.GC_X, CoreInput.GC_Y, CoreInput.GC_Z,
      CoreInput.GC_START, 0};
  /** The action a combination holds with its first key; -1: a single key. */
  static final int[] HOLD = {-1, -1, -1, -1, -1, -1, -1, AIM, AIM, -1, KNIFE, -1, -1, -1, -1, -1};

  // android.view.KeyEvent key codes
  static final int KEY_A = 29, KEY_C = 31, KEY_D = 32, KEY_E = 33, KEY_F = 34, KEY_M = 41, KEY_Q = 45, KEY_R = 46,
      KEY_S = 47, KEY_W = 51, KEY_SHIFT_LEFT = 59, KEY_SPACE = 62, KEY_ENTER = 66, KEY_DEL = 67, KEY_TAB = 61,
      KEY_ESCAPE = 111, KEY_F1 = 131, KEY_NUMPAD_ENTER = 160;

  /**
   * The keyboard's keys for the game's menus and dialogs, at all times and not to be mapped: the arrows
   * are the D-pad, Enter confirms (A), Backspace goes back (B).
   */
  static int fixedKeyboardButton(int keyCode) {
    switch (keyCode) {
      case CoreInput.KEYCODE_DPAD_UP: return CoreInput.GC_UP;
      case CoreInput.KEYCODE_DPAD_DOWN: return CoreInput.GC_DOWN;
      case CoreInput.KEYCODE_DPAD_LEFT: return CoreInput.GC_LEFT;
      case CoreInput.KEYCODE_DPAD_RIGHT: return CoreInput.GC_RIGHT;
      case KEY_ENTER:
      case KEY_NUMPAD_ENTER: return CoreInput.GC_A;
      case KEY_DEL: return CoreInput.GC_B;
      default: return 0;
    }
  }

  /** Whether `action` is on the page of `kind` (the controller moves with its left stick). */
  public static boolean available(int kind, int action) {
    return action >= 0 && action < ACTIONS && (kind == KIND_KEYBOARD || action > MOVE_RIGHT);
  }

  public static boolean combination(int action) { return HOLD[action] >= 0; }

  public final int kind;
  private final int[][] keys = new int[ACTIONS][];

  private ActionMap(int kind) {
    this.kind = kind;
    for (int a = 0; a < ACTIONS; a++) keys[a] = new int[0];
  }

  /**
   * The default of `kind`. The controller's is its layout without a mapping (CoreInput): A action, B run,
   * R2 aim, R2+A fire, R2+B reload, L2 knife, L2+A slash, X Ashley, Y inventory, R1 map, START options,
   * SELECT the app's menu. The keyboard's: WASD move, E action, left Shift run, Space aim, Space+F fire,
   * Space+R reload, C knife, C+F slash, Q Ashley, Tab inventory, M map, Esc options, F1 the app's menu.
   */
  public static ActionMap defaults(int kind) {
    ActionMap m = new ActionMap(kind);
    if (kind == KIND_CONTROLLER) {
      int a = CoreInput.KEYCODE_BUTTON_A, b = CoreInput.KEYCODE_BUTTON_B, l2 = CoreInput.KEYCODE_BUTTON_L2,
          r2 = CoreInput.KEYCODE_BUTTON_R2;
      m.keys[ACTION] = new int[] {a};
      m.keys[RUN] = new int[] {b};
      m.keys[AIM] = new int[] {r2};
      m.keys[FIRE] = new int[] {r2, a};
      m.keys[RELOAD] = new int[] {r2, b};
      m.keys[KNIFE] = new int[] {l2};
      m.keys[SLASH] = new int[] {l2, a};
      m.keys[ASHLEY] = new int[] {CoreInput.KEYCODE_BUTTON_X};
      m.keys[INVENTORY] = new int[] {CoreInput.KEYCODE_BUTTON_Y};
      m.keys[MAP] = new int[] {CoreInput.KEYCODE_BUTTON_R1};
      m.keys[OPTIONS] = new int[] {CoreInput.KEYCODE_BUTTON_START};
      m.keys[APP_MENU] = new int[] {CoreInput.KEYCODE_BUTTON_SELECT};
    } else {
      m.keys[MOVE_FORWARD] = new int[] {KEY_W};
      m.keys[MOVE_BACK] = new int[] {KEY_S};
      m.keys[MOVE_LEFT] = new int[] {KEY_A};
      m.keys[MOVE_RIGHT] = new int[] {KEY_D};
      m.keys[ACTION] = new int[] {KEY_E};
      m.keys[RUN] = new int[] {KEY_SHIFT_LEFT};
      m.keys[AIM] = new int[] {KEY_SPACE};
      m.keys[FIRE] = new int[] {KEY_SPACE, KEY_F};
      m.keys[RELOAD] = new int[] {KEY_SPACE, KEY_R};
      m.keys[KNIFE] = new int[] {KEY_C};
      m.keys[SLASH] = new int[] {KEY_C, KEY_F};
      m.keys[ASHLEY] = new int[] {KEY_Q};
      m.keys[INVENTORY] = new int[] {KEY_TAB};
      m.keys[MAP] = new int[] {KEY_M};
      m.keys[OPTIONS] = new int[] {KEY_ESCAPE};
      m.keys[APP_MENU] = new int[] {KEY_F1};
    }
    return m;
  }

  /** The keys of `action`: one, two (held, pressed) or none. */
  public int[] keys(int action) { return keys[action].clone(); }

  public boolean isDefault() { return equals(defaults(kind)); }

  @Override public boolean equals(Object o) {
    if (!(o instanceof ActionMap)) return false;
    ActionMap m = (ActionMap) o;
    if (m.kind != kind) return false;
    for (int a = 0; a < ACTIONS; a++) if (!Arrays.equals(m.keys[a], keys[a])) return false;
    return true;
  }

  @Override public int hashCode() { return Arrays.deepHashCode(keys) * 31 + kind; }

  private ActionMap copy() {
    ActionMap m = new ActionMap(kind);
    for (int a = 0; a < ACTIONS; a++) m.keys[a] = keys[a].clone();
    return m;
  }

  /**
   * `action` on `newKeys` (one key for a single action, two different ones for a combination); null when
   * they do not fit, a key is a fixed keyboard key (or the controller's D-pad) or the action is not on this kind's page. A single
   * action's key that another single action had goes to that one's old key (they swap); combinations
   * may share keys with the others (aim's key is fire's first).
   */
  public ActionMap assign(int action, int[] newKeys) {
    if (!available(kind, action) || newKeys == null) return null;
    int need = combination(action) ? 2 : 1;
    if (newKeys.length != need || (need == 2 && newKeys[0] == newKeys[1])) return null;
    for (int k : newKeys) {
      if (k <= 0 || (kind == KIND_KEYBOARD ? fixedKeyboardButton(k) != 0 : CoreInput.dpad(k) != 0)) return null;
    }
    ActionMap m = copy();
    if (need == 1) {
      int[] old = m.keys[action];
      for (int a = 0; a < ACTIONS; a++) {
        if (a != action && !combination(a) && m.keys[a].length == 1 && m.keys[a][0] == newKeys[0]) {
          m.keys[a] = old.clone();
        }
      }
    }
    m.keys[action] = newKeys.clone();
    return m;
  }

  /** The single action whose key is `keyCode` (-1: none). */
  public int singleOn(int keyCode) {
    for (int a = 0; a < ACTIONS; a++) if (!combination(a) && keys[a].length == 1 && keys[a][0] == keyCode) return a;
    return -1;
  }

  /** Whether some action uses `keyCode` (alone or in a combination). */
  public boolean uses(int keyCode) {
    for (int[] k : keys) for (int c : k) if (c == keyCode) return true;
    return false;
  }

  /** The keys held of a device. */
  public interface Held { boolean held(int keyCode); }

  /** The GameCube buttons the held keys press. */
  public int buttons(Held h) {
    int out = 0;
    for (int a = 0; a < ACTIONS; a++) {
      int[] k = keys[a];
      if (k.length == 0 || !h.held(k[0])) continue;
      if (HOLD[a] < 0) {
        out |= OUT[a];
      } else {
        out |= OUT[HOLD[a]];
        if (h.held(k[1])) out |= OUT[a];
      }
    }
    return out;
  }

  /** The control stick of the move keys: x right, y down, -1..1 (a diagonal of length 1). */
  public float[] stick(Held h) {
    float x = (held(h, MOVE_RIGHT) ? 1 : 0) - (held(h, MOVE_LEFT) ? 1 : 0);
    float y = (held(h, MOVE_BACK) ? 1 : 0) - (held(h, MOVE_FORWARD) ? 1 : 0);
    if (x != 0 && y != 0) {
      x *= 0.70710677f;
      y *= 0.70710677f;
    }
    return new float[] {x, y};
  }

  private boolean held(Held h, int action) { return keys[action].length == 1 && h.held(keys[action][0]); }

  static final String[] NAMES = {"forward", "back", "left", "right", "action", "run", "aim", "fire", "reload",
      "knife", "slash", "Ashley", "inventory", "map", "options", "app menu"};

  /** For the log: "action = A, run = B, …, fire = R2+A" with the keys' names. */
  public String describe(java.util.function.IntFunction<String> keyName) {
    StringBuilder s = new StringBuilder();
    for (int a = 0; a < ACTIONS; a++) {
      if (!available(kind, a)) continue;
      s.append(s.length() == 0 ? "" : ", ").append(NAMES[a]).append(" = ");
      if (keys[a].length == 0) s.append("none");
      for (int i = 0; i < keys[a].length; i++) s.append(i == 0 ? "" : "+").append(keyName.apply(keys[a][i]));
    }
    return s.toString();
  }

  /** As kept in the preferences ("4=96;7=105+96"); null for the default. */
  public String encode() {
    if (isDefault()) return null;
    StringBuilder s = new StringBuilder();
    for (int a = 0; a < ACTIONS; a++) {
      if (!available(kind, a)) continue;
      if (s.length() > 0) s.append(';');
      s.append(a).append('=');
      for (int i = 0; i < keys[a].length; i++) s.append(i == 0 ? "" : "+").append(keys[a][i]);
    }
    return s.toString();
  }

  /** The preferences' text back; the default of `kind` when there is none or it does not fit. */
  public static ActionMap decode(int kind, String text) {
    if (text == null || text.isEmpty()) return defaults(kind);
    ActionMap m = new ActionMap(kind);
    try {
      for (String part : text.split(";")) {
        int eq = part.indexOf('=');
        if (eq < 0) return defaults(kind);
        int a = Integer.parseInt(part.substring(0, eq).trim());
        if (!available(kind, a)) return defaults(kind);
        String rest = part.substring(eq + 1).trim();
        if (rest.isEmpty()) continue;
        String[] codes = rest.split("\\+");
        int[] k = new int[codes.length];
        for (int i = 0; i < k.length; i++) k[i] = Integer.parseInt(codes[i].trim());
        if (k.length != (combination(a) ? 2 : 1)) return defaults(kind);
        m.keys[a] = k;
      }
    } catch (NumberFormatException e) {
      return defaults(kind);
    }
    return m;
  }
}
