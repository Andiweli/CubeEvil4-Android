package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * Real-time shadows (debug builds only until after 1.0, decision of the project owner, 15.11.2026; RealtimeShadows): a
 * release build never gets them and sends the core the Potato mask as before; a debug build on a capable device gets
 * them by default, unless Potato's "Shadows &amp; light" or the menu's choice is off; weak devices, no benchmark, a
 * processor at its limit or the Vulkan renderer (not yet) do not; the menu row exists only where they are offered.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class RealtimeShadowsTest {
  private SharedPreferences g;
  // the project owner's Retroid Pocket Flip 2: Adreno 650, 8 cores (4 fast, 2.84 GHz), 8 GB; the benchmark 3x at 30
  // and 2.5x at 60 pictures a second, the game's thread at 27 %
  private static final PerfProfile.Device RETROID =
      new PerfProfile.Device("Qualcomm Adreno (TM) 650, OpenGL ES 3.2", 8, 4, 2841, 7600, 60.0, 4, 16, 1080);
  private static final PerfProfile.Bench MEASURED = new PerfProfile.Bench(6, 5, false, true, 27);
  // a Mali-G52 phone (the weak end of the testers' devices)
  private static final PerfProfile.Device MALI_G52 =
      new PerfProfile.Device("Mali-G52 MC2, OpenGL ES 3.2", 8, 2, 2000, 3800, 60.0, 4, 16, 720);

  @Before public void clean() {
    g = RuntimeEnvironment.getApplication().getSharedPreferences("graphics-test", Context.MODE_PRIVATE);
    g.edit().clear().commit();
    CoreBridge.shadowBit = 0;
  }

  @Test public void aReleaseBuildNeverGetsThem() {
    String[] why = new String[1];
    assertEquals(0, RealtimeShadows.level(false, g, RETROID, MEASURED, false, why));
    assertEquals("release build", why[0]);
    assertFalse(RealtimeShadows.offered(false, RETROID, MEASURED, false));
    // the core's Potato mask exactly as before: no bit 5 without the shadows
    g.edit().putBoolean("shadowsLight", false).putBoolean("weatherEffects", false).putInt("particleEffects", 2).commit();
    assertEquals(CoreBridge.potatoMask(g), CoreBridge.potatoSent(g));
    assertEquals(0, CoreBridge.potatoSent(g) & RealtimeShadows.CORE_BIT);
  }

  @Test public void aDebugBuildOnACapableDeviceGetsThemByDefault() {
    String[] why = new String[1];
    assertTrue(RealtimeShadows.offered(true, RETROID, MEASURED, false));
    assertEquals(1, RealtimeShadows.level(true, g, RETROID, MEASURED, false, why));
    assertTrue(why[0], why[0].startsWith("debug build, Qualcomm Adreno (TM) 650"));
    CoreBridge.shadowBit = RealtimeShadows.CORE_BIT;
    assertEquals(CoreBridge.potatoMask(g) | 32, CoreBridge.potatoSent(g));
  }

  @Test public void potatoShadowsOffOrTheMenusChoiceOffTurnThemOff() {
    String[] why = new String[1];
    g.edit().putBoolean("shadowsLight", false).commit();
    assertEquals(0, RealtimeShadows.level(true, g, RETROID, MEASURED, false, why));
    assertEquals("Potato: Shadows & light off", why[0]);
    g.edit().putBoolean("shadowsLight", true).putInt(RealtimeShadows.KEY, 0).commit();
    assertEquals(0, RealtimeShadows.level(true, g, RETROID, MEASURED, false, why));
    assertEquals("the menu's choice", why[0]);
    assertTrue(RealtimeShadows.offered(true, RETROID, MEASURED, false));  // the row stays, to switch them on again
  }

  @Test public void weakOrUnmeasuredDevicesNeverSeeThem() {
    assertNotNull(RealtimeShadows.refusal(true, MALI_G52, MEASURED, false));
    assertNotNull(RealtimeShadows.refusal(true, RETROID, new PerfProfile.Bench(0, 0, false, false), false));
    assertNotNull(RealtimeShadows.refusal(true, RETROID, new PerfProfile.Bench(5, 3, false, true, 27), false));
    assertNotNull(RealtimeShadows.refusal(true, RETROID, new PerfProfile.Bench(6, 5, false, true, 80), false));
    assertNotNull(RealtimeShadows.refusal(true, new PerfProfile.Device("", 8, 4, 2841, 7600, 60.0, 4, 16, 1080),
        MEASURED, false));
    PerfProfile.Device small = new PerfProfile.Device("Adreno (TM) 650", 8, 4, 2841, 3000, 60.0, 4, 16, 1080);
    assertNotNull(RealtimeShadows.refusal(true, small, MEASURED, false));
    assertEquals(RealtimeShadows.VULKAN_READY, RealtimeShadows.refusal(true, RETROID, MEASURED, true) == null);
  }

  /** The Performance page: the row "Real-time shadows · Characters" with its help only where they are offered. */
  @Test public void theMenuRowOnlyWhereOffered() throws Exception {
    for (boolean capable : new boolean[] {false, true}) {
      final PerfProfile.Device device = capable ? RETROID : MALI_G52;
      final int[] changed = {0};
      CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
          new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
            if (method.getName().equals("perfDevice")) return device;
            if (method.getName().equals("shadowsChanged")) changed[0]++;
            return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0
                : method.getReturnType() == float.class ? (Object) 1f
                : method.getReturnType() == double.class ? (Object) 60.0 : null;
          });
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        // a debug build (Android Studio's coreDebug is debuggable)
        activity.getApplicationInfo().flags |= android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE;
        SharedPreferences graphics = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0);
        graphics.edit().clear().putInt(CoreMenu.DYN_CAP_30, 6).putInt(CoreMenu.DYN_CAP_60, 5)
            .putInt(PerfProfile.BENCH_CPU_LOAD, 27).apply();
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
        show.setAccessible(true);
        show.invoke(menu, 4, false);  // PERFORMANCE
        java.lang.reflect.Field rootField = CoreMenu.class.getDeclaredField("root");
        rootField.setAccessible(true);
        View root = (View) rootField.get(menu);
        String label = activity.getString(R.string.ingame_rt_shadows);
        TextView row = find(root, label);
        assertEquals("the row " + (capable ? "on a capable device" : "on a weak device"), capable, row != null);
        if (capable) {
          assertTrue(row.getText().toString(), row.getText().toString().contains(activity.getString(R.string.ingame_rt_shadows_figures)));
          row.performClick();
          assertEquals(0, RealtimeShadows.choice(graphics));
          assertEquals(1, changed[0]);
          assertTrue(row.getText().toString().contains(activity.getString(R.string.ingame_off)));
        }
      }
    }
  }

  private static TextView find(View v, String text) {
    if (v instanceof TextView && ((TextView) v).getText().toString().startsWith(text)) return (TextView) v;
    if (v instanceof ViewGroup) {
      ViewGroup group = (ViewGroup) v;
      for (int i = 0; i < group.getChildCount(); i++) {
        TextView t = find(group.getChildAt(i), text);
        if (t != null) return t;
      }
    }
    return null;
  }
}
