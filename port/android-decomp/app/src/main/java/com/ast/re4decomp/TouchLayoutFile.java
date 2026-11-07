package com.ast.re4decomp;

import android.content.SharedPreferences;
import com.ast.retrotouch.RetroTouchLayout;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * The player's own touch layout (RetroTouch's editor: the gear, then save) as a file of its own next to the exported
 * saves, and back with their import (wish of the project owner, 07.11.2026).
 *
 * <p>RetroTouch (1.0.0-beta.9) keeps it in the app's SharedPreferences "retrotouch_layouts": "layout_cubeevil4"
 * (RetroTouchLayout.toJson: each control's centre x, y and its width, height as parts of the screen's width and
 * height, and its opacity) and "layout_cubeevil4_editor" (the gear's place, size and opacity, the editor bar's
 * height). Nothing in it is in pixels, so a layout fits every screen: on another screen shape the controls keep their
 * places relative to the edges, and a round one (its diameter is the smaller of width x screen width and height x
 * screen height) can come out a little larger or smaller, and nearer to an edge. The import moves every control whose
 * visible shape would cross an edge of this screen back inside, as RetroTouch's editor places a dragged control
 * (RetroTouchView.placeControl / visibleRect / buttonRect); the gear keeps itself on the screen anyway (gearRect).
 */
final class TouchLayoutFile {
  /** The file's name in the export folder. */
  static final String NAME = "CubeEvil4-touch-layout.json";
  /** RetroTouchView.PREFS_NAME and its keys for the layout id TouchControls.LAYOUT_ID. */
  static final String PREFS = "retrotouch_layouts";
  static final String KEY = "layout_" + TouchControls.LAYOUT_ID;
  static final String EDITOR_KEY = KEY + "_editor";
  /** The first key of the file: what it is (and its format). */
  static final String MARK = "cubeevil4_touch_layout";
  static final int FORMAT = 1;
  static final int MAX = 1 << 20;
  // RetroTouchView 1.0.0-beta.9: the border's width, the labels drawn as rectangles and as shoulder buttons
  private static final float BORDER_DP = 2.5f;
  private static final Set<String> RECTANGLE = new HashSet<>(Arrays.asList("START", "SELECT", "MENU"));
  private static final Set<String> SHOULDER = new HashSet<>(Arrays.asList(
      "L", "R", "ZL", "ZR", "L1", "R1", "L2", "R2", "LB", "RB", "LT", "RT"));
  /** More than this many pixels over an edge counts as outside (rounding, the half border). */
  private static final float SLACK_PX = 2.0f;

  private TouchLayoutFile() {}

  /** What a file holds: RetroTouch's layout JSON and the editor's state (null when the file has none). */
  static final class Parsed {
    final String layout, editor;

    Parsed(String layout, String editor) {
      this.layout = layout;
      this.editor = editor;
    }
  }

  /** A layout and how many of its controls were moved onto the screen. */
  static final class Fitted {
    final String layout;
    final int moved;

    Fitted(String layout, int moved) {
      this.layout = layout;
      this.moved = moved;
    }
  }

  /** A file of this kind: JSON whose first key is MARK (a byte check, for SaveImport). */
  static boolean is(byte[] data) {
    if (data == null || data.length < 2 || data.length > MAX) return false;
    String head = new String(data, 0, Math.min(data.length, 256), StandardCharsets.UTF_8);
    int i = 0;
    while (i < head.length() && (Character.isWhitespace(head.charAt(i)) || head.charAt(i) == '\uFEFF')) i++;
    return i < head.length() && head.charAt(i) == '{' && head.indexOf('"' + MARK + '"') > i;
  }

  /** The layout RetroTouch has saved; null when none. */
  static String saved(SharedPreferences prefs) {
    return prefs.getString(KEY, null);
  }

  /** The player has a layout of their own: one saved that RetroTouch can read and that differs from the defaults. */
  static boolean ownSaved(SharedPreferences prefs, RetroTouchLayout defaults) {
    String s = saved(prefs);
    return s != null && readable(s) && !same(s, defaults.toJson());
  }

  /** The file for the export; null when the player has no layout of their own. */
  static byte[] export(SharedPreferences prefs, RetroTouchLayout defaults, int width, int height, String version) {
    if (!ownSaved(prefs, defaults)) return null;
    try {
      JSONObject root = new JSONObject();
      root.put(MARK, FORMAT);
      root.put("app", version == null ? "" : version);
      root.put("screen", new JSONArray().put(width).put(height));
      root.put("layout", new JSONObject(saved(prefs)));
      String editor = prefs.getString(EDITOR_KEY, null);
      if (editor != null) {
        try {
          root.put("editor", new JSONObject(editor));
        } catch (JSONException damaged) {
          // RetroTouch ignores a damaged editor state too (its defaults)
        }
      }
      return root.toString(2).getBytes(StandardCharsets.UTF_8);
    } catch (JSONException e) {
      return null;
    }
  }

