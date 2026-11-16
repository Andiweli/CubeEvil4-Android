package com.ast.re4decomp;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.os.Build;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Whether this APK is one of the project owner's own releases (0.99.19, the project owner, 16.11.2026: "Nur der Hinweis
 * reicht. Den Debug Build nimm aber raus. Es soll nur mit Release Versionen geprüft werden."). Copies of CubeEvil4 turn
 * up on other sites and game platforms; a release build signed with another key than the owner's shows a calm note on
 * the start screen (MainActivity, R.string.core_unofficial) and the same text as one line in the error log
 * (CoreLog.deviceText). Nothing is locked and the game is unchanged.
 *
 * Only release builds are checked: a debug build (Android Studio's coreDebug, debuggable, RealtimeShadows.built) never
 * reads its signature and never shows the note. A signature Android does not give (or that cannot be read) shows no
 * note either: no false alarm.
 */
final class OfficialBuild {
  /** The SHA-256 of the release signing certificates (their DER form, as `apksigner verify --print-certs` shows it),
   *  lower case. A later key is added to the list; any of them counts. */
  static final String[] RELEASE_CERTS = {
      // the releases' key (read from the published CubeEvil4-0.99.18-Android.apk, 16.11.2026)
      "a3810c7ae720118333f68c55f49cd4145fd4a02ec01127f8cc6256d6a507f41a",
  };

  private static Boolean cached;  // the answer for this process (the APK does not change while it runs)

  private OfficialBuild() {}

  /** The decision: the note for a release build (not `debuggable`) whose signing certificates (`signers`, SHA-256 in
   *  hex) are all unknown; none when the build is debuggable or the certificates could not be read (null, empty). */
  static boolean noticeFor(boolean debuggable, List<String> signers) {
    if (debuggable || signers == null || signers.isEmpty()) return false;
    for (String signer : signers) {
      for (String known : RELEASE_CERTS) {
        if (known.equalsIgnoreCase(signer)) return false;
      }
    }
    return true;
  }

  /** This APK: a release build signed with an unknown key (once per process; a debug build reads nothing). */
  static synchronized boolean unofficial(Context c) {
    if (cached == null) {
      boolean debug = RealtimeShadows.built(c);
      cached = noticeFor(debug, debug ? null : signers(c));
    }
    return cached;
  }

  /** (tests) the next unofficial() asks again */
  static synchronized void forget() {
    cached = null;
  }

  /** The SHA-256 (hex) of every certificate this APK is signed with: from Android 9 on all signers of the APK, or,
   *  with one signer, its certificate and those it rotated from; before that the signatures. null when Android gives
   *  none or they cannot be read. */
  @SuppressWarnings("deprecation")  // GET_SIGNATURES / signatures: what Android before 9 has
  static List<String> signers(Context c) {
    try {
      PackageManager pm = c.getPackageManager();
      Signature[] certs;
      if (Build.VERSION.SDK_INT >= 28) {
        PackageInfo p = pm.getPackageInfo(c.getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES);
        if (p.signingInfo == null) return null;
        certs = p.signingInfo.hasMultipleSigners() ? p.signingInfo.getApkContentsSigners()
            : p.signingInfo.getSigningCertificateHistory();
      } else {
        certs = pm.getPackageInfo(c.getPackageName(), PackageManager.GET_SIGNATURES).signatures;
      }
      if (certs == null) return null;
      List<String> out = new ArrayList<>();
      for (Signature cert : certs) {
        if (cert != null) out.add(sha256(cert.toByteArray()));
      }
      return out.isEmpty() ? null : out;
    } catch (Exception | LinkageError e) {
      return null;
    }
  }

  static String sha256(byte[] data) throws java.security.NoSuchAlgorithmException {
    StringBuilder b = new StringBuilder();
    for (byte x : MessageDigest.getInstance("SHA-256").digest(data)) b.append(String.format(Locale.ROOT, "%02x", x));
    return b.toString();
  }
}
