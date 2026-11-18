package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.app.Application;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Locale;
import java.util.function.Predicate;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

/**
 * The Mods page under Cheats & Mods (1.0.0, wish of the project owner, 17.11.2026): with a pack imported, left Import,
 * Sharpness, Delete texture pack; right HD textures, Costumes, Weapons. Costumes and Weapons say "Texture pack" at first
 * and move to "Original" and back on a press (the setting kept, the renderer told through graphicsChanged). With the
 * environment variable RE4_MENU_SHOTS set to a folder: pictures of the page (English and German, 960 and 360 px).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class,
    qualifiers = "w960dp-h540dp-land-mdpi")
public class ModsPageTest {
  private static int graphicsChanged;

  private static CoreMenu.Host host() {
    return (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(), new Class<?>[] {CoreMenu.Host.class},
        (proxy, method, args) -> {
          if (method.getName().equals("graphicsChanged")) graphicsChanged++;
          return method.getReturnType() == boolean.class ? Boolean.TRUE
              : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
              : method.getReturnType() == double.class ? (Object) 60.0 : null;
        });
  }

  private static Button find(View view, Predicate<Button> which) {
    if (view instanceof Button && view.getVisibility() == View.VISIBLE && which.test((Button) view)) return (Button) view;
    if (view instanceof ViewGroup) {
      ViewGroup group = (ViewGroup) view;
      for (int i = 0; i < group.getChildCount(); i++) {
        Button found = find(group.getChildAt(i), which);
        if (found != null) return found;
      }
    }
    return null;
  }

  private static Button starting(View body, String text) {
    Button b = find(body, x -> x.getText().toString().startsWith(text));
    assertNotNull("a button starting with " + text, b);
    return b;
  }

  /** The view's place in the page (the views are not in a window here: the offsets added up). */
  private static int[] place(View v) {
    int x = 0, y = 0;
    for (View p = v; p != null && p.getParent() instanceof View; p = (View) p.getParent()) {
      x += p.getLeft();
      y += p.getTop();
    }
    return new int[] {x, y};
  }

  private static View body(CoreMenu menu, Activity activity, Configuration config, int width) throws Exception {
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
    show.setAccessible(true);
    Field rootField = CoreMenu.class.getDeclaredField("root");
    rootField.setAccessible(true);
    Field bodyField = CoreMenu.class.getDeclaredField("body");
    bodyField.setAccessible(true);
    ((View) rootField.get(menu)).setLayoutDirection(config.getLayoutDirection());
    show.invoke(menu, 5, false);
    View body = (View) bodyField.get(menu);
    body.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
    body.layout(0, 0, width, body.getMeasuredHeight());
    return body;
  }

  private static Activity activity(String language, Configuration[] config) throws Exception {
    Activity activity = Robolectric.buildActivity(Activity.class).create().get();
    Configuration c = new Configuration(activity.getResources().getConfiguration());
    c.setLocale(Locale.forLanguageTag(language));
    activity.getResources().updateConfiguration(c, activity.getResources().getDisplayMetrics());
    CoreMenuLayoutTest.setVersion(activity, "1.0.0-core");
    File pack = TexturePacks.file(activity);
    pack.getParentFile().mkdirs();
    try (FileOutputStream out = new FileOutputStream(pack)) {
      out.write(TexturePacksTest.pack(3));
    }
    config[0] = c;
    return activity;
  }

  @Test public void layoutAndSwitches() throws Exception {
    TestLooper.stopAnimations("left running before " + getClass().getSimpleName());
    Configuration[] config = new Configuration[1];
    Activity activity = activity("en", config);
    SharedPreferences graphics = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, android.content.Context.MODE_PRIVATE);
    graphics.edit().remove(TexturePacks.COSTUMES).remove(TexturePacks.WEAPONS).apply();
    CoreMenu menu = new CoreMenu(activity, host(), false, 1920, 1080);
    View body = body(menu, activity, config[0], 960);
    Button importPack = starting(body, activity.getString(R.string.ingame_hd_import));
    Button sharpness = starting(body, activity.getString(R.string.ingame_hd_sharpness));
    Button delete = starting(body, activity.getString(R.string.ingame_hd_delete));
    Button onOff = starting(body, activity.getString(R.string.ingame_hd_textures));
    // (the cheats above have their own "Costumes · Normal": the Mods page's says "Texture pack" or "Original")
    String packWord = activity.getString(R.string.ingame_hd_pack), originalWord = activity.getString(R.string.ingame_hd_original);
    Predicate<Button> modsValue = b -> b.getText().toString().contains(packWord) || b.getText().toString().contains(originalWord);
    Button costumes = find(body, b -> b.getText().toString().startsWith(activity.getString(R.string.ingame_hd_costumes)) && modsValue.test(b));
    Button weapons = find(body, b -> b.getText().toString().startsWith(activity.getString(R.string.ingame_hd_weapons)) && modsValue.test(b));
    assertNotNull("the Mods page's Costumes", costumes);
    assertNotNull("the Mods page's Weapons", weapons);
    int[] i = place(importPack), s = place(sharpness), d = place(delete), o = place(onOff), c = place(costumes),
        w = place(weapons);
    assertTrue("left column: Import, Sharpness, Delete one under the other", i[0] == s[0] && s[0] == d[0] && i[1] < s[1] && s[1] < d[1]);
    assertTrue("right column: HD textures, Costumes, Weapons", o[0] == c[0] && c[0] == w[0] && o[1] < c[1] && c[1] < w[1]);
    assertTrue("the right column right of the left one", o[0] > i[0]);
    assertTrue("rows side by side", i[1] == o[1] && s[1] == c[1] && d[1] == w[1]);
    assertTrue("all of them usable with a pack", costumes.isEnabled() && weapons.isEnabled() && delete.isEnabled());
    String pack = activity.getString(R.string.ingame_hd_pack), original = activity.getString(R.string.ingame_hd_original);
    assertTrue("Costumes at first: " + costumes.getText(), costumes.getText().toString().contains(pack));
    assertTrue("Weapons at first: " + weapons.getText(), weapons.getText().toString().contains(pack));
    int before = graphicsChanged;
    costumes.performClick();
    assertFalse("Costumes · Original kept", graphics.getBoolean(TexturePacks.COSTUMES, true));
    assertTrue("Costumes now: " + costumes.getText(), costumes.getText().toString().contains(original));
    assertTrue("Weapons untouched", graphics.getBoolean(TexturePacks.WEAPONS, true));
    assertEquals("the renderer told", before + 1, graphicsChanged);
    costumes.performClick();
    weapons.performClick();
    assertTrue("Costumes back to the pack", graphics.getBoolean(TexturePacks.COSTUMES, true));
    assertFalse("Weapons · Original kept", graphics.getBoolean(TexturePacks.WEAPONS, true));
    graphics.edit().remove(TexturePacks.COSTUMES).remove(TexturePacks.WEAPONS).apply();
    TexturePacks.file(activity).delete();
  }

  @Test public void pictures() throws Exception {
    String folder = System.getenv("RE4_MENU_SHOTS");
    org.junit.Assume.assumeTrue(folder != null && !folder.isEmpty());
    new File(folder).mkdirs();
    for (String language : new String[] {"en", "de"}) {
      for (boolean originals : new boolean[] {false, true}) {
        Configuration[] config = new Configuration[1];
        Activity activity = activity(language, config);
        SharedPreferences graphics = activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, android.content.Context.MODE_PRIVATE);
        graphics.edit().putBoolean(TexturePacks.COSTUMES, !originals).putBoolean(TexturePacks.WEAPONS, !originals).apply();
        CoreMenu menu = new CoreMenu(activity, host(), false, 1920, 1080);
        for (int width : new int[] {960, 360}) {
          View body = body(menu, activity, config[0], width);
          View heading = find(body, b -> b.getText().toString().startsWith(activity.getString(R.string.ingame_hd_import)));
          int top = Math.max(0, place(heading)[1] - 140);
          Bitmap page = Bitmap.createBitmap(width, body.getMeasuredHeight() - top, Bitmap.Config.ARGB_8888);
          Canvas canvas = new Canvas(page);
          canvas.drawColor(0xff191f1c);
          canvas.translate(0, -top);
          body.draw(canvas);
          String name = language + "-mods-" + (originals ? "original" : "pack") + "-" + width + ".png";
          try (FileOutputStream out = new FileOutputStream(new File(folder, name))) {
            page.compress(Bitmap.CompressFormat.PNG, 100, out);
          }
        }
        graphics.edit().remove(TexturePacks.COSTUMES).remove(TexturePacks.WEAPONS).apply();
        TexturePacks.file(activity).delete();
      }
    }
  }
}