  /** The layout of a file; IllegalArgumentException (in English, for the log and the report) when it is no use. */
  static Parsed parse(byte[] data) {
    if (!is(data)) throw new IllegalArgumentException("not a touch layout of CubeEvil4");
    try {
      JSONObject root = new JSONObject(new String(data, StandardCharsets.UTF_8));
      int format = root.optInt(MARK, 0);
      if (format < 1 || format > FORMAT) throw new IllegalArgumentException("made by a newer version of the app");
      String layout = root.getJSONObject("layout").toString();
      RetroTouchLayout l = RetroTouchLayout.fromJson(layout);
      if (!TouchControls.LAYOUT_ID.equals(l.getGameId())) throw new IllegalArgumentException("a layout of another game");
      JSONObject editor = root.optJSONObject("editor");
      return new Parsed(layout, editor == null ? null : editor.toString());
    } catch (JSONException e) {
      throw new IllegalArgumentException("the file is damaged (" + e.getMessage() + ")");
    }
  }

  /** The same controls at the same places and sizes (labels and the order left out). */
  static boolean same(String a, String b) {
    try {
      Map<String, JSONObject> x = controls(a), y = controls(b);
      if (!x.keySet().equals(y.keySet())) return false;
      for (Map.Entry<String, JSONObject> e : x.entrySet()) {
        JSONObject p = e.getValue(), q = y.get(e.getKey());
        if (!p.optString("type").equals(q.optString("type")) || !p.optString("action").equals(q.optString("action"))) return false;
        for (String k : new String[] {"x", "y", "width", "height", "opacity"}) {
          if (Math.abs(p.optDouble(k, 0) - q.optDouble(k, 0)) > 1e-4) return false;
        }
      }
      return true;
    } catch (JSONException e) {
      return false;
    }
  }

  private static Map<String, JSONObject> controls(String layout) throws JSONException {
    JSONArray items = new JSONObject(layout).getJSONArray("controls");
    Map<String, JSONObject> out = new HashMap<>();
    for (int i = 0; i < items.length(); i++) out.put(items.getJSONObject(i).getString("id"), items.getJSONObject(i));
    return out;
  }

  private static boolean readable(String layout) {
    try {
      return TouchControls.LAYOUT_ID.equals(RetroTouchLayout.fromJson(layout).getGameId());
    } catch (JSONException | IllegalArgumentException e) {
      return false;
    }
  }

  /**
   * The layout with every control whose visible shape (border included) would reach more than SLACK_PX over an edge
   * of a width x height screen moved inside, the way RetroTouch's editor keeps a dragged control on the screen.
   */
  static Fitted fit(String layout, int width, int height, float density) {
    if (width <= 0 || height <= 0) return new Fitted(layout, 0);
    try {
      JSONObject root = new JSONObject(layout);
      JSONArray items = root.getJSONArray("controls");
      float border = BORDER_DP * density * 0.5f;
      int moved = 0;
      for (int i = 0; i < items.length(); i++) {
        JSONObject c = items.getJSONObject(i);
        float w = (float) c.getDouble("width") * width, h = (float) c.getDouble("height") * height;
        float halfW, halfH, edge = border;
        String type = c.optString("type").toUpperCase(Locale.ROOT);
        if (type.equals("LOOK_ZONE")) {
          // an area up to the screen's edge (the app's own: the right half): its faint border may lie on the edge
          halfW = w * 0.5f;
          halfH = h * 0.5f;
          edge = 0;
        } else {
          float d = Math.min(w, h);
          String label = type.equals("BUTTON") ? label(c.optString("label")) : "";
          if (RECTANGLE.contains(label)) {
            halfW = d * 0.60f;
            halfH = halfW * 0.5f;
          } else if (SHOULDER.contains(label)) {
            halfW = d * 0.75f;
            halfH = halfW * 0.5f;
          } else {
            halfW = halfH = d * 0.5f;
          }
        }
        float x = (float) c.getDouble("x"), y = (float) c.getDouble("y");
        float nx = inside(x, (halfW + edge) / width), ny = inside(y, (halfH + edge) / height);
        if (Math.abs(nx - x) * width > SLACK_PX || Math.abs(ny - y) * height > SLACK_PX) {
          c.put("x", nx);
          c.put("y", ny);
          moved++;
        }
      }
      return new Fitted(root.toString(), moved);
    } catch (JSONException e) {
      return new Fitted(layout, 0);
    }
  }

  /** RetroTouchControl.setPosition: the centre between half and 1 - half (the middle when the control is larger). */
  private static float inside(float v, float half) {
    return Math.max(Math.min(0.5f, half), Math.min(Math.max(0.5f, 1.0f - half), v));
  }

  /** RetroTouchView.normalizeLabel, as far as the shapes need it: capitals, no spaces around. */
  private static String label(String s) {
    return s == null ? "" : s.toUpperCase(Locale.ROOT).trim().replaceAll("\\s+", " ");
  }

  /**
   * Saves the file's layout as RetroTouch's own (fitted to a width x height screen, written by RetroTouchLayout.toJson)
   * and its editor state (or none); RetroTouch reads both when it next shows its layout. The controls moved.
   */
  static int apply(SharedPreferences prefs, Parsed file, int width, int height, float density) {
    Fitted f = fit(file.layout, width, height, density);
    String layout;
    try {
      layout = RetroTouchLayout.fromJson(f.layout).toJson();
    } catch (JSONException e) {
      throw new IllegalArgumentException("the file is damaged (" + e.getMessage() + ")");
    }
    SharedPreferences.Editor e = prefs.edit().putString(KEY, layout);
    if (file.editor != null) e.putString(EDITOR_KEY, file.editor);
    else e.remove(EDITOR_KEY);
    e.commit();
    return f.moved;
  }
}
