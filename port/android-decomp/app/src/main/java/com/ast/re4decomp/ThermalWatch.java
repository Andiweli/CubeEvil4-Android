package com.ast.re4decomp;

import android.content.Context;
import android.os.Build;
import android.os.PowerManager;
import android.util.Log;
import java.util.ArrayDeque;
import java.util.Locale;

/**
 * How hot the device is, for the error log and the report (decision of the project owner, 14.11.2026, 0.99.15: "Ja,
 * einbauen"; a user's idea of thermal-aware performance). Nothing is changed by it: it only says, so that a report of
 * "after 20 minutes it gets slow" shows whether the device was hot and Android held it back.
 *
 * <p>Android's thermal status (PowerManager, Android 10 and later: none, light, moderate, severe, critical, emergency,
 * shutdown) is logged at the start and at each change. Its headroom (Android 11 and later: how near the device is to
 * severe throttling, 1.0 = there; a forecast of 10 seconds; Android allows a call about once a second, else NaN) is read
 * every 10 seconds while the game runs and logged when it moved by 0.05 or more (or once a minute while it is 0.7 or
 * more). The last 15 minutes of readings and every change of the status go into the report as thermal.txt.
 */
final class ThermalWatch {
  static final String TAG = "RE4Thermal";
  /** Between two readings of the headroom. */
  static final long EVERY_MS = 10_000;
  /** Readings kept for the report (15 minutes). */
  static final int KEEP = 90;
  static final float LOG_STEP = 0.05f, HOT = 0.7f;
  static final long HOT_LOG_MS = 60_000;
  static final int FORECAST_S = 10;

  /** Hears every new status and every reading (CoreActivity: on to the renderer, so that Dynamic's lines name the
   *  heat: CoreBridge.setThermal). On the main thread. */
  interface Listener {
    void heat(int status, float headroom);
  }

  /** Where the figures come from (Android's PowerManager; a fake one in the tests). */
  interface Source {
    /** The thermal status (PowerManager.THERMAL_STATUS_*), -1 unknown (before Android 10). */
    int status();
    /** The headroom in `forecastSeconds`, NaN unknown (before Android 11, too soon after the last call, no sensor). */
    float headroom(int forecastSeconds);
  }

  private final Source source;
  private final ArrayDeque<String> readings = new ArrayDeque<>();
  private final ArrayDeque<String> changes = new ArrayDeque<>();
  private final long started;
  private int status = Integer.MIN_VALUE;
  private float headroom = Float.NaN, logged = Float.NaN, peak = Float.NaN;
  private long lastReading = Long.MIN_VALUE / 2, lastLogged = Long.MIN_VALUE / 2;
  private int peakStatus = -1;
  private long peakStatusAt = -1, peakAt = -1;
  private int missing;            // readings without a value so far, none with one
  private boolean saidMissing;    // "not available" said once
  private Listener heat;
  private Object listener;  // PowerManager.OnThermalStatusChangedListener (Android 10)
  private PowerManager power;

  ThermalWatch(Source source, long now) {
    this.source = source;
    this.started = now;
  }

  /** Android's figures for `context`'s device. */
  static ThermalWatch forDevice(Context context, long now) {
    final PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
    ThermalWatch w = new ThermalWatch(new Source() {
      @Override public int status() {
        if (pm == null || Build.VERSION.SDK_INT < 29) return -1;
        try {
          return pm.getCurrentThermalStatus();
        } catch (RuntimeException e) {
          return -1;
        }
      }

      @Override public float headroom(int forecastSeconds) {
        if (pm == null || Build.VERSION.SDK_INT < 30) return Float.NaN;
        try {
          return pm.getThermalHeadroom(forecastSeconds);
        } catch (RuntimeException e) {
          return Float.NaN;
        }
      }
    }, now);
    w.power = pm;
    return w;
  }

  /** Who hears the figures (null: nobody). */
  synchronized void setListener(Listener l) {
    heat = l;
    if (l != null && status != Integer.MIN_VALUE) l.heat(status, headroom);
  }

  /** The status now (a line), and Android's calls at each change from now on (Android 10 and later). */
  synchronized void start(long now) {
    statusChanged(source.status(), now);
    if (power != null && Build.VERSION.SDK_INT >= 29 && listener == null) {
      PowerManager.OnThermalStatusChangedListener l = s -> statusChanged(s, android.os.SystemClock.elapsedRealtime());
      try {
        power.addThermalStatusListener(l);
        listener = l;
      } catch (RuntimeException e) {
        Log.w(TAG, "thermal: no listener (" + e + ")");
      }
    }
  }

  synchronized void stop() {
    if (power != null && Build.VERSION.SDK_INT >= 29 && listener != null) {
      try {
        power.removeThermalStatusListener((PowerManager.OnThermalStatusChangedListener) listener);
      } catch (RuntimeException ignored) {
      }
    }
    listener = null;
  }

