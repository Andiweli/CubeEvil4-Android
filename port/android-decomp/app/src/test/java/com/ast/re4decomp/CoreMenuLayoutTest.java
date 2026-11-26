package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.res.Configuration;
import android.graphics.Typeface;
import android.graphics.Rect;
import android.text.Spanned;
import android.text.style.StyleSpan;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Locale;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;

/** Measure the actual Android views, without loading the game or needing disc data. */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class CoreMenuLayoutTest {
  @Implements(CoreBridge.class)
  public static class Bridge {
    @Implementation public static int[] graphicsInfo() { return new int[] {4, 16}; }
    @Implementation public static String[] cardList() { return new String[0]; }
    static int sharpen = -1;  // the last level the menu sent (setSharpen)
    @Implementation public static void setSharpen(int level) { sharpen = level; }
    static int potato = -1;   // the last Potato mode bits the menu sent (setPotato)
    @Implementation public static void setPotato(int mask) { potato = mask; }
    static String texturePack = null;  // the last pack the menu or TexturePacks sent (setTexturePack)
    @Implementation public static String setTexturePack(String path, boolean half, int budgetMb) {
      texturePack = path;
      return "";
    }
    @Implementation public static long[] texturePackUse() { return new long[] {87, 133, 12_000_000}; }
  }

  /** An endless animation in this SDK 28 sandbox's main thread kept Robolectric's idle() busy for good (full runs hung
   *  three times, 11.11.2026; TestLooper): one left from before is named on stderr and stopped before each test, and
   *  the tests wait for the looper with TestLooper.idle (bounded). */
  @org.junit.Before public void noAnimationLeftRunning() {
    TestLooper.stopAnimations("left running before " + getClass().getSimpleName());
  }

  static Object field(Object owner, String name) throws Exception {
    Field f = owner.getClass().getDeclaredField(name); f.setAccessible(true); return f.get(owner);
  }

  static void measure(View root, int width, int height) {
    root.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY));
    root.layout(0, 0, width, height);
  }

  static void checkBounds(View view, String scenario) {
    if (view.getVisibility() != View.VISIBLE) return;
    if (view instanceof TextView && ((TextView) view).getText().length() > 0) {
      TextView text = (TextView) view;
      assertNotNull(scenario + ": missing text layout: " + text.getText(), text.getLayout());
      assertTrue(scenario + ": clipped height: " + text.getText(),
          text.getHeight() >= text.getLayout().getHeight() + text.getCompoundPaddingTop() + text.getCompoundPaddingBottom());
      for (int line = 0; line < text.getLayout().getLineCount(); line++)
        assertEquals(scenario + ": ellipsized text", 0, text.getLayout().getEllipsisCount(line));
    }
    if (view instanceof ViewGroup) {
      ViewGroup group = (ViewGroup) view;
      for (int i = 0; i < group.getChildCount(); i++) {
        View child = group.getChildAt(i);
        if (child.getVisibility() != View.VISIBLE) continue;
        assertTrue(scenario + ": overflow left: " + child.getClass(), child.getLeft() >= 0);
        assertTrue(scenario + ": overflow right: " + child.getClass(), child.getRight() <= group.getWidth());
        if (!(group instanceof ScrollView)) {
          assertTrue(scenario + ": overflow top", child.getTop() >= 0);
          assertTrue(scenario + ": overflow bottom: " + child.getClass(), child.getBottom() <= group.getHeight());
          Rect bounds = new Rect(child.getLeft(), child.getTop(), child.getRight(), child.getBottom());
          for (int j = 0; j < i; j++) {
            View earlier = group.getChildAt(j);
            if (earlier.getVisibility() == View.VISIBLE)
              assertFalse(scenario + ": overlapping views", Rect.intersects(bounds,
                  new Rect(earlier.getLeft(), earlier.getTop(), earlier.getRight(), earlier.getBottom())));
          }
        }
        checkBounds(child, scenario);
      }
    }
  }

  @Test public void allPagesFitAcrossLanguagesWidthsAndFontScales() throws Exception {
    String[] languages = {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"};
    // a capable device (the project owner's Retroid, its benchmark): the Display page with the
    // real-time shadows' row under the effects (16.11.2026), the longest of its values
    final PerfProfile.Device retroid =
        new PerfProfile.Device("Qualcomm Adreno (TM) 650, OpenGL ES 3.2", 8, 4, 2841, 7600, 60.0, 4, 16, 1080);
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        // gameLanguages: the home page with the European discs' "Game language" too
        (proxy, method, args) -> method.getName().equals("perfDevice") ? retroid
            : method.getReturnType() == boolean.class ? Boolean.TRUE
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0
            : method.getReturnType() == int.class ? (Object) 0 : null);
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    Method metadata = CoreMenu.class.getDeclaredMethod("showInstant", long[].class); metadata.setAccessible(true);
    for (String language : languages) for (float scale : new float[] {1f, 1.5f, 2f}) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
        activity.getApplicationInfo().flags |= android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE;
        activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0).edit().putInt(CoreMenu.DYN_CAP_30, 6)
            .putInt(CoreMenu.DYN_CAP_60, 5).putInt(PerfProfile.BENCH_CPU_LOAD, 27).putInt(RealtimeShadows.KEY, 2)
            // (the upscaler's longer values of 1.1.0, "SGSR (light)" / "FSR (strong)", in their half-width cell)
            .putInt(CoreMenu.UPSCALER, scale == 2f ? 2 : 1)
            .apply();
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        // the update button beside "Back to the game": both of its labels
        menu.showUpdate(scale == 1.5f ? AppUpdate.CURRENT : AppUpdate.OLDER);
        View root = (View) field(menu, "root");
        root.setLayoutDirection(config.getLayoutDirection());
        for (int page = 0; page < 9; page++) {  // 7: the button mapping of the controller, 8: of the keyboard
          if (page >= 7) {
            java.lang.reflect.Field kind = CoreMenu.class.getDeclaredField("mapKind"); kind.setAccessible(true);
            kind.setInt(menu, page == 7 ? ActionMap.KIND_CONTROLLER : ActionMap.KIND_KEYBOARD);
          }
          show.invoke(menu, Math.min(page, 7), false);
          if (page == 1)
            assertNotNull("the real-time shadows under the effects (" + language + ")",
                findButtonWith((View) field(menu, "root"), activity.getString(R.string.ingame_rt_shadows)));
          if (page == 0) metadata.invoke(menu, (Object) new long[] {0, 1, 1, 1, 0x100, 101, 123456, 1792841040});
          if (page == 5) {
            // the cheat buttons' longest answer shown under them
            TextView answer = (TextView) field(menu, "cheatStatus");
            answer.setText(activity.getString(R.string.ingame_cheat_not_now)); answer.setVisibility(View.VISIBLE);
          }
          for (int width : new int[] {960, 320, 480, 700, 960}) {
            int pxWidth = Math.round(width * activity.getResources().getDisplayMetrics().density);
            int pxHeight = Math.round(300 * activity.getResources().getDisplayMetrics().density);
            measure(root, pxWidth, pxHeight);
            checkBounds(root, language + "/" + scale + "/" + width + "/page " + page);
            if (page == 0) {
              Button save = (Button) field(menu, "saveSlot"), load = (Button) field(menu, "loadSlot");
              MenuLayout.Buttons actions = (MenuLayout.Buttons) save.getParent();
              LinearLayout slot = (LinearLayout) actions.getParent();
              if (slot.getOrientation() == LinearLayout.HORIZONTAL && actions.getColumnCount() == 2) {
                ViewGroup details = (ViewGroup) slot.getChildAt(0);
                int headingInset = details.getLeft() + details.getChildAt(0).getLeft();
                int rightInset = slot.getWidth() - actions.getLeft() - Math.max(save.getRight(), load.getRight());
                assertEquals("Right edge matches the slot heading's left inset", headingInset, rightInset, 1);
                assertEquals("Save/load stay side by side", save.getTop(), load.getTop());
                assertEquals("Both actions fill the same height", save.getHeight(), load.getHeight());
                assertEquals("Actions keep the same top inset", headingInset, actions.getTop() + save.getTop(), 1);
                assertEquals("Actions keep the same bottom inset", headingInset,
                    slot.getHeight() - actions.getTop() - save.getBottom(), 1);
                assertTrue("Actions stay in the right half", actions.getLeft() >= slot.getWidth() / 2 - 1);
              }
              assertEquals(View.GONE, ((TextView) field(menu, "slotStatus")).getVisibility());
            }
          }
        }
      }
    }
  }

  private static MenuLayout.Keys findKeys(View view) {
    if (view instanceof MenuLayout.Keys) return (MenuLayout.Keys) view;
    if (view instanceof ViewGroup) {
      ViewGroup group = (ViewGroup) view;
      for (int i = 0; i < group.getChildCount(); i++) {
        MenuLayout.Keys found = findKeys(group.getChildAt(i));
        if (found != null) return found;
      }
    }
    return null;
  }

  @Test public void resolutionLevelsSitLikeAPianoAndKeepHalves() throws Exception {
    final int[] changed = {0};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("graphicsChanged")) changed[0]++;
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      // a choice of the old whole levels (until 27.10.2026): 3x
      android.content.SharedPreferences g = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0);
      g.edit().clear().putInt("scale", 3).apply();
      assertEquals(6, CoreMenu.scaleHalves(g));
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 1, false);
      View root = (View) field(menu, "root");
      measure(root, Math.round(960 * activity.getResources().getDisplayMetrics().density),
          Math.round(300 * activity.getResources().getDisplayMetrics().density));
      // 1.1.0 (the project owner's design of 21.11.2026): one arrow selector by level, Dynamic first, the benchmark
      // beside it
      ArrowSelector res = selectorNamed(root, activity.getString(R.string.ingame_resolution));
      assertNotNull(res);
      assertEquals(8, res.count());
      View benchKey = ((ViewGroup) res.getParent()).getChildAt(1);
      assertEquals(activity.getString(R.string.ingame_benchmark), ((Button) benchKey).getText().toString());
      assertEquals("3x chosen before stays chosen", "3×", res.valueText());
      assertTrue("lit: no level is Off", res.isSelected());
      res.step(-1);  // 2.5x
      assertEquals(5, g.getInt("scaleHalves", -1));
      res.step(1); res.step(1);  // 3.5x
      assertEquals(7, CoreMenu.scaleHalves(g));
      for (int i = 0; i < 3; i++) res.step(1);  // 4x, round to Dynamic, 1x
      assertEquals(2, CoreMenu.scaleHalves(g));
      res.step(-1);
      assertEquals(0, CoreMenu.scaleHalves(g)); assertTrue(res.isSelected());
      assertEquals(7, changed[0]);
    }
  }

  private static int top(View v, View root) {
    int y = 0;
    for (View at = v; at != root && at != null; at = (View) at.getParent()) y += at.getTop();
    return y;
  }

  /** The first button whose text holds `part` (Arabic's "name · value" starts with a right-to-left mark). */
  /** The arrow selector named `name` (1.1.0). */
  static ArrowSelector selectorNamed(View view, String name) {
    if (view instanceof ArrowSelector && ((ArrowSelector) view).nameText().equals(name)) return (ArrowSelector) view;
    if (view instanceof ViewGroup) {
      ViewGroup g = (ViewGroup) view;
      for (int i = 0; i < g.getChildCount(); i++) {
        ArrowSelector a = selectorNamed(g.getChildAt(i), name);
        if (a != null) return a;
      }
    }
    return null;
  }

  static Button findButtonWith(View view, String part) {
    if (view instanceof Button && ((Button) view).getText().toString().contains(part)) return (Button) view;
    if (view instanceof ViewGroup) {
      ViewGroup g = (ViewGroup) view;
      for (int i = 0; i < g.getChildCount(); i++) {
        Button b = findButtonWith(g.getChildAt(i), part);
        if (b != null) return b;
      }
    }
    return null;
  }

  static Button findButton(View view, String start) {
    if (view instanceof Button && ((Button) view).getText().toString().startsWith(start)) return (Button) view;
    if (view instanceof ViewGroup) {
      ViewGroup g = (ViewGroup) view;
      for (int i = 0; i < g.getChildCount(); i++) {
        Button b = findButton(g.getChildAt(i), start);
        if (b != null) return b;
      }
    }
    return null;
  }

  /** The body's direct child that holds `v`. */
  private static View rowOf(View v, View body) {
    View at = v;
    while (at.getParent() != body) at = (View) at.getParent();
    return at;
  }

  private static int rowIndex(View v, ViewGroup body) { return body.indexOfChild(rowOf(v, body)); }
  /** The side label of the group `v` is in (1.1.0, the fourth menu round): the first child of its MenuLayout.Columns. */
  static String sideLabel(View v) {
    for (View p = v; p != null && p.getParent() instanceof View; p = (View) p.getParent()) {
      if (p instanceof MenuLayout.Columns) {
        View first = ((ViewGroup) p).getChildAt(0);
        return first instanceof TextView ? ((TextView) first).getText().toString() : "";
      }
    }
    return null;
  }
  /** The first view below `view` whose content description is `text`. */
  private static View byDescription(View view, String text) {
    if (text.contentEquals(view.getContentDescription() == null ? "" : view.getContentDescription())) return view;
    if (view instanceof ViewGroup) {
      for (int i = 0; i < ((ViewGroup) view).getChildCount(); i++) {
        View found = byDescription(((ViewGroup) view).getChildAt(i), text);
        if (found != null) return found;
      }
    }
    return null;
  }

  @Test public void updateButtonSitsLeftOfResumeAndComparesVersions() throws Exception {
    assertEquals(0, AppUpdate.compare("0.95.2-beta", "0.95.2-beta"));
    assertEquals(-1, AppUpdate.compare("0.95.2-beta", "0.96.0-beta"));
    assertEquals(-1, AppUpdate.compare("0.95.2-beta", "v0.95.10-beta"));
    assertEquals(1, AppUpdate.compare("1.0", "1.0-beta"));
    assertEquals(1, AppUpdate.compare("0.96.0-beta", "0.95.2-beta"));
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      setVersion(activity, "0.99.18-rc-core");
      CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
      Button update = (Button) field(menu, "update"), resume = (Button) field(menu, "resume");
      TextView version = (TextView) field(menu, "versionLabel");
      View root = (View) field(menu, "root");
      int width = Math.round(960 * activity.getResources().getDisplayMetrics().density);
      int height = Math.round(300 * activity.getResources().getDisplayMetrics().density);
      assertEquals("hidden until GitHub answered", View.GONE, update.getVisibility());
      // 0.99.18 (the project owner, 15.11.2026, as in the OUYA menu): "v0.99.18-rc" left of the update button, without
      // "-core"; the title only the page's name; the version also while the update button is hidden (no internet)
      TextView title = (TextView) field(menu, "title");
      assertEquals("the title without the version", activity.getString(R.string.ingame_title), title.getText().toString());
      assertEquals("v0.99.18-rc", version.getText().toString());
      measure(root, width, height);
      View header = (View) version.getParent();
      assertEquals("the version without an answer from GitHub", View.VISIBLE, version.getVisibility());
      assertTrue("then left of Back to the game", version.getRight() <= leftIn(resume, header));
      menu.showUpdate(AppUpdate.CURRENT);
      measure(root, width, height);
      assertEquals(activity.getString(R.string.ingame_up_to_date), update.getText().toString());
      assertFalse("up to date: not clickable", update.isEnabled());
      assertTrue("left of Back to the game", update.getRight() <= resume.getLeft());
      assertTrue("the version left of the update button", version.getRight() <= leftIn(update, header));
      assertEquals("in the same row", update.getTop(), resume.getTop());
      menu.showUpdate(AppUpdate.OLDER);
      assertEquals(activity.getString(R.string.ingame_update), update.getText().toString());
      assertTrue("an update: clickable", update.isEnabled());
      assertFalse("the version is no button", version.isFocusable() || version.isClickable());
    }
  }

  /** `v`'s left and top edge in `ancestor`'s coordinates (views in different groups compared). */
  static int leftIn(View v, View ancestor) {
    int x = 0;
    for (View at = v; at != ancestor; at = (View) at.getParent()) x += at.getLeft();
    return x;
  }
  static int topIn(View v, View ancestor) {
    int y = 0;
    for (View at = v; at != ancestor; at = (View) at.getParent()) y += at.getTop();
    return y;
  }

  /** The APK's versionName as the package manager gives it to CoreMenu.version() (the tests' own is not set). */
  static void setVersion(Activity activity, String versionName) {
    org.robolectric.Shadows.shadowOf(activity.getPackageManager())
        .getInternalMutablePackageInfo(activity.getPackageName()).versionName = versionName;
  }

  @Test public void controlsPageHasMotionComfortAndControllerSectionsWithOneGap() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 3, false);
      View root = (View) field(menu, "root");
      ViewGroup body = (ViewGroup) field(menu, "body");
      measure(root, Math.round(960 * activity.getResources().getDisplayMetrics().density),
          Math.round(300 * activity.getResources().getDisplayMetrics().density));
      Button alternate = findButton(body, activity.getString(R.string.ingame_alternate));
      Button latch = findButton(body, activity.getString(R.string.ingame_latch));
      // "Auto aim" and "Always run" under the latches (30.10.2026), then the comfort controls' "Invert vertical"
      // (the gyro's above it may read the same): the one after them
      Button autoAim = findButton(body, activity.getString(R.string.ingame_auto_aim));
      Button alwaysRun = findButton(body, activity.getString(R.string.ingame_always_run));
      assertNotNull(autoAim); assertNotNull(alwaysRun);
      // "Far camera" since 05.11.2026 in PLAYER VIEW, beside the field of view's slider
      Button farCamera = findButton(body, activity.getString(R.string.ingame_far_camera));
      assertNotNull(farCamera);
      // (the fourth menu round: the comfort rows in one group, "Control type") the one in auto aim's group
      Button aimInvert = findButton((ViewGroup) ((View) autoAim.getParent()).getParent(),
          activity.getString(R.string.ingame_aim_invert_v));
      Button touchAim = findButton(body, activity.getString(R.string.ingame_aim_move_stick));
      Button gyro = findButton(body, activity.getString(R.string.ingame_gyro_none).split(" ")[0]);
      Button gyroInvert = findButton(body, activity.getString(R.string.ingame_gyro_invert_h));
      Button mapping = findButton(body, activity.getString(R.string.ingame_controller_config));
      assertNotNull(alternate); assertNotNull(latch); assertNotNull(aimInvert); assertNotNull(touchAim);
      assertNotNull(gyro); assertNotNull(gyroInvert); assertNotNull(mapping);
      int motion = heading(body, activity, R.string.ingame_motion), comfort = heading(body, activity, R.string.ingame_comfort);
      int devices = heading(body, activity, R.string.ingame_controller_section);
      int touch = heading(body, activity, R.string.ingame_touch_controls), aiming = heading(body, activity, R.string.ingame_aiming_section);
      int view = heading(body, activity, R.string.ingame_player_view);
      int assign = heading(body, activity, R.string.ingame_mapping_section);
      // 05.11.2026: TOUCH CONTROLS first, a third of the row each; TOUCH AIMING after the comfort controls, PLAYER VIEW
      // (far camera | field of view) above the LASER SIGHT (a quarter of the row each)
      assertEquals("touch controls first", 0, touch);
      // 1.1.0: the touch choice an arrow selector with its name (the project owner's third menu round: beside the button
      // style), Auto
      ArrowSelector auto = selectorNamed(body, activity.getString(R.string.ingame_touch_controls));
      assertNotNull(auto);
      assertEquals(activity.getString(R.string.ingame_touch_auto), auto.valueText());
      assertEquals("the touch choices under their heading", touch + 1, rowIndex(auto, body));
      assertEquals(3, auto.count());
      // "Button style" (09.11.2026; four looks since 17.11.2026): RE, GC, PS2 and XBOX in one arrow selector (1.1.0)
      // under the touch choices and their help, RE without a host (a new player's look, TouchStyle.DEFAULT_LOOK)
      ArrowSelector icons = selectorNamed(body, activity.getString(R.string.ingame_button_style));
      assertNotNull(icons);
      assertEquals(4, icons.count());
      assertEquals("the button style beside the touch choices", rowIndex(auto, body), rowIndex(icons, body));
      assertEquals("in the same row", auto.getParent(), icons.getParent());
      assertEquals("side by side", auto.getTop(), icons.getTop());
      // (lit: every look counts as on, none is "Off")
      assertEquals(activity.getString(R.string.ingame_button_icons), icons.valueText()); assertTrue(icons.isSelected());
      assertTrue("touch aiming after the button style", aiming > rowIndex(icons, body));
      assertTrue("touch aiming after the comfort controls", aiming > comfort);
      assertEquals("touch aiming's choices under their heading", aiming + 1, rowIndex(touchAim, body));
      assertTrue("player view after touch aiming", view > aiming);
      // 1.1.5 (the project owner, 26.11.2026): AIMING with "Controller aim with" (left stick | right stick) under the touch
      // row, then CONTROLLER, KEYBOARD AND MOUSE, then PLAYER VIEW, the mapping last
      ArrowSelector controllerAim = (ArrowSelector) findButton(body, activity.getString(R.string.ingame_aim_left_stick));
      assertEquals("controller aiming under touch aiming", aiming + 2, rowIndex(controllerAim, body));
      assertEquals(activity.getString(R.string.ingame_side_controller_aim), sideLabel(controllerAim));
      assertEquals(2, controllerAim.count());
      assertTrue("the controller section after aiming", devices > aiming);
      assertTrue("player view after the controller section", view > devices);
      assertTrue("the mapping after player view", assign > view);
      Button moveAim = findButton(body, activity.getString(R.string.ingame_move_aim));
      assertEquals("move and aim in the sticks' group", devices + 1, rowIndex(moveAim, body));
      assertFalse("greyed out while the move stick and the left stick aim", moveAim.isEnabled());
      assertEquals("far camera under player view", view + 1, rowIndex(farCamera, body));
      // (the fourth menu round, 21.11.2026) the laser sight's section gone: its side label under the view's group
      Button red = findButton(body, activity.getString(R.string.ingame_laser_red));
      assertEquals("the laser right under the view", view + 2, rowIndex(red, body));
      assertEquals("the four colours in one arrow selector", 4, ((ArrowSelector) red).count());
      assertEquals(activity.getString(R.string.ingame_laser_section), sideLabel(red));
      assertTrue("the laser after the controller section", rowIndex(red, body) > devices);
      // motion controls first (wish of the project owner, 30.10.2026): "Gyroscope" (the gyro switch with its slider,
      // "Gyro from" under them), then "Axes" (its directions)
      assertTrue("the motion heading", motion >= 0);
      assertEquals("the gyro group right under the motion heading", motion + 1, rowIndex(gyro, body));
      assertEquals(activity.getString(R.string.ingame_side_gyro), sideLabel(gyro));
      assertEquals("the gyro's directions under it", rowIndex(gyro, body) + 1, rowIndex(gyroInvert, body));
      assertEquals(activity.getString(R.string.ingame_side_axes), sideLabel(gyroInvert));
      // "Gyro from" (1.1.0): Auto, Device, Controller; since the fourth menu round in the gyro's group, under the switch
      ArrowSelector gyroFrom = selectorNamed((ViewGroup) body.getChildAt(rowIndex(gyro, body)),
          activity.getString(R.string.ingame_gyro_source));
      assertNotNull("gyro from in the gyro's group", gyroFrom);
      assertTrue("under the switch", top(gyroFrom, body) > top(gyro, body));
      assertEquals(3, gyroFrom.count());
      assertEquals("gyro from Auto", activity.getString(R.string.ingame_touch_auto), gyroFrom.valueText());
      // then the comfort controls: under the latches "Auto aim" with "Always run", then "Invert vertical" with
      // "Aim speed", then touch aiming
      assertTrue("the comfort heading after the motion controls", comfort > rowIndex(gyroInvert, body));
      // (the fourth menu round) one group "Control type": alternate, latches, auto aim | always run, the aim row
      assertEquals(activity.getString(R.string.ingame_side_control_type), sideLabel(alternate));
      assertEquals("one group", rowIndex(alternate, body), rowIndex(aimInvert, body));
      assertEquals("always run beside auto aim", autoAim.getParent(), alwaysRun.getParent());
      assertTrue("auto aim under the latches", top(autoAim, body) > top(latch, body));
      assertTrue("the aim row under them", top(aimInvert, body) > top(autoAim, body));
      Button bodyTurn = findButton(body, activity.getString(R.string.ingame_body_turn));
      assertEquals(activity.getString(R.string.ingame_side_assists), sideLabel(bodyTurn));
      assertEquals(activity.getString(R.string.ingame_side_aim_with), sideLabel(touchAim));
      assertEquals(activity.getString(R.string.ingame_side_view), sideLabel(farCamera));
      assertEquals(activity.getString(R.string.ingame_side_touch_style), sideLabel(auto));
      // the button mapping at the bottom, under a heading of its own
      assertTrue("the controller heading after the comfort controls", devices > rowIndex(touchAim, body));
      // since 0.99.2 first the sticks' dead zone and range (sliders 0..30 and 50..100 in steps of 5) and the choice
      // where the vibration goes (four buttons), then the button mapping
      View deadZone = byDescription(body, activity.getString(R.string.ingame_stick_dead_zone));
      View range = byDescription(body, activity.getString(R.string.ingame_stick_range));
      assertTrue(deadZone instanceof android.widget.SeekBar && range instanceof android.widget.SeekBar);
      // (1.1.0, the project owner, 21.11.2026) the two side by side, slider cells under the heading ("Stick options")
      assertEquals("the dead zone right under the heading", devices + 1, rowIndex(deadZone, body));
      assertEquals("the range beside it", devices + 1, rowIndex(range, body));
      assertEquals(activity.getString(R.string.ingame_side_stick), sideLabel(deadZone));
      assertTrue(deadZone.getParent() instanceof SliderCell);
      assertEquals(6, ((android.widget.SeekBar) deadZone).getMax());
      assertEquals(0, ((android.widget.SeekBar) deadZone).getProgress());
      assertEquals(10, ((android.widget.SeekBar) range).getMax());
      assertEquals(10, ((android.widget.SeekBar) range).getProgress());
      // the vibration's "Device" below the controller heading ("Gyro from" above has one too, 1.1.0)
      ArrowSelector device = selectorNamed(body, activity.getString(R.string.ingame_vibration));
      assertNotNull(device);
      assertEquals(4, device.count());
      assertEquals("Auto chosen", activity.getString(R.string.ingame_touch_auto), device.valueText());
      // (the fourth menu round) the vibration in the sticks' group, under them; the button mapping in a section of its
      // own, beside "Assign"
      assertEquals("the vibration in the sticks' group", rowIndex(range, body), rowIndex(device, body));
      assertTrue("under them", top(device, body) > top(range, body));
      assertTrue("the mapping's section after the controller's", assign > devices);
      assertEquals("the button mapping under its heading", assign + 1, rowIndex(mapping, body));
      assertEquals("move and aim beside the vibration", moveAim.getParent(), device.getParent());
      assertEquals("side by side", top(moveAim, body), top(device, body));
      assertTrue("move and aim on the left", moveAim.getLeft() < device.getLeft());
      assertEquals(activity.getString(R.string.ingame_side_assign), sideLabel(mapping));
      // one gap between all rows of buttons, the rows with a slider too (wish of the project owner, 30.10.2026)
      int gap = top(latch, body) - top(alternate, body) - alternate.getHeight();
      assertEquals("the gap of the rows of two", Math.round(2 * MenuLayout.GAP * activity.getResources().getDisplayMetrics().density), gap, 1);
      // the rows with a slider: as tall as a row of buttons where the title leaves the slider 32 dp or more (on
      // the devices; this test's fonts are about twice as tall), the switch in the middle: the same gap
      float density = activity.getResources().getDisplayMetrics().density;
      // (1.1.0, the project owner, 21.11.2026) the switch and its slider as two cells of one row, alike in height
      for (Button b : new Button[] {aimInvert, gyro}) {
        ViewGroup row = (ViewGroup) b.getParent();
        View cell = row.getChildAt(1);
        assertTrue("the slider a cell beside the switch", cell instanceof SliderCell);
        assertEquals("as high as the switch", b.getHeight(), cell.getHeight(), 1);
        assertEquals("side by side", b.getTop(), cell.getTop());
      }
    }
  }

  @Test public void buttonMappingPutsActionsOnKeysAndCombinationsOnTwo() throws Exception {
    final ActionMap[] current = {ActionMap.defaults(ActionMap.KIND_CONTROLLER), ActionMap.defaults(ActionMap.KIND_KEYBOARD)};
    final boolean[] controller = {false}, keyboard = {false};
    final int[] calls = {0};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          switch (method.getName()) {
            case "mappingChanged": { ActionMap m = (ActionMap) args[0]; current[m.kind] = m; calls[0]++; return null; }
            case "mapping": return current[(Integer) args[0]];
            case "faceLabel": return args[0];
            case "controllerConnected": return controller[0];
            case "keyboardConnected": return keyboard[0];
            default: return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null;
          }
        });
    try (var controllerActivity = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controllerActivity.get();
      activity.getSharedPreferences(CoreActivity.INPUT_PREFS, 0).edit().clear().apply();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      // on the controls page, at the bottom: with a controller or a keyboard
      show.invoke(menu, 3, false);
      Button open = (Button) field(menu, "controllerButton");
      assertFalse("without a controller or a keyboard", open.isEnabled());
      keyboard[0] = true;
      show.invoke(menu, 3, false);
      open = (Button) field(menu, "controllerButton");
      assertTrue("with a keyboard", open.isEnabled());
      open.performClick();
      Button[] keys = (Button[]) field(menu, "bindingButtons");
      assertEquals("the keyboard's page first without a controller", ActionMap.KIND_KEYBOARD, (int) (Integer) field(menu, "mapKind"));
      assertNotNull("the keyboard moves with keys", keys[ActionMap.MOVE_FORWARD]);
      assertTrue(keys[ActionMap.FIRE].getText().toString().startsWith("Mouse R + Mouse L"));
      // the controller's page: no move keys (its stick), fire is R2 + A
      java.lang.reflect.Field kind = CoreMenu.class.getDeclaredField("mapKind"); kind.setAccessible(true);
      kind.setInt(menu, ActionMap.KIND_CONTROLLER);
      show.invoke(menu, 7, false);
      keys = (Button[]) field(menu, "bindingButtons");
      assertNull(keys[ActionMap.MOVE_FORWARD]);
      assertEquals("R2 + A", keys[ActionMap.FIRE].getText().toString());
      // fire on L2 + R2: the key to hold, then the second
      keys[ActionMap.FIRE].performClick();
      assertEquals(activity.getString(R.string.ingame_map_hold), keys[ActionMap.FIRE].getText().toString());
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_L2));
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_UP, android.view.KeyEvent.KEYCODE_BUTTON_L2));
      assertEquals(activity.getString(R.string.ingame_map_second, "L2"), keys[ActionMap.FIRE].getText().toString());
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_R2));
      assertEquals(1, calls[0]);
      assertArrayEquals(new int[] {android.view.KeyEvent.KEYCODE_BUTTON_L2, android.view.KeyEvent.KEYCODE_BUTTON_R2},
          current[ActionMap.KIND_CONTROLLER].keys(ActionMap.FIRE));
      keys = (Button[]) field(menu, "bindingButtons");
      assertTrue(keys[ActionMap.FIRE].getText().toString().startsWith("L2 + R2\n"));
      assertTrue(keys[ActionMap.FIRE].getText().toString().endsWith(activity.getString(R.string.ingame_map_default, "R2 + A")));
      // run on L1, then the action on L1: run takes the action's old key (A), the page says so
      keys[ActionMap.RUN].performClick();
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_L1));
      keys = (Button[]) field(menu, "bindingButtons");
      keys[ActionMap.ACTION].performClick();
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_L1));
      ActionMap now = current[ActionMap.KIND_CONTROLLER];
      assertEquals(android.view.KeyEvent.KEYCODE_BUTTON_L1, now.keys(ActionMap.ACTION)[0]);
      assertEquals(android.view.KeyEvent.KEYCODE_BUTTON_A, now.keys(ActionMap.RUN)[0]);
      String swapped = activity.getString(R.string.ingame_map_swapped, activity.getString(R.string.ingame_act_run), "A");
      assertEquals(swapped, ((TextView) field(menu, "mapStatus")).getText().toString());
      // the D-pad stays the D-pad: refused, still waiting
      keys = (Button[]) field(menu, "bindingButtons");
      keys[ActionMap.MAP].performClick();
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_DPAD_UP));
      assertEquals(activity.getString(R.string.ingame_map_fixed_key), ((TextView) field(menu, "mapStatus")).getText().toString());
      assertEquals(ActionMap.MAP, (int) (Integer) field(menu, "captureAction"));
      // the same button again: no change
      keys[ActionMap.MAP].performClick();
      assertEquals(-1, (int) (Integer) field(menu, "captureAction"));
      assertEquals(3, calls[0]);
      // the default layout again
      findButton((ViewGroup) field(menu, "body"), activity.getString(R.string.ingame_controller_default)).performClick();
      assertTrue(current[ActionMap.KIND_CONTROLLER].isDefault());
      // back goes to the controls page
      Method back = CoreMenu.class.getDeclaredMethod("goBack"); back.setAccessible(true);
      back.invoke(menu);
      assertEquals(3, (int) (Integer) field(menu, "page"));
    }
  }

  /** The body's row of a section heading (CoreMenu.Heading), -1 when there is none. */
  private static int heading(ViewGroup body, Activity activity, int id) {
    String want = activity.getString(id).toUpperCase(Locale.getDefault());
    for (int i = 0; i < body.getChildCount(); i++) {
      View v = body.getChildAt(i);
      if (v instanceof CoreMenu.Heading && ((CoreMenu.Heading) v).title().equals(want)) {
        // the faint line beside the text (05.11.2026), to the end of the row
        View rule = ((ViewGroup) v).getChildAt(1);
        assertTrue("a line right of the heading", rule.getLeft() >= ((ViewGroup) v).getChildAt(0).getRight());
        return i;
      }
    }
    return -1;
  }

  private static TextView findText(View view, String text) {
    if (view instanceof TextView && !(view instanceof Button) && ((TextView) view).getText().toString().equals(text)) {
      return (TextView) view;
    }
    if (view instanceof ViewGroup) {
      ViewGroup g = (ViewGroup) view;
      for (int i = 0; i < g.getChildCount(); i++) {
        TextView found = findText(g.getChildAt(i), text);
        if (found != null) return found;
      }
    }
    return null;
  }

  @Test public void cheatsPageSwitchesSendTheirBits() throws Exception {
    final int[] sent = {-1};
    final int[] enemySeed = {-1};
    final Boolean[] jacket = {null};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("cheatsChanged")) sent[0] = (Integer) args[0];
          if (method.getName().equals("leonJacketChanged")) {
            jacket[0] = (Boolean) args[0];
            // as CoreActivity: the setting the button reads at its next press
            org.robolectric.RuntimeEnvironment.getApplication().getSharedPreferences(CoreActivity.GAME_PREFS, 0)
                .edit().putBoolean(CoreActivity.LEON_JACKET, (Boolean) args[0]).commit();
          }
          if (method.getName().equals("enemyRandomChanged")) enemySeed[0] = (Integer) args[0];
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 5, false);  // CHEATS, a page of its own since 30.10.2026
      ViewGroup body = (ViewGroup) field(menu, "body");
      // 16.11.2026: PLAYER (health, ammo, invisibility; one-hit kill, steady aim, special costumes), WEAPONS
      // (Handcannon, Killer7, Matilda; Chicago Typewriter, Mine Thrower, rocket launcher), GENERAL & INVENTORY (QTEs,
      // extra games; random items and enemies; case, money)
      assertEquals(0, heading(body, activity, R.string.ingame_cheat_section_player));
      // (1.1.0, the project owner's fourth menu round, 21.11.2026) always two a row, beside side labels: Options (health |
      // ammo, invisibility | one-hit kill, steady aim | costumes), Basic weapons (Handcannon | Killer7, Matilda |
      // Chicago Typewriter), Explosives (Mine Thrower | rocket launcher)
      String[][] labels = {{"ingame_cheat_health", "ingame_cheat_ammo"}, {"ingame_cheat_invisible", "ingame_cheat_onehit"},
          {"ingame_cheat_steady", "ingame_cheat_costume"}, {"ingame_cheat_handcannon", "ingame_cheat_killer7"},
          {"ingame_cheat_matilda", "ingame_cheat_chicago"}, {"ingame_cheat_mine", "ingame_cheat_rocket"}};
      int[] sides = {R.string.ingame_side_options, 0, 0, R.string.ingame_side_basic_weapons, 0,
          R.string.ingame_side_explosive_weapons};
      ViewGroup[] rows = new ViewGroup[labels.length];
      for (int r = 0; r < labels.length; r++) {
        int first = activity.getResources().getIdentifier(labels[r][0], "string", activity.getPackageName());
        int second = activity.getResources().getIdentifier(labels[r][1], "string", activity.getPackageName());
        rows[r] = (ViewGroup) findButtonWith(body, activity.getString(first)).getParent();
        assertEquals(labels[r][0] + ": two a row", 2, rows[r].getChildCount());
        assertTrue(labels[r][1], ((Button) rows[r].getChildAt(1)).getText().toString().contains(activity.getString(second)));
        if (sides[r] != 0) {
          ViewGroup group = (ViewGroup) rows[r].getParent(), columns = (ViewGroup) group.getParent();
          assertEquals("the side label", activity.getString(sides[r]), ((TextView) columns.getChildAt(0)).getText().toString());
          assertSame("the group's first row", rows[r], group.getChildAt(0));
        }
      }
      assertSame("Options: three rows", rows[0].getParent(), rows[2].getParent());
      ViewGroup weapons = rows[3], inventory = rows[2];
      Button health = (Button) rows[0].getChildAt(0), oneHit = (Button) rows[1].getChildAt(1);
      Button invisible = (Button) rows[1].getChildAt(0);
      assertFalse(health.isSelected());
      health.performClick(); assertEquals(1, sent[0]);
      oneHit.performClick(); assertEquals(5, sent[0]);
      invisible.performClick(); assertEquals(5 | CoreMenu.INVISIBLE_BIT, sent[0]);
      invisible.performClick(); assertEquals(5, sent[0]);
      // "Steady aim" (16.11.2026): bit 31
      Button steady = (Button) inventory.getChildAt(0);
      steady.performClick(); assertEquals(5 | CoreMenu.STEADY_AIM_BIT, sent[0]); assertTrue(steady.isSelected());
      steady.performClick(); assertEquals(5, sent[0]); assertFalse(steady.isSelected());
      health.performClick(); assertEquals(4, sent[0]);
      // the bottom row (29.10.2026): two, automatic QTEs (bit 8) and the extra games' button
      ViewGroup more = (ViewGroup) findButtonWith(body, activity.getString(R.string.ingame_cheat_qte)).getParent();
      assertEquals(2, more.getChildCount());
      Button qte = (Button) more.getChildAt(0), extras = (Button) more.getChildAt(1);
      assertTrue(qte.getText().toString().contains(activity.getString(R.string.ingame_cheat_qte)));
      assertTrue(extras.getText().toString().contains(activity.getString(R.string.ingame_cheat_extras)));
      qte.performClick(); assertEquals(12, sent[0]);
      qte.performClick(); assertEquals(4, sent[0]);
      // the case and the money under them (16.11.2026), then "Stack items" and "Refill ammo" (21.11.2026; "Stack items"
      // a switch of its own kept in core_game stackItems, the cheat switches untouched; "Refill ammo" action 10)
      ViewGroup goods = (ViewGroup) findButtonWith(body, activity.getString(R.string.ingame_cheat_case)).getParent();
      ViewGroup ammo = (ViewGroup) findButtonWith(body, activity.getString(R.string.ingame_cheat_refill)).getParent();
      assertSame("Unlocks: QTEs | extra games, case | money", more.getParent(), goods.getParent());
      assertEquals(2, goods.getChildCount());
      assertEquals(2, ammo.getChildCount());
      assertTrue(((Button) goods.getChildAt(0)).getText().toString().contains(activity.getString(R.string.ingame_cheat_case)));
      assertTrue(((Button) goods.getChildAt(1)).getText().toString().contains(activity.getString(R.string.ingame_cheat_money)));
      assertTrue(((Button) ammo.getChildAt(1)).getText().toString().contains(activity.getString(R.string.ingame_cheat_refill)));
      assertNotNull(findText(body, activity.getString(R.string.ingame_cheat_refill_help)));
      // "Stack items" an arrow selector since the design of 21.11.2026: Standard > 250 > 500 > 999 > Standard
      ArrowSelector stack = (ArrowSelector) ammo.getChildAt(0);
      assertTrue(stack.getText().toString().contains(activity.getString(R.string.ingame_cheat_stack)));
      assertEquals(activity.getString(R.string.ingame_stack_standard), stack.valueText()); assertFalse(stack.isSelected());
      stack.performClick();
      assertEquals("250", stack.valueText()); assertTrue(stack.isSelected());
      assertEquals("the cheat switches unchanged", 4, sent[0]);
      stack.performClick(); stack.performClick();
      assertEquals("999", stack.valueText());
      stack.performClick();
      assertEquals(activity.getString(R.string.ingame_stack_standard), stack.valueText()); assertFalse(stack.isSelected());
      assertNotNull(findText(body, activity.getString(R.string.ingame_cheat_stack_help)));
      // "Random items" (0.99.2): on with a seed in bits 11..30, off without one; since 21.11.2026 with "Random enemies"
      // the first row of ENEMIES AND WORLD
      int world = heading(body, activity, R.string.ingame_cheat_section_world);
      assertTrue(world > heading(body, activity, R.string.ingame_cheat_section_game));
      ViewGroup items = (ViewGroup) findButtonWith(body, activity.getString(R.string.ingame_cheat_random_items)).getParent();
      assertEquals("Randomizer right under the heading", world + 1, rowIndex(items, body));
      Button random = (Button) items.getChildAt(0);
      assertTrue(random.getText().toString().contains(activity.getString(R.string.ingame_cheat_random_items)));
      random.performClick();
      assertEquals(4 | CoreMenu.RANDOM_ITEMS_BIT, sent[0] & ~CoreMenu.SEED_BITS);
      random.performClick(); assertEquals(4, sent[0]);
      // "Random enemies" next to it (0.99.12): a seed of its own (kept in core_game enemySeed), the switches untouched
      assertEquals(2, items.getChildCount());
      Button enemies = (Button) items.getChildAt(1);
      assertTrue(enemies.getText().toString().contains(activity.getString(R.string.ingame_cheat_random_enemies)));
      android.content.SharedPreferences game = activity.getSharedPreferences(CoreActivity.GAME_PREFS, 0);
      assertFalse(enemies.isSelected());
      enemies.performClick();
      int seed = game.getInt(CoreActivity.ENEMY_SEED, 0);
      assertTrue("a seed of 1..2^20-1", seed > 0 && seed < (1 << 20));
      assertEquals(seed, enemySeed[0]);
      assertEquals("the cheat switches unchanged", 4, sent[0]);
      enemies.performClick();
      assertEquals(0, game.getInt(CoreActivity.ENEMY_SEED, -1));
      assertEquals(0, enemySeed[0]);
      assertNotNull(findText(body, activity.getString(R.string.ingame_cheat_random_enemies_help)));
      Button killer7 = (Button) weapons.getChildAt(1);
      // a button lights up for a moment after a press
      killer7.performClick(); assertTrue(killer7.isSelected());
      // the flash ends after CHEAT_FLASH_MS: the main looper's tasks one by one, at most 2000 (idleMainLooper ran for
      // ever in a full run, 11.11.2026, see TestLooper)
      org.robolectric.shadows.ShadowLooper looper = org.robolectric.shadows.ShadowLooper.shadowMainLooper();
      for (int n = 0; killer7.isSelected() && n < 2000; n++) looper.runOneTask();
      assertFalse(killer7.isSelected());
      // the story's player character below the buttons (03.11.2026): its player type << 5, the switches kept
      // three a row without a row label (03.11.2026)
      assertNull(findText(body, "Play as"));
      Button leon = findButton(body, "Leon"), hunk = findButton(body, "HUNK"), wesker = findButton(body, "Wesker");
      assertTrue(leon.isSelected()); assertFalse(hunk.isSelected());
      // (1.1.0, the project owner's fourth menu round) "Main character": Leon over the row; "Mercenaries": Ada | HUNK,
      // Krauser | Wesker
      ViewGroup leonRow = (ViewGroup) leon.getParent();
      assertEquals(1, leonRow.getChildCount());
      Button ada = findButton(body, "Ada"), krauser = findButton(body, "Krauser");
      assertSame(ada.getParent(), hunk.getParent()); assertSame(krauser.getParent(), wesker.getParent());
      assertEquals(2, ((ViewGroup) ada.getParent()).getChildCount());
      assertEquals(activity.getString(R.string.ingame_side_main_character),
          ((TextView) ((ViewGroup) leonRow.getParent().getParent()).getChildAt(0)).getText().toString());
      assertEquals(activity.getString(R.string.ingame_side_mercenaries),
          ((TextView) ((ViewGroup) ada.getParent().getParent().getParent()).getChildAt(0)).getText().toString());
      View root = (View) field(menu, "root");
      float density = activity.getResources().getDisplayMetrics().density;
      measure(root, Math.round(960 * density), Math.round(540 * density));
      assertEquals("Leon over the whole row", leonRow.getWidth(), leon.getWidth(), Math.round(2 * MenuLayout.GAP * density) + 1);
      assertEquals("Krauser under Ada", ada.getLeft(), krauser.getLeft());
      assertEquals("Wesker under HUNK", hunk.getLeft(), wesker.getLeft());
      assertTrue("Krauser | Wesker under Ada | HUNK", top(krauser, body) > top(ada, body));
      // another character than Leon warns first (08.11.2026): its name centred on top, the warning, one button
      // "I understand" centred below; only that button switches, the choice stays until then
      hunk.performClick();
      android.app.AlertDialog ask = warning(activity, "HUNK");
      assertEquals(4, sent[0]); assertTrue(leon.isSelected()); assertFalse(hunk.isSelected());
      ok(ask); assertEquals(4 | 3 << 5, sent[0]);
      assertTrue(hunk.isSelected()); assertFalse(leon.isSelected());
      // B (as Back, Esc, START, SELECT) closes it without the switch; X does nothing here, A is the button
      int[] cancels = {android.view.KeyEvent.KEYCODE_BUTTON_B, android.view.KeyEvent.KEYCODE_BACK,
          android.view.KeyEvent.KEYCODE_ESCAPE, android.view.KeyEvent.KEYCODE_BUTTON_START,
          android.view.KeyEvent.KEYCODE_BUTTON_SELECT};
      for (int key : cancels) {
        wesker.performClick(); ask = warning(activity, "Wesker");
        press(ask, android.view.KeyEvent.KEYCODE_BUTTON_X); assertTrue("X does not answer", ask.isShowing());
        press(ask, key);
        assertFalse(key + " closes the warning", ask.isShowing());
        assertEquals(key + " keeps the character", 4 | 3 << 5, sent[0]);
        assertTrue(hunk.isSelected()); assertFalse(wesker.isSelected());
      }
      wesker.performClick(); ask = warning(activity, "Wesker");
      // a controller leaves the touch mode; the button has the focus then (the dialog gives it)
      understood(ask).requestFocusFromTouch();
      assertTrue(understood(ask).isFocused());
      press(ask, android.view.KeyEvent.KEYCODE_BUTTON_A);
      assertFalse("A is the button", ask.isShowing()); assertEquals(4 | 5 << 5, sent[0]); assertTrue(wesker.isSelected());
      // also from one of them to another; the one already chosen and Leon at once
      findButton(body, "Ada").performClick(); ok(warning(activity, "Ada")); assertEquals(4 | 2 << 5, sent[0]);
      wesker.performClick(); ok(warning(activity, "Wesker")); assertEquals(4 | 5 << 5, sent[0]);
      ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      wesker.performClick(); assertEquals(4 | 5 << 5, sent[0]);
      health.performClick(); assertEquals(5 | 5 << 5, sent[0]);
      leon.performClick(); assertEquals(5, sent[0]);
      assertSame("Leon and the chosen one ask nothing", ask, org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog());
      assertTrue(leon.isSelected()); assertFalse(wesker.isSelected());
      // the costumes, the player section's sixth button (1.0, the project owner, 16.11.2026): Normal > Leon's jacket >
      // Special > Normal, as Frame skip; Special is bit 256 (the special costumes of before), Leon's jacket a setting
      // of its own (leonJacketChanged); the character kept; lit but at Normal; no note under it any more
      Button costume = findButton(body, activity.getString(R.string.ingame_cheat_costume));
      assertSame(inventory, costume.getParent());
      assertEquals(1, inventory.indexOfChild(costume));
      final String name = activity.getString(R.string.ingame_cheat_costume);
      assertEquals(name + "\n" + activity.getString(R.string.ingame_cheat_costume_normal), costume.getText().toString());
      assertFalse(costume.isSelected());
      costume.performClick();
      assertEquals(Boolean.TRUE, jacket[0]); assertEquals(5, sent[0]); assertTrue(costume.isSelected());
      assertEquals(name + "\n" + activity.getString(R.string.ingame_cheat_costume_jacket), costume.getText().toString());
      costume.performClick();
      assertEquals(Boolean.FALSE, jacket[0]); assertEquals(5 | 256, sent[0]); assertTrue(costume.isSelected());
      assertEquals(name + "\n" + activity.getString(R.string.ingame_cheat_costume_special), costume.getText().toString());
      hunk.performClick(); ok(warning(activity, "HUNK")); assertEquals(5 | 256 | 3 << 5, sent[0]);
      jacket[0] = null;
      costume.performClick(); assertEquals(5 | 3 << 5, sent[0]); assertFalse(costume.isSelected());
      assertNull("Special > Normal leaves the jacket off", jacket[0]);
      // the switch of before (bit 256 stored) shows Special; Leon's jacket alone Leon's jacket
      assertEquals(CoreMenu.COSTUME_SPECIAL, CoreMenu.costumeChoice(256, false));
      assertEquals(CoreMenu.COSTUME_SPECIAL, CoreMenu.costumeChoice(256 | 5, true));
      assertEquals(CoreMenu.COSTUME_JACKET, CoreMenu.costumeChoice(5, true));
      assertEquals(CoreMenu.COSTUME_NORMAL, CoreMenu.costumeChoice(5, false));
      assertEquals(5 | 256, CoreMenu.costumeBits(5, CoreMenu.COSTUME_SPECIAL));
      assertEquals(5, CoreMenu.costumeBits(5 | 256, CoreMenu.COSTUME_JACKET));
    }
  }

  /** The warning before another player character (the owner, 08.11.2026): shown; the character's name centred on
   *  top, the warning centred under it, the one button "I understand" centred under that. */
  private static android.app.AlertDialog warning(Activity activity, String name) {
    android.app.AlertDialog ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
    assertNotNull("a warning for " + name, ask);
    assertTrue("the warning for " + name + " is shown", ask.isShowing());
    TestLooper.idle();  // laid out
    View decor = ask.getWindow().getDecorView();
    TextView title = findText(decor, name);
    TextView text = findText(decor, activity.getString(R.string.ingame_cheat_character_warning));
    Button understood = understood(ask);
    assertNotNull("the name on top", title); assertNotNull("the warning", text);
    assertEquals(activity.getString(R.string.ingame_understood), understood.getText().toString());
    assertEquals(android.view.Gravity.CENTER_HORIZONTAL, title.getGravity() & android.view.Gravity.HORIZONTAL_GRAVITY_MASK);
    // the warning centred too (the owner, 08.11.2026), over the whole width
    assertEquals(android.view.Gravity.CENTER_HORIZONTAL, text.getGravity() & android.view.Gravity.HORIZONTAL_GRAVITY_MASK);
    assertEquals(View.TEXT_ALIGNMENT_GRAVITY, text.getTextAlignment());
    assertEquals(((View) text.getParent()).getWidth(), text.getWidth());
    int[] t = new int[2], m = new int[2], b = new int[2];
    title.getLocationInWindow(t); text.getLocationInWindow(m); understood.getLocationInWindow(b);
    assertTrue("the name over the warning", t[1] + title.getHeight() <= m[1]);
    assertTrue("the button under the warning", m[1] + text.getHeight() <= b[1]);
    View row = (View) understood.getParent();
    assertEquals("the button centred", row.getWidth() / 2f, understood.getLeft() + understood.getWidth() / 2f, 1f);
    assertEquals("the name centred", row.getWidth() / 2f, title.getLeft() + title.getWidth() / 2f, 1f);
    return ask;
  }
  /** The warning's only button shown. */
  private static Button understood(android.app.AlertDialog ask) {
    java.util.List<Button> shown = new java.util.ArrayList<>();
    java.util.ArrayDeque<View> todo = new java.util.ArrayDeque<>();
    todo.add(ask.getWindow().getDecorView());
    while (!todo.isEmpty()) {
      View v = todo.poll();
      if (!v.isShown()) continue;
      if (v instanceof Button) shown.add((Button) v);
      if (v instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) todo.add(((ViewGroup) v).getChildAt(i));
    }
    assertEquals("one button", 1, shown.size());
    return shown.get(0);
  }
  private static void ok(android.app.AlertDialog ask) {
    understood(ask).performClick();
    TestLooper.idle();  // the dialog posts its answer
    assertFalse(ask.isShowing());
  }
  private static void press(android.app.Dialog dialog, int key) {
    TestLooper.idle();  // laid out: the keys reach its buttons
    dialog.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, key));
    dialog.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_UP, key));
    TestLooper.idle();
  }

  @Test public void navigationHasBoldTitleAndNormalSmallerSummary() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      CoreMenu menu = new CoreMenu(controller.get(), null, false, 1920, 1080);
      Button nav = ((Button[]) field(menu, "navigation"))[1];
      assertFalse(nav.getTypeface().isBold());
      Spanned label = (Spanned) nav.getText();
      int split = label.toString().indexOf('\n') + 1;
      StyleSpan[] title = label.getSpans(0, split, StyleSpan.class);
      assertEquals(Typeface.BOLD, title[0].getStyle());
      StyleSpan[] summary = label.getSpans(split, label.length(), StyleSpan.class);
      assertEquals(Typeface.NORMAL, summary[0].getStyle());
    }
  }

  @Test public void titleCanLoadWithoutEnablingSaveAndFrameSkipIsAToggle() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      CoreMenu menu = new CoreMenu(controller.get(), null, false, 1920, 1080);
      Method metadata = CoreMenu.class.getDeclaredMethod("showInstant", long[].class); metadata.setAccessible(true);
      metadata.invoke(menu, (Object) new long[] {0, 1, 0, 1, 0x100, 101, 1234, 1792841040});
      assertFalse(((Button) field(menu, "saveSlot")).isEnabled());
      assertTrue(((Button) field(menu, "loadSlot")).isEnabled());
      TextView status = (TextView) field(menu, "slotStatus");
      assertEquals(View.GONE, status.getVisibility());
      Method feedback = CoreMenu.class.getDeclaredMethod("showSlotStatus", int.class); feedback.setAccessible(true);
      feedback.invoke(menu, R.string.ingame_instant_working);
      assertEquals(View.VISIBLE, status.getVisibility());
      metadata.invoke(menu, (Object) new long[] {-4, 1, 0, 0, 0x100, 101, 1234, 1792841040});
      assertEquals(View.VISIBLE, status.getVisibility());
      assertEquals(controller.get().getString(R.string.ingame_instant_failed), status.getText().toString());
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 4, false);
      ViewGroup body = (ViewGroup) field(menu, "body");
      // found by their labels (the page's rows moved with the Potato mode, 30.10.2026)
      Button skip = findButton(body, controller.get().getString(R.string.ingame_frame_skip));  // (1.1.0: an arrow selector)
      Button smooth = findButton(body, controller.get().getString(R.string.ingame_smooth60));
      assertNotNull(skip); assertNotNull(smooth);
      assertTrue(skip.isEnabled()); assertTrue(skip.hasOnClickListeners());
      // merged draw calls: a toggle of its own below 60 FPS (30.10.2026)
      Button merge = findButton(body, controller.get().getString(R.string.ingame_merge_draws));
      assertNotNull(merge); assertTrue(merge.hasOnClickListeners());
      // Potato mode, 0.99.3: "Particle effects" in three steps (normal, less, none: bits 2..3) and "Shadows & light"
      // (bit 4 when off) in a second row under weather and special effects, all as before by default
      Button weather = findButton(body, controller.get().getString(R.string.ingame_potato_weather));
      Button particles = findButton(body, controller.get().getString(R.string.ingame_potato_particles));
      Button shadows = findButton(body, controller.get().getString(R.string.ingame_potato_shadows));
      assertNotNull(particles); assertNotNull(shadows);
      assertNotSame(weather.getParent(), particles.getParent()); assertSame(particles.getParent(), shadows.getParent());
      String normal = controller.get().getString(R.string.ingame_particles_normal);
      // (1.1.0, an arrow selector: lit while not "None")
      assertTrue(particles.getText().toString().endsWith(normal)); assertTrue(particles.isSelected());
      assertTrue(shadows.isSelected());
      particles.performClick();
      // lit at "Less" too, off only at "None" (wish of the project owner, 09.11.2026)
      assertEquals(4, Bridge.potato); assertTrue(particles.isSelected());
      assertTrue(particles.getText().toString().endsWith(controller.get().getString(R.string.ingame_particles_less)));
      particles.performClick();
      assertEquals(8, Bridge.potato); assertFalse(particles.isSelected());
      assertTrue(particles.getText().toString().endsWith(controller.get().getString(R.string.ingame_particles_none)));
      shadows.performClick();
      assertEquals(8 | 16, Bridge.potato); assertFalse(shadows.isSelected());
      weather.performClick();
      assertEquals(1 | 8 | 16, Bridge.potato);
      particles.performClick();
      assertEquals(1 | 16, Bridge.potato); assertTrue(particles.isSelected());
      assertTrue(particles.getText().toString().endsWith(normal));
      shadows.performClick(); weather.performClick();
      assertEquals(0, Bridge.potato);
      // the FPS display (since 0.99.11 the Information HUD) moved to DISPLAY, OPTIONAL FEATURES, with the minimap (order of
      // the project owner, 04.11.2026)
      assertNull(findButton(body, controller.get().getString(R.string.ingame_info_hud)));
      show.invoke(menu, 1, false);
      body = (ViewGroup) field(menu, "body");
      Button fps = findButton(body, controller.get().getString(R.string.ingame_info_hud));
      Button minimap = findButton(body, controller.get().getString(R.string.ingame_minimap));
      assertNotNull(fps); assertNotNull(minimap);
      assertTrue(fps.hasOnClickListeners()); assertTrue(minimap.hasOnClickListeners());
      assertNotNull(findButton(body, controller.get().getString(R.string.ingame_flares)));
      assertNotNull(findButton(body, controller.get().getString(R.string.ingame_psx)));
      // the filter "Sharpen" (08.11.2026): a third row under the two filters, off by default; each press moves it on,
      // off, light, strong, off, and sends the level
      Button sharpen = findButton(body, controller.get().getString(R.string.ingame_sharpen));
      assertNotNull(sharpen);
      String off = controller.get().getString(R.string.ingame_off);
      assertTrue(sharpen.getText().toString().endsWith(off)); assertFalse(sharpen.isSelected());
      sharpen.performClick();
      assertEquals(1, Bridge.sharpen); assertTrue(sharpen.isSelected());
      assertTrue(sharpen.getText().toString().endsWith(controller.get().getString(R.string.ingame_dof_light)));
      sharpen.performClick();
      assertEquals(2, Bridge.sharpen);
      assertTrue(sharpen.getText().toString().endsWith(controller.get().getString(R.string.ingame_dof_strong)));
      sharpen.performClick();
      assertEquals(0, Bridge.sharpen); assertFalse(sharpen.isSelected());
      assertTrue(sharpen.getText().toString().endsWith(off));
    }
  }

  // the button mapping names the debug menu (SELECT held) only with the debug build's discs; the retail discs have
  // none (a user with the European discs held SELECT for it, 06.11.2026)
  @Test public void appMenuNamesTheDebugMenuOnlyWithTheDebugDiscs() throws Exception {
    final boolean[] debug = {false};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> method.getName().equals("debugMenu") ? debug[0]
            : method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? (Object) 0
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null);
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method label = CoreMenu.class.getDeclaredMethod("actionLabel", int.class); label.setAccessible(true);
      assertEquals(activity.getString(R.string.ingame_act_menu_app), label.invoke(menu, ActionMap.APP_MENU));
      debug[0] = true;
      assertEquals(activity.getString(R.string.ingame_act_menu), label.invoke(menu, ActionMap.APP_MENU));
      assertEquals(activity.getString(R.string.ingame_act_map), label.invoke(menu, ActionMap.MAP));
    }
  }

  /** Management: "Import other discs" asks first (the game ends for it); Cancel keeps the game, the other answer
   *  hands it to the host (CoreActivity: the start screen without the game starting at once). */
  @Test public void managementOffersOtherDiscsAndAsksFirst() throws Exception {
    final int[] calls = {0};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("importDiscs")) calls[0]++;
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? (Object) 0
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 6, false);  // MANAGEMENT
      Button discs = findButton((View) field(menu, "root"), activity.getString(R.string.ingame_import_discs));
      assertNotNull("Management has Import other discs", discs);
      discs.performClick();
      android.app.AlertDialog ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      assertNotNull("it asks first", ask);
      assertEquals(0, calls[0]);
      ask.getButton(android.app.AlertDialog.BUTTON_NEGATIVE).performClick();
      TestLooper.idle();  // the dialog posts its answer
      assertEquals("Cancel keeps the game", 0, calls[0]);
      discs.performClick();
      ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      assertEquals(activity.getString(R.string.ingame_start_screen), ask.getButton(android.app.AlertDialog.BUTTON_POSITIVE).getText().toString());
      ask.getButton(android.app.AlertDialog.BUTTON_POSITIVE).performClick();
      TestLooper.idle();
      assertEquals("Start screen ends the game for the start screen", 1, calls[0]);
    }
  }

  /** Management: "Clear shader cache" (0.99.4) asks first (the game restarts for it); Cancel keeps the shaders,
   *  Restart hands it to the host (CoreActivity: cleared at the restart, ShaderCache). */
  @Test public void managementClearsTheShaderCacheAfterAsking() throws Exception {
    final int[] calls = {0};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("clearShaderCache")) calls[0]++;
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? (Object) 0
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 6, false);  // MANAGEMENT
      Button clear = findButton((View) field(menu, "root"), activity.getString(R.string.ingame_shader_clear));
      assertNotNull("Management has Clear shader cache", clear);
      clear.performClick();
      android.app.AlertDialog ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      assertNotNull("it asks first", ask);
      ask.getButton(android.app.AlertDialog.BUTTON_NEGATIVE).performClick();
      TestLooper.idle();
      assertEquals("Cancel keeps the shaders", 0, calls[0]);
      clear.performClick();
      ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      assertEquals(activity.getString(R.string.ingame_restart), ask.getButton(android.app.AlertDialog.BUTTON_POSITIVE).getText().toString());
      ask.getButton(android.app.AlertDialog.BUTTON_POSITIVE).performClick();
      TestLooper.idle();
      assertEquals("Restart clears the shader cache", 1, calls[0]);
    }
  }

  /** Management (0.99.15): the memory card's four buttons in this order (.gci export, .zip export, import, Delete All
   *  Data); the .zip also with an empty card; "Delete All Data" asks in the red question with Cancel and Delete,
   *  Cancel (and B) keep everything, Delete hands it to the host (CoreActivity: the restart without the card's data). */
  @Test public void managementExportsZipAndDeletesAllDataAfterAsking() throws Exception {
    final int[] wipes = {0}, zips = {0};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("wipePlayerData")) wipes[0]++;
          if (method.getName().equals("exportZip")) zips[0]++;
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? (Object) 0
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 6, false);  // MANAGEMENT
      View root = (View) field(menu, "root");
      Button gci = findButton(root, activity.getString(R.string.ingame_export));
      Button zip = findButton(root, activity.getString(R.string.ingame_export_zip));
      Button imports = findButton(root, activity.getString(R.string.ingame_import));
      Button wipe = findButton(root, activity.getString(R.string.ingame_wipe));
      assertNotNull(zip);
      assertNotNull(wipe);
      ViewGroup row = (ViewGroup) gci.getParent();
      assertSame("one group of buttons", row, zip.getParent());
      assertSame(row, wipe.getParent());
      assertEquals(0, row.indexOfChild(gci));
      assertEquals(1, row.indexOfChild(zip));
      assertEquals(2, row.indexOfChild(imports));
      assertEquals(3, row.indexOfChild(wipe));
      assertFalse("no saves: no .gci", gci.isEnabled());
      assertTrue("the .zip holds the settings anyway", zip.isEnabled());
      zip.performClick();
      assertEquals(1, zips[0]);
      // the summary and the heading at the bottom (wish of the project owner, 14.11.2026)
      assertNotNull(findText(root, activity.getString(R.string.ingame_source).toUpperCase(Locale.getDefault())));
      // and under the link the note that CubeEvil4 is only there (the project owner, 15.11.2026)
      assertNotNull(findText(root, activity.getString(R.string.ingame_source_only) + "\n"
          + activity.getString(R.string.ingame_source_avoid)));
      wipe.performClick();
      TestLooper.idle();
      android.app.AlertDialog ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      assertNotNull("it asks first", ask);
      View box = ask.getWindow().getDecorView();
      assertNotNull("the text says what goes and what stays", findText(box, activity.getString(R.string.ingame_wipe_text)));
      Button cancel = findButton(box, activity.getString(R.string.ingame_wipe_cancel));
      Button delete = findButton(box, activity.getString(R.string.ingame_wipe_yes));
      assertNotNull(cancel);
      assertNotNull(delete);
      assertTrue("the focus starts on Cancel", cancel.isFocused());
      cancel.performClick();
      TestLooper.idle();
      assertFalse(ask.isShowing());
      assertEquals("Cancel keeps everything", 0, wipes[0]);
      wipe.performClick();
      TestLooper.idle();
      ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      ask.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_B));
      TestLooper.idle();
      assertFalse("B cancels", ask.isShowing());
      assertEquals(0, wipes[0]);
      wipe.performClick();
      TestLooper.idle();
      ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
      findButton(ask.getWindow().getDecorView(), activity.getString(R.string.ingame_wipe_yes)).performClick();
      TestLooper.idle();
      assertEquals("Delete hands it to the host", 1, wipes[0]);
    }
  }

  /** With Vulkan (09.11.2026): "Merge draw calls" and "Vertex ring buffer" greyed out and shown off, a line says why;
   *  the stored choice stays for OpenGL ES. */
  @Test public void vulkanGreysMergeAndVertexRingOut() throws Exception {
    for (boolean vulkan : new boolean[] {false, true}) {
      CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
          new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
            if (method.getName().equals("vulkanRunning")) return vulkan;
            return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? (Object) 0
                : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == double.class ? (Object) 60.0 : null;
          });
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0).edit().clear()
            .putBoolean("mergeDraws", true).putBoolean("vertexRing", true).apply();
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
        show.invoke(menu, 4, false);  // PERFORMANCE
        View root = (View) field(menu, "root");
        Button merge = findButton(root, activity.getString(R.string.ingame_merge_draws));
        Button ring = findButton(root, activity.getString(R.string.ingame_vertex_ring));
        assertNotNull(merge); assertNotNull(ring);
        assertEquals(!vulkan, merge.isEnabled()); assertEquals(!vulkan, ring.isEnabled());
        assertEquals(!vulkan, merge.isSelected()); assertEquals(!vulkan, ring.isSelected());
        assertEquals(vulkan, hasText(root, activity.getString(R.string.ingame_vulkan_perf_help)));
        assertTrue(activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0).getBoolean("mergeDraws", false));
      }
    }
  }

  /** Lens flares in three steps (0.99.6): off, lens flares, halos + lens flares; the old switch "flares" on becomes the
   *  third step (as it looked before), off the first; lit while not off. */
  @Test public void lensFlaresMoveThroughThreeStepsAndKeepTheOldSwitch() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      android.content.SharedPreferences g = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0);
      g.edit().clear().putBoolean("flares", true).apply();
      assertEquals(2, CoreBridge.flareMode(g));
      g.edit().clear().putBoolean("flares", false).apply();
      assertEquals(0, CoreBridge.flareMode(g));
      g.edit().clear().putBoolean("flares", true).apply();
      CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 1, false);  // DISPLAY
      View root = (View) field(menu, "root");
      Button flares = findButton(root, activity.getString(R.string.ingame_flares));
      assertNotNull(flares);
      String both = activity.getString(R.string.ingame_flares_both), lens = activity.getString(R.string.ingame_flares_lens);
      assertTrue(flares.getText().toString().endsWith(both)); assertTrue(flares.isSelected());
      flares.performClick();
      assertEquals(0, CoreBridge.flareMode(g)); assertFalse(flares.isSelected());
      assertTrue(flares.getText().toString().endsWith(activity.getString(R.string.ingame_off)));
      flares.performClick();
      assertEquals(1, CoreBridge.flareMode(g)); assertTrue(flares.isSelected());
      assertTrue(flares.getText().toString().endsWith(lens));
      flares.performClick();
      assertEquals(2, CoreBridge.flareMode(g)); assertTrue(flares.getText().toString().endsWith(both));
      assertTrue(hasText(root, activity.getString(R.string.ingame_flares_help)));
    }
  }

  /** Bloom, 0.99.13: under Display's effects, off by default, Off -> Light -> Strong -> Off, its own line of help;
   *  the renderer gets none while a Potato mode setting is on (CoreBridge.bloomSent). */
  @Test public void bloomMovesThroughThreeStepsAndStaysOffWithPotato() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      android.content.SharedPreferences g = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0);
      g.edit().clear().apply();
      assertEquals(0, CoreMenu.bloomLevel(g));
      CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 1, false);  // DISPLAY
      View root = (View) field(menu, "root");
      Button bloom = findButton(root, activity.getString(R.string.ingame_bloom));
      assertNotNull(bloom);
      assertFalse(bloom.isSelected());
      assertTrue(bloom.getText().toString().endsWith(activity.getString(R.string.ingame_off)));
      try {
        bloom.performClick();
      } catch (UnsatisfiedLinkError e) {
        // (no native library in the host tests: CoreBridge.setBloom is the last thing the press does)
      }
      assertEquals(1, CoreMenu.bloomLevel(g));
      assertTrue(bloom.isSelected());
      assertTrue(bloom.getText().toString().endsWith(activity.getString(R.string.ingame_dof_light)));
      assertTrue(hasText(root, activity.getString(R.string.ingame_bloom_help)));
      assertEquals(1, CoreBridge.bloomSent(g));
      g.edit().putBoolean("weatherEffects", false).apply();
      assertEquals("off while a Potato mode setting is on", 0, CoreBridge.bloomSent(g));
      g.edit().putBoolean("weatherEffects", true).putInt(CoreBridge.VIEW_DISTANCE, 1).apply();
      assertEquals("off with a shorter view distance", 0, CoreBridge.bloomSent(g));
      g.edit().putInt(CoreBridge.VIEW_DISTANCE, 0).putInt(CoreMenu.BLOOM_LEVEL, 2).apply();
      assertEquals(2, CoreBridge.bloomSent(g));
    }
  }

  /** Potato mode, 0.99.6: "View distance" in a third row with its own line of help, each press one step on; since
   *  0.99.18 (the project owner, 15.11.2026) Normal, Medium, Short, None (the 35 %), Far, then Normal again, lit but at
   *  None, and after the text eyes: 3, 2, 1, a crossed-out one, 4 (CoreMenu.Eyes); TalkBack reads the text alone. */
  @Test public void viewDistanceMovesThroughFiveStepsWithEyes() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      android.content.SharedPreferences g = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0);
      g.edit().clear().apply();
      CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 4, false);  // PERFORMANCE
      View root = (View) field(menu, "root");
      Button view = findButton(root, activity.getString(R.string.ingame_potato_view));
      Button particles = findButton(root, activity.getString(R.string.ingame_potato_particles));
      assertNotNull(view); assertNotSame(particles.getParent(), view.getParent());
      int[] levels = {0, 1, 2, 3, 4, 0};
      int[] labels = {R.string.ingame_view_normal, R.string.ingame_view_medium, R.string.ingame_view_short,
          R.string.ingame_view_very_short, R.string.ingame_view_far, R.string.ingame_view_normal};
      // 1.1.0: an arrow selector (Far, Normal, Medium, Short, None from the left), lit while not None
      boolean[] lit = {true, true, true, false, true, true};
      assertTrue(view instanceof ArrowSelector);
      assertEquals(5, ((ArrowSelector) view).count());
      for (int i = 0; i < levels.length; i++) {
        if (i > 0) view.performClick();
        String where = "step " + i;
        assertEquals(where, levels[i], CoreBridge.viewDistance(g));
        assertEquals(where + ": lit", lit[i], view.isSelected());
        String name = activity.getString(R.string.ingame_potato_view), value = activity.getString(labels[i]);
        assertEquals(where + ": the name over the value", name + "\n" + value, view.getText().toString());
        assertEquals(where + ": TalkBack reads both", name + ", " + value, view.getContentDescription().toString());
      }
      assertEquals(35, CoreBridge.VIEW_PERCENT[3]);
      assertTrue(CoreBridge.VIEW_PERCENT[4] > 100);
      assertTrue(hasText(root, activity.getString(R.string.ingame_view_help)));
    }
  }

  /** 0.99.18: the levels saved before (0 Normal, 1 Medium, 2 Short) mean what they meant, the new ones keep their
   *  numbers (3 Very short, 4 Far), an unknown value is Normal; Far keeps the bloom as Normal does, the shorter levels
   *  turn it off; a profile's Far reaches the core as Far. */
  @Test public void viewDistanceLevelsKeepSavedValuesAndTheBloom() {
    android.content.SharedPreferences g = org.robolectric.RuntimeEnvironment.getApplication()
        .getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0);
    g.edit().clear().apply();
    // (since 1.0.0 Medium 65 % and Far 160 %: a saved level keeps its meaning, only its percent changed)
    int[] percent = {100, 65, 50, 35, 160};
    for (int i = 0; i < percent.length; i++) {
      g.edit().putInt(CoreBridge.VIEW_DISTANCE, i).apply();
      assertEquals(i, CoreBridge.viewDistance(g));
      assertEquals(percent[i], CoreBridge.VIEW_PERCENT[i]);
    }
    g.edit().putInt(CoreBridge.VIEW_DISTANCE, 4).apply();
    assertEquals(CoreBridge.VIEW_FAR, CoreBridge.viewDistance(g));
    g.edit().putInt(CoreBridge.VIEW_DISTANCE, 9).apply();
    assertEquals(CoreBridge.VIEW_NORMAL, CoreBridge.viewDistance(g));
    g.edit().putInt(CoreBridge.VIEW_DISTANCE, -1).apply();
    assertEquals(CoreBridge.VIEW_NORMAL, CoreBridge.viewDistance(g));
    g.edit().putInt(CoreMenu.BLOOM_LEVEL, 2).putInt(CoreBridge.VIEW_DISTANCE, CoreBridge.VIEW_FAR).apply();
    assertEquals(2, CoreBridge.bloomSent(g));
    g.edit().putInt(CoreBridge.VIEW_DISTANCE, CoreBridge.VIEW_VERY_SHORT).apply();
    assertEquals(0, CoreBridge.bloomSent(g));
    // the menu's order round: Normal, Medium, Short, Very short, Far, Normal
    int level = CoreBridge.VIEW_NORMAL;
    int[] seen = new int[6];
    for (int i = 0; i < seen.length; i++) { seen[i] = level; level = CoreBridge.nextView(level); }
    assertArrayEquals(new int[] {0, 1, 2, 3, 4, 0}, seen);
    // a profile with Far (the Custom profile holds the raw value): the core gets Far's percent
    int[] v = PerfProfile.current(g);
    assertEquals(CoreBridge.VIEW_VERY_SHORT, v[PerfProfile.VIEW]);
    assertEquals(CoreBridge.VIEW_PERCENT[CoreBridge.VIEW_FAR],
        CoreBridge.VIEW_PERCENT[CoreBridge.viewLevel(CoreBridge.VIEW_FAR)]);
  }

  /** The performance profiles in one row with the benchmark at its end, the lines under it over the whole width (the
   *  project owner after the Retroid test, 11.11.2026), in each language at the three font sizes on a handheld's width;
   *  on a narrow window the row may break, never clip (allPagesFitAcrossLanguagesWidthsAndFontScales). */
  @Test public void profileRowHoldsFiveButtonsAndTheTextsSpanTheWidth() throws Exception {
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> method.getReturnType() == boolean.class
            ? Boolean.FALSE : method.getReturnType() == int.class ? 0 : method.getReturnType() == float.class ? (Object) 1f
            : method.getReturnType() == double.class ? (Object) 60.0 : null);
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    for (String language : new String[] {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"})
      for (float scale : new float[] {1f, 1.15f, 1.3f}) {
        try (var controller = Robolectric.buildActivity(Activity.class).create()) {
          Activity activity = controller.get();
          Configuration config = new Configuration(activity.getResources().getConfiguration());
          config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
          activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
          CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
          show.invoke(menu, 0, false);
          View root = (View) field(menu, "root");
          root.setLayoutDirection(config.getLayoutDirection());
          float density = activity.getResources().getDisplayMetrics().density;
          measure(root, Math.round(960 * density), Math.round(300 * density));
          String where = language + "/" + scale;
          Button custom = findButton(root, activity.getString(R.string.ingame_profile_custom));
          assertNotNull(where, custom);
          MenuLayout.Buttons row = (MenuLayout.Buttons) custom.getParent();
          assertEquals(where + ": four profiles and the benchmark", 5, row.getChildCount());
          assertEquals(where, activity.getString(R.string.ingame_benchmark), ((Button) row.getChildAt(4)).getText().toString());
          for (int i = 1; i < 5; i++) assertEquals(where + ": one row", row.getChildAt(0).getTop(), row.getChildAt(i).getTop());
          checkBounds(row, where);
          // the line under it and the help: as wide as the body
          ViewGroup body = (ViewGroup) row.getParent();
          int index = body.indexOfChild(row);
          for (int i = index + 1; i <= index + 2; i++) {
            View line = body.getChildAt(i);
            assertTrue(where + ": a text under the row", line instanceof TextView && !(line instanceof Button));
            assertEquals(where + ": over the whole width", body.getWidth() - body.getPaddingLeft() - body.getPaddingRight(),
                line.getWidth());
          }
        }
      }
  }

  private static boolean hasText(View view, String text) {
    if (view instanceof TextView && !(view instanceof Button) && ((TextView) view).getText().toString().equals(text)) return true;
    if (view instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup) view).getChildCount(); i++) if (hasText(((ViewGroup) view).getChildAt(i), text)) return true;
    return false;
  }
}
