package com.ast.re4decomp;

import java.util.Locale;

/**
 * Which of a controller's axes are the right stick (the C stick) and which the triggers (L, R), decided once per
 * device from the axes it reports (a user's report for 0.99.13, 13.11.2026: a wired Xbox One controller on a
 * Motorola Edge 70 Fusion, "pressing LT or RT moves the camera to one side by itself, and it stays stuck there").
 *
 * <p>Android maps the controllers it knows (its key layout files, Vendor_xxxx_Product_yyyy.kl) to one order: the
 * right stick on AXIS_Z / AXIS_RZ and the triggers on AXIS_LTRIGGER / AXIS_RTRIGGER, often AXIS_BRAKE / AXIS_GAS as
 * well (all controllers in the reports so far: the Retroid, Xbox and PlayStation pads over Bluetooth, GamePad Plus).
 * A controller it does not know keeps the Linux driver's order (Generic.kl): the xpad driver of wired Xbox pads (and
 * of most wired pads in X-input mode) puts the right stick on ABS_RX / ABS_RY and the triggers on ABS_Z / ABS_RZ,
 * which Android hands on as AXIS_RX / AXIS_RY and AXIS_Z / AXIS_RZ, with no trigger axis at all. Z and RZ are
 * centred axes to Android (-1..1), so a released trigger reads -1: taken for the stick, it pushes the camera fully to
 * one side for as long as it rests (before 0.99.13 the app took Z / RZ for the stick whenever both were there).
 *
 * <p>The rule: a device with a trigger axis keeps Android's order (stick Z / RZ when it has both, else RX / RY;
 * triggers LTRIGGER or BRAKE, RTRIGGER or GAS, as before). A device without one that has Z, RZ, RX and RY has the
 * driver's order: stick RX / RY, triggers Z (left) and RZ (right) from their minimum (released) to their maximum,
 * unless the kernel's flat values say the opposite (Z / RZ with a flat, RX / RY without one; xpad gives its sticks a
 * flat and its triggers none). A trigger read from a centred axis counts only once it was seen near its minimum
 * (released): before its first event Android reports it at 0, the middle, which would be a half pressed trigger, and
 * a pad that has a stick there after all can never press L or R by resting.
 *
 * <p>No Android types (the axis numbers are MotionEvent's): the host tests run it (ControllerAxesTest).
 */
final class ControllerAxes {
  // MotionEvent.AXIS_*
  static final int X = 0, Y = 1, Z = 11, RX = 12, RY = 13, RZ = 14, HAT_X = 15, HAT_Y = 16, LTRIGGER = 17,
      RTRIGGER = 18, GAS = 22, BRAKE = 23;
  /** A trigger on a centred axis counts once it was seen within this part of its way from the released end. */
  static final float REST = 0.1f;

  /** A device's axes (InputDevice.getMotionRange) and, for read, their values in one event (MotionEvent). */
  interface Axes {
    /** Whether the device has `axis` (a range with max above min). */
    boolean has(int axis);
    float min(int axis);
    float max(int axis);
    float flat(int axis);
    float value(int axis);
  }

  /** The right stick's axes. */
  final int rightX, rightY;
  /** The triggers' axes, the larger value counts (-1: none). */
  final int left1, left2, right1, right2;
  /** The triggers are on centred axes (Z, RZ: the driver's order). */
  final boolean centredTriggers;
  private boolean leftLive, rightLive;

  private ControllerAxes(int rightX, int rightY, int left1, int left2, int right1, int right2, boolean centred) {
    this.rightX = rightX;
    this.rightY = rightY;
    this.left1 = left1;
    this.left2 = left2;
    this.right1 = right1;
    this.right2 = right2;
    this.centredTriggers = centred;
  }

