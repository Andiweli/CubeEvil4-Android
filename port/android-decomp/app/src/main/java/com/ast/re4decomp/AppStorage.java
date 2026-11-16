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
 *
 * <p>The flavour "sideload" (1.0, DataFolder) can have a folder of the player's choice instead: taken while it can be
 * used; else the folder above for this run (fallback says why, the start screen shows it), the choice kept.
 */
final class AppStorage {
  private static final String TAG = "RE4Storage";
  private static File chosen;
  private static String fallback;

  private AppStorage() {}

  static synchronized File base(Context c) {
    if (chosen != null) return chosen;
    if (DataFolder.enabled) {
      String[] why = new String[1];
      File custom = DataFolder.usable(c, why);
      if (custom != null) {
        chosen = custom;
        Log.i(TAG, "data folder: the player's folder " + custom);
        return chosen;
      }
      fallback = why[0];
    }
    chosen = standard(c);
    return chosen;
  }

  /** The app's own data folder (on the shared storage, else internal), whatever the player chose. */
  static File standard(Context c) {
    File internal = c.getFilesDir();
    File external = null;
    if (!new File(internal, "game").isDirectory()) {
      try {
        external = c.getExternalFilesDir(null);
      } catch (RuntimeException e) {
        Log.w(TAG, "the app's folder on the shared storage: " + e);
      }
    }
    if (external != null) return external;
    Log.w(TAG, "data folder: the app's internal folder " + internal);
    return internal;
  }

  /** The player's folder could not be used at this start (DataFolder.usable): why; null when none or it is used. */
  static synchronized String fallback(Context c) {
    base(c);
    return fallback;
  }

  /** Tests: choose again. */
  static synchronized void forget() {
    chosen = null;
    fallback = null;
  }

  /** For the error report: which folder holds the data. */
  static String describe(Context c) {
    File base = base(c);
    String chosenPath = DataFolder.chosen(c);
    if (chosenPath != null && base.getAbsolutePath().equals(chosenPath)) return "the player's folder, " + base;
    String note = chosenPath == null ? "" : " (the player's folder " + chosenPath + " cannot be used: " + fallback(c) + ")";
    return (base.equals(c.getFilesDir()) ? "the app's internal folder (the shared storage's app folder was unavailable)"
        : "the app's folder on the shared storage") + ", " + base + note;
  }
}
