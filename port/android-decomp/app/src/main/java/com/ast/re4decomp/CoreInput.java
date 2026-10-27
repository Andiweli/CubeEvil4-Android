package com.ast.re4decomp;

import java.util.HashMap;
import java.util.Map;

/**
 * The controller of the core mode: Android keys and axes become GameCube
 * controller 0 of the original game (port/whole/runtime/pad.cpp). The values
 * are raw like the hardware's; the game applies the SDK's dead zones and
 * ranges itself (PADClamp: sticks 15, triggers 30). Independent of Android so
 * the host tests can check it (tests/CoreInputTests.java).
 *
 * Layout (decision of the project owner, 16.10.2026): A, B, X, Y by their
 * labels (a switch swaps A/B and X/Y for controllers that send the other
 * codes), D-pad, START; R2 = R (ready weapon, analog), L2 = L (knife, analog),
 * R1 = Z (map), L1 free; left stick = control stick, right stick = C stick.
 * SELECT and R3 belong to the app (decision of the project owner,
 * 17.10.2026): SELECT pressed opens the app's menu (picture options, memory
 * card, error log), held for a second the game's debug menu; R3 saves
 * graphics data for the diagnosis when the tools are on. The touch controls
 * (CoreTouch, TouchControls) are a device of their own (TOUCH).
 *
 * Alternate controls (the menu's "Alternate Controls", wish of the project
 * owner, 19.10.2026): L1 = L (ready the knife, the trigger at its end stop),
 * L2 = R (ready the weapon, analog), R1 = Y (the inventory: weapons and
 * recovery), R2 = A past half way (fire, or slash with the knife ready),
 * Y = Z (map); A, B, X (Ashley: wait / follow), D-pad and START as above.
 * While L1 or L2 holds a weapon ready the right stick aims (the control stick
 * for the game) and the left one rests (wish of the project owner,
 * 20.10.2026: the game aims with the control stick and leaves the C stick
 * unused then, CameraQuasiFPS::calcDepressionRatio); otherwise the sticks as
 * above. The touch controls keep their GameCube buttons and sticks.
 *
 * Latching (the menu's comfort controls "Latch L and R" and "Latch B", for the touch controls and
 * the controller since 23.10.2026, wish of the project owner): a press of the controller's L or R
 * (the trigger past half way, with the alternate controls L1 and L2) holds it fully until the next
 * press, a press of B holds B (Leon keeps running). The touch controls latch their own buttons
 * (CoreTouch).
 *
 * Own bindings (the menu's "Controller configuration", wish of the project owner, 27.10.2026): each
 * function (the GameCube buttons and the app's menu) on any key of the controller, one key each;
 * null keeps the layout above. They apply in play while the alternate controls are off (those keep
 * their own layout and aiming); the game's menus and button prompts (the title, sub screens, "A Open",
 * the quick-time events: the core's bit 4 of re4_core_rumble, setMenu) keep the layout above, so a
 * prompt's button is the one it shows (wishes of the project owner, 27.10.2026). A function on L2 or R2 reads the trigger: L and R as the analog value, the
 * others pressed past half way. The D-pad of the hat axes counts as the D-pad keys.
 */
public final class CoreInput {
  // GameCube pad bits (PAD_BUTTON_*)
  public static final int GC_LEFT = 0x0001, GC_RIGHT = 0x0002, GC_DOWN = 0x0004, GC_UP = 0x0008,
      GC_Z = 0x0010, GC_R = 0x0020, GC_L = 0x0040, GC_A = 0x0100, GC_B = 0x0200, GC_X = 0x0400,
      GC_Y = 0x0800, GC_START = 0x1000;
  // what a key does for the app instead of the game
  public static final int APP_NONE = 0, APP_MENU = 1, APP_GX_DUMP = 2;
  // the functions of the controller configuration, in the order of its page
  public static final int F_A = 0, F_B = 1, F_X = 2, F_Y = 3, F_Z = 4, F_L = 5, F_R = 6, F_START = 7,
      F_UP = 8, F_DOWN = 9, F_LEFT = 10, F_RIGHT = 11, F_MENU = 12, FUNCTIONS = 13;
  /** The GameCube button of each function (F_MENU: none, the app's). */
  static final int[] FUNCTION_BITS = {GC_A, GC_B, GC_X, GC_Y, GC_Z, GC_L, GC_R, GC_START, GC_UP, GC_DOWN,
      GC_LEFT, GC_RIGHT, 0};

