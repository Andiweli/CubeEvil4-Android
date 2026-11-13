package com.ast.re4decomp;

import static org.junit.Assert.*;

import java.util.HashMap;
import java.util.Map;
import org.junit.Test;

/**
 * ControllerAxes: which axes are the right stick and which the triggers, with motion events modelled on the devices
 * (a user's report for 0.99.13: a wired Xbox One controller, "pressing LT or RT moves the camera to one side by
 * itself, and it stays stuck there"; the other devices as their lines in the reports' controllers.txt name them).
 */
public class ControllerAxesTest {
  private static final float EPS = 1e-4f;
  // xpad (Linux): sticks -32768..32767 with flat 128, triggers 0..1023 without; Android: centred axes -1..1
  private static final float STICK_FLAT = 128f * 2f / 65535f;

  /** A device as Android shows it: its axes' ranges and the values of the last event (0 before an axis reported). */
  private static final class Pad implements ControllerAxes.Axes {
    final Map<Integer, float[]> ranges = new HashMap<>();
    final Map<Integer, Float> values = new HashMap<>();

    Pad axis(int axis, float min, float max, float flat) {
      ranges.put(axis, new float[] {min, max, flat});
      return this;
    }

    Pad set(int axis, float v) {
      values.put(axis, v);
      return this;
    }

    @Override public boolean has(int a) { float[] r = ranges.get(a); return r != null && r[1] > r[0]; }
    @Override public float min(int a) { return ranges.get(a)[0]; }
    @Override public float max(int a) { return ranges.get(a)[1]; }
    @Override public float flat(int a) { float[] r = ranges.get(a); return r == null ? 0 : r[2]; }
    @Override public float value(int a) { Float v = values.get(a); return v == null ? 0f : v; }
  }

  /** The wired Xbox One controller without its key layout file (Generic.kl): the xpad driver's order. */
  private static Pad wiredXbox() {
    return new Pad()
        .axis(ControllerAxes.X, -1, 1, STICK_FLAT).axis(ControllerAxes.Y, -1, 1, STICK_FLAT)
        .axis(ControllerAxes.Z, -1, 1, 0).axis(ControllerAxes.RX, -1, 1, STICK_FLAT)
        .axis(ControllerAxes.RY, -1, 1, STICK_FLAT).axis(ControllerAxes.RZ, -1, 1, 0)
        .axis(ControllerAxes.HAT_X, -1, 1, 0).axis(ControllerAxes.HAT_Y, -1, 1, 0);
  }

  private static float[] read(ControllerAxes layout, Pad pad) {
    float[] out = new float[4];
    layout.read(pad, out);
    return out;
  }

  /** What the app read as the right stick before 0.99.13: Z/RZ whenever both were there. */
  private static float oldRightX(Pad pad) {
    return ControllerAxes.normalise(pad.value(ControllerAxes.Z), -1, 1, false);
  }

  @Test public void wiredXboxTriggersAreLAndRAndNeverTheCamera() {
    Pad pad = wiredXbox();
    ControllerAxes layout = ControllerAxes.of(pad);
    assertEquals(ControllerAxes.RX, layout.rightX);
    assertEquals(ControllerAxes.RY, layout.rightY);
    assertTrue(layout.centredTriggers);
    assertTrue(layout.isTrigger(ControllerAxes.Z));
    assertTrue(layout.isTrigger(ControllerAxes.RZ));
    assertFalse(layout.isTrigger(layout.rightX));
    assertFalse(layout.isTrigger(layout.rightY));

    // walking before the triggers were touched: Android still has Z and RZ at 0 (the middle): no half pressed L, R
    float[] v = read(layout, pad.set(ControllerAxes.X, 0.6f));
    assertArrayEquals(new float[] {0, 0, 0, 0}, v, EPS);

    // LT pressed: it starts at its minimum (-1) and goes up; L follows, the camera stays
    v = read(layout, pad.set(ControllerAxes.Z, -0.96f));
    assertEquals(0.02f, v[2], EPS);
    v = read(layout, pad.set(ControllerAxes.Z, 0f));
    assertEquals(0.5f, v[2], EPS);
    v = read(layout, pad.set(ControllerAxes.Z, 1f));
    assertEquals(1f, v[2], EPS);
    assertEquals(0f, v[0], EPS);
    assertEquals(0f, v[1], EPS);

    // LT released: Z rests at -1. The old rule took that for the C stick pushed fully to one side, for good
    pad.set(ControllerAxes.Z, -1f);
    assertEquals(-1f, oldRightX(pad), EPS);
    v = read(layout, pad);
    assertArrayEquals(new float[] {0, 0, 0, 0}, v, EPS);

    // another button or the left stick afterwards: the camera still does not move
    v = read(layout, pad.set(ControllerAxes.X, -0.3f).set(ControllerAxes.HAT_X, 1f));
    assertEquals(0f, v[0], EPS);
    assertEquals(0f, v[1], EPS);

    // the right stick moves the camera
    v = read(layout, pad.set(ControllerAxes.RX, 0.8f).set(ControllerAxes.RY, -0.6f));
    assertEquals(0.8f, v[0], EPS);
    assertEquals(-0.6f, v[1], EPS);
    assertEquals(0f, v[2], EPS);

    // RT: from its minimum to full and back; L stays released
    v = read(layout, pad.set(ControllerAxes.RZ, -0.9f));
    assertEquals(0.05f, v[3], EPS);
    v = read(layout, pad.set(ControllerAxes.RZ, 1f));
    assertEquals(1f, v[3], EPS);
    assertEquals(0f, v[2], EPS);
    v = read(layout, pad.set(ControllerAxes.RZ, -1f));
    assertEquals(0f, v[3], EPS);
    assertEquals(0.8f, v[0], EPS);  // the right stick is still held
  }

