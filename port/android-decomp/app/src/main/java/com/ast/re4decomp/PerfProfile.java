package com.ast.re4decomp;

import android.content.SharedPreferences;
import java.io.File;
import java.io.FileReader;
import java.io.BufferedReader;
import java.util.Arrays;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * The performance profiles of the app menu's start page and the benchmark's recommendation (0.99.10, wishes of the
 * project owner, 11.11.2026; port/docs/leistungsprofil-plan.md). Custom, Low, Medium, High: Low, Medium and High are
 * computed for this device from the benchmark's levels (the 30 and 60 FPS levels, the processor sign) and the
 * device's data (graphics chip, processor, memory, the display's rate and rows). A profile sets the settings of KEYS; the
 * others (picture format, look filters, renderer, the untried experiments) stay the player's.
 *
 * Custom is kept: choosing a profile while Custom is active keeps its values (CUSTOM_PREFIX), choosing Custom brings
 * them back; a hand change of one of KEYS while a profile is active makes Custom active with the values as they are
 * (sync: the values the profile set are kept, APPLIED_PREFIX).
 */
final class PerfProfile {
  static final int CUSTOM = 0, LOW = 1, MEDIUM = 2, HIGH = 3;
  /** GRAPHICS_PREFS: the active profile. */
  static final String ACTIVE = "perfProfile";
  static final String CUSTOM_PREFIX = "perfCustom.", APPLIED_PREFIX = "perfApplied.";
  /** The last benchmark (System.currentTimeMillis, 0 none) and its processor sign. */
  static final String BENCH_DATE = "benchDate", BENCH_CPU = "benchCpu";
  /** 1 the first start's benchmark is to come, 2 the menu asks once (an update), 0 nothing (FIRST_*). */
  static final String FIRST = "benchFirst";
  static final int FIRST_NONE = 0, FIRST_PENDING = 1, FIRST_ASK = 2;

  /** The settings a profile sets, in this order (booleans as 0/1). */
  static final String[] KEYS = {"scaleHalves", "samples", "fxaa", "anisotropy", "smooth60", CoreMenu.FRAME_SKIP,
      "fastCores", "weatherEffects", "specialEffects", "particleEffects", "shadowsLight", CoreBridge.VIEW_DISTANCE};
  static final int SCALE = 0, SAMPLES = 1, FXAA = 2, ANISO = 3, SMOOTH = 4, SKIP = 5, CORES = 6, WEATHER = 7,
      SPECIAL = 8, PARTICLES = 9, SHADOWS = 10, VIEW = 11;
  private static final boolean[] BOOL = {false, false, true, false, true, false, false, true, true, false, true, false};

  private PerfProfile() {}

  /** What the app knows of the device. */
  static final class Device {
    final String gpu;          // the renderer's text (CoreBridge.rendererInfo), the chip's name in it
    final int cores, fastCores, maxMhz, ramMb;
    final double hz;           // the display (FramePacing), 0 unknown
    final int samplesMax, anisoMax;
    final int rows;            // the screen's rows of a 16:9 picture (min(height, width * 9 / 16)), 0 unknown
    Device(String gpu, int cores, int fastCores, int maxMhz, int ramMb, double hz, int samplesMax, int anisoMax) {
      this(gpu, cores, fastCores, maxMhz, ramMb, hz, samplesMax, anisoMax, 0);
    }
    Device(String gpu, int cores, int fastCores, int maxMhz, int ramMb, double hz, int samplesMax, int anisoMax,
           int rows) {
      this.gpu = gpu == null ? "" : gpu; this.cores = cores; this.fastCores = fastCores; this.maxMhz = maxMhz;
      this.ramMb = ramMb; this.hz = hz; this.samplesMax = samplesMax; this.anisoMax = anisoMax; this.rows = rows;
    }
  }

