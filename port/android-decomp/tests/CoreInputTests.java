import com.ast.re4decomp.CoreInput;

/**
 * The core mode's controller (CoreInput): the agreed layout, raw values that
 * the game's own PADClamp (src/lib/Padclamp.c, copied below for the check)
 * turns into the GameCube's ranges, several input devices, release on clear.
 */
public final class CoreInputTests {
  static int checks;

  static void check(boolean value) {
    checks++;
    if (!value)
      throw new AssertionError("check " + checks);
  }

  // android.view.KeyEvent key codes
  static final int UP = 19, DOWN = 20, LEFT = 21, RIGHT = 22, A = 96, B = 97, X = 99, Y = 100,
      L1 = 102, R1 = 103, L2 = 104, R2 = 105, L3 = 106, R3 = 107, START = 108, SELECT = 109;

  // src/lib/Padclamp.c ClampStick and ClampTrigger with the SDK's ClampRegion
  static int[] clampStick(int x, int y, int max, int xy, int min) {
    int signX = x >= 0 ? 1 : -1, signY = y >= 0 ? 1 : -1;
    x = Math.abs(x);
    y = Math.abs(y);
    x = x <= min ? 0 : x - min;
    y = y <= min ? 0 : y - min;
    if (x == 0 && y == 0) return new int[] {0, 0};
    int d = xy * y <= xy * x ? xy * x + (max - xy) * y : xy * y + (max - xy) * x;
    if (xy * max < d) {
      x = (byte) (xy * max * x / d);
      y = (byte) (xy * max * y / d);
    }
    return new int[] {signX * x, signY * y};
  }

  static int[] mainStick(int[] pad) { return clampStick(pad[1], pad[2], 72, 40, 15); }
  static int[] cStick(int[] pad) { return clampStick(pad[3], pad[4], 59, 31, 15); }

  static int clampTrigger(int t) { return t <= 30 ? 0 : Math.min(t, 180) - 30; }

  static int[] stick(float x, float y) {
    CoreInput in = new CoreInput();
    in.motion(1, x, y, 0, 0, 0, 0, 0, 0);
    return in.pad();
  }

