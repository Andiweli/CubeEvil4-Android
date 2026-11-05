package com.ast.re4decomp;

import android.app.Activity;
import android.app.Application;
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
 * Pictures of the menu's pages (PNG, the whole scrolled body) in every language, to look at the layout
 * without the game: only with the environment variable RE4_MENU_SHOTS set to a folder (the pictures go there).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class, qualifiers = "w960dp-h540dp-land-mdpi")
public class CoreMenuShotTest {
  @Test public void pagesAsPictures() throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    Assume.assumeTrue(folder != null && !folder.isEmpty());
    String only = System.getenv("RE4_MENU_SHOTS_LANGUAGES");
    String[] languages = only != null && !only.isEmpty() ? only.split(",")
        : new String[] {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.TRUE
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0 : null);
    new File(folder).mkdirs();
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    String[] names = {"home", "display", "audio", "controls", "performance", "cheats", "management"};
    for (String language : languages) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language));
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        Field rootField = CoreMenu.class.getDeclaredField("root"); rootField.setAccessible(true);
        Field bodyField = CoreMenu.class.getDeclaredField("body"); bodyField.setAccessible(true);
        View root = (View) rootField.get(menu), body = (View) bodyField.get(menu);
        root.setLayoutDirection(config.getLayoutDirection());
        for (int page = 0; page < names.length; page++) {
          show.invoke(menu, page, false);
          int width = 900;
          body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
              View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
          body.layout(0, 0, width, body.getMeasuredHeight());
          Bitmap picture = Bitmap.createBitmap(width, body.getMeasuredHeight(), Bitmap.Config.ARGB_8888);
          Canvas canvas = new Canvas(picture);
          canvas.drawColor(0xff191f1c);
          body.draw(canvas);
          if (System.getenv("RE4_MENU_SHOTS_GUIDES") != null) {
            // the buttons' grid edges (MenuLayout.GAP in from both sides): the alignment check
            android.graphics.Paint guide = new android.graphics.Paint(); guide.setColor(0x80ff3030);
            float gap = MenuLayout.GAP * activity.getResources().getDisplayMetrics().density;
            canvas.drawRect(gap - 0.5f, 0, gap + 0.5f, picture.getHeight(), guide);
            canvas.drawRect(width - gap - 0.5f, 0, width - gap + 0.5f, picture.getHeight(), guide);
          }
          try (FileOutputStream out = new FileOutputStream(new File(folder, language + "-" + names[page] + ".png"))) {
            picture.compress(Bitmap.CompressFormat.PNG, 100, out);
          }
        }
      }
    }
  }
}
