package com.ast.re4decomp;

import android.content.SharedPreferences;

/**
 * Real-time shadows of the figures (port/docs/echtzeit-schatten-studie.md). Debug builds only until after 1.0: the
 * project owner, 15.11.2026: "Wir bauen sie aktuell ein, sie sind aber nur in der Debug Version die ich nutze
 * aktiviert, nicht wenn man Release baut. Erst nach 1.0 kommen sie ins Release." A debug build is Android Studio's
 * coreDebug, which the app knows by Android's flag "debuggable" (built()); a signed release build has it not. In a
 * release build nothing of it exists: no menu row, the setting is never read, nothing is sent to the core or the
 * renderer, so the game runs as before.
 *
 * In a debug build the figures cast shadows when all of these hold: the device is capable (a strong graphics chip, the
 * benchmark measured with 3x at 30 or 2x at 60 pictures a second, the processor not at its limit, 4 GB of memory;
 * weak devices never see the row), the renderer can draw them (OpenGL ES and Vulkan, VULKAN_READY),
 * the Potato mode's "Shadows &amp; light" is on, and the menu's "Real-time shadows" is "Characters" (the default on a
 * capable device). The game core then marks where they go (bit 5 of the Potato mask, runtime/rt_shadow.cpp) and leaves
 * its round foot shadows out there; the renderer draws them (CoreBridge.setShadows, GxRenderer::SetShadows).
 */
final class RealtimeShadows {
  /** GRAPHICS_PREFS: 0 off, 1 the figures; missing: 1 (on in a debug build on a capable device). */
  static final String KEY = "rtShadows";
  /** Bit 5 of the Potato mask the core gets (CoreBridge.setPotato): the core's mark and no foot shadow blobs. */
  static final int CORE_BIT = 32;
  /** The Vulkan renderer draws them too (gx_vk.cpp ShadowPass, stage 3; the same picture as OpenGL ES on the PC). */
  static final boolean VULKAN_READY = true;

  private RealtimeShadows() {}

  /** A debug build (Android Studio's coreDebug: debuggable, a signed release is not); else the feature does not exist. */
  static boolean built(android.content.Context c) {
    return c != null
        && (c.getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0;
  }

  /** Why this device does not get them (null: it may). `debug` is built() (the tests pass both). */
  static String refusal(boolean debug, PerfProfile.Device d, PerfProfile.Bench b, boolean vulkan) {
    if (!debug) return "release build";
    if (vulkan && !VULKAN_READY) return "the Vulkan renderer draws them not yet";
    if (d == null || d.gpu.isEmpty()) return "the graphics chip is not known yet";
    if (!PerfProfile.strongGpu(d) || PerfProfile.weakGpu(d)) return "graphics chip not strong enough (" + d.gpu + ")";
    if (b == null || !b.measured) return "no benchmark yet";
    if (b.cap30 < 6 && b.cap60 < 4) {
      return "the benchmark's levels too low (" + b.cap30 / 2.0 + "x at 30, " + b.cap60 / 2.0 + "x at 60 FPS; needs 3x or 2x)";
    }
    if (PerfProfile.cpuWeak(d, b) || PerfProfile.cpuTight(d, b)) return "the processor is at its limit";
    if (d.ramMb > 0 && d.ramMb < 3800) return "less than 4 GB of memory";
    return null;
  }

  /** The menu shows the row (a debug build, a capable device and renderer). */
  static boolean offered(boolean debug, PerfProfile.Device d, PerfProfile.Bench b, boolean vulkan) {
    return refusal(debug, d, b, vulkan) == null;
  }

  /** The menu's choice: 1 the figures (the default), 0 off. */
  static int choice(SharedPreferences g) {
    return g.getInt(KEY, 1) != 0 ? 1 : 0;
  }

  /** The level the game gets (0 off, 1 the figures) and, in why[0], the reason for the log. */
  static int level(boolean debug, SharedPreferences g, PerfProfile.Device d, PerfProfile.Bench b, boolean vulkan,
                   String[] why) {
    String refused = refusal(debug, d, b, vulkan);
    int level = 0;
    String reason;
    if (refused != null) {
      reason = refused;
    } else if (!g.getBoolean("shadowsLight", true)) {
      reason = "Potato: Shadows & light off";
    } else if (choice(g) == 0) {
      reason = "the menu's choice";
    } else {
      level = 1;
      reason = "debug build, " + d.gpu + ", benchmark " + b.cap30 / 2.0 + "x at 30 / " + b.cap60 / 2.0 + "x at 60 FPS";
    }
    if (why != null && why.length > 0) why[0] = reason;
    return level;
  }
}
