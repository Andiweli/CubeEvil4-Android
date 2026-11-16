package com.ast.re4decomp;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The core mode's touch controls (CoreTouch): RetroTouch's callbacks as
 * GameCube controller 0 (buttons, L/R held while touched or latched by a tap, D-pad, move stick,
 * look zone as the C stick that goes back when the finger lifts, MENU as
 * SELECT), together with a real controller, and the default layout on
 * screens of 4:3 to 20:9 (RetroTouch's geometry: no touch areas overlap,
 * everything drawn inside the screen, short labels).
 */
public final class CoreTouchTests {
  static int checks;

  static void check(boolean value) {
    checks++;
    if (!value)
      throw new AssertionError("check " + checks);
  }

  static final class Host implements CoreTouch.Host {
    final List<String> calls = new ArrayList<>();
    @Override public void select(boolean down) { calls.add("select " + down); }
    @Override public void latched(String action, boolean on) { calls.add(action + " " + on); }
    @Override public void r3(boolean down) { calls.add("r3 " + down); }
    @Override public void zoom(int direction, boolean down) { calls.add("zoom " + direction + " " + down); }
  }

  public static void main(String[] args) {
    CoreInput in = new CoreInput();
    Host host = new Host();
    CoreTouch t = new CoreTouch(in, host);

    // every button by its label, the D-pad's directions
    String[] actions = {CoreTouch.A, CoreTouch.B, CoreTouch.X, CoreTouch.Y, CoreTouch.Z, CoreTouch.START,
        CoreTouch.UP, CoreTouch.DOWN, CoreTouch.LEFT, CoreTouch.RIGHT};
    int[] bits = {CoreInput.GC_A, CoreInput.GC_B, CoreInput.GC_X, CoreInput.GC_Y, CoreInput.GC_Z,
        CoreInput.GC_START, CoreInput.GC_UP, CoreInput.GC_DOWN, CoreInput.GC_LEFT, CoreInput.GC_RIGHT};
    for (int i = 0; i < actions.length; i++) {
      t.action(actions[i], true);
      check(in.pad()[0] == bits[i]);
      t.action(actions[i], false);
      check(in.pad()[0] == 0);
    }
    t.action("unknown", true);
    check(in.pad()[0] == 0);

    // R3: the app's graphics capture, no GameCube button; offered to the editor with its label
    host.calls.clear();
    t.action(CoreTouch.R3, true);
    t.action(CoreTouch.R3, false);
    check(in.pad()[0] == 0 && host.calls.size() == 2 && host.calls.get(0).equals("r3 true")
        && host.calls.get(1).equals("r3 false"));
    check(CoreTouch.label(CoreTouch.R3).equals("R3")
        && CoreTouch.ACTIONS.length == CoreTouch.LABELS.length && CoreTouch.LABELS.length == CoreTouch.COLOURS.length);
    // the zoom buttons (0.99.15): no GameCube button, the host zooms while held; + and - as labels
    host.calls.clear();
    t.action(CoreTouch.ZOOM_IN, true);
    t.action(CoreTouch.ZOOM_IN, false);
    t.action(CoreTouch.ZOOM_OUT, true);
    check(in.pad()[0] == 0 && host.calls.size() == 3 && host.calls.get(0).equals("zoom 1 true")
        && host.calls.get(1).equals("zoom 1 false") && host.calls.get(2).equals("zoom -1 true"));
    check(CoreTouch.label(CoreTouch.ZOOM_IN).equals("+") && CoreTouch.label(CoreTouch.ZOOM_OUT).equals("−"));
    t.action(CoreTouch.ZOOM_OUT, false);
    host.calls.clear();

    // L and R by default: held while the finger is on them (the trigger at its end stop), as real buttons
    check(!t.latch());
    t.action(CoreTouch.R, true);
    check(in.pad()[0] == CoreInput.GC_R && in.pad()[6] == 255 && !t.latched(CoreTouch.R));
    t.action(CoreTouch.A, true);   // fire while the weapon is ready (two fingers)
    check(in.pad()[0] == (CoreInput.GC_R | CoreInput.GC_A));
    t.action(CoreTouch.A, false);
    t.action(CoreTouch.R, false);  // the finger lifts: R goes
    check(in.pad()[0] == 0 && in.pad()[6] == 0);
    t.action(CoreTouch.L, true);
    check(in.pad()[0] == CoreInput.GC_L && in.pad()[5] == 255);
    t.action(CoreTouch.L, false);
    check(in.pad()[0] == 0 && in.pad()[5] == 0 && host.calls.isEmpty());
    // switching to latching while L is held lets it go
    t.action(CoreTouch.L, true);
    t.setLatch(true);
    check(t.latch() && in.pad()[0] == 0 && in.pad()[5] == 0 && host.calls.isEmpty());

    // with the menu's "Latch L and R": a tap latches them, the next tap lets go
    t.action(CoreTouch.R, true);
    check(in.pad()[0] == CoreInput.GC_R && in.pad()[6] == 255 && in.pad()[5] == 0);
    t.action(CoreTouch.R, false);  // the finger lifts: R stays
    check(in.pad()[0] == CoreInput.GC_R && t.latched(CoreTouch.R));
    t.action(CoreTouch.A, true);   // fire while the weapon is ready
    check(in.pad()[0] == (CoreInput.GC_R | CoreInput.GC_A));
    t.action(CoreTouch.A, false);
    t.action(CoreTouch.L, true);
    check(in.pad()[0] == (CoreInput.GC_R | CoreInput.GC_L) && in.pad()[5] == 255);
    t.action(CoreTouch.R, true);
    t.action(CoreTouch.R, false);
    check(in.pad()[0] == CoreInput.GC_L && in.pad()[6] == 0 && !t.latched(CoreTouch.R));
    check(host.calls.equals(List.of("r true", "l true", "r false")));
    host.calls.clear();

    // MENU is the app's SELECT, nothing for the game
    t.action(CoreTouch.MENU, true);
    t.action(CoreTouch.MENU, false);
    check(host.calls.equals(List.of("select true", "select false")) && in.pad()[0] == CoreInput.GC_L);
    host.calls.clear();

    // reset: everything let go, the latched L shown let go too
    t.reset();
    check(in.pad()[0] == 0 && in.pad()[5] == 0 && host.calls.equals(List.of("l false")) && !t.latched(CoreTouch.L));
    host.calls.clear();

    // switching latching off lets a latched button go and shows it let go
    t.action(CoreTouch.R, true);
    t.action(CoreTouch.R, false);
    check(in.pad()[0] == CoreInput.GC_R && t.latched(CoreTouch.R));
    host.calls.clear();
    t.setLatch(false);
    check(!t.latch() && in.pad()[0] == 0 && in.pad()[6] == 0 && !t.latched(CoreTouch.R)
        && host.calls.equals(List.of("r false")));
    host.calls.clear();

    // "Latch B" (22.10.2026): off, B is held while touched; on, a tap keeps B (run) pressed, the
    // next tap lets go; L and R stay as they are; switching it off or a reset lets B go
    check(!t.latchB());
    t.action(CoreTouch.B, true);
    check(in.pad()[0] == CoreInput.GC_B && !t.latched(CoreTouch.B));
    t.action(CoreTouch.B, false);
    check(in.pad()[0] == 0 && host.calls.isEmpty());
    t.setLatchB(true);
    check(t.latchB() && !t.latch());
    t.action(CoreTouch.B, true);
    t.action(CoreTouch.B, false);
    check(in.pad()[0] == CoreInput.GC_B && t.latched(CoreTouch.B));
    t.action(CoreTouch.A, true);
    check(in.pad()[0] == (CoreInput.GC_B | CoreInput.GC_A));
    t.action(CoreTouch.A, false);
    t.action(CoreTouch.R, true);
    t.action(CoreTouch.R, false);
    check(in.pad()[0] == CoreInput.GC_B && !t.latched(CoreTouch.R));
    t.action(CoreTouch.B, true);
    t.action(CoreTouch.B, false);
    check(in.pad()[0] == 0 && !t.latched(CoreTouch.B) && host.calls.equals(List.of("b true", "b false")));
    host.calls.clear();
    t.action(CoreTouch.B, true);
    t.setLatchB(false);
    check(!t.latchB() && in.pad()[0] == 0 && host.calls.equals(List.of("b true", "b false")));
    host.calls.clear();
    t.setLatchB(true);
    t.action(CoreTouch.B, true);
    t.reset();
    check(in.pad()[0] == 0 && !t.latched(CoreTouch.B) && host.calls.equals(List.of("b true", "b false")));
    t.setLatchB(false);
    host.calls.clear();

    // the move stick is the control stick (raw, up positive)
    t.move(1, 0);
    check(in.pad()[1] == 101 && in.pad()[2] == 0);
    t.move(0, -1);
    check(in.pad()[1] == 0 && in.pad()[2] == 101);
    t.move(0, 0);
    check(in.pad()[1] == 0 && in.pad()[2] == 0);

    // the C stick as an analog stick of the editor (RetroTouch's stick action, not in the default
    // layout, wish of the project owner 26.10.2026): its tilt as it is, back when let go; with the look
    // zone the one that tilts further counts; yellow as on the GameCube, no button of its own
    check(CoreTouch.isStick(CoreTouch.C_STICK) && !CoreTouch.isStick(CoreTouch.A));
    check(CoreTouch.label(CoreTouch.C_STICK).equals("C") && CoreTouch.gcButton(CoreTouch.C_STICK) == 0);
    t.stick(CoreTouch.C_STICK, 1, 0);
    check(in.pad()[0] == 0 && in.pad()[3] == 92 && in.pad()[4] == 0 && host.calls.isEmpty());
    t.stick(CoreTouch.C_STICK, 0.5f, -1);
    check(in.pad()[3] == 46 && in.pad()[4] == 92);
    t.stick("other", 0, 0);  // another stick action: nothing
    check(in.pad()[3] == 46 && in.pad()[4] == 92);
    t.stick(CoreTouch.C_STICK, 0, 0);
    check(in.pad()[3] == 0 && in.pad()[4] == 0);
    t.stick(CoreTouch.C_STICK, 0, 1);
    check(in.pad()[4] == -92);
    t.reset();  // everything let go
    check(in.pad()[3] == 0 && in.pad()[4] == 0);
    t.stick(CoreTouch.C_STICK, 1, 0);
    t.beginEvent();
    t.fingerDown(5, true);
    t.look(CoreTouch.LOOK_RANGE / 2, 0);  // the look zone tilts less than the stick: the stick stays
    check(in.pad()[3] == 92);
    t.look(0, 0);
    t.stick(CoreTouch.C_STICK, 0, 0);
    check(in.pad()[3] == 46);  // the looking finger shows through again
    t.fingerUp(5);
    check(in.pad()[3] == 0 && in.pad()[4] == 0);

    // the look zone: a finger that went down there (no control answered) tilts the C stick by its way
    t.beginEvent();
    t.fingerDown(3, true);
    check(t.looking());
    t.look(CoreTouch.LOOK_RANGE / 2, 0);
    check(in.pad()[3] == 46 && in.pad()[4] == 0);
    t.look(0, CoreTouch.LOOK_RANGE / 2);  // down on the screen: C stick down
    check(in.pad()[3] == 46 && in.pad()[4] == -46);
    t.look(0, -CoreTouch.LOOK_RANGE / 2);
    t.look(CoreTouch.LOOK_RANGE * 2, 0);  // past the full way: full, the centre moves along
    check(in.pad()[3] == 92);
    t.look(-CoreTouch.LOOK_RANGE / 2, 0);  // back by half the range: half at once
    check(in.pad()[3] == 46);
    t.fingerUp(3);  // lifted: back to the centre
    check(!t.looking() && in.pad()[3] == 0 && in.pad()[4] == 0);
    t.look(CoreTouch.LOOK_RANGE, 0);  // no looking finger: nothing
    check(in.pad()[3] == 0);

    // a finger that pressed a control does not look, nor one outside the look zone
    t.beginEvent();
    t.action(CoreTouch.A, true);
    t.fingerDown(4, true);
    t.beginEvent();
    t.move(0.5f, 0);
    t.fingerDown(5, true);
    t.beginEvent();
    t.fingerDown(6, false);
    check(!t.looking());
    t.action(CoreTouch.A, false);
    t.move(0, 0);

    // two looking fingers: the C stick goes back when the last lifts
    t.beginEvent();
    t.fingerDown(7, true);
    t.beginEvent();
    t.fingerDown(8, true);
    t.look(CoreTouch.LOOK_RANGE, 0);
    t.fingerUp(7);
    check(in.pad()[3] == 92);
    t.fingerUp(9);  // a finger that did not look: nothing
    check(in.pad()[3] == 92);
    t.fingerUp(8);
    check(in.pad()[3] == 0);

    // Android took the fingers (another window): back to the centre
    t.beginEvent();
    t.fingerDown(1, true);
    t.look(0, -CoreTouch.LOOK_RANGE);
    check(in.pad()[4] == 92);
    t.cancel();
    check(!t.looking() && in.pad()[4] == 0);

    // 1.0, "Turn Leon with mouse / look area": a looking finger's way sideways turns Leon (CoreInput.takeBodyTurn; the
    // core turns him where he stands, walks or runs), the C stick only up and down; aiming with "Touch Area" the swipe
    // aims as before and turns no one; off again the look zone looks as before
    in.setBodyTurn(true);
    t.beginEvent();
    t.fingerDown(1, true);
    t.look(0.05f, 0);  // to the right: Leon turns right (yaw negative), the camera stays
    check(in.pad()[3] == 0 && Math.abs(in.takeBodyTurn() + 0.05f * CoreInput.SWIPE_RADIANS) < 1e-6f);
    t.look(-0.02f, -CoreTouch.LOOK_RANGE / 2);  // left and up: Leon turns left, the camera looks up
    check(in.pad()[3] == 0 && in.pad()[4] == 46
        && Math.abs(in.takeBodyTurn() - 0.02f * CoreInput.SWIPE_RADIANS) < 1e-6f && in.takeBodyTurn() == 0);
    in.setTouchAim(CoreInput.AIM_LOOK);
    t.action(CoreTouch.R, true);
    in.pad();  // (R held: the look zone aims)
    t.look(0.1f, 0);
    in.pad();
    check(in.takeAimSwipe() != null && in.takeBodyTurn() == 0);
    t.action(CoreTouch.R, false);
    in.setTouchAim(CoreInput.AIM_MOVE_STICK);
    t.fingerUp(1);
    in.setBodyTurn(false);
    t.beginEvent();
    t.fingerDown(1, true);
    t.look(0.05f, 0);
    check(in.pad()[3] > 20 && in.takeBodyTurn() == 0);
    t.fingerUp(1);
    check(in.pad()[3] == 0 && in.pad()[4] == 0);

    // touch aiming (the menu's "Touch Aiming"): while R is held the chosen way moves the control
    // stick, the other stays the C stick; without R both are the C stick
    t.reset();
    check(in.touchAim() == CoreInput.AIM_MOVE_STICK);
    t.beginEvent();
    t.fingerDown(2, true);
    t.look(CoreTouch.LOOK_RANGE, 0);
    t.action(CoreTouch.R, true);
    int[] p = in.pad();
    check(p[1] == 0 && p[3] == 92);  // the default: aiming as on the GameCube, the look zone looks
    in.setTouchAim(CoreInput.AIM_LOOK);
    p = in.pad();
    // the look zone aims by swipes (29.10.2026): no stick, no look while R is held
    check(p[1] == 0 && p[2] == 0 && p[3] == 0 && p[0] == CoreInput.GC_R && in.takeAimSwipe() == null);
    t.look(0.1f, 0);  // a swipe to the right: the aim turns right (yaw negative) by the swipe's way
    p = in.pad();
    float[] turn = in.takeAimSwipe();
    check(turn != null && Math.abs(turn[0] + 0.1f * CoreInput.SWIPE_RADIANS) < 1e-5f && turn[1] == 0 && p[1] == 0
        && p[3] == 0);
    check(in.takeAimSwipe() == null);  // the finger stops: the aim stops
    t.look(0.02f, -0.05f);
    t.look(0.03f, 0);  // two moves before the next event: added up, up is positive
    in.pad();
    turn = in.takeAimSwipe();
    check(turn != null && Math.abs(turn[0] + 0.05f * CoreInput.SWIPE_RADIANS) < 1e-5f
        && Math.abs(turn[1] - 0.05f * CoreInput.SWIPE_RADIANS) < 1e-5f);
    t.stick(CoreTouch.C_STICK, 0, -1);
    p = in.pad();
    check(p[1] == 0 && p[3] == 0 && p[4] == 92);  // the C stick control stays the C stick
    t.move(-0.2f, 0);
    check(in.pad()[1] < 0);  // the move stick still aims as on the GameCube
    t.move(0, 0);
    in.setTouchAim(CoreInput.AIM_C_STICK);
    t.look(CoreTouch.LOOK_RANGE, 0);  // not by swipes now: the zone looks again
    check(in.takeAimSwipe() == null);
    p = in.pad();
    check(p[2] > 0 && p[1] == 0 && p[3] == 92 && p[4] == 0);  // now the C stick control aims up, the zone looks
    t.action(CoreTouch.R, false);
    p = in.pad();
    check(p[1] == 0 && p[2] == 0 && p[4] == 92);  // R let go: both the C stick again
    // R let go with "Touch Area": the zone's finger looks, no swipe aims
    in.setTouchAim(CoreInput.AIM_LOOK);
    in.pad();
    t.look(-0.01f, 0);
    check(in.takeAimSwipe() == null);
    // the knife (L held, 0.99.9: the core turns the knife stance by the swipe too): the same two ways as with R
    t.reset();
    t.beginEvent();
    t.fingerDown(2, true);
    t.action(CoreTouch.L, true);
    in.setTouchAim(CoreInput.AIM_C_STICK);
    t.stick(CoreTouch.C_STICK, -1, 0);
    p = in.pad();
    check(p[1] < 0 && p[3] == 0 && (p[0] & CoreInput.GC_L) != 0);  // the C stick control aims the knife left
    t.stick(CoreTouch.C_STICK, 0, 0);
    in.setTouchAim(CoreInput.AIM_LOOK);
    in.pad();
    t.look(-0.1f, 0.05f);
    p = in.pad();
    turn = in.takeAimSwipe();
    check(turn != null && Math.abs(turn[0] - 0.1f * CoreInput.SWIPE_RADIANS) < 1e-5f
        && Math.abs(turn[1] + 0.05f * CoreInput.SWIPE_RADIANS) < 1e-5f && p[1] == 0 && p[3] == 0);
    t.action(CoreTouch.L, false);
    in.pad();
    t.look(-0.1f, 0);
    check(in.takeAimSwipe() == null);  // L let go: the zone looks again
    t.reset();
    in.setTouchAim(9);
    check(in.touchAim() == CoreInput.AIM_MOVE_STICK);
    t.reset();

    // the touch controls next to a real controller: buttons of both, the stick moved furthest
    in.key(1, 96, true);  // KEYCODE_BUTTON_A
    t.action(CoreTouch.B, true);
    check(in.pad()[0] == (CoreInput.GC_A | CoreInput.GC_B));
    in.motion(1, 0.4f, 0, 0, 0, 0, 0, 0, 0);
    t.move(-1, 0);
    check(in.pad()[1] == -101);
    t.move(0, 0);
    check(in.pad()[1] == 40);
    t.reset();  // the touch controls let go, the controller keeps its A
    check(in.pad()[0] == CoreInput.GC_A);
    in.clear();
    check(in.pad()[0] == 0);

    // the swap of A/B for controllers does not touch the labels on the screen
    in.setSwapFace(true);
    t.action(CoreTouch.A, true);
    check(in.pad()[0] == CoreInput.GC_A);
    t.reset();
    in.setSwapFace(false);

    aimSticks(in, t);
    layout();
    System.out.println("CoreTouchTests: " + checks + " checks passed");
  }

