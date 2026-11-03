package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import android.content.ContextWrapper;
import java.io.File;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/** AppStorage: the internal folder when the shared storage's app folder fails (Android 16, 03.11.2026). */
@RunWith(RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class AppStorageTest {
  /** A context whose getExternalFilesDir gives null, throws, or works. */
  private static Context context(int external) {
    return new ContextWrapper(RuntimeEnvironment.getApplication()) {
      @Override public File getExternalFilesDir(String type) {
        if (external == 0) return null;
        if (external == 1) throw new IllegalStateException("Failed to prepare /storage/emulated/0/Android/data//files");
        return super.getExternalFilesDir(type);
      }
    };
  }

  @After public void forget() {
    AppStorage.forget();
    new File(RuntimeEnvironment.getApplication().getFilesDir(), "game").delete();
  }

  @Test public void sharedFolderWhenItWorks() {
    Context c = context(2);
    assertEquals(c.getExternalFilesDir(null), AppStorage.base(c));
  }

  @Test public void internalFolderWhenTheSharedOneIsNull() {
    Context c = context(0);
    assertEquals(c.getFilesDir(), AppStorage.base(c));
    assertTrue(AppStorage.describe(c).startsWith("the app's internal folder"));
  }

  @Test public void internalFolderWhenTheSharedOneThrows() {
    Context c = context(1);
    assertEquals(c.getFilesDir(), AppStorage.base(c));
  }

  @Test public void internalFolderStaysOnceItHoldsTheGame() {
    Context c = context(2);
    assertTrue(new File(c.getFilesDir(), "game").mkdirs());
    assertEquals(c.getFilesDir(), AppStorage.base(c));
  }

  @Test public void importManagerHasARootWithoutTheSharedFolder() {
    ImportManager m = new ImportManager(context(0));
    assertEquals(new File(RuntimeEnvironment.getApplication().getFilesDir(), "game"), m.root);
    assertTrue(m.root.isDirectory());
    assertFalse(m.status.contains("unavailable"));
  }
}
