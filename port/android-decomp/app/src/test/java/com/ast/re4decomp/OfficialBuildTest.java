package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.Signature;
import android.content.pm.SigningInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.shadow.api.Shadow;
import org.robolectric.shadows.ShadowSigningInfo;

/** The note for unofficial builds (OfficialBuild, 0.99.19): the decision, the reading of the APK's certificates and
 *  the start screen with the note (with RE4_MENU_SHOTS set, its pictures go there). */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class OfficialBuildTest {
  static final String OWNER = OfficialBuild.RELEASE_CERTS[0];
  static final String OTHER = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

  @After public void forget() {
    OfficialBuild.forget();
  }

  @Test public void theDecision() {
    assertFalse("a debug build never shows it", OfficialBuild.noticeFor(true, Collections.singletonList(OTHER)));
    assertFalse("a debug build never shows it", OfficialBuild.noticeFor(true, null));
    assertFalse("the owner's release", OfficialBuild.noticeFor(false, Collections.singletonList(OWNER)));
    assertFalse("upper case is the same key", OfficialBuild.noticeFor(false, Collections.singletonList(OWNER.toUpperCase())));
    assertTrue("another key", OfficialBuild.noticeFor(false, Collections.singletonList(OTHER)));
    assertFalse("several signers or a rotation: any match counts", OfficialBuild.noticeFor(false, Arrays.asList(OTHER, OWNER)));
    assertTrue("several unknown signers", OfficialBuild.noticeFor(false, Arrays.asList(OTHER, OTHER.replace('0', '1'))));
    assertFalse("no signature read: no false alarm", OfficialBuild.noticeFor(false, null));
    assertFalse("no signature read: no false alarm", OfficialBuild.noticeFor(false, Collections.emptyList()));
  }

  @Test public void theOwnersFingerprintIsWhole() {
    for (String cert : OfficialBuild.RELEASE_CERTS) assertTrue(cert, cert.matches("[0-9a-f]{64}"));
    assertTrue(OWNER.startsWith("a3810c7a"));
    assertTrue(OWNER.endsWith("f41a"));
  }

  private static void sign(Signature[] now, Signature[] past) {
    Application app = RuntimeEnvironment.getApplication();
    PackageInfo p = Shadows.shadowOf(app.getPackageManager()).getInternalMutablePackageInfo(app.getPackageName());
    SigningInfo info = Shadow.newInstanceOf(SigningInfo.class);
    ShadowSigningInfo shadow = Shadow.extract(info);
    shadow.setSignatures(now);
    if (past != null) shadow.setPastSigningCertificates(past);
    p.signingInfo = info;
    p.signatures = now;
  }

  private static void release() {
    RuntimeEnvironment.getApplication().getApplicationInfo().flags &= ~ApplicationInfo.FLAG_DEBUGGABLE;
  }

  @Test public void readsTheCertificatesSha256() throws Exception {
    byte[] cert = {1, 2, 3, 4, 5};
    sign(new Signature[] {new Signature(cert)}, null);
    List<String> signers = OfficialBuild.signers(RuntimeEnvironment.getApplication());
    assertEquals(Collections.singletonList(OfficialBuild.sha256(cert)), signers);
    // SHA-256 of 01 02 03 04 05
    assertEquals("74f81fe167d99b4cb41d6d0ccda82278caee9f3e2f25d5e5a3936ff3dcec60d0", OfficialBuild.sha256(cert));
  }

  @Test public void aRotatedKeyCountsWithItsPast() {
    byte[] old = {9, 9}, now = {7, 7};
    sign(new Signature[] {new Signature(now)}, new Signature[] {new Signature(old), new Signature(now)});
    List<String> signers = OfficialBuild.signers(RuntimeEnvironment.getApplication());
    assertNotNull(signers);
    assertEquals(2, signers.size());
  }

  @Config(sdk = 27)
  @Test public void beforeAndroid9TheSignatures() throws Exception {
    byte[] cert = {4, 2};
    Application app = RuntimeEnvironment.getApplication();
    PackageInfo p = Shadows.shadowOf(app.getPackageManager()).getInternalMutablePackageInfo(app.getPackageName());
    p.signatures = new Signature[] {new Signature(cert)};
    assertEquals(Collections.singletonList(OfficialBuild.sha256(cert)), OfficialBuild.signers(app));
  }

  @Test public void theAppsAnswer() {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    app.getApplicationInfo().flags |= ApplicationInfo.FLAG_DEBUGGABLE;
    assertFalse("debuggable: never", OfficialBuild.unofficial(app));
    OfficialBuild.forget();
    release();
    assertTrue("a release with another key", OfficialBuild.unofficial(app));
    OfficialBuild.forget();
    Shadows.shadowOf(app.getPackageManager()).getInternalMutablePackageInfo(app.getPackageName()).signingInfo = null;
    assertFalse("nothing to read: no note", OfficialBuild.unofficial(app));
  }

  /** The start screen needs Re4Application, whose core this sandbox cannot load. */
  @Implements(CoreBridge.class)
  public static class NoCore {
    @Implementation public static void load() {}
  }

  private static TextView withText(View v, String text) {
    if (v instanceof TextView && ((TextView) v).getText().toString().equals(text)) return (TextView) v;
    if (v instanceof ViewGroup) {
      for (int i = 0; i < ((ViewGroup) v).getChildCount(); i++) {
        TextView t = withText(((ViewGroup) v).getChildAt(i), text);
        if (t != null) return t;
      }
    }
    return null;
  }

  /** The start screen of a release build signed with another key. */
  private static View startScreen() {
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    release();
    try (var controller = Robolectric.buildActivity(MainActivity.class).create()) {
      return controller.get().findViewById(android.R.id.content);
    }
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class, qualifiers = "w960dp-h540dp-land-mdpi")
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theStartScreenShowsTheNote() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    String note = app.getString(R.string.core_unofficial);
    assertTrue(note.contains("github.com/Andiweli/CubeEvil4-Android"));
    View screen = startScreen();
    TextView shown = withText(screen, note);
    assertNotNull("the note under the name", shown);
    assertEquals(View.VISIBLE, shown.getVisibility());
    String report = CoreLog.deviceText(app, "");
    assertTrue("the same line in the error log", report.contains(note + "\n"));
    picture(screen, "start-unofficial");
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class, qualifiers = "w360dp-h740dp-port-mdpi")
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theNoteOnANarrowPhone() throws Exception {
    View screen = startScreen();
    assertNotNull(withText(screen, RuntimeEnvironment.getApplication().getString(R.string.core_unofficial)));
    picture(screen, "start-unofficial-phone");
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class)
  @Test public void aDebugBuildShowsNoNote() {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    app.getApplicationInfo().flags |= ApplicationInfo.FLAG_DEBUGGABLE;
    try (var controller = Robolectric.buildActivity(MainActivity.class).create()) {
      View screen = controller.get().findViewById(android.R.id.content);
      assertNull(withText(screen, app.getString(R.string.core_unofficial)));
      assertFalse(CoreLog.deviceText(app, "").contains(app.getString(R.string.core_unofficial)));
    }
  }

  private static void picture(View screen, String name) throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    if (folder == null || folder.isEmpty()) return;
    new File(folder).mkdirs();
    android.util.DisplayMetrics m = screen.getResources().getDisplayMetrics();
    View root = screen.getRootView();
    root.measure(View.MeasureSpec.makeMeasureSpec(m.widthPixels, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(m.heightPixels, View.MeasureSpec.EXACTLY));
    root.layout(0, 0, m.widthPixels, m.heightPixels);
    Bitmap b = Bitmap.createBitmap(m.widthPixels, m.heightPixels, Bitmap.Config.ARGB_8888);
    Canvas c = new Canvas(b);
    c.drawColor(0xff303030);
    root.draw(c);
    try (FileOutputStream out = new FileOutputStream(new File(folder, name + ".png"))) {
      b.compress(Bitmap.CompressFormat.PNG, 100, out);
    }
  }
}
