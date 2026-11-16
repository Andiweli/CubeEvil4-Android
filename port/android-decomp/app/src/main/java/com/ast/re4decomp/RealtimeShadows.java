package com.ast.re4decomp;

import android.content.SharedPreferences;

/**
 * Real-time shadows of the figures and objects (port/docs/echtzeit-schatten-studie.md). Debug builds only until after 1.0: the
 * project owner, 15.11.2026: "Wir bauen sie aktuell ein, sie sind aber nur in der Debug Version die ich nutze
 * aktiviert, nicht wenn man Release baut. Erst nach 1.0 kommen sie ins Release." A debug build is Android Studio's
 * coreDebug, which the app knows by Android's flag "debuggable" (built()); a signed release build has it not. In a
 * release build nothing of it exists: no menu row, the setting is never read, nothing is sent to the core or the
 * renderer, so the game runs as before.
 *
 * In a debug build they cast shadows when all of these hold: the device is capable (a strong graphics chip, the
 * benchmark measured with 3x at 30 or 2x at 60 pictures a second, the processor not at its limit, 4 GB of memory;
 * weak devices never see the row), the renderer can draw them (OpenGL ES and Vulkan, VULKAN_READY),
 * the Potato mode's "Shadows &amp; light" is on, and the menu's "Real-time shadows" is not off: "Characters" (the
 * figures and what they hold) or "Characters and objects" (also what moves: doors, crates, barrels, the windmill's
 * blades, flags; the project owner, 15.11.2026: "Aus &gt; Figuren &gt; Figuren und Objekte"; DEFAULT_LEVEL on a capable
 * device). The game core then marks where they go (bit 5 of the Potato mask, bit 6 for the objects;
 * runtime/rt_shadow.cpp, runtime/gx_owner.cpp) and leaves its round foot shadows out there; the renderer draws them
 * (CoreBridge.setShadows, GxRenderer::SetShadows).
 */
final class RealtimeShadows {
  /** GRAPHICS_PREFS: 0 off, 1 the figures, 2 the figures and the objects; missing: DEFAULT_LEVEL. */
  static final String KEY = "rtShadows";
  /** The level of a capable device until the menu changes it: "Characters and objects" (the PC measurement of
   *  15.11.2026, port/docs/whole-program-status.md: the objects add little to the figures' cost). */
  static final int DEFAULT_LEVEL = 2;
  /** Bit 5 of the Potato mask the core gets (CoreBridge.setPotato): the core's mark and no foot shadow blobs. */
  static final int CORE_BIT = 32;
  /** Bit 6 with "Characters and objects": the core tags the objects that cast (runtime/gx_owner.cpp). */
  static final int OBJECTS_BIT = 64;
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

  /** The menu's choice: 0 off, 1 the figures, 2 the figures and the objects (missing: DEFAULT_LEVEL). */
  static int choice(SharedPreferences g) {
    int v = g.getInt(KEY, DEFAULT_LEVEL);
    return v <= 0 ? 0 : v >= 2 ? 2 : 1;
  }

  /** The menu's next choice: Off, Characters, Characters and objects, Off again. */
  static int next(int choice) {
    return (choice + 1) % 3;
  }

  /** The Potato mask's bits for the core at `level` (0 none). */
  static int coreBits(int level) {
    return level <= 0 ? 0 : level >= 2 ? CORE_BIT | OBJECTS_BIT : CORE_BIT;
  }

  /** The level's name in the error log. */
  static String logName(int level) {
    return level <= 0 ? "off" : level >= 2 ? "on (characters and objects)" : "on (characters)";
  }

  /** The level the game gets (0 off, 1 the figures, 2 also the objects) and, in why[0], the reason for the log. */
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
      level = choice(g);
      reason = "debug build, " + d.gpu + ", benchmark " + b.cap30 / 2.0 + "x at 30 / " + b.cap60 / 2.0 + "x at 60 FPS";
    }
    if (why != null && why.length > 0) why[0] = reason;
    return level;
  }
}