  // android.view.KeyEvent key codes
  static final int KEYCODE_DPAD_UP = 19, KEYCODE_DPAD_DOWN = 20, KEYCODE_DPAD_LEFT = 21,
      KEYCODE_DPAD_RIGHT = 22, KEYCODE_BUTTON_A = 96, KEYCODE_BUTTON_B = 97,
      KEYCODE_BUTTON_X = 99, KEYCODE_BUTTON_Y = 100, KEYCODE_BUTTON_L1 = 102,
      KEYCODE_BUTTON_R1 = 103, KEYCODE_BUTTON_L2 = 104, KEYCODE_BUTTON_R2 = 105,
      KEYCODE_BUTTON_THUMBL = 106, KEYCODE_BUTTON_THUMBR = 107, KEYCODE_BUTTON_START = 108,
      KEYCODE_BUTTON_SELECT = 109;

  /** The layout above as bindings (one key per function, F_A .. F_MENU). */
  public static int[] defaultBindings() {
    return new int[] {KEYCODE_BUTTON_A, KEYCODE_BUTTON_B, KEYCODE_BUTTON_X, KEYCODE_BUTTON_Y, KEYCODE_BUTTON_R1,
        KEYCODE_BUTTON_L2, KEYCODE_BUTTON_R2, KEYCODE_BUTTON_START, KEYCODE_DPAD_UP, KEYCODE_DPAD_DOWN,
        KEYCODE_DPAD_LEFT, KEYCODE_DPAD_RIGHT, KEYCODE_BUTTON_SELECT};
  }

  /** Bindings as kept in the preferences ("96,97,…"); null for the default layout. */
  public static String encode(int[] bindings) {
    if (bindings == null || java.util.Arrays.equals(bindings, defaultBindings())) return null;
    StringBuilder s = new StringBuilder();
    for (int i = 0; i < bindings.length; i++) s.append(i == 0 ? "" : ",").append(bindings[i]);
    return s.toString();
  }

  /** The preferences' text back; null when there is none or it is not FUNCTIONS distinct keys. */
  public static int[] decode(String text) {
    if (text == null || text.isEmpty()) return null;
    String[] parts = text.split(",");
    if (parts.length != FUNCTIONS) return null;
    int[] b = new int[FUNCTIONS];
    try {
      for (int i = 0; i < FUNCTIONS; i++) b[i] = Integer.parseInt(parts[i].trim());
    } catch (NumberFormatException e) {
      return null;
    }
    for (int i = 0; i < FUNCTIONS; i++) {
      if (b[i] <= 0) return null;
      for (int k = 0; k < i; k++) if (b[k] == b[i]) return null;
    }
    return b;
  }

  /**
   * `function` on `keyCode`: a function that had this key takes the old key of `function` (the two
   * swap, so every function keeps a key). Returns the new bindings.
   */
  public static int[] assign(int[] bindings, int function, int keyCode) {
    int[] b = (bindings == null ? defaultBindings() : bindings).clone();
    int old = b[function];
    for (int i = 0; i < b.length; i++) if (b[i] == keyCode) b[i] = old;
    b[function] = keyCode;
    return b;
  }

  /**
   * Deflection of a genuine controller at its gate in raw units: main stick
   * about 101, C stick about 92 (averages over real controllers, 0.794 and
   * 0.722 of 127). Full deflection then reaches the clamp (72 / 59) as on the
   * GameCube.
   */
  static final float MAIN_RAW = 101f, C_RAW = 92f;
  /**
   * Where an analog trigger also presses the digital L/R (the click of a GameCube trigger), and
   * where it lets go again: a trigger held near the click that jitters across it gave a press after
   * every jitter, and the core gives a new press a frame without the button (pad.cpp, every tap
   * counts since 26.10.2026): the weapon went down for a frame.
   */
  static final float TRIGGER_CLICK = 0.85f, TRIGGER_UNCLICK = 0.70f;
  /** Alternate controls: where R2 presses A (fire), and where it lets go (the same reason). */
  static final float FIRE_PRESS = 0.5f, FIRE_RELEASE = 0.35f;
  /** A trigger value the game takes as pressed (PADClamp: past 30 of 255). */
  static final int TRIGGER_PRESSED = 31;
  /** Latching: a trigger past this counts as a press, below LATCH_RELEASE as let go again. */
  static final float LATCH_PRESS = 0.5f, LATCH_RELEASE = 0.25f;