  /** The benchmark's levels in halves (0: none measured) and its processor sign. */
  static final class Bench {
    final int cap30, cap60;
    final boolean cpuSign, measured;
    Bench(int cap30, int cap60, boolean cpuSign, boolean measured) {
      this.cap30 = cap30; this.cap60 = cap60; this.cpuSign = cpuSign; this.measured = measured;
    }
    static Bench of(SharedPreferences g) {
      int c30 = g.getInt(CoreMenu.DYN_CAP_30, 0), c60 = g.getInt(CoreMenu.DYN_CAP_60, 0);
      boolean measured = c30 >= 2 && c30 <= 8;
      return new Bench(measured ? c30 : 0, c60 >= 2 && c60 <= 8 ? c60 : 0, g.getBoolean(BENCH_CPU, false), measured);
    }
  }

  // ---- the device ----------------------------------------------------------------------------------------------

  // graphics chips the reports showed at the limit (Mali-G52/G57 phones: 1x..2.5x with 30 FPS, 1x with 60)
  private static final Pattern WEAK_GPU = Pattern.compile(
      "Mali-(G31|G51|G52|G57|G71|G72|G76|T\\d+)|Adreno \\(TM\\) [345]\\d\\d|Adreno [345]\\d\\d|Adreno \\(TM\\) 6[01]\\d|"
          + "Adreno 6[01]\\d|PowerVR", Pattern.CASE_INSENSITIVE);
  // and those that held 2x..4x (Retroid's Adreno 650, the 7xx/8xx, the Mali-G6xx/G7xx of the newer phones)
  private static final Pattern STRONG_GPU = Pattern.compile(
      "Adreno \\(TM\\) (6[4-9]\\d|[78]\\d\\d)|Adreno (6[4-9]\\d|[78]\\d\\d)|Mali-G(6[1-9]\\d|7[1-9]\\d)|Immortalis|Xclipse",
      Pattern.CASE_INSENSITIVE);

  static boolean weakGpu(Device d) { return WEAK_GPU.matcher(d.gpu).find(); }
  static boolean strongGpu(Device d) { return STRONG_GPU.matcher(d.gpu).find(); }

  /** The 30 pass's first level for this chip: 2.5x for the weak ones (saves up to 30 s), else 4x. */
  static int benchTop(Device d) { return weakGpu(d) ? 5 : 8; }

  /** Only small cores (Galaxy A13: eight at 2.0 GHz) or the benchmark's sign: the processor holds the game back. */
  static boolean cpuWeak(Device d, Bench b) {
    if (b.cpuSign) return true;
    return d.maxMhz > 0 && d.maxMhz <= 2100 && d.fastCores >= d.cores;
  }

  static boolean lowMemory(Device d) { return d.ramMb > 0 && d.ramMb < 2800; }

  /** The level (halves) whose 336 picture rows a 1x frame has come closest to the screen's rows (core_mode.cpp's
   *  "Dynamic" counts the same way): the Retroid's 1080 rows 6 (3x), a 1220-row phone 7, a 720-row one 4; 6 unknown. */
  static int screenLevel(Device d) {
    if (d.rows <= 0) return 6;
    return Math.max(2, Math.min(8, (int) Math.round(d.rows * 2 / 336.0)));
  }

  /** Without a benchmark: the levels guessed from the chip (the reports' figures). */
  static Bench guess(Device d, Bench b) {
    if (b.measured) return b;
    if (weakGpu(d)) return new Bench(2, 2, false, false);
    if (strongGpu(d)) return new Bench(6, 4, false, false);
    return new Bench(4, 2, false, false);
  }

  // ---- the profiles --------------------------------------------------------------------------------------------

  /** The profile the benchmark suggests: High when 60 FPS held at 1.5x at least, Medium when 30 did, else Low; Low
   *  too with little memory or a weak processor. */
  static int recommend(Device d, Bench measured) {
    Bench b = guess(d, measured);
    if (lowMemory(d) || cpuWeak(d, b)) return LOW;
    if (b.cap60 >= 3) return HIGH;
    if (b.cap30 >= 3) return MEDIUM;
    return LOW;
  }

