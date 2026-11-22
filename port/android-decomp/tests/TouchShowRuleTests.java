package com.ast.re4decomp;

/**
 * When the touch controls show (TouchShowRule, the menu's setting "Touch Controls" of 21.10.2026):
 * Auto shows them without a controller and after a tap until the controller's next button;
 * Force always; Suppress not while a controller is connected and a tap does not bring them back,
 * except as long as no controller was used since the start (a device that only says it is a
 * controller, as on a Redmi 15 5G, must not leave the player without any controls).
 */
public final class TouchShowRuleTests {
  static int checks;

  static void check(boolean value) {
    checks++;
    if (!value)
      throw new AssertionError("check " + checks);
  }

  public static void main(String[] args) {
    // (1.1.1) what counts as a controller: a gamepad always; a keyboard, a mouse or a PC / Chromebook with Suppress only
    check(TouchShowRule.counts(TouchShowRule.AUTO, true, false, false));
    check(!TouchShowRule.counts(TouchShowRule.AUTO, false, true, true));     // Auto: a phone with a keyboard as before
    check(!TouchShowRule.counts(TouchShowRule.FORCE, false, true, true));
    check(TouchShowRule.counts(TouchShowRule.SUPPRESS, false, true, false)); // Suppress: a keyboard or a mouse
    check(TouchShowRule.counts(TouchShowRule.SUPPRESS, false, false, true)); // Suppress: a Chromebook
    check(!TouchShowRule.counts(TouchShowRule.SUPPRESS, false, false, false));
    // a Chromebook with Suppress: hidden from the start; a tap shows them until a key is used, then no more
    TouchShowRule book = new TouchShowRule();
    book.setMode(TouchShowRule.SUPPRESS);
    final boolean counted = TouchShowRule.counts(book.mode(), false, true, true);
    check(!book.shown(counted));
    check(book.touch());
    check(book.shown(counted));
    check(book.controller());     // a key of its keyboard (CoreActivity: keyboard keys count as the controller's use)
    check(!book.shown(counted));
    check(!book.touch());
    check(!book.shown(counted));

    // Auto (the default)
    TouchShowRule rule = new TouchShowRule();
    check(rule.mode() == TouchShowRule.AUTO);
    check(rule.shown(false));
    check(!rule.shown(true));
    check(!rule.touched());
    check(rule.touch());          // a tap while a controller hides them: shown, once
    check(rule.touched());
    check(rule.shown(true));
    check(!rule.touch());
    check(rule.controller());     // the controller's button: hidden again, once
    check(!rule.shown(true));
    check(!rule.controller());
    check(rule.touch());          // in Auto a tap shows them again after the controller's use too
    check(rule.shown(true));
    check(rule.shown(false));

    // Force: always, whatever happens
    TouchShowRule force = new TouchShowRule();
    force.setMode(TouchShowRule.FORCE);
    check(force.mode() == TouchShowRule.FORCE);
    check(force.shown(true));
    check(force.shown(false));
    force.controller();
    check(force.shown(true));

    // Suppress with a real controller: hidden; a tap before its first use shows them (it could be
    // a device that only says it is a controller), its first use hides them, later taps do not
    TouchShowRule suppress = new TouchShowRule();
    suppress.setMode(TouchShowRule.SUPPRESS);
    check(!suppress.shown(true));
    check(suppress.shown(false));         // no controller at all: shown
    check(suppress.touch());
    check(suppress.shown(true));
    check(suppress.controller());
    check(!suppress.shown(true));
    check(!suppress.touch());             // screenshots with the controller in the hand
    check(!suppress.shown(true));
    check(!suppress.touched());
    check(suppress.shown(false));         // the controller went away: shown

    // Suppress with a device that never sends anything: a tap shows them for good
    TouchShowRule phantom = new TouchShowRule();
    phantom.setMode(TouchShowRule.SUPPRESS);
    check(!phantom.shown(true));
    check(phantom.touch());
    check(phantom.shown(true));
    check(!phantom.touch());
    check(phantom.shown(true));

    // changing the setting in the menu takes effect at once
    TouchShowRule change = new TouchShowRule();
    change.controller();                  // a controller was used
    change.touch();                       // Auto: a tap shows them
    check(change.shown(true));
    change.setMode(TouchShowRule.SUPPRESS);
    check(!change.shown(true));           // Suppress after a used controller: hidden
    change.setMode(TouchShowRule.FORCE);
    check(change.shown(true));
    change.setMode(TouchShowRule.AUTO);
    check(change.shown(true));            // the earlier tap still counts in Auto
    change.setMode(7);                    // an unknown value: Auto
    check(change.mode() == TouchShowRule.AUTO);
    check(TouchShowRule.name(TouchShowRule.AUTO).equals("auto"));
    check(TouchShowRule.name(TouchShowRule.FORCE).equals("force"));
    check(TouchShowRule.name(TouchShowRule.SUPPRESS).equals("suppress"));

    // deliberate moves: past half the way, in either direction; a resting stick's noise is not one
    check(!TouchShowRule.moved());
    check(!TouchShowRule.moved(0, 0, 0, 0, 0, 0, 0, 0));
    check(!TouchShowRule.moved(0.05f, -0.04f, 0.1f, -0.2f, 0.3f, 0.1f, 0, 0));
    check(!TouchShowRule.moved(TouchShowRule.MOVED, -TouchShowRule.MOVED));
    check(TouchShowRule.moved(0.51f));
    check(TouchShowRule.moved(0, -0.8f));
    check(TouchShowRule.moved(0, 0, 0, 0, 1, 0, 0, 0));      // a trigger pulled
    check(TouchShowRule.moved(0, 0, 0, 0, 0, 0, -1, 0));     // the hat left
    check(TouchShowRule.moved(0, 0, 0, 0, 0, 0, 0, 1));      // the hat down
    check(!TouchShowRule.moved(Float.NaN));                  // CoreActivity.axis gives 0 for NaN anyway

    System.out.println("TouchShowRuleTests: " + checks + " checks passed");
  }
}
