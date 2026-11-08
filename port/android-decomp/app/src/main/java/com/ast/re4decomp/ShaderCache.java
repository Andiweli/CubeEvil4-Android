package com.ast.re4decomp;

import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;
import java.util.Locale;

/**
 * The shaders the renderers built, in one folder the core gets as RE4_GX_PROGRAM_CACHE: OpenGL ES's linked
 * programs (&lt;stem&gt;.bin with &lt;stem&gt;.key; the stem names the driver, so another driver never loads them) and
 * both renderers' room lists of programs (room-NNN.keys); Vulkan's SPIR-V store (vk-spirv.bin, at most 160 MB), the
 * driver's pipeline cache (vk-pipelines.bin, taken only when its header names this device and driver, gx_vk.cpp) and
 * the rooms' pipelines (room-NNN.vkpipes). The limits and the clean-up of stale files stay in the renderers.
 *
 * <p>Since 0.99.4 (decision of the project owner, 08.11.2026) in getNoBackupFilesDir()/shader-cache instead of
 * getCacheDir()/gx-programs: Android and cleaner apps empty the cache folder (a user's report of 07.11.2026, Xiaomi:
 * empty after "SwipeUpClean", the next start linked every program at its first draw, hitches up to 1.75 s). The
 * no-backup folder is not cleared by the system and not part of a cloud backup (the binaries belong to this device).
 * At the start, before the core opens the folder: the old folder moved over once, and a clearing the menu asked for
 * (Management, "Clear shader cache") done.
 */
final class ShaderCache {
  static final String FOLDER = "shader-cache";
  /** Up to 0.99.3: under getCacheDir(). */
  static final String OLD_FOLDER = "gx-programs";
  static final String PREFS = "shader_cache";
  static final String CLEAR_AT_START = "clearAtStart";

  /** What this process's start did with the folder (for the report). */
  private static volatile String startNote = "not prepared in this run";

  private ShaderCache() {}

  static File dir(Context c) {
    return new File(c.getNoBackupFilesDir(), FOLDER);
  }

  /** Management -> "Clear shader cache" (the menu asked first): the folder is emptied at the next start. */
  static void requestClear(Context c) {
    c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(CLEAR_AT_START, true).commit();
  }

  /** At the start, before the core: the old folder moved over, a requested clearing done; one line for the log. */
  static String prepare(Context c) {
    SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    boolean clear = p.getBoolean(CLEAR_AT_START, false);
    String note = prepare(new File(c.getCacheDir(), OLD_FOLDER), dir(c), clear);
    if (clear) p.edit().remove(CLEAR_AT_START).commit();
    startNote = note;
    return note;
  }

  /**
   * The work of {@link #prepare(Context)} on plain folders: `old` (the cache folder's gx-programs) into `now`
   * (renamed whole when `now` is not there yet; else file by file, files `now` has already win), then the old folder
   * gone; with `clear` both emptied instead.
   */
  static String prepare(File old, File now, boolean clear) {
    if (clear) {
      Count c = new Count();
      count(now, c);
      count(old, c);
      deleteTree(old);
      deleteTree(now);
      now.mkdirs();
      return "cleared as asked in the menu (" + c + "); the renderer builds the shaders anew";
    }
    String note = "in place";
    if (old.isDirectory()) {
      Count c = new Count();
      count(old, c);
      if (!now.exists()) {
        File parent = now.getParentFile();
        if (parent != null) parent.mkdirs();
        if (old.renameTo(now)) {
          note = "moved from the cache folder (" + c + ")";
        }
      }
      if (old.exists()) {
        now.mkdirs();
        Count moved = new Count();
        File[] files = old.listFiles();
        if (files != null) {
          for (File f : files) {
            File target = new File(now, f.getName());
            long size = f.length();
            if (f.isFile() && !f.getName().endsWith(".tmp") && !target.exists() && f.renameTo(target)) {
              moved.files++;
              moved.bytes += size;
            }
          }
        }
        deleteTree(old);
        note = "merged from the cache folder (" + moved + " of " + c + " moved, the rest was here already)"
            + (old.exists() ? "; the old folder could not be removed" : "");
      }
    }
    now.mkdirs();
    return note;
  }

