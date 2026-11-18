package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * TouchLayoutFile: the player's own RetroTouch layout out with the exported saves and back (wish of the project owner,
 * 07.11.2026), with RetroTouch's JSON (org.json, hence Robolectric).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class TouchLayoutFileTest {
  private SharedPreferences prefs;

  @Before public void clean() {
    prefs = RuntimeEnvironment.getApplication().getSharedPreferences(TouchLayoutFile.PREFS, Context.MODE_PRIVATE);
    prefs.edit().clear().commit();
  }

  private static String defaults() {
    return TouchControls.defaultLayout().toJson();
  }

  /** The default layout with its control labelled `label` (Z: a round button) at x, y. */
  private static String with(String label, double x, double y) throws JSONException {
    JSONObject root = new JSONObject(defaults());
    JSONArray c = root.getJSONArray("controls");
    for (int i = 0; i < c.length(); i++) {
      if (c.getJSONObject(i).getString("label").equals(label)) {
        c.getJSONObject(i).put("x", x).put("y", y);
        return root.toString();
      }
    }
    throw new AssertionError("no " + label);
  }

  private static JSONObject first(String layout, String label) throws JSONException {
    JSONArray c = new JSONObject(layout).getJSONArray("controls");
    for (int i = 0; i < c.length(); i++) if (c.getJSONObject(i).getString("label").equals(label)) return c.getJSONObject(i);
    throw new AssertionError("no " + label);
  }

  @Test public void noFileWithoutALayoutOfTheirOwn() {
    assertNull(TouchLayoutFile.export(prefs, TouchControls.defaultLayout(), 1920, 1080, "0.99.2-rc"));
    // RetroTouch's "reset" saves the defaults: still none of the player's own
    prefs.edit().putString(TouchLayoutFile.KEY, defaults()).commit();
    assertFalse(TouchLayoutFile.ownSaved(prefs, TouchControls.defaultLayout()));
    assertNull(TouchLayoutFile.export(prefs, TouchControls.defaultLayout(), 1920, 1080, "0.99.2-rc"));
    // a damaged one RetroTouch ignores is none either
    prefs.edit().putString(TouchLayoutFile.KEY, "{\"format\":2").commit();
    assertNull(TouchLayoutFile.export(prefs, TouchControls.defaultLayout(), 1920, 1080, "0.99.2-rc"));
  }

  @Test public void outAndBackAgain() throws JSONException {
    String own = with("Z", 0.40, 0.55);
    String editor = "{\"format\":1,\"settings_x\":0.9,\"settings_y\":0.1,\"settings_scale\":0.8,\"settings_opacity\":0.3,\"toolbar_y\":0.7}";
    prefs.edit().putString(TouchLayoutFile.KEY, own).putString(TouchLayoutFile.EDITOR_KEY, editor).commit();
    assertTrue(TouchLayoutFile.ownSaved(prefs, TouchControls.defaultLayout()));
    byte[] file = TouchLayoutFile.export(prefs, TouchControls.defaultLayout(), 1920, 1080, "0.99.2-rc");
    assertNotNull(file);
    assertTrue(TouchLayoutFile.is(file));
    assertTrue(new String(file, StandardCharsets.UTF_8).trim().startsWith("{\n  \"" + TouchLayoutFile.MARK + "\""));

    prefs.edit().clear().commit();
    TouchLayoutFile.Parsed p = TouchLayoutFile.parse(file);
    assertEquals(0, TouchLayoutFile.apply(prefs, p, 1920, 1080, 2.0f));
    assertTrue(TouchLayoutFile.same(TouchLayoutFile.saved(prefs), own));
    assertEquals(0.8, new JSONObject(prefs.getString(TouchLayoutFile.EDITOR_KEY, "{}")).getDouble("settings_scale"), 1e-6);
    assertTrue(TouchLayoutFile.ownSaved(prefs, TouchControls.defaultLayout()));
    // saved by RetroTouch's own writer: RetroTouch reads it
    assertEquals(TouchControls.LAYOUT_ID, com.ast.retrotouch.RetroTouchLayout.fromJson(TouchLayoutFile.saved(prefs)).getGameId());
  }

  @Test public void aFileWithoutTheEditorLeavesRetroTouchItsDefaults() {
    prefs.edit().putString(TouchLayoutFile.EDITOR_KEY, "{\"format\":1}").commit();
    String file = "{\"" + TouchLayoutFile.MARK + "\":1,\"layout\":" + defaults() + "}";
    TouchLayoutFile.apply(prefs, TouchLayoutFile.parse(file.getBytes(StandardCharsets.UTF_8)), 1920, 1080, 2.0f);
    assertNull(prefs.getString(TouchLayoutFile.EDITOR_KEY, null));
  }

  @Test public void theDefaultsStayWhereTheyAre() {
    // nothing of the app's own layout is off a 16:9 or a 20:9 screen
    assertEquals(0, TouchLayoutFile.fit(defaults(), 1920, 1080, 2.0f).moved);
    assertEquals(0, TouchLayoutFile.fit(defaults(), 2400, 1080, 2.75f).moved);
  }

  @Test public void aControlOffTheScreenComesBackOn() throws JSONException {
    // a button with its centre on the left edge (a layout of another screen, or a damaged one)
    TouchLayoutFile.Fitted f = TouchLayoutFile.fit(with("Z", 0.0, 1.0), 1920, 1080, 2.0f);
    assertEquals(1, f.moved);
    JSONObject b = first(f.layout, "Z");
    double d = Math.min(b.getDouble("width") * 1920, b.getDouble("height") * 1080);
    assertTrue("x " + b.getDouble("x"), b.getDouble("x") * 1920 >= d * 0.5 - 0.01);
    assertTrue("y " + b.getDouble("y"), (1 - b.getDouble("y")) * 1080 >= d * 0.5 - 0.01);
    // RetroTouch takes it
    assertEquals(TouchControls.LAYOUT_ID, com.ast.retrotouch.RetroTouchLayout.fromJson(f.layout).getGameId());
  }

  @Test public void aPlaceAtTheEdgeOfAWideScreenFitsANarrowOne() throws JSONException {
    // Z pushed into the right edge on a 20:9 phone, its visible circle touching it
    JSONObject b = first(defaults(), "Z");
    double d = Math.min(b.getDouble("width") * 2400, b.getDouble("height") * 1080);
    double x = 1 - (d * 0.5 + 2.75 * 1.25) / 2400;
    String layout = with("Z", x, 0.5);
    assertEquals(0, TouchLayoutFile.fit(layout, 2400, 1080, 2.75f).moved);
    // on a 4:3 tablet the same parts make a circle wider than the gap to the edge: moved in
    TouchLayoutFile.Fitted f = TouchLayoutFile.fit(layout, 1440, 1080, 2.0f);
    JSONObject n = first(f.layout, "Z");
    double dn = Math.min(n.getDouble("width") * 1440, n.getDouble("height") * 1080);
    assertTrue((1 - n.getDouble("x")) * 1440 >= dn * 0.5 - 0.01);
  }

  /** The default layout with the sticks the editor adds (RetroTouch writes them as the move stick with an action). */
  private static String withSticks(String... actions) throws JSONException {
    JSONObject root = new JSONObject(defaults());
    JSONArray c = root.getJSONArray("controls");
    JSONObject move = null;
    for (int i = 0; i < c.length(); i++) if (c.getJSONObject(i).getString("id").equals("stick")) move = c.getJSONObject(i);
    assertNotNull(move);
    for (int k = 0; k < actions.length; k++) {
      JSONObject s = new JSONObject(move.toString());
      s.put("id", "button_" + (20 + k)).put("action", actions[k]).put("label", CoreTouch.label(actions[k]))
          .put("x", 0.40 + 0.12 * k).put("y", 0.45);
      c.put(s);
    }
    return root.toString();
  }

  private static int sticks(com.ast.retrotouch.RetroTouchLayout layout, String action) {
    int n = 0;
    for (com.ast.retrotouch.RetroTouchControl k : layout.getControls()) {
      if (k.getType() == com.ast.retrotouch.RetroTouchControl.Type.MOVE_STICK
          && (action == null ? k.getActionId() == null || k.getActionId().isEmpty() : action.equals(k.getActionId()))) n++;
    }
    return n;
  }

  /** RetroTouch 1.0.0-beta.12 (16.11.2026): the move stick, the C stick, the aim and the knife stick in one layout, out
   *  with the exported saves and back, and RetroTouch shows all four (CoreTouch.MAX_STICKS). */
  @Test public void fourSticksOutAndBack() throws JSONException {
    String own = withSticks(CoreTouch.C_STICK, CoreTouch.AIM_STICK, CoreTouch.KNIFE_STICK);
    com.ast.retrotouch.RetroTouchLayout parsed = com.ast.retrotouch.RetroTouchLayout.fromJson(own);
    assertEquals(1, sticks(parsed, CoreTouch.AIM_STICK));
    prefs.edit().putString(TouchLayoutFile.KEY, own).commit();
    byte[] file = TouchLayoutFile.export(prefs, TouchControls.defaultLayout(), 1920, 1080, "1.0.0");
    assertNotNull(file);
    prefs.edit().clear().commit();
    assertEquals(0, TouchLayoutFile.apply(prefs, TouchLayoutFile.parse(file), 1920, 1080, 2.0f));
    assertTrue(TouchLayoutFile.same(TouchLayoutFile.saved(prefs), own));
    com.ast.retrotouch.RetroTouchView view = shown();
    assertEquals(CoreTouch.MAX_STICKS, view.getMaxSticks());
    com.ast.retrotouch.RetroTouchLayout layout = view.getCurrentLayout();
    for (String action : new String[] {null, CoreTouch.C_STICK, CoreTouch.AIM_STICK, CoreTouch.KNIFE_STICK}) {
      assertEquals("one stick " + action, 1, sticks(layout, action));
    }
  }

  /** A layout saved before beta.12 (RetroTouch's reader and writer are the same, format 2; there were at most two sticks
   *  then: the move stick and one added one) loads as before. */
  @Test public void anOldLayoutStillLoads() throws JSONException {
    String old = withSticks(CoreTouch.AIM_STICK);
    assertEquals(2, new JSONObject(old).getInt("format"));
    prefs.edit().putString(TouchLayoutFile.KEY, old).commit();
    com.ast.retrotouch.RetroTouchLayout layout = shown().getCurrentLayout();
    assertEquals(1, sticks(layout, null));
    assertEquals(1, sticks(layout, CoreTouch.AIM_STICK));
    assertEquals(TouchControls.LAYOUT_ID, layout.getGameId());
    // a layout file exported by 0.99.18 (the same JSON in the file): read and taken
    String file = "{\"" + TouchLayoutFile.MARK + "\":1,\"version\":\"0.99.18-rc\",\"layout\":" + withSticks(CoreTouch.C_STICK) + "}";
    prefs.edit().clear().commit();
    assertEquals(0, TouchLayoutFile.apply(prefs, TouchLayoutFile.parse(file.getBytes(StandardCharsets.UTF_8)), 1920, 1080, 2.0f));
    assertEquals(1, sticks(shown().getCurrentLayout(), CoreTouch.C_STICK));
  }

  /** The touch controls as the game shows them (Force), reading the saved layout. */
  private static com.ast.retrotouch.RetroTouchView shown() {
    TouchControls t = new TouchControls(RuntimeEnvironment.getApplication(), new CoreInput(), TouchShowRule.FORCE,
        new TouchControls.Host() {
          @Override public void select(boolean down, boolean finger) {}
          @Override public void editing(boolean on) {}
          @Override public void send() {}
          @Override public void r3(boolean down) {}
          @Override public void zoom(int direction, boolean down) {}
        });
    return (com.ast.retrotouch.RetroTouchView) t.view();
  }

  @Test public void filesThatAreNoUse() {
    assertFalse(TouchLayoutFile.is("{\"format\":2}".getBytes(StandardCharsets.UTF_8)));
    assertFalse(TouchLayoutFile.is(new byte[] {'P', 'K', 3, 4}));
    String[] bad = {
        "{\"" + TouchLayoutFile.MARK + "\":1}",
        "{\"" + TouchLayoutFile.MARK + "\":2,\"layout\":" + defaults() + "}",
        "{\"" + TouchLayoutFile.MARK + "\":1,\"layout\":" + defaults().replace("\"cubeevil4\"", "\"othergame\"") + "}",
        "{\"" + TouchLayoutFile.MARK + "\":1,\"layout\":{\"format\":2,\"game\":\"cubeevil4\",\"controls\":[",
    };
    for (String b : bad) {
      try {
        TouchLayoutFile.parse(b.getBytes(StandardCharsets.UTF_8));
        fail(b);
      } catch (IllegalArgumentException expected) {
        // in English, for the report
      }
    }
  }
}
