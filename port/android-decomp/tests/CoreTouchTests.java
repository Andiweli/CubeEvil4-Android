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

    // the C stick as four editor buttons (not in the default layout): full tilt while held; both
    // directions of an axis cancel; with the look zone the one that tilts further counts
    check(CoreTouch.label(CoreTouch.C_UP).length() <= 5 && CoreTouch.label(CoreTouch.C_RIGHT).startsWith("C"));
    t.action(CoreTouch.C_RIGHT, true);
    check(in.pad()[0] == 0 && in.pad()[3] == 92 && in.pad()[4] == 0);
    t.action(CoreTouch.C_UP, true);
    check(in.pad()[3] == 92 && in.pad()[4] == 92 && host.calls.isEmpty());
    t.action(CoreTouch.C_LEFT, true);  // left and right cancel
    check(in.pad()[3] == 0 && in.pad()[4] == 92);
    t.action(CoreTouch.C_LEFT, false);
    t.action(CoreTouch.C_UP, false);
    t.action(CoreTouch.C_RIGHT, false);
    check(in.pad()[3] == 0 && in.pad()[4] == 0);
    t.action(CoreTouch.C_DOWN, true);
    check(in.pad()[4] == -92);
    t.reset();  // everything let go
    check(in.pad()[3] == 0 && in.pad()[4] == 0);
    t.action(CoreTouch.C_RIGHT, true);
    t.beginEvent();
    t.fingerDown(5, true);
    t.look(CoreTouch.LOOK_RANGE / 2, 0);  // the look zone tilts less than the button: the button stays
    check(in.pad()[3] == 92);
    t.look(0, 0);
    t.action(CoreTouch.C_RIGHT, false);
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
    t.fingerUp(9);  // a finger that did not look
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

    layout();
    System.out.println("CoreTouchTests: " + checks + " checks passed");
  }

  static boolean validId(String id) {
    if (id.isEmpty()) return false;
    for (char c : id.toCharArray()) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
  }

  /** The default layout with RetroTouch's rules and geometry (RetroTouchView.contains, drawButton). */
  static void layout() {
    check(CoreTouch.ACTIONS.length == CoreTouch.LABELS.length && CoreTouch.ACTIONS.length == CoreTouch.COLOURS.length);
    // the GameCube's colours: A green, B red, Z purple, every other button grey
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String a = CoreTouch.ACTIONS[i];
      int want = a.equals(CoreTouch.A) ? 0xFF109B61 : a.equals(CoreTouch.B) ? 0xFFF6392C
          : a.equals(CoreTouch.Z) ? 0xFF6B5CB1 : 0xFFDDDDDD;
      check(CoreTouch.COLOURS[i] == want);
    }
    Set<String> registered = new HashSet<>();
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String a = CoreTouch.ACTIONS[i];
      check(validId(a) && registered.add(a));
      // short labels: RetroTouch shrinks the text to the button, five letters stay readable
      check(!CoreTouch.LABELS[i].isEmpty() && CoreTouch.LABELS[i].length() <= 5);
      check(CoreTouch.label(a).equals(CoreTouch.LABELS[i]));
      boolean noButton = a.equals(CoreTouch.MENU) || a.equals(CoreTouch.R3) || a.startsWith("cstick_");  // app / C stick actions
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
    placed.remove(CoreTouch.C_UP);
    placed.remove(CoreTouch.C_DOWN);
    placed.remove(CoreTouch.C_LEFT);
    placed.remove(CoreTouch.C_RIGHT);
    check(used.equals(placed) && buttons + 5 <= 16 && sticks == 1 && looks == 1 && pads == 1);
    // one move stick: RetroTouch's onMove does not say which stick moved

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
