package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.Application;
import android.content.res.Configuration;
import android.view.View;
import android.widget.Button;
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

}
