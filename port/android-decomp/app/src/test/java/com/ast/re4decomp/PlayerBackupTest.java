package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

/**
 * PlayerBackup and PlayerWipe (0.99.15): the ZIP of the player's data and its way back (SaveImport), the settings key by
 * key (types, newer versions, another device), the instant save's checks, and "Delete All Data" at the start.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class PlayerBackupTest {
  @Rule public TemporaryFolder tmp = new TemporaryFolder();

  private static Context context() { return org.robolectric.RuntimeEnvironment.getApplication(); }

  private static byte[] gci(String name) {
    byte[] g = new byte[0x40 + DolphinCard.BLOCK];
    System.arraycopy("G4BE08".getBytes(StandardCharsets.ISO_8859_1), 0, g, 0, 6);
    System.arraycopy(name.getBytes(StandardCharsets.ISO_8859_1), 0, g, 8, name.length());
    g[0x40] = 42;
    return g;
  }

  /** An instant save as runtime/instant_store.cpp writes it (magic, format 1, the payload's size and both CRCs). */
  static byte[] instant(int fill) {
    byte[] f = new byte[PlayerBackup.INSTANT_BYTES];
    System.arraycopy(new byte[] {'C', 'E', '4', 'I', 'N', 'S', 'T', 0}, 0, f, 0, 8);
    put(f, 8, 1);
    put(f, 12, PlayerBackup.INSTANT_PAYLOAD);
    put(f, 16, 123456);
    for (int i = PlayerBackup.INSTANT_HEADER; i < f.length; i++) f[i] = (byte) (i * 7 + fill);
    put(f, 20, PlayerBackup.checksum(f, PlayerBackup.INSTANT_HEADER, PlayerBackup.INSTANT_PAYLOAD));
    put(f, 28, PlayerBackup.checksum(f, 0, 28));
    return f;
  }

  private static void put(byte[] p, int at, int n) {
    for (int i = 3; i >= 0; i--) { p[at + i] = (byte) n; n >>>= 8; }
  }

  private static final byte[] TOUCH = ("{\n  \"" + TouchLayoutFile.MARK + "\": 1,\n  \"layout\": {}\n}")
      .getBytes(StandardCharsets.UTF_8);

  private static SharedPreferences prefs(String name) {
    return context().getSharedPreferences(name, Context.MODE_PRIVATE);
  }

  private static Map<String, Map<String, ?>> groups() {
    Map<String, Map<String, ?>> g = new LinkedHashMap<>();
    for (String group : PlayerBackup.GROUPS) g.put(group, prefs(group).getAll());
    return g;
  }

  /** The player's settings of every type in the groups, and the app's bookkeeping. */
  private static void fill() {
    prefs(CoreActivity.INPUT_PREFS).edit().putInt(CoreActivity.TOUCH_MODE, 2).putBoolean(CoreActivity.GYRO_AIM, true)
        .putFloat(CoreActivity.AIM_SPEED, 1.1f).putString(CoreActivity.BINDINGS_CONTROLLER, "a=1;b=2")
        .putStringSet("someSet", new HashSet<>(Arrays.asList("x", "y"))).commit();
    prefs(CoreActivity.GAME_PREFS).edit().putInt(CoreActivity.CHEATS, 1 | 4).putInt(CoreActivity.ENEMY_SEED, 777).commit();
    prefs(CoreActivity.GRAPHICS_PREFS).edit().putInt("scaleHalves", 5).putString(CoreActivity.RENDERER, "vulkan")
        .putInt("musicVolume", 40).putLong(PerfProfile.BENCH_DATE, 1731500000000L).putInt(CoreMenu.CRT_LEVEL, 2)
        .putBoolean(VulkanGuard.STARTING, true).putInt(VulkanGuard.PID, 4321).commit();
    prefs(CoreLog.PREFS).edit().putBoolean(CoreLog.RECORD, false).commit();
    // RetroTouch's own: its button style comes along, its layout has the touch layout file
    prefs(TouchLayoutFile.PREFS).edit().putString("button_style_" + TouchControls.LAYOUT_ID, "LABELS")
        .putString(TouchLayoutFile.KEY, "{\"controls\":[]}").commit();
  }

  private static void clearAll() {
    for (String group : PlayerBackup.GROUPS) prefs(group).edit().clear().commit();
  }

  private static void restore(byte[] settings, boolean newer, boolean sameDevice, StringBuilder log) {
    for (PlayerBackup.Plan p : PlayerBackup.plan(settings, groups(), newer, sameDevice, log)) {
      PlayerBackup.apply(prefs(p.group), p);
    }
  }

  @Test public void settingsComeBackWithTheirTypesWithoutTheBookkeeping() {
    clearAll();
    fill();
    Map<String, Map<String, ?>> before = groups();
    byte[] file = PlayerBackup.settings(before, "0.99.15-rc", 117, "Retroid Pocket Flip 2").getBytes(StandardCharsets.UTF_8);
    assertTrue(PlayerBackup.isSettings(file));
    assertFalse("the Vulkan start guard stays out", new String(file, StandardCharsets.UTF_8).contains(VulkanGuard.STARTING));
    assertFalse("RetroTouch's layout stays out", new String(file, StandardCharsets.UTF_8).contains(TouchLayoutFile.KEY));
    clearAll();
    prefs(CoreActivity.GRAPHICS_PREFS).edit().putBoolean(VulkanGuard.STARTING, false).commit();
    restore(file, false, true, new StringBuilder());
    SharedPreferences input = prefs(CoreActivity.INPUT_PREFS), g = prefs(CoreActivity.GRAPHICS_PREFS);
    assertEquals(2, input.getInt(CoreActivity.TOUCH_MODE, 0));
    assertTrue(input.getBoolean(CoreActivity.GYRO_AIM, false));
    assertEquals(1.1f, input.getFloat(CoreActivity.AIM_SPEED, 0), 0f);
    assertEquals("a=1;b=2", input.getString(CoreActivity.BINDINGS_CONTROLLER, null));
    assertEquals(new HashSet<>(Arrays.asList("x", "y")), input.getStringSet("someSet", null));
    assertEquals(5, prefs(CoreActivity.GAME_PREFS).getInt(CoreActivity.CHEATS, 0));
    assertEquals(777, prefs(CoreActivity.GAME_PREFS).getInt(CoreActivity.ENEMY_SEED, 0));
    assertEquals(5, g.getInt("scaleHalves", 0));
    assertEquals(1731500000000L, g.getLong(PerfProfile.BENCH_DATE, 0));
    assertFalse("the device's own guard is kept", g.getBoolean(VulkanGuard.STARTING, true));
    assertFalse(g.contains(VulkanGuard.PID));
    assertFalse(prefs(CoreLog.PREFS).getBoolean(CoreLog.RECORD, true));
    assertEquals("LABELS", prefs(TouchLayoutFile.PREFS).getString("button_style_" + TouchControls.LAYOUT_ID, null));
    assertFalse("the layout comes with its own file", prefs(TouchLayoutFile.PREFS).contains(TouchLayoutFile.KEY));
    // the same again: nothing to change
    for (PlayerBackup.Plan p : PlayerBackup.plan(file, groups(), false, true, new StringBuilder())) {
      assertFalse(p.group + " " + p.put + " " + p.remove, p.changes());
    }
  }

  @Test public void aSettingTheZipDoesNotHoldGoesBackToItsDefault() {
    clearAll();
    fill();
    byte[] file = PlayerBackup.settings(groups(), "0.99.15-rc", 117, "A").getBytes(StandardCharsets.UTF_8);
    prefs(CoreActivity.INPUT_PREFS).edit().putBoolean(CoreActivity.KNIFE_SWIPE, false).commit();
    restore(file, false, true, new StringBuilder());
    assertFalse("changed after the export: back to the default", prefs(CoreActivity.INPUT_PREFS).contains(CoreActivity.KNIFE_SWIPE));
  }

  @Test public void fromAnotherDeviceThePerformanceSettingsStay() {
    clearAll();
    fill();
    byte[] file = PlayerBackup.settings(groups(), "0.99.15-rc", 117, "Phone A").getBytes(StandardCharsets.UTF_8);
    clearAll();
    prefs(CoreActivity.GRAPHICS_PREFS).edit().putInt("scaleHalves", 2).putString(CoreActivity.RENDERER, "gles")
        .putInt(PerfProfile.ACTIVE, PerfProfile.LOW).putInt(CoreMenu.DYN_CAP_60, 3).commit();
    StringBuilder log = new StringBuilder();
    List<PlayerBackup.Plan> plans = PlayerBackup.plan(file, groups(), false, false, log);
    int kept = 0;
    for (PlayerBackup.Plan p : plans) { kept += p.keptPerformance; PlayerBackup.apply(prefs(p.group), p); }
    SharedPreferences g = prefs(CoreActivity.GRAPHICS_PREFS);
    assertEquals("this device's resolution", 2, g.getInt("scaleHalves", 0));
    assertEquals("this device's renderer", "gles", g.getString(CoreActivity.RENDERER, null));
    assertEquals("this device's profile", PerfProfile.LOW, g.getInt(PerfProfile.ACTIVE, -1));
    assertEquals("this device's Dynamic top", 3, g.getInt(CoreMenu.DYN_CAP_60, 0));
    assertFalse("no other device's benchmark", g.contains(PerfProfile.BENCH_DATE));
    assertEquals("the sound comes along", 40, g.getInt("musicVolume", 0));
    assertEquals("the picture style comes along", 2, g.getInt(CoreMenu.CRT_LEVEL, 0));
    assertEquals(2, prefs(CoreActivity.INPUT_PREFS).getInt(CoreActivity.TOUCH_MODE, 0));
    assertEquals("resolution, renderer, benchmark date", 3, kept);
  }

  @Test public void aNewerVersionsUnknownSettingsAndOtherTypesAreLeftOut() throws Exception {
    clearAll();
    prefs(CoreActivity.INPUT_PREFS).edit().putInt(CoreActivity.TOUCH_MODE, 1).putInt(CoreActivity.VIBRATION, 1).commit();
    org.json.JSONObject root = new org.json.JSONObject();
    root.put(PlayerBackup.SETTINGS_MARK, 1).put("app", "1.2.0").put("versionCode", 999).put("device", "X");
    org.json.JSONObject input = new org.json.JSONObject();
    input.put(CoreActivity.TOUCH_MODE, new org.json.JSONArray().put("i").put(2));
    input.put(CoreActivity.VIBRATION, new org.json.JSONArray().put("s").put("strong"));  // another type: left out
    input.put("futureSwitch", new org.json.JSONArray().put("b").put(true));             // unknown here: left out
    input.put("strangeType", new org.json.JSONArray().put("q").put(1));                 // unknown type: left out
    root.put("groups", new org.json.JSONObject().put(CoreActivity.INPUT_PREFS, input));
    byte[] file = root.toString().getBytes(StandardCharsets.UTF_8);
    PlayerBackup.Origin from = PlayerBackup.Origin.of(file);
    assertEquals(999, from.code);
    assertEquals("1.2.0", from.app);
    List<PlayerBackup.Plan> plans = PlayerBackup.plan(file, groups(), true, false, new StringBuilder());
    assertEquals(1, plans.size());
    assertEquals(3, plans.get(0).skipped);
    PlayerBackup.apply(prefs(CoreActivity.INPUT_PREFS), plans.get(0));
    SharedPreferences p = prefs(CoreActivity.INPUT_PREFS);
    assertEquals(2, p.getInt(CoreActivity.TOUCH_MODE, 0));
    assertEquals("kept as an int", 1, p.getInt(CoreActivity.VIBRATION, 0));
    assertFalse(p.contains("futureSwitch"));
    // from an older or the same version a setting not set here is taken
    plans = PlayerBackup.plan(file, groups(), false, false, new StringBuilder());
    assertEquals(Boolean.TRUE, plans.get(0).put.get("futureSwitch"));
  }

  @Test public void notASettingsFileIsRefused() {
    try {
      PlayerBackup.plan("{\"other\": 1}".getBytes(StandardCharsets.UTF_8), groups(), false, true, new StringBuilder());
      fail();
    } catch (IllegalArgumentException e) {
      assertTrue(e.getMessage(), e.getMessage().contains("settings"));
    }
  }

  @Test public void theInstantSavesChecks() {
    byte[] good = instant(1);
    assertNull(PlayerBackup.instantProblem(good));
    assertTrue(PlayerBackup.isInstant(good));
    byte[] bad = good.clone();
    bad[1000] ^= 1;
    assertEquals("damaged", PlayerBackup.instantProblem(bad));
    assertEquals("not an instant save", PlayerBackup.instantProblem(new byte[PlayerBackup.INSTANT_BYTES]));
    assertNotNull(PlayerBackup.instantProblem(new byte[10]));
  }

  private static byte[] backup(PlayerBackup.Contents c) throws IOException {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    PlayerBackup.write(out, c, "0.99.15-rc", 117, "Retroid Pocket Flip 2", 0L);
    return out.toByteArray();
  }

  /** The whole round trip as the menu does it: export, delete all, import; every part comes back. */
  @Test public void exportDeleteAllImport() throws Exception {
    clearAll();
    fill();
    File root = tmp.newFolder("game");
    File card = PlayerWipe.cardFile(root), slot = PlayerBackup.instantFile(root);
    card.getParentFile().mkdirs();
    try (FileOutputStream out = new FileOutputStream(card)) { out.write(new byte[2 * 1024 * 1024]); }
    byte[] slotFile = instant(3);
    PlayerBackup.writeAtomically(slot, slotFile);
    assertArrayEquals(slotFile, PlayerBackup.read(slot, PlayerBackup.INSTANT_BYTES + 1));

    PlayerBackup.Contents c = new PlayerBackup.Contents();
    c.card("08-G4BE-bh4_system.gci", gci("bh4_system"));
    c.card("08-G4BE-bh4_data00.gci", gci("bh4_data00"));
    c.touch = TOUCH;
    c.instant = PlayerBackup.read(slot, PlayerBackup.INSTANT_BYTES + 1);
    c.settings = PlayerBackup.settings(groups(), "0.99.15-rc", 117, "Retroid Pocket Flip 2");
    Map<String, Map<String, ?>> before = groups();
    byte[] zip = backup(c);

    // what is in it, the manifest first
    try (ZipInputStream z = new ZipInputStream(new java.io.ByteArrayInputStream(zip))) {
      assertEquals(PlayerBackup.MANIFEST, z.getNextEntry().getName());
      org.json.JSONObject m = new org.json.JSONObject(new String(readAll(z), StandardCharsets.UTF_8));
      assertEquals(1, m.getInt(PlayerBackup.MARK));
      assertEquals("0.99.15-rc", m.getString("app"));
      assertEquals(2, m.getJSONArray("memoryCard").length());
      assertTrue(m.getBoolean("instantSave") && m.getBoolean("touchLayout") && m.getBoolean("settings"));
      assertEquals(PlayerBackup.CARD_FOLDER + "08-G4BE-bh4_system.gci", z.getNextEntry().getName());
    }

    // Delete All Data: asked, done at the next start before the core
    PlayerWipe.request(context());
    assertTrue(PlayerWipe.pending(context()));
    String note = PlayerWipe.prepare(context(), root);
    assertTrue(note, note.contains("memory card deleted") && note.contains("instant save deleted"));
    assertFalse(card.exists());
    assertFalse(slot.exists());
    assertFalse(PlayerWipe.pending(context()));
    assertEquals("only once", "", PlayerWipe.prepare(context(), root));
    assertTrue("the card's folder stays for the core's new card", card.getParentFile().isDirectory());
    // the settings and RetroTouch stay with Delete All Data
    assertEquals(before, groups());
    clearAll();

    // Import: every part found
    StringBuilder log = new StringBuilder();
    SaveImport.Found f = SaveImport.read(zip, log);
    assertEquals(2, f.gci.size());
    assertArrayEquals(gci("bh4_system"), f.gci.get(0));
    assertEquals(1, f.touch.size());
    assertArrayEquals(slotFile, f.instant);
    assertNotNull(f.manifest);
    assertNotNull(f.settings);
    assertTrue(log.toString(), log.toString().contains("2 files to import") && log.toString().contains("0 other files left out"));
    assertEquals("Retroid Pocket Flip 2", PlayerBackup.Origin.of(f.manifest).device);
    assertEquals(117, PlayerBackup.Origin.of(f.settings).code);
    PlayerBackup.writeAtomically(slot, f.instant);
    restore(f.settings, false, true, new StringBuilder());
    assertArrayEquals(slotFile, PlayerBackup.read(slot, PlayerBackup.INSTANT_BYTES + 1));
    Map<String, Map<String, ?>> after = groups();
    for (String group : PlayerBackup.GROUPS) {
      Map<String, Object> want = new LinkedHashMap<>(before.get(group)), got = new LinkedHashMap<>(after.get(group));
      want.keySet().removeIf(k -> PlayerBackup.outside(group, k));
      assertEquals(group, want, got);
    }
  }

  /** An app of 0.99.14 takes the .gci files and the touch layout of the new ZIP and leaves the rest out (the old
   *  SaveImport is this one without the extras: SaveImport.files with or without the touch list). */
  @Test public void anOlderAppTakesTheSavesOfTheNewZip() throws Exception {
    PlayerBackup.Contents c = new PlayerBackup.Contents();
    c.card("08-G4BE-bh4_data00.gci", gci("bh4_data00"));
    c.touch = TOUCH;
    c.instant = instant(0);
    c.settings = PlayerBackup.settings(new LinkedHashMap<String, Map<String, ?>>(), "0.99.15-rc", 117, "A");
    StringBuilder log = new StringBuilder();
    List<byte[]> saves = SaveImport.files(backup(c), log);
    assertEquals(1, saves.size());
    assertTrue(log.toString(), log.toString().contains("4 other files left out"));
  }

  /** A ZIP of the old export folder (0.99.14: the .gci files and CubeEvil4-touch-layout.json) still imports. */
  @Test public void theOldExportStillImports() throws Exception {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(out)) {
      z.putNextEntry(new ZipEntry("export/08-G4BE-bh4_data00.gci")); z.write(gci("bh4_data00")); z.closeEntry();
      z.putNextEntry(new ZipEntry("export/" + TouchLayoutFile.NAME)); z.write(TOUCH); z.closeEntry();
    }
    SaveImport.Found f = SaveImport.read(out.toByteArray(), new StringBuilder());
    assertEquals(1, f.gci.size());
    assertEquals(1, f.touch.size());
    assertNull(f.settings);
    assertNull(f.instant);
    // and a .gci alone, a settings file alone, an instant save alone
    assertEquals(1, SaveImport.read(gci("bh4_data00"), new StringBuilder()).gci.size());
    byte[] settings = PlayerBackup.settings(groups(), "0.99.15-rc", 117, "A").getBytes(StandardCharsets.UTF_8);
    assertNotNull(SaveImport.read(settings, new StringBuilder()).settings);
    assertNotNull(SaveImport.read(instant(5), new StringBuilder()).instant);
  }

  @Test public void aZipWithOnlySettingsIsNoError() throws Exception {
    PlayerBackup.Contents c = new PlayerBackup.Contents();
    c.settings = PlayerBackup.settings(groups(), "0.99.15-rc", 117, "A");
    SaveImport.Found f = SaveImport.read(backup(c), new StringBuilder());
    assertTrue(f.gci.isEmpty());
    assertNotNull(f.settings);
  }

  @Test public void theWipeEmptiesWhatItCannotDelete() throws Exception {
    File root = tmp.newFolder("game2");
    String nothing = PlayerWipe.wipe(root);
    assertTrue(nothing, nothing.contains("memory card not there"));
    File leftover = new File(PlayerBackup.instantFile(root).getPath() + ".tmp");
    leftover.getParentFile().mkdirs();
    assertTrue(leftover.createNewFile());
    PlayerWipe.wipe(root);
    assertFalse("an unfinished instant save goes too", leftover.exists());
    assertEquals("nothing asked: nothing done", "", PlayerWipe.prepare(context(), root));
  }

  @Test public void theNamesOfTheFile() {
    assertTrue(PlayerBackup.fileName(0L).matches("CubeEvil4-backup-\\d{8}-\\d{6}\\.zip"));
    assertEquals("1970-01-01T00:00:00Z", PlayerBackup.time(0L));
  }

  private static byte[] readAll(ZipInputStream z) throws IOException {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    byte[] b = new byte[4096];
    for (int n; (n = z.read(b)) > 0; ) out.write(b, 0, n);
    return out.toByteArray();
  }
}
