package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Application;
import android.content.res.TypedArray;
import android.graphics.Color;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.SeekBar;
import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowDialog;

/**
 * The controller's focus is golden on every button of the app menu, its pages, its questions and popups, and Android's
 * dark mode for apps ("force dark") cannot darken it (0.99.11, a user's Xiaomi with Android 11 showed only a faint dark
 * frame; FocusLook).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class FocusLookTest {
  private static final int[] FOCUSED = {android.R.attr.state_focused, android.R.attr.state_enabled,
      android.R.attr.state_window_focused};

  @org.junit.Before public void noAnimationLeftRunning() {
    TestLooper.stopAnimations("left running before " + getClass().getSimpleName());
  }

  // ---- Android's force dark (frameworks/base/libs/hwui/CanvasTransform.cpp makeDark), sRGB <-> CIE Lab (D65) ----
  private static double lin(int c) { double v = c / 255.0; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }
  private static int srgb(double v) {
    v = Math.max(0, Math.min(1, v));
    return (int) Math.round(255 * (v <= 0.0031308 ? 12.92 * v : 1.055 * Math.pow(v, 1 / 2.4) - 0.055));
  }
  private static double f(double t) { return t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116.0; }
  private static double g(double t) { return t * t * t > 0.008856 ? t * t * t : (t - 16 / 116.0) / 7.787; }
  private static double[] lab(int colour) {
    double r = lin(Color.red(colour)), gr = lin(Color.green(colour)), b = lin(Color.blue(colour));
    double x = (0.4124 * r + 0.3576 * gr + 0.1805 * b) / 0.95047, y = 0.2126 * r + 0.7152 * gr + 0.0722 * b;
    double z = (0.0193 * r + 0.1192 * gr + 0.9505 * b) / 1.08883;
    return new double[] {116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
  }
  private static int fromLab(double l, double a, double bb) {
    double fy = (l + 16) / 116, fx = fy + a / 500, fz = fy - bb / 200;
    double x = 0.95047 * g(fx), y = g(fy), z = 1.08883 * g(fz);
    return Color.rgb(srgb(3.2406 * x - 1.5372 * y - 0.4986 * z), srgb(-0.9689 * x + 1.8758 * y + 0.0415 * z),
        srgb(0.0557 * x - 0.2040 * y + 1.0570 * z));
  }
  /** What force dark draws for a background colour. */
  static int forceDark(int colour) {
    double[] l = lab(colour);
    double inverted = Math.min(110 - l[0], 100);
    return inverted < l[0] ? fromLab(inverted, l[1], l[2]) : colour;
  }
  private static double luminance(int c) {
    return 0.2126 * lin(Color.red(c)) + 0.7152 * lin(Color.green(c)) + 0.0722 * lin(Color.blue(c));
  }
  private static double contrast(int a, int b) {
    double x = luminance(a) + .05, y = luminance(b) + .05; return Math.max(x, y) / Math.min(x, y);
  }
  private static void near(String what, int expected, int actual) {
    assertTrue(what + ": " + Integer.toHexString(actual) + " instead of " + Integer.toHexString(expected),
        Math.abs(Color.red(expected) - Color.red(actual)) <= 2 && Math.abs(Color.green(expected) - Color.green(actual)) <= 2
            && Math.abs(Color.blue(expected) - Color.blue(actual)) <= 2);
  }

  @Test public void forceDarkLeavesTheFocusAsItIs() {
    // the cause: the old white frame (the menu's ink) came out as the user's picture shows it (90 % there)
    int old = forceDark(0xfff0eee5);
    near("the old frame under force dark", Color.rgb(41, 40, 33), old);
    near("... in the user's picture", Color.rgb(35, 36, 30),
        Color.rgb(Math.round(Color.red(old) * .9f), Math.round(Color.green(old) * .9f), Math.round(Color.blue(old) * .9f)));
    // the new look: the fill untouched, clearly apart from a button and from a selected choice, the text readable on
    // it; the edge stays apart from the fill even if a phone recoloured it
    assertEquals("the focus's fill", FocusLook.FILL, forceDark(FocusLook.FILL));
    double panel = lab(0xff222a25)[0], selected = lab(0xff45432e)[0], fill = lab(FocusLook.FILL)[0];
    assertTrue("fill lighter than a button", fill - panel >= 20);
    assertTrue("fill lighter than a selected choice", fill - selected >= 12);
    assertTrue("the edge over the fill", lab(FocusLook.EDGE)[0] - fill >= 25);
    assertTrue("the edge under force dark still apart from the fill", Math.abs(lab(forceDark(FocusLook.EDGE))[0] - fill) >= 8);
    assertTrue("the text on the fill: at least 4.5 : 1", contrast(FocusLook.TEXT, FocusLook.FILL) >= 4.5);
  }

  private static int focusedFill(View v) {
    Drawable d = v.getBackground();
    assertTrue(v + ": a state list", d instanceof StateListDrawable);
    StateListDrawable list = (StateListDrawable) d;
    int index = list.findStateDrawableIndex(FOCUSED);
    assertTrue(v + ": a focused look", index >= 0);
    Drawable focused = list.getStateDrawable(index);
    assertTrue(v + ": the focused look is a shape", focused instanceof GradientDrawable);
    return ((GradientDrawable) focused).getColor().getDefaultColor();
  }

  /** Every button and slider under `view` that can take the focus: the golden focus, the text in ink. */
  private static int check(View view, String where) {
    int n = 0;
    if (view.getVisibility() != View.VISIBLE) return 0;
    if ((view instanceof Button || view instanceof SeekBar) && view.isFocusable()) {
      String name = where + " " + (view instanceof Button ? "\"" + ((Button) view).getText() + "\"" : "slider");
      assertEquals(name + ": golden focus", FocusLook.FILL, focusedFill(view));
      if (view instanceof Button && !view.isSelected())
        assertEquals(name + ": its text in ink while focused", FocusLook.TEXT,
            ((Button) view).getTextColors().getColorForState(FOCUSED, 0));
      n++;
    }
    if (view instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup) view).getChildCount(); i++) n += check(((ViewGroup) view).getChildAt(i), where);
    return n;
  }

  private static CoreMenu.Host host() {
    return (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.TRUE
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
            : method.getReturnType() == double.class ? (Object) 60.0 : null);
  }

  @Test public void everyButtonAndSliderOfTheMenuIsGoldenWhenFocused() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      CoreMenu menu = new CoreMenu(controller.get(), host(), true, 1920, 1080);
      Immersive.show(menu);
      TestLooper.idle();
      menu.showUpdate(AppUpdate.OLDER);  // the red "Update" can take the focus
      Field rootField = CoreMenu.class.getDeclaredField("root"); rootField.setAccessible(true);
      View root = (View) rootField.get(menu);
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      int buttons = 0, sliders = 0;
      Field kind = CoreMenu.class.getDeclaredField("mapKind"); kind.setAccessible(true);
      for (int page = 0; page < 8; page++) {  // 7: the controller's button mapping
        if (page == 7) kind.setInt(menu, ActionMap.KIND_CONTROLLER);
        show.invoke(menu, page, false);
        buttons += check(root, "page " + page);
      }
      assertTrue("buttons checked: " + buttons, buttons > 150);
      // the sliders on their pages
      for (int page = 0; page < 7; page++) {
        show.invoke(menu, page, false);
        sliders += count(root, SeekBar.class);
      }
      assertTrue("sliders found: " + sliders, sliders >= 4);
      // no force dark in the menu's window
      assertFalse("the menu: no force dark", menu.getWindow().getDecorView().isForceDarkAllowed());
      menu.dismiss();
    }
  }

  private static int count(View view, Class<?> kind) {
    int n = kind.isInstance(view) ? 1 : 0;
    if (view instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup) view).getChildCount(); i++) n += count(((ViewGroup) view).getChildAt(i), kind);
    return n;
  }

  @Test public void questionsAndTheCharacterWarningToo() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      CoreMenu menu = new CoreMenu(controller.get(), host(), false, 1920, 1080);
      Immersive.show(menu);
      TestLooper.idle();
      for (String ask : new String[] {"askImportDiscs", "askClearShaders", "askBenchmark"}) {
        Method m = CoreMenu.class.getDeclaredMethod(ask); m.setAccessible(true);
        m.invoke(menu);
        TestLooper.idle();
        AlertDialog question = (AlertDialog) ShadowDialog.getLatestDialog();
        assertFalse(ask + ": no force dark", question.getWindow().getDecorView().isForceDarkAllowed());
        int n = 0;
        for (int which : new int[] {AlertDialog.BUTTON_POSITIVE, AlertDialog.BUTTON_NEGATIVE, AlertDialog.BUTTON_NEUTRAL}) {
          Button b = question.getButton(which);
          if (b == null || b.getVisibility() != View.VISIBLE) continue;
          assertEquals(ask + " button " + which, FocusLook.FILL, focusedFill(b));
          assertEquals(ask + " button " + which + ": ink", FocusLook.TEXT, b.getTextColors().getColorForState(FOCUSED, 0));
          n++;
        }
        assertTrue(ask + ": its buttons", n >= 1);
        // showing again does not wrap the look twice
        FocusLook.afterShow(question);
        assertEquals(FocusLook.FILL, focusedFill(question.getButton(AlertDialog.BUTTON_NEGATIVE)));
        question.dismiss();
        TestLooper.idle();
      }
      // the warning before another player character: its red "Understood"
      Method warn = CoreMenu.class.getDeclaredMethod("warnCharacter", String.class, Runnable.class);
      warn.setAccessible(true);
      warn.invoke(menu, "HUNK", (Runnable) () -> {});
      TestLooper.idle();
      AlertDialog warning = (AlertDialog) ShadowDialog.getLatestDialog();
      assertEquals("the warning's button", 1, check(warning.getWindow().getDecorView(), "warning"));
      warning.dismiss();
      menu.dismiss();
    }
  }

  @Test public void theAppsThemeSaysNoToForceDark() {
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      TypedArray a = controller.get().getTheme().obtainStyledAttributes(new int[] {android.R.attr.forceDarkAllowed});
      assertFalse("the theme's forceDarkAllowed", a.getBoolean(0, true));
      a.recycle();
    }
  }

  /** No focused look of a button in the app's windows without FocusLook (a new one would be white again). */
  @Test public void everyFocusedLookIsTheGoldenOne() throws Exception {
    for (String name : new String[] {"CoreMenu", "BenchmarkFlow", "CoreActivity"}) {
      File source = new File("src/main/java/com/ast/re4decomp/" + name + ".java");
      if (!source.exists()) source = new File("app/" + source.getPath());
      assertTrue(source.getPath(), source.exists());
      List<String> lines = Files.readAllLines(source.toPath(), StandardCharsets.UTF_8);
      for (int i = 0; i < lines.size(); i++) {
        String line = lines.get(i);
        if (!line.contains("state_focused")) continue;
        assertTrue(name + ".java:" + (i + 1) + " a focused look without FocusLook: " + line.trim(),
            line.contains("focused()") || line.contains("FocusLook"));
      }
    }
  }
}