  /** One input device (the built-in controller may report keys and axes separately). */
  private static final class Device {
    int buttons;            // GameCube bits held through keys
    boolean l2Key, r2Key;   // L2/R2 as keys (triggers without an axis)
    boolean l1Key;          // L1 (the alternate controls' knife)
    int hats;               // D-pad from the hat axes
    float lx, ly, rx, ry;   // sticks -1..1, y down as Android reports it
    float lt, rt;           // analog triggers 0..1
    final java.util.Set<Integer> keys = new java.util.HashSet<>();  // keys held (own bindings)
  }

  private final Map<Integer, Device> devices = new HashMap<>();
  private boolean swapFace, alternate;
  private boolean latchLR, latchB;               // the menu's switches
  private boolean latchedL, latchedR, latchedB;  // held by the controller's latching
  private boolean pressedL, pressedR, pressedB;  // the controller's L, R, B at the last pad()
  private boolean clickL, clickR, fire;          // the triggers' digital L/R and fire at the last pad()
  private int[] bindings;                        // own bindings, null: the layout above
  private final boolean[] held = new boolean[FUNCTIONS];  // own bindings: a function on a trigger, pressed
  private boolean menu;                          // the game shows a menu: the layout above (setMenu)

  /**
   * The game shows a menu (true) or is played: the own bindings apply only in play. The keys held
   * now count on in the layout that applies from now on.
   */
  public void setMenu(boolean on) {
    if (on == menu) return;
    menu = on;
    for (Device d : devices.values()) {
      int bits = 0;
      for (int code : d.keys) {
        // (L1, L2, R2: their own flags, held in both layouts)
        if (code != KEYCODE_BUTTON_L1 && code != KEYCODE_BUTTON_L2 && code != KEYCODE_BUTTON_R2) bits |= layoutButton(code);
      }
      d.buttons = bits;
    }
    java.util.Arrays.fill(held, false);
  }

  public boolean menu() { return menu; }

  /** Own bindings (FUNCTIONS keys, F_A .. F_MENU); null or the default layout: the layout above. */
  public void setBindings(int[] b) {
    clear();
    bindings = b == null || b.length != FUNCTIONS || java.util.Arrays.equals(b, defaultBindings()) ? null : b.clone();
  }

  /** The own bindings in use, null without. */
  public int[] bindings() { return bindings == null ? null : bindings.clone(); }

  /** Own bindings apply (set, and the alternate controls off). */
  private boolean own() { return bindings != null && !alternate && !menu; }

  /** The function on `keyCode` with the own bindings, -1 for none. */
  private int functionOf(int keyCode) {
    for (int i = 0; i < FUNCTIONS; i++) if (bindings[i] == keyCode) return i;
    return -1;
  }

  /** The menu's "Latch L and R" and "Latch B" for the controller (see above). */
  public void setLatch(boolean lr, boolean b) {
    if (lr != latchLR) latchedL = latchedR = false;
    if (b != latchB) latchedB = false;
    latchLR = lr;
    latchB = b;
  }

  public boolean latchLR() { return latchLR; }

  public boolean latchB() { return latchB; }

  /** Swaps A/B and X/Y (controllers that send the codes of the other layout). */
  public void setSwapFace(boolean swap) {
    if (swap != swapFace) clear();
    swapFace = swap;
  }

  public boolean swapFace() { return swapFace; }

  /** The alternate controls (see above) or the agreed layout. */
  public void setAlternate(boolean on) {
    if (on != alternate) clear();
    alternate = on;
  }

  public boolean alternate() { return alternate; }

  /**
   * The GameCube button of an Android key, 0 for none (the triggers: L2 and
   * R2, with the alternate controls L1 and L2, the one they drive).
   */
  public int gcButton(int keyCode) {
    if (own()) {
      int f = functionOf(keyCode);
      return f < 0 ? 0 : FUNCTION_BITS[f];
    }
    return layoutButton(keyCode);
  }