  /**
   * The aim stick and the knife stick (1.0): R or L held while their finger is on them, also resting in the centre;
   * their tilt is the control stick (the further of it and the move stick); L and R of the buttons and of the sticks
   * add up. Since RetroTouch 1.0.0-beta.12 its onStickDown and onStickUp say when a finger is on a stick (before its
   * first report, after its last), so the reports only tilt.
   */
  static void aimSticks(CoreInput in, CoreTouch t) {
    check(CoreTouch.isStick(CoreTouch.AIM_STICK) && CoreTouch.isStick(CoreTouch.KNIFE_STICK));
    check(CoreTouch.label(CoreTouch.AIM_STICK).equals("AIM") && CoreTouch.label(CoreTouch.KNIFE_STICK).equals("KNIFE"));
    for (Object[] each : new Object[][] {{CoreTouch.AIM_STICK, CoreInput.GC_R, 6}, {CoreTouch.KNIFE_STICK, CoreInput.GC_L, 5}}) {
      String stick = (String) each[0];
      int bit = (Integer) each[1], trigger = (Integer) each[2];
      t.reset();
      in.setTouchAim(CoreInput.AIM_MOVE_STICK);
      // a finger goes down in its centre (onStickDown, then the report (0, 0)): held at once, no tilt; that finger
      // does not look
      t.beginEvent();
      t.stickDown(stick);
      t.stick(stick, 0, 0);
      t.fingerDown(5, true);
      int[] p = in.pad();
      check((p[0] & bit) != 0 && p[trigger] == 255 && p[1] == 0 && p[2] == 0);
      check(!t.looking());
      // tilted up and right: the control stick (the aim)
      t.beginEvent();
      t.stick(stick, 0.5f, -1f);
      p = in.pad();
      check((p[0] & bit) != 0 && p[1] > 0 && p[2] > 0);
      // back to the centre while the finger stays: still held
      t.beginEvent();
      t.stick(stick, 0, 0);
      p = in.pad();
      check((p[0] & bit) != 0 && p[1] == 0 && p[2] == 0);
      // the button of the same trigger pressed and let go: the stick keeps it held
      String button = bit == CoreInput.GC_R ? CoreTouch.R : CoreTouch.L;
      t.action(button, true);
      t.action(button, false);
      check((in.pad()[0] & bit) != 0);
      // A fires while it holds
      t.action(CoreTouch.A, true);
      p = in.pad();
      check((p[0] & (bit | CoreInput.GC_A)) == (bit | CoreInput.GC_A));
      t.action(CoreTouch.A, false);
      // with the move stick: the further tilt wins
      t.beginEvent();
      t.stick(stick, 0.8f, 0);
      t.move(-0.3f, 0);
      check(in.pad()[1] > 0);
      t.move(-1f, 0);
      check(in.pad()[1] < 0);
      t.move(0, 0);
      check(in.pad()[1] > 0);
      // the finger lifts (RetroTouch's (0, 0), then onStickUp, then the lift): let go and centred
      t.beginEvent();
      t.stick(stick, 0, 0);
      t.stickUp(stick);
      p = in.pad();
      check((p[0] & bit) == 0 && p[trigger] == 0 && p[1] == 0 && p[2] == 0);
      t.fingerUp(5);
      check((in.pad()[0] & bit) == 0);
      // the button alone holds it again
      t.action(button, true);
      check((in.pad()[0] & bit) != 0);
      t.action(button, false);
      check((in.pad()[0] & bit) == 0);
      // a report without onStickDown (RetroTouch centres its sticks when its layout loads, even ones not placed):
      // nothing held, no tilt
      t.stick(stick, 0, 0);
      t.stick(stick, 0.3f, 0);
      p = in.pad();
      check((p[0] & bit) == 0 && p[trigger] == 0 && p[1] == 0);
      // an onStickUp of a stick not held: nothing
      t.stickUp(stick);
      check((in.pad()[0] & bit) == 0);
    }
    // the other sticks' onStickDown (the move stick, the C stick, an id RetroTouch has none for): nothing held
    t.reset();
    t.stickDown(null);
    t.stickDown(CoreTouch.C_STICK);
    t.stickDown("other");
    check((in.pad()[0] & (CoreInput.GC_L | CoreInput.GC_R)) == 0);
    t.stickUp(null);
    t.stickUp(CoreTouch.C_STICK);
    // latching: a tapped R stays latched when the aim stick's finger lifts
    t.reset();
    t.setLatch(true);
    t.action(CoreTouch.R, true);
    t.action(CoreTouch.R, false);
    t.beginEvent();
    t.stickDown(CoreTouch.AIM_STICK);
    t.stick(CoreTouch.AIM_STICK, 0, 0);
    t.fingerDown(7, false);
    t.stick(CoreTouch.AIM_STICK, 0, 0);
    t.stickUp(CoreTouch.AIM_STICK);
    t.fingerUp(7);
    check((in.pad()[0] & CoreInput.GC_R) != 0);
    // and a tap that lets the latched R go keeps it held while the aim stick's finger is on it
    t.beginEvent();
    t.stickDown(CoreTouch.AIM_STICK);
    t.stick(CoreTouch.AIM_STICK, 0, 0);
    t.fingerDown(8, false);
    t.action(CoreTouch.R, true);
    t.action(CoreTouch.R, false);
    check(!t.latched(CoreTouch.R) && (in.pad()[0] & CoreInput.GC_R) != 0);
    t.stickUp(CoreTouch.AIM_STICK);
    t.fingerUp(8);
    check((in.pad()[0] & CoreInput.GC_R) == 0);
    t.action(CoreTouch.R, true);
    t.action(CoreTouch.R, false);
    check(t.latched(CoreTouch.R) && (in.pad()[0] & CoreInput.GC_R) != 0);
    t.setLatch(false);
    check((in.pad()[0] & CoreInput.GC_R) == 0);
    // the layout changes under the finger (RetroTouch lets all go: (0, 0), onStickUp): R let go; the finger's later
    // lift changes nothing
    t.reset();
    t.beginEvent();
    t.stickDown(CoreTouch.AIM_STICK);
    t.stick(CoreTouch.AIM_STICK, 0.4f, 0);
    t.fingerDown(3, false);
    check((in.pad()[0] & CoreInput.GC_R) != 0 && in.pad()[1] > 0);
    t.stick(CoreTouch.AIM_STICK, 0, 0);
    t.stickUp(CoreTouch.AIM_STICK);
    check((in.pad()[0] & CoreInput.GC_R) == 0 && in.pad()[1] == 0);
    t.fingerUp(3);
    check((in.pad()[0] & CoreInput.GC_R) == 0);
    // all four sticks of one layout at once (RetroTouch 1.0.0-beta.12: up to 8; the app allows 4): the move stick,
    // the C stick, the aim and the knife stick, each its own part
    t.reset();
    t.beginEvent();
    t.stickDown(CoreTouch.AIM_STICK);
    t.stick(CoreTouch.AIM_STICK, 0, 1f);
    t.fingerDown(1, false);
    t.beginEvent();
    t.stickDown(CoreTouch.KNIFE_STICK);
    t.stick(CoreTouch.KNIFE_STICK, 0, 0);
    t.fingerDown(2, false);
    t.beginEvent();
    t.stickDown(CoreTouch.C_STICK);
    t.stick(CoreTouch.C_STICK, 1f, 0);
    t.fingerDown(4, true);
    check(!t.looking());
    int[] p = in.pad();
    check((p[0] & (CoreInput.GC_L | CoreInput.GC_R)) == (CoreInput.GC_L | CoreInput.GC_R) && p[2] < 0 && p[3] > 0);
    // then Android takes the fingers: everything let go
    t.cancel();
    p = in.pad();
    check((p[0] & (CoreInput.GC_L | CoreInput.GC_R)) == 0 && p[2] == 0);
    t.reset();
  }

