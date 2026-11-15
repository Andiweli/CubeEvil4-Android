package com.ast.cubeevil4.ouya;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.net.URL;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * The menu's update button (wish of the project owner, 15.11.2026; the main app's AppUpdate): the OUYA APK lies in
 * CubeEvil4's releases on GitHub as "CubeEvil4-<version>-OUYA.apk" (0.99.16-rc: CubeEvil4-0.99-OUYA.apk); the newest
 * release with one names the OUYA version, compared with the installed one ("0.99-Ouya": the numbers). Newer: "Update"
 * (red), which opens that release's page; else "Up to date" (green, off). Without an answer (no internet, the OUYA's
 * old TLS) the last answer stays, at first "Up to date" (wish of the project owner, 15.11.2026). Asked once per run.
 * Android 4.1 offers TLS 1.2 but does not switch it on: GitHub needs it, the connection here turns it on.
 */
final class OuyaUpdate {
    static final String TAG = "RE4OUYA";
    static final String RELEASES_API = "https://api.github.com/repos/Andiweli/CubeEvil4-Android/releases?per_page=15";
    static final String RELEASES = "https://github.com/Andiweli/CubeEvil4-Android/releases/latest";
    static final int CURRENT = 1, OLDER = 2;
    // "CubeEvil4-0.99-OUYA.apk" (the version before the name) or "CubeEvil4-Potato-1.0.apk" (after it)
    private static final Pattern OUYA_APK = Pattern.compile("(?i)cubeevil4[-_ ]?v?([0-9][0-9.]*)[-_ ]?(ouya|potato).*\\.apk");
    private static final Pattern POTATO_APK = Pattern.compile("(?i)cubeevil4[-_ ]?(?:ouya|potato)[-_ ]?v?([0-9][0-9.]*).*\\.apk");
    private static final String PREFS = "update", STATE = "state", PAGE = "page";

    interface Listener {
        void checked(int state);
    }

    private static boolean asked;
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    /** The last answer (kept across runs): OLDER or CURRENT (also when there never was one). */
    static int state(Context c) {
        return prefs(c).getInt(STATE, CURRENT) == OLDER ? OLDER : CURRENT;
    }

    /** The release page to open (the last answer's, else the newest release). */
    static String page(Context c) {
        String page = prefs(c).getString(PAGE, "");
        return page.startsWith("https://github.com/") ? page : RELEASES;
    }

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    /** The installed version's numbers ("0.99-Ouya": "0.99") */
    static String installed(Context c) {
        try {
            String v = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName;
            return numbers(v);
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            return null;
        }
    }

    private static String numbers(String v) {
        if (v == null)
            return null;
        Matcher m = Pattern.compile("([0-9]+(\\.[0-9]+)*)").matcher(v);
        return m.find() ? m.group(1) : null;
    }

    /** -1, 0, 1: "0.99" against "1.0" part by part (a missing part is 0) */
    static int compare(String a, String b) {
        String[] p = a.split("\\."), q = b.split("\\.");
        for (int i = 0; i < Math.max(p.length, q.length); i++) {
            long m = i < p.length ? Long.parseLong(p[i]) : 0, n = i < q.length ? Long.parseLong(q[i]) : 0;
            if (m != n)
                return m < n ? -1 : 1;
        }
        return 0;
    }

    /** Asks GitHub once per run (a thread of its own); `listener` hears an answer, not a failure. */
    static void check(final Context context, final Listener listener) {
        if (asked)
            return;
        asked = true;
        final Context app = context.getApplicationContext();
        final String mine = installed(app);
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    JSONArray releases = new JSONArray(get(RELEASES_API));
                    String version = null, page = null;
                    for (int r = 0; r < releases.length() && version == null; r++) {
                        JSONObject release = releases.getJSONObject(r);
                        if (release.optBoolean("draft"))
                            continue;
                        JSONArray assets = release.optJSONArray("assets");
                        for (int i = 0; assets != null && i < assets.length() && version == null; i++) {
                            String name = assets.getJSONObject(i).optString("name");
                            Matcher m = OUYA_APK.matcher(name);
                            if (!m.matches())
                                m = POTATO_APK.matcher(name);
                            if (m.matches()) {
                                version = m.group(1).replaceAll("\\.$", "");
                                page = release.optString("html_url", "");
                            }
                        }
                    }
                    if (version == null || mine == null)
                        throw new IOException("no OUYA APK in the newest releases");
                    final int state = compare(mine, version) < 0 ? OLDER : CURRENT;
                    prefs(app).edit().putInt(STATE, state).putString(PAGE, page == null ? "" : page).apply();
                    Log.i(TAG, "update check: the newest OUYA APK on GitHub is " + version + ", installed " + mine
                            + (state == OLDER ? ": an update" : ": up to date"));
                    MAIN.post(new Runnable() {
                        @Override
                        public void run() {
                            listener.checked(state);
                        }
                    });
                } catch (Exception e) {
                    asked = false;  // the next menu asks again
                    Log.i(TAG, "update check: no answer from GitHub (" + e + "), the last answer stays");
                }
            }
        }, "update-check").start();
    }

    private static String get(String address) throws IOException {
        HttpsURLConnection c = (HttpsURLConnection) new URL(address).openConnection();
        c.setSSLSocketFactory(new Tls12(c.getSSLSocketFactory()));
        c.setConnectTimeout(8000);
        c.setReadTimeout(8000);
        c.setRequestProperty("Accept", "application/vnd.github+json");
        c.setRequestProperty("User-Agent", "CubeEvil4-Potato");
        InputStream in = c.getInputStream();
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            for (int n; (n = in.read(buffer)) > 0; )
                out.write(buffer, 0, n);
            return out.toString("UTF-8");
        } finally {
            in.close();
            c.disconnect();
        }
    }

    /** The system's sockets with TLS 1.2 and 1.1 switched on (Android 4.1 to 4.4 have them, off) */
    private static final class Tls12 extends SSLSocketFactory {
        private final SSLSocketFactory base;

        Tls12(SSLSocketFactory base) {
            this.base = base;
        }

        private Socket on(Socket s) {
            if (s instanceof SSLSocket) {
                SSLSocket ssl = (SSLSocket) s;
                List<String> want = new ArrayList<String>();
                List<String> supported = Arrays.asList(ssl.getSupportedProtocols());
                for (String p : new String[] {"TLSv1.2", "TLSv1.1", "TLSv1"})
                    if (supported.contains(p))
                        want.add(p);
                if (!want.isEmpty())
                    ssl.setEnabledProtocols(want.toArray(new String[want.size()]));
            }
            return s;
        }

        @Override
        public String[] getDefaultCipherSuites() {
            return base.getDefaultCipherSuites();
        }

        @Override
        public String[] getSupportedCipherSuites() {
            return base.getSupportedCipherSuites();
        }

        @Override
        public Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException {
            return on(base.createSocket(s, host, port, autoClose));
        }

        @Override
        public Socket createSocket(String host, int port) throws IOException {
            return on(base.createSocket(host, port));
        }

        @Override
        public Socket createSocket(String host, int port, InetAddress local, int localPort) throws IOException {
            return on(base.createSocket(host, port, local, localPort));
        }

        @Override
        public Socket createSocket(InetAddress host, int port) throws IOException {
            return on(base.createSocket(host, port));
        }

        @Override
        public Socket createSocket(InetAddress address, int port, InetAddress local, int localPort) throws IOException {
            return on(base.createSocket(address, port, local, localPort));
        }
    }
}
