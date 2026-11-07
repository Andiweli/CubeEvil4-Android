package com.ast.re4decomp;

import static com.ast.re4decomp.RumbleTarget.*;
import static org.junit.Assert.*;

import org.junit.Test;

/** The menu's "Vibration" (0.99.2): where the game's rumble goes, and the random items' seed in the cheat bits. */
public class RumbleTargetTest {
  @Test public void autoTakesAnExternalControllersMotors() {
    assertEquals(PAD, choose(AUTO, 31, true, true, true));
    assertEquals(PAD, choose(AUTO, 35, true, true, false));
  }

  @Test public void autoKeepsTheDeviceOnHandhelds() {
    // the Retroid's or AYN's built-in controller is the device itself: its rumble stays the device's
    assertEquals(PHONE, choose(AUTO, 33, true, false, true));
    // a controller without motors, or before Android 12
    assertEquals(PHONE, choose(AUTO, 33, false, true, true));
    assertEquals(PHONE, choose(AUTO, 30, true, true, true));
    assertEquals(NONE, choose(AUTO, 30, true, true, false));
  }

  @Test public void theOtherChoices() {
    assertEquals(PHONE, choose(DEVICE, 35, true, true, true));
    assertEquals(NONE, choose(DEVICE, 35, true, true, false));
    assertEquals(PAD, choose(CONTROLLER, 31, true, false, true));
    assertEquals(NONE, choose(CONTROLLER, 31, false, true, true));
    assertEquals(PHONE, choose(CONTROLLER, 29, true, true, true));  // the motors need Android 12
    assertEquals(NONE, choose(OFF, 35, true, true, true));
    // an unknown stored value is Auto
    assertEquals(AUTO, clampMode(7));
    assertEquals("Controller", modeName(CONTROLLER));
  }

  @Test public void randomItemsBitsKeepTheOtherCheats() {
    int bits = 1 | CoreMenu.INVISIBLE_BIT | (3 << CoreMenu.CHARACTER_SHIFT);
    int on = CoreMenu.withRandomItems(bits, true, 0xFFFFF);
    assertEquals(bits, on & ~(CoreMenu.RANDOM_ITEMS_BIT | CoreMenu.SEED_BITS));
    assertEquals(0xFFFFF, (on & CoreMenu.SEED_BITS) >>> CoreMenu.SEED_SHIFT);
    assertTrue(on > 0);  // bit 31 stays free
    assertEquals(bits, CoreMenu.withRandomItems(on, false, 5));
    // the core's mask (runtime/cheats.cpp kAll): health .. invisibility, 1024 and the seed bits
    assertEquals(0x7FFFF800, CoreMenu.SEED_BITS);
  }
}
