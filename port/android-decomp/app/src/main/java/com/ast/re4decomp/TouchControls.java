package com.ast.re4decomp;

import android.content.Context;
import android.os.Build;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import com.ast.retrotouch.RetroTouchAdapter;
import com.ast.retrotouch.RetroTouchColors;
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchLayout;
import com.ast.retrotouch.RetroTouchMode;
import com.ast.retrotouch.RetroTouchStickListener;
import com.ast.retrotouch.RetroTouchView;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * RetroTouch (app/libs/retrotouch.aar; an update replaces the file) above the
 * game's picture: the touch controls of the core mode, mapped by CoreTouch.
 * Shown by the menu's setting "Touch Controls" (TouchShowRule: Auto, Force,
 * Suppress; decision of the project owner, 21.10.2026) and the controllers
 * connected (built-in, USB and Bluetooth, counted as RetroTouchControllers
 * counts them). The log names each device counted as a controller.
 * RetroTouch's gear at the top right opens its editor (move, resize, save the
 * layout); the game stands meanwhile. Everything here runs on the UI thread.
 */
final class TouchControls implements CoreTouch.Host {
  interface Host {
    /** MENU went down or up: the app's SELECT; `finger` false when RetroTouch let it go itself (no menu then). */
    void select(boolean down, boolean finger);
    /** RetroTouch's editor opened or closed. */
    void editing(boolean on);
    /** The controller changed: to the core. */
    void send();
    /** R3 of the touch controls went down or up: the graphics capture. */
    void r3(boolean down);
  }

  /** The key of the saved layout (RetroTouch keeps the player's layout under it). */
  static final String LAYOUT_ID = "cubeevil4";

  private final RetroTouchView view;
  private final CoreTouch touch;
  private final Host host;
  private final int[] where = new int[2];
  private final TouchShowRule rule = new TouchShowRule();
  private boolean shown, editing, quiet, first = true;
  // the devices counted as controllers at the last update, for the log
  private String devices = "";