  /** The values of LOW, MEDIUM or HIGH for this device (KEYS order). */
  static int[] values(int profile, Device d, Bench measured) {
    Bench b = guess(d, measured);
    boolean cpu = cpuWeak(d, b);
    boolean uneven = FramePacing.sixtyUneven(d.hz);
    int[] v = new int[KEYS.length];
    v[SCALE] = 0;  // Dynamic, up to the benchmark's levels
    boolean smooth = profile == HIGH ? b.cap60 >= 3 && !uneven : profile == MEDIUM && b.cap60 >= 4 && !uneven;
    v[SMOOTH] = smooth ? 1 : 0;
    int room = smooth ? b.cap60 : b.cap30;  // the headroom: the level held at the profile's rate
    int[] fx = effects(profile, room, screenLevel(d));
    v[SAMPLES] = clampSamples(fx[0], d.samplesMax);
    v[FXAA] = fx[1];
    v[ANISO] = clampAniso(fx[2], d.anisoMax);
    // the performance cores in every profile (the project owner's Retroid test, 11.11.2026: "Standard" left the
    // render thread on the middle cores, Dynamic settled at 2.5x instead of 3x..3.5x); a phone with only one kind
    // of core is not changed by it
    v[CORES] = 1;
    if (profile == LOW) {
      v[SKIP] = cpu ? 2 : 1;
      v[WEATHER] = 0; v[SPECIAL] = 0; v[PARTICLES] = 1; v[SHADOWS] = 0;
      v[VIEW] = cpu ? 2 : 1;
    } else {
      v[SKIP] = smooth ? 0 : 1;
      v[WEATHER] = 1; v[SPECIAL] = 1; v[PARTICLES] = 0; v[SHADOWS] = 1;
      v[VIEW] = 0;
    }
    return v;
  }

  /** {MSAA samples, FXAA, anisotropy} by the headroom `room` and the screen's level `screen` (halves; plan "Effekte
   *  nach Spielraum"). MSAA only where the chip holds more than the screen's level (the surplus room - screen): up to
   *  the screen's level the resolution itself is the better use (the project owner's Retroid, 11.11.2026: 3x held
   *  with 60 FPS on 1080 rows, 4x MSAA took Dynamic to 2x and the FPS to 39); FXAA where there is no MSAA. */
  static int[] effects(int profile, int room, int screen) {
    int surplus = room - screen;
    if (profile == HIGH) {
      int msaa = surplus >= 2 ? 4 : surplus >= 1 ? 2 : 1;
      return new int[] {msaa, msaa == 1 && room >= 3 ? 1 : 0, room >= 4 ? 16 : room >= 3 ? 8 : 1};
    }
    if (profile == MEDIUM) {
      int msaa = surplus >= 2 ? 2 : 1;
      return new int[] {msaa, msaa == 1 && room >= 4 ? 1 : 0, room >= 6 ? 8 : room >= 3 ? 4 : 1};
    }
    return new int[] {1, 0, 1};
  }

  static int clampSamples(int want, int max) {
    if (max <= 1) return 1;
    if (want >= 4 && max >= 4) return 4;
    return want >= 2 ? 2 : 1;
  }

  static int clampAniso(int want, int max) {
    if (max <= 1) return 1;
    int a = 1;
    while (a * 2 <= want && a * 2 <= max) a *= 2;
    return a;
  }

  /** The headroom the profile's values are measured against (halves): the level held at their rate. */
  static int headroom(int[] v, Device d, Bench measured) {
    Bench b = guess(d, measured);
    return v[SMOOTH] != 0 ? b.cap60 : b.cap30;
  }

