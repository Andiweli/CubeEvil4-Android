package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * The performance profiles and the benchmark's recommendation (0.99.10, port/docs/leistungsprofil-plan.md), with the
 * devices of the user reports as cases (E:\Development\Android\reports: their gpu, cores, memory, display and the
 * levels their benchmark found).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class PerfProfileTest {
  private static PerfProfile.Device dev(String gpu, int cores, int fast, int mhz, int ramMb, double hz) {
    return new PerfProfile.Device(gpu, cores, fast, mhz, ramMb, hz, 4, 16);
  }
  /** With the screen's rows of a 16:9 picture (min(height, width * 9 / 16)). */
  private static PerfProfile.Device dev(String gpu, int cores, int fast, int mhz, int ramMb, double hz, int rows) {
    return new PerfProfile.Device(gpu, cores, fast, mhz, ramMb, hz, 4, 16, rows);
  }
  private static PerfProfile.Bench bench(int c30, int c60) { return new PerfProfile.Bench(c30, c60, false, true); }
  private static final PerfProfile.Bench NONE = new PerfProfile.Bench(0, 0, false, false);

  @Test public void bloomOnlyOnStrongDevices() {
    // 0.99.13: "Light" for High where the chip holds more than the screen's level (as MSAA), never for Medium or
    // Low, never with the processor near its end; off by default (an older saved profile reads 0 there)
    PerfProfile.Device retroid = dev("OpenGL ES 3.2, Adreno (TM) 650", 8, 1, 2841, 7700, 60, 1080);
    assertEquals(1, PerfProfile.values(PerfProfile.HIGH, retroid, bench(8, 8))[PerfProfile.BLOOM]);
    assertEquals(0, PerfProfile.values(PerfProfile.MEDIUM, retroid, bench(8, 8))[PerfProfile.BLOOM]);
    assertEquals(0, PerfProfile.values(PerfProfile.LOW, retroid, bench(8, 8))[PerfProfile.BLOOM]);
    // the project owner's Retroid (4x / 3x): 3x is its screen's level, nothing to spare
    assertEquals(0, PerfProfile.values(PerfProfile.HIGH, retroid, bench(8, 6))[PerfProfile.BLOOM]);
    assertEquals(0, PerfProfile.bloomFor(PerfProfile.HIGH, 8, 6, true));
    assertEquals(1, PerfProfile.bloomFor(PerfProfile.HIGH, 7, 6, false));
    assertEquals(0, PerfProfile.bloomFor(PerfProfile.HIGH, 6, 6, false));
    assertEquals(CoreMenu.BLOOM_LEVEL, PerfProfile.KEYS[PerfProfile.BLOOM]);
  }

  @Test public void theReportsDevicesGetTheirProfiles() {
    // POCO X7 Pro (Mali-G720 MC7, 3.5x / 2.5x, 120 Hz, 2712 x 1220): High, 60 FPS; 2.5x is below its screen's 3.5x:
    // FXAA, no MSAA, and 16x
    PerfProfile.Device poco = dev("Vulkan 1.3, Mali-G720 MC7", 8, 4, 3250, 11312, 120, 1220);
    assertEquals(PerfProfile.HIGH, PerfProfile.recommend(poco, bench(7, 5)));
    int[] high = PerfProfile.values(PerfProfile.HIGH, poco, bench(7, 5));
    assertEquals(1, high[PerfProfile.SMOOTH]);
    assertEquals(1, high[PerfProfile.SAMPLES]);
    assertEquals(1, high[PerfProfile.FXAA]);
    assertEquals(16, high[PerfProfile.ANISO]);
    assertEquals(0, high[PerfProfile.SKIP]);
    // Retroid Pocket Flip 2 (Adreno 650, 2.5x / 1.5x, 1080 rows): High, 60 FPS, FXAA and 8x
    PerfProfile.Device retroid = dev("OpenGL ES 3.2, Adreno (TM) 650", 8, 1, 2841, 7700, 60, 1080);
    assertEquals(PerfProfile.HIGH, PerfProfile.recommend(retroid, bench(5, 3)));
    int[] r = PerfProfile.values(PerfProfile.HIGH, retroid, bench(5, 3));
    assertEquals(1, r[PerfProfile.SMOOTH]);
    assertEquals(1, r[PerfProfile.SAMPLES]);
    assertEquals(1, r[PerfProfile.FXAA]);
    assertEquals(8, r[PerfProfile.ANISO]);
    // the project owner's Retroid 11.11.2026 (Vulkan, 4x / 3x): 3x is its screen's level, so no MSAA (4x MSAA there
    // took Dynamic to 2x and the shown FPS to 39): FXAA and 16x
    int[] owner = PerfProfile.values(PerfProfile.HIGH, retroid, bench(8, 6));
    assertEquals(1, owner[PerfProfile.SAMPLES]);
    assertEquals(1, owner[PerfProfile.FXAA]);
    assertEquals(16, owner[PerfProfile.ANISO]);
    // Infinix X6882 (Mali-G57 MC2, 2x / 1x, 720 rows): Medium, 30 FPS with frame skip, FXAA and 4x
    PerfProfile.Device infinix = dev("Mali-G57 MC2", 8, 2, 2200, 5673, 60, 720);
    assertEquals(PerfProfile.MEDIUM, PerfProfile.recommend(infinix, bench(4, 2)));
    int[] m = PerfProfile.values(PerfProfile.MEDIUM, infinix, bench(4, 2));
    assertEquals(0, m[PerfProfile.SMOOTH]);
    assertEquals(1, m[PerfProfile.SKIP]);
    assertEquals(1, m[PerfProfile.FXAA]);
    assertEquals(4, m[PerfProfile.ANISO]);
    assertEquals(1, m[PerfProfile.WEATHER]);
    // moto g15 (Mali-G52 MC2, 1x / 1x): Low
    PerfProfile.Device moto = dev("Mali-G52 MC2", 8, 2, 2000, 3689, 60);
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(moto, bench(2, 2)));
    // Galaxy A13 (only small cores, eight at 2.0 GHz): Low with the game's own frame skip and a short view
    PerfProfile.Device a13 = dev("Mali-G52", 8, 8, 2002, 3706, 60);
    assertTrue(PerfProfile.cpuWeak(a13, NONE));
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(a13, bench(4, 2)));
    int[] low = PerfProfile.values(PerfProfile.LOW, a13, bench(4, 2));
    assertEquals(2, low[PerfProfile.SKIP]);
    assertEquals(2, low[PerfProfile.VIEW]);
    assertEquals(1, low[PerfProfile.CORES]);
    assertEquals(0, low[PerfProfile.WEATHER]);
    assertEquals(1, low[PerfProfile.PARTICLES]);
    // less than 3 GB (Mobicel P11 Plus, 1.9 GB): Low
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(dev("Mali-G57", 8, 2, 2000, 1892, 60), bench(6, 4)));
    // the processor sign of the benchmark: Low
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(retroid, new PerfProfile.Bench(5, 3, true, true)));
  }

  /** 0.99.12: the game's thread at the benchmark's place (report 09912-benchmark-too-good, Xiaomi 25078RA3EL "dew",
   *  Helio G85 with two fast cores, Mali-G52 MC2, 720 x 1600 at 60 Hz: 30 FPS held at exactly 1.5x, 60 not even at
   *  1x, the game's thread 25.4 ms of 33.4 = 76 %; rated Medium, small frame drops in the village). */
  @Test public void theProcessorAtThePlaceDecides() {
    PerfProfile.Device dew = dev("Vulkan 1.3.278 (used as 1.1), Mali-G52 MC2", 8, 2, 2000, 3654, 60.1, 720);
    // before 0.99.12 (no load known): Medium with frame skip Normal, as the user got it
    PerfProfile.Bench old = new PerfProfile.Bench(3, 2, false, true);
    assertEquals(PerfProfile.MEDIUM, PerfProfile.recommend(dew, old));
    assertEquals(1, PerfProfile.values(PerfProfile.MEDIUM, dew, old)[PerfProfile.SKIP]);
    // 76 %: the processor holds it back: Low with the game's own frame skip and a short view
    PerfProfile.Bench busy = new PerfProfile.Bench(3, 2, false, true, 76);
    assertTrue(PerfProfile.cpuWeak(dew, busy));
    assertFalse(PerfProfile.cpuTight(dew, busy));
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(dew, busy));
    int[] low = PerfProfile.values(PerfProfile.LOW, dew, busy);
    assertEquals(2, low[PerfProfile.SKIP]);
    assertEquals(2, low[PerfProfile.VIEW]);
    assertEquals(R.string.ingame_limit_cpu, BenchmarkFlow.limitText(dew, busy));
    // Medium chosen by hand on that phone: Aggressive frame skip, a medium view, no 60 FPS
    int[] medium = PerfProfile.values(PerfProfile.MEDIUM, dew, busy);
    assertEquals(2, medium[PerfProfile.SKIP]);
    assertEquals(1, medium[PerfProfile.VIEW]);
    assertEquals(0, medium[PerfProfile.SMOOTH]);
    // the same phone a little cooler (70 %, near its end): one lower than the graphics chip allows, Medium -> Low
    PerfProfile.Bench tight = new PerfProfile.Bench(3, 2, false, true, 70);
    assertFalse(PerfProfile.cpuWeak(dew, tight));
    assertTrue(PerfProfile.cpuTight(dew, tight));
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(dew, tight));
    assertEquals(2, PerfProfile.values(PerfProfile.LOW, dew, tight)[PerfProfile.SKIP]);
    assertEquals("Low keeps the medium view when only tight", 1, PerfProfile.values(PerfProfile.LOW, dew, tight)[PerfProfile.VIEW]);
    assertEquals(R.string.ingame_limit_both, BenchmarkFlow.limitText(dew, tight));
    // a phone whose graphics chip allows High, the processor near its end: Medium, without 60 FPS, Aggressive skip
    PerfProfile.Device mid = dev("Mali-G57 MC2", 8, 2, 2200, 5673, 60, 720);
    PerfProfile.Bench highTight = new PerfProfile.Bench(5, 3, false, true, 65);
    assertEquals(PerfProfile.MEDIUM, PerfProfile.recommend(mid, highTight));
    int[] h = PerfProfile.values(PerfProfile.HIGH, mid, highTight);
    assertEquals(0, h[PerfProfile.SMOOTH]);
    assertEquals(2, h[PerfProfile.SKIP]);
    // with room to spare (50 %): as before, Medium with frame skip Normal
    PerfProfile.Bench calm = new PerfProfile.Bench(3, 2, false, true, 50);
    assertEquals(PerfProfile.MEDIUM, PerfProfile.recommend(dew, calm));
    assertEquals(1, PerfProfile.values(PerfProfile.MEDIUM, dew, calm)[PerfProfile.SKIP]);
    assertEquals(0, PerfProfile.values(PerfProfile.MEDIUM, dew, calm)[PerfProfile.VIEW]);
    // the Retroid at the dock (about 27 %): High with 60 FPS, unchanged
    PerfProfile.Device retroid = dev("OpenGL ES 3.2, Adreno (TM) 650", 8, 1, 2841, 7700, 60, 1080);
    PerfProfile.Bench r = new PerfProfile.Bench(8, 6, false, true, 27);
    assertEquals(PerfProfile.HIGH, PerfProfile.recommend(retroid, r));
    assertEquals(1, PerfProfile.values(PerfProfile.HIGH, retroid, r)[PerfProfile.SMOOTH]);
    // the preferences keep the load with the levels
    SharedPreferences g = RuntimeEnvironment.getApplication().getSharedPreferences("perfLoadTest", Context.MODE_PRIVATE);
    g.edit().clear().putInt(CoreMenu.DYN_CAP_30, 3).putInt(CoreMenu.DYN_CAP_60, 2)
        .putInt(PerfProfile.BENCH_CPU_LOAD, 76).commit();
    PerfProfile.Bench stored = PerfProfile.Bench.of(g);
    assertEquals(76, stored.cpuLoad);
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(dew, stored));
    g.edit().remove(PerfProfile.BENCH_CPU_LOAD).commit();
    assertEquals(-1, PerfProfile.Bench.of(g).cpuLoad);
  }

  @Test public void aNinetyHertzScreenKeepsThirty() {
    // Galaxy A24 class with a strong chip on 90 Hz: High without 60 FPS, effects by the 30 FPS level (3x)
    PerfProfile.Device d = dev("Adreno (TM) 735", 8, 1, 2900, 7700, 90, 720);
    assertEquals(PerfProfile.HIGH, PerfProfile.recommend(d, bench(6, 4)));
    int[] v = PerfProfile.values(PerfProfile.HIGH, d, bench(6, 4));
    assertEquals(0, v[PerfProfile.SMOOTH]);
    assertEquals(1, v[PerfProfile.SKIP]);
    assertEquals("3x on a 720-row screen (2x): MSAA 4x", 4, v[PerfProfile.SAMPLES]);
    assertEquals(6, PerfProfile.headroom(v, d, bench(6, 4)));
    // the same on 1080 rows (3x): no surplus, FXAA instead
    int[] fhd = PerfProfile.values(PerfProfile.HIGH, dev("Adreno (TM) 735", 8, 1, 2900, 7700, 90, 1080), bench(6, 4));
    assertEquals(1, fhd[PerfProfile.SAMPLES]);
    assertEquals(1, fhd[PerfProfile.FXAA]);
    // half a level of surplus: MSAA 2x
    int[] half = PerfProfile.values(PerfProfile.HIGH, dev("Adreno (TM) 735", 8, 1, 2900, 7700, 90, 1080), bench(7, 4));
    assertEquals(2, half[PerfProfile.SAMPLES]);
    assertEquals(0, half[PerfProfile.FXAA]);
  }

  @Test public void theScreensLevel() {
    assertEquals(6, PerfProfile.screenLevel(dev("x", 8, 1, 2000, 4000, 60, 1080)));
    assertEquals(7, PerfProfile.screenLevel(dev("x", 8, 1, 2000, 4000, 60, 1220)));
    assertEquals(4, PerfProfile.screenLevel(dev("x", 8, 1, 2000, 4000, 60, 720)));
    assertEquals(8, PerfProfile.screenLevel(dev("x", 8, 1, 2000, 4000, 60, 1600)));
    assertEquals("unknown", 6, PerfProfile.screenLevel(dev("x", 8, 1, 2000, 4000, 60)));
  }

  @Test public void everyProfileUsesThePerformanceCores() {
    PerfProfile.Device d = dev("Adreno (TM) 650", 8, 1, 2841, 7700, 60, 1080);
    for (int p = PerfProfile.LOW; p <= PerfProfile.HIGH; p++)
      assertEquals(1, PerfProfile.values(p, d, bench(8, 6))[PerfProfile.CORES]);
  }

  @Test public void aSuggestionKeepsUpWithTheSettingsBefore() {
    // the project owner's Retroid check 11.11.2026, 20 s windows: {seconds, game pictures, shown, weak, levels}
    int[] before = {20, 602, 1014, 9, 129};  // 30.1 / 50.7, 9 weak, 6.47 halves
    int[] fxaa = {20, 602, 1034, 6, 120};    // 30.1 / 51.7, 6 weak, 6.00
    int[] msaa4 = {20, 542, 776, 13, 88};    // 27.1 / 38.8, 13 weak, 4.40
    int[] msaa2 = {20, 588, 644, 20, 98};    // 29.4 / 32.2, 20 weak, 4.90
    assertFalse(PerfProfile.holds(fxaa, true, 6));
    assertTrue("FXAA as smooth as before", PerfProfile.keepsUp(fxaa, before, 6));
    assertFalse(PerfProfile.keepsUp(msaa4, before, 6));
    assertFalse(PerfProfile.keepsUp(msaa2, before, 6));
    assertFalse("one picture a second less than before at most",
        PerfProfile.keepsUp(new int[] {20, 602, 990, 9, 129}, before, 6));
    assertFalse("Dynamic more than half a level lower",
        PerfProfile.keepsUp(new int[] {20, 602, 1014, 9, 98}, before, 6));
    assertFalse("too short", PerfProfile.keepsUp(new int[] {5, 150, 250, 0, 30}, before, 6));
  }

  @Test public void withoutABenchmarkTheChipDecides() {
    assertEquals(PerfProfile.LOW, PerfProfile.recommend(dev("Mali-G57 MC2", 8, 2, 2200, 3681, 90), NONE));
    assertEquals(PerfProfile.HIGH, PerfProfile.recommend(dev("Adreno (TM) 650", 8, 1, 2841, 7700, 60), NONE));
    assertEquals(5, PerfProfile.benchTop(dev("Mali-G52 MC2", 8, 2, 2000, 3700, 60)));
    assertEquals(8, PerfProfile.benchTop(dev("Adreno (TM) 650", 8, 1, 2841, 7700, 60)));
  }

  @Test public void theDeviceLimitsTheEffects() {
    PerfProfile.Device d = new PerfProfile.Device("Mali-G720", 8, 4, 3250, 11000, 60, 2, 1);
    int[] v = PerfProfile.values(PerfProfile.HIGH, d, bench(8, 8));
    assertEquals(2, v[PerfProfile.SAMPLES]);
    assertEquals(1, v[PerfProfile.ANISO]);
  }

  @Test public void stepBackTakesTheDearestFirst() {
    int[] v = PerfProfile.values(PerfProfile.HIGH, dev("Adreno 740", 8, 1, 3200, 12000, 60), bench(8, 8));
    assertEquals(4, v[PerfProfile.SAMPLES]);
    int[][] steps = new int[6][];
    int[] w = v;
    int n = 0;
    while ((w = PerfProfile.stepBack(w)) != null) steps[n++] = w;
    assertEquals(5, n);
    assertEquals(2, steps[0][PerfProfile.SAMPLES]);
    assertEquals(1, steps[1][PerfProfile.SAMPLES]);
    assertEquals(1, steps[1][PerfProfile.FXAA]);
    assertEquals(0, steps[2][PerfProfile.FXAA]);
    assertEquals(8, steps[3][PerfProfile.ANISO]);
    assertEquals(4, steps[4][PerfProfile.ANISO]);
  }

  @Test public void aWindowHoldsOrNot() {
    assertTrue(PerfProfile.holds(new int[] {20, 600, 600, 0, 100}, false, 5));
    assertTrue(PerfProfile.holds(new int[] {20, 600, 1200, 1, 80}, true, 5));
    assertFalse("Dynamic more than half a level under the headroom",
        PerfProfile.holds(new int[] {20, 600, 600, 0, 78}, false, 5));
    assertFalse("60 FPS not shown", PerfProfile.holds(new int[] {20, 600, 1000, 0, 100}, true, 5));
    assertFalse("two weak seconds in twenty", PerfProfile.holds(new int[] {20, 600, 600, 2, 100}, false, 5));
    assertFalse("too short", PerfProfile.holds(new int[] {5, 150, 150, 0, 25}, false, 5));
  }

  @Test public void customIsKeptAndAHandChangeMakesItCustom() {
    SharedPreferences g = RuntimeEnvironment.getApplication().getSharedPreferences("perf-test", Context.MODE_PRIVATE);
    g.edit().clear().putInt("samples", 2).putInt("anisotropy", 16).putBoolean("smooth60", true).putInt("scaleHalves", 6)
        .commit();
    int[] mine = PerfProfile.current(g);
    PerfProfile.Device d = dev("Mali-G57 MC2", 8, 2, 2200, 5673, 60);
    int[] low = PerfProfile.values(PerfProfile.LOW, d, bench(4, 2));
    PerfProfile.choose(g, PerfProfile.LOW, low);
    assertEquals(PerfProfile.LOW, PerfProfile.active(g));
    assertArrayEquals(low, PerfProfile.current(g));
    assertEquals(PerfProfile.LOW, PerfProfile.sync(g));
    PerfProfile.choose(g, PerfProfile.MEDIUM, PerfProfile.values(PerfProfile.MEDIUM, d, bench(4, 2)));
    PerfProfile.choose(g, PerfProfile.CUSTOM, null);
    assertEquals(PerfProfile.CUSTOM, PerfProfile.active(g));
    assertArrayEquals("Custom's values come back", mine, PerfProfile.current(g));
    // a profile, then one of its settings by hand: Custom, with that value
    PerfProfile.choose(g, PerfProfile.HIGH, PerfProfile.values(PerfProfile.HIGH, d, bench(4, 2)));
    g.edit().putInt("anisotropy", 2).commit();
    assertEquals(PerfProfile.CUSTOM, PerfProfile.sync(g));
    assertEquals(2, PerfProfile.read(g, PerfProfile.CUSTOM_PREFIX)[PerfProfile.ANISO]);
    // a setting outside the profiles (the picture format) changes nothing
    PerfProfile.choose(g, PerfProfile.LOW, low);
    g.edit().putInt("aspect", 5).commit();
    assertEquals(PerfProfile.LOW, PerfProfile.sync(g));
  }

  @Test public void theChipsNameFromTheRenderersText() {
    assertEquals("Mali-G57 MC2", PerfProfile.gpuName(
        "Device and driver: Vulkan 1.3.303 (used as 1.1), Mali-G57 MC2 (vendor 0x13b5, device 0x90930010)"));
    assertEquals("Adreno 650", PerfProfile.gpuName("OpenGL ES 3.2 V@0502.0, Adreno (TM) 650"));
  }
}
