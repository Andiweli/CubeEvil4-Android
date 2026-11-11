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
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
            : method.getReturnType() == double.class ? (Object) 60.0 : null);
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
        // the warning before another player character (08.11.2026), about as wide as Android's alert in landscape
        // on a handheld (65 % of the Retroid Pocket Flip 2's 768 dp)
        show.invoke(menu, 5, false);
        View hunk = withText(body, "HUNK");
        hunk.performClick();
        TestLooper.idle();  // shown: its look is set
        android.app.AlertDialog ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
        View decor = ask.getWindow().getDecorView();
        decor.setLayoutDirection(config.getLayoutDirection());
        int width = 500;
        decor.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        decor.layout(0, 0, width, decor.getMeasuredHeight());
        // as before a frame on the device: the one-line title scrolls to its text's place (right to left: the right)
        decor.getViewTreeObserver().dispatchOnPreDraw();
        Bitmap picture = Bitmap.createBitmap(width, decor.getMeasuredHeight(), Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(picture);
        canvas.drawColor(0xff0b0d0c);
        decor.draw(canvas);
        try (FileOutputStream out = new FileOutputStream(new File(folder, language + "-character-warning.png"))) {
          picture.compress(Bitmap.CompressFormat.PNG, 100, out);
        }
        ask.dismiss();
      }
    }
  }
  private static View withText(View view, String text) {
    if (view instanceof android.widget.TextView && text.equals(((android.widget.TextView) view).getText().toString())) return view;
    if (view instanceof android.view.ViewGroup) {
      android.view.ViewGroup group = (android.view.ViewGroup) view;
      for (int i = 0; i < group.getChildCount(); i++) {
        View found = withText(group.getChildAt(i), text);
        if (found != null) return found;
      }
    }
    return null;
  }
}
