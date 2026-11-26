package com.ast.re4decomp;

import android.app.Activity;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Handler;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;

/**
 * Gyro aiming of the core mode (wish of the project owner, 19.10.2026: as in
 * the first Android port, ControllerActivity / leon_preview.cpp aimTick): the
 * gyroscope's turn rates go to the core (CoreBridge.gyro), which turns the aim
 * by them only while Leon aims a gun, not before (port/whole/runtime). The
 * axes (GyroAxes): pitch about the screen's horizontal axis, yaw about its
 * vertical axis (turning the device like a camera; until 20.10.2026 the axis
 * through the screen, a steering wheel). The filter is the first port's:
 * smoothed (35 % of each new reading), below 0.006 rad/s nothing, a reading
 * older than 150 ms or not a number: nothing. Switched by the menu ("Use
 * Gyro-Aiming"); off while the app is not in front. The menu's slider
 * (setSensitivity, wish of the project owner, 22.10.2026) multiplies the
 * filtered rates by MIN_SENSITIVITY..MAX_SENSITIVITY (1: as before). The core
 * turns the aim by 35 % of them (runtime/gyro.cpp kGain), so 3x turns the aim
 * about as far as the device; since 1.1.4 up to 6x, twice as far (a user's
 * report, the project owner: "Können wir die Empfindlichkeit für den Gyro
 * verdoppeln?"); 0.5x..3x mean what they meant before.
 *
 * <p>Since 1.1.0 (a DualSense user's wish, the project owner's choice "Gyro A", 19.11.2026) the gyroscope can also be
 * a controller's own (Android 12 and later: InputDevice.getSensorManager, as Dolphin reads it): the menu's "Gyro
 * from" Auto (the last used controller's when it has one, else this device's), Device, Controller. A controller's
 * axes are its own (held in both hands: x through its sides = pitch, y up = yaw), the screen's rotation does not
 * turn them. Its first readings and the first clear turns go into the log, so a tester can confirm the directions.
 */
final class GyroAim implements SensorEventListener {
  private final Activity activity;
  private final SensorManager sensors;
  private final Sensor gyroscope;
  private final Handler handler;
  /** The menu's "Gyro from": AUTO the controller's when it has one, else this device's. */
  static final int SOURCE_AUTO = 0, SOURCE_DEVICE = 1, SOURCE_CONTROLLER = 2;
  private int source = SOURCE_AUTO;
  // the last used controller's own gyroscope (Android 12 and later), null: none
  private SensorManager padSensors;
  private Sensor padGyro;
  private String padName = "";
  // what listens now: the manager and the sensor registered, whether they are a controller's
  private SensorManager listenManager;
  private Sensor listenSensor;
  private boolean listenPad;
  private int padLines, padTurnLines;
  // 1.1.6: this device's sensors' quarter turns from the natural orientation (GyroAxes.sensorTurn), from gravity when
  // the listening starts; kept while gravity does not tell (lying flat). turnDecided false: the aim waits for it (at
  // most TURN_WAIT_NS)
  private int sensorTurn;
  private boolean turnDecided = true;
  private long listenSinceNs;
  private SensorEventListener gravityListener;
  private final float[] turned = new float[3];
  private static final int GRAVITY_READINGS = 5;
  private static final long TURN_WAIT_NS = 600_000_000L;
  /** The slider's range and steps (1: the first port's gain). */
  static final float MIN_SENSITIVITY = 0.5f, MAX_SENSITIVITY = 6f, SENSITIVITY_STEP = 0.25f;
  private boolean enabled, listening, resumed;
  private float yaw, pitch;
  private volatile float sensitivity = 1f;
  // the menu's "Invert vertical" / "Invert horizontal": -1 turns that rate round
  private volatile float pitchSign = 1f, yawSign = 1f;

  GyroAim(Activity activity, Handler handler) {
    this.activity = activity;
    this.handler = handler;
    sensors = (SensorManager) activity.getSystemService(Context.SENSOR_SERVICE);
    gyroscope = sensors == null ? null : sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
  }

  /** The device has a gyroscope, or (Android 12 and later) a connected controller has one. */
  static boolean available(Context context) {
    SensorManager s = (SensorManager) context.getSystemService(Context.SENSOR_SERVICE);
    return (s != null && s.getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null) || controllerAvailable();
  }

  /** A connected controller with its own gyroscope (Android 12 and later). */
  static boolean controllerAvailable() {
    if (Build.VERSION.SDK_INT < 31) return false;
    for (int id : InputDevice.getDeviceIds()) {
      if (padGyroOf(InputDevice.getDevice(id)) != null) return true;
    }
    return false;
  }

