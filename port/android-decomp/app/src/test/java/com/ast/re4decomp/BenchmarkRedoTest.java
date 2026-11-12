package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowDialog;
import org.robolectric.shadows.ShadowLooper;

/**
 * 0.99.12: a benchmark of before (no processor share) is made again once after the update, as on the first start
 * (decision of the project owner, 12.11.2026): PerfProfile.redoDue / scheduleRedo / redo and popup 1 with the reason.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class BenchmarkRedoTest {
  private static SharedPreferences prefs(Context c, String name) {
    SharedPreferences g = c.getSharedPreferences(name, Context.MODE_PRIVATE);
    g.edit().clear().commit();
    return g;
  }

  @Test public void anOldBenchmarkIsRedoneOnce() {
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      Activity a = controller.get();
      // a new installation's first start: nothing to redo (the first start's own benchmark comes)
      SharedPreferences fresh = prefs(a, "redoFresh");
      fresh.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_PENDING).commit();
      assertFalse(PerfProfile.redoDue(fresh));
      assertFalse(PerfProfile.redo(fresh));
      // no benchmark at all (an update that put the question off): nothing to redo
      SharedPreferences none = prefs(a, "redoNone");
      none.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE).commit();
      assertFalse(PerfProfile.redoDue(none));
      // a benchmark of 0.99.10/11 (date, levels, no processor share): due, scheduled as the first start's
      SharedPreferences old = prefs(a, "redoOld");
      old.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE).putLong(PerfProfile.BENCH_DATE, 1794486713147L)
          .putInt(CoreMenu.DYN_CAP_30, 3).putInt(CoreMenu.DYN_CAP_60, 2).putBoolean(PerfProfile.BENCH_CPU, false)
          .putInt("scaleHalves", 3).commit();
      assertTrue(PerfProfile.redoDue(old));
      assertTrue(PerfProfile.scheduleRedo(old));
      assertEquals(PerfProfile.FIRST_PENDING, old.getInt(PerfProfile.FIRST, -1));
      assertEquals(PerfProfile.REDO_CPU, old.getInt(PerfProfile.BENCH_REDO, 0));
      assertTrue("popup 1 shows the reason", PerfProfile.redo(old));
      assertFalse("not twice in one start", PerfProfile.scheduleRedo(old));
      assertEquals("the player's resolution untouched", 3, old.getInt("scaleHalves", -1));
      // put off with B / Back: FIRST stays pending (offered at the next start), not scheduled anew
      assertFalse(PerfProfile.redoDue(old));
      // the redo ran: the processor share stored, the first start's mark gone: never again
      old.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE).putInt(PerfProfile.BENCH_CPU_LOAD, 76)
          .putLong(PerfProfile.BENCH_DATE, 1794500000000L).commit();
      assertFalse(PerfProfile.redoDue(old));
      // once a device even if the share should be missing again
      old.edit().remove(PerfProfile.BENCH_CPU_LOAD).commit();
      assertFalse(PerfProfile.redoDue(old));
      // a benchmark made with 0.99.12 (the share there, -1 counts too): nothing to redo
      SharedPreferences now = prefs(a, "redoNow");
      now.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE).putLong(PerfProfile.BENCH_DATE, 1L)
          .putInt(PerfProfile.BENCH_CPU_LOAD, -1).commit();
      assertFalse(PerfProfile.redoDue(now));
    }
  }

  /** Popup 1: the redo has its own title and the reason line, the first start its welcome; B / Back keeps it due. */
  @Test public void popupOneSaysWhy() {
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      Activity a = controller.get();
      SharedPreferences g = prefs(a, "redoPopup");
      g.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE).putLong(PerfProfile.BENCH_DATE, 1794486713147L)
          .putInt(CoreMenu.DYN_CAP_30, 3).commit();
      assertTrue(PerfProfile.scheduleRedo(g));
      BenchmarkFlow flow = new BenchmarkFlow(host(a, g));
      assertTrue(flow.firstDue());
      flow.offerFirst();
      ShadowLooper.idleMainLooper();
      List<String> texts = texts(ShadowDialog.getLatestDialog());
      assertTrue(texts.toString(), texts.contains(a.getString(R.string.ingame_bench_redo)));
      assertTrue(texts.toString(), texts.contains(a.getString(R.string.ingame_bench_redo_text)));
      assertFalse(texts.contains(a.getString(R.string.ingame_bench_welcome)));
      assertTrue(texts.contains(a.getString(R.string.ingame_bench_text)));
      ShadowDialog.getLatestDialog().cancel();  // B / Back
      ShadowLooper.idleMainLooper();
      assertEquals("offered again at the next start", PerfProfile.FIRST_PENDING, g.getInt(PerfProfile.FIRST, -1));
      // a new installation: the welcome, no reason
      SharedPreferences f = prefs(a, "redoPopupFresh");
      f.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_PENDING).commit();
      BenchmarkFlow first = new BenchmarkFlow(host(a, f));
      first.offerFirst();
      ShadowLooper.idleMainLooper();
      List<String> welcome = texts(ShadowDialog.getLatestDialog());
      assertTrue(welcome.contains(a.getString(R.string.ingame_bench_welcome)));
      assertFalse(welcome.contains(a.getString(R.string.ingame_bench_redo_text)));
    }
  }

  private static BenchmarkFlow.Host host(Activity a, SharedPreferences g) {
    return new BenchmarkFlow.Host() {
      @Override public Activity activity() { return a; }
      @Override public SharedPreferences graphics() { return g; }
      @Override public void applyOptions() {}
      @Override public void applyClean() {}
      @Override public void applyValues(int[] values) {}
      @Override public void progress(String text) {}
      @Override public void mute(boolean on) {}
      @Override public void hold(boolean on) {}
      @Override public PerfProfile.Device device() {
        return new PerfProfile.Device("Mali-G52 MC2", 8, 2, 2000, 3654, 60, 4, 1, 720);
      }
    };
  }

  private static List<String> texts(Dialog d) {
    List<String> out = new ArrayList<>();
    assertNotNull("a dialog", d);
    collect(d.getWindow().getDecorView(), out);
    return out;
  }

  private static void collect(View v, List<String> out) {
    if (v instanceof TextView) out.add(((TextView) v).getText().toString());
    if (v instanceof ViewGroup) {
      ViewGroup g = (ViewGroup) v;
      for (int i = 0; i < g.getChildCount(); i++) collect(g.getChildAt(i), out);
    }
  }
}
