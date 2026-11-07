package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.view.InputDevice;
import org.junit.Test;

/** Which devices are keyboards and which are controllers (CoreActivity.keyboardSources / gameSources). */
public class InputDeviceKindTest {
  private static final int ALPHA = InputDevice.KEYBOARD_TYPE_ALPHABETIC, NONE = InputDevice.KEYBOARD_TYPE_NONE,
      NON_ALPHA = InputDevice.KEYBOARD_TYPE_NON_ALPHABETIC;

  @Test public void aKeyboardWithArrowKeysIsAKeyboard() {
    // Android gives a keyboard with arrow keys the D-pad's source as well (0x301): tester report 05.11.2026
    int keyboardWithArrows = InputDevice.SOURCE_KEYBOARD | InputDevice.SOURCE_DPAD;
    assertTrue(CoreActivity.keyboardSources(keyboardWithArrows, ALPHA));
    assertFalse(CoreActivity.gameSources(keyboardWithArrows, ALPHA));
    // a keyboard and touchpad / mouse in one device
    int combo = InputDevice.SOURCE_KEYBOARD | InputDevice.SOURCE_DPAD | InputDevice.SOURCE_MOUSE;
    assertTrue(CoreActivity.keyboardSources(combo, ALPHA));
    assertFalse(CoreActivity.gameSources(combo, ALPHA));
    assertTrue(CoreActivity.keyboardSources(InputDevice.SOURCE_KEYBOARD, ALPHA));
    // a Bluetooth keyboard that declares a few gamepad buttons as well (0x701 and no stick): still a keyboard
    int btKeyboard = InputDevice.SOURCE_KEYBOARD | InputDevice.SOURCE_DPAD | InputDevice.SOURCE_GAMEPAD;
    assertTrue(CoreActivity.keyboardSources(btKeyboard, ALPHA));
    assertFalse(CoreActivity.gameSources(btKeyboard, ALPHA));
  }

  @Test public void controllersStayControllers() {
    // the Retroid's built-in controller: keys, gamepad, joystick
    int retroid = InputDevice.SOURCE_KEYBOARD | InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK | InputDevice.SOURCE_DPAD;
    assertFalse(CoreActivity.keyboardSources(retroid, NON_ALPHA));
    assertTrue(CoreActivity.gameSources(retroid, NON_ALPHA));
    // a controller that says it has letters still is a controller (gamepad)
    assertFalse(CoreActivity.keyboardSources(retroid, ALPHA));
    assertTrue(CoreActivity.gameSources(retroid, ALPHA));
    // a remote with a D-pad only
    int remote = InputDevice.SOURCE_KEYBOARD | InputDevice.SOURCE_DPAD;
    assertFalse(CoreActivity.keyboardSources(remote, NON_ALPHA));
    assertTrue(CoreActivity.gameSources(remote, NON_ALPHA));
    // the OnePlus alert slider ("gf_input", 0x501): a gamepad source, not a keyboard
    assertFalse(CoreActivity.keyboardSources(0x501, NON_ALPHA));
    // a mouse alone: neither
    assertFalse(CoreActivity.keyboardSources(InputDevice.SOURCE_MOUSE, NONE));
    assertFalse(CoreActivity.gameSources(InputDevice.SOURCE_MOUSE, NONE));
  }
}
