package com.ast.re4decomp;

import android.content.SharedPreferences;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TimeZone;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * "Export Player Data (.zip)" and its import (wish of the project owner, 14.11.2026, 0.99.15): one ZIP with the
 * player's own data, which "Import Player Data" brings back.
 *
 * <p>The ZIP (format 1):
 * <pre>
 *   CubeEvil4-backup.json              what it is (MARK), the app's version and code, the device, the time, its contents
 *   memory-card/08-G4BE-bh4_data00.gci each file of the memory card as .gci (Dolphin's names, GameCube byte order)
 *   CubeEvil4-touch-layout.json        RetroTouch's layout (TouchLayoutFile), when the player saved one of their own
 *   instant/slot-0.ce4save             the instant save (runtime/instant_store.cpp), when there is one
 *   CubeEvil4-settings.json            the app's settings (SETTINGS_MARK): controls with the touch switches, game,
 *                                      graphics and sound, the error log's switch, RetroTouch's button style
 * </pre>
 * Left out: the discs and HD texture packs (game data, large), the shader cache, the logs, and the app's bookkeeping
 * of the device (the Vulkan start guard, the learned sound buffer). An app of 0.99.14 or older imports the .gci files
 * and the touch layout of such a ZIP and leaves the rest out (SaveImport).
 *
 * <p>The settings come back key by key: a value of another type than this app's is left out, and so is a setting this
 * app does not have when the ZIP comes from a newer version (an older app never reads a setting with the wrong type);
 * a setting of this app the ZIP does not hold goes back to its default (the ZIP's state is the one restored). The
 * performance settings (performance()) only come back on the same kind of device: they follow its graphics chip, its
 * benchmark and its screen, so on another one this device's stay; sound, picture style and displays come along.
 */
final class PlayerBackup {
  static final String MARK = "cubeevil4_backup", SETTINGS_MARK = "cubeevil4_settings";
  static final int FORMAT = 1;
  static final String MANIFEST = "CubeEvil4-backup.json", SETTINGS = "CubeEvil4-settings.json";
  static final String CARD_FOLDER = "memory-card/", INSTANT = "instant/slot-0.ce4save";
  /** The instant save's file (runtime/instant_store.cpp): a header of 32 bytes, 0x12000 of payload. */
  static final int INSTANT_HEADER = 32, INSTANT_PAYLOAD = 0x12000, INSTANT_BYTES = INSTANT_HEADER + INSTANT_PAYLOAD;
  private static final byte[] INSTANT_MAGIC = {'C', 'E', '4', 'I', 'N', 'S', 'T', 0};
  /** The settings in a ZIP (SharedPreferences names): controls (the app's touch switches too), game, graphics and
   *  sound, the error log's switch, and RetroTouch's own (its button style); RetroTouch's layout has a file of its own
   *  (TouchLayoutFile, fitted to the screen at the import). */
  static final String GRAPHICS = CoreActivity.GRAPHICS_PREFS, RETROTOUCH = TouchLayoutFile.PREFS;
  static final String[] GROUPS = {CoreActivity.INPUT_PREFS, CoreActivity.GAME_PREFS, GRAPHICS, CoreLog.PREFS, RETROTOUCH};
  private static final int MAX_JSON = 1 << 20;

  private PlayerBackup() {}

  /** Never in the settings file, never replaced by one: the app's bookkeeping of this device (the Vulkan start guard)
   *  and RetroTouch's layouts ("layout_cubeevil4" and its editor's state: TouchLayoutFile has them). */
  static boolean outside(String group, String key) {
    return (GRAPHICS.equals(group) && key.startsWith(VulkanGuard.STARTING))
        || (RETROTOUCH.equals(group) && key.startsWith("layout_"));
  }

  /** The graphics settings of the device's power (the performance profiles' settings, the resolution, the renderer
   *  and the benchmark with its levels, the texture pack's switches: the pack is no part of a ZIP): from a ZIP of
   *  another kind of device this device's stay. */
  private static final Set<String> PERFORMANCE = new HashSet<>(java.util.Arrays.asList("scale", "frameskip", "cpuSkip",
      "mergeDraws", "vertexRing", "vkBandwidth", CoreActivity.RENDERER, CoreMenu.DYN_CAP_30, CoreMenu.DYN_CAP_60,
      TexturePacks.ON, TexturePacks.HALF));
  static {
    PERFORMANCE.addAll(java.util.Arrays.asList(PerfProfile.KEYS));
  }

  static boolean performance(String group, String key) {
    return GRAPHICS.equals(group) && (PERFORMANCE.contains(key) || key.startsWith("perf") || key.startsWith("bench"));
  }

  /** A setting this restore leaves as it is: the bookkeeping, and from another device the performance settings. */
  static boolean kept(String group, String key, boolean sameDevice) {
    return outside(group, key) || (!sameDevice && performance(group, key));
  }

  // --- the ZIP ---------------------------------------------------------------------------------------------------

  /** What goes into a ZIP. */
  static final class Contents {
    final List<String> cardNames = new ArrayList<>();
    final List<byte[]> cardFiles = new ArrayList<>();
    byte[] touch, instant;
    String settings;

    void card(String name, byte[] gci) { cardNames.add(name); cardFiles.add(gci); }
  }

  /** The ZIP of `c` into `out` (the manifest first); `app`, `code`, `device` and `now` go into the manifest. */
  static void write(OutputStream out, Contents c, String app, long code, String device, long now) throws IOException {
    try (ZipOutputStream zip = new ZipOutputStream(out)) {
      entry(zip, MANIFEST, manifest(c, app, code, device, now).getBytes(StandardCharsets.UTF_8));
      for (int i = 0; i < c.cardFiles.size(); i++) entry(zip, CARD_FOLDER + c.cardNames.get(i), c.cardFiles.get(i));
      if (c.touch != null) entry(zip, TouchLayoutFile.NAME, c.touch);
      if (c.instant != null) entry(zip, INSTANT, c.instant);
      if (c.settings != null) entry(zip, SETTINGS, c.settings.getBytes(StandardCharsets.UTF_8));
    }
  }

  private static void entry(ZipOutputStream zip, String name, byte[] data) throws IOException {
    zip.putNextEntry(new ZipEntry(name));
    zip.write(data);
    zip.closeEntry();
  }

  static String manifest(Contents c, String app, long code, String device, long now) {
    try {
      JSONObject m = new JSONObject();
      m.put(MARK, FORMAT);
      m.put("app", app);
      m.put("versionCode", code);
      m.put("device", device);
      m.put("made", time(now));
      JSONArray card = new JSONArray();
      for (String name : c.cardNames) card.put(name);
      m.put("memoryCard", card);
      m.put("touchLayout", c.touch != null);
      m.put("instantSave", c.instant != null);
      m.put("settings", c.settings != null);
      return m.toString(1) + "\n";
    } catch (JSONException e) {
      throw new IllegalStateException(e);
    }
  }

  static String time(long now) {
    SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.ROOT);
    f.setTimeZone(TimeZone.getTimeZone("UTC"));
    return f.format(new Date(now));
  }

  /** A suggested file name: "CubeEvil4-backup-20261114-153000.zip". */
  static String fileName(long now) {
    return "CubeEvil4-backup-" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date(now)) + ".zip";
  }

  /** JSON whose first key is `mark` (a byte check, as TouchLayoutFile.is). */
  static boolean marked(byte[] data, String mark) {
    if (data == null || data.length < 2 || data.length > MAX_JSON) return false;
    String head = new String(data, 0, Math.min(data.length, 256), StandardCharsets.UTF_8);
    int i = 0;
    while (i < head.length() && (Character.isWhitespace(head.charAt(i)) || head.charAt(i) == '﻿')) i++;
    return i < head.length() && head.charAt(i) == '{' && head.indexOf('"' + mark + '"') > i;
  }

  static boolean isManifest(byte[] data) { return marked(data, MARK); }
  static boolean isSettings(byte[] data) { return marked(data, SETTINGS_MARK); }

  /** Where a ZIP comes from (its manifest or its settings file): version, its code, the device; null fields unknown. */
  static final class Origin {
    final String app, device;
    final long code;

    Origin(String app, long code, String device) { this.app = app; this.code = code; this.device = device; }

    static Origin of(byte[] json) {
      if (json == null) return null;
      try {
        JSONObject o = new JSONObject(text(json));
        return new Origin(o.optString("app", ""), o.optLong("versionCode", 0), o.optString("device", ""));
      } catch (JSONException e) {
        return null;
      }
    }
  }

  private static String text(byte[] json) {
    String t = new String(json, StandardCharsets.UTF_8);
    return t.startsWith("﻿") ? t.substring(1) : t;
  }

  // --- the instant save ------------------------------------------------------------------------------------------

  /** Why `f` is no instant save of the app (the checks of runtime/instant_store.cpp readSlot); null when it is one. */
  static String instantProblem(byte[] f) {
    if (f == null || f.length != INSTANT_BYTES) return "not " + INSTANT_BYTES + " bytes";
    for (int i = 0; i < INSTANT_MAGIC.length; i++) if (f[i] != INSTANT_MAGIC[i]) return "not an instant save";
    if (get(f, 8) != 1) return "format " + (get(f, 8) & 0xffffffffL) + " unknown";
    if (get(f, 12) != INSTANT_PAYLOAD) return "a different size inside";
    if (get(f, 28) != checksum(f, 0, 28) || get(f, 20) != checksum(f, INSTANT_HEADER, INSTANT_PAYLOAD)) return "damaged";
    return null;
  }

  /** The start of an instant save (its magic): for SaveImport. */
  static boolean isInstant(byte[] f) {
    if (f == null || f.length != INSTANT_BYTES) return false;
    for (int i = 0; i < INSTANT_MAGIC.length; i++) if (f[i] != INSTANT_MAGIC[i]) return false;
    return true;
  }

  private static int get(byte[] p, int at) {
    return (p[at] & 255) << 24 | (p[at + 1] & 255) << 16 | (p[at + 2] & 255) << 8 | (p[at + 3] & 255);
  }

  /** CRC-32 with the polynomial 0x04C11DB7, highest bit first, from 0 (runtime/instant_store.cpp checksum). */
  static int checksum(byte[] p, int from, int size) {
    int c = 0;
    for (int i = from; i < from + size; i++) {
      c ^= (p[i] & 255) << 24;
      for (int b = 0; b < 8; b++) c = (c & 0x80000000) != 0 ? (c << 1) ^ 0x04C11DB7 : c << 1;
    }
    return c;
  }

  /** The instant save's file under the game's folder (runtime/instant_store.cpp). */
  static File instantFile(File gameRoot) { return new File(new File(gameRoot, "instant"), "slot-0.ce4save"); }

  /** A file's bytes; null when it is not there or larger than `max`. */
  static byte[] read(File f, int max) throws IOException {
    if (!f.isFile() || f.length() > max) return null;
    try (InputStream in = new FileInputStream(f)) {
      ByteArrayOutputStream out = new ByteArrayOutputStream((int) f.length());
      byte[] buffer = new byte[65536];
      for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
      return out.toByteArray();
    }
  }

  /** `data` as `f`, written beside it first and then renamed over it (the core sees the old file or the new one). */
  static void writeAtomically(File f, byte[] data) throws IOException {
    File parent = f.getParentFile();
    if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("no folder " + parent);
    File part = new File(parent, f.getName() + ".import");
    try (FileOutputStream out = new FileOutputStream(part)) {
      out.write(data);
      out.getFD().sync();
    }
    if (!part.renameTo(f)) {
      part.delete();
      throw new IOException("cannot replace " + f.getName());
    }
  }

  // --- the settings ----------------------------------------------------------------------------------------------

  /** The settings file of `groups` (name -> SharedPreferences.getAll()), with where it was made. */
  static String settings(Map<String, ? extends Map<String, ?>> groups, String app, long code, String device) {
    try {
      JSONObject root = new JSONObject();
      root.put(SETTINGS_MARK, FORMAT);
      root.put("app", app);
      root.put("versionCode", code);
      root.put("device", device);
      JSONObject all = new JSONObject();
      for (Map.Entry<String, ? extends Map<String, ?>> g : groups.entrySet()) {
        JSONObject one = new JSONObject();
        for (Map.Entry<String, Object> e : new TreeMap<String, Object>(g.getValue()).entrySet()) {
          if (outside(g.getKey(), e.getKey())) continue;
          JSONArray typed = encode(e.getValue());
          if (typed != null) one.put(e.getKey(), typed);
        }
        all.put(g.getKey(), one);
      }
      root.put("groups", all);
      return root.toString(1) + "\n";
    } catch (JSONException e) {
      throw new IllegalStateException(e);
    }
  }

  /** A setting as [type, value]: "b" boolean, "i" int, "l" long, "f" float, "s" string, "set" a set of strings. */
  static JSONArray encode(Object v) {
    try {
      JSONArray a = new JSONArray();
      if (v instanceof Boolean) return a.put("b").put(((Boolean) v).booleanValue());
      if (v instanceof Integer) return a.put("i").put(((Integer) v).intValue());
      if (v instanceof Long) return a.put("l").put(((Long) v).longValue());
      if (v instanceof Float) {
        float f = (Float) v;
        return Float.isNaN(f) || Float.isInfinite(f) ? null : a.put("f").put((double) f);
      }
      if (v instanceof String) return a.put("s").put(v);
      if (v instanceof Set) {
        JSONArray items = new JSONArray();
        for (Object o : new TreeSet<Object>((Set<?>) v)) items.put(String.valueOf(o));
        return a.put("set").put(items);
      }
    } catch (JSONException e) {
      return null;
    }
    return null;
  }

  /** A setting of the file as SharedPreferences gives it back (Boolean, Integer, Long, Float, String, Set); null when
   *  its type is unknown. */
  static Object decode(JSONArray a) {
    try {
      switch (a.getString(0)) {
        case "b": return a.getBoolean(1);
        case "i": return a.getInt(1);
        case "l": return a.getLong(1);
        case "f": return (float) a.getDouble(1);
        case "s": return a.getString(1);
        case "set":
          JSONArray items = a.getJSONArray(1);
          Set<String> set = new HashSet<>();
          for (int i = 0; i < items.length(); i++) set.add(items.getString(i));
          return set;
        default: return null;
      }
    } catch (JSONException | ClassCastException e) {
      return null;
    }
  }

  /** What a settings file would change in one group. */
  static final class Plan {
    final String group;
    final Map<String, Object> put = new LinkedHashMap<>();
    final Set<String> remove = new TreeSet<>();
    /** Settings left out (another type, unknown to an older app); the performance settings kept from another device. */
    int skipped, keptPerformance;

    Plan(String group) { this.group = group; }

    boolean changes() { return !put.isEmpty() || !remove.isEmpty(); }
  }

  /** The settings file in plans for each group it holds and this app has (GROUPS); IllegalArgumentException when it
   *  is none. `newer`: made by a newer version; `sameDevice`: on this kind of device (else its performance settings
   *  stay). */
  static List<Plan> plan(byte[] file, Map<String, ? extends Map<String, ?>> now, boolean newer, boolean sameDevice,
                         StringBuilder log) {
    JSONObject groups;
    try {
      JSONObject root = new JSONObject(text(file));
      if (!root.has(SETTINGS_MARK)) throw new IllegalArgumentException("not a settings file of CubeEvil4");
      groups = root.getJSONObject("groups");
    } catch (JSONException e) {
      throw new IllegalArgumentException("the settings file cannot be read (" + e.getMessage() + ")");
    }
    List<Plan> plans = new ArrayList<>();
    for (String group : GROUPS) {
      JSONObject entries = groups.optJSONObject(group);
      if (entries == null) continue;
      Map<String, ?> current = now.get(group);
      plans.add(plan(group, entries, current != null ? current : Collections.<String, Object>emptyMap(), newer,
          sameDevice));
    }
    return plans;
  }

  static Plan plan(String group, JSONObject entries, Map<String, ?> current, boolean newer, boolean sameDevice) {
    Plan p = new Plan(group);
    for (Iterator<String> keys = entries.keys(); keys.hasNext(); ) {
      String key = keys.next();
      if (kept(group, key, sameDevice)) {
        if (!outside(group, key)) p.keptPerformance++;
        continue;
      }
      JSONArray typed = entries.optJSONArray(key);
      Object value = typed != null ? decode(typed) : null;
      Object local = current.get(key);
      if (value == null || (local != null && !sameType(local, value)) || (local == null && newer)) {
        p.skipped++;
        continue;
      }
      if (!value.equals(local)) p.put.put(key, value);
    }
    for (String key : current.keySet()) {
      if (!kept(group, key, sameDevice) && !entries.has(key)) p.remove.add(key);
    }
    return p;
  }

  private static boolean sameType(Object a, Object b) {
    return a instanceof Set ? b instanceof Set : a.getClass() == b.getClass();
  }

  /** A plan into its group's preferences (written at once: a restart may follow). */
  @SuppressWarnings("unchecked")
  static void apply(SharedPreferences prefs, Plan p) {
    SharedPreferences.Editor e = prefs.edit();
    for (String key : p.remove) e.remove(key);
    for (Map.Entry<String, Object> kv : p.put.entrySet()) {
      Object v = kv.getValue();
      if (v instanceof Boolean) e.putBoolean(kv.getKey(), (Boolean) v);
      else if (v instanceof Integer) e.putInt(kv.getKey(), (Integer) v);
      else if (v instanceof Long) e.putLong(kv.getKey(), (Long) v);
      else if (v instanceof Float) e.putFloat(kv.getKey(), (Float) v);
      else if (v instanceof String) e.putString(kv.getKey(), (String) v);
      else if (v instanceof Set) e.putStringSet(kv.getKey(), (Set<String>) v);
    }
    e.commit();
  }
}
