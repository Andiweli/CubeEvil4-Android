package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchLayout;
import java.nio.charset.StandardCharsets;
import org.json.JSONArray;
import org.json.JSONObject;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * TouchFrame (0.99.17): a place of the safe area (the window without the camera's strip, RetroTouch's view up to
 * 0.99.16) is the same spot on the screen in the whole window.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class TouchFrameTest {
  // the Moto G22 in landscape: 1600 x 720, the camera's strip 95 pixels
  private static final int W = 1600, H = 720, S = 95;

  private static RetroTouchControl find(RetroTouchLayout layout, String id) {
    for (RetroTouchControl c : layout.getControls()) if (c.getId().equals(id)) return c;
    throw new AssertionError("no " + id);
  }

  /** The spot on the screen of a control before (safe area on the camera's other side) and after. */
  @Test public void sameSpotOnTheScreenOnEitherSide() throws Exception {
    final RetroTouchLayout before = TouchControls.defaultLayout();
    for (int[] safe : new int[][] {{S, 0, 0, 0}, {0, 0, S, 0}}) {
      final RetroTouchLayout after = RetroTouchLayout.fromJson(TouchFrame.toWindow(before.toJson(), W, H, safe));
      final int safeWidth = W - S;
      for (RetroTouchControl b : before.getControls()) {
        final RetroTouchControl a = find(after, b.getId());
        // the centre and the width in pixels from the screen's left edge
        assertEquals(b.getId(), safe[0] + b.getX() * safeWidth, a.getX() * W, 0.01);
        assertEquals(b.getId(), b.getWidth() * safeWidth, a.getWidth() * W, 0.01);
        assertEquals(b.getId(), b.getY(), a.getY(), 1e-6);
        assertEquals(b.getId(), b.getHeight(), a.getHeight(), 1e-6);
        assertEquals(b.getId(), b.getOpacity(), a.getOpacity(), 1e-6);
        assertEquals(b.getId(), b.getType(), a.getType());
        assertEquals(b.getId(), b.getActionId(), a.getActionId());
        // nothing of it in the camera's strip (RetroTouch: a round control's size the smaller of the two; the look
        // zone its whole width)
        final double half = (b.getType() == RetroTouchControl.Type.LOOK_ZONE ? a.getWidth() * W
            : Math.min(a.getWidth() * W, a.getHeight() * H)) / 2;
        assertTrue(b.getId(), a.getX() * W - half >= safe[0] - 0.01);
        assertTrue(b.getId(), a.getX() * W + half <= W - safe[2] + 0.01);
      }
    }
  }

  @Test public void withoutACutoutNothingChanges() throws Exception {
    final String layout = TouchControls.defaultLayout().toJson();
    assertFalse(TouchFrame.moves(W, H, new int[4]));
    assertTrue(TouchFrame.moves(W, H, new int[] {S, 0, 0, 0}));
    assertFalse("not laid out yet", TouchFrame.moves(0, 0, new int[] {S, 0, 0, 0}));
    final RetroTouchLayout same = RetroTouchLayout.fromJson(TouchFrame.toWindow(layout, W, H, new int[4]));
    for (RetroTouchControl b : TouchControls.defaultLayout().getControls()) {
      assertEquals(b.getX(), find(same, b.getId()).getX(), 1e-6);
      assertEquals(b.getWidth(), find(same, b.getId()).getWidth(), 1e-6);
    }
    assertEquals(0.25, TouchFrame.place(0.25, 0, S, 0), 0);
  }

  @Test public void aCutoutAtTheTop() {
    // a phone held the other way: the strip above, the heights change, the widths not
    assertEquals((40 + 0.5 * 640) / 720, TouchFrame.place(0.5, 720, 40, 40), 1e-9);
    assertEquals(0.2 * 640 / 720, TouchFrame.extent(0.2, 720, 40, 40), 1e-9);
  }

  @Test public void theEditorsGearAndBar() throws Exception {
    final String editor = "{\"format\":1,\"settings_x\":0.95,\"settings_y\":0.08,\"settings_scale\":1,"
        + "\"settings_opacity\":0.28,\"toolbar_y\":0.5}";
    final JSONObject left = new JSONObject(TouchFrame.editorToWindow(editor, W, H, new int[] {S, 0, 0, 0}));
    assertEquals((S + 0.95 * (W - S)) / W, left.getDouble("settings_x"), 1e-9);
    assertEquals(0.08, left.getDouble("settings_y"), 1e-9);
    assertEquals(0.5, left.getDouble("toolbar_y"), 1e-9);
    assertEquals(0.28, left.getDouble("settings_opacity"), 1e-9);
    final JSONObject right = new JSONObject(TouchFrame.editorToWindow(editor, W, H, new int[] {0, 0, S, 0}));
    assertEquals(0.95 * (W - S) / W, right.getDouble("settings_x"), 1e-9);
    // an editor state without the gear's place keeps RetroTouch's own default
    assertFalse(new JSONObject(TouchFrame.editorToWindow("{\"format\":1}", W, H, new int[] {S, 0, 0, 0}))
        .has("settings_x"));
  }

  // --- the flow with RetroTouch's preferences ---------------------------------------------------------------------

  private static final int[] LEFT = {S, 0, 0, 0}, RIGHT = {0, 0, S, 0};
  private static final String EDITOR = "{\"format\":1,\"settings_x\":0.9,\"settings_y\":0.1,\"settings_scale\":0.8,"
      + "\"settings_opacity\":0.3,\"toolbar_y\":0.7}";
  private SharedPreferences prefs;

  @Before public void clean() {
    prefs = RuntimeEnvironment.getApplication().getSharedPreferences(TouchLayoutFile.PREFS, Context.MODE_PRIVATE);
    prefs.edit().clear().commit();
  }

  /** The default layout with the control `id` at x, y (a player's own layout of before 0.99.17). */
  private static String own(String id, double x, double y) throws Exception {
    JSONObject root = new JSONObject(TouchControls.defaultLayout().toJson());
    JSONArray c = root.getJSONArray("controls");
    for (int i = 0; i < c.length(); i++) if (c.getJSONObject(i).getString("id").equals(id)) c.getJSONObject(i).put("x", x).put("y", y);
    return root.toString();
  }

  private RetroTouchLayout saved() throws Exception {
    return RetroTouchLayout.fromJson(prefs.getString(TouchLayoutFile.KEY, null));
  }

  /** What RetroTouch saves when its editor closes with nothing changed: the layout as it read it. */
  private void editorClosedUnchanged() throws Exception {
    prefs.edit().putString(TouchLayoutFile.KEY, saved().toJson()).commit();
  }

  /** A control's centre in pixels from the screen's left edge. */
  private static double px(RetroTouchControl c) {
    return c.getX() * W;
  }

  @Test public void anOldLayoutOnEitherSideAndAfterTurning() throws Exception {
    final String old = own("z", 0.40, 0.55);
    prefs.edit().putString(TouchLayoutFile.KEY, old).putString(TouchLayoutFile.EDITOR_KEY, EDITOR).commit();
    final RetroTouchLayout before = RetroTouchLayout.fromJson(old);
    // the camera on the left: every control where it was, the safe area starting after the strip
    assertTrue(TouchFrame.legacyApply(prefs, W, H, LEFT));
    for (RetroTouchControl b : before.getControls()) {
      assertEquals(b.getId(), S + b.getX() * (W - S), px(find(saved(), b.getId())), 0.01);
    }
    assertFalse("nothing new for the same side", TouchFrame.legacyApply(prefs, W, H, LEFT));
    // turned by 180 degrees: the camera on the right, the safe area from the left edge
    assertTrue(TouchFrame.legacyApply(prefs, W, H, RIGHT));
    for (RetroTouchControl b : before.getControls()) {
      assertEquals(b.getId(), b.getX() * (W - S), px(find(saved(), b.getId())), 0.01);
    }
    // and back
    assertTrue(TouchFrame.legacyApply(prefs, W, H, LEFT));
    assertEquals(S + 0.40 * (W - S), px(find(saved(), "z")), 0.01);
    // the old one itself is kept as it was, once
    assertTrue(TouchLayoutFile.same(old, TouchFrame.legacyLayout(prefs)));
    assertFalse("only once", TouchFrame.migrate(prefs));
  }

  @Test public void anOldLayoutBeforeAndAfterASaveInTheEditor() throws Exception {
    prefs.edit().putString(TouchLayoutFile.KEY, own("z", 0.40, 0.55)).commit();
    TouchFrame.legacyApply(prefs, W, H, LEFT);
    final String windowDefault = TouchControls.layout(TouchStyle.SHOULDER_ASPECT, W, H, LEFT).toJson();
    // the editor opened and closed with nothing changed: still the old one, turning still keeps its spots
    editorClosedUnchanged();
    assertEquals("", TouchFrame.afterEditor(prefs, windowDefault));
    assertNotNull(TouchFrame.legacyLayout(prefs));
    assertTrue(TouchFrame.legacyApply(prefs, W, H, RIGHT));
    assertEquals(0.40 * (W - S), px(find(saved(), "z")), 0.01);
    TouchFrame.legacyApply(prefs, W, H, LEFT);
    // the player drags Z into the camera's strip and saves: the whole window's layout from now on
    JSONObject moved = new JSONObject(saved().toJson());
    JSONArray c = moved.getJSONArray("controls");
    for (int i = 0; i < c.length(); i++) if (c.getJSONObject(i).getString("id").equals("z")) c.getJSONObject(i).put("x", 0.03);
    prefs.edit().putString(TouchLayoutFile.KEY, moved.toString()).commit();
    assertTrue(TouchFrame.afterEditor(prefs, windowDefault).contains("the player's own of the whole window now"));
    assertNull(TouchFrame.legacyLayout(prefs));
    // turning changes nothing any more: the fractions of the whole screen stay
    assertFalse(TouchFrame.legacyApply(prefs, W, H, RIGHT));
    assertEquals(0.03, find(saved(), "z").getX(), 1e-6);
  }

  @Test public void theDefaultLayoutFollowsTheCamera() throws Exception {
    final RetroTouchLayout plain = TouchControls.defaultLayout();
    for (int[] safe : new int[][] {LEFT, RIGHT}) {
      final RetroTouchLayout d = TouchControls.layout(TouchStyle.SHOULDER_ASPECT, W, H, safe);
      for (RetroTouchControl b : plain.getControls()) {
        final RetroTouchControl a = find(d, b.getId());
        // the centre where it was in the safe area, the size (RetroTouch: the smaller of the two) the same
        assertEquals(b.getId(), safe[0] + b.getX() * (W - S), px(a), 0.01);
        assertEquals(b.getId(), b.getY(), a.getY(), 1e-6);
        final double sizeBefore = Math.min(b.getWidth() * (W - S), b.getHeight() * H);
        final double sizeAfter = Math.min(a.getWidth() * W, a.getHeight() * H);
        if (b.getType() == RetroTouchControl.Type.LOOK_ZONE) {
          assertEquals(b.getId(), b.getWidth() * (W - S), a.getWidth() * W, 0.01);
        } else {
          assertEquals(b.getId(), sizeBefore, sizeAfter, 0.01);
          // nothing of it in the camera's strip
          assertTrue(b.getId(), px(a) - sizeAfter / 2 >= safe[0] - 0.01);
          assertTrue(b.getId(), px(a) + sizeAfter / 2 <= W - safe[2] + 0.01);
        }
      }
    }
    // without a cutout the default layout as always
    final RetroTouchLayout none = TouchControls.layout(TouchStyle.SHOULDER_ASPECT, W, H, new int[4]);
    for (RetroTouchControl b : plain.getControls()) assertEquals(b.getX(), find(none, b.getId()).getX(), 0f);
  }

  @Test public void theDefaultSavedAsItIsFollowsTheCamera() throws Exception {
    // a player without a layout of their own opens the editor and closes it (or presses RESET): RetroTouch saves the
    // default of this side; it goes again, so after turning the default of the other side shows
    final String left = TouchControls.layout(TouchStyle.SHOULDER_ASPECT, W, H, LEFT).toJson();
    TouchFrame.migrate(prefs);
    prefs.edit().putString(TouchLayoutFile.KEY, left).commit();
    assertTrue(TouchFrame.afterEditor(prefs, left).contains("the default layout"));
    assertNull(prefs.getString(TouchLayoutFile.KEY, null));
    // an own layout of the whole window stays
    prefs.edit().putString(TouchLayoutFile.KEY, own("z", 0.02, 0.5)).commit();
    assertEquals("", TouchFrame.afterEditor(prefs, left));
    assertNotNull(prefs.getString(TouchLayoutFile.KEY, null));
  }

  @Test public void theGear() throws Exception {
    prefs.edit().putString(TouchLayoutFile.KEY, own("z", 0.40, 0.55)).putString(TouchLayoutFile.EDITOR_KEY, EDITOR)
        .commit();
    TouchFrame.legacyApply(prefs, W, H, LEFT);
    final JSONObject left = new JSONObject(prefs.getString(TouchLayoutFile.EDITOR_KEY, null));
    assertEquals((S + 0.9 * (W - S)) / W, left.getDouble("settings_x"), 1e-9);
    TouchFrame.legacyApply(prefs, W, H, RIGHT);
    final JSONObject right = new JSONObject(prefs.getString(TouchLayoutFile.EDITOR_KEY, null));
    assertEquals(0.9 * (W - S) / W, right.getDouble("settings_x"), 1e-9);
    assertEquals(0.8, right.getDouble("settings_scale"), 1e-9);
    // the editor closed with the gear where it was (RetroTouch writes its centre back, the bar's height too)
    final String windowDefault = TouchControls.layout(TouchStyle.SHOULDER_ASPECT, W, H, RIGHT).toJson();
    editorClosedUnchanged();
    prefs.edit().putString(TouchLayoutFile.EDITOR_KEY, right.put("toolbar_y", 0.4).toString()).commit();
    TouchFrame.afterEditor(prefs, windowDefault);
    assertNotNull("kept", TouchFrame.legacyEditor(prefs));
    // the gear dragged: its place of the whole window from now on
    prefs.edit().putString(TouchLayoutFile.EDITOR_KEY, right.put("settings_x", 0.5).toString()).commit();
    assertTrue(TouchFrame.afterEditor(prefs, windowDefault).contains("the gear moved"));
    assertNull(TouchFrame.legacyEditor(prefs));
    // turning again moves the old layout, not the gear
    TouchFrame.legacyApply(prefs, W, H, LEFT);
    assertNotNull("the old layout itself is kept still", TouchFrame.legacyLayout(prefs));
    assertEquals(0.5, new JSONObject(prefs.getString(TouchLayoutFile.EDITOR_KEY, null)).getDouble("settings_x"), 1e-9);
  }

  @Test public void filesOfBothFrames() throws Exception {
    // an old layout kept: the file carries it as it is, with the safe area's size and no frame
    prefs.edit().putString(TouchLayoutFile.KEY, own("z", 0.40, 0.55)).commit();
    TouchFrame.legacyApply(prefs, W, H, LEFT);
    final RetroTouchLayout safeDefaults = TouchControls.defaultLayout();
    final RetroTouchLayout windowDefaults = TouchControls.layout(TouchStyle.SHOULDER_ASPECT, W, H, LEFT);
    byte[] file = TouchLayoutFile.export(prefs, safeDefaults, windowDefaults, W, H, LEFT, "0.99.17-rc");
    JSONObject root = new JSONObject(new String(file, StandardCharsets.UTF_8));
    assertFalse(root.has("frame"));
    assertEquals(W - S, root.getJSONArray("screen").getInt(0));
    assertTrue(TouchLayoutFile.same(own("z", 0.40, 0.55), root.getJSONObject("layout").toString()));
    // back on a phone with the camera on the right: kept as the old one, written for that side
    prefs.edit().clear().commit();
    TouchLayoutFile.Parsed p = TouchLayoutFile.parse(file);
    assertFalse(p.window);
    TouchLayoutFile.apply(prefs, p, W, H, RIGHT, 2.0f);
    assertNotNull(TouchFrame.legacyLayout(prefs));
    assertEquals(0.40 * (W - S), px(find(saved(), "z")), 0.01);
    assertTrue(TouchLayoutFile.sameAsSaved(prefs, p));
    // the whole window's layout (after a change in the editor): "frame": "window", taken as it is
    prefs.edit().clear().commit();
    TouchFrame.migrate(prefs);
    prefs.edit().putString(TouchLayoutFile.KEY, own("z", 0.03, 0.5)).commit();
    file = TouchLayoutFile.export(prefs, safeDefaults, windowDefaults, W, H, LEFT, "0.99.17-rc");
    root = new JSONObject(new String(file, StandardCharsets.UTF_8));
    assertEquals("window", root.getString("frame"));
    assertEquals(W, root.getJSONArray("screen").getInt(0));
    prefs.edit().clear().commit();
    p = TouchLayoutFile.parse(file);
    assertTrue(p.window);
    TouchLayoutFile.apply(prefs, p, W, H, RIGHT, 2.0f);
    assertNull(TouchFrame.legacyLayout(prefs));
    assertEquals(0.03, find(saved(), "z").getX(), 0.01);
  }

  // --- RetroTouch 1.0.0-beta.11: the editor's bar and the gear's default place keep clear of the camera -------------

  private static Object field(Object o, String name) throws Exception {
    java.lang.reflect.Field f = o.getClass().getDeclaredField(name);
    f.setAccessible(true);
    return f.get(o);
  }

  private static int[] insets(com.ast.retrotouch.RetroTouchView v) throws Exception {
    return new int[] {(Integer) field(v, "safeInsetLeft"), (Integer) field(v, "safeInsetTop"),
        (Integer) field(v, "safeInsetRight"), (Integer) field(v, "safeInsetBottom")};
  }

  private static android.graphics.RectF gear(com.ast.retrotouch.RetroTouchView v) throws Exception {
    java.lang.reflect.Method m = v.getClass().getDeclaredMethod("gearRect");
    m.setAccessible(true);
    return (android.graphics.RectF) m.invoke(v);
  }

  @Test public void retroTouchGetsTheInsetsOnEitherSide() throws Exception {
    TouchControls t = new TouchControls(RuntimeEnvironment.getApplication(), new CoreInput(), TouchShowRule.FORCE,
        new TouchControls.Host() {
          @Override public void select(boolean down, boolean finger) {}
          @Override public void editing(boolean on) {}
          @Override public void send() {}
          @Override public void r3(boolean down) {}
          @Override public void zoom(int direction, boolean down) {}
        });
    com.ast.retrotouch.RetroTouchView view = (com.ast.retrotouch.RetroTouchView) t.view();
    view.layout(0, 0, W, H);
    assertArrayEquals(new int[4], insets(view));
    final float widthWithout = gear(view).right;
    // the camera on the left, then turned by 180 degrees: on the right
    t.setSafeInsets(LEFT);
    assertArrayEquals(LEFT, insets(view));
    assertArrayEquals(LEFT, t.safeInsets());
    t.setSafeInsets(RIGHT);
    assertArrayEquals(RIGHT, insets(view));
    // the gear's default place (top right) keeps clear of a camera on the right ...
    assertEquals(widthWithout - S, gear(view).right, 0.5f);
    assertTrue(gear(view).right <= W - S);
    // ... a place the player gave it stays (a fraction of the whole width, TouchFrame converts it)
    java.lang.reflect.Field x = view.getClass().getDeclaredField("settingsX");
    x.setAccessible(true);
    x.setFloat(view, 0.98f);
    assertEquals(0.98f * W, gear(view).centerX(), 0.5f);
    assertTrue("into the strip", gear(view).right > W - S);
    t.setSafeInsets(new int[4]);
    assertArrayEquals(new int[4], insets(view));
  }
}
