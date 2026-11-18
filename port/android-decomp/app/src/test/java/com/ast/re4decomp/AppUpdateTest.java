package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import android.app.Application;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

/**
 * AppUpdate.compare: the installed version (the APK's versionName, "-core" of the build left out) against GitHub's
 * latest release tag. -1: an update. The releases carry a suffix until 1.0 ("0.99.1-rc"), from 1.0 on none (the
 * project owner, 07.11.2026).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)  // AppUpdate holds a Handler on the main looper
public class AppUpdateTest {
  private static void older(String installed, String release) {
    assertEquals(installed + " < " + release, -1, AppUpdate.compare(installed, release));
    assertEquals(release + " > " + installed, 1, AppUpdate.compare(release, installed));
  }

  private static void same(String installed, String release) {
    assertEquals(installed + " = " + release, 0, AppUpdate.compare(installed, release));
    assertEquals(release + " = " + installed, 0, AppUpdate.compare(release, installed));
  }

  @Test public void theSuffixWordDoesNotMakeAnUpdate() {
    // 0.99.0: the APK says "0.99.0-beta", its release "0.99.0-rc"; until then the update came after every install
    same("0.99.0-beta", "0.99.0-rc");
    same("0.99.0-rc", "0.99.0-rc");
    same("0.99.0-beta-core", "0.99.0-rc");
    // a branch's test APK (the Vulkan branch, 08.11.2026): the same version as its release, the next one an update
    same("0.99.2-vulkan0", "0.99.2-rc");
    same("0.99.2-vulkan1", "0.99.2-rc");
    same("0.99.2-vulkan1-core", "0.99.2-rc");
    older("0.99.2-vulkan3", "0.99.3-rc");
  }

  @Test public void theNumbersDecide() {
    older("0.99.0-rc", "0.99.1-rc");
    older("0.99.0-beta", "0.99.1-rc");  // what 0.99.0 sees when 0.99.1 comes
    older("0.98.7-beta", "0.99.0-rc");
    older("0.95.2-beta", "0.96.0-beta");
    older("0.95.2-beta", "v0.95.10-beta");
    older("0.99.0-rc", "1.0");
    older("0.99.9-rc", "1.0");
    older("0.99.9-rc", "0.99.10-rc");  // 0.99.10 (11.11.2026): ten after nine, not "1" after "9"
    older("0.99.10-rc", "0.99.11-rc");
    older("1.0", "1.0.1");
    older("1.0", "1.1");
    older("1.9", "1.10");
  }

  @Test public void aReleaseWithoutSuffixIsAheadOfOneWithIt() {
    older("1.0-rc", "1.0");
    older("1.0-beta", "1.0");
    older("0.99.0-beta", "0.99.0");
    older("1.0-rc", "1.0.0");
  }

  @Test public void missingPartsAreZero() {
    same("1.0", "1.0.0");
    same("1", "1.0");
  }

  @Test public void theSameWordCountsItsNumber() {
    older("1.0-rc2", "1.0-rc10");
    older("1.0-rc", "1.0-rc2");
    older("1.0-rc.2", "1.0-rc-3");
    same("1.0-rc2", "1.0-RC2");
    same("1.0-beta2", "1.0-rc1");  // different words: the same version
  }

  @Test public void theSideloadBuildIsTheSameVersion() {
    // the flavour "sideload" (1.0): "-sideload" as "-core", no update offered for its own release, the next one is
    same("0.99.19-rc-sideload", "0.99.19-rc");
    same("1.0-sideload", "1.0");
    same("1.0-sideload", "v1.0.0");
    same("1.0-SIDELOAD", "1.0");
    older("1.0-sideload", "1.0.1");
    older("0.99.19-rc-sideload", "1.0");
    assertEquals("1.0", AppUpdate.plain("1.0-sideload"));
    assertEquals("0.99.19-rc", AppUpdate.plain("0.99.19-rc-core"));
    assertEquals("0.99.19-rc", AppUpdate.plain("0.99.19-rc"));
    assertEquals("1.0-sideloader", AppUpdate.plain("1.0-sideloader"));
    assertNull(AppUpdate.plain(null));
  }

  /** 1.0.0 (16.11.2026, the release that was to be 0.99.19-rc): its APKs ("1.0.0-core", "1.0.0-sideload") are the
   *  tag 1.0.0, newer than 0.99.18-rc (and than 0.99.19-rc, never released), older than 1.0.1. */
  @Test public void theFirstReleaseWithoutSuffix() {
    for (String apk : new String[] {"1.0.0-core", "1.0.0-sideload", "1.0.0"}) {
      same(apk, "1.0.0");
      same(apk, "v1.0.0");
      same(apk, "1.0");
      older("0.99.18-rc", apk);
      older("0.99.18-rc-core", apk);
      older("0.99.19-rc", apk);
      older(apk, "1.0.1");
      older(apk, "1.1.0");
    }
    assertEquals("1.0.0", AppUpdate.plain("1.0.0-core"));
    assertEquals("1.0.0", AppUpdate.plain("1.0.0-sideload"));
  }

  @Test public void prefixCaseAndBlanksDoNotCount() {
    same("v0.99.1-rc", "0.99.1-rc");
    same("V0.99.1-RC", " 0.99.1-rc ");
    same("0.99.1-rc-core", "v0.99.1-rc");
  }

  @Test public void noVersionIsNeverOlder() {
    same(null, "0.99.1-rc");
    same("", "0.99.1-rc");
    same("0.99.1-rc", "latest");
  }
}