  /** The layout of a device by its axes (`a` may be null: a device Android does not name). */
  static ControllerAxes of(Axes a) {
    boolean triggerAxes = a != null && (a.has(LTRIGGER) || a.has(RTRIGGER) || a.has(BRAKE) || a.has(GAS));
    boolean zr = a != null && a.has(Z) && a.has(RZ), rxy = a != null && a.has(RX) && a.has(RY);
    if (!triggerAxes && zr && rxy) {
      // the kernel's flats the other way round: the stick on Z / RZ (with a flat), the triggers on RX / RY (none)
      boolean stickOnZ = a.flat(Z) > 0 && a.flat(RZ) > 0 && a.flat(RX) == 0 && a.flat(RY) == 0;
      if (!stickOnZ) return new ControllerAxes(RX, RY, Z, -1, RZ, -1, true);
    }
    return new ControllerAxes(zr ? Z : RX, zr ? RZ : RY, LTRIGGER, BRAKE, RTRIGGER, GAS, false);
  }

  /** Whether `axis` is one of this layout's triggers (it never goes to the stick). */
  boolean isTrigger(int axis) {
    return axis >= 0 && (axis == left1 || axis == left2 || axis == right1 || axis == right2);
  }

  /**
   * `value` of an axis from `min` to `max` as a stick (-1..1) or a trigger (0..1); 0 for a missing axis (max not
   * above min) or a value that is no number.
   */
  static float normalise(float value, float min, float max, boolean trigger) {
    if (!(max > min) || Float.isNaN(value) || Float.isInfinite(value)) return 0f;
    return trigger ? (value - min) / (max - min) : 2 * (value - min) / (max - min) - 1;
  }

  private static float axis(Axes a, int axis, boolean trigger) {
    if (axis < 0 || !a.has(axis)) return 0f;
    return normalise(a.value(axis), a.min(axis), a.max(axis), trigger);
  }

  /**
   * One event: out = {right stick x, right stick y (-1..1, y down as Android gives it), left trigger, right
   * trigger (0..1)}. Remembers for centred triggers whether they were seen released, so call it once per event.
   */
  void read(Axes a, float[] out) {
    out[0] = axis(a, rightX, false);
    out[1] = axis(a, rightY, false);
    out[2] = trigger(false, axis(a, left1, true), axis(a, left2, true));
    out[3] = trigger(true, axis(a, right1, true), axis(a, right2, true));
  }

  /** A trigger as the event has it, not gated (the button mapping's capture, which asks for more than half). */
  float triggerNow(Axes a, boolean right) {
    return right ? Math.max(clamp(axis(a, right1, true)), clamp(axis(a, right2, true)))
        : Math.max(clamp(axis(a, left1, true)), clamp(axis(a, left2, true)));
  }

  /** A trigger from its axes' values (0..1 each): the larger, gated for a centred axis (see the class). */
  float trigger(boolean right, float a, float b) {
    float v = Math.max(clamp(a), clamp(b));
    if (!centredTriggers) return v;
    if (v <= REST) {
      if (right) rightLive = true;
      else leftLive = true;
    }
    return (right ? rightLive : leftLive) ? v : 0f;
  }

  private static float clamp(float v) {
    return Float.isNaN(v) ? 0f : Math.max(0f, Math.min(1f, v));
  }

  /** For the log and controllers.txt. */
  String describe() {
    return String.format(Locale.ROOT, "right stick %s/%s, triggers %s", name(rightX), name(rightY),
        centredTriggers ? name(left1) + "/" + name(right1) + " (the driver's order: released at their minimum)"
            : name(left1) + " or " + name(left2) + ", " + name(right1) + " or " + name(right2));
  }

  static String name(int axis) {
    switch (axis) {
      case X: return "X";
      case Y: return "Y";
      case Z: return "Z";
      case RX: return "RX";
      case RY: return "RY";
      case RZ: return "RZ";
      case LTRIGGER: return "LTRIGGER";
      case RTRIGGER: return "RTRIGGER";
      case GAS: return "GAS";
      case BRAKE: return "BRAKE";
      default: return Integer.toString(axis);
    }
  }
}
