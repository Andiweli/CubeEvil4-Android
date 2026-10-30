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
  }

  private static Object field(Object owner, String name) throws Exception {
    Field f = owner.getClass().getDeclaredField(name); f.setAccessible(true); return f.get(owner);
  }

  private static void measure(View root, int width, int height) {
    root.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY));
    root.layout(0, 0, width, height);
  }

  private static void checkBounds(View view, String scenario) {
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
    String[] languages = {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi"};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        // gameLanguages: the home page with the European discs' "Game language" too
        (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.TRUE
            : method.getReturnType() == float.class ? (Object) 1f : null);
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    Method metadata = CoreMenu.class.getDeclaredMethod("showInstant", long[].class); metadata.setAccessible(true);
    for (String language : languages) for (float scale : new float[] {1f, 1.5f, 2f}) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
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
              : method.getReturnType() == float.class ? (Object) 1f : null;
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
      MenuLayout.Keys keys = findKeys(root);
      assertNotNull(keys);
      assertEquals(8, keys.getChildCount());
      String[] labels = {activity.getString(R.string.ingame_dynamic), "1×", "2×", "3×", "4×", "1.5×", "2.5×", "3.5×"};
      for (int i = 0; i < 8; i++) assertEquals(labels[i], ((Button) keys.getChildAt(i)).getText().toString());
      assertTrue("3x chosen before stays chosen", keys.getChildAt(3).isSelected());
      for (int i = 1; i < 8; i++) {
        Button key = (Button) keys.getChildAt(i), first = (Button) keys.getChildAt(0);
        assertEquals("the same text size on every key", first.getTextSize(), key.getTextSize(), 0.01f);
        assertEquals("the same typeface on every key", first.getTypeface(), key.getTypeface());
      }
      View[] k = new View[8];
      for (int i = 0; i < 8; i++) k[i] = keys.getChildAt(i);
      for (int i = 1; i < 5; i++) assertEquals("the whole levels in one row", k[0].getTop(), k[i].getTop());
      for (int i = 5; i < 8; i++) {
        assertTrue("the in-between levels below", k[i].getTop() >= k[0].getBottom());
        assertEquals("as wide as the others", k[1].getWidth(), k[i].getWidth(), 1);
        View left = k[i - 4], right = k[i - 3];  // 1.5x between 1x and 2x ...
        float line = (left.getRight() + right.getLeft()) / 2f, centre = (k[i].getLeft() + k[i].getRight()) / 2f;
        assertEquals("half a key offset: centred on the line between " + labels[i - 4] + " and " + labels[i - 3],
            line, centre, 1.5f);
      }
      k[5].performClick();
      assertEquals(3, g.getInt("scaleHalves", -1));
      assertEquals(3, CoreMenu.scaleHalves(g));
      assertTrue(k[5].isSelected()); assertFalse(k[3].isSelected());
      assertEquals(1, changed[0]);
    }
  }

  private static int top(View v, View root) {
    int y = 0;
    for (View at = v; at != root && at != null; at = (View) at.getParent()) y += at.getTop();
    return y;
  }

  private static Button findButton(View view, String start) {
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

  @Test public void updateButtonSitsLeftOfResumeAndComparesVersions() throws Exception {
    assertEquals(0, AppUpdate.compare("0.95.2-beta", "0.95.2-beta"));
    assertEquals(-1, AppUpdate.compare("0.95.2-beta", "0.96.0-beta"));
    assertEquals(-1, AppUpdate.compare("0.95.2-beta", "v0.95.10-beta"));
    assertEquals(1, AppUpdate.compare("1.0", "1.0-beta"));
    assertEquals(1, AppUpdate.compare("0.96.0-beta", "0.95.2-beta"));
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
      Button update = (Button) field(menu, "update"), resume = (Button) field(menu, "resume");
      assertEquals("hidden until GitHub answered", View.GONE, update.getVisibility());
      menu.showUpdate(AppUpdate.CURRENT);
      View root = (View) field(menu, "root");
      measure(root, Math.round(960 * activity.getResources().getDisplayMetrics().density),
          Math.round(300 * activity.getResources().getDisplayMetrics().density));
      assertEquals(activity.getString(R.string.ingame_up_to_date), update.getText().toString());
      assertFalse("up to date: not clickable", update.isEnabled());
      assertTrue("left of Back to the game", update.getRight() <= resume.getLeft());
      assertEquals("in the same row", update.getTop(), resume.getTop());
      menu.showUpdate(AppUpdate.OLDER);
      assertEquals(activity.getString(R.string.ingame_update), update.getText().toString());
      assertTrue("an update: clickable", update.isEnabled());
    }
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
      // the comfort controls' "Invert vertical" (the gyro's above it may read the same): the one after the latches
      Button aimInvert = findButton((ViewGroup) body.getChildAt(rowIndex(latch, body) + 1),
          activity.getString(R.string.ingame_aim_invert_v));
      Button touchAim = findButton(body, activity.getString(R.string.ingame_aim_move_stick));
      Button gyro = findButton(body, activity.getString(R.string.ingame_gyro_none).split(" ")[0]);
      Button gyroInvert = findButton(body, activity.getString(R.string.ingame_gyro_invert_h));
      Button mapping = findButton(body, activity.getString(R.string.ingame_controller_config));
      assertNotNull(alternate); assertNotNull(latch); assertNotNull(aimInvert); assertNotNull(touchAim);
      assertNotNull(gyro); assertNotNull(gyroInvert); assertNotNull(mapping);
      int motion = -1, comfort = -1, devices = -1;
      for (int i = 0; i < body.getChildCount(); i++) {
        if (!(body.getChildAt(i) instanceof TextView) || body.getChildAt(i) instanceof Button) continue;
        String t = ((TextView) body.getChildAt(i)).getText().toString();
        if (t.equals(activity.getString(R.string.ingame_motion).toUpperCase(Locale.getDefault()))) motion = i;
        if (t.equals(activity.getString(R.string.ingame_comfort).toUpperCase(Locale.getDefault()))) comfort = i;
        if (t.equals(activity.getString(R.string.ingame_controller_section).toUpperCase(Locale.getDefault()))) devices = i;
      }
      // motion controls first (wish of the project owner, 30.10.2026): the gyro switch with its slider, its directions
      assertTrue("the motion heading", motion >= 0);
      assertEquals("the gyro row right under the motion heading", motion + 1, rowIndex(gyro, body));
      assertEquals("the gyro's directions under it", rowIndex(gyro, body) + 1, rowIndex(gyroInvert, body));
      // then the comfort controls: right under the latches "Invert vertical" with "Aim speed", then touch aiming
      assertTrue("the comfort heading after the motion controls", comfort > rowIndex(gyroInvert, body));
      assertEquals("the aim row right under the latches", rowIndex(latch, body) + 1, rowIndex(aimInvert, body));
      assertEquals("touch aiming right under the aim row", rowIndex(aimInvert, body) + 1, rowIndex(touchAim, body));
      // the button mapping at the bottom, under a heading of its own
      assertTrue("the controller heading after the comfort controls", devices > rowIndex(touchAim, body));
      assertEquals("the button mapping right under its heading", devices + 1, rowIndex(mapping, body));
      // one gap between all rows of buttons, the rows with a slider too (wish of the project owner, 30.10.2026)
      int gap = top(latch, body) - top(alternate, body) - alternate.getHeight();
      assertEquals("the gap of the rows of two", Math.round(2 * MenuLayout.GAP * activity.getResources().getDisplayMetrics().density), gap, 1);
      // the rows with a slider: as tall as a row of buttons where the title leaves the slider 32 dp or more (on
      // the devices; this test's fonts are about twice as tall), the switch in the middle: the same gap
      float density = activity.getResources().getDisplayMetrics().density;
      for (Button b : new Button[] {aimInvert, gyro}) {
        ViewGroup row = (ViewGroup) rowOf(b, body), box = (ViewGroup) row.getChildAt(1);
        int title = box.getChildAt(0).getHeight(), slider = box.getChildAt(1).getHeight();
        int buttons = b.getHeight() + Math.round(2 * MenuLayout.GAP * density);
        int expected = Math.max(buttons, title + Math.max(Math.round(32 * density), slider));
        assertEquals("the slider row's height", expected, row.getHeight(), 1);
        assertEquals("the slider fills what the title leaves", Math.max(Math.round(32 * density), buttons - title),
            ((ViewGroup) box.getChildAt(1)).getChildAt(0).getHeight(), 1);
        int above = top(b, body) - top(row, body), below = top(row, body) + row.getHeight() - top(b, body) - b.getHeight();
        assertEquals("the switch in the middle of its row", below, above, 2);
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
              : method.getReturnType() == float.class ? (Object) 1f : null;
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
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("cheatsChanged")) sent[0] = (Integer) args[0];
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0
              : method.getReturnType() == float.class ? (Object) 1f : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 5, false);  // CHEATS, a page of its own since 30.10.2026
      ViewGroup body = (ViewGroup) field(menu, "body");
      ViewGroup switches = (ViewGroup) body.getChildAt(0), actions = (ViewGroup) body.getChildAt(1);
      assertEquals(3, switches.getChildCount()); assertEquals(3, actions.getChildCount());
      Button health = (Button) switches.getChildAt(0), oneHit = (Button) switches.getChildAt(2);
      assertFalse(health.isSelected());
      health.performClick(); assertEquals(1, sent[0]);
      oneHit.performClick(); assertEquals(5, sent[0]);
      health.performClick(); assertEquals(4, sent[0]);
      // the bottom row (29.10.2026): two, automatic QTEs (bit 8) and the extra games' button
      ViewGroup more = (ViewGroup) body.getChildAt(2);
      assertEquals(2, more.getChildCount());
      Button qte = (Button) more.getChildAt(0), extras = (Button) more.getChildAt(1);
      assertTrue(qte.getText().toString().contains(activity.getString(R.string.ingame_cheat_qte)));
      assertTrue(extras.getText().toString().contains(activity.getString(R.string.ingame_cheat_extras)));
      qte.performClick(); assertEquals(12, sent[0]);
      qte.performClick(); assertEquals(4, sent[0]);
      Button killer7 = (Button) actions.getChildAt(0);
      assertTrue(killer7.getText().toString().contains(activity.getString(R.string.ingame_cheat_killer7)));
      // a button lights up for a moment after a press
      killer7.performClick(); assertTrue(killer7.isSelected());
      org.robolectric.shadows.ShadowLooper.idleMainLooper(2, java.util.concurrent.TimeUnit.SECONDS);
      assertFalse(killer7.isSelected());
    }
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
      Button fps = findButton(body, controller.get().getString(R.string.ingame_fps_display));
      Button skip = findButton(body, controller.get().getString(R.string.ingame_frame_skip));
      Button smooth = findButton(body, controller.get().getString(R.string.ingame_smooth60));
      assertNotNull(fps); assertNotNull(skip); assertNotNull(smooth);
      assertTrue(skip.isEnabled()); assertTrue(skip.hasOnClickListeners());
    }
  }
}
