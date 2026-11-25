package com.ast.re4decomp;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.app.Application;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowInsets;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.annotation.LooperMode;

/**
 * Android 7.0 (API 24, the core app's minSdk; 1.1.3): an NVIDIA Shield Tablet stopped right after the start, the game
 * window read WindowManager.LayoutParams.layoutInDisplayCutoutMode (API 28) for its error log line. Here the game
 * window, its insets, the app menu's pages and the calls that lint named run on Android 7.0's framework: a field or
 * method Android 7 does not have throws (NoSuchFieldError, NoSuchMethodError) as on the device.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@LooperMode(LooperMode.Mode.PAUSED)
@Config(sdk = 24, application = Re4Application.class, shadows = Android7Test.Core.class,
    qualifiers = "w960dp-h540dp-land-mdpi")
public class Android7Test {
  /** No core library in this sandbox; the menu's questions answered as on a device. */
  @Implements(CoreBridge.class)
  public static class Core {
    @Implementation public static void load() {}
    @Implementation public static int[] graphicsInfo() { return new int[] {4, 16, 2, 1, 1}; }
    @Implementation public static String[] cardList() { return new String[0]; }
    @Implementation public static long[] texturePackUse() { return new long[] {0, 0, 0}; }
    /** The core "started" (no problem): the game window goes the whole way of a device with the game. */
    @Implementation public static String start(String root, String settings) { return ""; }
    @Implementation public static boolean hasMenu() { return true; }
  }

  private static void discsReady() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    File root = ((Re4Application) app).imports.root;
    for (int n = 1; n <= 2; n++) {
      File disc = new File(root, "disc" + n);
      new File(disc, "sys").mkdirs();
      try (FileOutputStream out = new FileOutputStream(new File(disc, "ready.properties"))) {
        out.write(("format=1\nid=G4BE08\ndisc=" + n + "\nreferences=" + (n == 1 ? 111 : 5) + "\n").getBytes("UTF-8"));
      }
      try (java.io.RandomAccessFile dol = new java.io.RandomAccessFile(new File(disc, "sys/main.dol"), "rw")) {
        dol.setLength(com.ast.re4decomp.disc.GameCubeDisc.dolSize("debug"));
      }
      try (java.io.RandomAccessFile boot = new java.io.RandomAccessFile(new File(disc, "sys/boot.bin"), "rw")) {
        boot.setLength(0x440);
      }
      new File(disc, "sys/fst.bin").createNewFile();
      new File(disc, "manifest.tsv").createNewFile();
    }
  }

  @Test public void theGameWindowStartsOnAndroid7() throws Exception {
    assertTrue(android.os.Build.VERSION.SDK_INT == 24);
    discsReady();
    // created only: Robolectric's own idling after start/resume never ends with the game window's frame callbacks;
    // the main looper runs bounded (TestLooper)
    var controller = Robolectric.buildActivity(CoreActivity.class).create();
    CoreActivity activity = controller.get();
    TestLooper.idle();
    View root = activity.getWindow().getDecorView();
    CoreMenuLayoutTest.measure(root, 960, 540);
    // the insets as Android 7 gives them: the listener's error log line (cutoutInsets, where the Shield stopped)
    WindowInsets insets = root.getRootWindowInsets();
    if (insets == null) {
      insets = org.robolectric.util.ReflectionHelpers.callConstructor(WindowInsets.class,
          org.robolectric.util.ReflectionHelpers.ClassParameter.from(android.graphics.Rect.class, new android.graphics.Rect()));
    }
    View game = ((android.view.ViewGroup) activity.findViewById(android.R.id.content)).getChildAt(0);
    game.dispatchApplyWindowInsets(insets);
    TestLooper.idle();
    // the line itself (the listener posts it, and the window is not attached in this sandbox)
    Method line = CoreActivity.class.getDeclaredMethod("cutoutInsets", View.class, WindowInsets.class);
    line.setAccessible(true);
    try {
      line.invoke(activity, game, insets);
    } catch (java.lang.reflect.InvocationTargetException e) {
      throw new AssertionError("the error log's cutout line on Android 7", e.getCause());
    }
    // the window's cutout mode: none before Android 9
    assertTrue(Cutout.windowMode(activity.getWindow()) == -1);
    Cutout.extend(activity.getWindow());
  }

  @Test public void theSystemKeysOnAndroid7() {
    // KeyEvent.isMediaSessionKey is API 31: the same keys without it
    assertTrue(CoreActivity.systemKey(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE));
    assertTrue(CoreActivity.systemKey(KeyEvent.KEYCODE_HEADSETHOOK));
    assertTrue(CoreActivity.systemKey(KeyEvent.KEYCODE_VOLUME_UP));
    assertFalse(CoreActivity.systemKey(KeyEvent.KEYCODE_BUTTON_A));
    assertFalse(CoreActivity.systemKey(KeyEvent.KEYCODE_W));
  }

  @Test public void theThermalWatchOnAndroid7() {
    ThermalWatch w = ThermalWatch.forDevice(RuntimeEnvironment.getApplication(), 0);
    w.start(0);
    w.stop();
  }

  @Test public void theGyroOfAControllerOnAndroid7() {
    GyroAim.padGyroOf(null);
    assertTrue(GyroAim.padGyroOf(android.view.InputDevice.getDevice(0)) == null);
  }

  @Test public void everyPageOfTheAppMenuOnAndroid7() throws Exception {
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.TRUE
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
            : method.getReturnType() == double.class ? (Object) 60.0 : null);
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
    show.setAccessible(true);
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      Activity activity = controller.get();
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      assertNotNull(menu);
      View body = (View) CoreMenuLayoutTest.field(menu, "body");
      for (int page = 0; page < 7; page++) {
        show.invoke(menu, page, false);
        CoreMenuLayoutTest.measure(body, 900, 4000);
        TestLooper.idle();
      }
    }
  }
}
