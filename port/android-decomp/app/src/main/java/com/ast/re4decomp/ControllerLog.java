package com.ast.re4decomp;

import java.util.HashSet;
import java.util.Set;

/**
 * Which keys and axes of a controller were reported already, so the error log names each unmapped
 * key and unread axis once per device (a rare controller whose buttons the game does not see; report
 * of 25.10.2026). No Android types: the host tests run it.
 */
final class ControllerLog {
  private final Set<Long> keys = new HashSet<>();
  private final Set<Long> axes = new HashSet<>();
  private final Set<Integer> devices = new HashSet<>();
  /** At most this many lines per kind, so a noisy device cannot fill the log. */
  static final int LIMIT = 64;

  private static long key(int device, int code) { return ((long) device << 32) | (code & 0xffffffffL); }

  /** True the first time `code` is seen from `device` (and within the limit). */
  synchronized boolean firstKey(int device, int code) {
    return keys.size() < LIMIT && keys.add(key(device, code));
  }

  /** True the first time `axis` of `device` moved past half its range (and within the limit). */
  synchronized boolean firstAxis(int device, int axis) {
    return axes.size() < LIMIT && axes.add(key(device, axis));
  }

  /** True the first time `device` is described (until forget). */
  synchronized boolean firstDevice(int device) { return devices.add(device); }

  /** A changed or removed device is reported again. */
  synchronized void forget(int device) {
    devices.remove(device);
    keys.removeIf(k -> (int) (k >> 32) == device);
    axes.removeIf(k -> (int) (k >> 32) == device);
  }

  /** The axes the game reads (sticks, triggers, hat): everything else is "not read". */
  static boolean readAxis(int axis) {
    switch (axis) {
      case 0:   // AXIS_X
      case 1:   // AXIS_Y
      case 11:  // AXIS_Z
      case 14:  // AXIS_RZ
      case 12:  // AXIS_RX
      case 13:  // AXIS_RY
      case 17:  // AXIS_LTRIGGER
      case 18:  // AXIS_RTRIGGER
      case 22:  // AXIS_BRAKE
      case 23:  // AXIS_GAS
      case 15:  // AXIS_HAT_X
      case 16:  // AXIS_HAT_Y
        return true;
      default:
        return false;
    }
  }
}
