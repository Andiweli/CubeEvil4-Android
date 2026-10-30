package com.ast.re4decomp;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.hardware.input.InputManager;
import android.media.AudioAttributes;
import android.media.AudioManager;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.os.VibrationAttributes;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.provider.DocumentsContract;
import android.util.Log;
import android.view.Choreographer;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;
import android.widget.Toast;
import com.ast.re4decomp.disc.RoomFileCheck;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.Reader;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Core mode: the original game core runs the game (started once per process);
 * this Activity draws its frames, passes the controller as GameCube pad 0
 * (CoreInput: layout, raw values), plays the game's sound (CoreAudio), drives
 * the vibrator from the game's rumble and prepares the room archive the game
 * enters. SELECT pressed opens the app's menu (CoreMenu: picture options,
 * memory card as .gci, error log; the game stands meanwhile), SELECT held
 * for a second the game's debug menu (decision of the project owner,
 * 17.10.2026); R3 saves graphics data for the diagnosis when the tools are
 * on (the test scripts, or the menu's switch). The touch controls
 * (TouchControls: RetroTouch above the picture) appear while no controller is
 * connected or when the menu's switch is on; their MENU is SELECT. The game
 * also stands while the app is not in front and while RetroTouch's editor is
 * open. Back ends the game (the core cannot be restarted
 * inside the same process). The error log records from the start (CoreLog).
 */
public final class CoreActivity extends Activity implements InputManager.InputDeviceListener {
  static final String TAG = "RE4Input";
  /** Preferences of the core mode's controller (MainActivity sets the swap). */
  static final String INPUT_PREFS = "core_input", SWAP_FACE = "swapFace", R3_CAPTURE = "r3Capture";
  /** The menu's setting "Touch Controls": TouchShowRule.AUTO (the default), FORCE or SUPPRESS. */
  static final String TOUCH_MODE = "touchMode";
  /** Until 21.10.2026 the switch "Use Touch Controls" (true: shown even with a controller, now FORCE). */
  static final String TOUCH_ALWAYS = "touchControls";
  // the menu's switch "Use Gyro-Aiming" (GyroAim)
  static final String GYRO_AIM = "gyroAiming";
  // the menu's switch "Alternate Controls" (CoreInput.setAlternate)
  static final String ALT_CONTROLS = "alternateControls";
  // the menu's "Button Mapping" (ActionMap.encode; absent: the default), the controller's and the
  // keyboard's; "bindings", "extraKeys" (27.10.2026) and "keyMap" (the free keys until 29.10.2026) held
  // the versions before, dropped
  static final String BINDINGS_CONTROLLER = "mappingController", BINDINGS_KEYBOARD = "mappingKeyboard";
  static final String OLD_KEY_MAP = "keyMap";
  // the mouse's sensitivity (the button mapping's slider, 0.25 .. 3, 1 = as below)
  static final String MOUSE_SENSITIVITY = "mouseSensitivity";
  static final float MOUSE_MIN = 0.25f, MOUSE_MAX = 3f, MOUSE_STEP = 0.25f;
  /** The aim's turn per count of the captured mouse at sensitivity 1 (about 400 counts a radian). */
  static final float MOUSE_AIM_RADIANS = 0.0025f;
  /** The camera's C stick per count of the captured mouse in one event at sensitivity 1. */
  static final float MOUSE_LOOK = 0.05f;
  // the menu's comfort control "Latch L and R" (CoreTouch.setLatch, CoreInput.setLatch; off: held
  // while touched or pressed); the touch controls' only until 23.10.2026, the name stays
  static final String TOUCH_LATCH = "touchLatchLR";
  // the menu's comfort control "Latch B" (the same; on: Leon keeps running)
  static final String TOUCH_LATCH_B = "touchLatchB";
  static final String TOUCH_AIM = "touchAim";
  // the menu's slider of gyro aiming (GyroAim.setSensitivity, a float, 1 = as before)
  static final String GYRO_SENSITIVITY = "gyroSensitivity";
  // the menu's switches "Invert vertical" / "Invert horizontal" of gyro aiming (GyroAim.setInvert; off)
  static final String GYRO_INVERT_V = "gyroInvertVertical", GYRO_INVERT_H = "gyroInvertHorizontal";
  // the menu's switch "Knife by Swipe" (watchTap: a swipe while playing draws the knife and slashes; on)
  static final String KNIFE_SWIPE = "knifeSwipe";
  // the menu's "Invert vertical" and "Aim speed" of aiming by touch and controllers, not the gyro (wish of the
  // project owner, 30.10.2026; CoreBridge.setAim: the game's own "reverse" option and the stick's turn while
  // aiming, the touch area's swipes too; off, 1 = as the game)
  static final String AIM_INVERT_V = "aimInvertVertical", AIM_SPEED = "aimSpeed";
  static final float AIM_SPEED_MIN = 0.5f, AIM_SPEED_MAX = 3f, AIM_SPEED_STEP = 0.25f;
  /** The menu's picture options: scale (0 auto, 1-4), samples, anisotropy, aspect (0 16:9, 1 4:3, 2 stretched, 3 4:3 without bars). */
  static final String GRAPHICS_PREFS = "core_graphics";
  /** The menu's "Game language" with the European discs (0 the device's, 2..6 as CoreBridge.setLanguage). */
  static final String GAME_PREFS = "core_game", GAME_LANGUAGE = "gameLanguage";
  /** The cheat menu's switches (CoreBridge.setCheats: 1 health, 2 ammo, 4 one-hit kill, 8 automatic QTEs, 16 the
   *  extra games kept unlocked; off). */
  static final String CHEATS = "cheats";
  private static final int REQUEST_EXPORT_ALL = 2, REQUEST_IMPORT = 3, REQUEST_REPORT = 4;
  /** SELECT held this long: the game's debug menu. */
  private static final long SELECT_HOLD_MS = 1000;

  private final Handler main = new Handler(Looper.getMainLooper());
  private final ExecutorService rooms = Executors.newSingleThreadExecutor();
  private final CoreInput input = new CoreInput();
  private final CoreAudio audio = new CoreAudio(this);
  private final Set<Integer> seenDevices = new HashSet<>();
  private final ControllerLog controllerLog = new ControllerLog();
  private GLSurfaceView surface;
  // the touch controls (RetroTouch) and its editor, during which the game stands
  private TouchControls touchControls;
  private GyroAim gyroAim;
  private boolean touchEditing;
  private String startProblem = "";
  private String settings = "";
  private volatile boolean destroyed;
  // the language at the start or the last change (onConfigurationChanged)
  private String shownLanguage = "";
  private boolean retailEu;  // the European discs are imported: the menu offers the game's language
  private boolean retail;  // any retail discs (European, Japanese, Korean): no debug menu (retail-jp-plan.md)
  private int gameLanguageNow;  // the language the game runs with (European discs)
  // the menu (SELECT) and the debug menu (SELECT held)
  private CoreMenu menu;
  private boolean menuSupported, tools, selectDown, selectHeld;
  private final List<byte[]> pendingImports = new ArrayList<>();
  private boolean resumed;
  // GPU step c: the picture drawn at each vsync or, on a faster screen, at every n-th
  private final FramePacing pacing = new FramePacing();
  private int vsyncs;
  private boolean paceLogged;
  // the screen was asked for 60 Hz (preferSixtyHz): the surface too, once it exists
  private boolean askSixty;
  // rumble: the game's motor (CoreBridge.rumble) on the device's vibrator
  private Vibrator vibrator;
  /**
   * Each start of the motor by its level 1..4 (the game's motor in the last four frames): 190 up to
   * 310 ms (120 until 27.10.2026, then 170: a shotgun still felt like a slingshot, device test).
   */
  static int rumblePulseMs(int level) { return 150 + 40 * Math.max(1, Math.min(4, level)); }
  private boolean amplitudeControl, vibrating;
  private int rumbleFrames = -1, rumbleLevel;
  private long rumbleSeen, rumbleUntil;
  private volatile int preparedRoom = -1;
  private boolean roomBusy;
  // the FPS display (menu switch): the game's frames per second and the screen's rate, bottom left
  // ("30 FPS (Game) · 60 Hz": the game makes 30 pictures a second, as on the GameCube; a phone's
  // own counter shows the screen's rate, which is not the game's)
  private TextView fpsView;
  private long fpsFrames = -1, fpsPictures, fpsSince;
  // a game that stands without a pause (a hang): reported once per stand
  private long lastFrames = -1, framesSince;
  private boolean hangReported;
  private static final long HANG_MS = 8000;
  // the core in a process of its own ended (CoreBridge.coreProblem): shown once
  private boolean coreEndShown;
  // a disc file the game could not read (CoreBridge.discProblem): checked and shown once; the
  // check's line goes into the error report (disc-check.txt)
  private boolean discProblemSeen;
  private volatile String discCheck = "";

  @Override
  @SuppressLint("ClickableViewAccessibility")
  public void onCreate(Bundle state) {
    super.onCreate(state);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    hideSystemUi();
    ImportManager imports = ((Re4Application) getApplication()).imports;
    // test switches of the core (port/scripts/test-core-app.ps1 passes them)
    settings = getIntent().getStringExtra("coreSettings");
    if (settings == null) settings = "";
    // the error log of this run (the one before is kept for the report)
    CoreLog.start(this, settings);
    shownLanguage = language();
    Log.i(TAG, "app menu: " + shownLanguage);
    // linked shader programs kept between runs (no compile stutter the second time)
    String programs = "RE4_GX_PROGRAM_CACHE=" + new File(getCacheDir(), "gx-programs").getAbsolutePath();
    // the app's list of shader programs (port/tools/gx_program_list.py): made ahead from the first start
    String programList = programList();
    if (programList != null) programs += ";RE4_GX_PROGRAM_LIST=" + programList;
    String coreSettings = settings.isEmpty() ? programs : settings + ";" + programs;
    // the game's language with the European discs (retail-eu-plan.md R7; a test script's setting wins)
    String release = imports.root == null ? null
        : com.ast.re4decomp.disc.DiscInstaller.release(new File(imports.root, "disc1"));
    retailEu = com.ast.re4decomp.disc.GameCubeDisc.EUROPEAN.equals(release);
    retail = release != null && !com.ast.re4decomp.disc.GameCubeDisc.DEBUG.equals(release);
    gameLanguageNow = gameLanguage(this);
    if (retailEu && !settings.contains("RE4_GAME_LANGUAGE=")) {
      coreSettings += ";RE4_GAME_LANGUAGE=" + gameLanguageNow;
      Log.i(TAG, "game language: " + gameLanguageNow);
    }
    // the app's own folder on the shared storage, where test switches write (RE4_AUDIO_WAV)
    getExternalFilesDir(null);
    applyGraphics();
    startProblem = imports.root == null ? "No data folder"
        : CoreBridge.start(imports.root.getAbsolutePath(), coreSettings);
    menuSupported = startProblem.isEmpty() && CoreBridge.hasMenu();
    SharedPreferences inputPrefs = getSharedPreferences(INPUT_PREFS, MODE_PRIVATE);
    input.setSwapFace(inputPrefs.getBoolean(SWAP_FACE, false));
    input.setAlternate(inputPrefs.getBoolean(ALT_CONTROLS, false));
    if (inputPrefs.contains(OLD_KEY_MAP)) {
      Log.i(TAG, "layout: the free keys of the controller configuration (" + inputPrefs.getString(OLD_KEY_MAP, "")
          + ") dropped; the button mapping starts from the default");
      inputPrefs.edit().remove(OLD_KEY_MAP).apply();
    }
    input.setControllerMap(ActionMap.decode(ActionMap.KIND_CONTROLLER, inputPrefs.getString(BINDINGS_CONTROLLER, null)));
    input.setKeyboardMap(ActionMap.decode(ActionMap.KIND_KEYBOARD, inputPrefs.getString(BINDINGS_KEYBOARD, null)));
    if (inputPrefs.contains("bindings") || inputPrefs.contains("extraKeys")) {
      inputPrefs.edit().remove("bindings").remove("extraKeys").apply();
    }
    input.setLatch(inputPrefs.getBoolean(TOUCH_LATCH, false), inputPrefs.getBoolean(TOUCH_LATCH_B, false));
    input.setTouchAim(inputPrefs.getInt(TOUCH_AIM, CoreInput.AIM_MOVE_STICK));
    knifeSwipe = inputPrefs.getBoolean(KNIFE_SWIPE, true);
    // R3 saves graphics data with the test scripts (RE4_APP_TOOLS=1) or when the menu switched it on
    tools = settings.contains("RE4_APP_TOOLS=1") || inputPrefs.getBoolean(R3_CAPTURE, false);
    logLayout();
    setUpVibrator();
    // the volume buttons set the game's volume
    setVolumeControlStream(AudioManager.STREAM_MUSIC);

    FrameLayout root = new FrameLayout(this);
    surface = new GLSurfaceView(this);
    surface.setEGLContextClientVersion(3);
    surface.setPreserveEGLContextOnPause(true);
    surface.setRenderer(new GLSurfaceView.Renderer() {
      @Override public void onSurfaceCreated(GL10 gl, EGLConfig config) { CoreBridge.surfaceCreated(); }
      @Override public void onSurfaceChanged(GL10 gl, int w, int h) { CoreBridge.resize(w, h); }
      @Override public void onDrawFrame(GL10 gl) { CoreBridge.draw(); }
    });
    root.addView(surface);
    surface.getHolder().addCallback(new SurfaceHolder.Callback() {
      @Override public void surfaceCreated(SurfaceHolder holder) { surfaceSixtyHz(holder.getSurface()); }
      @Override public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {}
      @Override public void surfaceDestroyed(SurfaceHolder holder) {}
    });
    // the surface takes the fingers beside the touch controls: Android gives the further fingers
    // of a touch only to views that took one, so a thumb beside the buttons would block them
    surface.setOnTouchListener((v, e) -> true);
    fpsView = new TextView(this);
    fpsView.setTypeface(Typeface.MONOSPACE);
    fpsView.setTextSize(11);
    fpsView.setTextColor(Color.WHITE);
    fpsView.setBackgroundColor(0x60000000);
    fpsView.setPadding(8, 2, 8, 2);
    // bottom left and left to right in every language, Arabic too (the menu's switch says so)
    fpsView.setTextDirection(View.TEXT_DIRECTION_LTR);
    root.addView(fpsView, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT,
        FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM | Gravity.LEFT));
    showFps(getSharedPreferences(GRAPHICS_PREFS, MODE_PRIVATE).getBoolean("fps", false));
    // the touch controls above everything
    touchControls = new TouchControls(this, input, touchMode(inputPrefs), touchHost);
    touchControls.setLatch(inputPrefs.getBoolean(TOUCH_LATCH, false));
    touchControls.setLatchB(inputPrefs.getBoolean(TOUCH_LATCH_B, false));
    gyroAim = new GyroAim(this, main);
    gyroAim.setSensitivity(inputPrefs.getFloat(GYRO_SENSITIVITY, 1f));
    mouseSensitivity = clampMouse(inputPrefs.getFloat(MOUSE_SENSITIVITY, 1f));
    aimInvert = inputPrefs.getBoolean(AIM_INVERT_V, false);
    aimSpeed = clampAimSpeed(inputPrefs.getFloat(AIM_SPEED, 1f));
    // the mouse (step 2 of the button mapping, 29.10.2026): captured while the game is in front, its
    // buttons through the keyboard's mapping, its movement the camera and, while aiming, the aim
    if (Build.VERSION.SDK_INT >= 26) {
      surface.setFocusable(true);
      surface.setFocusableInTouchMode(true);
      surface.setOnCapturedPointerListener((v, e) -> {
        capturedMouse(e);
        return true;
      });
    }
    gyroAim.setInvert(inputPrefs.getBoolean(GYRO_INVERT_V, false), inputPrefs.getBoolean(GYRO_INVERT_H, false));
    gyroAim.setEnabled(inputPrefs.getBoolean(GYRO_AIM, false));
    root.addView(touchControls.view(), new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT,
        FrameLayout.LayoutParams.MATCH_PARENT));
    if (startProblem.isEmpty()) {
      SharedPreferences g = getSharedPreferences(GRAPHICS_PREFS, MODE_PRIVATE);
      CoreBridge.setDof(g.getBoolean("dof", true));
      // one or the other (CoreMenu): saved with both on before 26.10.2026, 60 FPS stays
      if (g.getBoolean("smooth60", false) && g.getBoolean("frameskip", false))
        g.edit().putBoolean("frameskip", false).apply();
      CoreBridge.setSmooth(g.getBoolean("smooth60", false));
      CoreBridge.setFrameSkip(g.getBoolean("frameskip", false));
      CoreBridge.setFxaa(g.getBoolean("fxaa", false));
      CoreBridge.setFastCores(g.getInt("fastCores", 0) == 1);
      CoreBridge.setPotato(CoreBridge.potatoMask(g));
      CoreBridge.sendSoundVolume(g);
      CoreBridge.setAim(aimInvert, aimSpeed);
      // the cheat switches kept from the last run (a test script's RE4_CHEATS wins)
      int cheats = getSharedPreferences(GAME_PREFS, MODE_PRIVATE).getInt(CHEATS, 0);
      if (!settings.contains("RE4_CHEATS=")) {
        if (cheats != 0) Log.i(TAG, "cheats: " + cheats);
        CoreBridge.setCheats(cheats);
      }
    }
    setContentView(root);
    preferSixtyHz();
    surface.setFocusable(true);
    surface.requestFocus();
    if (Build.VERSION.SDK_INT >= 33)
      getOnBackInvokedDispatcher().registerOnBackInvokedCallback(0, this::askExit);
    if (!startProblem.isEmpty()) {
      Log.e(TAG, "core not started: " + startProblem);
      main.post(this::showStartProblem);
    }
    main.post(this::tick);
  }

  /** The core did not start: why, with the error report to send. */
  private void showStartProblem() {
    if (destroyed) return;
    new AlertDialog.Builder(this)
        .setMessage(getString(R.string.ingame_core_not_started, startProblem))
        .setPositiveButton(R.string.ingame_send_report, (d, w) -> shareReport())
        .setNegativeButton(R.string.ingame_quit, (d, w) -> quit())
        .setCancelable(false).show();
  }

  /**
   * The core in a process of its own (32-bit devices whose Java heap lies where the game's memory
   * must be): when that process ends (a crash), the app stays and says so, with the error report
   * to send (the crash is in it).
   */
  private void checkCoreProcess() {
    if (coreEndShown || !startProblem.isEmpty()) return;
    String ended = CoreBridge.coreProblem();
    if (ended == null || ended.isEmpty()) return;
    coreEndShown = true;
    Log.e(TAG, "core process: " + ended);
    audio.stop();
    stopVibrator();
    new AlertDialog.Builder(this)
        .setMessage(getString(R.string.ingame_core_stopped, ended))
        .setPositiveButton(R.string.ingame_send_report, (d, w) -> shareReport())
        .setNegativeButton(R.string.ingame_quit, (d, w) -> quit())
        .setCancelable(false).show();
  }

  /**
   * A disc file the game could not read (the core gives up on it and the game stands, reports of
   * 25./26.10.2026: St2/r222.das read as zeros): which file, whether the disc image or the copy on the
   * device is at fault (RoomFileCheck.diagnose against the import's manifest and files.tsv), and what
   * to do, once.
   */
  private void checkDiscProblem() {
    if (discProblemSeen || !startProblem.isEmpty()) return;
    String problem = CoreBridge.discProblem();
    if (problem == null || problem.isEmpty()) return;
    discProblemSeen = true;
    String[] f = problem.split("\t", 3);
    if (f.length < 3) return;
    final int disc = f[0].equals("2") ? 2 : 1;
    final String path = f[1], why = f[2];
    Log.e(TAG, "disc file: disc " + disc + " " + path + ": " + why);
    final ImportManager imports = ((Re4Application) getApplication()).imports;
    rooms.execute(() -> {
      File folder = new File(imports.root, "disc" + disc);
      InputStream catalog = null;
      try {
        catalog = imports.roomCatalog(folder);
      } catch (IOException ignored) { /* diagnosed without the catalog */
      }
      RoomFileCheck.Diagnosis d = RoomFileCheck.diagnose(folder, path, catalog, disc);
      discCheck = "core: " + why + "\n" + d.cause + ": " + d.detail + "\n";
      Log.e(TAG, "disc file check: " + d.cause + ": " + d.detail);
      main.post(() -> showDiscProblem(d.cause, path, disc));
    });
  }

  private void showDiscProblem(RoomFileCheck.Cause cause, String path, int disc) {
    if (destroyed) return;
    int text = cause == RoomFileCheck.Cause.IMAGE ? R.string.ingame_disc_image
        : cause == RoomFileCheck.Cause.DEVICE ? R.string.ingame_disc_device
        : cause == RoomFileCheck.Cause.READ ? R.string.ingame_disc_read : R.string.ingame_disc_unknown;
    new AlertDialog.Builder(this)
        .setTitle(R.string.ingame_disc_title)
        .setMessage(getString(text, path, disc))
        .setPositiveButton(R.string.ingame_send_report, (dlg, w) -> shareReport())
        .setNegativeButton(R.string.ingame_quit, (dlg, w) -> quit())
        .setNeutralButton(R.string.ingame_continue, null)
        .setCancelable(false).show();
  }

  /** The menu's picture options to the renderer (applied at the next picture while the game runs). */
  private void applyGraphics() {
    SharedPreferences g = getSharedPreferences(GRAPHICS_PREFS, MODE_PRIVATE);
    CoreBridge.setGraphics(CoreMenu.scaleHalves(g), g.getInt("samples", 1), g.getInt("anisotropy", 1),
        g.getInt("aspect", 0));
  }

  private void hideSystemUi() {
    getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
  }

  /** The menu's setting "Touch Controls"; the old switch "Use Touch Controls" on counts as Force. */
  static int touchMode(SharedPreferences prefs) {
    if (prefs.contains(TOUCH_MODE)) return prefs.getInt(TOUCH_MODE, TouchShowRule.AUTO);
    return prefs.getBoolean(TOUCH_ALWAYS, false) ? TouchShowRule.FORCE : TouchShowRule.AUTO;
  }

  /**
   * Twice a second: the room the game wants; the FPS display; a game that
   * stands without a pause; the touch controls shown or hidden by the
   * controllers present.
   */
  private void tick() {
    if (destroyed) return;
    touchControls.update();
    updateFps();
    checkHang();
    checkCoreProcess();
    checkDiscProblem();
    int room = CoreBridge.wantedRoom();
    if (room >= 0 && room != preparedRoom && !roomBusy) prepareRoom(room);
    main.postDelayed(this::tick, 500);
  }

  private void showFps(boolean on) {
    fpsView.setVisibility(on && menuSupported ? View.VISIBLE : View.GONE);
    fpsView.setText(getString(R.string.ingame_fps_none) + screenHz() + resolution());
    fpsFrames = -1;
  }

  /**
   * " · 1280×672 (2×)": the resolution the renderer draws at now, the level "Dynamic" chose too
   * (wish of the project owner, 23.10.2026), the picture's rows as the menu counts them (336, 4:3
   * 448); "" before the renderer started.
   */
  private String resolution() {
    int[] info = CoreBridge.graphicsInfo();
    if (info == null || info[2] < 2) return "";  // the level in halves (1.5×: 3)
    int aspect = getSharedPreferences(GRAPHICS_PREFS, MODE_PRIVATE).getInt("aspect", 0);
    int rows = aspect == 1 || aspect == 3 ? 448 : 336;
    return String.format(Locale.ROOT, " · %d×%d (%s×)", 640 * info[2] / 2, rows * info[2] / 2,
        CoreMenu.scaleNumber(info[2], Locale.getDefault()));
  }

  /** " · 60 Hz": the screen's rate as measured (FramePacing), else as Android says; "" unknown. */
  private String screenHz() {
    double hz = pacing.hz();
    if (!(hz > 0)) {
      Display display = getWindowManager().getDefaultDisplay();
      hz = display != null ? display.getRefreshRate() : 0;
    }
    return hz > 0 ? String.format(Locale.ROOT, " · %d Hz", Math.round(hz)) : "";
  }

  /**
   * The game's frames per second (one controller read per frame), counted over a second; with
   * "60 FPS (experimental)" also the pictures on the screen, the in-between ones included (the game
   * itself stays at 30; device test of the project owner, 22.10.2026).
   */
  private void updateFps() {
    if (fpsView.getVisibility() != View.VISIBLE) return;
    long now = SystemClock.uptimeMillis(), frames = CoreBridge.gameFrames(), pictures = CoreBridge.pictures();
    if (frames < 0 || !resumed || menu != null || touchEditing) {
      fpsFrames = -1;  // the game stands: count anew afterwards
      return;
    }
    if (fpsFrames < 0) {
      fpsFrames = frames;
      fpsPictures = pictures;
      fpsSince = now;
    } else if (now - fpsSince >= 1000) {
      double seconds = (now - fpsSince) / 1000.0;
      long game = Math.round((frames - fpsFrames) / seconds), shown = Math.round((pictures - fpsPictures) / seconds);
      boolean smooth = getSharedPreferences(GRAPHICS_PREFS, MODE_PRIVATE).getBoolean("smooth60", false);
      fpsView.setText((smooth ? String.format(Locale.ROOT, getString(R.string.ingame_fps_smooth), game, shown)
          : String.format(Locale.ROOT, getString(R.string.ingame_fps), game)) + screenHz() + resolution());
      fpsFrames = frames;
      fpsPictures = pictures;
      fpsSince = now;
    }
  }

  /**
   * The game reads the controller every frame; when its frames stand still
   * for 8 s while it is neither paused nor in the background, the error log
   * gets where its thread is (a loop or a wait that never ends).
   */
  private void checkHang() {
    if (!menuSupported) return;
    long now = SystemClock.uptimeMillis();
    long frames = CoreBridge.gameFrames();
    if (frames < 0) return;
    if (frames != lastFrames || !resumed || menu != null || touchEditing) {
      if (frames != lastFrames) hangReported = false;  // the game moves (again)
      lastFrames = frames;
      framesSince = now;
      return;
    }
    if (!hangReported && now - framesSince > HANG_MS) {
      hangReported = true;
      Log.w(TAG, "hang: no game frame for " + (now - framesSince) / 1000 + " s");
      rooms.execute(() -> CoreBridge.reportHang((int) ((SystemClock.uptimeMillis() - framesSince) / 1000)));
    }
  }

  /** Decodes the room archive the game entered with the app's room loader. */
  private void prepareRoom(final int room) {
    roomBusy = true;
    final File root = ((Re4Application) getApplication()).imports.root;
    final File cache = getCacheDir();
    rooms.execute(() -> {
      String path = "", error = "";
      String id = String.format(Locale.ROOT, "r%03x", room);
      // the verified rooms of the imported release (rooms.tsv / rooms-eu.tsv)
      String release = com.ast.re4decomp.disc.DiscInstaller.release(new File(root, "disc1"));
      if (release == null) release = com.ast.re4decomp.disc.GameCubeDisc.DEBUG;
      try (Reader rows = new InputStreamReader(getAssets().open(com.ast.re4decomp.disc.GameCubeDisc.catalog("rooms", release)),
               StandardCharsets.UTF_8);
           Reader blocks = new InputStreamReader(getAssets().open(com.ast.re4decomp.disc.GameCubeDisc.catalog("blocks", release)),
               StandardCharsets.UTF_8)) {
        RoomRepository repository = new RoomRepository(root, cache, rows, blocks,
            (source, output) -> NativeBridge.decodeRoom(source.getAbsolutePath(), output.getAbsolutePath()));
        path = repository.prepare(id).getAbsolutePath();
      } catch (Exception e) {
        error = id + ": " + (e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage());
      }
      CoreBridge.roomPrepared(room, path, error);
      preparedRoom = room;
      main.post(() -> roomBusy = false);
    });
  }

  private void askExit() {
    new AlertDialog.Builder(this).setMessage(R.string.ingame_end_question)
        .setPositiveButton(R.string.ingame_end, (d, w) -> quit())
        .setNegativeButton(R.string.ingame_continue, null).show();
  }

  private void quit() {
    CoreLog.stop();
    finishAndRemoveTask();
    // the core cannot be restarted inside this process
    main.postDelayed(() -> System.exit(0), 300);
  }

  // --- the menu (SELECT) -------------------------------------------------------

  /** SELECT held for a second: the game's debug menu (patch 0061), once per press. */
  private final Runnable selectHold = () -> {
    if (!selectDown || menu != null) return;
    selectHeld = true;
    // the retail discs have no debug menu (decision of the project owner, 26.10.2026; the cheat menu
    // of the in-game menu instead; the Japanese discs lack the debug tools as well): holding SELECT
    // opens nothing
    if (retail) {
      Log.i(TAG, "SELECT held: no debug menu with the retail discs");
      return;
    }
    if (!menuSupported) {
      Toast.makeText(this, R.string.ingame_no_debug_menu,
          Toast.LENGTH_SHORT).show();
      return;
    }
    CoreBridge.debugMenu();
    vibrate(2);
    Log.i(TAG, "SELECT held: debug menu");
  };

  /** SELECT (a controller's key or the touch controls' MENU) went down or up. */
  private void select(boolean down) {
    if (down) {
      selectDown = true;
      selectHeld = false;
      main.removeCallbacks(selectHold);
      main.postDelayed(selectHold, SELECT_HOLD_MS);
    } else if (selectDown) {
      // let go before the second: the menu
      selectDown = false;
      main.removeCallbacks(selectHold);
      if (!selectHeld) openMenu();
    }
  }

  private void openMenu() {
    if (menu != null || destroyed) return;
    if (!menuSupported) {
      Toast.makeText(this, R.string.ingame_no_menu,
          Toast.LENGTH_SHORT).show();
      return;
    }
    releaseAll();
    boolean paused = CoreBridge.pause(true, 800);
    audio.stop();
    stopVibrator();
    Log.i(TAG, "menu open" + (paused ? ", the game stands" : ", the game did not stop yet (loading?)")
        + " (" + language() + ")");
    menu = new CoreMenu(this, menuHost, tools, surface.getWidth(), surface.getHeight());
    menu.setOnDismissListener(d -> {
      menu = null;
      releaseAll();
      if (resumed && !touchEditing) {
        CoreBridge.pause(false, 0);
        audio.start();
        main.removeCallbacks(rumbleTick);
        main.post(rumbleTick);
      }
      Log.i(TAG, "menu closed");
      surface.requestFocus();
      main.post(this::updateMouseCapture);
    });
    menu.show();
  }

  private final CoreMenu.Host menuHost = new CoreMenu.Host() {
    @Override public void graphicsChanged() { applyGraphics(); }

    @Override public void exportAll() {
      startForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_EXPORT_ALL);
    }

    @Override public void importFiles() {
      Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
          .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
      startForResult(i, REQUEST_IMPORT);
    }

    @Override public void shareReport() { CoreActivity.this.shareReport(); }

    @Override public void saveReport() {
      Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
          .setType("application/zip").putExtra(Intent.EXTRA_TITLE, reportName());
      startForResult(i, REQUEST_REPORT);
    }

    @Override public void logChanged(boolean on) { CoreLog.setEnabled(CoreActivity.this, on); }

    @Override public void toolsChanged(boolean on) {
      tools = on || settings.contains("RE4_APP_TOOLS=1");
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putBoolean(R3_CAPTURE, on).apply();
    }

    @Override public void fpsChanged(boolean on) { showFps(on); }

    @Override public void touchModeChanged(int mode) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putInt(TOUCH_MODE, mode).apply();
      touchControls.setMode(mode);
    }

    @Override public void gyroChanged(boolean on) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putBoolean(GYRO_AIM, on).apply();
      gyroAim.setEnabled(on);
    }

    @Override public void latchChanged(boolean on) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putBoolean(TOUCH_LATCH, on).apply();
      touchControls.setLatch(on);
      input.setLatch(on, input.latchB());
      send();
      Log.i(TAG, "comfort controls: L and R " + (on ? "latch on a tap or a press" : "held while touched or pressed"));
    }

    @Override public void latchBChanged(boolean on) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putBoolean(TOUCH_LATCH_B, on).apply();
      touchControls.setLatchB(on);
      input.setLatch(input.latchLR(), on);
      send();
      Log.i(TAG, "comfort controls: B " + (on ? "latches on a tap or a press" : "held while touched or pressed"));
    }

    @Override public void touchAimChanged(int mode) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putInt(TOUCH_AIM, mode).apply();
      input.setTouchAim(mode);
      send();
      Log.i(TAG, "touch aiming: " + (mode == CoreInput.AIM_LOOK ? "the touch area" : mode == CoreInput.AIM_C_STICK
          ? "the touch C stick" : "the move stick") + " aims while L or R is held");
    }

    @Override public void gyroSensitivityChanged(float value) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putFloat(GYRO_SENSITIVITY, value).apply();
      gyroAim.setSensitivity(value);
    }

    @Override public void gyroInvertChanged(boolean vertical, boolean on) {
      SharedPreferences prefs = getSharedPreferences(INPUT_PREFS, MODE_PRIVATE);
      prefs.edit().putBoolean(vertical ? GYRO_INVERT_V : GYRO_INVERT_H, on).apply();
      gyroAim.setInvert(prefs.getBoolean(GYRO_INVERT_V, false), prefs.getBoolean(GYRO_INVERT_H, false));
    }

    @Override public void knifeSwipeChanged(boolean on) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putBoolean(KNIFE_SWIPE, on).apply();
      knifeSwipe = on;
      Log.i(TAG, "touch: swipes " + (on ? "draw the knife and slash" : "leave the knife alone"));
    }

    @Override public void checkUpdate(AppUpdate.Listener listener) { AppUpdate.check(CoreActivity.this, listener); }

    @Override public void startUpdate(AppUpdate.Listener listener) { AppUpdate.install(CoreActivity.this, listener); }

    @Override public void aimInvertChanged(boolean on) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putBoolean(AIM_INVERT_V, on).apply();
      aimInvert = on;
      CoreBridge.setAim(aimInvert, aimSpeed);
    }

    @Override public void aimSpeedChanged(float value) {
      aimSpeed = clampAimSpeed(value);
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putFloat(AIM_SPEED, aimSpeed).apply();
      CoreBridge.setAim(aimInvert, aimSpeed);
    }

    @Override public boolean gameLanguages() { return retailEu; }

    @Override public int gameLanguageOf(int choice) { return CoreActivity.gameLanguageOf(choice); }

    @Override public void gameLanguageChanged(int choice) {
      getSharedPreferences(GAME_PREFS, MODE_PRIVATE).edit().putInt(GAME_LANGUAGE, choice).apply();
      int language = gameLanguageOf(choice);
      Log.i(TAG, "game language: " + language + (choice == 0 ? " (the device's)" : "")
          + (language != gameLanguageNow ? ", the game restarts" : ""));
      if (language != gameLanguageNow) {
        gameLanguageNow = language;
        CoreBridge.restartGame(language);
      }
    }

    @Override public void cheatsChanged(int bits) {
      getSharedPreferences(GAME_PREFS, MODE_PRIVATE).edit().putInt(CHEATS, bits).apply();
      Log.i(TAG, "cheats: " + bits + " (1 health, 2 ammo, 4 one-hit kill, 8 automatic QTEs, 16 extra games kept)");
      CoreBridge.setCheats(bits);
    }

    @Override public void restartGame() {
      Log.i(TAG, "the game restarts (extra games), its language stays");
      CoreBridge.restartGame(0);
    }

    @Override public int cheatAction(int what) {
      int result = CoreBridge.cheatAction(what);
      Log.i(TAG, "cheat button " + what + ": " + result);
      return result;
    }

    @Override public void alternateChanged(boolean on) {
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putBoolean(ALT_CONTROLS, on).apply();
      input.setAlternate(on);
      send();
      logLayout();
    }

    @Override public void mappingChanged(ActionMap map) {
      String text = map.encode();
      String key = map.kind == ActionMap.KIND_KEYBOARD ? BINDINGS_KEYBOARD : BINDINGS_CONTROLLER;
      SharedPreferences.Editor e = getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit();
      if (text == null) e.remove(key); else e.putString(key, text);
      e.apply();
      if (map.kind == ActionMap.KIND_KEYBOARD) input.setKeyboardMap(map); else input.setControllerMap(map);
      send();
      logLayout();
    }

    @Override public ActionMap mapping(int kind) {
      return kind == ActionMap.KIND_KEYBOARD ? input.keyboardMap() : input.controllerMap();
    }

    @Override public int faceLabel(int keyCode) { return input.faceLabel(keyCode); }

    @Override public boolean controllerConnected() {
      for (int id : InputDevice.getDeviceIds()) if (gameDevice(InputDevice.getDevice(id))) return true;
      return false;
    }

    @Override public boolean keyboardConnected() {
      for (int id : InputDevice.getDeviceIds()) {
        InputDevice d = InputDevice.getDevice(id);
        if (keyboardDevice(d) || mouseDevice(d)) return true;
      }
      return false;
    }

    @Override public float mouseSensitivity() { return mouseSensitivity; }

    @Override public void mouseSensitivityChanged(float value) {
      mouseSensitivity = clampMouse(value);
      getSharedPreferences(INPUT_PREFS, MODE_PRIVATE).edit().putFloat(MOUSE_SENSITIVITY, mouseSensitivity).apply();
      Log.i(TAG, "mouse: sensitivity " + mouseSensitivity);
    }
  };

  private void logLayout() {
    // the controller's own mapping (not with the alternate controls) and the keyboard's
    String extra = (input.ownControllerMap() && !input.alternate()
        ? "; own controller mapping (in the game, the menus keep the layout): "
            + input.controllerMap().describe(CoreMenu::keyName) : "")
        + "; keyboard: " + (input.keyboardMap().isDefault() ? "default" : "own")
        + " mapping: " + input.keyboardMap().describe(CoreMenu::keyName);
    Log.i(TAG, "layout: A B X Y by label" + (input.swapFace() ? ", A/B and X/Y swapped" : "")
        + (input.alternate() ? ", alternate controls: L1 = L, L2 = R, R1 = Y, R2 = A, Y = Z, aiming with the right stick"
            : ", R2 = R, L2 = L, R1 = Z")
        + ", SELECT menu (held: debug menu), R3 " + (tools ? "graphics capture" : "unused") + extra
        + (menuSupported ? "" : "; this core has no menu functions"));
  }

  private final TouchControls.Host touchHost = new TouchControls.Host() {
    @Override public void select(boolean down, boolean finger) {
      if (down || finger) {
        CoreActivity.this.select(down);
      } else {
        // RetroTouch let MENU go itself (hidden, reset): no menu
        selectDown = false;
        main.removeCallbacks(selectHold);
      }
    }

    @Override public void editing(boolean on) { touchEditing(on); }

    @Override public void send() { CoreActivity.this.send(); }

    @Override public void r3(boolean down) {
      Log.i(TAG, "touch R3 -> app " + (tools ? "graphics capture" : "nothing (tools off)"));
      if (down) capture();
    }
  };

  /**
   * R3 (the controller's right stick pressed, or the touch controls' R3): the graphics data of the
   * next pictures for the diagnosis in the cloud (files/gx-dumps, test-core-app.ps1 /
   * pull-gx-dumps.ps1 fetch them); only with "R3 = Graphics capture" or the test scripts' tools.
   */
  private void capture() {
    if (!tools) return;
    File dir = getExternalFilesDir(null);
    if (dir != null) {
      CoreBridge.gxDump(new File(dir, "gx-dumps").getAbsolutePath());
      Toast.makeText(this, R.string.ingame_capture_saved, Toast.LENGTH_SHORT).show();
    }
  }

  /** RetroTouch's editor (its gear) opened or closed: the game stands while the player arranges the controls. */
  private void touchEditing(boolean on) {
    touchEditing = on;
    input.clear();
    send();
    updateMouseCapture();
    if (!menuSupported) return;
    if (on) {
      boolean paused = CoreBridge.pause(true, 800);
      audio.stop();
      stopVibrator();
      Log.i(TAG, "touch editor: " + (paused ? "the game stands" : "the game did not stop yet (loading?)"));
    } else if (resumed && menu == null) {
      CoreBridge.pause(false, 0);
      audio.start();
      main.removeCallbacks(rumbleTick);
      main.post(rumbleTick);
    }
  }

  private void startForResult(Intent intent, int request) {
    try {
      startActivityForResult(intent, request);
    } catch (android.content.ActivityNotFoundException e) {
      Toast.makeText(this, R.string.ingame_no_file_picker,
          Toast.LENGTH_LONG).show();
    }
  }

  private void message(String text) {
    new AlertDialog.Builder(this).setMessage(text).setPositiveButton(android.R.string.ok, null).show();
  }

  @Override
  protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (result != RESULT_OK || data == null) return;
    try {
      if (request == REQUEST_EXPORT_ALL && data.getData() != null) {
        exportAllInto(data.getData());
      } else if (request == REQUEST_IMPORT) {
        pendingImports.clear();
        if (data.getClipData() != null) {
          for (int i = 0; i < data.getClipData().getItemCount(); i++) readImport(data.getClipData().getItemAt(i).getUri());
        } else if (data.getData() != null) {
          readImport(data.getData());
        }
        importNext(new StringBuilder());
      } else if (request == REQUEST_REPORT && data.getData() != null) {
        try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
          if (out == null) throw new IOException("no stream");
          CoreLog.writeReport(this, out, settings, reportExtras());
        }
        Toast.makeText(this, R.string.ingame_report_saved, Toast.LENGTH_SHORT).show();
      }
    } catch (IOException | RuntimeException e) {
      Log.w(TAG, "menu: " + e);
      message(getString(R.string.ingame_failed, e.getMessage()));
    }
  }

  /** Every file of the card as .gci into the chosen folder. */
  private void exportAllInto(Uri tree) throws IOException {
    String[] rows = CoreBridge.cardList();
    if (rows == null) {
      message(getString(R.string.ingame_card_unreadable));
      return;
    }
    Uri folder = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
    int done = 0;
    StringBuilder problems = new StringBuilder();
    for (String row : rows) {
      int index = Integer.parseInt(row.split("\t", 2)[0]);
      String name = CoreBridge.cardFileName(index);
      byte[] gci = CoreBridge.cardExport(index);
      if (gci == null || name == null) {
        problems.append('\n').append(CoreBridge.cardNote());
        continue;
      }
      Uri file = DocumentsContract.createDocument(getContentResolver(), folder, "application/octet-stream", name);
      if (file == null) throw new IOException(name);
      try (OutputStream out = getContentResolver().openOutputStream(file)) {
        if (out == null) throw new IOException(name);
        out.write(gci);
      }
      done++;
    }
    message(String.format(java.util.Locale.ROOT, getResources().getQuantityString(R.plurals.ingame_exported, done), done)
        + problems);
  }

  private void readImport(Uri uri) throws IOException {
    try (InputStream in = getContentResolver().openInputStream(uri)) {
      if (in == null) throw new IOException(String.valueOf(uri));
      // a .gci, a whole Dolphin card (.raw, .gcp) or a ZIP of either: the .gci files of this game
      StringBuilder found = new StringBuilder();
      try {
        pendingImports.addAll(SaveImport.files(CoreLog.readAll(in), found));
      } catch (IllegalArgumentException e) {
        throw new IOException(e.getMessage());
      }
      if (found.length() > 0) Log.i(TAG, "import: " + found);
    }
  }

  /** The file name inside a .gci (its directory entry, bytes 8..39). */
  private static String gciName(byte[] gci) {
    if (gci.length < 0x40) return "?";
    int n = 0;
    while (n < 32 && gci[8 + n] != 0) n++;
    return new String(gci, 8, n, java.nio.charset.StandardCharsets.ISO_8859_1);
  }

  /** What became of an import, in the menu's language. */
  private String importText(int r, String name) {
    switch (r) {
      case CoreBridge.CARD_OK: return getString(R.string.ingame_imported, name);
      case CoreBridge.CARD_NOSPACE: return getString(R.string.ingame_import_no_space, name);
      case CoreBridge.CARD_BUSY: return getString(R.string.ingame_import_busy, name);
      case CoreBridge.CARD_NOCARD: return getString(R.string.ingame_import_no_card, name);
      default:
        return getString(R.string.ingame_import_bad, name, CoreBridge.cardNote());
    }
  }

  /** The chosen .gci files one by one; a file already on the card is replaced only when asked. */
  private void importNext(StringBuilder report) {
    if (pendingImports.isEmpty()) {
      if (report.length() > 0) message(report.toString().trim());
      if (menu != null) menu.refreshCard();
      return;
    }
    byte[] gci = pendingImports.remove(0);
    String name = gciName(gci);
    int r = CoreBridge.cardImport(gci, false);
    Log.i(TAG, "import " + name + ": " + r + " " + CoreBridge.cardNote());
    if (r == CoreBridge.CARD_EXISTS) {
      new AlertDialog.Builder(this)
          .setMessage(getString(R.string.ingame_import_exists, name))
          .setPositiveButton(R.string.ingame_replace, (d, w) -> {
            int again = CoreBridge.cardImport(gci, true);
            Log.i(TAG, "import " + name + " (replace): " + again + " " + CoreBridge.cardNote());
            report.append(importText(again, name)).append('\n');
            importNext(report);
          })
          .setNegativeButton(R.string.ingame_skip, (d, w) -> importNext(report))
          .setCancelable(false).show();
      return;
    }
    report.append(importText(r, name)).append('\n');
    importNext(report);
  }

  private String reportName() {
    return "cubeevil4-report-" + new SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.ROOT).format(new Date()) + ".zip";
  }

  /** The card's file list (without the saves themselves) for the report. */
  /**
   * The asset gx-programs.keys (the shader programs of all devices' error reports, merged by
   * port/tools/gx_program_list.py) as a file the core reads; null when the app has none.
   */
  private String programList() {
    byte[] list;
    try (InputStream in = getAssets().open("gx-programs.keys")) {
      list = CoreLog.readAll(in);
    } catch (IOException e) {
      return null;
    }
    File file = new File(getCacheDir(), "gx-programs.keys");
    try {
      boolean same = false;
      if (file.length() == list.length) {
        try (InputStream in = new java.io.FileInputStream(file)) {
          same = java.util.Arrays.equals(CoreLog.readAll(in), list);
        }
      }
      if (!same) {
        try (OutputStream out = new FileOutputStream(file)) {
          out.write(list);
        }
      }
      return file.getAbsolutePath();
    } catch (IOException e) {
      Log.w(TAG, "program list: " + e);
      return null;
    }
  }

  private String[][] reportExtras() {
    // the keys of the shader programs this device made (they go into the app's list)
    try {
      CoreBridge.writeProgramKeys(new File(CoreLog.folder(this), "program-keys.bin").getAbsolutePath());
    } catch (UnsatisfiedLinkError e) {
      Log.w(TAG, "program keys: " + e);
    }
    StringBuilder card = new StringBuilder();
    String[] rows = CoreBridge.cardList();
    if (rows == null) {
      card.append("(card: ").append(CoreBridge.cardNote()).append(")\n");
    } else {
      for (String row : rows) card.append(CoreMenu.describe(row.split("\t", -1))).append('\n');
      card.append(CoreBridge.cardFreeBlocks()).append(" blocks free\n");
    }
    String map = CoreBridge.addressMap();
    String controllers = controllerList();
    String discs = discCheckText();
    if (map == null || map.isEmpty())
      return new String[][] {{"memory-card.txt", card.toString()}, {"controllers.txt", controllers}, {"disc-check.txt", discs}};
    return new String[][] {{"memory-card.txt", card.toString()}, {"address-map.txt", map}, {"controllers.txt", controllers},
        {"disc-check.txt", discs}};
  }

  /** The report's disc-check.txt: each imported disc against the original's files, and a file the game could not read. */
  private String discCheckText() {
    ImportManager imports = ((Re4Application) getApplication()).imports;
    StringBuilder t = new StringBuilder();
    for (int n = 1; n <= 2 && imports.root != null; n++) {
      File folder = new File(imports.root, "disc" + n);
      t.append("Disc ").append(n).append(": ");
      if (!com.ast.re4decomp.disc.DiscInstaller.ready(folder, n)) {
        t.append("not imported\n");
        continue;
      }
      t.append(ImportManager.releaseName(com.ast.re4decomp.disc.DiscInstaller.release(folder))).append(", ");
      try (InputStream catalog = imports.roomCatalog(folder)) {
        RoomFileCheck c = RoomFileCheck.verify(folder, catalog, n);
        t.append(c.checked).append(c.everyFile ? " files" : " room files").append(" checked (the import's hashes), ");
        t.append(c.bad.isEmpty() ? "all as on the original disc" : c.bad.size() + " differ: " + String.join(", ",
            c.bad.subList(0, Math.min(20, c.bad.size()))));
      } catch (IOException | RuntimeException e) {
        t.append("no check: ").append(e);
      }
      t.append('\n');
    }
    String problem = CoreBridge.discProblem();
    t.append("Read problem: ").append(problem == null || problem.isEmpty() ? "none" : problem.replace('\t', ' ')).append('\n');
    t.append(discCheck);
    return t.toString();
  }

  /** The error report to an app of the tester's choice (mail, messenger, cloud). */
  private void shareReport() {
    try {
      File dir = ReportProvider.folder(this);
      File[] old = dir.listFiles();
      if (old != null) for (File f : old) f.delete();
      File zip = new File(dir, reportName());
      try (OutputStream out = new FileOutputStream(zip)) {
        CoreLog.writeReport(this, out, settings, reportExtras());
      }
      Uri uri = ReportProvider.uri(this, zip);
      Intent send = new Intent(Intent.ACTION_SEND).setType("application/zip").putExtra(Intent.EXTRA_STREAM, uri)
          .putExtra(Intent.EXTRA_SUBJECT, getString(R.string.app_label) + ": " + zip.getName()).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
      send.setClipData(ClipData.newRawUri(zip.getName(), uri));
      startActivity(Intent.createChooser(send, getString(R.string.ingame_report_chooser)));
    } catch (IOException | RuntimeException e) {
      Log.w(TAG, "report: " + e);
      message(getString(R.string.ingame_report_failed, e.getMessage()));
    }
  }

  @Override
  @SuppressWarnings("deprecation")
  public void onBackPressed() {
    askExit();
  }

  private static boolean controller(android.view.InputEvent e) {
    return (e.getSource() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        || (e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        || (e.getSource() & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD;
  }

  private void send() {
    int[] p = input.pad();
    CoreBridge.pad(p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
    // swipe aiming (Touch Aiming: Touch Area): the finger's way since the last event as angles, with the
    // menu's "Aim speed" and "Invert vertical" as the sticks
    float[] turn = input.takeAimSwipe();
    if (turn != null) CoreBridge.aimTurn(turn[0] * aimSpeed, (aimInvert ? -turn[1] : turn[1]) * aimSpeed);
  }

  /** The keys the game maps or the app uses: which of them a device reports. */
  private static final int[] WATCHED_KEYS = {
      KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_BUTTON_Y,
      KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_BUTTON_R2,
      KeyEvent.KEYCODE_BUTTON_THUMBL, KeyEvent.KEYCODE_BUTTON_THUMBR, KeyEvent.KEYCODE_BUTTON_START,
      KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BUTTON_MODE, KeyEvent.KEYCODE_BUTTON_C,
      KeyEvent.KEYCODE_BUTTON_Z, KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_DPAD_DOWN,
      KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_DPAD_RIGHT};

  /** One line about a device: name, ids, sources, axes and which of the watched keys it has. */
  private static String describeDevice(InputDevice d) {
    StringBuilder axes = new StringBuilder();
    for (InputDevice.MotionRange r : d.getMotionRanges()) {
      axes.append(' ').append(MotionEvent.axisToString(r.getAxis()))
          .append(String.format(Locale.ROOT, "(%.2f..%.2f flat %.3f)", r.getMin(), r.getMax(), r.getFlat()));
    }
    StringBuilder keys = new StringBuilder();
    boolean[] has = d.hasKeys(WATCHED_KEYS);
    for (int i = 0; i < WATCHED_KEYS.length; i++) {
      if (has[i]) keys.append(' ').append(KeyEvent.keyCodeToString(WATCHED_KEYS[i]).replace("KEYCODE_", ""));
    }
    return String.format(Locale.ROOT, "device %d \"%s\" vendor %04x product %04x descriptor %s sources 0x%x%s%s keys:%s axes:%s",
        d.getId(), d.getName(), d.getVendorId(), d.getProductId(), d.getDescriptor(), d.getSources(),
        d.isVirtual() ? " virtual" : "", Build.VERSION.SDK_INT < 29 ? "" : d.isExternal() ? " external" : " built-in",
        keys.length() == 0 ? " none" : keys.toString(), axes.length() == 0 ? " none" : axes.toString());
  }

  /** Once per device: what it reports (name, sources, axes), for the device test's log. */
  private void noteDevice(InputDevice d) {
    if (d == null || !seenDevices.add(d.getId())) return;
    Log.i(TAG, describeDevice(d));
  }

  /** Game controllers and gamepad-like devices, for the connect lines and controllers.txt. */
  /** A real keyboard (letters), not a controller that also reports keys (the Retroid's built-in one). */
  static boolean keyboardDevice(InputDevice d) {
    if (d == null || d.isVirtual() || gameDevice(d)) return false;
    return (d.getSources() & InputDevice.SOURCE_KEYBOARD) == InputDevice.SOURCE_KEYBOARD
        && d.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC;
  }

  private static boolean gameDevice(InputDevice d) {
    if (d == null || d.isVirtual()) return false;
    int s = d.getSources();
    return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
        || (s & InputDevice.SOURCE_DPAD) == InputDevice.SOURCE_DPAD;
  }

  /** A device that is plugged in or changed: described at once, not only at its first mapped key. */
  private void noteConnected(int id) {
    InputDevice d = InputDevice.getDevice(id);
    if (gameDevice(d) && controllerLog.firstDevice(id)) Log.i(TAG, "connected: " + describeDevice(d));
  }

  /** controllers.txt of the error report: every game controller connected now. */
  private String controllerList() {
    StringBuilder out = new StringBuilder();
    for (int id : InputDevice.getDeviceIds()) {
      InputDevice d = InputDevice.getDevice(id);
      if (gameDevice(d)) out.append(describeDevice(d)).append('\n');
    }
    return out.length() == 0 ? "(no game controller connected)\n" : out.toString();
  }

  @Override
  public boolean dispatchKeyEvent(KeyEvent e) {
    int code = e.getKeyCode();
    boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
    boolean first = down && e.getRepeatCount() == 0;
    boolean keyboard = !controller(e) && !KeyEvent.isGamepadButton(code) && keyboardDevice(e.getDevice());
    int function = input.appFunctionOf(code, keyboard);
    if (function != CoreInput.APP_NONE && first) {
      String what = function == CoreInput.APP_MENU ? "menu" : tools ? "graphics capture" : "nothing (tools off)";
      Log.i(TAG, "key " + KeyEvent.keyCodeToString(code) + " (" + code + ") -> app " + what);
    }
    switch (function) {
      case CoreInput.APP_MENU:
        // pressed: the menu when let go; held for a second: the game's debug menu
        if (first) {
          if (controller(e) || keyboard) touchControls.controllerUsed();
          select(true);
        } else if (e.getAction() == KeyEvent.ACTION_UP) {
          select(false);
        }
        return true;
      case CoreInput.APP_GX_DUMP:
        // R3 (right stick pressed): the graphics data of the next pictures for the
        // diagnosis in the cloud (files/gx-dumps, test-core-app.ps1 / pull-gx-dumps.ps1 fetch them)
        if (first) capture();
        return true;
      default:
        break;
    }
    // a keyboard (the button mapping, 29.10.2026): its mapping's keys and the fixed ones go to the game;
    // Esc always (Android would make it "back" and ask to quit), the rest to Android
    if (keyboard) {
      if (e.getAction() != KeyEvent.ACTION_DOWN && e.getAction() != KeyEvent.ACTION_UP) return true;
      if (e.getRepeatCount() > 0) return true;
      boolean taken = input.keyboardKey(e.getDeviceId(), code, down);
      if (taken || code == KeyEvent.KEYCODE_ESCAPE) {
        if (down) {
          touchControls.controllerUsed();
          noteDevice(e.getDevice());
          if (controllerLog.firstKey(e.getDeviceId(), code)) {
            Log.i(TAG, "keyboard " + KeyEvent.keyCodeToString(code) + " (" + code + ", device " + e.getDeviceId()
                + ") -> GC " + CoreInput.names(input.keyboardButton(code)) + (input.menu() ? " (a game menu)" : ""));
          }
        }
        send();
        return true;
      }
      return super.dispatchKeyEvent(e);
    }
    if (controller(e) || KeyEvent.isGamepadButton(code)) {
      if (e.getAction() != KeyEvent.ACTION_DOWN && e.getAction() != KeyEvent.ACTION_UP) return true;
      if (e.getRepeatCount() > 0) return true;  // held: nothing changes
      if (input.key(e.getDeviceId(), code, down)) {
        if (down) {
          touchControls.controllerUsed();
          noteDevice(e.getDevice());
          Log.i(TAG, "key " + KeyEvent.keyCodeToString(code) + " (" + code + ", scan " + e.getScanCode()
              + ", device " + e.getDeviceId() + ") -> GC " + CoreInput.names(input.gcButton(code)));
        }
        send();
        return true;
      }
      if (CoreInput.unused(code)) {
        if (down) {
          Log.i(TAG, "key " + KeyEvent.keyCodeToString(code) + " (" + code + ") -> no function");
        }
        return true;
      }
    }
    // a controller's button without a function (L1, L3, R3 without an extra key): kept from Android,
    // which would make B "back" and ask to quit the game (device test 27.10.2026)
    boolean dpad = code == KeyEvent.KEYCODE_DPAD_UP || code == KeyEvent.KEYCODE_DPAD_DOWN
        || code == KeyEvent.KEYCODE_DPAD_LEFT || code == KeyEvent.KEYCODE_DPAD_RIGHT;
    if (controller(e) && (KeyEvent.isGamepadButton(code) || dpad)) {
      if (down && controllerLog.firstKey(e.getDeviceId(), code)) {
        Log.i(TAG, "key " + KeyEvent.keyCodeToString(code) + " (" + code + ", device " + e.getDeviceId()
            + ") -> no function, not passed on");
      }
      return true;
    }
    // a key nothing here handles (a rare controller's extra buttons): named once per device and code
    if (down && controller(e) && controllerLog.firstKey(e.getDeviceId(), code)) {
      InputDevice d = e.getDevice();
      Log.i(TAG, "key " + KeyEvent.keyCodeToString(code) + " (" + code + ", scan " + e.getScanCode() + ", device "
          + e.getDeviceId() + (d == null ? "" : " \"" + d.getName() + "\"") + ") -> not mapped, passed on to Android");
    }
    return super.dispatchKeyEvent(e);
  }

  /** An axis the game does not read moved past half its range: named once per device. */
  private void noteUnreadAxes(MotionEvent e, InputDevice d) {
    if (d == null) return;
    for (InputDevice.MotionRange r : d.getMotionRanges()) {
      int a = r.getAxis();
      if (ControllerLog.readAxis(a) || r.getRange() <= 0) continue;
      float v = e.getAxisValue(a);
      if (Math.abs(2 * (v - r.getMin()) / r.getRange() - 1) > 0.5f && controllerLog.firstAxis(e.getDeviceId(), a)) {
        Log.i(TAG, String.format(Locale.ROOT, "axis %s moved to %.2f on device %d \"%s\" -> not read by the game",
            MotionEvent.axisToString(a), v, e.getDeviceId(), d.getName()));
      }
    }
  }

  /** An axis of `e` in -1..1 (sticks, hat) or 0..1 (triggers); 0 when the device lacks it. */
  private static float axis(MotionEvent e, InputDevice d, int a, boolean trigger) {
    InputDevice.MotionRange range = d == null ? null : d.getMotionRange(a, e.getSource());
    if (range == null || range.getRange() <= 0) return 0;
    float v = e.getAxisValue(a);
    if (Float.isNaN(v) || Float.isInfinite(v)) return 0;
    return trigger ? (v - range.getMin()) / range.getRange()
        : 2 * (v - range.getMin()) / range.getRange() - 1;
  }

  @Override
  public boolean onGenericMotionEvent(MotionEvent e) {
    if (e.getAction() == MotionEvent.ACTION_MOVE && controller(e)) {
      InputDevice d = e.getDevice();
      noteDevice(d);
      // the right stick: Z/RZ on most Android controllers, RX/RY on others
      boolean zr = d != null && d.getMotionRange(MotionEvent.AXIS_Z, e.getSource()) != null
          && d.getMotionRange(MotionEvent.AXIS_RZ, e.getSource()) != null;
      float lx = axis(e, d, MotionEvent.AXIS_X, false), ly = axis(e, d, MotionEvent.AXIS_Y, false);
      float rx = axis(e, d, zr ? MotionEvent.AXIS_Z : MotionEvent.AXIS_RX, false);
      float ry = axis(e, d, zr ? MotionEvent.AXIS_RZ : MotionEvent.AXIS_RY, false);
      float l = Math.max(axis(e, d, MotionEvent.AXIS_LTRIGGER, true), axis(e, d, MotionEvent.AXIS_BRAKE, true));
      float r = Math.max(axis(e, d, MotionEvent.AXIS_RTRIGGER, true), axis(e, d, MotionEvent.AXIS_GAS, true));
      float hx = axis(e, d, MotionEvent.AXIS_HAT_X, false), hy = axis(e, d, MotionEvent.AXIS_HAT_Y, false);
      noteUnreadAxes(e, d);
      input.motion(e.getDeviceId(), lx, ly, rx, ry, l, r, hx, hy);
      // a stick, trigger or hat moved on purpose: the touch controls go again (TouchShowRule)
      if (TouchShowRule.moved(lx, ly, rx, ry, l, r, hx, hy)) touchControls.controllerUsed();
      send();
      return true;
    }
    return super.onGenericMotionEvent(e);
  }

  /**
   * A touch of the window: through the views (RetroTouch) with the touch
   * controls watching which finger looks (TouchControls).
   */
  @Override
  public boolean dispatchTouchEvent(MotionEvent e) {
    // a mouse on the picture while the game is in front: it is captured (its clicks are no taps)
    if (Build.VERSION.SDK_INT >= 26 && e.isFromSource(InputDevice.SOURCE_MOUSE) && mouseWanted()) {
      if (e.getActionMasked() == MotionEvent.ACTION_DOWN) updateMouseCapture();
      return true;
    }
    watchTap(e);
    if (touchControls == null) return super.dispatchTouchEvent(e);
    touchControls.before(e);
    boolean taken = super.dispatchTouchEvent(e);
    touchControls.after(e);
    return taken;
  }

  // A tap on the picture beside the touch controls (one finger, short, hardly moved; wish of the
  // project owner, 22.10.2026: the inventory by touch) goes to the core, which uses it in the
  // attache case (runtime/menu_touch.cpp) and ignores it elsewhere; a swipe (one finger, mostly
  // sideways, at least an eighth of the picture's width, within 700 ms) changes the sub screen's
  // tab (wish of the project owner after the first device test). A swipe in any direction, at
  // least a sixth of the picture's shorter side, draws the knife and slashes while playing, higher
  // or lower by its slope (runtime/knife_swipe.cpp; wish of the project owner, 22.10.2026; the
  // menu's switch "Knife by Swipe"); the core ignores it in sub screens. Dragging the map with a
  // finger was taken out again (device test 22.10.2026 21:22: it kept swipes from leaving the map).
  private static final long TAP_MS = 500, SWIPE_MS = 700;
  private boolean knifeSwipe = true;
  private int tapFinger = -1;
  private boolean tapMoved;
  private float tapX, tapY;
  private long tapTime;
  private final int[] surfaceAt = new int[2];

  private void watchTap(MotionEvent e) {
    int i = e.getActionIndex();
    switch (e.getActionMasked()) {
      case MotionEvent.ACTION_DOWN: {
        boolean control = touchControls != null && touchControls.onControl(e.getX(), e.getY());
        tapFinger = control ? -1 : e.getPointerId(0);
        tapMoved = false;
        tapX = e.getX();
        tapY = e.getY();
        tapTime = e.getEventTime();
        break;
      }
      case MotionEvent.ACTION_POINTER_DOWN:
        tapFinger = -1;  // a second finger: no tap, no swipe
        break;
      case MotionEvent.ACTION_MOVE: {
        int k = tapFinger >= 0 ? e.findPointerIndex(tapFinger) : -1;
        float slop = 2 * ViewConfiguration.get(this).getScaledTouchSlop();
        if (k >= 0 && Math.hypot(e.getX(k) - tapX, e.getY(k) - tapY) > slop) tapMoved = true;
        break;
      }
      case MotionEvent.ACTION_UP:
        if (tapFinger >= 0 && tapFinger == e.getPointerId(i) && surface != null) {
          long time = e.getEventTime() - tapTime;
          float dx = e.getX(i) - tapX, dy = e.getY(i) - tapY;
          if (!tapMoved && time <= TAP_MS) {
            surface.getLocationInWindow(surfaceAt);
            CoreBridge.tap(e.getX(i) - surfaceAt[0], e.getY(i) - surfaceAt[1]);
          } else if (time <= SWIPE_MS) {
            if (Math.abs(dx) >= surface.getWidth() / 8f && Math.abs(dx) >= 2 * Math.abs(dy)) {
              CoreBridge.swipe(dx < 0 ? 1 : -1);
            }
            float length = (float) Math.hypot(dx, dy);
            if (knifeSwipe && length >= Math.min(surface.getWidth(), surface.getHeight()) / 6f) {
              CoreBridge.slash(-dy / length);
            }
          }
        }
        tapFinger = -1;
        break;
      case MotionEvent.ACTION_CANCEL:
        tapFinger = -1;
        break;
      default:
        break;
    }
  }

  @Override public void onInputDeviceAdded(int id) { noteConnected(id); touchControls.update(); updateMouseCapture(); }
  @Override public void onInputDeviceChanged(int id) {
    input.remove(id);
    seenDevices.remove(id);
    controllerLog.forget(id);
    noteConnected(id);
    send();
    touchControls.update();
  }
  @Override public void onInputDeviceRemoved(int id) {
    controllerLog.forget(id);
    input.remove(id);
    send();
    touchControls.update();
  }

  /** Nothing held while the game is not in front (no stuck keys after a dialog or the home screen). */
  private void releaseAll() {
    input.clear();
    mouseButtons = 0;
    main.removeCallbacks(mouseRest);
    if (touchControls != null) touchControls.reset();
    send();
  }

  @Override
  public void onWindowFocusChanged(boolean focus) {
    super.onWindowFocusChanged(focus);
    if (!focus) releaseAll();
    else main.post(this::updateMouseCapture);
  }

  // ---- aiming by touch and controllers (the menu's "Invert vertical", "Aim speed") ----
  private boolean aimInvert;
  private float aimSpeed = 1f;

  /** The stored aim speed on the slider's steps (1 for anything else). */
  static float clampAimSpeed(float v) {
    if (Float.isNaN(v) || v < AIM_SPEED_MIN || v > AIM_SPEED_MAX) return 1f;
    return Math.round(v / AIM_SPEED_STEP) * AIM_SPEED_STEP;
  }

  // ---- the mouse ----
  private float mouseSensitivity = 1f;
  private int mouseButtons;  // the captured mouse's buttons held (MotionEvent.BUTTON_*)
  private int mouseCaptures;
  private final Runnable mouseRest = () -> {
    input.mouseLook(0, 0);
    send();
  };

  static float clampMouse(float v) {
    return Float.isNaN(v) ? 1f : Math.max(MOUSE_MIN, Math.min(MOUSE_MAX, v));
  }

  /** A real mouse is connected (not a touchpad of the screen). */
  static boolean mouseDevice(InputDevice d) {
    return d != null && !d.isVirtual() && (d.getSources() & InputDevice.SOURCE_MOUSE) == InputDevice.SOURCE_MOUSE;
  }

  private boolean mouseConnected() {
    for (int id : InputDevice.getDeviceIds()) if (mouseDevice(InputDevice.getDevice(id))) return true;
    return false;
  }

  /** The game in front, no menu, no touch editor, a mouse there: the mouse belongs to the game. */
  private boolean mouseWanted() {
    return resumed && hasWindowFocus() && menu == null && !touchEditing && mouseConnected();
  }

  /** Captures the mouse (Android 8 and later) when the game wants it, lets it go when not. */
  private void updateMouseCapture() {
    if (Build.VERSION.SDK_INT < 26 || surface == null) return;
    boolean want = mouseWanted();
    if (want && !surface.hasPointerCapture()) {
      surface.requestFocus();
      surface.requestPointerCapture();
      if (++mouseCaptures <= 20) Log.i(TAG, "mouse: captured (its buttons through the keyboard's mapping, F1 or the "
          + "app menu's key frees it), sensitivity " + mouseSensitivity);
    } else if (!want && surface.hasPointerCapture()) {
      surface.releasePointerCapture();
    }
  }

  /** An event of the captured mouse: its buttons as keys (ActionMap.MOUSE_*), its movement camera and aim. */
  private void capturedMouse(MotionEvent e) {
    int state = e.getButtonState() & 31;
    int changed = state ^ mouseButtons;
    mouseButtons = state;
    for (int bit = 1; bit <= 16; bit <<= 1) {
      if ((changed & bit) == 0) continue;
      int code = ActionMap.mouseButton(bit);
      boolean down = (state & bit) != 0;
      if (input.appFunctionOf(code, true) == CoreInput.APP_MENU) {
        select(down);
        continue;
      }
      input.keyboardKey(e.getDeviceId(), code, down);
      if (down) {
        touchControls.controllerUsed();
        if (controllerLog.firstKey(e.getDeviceId(), code)) {
          Log.i(TAG, "mouse button " + CoreMenu.keyName(code) + " -> GC " + CoreInput.names(input.keyboardButton(code))
              + (input.menu() ? " (a game menu)" : ""));
        }
      }
    }
    int action = e.getActionMasked();
    if (action == MotionEvent.ACTION_MOVE || action == MotionEvent.ACTION_HOVER_MOVE) {
      // relative counts, the batched ones too
      float dx = e.getX(), dy = e.getY();
      for (int h = 0; h < e.getHistorySize(); h++) {
        dx += e.getHistoricalX(h);
        dy += e.getHistoricalY(h);
      }
      if (dx != 0 || dy != 0) {
        // while aiming the core turns the aim by it (and ignores it otherwise); the camera's C stick as long
        // as the mouse moves
        float k = MOUSE_AIM_RADIANS * mouseSensitivity;
        CoreBridge.aimTurn(-dx * k, -dy * k);
        input.mouseLook(dx * MOUSE_LOOK * mouseSensitivity, dy * MOUSE_LOOK * mouseSensitivity);
        main.removeCallbacks(mouseRest);
        main.postDelayed(mouseRest, 60);
      }
    }
    send();
  }

  @SuppressWarnings("deprecation")
  private void setUpVibrator() {
    if (Build.VERSION.SDK_INT >= 31) {
      VibratorManager m = (VibratorManager) getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
      vibrator = m == null ? null : m.getDefaultVibrator();
    } else {
      vibrator = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
    }
    if (vibrator != null && !vibrator.hasVibrator()) vibrator = null;
    amplitudeControl = vibrator != null && Build.VERSION.SDK_INT >= 26 && vibrator.hasAmplitudeControl();
    Log.i(TAG, vibrator == null ? "rumble: no vibrator"
        : "rumble: vibrator" + (amplitudeControl ? " with amplitude control" : " (on/off only)"));
  }

  /**
   * About every 16 ms while the game is in front: the motor of the last four
   * game frames (the game pulses it for weaker rumble) as the vibrator's
   * strength; off when the game stands still.
   */
  private final Runnable rumbleTick = new Runnable() {
    @Override public void run() {
      if (destroyed || !resumed) return;
      int r = CoreBridge.rumble();
      long now = SystemClock.uptimeMillis();
      // the game shows a menu (bit 4): the button mappings give way to the devices' defaults
      boolean gameMenu = (r & 0x10) != 0;
      if (gameMenu != input.menu()) {
        input.setMenu(gameMenu);
        send();
      }
      int frames = r >>> 8;
      if (frames != rumbleFrames) {
        rumbleFrames = frames;
        rumbleSeen = now;
      }
      int level = now - rumbleSeen < 200 ? Integer.bitCount(r & 0xF) : 0;
      // A shot rumbles for 3-5 game frames only (device test 24.10.2026: nothing felt in play, only
      // the long rumble of the options screen): each start runs its pulse out at least, a stronger
      // level starts at once, a weaker one waits for the running one; no cancel when it ends.
      if (now >= rumbleUntil) {
        vibrating = false;
        rumbleLevel = 0;
      }
      if (level > 0 && (!vibrating || level > rumbleLevel || now >= rumbleUntil - 40)) {
        vibrate(level);
        rumbleLevel = level;
        rumbleUntil = now + rumblePulseMs(level);
        vibrating = true;
      }
      main.postDelayed(this, 16);
    }
  };

  @SuppressWarnings("deprecation")
  private void vibrate(int level) {
    if (vibrator == null) return;
    try {
      if (Build.VERSION.SDK_INT >= 26) {
        // a quarter of the game's motor already clearly felt (70 was too weak on the Retroid; 150 and
        // then 210 still too weak, device tests 27.10.2026: now 230 up to the strongest, and longer)
        int strength = amplitudeControl ? 230 + 25 * level / 4 : VibrationEffect.DEFAULT_AMPLITUDE;
        VibrationEffect effect = VibrationEffect.createOneShot(rumblePulseMs(level), strength);
        if (Build.VERSION.SDK_INT >= 33) {
          vibrator.vibrate(effect, VibrationAttributes.createForUsage(VibrationAttributes.USAGE_MEDIA));
        } else {
          vibrator.vibrate(effect, new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build());
        }
      } else {
        vibrator.vibrate(rumblePulseMs(level));
      }
    } catch (RuntimeException e) {
      Log.w(TAG, "rumble: " + e);
      vibrator = null;
    }
  }

  private void stopVibrator() {
    if (vibrating && vibrator != null) vibrator.cancel();
    vibrating = false;
    rumbleLevel = 0;
  }

  // every vsync of the display while the game is shown: the core's retraces
  // follow it (runtime/vi.cpp), so the pictures do not drift against 60 Hz.
  // A screen faster than 60 Hz has the picture drawn at every n-th vsync only
  // (FramePacing, GPU step c)
  private final Choreographer.FrameCallback vsyncTick = new Choreographer.FrameCallback() {
    @Override public void doFrame(long frameTimeNanos) {
      if (!resumed) return;
      CoreBridge.vsync(frameTimeNanos);
      if (pacing.vsync(frameTimeNanos) || (!paceLogged && pacing.hz() > 0)) pace();
      if (pacing.every() > 1 && ++vsyncs % pacing.every() == 0) surface.requestRender();
      Choreographer.getInstance().postFrameCallback(this);
    }
  };

  /** The screen's rate measured anew (FramePacing): the picture drawn at each vsync or at every n-th. */
  private void pace() {
    paceLogged = true;
    int every = pacing.every();
    surface.setRenderMode(every > 1 ? GLSurfaceView.RENDERMODE_WHEN_DIRTY : GLSurfaceView.RENDERMODE_CONTINUOUSLY);
    Log.i(TAG, String.format(Locale.ROOT, "display at %.1f Hz: the picture drawn %s", pacing.hz(),
        every > 1 ? "at every " + every + " vsyncs (the screen stayed faster than 60 Hz)" : "at each vsync"));
  }

  /**
   * GPU step c (19.10.2026): the game makes at most 60 pictures a second; a screen that can run
   * faster than 60 Hz (the tester's POCO X7 Pro: 120 Hz, the picture drawn 120 times a second) is
   * asked for 60 Hz: its display mode of the same size with 60 Hz where it has one, and from
   * Android 11 the surface's frame rate. A screen of 60 Hz only (the Retroid) is left as it is.
   * What the screen then does, FramePacing measures.
   */
  private void preferSixtyHz() {
    if (Build.VERSION.SDK_INT < 23) return;
    Display display = getWindowManager().getDefaultDisplay();
    Display.Mode current = display.getMode();
    Display.Mode sixty = null;
    float fastest = 0;
    StringBuilder modes = new StringBuilder();
    for (Display.Mode m : display.getSupportedModes()) {
      modes.append(String.format(Locale.ROOT, " %dx%d@%.1f", m.getPhysicalWidth(), m.getPhysicalHeight(),
          m.getRefreshRate()));
      if (m.getPhysicalWidth() != current.getPhysicalWidth() || m.getPhysicalHeight() != current.getPhysicalHeight())
        continue;
      fastest = Math.max(fastest, m.getRefreshRate());
      if (Math.abs(m.getRefreshRate() - 60f) < 1.5f) sixty = m;
    }
    askSixty = fastest > 65f;
    if (askSixty && sixty != null) {
      WindowManager.LayoutParams attributes = getWindow().getAttributes();
      attributes.preferredDisplayModeId = sixty.getModeId();
      getWindow().setAttributes(attributes);
    }
    Log.i(TAG, String.format(Locale.ROOT, "display at %.1f Hz (modes:%s)%s", current.getRefreshRate(), modes,
        !askSixty ? ": left as it is"
        : sixty != null ? ": asked for its 60 Hz mode" + (Build.VERSION.SDK_INT >= 30 ? " and 60 pictures a second" : "")
        : Build.VERSION.SDK_INT >= 30 ? ": no 60 Hz mode, asked for 60 pictures a second" : ": no 60 Hz mode"));
  }

  /** The surface's frame rate (Android 11 and later) when the screen was asked for 60 Hz. */
  private void surfaceSixtyHz(Surface s) {
    if (!askSixty || Build.VERSION.SDK_INT < 30) return;
    try {
      s.setFrameRate(60f, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
    } catch (RuntimeException e) {
      Log.w(TAG, "surface frame rate: " + e);
    }
  }

  @Override
  protected void onResume() {
    super.onResume();
    hideSystemUi();
    surface.onResume();
    resumed = true;
    // the game runs again unless the menu or the touch editor is open (a file picker of the menu came back)
    if (menuSupported && menu == null && !touchEditing) CoreBridge.pause(false, 0);
    Choreographer.getInstance().removeFrameCallback(vsyncTick);
    Choreographer.getInstance().postFrameCallback(vsyncTick);
    if (gyroAim != null) gyroAim.resume();
    InputManager im = (InputManager) getSystemService(Context.INPUT_SERVICE);
    if (im != null) im.registerInputDeviceListener(this, main);
    if (menu == null && !touchEditing) {
      main.removeCallbacks(rumbleTick);
      main.post(rumbleTick);
      audio.start();
    }
  }

  @Override
  protected void onPause() {
    resumed = false;
    // the game stands while the app is not in front (home screen, a file picker of the menu)
    if (menuSupported) CoreBridge.pause(true, 0);
    selectDown = false;
    main.removeCallbacks(selectHold);
    Choreographer.getInstance().removeFrameCallback(vsyncTick);
    pacing.restart();
    if (gyroAim != null) gyroAim.pause();
    // the sound pauses with the app (the core skips what it played meanwhile)
    audio.stop();
    main.removeCallbacks(rumbleTick);
    stopVibrator();
    InputManager im = (InputManager) getSystemService(Context.INPUT_SERVICE);
    if (im != null) im.unregisterInputDeviceListener(this);
    releaseAll();
    surface.onPause();
    super.onPause();
  }

  /**
   * The game's language with the European discs (decision of the project owner, 26.10.2026): the
   * menu's choice, else the device's (the menu's language) if the discs have it, else English.
   * 2 English, 3 German, 4 French, 5 Spanish, 6 Italian (pSys->language of the European release).
   */
  static int gameLanguage(Context context) {
    return gameLanguageOf(context.getSharedPreferences(GAME_PREFS, MODE_PRIVATE).getInt(GAME_LANGUAGE, 0));
  }

  /** The game's language of a menu choice: 2..6 as they are, 0 the device's. */
  static int gameLanguageOf(int chosen) {
    if (chosen >= 2 && chosen <= 6) return chosen;
    switch (Locale.getDefault().getLanguage()) {
      case "de": return 3;
      case "fr": return 4;
      case "es": return 5;
      case "it": return 6;
      default: return 2;
    }
  }

  /** "language de" / "language ar, right to left": the menu's language (the phone's or the app's own). */
  private String language() {
    boolean rtl = getResources().getConfiguration().getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
    return "language " + Locale.getDefault().toLanguageTag() + (rtl ? ", right to left" : "");
  }

  /** A new language keeps the game running (configChanges locale|layoutDirection); the log says so. */
  @Override
  public void onConfigurationChanged(Configuration config) {
    super.onConfigurationChanged(config);
    String now = language();
    if (!now.equals(shownLanguage)) Log.i(TAG, "configuration changed, the game keeps running: " + now);
    shownLanguage = now;
  }

  @Override
  protected void onDestroy() {
    destroyed = true;
    if (isFinishing()) CoreLog.stop();
    main.removeCallbacksAndMessages(null);
    rooms.shutdownNow();
    super.onDestroy();
  }
}