  /** The benchmark's second pass took too much: one step less (MSAA 4x -> 2x -> FXAA -> off, then the texture filter
   *  16x -> 8x -> 4x); null when nothing is left to take. */
  static int[] stepBack(int[] v) {
    int[] w = v.clone();
    if (w[SAMPLES] >= 4) w[SAMPLES] = 2;
    else if (w[SAMPLES] == 2) { w[SAMPLES] = 1; w[FXAA] = 1; }
    else if (w[FXAA] != 0) w[FXAA] = 0;
    else if (w[ANISO] >= 16) w[ANISO] = 8;
    else if (w[ANISO] >= 8) w[ANISO] = 4;
    else return null;
    return w;
  }

  /** Did a window of the check hold the profile's rate? {seconds, game pictures, shown, weak seconds, levels}: on
   *  average 29 game pictures a second (with 60 FPS 57 shown), at most one weak second in twenty, and Dynamic not more
   *  than half a level under the headroom. */
  static boolean holds(int[] m, boolean sixty, int headroom) {
    if (m == null || m.length < 5 || m[0] < 10) return false;
    double s = m[0];
    if (m[1] / s < 29.0) return false;
    if (sixty && m[2] / s < 57.0) return false;
    if (m[3] > Math.max(1, m[0] / 20)) return false;
    return headroom < 2 || m[4] / s >= headroom - 1;
  }

  /** Does the suggestion's window `after` run at least as smoothly as the settings before (`before`), the same place,
   *  the same check? At most one picture a second fewer (game and shown), at most one weak second more in twenty,
   *  and Dynamic not more than half a level lower than before (or than the headroom, the lower of the two). The
   *  Retroid's check 11.11.2026: before 50.7 shown a second, 9 weak, 3.2x; FXAA 51.7, 6 weak, 3x: as smooth. */
  static boolean keepsUp(int[] after, int[] before, int headroom) {
    if (after == null || before == null || after.length < 5 || before.length < 5 || after[0] < 10 || before[0] < 10)
      return false;
    double a = after[0], b = before[0];
    if (after[1] / a < before[1] / b - 1.0) return false;
    if (after[2] / a < before[2] / b - 1.0) return false;
    if (after[3] / a > before[3] / b + 1.0 / 20) return false;
    double level = before[4] / b;
    if (headroom >= 2) level = Math.min(level, headroom);
    return after[4] / a >= level - 1.0;
  }

  // ---- the preferences -----------------------------------------------------------------------------------------

  /** The values as the preferences hold them (the menu's own readers for the old keys). */
  static int[] current(SharedPreferences g) {
    int[] v = new int[KEYS.length];
    v[SCALE] = CoreMenu.scaleHalves(g);
    v[SAMPLES] = g.getInt("samples", 1);
    v[FXAA] = g.getBoolean("fxaa", false) ? 1 : 0;
    v[ANISO] = g.getInt("anisotropy", 1);
    v[SMOOTH] = g.getBoolean("smooth60", false) ? 1 : 0;
    v[SKIP] = CoreMenu.frameSkipMode(g);
    v[CORES] = g.getInt("fastCores", 0);
    v[WEATHER] = g.getBoolean("weatherEffects", true) ? 1 : 0;
    v[SPECIAL] = g.getBoolean("specialEffects", true) ? 1 : 0;
    v[PARTICLES] = CoreBridge.particleEffects(g);
    v[SHADOWS] = g.getBoolean("shadowsLight", true) ? 1 : 0;
    v[VIEW] = CoreBridge.viewDistance(g);
    return v;
  }

  static void write(SharedPreferences.Editor e, String prefix, int[] v) {
    for (int i = 0; i < KEYS.length; i++) {
      if (BOOL[i]) e.putBoolean(prefix + KEYS[i], v[i] != 0);
      else e.putInt(prefix + KEYS[i], v[i]);
    }
    if (prefix.isEmpty()) e.remove("frameskip").remove("cpuSkip").remove("scale");
  }

