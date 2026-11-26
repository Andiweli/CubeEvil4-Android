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
    shadow.sendSensorEventToListeners(g);
    assertTrue(logged("gyro aiming: gravity x 0.2, y 6.9, z 6.9 m/s2"));
    assertTrue(logged("0.2 across, 6.9 up/down, 6.9 out"));
    // a clear turn up (about the screen's horizontal axis, the device's x at rotation 0)
    SensorEvent t = ShadowSensorManager.createSensorEvent(3, Sensor.TYPE_GYROSCOPE);
    t.values[0] = 2f;
    t.timestamp = SystemClock.elapsedRealtimeNanos();
    shadow.sendSensorEventToListeners(t);
    assertTrue(logged("gyro aiming: device turn: x 2.00, y 0.00, z 0.00 rad/s, mostly x; screen rotation 0: "
        + "left/right 0.00, up/down 2.00"));
    assertEquals(0f, Bridge.yaw, 0f);
    assertTrue(Bridge.pitch > 0f);
    controller.pause().stop().destroy();
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