  @Test public void aTriggerPressedHardAtOnceCountsFromItsRelease() {
    // a trigger whose first event is already deep (a fast press): it counts once it was seen released
    Pad pad = wiredXbox();
    ControllerAxes layout = ControllerAxes.of(pad);
    assertEquals(0f, read(layout, pad.set(ControllerAxes.Z, 0.8f))[2], EPS);
    assertEquals(0f, read(layout, pad.set(ControllerAxes.Z, -1f))[2], EPS);
    assertEquals(0.9f, read(layout, pad.set(ControllerAxes.Z, 0.8f))[2], EPS);
  }

  @Test public void theButtonMappingSeesTheWiredXboxTriggers() {
    Pad pad = wiredXbox();
    ControllerAxes layout = ControllerAxes.of(pad);
    // not reported yet (0, the middle): not more than half
    assertFalse(layout.triggerNow(pad, false) > 0.6f);
    assertTrue(layout.triggerNow(pad.set(ControllerAxes.Z, 0.5f), false) > 0.6f);
    assertFalse(layout.triggerNow(pad, true) > 0.6f);
    assertTrue(layout.triggerNow(pad.set(ControllerAxes.RZ, 1f), true) > 0.6f);
  }

  @Test public void theControllersOfTheReportsKeepAndroidsOrder() {
    // the Retroid Pocket's controller (vendor 2022 product 3001): no RX/RY, BRAKE/GAS, all flats 0
    Pad retroid = new Pad().axis(ControllerAxes.HAT_Y, -1, 1, 0).axis(ControllerAxes.BRAKE, 0, 1, 0)
        .axis(ControllerAxes.GAS, 0, 1, 0).axis(ControllerAxes.Z, -1, 1, 0).axis(ControllerAxes.Y, -1, 1, 0)
        .axis(ControllerAxes.HAT_X, -1, 1, 0).axis(ControllerAxes.RZ, -1, 1, 0).axis(ControllerAxes.X, -1, 1, 0);
    ControllerAxes layout = ControllerAxes.of(retroid);
    assertEquals(ControllerAxes.Z, layout.rightX);
    assertEquals(ControllerAxes.RZ, layout.rightY);
    assertFalse(layout.centredTriggers);
    float[] v = read(layout, retroid);
    assertArrayEquals(new float[] {0, 0, 0, 0}, v, EPS);  // at rest
    v = read(layout, retroid.set(ControllerAxes.Z, 0.5f).set(ControllerAxes.RZ, -1f).set(ControllerAxes.GAS, 1f));
    assertArrayEquals(new float[] {0.5f, -1f, 0f, 1f}, v, EPS);
    v = read(layout, retroid.set(ControllerAxes.BRAKE, 0.7f));
    assertEquals(0.7f, v[2], EPS);

    // Xbox Wireless Controller over Bluetooth (045e:02e0), DualShock 4 (054c:05c4), GamePad Plus (1949:0402):
    // Z/RZ, LTRIGGER/RTRIGGER and BRAKE/GAS
    for (float stickFlat : new float[] {0.125f, 0.118f}) {
      Pad pad = new Pad().axis(ControllerAxes.X, -1, 1, stickFlat).axis(ControllerAxes.Y, -1, 1, stickFlat)
          .axis(ControllerAxes.Z, -1, 1, stickFlat).axis(ControllerAxes.RZ, -1, 1, stickFlat)
          .axis(ControllerAxes.LTRIGGER, 0, 1, 0.06f).axis(ControllerAxes.BRAKE, 0, 1, 0.06f)
          .axis(ControllerAxes.RTRIGGER, 0, 1, 0.06f).axis(ControllerAxes.GAS, 0, 1, 0.06f)
          .axis(ControllerAxes.HAT_X, -1, 1, 0).axis(ControllerAxes.HAT_Y, -1, 1, 0);
      layout = ControllerAxes.of(pad);
      assertEquals(ControllerAxes.Z, layout.rightX);
      assertEquals(ControllerAxes.RZ, layout.rightY);
      assertFalse(layout.centredTriggers);
      v = read(layout, pad.set(ControllerAxes.Z, -0.4f).set(ControllerAxes.LTRIGGER, 0.3f).set(ControllerAxes.GAS, 1f));
      assertArrayEquals(new float[] {-0.4f, 0f, 0.3f, 1f}, v, EPS);
    }
  }

