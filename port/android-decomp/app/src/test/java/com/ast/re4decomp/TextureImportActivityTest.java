package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.AlertDialog;
import android.app.Application;
import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.List;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.Shadows;
import org.robolectric.android.controller.ActivityController;
import org.robolectric.annotation.Config;
import org.robolectric.shadows.ShadowAlertDialog;

/**
 * A pack handed over by the Texture Packer app ("Import into CubeEvil4", TextureImportActivity): its intent reaches
 * CubeEvil4, the import asks first, copies into the textures folder only, switches HD textures on and gives the pack
 * to the renderer; other URIs than content ones are refused.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class TextureImportActivityTest {
  static final String AUTHORITY = "com.ast.cubeevil4.texturepacker.test";

  /** The other app's file, as MediaStore would hand it over: its name and size, and the file to read. */
  public static class Provider extends ContentProvider {
    static File file;
    @Override public boolean onCreate() { return true; }
    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
      MatrixCursor c = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
      c.addRow(new Object[] {"CubeEvil4-textures-512.c4tex", file.length()});
      return c;
    }
    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
      return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }
    @Override public String getType(Uri uri) { return "application/octet-stream"; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] args) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) { return 0; }
  }

  @Before public void noAnimationLeftRunning() {
    TestLooper.stopAnimations("left running before " + getClass().getSimpleName());
    TexturePacks.importing = false;
  }

  private static Intent handOver(Uri uri) {
    return new Intent(TextureImportActivity.ACTION).setDataAndType(uri, "application/octet-stream")
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
  }

  @Test public void theTexturePackersIntentReachesTheImport() {
    Context c = RuntimeEnvironment.getApplication();
    List<ResolveInfo> found = c.getPackageManager().queryIntentActivities(
        handOver(Uri.parse("content://media/external/downloads/12")), 0);
    boolean import_ = false;
    for (ResolveInfo r : found) import_ |= r.activityInfo.name.endsWith(".TextureImportActivity");
    assertTrue("the Texture Packer's intent must reach TextureImportActivity", import_);
  }

  @Test public void onlyContentIsRead() {
    ActivityController<TextureImportActivity> a = Robolectric.buildActivity(TextureImportActivity.class,
        handOver(Uri.parse("file:///data/data/com.ast.cubeevil4/files/x.c4tex"))).setup();
    assertTrue(a.get().isFinishing());
  }

  @Test public void aHandedOverPackIsImportedAfterAsking() throws Exception {
    Context c = RuntimeEnvironment.getApplication();
    Assume.assumeTrue("core mode only", c.getResources().getBoolean(R.bool.core_mode));
    AppStorage.forget();
    TexturePacks.delete(c);
    c.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, Context.MODE_PRIVATE).edit()
        .putBoolean(TexturePacks.ON, false).commit();
    File game = new File(AppStorage.base(c), "game");
    assertTrue(game.isDirectory() || game.mkdirs());
    File disc = new File(game, "disc.bin");
    try (FileOutputStream out = new FileOutputStream(disc)) {
      out.write(new byte[] {1, 2, 3});
    }
    Provider.file = new File(c.getCacheDir(), "handed.c4tex");
    try (FileOutputStream out = new FileOutputStream(Provider.file)) {
      out.write(TexturePacksTest.pack(7));
    }
    Robolectric.setupContentProvider(Provider.class, AUTHORITY);
    Uri uri = Uri.parse("content://" + AUTHORITY + "/pack");

    ActivityController<TextureImportActivity> a = Robolectric.buildActivity(TextureImportActivity.class,
        handOver(uri)).setup();
    AlertDialog ask = (AlertDialog) ShadowAlertDialog.getLatestDialog();
    assertNotNull("it asks first", ask);
    assertTrue(Shadows.shadowOf(ask).getMessage().toString().contains("CubeEvil4-textures-512.c4tex"));
    assertFalse("nothing is copied before the answer", TexturePacks.file(c).exists());
    ask.getButton(AlertDialog.BUTTON_POSITIVE).performClick();
    long until = System.currentTimeMillis() + 10_000;
    do {
      TestLooper.idle();
      Thread.sleep(20);
      TestLooper.idle();
    } while (TexturePacks.importing && System.currentTimeMillis() < until);
    assertFalse("the import ends", TexturePacks.importing);
    assertEquals(7, TexturePacks.info(c).images);
    assertTrue(c.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, Context.MODE_PRIVATE)
        .getBoolean(TexturePacks.ON, false));
    assertEquals(TexturePacks.file(c).getAbsolutePath(), CoreMenuLayoutTest.Bridge.texturePack);
    assertTrue("the discs stay", disc.isFile());
    AlertDialog end = (AlertDialog) ShadowAlertDialog.getLatestDialog();
    assertTrue(Shadows.shadowOf(end).getMessage().toString().contains("7"));
    end.getButton(AlertDialog.BUTTON_POSITIVE).performClick();
    TestLooper.idle();
    assertTrue(a.get().isFinishing());
    TexturePacks.delete(c);
  }
}
