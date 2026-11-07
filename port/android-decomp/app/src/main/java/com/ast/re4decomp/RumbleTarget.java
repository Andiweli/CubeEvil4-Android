package com.ast.re4decomp;

/**
 * Where the game's rumble goes (the menu's "Vibration", wish of the project owner, 07.11.2026: with a Bluetooth
 * controller on a phone the hits were felt in the phone). Before 0.99.2 always the device's own vibrator.
 *
 * <ul>
 *   <li>AUTO (the default): the motors of the controller used last when it has some and is not built in (Android 12
 *       and later: InputDevice.getVibratorManager), else the device's vibrator. On handhelds such as the Retroid or
 *       the AYN the built-in controller is the device itself: their rumble stays the device's, as before.
 *   <li>DEVICE: the device's vibrator.
 *   <li>CONTROLLER: the controller's motors, nothing when it has none; before Android 12 the device's vibrator
 *       (a controller's motors are reachable from Android 12 on).
 *   <li>OFF: none.
 * </ul>
 * The game's own option "Vibration" and the app's rules (no rumble while the game stands still) stay as they are.
 * Pure Java (RumbleTargetTest).
 */
final class RumbleTarget {
  static final int AUTO = 0, DEVICE = 1, CONTROLLER = 2, OFF = 3;
  static final int NONE = 0, PHONE = 1, PAD = 2;
  /** The first Android version whose controllers' motors an app can drive (InputDevice.getVibratorManager). */
  static final int PAD_SDK = 31;

  private RumbleTarget() {}

  static int clampMode(int mode) {
    return mode < AUTO || mode > OFF ? AUTO : mode;
  }

  /**
   * The target for menu mode `mode` on Android `sdk`: `padMotors` the controller used last has motors, `padExternal`
   * it is not built in, `phoneMotor` the device has a vibrator.
   */
  static int choose(int mode, int sdk, boolean padMotors, boolean padExternal, boolean phoneMotor) {
    int phone = phoneMotor ? PHONE : NONE;
    switch (clampMode(mode)) {
      case OFF:
        return NONE;
      case DEVICE:
        return phone;
      case CONTROLLER:
        if (sdk < PAD_SDK) return phone;
        return padMotors ? PAD : NONE;
      default:
        return sdk >= PAD_SDK && padMotors && padExternal ? PAD : phone;
    }
  }

  static String modeName(int mode) {
    switch (clampMode(mode)) {
      case DEVICE: return "Device";
      case CONTROLLER: return "Controller";
      case OFF: return "Off";
      default: return "Auto";
    }
  }
}