  /** The controller's own gyroscope, or null (no controller, none, before Android 12). */
  static Sensor padGyroOf(InputDevice d) {
    if (Build.VERSION.SDK_INT < 31 || d == null || d.isVirtual()) return null;
    int src = d.getSources();
    if ((src & InputDevice.SOURCE_GAMEPAD) != InputDevice.SOURCE_GAMEPAD
        && (src & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK) return null;
    try {
      SensorManager m = d.getSensorManager();
      return m == null ? null : m.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
    } catch (RuntimeException e) {
      return null;
    }
  }

  /** The menu's "Gyro from" (SOURCE_*); from the next reading. */
  void setSource(int value) {
    source = clampSource(value);
    Log.i(CoreActivity.TAG, "gyro aiming: gyro from " + sourceName(source));
    update();
  }

  static int clampSource(int value) {
    return value < SOURCE_AUTO || value > SOURCE_CONTROLLER ? SOURCE_AUTO : value;
  }

  static String sourceName(int s) {
    return s == SOURCE_DEVICE ? "the device" : s == SOURCE_CONTROLLER ? "the controller" : "auto";
  }

  /** The controller used last (CoreActivity.chooseRumble), or null: its gyroscope, if it has one. */
  void setController(InputDevice d) {
    Sensor g = padGyroOf(d);
    SensorManager m = null;
    if (g != null && Build.VERSION.SDK_INT >= 31) {
      try {
        m = d.getSensorManager();
      } catch (RuntimeException e) {
        g = null;
      }
    }
    String name = d == null ? "" : d.getName();
    if (g == padGyro && name.equals(padName)) return;
    padSensors = m;
    padGyro = g;
    padName = name;
    padLines = padTurnLines = 0;
    if (d != null && Build.VERSION.SDK_INT >= 31) {
      Log.i(CoreActivity.TAG, "gyro aiming: controller \"" + name + "\" " + (g == null ? "has no gyroscope"
          : "has a gyroscope: \"" + g.getName() + "\" by " + g.getVendor() + ", range " + g.getMaximumRange()
          + " rad/s, resolution " + g.getResolution()));
    }
    update();
  }
  /** A stored value within the slider's range (1 for anything else). */
  static float clampSensitivity(float value) {
    if (Float.isNaN(value) || value < MIN_SENSITIVITY || value > MAX_SENSITIVITY) return 1f;
    return Math.round(value / SENSITIVITY_STEP) * SENSITIVITY_STEP;
  }

  /** The menu's slider: the turn rates times `value`. */
  void setSensitivity(float value) {
    sensitivity = clampSensitivity(value);
    Log.i(CoreActivity.TAG, "gyro aiming: sensitivity " + sensitivity);
  }

  /** The menu's switches "Invert vertical" (the pitch) and "Invert horizontal" (the yaw): from the next reading. */
  void setInvert(boolean vertical, boolean horizontal) {
    pitchSign = vertical ? -1f : 1f;
    yawSign = horizontal ? -1f : 1f;
    Log.i(CoreActivity.TAG, "gyro aiming: vertical " + (vertical ? "inverted" : "normal") + ", horizontal "
        + (horizontal ? "inverted" : "normal"));
  }

  /** The menu's switch (a controller's gyroscope may come later: the switch is kept). */
  void setEnabled(boolean on) {
    enabled = on;
    Log.i(CoreActivity.TAG, "gyro aiming " + (enabled ? "on" : "off")
        + (gyroscope == null ? " (no gyroscope in this device)" : ""));
    update();
  }

  void resume() {
    resumed = true;
    update();
  }

  void pause() {
    resumed = false;
    update();
  }

  private void update() {
    // the sensor by "Gyro from": a controller's own (Auto prefers it), else this device's
    boolean usePad = padGyro != null && source != SOURCE_DEVICE;
    SensorManager m = usePad ? padSensors : source == SOURCE_CONTROLLER ? null : sensors;
    Sensor g = usePad ? padGyro : source == SOURCE_CONTROLLER ? null : gyroscope;
    boolean want = enabled && resumed && m != null && g != null;
    if (want && listening && (m != listenManager || g != listenSensor)) {
      unregister();
      listening = false;
    }
    if (want == listening) return;
    listening = want;
    if (want) {
      listenManager = m;
      listenSensor = g;
      listenPad = usePad;
      m.registerListener(this, g, SensorManager.SENSOR_DELAY_GAME, handler);
      Log.i(CoreActivity.TAG, "gyro aiming: listening to " + (usePad ? "the controller \"" + padName + "\"" : "this device"));
      if (!usePad) logDeviceAxes(g);
    } else {
      unregister();
      still();
      if (enabled && resumed && source == SOURCE_CONTROLLER) {
        Log.i(CoreActivity.TAG, "gyro aiming: gyro from the controller, but no controller with a gyroscope used yet");
      }
    }
  }

  private void unregister() {
    if (listenManager != null) listenManager.unregisterListener(this);
    if (gravityListener != null && sensors != null) sensors.unregisterListener(gravityListener);
    gravityListener = null;
    turnDecided = true;
    listenManager = null;
    listenSensor = null;
    listenPad = false;
  }

  private void still() {
    yaw = pitch = 0;
    CoreBridge.gyro(0, 0);
  }

  @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}