  /** The GameCube button of a key in the layout above (no own bindings). */
  private int layoutButton(int keyCode) {
    int label = keyCode;
    if (swapFace) {
      switch (keyCode) {
        case KEYCODE_BUTTON_A: label = KEYCODE_BUTTON_B; break;
        case KEYCODE_BUTTON_B: label = KEYCODE_BUTTON_A; break;
        case KEYCODE_BUTTON_X: label = KEYCODE_BUTTON_Y; break;
        case KEYCODE_BUTTON_Y: label = KEYCODE_BUTTON_X; break;
        default: break;
      }
    }
    switch (label) {
      case KEYCODE_BUTTON_A: return GC_A;
      case KEYCODE_BUTTON_B: return GC_B;
      case KEYCODE_BUTTON_X: return GC_X;
      case KEYCODE_BUTTON_Y: return alternate ? GC_Z : GC_Y;
      case KEYCODE_BUTTON_L1: return alternate ? GC_L : 0;
      case KEYCODE_BUTTON_R1: return alternate ? GC_Y : GC_Z;
      case KEYCODE_BUTTON_L2: return alternate ? GC_R : GC_L;
      case KEYCODE_BUTTON_R2: return alternate ? GC_A : GC_R;
      case KEYCODE_BUTTON_START: return GC_START;
      case KEYCODE_DPAD_UP: return GC_UP;
      case KEYCODE_DPAD_DOWN: return GC_DOWN;
      case KEYCODE_DPAD_LEFT: return GC_LEFT;
      case KEYCODE_DPAD_RIGHT: return GC_RIGHT;
      default: return 0;
    }
  }

  /** What a key does for the app (menu, graphics capture), APP_NONE for the rest. */
  public static int appFunction(int keyCode) {
    switch (keyCode) {
      case KEYCODE_BUTTON_SELECT: return APP_MENU;
      case KEYCODE_BUTTON_THUMBR: return APP_GX_DUMP;
      default: return APP_NONE;
    }
  }

  /** What a key does for the app, with the own bindings (the menu on its key; R3 when it has no function). */
  public int appFunctionOf(int keyCode) {
    if (!own()) return appFunction(keyCode);
    int f = functionOf(keyCode);
    if (f == F_MENU) return APP_MENU;
    return f < 0 && keyCode == KEYCODE_BUTTON_THUMBR ? APP_GX_DUMP : APP_NONE;
  }

  /**
   * Keys the game controller takes without a function (L1 but with the
   * alternate controls, L3): consumed so Android does not act on them.
   */
  public static boolean unused(int keyCode) {
    return keyCode == KEYCODE_BUTTON_L1 || keyCode == KEYCODE_BUTTON_THUMBL;
  }

  private Device device(int id) {
    Device d = devices.get(id);
    if (d == null) {
      d = new Device();
      devices.put(id, d);
    }
    return d;
  }

  /** A key of `device` went down or up; false when it is no game button. */
  public boolean key(int device, int keyCode, boolean down) {
    Device d = device(device);
    // the keys held, in either layout (setMenu changes between them while a key is held)
    if (down) d.keys.add(keyCode); else d.keys.remove(keyCode);
    if (own()) {
      if (keyCode == KEYCODE_BUTTON_L1) d.l1Key = down;
      if (keyCode == KEYCODE_BUTTON_L2) d.l2Key = down;
      if (keyCode == KEYCODE_BUTTON_R2) d.r2Key = down;
      int bit = layoutButton(keyCode);
      if (bit != 0 && keyCode != KEYCODE_BUTTON_L1 && keyCode != KEYCODE_BUTTON_L2 && keyCode != KEYCODE_BUTTON_R2) {
        d.buttons = down ? d.buttons | bit : d.buttons & ~bit;
      }
      int f = functionOf(keyCode);
      return f >= 0 && f != F_MENU;
    }
    if (keyCode == KEYCODE_BUTTON_L1) {
      d.l1Key = down;
      return alternate;
    }
    if (keyCode == KEYCODE_BUTTON_L2) {
      d.l2Key = down;
      return true;
    }
    if (keyCode == KEYCODE_BUTTON_R2) {
      d.r2Key = down;
      return true;
    }
    int bit = gcButton(keyCode);
    if (bit == 0) return false;
    d.buttons = down ? d.buttons | bit : d.buttons & ~bit;
    return true;
  }