  static boolean validId(String id) {
    if (id.isEmpty()) return false;
    for (char c : id.toCharArray()) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
  }

  /** The default layout with RetroTouch's rules and geometry (RetroTouchView.contains, drawButton). */
  static void layout() {
    check(CoreTouch.ACTIONS.length == CoreTouch.LABELS.length && CoreTouch.ACTIONS.length == CoreTouch.COLOURS.length);
    // the GameCube's colours: A green, B red, Z purple, the C stick yellow, every other button grey
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String a = CoreTouch.ACTIONS[i];
      int want = a.equals(CoreTouch.A) ? 0xFF109B61 : a.equals(CoreTouch.B) ? 0xFFF6392C
          : a.equals(CoreTouch.Z) ? 0xFF6B5CB1 : a.equals(CoreTouch.C_STICK) ? 0xFFF7C81E : 0xFFDDDDDD;
      check(CoreTouch.COLOURS[i] == want);
    }
    Set<String> registered = new HashSet<>();
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String a = CoreTouch.ACTIONS[i];
      check(validId(a) && registered.add(a));
      // short labels: RetroTouch shrinks the text to the button, five letters stay readable
      check(!CoreTouch.LABELS[i].isEmpty() && CoreTouch.LABELS[i].length() <= 5);
      check(CoreTouch.label(a).equals(CoreTouch.LABELS[i]));
      boolean noButton = a.equals(CoreTouch.MENU) || a.equals(CoreTouch.R3) || CoreTouch.isStick(a)
          || CoreTouch.isZoom(a);  // app / C stick / zoom actions
      check(noButton ? CoreTouch.gcButton(a) == 0 : CoreTouch.gcButton(a) != 0);
    }
    Set<String> ids = new HashSet<>(), used = new HashSet<>();
    int buttons = 0, sticks = 0, looks = 0, pads = 0;
    for (CoreTouch.Control c : CoreTouch.LAYOUT) {
      check(validId(c.id) && ids.add(c.id));
      if (c.kind == CoreTouch.BUTTON) {
        buttons++;
        check(registered.contains(c.action) && used.add(c.action) && c.width == c.height);
      } else if (c.kind == CoreTouch.STICK) {
        sticks++;
      } else if (c.kind == CoreTouch.LOOK) {
        looks++;
      } else {
        pads++;
      }
    }
    // every action has its button but R3 (the editor adds it, wish of the project owner, 24.10.2026);
    // RetroTouch's limits (RetroTouchLayout.validate), with room for R3
    Set<String> placed = new HashSet<>(registered);
    placed.remove(CoreTouch.R3);
    placed.remove(CoreTouch.C_STICK);
    placed.remove(CoreTouch.ZOOM_IN);
    placed.remove(CoreTouch.ZOOM_OUT);
    placed.remove(CoreTouch.AIM_STICK);
    placed.remove(CoreTouch.KNIFE_STICK);
    // (room for R3 and the two zoom buttons the editor adds, 0.99.15)
    check(used.equals(placed) && buttons + 3 <= 16 && sticks == 1 && looks == 1 && pads == 1);
    // one move stick in the default layout, room for one more stick the editor adds: the C stick, the aim stick or
    // the knife stick (RetroTouch: at most two sticks, its editor skips a stick action when two are placed)

    // the touch areas: a button and the stick an ellipse of half the width and height times
    // sqrt(1.15), the D-pad its rectangle; in shares of the screen a circle, whatever the screen
    for (int i = 0; i < CoreTouch.LAYOUT.length; i++) {
      CoreTouch.Control a = CoreTouch.LAYOUT[i];
      if (a.kind == CoreTouch.LOOK) continue;
      for (int j = i + 1; j < CoreTouch.LAYOUT.length; j++) {
        CoreTouch.Control b = CoreTouch.LAYOUT[j];
        if (b.kind == CoreTouch.LOOK) continue;
        check(!overlap(a, b));
      }
    }
    // drawn inside the screen: radius (D-pad: half side) of the screen's height, for 4:3 to 20:9
    for (float aspect : new float[] {4f / 3, 16f / 9, 20f / 9}) {
      for (CoreTouch.Control c : CoreTouch.LAYOUT) {
        if (c.kind == CoreTouch.LOOK) continue;
        float half = Math.min(c.width * aspect, c.height) / 2;
        check(c.x * aspect - half >= 0 && c.x * aspect + half <= aspect);
        check(c.y - half >= 0 && c.y + half <= 1);
      }
    }
    // the look zone is the right half; the right-hand buttons lie in it, the left-hand ones not
    CoreTouch.Control look = CoreTouch.LAYOUT[0];
    check(look.kind == CoreTouch.LOOK && look.x - look.width / 2 == 0.5f && look.x + look.width / 2 == 1f
        && look.height == 1f);
  }

  static boolean overlap(CoreTouch.Control a, CoreTouch.Control b) {
    float k = (float) Math.sqrt(1.15);
    if (a.kind == CoreTouch.DPAD) {
      CoreTouch.Control s = a;
      a = b;
      b = s;
    }
    if (b.kind == CoreTouch.DPAD) {
      if (a.kind == CoreTouch.DPAD) return true;  // not in the layout
      // the circle of `a` against the rectangle of `b`
      float nx = Math.max(b.x - b.width / 2, Math.min(a.x, b.x + b.width / 2));
      float ny = Math.max(b.y - b.height / 2, Math.min(a.y, b.y + b.height / 2));
      return Math.hypot(a.x - nx, a.y - ny) < a.width / 2 * k;
    }
    return Math.hypot(a.x - b.x, a.y - b.y) < (a.width + b.width) / 2 * k;
  }
}
