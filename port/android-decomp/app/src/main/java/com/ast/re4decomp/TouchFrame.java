package com.ast.re4decomp;

import android.content.SharedPreferences;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Touch places between two frames (0.99.17, the camera's cutout; decision of the project owner, 15.11.2026: "so
 * umbauen wie von dir vorgeschlagen"). RetroTouch keeps a control's centre, width and height as fractions of its view.
 * Up to 0.99.16 that view was the window without the camera's strip (the safe area, "safe" here); now it is the whole
 * window ("window"). A place of the safe area becomes the same spot on the screen in the whole window with toWindow:
 * x' = (left + x * safeWidth) / width, w' = w * safeWidth / width, the same for y and the height with the top and bottom
 * insets. Without insets nothing changes.
 *
 * <p>The safe area moves with the camera when the screen turns by 180 degrees, so one layout of the whole window can be
 * the old spots for one side only. Therefore:
 * <ul>
 * <li>The app's default layout is laid into the safe area of the side the camera is on now (TouchControls), at each
 *     change: without a layout of their own a player sees the controls where they were, never under the camera.</li>
 * <li>A layout saved before 0.99.17 (and its editor state: the gear's place) is kept as it was ("_safe" keys, once,
 *     migrate) and written into RetroTouch's preferences for the side the camera is on (legacyApply), again at each
 *     change of side. When the player changes it in the editor (afterEditor: RetroTouch's saved layout differs from
 *     the one written), the old one goes: the layout is the player's own of the whole window from then on, a control
 *     may lie in the camera's strip, and turning keeps its fractions of the screen.</li>
 * <li>A layout saved in the editor that is the default for this side (nothing moved, or RESET) is taken away again:
 *     the default follows the camera.</li>
 * </ul>
 * The keys live in RetroTouch's preferences (TouchLayoutFile.PREFS) and start with "layout_", so the settings of a
 * player's ZIP leave them out as they leave out the layout (PlayerBackup.outside); the touch layout file carries the
 * frame of its layout (TouchLayoutFile).
 */
final class TouchFrame {
  private TouchFrame() {}

  /** Set once the old layout was taken over (migrate ran). */
  static final String MARK = TouchLayoutFile.KEY + "_frame";
  /** The layout and the editor state of the safe area (saved before 0.99.17 or imported from such a file). */
  static final String SAFE = TouchLayoutFile.KEY + "_safe", SAFE_EDITOR = SAFE + "_editor";
  /** What legacyApply last wrote into RetroTouch's keys (afterEditor: has the player changed it?). */
  static final String WROTE = TouchLayoutFile.KEY + "_wrote", WROTE_EDITOR = WROTE + "_editor";
  /** The gear's place and look in the editor state; the bar's height does not move with a left or right strip. */
  private static final String[] GEAR = {"settings_x", "settings_y", "settings_scale", "settings_opacity"};

  // --- the arithmetic ---------------------------------------------------------------------------------------------

  /** One coordinate: a fraction of the safe area's extent, as a fraction of the whole extent. */
  static double place(double fraction, int whole, int before, int after) {
    final int safe = whole - before - after;
    if (whole <= 0 || safe <= 0 || (before == 0 && after == 0)) return fraction;
    return (before + fraction * safe) / whole;
  }

  /** One extent (a width or a height) of the safe area as a fraction of the whole extent. */
  static double extent(double fraction, int whole, int before, int after) {
    final int safe = whole - before - after;
    if (whole <= 0 || safe <= 0 || (before == 0 && after == 0)) return fraction;
    return fraction * safe / whole;
  }

  /** True when the insets {left, top, right, bottom} change anything in a window of width x height. */
  static boolean moves(int width, int height, int[] safe) {
    return width > 0 && height > 0 && (safe[0] > 0 || safe[1] > 0 || safe[2] > 0 || safe[3] > 0)
        && width - safe[0] - safe[2] > 0 && height - safe[1] - safe[3] > 0;
  }

  /** RetroTouch's layout (RetroTouchLayout.toJson) of the safe area in the whole window of width x height. */
  static String toWindow(String layout, int width, int height, int[] safe) throws JSONException {
    final JSONObject root = new JSONObject(layout);
    final JSONArray controls = root.getJSONArray("controls");
    for (int i = 0; i < controls.length(); i++) {
      final JSONObject c = controls.getJSONObject(i);
      c.put("x", place(c.getDouble("x"), width, safe[0], safe[2]));
      c.put("y", place(c.getDouble("y"), height, safe[1], safe[3]));
      c.put("width", extent(c.getDouble("width"), width, safe[0], safe[2]));
      c.put("height", extent(c.getDouble("height"), height, safe[1], safe[3]));
    }
    return root.toString();
  }

  /** RetroTouch's editor state (the gear's centre, the bar's height on the screen) the same way. */
  static String editorToWindow(String editor, int width, int height, int[] safe) throws JSONException {
    final JSONObject e = new JSONObject(editor);
    if (e.has("settings_x")) e.put("settings_x", place(e.getDouble("settings_x"), width, safe[0], safe[2]));
    if (e.has("settings_y")) e.put("settings_y", place(e.getDouble("settings_y"), height, safe[1], safe[3]));
    if (e.has("toolbar_y")) e.put("toolbar_y", place(e.getDouble("toolbar_y"), height, safe[1], safe[3]));
    return e.toString();
  }

  // --- the old layout ---------------------------------------------------------------------------------------------

  /**
   * Once (MARK): a layout and editor state saved before 0.99.17 become the safe area's (SAFE, SAFE_EDITOR); true when
   * there was one.
   */
  static boolean migrate(SharedPreferences p) {
    if (p.contains(MARK)) return false;
    final String layout = p.getString(TouchLayoutFile.KEY, null), editor = p.getString(TouchLayoutFile.EDITOR_KEY, null);
    final SharedPreferences.Editor e = p.edit().putInt(MARK, 1);
    if (layout != null) e.putString(SAFE, layout);
    if (editor != null) e.putString(SAFE_EDITOR, editor);
    e.commit();
    return layout != null || editor != null;
  }

  /**
   * The safe area's layout kept from before 0.99.17; null when there is none (any more). Takes it over first if that
   * has not happened yet (an export before the touch controls were laid out).
   */
  static String legacyLayout(SharedPreferences p) {
    migrate(p);
    return p.getString(SAFE, null);
  }

  /** Its editor state; null when there is none. */
  static String legacyEditor(SharedPreferences p) {
    migrate(p);
    return p.getString(SAFE_EDITOR, null);
  }

  /**
   * The kept layout and editor state into RetroTouch's keys for a window of width x height with the camera's safe
   * insets; true when they changed (RetroTouch must read them again). Nothing without a size or a kept one.
   */
  static boolean legacyApply(SharedPreferences p, int width, int height, int[] safe) {
    if (width <= 0 || height <= 0) return false;
    final String layout = legacyLayout(p), editor = legacyEditor(p);
    if (layout == null && editor == null) return false;
    final SharedPreferences.Editor e = p.edit();
    boolean changed = false;
    try {
      if (layout != null) {
        final String w = toWindow(layout, width, height, safe);
        if (!w.equals(p.getString(TouchLayoutFile.KEY, null))) {
          e.putString(TouchLayoutFile.KEY, w);
          changed = true;
        }
        e.putString(WROTE, w);
      }
      if (editor != null) {
        final String w = editorToWindow(editor, width, height, safe);
        if (!w.equals(p.getString(TouchLayoutFile.EDITOR_KEY, null))) {
          e.putString(TouchLayoutFile.EDITOR_KEY, w);
          changed = true;
        }
        e.putString(WROTE_EDITOR, w);
      }
    } catch (JSONException damaged) {
      // RetroTouch ignores a damaged layout too (its defaults); the kept one goes
      e.remove(SAFE).remove(SAFE_EDITOR).remove(WROTE).remove(WROTE_EDITOR);
    }
    e.commit();
    return changed;
  }

  /** A layout and editor state of the safe area (an imported file of before 0.99.17) kept as the old one is. */
  static void setLegacy(SharedPreferences p, String layout, String editor) {
    final SharedPreferences.Editor e = p.edit().putInt(MARK, 1).putString(SAFE, layout).remove(WROTE)
        .remove(WROTE_EDITOR);
    if (editor != null) e.putString(SAFE_EDITOR, editor);
    else e.remove(SAFE_EDITOR);
    e.commit();
  }

  /** No kept layout any more (an imported layout of the whole window). */
  static void clearLegacy(SharedPreferences p) {
    p.edit().putInt(MARK, 1).remove(SAFE).remove(SAFE_EDITOR).remove(WROTE).remove(WROTE_EDITOR).commit();
  }

  /**
   * After the editor closed (RetroTouch saved its layout and editor state): a kept layout the player changed goes, the
   * player's layout is the whole window's from now on; a saved layout that is this side's default (`windowDefault`)
   * goes, so the default follows the camera again; a kept editor state whose gear the player moved goes. What happened,
   * for the log ("" for nothing).
   */
  static String afterEditor(SharedPreferences p, String windowDefault) {
    final SharedPreferences.Editor e = p.edit();
    final StringBuilder what = new StringBuilder();
    final String saved = p.getString(TouchLayoutFile.KEY, null);
    boolean kept = p.contains(SAFE);
    if (kept && saved != null && !TouchLayoutFile.same(saved, p.getString(WROTE, "{}"))) {
      e.remove(SAFE).remove(WROTE);
      kept = false;
      what.append("the layout of before 0.99.17 changed: the player's own of the whole window now");
    }
    if (!kept && saved != null && windowDefault != null && TouchLayoutFile.same(saved, windowDefault)) {
      e.remove(TouchLayoutFile.KEY);
      if (what.length() > 0) what.append("; ");
      what.append("the default layout (it follows the camera's side)");
    }
    final String editor = p.getString(TouchLayoutFile.EDITOR_KEY, null);
    if (p.contains(SAFE_EDITOR) && editor != null && !sameGear(editor, p.getString(WROTE_EDITOR, "{}"))) {
      e.remove(SAFE_EDITOR).remove(WROTE_EDITOR);
      if (what.length() > 0) what.append("; ");
      what.append("the gear moved: its place of the whole window now");
    }
    e.commit();
    return what.toString();
  }

  /** The gear at the same place with the same size and opacity in two editor states. */
  static boolean sameGear(String a, String b) {
    try {
      final JSONObject x = new JSONObject(a), y = new JSONObject(b);
      for (String k : GEAR) {
        if (x.has(k) != y.has(k)) return false;
        if (x.has(k) && Math.abs(x.getDouble(k) - y.getDouble(k)) > 1e-3) return false;
      }
      return true;
    } catch (JSONException e) {
      return false;
    }
  }
}
