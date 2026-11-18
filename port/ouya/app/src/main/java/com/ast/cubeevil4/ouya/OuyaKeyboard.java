package com.ast.cubeevil4.ouya;

import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.util.HashSet;
import java.util.Set;

/**
 * Playing with a keyboard and a mouse (1.0.0, wish of the project owner, 17.11.2026): the main app's default keys
 * (its ActionMap.defaults(KIND_KEYBOARD), without its "Button Mapping"), for the OUYA and for old phones with a
 * keyboard. WASD move (and aim, as the GameCube's stick does), E action, left or right Shift run, the right mouse
 * button aims, right + left fire, right + R reload, C the knife, C + left slash, Q Ashley, Tab the case, M the map,
 * Esc the game's options, F1 the app's menu; I K J L the C stick (the camera, the scope's zoom); the mouse wheel zooms
 * the scope and the binoculars (the core, re4ipc_zoom). Fixed at all times as in the main app: the arrows the D-pad,
 * Enter confirms (A), Backspace goes back (B); the arrows come as D-pad keys and go the controller's way
 * (OuyaActivity.buttonOf).
 *
 * The mouse pointer does not aim: Android lets an app hold the pointer only from Android 8 on (the main app's
 * pointer capture); on Android 4.1 it stops at the screen's edge. Its buttons and its wheel count.
 *
 * A combination (fire, reload, slash) presses the button of the action it holds first (aim R, knife L) while its
 * first key is held and its own (A, B) while the second is held too; the second alone (the left button, R) does
 * nothing. Moving tilts the stick fully. Everything here runs on the UI thread.
 */
final class OuyaKeyboard {
    // the mouse's buttons as keys (the main app's ActionMap.MOUSE_*: above Android's key codes)
    static final int MOUSE_LEFT = 0x10001, MOUSE_RIGHT = 0x10002, MOUSE_MIDDLE = 0x10003;
    // Android's key codes (KeyEvent; the constants of API 11 written out, the app is built against API 16)
    static final int KEY_A = 29, KEY_C = 31, KEY_D = 32, KEY_E = 33, KEY_I = 37, KEY_J = 38, KEY_K = 39, KEY_L = 40,
            KEY_M = 41, KEY_Q = 45, KEY_R = 46, KEY_S = 47, KEY_W = 51, KEY_SHIFT_LEFT = 59, KEY_SHIFT_RIGHT = 60,
            KEY_TAB = 61, KEY_ENTER = 66, KEY_DEL = 67, KEY_ESCAPE = 111, KEY_F1 = 131, KEY_NUMPAD_ENTER = 160;

    private final Set<Integer> held = new HashSet<Integer>();
    private int mouseButtons;  // MotionEvent.getButtonState() of the last mouse event
    private boolean used;      // a key or button of it was used since the start (for the log)
    private float wheelRest;   // a fine wheel's part of a notch

    /** A key of the keyboard this class takes (not the controller's, not the arrows: those go the controller's way). */
    static boolean takes(int keyCode) {
        switch (keyCode) {
        case KEY_W: case KEY_A: case KEY_S: case KEY_D: case KEY_E: case KEY_SHIFT_LEFT: case KEY_SHIFT_RIGHT:
        case KEY_R: case KEY_C: case KEY_Q: case KEY_TAB: case KEY_M: case KEY_ESCAPE: case KEY_F1:
        case KEY_I: case KEY_J: case KEY_K: case KEY_L: case KEY_ENTER: case KEY_NUMPAD_ENTER: case KEY_DEL:
            return true;
        default:
            return false;
        }
    }

    /** F1: the app's menu (OuyaActivity opens it). */
    static boolean menuKey(int keyCode) {
        return keyCode == KEY_F1;
    }

    /** A key went down or up; true when this changes what the game gets. */
    boolean key(int keyCode, boolean down) {
        used = true;
        return down ? held.add(keyCode) : held.remove(keyCode);
    }