  /**
   * 1.1.5 (a user's report, Anbernic RG477M: up/down turns the aim sideways and left/right up and down): the device's
   * gyroscope, the screen's rotation, the device's natural orientation and one reading of gravity, into the log when
   * the gyro aiming starts listening to this device. GyroAxes takes the gyroscope's axes as the device's natural
   * orientation's (Android's rule); a sensor that reports them a quarter turn apart (a panel built in turned, its
   * rotation set in the system, the sensor left as it was) shows here: held normally, gravity lies along the screen's
   * left/right instead of its up/down.
   */
  private void logDeviceAxes(Sensor g) {
    android.view.Display d = activity.getWindowManager().getDefaultDisplay();
    int rotation = d.getRotation();
    android.graphics.Point size = new android.graphics.Point();
    d.getRealSize(size);
    boolean turned = (rotation & 1) == 1;
    int naturalW = turned ? size.y : size.x, naturalH = turned ? size.x : size.y;
    Log.i(CoreActivity.TAG, "gyro aiming: device gyroscope \"" + g.getName() + "\" by " + g.getVendor()
        + "; screen rotation " + (rotation * 90) + " degrees, " + size.x + "x" + size.y + " now; natural orientation "
        + (naturalW >= naturalH ? "landscape" : "portrait") + " (" + naturalW + "x" + naturalH + ")");
    final Sensor acc = sensors == null ? null : sensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
    if (acc == null) {
      Log.i(CoreActivity.TAG, "gyro aiming: no accelerometer, the gyroscope's axes taken as they are"
          + (sensorTurn != 0 ? " (turned " + (sensorTurn * 90) + " degrees as before)" : ""));
      return;
    }
    // 1.1.6: the mean of the first readings of gravity decides whether this device's sensors lie a quarter turn from
    // the natural orientation (GyroAxes.sensorTurn); the aim waits for it
    turnDecided = false;
    listenSinceNs = SystemClock.elapsedRealtimeNanos();
    gravityListener = new SensorEventListener() {
      private final float[] sum = new float[3];
      private int n;

      @Override public void onSensorChanged(SensorEvent e) {
        if (gravityListener != this || e.values.length < 3) return;
        if (e.sensor != null && e.sensor.getType() != Sensor.TYPE_ACCELEROMETER) return;
        for (int i = 0; i < 3; i++) sum[i] += e.values[i];
        if (++n < GRAVITY_READINGS) return;
        sensors.unregisterListener(this);
        gravityListener = null;
        float[] v = {sum[0] / n, sum[1] / n, sum[2] / n};
        // gravity in the screen's terms, as GyroAxes turns the gyroscope: right and up of the screen as shown
        float right = GyroAxes.pitch(rotation, v), up = GyroAxes.yaw(rotation, v);
        Log.i(CoreActivity.TAG, String.format(java.util.Locale.ROOT,
            "gyro aiming: gravity x %.1f, y %.1f, z %.1f m/s2 (the device's axes); along the screen: %.1f across, "
                + "%.1f up/down, %.1f out (held normally: mostly up/down or out; mostly across: the sensor's axes "
                + "a quarter turn from the screen's)", v[0], v[1], v[2], right, up, v[2]));
        int q = GyroAxes.sensorTurn(rotation, v);
        if (q != GyroAxes.TURN_UNKNOWN) sensorTurn = q;
        turnDecided = true;
        Log.i(CoreActivity.TAG, q == GyroAxes.TURN_UNKNOWN
            ? "gyro aiming: gravity does not tell the sensor's turn (lying flat?), kept: " + (sensorTurn * 90) + " degrees"
            : q == 0 ? "gyro aiming: the sensor's axes are the natural orientation's (not turned)"
            : "gyro aiming: the sensor's axes lie " + (q * 90) + " degrees from the natural orientation's: "
                + "turned back by gravity (1.1.6)");
      }

      @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}
    };
    sensors.registerListener(gravityListener, acc, SensorManager.SENSOR_DELAY_GAME, handler);
  }

  /** This device's first clear turns into the log: the raw axes and what GyroAxes made of them. */
  private int deviceTurnLines;
  private void noteDevice(float[] v, int rotation, float[] t) {
    if (v.length < 3 || deviceTurnLines >= 12) return;
    float ax = Math.abs(v[0]), ay = Math.abs(v[1]), az = Math.abs(v[2]);
    float big = Math.max(ax, Math.max(ay, az));
    if (big <= 1.5f) return;
    deviceTurnLines++;
    Log.i(CoreActivity.TAG, String.format(java.util.Locale.ROOT,
        "gyro aiming: device turn: x %.2f, y %.2f, z %.2f rad/s, mostly %s; sensor turn %d, screen rotation %d: "
            + "left/right %.2f, up/down %.2f", v[0], v[1], v[2], big == ax ? "x" : big == ay ? "y" : "z",
        sensorTurn * 90, rotation * 90, GyroAxes.yaw(rotation, t), GyroAxes.pitch(rotation, t)));
  }

  /** The controller's first readings and its first clear turns into the log (which axis moves for which turn). */
  private void notePad(float[] v) {
    if (v.length < 3) return;
    if (padLines < 5) {
      padLines++;
      Log.i(CoreActivity.TAG, String.format(java.util.Locale.ROOT,
          "gyro aiming: controller reading %d: x %.3f, y %.3f, z %.3f rad/s", padLines, v[0], v[1], v[2]));
    }
    float ax = Math.abs(v[0]), ay = Math.abs(v[1]), az = Math.abs(v[2]);
    float big = Math.max(ax, Math.max(ay, az));
    if (big > 1.5f && padTurnLines < 12) {
      padTurnLines++;
      String axis = big == ax ? "x (taken as up/down)" : big == ay ? "y (taken as left/right)" : "z (not used)";
      float value = big == ax ? v[0] : big == ay ? v[1] : v[2];
      Log.i(CoreActivity.TAG, String.format(java.util.Locale.ROOT,
          "gyro aiming: controller turn: x %.2f, y %.2f, z %.2f rad/s, mostly %s, %s", v[0], v[1], v[2], axis,
          value > 0 ? "positive" : "negative"));
    }
  }

  // Float.isFinite needs Android 7.0; the app runs from 6.0
  private static boolean finite(float v) { return !Float.isNaN(v) && !Float.isInfinite(v); }

  @Override public void onSensorChanged(SensorEvent event) {
    if (event.sensor != null && event.sensor.getType() != Sensor.TYPE_GYROSCOPE) return;  // (only the gyroscope's)
    if (!activity.hasWindowFocus()) {
      still();
      return;
    }
    // the gyroscope's axes are the device's natural orientation's (after the sensor's own turn, 1.1.6), the aim's the
    // screen's; a controller's are its own (the screen's rotation does not turn it)
    boolean pad = listenPad;
    if (event.values.length < 3) return;
    float[] v = event.values;
    if (pad) {
      notePad(v);
    } else {
      if (!turnDecided) {
        if (SystemClock.elapsedRealtimeNanos() - listenSinceNs < TURN_WAIT_NS) {
          still();
          return;
        }
        turnDecided = true;
        Log.i(CoreActivity.TAG, "gyro aiming: no gravity reading in time, the sensor's turn kept: "
            + (sensorTurn * 90) + " degrees");
      }
      GyroAxes.turn(sensorTurn, v, turned);
      v = turned;
    }
    int rotation = pad ? 0 : activity.getWindowManager().getDefaultDisplay().getRotation();
    if (!pad) noteDevice(event.values, rotation, v);
    float p = GyroAxes.pitch(rotation, v);
    float y = GyroAxes.yaw(rotation, v);
    if (SystemClock.elapsedRealtimeNanos() - event.timestamp > 150_000_000L || !finite(y) || !finite(p)) {
      still();
      return;
    }
    yaw += .35f * (y - yaw);
    pitch += .35f * (p - pitch);
    float s = sensitivity;
    CoreBridge.gyro(Math.abs(yaw) < .006f ? 0 : yaw * s * yawSign, Math.abs(pitch) < .006f ? 0 : pitch * s * pitchSign);
  }
}