  /** A new status (Android's call, or the start): a line and an entry for the report. */
  synchronized void statusChanged(int s, long now) {
    if (s == status) return;
    final boolean first = status == Integer.MIN_VALUE;
    status = s;
    if (s > peakStatus) {
      peakStatus = s;
      peakStatusAt = now;
    }
    if (s < 0) {
      // said once: Android 9 and older have no thermal status
      Log.i(TAG, "thermal: status not available (Android 9 or older); the app keeps playing as before");
    } else {
      remember(changes, clock(now) + "  status " + statusName(s));
      Log.i(TAG, first ? "thermal: status " + statusName(s) + " at the start"
          : "thermal: status " + statusName(s) + " after " + minutes(now - started)
              + (Float.isNaN(headroom) ? "" : ", headroom " + headroomText(headroom)));
    }
    if (heat != null) heat.heat(status, headroom);
  }

  /** Twice a second from CoreActivity's tick: the headroom every EVERY_MS. */
  synchronized void tick(long now) {
    if (now - lastReading < EVERY_MS) return;
    lastReading = now;
    final float h = source.headroom(FORECAST_S);
    if (Float.isNaN(h)) {
      // said once when the device never gives one (Android 10 and older, or no value from its sensors)
      if (Float.isNaN(peak) && ++missing >= 3 && !saidMissing) {
        saidMissing = true;
        Log.i(TAG, "thermal: headroom not available on this device (Android 10 or older, or no value from its sensors)");
      }
      return;
    }
    headroom = h;
    if (Float.isNaN(peak) || h > peak) {
      peak = h;
      peakAt = now;
    }
    if (heat != null) heat.heat(status, h);
    remember(readings, clock(now) + "  headroom " + headroomText(h) + "  status " + statusName(status));
    final boolean moved = Float.isNaN(logged) || Math.abs(h - logged) >= LOG_STEP;
    final boolean hotAgain = h >= HOT && now - lastLogged >= HOT_LOG_MS;
    if (moved || hotAgain) {
      logged = h;
      lastLogged = now;
      Log.i(TAG, "thermal: headroom " + headroomText(h) + " (1.0: Android throttles hard), status " + statusName(status)
          + ", after " + minutes(now - started));
    }
  }

  /** The report's thermal.txt. */
  synchronized String report(long now) {
    StringBuilder t = new StringBuilder();
    t.append("Thermal status now: ").append(statusName(status < -1 ? source.status() : status))
        .append(", the highest since the start: ").append(statusName(peakStatus))
        .append(peakStatusAt >= 0 && peakStatus >= 0 ? " (at " + clock(peakStatusAt) + ")" : "").append('\n');
    t.append("Headroom (1.0: Android throttles hard; ").append(FORECAST_S).append(" s ahead): last ")
        .append(headroomText(headroom)).append(", highest ").append(headroomText(peak))
        .append(peakAt >= 0 ? " (at " + clock(peakAt) + ")" : "").append('\n');
    t.append("(times since the game started; the error log's lines \"thermal:\" and Dynamic's \"level ... ; heat:\" "
        + "lines have the clock)\n");
    t.append("Game running for ").append(minutes(now - started)).append('\n');
    t.append("\nStatus changes:\n");
    if (changes.isEmpty()) t.append("  (none)\n");
    for (String c : changes) t.append("  ").append(c).append('\n');
    t.append("\nReadings every ").append(EVERY_MS / 1000).append(" s (the last ").append(KEEP).append("):\n");
    if (readings.isEmpty()) t.append("  (none: Android 10 or older, or the device has no headroom)\n");
    for (String r : readings) t.append("  ").append(r).append('\n');
    return t.toString();
  }

  private void remember(ArrayDeque<String> list, String line) {
    list.addLast(line);
    while (list.size() > KEEP) list.removeFirst();
  }

  private String clock(long now) {
    return "+" + minutes(now - started);
  }

  /** "12:05" minutes and seconds since the start. */
  static String minutes(long ms) {
    long s = Math.max(0, ms) / 1000;
    return String.format(Locale.ROOT, "%d:%02d", s / 60, s % 60);
  }

  /** "; heat: moderate, headroom 0.71" for a line of Dynamic ("" when nothing is known). */
  static String heatText(int status, float headroom) {
    if (status < 0 && Float.isNaN(headroom)) return "";
    return "; heat: " + statusName(status) + (Float.isNaN(headroom) ? "" : ", headroom " + headroomText(headroom));
  }

  static String headroomText(float h) {
    return Float.isNaN(h) ? "unknown" : String.format(Locale.ROOT, "%.2f", h);
  }

  /** PowerManager.THERMAL_STATUS_* by name. */
  static String statusName(int s) {
    switch (s) {
      case 0: return "none";
      case 1: return "light";
      case 2: return "moderate";
      case 3: return "severe";
      case 4: return "critical";
      case 5: return "emergency";
      case 6: return "shutdown";
      default: return "unknown";
    }
  }
}
