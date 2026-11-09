package com.ast.re4decomp;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * The crash guard of a Vulkan start (vulkan-bauplan.md E7). A start with Vulkan sets a mark (its process and time);
 * the mark goes once Vulkan has drawn for a few seconds, and when the game ends normally or goes to the background.
 * A mark still there at the next start means that start ended early. Since 0.99.6 (a user's report with Android 17:
 * the message came after the player had quit the game) only a real crash counts: from Android 11 on the system names
 * why that process ended (ApplicationExitInfo), and only a crash, a native crash, a hang (ANR) or a failed start sends
 * the game to OpenGL ES; the player quitting, Android ending the app in the background or for memory do not. Android
 * 10 names no reason: there the mark alone counts, as before.
 */
final class VulkanGuard {
  // the mark (GraphicsPrefs): set, its process, its time (System.currentTimeMillis); how often in a row the system
  // knew no end of the marked process (twice: counted as a crash)
  static final String STARTING = "vulkanStarting", PID = "vulkanStartingPid", AT = "vulkanStartingAt",
      UNSURE = "vulkanStartingUnsure";
  // ApplicationExitInfo.REASON_* (API 30; as numbers, so the decision is a plain function the tests run)
  static final int REASON_UNKNOWN = 0, REASON_EXIT_SELF = 1, REASON_SIGNALED = 2, REASON_LOW_MEMORY = 3,
      REASON_CRASH = 4, REASON_CRASH_NATIVE = 5, REASON_ANR = 6, REASON_INITIALIZATION_FAILURE = 7,
      REASON_USER_REQUESTED = 10, REASON_USER_STOPPED = 11;
  // an end of the marked process is the one after the mark (the system's time a little before ours is fine)
  static final long SLACK_MS = 10_000;

  private VulkanGuard() {}

  /** How a process ended, as the system records it (ApplicationExitInfo). */
  static final class Exit {
    final int pid, reason, status;
    final long at;
    final String description;

    Exit(int pid, int reason, int status, long at, String description) {
      this.pid = pid;
      this.reason = reason;
      this.status = status;
      this.at = at;
      this.description = description == null ? "" : description;
    }

    @Override public String toString() {
      return "pid " + pid + ", " + reasonName(reason) + (status != 0 ? " (status " + status + ")" : "")
          + (description.isEmpty() ? "" : ", \"" + description + "\"") + ", at " + time(at);
    }
  }

  /** What the mark of the last start says. */
  static final class Verdict {
    final boolean fallBack;  // OpenGL ES now, with the message
    final boolean unsure;    // the system knew no end of the marked process (counted, UNSURE)
    final String why;        // for the log

    Verdict(boolean fallBack, boolean unsure, String why) {
      this.fallBack = fallBack;
      this.unsure = unsure;
      this.why = why;
    }
  }

  /** A crash while starting: an exception, a native crash (the GPU driver), a hang, a failed start. */
  static boolean crash(int reason) {
    return reason == REASON_CRASH || reason == REASON_CRASH_NATIVE || reason == REASON_ANR
        || reason == REASON_INITIALIZATION_FAILURE;
  }

  /**
   * The mark of the last start (`marked`: still there; its process and time) against how that process ended (`exit`,
   * the system's newest record of that process; null: none, or Android before 11, `sdk`); `unsure`: how often in a
   * row before the system knew no end.
   */
  static Verdict judge(boolean marked, int markPid, long markAt, int sdk, Exit exit, int unsure) {
    if (!marked) return new Verdict(false, false, "no mark");
    if (sdk < 30) {
      return new Verdict(true, false, "the mark of the last start was still there (Android " + sdk
          + " names no reason why the game ended)");
    }
    if (exit == null || exit.pid != markPid || exit.at + SLACK_MS < markAt) {
      // (twice in a row: a start that keeps ending without a record, OpenGL ES to be safe)
      if (unsure >= 1) {
        return new Verdict(true, true, "the mark of the last start was still there, and again the system names no end "
            + "of its process " + markPid);
      }
      return new Verdict(false, true, "the mark of the last start was still there, but the system names no end of its "
          + "process " + markPid + (exit != null ? " (its newest record: " + exit + ")" : "") + ": not counted as a crash");
    }
    if (crash(exit.reason)) {
      return new Verdict(true, false, "the last start with Vulkan crashed: " + exit);
    }
    return new Verdict(false, false, "the last start with Vulkan ended without a crash: " + exit);
  }

  static String reasonName(int reason) {
    switch (reason) {
      case REASON_EXIT_SELF: return "exit self";
      case REASON_SIGNALED: return "signaled";
      case REASON_LOW_MEMORY: return "low memory";
      case REASON_CRASH: return "crash";
      case REASON_CRASH_NATIVE: return "native crash";
      case REASON_ANR: return "not responding (ANR)";
      case REASON_INITIALIZATION_FAILURE: return "initialization failure";
      case 8: return "permission change";
      case 9: return "excessive resource use";
      case REASON_USER_REQUESTED: return "user requested";
      case REASON_USER_STOPPED: return "user stopped";
      case 12: return "dependency died";
      case 13: return "other";
      case 14: return "freezer";
      case 15: return "package state change";
      case 16: return "package updated";
      default: return "unknown (" + reason + ")";
    }
  }

  static String time(long at) {
    return at <= 0 ? "?" : new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date(at));
  }

  /**
   * The system's newest record of how this app's process `pid` ended (0: its newest of the game's process, not
   * the restart helper's); null before Android 11 or without one.
   */
  static Exit lastExit(Context c, int pid) {
    if (Build.VERSION.SDK_INT < 30) return null;
    try {
      ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
      if (am == null) return null;
      List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(null, pid, pid != 0 ? 1 : 8);
      for (ApplicationExitInfo e : list) {
        // (the restart helper runs in a process of its own, ":restart")
        if (pid == 0 && e.getProcessName() != null && !e.getProcessName().equals(c.getPackageName())) continue;
        return new Exit(e.getPid(), e.getReason(), e.getStatus(), e.getTimestamp(), e.getDescription());
      }
    } catch (RuntimeException e) {
      return null;
    }
    return null;
  }
}