  static int[] read(SharedPreferences g, String prefix) {
    if (!g.contains(prefix + KEYS[0])) return null;
    int[] v = new int[KEYS.length];
    for (int i = 0; i < KEYS.length; i++) {
      v[i] = BOOL[i] ? (g.getBoolean(prefix + KEYS[i], false) ? 1 : 0) : g.getInt(prefix + KEYS[i], 0);
    }
    return v;
  }

  static int active(SharedPreferences g) {
    int a = g.getInt(ACTIVE, CUSTOM);
    return a >= CUSTOM && a <= HIGH ? a : CUSTOM;
  }

  /** `profile` chosen: Custom's values kept when leaving it, brought back when choosing it. `values` the profile's
   *  (ignored for CUSTOM). Writes the preferences; the caller applies them. */
  static void choose(SharedPreferences g, int profile, int[] values) {
    int was = active(g);
    SharedPreferences.Editor e = g.edit();
    int[] now = current(g);
    if (was == CUSTOM && profile != CUSTOM) write(e, CUSTOM_PREFIX, now);
    int[] v = profile == CUSTOM ? read(g, CUSTOM_PREFIX) : values;
    if (v == null) v = now;  // Custom never saved: as it is
    write(e, "", v);
    write(e, APPLIED_PREFIX, v);
    e.putInt(ACTIVE, profile);
    e.apply();
  }

  /** A hand change of a profile's setting: Custom active, with the values as they are (returns the active one). */
  static int sync(SharedPreferences g) {
    int a = active(g);
    if (a == CUSTOM) return a;
    int[] applied = read(g, APPLIED_PREFIX);
    int[] now = current(g);
    if (applied != null && Arrays.equals(applied, now)) return a;
    SharedPreferences.Editor e = g.edit();
    write(e, CUSTOM_PREFIX, now);
    e.putInt(ACTIVE, CUSTOM);
    e.apply();
    return CUSTOM;
  }

  // ---- the device's data on Android ------------------------------------------------------------------------------

  /** Cores, the cores at the highest clock (within 10 %), the highest clock in MHz (cpufreq; 0 unknown). */
  static int[] cpu() {
    int cores = Runtime.getRuntime().availableProcessors(), max = 0;
    int[] mhz = new int[Math.max(cores, 1)];
    for (int i = 0; i < mhz.length; i++) {
      mhz[i] = readInt("/sys/devices/system/cpu/cpu" + i + "/cpufreq/cpuinfo_max_freq") / 1000;
      max = Math.max(max, mhz[i]);
    }
    int fast = 0;
    for (int m : mhz) if (max > 0 && m >= max * 0.9) fast++;
    return new int[] {cores, max > 0 ? fast : cores, max};
  }

  private static int readInt(String path) {
    File f = new File(path);
    if (!f.canRead()) return 0;
    try (BufferedReader r = new BufferedReader(new FileReader(f))) {
      String line = r.readLine();
      return line == null ? 0 : Integer.parseInt(line.trim());
    } catch (Exception e) {
      return 0;
    }
  }

  /** The chip's name out of the renderer's text ("…, Mali-G57 MC2 (…)…"), else the text's start. */
  static String gpuName(String info) {
    if (info == null) return "";
    java.util.regex.Matcher m = Pattern.compile("(Mali-[A-Z]?\\d+( MC\\d+| MP\\d+)?|Adreno( \\(TM\\))? \\d+|"
        + "PowerVR [A-Za-z0-9 -]+?(?=[,)]|$)|Immortalis-[A-Z]?\\d+( MC\\d+)?|Xclipse \\d+)").matcher(info);
    if (m.find()) return m.group(1).replace("(TM) ", "");
    String t = info.trim();
    return t.length() > 40 ? t.substring(0, 40) : t;
  }

  static String level(int halves) {
    return CoreMenu.scaleNumber(halves, Locale.getDefault()) + "×";
  }
}
