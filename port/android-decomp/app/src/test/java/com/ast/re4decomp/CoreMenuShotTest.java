package com.ast.re4decomp;

import android.app.Activity;
import android.app.Application;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import android.widget.Button;
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
        CoreMenuLayoutTest.setVersion(activity, "0.99.18-rc-core");
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
        // the menu's header with the version left of the update button (0.99.18): up to date, an update, no answer from
        // GitHub (the button hidden); 900 px wide, 480 (the buttons under the title) and 340 (no room beside them: the
        // version under them)
        show.invoke(menu, 0, false);
        String[] states = {"offline", "current", "update"};
        int[] codes = {AppUpdate.UNKNOWN, AppUpdate.CURRENT, AppUpdate.OLDER};
        for (int s = 0; s < states.length; s++) {
          menu.showUpdate(codes[s]);
          for (int width : new int[] {900, 480, 340}) {
            root.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(600, View.MeasureSpec.EXACTLY));
            root.layout(0, 0, width, 600);
            View header = ((android.view.ViewGroup) root).getChildAt(0);
            int height = header.getBottom() + 12;
            Bitmap picture = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(picture);
            canvas.drawColor(0xff191f1c);
            canvas.save(); canvas.clipRect(0, 0, width, height); root.draw(canvas); canvas.restore();
            try (FileOutputStream out = new FileOutputStream(new File(folder, language + "-header-" + states[s] + "-" + width + ".png"))) {
              picture.compress(Bitmap.CompressFormat.PNG, 100, out);
            }
          }
        }
        menu.showUpdate(AppUpdate.UNKNOWN);
        // the view distance button in its five levels with the eyes (0.99.18): the button alone, the page 900 px and
        // 360 px wide (narrow: the eyes may go to the next line)
        android.content.SharedPreferences graphics = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS,
            android.content.Context.MODE_PRIVATE);
        int[] levels = {CoreBridge.VIEW_FAR, CoreBridge.VIEW_NORMAL, 1, CoreBridge.VIEW_SHORT, CoreBridge.VIEW_VERY_SHORT};
        String[] levelNames = {"1-far", "2-normal", "3-medium", "4-short", "5-none"};
        for (int l = 0; l < levels.length; l++) {
          graphics.edit().putInt(CoreBridge.VIEW_DISTANCE, levels[l]).apply();
          show.invoke(menu, 4, false);
          for (int width : new int[] {900, 360}) {
            body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
            body.layout(0, 0, width, body.getMeasuredHeight());
            Button view = firstButton(body, b -> b.getText().toString().startsWith(activity.getString(R.string.ingame_potato_view)));
            Bitmap picture = Bitmap.createBitmap(view.getWidth() + 16, view.getHeight() + 16, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(picture);
            canvas.drawColor(0xff191f1c);
            canvas.translate(8, 8);
            view.draw(canvas);
            try (FileOutputStream out = new FileOutputStream(new File(folder,
                language + "-view-" + levelNames[l] + "-" + width + ".png"))) {
              picture.compress(Bitmap.CompressFormat.PNG, 100, out);
            }
          }
        }
        graphics.edit().remove(CoreBridge.VIEW_DISTANCE).apply();
        // the Cheats & Mods page (1.0.0, the project owner's new order): a phone upright (360 px) and a handheld on
        // its side (960 px)
        show.invoke(menu, 5, false);
        for (int width : new int[] {360, 960}) {
          body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
              View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
          body.layout(0, 0, width, body.getMeasuredHeight());
          Bitmap page = Bitmap.createBitmap(width, body.getMeasuredHeight(), Bitmap.Config.ARGB_8888);
          Canvas canvas = new Canvas(page);
          canvas.drawColor(0xff191f1c);
          body.draw(canvas);
          try (FileOutputStream out = new FileOutputStream(new File(folder, language + "-cheats-" + width + ".png"))) {
            page.compress(Bitmap.CompressFormat.PNG, 100, out);
          }
        }
        // the note under "Source and updates" (0.99.18): the management page's end from its heading on, 900 px and a
        // handheld's 360 px wide
        show.invoke(menu, 6, false);
        for (int width : new int[] {900, 360}) {
          body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
              View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
          body.layout(0, 0, width, body.getMeasuredHeight());
          View heading = null;
          android.view.ViewGroup page = (android.view.ViewGroup) body;
          for (int i = 0; i < page.getChildCount(); i++)
            if (page.getChildAt(i) instanceof CoreMenu.Heading) heading = page.getChildAt(i);  // the last: the source's
          int top = heading.getTop(), height = body.getMeasuredHeight() - top;
          Bitmap picture = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
          Canvas canvas = new Canvas(picture);
          canvas.drawColor(0xff191f1c);
          canvas.translate(0, -top);
          body.draw(canvas);
          try (FileOutputStream out = new FileOutputStream(new File(folder, language + "-source-note-" + width + ".png"))) {
            picture.compress(Bitmap.CompressFormat.PNG, 100, out);
          }
        }
        // the controller's focus (0.99.11, FocusLook): "Performance" on the start page, a resolution level beside the
        // selected one on the display page, the benchmark button
        show.invoke(menu, 0, false);
        focus(firstButton(body, b -> b.getText().toString().startsWith(activity.getString(R.string.ingame_performance))));
        picture(body, 900, new File(folder, language + "-focus-home.png"));
        show.invoke(menu, 0, false);
        focus(firstButton(body, b -> b.getText().toString().equals(activity.getString(R.string.ingame_benchmark))));
        picture(body, 900, new File(folder, language + "-focus-benchmark.png"));
        show.invoke(menu, 1, false);
        Button selected = firstButton(body, Button::isSelected);
        android.view.ViewGroup row = (android.view.ViewGroup) selected.getParent();
        int at = row.indexOfChild(selected);
        focus((Button) row.getChildAt(at + 1 < row.getChildCount() ? at + 1 : at - 1));
        picture(body, 900, new File(folder, language + "-focus-display.png"));
        // the Information HUD on (0.99.11, InfoHud): its button with the check boxes under it, the time's box cleared
        android.content.SharedPreferences g = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS,
            android.content.Context.MODE_PRIVATE);
        g.edit().putInt(CoreActivity.FPS_PLACE, 1).putBoolean(InfoHud.TIME, false).apply();
        show.invoke(menu, 1, false);
        picture(body, 900, new File(folder, language + "-display-hud.png"));
        g.edit().remove(CoreActivity.FPS_PLACE).remove(InfoHud.TIME).apply();
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
        // the question before "Delete All Data" (0.99.15), as wide as the warning above; the focus on Cancel
        show.invoke(menu, 6, false);
        firstButton(body, b -> b.getText().toString().equals(activity.getString(R.string.ingame_wipe))).performClick();
        TestLooper.idle();
        ask = (android.app.AlertDialog) org.robolectric.shadows.ShadowAlertDialog.getLatestAlertDialog();
        dialogPicture(ask, config, new File(folder, language + "-wipe-question.png"));
        ask.dismiss();
        // the memory card's buttons with the controller's focus on "Delete All Data"
        show.invoke(menu, 6, false);
        focus(firstButton(body, b -> b.getText().toString().equals(activity.getString(R.string.ingame_wipe))));
        picture(body, 900, new File(folder, language + "-focus-wipe.png"));
      }
    }
  }
  /** The focus on `b` as a controller puts it there (the body is not in a window here: focusable in touch mode). */
  private static void focus(Button b) {
    b.setFocusableInTouchMode(true);
    org.junit.Assert.assertTrue("focused: " + b.getText(), b.requestFocus());
  }
  private static Button firstButton(View view, java.util.function.Predicate<Button> which) {
    if (view instanceof Button && view.getVisibility() == View.VISIBLE && which.test((Button) view)) return (Button) view;
    if (view instanceof android.view.ViewGroup) {
      android.view.ViewGroup group = (android.view.ViewGroup) view;
      for (int i = 0; i < group.getChildCount(); i++) {
        Button found = firstButton(group.getChildAt(i), which);
        if (found != null) return found;
      }
    }
    return null;
  }
  /** A question of the menu as Android shows it, 500 px wide (the character warning's width). */
  private static void dialogPicture(android.app.AlertDialog ask, Configuration config, File file) throws Exception {
    View decor = ask.getWindow().getDecorView();
    decor.setLayoutDirection(config.getLayoutDirection());
    int width = 500;
    decor.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
    decor.layout(0, 0, width, decor.getMeasuredHeight());
    decor.getViewTreeObserver().dispatchOnPreDraw();
    Bitmap picture = Bitmap.createBitmap(width, decor.getMeasuredHeight(), Bitmap.Config.ARGB_8888);
    Canvas canvas = new Canvas(picture);
    canvas.drawColor(0xff0b0d0c);
    decor.draw(canvas);
    try (FileOutputStream out = new FileOutputStream(file)) { picture.compress(Bitmap.CompressFormat.PNG, 100, out); }
  }
  private static void picture(View body, int width, File file) throws Exception {
    body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
    body.layout(0, 0, width, body.getMeasuredHeight());
    Bitmap picture = Bitmap.createBitmap(width, body.getMeasuredHeight(), Bitmap.Config.ARGB_8888);
    Canvas canvas = new Canvas(picture);
    canvas.drawColor(0xff191f1c);
    body.draw(canvas);
    try (FileOutputStream out = new FileOutputStream(file)) { picture.compress(Bitmap.CompressFormat.PNG, 100, out); }
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