  @Test public void otherLayoutsStayAsBefore() {
    // a stick on RX/RY only (no Z/RZ), no analog triggers
    Pad pro = new Pad().axis(ControllerAxes.X, -1, 1, 0.01f).axis(ControllerAxes.Y, -1, 1, 0.01f)
        .axis(ControllerAxes.RX, -1, 1, 0.01f).axis(ControllerAxes.RY, -1, 1, 0.01f);
    ControllerAxes layout = ControllerAxes.of(pro);
    assertEquals(ControllerAxes.RX, layout.rightX);
    assertEquals(ControllerAxes.RY, layout.rightY);
    assertFalse(layout.centredTriggers);
    assertArrayEquals(new float[] {0.5f, 0f, 0f, 0f}, read(layout, pro.set(ControllerAxes.RX, 0.5f)), EPS);

    // a stick on Z/RZ without RX/RY and without trigger axes (a simple USB pad)
    Pad simple = new Pad().axis(ControllerAxes.X, -1, 1, 0.06f).axis(ControllerAxes.Y, -1, 1, 0.06f)
        .axis(ControllerAxes.Z, -1, 1, 0.06f).axis(ControllerAxes.RZ, -1, 1, 0.06f);
    layout = ControllerAxes.of(simple);
    assertEquals(ControllerAxes.Z, layout.rightX);
    assertEquals(ControllerAxes.RZ, layout.rightY);

    // all four, the flats saying the stick is on Z/RZ and the triggers on RX/RY: the stick stays on Z/RZ
    Pad hid = new Pad().axis(ControllerAxes.X, -1, 1, 0.06f).axis(ControllerAxes.Y, -1, 1, 0.06f)
        .axis(ControllerAxes.Z, -1, 1, 0.06f).axis(ControllerAxes.RZ, -1, 1, 0.06f)
        .axis(ControllerAxes.RX, -1, 1, 0).axis(ControllerAxes.RY, -1, 1, 0);
    layout = ControllerAxes.of(hid);
    assertEquals(ControllerAxes.Z, layout.rightX);
    assertEquals(ControllerAxes.RZ, layout.rightY);
    assertFalse(layout.centredTriggers);

    // a device Android does not name: nothing is read
    layout = ControllerAxes.of(null);
    assertArrayEquals(new float[] {0, 0, 0, 0}, read(layout, new Pad()), EPS);
  }

  @Test public void aStickTakenForATriggerNeverPressesLOrRByResting() {
    // all four axes with the same flats (no way to tell): the driver's order; should Z/RZ be a stick after all,
    // resting at the middle it never presses L or R
    Pad pad = new Pad().axis(ControllerAxes.X, -1, 1, 0.06f).axis(ControllerAxes.Y, -1, 1, 0.06f)
        .axis(ControllerAxes.Z, -1, 1, 0.06f).axis(ControllerAxes.RZ, -1, 1, 0.06f)
        .axis(ControllerAxes.RX, -1, 1, 0.06f).axis(ControllerAxes.RY, -1, 1, 0.06f);
    ControllerAxes layout = ControllerAxes.of(pad);
    assertTrue(layout.centredTriggers);
    for (float rest : new float[] {0f, 0.02f, -0.03f, 0.01f}) {
      float[] v = read(layout, pad.set(ControllerAxes.Z, rest).set(ControllerAxes.RZ, -rest));
      assertEquals(0f, v[2], EPS);
      assertEquals(0f, v[3], EPS);
    }
  }

  @Test public void normaliseAndTheLogLine() {
    assertEquals(-1f, ControllerAxes.normalise(-1f, -1f, 1f, false), EPS);
    assertEquals(0f, ControllerAxes.normalise(-1f, -1f, 1f, true), EPS);
    assertEquals(0.75f, ControllerAxes.normalise(0.5f, -1f, 1f, true), EPS);
    assertEquals(0f, ControllerAxes.normalise(Float.NaN, -1f, 1f, false), EPS);
    assertEquals(0f, ControllerAxes.normalise(0.5f, 0f, 0f, false), EPS);
    assertEquals("right stick RX/RY, triggers Z/RZ (the driver's order: released at their minimum)",
        ControllerAxes.of(wiredXbox()).describe());
    assertEquals("right stick Z/RZ, triggers LTRIGGER or BRAKE, RTRIGGER or GAS",
        ControllerAxes.of(new Pad().axis(ControllerAxes.Z, -1, 1, 0).axis(ControllerAxes.RZ, -1, 1, 0)
            .axis(ControllerAxes.GAS, 0, 1, 0)).describe());
  }
}
