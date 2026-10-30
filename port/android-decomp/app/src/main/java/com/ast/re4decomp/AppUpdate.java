package com.ast.re4decomp;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageInstaller;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * The in-game menu's update button (wish of the project owner, 30.10.2026): the newest release on GitHub
 * (SOURCE's releases, its tag the version, its one .apk) against the installed version. The same: "Up to
 * date", green and off; the installed one older: "Update", red, which downloads the APK into Android's
 * package installer, and Android asks before it installs (the game closes then). Asked once per run, in
 * the background; without the internet the button stays hidden.
 */
final class AppUpdate {
  static final String TAG = "RE4Update";
  static final String LATEST = "https://api.github.com/repos/Andiweli/CubeEvil4-Android/releases/latest";
  private static final String ACTION = "com.ast.re4decomp.UPDATE_INSTALLED";
  /** The check's answer: not known (yet, or no internet), the same version (or newer), an older one. */
  static final int UNKNOWN = 0, CURRENT = 1, OLDER = 2;

  interface Listener {
    /** The check answered (main thread). */
    void checked(int state);
    /** The download's progress in percent, -1 when it stopped with `problem` (main thread). */
    void progress(int percent, String problem);
  }

  private static int state = UNKNOWN;
  private static String latest, apkUrl;
  private static long apkSize;
  private static boolean asked, loading;
  private static final Handler MAIN = new Handler(Looper.getMainLooper());

  static int state() { return state; }
  static boolean loading() { return loading; }

  /** The installed version as the releases name it ("0.95.2-beta", the core build's "-core" left out). */
  static String installed(Context c) {
    try {
      String v = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName;
      return v != null && v.endsWith("-core") ? v.substring(0, v.length() - 5) : v;
    } catch (android.content.pm.PackageManager.NameNotFoundException e) { return null; }
  }

  /**
   * "0.95.2-beta" against "0.96.0-beta": the numbers part by part, then a version without a suffix
   * ahead of one with it ("1.0" after "1.0-beta"), then the suffixes; a leading "v" does not count.
   */
  static int compare(String a, String b) {
    String[] x = split(a), y = split(b);
    String[] p = x[0].split("\\."), q = y[0].split("\\.");
    for (int i = 0; i < Math.max(p.length, q.length); i++) {
      int m = i < p.length ? number(p[i]) : 0, n = i < q.length ? number(q[i]) : 0;
      if (m != n) return m < n ? -1 : 1;
    }
    if (x[1].isEmpty() != y[1].isEmpty()) return x[1].isEmpty() ? 1 : -1;
    return Integer.signum(x[1].compareTo(y[1]));
  }

  private static String[] split(String v) {
    v = v == null ? "" : v.trim();
    if (v.startsWith("v") || v.startsWith("V")) v = v.substring(1);
    int dash = v.indexOf('-');
    return dash < 0 ? new String[] {v, ""} : new String[] {v.substring(0, dash), v.substring(dash + 1)};
  }

  private static int number(String s) {
    try { return Integer.parseInt(s.trim()); } catch (NumberFormatException e) { return 0; }
  }

  /** Asks GitHub once per run (a thread of its own); `listener` hears the answer. */
  static void check(Context context, Listener listener) {
    if (asked) {
      if (state != UNKNOWN) listener.checked(state);
      return;
    }
    asked = true;
    final String mine = installed(context);
    new Thread(() -> {
      int answer = UNKNOWN;
      try {
        JSONObject release = new JSONObject(get(LATEST));
        String tag = release.getString("tag_name");
        JSONArray assets = release.getJSONArray("assets");
        String url = null; long size = 0;
        for (int i = 0; i < assets.length(); i++) {
          JSONObject a = assets.getJSONObject(i);
          if (a.getString("name").endsWith(".apk")) { url = a.getString("browser_download_url"); size = a.optLong("size"); break; }
        }
        if (mine != null) answer = compare(mine, tag) < 0 && url != null ? OLDER : CURRENT;
        latest = tag; apkUrl = url; apkSize = size;
        Log.i(TAG, "GitHub's latest release " + tag + ", installed " + mine
            + (answer == OLDER ? ": an update" : ": up to date"));
      } catch (Exception e) {
        asked = false;  // the next menu asks again
        Log.i(TAG, "no answer from GitHub: " + e);
      }
      final int result = answer;
      MAIN.post(() -> { state = result; if (result != UNKNOWN) listener.checked(result); });
    }, "update-check").start();
  }

  private static String get(String address) throws Exception {
    HttpURLConnection c = (HttpURLConnection) new URL(address).openConnection();
    c.setConnectTimeout(8000); c.setReadTimeout(8000);
    c.setRequestProperty("Accept", "application/vnd.github+json");
    try (InputStream in = c.getInputStream()) {
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      byte[] buffer = new byte[8192];
      for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
      return out.toString("UTF-8");
    } finally { c.disconnect(); }
  }

  /**
   * The button: Android's permission to install first (its settings page when missing, the player comes
   * back and presses again), then the APK into a session of the package installer, which asks.
   */
  static void install(Activity activity, Listener listener) {
    if (loading || apkUrl == null) return;
    if (Build.VERSION.SDK_INT >= 26 && !activity.getPackageManager().canRequestPackageInstalls()) {
      listener.progress(-1, activity.getString(R.string.ingame_update_allow));
      try {
        activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
            Uri.parse("package:" + activity.getPackageName())));
      } catch (Exception e) { Log.w(TAG, "no settings page for installing apps: " + e); }
      return;
    }
    loading = true;
    final Context app = activity.getApplicationContext();
    final String url = apkUrl, version = latest;
    final long size = apkSize;
    listener.progress(0, null);
    new Thread(() -> {
      PackageInstaller installer = app.getPackageManager().getPackageInstaller();
      int session = -1;
      try {
        PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
            PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(app.getPackageName());
        if (size > 0) params.setSize(size);
        session = installer.createSession(params);
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(10000); c.setReadTimeout(20000);
        try (PackageInstaller.Session s = installer.openSession(session)) {
          long total = size > 0 ? size : c.getContentLength(), done = 0;
          try (InputStream in = c.getInputStream();
               OutputStream out = s.openWrite("CubeEvil4-" + version + ".apk", 0, size > 0 ? size : -1)) {
            int shown = 0;
            byte[] buffer = new byte[65536];
            for (int n; (n = in.read(buffer)) > 0; ) {
              out.write(buffer, 0, n); done += n;
              int percent = total > 0 ? (int) Math.min(99, done * 100 / total) : 0;
              if (percent != shown) { shown = percent; MAIN.post(() -> listener.progress(percent, null)); }
            }
            s.fsync(out);
          }
          listenForResult(app, listener);
          Intent answer = new Intent(ACTION).setPackage(app.getPackageName());
          int flags = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
          s.commit(PendingIntent.getBroadcast(app, session, answer, flags).getIntentSender());
          Log.i(TAG, "downloaded " + version + " (" + done + " bytes), Android installs it");
          MAIN.post(() -> listener.progress(100, null));
        } finally { c.disconnect(); }
      } catch (Exception e) {
        Log.w(TAG, "update failed: " + e);
        if (session >= 0) try { installer.abandonSession(session); } catch (Exception ignored) {}
        final String why = String.valueOf(e.getMessage());
        MAIN.post(() -> { loading = false; listener.progress(-1, app.getString(R.string.ingame_update_failed, why)); });
      }
    }, "update-download").start();
  }

  /** The installer's answer: its question to the player (an activity of its own), or why it stopped. */
  private static void listenForResult(Context app, Listener listener) {
    BroadcastReceiver receiver = new BroadcastReceiver() {
      @Override public void onReceive(Context context, Intent intent) {
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
          Intent ask = intent.getParcelableExtra(Intent.EXTRA_INTENT);
          if (ask != null) context.startActivity(ask.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
          return;
        }
        context.unregisterReceiver(this);
        loading = false;
        if (status != PackageInstaller.STATUS_SUCCESS) {
          String why = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
          Log.w(TAG, "the installer stopped: " + status + " " + why);
          listener.progress(-1, context.getString(R.string.ingame_update_failed, String.valueOf(why)));
        }
      }
    };
    // before the commit (the answer can come at once); onReceive runs on the main thread
    IntentFilter filter = new IntentFilter(ACTION);
    if (Build.VERSION.SDK_INT >= 33) app.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
    else app.registerReceiver(receiver, filter);
  }
}
