package com.ast.cubeevil4.ouya;

import android.app.Activity;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

import com.ast.re4decomp.disc.DiscInstaller;
import com.ast.re4decomp.disc.GameCubeDisc;

import java.io.File;
import java.util.Locale;

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
 * D-pad. The OUYA has no START button: the system button's short press (MENU) is START. L3 opens the
 * app's menu (OuyaMenu, the main app's SELECT menu for the OUYA; wish of the project owner, 07.11.2026).
 * The picture fills the screen as the main app's default: the game's 16:9 rows (56..391).
 */
public class OuyaActivity extends Activity implements OuyaMenu.Host, OuyaInstaller.Host {
    static {
        // Android before 4.3 does not look in the app's library folder for what a library needs:
        // the C++ library first
        System.loadLibrary("c++_shared");
        System.loadLibrary("re4ouya");
    }

    /** gameLanguage: the European discs' language (2 English .. 6 Italian, RE4_GAME_LANGUAGE), 0 none. */
    static native String nativeStart(String dataDir, String libraryDir, int gameLanguage);
    static native void nativeSurfaceCreated();
    static native void nativeSurfaceChanged(int width, int height);
    static native void nativeFrame();
    static native void nativePad(int buttons, int stickX, int stickY, int substickX, int substickY, int triggerL,
            int triggerR);
    static native void nativePause(boolean on);
    /** The menu's settings (OuyaMenu.Settings): before the start for the core's start, then at once. */
    static native void nativeSettings(int resolutionEighths, boolean fastTev, int viewPercent, int potato, int aim,
            boolean frameSkip, int cheats);
    /** A cheat button of the menu (runtime/cheats.cpp); the answer as OuyaMenu.Host.cheatAction. */
    static native int nativeCheatAction(int what);

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
    private int tapped;                     // buttons pressed for a moment (the OUYA button)
    private OuyaMenu menu;                  // the app's menu while it is open
    private OuyaInstaller installer;        // the disc installer while it is shown
    private FrameLayout root;
    private boolean paused;                 // the activity is in the background
    private final Handler handler = new Handler();
    private final Runnable untap = new Runnable() {
        @Override
        public void run() {
            tapped = 0;
            sendPad();
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        root = new FrameLayout(this);
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

        // the discs: installed by the app's installer (OuyaInstaller), else the developer's folder of
        // port/scripts/ouya-app.ps1 (disc 1 as /sdcard/RE4-OUYA/disc0), else the installer
        File data = OuyaInstaller.installedRoot(this);
        if (data == null) {
            File legacy = new File(Environment.getExternalStorageDirectory(), "RE4-OUYA");
            if (new File(legacy, "disc0/sys/fst.bin").isFile())
                data = legacy;
        }
        final boolean again = getSharedPreferences(OuyaInstaller.PREFS, MODE_PRIVATE).getBoolean(OuyaInstaller.AGAIN, false);
        if (data == null || again) {
            existing = data;
            message.setVisibility(View.GONE);
            installer = new OuyaInstaller(this, this, data);
            root.addView(installer, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT));
            return;
        }
        startGame(data);
    }

    private File existing;  // the discs installed before, while the installer runs again

    @Override
    public void cancelled() {
        getSharedPreferences(OuyaInstaller.PREFS, MODE_PRIVATE).edit().putBoolean(OuyaInstaller.AGAIN, false).apply();
        root.removeView(installer);
        installer = null;
        message.setVisibility(View.VISIBLE);
        startGame(existing);
    }

