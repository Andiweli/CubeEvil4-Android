package com.ast.re4decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Application;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import android.view.ViewGroup;
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
import org.robolectric.shadows.ShadowAlertDialog;

/**
 * Pictures of the language pack in the menu (port/docs/sprachpakete.md), to show the project owner before it goes in:
 * the "Language pack" part of Cheats & Mods without a pack, with one (and in use), with an import refused, the
 * "Game language" row with the pack, and the questions (where the pack is, its name after the import, Delete). Only
 * with the environment variable RE4_MENU_SHOTS set to a folder; RE4_MENU_SHOTS_LANGUAGES picks the languages (en,de).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class, qualifiers = "w960dp-h540dp-land-mdpi")
public class LanguagePackShotTest {
  private LanguagePacks.Info pack;   // what the host says is imported (null none)
  private boolean inUse;

  @Test public void languagePackPictures() throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    Assume.assumeTrue(folder != null && !folder.isEmpty());
    String only = System.getenv("RE4_MENU_SHOTS_LANGUAGES");
    String[] languages = only != null && !only.isEmpty() ? only.split(",") : new String[] {"en", "de"};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> {
          switch (method.getName()) {
            case "languagePack": return pack;
            case "languagePackInUse": return inUse;
            case "gameLanguageOf": return (Integer) args[0] == LanguagePacks.CHOICE ? 5 | 0x100 : (Object) args[0];
            case "leaveLanguagePack": ((Runnable) args[0]).run(); return null;
            default: break;
          }
          return method.getReturnType() == boolean.class ? Boolean.TRUE
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
              : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
    new File(folder).mkdirs();
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
    show.setAccessible(true);
    LanguagePacks.Info sample = new LanguagePacks.Info();
    sample.name = "Português (BR)";
    sample.source = "Translation PT-BR Re4 GC";
    sample.imported = "2026-11-26 19:43";
    sample.language = 5;
    sample.replaced = 315;
    sample.files = 265;
    sample.bytes = 631359056L;
    for (String language : languages) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language));
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
        CoreMenuLayoutTest.setVersion(activity, "1.1.6-core");
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        Field bodyField = CoreMenu.class.getDeclaredField("body");
        bodyField.setAccessible(true);
        View body = (View) bodyField.get(menu);
        String heading = activity.getString(R.string.ingame_lang_pack);
        // 1 no pack, 2 a pack, 3 a pack in use, 4 an import refused
        pack = null;
        inUse = false;
        show.invoke(menu, 5, false);
        part(body, heading, null, 900, new File(folder, language + "-1-mods-none.png"));
        pack = sample;
        show.invoke(menu, 5, false);
        part(body, heading, null, 900, new File(folder, language + "-2-mods-imported.png"));
        part(body, heading, null, 360, new File(folder, language + "-2-mods-imported-360.png"));
        inUse = true;
        show.invoke(menu, 5, false);
        part(body, heading, null, 900, new File(folder, language + "-3-mods-in-use.png"));
        inUse = false;
        pack = null;
        show.invoke(menu, 5, false);
        Field listenerField = CoreMenu.class.getDeclaredField("languageListener");
        listenerField.setAccessible(true);
        LanguagePacks.Listener listener = (LanguagePacks.Listener) listenerField.get(menu);
        listener.done(null, new LanguagePacks.PackException(LanguagePacks.MORE_THAN_TEXT, "Disc 1/St1/r100.das",
            "St1/r100.das: block 0: part 1 (SAT) differs from the disc's"));
        part(body, heading, null, 900, new File(folder, language + "-4-refused-more-than-text.png"));
        listener.done(null, new LanguagePacks.PackException(LanguagePacks.NOT_EUROPEAN, "", ""));
        part(body, heading, null, 900, new File(folder, language + "-4-refused-not-european.png"));
        listener.done(null, new LanguagePacks.PackException(LanguagePacks.PROGRAM_FILE, "Disc 1/sys/main.dol", ""));
        part(body, heading, null, 900, new File(folder, language + "-4-refused-program.png"));
        // 5 "Game language" with the pack chosen (the home page)
        pack = sample;
        activity.getSharedPreferences(CoreActivity.GAME_PREFS, android.content.Context.MODE_PRIVATE).edit()
            .putInt(CoreActivity.GAME_LANGUAGE, LanguagePacks.CHOICE).commit();
        show.invoke(menu, 0, false);
        part(body, activity.getString(R.string.ingame_game_language), null,
            900, new File(folder, language + "-5-game-language.png"));
        part(body, activity.getString(R.string.ingame_game_language), null,
            360, new File(folder, language + "-5-game-language-360.png"));
        // 6 the questions: where the pack is (none yet, and with one), its name after the import, Delete
        pack = null;
        ask(menu, "askLanguageSource", new Class<?>[] {LanguagePacks.Info.class}, new Object[] {null}, config,
            new File(folder, language + "-6-ask-source.png"));
        pack = sample;
        inUse = true;
        ask(menu, "askLanguageSource", new Class<?>[] {LanguagePacks.Info.class}, new Object[] {sample}, config,
            new File(folder, language + "-6-ask-source-replace.png"));
        inUse = false;
        ask(menu, "askLanguageName", new Class<?>[] {LanguagePacks.Info.class}, new Object[] {sample}, config,
            new File(folder, language + "-7-imported-name.png"));
        ask(menu, "askDeleteLanguagePack", new Class<?>[] {LanguagePacks.Info.class}, new Object[] {sample}, config,
            new File(folder, language + "-8-ask-delete.png"));
      }
    }
  }

  /** The menu's question from `method`, as Android shows it, 500 px wide. */
  private static void ask(CoreMenu menu, String method, Class<?>[] types, Object[] args, Configuration config, File file)
      throws Exception {
    Method m = CoreMenu.class.getDeclaredMethod(method, types);
    m.setAccessible(true);
    m.invoke(menu, args);
    TestLooper.idle();
    AlertDialog dialog = (AlertDialog) ShadowAlertDialog.getLatestAlertDialog();
    View decor = dialog.getWindow().getDecorView();
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
    try (FileOutputStream out = new FileOutputStream(file)) {
      picture.compress(Bitmap.CompressFormat.PNG, 100, out);
    }
    dialog.dismiss();
  }

  /** The page from the heading titled `from` up to the heading titled `to` (null: the page's end), `width` px wide. */
  private static void part(View body, String from, String to, int width, File file) throws Exception {
    body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
    body.layout(0, 0, width, body.getMeasuredHeight());
    ViewGroup page = (ViewGroup) body;
    int top = -1, bottom = body.getMeasuredHeight();
    for (int i = 0; i < page.getChildCount(); i++) {
      View child = page.getChildAt(i);
      if (!(child instanceof CoreMenu.Heading)) continue;
      String title = ((CoreMenu.Heading) child).title();
      if (top < 0 && title.equalsIgnoreCase(from)) top = child.getTop();
      else if (top >= 0 && to != null && title.equalsIgnoreCase(to)) {
        bottom = child.getTop();
        break;
      } else if (top >= 0 && to == null) {
        bottom = child.getTop();
        break;
      }
    }
    org.junit.Assert.assertTrue("heading " + from, top >= 0);
    Bitmap picture = Bitmap.createBitmap(width, bottom - top, Bitmap.Config.ARGB_8888);
    Canvas canvas = new Canvas(picture);
    canvas.drawColor(0xff191f1c);
    canvas.translate(0, -top);
    body.draw(canvas);
    try (FileOutputStream out = new FileOutputStream(file)) {
      picture.compress(Bitmap.CompressFormat.PNG, 100, out);
    }
  }
}
