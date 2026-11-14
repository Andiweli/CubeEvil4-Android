package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Application;
import android.app.Dialog;
import android.view.WindowManager;
import java.io.File;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowDialog;

/**
 * The app menu, its questions and the messages over the game keep the game's full screen (wish of the project
 * owner, 11.11.2026: the navigation bar came with the app menu on a Samsung): Immersive.show.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = {28, 33}, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class ImmersiveTest {
  private static void checkFullScreen(Dialog dialog, String what) {
    assertTrue(what + " shows", dialog.isShowing());
    int flags = dialog.getWindow().getDecorView().getSystemUiVisibility();
    assertEquals(what + ": the bars hidden (sticky)", Immersive.FLAGS, flags & Immersive.FLAGS);
    assertEquals(what + ": focusable again for keys and controllers", 0,
        dialog.getWindow().getAttributes().flags & WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE);
  }

  private static void idle() { TestLooper.idle(); }

  @Test public void aQuestionKeepsTheFullScreen() throws Exception {
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      AlertDialog dialog = new AlertDialog.Builder(controller.get()).setMessage("?")
          .setPositiveButton(android.R.string.ok, null).create();
      assertSame(dialog, Immersive.show(dialog));
      idle();
      checkFullScreen(dialog, "a question");
      // the bars asked away again when the window gets the focus back
      dialog.getWindow().getDecorView().setSystemUiVisibility(0);
      Object observer = dialog.getWindow().getDecorView().getViewTreeObserver();
      Method focus = observer.getClass().getDeclaredMethod("dispatchOnWindowFocusChange", boolean.class);
      focus.setAccessible(true);
      focus.invoke(observer, true);
      assertEquals("again after the focus came back", Immersive.FLAGS,
          dialog.getWindow().getDecorView().getSystemUiVisibility() & Immersive.FLAGS);
    }
  }

  @Test public void theMenuAndItsQuestionsKeepTheFullScreen() throws Exception {
    CoreMenu.Host host = (CoreMenu.Host) Proxy.newProxyInstance(CoreMenu.Host.class.getClassLoader(),
        new Class<?>[] {CoreMenu.Host.class},
        (proxy, method, args) -> method.getReturnType() == boolean.class ? Boolean.TRUE
            : method.getReturnType() == float.class ? (Object) 1f : method.getReturnType() == int.class ? (Object) 0
            : method.getReturnType() == double.class ? (Object) 60.0 : null);
    try (var controller = Robolectric.buildActivity(Activity.class).setup()) {
      CoreMenu menu = new CoreMenu(controller.get(), host, false, 1920, 1080);
      Immersive.show(menu);
      idle();
      checkFullScreen(menu, "the app menu");
      // its questions: "Import other discs", "Clear shader cache", "Start the benchmark?"
      for (String ask : new String[] {"askImportDiscs", "askClearShaders", "askBenchmark"}) {
        Method m = CoreMenu.class.getDeclaredMethod(ask); m.setAccessible(true);
        m.invoke(menu);
        idle();
        Dialog question = ShadowDialog.getLatestDialog();
        assertNotSame(ask, menu, question);
        checkFullScreen(question, ask);
        question.dismiss();
        idle();
      }
      checkFullScreen(menu, "the app menu after its questions");
      menu.dismiss();
    }
  }

  /** Every dialog of the app menu and of the game's screen goes through Immersive.show, not dialog.show(). */
  @Test public void noDialogShowsWithoutIt() throws Exception {
    for (String name : new String[] {"CoreMenu", "CoreActivity"}) {
      File source = new File("src/main/java/com/ast/re4decomp/" + name + ".java");
      if (!source.exists()) source = new File("app/" + source.getPath());
      assertTrue(source.getPath(), source.exists());
      List<String> lines = Files.readAllLines(source.toPath(), StandardCharsets.UTF_8);
      for (int i = 0; i < lines.size(); i++) {
        String line = lines.get(i);
        if (!line.contains(".show()") || line.contains("Toast")) continue;
        fail(name + ".java:" + (i + 1) + " shows a window without Immersive.show: " + line.trim());
      }
    }
  }
}
