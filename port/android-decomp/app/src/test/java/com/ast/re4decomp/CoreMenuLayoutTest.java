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
        (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.TRUE : null);
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    Method metadata = CoreMenu.class.getDeclaredMethod("showInstant", long[].class); metadata.setAccessible(true);
    for (String language : languages) for (float scale : new float[] {1f, 1.5f, 2f}) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        View root = (View) field(menu, "root");
        root.setLayoutDirection(config.getLayoutDirection());
        for (int page = 0; page < 6; page++) {  // 5: the controller configuration
          show.invoke(menu, page, false);
          if (page == 0) metadata.invoke(menu, (Object) new long[] {0, 1, 1, 1, 0x100, 101, 123456, 1792841040});
          if (page == 4) {
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
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0 : null;
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

  @Test public void gyroSwitchKeepsTheSameGapAboveAndBelow() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 2, false);
      View root = (View) field(menu, "root");
      ViewGroup body = (ViewGroup) field(menu, "body");
      measure(root, Math.round(960 * activity.getResources().getDisplayMetrics().density),
          Math.round(300 * activity.getResources().getDisplayMetrics().density));
      Button gyro = findButton(body, activity.getString(R.string.ingame_gyro_none).split(" ")[0]);
      Button above = findButton(body, activity.getString(R.string.ingame_latch).split(" ")[0]);
      Button below = findButton(body, activity.getString(R.string.ingame_gyro_invert_v).split(" ")[0]);
      assertNotNull(gyro); assertNotNull(above); assertNotNull(below);
      int gapAbove = top(gyro, body) - (top(above, body) + above.getHeight());
      int gapBelow = top(below, body) - (top(gyro, body) + gyro.getHeight());
      assertTrue("a gap above", gapAbove > 0);
      assertEquals("the same gap above and below the gyro switch", gapBelow, gapAbove, 2);
    }
  }

  @Test public void controllerConfigurationPutsFunctionsOnFreeKeysAsWell() throws Exception {
    final int[][] sent = {null};
    final boolean[] controller = {false};
    final int[] calls = {0};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("extraKeysChanged")) { sent[0] = (int[]) args[0]; calls[0]++; }
          if (method.getName().equals("controllerConnected")) return controller[0];
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0 : null;
        });
    try (var controllerActivity = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controllerActivity.get();
      android.content.SharedPreferences prefs = activity.getSharedPreferences(CoreActivity.INPUT_PREFS, 0);
      prefs.edit().clear().apply();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      // on the controls page, at the bottom: only with a controller
      show.invoke(menu, 2, false);
      Button open = (Button) field(menu, "controllerButton");
      assertNotNull(open);
      assertFalse("without a controller", open.isEnabled());
      controller[0] = true;
      show.invoke(menu, 2, false);
      open = (Button) field(menu, "controllerButton");
      assertTrue("with a controller", open.isEnabled());
      assertTrue(open.getText().toString().startsWith(activity.getString(R.string.ingame_controller_config)));
      open.performClick();
      Button[] keys = (Button[]) field(menu, "bindingButtons");
      assertEquals(13, keys.length);
      for (Button b : keys) assertEquals("no extra key yet", "—", b.getText().toString());
      // the map (Z) also on L1
      keys[CoreInput.F_Z].performClick();
      assertEquals(activity.getString(R.string.ingame_controller_press), keys[CoreInput.F_Z].getText().toString());
      // a key of the layout (Y) is not taken: the waiting ends, nothing changes
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_Y));
      assertEquals(0, calls[0]);
      assertEquals("—", keys[CoreInput.F_Z].getText().toString());
      keys[CoreInput.F_Z].performClick();
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_L1));
      assertTrue("the release is swallowed", menu.dispatchKeyEvent(
          new android.view.KeyEvent(android.view.KeyEvent.ACTION_UP, android.view.KeyEvent.KEYCODE_BUTTON_L1)));
      assertEquals(1, calls[0]);
      assertEquals(android.view.KeyEvent.KEYCODE_BUTTON_L1, sent[0][CoreInput.F_Z]);
      assertEquals("L1", keys[CoreInput.F_Z].getText().toString());
      prefs.edit().putString(CoreActivity.EXTRA_KEYS, CoreInput.encode(sent[0])).apply();
      // L1 to running (B): the map loses it
      keys[CoreInput.F_B].performClick();
      menu.dispatchKeyEvent(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, android.view.KeyEvent.KEYCODE_BUTTON_L1));
      assertEquals(android.view.KeyEvent.KEYCODE_BUTTON_L1, sent[0][CoreInput.F_B]);
      assertEquals(0, sent[0][CoreInput.F_Z]);
      assertEquals("L1", keys[CoreInput.F_B].getText().toString());
      assertEquals("—", keys[CoreInput.F_Z].getText().toString());
      // the same button twice: nothing changes
      keys[CoreInput.F_A].performClick(); keys[CoreInput.F_A].performClick();
      assertEquals("—", keys[CoreInput.F_A].getText().toString());
      // back goes to the controls page
      Method back = CoreMenu.class.getDeclaredMethod("goBack"); back.setAccessible(true);
      back.invoke(menu);
      assertEquals(2, (int) (Integer) field(menu, "page"));
    }
  }

  @Test public void cheatsComeFirstOnManagementAndSwitchesSendTheirBits() throws Exception {
    final int[] sent = {-1};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("cheatsChanged")) sent[0] = (Integer) args[0];
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0 : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 4, false);
      ViewGroup body = (ViewGroup) field(menu, "body");
      assertTrue(((TextView) body.getChildAt(0)).getText().toString()
          .equalsIgnoreCase(activity.getString(R.string.ingame_cheats)));
      ViewGroup switches = (ViewGroup) body.getChildAt(1), actions = (ViewGroup) body.getChildAt(2);
      assertEquals(3, switches.getChildCount()); assertEquals(3, actions.getChildCount());
      Button health = (Button) switches.getChildAt(0), oneHit = (Button) switches.getChildAt(2);
      assertFalse(health.isSelected());
      health.performClick(); assertEquals(1, sent[0]);
      oneHit.performClick(); assertEquals(5, sent[0]);
      health.performClick(); assertEquals(4, sent[0]);
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
      show.invoke(menu, 3, false);
      ViewGroup body = (ViewGroup) field(menu, "body");
      MenuLayout.Columns output = (MenuLayout.Columns) body.getChildAt(3);
      ViewGroup options = (ViewGroup) output.getChildAt(1);
      Button fps = (Button) ((ViewGroup) options.getChildAt(0)).getChildAt(0);
      Button skip = (Button) ((ViewGroup) options.getChildAt(1)).getChildAt(0);
      Button smooth = (Button) ((ViewGroup) options.getChildAt(2)).getChildAt(0);
      assertTrue(fps.getText().toString().contains(controller.get().getString(R.string.ingame_fps_display)));
      assertTrue(skip.getText().toString().contains(controller.get().getString(R.string.ingame_frame_skip)));
      assertTrue(skip.isEnabled()); assertTrue(skip.hasOnClickListeners());
      assertTrue(smooth.getText().toString().contains(controller.get().getString(R.string.ingame_smooth60)));
    }
  }
}
