package com.ast.re4decomp;

/**
 * The two turn rates of the gyro aiming (GyroAim) from a gyroscope reading. The gyroscope's
 * axes are the device's natural orientation's (x right, y up, z out of the screen; rad/s,
 * counter-clockwise positive); the aim's are the screen's as it is shown (Display.getRotation,
 * Surface.ROTATION_0..270 = 0..3; 1: the device turned a quarter counter-clockwise, its screen's
 * right is the device's -y and its up the device's x).
 *
 * <ul>
 *   <li>yaw: about the screen's vertical axis, positive turns left: the device turned like a
 *       camera, as most shooters do (a tester, 20.10.2026; before, the axis through the screen
 *       as in the first port: tilting like a steering wheel, "as if it's a racing game")
 *   <li>pitch: about the screen's horizontal axis, positive looks up (as before)
 * </ul>
 *
 * Tilting about the axis through the screen does nothing.
 */
final class GyroAxes {
  private GyroAxes() {}

  /** Turning left (rad/s) at the display's rotation; v: x, y, z of the reading. */
  static float yaw(int rotation, float[] v) {
    switch (rotation & 3) {
      case 1: return v[0];
      case 2: return -v[1];
      case 3: return -v[0];
      default: return v[1];
    }
  }

  /** Looking up (rad/s) at the display's rotation; v: x, y, z of the reading. */
  static float pitch(int rotation, float[] v) {
    switch (rotation & 3) {
      case 1: return -v[1];
      case 2: return -v[0];
      case 3: return v[1];
      default: return v[0];
    }
  }

  /** sensorTurn: gravity does not tell (the device lies flat or is held between two quarter turns). */
  static final int TURN_UNKNOWN = -1;

  /**
   * 1.1.6 (a user's report, Anbernic RG477M: up/down turns the aim sideways and left/right up and down; the log showed
   * gravity along the screen's left/right while held normally): some devices report the sensors' axes a quarter (or
   * half) turn from the natural orientation's, about the axis through the screen. The quarter turns (0..3,
   * counter-clockwise) that bring them back, from one gravity reading (the accelerometer, the device's axes, m/s2;
   * at rest it points to the sky) while the screen shows `rotation`: whoever aims holds the picture upright, so
   * gravity in the screen's plane points to the screen's up. TURN_UNKNOWN when the screen's plane holds less than
   * 4 m/s2 of it (lying flat or nearly) or neither of its axes holds twice the other's.
   *
   * <p>The turn is the same whatever the screen's rotation (turns about one axis can be taken in any order): turn(q, v)
   * comes before yaw/pitch(rotation, ...).
   */
  static int sensorTurn(int rotation, float[] gravity) {
    float across = pitch(rotation, gravity), up = yaw(rotation, gravity);
    float a = Math.abs(across), u = Math.abs(up);
    if (Math.max(a, u) < 4f) return TURN_UNKNOWN;
    if (u >= 2f * a) return up > 0 ? 0 : 2;
    if (a >= 2f * u) return across > 0 ? 1 : 3;
    return TURN_UNKNOWN;
  }

  /** v (x, y, z) turned by `quarters` quarter turns counter-clockwise about z, into out (may be v). */
  static void turn(int quarters, float[] v, float[] out) {
    float x = v[0], y = v[1];
    switch (quarters & 3) {
      case 1: out[0] = -y; out[1] = x; break;
      case 2: out[0] = -x; out[1] = -y; break;
      case 3: out[0] = y; out[1] = -x; break;
      default: out[0] = x; out[1] = y; break;
    }
    out[2] = v[2];
  }
}
