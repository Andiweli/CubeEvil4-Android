package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;

import android.app.Activity;
import android.content.Context;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.hardware.Sensor;
import android.hardware.SensorManager;
import android.view.View;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Locale;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;
import org.robolectric.shadows.ShadowSensor;

/**
 * The gyro's sensitivity (1.1.4, a user's report, the project owner: "Können wir die Empfindlichkeit für den Gyro
 * verdoppeln?"): the slider goes up to 6x, what 0.5x..3x meant stays. With RE4_MENU_SHOTS set to a folder the controls
 * page with the gyro at 6x as pictures (RE4_MENU_SHOTS_LANGUAGES: "en,de" ...).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, application = android.app.Application.class, shadows = CoreMenuLayoutTest.Bridge.class,
    qualifiers = "w960dp-h540dp-land-mdpi")
public class GyroSensitivityTest {
  @Test public void rangeUpToSixTimes() {
    assertEquals(0.5f, GyroAim.MIN_SENSITIVITY, 0f);
    assertEquals(6f, GyroAim.MAX_SENSITIVITY, 0f);
    assertEquals(0.25f, GyroAim.SENSITIVITY_STEP, 0f);
    // the values a player saved before stay what they were
    for (float v : new float[] {0.5f, 1f, 1.25f, 2f, 2.75f, 3f}) assertEquals(v, GyroAim.clampSensitivity(v), 0f);
    // the new ones
    assertEquals(3.25f, GyroAim.clampSensitivity(3.25f), 0f);
    assertEquals(4.5f, GyroAim.clampSensitivity(4.5f), 0f);
    assertEquals(6f, GyroAim.clampSensitivity(6f), 0f);
    assertEquals(5f, GyroAim.clampSensitivity(5.1f), 0f);  // onto the slider's steps
    // outside the range or not a number: 1, as before
    assertEquals(1f, GyroAim.clampSensitivity(6.25f), 0f);
    assertEquals(1f, GyroAim.clampSensitivity(0.25f), 0f);
    assertEquals(1f, GyroAim.clampSensitivity(Float.NaN), 0f);
    assertEquals("6×", CoreMenu.times(6f));
    assertEquals("5.75×", CoreMenu.times(5.75f));
  }

  @Test public void controlsPagePictures() throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    if (folder == null || folder.isEmpty()) return;
    String only = System.getenv("RE4_MENU_SHOTS_LANGUAGES");
    String[] languages = only != null && !only.isEmpty() ? only.split(",") : new String[] {"en", "de"};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.TRUE
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
            : method.getReturnType() == double.class ? (Object) 60.0 : null);
    new File(folder).mkdirs();
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
    show.setAccessible(true);
    for (String language : languages) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        SensorManager sensors = (SensorManager) activity.getSystemService(Context.SENSOR_SERVICE);
        Shadows.shadowOf(sensors).addSensor(ShadowSensor.newInstance(Sensor.TYPE_GYROSCOPE));
        activity.getSharedPreferences(CoreActivity.INPUT_PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean(CoreActivity.GYRO_AIM, true).putFloat(CoreActivity.GYRO_SENSITIVITY, 6f).commit();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language));
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        View root = (View) CoreMenuLayoutTest.field(menu, "root"), body = (View) CoreMenuLayoutTest.field(menu, "body");
        root.setLayoutDirection(config.getLayoutDirection());
        show.invoke(menu, 3, false);  // (CoreMenu.CONTROLS, private)
        for (int width : new int[] {900, 360}) {
          body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
              View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
          body.layout(0, 0, width, body.getMeasuredHeight());
          Bitmap picture = Bitmap.createBitmap(width, body.getMeasuredHeight(), Bitmap.Config.ARGB_8888);
          Canvas canvas = new Canvas(picture);
          canvas.drawColor(0xff191f1c);
          body.draw(canvas);
          try (FileOutputStream out = new FileOutputStream(new File(folder, language + "-gyro-6x-" + width + ".png"))) {
            picture.compress(Bitmap.CompressFormat.PNG, 100, out);
          }
        }
      }
    }
  }
}
