package com.ast.re4decomp;

import java.util.HashSet;
import java.util.Set;

/**
 * The game's actions on keys (ActionMap): the defaults of the controller and the keyboard, single actions
 * and combinations (fire only with aim held), the swap when a key is taken, the move keys as the stick,
 * the preferences' text.
 */
public final class ActionMapTests {
  static int checks;

  static void check(boolean value) {
    checks++;
    if (!value) throw new AssertionError("check " + checks);
  }

  static int press(ActionMap m, int... keys) {
    Set<Integer> held = new HashSet<>();
    for (int k : keys) held.add(k);
    return m.buttons(held::contains);
  }

  public static void main(String[] args) {
    final int A = CoreInput.KEYCODE_BUTTON_A, B = CoreInput.KEYCODE_BUTTON_B, X = CoreInput.KEYCODE_BUTTON_X,
        Y = CoreInput.KEYCODE_BUTTON_Y, L1 = CoreInput.KEYCODE_BUTTON_L1, R1 = CoreInput.KEYCODE_BUTTON_R1,
        L2 = CoreInput.KEYCODE_BUTTON_L2, R2 = CoreInput.KEYCODE_BUTTON_R2;

    // the controller's default: the layout without a mapping
    ActionMap c = ActionMap.defaults(ActionMap.KIND_CONTROLLER);
    check(c.isDefault() && c.encode() == null);
    check(press(c, A) == CoreInput.GC_A && press(c, B) == CoreInput.GC_B && press(c, X) == CoreInput.GC_X);
    check(press(c, Y) == CoreInput.GC_Y && press(c, R1) == CoreInput.GC_Z && press(c, L1) == 0);
    check(press(c, R2) == CoreInput.GC_R && press(c, L2) == CoreInput.GC_L);
    check(press(c, R2, A) == (CoreInput.GC_R | CoreInput.GC_A));  // aim and fire
    check(press(c, L2, A) == (CoreInput.GC_L | CoreInput.GC_A));  // knife and slash
    check(press(c, CoreInput.KEYCODE_BUTTON_START) == CoreInput.GC_START);
    check(press(c, CoreInput.KEYCODE_BUTTON_SELECT) == 0 && c.singleOn(CoreInput.KEYCODE_BUTTON_SELECT) == ActionMap.APP_MENU);
    check(!ActionMap.available(ActionMap.KIND_CONTROLLER, ActionMap.MOVE_FORWARD));
    check(c.assign(ActionMap.MAP, new int[] {CoreInput.KEYCODE_DPAD_LEFT}) == null);  // the D-pad stays the D-pad
    check(c.assign(ActionMap.MOVE_FORWARD, new int[] {A}) == null);

    // fire on L2 + R2 (the owner's example): R2 alone no longer fires with A, A alone stays the action
    ActionMap f = c.assign(ActionMap.FIRE, new int[] {L2, R2});
    check(f != null && !f.isDefault());
    check(press(f, L2) == (CoreInput.GC_R | CoreInput.GC_L));  // L2 holds aim (fire's first) and the knife
    check(press(f, L2, R2) == (CoreInput.GC_R | CoreInput.GC_L | CoreInput.GC_A));
    check(press(f, R2) == CoreInput.GC_R);  // aim alone, no fire
    check(press(f, A) == CoreInput.GC_A);
    // a combination needs two different keys
    check(c.assign(ActionMap.FIRE, new int[] {R2}) == null && c.assign(ActionMap.FIRE, new int[] {R2, R2}) == null);
    check(c.assign(ActionMap.AIM, new int[] {R2, A}) == null);

    // a single action on a key another single action had: they swap
    ActionMap s = c.assign(ActionMap.RUN, new int[] {L1});
    check(s.keys(ActionMap.RUN)[0] == L1 && press(s, L1) == CoreInput.GC_B && press(s, B) == 0);
    s = s.assign(ActionMap.ACTION, new int[] {L1});
    check(s.keys(ActionMap.ACTION)[0] == L1 && s.keys(ActionMap.RUN)[0] == A);
    check(press(s, A) == CoreInput.GC_B && press(s, L1) == CoreInput.GC_A);
    // combinations keep their keys (fire is still R2 + A)
    check(press(s, R2, A) == (CoreInput.GC_R | CoreInput.GC_A | CoreInput.GC_B));
    // the app's menu moves like any single action
    ActionMap menu = c.assign(ActionMap.APP_MENU, new int[] {CoreInput.KEYCODE_BUTTON_THUMBR});
    check(menu.singleOn(CoreInput.KEYCODE_BUTTON_THUMBR) == ActionMap.APP_MENU
        && menu.singleOn(CoreInput.KEYCODE_BUTTON_SELECT) == -1 && menu.uses(CoreInput.KEYCODE_BUTTON_THUMBR));

    // the preferences' text
    check(ActionMap.decode(ActionMap.KIND_CONTROLLER, f.encode()).equals(f));
    check(ActionMap.decode(ActionMap.KIND_CONTROLLER, s.encode()).equals(s));
    check(ActionMap.decode(ActionMap.KIND_CONTROLLER, null).isDefault());
    check(ActionMap.decode(ActionMap.KIND_CONTROLLER, "7=105").isDefault());  // fire with one key: not taken
    check(ActionMap.decode(ActionMap.KIND_CONTROLLER, "0=51").isDefault());   // a move on the controller
    check(ActionMap.decode(ActionMap.KIND_CONTROLLER, "x").isDefault());

    // the keyboard with the mouse: WASD as the stick, E acts, Shift runs, the right button aims, right + left
    // fire, right + R reload, C knife, C + left slash
    ActionMap k = ActionMap.defaults(ActionMap.KIND_KEYBOARD);
    float[] st = k.stick(code -> code == ActionMap.KEY_W);
    check(st[0] == 0 && st[1] == -1);
    st = k.stick(code -> code == ActionMap.KEY_S || code == ActionMap.KEY_D);
    check(Math.abs(st[0] - 0.7071f) < 1e-3f && Math.abs(st[1] - 0.7071f) < 1e-3f);
    check(k.stick(code -> code == ActionMap.KEY_A || code == ActionMap.KEY_D)[0] == 0);
    check(press(k, ActionMap.KEY_E) == CoreInput.GC_A && press(k, ActionMap.KEY_SHIFT_LEFT) == CoreInput.GC_B);
    check(press(k, ActionMap.MOUSE_LEFT) == 0);  // fire's button alone: nothing
    check(press(k, ActionMap.MOUSE_RIGHT) == CoreInput.GC_R);
    check(press(k, ActionMap.MOUSE_RIGHT, ActionMap.MOUSE_LEFT) == (CoreInput.GC_R | CoreInput.GC_A));
    check(press(k, ActionMap.KEY_C, ActionMap.MOUSE_LEFT) == (CoreInput.GC_L | CoreInput.GC_A));
    check(press(k, ActionMap.MOUSE_RIGHT, ActionMap.KEY_R) == (CoreInput.GC_R | CoreInput.GC_B));
    check(press(k, ActionMap.KEY_ESCAPE) == CoreInput.GC_START && k.singleOn(ActionMap.KEY_F1) == ActionMap.APP_MENU);
    // the mouse's buttons by Android's bits; a side button takes an action like a key
    check(ActionMap.mouseButton(1) == ActionMap.MOUSE_LEFT && ActionMap.mouseButton(2) == ActionMap.MOUSE_RIGHT
        && ActionMap.mouseButton(4) == ActionMap.MOUSE_MIDDLE && ActionMap.mouseButton(16) == ActionMap.MOUSE_FORWARD
        && ActionMap.mouseButton(32) == 0);
    ActionMap side = k.assign(ActionMap.ASHLEY, new int[] {ActionMap.MOUSE_BACK});
    check(press(side, ActionMap.MOUSE_BACK) == CoreInput.GC_X && press(side, ActionMap.KEY_Q) == 0);
    check(ActionMap.decode(ActionMap.KIND_KEYBOARD, side.encode()).equals(side));
    // the fixed keys: arrows, Enter, Backspace are not mapped
    check(ActionMap.fixedKeyboardButton(CoreInput.KEYCODE_DPAD_UP) == CoreInput.GC_UP
        && ActionMap.fixedKeyboardButton(ActionMap.KEY_ENTER) == CoreInput.GC_A
        && ActionMap.fixedKeyboardButton(ActionMap.KEY_DEL) == CoreInput.GC_B);
    check(k.assign(ActionMap.ACTION, new int[] {ActionMap.KEY_ENTER}) == null);
    check(k.assign(ActionMap.FIRE, new int[] {ActionMap.MOUSE_RIGHT, CoreInput.KEYCODE_DPAD_UP}) == null);
    // moving with I J K L: W goes to the old place of forward's new key (none had I)
    ActionMap kk = k.assign(ActionMap.MOVE_FORWARD, new int[] {37});
    check(kk.stick(code -> code == 37)[1] == -1 && kk.stick(code -> code == ActionMap.KEY_W)[1] == 0);
    check(ActionMap.decode(ActionMap.KIND_KEYBOARD, kk.encode()).equals(kk));

    System.out.println("ActionMapTests: " + checks + " checks passed");
  }
}
