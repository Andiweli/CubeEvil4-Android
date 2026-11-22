package com.ast.re4decomp;

import static org.junit.Assert.*;
import static org.robolectric.Shadows.shadowOf;

import android.app.Application;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * 1.1.1 (the project owner: "Auf einem Chromebook mit Touchscreen wird die Touchsteuerung nicht mit 'Unterdrücken'
 * ausgeblendet"): a PC or a Chromebook is told by Android's features (TouchControls.pcDevice), a phone is not; with
 * "Suppress" it counts as a controller (TouchShowRule.counts, its own tests in tests/TouchShowRuleTests.java).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class TouchChromebookTest {
  @Test public void aChromebookIsToldByItsFeatures() {
    Application app = RuntimeEnvironment.getApplication();
    assertFalse("a phone", TouchControls.pcDevice(app));
    shadowOf(app.getPackageManager()).setSystemFeature("org.chromium.arc", true);
    assertTrue("ChromeOS (ARC)", TouchControls.pcDevice(app));
    shadowOf(app.getPackageManager()).setSystemFeature("org.chromium.arc", false);
    shadowOf(app.getPackageManager()).setSystemFeature("android.hardware.type.pc", true);
    assertTrue("a PC", TouchControls.pcDevice(app));
    assertTrue(TouchShowRule.counts(TouchShowRule.SUPPRESS, false, false, TouchControls.pcDevice(app)));
    assertFalse("Auto as before", TouchShowRule.counts(TouchShowRule.AUTO, false, false, TouchControls.pcDevice(app)));
  }
}
