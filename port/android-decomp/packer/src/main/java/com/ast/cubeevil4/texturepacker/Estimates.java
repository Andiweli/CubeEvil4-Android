package com.ast.cubeevil4.texturepacker;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Space and time before a conversion (port/docs/textur-packer-android-plan.md, "Die Platzprüfung" and "Dauer"): the
 * file's size at most comes from the core (PackerNative.estimate); the time is the PC's, scaled by the device's cores.
 */
final class Estimates {
  static final int[] SIZES = {256, 512, 1024};
  /** Free space kept besides the pack file. */
  static final long RESERVE = 100L << 20;

  /**
   * The PC the times were measured on (Ryzen 7 5700X, 8 cores, 16 threads; the owner's HD Project, 11.8 GB, 5,916
   * images: 256 in 1:52, 512 in 4:22, 1024 in 14:23 with Texture Packer 1.2) in the units of units(): set so that
   * the Retroid Pocket Flip 2 (Snapdragon 865) comes out at the plan's 20 minutes for 512. The first runs on the
   * Retroid correct it.
   */
  static final double PC_UNITS = 60;
  /** PC seconds per GB of the pack's images (reading, decoding) and per GB of the pack file (ETC2), from those runs. */
  static final double PC_SECONDS_PER_SOURCE_GB = 1.14, PC_SECONDS_PER_OUTPUT_GB = 330;

  // little cores (ARM's "CPU part" in /proc/cpuinfo): Cortex-A5, A7, A32, A34, A53, A35, A55, A510, A520 and
  // Qualcomm's Kryo "silver" cores
  private static final int[] LITTLE = {0xc05, 0xc07, 0xd01, 0xd02, 0xd03, 0xd04, 0xd05, 0xd46, 0xd80, 0x801, 0x803,
      0x805};

  private Estimates() {}

  static boolean fits(long estimate, long free) {
    return free >= estimate + RESERVE;
  }

  /** The largest of SIZES whose estimate fits into free, -1 when none does. */
  static int largestFitting(long[] estimates, long free) {
    int best = -1;
    for (int i = 0; i < Math.min(estimates.length, SIZES.length); i++)
      if (fits(estimates[i], free)) best = i;
    return best;
  }

  /**
   * What the device's cores do compared with the PC: per core its top clock in GHz, a little core 0.4 of that.
   * maxKHz: each core's top clock (0: unknown), parts: its "CPU part" (-1: unknown; then a core well below the
   * fastest counts as little).
   */
  static double units(long[] maxKHz, int[] parts) {
    long top = 0;
    for (long k : maxKHz) top = Math.max(top, k);
    double sum = 0;
    for (int i = 0; i < maxKHz.length; i++) {
      long khz = maxKHz[i] > 0 ? maxKHz[i] : top > 0 ? top : 1_500_000;
      int part = parts != null && i < parts.length ? parts[i] : -1;
      boolean little = part >= 0 ? isLittle(part) : top > 0 && khz < top * 3 / 4;
      sum += khz / 1e6 * (little ? 0.4 : 1.0);
    }
    return sum > 0 ? sum : 1.5;
  }

  static boolean isLittle(int part) {
    for (int p : LITTLE)
      if (p == part) return true;
    return false;
  }

  /** About how long the conversion takes on a device with these units, in seconds. */
  static long seconds(long sourceBytes, long estimateBytes, double units) {
    double gb = 1e9;
    double pc = PC_SECONDS_PER_SOURCE_GB * sourceBytes / gb + PC_SECONDS_PER_OUTPUT_GB * estimateBytes / gb;
    return Math.round(pc * PC_UNITS / Math.max(units, 0.5));
  }

  /** Minutes to show for seconds: at least 1, from 10 minutes on in steps of 5. */
  static long roundedMinutes(long seconds) {
    long minutes = Math.max(1, Math.round(seconds / 60.0));
    return minutes < 10 ? minutes : Math.round(minutes / 5.0) * 5;
  }

  /** This device's units (units()). */
  static double deviceUnits() {
    int cores = Math.max(1, Runtime.getRuntime().availableProcessors());
    long[] khz = new long[cores];
    for (int i = 0; i < cores; i++) khz[i] = readLong("/sys/devices/system/cpu/cpu" + i + "/cpufreq/cpuinfo_max_freq");
    return units(khz, cpuParts(cores));
  }

  private static long readLong(String path) {
    try (BufferedReader r = new BufferedReader(new FileReader(new File(path)))) {
      String line = r.readLine();
      return line == null ? 0 : Long.parseLong(line.trim());
    } catch (IOException | NumberFormatException e) {
      return 0;
    }
  }

  /** Each core's "CPU part" from /proc/cpuinfo, -1 where it says none. */
  static int[] cpuParts(int cores) {
    int[] parts = new int[cores];
    Arrays.fill(parts, -1);
    List<String> lines = new ArrayList<>();
    try (BufferedReader r = new BufferedReader(new FileReader("/proc/cpuinfo"))) {
      for (String line; (line = r.readLine()) != null; ) lines.add(line);
    } catch (IOException e) {
      return parts;
    }
    return parseParts(lines, cores);
  }

  static int[] parseParts(List<String> lines, int cores) {
    int[] parts = new int[cores];
    Arrays.fill(parts, -1);
    int core = -1;
    for (String line : lines) {
      int colon = line.indexOf(':');
      if (colon < 0) continue;
      String key = line.substring(0, colon).trim(), value = line.substring(colon + 1).trim();
      try {
        if (key.equals("processor")) core = Integer.parseInt(value);
        else if (key.equals("CPU part") && core >= 0 && core < cores) parts[core] = Integer.decode(value);
      } catch (NumberFormatException e) {
        // a line of another kind ("processor : ARMv7 ...")
      }
    }
    return parts;
  }
}
