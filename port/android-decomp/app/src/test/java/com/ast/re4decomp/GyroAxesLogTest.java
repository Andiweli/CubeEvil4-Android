package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorManager;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;
import org.robolectric.shadows.ShadowLog;
import org.robolectric.shadows.ShadowSensor;
import org.robolectric.shadows.ShadowSensorManager;

/**
 * The gyro's axes (1.1.5, a user's report, Anbernic RG477M: up/down turns the aim sideways, left/right up and down):
 * GyroAxes takes the gyroscope's axes as the device's natural orientation's and turns them by the screen's rotation;
 * GyroAim writes the sensor, the rotation, the natural orientation and gravity into the log when it starts listening,
 * and the first clear turns with their raw axes, so the next report tells a sensor a quarter turn apart.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = android.app.Application.class, shadows = GyroAxesLogTest.Bridge.class)
public class GyroAxesLogTest {
  @Implements(CoreBridge.class)
  public static class Bridge {
    static float yaw, pitch;
    @Implementation public static void gyro(float y, float p) { yaw = y; pitch = p; }
  }

  @Test public void axesByRotation() {
    // the device turned left like a camera (about the screen's vertical axis) and tilted up (about its horizontal
    // axis), as each rotation's screen shows them: the reading in the natural orientation's axes
    float r = 2f;
    // rotation 0: the screen's up is the device's y, its right the device's x
    assertEquals(r, GyroAxes.yaw(0, new float[] {0, r, 0}), 0f);
    assertEquals(r, GyroAxes.pitch(0, new float[] {r, 0, 0}), 0f);
    // rotation 90 (a portrait device held in landscape, turned counter-clockwise): the screen's up is the device's x,
    // its right the device's -y
    assertEquals(r, GyroAxes.yaw(1, new float[] {r, 0, 0}), 0f);
    assertEquals(r, GyroAxes.pitch(1, new float[] {0, -r, 0}), 0f);
    assertEquals(0f, GyroAxes.pitch(1, new float[] {r, 0, 0}), 0f);  // a left turn does not move the aim up or down
    // rotation 270: the other way round
    assertEquals(r, GyroAxes.yaw(3, new float[] {-r, 0, 0}), 0f);
    assertEquals(r, GyroAxes.pitch(3, new float[] {0, r, 0}), 0f);
    // the axis through the screen does nothing
    for (int rot = 0; rot < 4; rot++) {
      assertEquals(0f, GyroAxes.yaw(rot, new float[] {0, 0, r}), 0f);
      assertEquals(0f, GyroAxes.pitch(rot, new float[] {0, 0, r}), 0f);
    }
  }

  @Test public void logsSensorRotationGravityAndTurns() {
    ShadowLog.clear();
    ActivityController<Activity> controller = Robolectric.buildActivity(Activity.class).setup();
    Activity activity = controller.get();
    controller.windowFocusChanged(true);
    SensorManager sensors = (SensorManager) activity.getSystemService(Context.SENSOR_SERVICE);
    ShadowSensorManager shadow = Shadows.shadowOf(sensors);
    shadow.addSensor(ShadowSensor.newInstance(Sensor.TYPE_GYROSCOPE));
    shadow.addSensor(ShadowSensor.newInstance(Sensor.TYPE_ACCELEROMETER));
    GyroAim aim = new GyroAim(activity, new Handler(Looper.getMainLooper()));
    aim.setEnabled(true);
    aim.resume();
    assertTrue(logged("gyro aiming: device gyroscope "));
    assertTrue(logged("screen rotation 0 degrees"));
    assertTrue(logged("natural orientation "));
    // held normally, tilted back: gravity along the screen's up/down and out of it
    SensorEvent g = ShadowSensorManager.createSensorEvent(3, Sensor.TYPE_ACCELEROMETER);
    g.values[0] = 0.2f;
    g.values[1] = 6.9f;
    g.values[2] = 6.9f;
    gravity(shadow, g);
    assertTrue(logged("gyro aiming: gravity x 0.2, y 6.9, z 6.9 m/s2"));
    assertTrue(logged("0.2 across, 6.9 up/down, 6.9 out"));
    assertTrue(logged("gyro aiming: the sensor's axes are the natural orientation's (not turned)"));
    // a clear turn up (about the screen's horizontal axis, the device's x at rotation 0)
    SensorEvent t = ShadowSensorManager.createSensorEvent(3, Sensor.TYPE_GYROSCOPE);
    t.values[0] = 2f;
    t.timestamp = SystemClock.elapsedRealtimeNanos();
    shadow.sendSensorEventToListeners(t);
    assertTrue(logged("gyro aiming: device turn: x 2.00, y 0.00, z 0.00 rad/s, mostly x; sensor turn 0, screen rotation 0: "
        + "left/right 0.00, up/down 2.00"));
    assertEquals(0f, Bridge.yaw, 0f);
    assertTrue(Bridge.pitch > 0f);
    controller.pause().stop().destroy();
  }

  /** 1.1.6: the sensor's turn by gravity (the user's RG477M: x 9.8, y 0.0, z 1.2 held normally, rotation 0). */
  @Test public void sensorTurnByGravity() {
    // held normally (upright or tilted back): not turned, at every rotation of the screen
    for (int rot = 0; rot < 4; rot++) {
      // the same gravity as the screen at `rot` shows it upright, in the device's axes
      assertEquals(0, GyroAxes.sensorTurn(rot, deviceAxes(rot, 0f, 6.9f, 6.9f)));
    }
    // the RG477M: gravity along the screen's across axis (to its right): the sensor lies a quarter turn clockwise,
    // turned back one quarter counter-clockwise
    assertEquals(1, GyroAxes.sensorTurn(0, new float[] {9.8f, 0f, 1.2f}));
    assertEquals(3, GyroAxes.sensorTurn(0, new float[] {-9.8f, 0f, 1.2f}));
    assertEquals(2, GyroAxes.sensorTurn(0, new float[] {0f, -9.8f, 1.2f}));
    // lying flat, or between two quarter turns: gravity does not tell
    assertEquals(GyroAxes.TURN_UNKNOWN, GyroAxes.sensorTurn(0, new float[] {0.5f, 1f, 9.7f}));
    assertEquals(GyroAxes.TURN_UNKNOWN, GyroAxes.sensorTurn(0, new float[] {6.5f, 6.5f, 2f}));
    // a turned sensor and the screen's rotation together: the same device turned by every rotation, its sensor a
    // quarter turn off; turned back by gravity, a left turn turns left and a tilt up looks up
    for (int rot = 0; rot < 4; rot++) {
      for (int mount = 0; mount < 4; mount++) {
        float[] grav = sensor(mount, deviceAxes(rot, 0f, 9.3f, 3f));
        int q = GyroAxes.sensorTurn(rot, grav);
        assertEquals("rotation " + rot + " mount " + mount, mount, q);
        float[] left = sensor(mount, deviceAxes(rot, 0f, 2f, 0f)), up = sensor(mount, deviceAxes(rot, 2f, 0f, 0f));
        float[] t = new float[3];
        GyroAxes.turn(q, left, t);
        assertEquals(2f, GyroAxes.yaw(rot, t), 1e-5f);
        assertEquals(0f, GyroAxes.pitch(rot, t), 1e-5f);
        GyroAxes.turn(q, up, t);
        assertEquals(0f, GyroAxes.yaw(rot, t), 1e-5f);
        assertEquals(2f, GyroAxes.pitch(rot, t), 1e-5f);
      }
    }
  }

  /** The user's log (RG477M, rotation 0): gravity x 9.8, a turn mostly about x now turns left/right, not up/down. */
  @Test public void turnedSensorAims() {
    ShadowLog.clear();
    ActivityController<Activity> controller = Robolectric.buildActivity(Activity.class).setup();
    Activity activity = controller.get();
    controller.windowFocusChanged(true);
    SensorManager sensors = (SensorManager) activity.getSystemService(Context.SENSOR_SERVICE);
    ShadowSensorManager shadow = Shadows.shadowOf(sensors);
    shadow.addSensor(ShadowSensor.newInstance(Sensor.TYPE_GYROSCOPE));
    shadow.addSensor(ShadowSensor.newInstance(Sensor.TYPE_ACCELEROMETER));
    GyroAim aim = new GyroAim(activity, new Handler(Looper.getMainLooper()));
    aim.setEnabled(true);
    aim.resume();
    SensorEvent t = ShadowSensorManager.createSensorEvent(3, Sensor.TYPE_GYROSCOPE);
    t.values[0] = 1.51f;
    t.values[1] = 0.21f;
    t.values[2] = 0.15f;
    // before gravity is known the aim waits
    Bridge.yaw = Bridge.pitch = 9f;
    t.timestamp = SystemClock.elapsedRealtimeNanos();
    shadow.sendSensorEventToListeners(t);
    assertEquals(0f, Bridge.yaw, 0f);
    assertEquals(0f, Bridge.pitch, 0f);
    SensorEvent g = ShadowSensorManager.createSensorEvent(3, Sensor.TYPE_ACCELEROMETER);
    g.values[0] = 9.8f;
    g.values[1] = 0f;
    g.values[2] = 1.2f;
    gravity(shadow, g);
    assertTrue(logged("9.8 across, 0.0 up/down, 1.2 out"));
    assertTrue(logged("gyro aiming: the sensor's axes lie 90 degrees from the natural orientation's: turned back"));
    t.timestamp = SystemClock.elapsedRealtimeNanos();
    shadow.sendSensorEventToListeners(t);
    assertTrue(logged("gyro aiming: device turn: x 1.51, y 0.21, z 0.15 rad/s, mostly x; sensor turn 90, "
        + "screen rotation 0: left/right 1.51, up/down -0.21"));
    assertTrue(Bridge.yaw > 0.4f);
    assertTrue(Math.abs(Bridge.pitch) < 0.1f);
    controller.pause().stop().destroy();
  }

  /** A gravity (screen's terms: right, up, out at rotation `rot`) in the natural orientation's axes. */
  private static float[] deviceAxes(int rot, float right, float up, float out) {
    // yaw/pitch(rot, v) give (up, right) from v; their inverse, found among the four quarter turns
    float[][] cand = {{right, up, out}, {-up, right, out}, {-right, -up, out}, {up, -right, out}};
    for (float[] v : cand) {
      if (GyroAxes.pitch(rot, v) == right && GyroAxes.yaw(rot, v) == up) return v;
    }
    throw new AssertionError("no inverse");
  }

  /** v (natural orientation's axes) as a sensor built in `mount` quarter turns counter-clockwise reports it. */
  private static float[] sensor(int mount, float[] v) {
    float[] out = new float[3];
    GyroAxes.turn((4 - mount) & 3, v, out);
    return out;
  }

  private static void gravity(ShadowSensorManager shadow, SensorEvent g) {
    for (int i = 0; i < 5; i++) shadow.sendSensorEventToListeners(g);
  }

  private static boolean logged(String part) {
    List<ShadowLog.LogItem> items = ShadowLog.getLogsForTag(CoreActivity.TAG);
    for (ShadowLog.LogItem i : items) {
      if (i.msg != null && i.msg.contains(part)) return true;
    }
    StringBuilder all = new StringBuilder();
    for (ShadowLog.LogItem i : items) all.append("\n  ").append(i.msg);
    System.err.println("not in the log: " + part + all);
    return false;
  }
}
