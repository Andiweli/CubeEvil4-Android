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
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchLayout;
import com.ast.retrotouch.RetroTouchMode;
import com.ast.retrotouch.RetroTouchStickListener;
import com.ast.retrotouch.RetroTouchStickTouchListener;
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
 * Four skins (TouchStyle: RE, GC, PS2, XBOX; wish of the project owner, 17.11.2026; RetroTouch 1.0.0-beta.13's
 * RetroTouchSkin): the editor's style button goes through them and shows their names, the app menu (Controls ->
 * Button style) offers all four (look(), setLook); RetroTouch keeps the choice (button_skin_cubeevil4, in the player's
 * backup). A player of the old GC style (RetroTouch's LABELS, 0.99.6 .. 1.0) gets the GC skin, a new player RE.
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
    /** A zoom button (the editor's + or -) went down or up: 1 in, -1 out (CoreBridge.zoom while held). */
    void zoom(int direction, boolean down);
  }

  /** The key of the saved layout (RetroTouch keeps the player's layout under it). */
  static final String LAYOUT_ID = "cubeevil4";
  /** RetroTouch's keys of the style (before skins) and of the skin chosen, in TouchLayoutFile.PREFS. */
  static final String STYLE_KEY = "button_style_" + LAYOUT_ID, SKIN_KEY = "button_skin_" + LAYOUT_ID;

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
  // the camera cutout's safe insets {left, top, right, bottom} and the view's size (the whole window since 0.99.17):
  // the default layout lies in the safe area, a layout of before 0.99.17 is written for the camera's side (TouchFrame)
  private final int[] safe = new int[4];
  private int width, height;
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
    // RE's style without skins (the base of the RE skin)
    view.setAvailableButtonStyles(RetroTouchButtonStyle.ICONS);
    view.setDefaultButtonStyle(TouchStyle.DEFAULT_BUTTON_STYLE);
    // the move stick, the C stick, the aim and the knife stick in one layout (RetroTouch 1.0.0-beta.12; before it two)
    view.setMaxSticks(CoreTouch.MAX_STICKS);
    // the actions with RE's labels and icons (every skin sets its own content on top), the C, aim and knife sticks as
    // analog sticks of their own (RetroTouch's stick action)
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String action = CoreTouch.ACTIONS[i];
      if (CoreTouch.isStick(action)) view.registerStickAction(action, CoreTouch.LABELS[i], null, TouchStyle.icon(action));
      else view.registerAction(action, CoreTouch.LABELS[i], null, TouchStyle.icon(action));
    }
    view.setDefaultColors(TouchStyle.COLOURS);
    registerSkins();
    view.setDefaultSkin(TouchStyle.IDS[TouchStyle.DEFAULT_LOOK]);
    // the old GC style becomes the GC skin (before the layout: RetroTouch reads the saved skin with it)
    migrateOldGc(prefs());
    view.setGameplayLayout(layout(shoulder));
    Log.i(CoreActivity.TAG, "touch controls: skin " + view.getSkin());
    view.setSkinListener(skin -> Log.i(CoreActivity.TAG, "touch controls: skin " + skin + " (editor)"));
    // L and R as wide as the screen has room for next to the gear (4:3); the layouts for the view's size (TouchFrame)
    view.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
      float aspect = TouchStyle.shoulderAspect(right - left, bottom - top, v.getResources().getDisplayMetrics().density);
      final boolean sized = right - left != width || bottom - top != height;
      if (aspect == shoulder && !sized) return;
      shoulder = aspect;
      width = right - left;
      height = bottom - top;
      if (editing) reshape = true;
      else reshape();
    });
    view.setMode(RetroTouchMode.OFF);  // hidden until update() decides
    view.setListener(new Listener() {
      @Override public void onStick(String action, float x, float y) {
        touch.stick(action, x, y);
        host.send();
      }

      // a finger on a stick and off it again (RetroTouch 1.0.0-beta.12): the aim and knife sticks hold R and L
      @Override public void onStickDown(String action) {
        touch.stickDown(action);
        host.send();
      }

      @Override public void onStickUp(String action) {
        touch.stickUp(action);
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
        // RetroTouch saved its layout as it closed: an old layout the player changed is the whole window's now, the
        // default saved as it is follows the camera again (TouchFrame)
        if (!on) {
          final String what = TouchFrame.afterEditor(prefs(), windowDefaults().toJson());
          if (!what.isEmpty()) Log.i(CoreActivity.TAG, "touch controls: " + what);
        }
        if (!on && reshape) reshape();
        Log.i(CoreActivity.TAG, "touch controls: editor " + (on ? "open, the game stands" : "closed"));
        host.editing(on);
        host.send();
      }
    });
    update();
  }

  /** RetroTouch's callbacks, those of its sticks other than the move stick (the C, aim and knife sticks) and the
   *  fingers on its sticks (RetroTouch calls the last on a listener that implements RetroTouchStickTouchListener). */
  private abstract static class Listener extends RetroTouchAdapter
      implements RetroTouchStickListener, RetroTouchStickTouchListener {}

  /** The four skins for L's and R's width on this screen (the same IDs again: RetroTouch replaces them in place). */
  private void registerSkins() {
    for (com.ast.retrotouch.RetroTouchSkin skin : TouchStyle.skins(shoulder)) view.registerSkin(skin);
  }

  /**
   * A player of the old GC style (RetroTouch's style LABELS, saved before skins) without a skin chosen yet: the GC skin
   * (written before RetroTouch reads its key, or set at once when the layout is already loaded). True when it did.
   */
  static boolean migrateOldGc(android.content.SharedPreferences p) {
    if (p.contains(SKIN_KEY) || !"LABELS".equals(p.getString(STYLE_KEY, null))) return false;
    p.edit().putString(SKIN_KEY, TouchStyle.IDS[TouchStyle.GC]).apply();
    Log.i(CoreActivity.TAG, "touch controls: the old GC style becomes the GC skin");
    return true;
  }

  /** The skin shown (TouchStyle.RE .. XBOX; the app menu's "Button style"). */
  int look() { return TouchStyle.look(view.getSkin()); }

  /** The app menu's "Button style": a skin at once (RetroTouch saves the choice). */
  void setLook(int which) {
    view.setSkin(TouchStyle.IDS[TouchStyle.valid(which)]);
    Log.i(CoreActivity.TAG, "touch controls: skin " + view.getSkin() + " (app menu)");
  }

  /** The default layout again with L's and R's aspect for this screen (the saved layout read again with it). */
  private void reshape() {
    reshape = false;
    frame();
    quietly(() -> view.setGameplayLayout(layout(shoulder, width, height, safe)));
    registerSkins();
    touch.reset();
    Log.i(CoreActivity.TAG, "touch controls: L and R " + shoulder + " times as wide as high (RE, GC)");
  }

  /**
   * The camera cutout's safe insets {left, top, right, bottom} of the window (CoreActivity, at each change: laid out,
   * turned by 180 degrees): the layouts again for the camera's side (TouchFrame), after the editor when it is open.
   */
  void setSafeInsets(int[] insets) {
    if (java.util.Arrays.equals(insets, safe)) return;
    System.arraycopy(insets, 0, safe, 0, 4);
    // RetroTouch 1.0.0-beta.11: its editor's bar, the "DRAG / Selected" line and the gear's default place keep clear of
    // the camera; the controls and their saved places are not touched
    view.setSafeInsets(safe[0], safe[1], safe[2], safe[3]);
    if (width <= 0 || height <= 0) return;  // the first layout does it
    if (editing) reshape = true;
    else reshape();
  }

  /** The safe insets the layouts follow now. */
  int[] safeInsets() { return safe.clone(); }

  /**
   * A layout saved before 0.99.17 kept once as the safe area's, and written for the camera's side now (TouchFrame);
   * RetroTouch reads it with the next setGameplayLayout.
   */
  private void frame() {
    if (width <= 0 || height <= 0) return;
    final android.content.SharedPreferences p = prefs();
    if (TouchFrame.migrate(p)) Log.i(CoreActivity.TAG, "touch controls: the layout saved before 0.99.17 kept as it was");
    if (TouchFrame.legacyApply(p, width, height, safe)) {
      Log.i(CoreActivity.TAG, "touch controls: the layout of before 0.99.17 at its places for the camera "
          + Cutout.side(safe) + " (" + width + "x" + height + ")");
    }
  }

  /** RetroTouch's preferences (TouchLayoutFile.PREFS). */
  private android.content.SharedPreferences prefs() {
    return view.getContext().getSharedPreferences(TouchLayoutFile.PREFS, Context.MODE_PRIVATE);
  }

  /** The default layout of this window: laid into the safe area of the camera's side now (what RESET gives). */
  RetroTouchLayout windowDefaults() { return layout(shoulder, width, height, safe); }

  /** The default layout in a safe area (the whole layout without insets): the centres move with the safe area. */
  static RetroTouchLayout layout(float shoulder, int width, int height, int[] safe) {
    if (!TouchFrame.moves(width, height, safe)) return layout(shoulder);
    List<RetroTouchControl> controls = new ArrayList<>();
    for (CoreTouch.Control c : CoreTouch.LAYOUT) {
      final float x = (float) TouchFrame.place(c.x, width, safe[0], safe[2]);
      final float y = (float) TouchFrame.place(c.y, height, safe[1], safe[3]);
      if (c.kind == CoreTouch.LOOK) {
        // the look zone: the same part of the safe area (its edge at the camera's strip)
        controls.add(RetroTouchControl.lookZone(c.id, x, y, (float) TouchFrame.extent(c.width, width, safe[0], safe[2]),
            (float) TouchFrame.extent(c.height, height, safe[1], safe[3])));
      } else {
        // round and shaped controls: their size (RetroTouch: the smaller of width x the view's width and height x its
        // height) is the height's in landscape, the same in both frames
        controls.add(control(c, x, y, shoulder));
      }
    }
    return new RetroTouchLayout(LAYOUT_ID, controls);
  }

  /**
   * The default layout. L, R, START and MENU carry their width over height (the skins' looks set their own), Z none
   * (its skin's counts). Shape and aspect are not part of
   * a saved layout: RetroTouch takes them from here by the control's id at every load.
   */
  private static RetroTouchLayout layout(float shoulder) {
    List<RetroTouchControl> controls = new ArrayList<>();
    for (CoreTouch.Control c : CoreTouch.LAYOUT) {
      if (c.kind == CoreTouch.LOOK) controls.add(RetroTouchControl.lookZone(c.id, c.x, c.y, c.width, c.height));
      else controls.add(control(c, c.x, c.y, shoulder));
    }
    return new RetroTouchLayout(LAYOUT_ID, controls);
  }

  /** A stick, a D-pad or a button of the default layout with its centre at x, y. */
  private static RetroTouchControl control(CoreTouch.Control c, float x, float y, float shoulder) {
    switch (c.kind) {
      case CoreTouch.STICK:
        return RetroTouchControl.moveStick(c.id, x, y, c.width);
      case CoreTouch.DPAD:
        return RetroTouchControl.dPad(c.id, x, y, c.width);
      default:
        return RetroTouchControl.button(c.id, c.action, CoreTouch.label(c.action), x, y, c.width, null,
            RetroTouchButtonShape.AUTO, TouchStyle.aspect(c.action, shoulder));
    }
  }

  /** The app's own layout, RetroTouch's "reset" (TouchLayoutFile: is the saved one the player's own?). */
  static RetroTouchLayout defaultLayout() { return layout(TouchStyle.SHOULDER_ASPECT); }

  /**
   * The saved layout again (after an import, TouchLayoutFile.apply): RetroTouch reads it from its preferences when it
   * shows a layout, now when the controls are shown, else the next time they are.
   */
  void reloadSaved() {
    frame();
    quietly(() -> view.setGameplayLayout(layout(shoulder, width, height, safe)));
    // the skin again too (a settings import brings the player's skin, or the style of a backup of before skins)
    final android.content.SharedPreferences p = prefs();
    migrateOldGc(p);
    view.setSkin(p.getString(SKIN_KEY, TouchStyle.IDS[TouchStyle.DEFAULT_LOOK]));
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

  @Override public void zoom(int direction, boolean down) { host.zoom(direction, down); }

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
