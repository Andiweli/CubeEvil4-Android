package com.ast.re4decomp;

import android.app.Activity;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Locale;
import org.junit.Assume;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

/**
 * The button mapping page (1.1.3: with "Keyboard & mouse" the reload switch and the mouse's sensitivity side by side
 * under "Device") as pictures, keyboard and controller, a handheld on its side (960 dp) and a phone upright (360 dp):
 * only with RE4_MENU_SHOTS set to a folder (RE4_MENU_SHOTS_LANGUAGES: "en,de" ...).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, application = android.app.Application.class, shadows = CoreMenuLayoutTest.Bridge.class,
    qualifiers = "w960dp-h540dp-land-mdpi")
public class MapPageShotTest {
  @Test public void buttonMappingPictures() throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    Assume.assumeTrue(folder != null && !folder.isEmpty());
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
    Field kind = CoreMenu.class.getDeclaredField("mapKind");
    kind.setAccessible(true);
    for (String language : languages) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language));
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        View root = (View) CoreMenuLayoutTest.field(menu, "root"), body = (View) CoreMenuLayoutTest.field(menu, "body");
        root.setLayoutDirection(config.getLayoutDirection());
        for (int k : new int[] {ActionMap.KIND_KEYBOARD, ActionMap.KIND_CONTROLLER}) {
          kind.setInt(menu, k);
          show.invoke(menu, 7, false);  // (CoreMenu.CONTROLLER, private)
          for (int width : new int[] {960, 360}) {
            body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
            body.layout(0, 0, width, body.getMeasuredHeight());
            int height = Math.min(body.getMeasuredHeight(), 900);
            Bitmap picture = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(picture);
            canvas.drawColor(0xff191f1c);
            body.draw(canvas);
            String name = language + "-mapping-" + (k == ActionMap.KIND_KEYBOARD ? "keyboard" : "controller") + "-"
                + width + ".png";
            try (FileOutputStream out = new FileOutputStream(new File(folder, name))) {
              picture.compress(Bitmap.CompressFormat.PNG, 100, out);
            }
          }
        }
      }
    }
  }
}
