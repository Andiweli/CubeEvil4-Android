package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

/**
 * Real-time shadows (RealtimeShadows; until 16.11.2026 debug builds only, since then every build, decisions of the
 * project owner): debug and release builds alike always show the row (the project owner, 16.11.2026: "Yes, always show
 * it"); until the player chooses, a capable device has them ("Characters and objects") and any other has "Off" with a
 * note why; the player's choice wins on any device; Potato's "Shadows &amp; light" off turns them off; Off sends the
 * core the Potato mask as before.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class RealtimeShadowsTest {
  private SharedPreferences g;
  // the project owner's Retroid Pocket Flip 2: Adreno 650, 8 cores (4 fast, 2.84 GHz), 8 GB; the benchmark 3x at 30
  // and 2.5x at 60 pictures a second, the game's thread at 27 %
  private static final PerfProfile.Device RETROID =
      new PerfProfile.Device("Qualcomm Adreno (TM) 650, OpenGL ES 3.2", 8, 4, 2841, 7600, 60.0, 4, 16, 1080);
  private static final PerfProfile.Bench MEASURED = new PerfProfile.Bench(6, 5, false, true, 27);
  private static final PerfProfile.Bench NONE = new PerfProfile.Bench(0, 0, false, false);
  // a Mali-G52 phone (the weak end of the testers' devices)
  private static final PerfProfile.Device MALI_G52 =
      new PerfProfile.Device("Mali-G52 MC2, OpenGL ES 3.2", 8, 2, 2000, 3800, 60.0, 4, 16, 720);

  @Before public void clean() {
    g = RuntimeEnvironment.getApplication().getSharedPreferences("graphics-test", Context.MODE_PRIVATE);
    g.edit().clear().commit();
    CoreBridge.shadowBit = 0;
  }

  /** Off (chosen, or the device decides): the core's Potato mask exactly as before the shadows, no bit 5 or 6. */
  @Test public void offSendsTheCoreTheMaskAsBefore() {
    String[] why = new String[1];
    g.edit().putInt(RealtimeShadows.KEY, 0).commit();
    assertEquals(0, RealtimeShadows.level(g, RETROID, MEASURED, false, why));
    CoreBridge.shadowBit = RealtimeShadows.coreBits(0);
    g.edit().putBoolean("weatherEffects", false).putInt("particleEffects", 2).commit();
    assertEquals(CoreBridge.potatoMask(g), CoreBridge.potatoSent(g));
    assertEquals(0, CoreBridge.potatoSent(g) & (RealtimeShadows.CORE_BIT | RealtimeShadows.OBJECTS_BIT));
    g.edit().clear().commit();
    assertEquals(0, RealtimeShadows.level(g, MALI_G52, MEASURED, false, why));
    CoreBridge.shadowBit = RealtimeShadows.coreBits(0);
    assertEquals(CoreBridge.potatoMask(g), CoreBridge.potatoSent(g));
  }

  /** No refusal for a release build any more (16.11.2026): only a renderer that cannot draw them would refuse. */
  @Test public void onlyTheRendererCanRefuse() {
    assertNull(RealtimeShadows.refusal(false));
    assertEquals(RealtimeShadows.VULKAN_READY, RealtimeShadows.refusal(true) == null);
  }

  @Test public void aCapableDeviceHasThemByDefault() {
    String[] why = new String[1];
    assertEquals(RealtimeShadows.CAPABLE, RealtimeShadows.deviceCheck(RETROID, MEASURED));
    assertEquals(RealtimeShadows.DEFAULT_LEVEL, RealtimeShadows.choice(g, true));
    assertEquals(RealtimeShadows.DEFAULT_LEVEL, RealtimeShadows.level(g, RETROID, MEASURED, false, why));
    assertEquals(2, RealtimeShadows.DEFAULT_LEVEL);
    assertTrue(why[0], why[0].startsWith("device check: passed (Qualcomm Adreno (TM) 650"));
    CoreBridge.shadowBit = RealtimeShadows.coreBits(RealtimeShadows.DEFAULT_LEVEL);
    assertEquals(CoreBridge.potatoMask(g) | 32 | 64, CoreBridge.potatoSent(g));
  }

  @Test public void anyOtherDeviceStartsOff() {
    String[] why = new String[1];
    assertEquals(0, RealtimeShadows.choice(g, false));
    assertEquals(0, RealtimeShadows.level(g, RETROID, NONE, false, why));
    assertEquals("device check: no benchmark yet", why[0]);
    assertEquals(0, RealtimeShadows.level(g, MALI_G52, MEASURED, false, why));
    assertTrue(why[0], why[0].startsWith("device check: graphics chip not strong enough"));
    assertEquals(0, RealtimeShadows.level(g, null, NONE, false, why));
    assertEquals("device check: the graphics chip is not known yet", why[0]);
  }

  @Test public void theDeviceChecksReasons() {
    assertEquals(RealtimeShadows.GPU_WEAK, RealtimeShadows.deviceCheck(MALI_G52, MEASURED));
    assertEquals(RealtimeShadows.NO_BENCHMARK, RealtimeShadows.deviceCheck(RETROID, NONE));
    assertEquals(RealtimeShadows.BENCHMARK_LOW, RealtimeShadows.deviceCheck(RETROID, new PerfProfile.Bench(5, 3, false, true, 27)));
    assertEquals(RealtimeShadows.CPU_LIMIT, RealtimeShadows.deviceCheck(RETROID, new PerfProfile.Bench(6, 5, false, true, 80)));
    assertEquals(RealtimeShadows.GPU_UNKNOWN, RealtimeShadows.deviceCheck(
        new PerfProfile.Device("", 8, 4, 2841, 7600, 60.0, 4, 16, 1080), MEASURED));
    PerfProfile.Device small = new PerfProfile.Device("Adreno (TM) 650", 8, 4, 2841, 3000, 60.0, 4, 16, 1080);
    assertEquals(RealtimeShadows.MEMORY, RealtimeShadows.deviceCheck(small, MEASURED));
  }

  /** Off, Characters, Characters and objects chosen in the menu: on any device, the core's bits and the log's words. */
  @Test public void theStoredChoiceWins() {
    String[] why = new String[1];
    int[] bits = {0, 32, 96};
    String[] names = {"off", "on (characters)", "on (characters and objects)"};
    for (int level = 0; level < 3; level++) {
      g.edit().putInt(RealtimeShadows.KEY, level).commit();
      assertEquals(level, RealtimeShadows.choice(g, true));
      assertEquals("a choice counts on a device that is not capable too", level, RealtimeShadows.choice(g, false));
      assertEquals(level, RealtimeShadows.level(g, RETROID, MEASURED, false, why));
      assertEquals(level, RealtimeShadows.level(g, RETROID, NONE, false, why));
      assertEquals("the menu's choice; device check: no benchmark yet", why[0]);
      assertEquals(level, RealtimeShadows.level(g, MALI_G52, MEASURED, false, why));
      assertEquals(bits[level], RealtimeShadows.coreBits(level));
      assertEquals(names[level], RealtimeShadows.logName(level));
      assertEquals((level + 1) % 3, RealtimeShadows.next(level));
    }
    g.edit().putInt(RealtimeShadows.KEY, 7).commit();  // (a value of a later version: the highest level)
    assertEquals(2, RealtimeShadows.choice(g, false));
  }

  @Test public void potatoShadowsOffStillTurnsThemOff() {
    String[] why = new String[1];
    g.edit().putBoolean("shadowsLight", false).commit();
    assertEquals(0, RealtimeShadows.level(g, RETROID, MEASURED, false, why));
    assertTrue(why[0], why[0].startsWith("Potato: Shadows & light off"));
    g.edit().putInt(RealtimeShadows.KEY, 2).commit();
    assertEquals("also against the menu's choice", 0, RealtimeShadows.level(g, RETROID, NONE, false, why));
    g.edit().putBoolean("shadowsLight", true).putInt(RealtimeShadows.KEY, 0).commit();
    assertEquals(0, RealtimeShadows.level(g, RETROID, MEASURED, false, why));
    assertEquals("the menu's choice; device check: passed", why[0].substring(0, "the menu's choice; device check: passed".length()));
  }

  private static CoreMenu.Host host(PerfProfile.Device device, int[] changed) {
    return (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          if (method.getName().equals("perfDevice")) return device;
          if (method.getName().equals("shadowsChanged")) changed[0]++;
          return method.getReturnType() == boolean.class ? Boolean.FALSE : method.getReturnType() == int.class ? 0
              : method.getReturnType() == float.class ? (Object) 1f
              : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
  }

  /** The Display page's effects (the project owner, 16.11.2026): the row in every build, debug and release alike,
   *  with the value in force (a capable device "Characters and objects", any other "Off" with the note why) until the
   *  player chooses; each press the next level, kept; never on the Performance page. */
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theMenuRowInEveryBuild() throws Exception {
    // a capable device, a weak one, and a capable one without a benchmark; each in a debug and a release build
    PerfProfile.Device[] devices = {RETROID, MALI_G52, RETROID};
    boolean[] benchmark = {true, true, false};
    for (int run = 0; run < 2 * devices.length; run++) {
      final int k = run % devices.length;
      final boolean debug = run < devices.length;
      final int[] changed = {0};
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        // a debug build (Android Studio's coreDebug is debuggable) or a signed release build (not debuggable)
        if (debug) activity.getApplicationInfo().flags |= android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE;
        else activity.getApplicationInfo().flags &= ~android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE;
        SharedPreferences graphics = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0);
        graphics.edit().clear().apply();
        if (benchmark[k]) {
          graphics.edit().putInt(CoreMenu.DYN_CAP_30, 6).putInt(CoreMenu.DYN_CAP_60, 5)
              .putInt(PerfProfile.BENCH_CPU_LOAD, 27).apply();
        }
        CoreMenu menu = new CoreMenu(activity, host(devices[k], changed), false, 1920, 1080);
        Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
        show.setAccessible(true);
        java.lang.reflect.Field rootField = CoreMenu.class.getDeclaredField("root");
        rootField.setAccessible(true);
        String label = activity.getString(R.string.ingame_rt_shadows);
        show.invoke(menu, 4, false);  // PERFORMANCE: not there
        assertNull(find((View) rootField.get(menu), label));
        show.invoke(menu, 1, false);  // DISPLAY
        View root = (View) rootField.get(menu);
        TextView row = find(root, label);
        assertNotNull("the row in every build (" + k + (debug ? ", debug" : ", release") + ")", row);
        assertNotNull("its help", find(root, activity.getString(R.string.ingame_rt_shadows_help)));
        boolean capable = k == 0;
        String note = activity.getString(R.string.ingame_rt_shadows_not_recommended,
            activity.getString(k == 1 ? R.string.ingame_rt_shadows_why_gpu : R.string.ingame_rt_shadows_why_benchmark));
        assertEquals("the note only where not capable (" + k + ")", !capable, find(root, note) != null);
        int start = capable ? 2 : 0;
        int[] values = {R.string.ingame_off, R.string.ingame_rt_shadows_figures, R.string.ingame_rt_shadows_objects};
        // (1.1.0: an arrow selector, the name over the value)
        assertEquals("the value in force, not the default of a capable device",
            label + "\n" + activity.getString(values[start]), row.getText().toString());
        assertFalse("nothing stored until the player chooses", RealtimeShadows.chosen(graphics));
        for (int i = 1; i <= 3; i++) {
          row.performClick();
          int level = (start + i) % 3;
          assertEquals(label + "\n" + activity.getString(values[level]), row.getText().toString());
          assertEquals(level, RealtimeShadows.choice(graphics, capable));
          assertEquals(level, RealtimeShadows.choice(graphics, !capable));  // kept: the device no longer decides
          assertEquals(i, changed[0]);
        }
        // the pictures of the row (RE4_MENU_SHOTS): a device without a benchmark and a capable one, at the start
        if (k != 1 && debug) {
          graphics.edit().remove(RealtimeShadows.KEY).apply();
          show.invoke(menu, 1, false);
          picture((View) CoreMenuLayoutTest.field(menu, "body"), label, k == 0 ? "row-capable" : "row-no-benchmark");
        }
      }
    }
  }

  /** The Display page from the effects' heading down to the end of the shadows' note (RE4_MENU_SHOTS: the folder). */
  private static void picture(View body, String label, String name) throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    if (folder == null || folder.isEmpty()) return;
    new File(folder).mkdirs();
    int width = 900;
    body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
    body.layout(0, 0, width, body.getMeasuredHeight());
    View row = find(body, label);
    int top = Math.max(0, topIn(row, body) - 140), bottom = Math.min(body.getMeasuredHeight(), topIn(row, body) + 900);
    Bitmap b = Bitmap.createBitmap(width, bottom - top, Bitmap.Config.ARGB_8888);
    Canvas c = new Canvas(b);
    c.drawColor(0xff191f1c);
    c.translate(0, -top);
    body.draw(c);
    try (FileOutputStream out = new FileOutputStream(new File(folder, name + ".png"))) {
      b.compress(Bitmap.CompressFormat.PNG, 100, out);
    }
  }

  private static int topIn(View v, View ancestor) {
    int y = 0;
    for (View at = v; at != ancestor && at != null; at = (View) at.getParent()) y += at.getTop();
    return y;
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
