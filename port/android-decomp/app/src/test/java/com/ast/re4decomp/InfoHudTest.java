package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;

/** The Information HUD (0.99.11, before the FPS display): its line, its time and the menu's button with its boxes. */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class InfoHudTest {
  @org.junit.Before public void noAnimationLeftRunning() {
    TestLooper.stopAnimations("left running before " + getClass().getSimpleName());
  }

  @Test public void playTimeHasTwoDigitsEach() {
    assertEquals("00:00:00", InfoHud.time(0));
    assertEquals("00:00:09", InfoHud.time(9));
    assertEquals("00:01:00", InfoHud.time(60));
    assertEquals("07:18:43", InfoHud.time(7 * 3600 + 18 * 60 + 43));  // the instant slot of the report
    assertEquals("99:59:59", InfoHud.time(99 * 3600 + 59 * 60 + 59));
    assertEquals("100:00:00", InfoHud.time(100 * 3600));
    assertEquals("outside a game", "--:--:--", InfoHud.time(-1));
  }

  @Test public void theLineHasTheChosenPartsInOrder() {
    String fps = "30 FPS (Game)", hz = "60 Hz", res = "1920×1008 (3×)", time = "07:18:43";
    assertEquals("as the FPS display, and the time",
        "30 FPS (Game) · 60 Hz · 1920×1008 (3×) · 07:18:43", InfoHud.line(true, fps, true, hz, true, res, true, time));
    assertEquals("60 Hz · 07:18:43", InfoHud.line(false, fps, true, hz, false, res, true, time));
    assertEquals("07:18:43", InfoHud.line(false, fps, false, hz, false, res, true, time));
    assertEquals("an unknown part left out", "30 FPS (Game) · 07:18:43", InfoHud.line(true, fps, true, "", true, "", true, time));
    assertEquals("none", "", InfoHud.line(false, fps, false, hz, false, res, false, time));
  }

  private static void boxes(View v, List<CheckBox> into) {
    if (v instanceof CheckBox) into.add((CheckBox) v);
    if (v instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) boxes(((ViewGroup) v).getChildAt(i), into);
  }

  private static Button button(View v, String start) {
    if (v instanceof Button && !(v instanceof CheckBox) && ((Button) v).getText().toString().startsWith(start)) return (Button) v;
    if (v instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) {
        Button b = button(((ViewGroup) v).getChildAt(i), start);
        if (b != null) return b;
      }
    return null;
  }

  @Test public void theButtonMovesTheHudAndItsBoxesChooseWhatItShows() throws Exception {
    final List<Integer> places = new ArrayList<>();
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        (proxy, method, args) -> {
          if (method.getName().equals("fpsChanged")) places.add((Integer) args[0]);
          return method.getReturnType() == boolean.class ? Boolean.TRUE
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
              : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      Activity activity = controller.get();
      SharedPreferences g = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, Context.MODE_PRIVATE);
      // a FPS display of before (the old switch, bottom left): the HUD there, everything shown
      g.edit().clear().putBoolean("fps", true).apply();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      Field bodyField = CoreMenu.class.getDeclaredField("body"); bodyField.setAccessible(true);
      show.invoke(menu, 1, false);
      View body = (View) bodyField.get(menu);
      Button hud = button(body, activity.getString(R.string.ingame_info_hud));
      assertNotNull("the Information HUD's button", hud);
      assertTrue("on (bottom left)", hud.isSelected());
      assertTrue(hud.getText().toString().endsWith(activity.getString(R.string.ingame_fps_bottom_left)));
      List<CheckBox> found = new ArrayList<>();
      boxes(body, found);
      assertEquals("four boxes", 4, found.size());
      String[] labels = {activity.getString(R.string.ingame_hud_fps), activity.getString(R.string.ingame_hud_hz),
          activity.getString(R.string.ingame_resolution), activity.getString(R.string.ingame_hud_time)};
      for (int i = 0; i < 4; i++) {
        assertEquals(labels[i], found.get(i).getText().toString());
        assertTrue(labels[i] + " on until cleared", found.get(i).isChecked());
        assertEquals(labels[i] + " shown", View.VISIBLE, ((View) found.get(i).getParent()).getVisibility());
      }
      // a box: stored, the HUD takes it at once
      found.get(0).performClick();
      assertFalse(InfoHud.shows(g, InfoHud.FPS));
      assertEquals(Integer.valueOf(1), places.get(places.size() - 1));
      found.get(0).performClick();
      assertTrue(InfoHud.shows(g, InfoHud.FPS));
      found.get(3).performClick();
      assertFalse(InfoHud.shows(g, InfoHud.TIME));
      // the button: on through the four places, then off (the boxes hidden)
      int[] next = {2, 3, 4, 0};
      for (int place : next) {
        hud.performClick();
        assertEquals(place, CoreActivity.fpsPlace(g));
        assertEquals(Integer.valueOf(place), places.get(places.size() - 1));
      }
      assertFalse("off", hud.isSelected());
      assertTrue(hud.getText().toString().endsWith(activity.getString(R.string.ingame_off)));
      assertEquals("the boxes hidden while off", View.GONE, ((View) found.get(0).getParent()).getVisibility());
      // the page shown again: the boxes keep what was chosen and stay hidden while off
      show.invoke(menu, 1, false);
      body = (View) bodyField.get(menu);
      found.clear();
      boxes(body, found);
      assertEquals(4, found.size());
      assertFalse("the time's box kept", found.get(3).isChecked());
      assertEquals(View.GONE, ((View) found.get(0).getParent()).getVisibility());
      button(body, activity.getString(R.string.ingame_info_hud)).performClick();
      assertEquals(1, CoreActivity.fpsPlace(g));
      assertEquals(View.VISIBLE, ((View) found.get(0).getParent()).getVisibility());
    }
  }
}
