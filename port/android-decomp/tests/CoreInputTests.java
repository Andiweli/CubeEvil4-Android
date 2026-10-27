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
      // extra keys (the menu's controller configuration, 27.10.2026): only on free keys, the layout stays
      check(CoreInput.freeKey(L1, false) && !CoreInput.freeKey(L1, true) && CoreInput.freeKey(L3, false)
          && CoreInput.freeKey(R3, true) && !CoreInput.freeKey(A, false) && !CoreInput.freeKey(R1, false)
          && !CoreInput.freeKey(L2, false) && !CoreInput.freeKey(SELECT, false) && !CoreInput.freeKey(UP, false));
      check(CoreInput.encode(null) == null && CoreInput.encode(new int[CoreInput.FUNCTIONS]) == null);
      check(CoreInput.decode("1,2") == null && CoreInput.decode("96,0,0,0,0,0,0,0,0,0,0,0,0") == null
          && CoreInput.decode("102,0,0,0,102,0,0,0,0,0,0,0,0") == null);
      int[] e = CoreInput.assign(null, CoreInput.F_Z, L1);  // the map also on L1
      check(e[CoreInput.F_Z] == L1 && java.util.Arrays.equals(CoreInput.decode(CoreInput.encode(e)), e));
      CoreInput x = new CoreInput();
      x.setExtraKeys(e);
      check(x.key(1, L1, true) && x.pad()[0] == CoreInput.GC_Z && x.gcButton(L1) == CoreInput.GC_Z);
      x.key(1, L1, false);
      check(x.pad()[0] == 0);
      // R1 is still the map, A still A: the layout stays
      check(x.key(1, R1, true) && x.pad()[0] == CoreInput.GC_Z);
      x.key(1, R1, false);
      check(x.key(1, A, true) && x.pad()[0] == CoreInput.GC_A);
      x.key(1, A, false);
      // L1 to another function: the map loses it; the same key again: no extra key
      e = CoreInput.assign(e, CoreInput.F_B, L1);
      check(e[CoreInput.F_B] == L1 && e[CoreInput.F_Z] == 0);
      check(CoreInput.assign(e, CoreInput.F_B, L1)[CoreInput.F_B] == 0);
      // B (run) on L1, latching works through it
      x.setExtraKeys(e);
      check(x.key(1, L1, true) && x.pad()[0] == CoreInput.GC_B);
      x.key(1, L1, false);
      x.setLatch(false, true);
      x.key(1, L1, true);
      x.pad();
      x.key(1, L1, false);
      check(x.pad()[0] == CoreInput.GC_B);
      x.setLatch(false, false);
      x.clear();
      // aiming (R) on L3: the trigger fully, digital R too
      x.setExtraKeys(CoreInput.assign(null, CoreInput.F_R, L3));
      x.key(1, L3, true);
      int[] q = x.pad();
      check(q[6] == 255 && (q[0] & CoreInput.GC_R) != 0);
      x.key(1, L3, false);
      check(x.pad()[6] == 0);
      // the app's menu on R3: no graphics capture there; SELECT still the menu
      x.setExtraKeys(CoreInput.assign(null, CoreInput.F_MENU, R3));
      check(x.appFunctionOf(R3) == CoreInput.APP_MENU && x.appFunctionOf(SELECT) == CoreInput.APP_MENU);
      check(!x.key(1, R3, true));
      // a game function on R3: no capture, the function
      x.setExtraKeys(CoreInput.assign(null, CoreInput.F_X, R3));
      check(x.appFunctionOf(R3) == CoreInput.APP_NONE && x.key(1, R3, true) && x.pad()[0] == CoreInput.GC_X);
      x.key(1, R3, false);
      // with the alternate controls L1 is theirs (the knife): an extra key there does nothing
      x.setExtraKeys(CoreInput.assign(null, CoreInput.F_Z, L1));
      x.setAlternate(true);
      x.key(1, L1, true);
      q = x.pad();
      check((q[0] & CoreInput.GC_Z) == 0 && q[5] == 255);
    }
    System.out.println("Core controller: layout, swap, alternate controls, extra keys, sticks and triggers through "
        + "PADClamp, devices: " + checks + " checks PASS");
  }
}