  /**
   * The axes of `device`: sticks -1..1 (y down, as Android reports it),
   * triggers 0..1, hat -1..1; 0 for axes the device does not have.
   */
  public void motion(int device, float lx, float ly, float rx, float ry, float lt, float rt,
      float hatX, float hatY) {
    Device d = device(device);
    d.lx = clamp(lx, -1, 1);
    d.ly = clamp(ly, -1, 1);
    d.rx = clamp(rx, -1, 1);
    d.ry = clamp(ry, -1, 1);
    d.lt = clamp(lt, 0, 1);
    d.rt = clamp(rt, 0, 1);
    d.hats = (hatY < -0.5f ? GC_UP : 0) | (hatY > 0.5f ? GC_DOWN : 0)
        | (hatX < -0.5f ? GC_LEFT : 0) | (hatX > 0.5f ? GC_RIGHT : 0);
  }

  /** The touch controls' device (CoreTouch): no Android device has this id. */
  static final int TOUCH = Integer.MIN_VALUE;

  /** Buttons of the touch controls (GameCube bits); L and R are the triggers at their end stop. */
  public void touchButton(int bits, boolean down) {
    Device d = device(TOUCH);
    if ((bits & GC_L) != 0) d.l2Key = down;
    if ((bits & GC_R) != 0) d.r2Key = down;
    bits &= ~(GC_L | GC_R);
    d.buttons = down ? d.buttons | bits : d.buttons & ~bits;
  }

  /** A stick of the touch controls (the C stick or the control stick), -1..1 with y down. */
  public void touchStick(boolean cStick, float x, float y) {
    Device d = device(TOUCH);
    if (cStick) {
      d.rx = clamp(x, -1, 1);
      d.ry = clamp(y, -1, 1);
    } else {
      d.lx = clamp(x, -1, 1);
      d.ly = clamp(y, -1, 1);
    }
  }

  /** The touch controls hold nothing any more. */
  public void touchClear() { devices.remove(TOUCH); }

  /** A device went away: its keys count as released. */
  public void remove(int device) { devices.remove(device); }

  /** Everything released (the app lost the focus, the layout changed), the latched buttons too. */
  public void clear() {
    devices.clear();
    latchedL = latchedR = latchedB = false;
    pressedL = pressedR = pressedB = false;
    java.util.Arrays.fill(held, false);
  }

  /** Own bindings: how far `device` presses the key of `function` (a trigger 0..1, a key 0 or 1). */
  private float value(Device d, int function) {
    int code = bindings[function];
    if (code == KEYCODE_BUTTON_L2) return d.l2Key ? 1f : d.lt;
    if (code == KEYCODE_BUTTON_R2) return d.r2Key ? 1f : d.rt;
    int hat = code == KEYCODE_DPAD_UP ? GC_UP : code == KEYCODE_DPAD_DOWN ? GC_DOWN
        : code == KEYCODE_DPAD_LEFT ? GC_LEFT : code == KEYCODE_DPAD_RIGHT ? GC_RIGHT : 0;
    return d.keys.contains(code) || (d.hats & hat) != 0 ? 1f : 0f;
  }

