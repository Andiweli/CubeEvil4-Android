package com.ast.cubeevil4.ouya;

import android.content.Context;
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
import com.ast.retrotouch.RetroTouchSkin;
import com.ast.retrotouch.RetroTouchSkinListener;
import com.ast.retrotouch.RetroTouchView;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * RetroTouch (libs/retrotouch.aar, the main app's; it runs on Android 4.1: none of its Android calls is newer)
 * above the game's picture, as in the main app (its TouchControls; wish of the project owner, 15.11.2026). For the
 * OUYA APK on a phone or tablet with Android 4.x: the OUYA has no touch screen, so the controls only show where
 * Android has one (FEATURE_TOUCHSCREEN), and by the menu's "Touch Controls" (Auto: without a controller, a tap shows
 * them, a button hides them; Force; Suppress). RetroTouch's gear opens its editor (move, resize, save the layout,
 * the button style); the game stands meanwhile. Everything here runs on the UI thread.
 *
 * Four skins since 1.0.0 (TouchStyle: RE, GC, PS2, XBOX; RetroTouch 1.0.0-beta.13's RetroTouchSkin, the main app's
 * TouchControls, merged 18.11.2026): the editor's style button goes through them, the menu offers all four (look(),
 * setLook); RetroTouch keeps the choice. A player of the old GC style (RetroTouch's LABELS) gets the GC skin.
 */
final class OuyaTouchControls {
  interface Host {
    /** MENU of the touch controls: the app's menu. */
    void openMenu();
    /** The editor opened or closed (the game stands while it is open). */
    void editing(boolean on);
    /** The touch controller changed: to the core. */
    void send();
  }

  static final String TAG = "RE4OUYA";
  /** The key of the saved layout (the main app's: an exported layout fits). */
  static final String LAYOUT_ID = "cubeevil4";
  /** RetroTouch's preferences (the main app's TouchLayoutFile.PREFS) and its keys of the style (before skins) and of
   *  the skin chosen. */
  static final String PREFS = "retrotouch_layouts";
  static final String STYLE_KEY = "button_style_" + LAYOUT_ID, SKIN_KEY = "button_skin_" + LAYOUT_ID;

  private final RetroTouchView view;
  private final OuyaTouch touch;
  private final Host host;
  private final boolean screen;  // the device has a touch screen
  private final int[] where = new int[2];
  private final TouchShowRule rule = new TouchShowRule();
  private boolean shown, editing, first = true;
  private float shoulder = TouchStyle.SHOULDER_ASPECT;
  private boolean reshape;
  private String devices = "";

  OuyaTouchControls(Context context, OuyaTouch.Input input, int mode, Host host) {
    this.host = host;
    screen = context.getPackageManager().hasSystemFeature("android.hardware.touchscreen");
    rule.setMode(mode);
    touch = new OuyaTouch(input, new OuyaTouch.Host() {
      @Override
      public void select(boolean down) {
        if (down && !editing)
          OuyaTouchControls.this.host.openMenu();
      }

      @Override
      public void latched(String action, boolean on) {
        view.setActionLatched(action, on);
      }
    });
    view = new RetroTouchView(context);
    view.setFocusable(false);
    view.setAutoHideOnController(false);
    // RE's style without skins (the base of the RE skin)
    view.setAvailableButtonStyles(RetroTouchButtonStyle.ICONS);
    view.setDefaultButtonStyle(TouchStyle.DEFAULT_BUTTON_STYLE);
    // the actions with RE's labels and icons (every skin sets its own content on top), the C stick an analog stick
    for (int i = 0; i < OuyaTouch.ACTIONS.length; i++) {
      String action = OuyaTouch.ACTIONS[i];
      if (OuyaTouch.isStick(action))
        view.registerStickAction(action, OuyaTouch.LABELS[i], null, TouchStyle.icon(action));
      else
        view.registerAction(action, OuyaTouch.LABELS[i], null, TouchStyle.icon(action));
    }
    view.setDefaultColors(TouchStyle.COLOURS);
    registerSkins();
    view.setDefaultSkin(TouchStyle.IDS[TouchStyle.DEFAULT_LOOK]);
    // the old GC style becomes the GC skin (before the layout: RetroTouch reads the saved skin with it)
    migrateOldGc(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE));
    view.setGameplayLayout(layout(shoulder));
    Log.i(TAG, "touch controls: skin " + view.getSkin());
    view.setSkinListener(new RetroTouchSkinListener() {
      @Override
      public void onSkinChanged(String skin) {
        Log.i(TAG, "touch controls: skin " + skin + " (editor)");
      }
    });
    view.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
      @Override
      public void onLayoutChange(View v, int left, int top, int right, int bottom, int oldLeft, int oldTop,
          int oldRight, int oldBottom) {
        float aspect = TouchStyle.shoulderAspect(right - left, bottom - top,
            v.getResources().getDisplayMetrics().density);
        if (aspect == shoulder)
          return;
        shoulder = aspect;
        if (editing)
          reshape = true;
        else
          reshape();
      }
    });
    view.setMode(RetroTouchMode.OFF);
    view.setListener(new Listener() {
      @Override
      public void onStick(String action, float x, float y) {
        touch.stick(action, x, y);
        OuyaTouchControls.this.host.send();
      }

      @Override
      public void onAction(String action, boolean pressed) {
        touch.action(action, pressed);
        OuyaTouchControls.this.host.send();
      }

      @Override
      public void onMove(float x, float y) {
        touch.move(x, y);
        OuyaTouchControls.this.host.send();
      }

      @Override
      public void onLook(float dx, float dy) {
        touch.look(dx, dy);
        OuyaTouchControls.this.host.send();
      }

      @Override
      public void onEditorStateChanged(boolean on) {
        editing = on;
        touch.reset();
        if (!on && reshape)
          reshape();
        Log.i(TAG, "touch controls: editor " + (on ? "open, the game stands" : "closed"));
        OuyaTouchControls.this.host.editing(on);
        OuyaTouchControls.this.host.send();
      }
    });
    update();
  }

  private abstract static class Listener extends RetroTouchAdapter implements RetroTouchStickListener {}

  /** The four skins for L's and R's width on this screen (the same IDs again: RetroTouch replaces them in place). */
  private void registerSkins() {
    for (RetroTouchSkin skin : TouchStyle.skins(shoulder))
      view.registerSkin(skin);
  }

  /** A player of the old GC style (RetroTouch's LABELS, saved before skins) without a skin chosen yet: the GC skin. */
  static boolean migrateOldGc(android.content.SharedPreferences p) {
    if (p.contains(SKIN_KEY) || !"LABELS".equals(p.getString(STYLE_KEY, null)))
      return false;
    p.edit().putString(SKIN_KEY, TouchStyle.IDS[TouchStyle.GC]).apply();
    Log.i(TAG, "touch controls: the old GC style becomes the GC skin");
    return true;
  }

  /** The skin shown (TouchStyle.RE .. XBOX; the menu's "Button style"). */
  int look() { return TouchStyle.look(view.getSkin()); }

  /** The menu's "Button style": a skin at once (RetroTouch saves the choice). */
  void setLook(int which) {
    view.setSkin(TouchStyle.IDS[TouchStyle.valid(which)]);
    Log.i(TAG, "touch controls: skin " + view.getSkin() + " (app menu)");
  }

  private void reshape() {
    reshape = false;
    view.setGameplayLayout(layout(shoulder));
    registerSkins();
    touch.reset();
  }

  private static RetroTouchLayout layout(float shoulder) {
    List<RetroTouchControl> controls = new ArrayList<RetroTouchControl>();
    for (OuyaTouch.Control c : OuyaTouch.LAYOUT) {
      switch (c.kind) {
      case OuyaTouch.LOOK:
        controls.add(RetroTouchControl.lookZone(c.id, c.x, c.y, c.width, c.height));
        break;
      case OuyaTouch.STICK:
        controls.add(RetroTouchControl.moveStick(c.id, c.x, c.y, c.width));
        break;
      case OuyaTouch.DPAD:
        controls.add(RetroTouchControl.dPad(c.id, c.x, c.y, c.width));
        break;
      default:
        controls.add(RetroTouchControl.button(c.id, c.action, OuyaTouch.label(c.action), c.x, c.y, c.width, null,
            RetroTouchButtonShape.AUTO, TouchStyle.aspect(c.action, shoulder)));
        break;
      }
    }
    return new RetroTouchLayout(LAYOUT_ID, controls);
  }

  View view() { return view; }

  boolean shown() { return shown; }

  boolean editing() { return editing; }

  /** The gamepads and joysticks connected (the sources GAMEPAD or JOYSTICK, as RetroTouchControllers counts them). */
  static String controllerDevices() {
    StringBuilder out = new StringBuilder();
    for (int id : InputDevice.getDeviceIds()) {
      InputDevice d = InputDevice.getDevice(id);
      if (d == null)
        continue;
      int sources = d.getSources();
      if ((sources & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD
          && (sources & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK)
        continue;
      if (out.length() > 0)
        out.append(", ");
      out.append(String.format(Locale.ROOT, "device %d \"%s\" sources 0x%x%s", id, d.getName(), sources,
          d.isVirtual() ? " virtual" : ""));
    }
    return out.toString();
  }

  /** The menu's "Touch Controls": TouchShowRule.AUTO, FORCE or SUPPRESS. */
  void setMode(int mode) {
    rule.setMode(mode);
    Log.i(TAG, "touch controls: setting " + TouchShowRule.name(rule.mode()));
    update();
  }

  void setLatch(boolean on) { touch.setLatch(on); }

  void setLatchB(boolean on) { touch.setLatchB(on); }

  /** Shown or hidden by the touch screen, the controllers, the setting and the last touch. */
  void update() {
    String now = controllerDevices();
    boolean controller = !now.isEmpty();
    boolean want = screen && rule.shown(controller);
    boolean change = want != shown || first;
    if (!change && now.equals(devices))
      return;
    first = false;
    devices = now;
    if (change) {
      shown = want;
      view.setMode(want ? RetroTouchMode.GAMEPLAY : RetroTouchMode.OFF);
      touch.reset();
    }
    String why = !screen ? "no touch screen" : TouchShowRule.name(rule.mode()) + ", " + (!controller ? "no controller"
        : "a controller is connected" + (rule.touched() && want ? ", the screen was touched" : "") + ": " + now);
    Log.i(TAG, "touch controls " + (want ? "shown" : "hidden") + " (" + why + ")");
    if (change)
      host.send();
  }

  /** A controller's button or stick was used: hidden again (unless no controller or Force). */
  void controllerUsed() {
    if (rule.controller())
      update();
  }

  /** Nothing held (the menu, pause): RetroTouch forgets its fingers too. */
  void reset() {
    touch.reset();
    view.resetInputState();
  }

  /** Before the window's touch event goes to the views (OuyaActivity.dispatchTouchEvent). */
  void before(MotionEvent e) {
    if (shown)
      touch.beginEvent();
  }

  /** After it went through them: which finger looks, which stopped. */
  void after(MotionEvent e) {
    if (!shown) {
      if (screen && e.getActionMasked() == MotionEvent.ACTION_UP && rule.touch())
        update();
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
      if (looked)
        host.send();
      break;
    case MotionEvent.ACTION_CANCEL:
      touch.cancel();
      host.send();
      break;
    default:
      break;
    }
  }

  private boolean inLookZone(float x, float y) {
    if (editing || view.getWidth() <= 0)
      return false;
    view.getLocationInWindow(where);
    x -= where[0];
    y -= where[1];
    int w = view.getWidth(), h = view.getHeight();
    for (RetroTouchControl c : view.getCurrentLayout().getControls()) {
      if (c.getType() != RetroTouchControl.Type.LOOK_ZONE)
        continue;
      float halfW = c.getWidth() * w * 0.5f, halfH = c.getHeight() * h * 0.5f;
      float cx = c.getX() * w, cy = c.getY() * h;
      if (x >= cx - halfW && x <= cx + halfW && y >= cy - halfH && y <= cy + halfH)
        return true;
    }
    return false;
  }
}
