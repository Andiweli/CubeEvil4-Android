package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.res.Configuration;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Locale;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.GraphicsMode;

/** The start page's navigation rows with real text widths (Robolectric's native graphics, as CoreMenuShotTest). */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class, qualifiers = "w960dp-h540dp-land-mdpi")
public class CoreMenuRowsTest {
  /** "Cheats & Mods" and "Management" side by side (wish of the project owner, 12.11.2026: the longer subtext with the
   *  HD textures had put them on two lines): in every language and font scale "Cheats & Mods" needs no more width than
   *  "Management" beside it, so the row keeps two buttons wherever it did before; there both sit on one line, nothing
   *  cut off. */
  private static int unwrapped(Button b) {
    b.measure(View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED),
        View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
    return b.getMeasuredWidth();
  }

  @Test public void cheatsAndManagementStaySideBySide() throws Exception {
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class}, (proxy, method, args) -> method.getReturnType() == boolean.class
            ? Boolean.FALSE : method.getReturnType() == int.class ? 0 : method.getReturnType() == float.class ? (Object) 1f
            : method.getReturnType() == double.class ? (Object) 60.0 : null);
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    int checked = 0;
    StringBuilder wider = new StringBuilder();
    for (String language : new String[] {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"})
      for (float scale : new float[] {1f, 1.15f, 1.3f, 1.5f}) {
        try (var controller = Robolectric.buildActivity(Activity.class).create()) {
          Activity activity = controller.get();
          Configuration config = new Configuration(activity.getResources().getConfiguration());
          config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
          activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
          CoreMenu menu = new CoreMenu(activity, host, false, 1920, 1080);
          show.invoke(menu, 0, false);
          View root = (View) CoreMenuLayoutTest.field(menu, "root");
          root.setLayoutDirection(config.getLayoutDirection());
          Button[] nav = (Button[]) CoreMenuLayoutTest.field(menu, "navigation");
          float density = activity.getResources().getDisplayMetrics().density;
          // the text each button needs on one line (MenuLayout.Buttons puts the row on two lines by the widest):
          // Cheats & Mods needs no more than Management, so the row breaks where it did before the HD textures
          int cheats = unwrapped(nav[5]), management = unwrapped(nav[6]);
          String where = language + "/" + scale;
          if (cheats > management) wider.append(where).append(": Cheats & Mods ").append(cheats).append(" px, Management ")
              .append(management).append(" px; ");
          for (int width = 320; width <= 1280; width += 20) {
            CoreMenuLayoutTest.measure(root, Math.round(width * density), Math.round(300 * density));
            MenuLayout.Buttons third = (MenuLayout.Buttons) nav[5].getParent();
            assertSame(where + ": Cheats & Mods and Management in one row", third, nav[6].getParent());
            if (third.getColumnCount() == 2) {
              assertEquals(where + "/" + width + ": on one line", nav[5].getTop(), nav[6].getTop());
              CoreMenuLayoutTest.checkBounds(third, where + "/" + width);
              checked++;
            }
          }
        }
      }
    assertEquals("wider than Management", "", wider.toString());
    assertTrue("widths with two buttons a row were checked", checked > 100);
  }

  /** The view distance button with its eyes (0.99.18, the project owner, 15.11.2026), with real text widths: in every
   *  language, font scale and level the text is whole (nothing cut, nothing ellipsized, the button as high as its lines)
   *  and inside the window from 320 dp up; the eyes come after the text, on its line or the next (one piece); at the
   *  normal font size and 700 dp or more everything on one line. */
  private static Button withLabel(View view, String label) {
    if (view instanceof Button && ((Button) view).getText().toString().startsWith(label)) return (Button) view;
    if (view instanceof android.view.ViewGroup) {
      android.view.ViewGroup g = (android.view.ViewGroup) view;
      for (int i = 0; i < g.getChildCount(); i++) {
        Button b = withLabel(g.getChildAt(i), label);
        if (b != null) return b;
      }
    }
    return null;
  }

  @Test public void viewDistanceEyesNeverCutTheText() throws Exception {
    java.lang.reflect.Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class);
    show.setAccessible(true);
    int checked = 0;
    for (String language : new String[] {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"})
      for (float scale : new float[] {1f, 1.3f, 1.5f}) {
        // one menu for each language and font size, the levels by the button's own clicks (Normal, Medium, Short,
        // None, Far: CoreBridge.nextView); a menu each with real graphics ran the tests out of memory
        try (var controller = Robolectric.buildActivity(Activity.class).create()) {
          Activity activity = controller.get();
          Configuration config = new Configuration(activity.getResources().getConfiguration());
          config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
          activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
          activity.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, 0).edit()
              .putInt(CoreBridge.VIEW_DISTANCE, CoreBridge.VIEW_NORMAL).apply();
          CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
          show.invoke(menu, 4, false);  // PERFORMANCE
          View root = (View) CoreMenuLayoutTest.field(menu, "root");
          root.setLayoutDirection(config.getLayoutDirection());
          // (by its value's text: the Arabic one begins with the right-to-left mark)
          Button view = withLabel(root, activity.getString(R.string.ingame_toggle_value,
              activity.getString(R.string.ingame_potato_view), activity.getString(R.string.ingame_view_normal)));
          assertNotNull(language + ": the view distance button", view);
          float density = activity.getResources().getDisplayMetrics().density;
          // the window's width fixed, the levels clicked through, as in the app (each click lays the button out anew)
          for (int width : new int[] {320, 480, 700, 960}) {
            for (int level = CoreBridge.VIEW_NORMAL, step = 0; step < CoreBridge.VIEW_PERCENT.length;
                 step++, level = CoreBridge.nextView(level)) {
              if (step > 0) view.performClick();
              String where = language + "/" + scale + "/level " + level + "/" + width;
              assertTrue(where + ": its name", view.getText().toString().contains(
                  activity.getString(CoreBridge.VIEW_LABEL[level])));
              CoreMenuLayoutTest.measure(root, Math.round(width * density), Math.round(300 * density));
              View row = (View) view.getParent();
              assertTrue(where + ": inside the page", view.getLeft() >= 0 && view.getRight() <= row.getWidth());
              CoreMenuLayoutTest.checkBounds(view, where);
              android.text.Layout layout = view.getLayout();
              int eyes = view.getText().toString().indexOf('\uFFFC');
              assertTrue(where + ": the eyes after the text", eyes > 0);
              assertTrue(where + ": the eyes on the text's last line or the next",
                  layout.getLineForOffset(eyes) == layout.getLineCount() - 1);
              if (scale == 1f && width >= 700)
                assertEquals(where + ": on one line", 1, layout.getLineCount());
              checked++;
            }
            view.performClick();  // round to Normal again for the next width
          }
        }
      }
    assertEquals("languages, font sizes, levels and widths", 11 * 3 * 5 * 4, checked);
  }

  /** The installed version left of the update button (0.99.18, the project owner, 15.11.2026, as in the OUYA menu),
   *  also while the button is hidden, with real text sizes: in every language and font scale the version stays on one
   *  line beside the buttons (on the side away from "Back to the game"; right to left mirrored), the buttons as
   *  before on one line each, and the header inside the window from a handheld's 320 dp up. */
  @Test public void versionLeftOfTheUpdateButton() throws Exception {
    int checked = 0;
    for (String language : new String[] {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"})
      for (float scale : new float[] {1f, 1.3f, 1.5f}) {
        try (var controller = Robolectric.buildActivity(Activity.class).create()) {
          Activity activity = controller.get();
          Configuration config = new Configuration(activity.getResources().getConfiguration());
          config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
          activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
          CoreMenuLayoutTest.setVersion(activity, "0.99.18-rc-core");
          CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
          View root = (View) CoreMenuLayoutTest.field(menu, "root");
          root.setLayoutDirection(config.getLayoutDirection());
          Button update = (Button) CoreMenuLayoutTest.field(menu, "update");
          Button resume = (Button) CoreMenuLayoutTest.field(menu, "resume");
          TextView version = (TextView) CoreMenuLayoutTest.field(menu, "versionLabel");
          boolean rtl = config.getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
          float density = activity.getResources().getDisplayMetrics().density;
          for (int state : new int[] {AppUpdate.UNKNOWN, AppUpdate.CURRENT, AppUpdate.OLDER}) {
            menu.showUpdate(state);
            String where = language + "/" + scale + "/state " + state;
            View header = ((android.view.ViewGroup) root).getChildAt(0);
            View beside = state == AppUpdate.UNKNOWN ? resume : update;  // the button the version is next to
            for (int width : new int[] {320, 480, 700, 960}) {
              CoreMenuLayoutTest.measure(root, Math.round(width * density), Math.round(300 * density));
              assertTrue(where + "/" + width + ": the header inside the window", header.getRight() <= root.getWidth()
                  && header.getBottom() <= root.getHeight());
              CoreMenuLayoutTest.checkBounds(header, where + "/" + width);
              assertEquals(where + "/" + width + ": the version on one line", 1, version.getLayout().getLineCount());
              int besideLeft = CoreMenuLayoutTest.leftIn(beside, header);
              int buttonsBottom = CoreMenuLayoutTest.topIn(resume, header) + resume.getHeight();
              if (version.getTop() >= buttonsBottom) {
                // under the buttons: only where a narrow window leaves no room beside them
                assertTrue(where + "/" + width + ": the version under the buttons only in a narrow window", width < 680 * scale);
              } else {
                assertTrue(where + "/" + width + ": the version beside the buttons",
                    rtl ? version.getLeft() >= besideLeft + beside.getWidth() : version.getRight() <= besideLeft);
              }
              if (width >= 700) {
                if (state != AppUpdate.UNKNOWN)
                  assertEquals(where + "/" + width + ": the update button on one line", 1, update.getLayout().getLineCount());
                assertEquals(where + "/" + width + ": Back to the game on one line", 1, resume.getLayout().getLineCount());
                checked++;
              }
            }
          }
        }
      }
    assertTrue("languages, font sizes and states checked", checked >= 11 * 3 * 3 * 2);
  }

  /** The note under "Source and updates" (the project owner, 15.11.2026), with real text widths: in every language
   *  and font scale, from a handheld's 320 dp up, it stands under the link, whole (nothing cut), inside the page, each
   *  line centred, its second sentence on a line of its own; at the normal font size from 700 dp on two lines. */
  private static TextView textIn(View view, String text) {
    if (view instanceof TextView && !(view instanceof Button) && ((TextView) view).getText().toString().equals(text))
      return (TextView) view;
    if (view instanceof android.view.ViewGroup) {
      android.view.ViewGroup g = (android.view.ViewGroup) view;
      for (int i = 0; i < g.getChildCount(); i++) {
        TextView t = textIn(g.getChildAt(i), text);
        if (t != null) return t;
      }
    }
    return null;
  }

  @Test public void sourceNoteCentredUnderTheLink() throws Exception {
    Method show = CoreMenu.class.getDeclaredMethod("showPage", int.class, boolean.class); show.setAccessible(true);
    int checked = 0;
    for (String language : new String[] {"en", "de", "es", "fr", "it", "pt", "ru", "ar", "zh", "hi", "in"})
      for (float scale : new float[] {1f, 1.3f, 1.5f}) {
        try (var controller = Robolectric.buildActivity(Activity.class).create()) {
          Activity activity = controller.get();
          Configuration config = new Configuration(activity.getResources().getConfiguration());
          config.setLocale(Locale.forLanguageTag(language)); config.fontScale = scale;
          activity.getResources().updateConfiguration(config, activity.getResources().getDisplayMetrics());
          CoreMenu menu = new CoreMenu(activity, null, false, 1920, 1080);
          show.invoke(menu, 6, false);  // MANAGEMENT
          View root = (View) CoreMenuLayoutTest.field(menu, "root");
          root.setLayoutDirection(config.getLayoutDirection());
          String only = activity.getString(R.string.ingame_source_only);
          String avoid = activity.getString(R.string.ingame_source_avoid);
          for (String kept : new String[] {"CubeEvil4", "Android", "OUYA/Potato"})
            assertTrue(language + ": keeps " + kept, only.contains(kept));
          TextView note = textIn(root, only + "\n" + avoid);
          assertNotNull(language + ": the note", note);
          Button link = withLabel(root, activity.getString(R.string.ingame_source_line, CoreMenu.SOURCE_URL));
          assertNotNull(language + ": the link", link);
          float density = activity.getResources().getDisplayMetrics().density;
          for (int width : new int[] {320, 480, 700, 960}) {
            String where = language + "/" + scale + "/" + width;
            CoreMenuLayoutTest.measure(root, Math.round(width * density), Math.round(300 * density));
            View page = (View) note.getParent();
            assertTrue(where + ": under the link", CoreMenuLayoutTest.topIn(note, page)
                >= CoreMenuLayoutTest.topIn(link, page) + link.getHeight());
            assertTrue(where + ": inside the page", note.getLeft() >= 0 && note.getRight() <= page.getWidth());
            CoreMenuLayoutTest.checkBounds(note, where);
            android.text.Layout layout = note.getLayout();
            int second = only.length() + 1;
            assertEquals(where + ": the second sentence on a line of its own", second,
                layout.getLineStart(layout.getLineForOffset(second)));
            int inner = note.getWidth() - note.getTotalPaddingLeft() - note.getTotalPaddingRight();
            for (int line = 0; line < layout.getLineCount(); line++) {
              float left = layout.getLineLeft(line), right = inner - layout.getLineRight(line);
              assertTrue(where + ": line " + line + " centred (" + left + " / " + right + ")", Math.abs(left - right) <= 2);
            }
            if (scale == 1f && width >= 700) assertEquals(where + ": two lines", 2, layout.getLineCount());
            checked++;
          }
        }
      }
    assertEquals("languages, font sizes and widths", 11 * 3 * 4, checked);
  }

}