    /** An event of a mouse (Android's SOURCE_MOUSE). */
    static boolean fromMouse(MotionEvent e) {
        return (e.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE;
    }

    /** A key synthesised from a mouse button (some Android versions turn the right button into BACK). */
    static boolean fromMouse(KeyEvent e) {
        return (e.getSource() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE;
    }

    /**
     * The mouse's buttons (MotionEvent.getButtonState: primary 1, secondary 2, tertiary 4) as keys; true when they
     * changed. Android 4.1 gives them with every mouse event (pressed, moved, released).
     */
    boolean mouse(int buttonState) {
        int now = buttonState & 7;
        if (now == mouseButtons)
            return false;
        mouseButtons = now;
        used = true;
        set(MOUSE_LEFT, (now & 1) != 0);
        set(MOUSE_RIGHT, (now & 2) != 0);
        set(MOUSE_MIDDLE, (now & 4) != 0);
        return true;
    }

    /** The right button as a key (a BACK made from it, fromMouse(KeyEvent)). */
    boolean rightButton(boolean down) {
        used = true;
        return set(MOUSE_RIGHT, down);
    }

    private boolean set(int key, boolean down) {
        return down ? held.add(key) : held.remove(key);
    }

    /**
     * A turn of the mouse wheel (AXIS_VSCROLL, up positive): the whole notches it completes, for the core's zoom. A
     * fine wheel's parts of a notch add up; turned the other way, the part left goes (the main app's CoreInput.wheel).
     */
    int wheel(float v) {
        if (wheelRest * v < 0)
            wheelRest = 0;
        wheelRest += v;
        int notches = (int) (wheelRest + (wheelRest > 0 ? 1e-3f : -1e-3f));
        wheelRest -= notches;
        if (notches != 0)
            used = true;
        return notches;
    }

    /** Nothing held (the menu opened, the app paused): no key stays down in the game. */
    void reset() {
        held.clear();
        mouseButtons = 0;
        wheelRest = 0;
    }

    boolean used() { return used; }

    private boolean is(int key) { return held.contains(key); }

    /** The GameCube buttons held by the keyboard and the mouse now. */
    int buttons() {
        int b = 0;
        boolean aim = is(MOUSE_RIGHT), knife = is(KEY_C);
        if (aim)
            b |= OuyaActivity.GC_R;
        if (knife)
            b |= OuyaActivity.GC_L;
        // fire (right + left), slash (C + left), reload (right + R)
        if ((aim || knife) && is(MOUSE_LEFT))
            b |= OuyaActivity.GC_A;
        if (aim && is(KEY_R))
            b |= OuyaActivity.GC_B;
        if (is(KEY_E) || is(KEY_ENTER) || is(KEY_NUMPAD_ENTER))
            b |= OuyaActivity.GC_A;
        if (is(KEY_SHIFT_LEFT) || is(KEY_SHIFT_RIGHT) || is(KEY_DEL))
            b |= OuyaActivity.GC_B;
        if (is(KEY_Q))
            b |= OuyaActivity.GC_X;
        if (is(KEY_TAB))
            b |= OuyaActivity.GC_Y;
        if (is(KEY_M))
            b |= OuyaActivity.GC_Z;
        if (is(KEY_ESCAPE))
            b |= OuyaActivity.GC_START;
        return b;
    }

    /** The control stick from W A S D: -1..1, y down (as Android's axes). */
    float stickX() { return axis(KEY_A, KEY_D); }

    float stickY() { return axis(KEY_W, KEY_S); }

    /** The C stick from I K J L: -1..1, y down; I up zooms the scope in, K out. */
    float cX() { return axis(KEY_J, KEY_L); }

    float cY() { return axis(KEY_I, KEY_K); }

    private float axis(int minus, int plus) {
        return (is(plus) ? 1f : 0f) - (is(minus) ? 1f : 0f);
    }
}
