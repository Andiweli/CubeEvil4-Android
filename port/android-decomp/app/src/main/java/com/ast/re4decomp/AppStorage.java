package com.ast.re4decomp;

import android.content.Context;
import android.util.Log;
import java.io.File;

/**
 * The app's data folder: its folder on the shared storage (Android/data/<app>/files: the discs, the
 * memory cards, the error log, graphics captures), else its internal folder (a tester's report,
 * 03.11.2026: on some Android 16 builds getExternalFilesDir gives null or throws, as the system never
 * sets sys.user.0.ce_available at boot, issuetracker.google.com/issues/460912704; START GAME and the
 * imports stood disabled). Once the internal folder holds the game ("game"), it stays the data folder,
 * also when the shared one works again, so the discs and cards imported there are not lost.
 */
final class AppStorage {
  private static final String TAG = "RE4Storage";
  private static File chosen;

  private AppStorage() {}

  static synchronized File base(Context c) {
    if (chosen != null) return chosen;
    File internal = c.getFilesDir();
    File external = null;
    if (!new File(internal, "game").isDirectory()) {
      try {
        external = c.getExternalFilesDir(null);
      } catch (RuntimeException e) {
        Log.w(TAG, "the app's folder on the shared storage: " + e);
      }
    }
    if (external != null) {
      chosen = external;
    } else {
      chosen = internal;
      Log.w(TAG, "data folder: the app's internal folder " + internal);
    }
    return chosen;
  }

  /** Tests: choose again. */
  static synchronized void forget() { chosen = null; }

  /** For the error report: which folder holds the data. */
  static String describe(Context c) {
    File base = base(c);
    return (base.equals(c.getFilesDir()) ? "the app's internal folder (the shared storage's app folder was unavailable)"
        : "the app's folder on the shared storage") + ", " + base;
  }
}
