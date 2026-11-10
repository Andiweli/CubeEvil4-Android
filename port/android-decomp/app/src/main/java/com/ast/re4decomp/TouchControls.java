package com.ast.re4decomp;

import android.content.Context;
import android.os.Build;
import android.util.Log;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import com.ast.retrotouch.RetroTouchAdapter;
import com.ast.retrotouch.RetroTouchButtonShape;
import com.ast.retrotouch.RetroTouchButtonStyle;
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
 *
 * Two button styles (RetroTouch 1.0.0-beta.10, wish of the project owner, 09.11.2026): LABELS, the
 * look of 0.99.6 and earlier, and ICONS (TouchStyle: icons, shapes, colours, the style a new player
 * gets). The player switches in the editor (its style button) or in the app menu (Controls -> Button
 * style); RetroTouch keeps the choice, the menu reads it from there (icons(), setIcons).
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
  // L's and R's width over height in the ICONS style on this screen (TouchStyle.shoulderAspect); `reshape`: to
  // build the layout again with it once the editor closes
  private float shoulder = TouchStyle.SHOULDER_ASPECT;
  private boolean reshape;
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
    // both styles; the editor shows its style button, the names in the phone's language (upper case as its other tools)
    view.setAvailableButtonStyles(RetroTouchButtonStyle.LABELS, RetroTouchButtonStyle.ICONS);
    view.setDefaultButtonStyle(TouchStyle.DEFAULT_BUTTON_STYLE);
    Locale language = context.getResources().getConfiguration().locale;
    view.setStyleToggleLabels(context.getString(R.string.ingame_button_labels).toUpperCase(language),
        context.getString(R.string.ingame_button_icons).toUpperCase(language));
    // the layout first: RetroTouch reads the player's saved style with it
    view.setGameplayLayout(layout(shoulder));
    applyStyle(view.getButtonStyle());
    Log.i(CoreActivity.TAG, "touch controls: button style " + view.getButtonStyle());
    // the player switched in the editor (RetroTouch saved the choice)
    view.setStyleListener(style -> {
      applyStyle(style);
      Log.i(CoreActivity.TAG, "touch controls: button style " + style + " (editor)");
    });
    // L and R as wide as the screen has room for next to the gear (4:3)
    view.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
      float aspect = TouchStyle.shoulderAspect(right - left, bottom - top, v.getResources().getDisplayMetrics().density);
      if (aspect == shoulder) return;
      shoulder = aspect;
      if (editing) reshape = true;
      else reshape();
    });
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
        if (!on && reshape) reshape();
        Log.i(CoreActivity.TAG, "touch controls: editor " + (on ? "open, the game stands" : "closed"));
        host.editing(on);
        host.send();
      }
    });
    update();
  }

  /** RetroTouch's callbacks and those of its sticks other than the move stick (the C stick). */
  private abstract static class Listener extends RetroTouchAdapter implements RetroTouchStickListener {}

  /**
   * The look of a button style (TouchStyle): the actions' labels, icons and colours, the shapes of L, R,
   * START and MENU and the colours of the D-pad and the sticks. LABELS as before: the GameCube's colours, the
   * shapes RetroTouch chooses by the labels, its grey D-pad and sticks.
   */
  private void applyStyle(RetroTouchButtonStyle style) {
    boolean icons = style == RetroTouchButtonStyle.ICONS;
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String action = CoreTouch.ACTIONS[i];
      // the border of every button #3e3c29 (wish of the project owner, 03.11.2026; RetroTouch 1.0.0-beta.8)
      RetroTouchColors colours = icons || CoreTouch.COLOURS[i] == 0 ? null
          : RetroTouchColors.of(CoreTouch.COLOURS[i]).withBorderColor(CoreTouch.BORDER);
      // the C stick: an analog stick of its own (RetroTouch 1.0.0-beta.7's stick action)
      if (CoreTouch.isStick(action)) view.registerStickAction(action, CoreTouch.LABELS[i], colours);
      else view.registerAction(action, CoreTouch.LABELS[i], colours, TouchStyle.icon(action));
      view.setButtonShape(action, icons ? TouchStyle.iconShape(action) : RetroTouchButtonShape.AUTO);
    }
    // ICONS: every button without colours of its own (all of them then), the D-pad and the sticks
    view.setDefaultColors(icons ? TouchStyle.COLOURS : null);
  }

  /** The button style shown is ICONS (the app menu's "Button style"). */
  boolean icons() { return view.getButtonStyle() == RetroTouchButtonStyle.ICONS; }

  /** The app menu's "Button style": ICONS (on) or LABELS, at once; RetroTouch keeps the choice. */
  void setIcons(boolean on) {
    RetroTouchButtonStyle style = on ? RetroTouchButtonStyle.ICONS : RetroTouchButtonStyle.LABELS;
    view.setButtonStyle(style);
    applyStyle(style);
    Log.i(CoreActivity.TAG, "touch controls: button style " + style + " (app menu)");
  }

  /** The default layout again with L's and R's aspect for this screen (the saved layout read again with it). */
  private void reshape() {
    reshape = false;
    quietly(() -> view.setGameplayLayout(layout(shoulder)));
    touch.reset();
    Log.i(CoreActivity.TAG, "touch controls: L and R " + shoulder + " times as wide as high in the icon style");
  }

  /**
   * The default layout. L, R, START and MENU carry their width over height for the ICONS style's shapes
   * (applyStyle); RetroTouch's shapes by the labels (LABELS) do not use it. Shape and aspect are not part of
   * a saved layout: RetroTouch takes them from here by the control's id at every load.
   */
  private static RetroTouchLayout layout(float shoulder) {
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
          controls.add(RetroTouchControl.button(c.id, c.action, CoreTouch.label(c.action), c.x, c.y, c.width, null,
              RetroTouchButtonShape.AUTO, TouchStyle.aspect(c.action, shoulder)));
          break;
      }
    }
    return new RetroTouchLayout(LAYOUT_ID, controls);
  }

  /** The app's own layout, RetroTouch's "reset" (TouchLayoutFile: is the saved one the player's own?). */
  static RetroTouchLayout defaultLayout() { return layout(TouchStyle.SHOULDER_ASPECT); }

  /**
   * The saved layout again (after an import, TouchLayoutFile.apply): RetroTouch reads it from its preferences when it
   * shows a layout, now when the controls are shown, else the next time they are.
   */
  void reloadSaved() {
    quietly(() -> view.setGameplayLayout(layout(shoulder)));
    touch.reset();
    Log.i(CoreActivity.TAG, "touch controls: the saved layout read again (" + (shown ? "shown" : "hidden") + ")");
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