  public static void main(String[] args) {
    CoreInput in = new CoreInput();
    // face buttons by their labels, D-pad, START, R1 = Z; L1 and L3 have no game function
    int[][] keys = {{A, CoreInput.GC_A}, {B, CoreInput.GC_B}, {X, CoreInput.GC_X},
        {Y, CoreInput.GC_Y}, {R1, CoreInput.GC_Z}, {START, CoreInput.GC_START},
        {UP, CoreInput.GC_UP}, {DOWN, CoreInput.GC_DOWN}, {LEFT, CoreInput.GC_LEFT},
        {RIGHT, CoreInput.GC_RIGHT}};
    for (int[] k : keys) {
      check(in.key(1, k[0], true));
      check(in.pad()[0] == k[1]);
      check(in.key(1, k[0], false));
      check(in.pad()[0] == 0);
    }
    check(!in.key(1, L1, true) && CoreInput.unused(L1) && CoreInput.unused(L3));
    check(in.pad()[0] == 0);
    check(CoreInput.appFunction(SELECT) == CoreInput.APP_MENU);
    check(CoreInput.appFunction(R3) == CoreInput.APP_GX_DUMP);
    check(CoreInput.appFunction(A) == CoreInput.APP_NONE && !in.key(1, SELECT, true));
    // the switch for controllers that send the other layout's codes
    in.setSwapFace(true);
    check(in.gcButton(A) == CoreInput.GC_B && in.gcButton(B) == CoreInput.GC_A
        && in.gcButton(X) == CoreInput.GC_Y && in.gcButton(Y) == CoreInput.GC_X);
    in.key(1, A, true);
    check(in.pad()[0] == CoreInput.GC_B);
    in.setSwapFace(false);  // releases everything
    check(in.pad()[0] == 0 && in.gcButton(A) == CoreInput.GC_A);

    // the alternate controls: L1 = L at its end stop (knife), L2 = R (aim,
    // analog), R1 = Y (inventory), R2 = A past half way (fire), Y = Z (map);
    // A, B, X (Ashley), START, D-pad as before
    in.key(1, A, true);
    in.setAlternate(true);  // releases everything
    check(in.alternate() && in.pad()[0] == 0);
    int[][] alt = {{A, CoreInput.GC_A}, {B, CoreInput.GC_B}, {X, CoreInput.GC_X},
        {Y, CoreInput.GC_Z}, {R1, CoreInput.GC_Y}, {START, CoreInput.GC_START},
        {UP, CoreInput.GC_UP}, {LEFT, CoreInput.GC_LEFT}};
    for (int[] k : alt) {
      check(in.key(1, k[0], true) && in.gcButton(k[0]) == k[1]);
      check(in.pad()[0] == k[1]);
      check(in.key(1, k[0], false));
      check(in.pad()[0] == 0);
    }
    check(in.key(1, L1, true) && in.gcButton(L1) == CoreInput.GC_L);
    int[] a = in.pad();
    check(a[0] == CoreInput.GC_L && a[5] == 255 && a[6] == 0);
    check(in.key(1, L1, false) && in.pad()[0] == 0 && in.pad()[5] == 0);
    // L2: the R trigger, analog (a light press aims, the click at the end stop)
    in.motion(1, 0, 0, 0, 0, 0.4f, 0, 0, 0);
    a = in.pad();
    check(a[0] == 0 && a[5] == 0 && clampTrigger(a[6]) > 0 && in.gcButton(L2) == CoreInput.GC_R);
    in.motion(1, 0, 0, 0, 0, 1f, 0, 0, 0);
    check(in.pad()[0] == CoreInput.GC_R && in.pad()[6] == 255);
    // R2: A from half way, no trigger value
    in.motion(1, 0, 0, 0, 0, 0, 0.4f, 0, 0);
    check(in.pad()[0] == 0 && in.pad()[6] == 0 && in.pad()[5] == 0);
    in.motion(1, 0, 0, 0, 0, 0, 0.6f, 0, 0);
    check(in.pad()[0] == CoreInput.GC_A && in.pad()[5] == 0 && in.gcButton(R2) == CoreInput.GC_A);
    in.motion(1, 0, 0, 0, 0, 0, 0.45f, 0, 0);  // resting near half way: still the same shot, no new press
    check(in.pad()[0] == CoreInput.GC_A);
    in.motion(1, 0, 0, 0, 0, 0, 0.3f, 0, 0);
    check(in.pad()[0] == 0);
    in.motion(1, 0, 0, 0, 0, 0, 0.45f, 0, 0);
    check(in.pad()[0] == 0);
    in.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);
    // L2/R2 as keys
    in.key(1, L2, true);
    in.key(1, R2, true);
    a = in.pad();
    check(a[0] == (CoreInput.GC_R | CoreInput.GC_A) && a[6] == 255 && a[5] == 0);
    in.key(1, L2, false);
    in.key(1, R2, false);
    check(in.pad()[0] == 0);
    // aim and fire together, the knife: L2 + R2, L1 + R2
    in.motion(1, 0, 0, 0, 0, 1f, 1f, 0, 0);
    check(in.pad()[0] == (CoreInput.GC_R | CoreInput.GC_A));
    in.motion(1, 0, 0, 0, 0, 0, 1f, 0, 0);
    in.key(1, L1, true);
    check(in.pad()[0] == (CoreInput.GC_L | CoreInput.GC_A) && in.pad()[5] == 255);
    in.key(1, L1, false);
    in.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);
    // aiming with the right stick: while L2 (or L1) holds a weapon ready the right stick is the
    // control stick, the left one rests, the C stick stays at rest; a light L2 the game does not
    // take as pressed (30 of 255) changes nothing
    in.motion(1, 0, -1, 0.5f, 0, 0, 0, 0, 0);  // running forward, looking right
    a = in.pad();
    check(a[2] == 101 && a[1] == 0 && a[3] == Math.round(0.5f * 92) && a[4] == 0);
    in.motion(1, 0, -1, 0.5f, 0, 0.1f, 0, 0, 0);  // L2 light: 26 of 255
    a = in.pad();
    check(a[6] == 26 && a[2] == 101 && a[3] == Math.round(0.5f * 92));
    in.motion(1, 0, -1, 0.5f, 0, 0.4f, 0, 0, 0);  // L2 held: the right stick aims
    a = in.pad();
    check(a[1] == Math.round(0.5f * 101) && a[2] == 0 && a[3] == 0 && a[4] == 0);
    in.motion(1, 0.3f, -1, 0, 0.8f, 1f, 0, 0, 0);  // aiming down, the left stick ignored
    a = in.pad();
    check(a[1] == 0 && a[2] == Math.round(-0.8f * 101) && a[3] == 0 && a[0] == CoreInput.GC_R);
    in.motion(1, 0.3f, -1, 0, 0.8f, 0, 0, 0, 0);
    in.key(2, L2, true);  // L2 as a key of another device (the built-in controller)
    a = in.pad();
    check(a[2] == Math.round(-0.8f * 101) && a[1] == 0 && a[3] == 0);
    in.key(2, L2, false);
    in.key(2, L1, true);  // the knife: the right stick aims as well
    a = in.pad();
    check(a[2] == Math.round(-0.8f * 101) && a[1] == 0 && a[0] == CoreInput.GC_L);
    in.key(2, L1, false);
    a = in.pad();  // nothing held: the sticks as they are
    check(a[1] == Math.round(0.3f * 101) && a[2] == 101 && a[4] == Math.round(-0.8f * 92));
    // the touch controls' sticks stay as they are while aiming
    in.motion(1, 0, 0, 0, 0, 1f, 0, 0, 0);
    in.touchStick(false, 0, -1);
    in.touchStick(true, 1, 0);
    a = in.pad();
    check(a[2] == 101 && a[3] == 92);
    in.touchClear();
    in.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);
    // with the swap: the labels first (the button labelled Y sends X's code)
    in.setSwapFace(true);
    check(in.gcButton(X) == CoreInput.GC_Z && in.gcButton(Y) == CoreInput.GC_X
        && in.gcButton(A) == CoreInput.GC_B && in.gcButton(B) == CoreInput.GC_A);
    in.setSwapFace(false);
    // the touch controls keep their GameCube buttons (L and R the triggers)
    in.touchButton(CoreInput.GC_L | CoreInput.GC_R, true);
    a = in.pad();
    check(a[0] == (CoreInput.GC_L | CoreInput.GC_R) && a[5] == 255 && a[6] == 255);
    in.touchClear();
    in.setAlternate(false);
    check(!in.alternate() && in.pad()[0] == 0 && in.gcButton(Y) == CoreInput.GC_Y
        && in.gcButton(R1) == CoreInput.GC_Z && !in.key(1, L1, true));
    // the agreed layout keeps the left stick while L2 (the knife there) is held
    in.motion(1, 0, -1, 0.5f, 0, 1f, 0, 0, 0);
    a = in.pad();
    check(a[2] == 101 && a[3] == Math.round(0.5f * 92));
    in.clear();

    // sticks: full deflection reaches the GameCube's range after PADClamp, up is positive
    int[] p = stick(1, 0);
    check(p[1] == 101 && p[2] == 0 && mainStick(p)[0] == 72);
    p = stick(0, -1);  // Android: up is negative
    check(p[2] == 101 && mainStick(p)[1] == 72);
    p = stick((float) -Math.sqrt(0.5), (float) Math.sqrt(0.5));  // down left: the octagon
    check(mainStick(p)[0] == -40 && mainStick(p)[1] == -40);
    // a stick at rest or barely moved stays in the dead zone (15 of 101)
    p = stick(0.12f, -0.14f);
    check(mainStick(p)[0] == 0 && mainStick(p)[1] == 0);
    // halfway past the dead zone: linear
    p = stick(0.5f, 0);
    check(mainStick(p)[0] == 51 - 15);
    // C stick: up to 59, as cam_qfps.cpp expects (it divides by 59)
    in.motion(1, 0, 0, -1, 0, 0, 0, 0, 0);
    p = in.pad();
    check(p[3] == -92 && cStick(p)[0] == -59);
    in.motion(1, 0, 0, 0, 1, 0, 0, 0, 0);
    p = in.pad();
    check(p[4] == -92 && cStick(p)[1] == -59);
    in.motion(1, 0, 0, 0.1f, 0.1f, 0, 0, 0, 0);
    check(cStick(in.pad())[0] == 0 && cStick(in.pad())[1] == 0);

    // triggers: analog 0..255; a light press counts once past 30 (12 %), the
    // digital L/R only at the end stop
    in.motion(1, 0, 0, 0, 0, 0.05f, 0.11f, 0, 0);
    p = in.pad();
    check(clampTrigger(p[5]) == 0 && clampTrigger(p[6]) == 0 && p[0] == 0);
    in.motion(1, 0, 0, 0, 0, 0.2f, 0.5f, 0, 0);
    p = in.pad();
    check(clampTrigger(p[5]) > 0 && clampTrigger(p[6]) > 0 && p[0] == 0);
    in.motion(1, 0, 0, 0, 0, 1f, 0.9f, 0, 0);
    p = in.pad();
    check(p[5] == 255 && clampTrigger(p[5]) == 150 && p[0] == (CoreInput.GC_L | CoreInput.GC_R));
    in.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);
    check(in.pad()[0] == 0 && in.pad()[5] == 0);
    // a trigger held near the click: R stays pressed until below 70 % (jitter is no new press, which
    // the core would show as a frame without R, 26.10.2026), and is pressed again only at 85 %
    in.motion(1, 0, 0, 0, 0, 0, 0.86f, 0, 0);
    check(in.pad()[0] == CoreInput.GC_R);
    in.motion(1, 0, 0, 0, 0, 0, 0.80f, 0, 0);
    check(in.pad()[0] == CoreInput.GC_R);
    in.motion(1, 0, 0, 0, 0, 0, 0.86f, 0, 0);
    check(in.pad()[0] == CoreInput.GC_R);
    in.motion(1, 0, 0, 0, 0, 0, 0.65f, 0, 0);
    check(in.pad()[0] == 0 && in.pad()[6] == Math.round(0.65f * 255));  // the analog value stays
    in.motion(1, 0, 0, 0, 0, 0, 0.80f, 0, 0);
    check(in.pad()[0] == 0);
    in.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);
    // L2/R2 as keys (digital triggers): fully pressed
    in.key(1, R2, true);
    p = in.pad();
    check(p[6] == 255 && p[0] == CoreInput.GC_R && p[5] == 0);
    in.key(1, R2, false);
    in.key(1, L2, true);
    check(in.pad()[5] == 255 && in.pad()[0] == CoreInput.GC_L);
    in.key(1, L2, false);
    check(in.pad()[0] == 0 && in.pad()[5] == 0);

    // the hat axes are the D-pad
    in.motion(1, 0, 0, 0, 0, 0, 0, -1, 1);
    check(in.pad()[0] == (CoreInput.GC_LEFT | CoreInput.GC_DOWN));
    in.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);

    // two devices (keys from one, axes from another): both count, an idle
    // device does not cancel the other's stick
    in.key(7, A, true);
    in.motion(8, 0.9f, 0, 0, 0, 0, 0.6f, 0, 0);
    in.motion(9, 0, 0, 0, 0, 0, 0, 0, 0);
    p = in.pad();
    check(p[0] == CoreInput.GC_A && p[1] == Math.round(0.9f * 101) && p[6] == Math.round(0.6f * 255));
    in.remove(8);
    p = in.pad();
    check(p[0] == CoreInput.GC_A && p[1] == 0 && p[6] == 0);
    in.clear();
    check(in.pad()[0] == 0);
    // out-of-range and broken values
    in.motion(1, 3f, Float.NaN, -2f, Float.POSITIVE_INFINITY, 2f, -1f, 0, 0);
    p = in.pad();
    check(p[1] == 101 && p[2] == 0 && p[3] == -92 && p[4] == 0 && p[5] == 255 && p[6] == 0);
    check(CoreInput.names(CoreInput.GC_A | CoreInput.GC_B | CoreInput.GC_START).equals("A+B+START")
        && CoreInput.names(0).equals("-"));

    // latching (the menu's comfort controls): a press of R2 holds R fully until the next press, an
    // analog trigger only past half way (and let go below a quarter), B the same; clear lets go
    {
      CoreInput l = new CoreInput();
      l.setLatch(true, true);
      l.key(2, R2, true);
      int[] q = l.pad();
      check((q[0] & CoreInput.GC_R) != 0 && q[6] == 255);
      l.key(2, R2, false);
      q = l.pad();
      check((q[0] & CoreInput.GC_R) != 0 && q[6] == 255);  // still held
      l.key(2, R2, true);
      l.pad();
      l.key(2, R2, false);
      q = l.pad();
      check((q[0] & CoreInput.GC_R) == 0 && q[6] == 0);  // the second press lets go
      l.motion(2, 0, 0, 0, 0, 0.4f, 0, 0, 0);  // L2 not past half way: no press
      check(l.pad()[5] == 0);
      l.motion(2, 0, 0, 0, 0, 0.6f, 0, 0, 0);
      q = l.pad();
      check((q[0] & CoreInput.GC_L) != 0 && q[5] == 255);
      l.motion(2, 0, 0, 0, 0, 0.3f, 0, 0, 0);  // above a quarter: still the same press
      l.pad();
      l.motion(2, 0, 0, 0, 0, 0.6f, 0, 0, 0);
      check(l.pad()[5] == 255);  // no new press: stays latched
      l.motion(2, 0, 0, 0, 0, 0.1f, 0, 0, 0);
      l.pad();
      l.motion(2, 0, 0, 0, 0, 0.6f, 0, 0, 0);
      check(l.pad()[5] == 0);  // let go and pressed again: released
      l.motion(2, 0, 0, 0, 0, 0, 0, 0, 0);
      l.key(2, B, true);
      check((l.pad()[0] & CoreInput.GC_B) != 0);
      l.key(2, B, false);
      check((l.pad()[0] & CoreInput.GC_B) != 0);  // Leon keeps running
      l.key(2, B, true);
      l.pad();
      l.key(2, B, false);
      check((l.pad()[0] & CoreInput.GC_B) == 0);
      // the touch controls' own buttons are not latched here (CoreTouch latches them)
      l.touchButton(CoreInput.GC_R, true);
      check(l.pad()[6] == 255);
      l.touchButton(CoreInput.GC_R, false);
      check(l.pad()[6] == 0);
      // clear (focus lost): the latched buttons go too
      l.key(2, R2, true);
      l.pad();
      l.clear();
      check(l.pad()[6] == 0 && l.pad()[0] == 0);
      // switched off: as before
      l.setLatch(false, false);
      l.key(2, R2, true);
      check(l.pad()[6] == 255);
      l.key(2, R2, false);
      check(l.pad()[6] == 0);
      // the alternate controls: L2 latches R (the weapon ready) and the right stick aims meanwhile
      l.setAlternate(true);
      l.setLatch(true, false);
      l.key(2, L2, true);
      l.pad();
      l.key(2, L2, false);
      l.motion(2, 0, 0, 1f, 0, 0, 0, 0, 0);
      q = l.pad();
      check((q[0] & CoreInput.GC_R) != 0 && q[6] == 255 && q[1] > 0 && q[3] == 0);
    }
    {
      // own bindings (the menu's controller configuration, 27.10.2026)
      int[] def = CoreInput.defaultBindings();
      check(CoreInput.encode(def) == null && CoreInput.encode(null) == null && CoreInput.decode(null) == null);
      check(CoreInput.decode("1,2,3") == null && CoreInput.decode("96,96,99,100,103,104,105,108,19,20,21,22,109") == null);
      CoreInput o = new CoreInput();
      o.setBindings(def);
      check(o.bindings() == null);  // the default layout: the layout of before
      // the action (A) on B: the two swap
      int[] b = CoreInput.assign(null, CoreInput.F_A, B);
      check(b[CoreInput.F_A] == B && b[CoreInput.F_B] == A);
      check(java.util.Arrays.equals(CoreInput.decode(CoreInput.encode(b)), b));
      o.setBindings(b);
      check(o.key(1, B, true) && o.pad()[0] == CoreInput.GC_A && o.gcButton(B) == CoreInput.GC_A);
      o.key(1, B, false);
      check(o.key(1, A, true) && o.pad()[0] == CoreInput.GC_B);
      o.key(1, A, false);
      check(o.pad()[0] == 0);
      // the action on R2 (the trigger: past half way), aiming (R) takes R2's old key... R2 had R: R on A's old key
      b = CoreInput.assign(null, CoreInput.F_A, R2);
      check(b[CoreInput.F_R] == A);
      o.setBindings(b);
      o.motion(1, 0, 0, 0, 0, 0, 0.6f, 0, 0);
      check(o.pad()[0] == CoreInput.GC_A && o.pad()[6] == 0);
      o.motion(1, 0, 0, 0, 0, 0, 0.4f, 0, 0);
      check(o.pad()[0] == CoreInput.GC_A);  // held until below 0.35
      o.motion(1, 0, 0, 0, 0, 0, 0.3f, 0, 0);
      check(o.pad()[0] == 0);
      // R on the A key: the trigger at its end stop, digital R too
      o.key(1, A, true);
      int[] q = o.pad();
      check(q[6] == 255 && (q[0] & CoreInput.GC_R) != 0);
      o.key(1, A, false);
      // L stays analog on L2
      o.motion(1, 0, 0, 0, 0, 0.5f, 0, 0, 0);
      q = o.pad();
      check(q[5] == 128 && (q[0] & CoreInput.GC_L) == 0);
      o.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);
      // the hat's D-pad goes through the D-pad's functions: map (Z) on D-pad up
      o.setBindings(CoreInput.assign(null, CoreInput.F_Z, UP));
      o.motion(1, 0, 0, 0, 0, 0, 0, 0, -1f);
      check(o.pad()[0] == CoreInput.GC_Z);
      o.motion(1, 0, 0, 0, 0, 0, 0, 0, 0);
      check(o.key(1, R1, true) && o.pad()[0] == CoreInput.GC_UP);  // D-pad up took R1, the map's old key
      o.key(1, R1, false);
      // the app's menu on L1: SELECT has no function any more, R3 still the graphics capture
      o.setBindings(CoreInput.assign(null, CoreInput.F_MENU, L1));
      check(o.appFunctionOf(L1) == CoreInput.APP_MENU && o.appFunctionOf(SELECT) == CoreInput.APP_NONE);
      check(!o.key(1, L1, true) && !o.key(1, SELECT, true) && o.pad()[0] == 0);
      check(o.appFunctionOf(R3) == CoreInput.APP_GX_DUMP);
      // a game function on R3: no graphics capture there
      o.setBindings(CoreInput.assign(null, CoreInput.F_X, R3));
      check(o.appFunctionOf(R3) == CoreInput.APP_NONE && o.key(1, R3, true) && o.pad()[0] == CoreInput.GC_X);
      // the alternate controls keep their own layout
      o.setAlternate(true);
      check(o.gcButton(R1) == CoreInput.GC_Y && o.appFunctionOf(SELECT) == CoreInput.APP_MENU);
      o.setAlternate(false);
      check(o.gcButton(R3) == CoreInput.GC_X);
      // latching works through the own bindings: B (run) on Y
      o.setBindings(CoreInput.assign(null, CoreInput.F_B, Y));
      o.setLatch(false, true);
      o.key(1, Y, true);
      o.pad();
      o.key(1, Y, false);
      check(o.pad()[0] == CoreInput.GC_B);
      o.setLatch(false, false);
      o.clear();
      // the game's menus keep the default layout (the core's bit 4 of the rumble): the action on R2,
      // in a menu A is A again
      o.setBindings(CoreInput.assign(null, CoreInput.F_A, R2));
      o.setMenu(true);
      check(o.menu() && o.key(1, A, true) && o.pad()[0] == CoreInput.GC_A && o.gcButton(A) == CoreInput.GC_A);
      // held across the change: in play A is aiming (R) now, its old key; released in play: nothing stuck
      o.setMenu(false);
      int[] q2 = o.pad();
      check((q2[0] & CoreInput.GC_A) == 0 && q2[6] == 255);
      o.key(1, A, false);
      check(o.pad()[0] == 0 && o.pad()[6] == 0);
      // pressed in play, released in a menu
      o.key(1, X, true);
      o.setMenu(true);
      check(o.pad()[0] == CoreInput.GC_X);
      o.key(1, X, false);
      check(o.pad()[0] == 0);
      // the app's menu key in a game menu: SELECT as always
      o.setBindings(CoreInput.assign(null, CoreInput.F_MENU, L1));
      check(o.appFunctionOf(SELECT) == CoreInput.APP_MENU);
      o.setMenu(false);
      check(o.appFunctionOf(L1) == CoreInput.APP_MENU);
    }
    System.out.println("Core controller: layout, swap, alternate controls, own bindings, sticks and triggers through "
        + "PADClamp, devices: " + checks + " checks PASS");
  }
}