    /** The menu's "Import other discs": the game ends (its core cannot start again in this process), the app
     *  starts anew with the installer */
    @Override
    public void importDiscs() {
        getSharedPreferences(OuyaInstaller.PREFS, MODE_PRIVATE).edit().putBoolean(OuyaInstaller.AGAIN, true).commit();
        android.app.AlarmManager alarms = (android.app.AlarmManager) getSystemService(ALARM_SERVICE);
        android.content.Intent start = new android.content.Intent(this, OuyaActivity.class);
        alarms.set(android.app.AlarmManager.RTC, System.currentTimeMillis() + 800, android.app.PendingIntent.getActivity(
                this, 0, start, android.app.PendingIntent.FLAG_CANCEL_CURRENT));
        finish();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override
    public void installed(File data) {
        root.removeView(installer);
        installer = null;
        message.setVisibility(View.VISIBLE);
        startGame(data);
    }

    /** The European discs: the game's language from the OUYA's (as the main app's default, CoreActivity) */
    private static int gameLanguage(File data) {
        if (!GameCubeDisc.EUROPEAN.equals(DiscInstaller.release(new File(data, "disc1"))))
            return 0;
        String language = Locale.getDefault().getLanguage();
        return "de".equals(language) ? 3 : "fr".equals(language) ? 4 : "es".equals(language) ? 5
                : "it".equals(language) ? 6 : 2;
    }

    private void startGame(final File data) {
        final int language = gameLanguage(data);
        message.setText("Starting the game ...");
        settingsChanged();
        new Thread(new Runnable() {
            @Override
            public void run() {
                // the core process lies among the app's libraries (Android 4.1's dladdr gives no folder)
                final String problem = nativeStart(data.getPath(), getApplicationInfo().nativeLibraryDir, language);
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
                // the core process ends with the thread that started it (PR_SET_PDEATHSIG goes by
                // the thread, not the process): this one stays for the app's life
                while (problem.isEmpty()) {
                    try {
                        Thread.sleep(Long.MAX_VALUE);
                    } catch (InterruptedException e) {
                        // on
                    }
                }
            }
        }, "re4-start").start();
    }

    @Override
    protected void onPause() {
        super.onPause();
        paused = true;
        view.onPause();
        nativePause(true);
    }

    @Override
    protected void onResume() {
        super.onResume();
        paused = false;
        view.onResume();
        if (menu == null) {
            nativePause(false);
        }
    }

    // --- the app's menu (L3) -----------------------------------------------------------------------

    @Override
    public void settingsChanged() {
        SharedPreferences p = getSharedPreferences(OuyaMenu.Settings.PREFS, MODE_PRIVATE);
        nativeSettings(OuyaMenu.Settings.eighths(p), OuyaMenu.Settings.fastTev(p), OuyaMenu.Settings.view(p),
                OuyaMenu.Settings.potato(p), OuyaMenu.Settings.aim(p), OuyaMenu.Settings.frameSkip(p),
                OuyaMenu.Settings.cheats(p));
    }

    @Override
    public int cheatAction(int what) {
        return running ? nativeCheatAction(what) : -2;
    }

    private void openMenu() {
        if (menu != null || !running) {
            return;
        }
        // no button stays held while the game waits; the game and its sound stop
        keys = hat = tapped = 0;
        lx = ly = rx = ry = lt = rt = 0;
        handler.removeCallbacks(untap);
        sendPad();
        nativePause(true);
        menu = new OuyaMenu(this, this);
        menu.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface dialog) {
                menu = null;
                if (!paused) {
                    nativePause(false);
                }
            }
        });
        menu.show();
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
        case KeyEvent.KEYCODE_BUTTON_START: return GC_START;
        case KeyEvent.KEYCODE_DPAD_UP: return GC_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN: return GC_DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT: return GC_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return GC_RIGHT;
        default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (installer != null) {
            // the installer: D-pad between its buttons, O chooses
            if (event.getKeyCode() == KeyEvent.KEYCODE_BUTTON_A)
                return super.dispatchKeyEvent(new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(),
                        KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount()));
            return super.dispatchKeyEvent(event);
        }
        // the OUYA's system button (short): Android gets only its release (KEYCODE_MENU), no press;
        // START for a fifth of a second (the game reads the pad once a frame)
        if (event.getKeyCode() == KeyEvent.KEYCODE_MENU) {
            if (event.getRepeatCount() == 0) {
                tapped = GC_START;
                sendPad();
                handler.removeCallbacks(untap);
                handler.postDelayed(untap, 200);
            }
            return true;
        }
        if (event.getKeyCode() == KeyEvent.KEYCODE_BUTTON_THUMBL) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                openMenu();
            }
            return true;
        }
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
        int buttons = keys | hat | tapped;
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
