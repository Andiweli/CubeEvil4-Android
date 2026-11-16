package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.Signature;
import android.content.pm.SigningInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
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

/** The warning of unofficial builds (OfficialBuild, UnofficialWarning, 1.0.0): the decision, the reading of the APK's
 *  certificates, once per process, and the popup before the game starts (with RE4_MENU_SHOTS set, its pictures go
 *  there). */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class OfficialBuildTest {
  static final String OWNER = OfficialBuild.RELEASE_CERTS[0];
  static final String OTHER = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

  @After public void forget() {
    OfficialBuild.forget();
    AppStorage.forget();
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
    assertFalse("nothing to read: no warning", OfficialBuild.unofficial(app));
  }

  /** The popup at the first start only (the owner, 17.11.2026): once confirmed, not again for that signature, also
   *  after a cold start; another foreign signature warns once more, and so does the app after its data were cleared. */
  @Test public void onceForEachSignature() {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    release();
    assertTrue("due at the first start", OfficialBuild.mustWarn(app));
    assertTrue("still due until confirmed", OfficialBuild.mustWarn(app));
    OfficialBuild.confirm(app);
    assertFalse("confirmed: not again in this process", OfficialBuild.mustWarn(app));
    assertTrue("the error log still names it", OfficialBuild.unofficial(app));
    OfficialBuild.forget();  // as a new process
    assertFalse("a cold start: not again for this signature", OfficialBuild.mustWarn(app));
    assertTrue("the error log still names it", OfficialBuild.unofficial(app));
    // an update signed with yet another foreign key: once more
    OfficialBuild.forget();
    sign(new Signature[] {new Signature(new byte[] {2})}, null);
    assertTrue("another foreign signature warns once more", OfficialBuild.mustWarn(app));
    OfficialBuild.confirm(app);
    OfficialBuild.forget();
    assertFalse(OfficialBuild.mustWarn(app));
    // the app's data cleared: the first start again
    app.getSharedPreferences(OfficialBuild.PREFS, 0).edit().clear().commit();
    OfficialBuild.forget();
    assertTrue("cleared data: warns again", OfficialBuild.mustWarn(app));
    // the official key: never; a debug build: never
    OfficialBuild.forget();
    app.getApplicationInfo().flags |= ApplicationInfo.FLAG_DEBUGGABLE;
    assertFalse("a debug build: never", OfficialBuild.mustWarn(app));
    // the decision alone
    assertTrue(OfficialBuild.warnFor(true, "aa", ""));
    assertFalse(OfficialBuild.warnFor(true, "aa", "aa"));
    assertTrue(OfficialBuild.warnFor(true, "bb", "aa"));
    assertFalse(OfficialBuild.warnFor(false, "aa", ""));
    assertFalse("no signature read: no warning", OfficialBuild.warnFor(true, "", ""));
    assertEquals("a,b", OfficialBuild.signerKey(java.util.Arrays.asList("B", "a")));
    assertEquals("", OfficialBuild.signerKey(null));
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

  /** Imported discs of the debug build as the start screen checks them (DiscInstaller.coreReady): the game starts by
   *  itself from the start screen then. */
  private static void discsReady() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    File root = ((Re4Application) app).imports.root;
    for (int n = 1; n <= 2; n++) {
      File disc = new File(root, "disc" + n);
      new File(disc, "sys").mkdirs();
      try (FileOutputStream out = new FileOutputStream(new File(disc, "ready.properties"))) {
        out.write(("format=1\nid=G4BE08\ndisc=" + n + "\nreferences=" + (n == 1 ? 111 : 5) + "\n").getBytes("UTF-8"));
      }
      try (java.io.RandomAccessFile dol = new java.io.RandomAccessFile(new File(disc, "sys/main.dol"), "rw")) {
        dol.setLength(com.ast.re4decomp.disc.GameCubeDisc.dolSize("debug"));
      }
      try (java.io.RandomAccessFile boot = new java.io.RandomAccessFile(new File(disc, "sys/boot.bin"), "rw")) {
        boot.setLength(0x440);
      }
      new File(disc, "sys/fst.bin").createNewFile();
      new File(disc, "manifest.tsv").createNewFile();
    }
  }

  /** What the start screen starts when it starts the game: CoreActivity (the classic variant: StartupActivity). */
  private static boolean startedGame(android.app.Activity screen) {
    android.content.Intent next = Shadows.shadowOf(screen).getNextStartedActivity();
    if (next == null || next.getComponent() == null) return false;
    String name = next.getComponent().getClassName();
    return name.equals(CoreActivity.class.getName()) || name.equals(StartupActivity.class.getName());
  }

  private static android.app.AlertDialog shownDialog() {
    android.app.Dialog d = org.robolectric.shadows.ShadowDialog.getLatestDialog();
    return d instanceof android.app.AlertDialog && d.isShowing() ? (android.app.AlertDialog) d : null;
  }

  private static Button okButton(android.app.AlertDialog dialog, Application app) {
    TextView t = withText(dialog.getWindow().getDecorView(), app.getString(R.string.ingame_unofficial_ok));
    return t instanceof Button ? (Button) t : null;
  }

  private static void key(android.app.AlertDialog dialog, int code) {
    for (int action : new int[] {KeyEvent.ACTION_DOWN, KeyEvent.ACTION_UP}) {
      KeyEvent e = new KeyEvent(0, 0, action, code, 0, 0, 0, 0, 0, android.view.InputDevice.SOURCE_GAMEPAD);
      dialog.getWindow().getDecorView().dispatchKeyEvent(e);
    }
    TestLooper.idle();
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class, qualifiers = "w960dp-h540dp-land-mdpi")
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theGameWaitsForTheWarning() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    release();
    discsReady();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity screen = controller.get();
      TestLooper.idle();
      android.app.AlertDialog dialog = shownDialog();
      assertNotNull("the warning at the start", dialog);
      assertFalse("the game does not start behind it", startedGame(screen));
      View box = dialog.getWindow().getDecorView();
      assertNotNull(withText(box, app.getString(R.string.ingame_unofficial_title)));
      assertNotNull(withText(box, app.getString(R.string.ingame_unofficial_text)));
      Button ok = okButton(dialog, app);
      assertNotNull("one button", ok);
      assertTrue("the focus on it (a touch screen gives it with the first key)", ok.isFocused() || ok.isInTouchMode());
      picture(screen, dialog, "warning-game-start-landscape");
      // Back, B, Select and a tap beside it: it stays
      dialog.onBackPressed();
      key(dialog, KeyEvent.KEYCODE_BACK);
      key(dialog, KeyEvent.KEYCODE_BUTTON_B);
      key(dialog, KeyEvent.KEYCODE_BUTTON_SELECT);
      assertTrue("Back and B do not close it", dialog.isShowing());
      assertFalse(startedGame(screen));
      // the start screen's own button waits for it as well
      Button start = (Button) withText(screen.findViewById(android.R.id.content), app.getString(R.string.core_start));
      if (start != null) start.performClick();
      assertFalse("Start game waits too", startedGame(screen));
      // A on the focused button confirms it, and the game starts
      key(dialog, KeyEvent.KEYCODE_BUTTON_A);
      assertFalse("confirmed", dialog.isShowing());
      assertTrue("then the game starts", startedGame(screen));
      assertFalse(OfficialBuild.mustWarn(app));
    }
    // a new start screen in the same process (back from the game): no warning again
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      TestLooper.idle();
      android.app.AlertDialog again = shownDialog();
      assertTrue("once per process", again == null
          || withText(again.getWindow().getDecorView(), app.getString(R.string.ingame_unofficial_ok)) == null);
    }
    // a cold start (a new process) after it was confirmed: no warning any more (the owner, 17.11.2026: only at the
    // app's first start), the game starts at once
    OfficialBuild.forget();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      TestLooper.idle();
      android.app.AlertDialog again = shownDialog();
      assertTrue("not at the next start", again == null || !again.isShowing()
          || withText(again.getWindow().getDecorView(), app.getString(R.string.ingame_unofficial_ok)) == null);
      assertFalse(OfficialBuild.mustWarn(app));
    }
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class, qualifiers = "w360dp-h740dp-port-mdpi")
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theWarningOnTheFirstStartScreen() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    release();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity screen = controller.get();
      TestLooper.idle();
      android.app.AlertDialog dialog = shownDialog();
      assertNotNull("the warning on the first start screen too", dialog);
      assertNull("no line under the name any more",
          withText(screen.findViewById(android.R.id.content), app.getString(R.string.core_unofficial)));
      assertTrue("the error log names it", CoreLog.deviceText(app, "").contains(app.getString(R.string.core_unofficial) + "\n"));
      picture(screen, dialog, "warning-first-start-phone");
      // Start on a controller confirms it
      key(dialog, KeyEvent.KEYCODE_BUTTON_START);
      assertFalse(dialog.isShowing());
    }
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class, qualifiers = "w360dp-h740dp-port-mdpi")
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theGameStartOnAPhone() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    release();
    discsReady();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      MainActivity screen = controller.get();
      TestLooper.idle();
      android.app.AlertDialog dialog = shownDialog();
      assertNotNull(dialog);
      picture(screen, dialog, "warning-game-start-phone");
      okButton(dialog, app).performClick();  // touch
      TestLooper.idle();
      assertTrue(startedGame(screen));
    }
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class, qualifiers = "w960dp-h540dp-land-mdpi")
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theFirstStartScreenLandscape() throws Exception {
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    release();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      TestLooper.idle();
      android.app.AlertDialog dialog = shownDialog();
      assertNotNull(dialog);
      picture(controller.get(), dialog, "warning-first-start-landscape");
    }
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class, qualifiers = "de-w960dp-h540dp-land-mdpi")
  @GraphicsMode(GraphicsMode.Mode.NATIVE)
  @Test public void theWarningInGerman() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    release();
    discsReady();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      TestLooper.idle();
      android.app.AlertDialog dialog = shownDialog();
      assertNotNull(dialog);
      assertNotNull(withText(dialog.getWindow().getDecorView(), "Inoffizielle Version"));
      assertNotNull(okButton(dialog, app));
      assertEquals("Verstanden", okButton(dialog, app).getText().toString());
      picture(controller.get(), dialog, "warning-game-start-landscape-de");
    }
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class)
  @Test public void aDebugBuildStartsWithoutIt() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    app.getApplicationInfo().flags |= ApplicationInfo.FLAG_DEBUGGABLE;
    discsReady();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      TestLooper.idle();
      assertNull("no warning", shownDialog());
      assertTrue("the game starts at once", startedGame(controller.get()));
      assertFalse(CoreLog.deviceText(app, "").contains(app.getString(R.string.core_unofficial)));
    }
  }

  @Config(sdk = 33, application = Re4Application.class, shadows = NoCore.class)
  @Test public void anUnreadableSignatureStartsWithoutIt() throws Exception {
    Application app = RuntimeEnvironment.getApplication();
    sign(new Signature[] {new Signature(new byte[] {1})}, null);
    Shadows.shadowOf(app.getPackageManager()).getInternalMutablePackageInfo(app.getPackageName()).signingInfo = null;
    release();
    discsReady();
    try (var controller = Robolectric.buildActivity(MainActivity.class).setup()) {
      TestLooper.idle();
      assertNull("no false alarm", shownDialog());
      assertTrue(startedGame(controller.get()));
    }
  }

  /** The start screen with the warning over it, dimmed behind as Android does (RE4_MENU_SHOTS: the folder). */
  private static void picture(android.app.Activity screen, android.app.AlertDialog dialog, String name) throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    if (folder == null || folder.isEmpty()) return;
    new File(folder).mkdirs();
    android.util.DisplayMetrics m = screen.getResources().getDisplayMetrics();
    View root = screen.getWindow().getDecorView();
    root.measure(View.MeasureSpec.makeMeasureSpec(m.widthPixels, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(m.heightPixels, View.MeasureSpec.EXACTLY));
    root.layout(0, 0, m.widthPixels, m.heightPixels);
    Bitmap b = Bitmap.createBitmap(m.widthPixels, m.heightPixels, Bitmap.Config.ARGB_8888);
    Canvas c = new Canvas(b);
    c.drawColor(0xff303030);
    root.draw(c);
    c.drawColor(0x99000000);  // Android's dim behind a dialog
    View box = dialog.getWindow().getDecorView();
    int width = Math.min(m.widthPixels - Math.round(32 * m.density), Math.round(520 * m.density));
    box.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(m.heightPixels - Math.round(48 * m.density), View.MeasureSpec.AT_MOST));
    box.layout(0, 0, box.getMeasuredWidth(), box.getMeasuredHeight());
    c.save();
    c.translate((m.widthPixels - box.getMeasuredWidth()) / 2f, (m.heightPixels - box.getMeasuredHeight()) / 2f);
    box.draw(c);
    c.restore();
    try (FileOutputStream out = new FileOutputStream(new File(folder, name + ".png"))) {
      b.compress(Bitmap.CompressFormat.PNG, 100, out);
    }
  }
}