  /**
   * The GameCube controller for the core: buttons, main stick x/y, C stick
   * x/y (raw, up positive), triggers L/R (0..255). Buttons of all devices
   * count; of each stick and trigger the device that moves it furthest.
   */
  public int[] pad() {
    // the controller's L, R (0..1) and B, of all its devices: with the alternate controls L1 = L
    // at its end stop and L2 = R; then latched where the menu says so
    float padL = 0, padR = 0;
    boolean padB = false;
    boolean own = own();
    int ownButtons = 0;  // own bindings: the controller's buttons but L, R, B
    if (own) {
      float[] most = new float[FUNCTIONS];
      for (Map.Entry<Integer, Device> e : devices.entrySet()) {
        if (e.getKey() == TOUCH) continue;
        for (int f = 0; f < F_MENU; f++) most[f] = Math.max(most[f], value(e.getValue(), f));
      }
      padL = most[F_L];
      padR = most[F_R];
      for (int f = 0; f < F_MENU; f++) {
        if (f == F_L || f == F_R) continue;
        held[f] = most[f] >= (held[f] ? FIRE_RELEASE : FIRE_PRESS);
        if (held[f] && f != F_B) ownButtons |= FUNCTION_BITS[f];
      }
      padB = held[F_B];
    }
    for (Map.Entry<Integer, Device> e : devices.entrySet()) {
      if (own || e.getKey() == TOUCH) continue;
      Device d = e.getValue();
      float l2 = d.l2Key ? 1f : d.lt, r2 = d.r2Key ? 1f : d.rt;
      padL = Math.max(padL, alternate ? (d.l1Key ? 1f : 0f) : l2);
      padR = Math.max(padR, alternate ? l2 : r2);
      padB |= (d.buttons & GC_B) != 0;
    }
    if (latchLR) {
      boolean downL = padL >= (pressedL ? LATCH_RELEASE : LATCH_PRESS);
      boolean downR = padR >= (pressedR ? LATCH_RELEASE : LATCH_PRESS);
      if (downL && !pressedL) latchedL = !latchedL;
      if (downR && !pressedR) latchedR = !latchedR;
      pressedL = downL;
      pressedR = downR;
      padL = latchedL ? 1f : 0f;
      padR = latchedR ? 1f : 0f;
    }
    if (latchB) {
      if (padB && !pressedB) latchedB = !latchedB;
      pressedB = padB;
      padB = latchedB;
    }
    // the alternate controls aim with the right stick while L1 or L2 holds a weapon ready (on any
    // device: the built-in controller may report the trigger and the sticks apart)
    boolean aiming = alternate
        && (Math.round(padL * 255f) >= TRIGGER_PRESSED || Math.round(padR * 255f) >= TRIGGER_PRESSED);
    int buttons = (padB ? GC_B : 0) | ownButtons;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = padL, rt = padR, fireR2 = 0;
    for (Map.Entry<Integer, Device> e : devices.entrySet()) {
      Device d = e.getValue();
      boolean touch = e.getKey() == TOUCH;
      if (touch) buttons |= d.buttons | d.hats;
      else if (!own) buttons |= (d.buttons & ~GC_B) | d.hats;
      float mainX = d.lx, mainY = d.ly, cX = d.rx, cY = d.ry;
      if (aiming && !touch) {
        mainX = d.rx;
        mainY = d.ry;
        cX = cY = 0;
      }
      if (mainX * mainX + mainY * mainY > lx * lx + ly * ly) {
        lx = mainX;
        ly = mainY;
      }
      if (cX * cX + cY * cY > rx * rx + ry * ry) {
        rx = cX;
        ry = cY;
      }
      float l2 = d.l2Key ? 1f : d.lt, r2 = d.r2Key ? 1f : d.rt;
      if (touch) {
        lt = Math.max(lt, l2);
        rt = Math.max(rt, r2);
      } else if (alternate && !own) {
        // (L1 = L and L2 = R above) R2 = A
        fireR2 = Math.max(fireR2, r2);
      }
    }
    fire = alternate && fireR2 >= (fire ? FIRE_RELEASE : FIRE_PRESS);
    if (fire) buttons |= GC_A;
    // the triggers' digital L/R: only at the end stop, the game reads the analog
    // value (PadRead: any value past the dead zone counts as L/R)
    clickL = lt >= (clickL ? TRIGGER_UNCLICK : TRIGGER_CLICK);
    clickR = rt >= (clickR ? TRIGGER_UNCLICK : TRIGGER_CLICK);
    if (clickL) buttons |= GC_L;
    if (clickR) buttons |= GC_R;
    return new int[] {buttons, raw(lx, MAIN_RAW), raw(-ly, MAIN_RAW), raw(rx, C_RAW),
        raw(-ry, C_RAW), Math.round(lt * 255f), Math.round(rt * 255f)};
  }

  private static int raw(float v, float range) {
    return Math.round(v * range);
  }

  private static float clamp(float v, float min, float max) {
    if (Float.isNaN(v) || Float.isInfinite(v)) return 0;
    return Math.max(min, Math.min(max, v));
  }

  /** GameCube buttons as text ("A+B"), for the overlay and the log. */
  public static String names(int bits) {
    StringBuilder s = new StringBuilder();
    String[] names = {"LEFT", "RIGHT", "DOWN", "UP", "Z", "R", "L", "", "A", "B", "X", "Y", "START"};
    for (int i = 0; i < names.length; i++) {
      if ((bits & (1 << i)) != 0 && !names[i].isEmpty()) {
        if (s.length() > 0) s.append('+');
        s.append(names[i]);
      }
    }
    return s.length() == 0 ? "-" : s.toString();
  }
}
