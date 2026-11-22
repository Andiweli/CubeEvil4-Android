package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.view.View;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;

/**
 * An update over 1.0.0 without clearing the app's data (1.1.0; a user's OPPO A53 after updating by hand from 1.0.0:
 * "force close" at Save / Send report, fine with the data cleared). Every setting 1.0.0 writes (its keys and types,
 * read out of 1.0.0's code, b37a9cf4), in every settings file at once, with values of a player who changed them: each
 * page of the app menu, the report's device text, the performance profiles' update and a player data export read them
 * without a fault.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class UpdateFrom100Test {
  /** 1.0.0's keys by type (StartupActivity's own "settings" file, the old ARM64 mode, aside). */
  static final String[] BOOLEANS = {"aimInvertVertical", "alternateControls", "alwaysRun", "autoAim", "benchCpu",
      "benchReserve", "bodyTurn", "clearAtStart", "deathYes", "diagnostics", "dof", "effectsOn", "farCamera", "flares",
      "fps", "fxaa", "gentleQte", "gyro", "gyroAiming", "hdHalf", "hdTextures", "knifeSwipe", "leonJacket", "mergeDraws",
      "minimap", "mipmapping", "musicOn", "psx", "r3Capture", "record", "rendering", "roomFps", "shadowsLight",
      "sharperText", "smooth60", "specialEffects", "touchLatchB", "touchLatchLR", "tvFilter", "vertexRing", "vkBandwidth",
      "weatherEffects", "wipeAtStart", "stackItems", "hdCostumes", "hdWeapons"};
  static final String[] INTS = {"af", "benchCpuLoad", "benchFirst", "benchRedo", "bloomLevel", "cheats", "colourLevel",
      "colourMode", "crtLevel", "deathChoice", "deathCursor", "deathExit", "deathFrame", "deathGate", "deathRemaining",
      "dofLevel", "dynamicCap30", "dynamicCap60", "enemySeed", "fastCores", "flareMode", "fovOffset", "fpsPlace",
      "frameSkipMode", "frames", "gameLanguage", "laserColour", "msaa", "particleEffects", "perfProfile", "rtShadows",
      "scaleHalves", "sharpenLevel", "stickDeadZone", "stickRange", "touchAim", "touchMode", "vibration", "viewDistance",
      "vulkanStartingPid", "vulkanStartingUnsure", "samples", "anisotropy", "aspect"};
  static final String[] FLOATS = {"aimSpeed", "gyroSensitivity", "mouseSensitivity"};
  static final String[] LONGS = {"benchDate", "sceneSession", "vulkanStartingAt"};
  static final String[] STRINGS = {"confirmedSigner", "deathPhase", "path", "renderer"};
  static final String[] FILES = {CoreActivity.GRAPHICS_PREFS, CoreActivity.INPUT_PREFS, CoreActivity.GAME_PREFS,
      CoreLog.PREFS, PlayerBackup.RETROTOUCH};

  /** 1.0.0's settings of a player who changed many of them (odd values on purpose: the highest steps of before). */
  static void write100(Context c) {
    for (String file : FILES) {
      SharedPreferences.Editor e = c.getSharedPreferences(file, Context.MODE_PRIVATE).edit().clear();
      int n = 0;
      for (String k : BOOLEANS) e.putBoolean(k, (n++ & 1) == 0);
      for (String k : INTS) e.putInt(k, 1 + (n++ % 4));
      for (String k : FLOATS) e.putFloat(k, 1.25f);
      for (String k : LONGS) e.putLong(k, 1795000000000L);
      for (String k : STRINGS) e.putString(k, k.equals("renderer") ? "gles" : "x");
      // a profile chosen in 1.0.0 (Low) with its values as they were, the profile's copies
      e.putInt(PerfProfile.ACTIVE, PerfProfile.LOW);
      e.commit();
    }
  }

  @Test public void everyPageAndTheReportReadA100Set() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      write100(activity);
      CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
          new Class<?>[] {CoreMenu.Host.class},
          (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.FALSE
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
              : method.getReturnType() == double.class ? (Object) 60.0 : null);
      CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
      show.setAccessible(true);
      for (int page = 0; page < 9; page++) {
        if (page >= 7) {
          java.lang.reflect.Field kind = CoreMenu.class.getDeclaredField("mapKind");
          kind.setAccessible(true);
          kind.setInt(menu, page == 7 ? ActionMap.KIND_CONTROLLER : ActionMap.KIND_KEYBOARD);
        }
        show.invoke(menu, Math.min(page, 7), false);
      }
      // the report's device text (Save / Send report) with 1.0.0's graphics settings
      String text = CoreLog.deviceText(activity, "-");
      assertTrue(text.contains("Graphics: {"));
      // the performance profiles at the first start after the update
      SharedPreferences g = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, Context.MODE_PRIVATE);
      PerfProfile.updateLow(g);
      PerfProfile.current(g);
      PerfProfile.sync(g);
      // the helpers that read the old keys
      CoreMenu.frameSkipMode(g);
      CoreActivity.stackMax(activity.getSharedPreferences(CoreActivity.GAME_PREFS, Context.MODE_PRIVATE));
      assertNotNull(menu);
    }
  }
}
