package com.ast.re4decomp;

import android.content.SharedPreferences;

/**
 * Real-time shadows of the figures and objects (port/docs/echtzeit-schatten-studie.md). In every build since 16.11.2026,
 * for 1.0: until then only in debug builds (the project owner, 15.11.2026), now in signed release builds too (the
 * project owner, 16.11.2026: "Die Schatten kannst du auch in den Hauptbuild übernehmen. Ich release erst 1.0 Ende der
 * Woche."), with the rules the debug builds had.
 *
 * The menu's row "Real-time shadows" is always there (the project owner, 16.11.2026: "Yes, always show it"): "Off",
 * "Characters" (the figures and what they hold) or "Characters and objects" (also what moves: doors, crates, barrels,
 * the windmill's blades, flags; the project owner, 15.11.2026: "Aus &gt; Figuren &gt; Figuren und Objekte"). Until
 * the player chooses, the device decides (deviceCheck): a capable one (a strong graphics chip, the benchmark measured
 * with 3x at 30 or 2x at 60 pictures a second, the processor not at its limit, 4 GB of memory) has DEFAULT_LEVEL, any
 * other "Off", with a note under the row why it is not recommended. A choice made in the menu (KEY stored) counts on
 * any device. They cast shadows when the renderer can draw them (OpenGL ES and Vulkan,
 * VULKAN_READY), the Potato mode's "Shadows &amp; light" is on and the level is not "Off". The game core then marks
 * where they go (bit 5 of the Potato mask, bit 6 for the objects; runtime/rt_shadow.cpp, runtime/gx_owner.cpp) and
 * leaves its round foot shadows out there; the renderer draws them (CoreBridge.setShadows, GxRenderer::SetShadows).
 */
final class RealtimeShadows {
  /** GRAPHICS_PREFS: 0 off, 1 the figures, 2 the figures and the objects; missing: the device decides (choice). */
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
  /** deviceCheck's answers: the device may have them by default (CAPABLE), or why not. */
  static final int CAPABLE = 0, GPU_UNKNOWN = 1, GPU_WEAK = 2, NO_BENCHMARK = 3, BENCHMARK_LOW = 4, CPU_LIMIT = 5,
      MEMORY = 6;

  private RealtimeShadows() {}

  /** Whether the device is capable enough for them by default (CAPABLE), or the first reason it is not. */
  static int deviceCheck(PerfProfile.Device d, PerfProfile.Bench b) {
    if (d == null || d.gpu.isEmpty()) return GPU_UNKNOWN;
    if (!PerfProfile.strongGpu(d) || PerfProfile.weakGpu(d)) return GPU_WEAK;
    if (b == null || !b.measured) return NO_BENCHMARK;
    if (b.cap30 < 6 && b.cap60 < 4) return BENCHMARK_LOW;
    if (PerfProfile.cpuWeak(d, b) || PerfProfile.cpuTight(d, b)) return CPU_LIMIT;
    if (d.ramMb > 0 && d.ramMb < 3800) return MEMORY;
    return CAPABLE;
  }

  /** The device check in the error log's words. */
  static String checkText(int check, PerfProfile.Device d, PerfProfile.Bench b) {
    switch (check) {
      case GPU_UNKNOWN: return "the graphics chip is not known yet";
      case GPU_WEAK: return "graphics chip not strong enough (" + d.gpu + ")";
      case NO_BENCHMARK: return "no benchmark yet";
      case BENCHMARK_LOW:
        return "the benchmark's levels too low (" + b.cap30 / 2.0 + "x at 30, " + b.cap60 / 2.0 + "x at 60 FPS; needs 3x or 2x)";
      case CPU_LIMIT: return "the processor is at its limit";
      case MEMORY: return "less than 4 GB of memory";
      default: return "passed (" + d.gpu + ", benchmark " + b.cap30 / 2.0 + "x at 30 / " + b.cap60 / 2.0 + "x at 60 FPS)";
    }
  }

  /** Why they cannot be drawn at all, whatever the menu says (null: they can): a renderer that cannot draw them. Debug
   *  and release builds alike (since 16.11.2026). */
  static String refusal(boolean vulkan) {
    if (vulkan && !VULKAN_READY) return "the Vulkan renderer draws them not yet";
    return null;
  }

  /** The player chose a level in the menu (KEY stored); until then the device decides. */
  static boolean chosen(SharedPreferences g) {
    return g.contains(KEY);
  }

  /** The level in force before Potato and the renderer: the menu's choice once made (0 off, 1 the figures, 2 the
   *  figures and the objects), else DEFAULT_LEVEL on a capable device (`capable`: deviceCheck is CAPABLE) and Off on
   *  any other. The menu's row shows this value. */
  static int choice(SharedPreferences g, boolean capable) {
    if (!chosen(g)) return capable ? DEFAULT_LEVEL : 0;
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

  /** The level the game gets (0 off, 1 the figures, 2 also the objects) and, in why[0], the reason for the log: who
   *  decided (the menu's choice or the device) and the device check, e.g. "the menu's choice; device check: no
   *  benchmark yet". */
  static int level(SharedPreferences g, PerfProfile.Device d, PerfProfile.Bench b, boolean vulkan, String[] why) {
    String refused = refusal(vulkan);
    int level = 0;
    String reason;
    if (refused != null) {
      reason = refused;
    } else {
      int check = deviceCheck(d, b);
      String device = "device check: " + checkText(check, d, b);
      if (!g.getBoolean("shadowsLight", true)) {
        reason = "Potato: Shadows & light off; " + device;
      } else {
        level = choice(g, check == CAPABLE);
        reason = (chosen(g) ? "the menu's choice; " : "") + device;
      }
    }
    if (why != null && why.length > 0) why[0] = reason;
    return level;
  }
}