  /** shader-cache.txt of the error report: where the shaders are and how much each renderer keeps there. */
  static String describe(Context c) {
    return describe(dir(c), new File(c.getCacheDir(), OLD_FOLDER), startNote);
  }

  static String describe(File dir, File old, String note) {
    Count glPrograms = new Count(), glKeys = new Count(), roomKeys = new Count(), vkPipes = new Count();
    Count spirv = new Count(), pipelines = new Count(), temp = new Count(), other = new Count();
    File[] files = dir.listFiles();
    if (files != null) {
      for (File f : files) {
        if (!f.isFile()) continue;
        String n = f.getName();
        Count to = n.equals("vk-spirv.bin") ? spirv
            : n.equals("vk-pipelines.bin") ? pipelines
            : n.startsWith("room-") && n.endsWith(".keys") ? roomKeys
            : n.startsWith("room-") && n.endsWith(".vkpipes") ? vkPipes
            : n.endsWith(".tmp") ? temp
            : n.endsWith(".bin") ? glPrograms
            : n.endsWith(".key") ? glKeys : other;
        to.files++;
        to.bytes += f.length();
      }
    }
    Count all = new Count();
    for (Count k : new Count[] {glPrograms, glKeys, roomKeys, vkPipes, spirv, pipelines, temp, other}) {
      all.files += k.files;
      all.bytes += k.bytes;
    }
    StringBuilder b = new StringBuilder();
    b.append("Shader cache: ").append(dir.getAbsolutePath())
        .append(" (not emptied by Android or cleaner apps; Management -> Clear shader cache empties it)\n");
    b.append("This start: ").append(note).append('\n');
    b.append("In all: ").append(all).append('\n');
    b.append("OpenGL ES: ").append(glPrograms.files).append(" linked programs (").append(mb(glPrograms.bytes))
        .append("), ").append(glKeys.files).append(" keys (").append(mb(glKeys.bytes)).append(")\n");
    b.append("Both renderers: ").append(roomKeys.files).append(" rooms' program lists (").append(mb(roomKeys.bytes))
        .append(")\n");
    b.append("Vulkan: SPIR-V store ").append(spirv.files == 0 ? "none" : mb(spirv.bytes))
        .append(", the driver's pipeline cache ").append(pipelines.files == 0 ? "none" : mb(pipelines.bytes))
        .append(", ").append(vkPipes.files).append(" rooms' pipeline lists (").append(mb(vkPipes.bytes)).append(")\n");
    if (temp.files + other.files > 0)
      b.append("Other: ").append(temp.files).append(" unfinished (.tmp), ").append(other.files).append(" others (")
          .append(mb(temp.bytes + other.bytes)).append(")\n");
    b.append("Old folder (").append(old.getAbsolutePath()).append("): ").append(old.exists() ? "still there" : "gone")
        .append('\n');
    return b.toString();
  }

  /** Files and their bytes. */
  static final class Count {
    int files;
    long bytes;

    @Override public String toString() {
      return files + (files == 1 ? " file, " : " files, ") + mb(bytes);
    }
  }

  private static void count(File f, Count c) {
    if (f.isFile()) {
      c.files++;
      c.bytes += f.length();
      return;
    }
    File[] files = f.listFiles();
    if (files == null) return;
    for (File g : files) count(g, c);
  }

  static String mb(long bytes) {
    return String.format(Locale.ROOT, "%.1f MB", bytes / 1048576.0);
  }

  private static void deleteTree(File f) {
    File[] files = f.listFiles();
    if (files != null) {
      for (File g : files) deleteTree(g);
    }
    if (f.exists()) f.delete();
  }
}
