package com.ast.cubeevil4.ouya;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Environment;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

import java.io.File;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * The OUYA app (stage O3 of port/docs/ouya-machbarkeit.md): the game core in a process of its own, its
 * picture through the GLES 2 renderer in the light version's settings, the OUYA controller as the
 * GameCube's. Disc 1 lies in /sdcard/RE4-OUYA/disc0 (port/scripts/ouya-app.ps1 copies it there), the
 * memory card goes to /sdcard/RE4-OUYA/memcard, the core's log to /sdcard/RE4-OUYA/core.log.
 *
 * Controller (as the main app's CoreInput, its default layout): O = A, A = B, U = X, Y = Y, R1 = Z,
 * L2 / R2 = L / R (analog), the left stick the main stick, the right stick the C stick, the D-pad the
 * D-pad. The OUYA has no START button: the system button's short press (MENU) and L3 are START.
 */
public class OuyaActivity extends Activity {
    static {
        System.loadLibrary("re4ouya");
    }

    static native String nativeStart(String dataDir);
    static native void nativeSurfaceCreated();
    static native void nativeSurfaceChanged(int width, int height);
    static native void nativeFrame();
    static native void nativePad(int buttons, int stickX, int stickY, int substickX, int substickY, int triggerL,
            int triggerR);
    static native void nativePause(boolean on);

    // GameCube buttons (PADStatus)
    static final int GC_LEFT = 0x0001, GC_RIGHT = 0x0002, GC_DOWN = 0x0004, GC_UP = 0x0008, GC_Z = 0x0010,
            GC_R = 0x0020, GC_L = 0x0040, GC_A = 0x0100, GC_B = 0x0200, GC_X = 0x0400, GC_Y = 0x0800,
            GC_START = 0x1000;

    private GLSurfaceView view;
    private TextView message;
    private volatile boolean running;
    private int keys;                       // GameCube buttons held by keys
    private float lx, ly, rx, ry, lt, rt;   // the axes
    private int hat;                        // D-pad from the hat axes (other controllers)

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        view = new GLSurfaceView(this);
        view.setEGLContextClientVersion(2);
        view.setEGLConfigChooser(8, 8, 8, 0, 0, 0);
        view.setPreserveEGLContextOnPause(true);
        view.setRenderer(new GLSurfaceView.Renderer() {
            @Override
            public void onSurfaceCreated(GL10 gl, EGLConfig config) {
                nativeSurfaceCreated();
            }

            @Override
            public void onSurfaceChanged(GL10 gl, int width, int height) {
                nativeSurfaceChanged(width, height);
            }

            @Override
            public void onDrawFrame(GL10 gl) {
                nativeFrame();
            }
        });
        root.addView(view);
        message = new TextView(this);
        message.setTextColor(Color.WHITE);
        message.setTextSize(22);
        message.setGravity(Gravity.CENTER);
        message.setPadding(80, 80, 80, 80);
        root.addView(message, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        setContentView(root);
        view.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);

        final File data = new File(Environment.getExternalStorageDirectory(), "RE4-OUYA");
        if (!new File(data, "disc0/sys/fst.bin").isFile()) {
            message.setText("Disc 1 is missing.\n\nCopy it from the PC with port/scripts/ouya-app.ps1\n(into "
                    + data.getPath() + "/disc0).");
            return;
        }
        message.setText("Starting the game ...");
        new Thread(new Runnable() {
            @Override
            public void run() {
                final String problem = nativeStart(data.getPath());
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        if (problem.isEmpty()) {
                            running = true;
                            message.setVisibility(View.GONE);
                        } else {
                            message.setText("The game did not start:\n\n" + problem);
                        }
                    }
                });
            }
        }, "re4-start").start();
    }

    @Override
    protected void onPause() {
        super.onPause();
        view.onPause();
        nativePause(true);
    }

    @Override
    protected void onResume() {
        super.onResume();
        view.onResume();
        nativePause(false);
    }

    // --- the controller --------------------------------------------------------------------------

    private static int buttonOf(int keyCode) {
        switch (keyCode) {
        case KeyEvent.KEYCODE_BUTTON_A: return GC_A;      // O
        case KeyEvent.KEYCODE_BUTTON_B: return GC_B;      // A
        case KeyEvent.KEYCODE_BACK: return GC_B;
        case KeyEvent.KEYCODE_BUTTON_X: return GC_X;      // U
        case KeyEvent.KEYCODE_BUTTON_Y: return GC_Y;      // Y
        case KeyEvent.KEYCODE_BUTTON_R1: return GC_Z;
        case KeyEvent.KEYCODE_BUTTON_L2: return GC_L;
        case KeyEvent.KEYCODE_BUTTON_R2: return GC_R;
        case KeyEvent.KEYCODE_MENU:                      // the OUYA's system button, short
        case KeyEvent.KEYCODE_BUTTON_START:
        case KeyEvent.KEYCODE_BUTTON_THUMBL: return GC_START;
        case KeyEvent.KEYCODE_DPAD_UP: return GC_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN: return GC_DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT: return GC_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return GC_RIGHT;
        default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int bit = buttonOf(event.getKeyCode());
        if (bit == 0) {
            return super.dispatchKeyEvent(event);
        }
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            keys |= bit;
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            keys &= ~bit;
        }
        sendPad();
        return true;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) == 0 || event.getAction() != MotionEvent.ACTION_MOVE) {
            return super.dispatchGenericMotionEvent(event);
        }
        lx = event.getAxisValue(MotionEvent.AXIS_X);
        ly = event.getAxisValue(MotionEvent.AXIS_Y);
        rx = event.getAxisValue(MotionEvent.AXIS_Z);
        ry = event.getAxisValue(MotionEvent.AXIS_RZ);
        lt = Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE));
        rt = Math.max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS));
        float hx = event.getAxisValue(MotionEvent.AXIS_HAT_X), hy = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        hat = (hx < -0.5f ? GC_LEFT : 0) | (hx > 0.5f ? GC_RIGHT : 0) | (hy < -0.5f ? GC_UP : 0) | (hy > 0.5f ? GC_DOWN : 0);
        sendPad();
        return true;
    }

    private void sendPad() {
        if (!running) {
            return;
        }
        int buttons = keys | hat;
        // the analog triggers pressed through: the digital L / R as well (as on the GameCube)
        if (lt > 0.9f) {
            buttons |= GC_L;
        }
        if (rt > 0.9f) {
            buttons |= GC_R;
        }
        int triggerL = (keys & GC_L) != 0 ? 255 : Math.round(lt * 255);
        int triggerR = (keys & GC_R) != 0 ? 255 : Math.round(rt * 255);
        // Android's y grows downwards, the GameCube's upwards; the game applies its own dead zones
        nativePad(buttons, Math.round(lx * 100), Math.round(-ly * 100), Math.round(rx * 90), Math.round(-ry * 90),
                triggerL, triggerR);
    }
}
