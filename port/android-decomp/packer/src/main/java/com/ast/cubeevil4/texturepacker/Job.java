package com.ast.cubeevil4.texturepacker;

import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The app's one pack and its conversion, shared by the screen (MainActivity) and the conversion's service
 * (ConvertService) for as long as the process lives. Changed on the main thread, except the progress numbers.
 */
final class Job {
  static final int EMPTY = 0, READING = 1, READY = 2, CONVERTING = 3, DONE = 4, FAILED = 5;

  private static final Handler MAIN = new Handler(Looper.getMainLooper());
  private static Runnable listener;

  static int state = EMPTY;
  /** the pack (PackerNative handle, 0: none), what it holds, where it came from */
  static long handle;
  static Summary summary;
  static Uri source;
  static boolean sourceIsFolder;
  /** reading a folder: files found so far */
  static volatile int listed;
  /** the choices (a variant per group, -1: none), the size, the estimates for 256 / 512 / 1024 (null: not yet) */
  static int[] choices = new int[0];
  static int size = 512;
  static long[] estimates;
  /** the conversion's progress */
  static volatile int done, total;
  static volatile long written;
  static long startedAt;
  /** the result: where the file is (for the import into CubeEvil4), its name to show, size, time, report */
  static Uri result;
  static String resultName, reportName;
  static long resultBytes, seconds;
  static String report;
  /** why it failed (shown as it is), or cancelled */
  static String failure;
  static boolean cancelled;

  private Job() {}

  /**
   * The one thread that opens, estimates and closes packs, one after the other: a pack is never closed while an
   * estimate still reads it (the conversion itself runs in ConvertService, while nothing else may touch the pack).
   */
  static final ExecutorService WORKER = Executors.newSingleThreadExecutor();
  private static int estimateRun;

  static void listen(Runnable r) {
    listener = r;
  }

  /** The screen shows the change (from any thread). */
  static void changed() {
    MAIN.post(() -> {
      if (listener != null) listener.run();
    });
  }

  /** r on the main thread, then the screen shows it. */
  static void onMain(Runnable r) {
    MAIN.post(() -> {
      r.run();
      if (listener != null) listener.run();
    });
  }

  /** The sizes for the choices made (main thread), on the worker; the newest request wins. */
  static void estimate() {
    if (handle == 0) return;
    final long h = handle;
    final int[] chosen = choices.clone();
    final int run = ++estimateRun;
    estimates = null;
    changed();
    WORKER.execute(() -> {
      long[] e = PackerNative.estimate(h, chosen);
      onMain(() -> {
        if (run == estimateRun && h == handle) estimates = e;
      });
    });
  }

  /** Lets go of the pack (another one is chosen; main thread): closed on the worker, after what still reads it. */
  static void closePack() {
    final long old = handle;
    if (old != 0) WORKER.execute(() -> PackerNative.close(old));
    handle = 0;
    summary = null;
    source = null;
    estimates = null;
    choices = new int[0];
    state = EMPTY;
  }
}
