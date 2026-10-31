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
 * Button mapping (the menu's "Button Mapping", wish of the project owner, 29.10.2026; ActionMap): every
 * action of the game on keys of the controller or of a keyboard, the GameCube's two-button actions
 * (fire: R + A) on two keys again. A controller with its own mapping and every keyboard go that way
 * (a controller without one keeps the layout above unchanged, the alternate controls theirs); in the
 * game's menus (the title, the sub screens: the core's menu bit, setMenu) the device's default applies
 * (decision of the project owner, 29.10.2026). Not while a button prompt is shown: switching there made
 * the first version of 27.10.2026 unusable (a key changed its meaning whenever a prompt came and went).
 * The keyboard's arrows, Enter and Backspace are the D-pad, A and B at all times. The free keys of
 * 27.10.2026 (X, Y, L1, R1, L3, R3) went into the mapping.
 */
public final class CoreInput {
  // GameCube pad bits (PAD_BUTTON_*)
  public static final int GC_LEFT = 0x0001, GC_RIGHT = 0x0002, GC_DOWN = 0x0004, GC_UP = 0x0008,
      GC_Z = 0x0010, GC_R = 0x0020, GC_L = 0x0040, GC_A = 0x0100, GC_B = 0x0200, GC_X = 0x0400,
      GC_Y = 0x0800, GC_START = 0x1000;
  // what a key does for the app instead of the game
  public static final int APP_NONE = 0, APP_MENU = 1, APP_GX_DUMP = 2;
  // android.view.KeyEvent key codes
  static final int KEYCODE_DPAD_UP = 19, KEYCODE_DPAD_DOWN = 20, KEYCODE_DPAD_LEFT = 21,
      KEYCODE_DPAD_RIGHT = 22, KEYCODE_BUTTON_A = 96, KEYCODE_BUTTON_B = 97,
      KEYCODE_BUTTON_X = 99, KEYCODE_BUTTON_Y = 100, KEYCODE_BUTTON_L1 = 102,
      KEYCODE_BUTTON_R1 = 103, KEYCODE_BUTTON_L2 = 104, KEYCODE_BUTTON_R2 = 105,
      KEYCODE_BUTTON_THUMBL = 106, KEYCODE_BUTTON_THUMBR = 107, KEYCODE_BUTTON_START = 108,
      KEYCODE_BUTTON_SELECT = 109;

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
    boolean keyboard;       // a keyboard (keyboardKey): its keys go through the keyboard's mapping
    final java.util.Set<Integer> held = new java.util.HashSet<>();  // the keys held (the mapping reads them)
    boolean trigL, trigR;   // the triggers past half way, as L2 and R2 for the mapping
    int mapped;             // pad(): the GameCube buttons of the mapping, -1 the layout above
    int hats;               // D-pad from the hat axes
    float lx, ly, rx, ry;   // sticks -1..1, y down as Android reports it
    float lt, rt;           // analog triggers 0..1
    // the touch controls' two ways to the C stick (touchLook): the look zone and the C stick control
    float lookX, lookY, cStickX, cStickY;
  }

  private final Map<Integer, Device> devices = new HashMap<>();
  private boolean swapFace, alternate;
  private boolean latchLR, latchB;               // the menu's switches
  private boolean latchedL, latchedR, latchedB;  // held by the controller's latching
  private boolean pressedL, pressedR, pressedB;  // the controller's L, R, B at the last pad()
  private boolean clickL, clickR, fire;          // the triggers' digital L/R and fire at the last pad()
  private ActionMap controllerMap;               // the controller's own mapping, null: the layout above
  private ActionMap keyboardMap = ActionMap.defaults(ActionMap.KIND_KEYBOARD);
  private static final ActionMap KEYBOARD_DEFAULT = ActionMap.defaults(ActionMap.KIND_KEYBOARD);
  private boolean menu;                          // the game shows a menu (setMenu)
  // the switch between the own mapping and the menus' default (user report 30.10.2026: "releasing the button
  // can count as a second input"; START held as the inventory opened came as a new START press there): a
  // GameCube button that a key held over the switch gives only in the new mapping waits until it is let go;
  // the ones it gave before as well stay held (B running while a prompt shows). Back in the game the held
  // actions B (run), R and L (ready the weapon, the knife) come at once (tester report 30.10.2026: A held as
  // the inventory closed, Leon did not run until it was pressed again)
  private static final int HELD_ACTIONS = GC_B | GC_R | GC_L;
  private boolean menuChanged;
  private int lastButtons, carried;

  /** The controller's mapping; null or the default: the layout above. */
  public void setControllerMap(ActionMap map) {
    controllerMap = map == null || map.isDefault() ? null : map;
  }

  public ActionMap controllerMap() {
    return controllerMap == null ? ActionMap.defaults(ActionMap.KIND_CONTROLLER) : controllerMap;
  }

  /** Whether the controller has a mapping of its own. */
  public boolean ownControllerMap() { return controllerMap != null; }

  /** The keyboard's mapping (null: the default). */
  public void setKeyboardMap(ActionMap map) {
    keyboardMap = map == null ? ActionMap.defaults(ActionMap.KIND_KEYBOARD) : map;
  }

  public ActionMap keyboardMap() { return keyboardMap; }

  /** The game shows a menu (the core's menu bit): the devices' defaults, not their mappings. */
  public void setMenu(boolean on) {
    if (on != menu) {
      // only when a key is held at the switch (the next pad() compares the buttons with the last ones)
      boolean held = false;
      for (Device d : devices.values())
        held |= !d.held.isEmpty() || d.buttons != 0 || d.trigL || d.trigR || d.l2Key || d.r2Key;
      menuChanged = held;
    }
    menu = on;
  }

  public boolean menu() { return menu; }

  /** The mapping a key of `keyboard` (or the controller) goes through now; null: the layout above. */
  private ActionMap mapping(boolean keyboard) {
    if (keyboard) return menu ? KEYBOARD_DEFAULT : keyboardMap;
    return controllerMap != null && !alternate && !menu ? controllerMap : null;
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
   * Touch aiming (the menu's "Touch Aiming", wish of the project owner, 28.10.2026): while L or R
   * holds a weapon ready, the look zone (AIM_LOOK) or the touch C stick (AIM_C_STICK) moves the aim
   * (AIM_MOVE_STICK, the default: the move stick as on the GameCube); the other one stays the C stick.
   * Not aiming, both are the C stick as before. The touch C stick aims as the control stick does; the
   * look zone by swipes (user report, 29.10.2026: as in mobile shooters, the aim follows the finger and
   * stops when it stops): its movement goes to the core as angles (takeAimSwipe, CoreBridge.aimTurn).
   */
  static final int AIM_MOVE_STICK = 0, AIM_LOOK = 1, AIM_C_STICK = 2;
  /** The swipe's turn: radians per share of the screen's shorter side (a swipe across it turns about 90 degrees). */
  static final float SWIPE_RADIANS = 1.6f;
  private int touchAim = AIM_MOVE_STICK;
  private boolean touchAiming;          // at the last pad(): L or R held with touch aiming
  private float swipeX, swipeY;         // the look zone's swipe since takeAimSwipe (shares, y down)

  public void setTouchAim(int mode) {
    touchAim = mode == AIM_LOOK || mode == AIM_C_STICK ? mode : AIM_MOVE_STICK;
  }

  public int touchAim() { return touchAim; }

  /**
   * The look zone's finger moved by dx/dy (shares of the shorter side, y down): true when it aims by
   * the swipe (AIM_LOOK while L or R is held, as the last pad() found), then it does not look.
   */
  public boolean touchSwipe(float dx, float dy) {
    if (touchAim != AIM_LOOK || !touchAiming) return false;
    swipeX += dx;
    swipeY += dy;
    return true;
  }

  /** The swipe since the last call as the core's yaw and pitch (radians, left and up positive); null: none. */
  public float[] takeAimSwipe() {
    if (swipeX == 0 && swipeY == 0) return null;
    float[] turn = {-swipeX * SWIPE_RADIANS, -swipeY * SWIPE_RADIANS};
    swipeX = swipeY = 0;
    return turn;
  }

  /**
   * The GameCube button of an Android key, 0 for none (the triggers: L2 and
   * R2, with the alternate controls L1 and L2, the one they drive).
   */
  public int gcButton(int keyCode) {
    ActionMap m = mapping(false);
    if (m != null) return m.buttons(code -> code == faceLabel(keyCode)) | dpad(keyCode);
    return layoutButton(keyCode);
  }

  /** The GameCube button of a key of a keyboard now (alone; for the log). */
  public int keyboardButton(int keyCode) {
    return mapping(true).buttons(code -> code == keyCode) | ActionMap.fixedKeyboardButton(keyCode);
  }

  /** The GameCube button of a key in the layout above (no mapping). */
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

  /** What a key of a controller or of a keyboard does for the app now (its mapping's menu key; R3 without a function). */
  public int appFunctionOf(int keyCode, boolean keyboard) {
    ActionMap m = mapping(keyboard);
    if (m == null) return appFunction(keyCode);
    if (m.singleOn(keyCode) == ActionMap.APP_MENU) return APP_MENU;
    return !keyboard && keyCode == KEYCODE_BUTTON_THUMBR && !m.uses(keyCode) ? APP_GX_DUMP : APP_NONE;
  }

  /** The D-pad's GameCube button of a D-pad key (or a keyboard's arrow), 0 for others. */
  static int dpad(int keyCode) {
    switch (keyCode) {
      case KEYCODE_DPAD_UP: return GC_UP;
      case KEYCODE_DPAD_DOWN: return GC_DOWN;
      case KEYCODE_DPAD_LEFT: return GC_LEFT;
      case KEYCODE_DPAD_RIGHT: return GC_RIGHT;
      default: return 0;
    }
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
    int label = faceLabel(keyCode);
    if (down) d.held.add(label); else d.held.remove(label);
    ActionMap own = mapping(false);
    if (own != null && (own.uses(label) || dpad(label) != 0)) {
      // the layout above keeps its own record too (for the game's menus and the alternate controls)
      keyLayout(d, keyCode, down);
      return true;
    }
    return keyLayout(d, keyCode, down);
  }

  /**
   * The keyboards and the mouse: one device for the game (KEYBOARD), so a combination may take a key and a
   * mouse button (right button + R reloads).
   */
  static final int KEYBOARD = -3;

  /**
   * A key of a keyboard or a mouse button (ActionMap.MOUSE_*); true when the game takes it (its mapping,
   * the default for the menus, the fixed keys). `device`: Android's, for the log only.
   */
  public boolean keyboardKey(int device, int keyCode, boolean down) {
    Device d = device(KEYBOARD);
    d.keyboard = true;
    if (down) d.held.add(keyCode); else d.held.remove(keyCode);
    return keyboardMap.uses(keyCode) || KEYBOARD_DEFAULT.uses(keyCode) || ActionMap.fixedKeyboardButton(keyCode) != 0;
  }

  /**
   * The mouse moving (captured, CoreActivity): the C stick of the keyboard and mouse, -1..1 with y down
   * (the camera; while aiming the core turns the aim by the movement instead, CoreBridge.aimTurn); 0, 0:
   * the mouse rests.
   */
  public void mouseLook(float x, float y) {
    Device d = device(KEYBOARD);
    d.keyboard = true;
    d.rx = clamp(x, -1, 1);
    d.ry = clamp(y, -1, 1);
  }

  /** A key as its label: with "swap A/B and X/Y" the controller's A sends B's code and so on. */
  public int faceLabel(int keyCode) {
    if (!swapFace) return keyCode;
    switch (keyCode) {
      case KEYCODE_BUTTON_A: return KEYCODE_BUTTON_B;
      case KEYCODE_BUTTON_B: return KEYCODE_BUTTON_A;
      case KEYCODE_BUTTON_X: return KEYCODE_BUTTON_Y;
      case KEYCODE_BUTTON_Y: return KEYCODE_BUTTON_X;
      default: return keyCode;
    }
  }

  /** The layout above: what a key does without a mapping. */
  private boolean keyLayout(Device d, int keyCode, boolean down) {
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
    int bit = layoutButton(keyCode);
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

  /**
   * The touch controls' look zone and C stick control, -1..1 with y down: the C stick is the one that
   * tilts further; with touch aiming one of them aims while L or R is held (pad).
   */
  public void touchLook(float lookX, float lookY, float cStickX, float cStickY) {
    Device d = device(TOUCH);
    d.lookX = clamp(lookX, -1, 1);
    d.lookY = clamp(lookY, -1, 1);
    d.cStickX = clamp(cStickX, -1, 1);
    d.cStickY = clamp(cStickY, -1, 1);
    boolean look = d.lookX * d.lookX + d.lookY * d.lookY > d.cStickX * d.cStickX + d.cStickY * d.cStickY;
    d.rx = look ? d.lookX : d.cStickX;
    d.ry = look ? d.lookY : d.cStickY;
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
    for (Map.Entry<Integer, Device> e : devices.entrySet()) {
      if (e.getKey() == TOUCH) continue;
      Device d = e.getValue();
      ActionMap m = mapping(d.keyboard);
      d.mapped = -1;
      if (m != null) {
        // the mapping: the held keys, the triggers past half way as L2 and R2; L, R and B through the
        // latching below, the rest in the loop after it
        d.trigL = d.lt >= (d.trigL ? LATCH_RELEASE : LATCH_PRESS);
        d.trigR = d.rt >= (d.trigR ? LATCH_RELEASE : LATCH_PRESS);
        final Device dev = d;
        int bits = m.buttons(code -> dev.held.contains(code) || code == KEYCODE_BUTTON_L2 && (dev.trigL || dev.l2Key)
            || code == KEYCODE_BUTTON_R2 && (dev.trigR || dev.r2Key));
        for (int code : d.held) bits |= d.keyboard ? ActionMap.fixedKeyboardButton(code) : dpad(code);
        if (!d.keyboard) bits |= d.hats;
        d.mapped = bits;
        if ((bits & GC_L) != 0) padL = 1f;
        if ((bits & GC_R) != 0) padR = 1f;
        padB |= (bits & GC_B) != 0;
        continue;
      }
      if (d.keyboard) continue;
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
    // touch aiming: L or R held, on the touch controls or any controller
    Device touchDevice = devices.get(TOUCH);
    boolean touchAiming = touchAim != AIM_MOVE_STICK
        && (touchDevice != null && (touchDevice.l2Key || touchDevice.r2Key)
            || Math.round(padL * 255f) >= TRIGGER_PRESSED || Math.round(padR * 255f) >= TRIGGER_PRESSED);
    if (!touchAiming) swipeX = swipeY = 0;  // a swipe left over from the aim before turns nothing
    this.touchAiming = touchAiming;
    int buttons = padB ? GC_B : 0;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = padL, rt = padR, fireR2 = 0;
    for (Map.Entry<Integer, Device> e : devices.entrySet()) {
      Device d = e.getValue();
      boolean touch = e.getKey() == TOUCH;
      if (!touch && d.mapped >= 0) {
        // the mapping: its buttons (L, R, B above), the keyboard's move keys or the controller's sticks
        buttons |= d.mapped & ~(GC_L | GC_R | GC_B);
        float mainX = d.lx, mainY = d.ly, cX = d.rx, cY = d.ry;
        if (d.keyboard) {
          final Device dev = d;
          float[] st = mapping(true).stick(dev.held::contains);
          mainX = st[0];
          mainY = st[1];
          cX = d.rx;  // the mouse (mouseLook)
          cY = d.ry;
        }
        if (mainX * mainX + mainY * mainY > lx * lx + ly * ly) {
          lx = mainX;
          ly = mainY;
        }
        if (cX * cX + cY * cY > rx * rx + ry * ry) {
          rx = cX;
          ry = cY;
        }
        continue;
      }
      if (!touch && d.keyboard) continue;
      buttons |= (touch ? d.buttons : d.buttons & ~GC_B) | d.hats;
      float mainX = d.lx, mainY = d.ly, cX = d.rx, cY = d.ry;
      if (aiming && !touch) {
        mainX = d.rx;
        mainY = d.ry;
        cX = cY = 0;
      }
      if (touchAiming && touch && touchAim == AIM_LOOK) {
        // the look zone aims by swipes (takeAimSwipe), not as a stick; the C stick stays the C stick
        cX = d.cStickX;
        cY = d.cStickY;
      } else if (touchAiming && touch) {
        // the touch C stick aims (the move stick too: the further wins), the look zone stays the C stick
        if (d.cStickX * d.cStickX + d.cStickY * d.cStickY > mainX * mainX + mainY * mainY) {
          mainX = d.cStickX;
          mainY = d.cStickY;
        }
        cX = d.lookX;
        cY = d.lookY;
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
      } else if (alternate) {
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
    if (menuChanged) {
      carried = buttons & ~lastButtons;
      if (!menu) carried &= ~HELD_ACTIONS;
      menuChanged = false;
    }
    carried &= buttons;
    buttons &= ~carried;
    lastButtons = buttons;
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
