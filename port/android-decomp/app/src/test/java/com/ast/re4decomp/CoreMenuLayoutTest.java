package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.res.Configuration;
import android.graphics.Typeface;
import android.graphics.Rect;
import android.text.Spanned;
import android.text.style.StyleSpan;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Locale;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.Implementation;
import org.robolectric.annotation.Implements;

/** Measure the actual Android views, without loading the game or needing disc data. */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class CoreMenuLayoutTest {
  @Implements(CoreBridge.class)
  public static class Bridge {
    @Implementation public static int[] graphicsInfo() { return new int[] {4, 16}; }
    @Implementation public static String[] cardList() { return new String[0]; }
  }

  private static Object field(Object owner, String name) throws Exception {
    Field f = owner.getClass().getDeclaredField(name); f.setAccessible(true); return f.get(owner);
  }

  private static void measure(View root, int width, int height) {
    root.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
        View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY));
    root.layout(0, 0, width, height);
  }

  private static void checkBounds(View view, String scenario) {
    if (view.getVisibility() != View.VISIBLE) return;
    if (view instanceof TextView && ((TextView) view).getText().length() > 0) {
      TextView text = (TextView) view;
      assertNotNull(scenario + ": missing text layout: " + text.getText(), text.getLayout());
      assertTrue(scenario + ": clipped height: " + text.getText(),
          text.getHeight() >= text.getLayout().getHeight() + text.getCompoundPaddingTop() + text.getCompoundPaddingBottom());
      for (int line = 0; line < text.getLayout().getLineCount(); line++)
        assertEquals(scenario + ": ellipsized text", 0, text.getLayout().getEllipsisCount(line));
    }
    if (view instanceof ViewGroup) {
      ViewGroup group = (ViewGroup) view;
      for (int i = 0; i < group.getChildCount(); i++) {
        View child = group.getChildAt(i);
        if (child.getVisibility() != View.VISIBLE) continue;
        assertTrue(scenario + ": overflow left: " + child.getClass(), child.getLeft() >= 0);
        assertTrue(scenario + ": overflow right: " + child.getClass(), child.getRight() <= group.getWidth());
        if (!(group instanceof ScrollView)) {
          assertTrue(scenario + ": overflow top", child.getTop() >= 0);
          assertTrue(scenario + ": overflow bottom: " + child.getClass(), child.getBottom() <= group.getHeight());
          Rect bounds = new Rect(child.getLeft(), child.getTop(), child.getRight(), child.getBottom());
          for (int j = 0; j < i; j++) {
            View earlier = group.getChildAt(j);
            if (earlier.getVisibility() == View.VISIBLE)
              assertFalse(scenario + ": overlapping views", Rect.intersects(bounds,
                  new Rect(earlier.getLeft(), earlier.getTop(), earlier.getRight(), earlier.getBottom())));
          }
        }
        checkBounds(child, scenario);
      }
    }
  }

  @Test public void allPagesFitAcrossLanguagesWidthsAndFontScales() throws Exception {
    String[] languages = {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi"};
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> null);
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    Method metadata = CoreMenu.class.getDeclaredMethod("showInstant", long[].class); metadata.setAccessible(true);
    for (String language : languages) for (float scale : new float[] {1f, 1.5f, 2f}) {
      try (var controller = Robolectric.buildActivity(Activity.class).create()) {
        Activity activity = controller.get();
        Configuration config = new Configuration(activity.getResources().getConfiguration());
        config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
        activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
        CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
        View root = (View) field(menu, "root");
        root.setLayoutDirection(config.getLayoutDirection());
        for (int page = 0; page < 5; page++) {
          show.invoke(menu, page, false);
          if (page == 0) metadata.invoke(menu, (Object) new long[] {0, 1, 1, 1, 0x100, 101, 123456, 1792841040});
          for (int width : new int[] {960, 320, 480, 700, 960}) {
            int pxWidth = Math.round(width * activity.getResources().getDisplayMetrics().density);
            int pxHeight = Math.round(300 * activity.getResources().getDisplayMetrics().density);
            measure(root, pxWidth, pxHeight);
            checkBounds(root, language + "/" + scale + "/" + width + "/page " + page);
            if (page == 0) {
              Button save = (Button) field(menu, "saveSlot"), load = (Button) field(menu, "loadSlot");
              MenuLayout.Buttons actions = (MenuLayout.Buttons) save.getParent();
              LinearLayout slot = (LinearLayout) actions.getParent();
              if (slot.getOrientation() == LinearLayout.HORIZONTAL && actions.getColumnCount() == 2) {
                ViewGroup details = (ViewGroup) slot.getChildAt(0);
                int headingInset = details.getLeft() + details.getChildAt(0).getLeft();
                int rightInset = slot.getWidth() - actions.getLeft() - Math.max(save.getRight(), load.getRight());
                assertEquals("Right edge matches the slot heading's left inset", headingInset, rightInset, 1);
                assertEquals("Save/load stay side by side", save.getTop(), load.getTop());
                assertEquals("Both actions fill the same height", save.getHeight(), load.getHeight());
                assertEquals("Actions keep the same top inset", headingInset, actions.getTop() + save.getTop(), 1);
                assertEquals("Actions keep the same bottom inset", headingInset,
                    slot.getHeight() - actions.getTop() - save.getBottom(), 1);
                assertTrue("Actions stay in the right half", actions.getLeft() >= slot.getWidth() / 2 - 1);
              }
              assertEquals(View.GONE, ((TextView) field(menu, "slotStatus")).getVisibility());
            }
          }
        }
      }
    }
  }

  @Test public void navigationHasBoldTitleAndNormalSmallerSummary() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      CoreMenu menu = new CoreMenu(controller.get(), null, false, 1920, 1080);
      Button nav = ((Button[]) field(menu, "navigation"))[1];
      assertFalse(nav.getTypeface().isBold());
      Spanned label = (Spanned) nav.getText();
      int split = label.toString().indexOf('\n') + 1;
      StyleSpan[] title = label.getSpans(0, split, StyleSpan.class);
      assertEquals(Typeface.BOLD, title[0].getStyle());
      StyleSpan[] summary = label.getSpans(split, label.length(), StyleSpan.class);
      assertEquals(Typeface.NORMAL, summary[0].getStyle());
    }
  }

  @Test public void titleCanLoadWithoutEnablingSaveAndFrameSkipStaysInert() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).create()) {
      CoreMenu menu = new CoreMenu(controller.get(), null, false, 1920, 1080);
      Method metadata = CoreMenu.class.getDeclaredMethod("showInstant", long[].class); metadata.setAccessible(true);
      metadata.invoke(menu, (Object) new long[] {0, 1, 0, 1, 0x100, 101, 1234, 1792841040});
      assertFalse(((Button) field(menu, "saveSlot")).isEnabled());
      assertTrue(((Button) field(menu, "loadSlot")).isEnabled());
      TextView status = (TextView) field(menu, "slotStatus");
      assertEquals(View.GONE, status.getVisibility());
      Method feedback = CoreMenu.class.getDeclaredMethod("showSlotStatus", int.class); feedback.setAccessible(true);
      feedback.invoke(menu, R.string.ingame_instant_working);
      assertEquals(View.VISIBLE, status.getVisibility());
      metadata.invoke(menu, (Object) new long[] {-4, 1, 0, 0, 0x100, 101, 1234, 1792841040});
      assertEquals(View.VISIBLE, status.getVisibility());
      assertEquals(controller.get().getString(R.string.ingame_instant_failed), status.getText().toString());
      Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
      show.invoke(menu, 3, false);
      ViewGroup body = (ViewGroup) field(menu, "body");
      MenuLayout.Columns output = (MenuLayout.Columns) body.getChildAt(3);
      ViewGroup options = (ViewGroup) output.getChildAt(1);
      Button fps = (Button) ((ViewGroup) options.getChildAt(0)).getChildAt(0);
      Button skip = (Button) ((ViewGroup) options.getChildAt(1)).getChildAt(0);
      Button smooth = (Button) ((ViewGroup) options.getChildAt(2)).getChildAt(0);
      assertTrue(fps.getText().toString().contains(controller.get().getString(R.string.ingame_fps_display)));
      assertEquals(controller.get().getString(R.string.ingame_frame_skip), skip.getText().toString());
      assertFalse(skip.isEnabled()); assertFalse(skip.hasOnClickListeners());
      assertTrue(smooth.getText().toString().contains(controller.get().getString(R.string.ingame_smooth60)));
    }
  }
}