  /** mode: the menu's setting (TouchShowRule.AUTO, FORCE, SUPPRESS). */
  TouchControls(Context context, CoreInput input, int mode, Host host) {
    this.host = host;
    rule.setMode(mode);
    touch = new CoreTouch(input, this);
    view = new RetroTouchView(context);
    // the game's surface keeps the focus (the controller's keys)
    view.setFocusable(false);
    // shown by the controllers present, not hidden for a while by their keys
    view.setAutoHideOnController(false);
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      final RetroTouchColors colours = CoreTouch.COLOURS[i] == 0 ? null : RetroTouchColors.of(CoreTouch.COLOURS[i]);
      // the C stick: an analog stick of its own (RetroTouch 1.0.0-beta.7's stick action)
      if (CoreTouch.isStick(CoreTouch.ACTIONS[i])) view.registerStickAction(CoreTouch.ACTIONS[i], CoreTouch.LABELS[i], colours);
      else view.registerAction(CoreTouch.ACTIONS[i], CoreTouch.LABELS[i], colours);
    }
    view.setGameplayLayout(layout());
    view.setMode(RetroTouchMode.OFF);  // hidden until update() decides
    view.setListener(new Listener() {
      @Override public void onStick(String action, float x, float y) {
        touch.stick(action, x, y);
        host.send();
      }

      @Override public void onAction(String action, boolean pressed) {
        touch.action(action, pressed);
        host.send();
      }

      @Override public void onMove(float x, float y) {
        touch.move(x, y);
        host.send();
      }

      @Override public void onLook(float dx, float dy) {
        touch.look(dx, dy);
        host.send();
      }

      @Override public void onEditorStateChanged(boolean on) {
        editing = on;
        touch.reset();
        Log.i(CoreActivity.TAG, "touch controls: editor " + (on ? "open, the game stands" : "closed"));
        host.editing(on);
        host.send();
      }
    });
    update();
  }

  /** RetroTouch's callbacks and those of its sticks other than the move stick (the C stick). */
  private abstract static class Listener extends RetroTouchAdapter implements RetroTouchStickListener {}

  private static RetroTouchLayout layout() {
    List<RetroTouchControl> controls = new ArrayList<>();
    for (CoreTouch.Control c : CoreTouch.LAYOUT) {
      switch (c.kind) {
        case CoreTouch.LOOK:
          controls.add(RetroTouchControl.lookZone(c.id, c.x, c.y, c.width, c.height));
          break;
        case CoreTouch.STICK:
          controls.add(RetroTouchControl.moveStick(c.id, c.x, c.y, c.width));
          break;
        case CoreTouch.DPAD:
          controls.add(RetroTouchControl.dPad(c.id, c.x, c.y, c.width));
          break;
        default:
          controls.add(RetroTouchControl.button(c.id, c.action, CoreTouch.label(c.action), c.x, c.y, c.width));
          break;
      }
    }
    return new RetroTouchLayout(LAYOUT_ID, controls);
  }

  View view() { return view; }

  boolean shown() { return shown; }

  /**
   * The gamepads and joysticks connected (built-in, USB, Bluetooth), by RetroTouchControllers'
   * rule (the sources GAMEPAD or JOYSTICK), one description each; "" when there is none.
   */
  static String controllerDevices() {
    StringBuilder out = new StringBuilder();
    for (int id : InputDevice.getDeviceIds()) {
      InputDevice d = InputDevice.getDevice(id);
      if (d == null) continue;
      int sources = d.getSources();
      if ((sources & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD
          && (sources & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) continue;
      if (out.length() > 0) out.append(", ");
      out.append(String.format(Locale.ROOT, "device %d \"%s\" vendor %04x product %04x sources 0x%x%s%s",
          id, d.getName(), d.getVendorId(), d.getProductId(), sources, d.isVirtual() ? " virtual" : "",
          Build.VERSION.SDK_INT < 29 ? "" : d.isExternal() ? " external" : " built-in"));
    }
    return out.toString();
  }

  /** The menu's setting "Touch Controls": TouchShowRule.AUTO, FORCE or SUPPRESS. */
  void setMode(int mode) {
    rule.setMode(mode);
    Log.i(CoreActivity.TAG, "touch controls: setting " + TouchShowRule.name(rule.mode()));
    update();
  }

  /** The menu's switch "Latch L and R": a tap keeps L or R pressed until the next tap (off: held while touched). */
  void setLatch(boolean on) { touch.setLatch(on); }

  /** The menu's switch "Latch B": a tap keeps B (run) pressed until the next tap (off: held while touched). */
  void setLatchB(boolean on) { touch.setLatchB(on); }

  /**
   * Shown or hidden by the controllers present, the setting and the last touch (twice a second,
   * when a device changes, after a touch or a controller's use).
   */
  void update() {
    String now = controllerDevices();
    boolean controller = !now.isEmpty();
    boolean want = rule.shown(controller);
    boolean change = want != shown || first;
    if (!change && now.equals(devices)) return;
    first = false;
    devices = now;
    if (change) {
      shown = want;
      quietly(() -> view.setMode(want ? RetroTouchMode.GAMEPLAY : RetroTouchMode.OFF));
      touch.reset();
    }
    String why = TouchShowRule.name(rule.mode()) + ", " + (!controller ? "no controller"
        : "a controller is connected" + (rule.touched() && want ? ", the screen was touched" : "") + ": " + now);
    Log.i(CoreActivity.TAG, "touch controls " + (want ? "shown" : "hidden") + " (" + why + ")");
    if (change) host.send();
  }

  /** A controller's button or stick was used: hidden again (unless no controller or Force). */
  void controllerUsed() {
    if (rule.controller()) update();
  }

  /** Nothing held (the app's menu, focus lost, pause): RetroTouch forgets its fingers too. */
  void reset() {
    touch.reset();
    quietly(view::resetInputState);
  }

  private void quietly(Runnable r) {
    quiet = true;
    try {
      r.run();
    } finally {
      quiet = false;
    }
  }

  @Override public void select(boolean down) { host.select(down, !quiet); }

  @Override public void latched(String action, boolean on) { view.setActionLatched(action, on); }

  @Override public void r3(boolean down) { host.r3(down); }

  /** Before the window's touch event goes to the views (CoreActivity.dispatchTouchEvent). */
  void before(MotionEvent e) {
    if (shown) touch.beginEvent();
  }

  /** After it went through them: which finger looks, which stopped. */
  void after(MotionEvent e) {
    if (!shown) {
      // hidden by a controller: the end of a touch shows them (the next one plays)
      if (e.getActionMasked() == MotionEvent.ACTION_UP && rule.touch()) update();
      return;
    }
    int i = e.getActionIndex();
    switch (e.getActionMasked()) {
      case MotionEvent.ACTION_DOWN:
      case MotionEvent.ACTION_POINTER_DOWN:
        touch.fingerDown(e.getPointerId(i), inLookZone(e.getX(i), e.getY(i)));
        break;
      case MotionEvent.ACTION_UP:
      case MotionEvent.ACTION_POINTER_UP:
        boolean looked = touch.looking();
        touch.fingerUp(e.getPointerId(i));
        if (looked) host.send();
        break;
      case MotionEvent.ACTION_CANCEL:
        touch.cancel();
        host.send();
        break;
      default:
        break;
    }
  }

  /**
   * A point of the window on a shown control other than a look zone, or anywhere while the layout
   * is edited: a tap there is no tap on the picture (CoreActivity.watchTap).
   */
  boolean onControl(float x, float y) {
    if (!shown) return false;
    if (editing || view.getWidth() <= 0) return true;
    view.getLocationInWindow(where);
    x -= where[0];
    y -= where[1];
    int w = view.getWidth(), h = view.getHeight();
    for (RetroTouchControl c : view.getCurrentLayout().getControls()) {
      if (c.getType() == RetroTouchControl.Type.LOOK_ZONE) continue;
      float halfW = c.getWidth() * w * 0.5f, halfH = c.getHeight() * h * 0.5f;
      float cx = c.getX() * w, cy = c.getY() * h;
      if (x >= cx - halfW && x <= cx + halfW && y >= cy - halfH && y <= cy + halfH) return true;
    }
    return false;
  }

  /** A point of the window inside a look zone of the current layout (as RetroTouch tests it). */
  private boolean inLookZone(float x, float y) {
    if (editing || view.getWidth() <= 0) return false;
    view.getLocationInWindow(where);
    x -= where[0];
    y -= where[1];
    int w = view.getWidth(), h = view.getHeight();
    for (RetroTouchControl c : view.getCurrentLayout().getControls()) {
      if (c.getType() != RetroTouchControl.Type.LOOK_ZONE) continue;
      float halfW = c.getWidth() * w * 0.5f, halfH = c.getHeight() * h * 0.5f;
      float cx = c.getX() * w, cy = c.getY() * h;
      if (x >= cx - halfW && x <= cx + halfW && y >= cy - halfH && y <= cy + halfH) return true;
    }
    return false;
  }
}
