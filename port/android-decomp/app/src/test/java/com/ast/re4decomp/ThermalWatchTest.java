package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowLog;

/** ThermalWatch (0.99.15): Android's thermal status and headroom into the log and the report, from a fake source. */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class ThermalWatchTest {
  static final class Fake implements ThermalWatch.Source {
    int status = 0;
    float headroom = 0.3f;
    int asked;

    @Override public int status() { return status; }

    @Override public float headroom(int forecastSeconds) {
      asked++;
      return headroom;
    }
  }

  @Before public void clearLog() { ShadowLog.clear(); }

  private static int lines(String part) {
    int n = 0;
    for (ShadowLog.LogItem item : ShadowLog.getLogsForTag(ThermalWatch.TAG)) if (item.msg.contains(part)) n++;
    return n;
  }

  @Test public void statusAtTheStartAndAtEachChange() {
    Fake f = new Fake();
    ThermalWatch w = new ThermalWatch(f, 0);
    w.start(0);
    assertEquals(1, lines("thermal: status none at the start"));
    w.statusChanged(0, 1000);
    assertEquals("no line without a change", 1, lines("thermal: status"));
    w.statusChanged(2, 125_000);
    assertEquals(1, lines("thermal: status moderate after 2:05"));
    w.statusChanged(1, 200_000);
    String r = w.report(300_000);
    assertTrue(r, r.contains("Thermal status now: light, the highest since the start: moderate"));
    assertTrue(r, r.contains("+2:05  status moderate"));
    assertTrue(r, r.contains("Game running for 5:00"));
  }

  @Test public void theHeadroomEveryTenSecondsLoggedWhenItMoves() {
    Fake f = new Fake();
    ThermalWatch w = new ThermalWatch(f, 0);
    w.start(0);
    w.tick(0);
    assertEquals(1, f.asked);
    assertEquals(1, lines("thermal: headroom 0.30"));
    w.tick(5_000);
    assertEquals("not before 10 s (Android allows about one call a second)", 1, f.asked);
    f.headroom = 0.33f;
    w.tick(10_000);
    assertEquals(2, f.asked);
    assertEquals("a small move: no line", 1, lines("thermal: headroom"));
    f.headroom = 0.36f;
    w.tick(20_000);
    assertEquals("0.06 from the last line: a line", 1, lines("thermal: headroom 0.36"));
    // hot: once a minute even when it stays
    f.headroom = 0.8f;
    w.tick(30_000);
    w.tick(40_000);
    w.tick(80_000);
    assertEquals(1, lines("thermal: headroom 0.80"));
    w.tick(90_000);
    assertEquals("hot: again after a minute", 2, lines("thermal: headroom 0.80"));
    String r = w.report(95_000);
    assertTrue(r, r.contains("last 0.80, highest 0.80"));
    assertTrue(r, r.contains("+0:20  headroom 0.36  status none"));
  }

  @Test public void unknownHeadroomAndOldAndroid() {
    Fake f = new Fake();
    f.status = -1;
    f.headroom = Float.NaN;
    ThermalWatch w = new ThermalWatch(f, 0);
    w.start(0);
    assertEquals(1, lines("thermal: status unknown at the start (Android 9 or older: not known)"));
    w.tick(0);
    w.tick(10_000);
    assertEquals(0, lines("thermal: headroom"));
    String r = w.report(20_000);
    assertTrue(r, r.contains("(none: Android 10 or older, or the device has no headroom)"));
    assertTrue(r, r.contains("last unknown, highest unknown"));
  }

  @Test public void theReportKeepsFifteenMinutes() {
    Fake f = new Fake();
    ThermalWatch w = new ThermalWatch(f, 0);
    w.start(0);
    for (int i = 0; i < 200; i++) {
      f.headroom = 0.2f + (i % 3) * 0.01f;
      w.tick(i * ThermalWatch.EVERY_MS);
    }
    String r = w.report(200 * ThermalWatch.EVERY_MS);
    int readings = r.split("  headroom ", -1).length - 1;
    assertEquals(ThermalWatch.KEEP, readings);
    assertFalse("the oldest are gone", r.contains("+0:00  headroom"));
  }

  @Test public void namesAndTimes() {
    assertEquals("severe", ThermalWatch.statusName(3));
    assertEquals("shutdown", ThermalWatch.statusName(6));
    assertEquals("unknown", ThermalWatch.statusName(-1));
    assertEquals("0:00", ThermalWatch.minutes(-5));
    assertEquals("61:01", ThermalWatch.minutes(3_661_000));
    assertEquals("unknown", ThermalWatch.headroomText(Float.NaN));
    assertEquals("0.95", ThermalWatch.headroomText(0.951f));
  }

  @Test public void theDevicesWatchStartsAndStops() {
    // Robolectric's PowerManager (Android 13): status none; the listener comes and goes without an error
    ThermalWatch w = ThermalWatch.forDevice(org.robolectric.RuntimeEnvironment.getApplication(), 0);
    w.start(0);
    w.tick(0);
    w.stop();
    List<ShadowLog.LogItem> items = ShadowLog.getLogsForTag(ThermalWatch.TAG);
    assertFalse(items.isEmpty());
    assertTrue(w.report(1000).startsWith("Thermal status now: "));
  }
}
