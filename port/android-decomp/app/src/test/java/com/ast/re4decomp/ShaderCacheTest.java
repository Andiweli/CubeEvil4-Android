package com.ast.re4decomp;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

/** ShaderCache (0.99.4): the old cache folder moved over once, merged, cleared, and its description for the report. */
public class ShaderCacheTest {
  @Rule public TemporaryFolder tmp = new TemporaryFolder();

  private static void write(File f, int bytes) throws IOException {
    f.getParentFile().mkdirs();
    try (FileOutputStream out = new FileOutputStream(f)) {
      out.write(new byte[bytes]);
    }
  }

  @Test public void oldFolderMovesOverWhole() throws Exception {
    File old = new File(tmp.getRoot(), "cache/gx-programs"), now = new File(tmp.getRoot(), "no_backup/shader-cache");
    write(new File(old, "0123456789abcdef.bin"), 100);
    write(new File(old, "0123456789abcdef.key"), 10);
    write(new File(old, "vk-spirv.bin"), 1000);
    String note = ShaderCache.prepare(old, now, false);
    assertTrue(note, note.startsWith("moved from the cache folder (3 files"));
    assertFalse("the old folder is gone", old.exists());
    assertEquals(100, new File(now, "0123456789abcdef.bin").length());
    assertEquals(1000, new File(now, "vk-spirv.bin").length());
    assertEquals("in place", ShaderCache.prepare(old, now, false));
  }

  @Test public void bothThereMergesAndKeepsTheNewFiles() throws Exception {
    File old = new File(tmp.getRoot(), "cache/gx-programs"), now = new File(tmp.getRoot(), "no_backup/shader-cache");
    write(new File(old, "a.bin"), 5);
    write(new File(old, "b.bin"), 7);
    write(new File(old, "vk-pipelines.bin.tmp"), 3);
    write(new File(now, "a.bin"), 9);
    String note = ShaderCache.prepare(old, now, false);
    assertTrue(note, note.startsWith("merged from the cache folder (1 file"));
    assertFalse(old.exists());
    assertEquals("the new folder's file wins", 9, new File(now, "a.bin").length());
    assertEquals(7, new File(now, "b.bin").length());
    assertFalse("an unfinished file is not taken", new File(now, "vk-pipelines.bin.tmp").exists());
  }

  @Test public void clearEmptiesBothFolders() throws Exception {
    File old = new File(tmp.getRoot(), "cache/gx-programs"), now = new File(tmp.getRoot(), "no_backup/shader-cache");
    write(new File(old, "a.bin"), 5);
    write(new File(now, "b.bin"), 7);
    write(new File(now, "vk-spirv.bin"), 11);
    String note = ShaderCache.prepare(old, now, true);
    assertTrue(note, note.startsWith("cleared as asked in the menu (3 files"));
    assertFalse(old.exists());
    assertTrue(now.isDirectory());
    String[] left = now.list();
    assertEquals(0, left == null ? -1 : left.length);
  }

  @Test public void describeNamesBothRenderers() throws Exception {
    File old = new File(tmp.getRoot(), "cache/gx-programs"), now = new File(tmp.getRoot(), "no_backup/shader-cache");
    write(new File(now, "0123456789abcdef.bin"), 1 << 20);
    write(new File(now, "0123456789abcdef.key"), 100);
    write(new File(now, "room-100.keys"), 200);
    write(new File(now, "room-100.vkpipes"), 300);
    write(new File(now, "vk-spirv.bin"), 2 << 20);
    write(new File(now, "vk-pipelines.bin"), 512 << 10);
    String d = ShaderCache.describe(now, old, "in place");
    assertTrue(d, d.startsWith("Shader cache: " + now.getAbsolutePath()));
    assertTrue(d, d.contains("This start: in place\n"));
    assertTrue(d, d.contains("OpenGL ES: 1 linked programs (1.0 MB), 1 keys (0.0 MB)\n"));
    assertTrue(d, d.contains("Both renderers: 1 rooms' program lists (0.0 MB)\n"));
    assertTrue(d, d.contains("Vulkan: SPIR-V store 2.0 MB, the driver's pipeline cache 0.5 MB, 1 rooms' pipeline lists"));
    assertTrue(d, d.contains("In all: 6 files, 3.5 MB\n"));
    assertTrue(d, d.contains("): gone\n"));
  }
}
