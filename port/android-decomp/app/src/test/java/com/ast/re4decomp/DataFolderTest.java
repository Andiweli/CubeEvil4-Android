package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.Manifest;
import android.app.Application;
import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;

/**
 * DataFolder (1.0, the flavour sideload): the picker's folder as a path, the folders that cannot be chosen, the
 * write check, moving the app's files, and AppStorage taking the chosen folder or falling back to the app's own.
 */
@RunWith(RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class DataFolderTest {
  private static final String PRIMARY = "/storage/emulated/0";
  private boolean wasEnabled;
  private File temp;

  @Before public void setUp() throws IOException {
    wasEnabled = DataFolder.enabled;
    temp = Files.createTempDirectory("datafolder").toFile();
  }

  @After public void tearDown() {
    DataFolder.enabled = wasEnabled;
    DataFolder.choose(RuntimeEnvironment.getApplication(), null);
    AppStorage.forget();
    delete(temp);
  }

  private static void delete(File f) {
    File[] all = f.listFiles();
    if (all != null) for (File c : all) delete(c);
    f.delete();
  }

  private static File write(File f, int bytes) throws IOException {
    f.getParentFile().mkdirs();
    try (FileOutputStream out = new FileOutputStream(f)) { out.write(new byte[bytes]); }
    return f;
  }

  @Test public void theFlavoursOtherThanSideloadHaveNoDataFolderOfTheirOwn() {
    // the unit tests run for core and classic (BuildConfig false), and for sideload (true)
    assertEquals(BuildConfig.CUSTOM_DATA_FOLDER, BuildConfig.FLAVOR.equals("sideload"));
    assertEquals(BuildConfig.CUSTOM_DATA_FOLDER, wasEnabled);
  }

  @Test public void thePickersFolderAsAPath() {
    String s = DataFolder.STORAGE_AUTHORITY;
    assertEquals(PRIMARY + "/Games/RE4", DataFolder.pathOfTree(s, "primary:Games/RE4", PRIMARY));
    assertEquals(PRIMARY, DataFolder.pathOfTree(s, "primary:", PRIMARY));
    assertEquals("/storage/1234-ABCD/RE4", DataFolder.pathOfTree(s, "1234-ABCD:RE4", PRIMARY));
    assertEquals("/storage/1234-ABCD", DataFolder.pathOfTree(s, "1234-ABCD:", PRIMARY));
    assertEquals(PRIMARY + "/a/b", DataFolder.pathOfTree(s, "primary:a//b/", PRIMARY));
    String d = DataFolder.DOWNLOADS_AUTHORITY;
    assertEquals(PRIMARY + "/Download/x", DataFolder.pathOfTree(d, "raw:" + PRIMARY + "/Download/x", PRIMARY));
    assertEquals(PRIMARY + "/Download", DataFolder.pathOfTree(d, "downloads", PRIMARY));
    // a cloud folder, another app's documents, nonsense: no path
    assertNull(DataFolder.pathOfTree("com.google.android.apps.docs.storage", "acc=1;doc=x", PRIMARY));
    assertNull(DataFolder.pathOfTree(d, "msf:42", PRIMARY));
    assertNull(DataFolder.pathOfTree(s, "Games", PRIMARY));
    assertNull(DataFolder.pathOfTree(s, ":Games", PRIMARY));
    assertNull(DataFolder.pathOfTree(s, "../x:Games", PRIMARY));
    assertNull(DataFolder.pathOfTree(s, null, PRIMARY));
  }

  @Test public void theFoldersThatCannotBeChosen() {
    // a storage's root: its folder CubeEvil4
    assertArrayEquals(new String[] {PRIMARY + "/CubeEvil4", null}, DataFolder.check(PRIMARY, PRIMARY));
    assertArrayEquals(new String[] {PRIMARY + "/CubeEvil4", null}, DataFolder.check(PRIMARY + "/", PRIMARY));
    assertArrayEquals(new String[] {"/storage/1234-ABCD/CubeEvil4", null}, DataFolder.check("/storage/1234-ABCD", PRIMARY));
    // folders of the player's
    assertArrayEquals(new String[] {PRIMARY + "/Games/RE4", null}, DataFolder.check(PRIMARY + "/Games/RE4", PRIMARY));
    assertArrayEquals(new String[] {"/storage/1234-ABCD/RE4", null}, DataFolder.check("/storage/1234-ABCD/RE4", PRIMARY));
    assertArrayEquals(new String[] {PRIMARY + "/Android/media/x", null}, DataFolder.check(PRIMARY + "/Android/media/x", PRIMARY));
    assertArrayEquals(new String[] {PRIMARY + "/AndroidData", null}, DataFolder.check(PRIMARY + "/AndroidData", PRIMARY));
    // Android, Android/data and Android/obb, any app's, on any storage
    for (String p : new String[] {PRIMARY + "/Android", PRIMARY + "/Android/data", PRIMARY + "/android/DATA/x",
        PRIMARY + "/Android/data/com.ast.cubeevil4/files", PRIMARY + "/Android/data/org.other.app",
        PRIMARY + "/Android/obb", PRIMARY + "/Android/obb/com.ast.cubeevil4", "/storage/1234-ABCD/Android/data/x"}) {
      assertArrayEquals(p, new String[] {null, DataFolder.ANDROID}, DataFolder.check(p, PRIMARY));
    }
    // not on the device's storage, an SD card or a USB drive
    for (String p : new String[] {null, "", "relative", "/data/data/com.ast.cubeevil4", "/sdcard/Games", "/storage",
        "/storage/emulated", "/storage/emulated/10/x", "/storage/self/primary"}) {
      assertArrayEquals(String.valueOf(p), new String[] {null, DataFolder.NOT_LOCAL}, DataFolder.check(p, PRIMARY));
    }
  }

  @Test public void aFolderMustBeWritable() throws IOException {
    File dir = new File(temp, "new/folder");
    assertNotNull(DataFolder.writable(dir));  // missing
    assertNull(DataFolder.prepare(dir));      // made, a file written and deleted
    assertTrue(dir.isDirectory());
    assertEquals(0, dir.list().length);
    File file = write(new File(temp, "a-file"), 3);
    assertNotNull(DataFolder.prepare(file));  // not a folder
  }

  @Test public void movingRenamesTheAppsFilesAndNothingElse() throws IOException {
    File from = new File(temp, "from"), to = new File(temp, "to");
    write(new File(from, "game/disc1/sys/main.dol"), 1000);
    write(new File(from, "game/card.raw"), 500);
    write(new File(from, "textures/pack.c4tex"), 200);
    write(new File(from, "music.mp3"), 50);  // not the app's
    assertTrue(to.mkdirs());
    assertEquals(Arrays.asList("game", "textures"), DataFolder.present(from));
    assertNull(DataFolder.move(from, to, null));
    assertEquals(1000, new File(to, "game/disc1/sys/main.dol").length());
    assertEquals(500, new File(to, "game/card.raw").length());
    assertEquals(200, new File(to, "textures/pack.c4tex").length());
    assertTrue(DataFolder.present(from).isEmpty());
    assertTrue(new File(from, "music.mp3").isFile());
    assertFalse(new File(to, "music.mp3").exists());
  }

  @Test public void movingBetweenStoragesCopiesThenDeletesTheOriginals() throws IOException {
    File from = new File(temp, "from"), to = new File(temp, "to");
    write(new File(from, "game/disc2/sys/fst.bin"), 3000);
    write(new File(from, "logs/core.log"), 100);
    assertTrue(to.mkdirs());
    List<long[]> steps = new ArrayList<>();
    assertNull(DataFolder.move(from, to, (done, total) -> steps.add(new long[] {done, total}), false));
    assertEquals(3000, new File(to, "game/disc2/sys/fst.bin").length());
    assertEquals(100, new File(to, "logs/core.log").length());
    assertFalse(new File(from, "game").exists());
    assertFalse(new File(from, "logs").exists());
    assertFalse(steps.isEmpty());
    long[] last = steps.get(steps.size() - 1);
    assertEquals(3100, last[0]);
    assertEquals(3100, last[1]);
  }

  @Test public void aFailedMoveLeavesEverythingAsItWas() throws IOException {
    File from = new File(temp, "from"), to = new File(temp, "to");
    write(new File(from, "game/disc1/a.bin"), 2000);
    write(new File(from, "game/disc1/b.bin"), 2000);
    assertTrue(to.mkdirs());
    // the copy fails half way (an error of the storage)
    String problem = DataFolder.move(from, to, (done, total) -> {
      if (done >= 2000) throw new IllegalStateException("the storage went away");
    }, false);
    assertEquals("the storage went away", problem);
    assertTrue(DataFolder.present(to).isEmpty());
    assertEquals(2000, new File(from, "game/disc1/a.bin").length());
    assertEquals(2000, new File(from, "game/disc1/b.bin").length());
  }

  @Test public void emptyFoldersAreNoData() throws IOException {
    // the imports make an empty "game" in a folder they look at (emulator, 16.11.2026: it kept the data from moving back)
    File from = new File(temp, "from"), to = new File(temp, "to");
    write(new File(from, "game/card.raw"), 10);
    assertTrue(new File(from, "logs/previous").mkdirs());
    assertTrue(new File(to, "game/disc1").mkdirs());
    assertEquals(Arrays.asList("game"), DataFolder.present(from));
    assertTrue(DataFolder.present(to).isEmpty());
    assertNull(DataFolder.move(from, to, null, false));
    assertEquals(10, new File(to, "game/card.raw").length());
    assertNull(DataFolder.move(new File(temp, "from2"), to, null));  // nothing there: nothing to do
  }

  @Test public void nothingMovesOverTheAppsFilesInTheNewFolder() throws IOException {
    File from = new File(temp, "from"), to = new File(temp, "to");
    write(new File(from, "game/card.raw"), 10);
    write(new File(to, "game/card.raw"), 20);
    assertNotNull(DataFolder.move(from, to, null));
    assertEquals(10, new File(from, "game/card.raw").length());
    assertEquals(20, new File(to, "game/card.raw").length());
  }

  @Test public void appStorageTakesTheChosenFolderWhileItCanBeUsed() {
    Context c = RuntimeEnvironment.getApplication();
    File standard = AppStorage.standard(c);
    File chosen = new File(temp, "Games/RE4");
    assertTrue(chosen.mkdirs());
    DataFolder.enabled = true;
    DataFolder.choose(c, chosen.getAbsolutePath());
    // no permission yet: the app's own folder, the choice kept
    AppStorage.forget();
    assertEquals(standard, AppStorage.base(c));
    assertNotNull(AppStorage.fallback(c));
    assertEquals(chosen.getAbsolutePath(), DataFolder.chosen(c));
    // the permission: the chosen folder, also for the imports (the core's path)
    Shadows.shadowOf((Application) c).grantPermissions(Manifest.permission.WRITE_EXTERNAL_STORAGE);
    AppStorage.forget();
    assertEquals(chosen, AppStorage.base(c));
    assertNull(AppStorage.fallback(c));
    assertEquals(new File(chosen, "game"), new ImportManager(c).root);
    assertTrue(AppStorage.describe(c).startsWith("the player's folder"));
    // the folder gone (an SD card taken out): the app's own folder, nothing deleted, the choice kept
    delete(chosen);  // (with the "game" the imports made)
    assertFalse(chosen.exists());
    AppStorage.forget();
    assertEquals(standard, AppStorage.base(c));
    assertTrue(AppStorage.fallback(c).contains("missing"));
    assertEquals(chosen.getAbsolutePath(), DataFolder.chosen(c));
    assertTrue(AppStorage.describe(c).contains("cannot be used"));
    // without the option (core, car, classic) a stored folder means nothing
    assertTrue(chosen.mkdirs());
    DataFolder.enabled = false;
    AppStorage.forget();
    assertNull(DataFolder.chosen(c));
    assertEquals(standard, AppStorage.base(c));
    assertNull(AppStorage.fallback(c));
  }
}
