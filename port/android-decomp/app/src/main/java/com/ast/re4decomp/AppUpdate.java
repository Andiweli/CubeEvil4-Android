package com.ast.re4decomp;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * The in-game menu's update button (wish of the project owner, 30.10.2026): the newest release on GitHub
 * (SOURCE's releases, its tag the version, its one .apk) against the installed version. The same: "Up to
 * date", green and off; the installed one older: "Update", red, which opens that release's page in the
 * system's browser (since 0.99.6; until then the app downloaded and installed the APK itself, which made Play
 * Protect warn). Asked once per run, in the background; without the internet the button stays hidden.
 */
final class AppUpdate {
  static final String TAG = "RE4Update";
  static final String LATEST = "https://api.github.com/repos/Andiweli/CubeEvil4-Android/releases/latest";
  /** The releases' page when GitHub's answer had none. */
  static final String RELEASES = "https://github.com/Andiweli/CubeEvil4-Android/releases/latest";
  /** The check's answer: not known (yet, or no internet), the same version (or newer), an older one. */
  static final int UNKNOWN = 0, CURRENT = 1, OLDER = 2;

  interface Listener {
    /** The check answered (main thread). */
    void checked(int state);
  }

  private static int state = UNKNOWN;
  private static String releasePage;
  private static boolean asked;
  private static final Handler MAIN = new Handler(Looper.getMainLooper());

  static int state() { return state; }

  /** The installed version as the releases name it ("0.95.2-beta", the core build's "-core" left out). */
  static String installed(Context c) {
    try {
      String v = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName;
      return v != null && v.endsWith("-core") ? v.substring(0, v.length() - 5) : v;
    } catch (android.content.pm.PackageManager.NameNotFoundException e) { return null; }
  }

  /**
   * "0.99.0-rc" against "0.99.1-rc": the numbers decide, part by part (a missing part is 0: "1.0" is "1.0.0").
   * With the same numbers only a version without a suffix is ahead of one with it ("1.0" after "1.0-rc"); the
   * same suffix word with a number counts that number ("1.0-rc10" after "1.0-rc2"); different words are the
   * same version ("0.99.0-beta" and "0.99.0-rc": the APK of 0.99.0 is named "-beta", its release "-rc";
   * until 0.99.0 the words were compared as text and "beta" stayed older than "rc", the update came again
   * after every install: owner's report 07.11.2026). Case, a leading "v" and the build's "-core" do not
   * count; a version that is no version ("", "latest") is never older.
   */
  static int compare(String a, String b) {
    String[] x = split(a), y = split(b);
    if (x == null || y == null) return 0;
    String[] p = x[0].split("\\."), q = y[0].split("\\.");
    for (int i = 0; i < Math.max(p.length, q.length); i++) {
      long m = i < p.length ? number(p[i]) : 0, n = i < q.length ? number(q[i]) : 0;
      if (m != n) return m < n ? -1 : 1;
    }
    if (x[1].isEmpty() != y[1].isEmpty()) return x[1].isEmpty() ? 1 : -1;
    if (x[1].isEmpty()) return 0;
    // the suffix: its word and the number at its end ("rc10": rc, 10; "beta": beta, none = 0)
    String[] s = suffix(x[1]), t = suffix(y[1]);
    if (!s[0].equals(t[0])) return 0;
    return Long.signum(number(s[1]) - number(t[1]));
  }

  /** {numbers, suffix} of a version, lower case, without "v", "-core" and blanks; null when it has no number. */
  private static String[] split(String v) {
    v = v == null ? "" : v.trim().toLowerCase(java.util.Locale.ROOT);
    if (v.startsWith("v")) v = v.substring(1).trim();
    if (v.endsWith("-core")) v = v.substring(0, v.length() - 5);
    int dash = v.indexOf('-');
    String numbers = dash < 0 ? v : v.substring(0, dash), rest = dash < 0 ? "" : v.substring(dash + 1);
    if (!numbers.matches("\\d+(\\.\\d+)*")) return null;
    return new String[] {numbers, rest};
  }

  /** "rc10" -> {"rc", "10"}, "beta" -> {"beta", ""}, "rc.2" / "rc-2" -> {"rc", "2"}. */
  private static String[] suffix(String s) {
    int end = s.length();
    while (end > 0 && Character.isDigit(s.charAt(end - 1))) end--;
    String word = s.substring(0, end).replaceAll("[^a-z]", "");
    return new String[] {word, s.substring(end)};
  }

  private static long number(String s) {
    try { return s.isEmpty() ? 0 : Long.parseLong(s.trim()); } catch (NumberFormatException e) { return 0; }
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
        // an update only with its APK there (a release still without it is not one yet)
        boolean apk = false;
        for (int i = 0; i < assets.length() && !apk; i++) apk = assets.getJSONObject(i).getString("name").endsWith(".apk");
        if (mine != null) answer = compare(mine, tag) < 0 && apk ? OLDER : CURRENT;
        String page = release.optString("html_url", "");
        releasePage = page.startsWith("https://github.com/") ? page : RELEASES;
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
   * The button (since 0.99.6, wish of the project owner, 09.11.2026: the download inside the app made Play Protect
   * warn): the newest release's page on GitHub in the system's browser; the player downloads and installs the APK
   * there.
   */
  static void open(Activity activity) {
    String page = releasePage != null ? releasePage : RELEASES;
    try {
      activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(page)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
      android.widget.Toast.makeText(activity, R.string.ingame_update_page, android.widget.Toast.LENGTH_LONG).show();
      Log.i(TAG, "opened " + page);
    } catch (ActivityNotFoundException e) {
      android.widget.Toast.makeText(activity, R.string.ingame_no_browser, android.widget.Toast.LENGTH_SHORT).show();
    }
  }
}
