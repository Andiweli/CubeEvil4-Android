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
