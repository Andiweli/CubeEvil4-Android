package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;

import android.app.Activity;
import android.app.Application;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ProgressBar;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

/**
 * "Load shaders at start" (ShaderPrepScreen): the bar in the brown of the RE touch skin (1.1.2, the project owner,
 * 24.11.2026), not the theme's cyan. With the environment variable RE4_PREP_SHOTS set to a folder: pictures of the screen
 * (a small phone in landscape, 640 x 360 dp) with the bar at 40 %, as now and with the theme's colours as before.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, application = Application.class, qualifiers = "w640dp-h360dp-land-xhdpi")
public class ShaderPrepShotTest {
  @Test public void barInTheTouchSkinsBrown() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      Activity activity = controller.get();
      activity.setTheme(R.style.AppTheme);
      ShaderPrepScreen screen = new ShaderPrepScreen(activity);
      ProgressBar bar = bar(screen);
      assertEquals(TouchStyle.BORDER, bar.getProgressTintList().getDefaultColor());
      assertEquals(TouchStyle.BORDER, bar.getIndeterminateTintList().getDefaultColor());
      assertEquals(TouchStyle.BORDER, bar.getProgressBackgroundTintList().getDefaultColor());
      String folder = System.getenv("RE4_PREP_SHOTS");
      if (folder == null || folder.isEmpty()) {
        return;
      }
      new File(folder).mkdirs();
      screen.update(2463, 6158);
      shot(activity, screen.view(), new File(folder, "shader-prep-new.png"));
      // before: the same bar without the tints (the theme's colours)
      ViewGroup column = (ViewGroup) bar.getParent();
      int at = column.indexOfChild(bar);
      ProgressBar plain = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
      plain.setMax(1000);
      plain.setIndeterminate(false);
      plain.setProgress(bar.getProgress());
      column.removeView(bar);
      column.addView(plain, at, bar.getLayoutParams());
      shot(activity, screen.view(), new File(folder, "shader-prep-old.png"));
    }
  }

  static ProgressBar bar(ShaderPrepScreen screen) throws Exception {
    Field f = ShaderPrepScreen.class.getDeclaredField("bar");
    f.setAccessible(true);
    return (ProgressBar) f.get(screen);
  }

  static void shot(Activity activity, View view, File file) throws Exception {
    float d = activity.getResources().getDisplayMetrics().density;
    int w = Math.round(640 * d), h = Math.round(360 * d);
    view.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY));
    view.layout(0, 0, w, h);
    Bitmap picture = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
    view.draw(new Canvas(picture));
    try (FileOutputStream out = new FileOutputStream(file)) {
      picture.compress(Bitmap.CompressFormat.PNG, 100, out);
    }
  }
}
