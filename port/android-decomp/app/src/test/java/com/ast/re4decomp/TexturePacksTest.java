package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import android.net.Uri;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.CRC32;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * HD texture packs (TexturePacks, port/docs/textur-lader-plan.md): the header and index checks of a .c4tex file as the
 * PC tool writes it (c4tex.h), the GPU budget by device memory, the import's copy, and that deleting touches only the
 * textures folder (the discs and saves stay).
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class, shadows = CoreMenuLayoutTest.Bridge.class)
public class TexturePacksTest {
  /** A small pack as the tool writes it: header, info text, `entries` index entries over dummy data. */
  static byte[] pack(int entries) {
    String info = "name=CubeEvil4-textures-512\ntool=CubeEvil4 Texture Packer 1.0\ncreated=2026-11-12\nimages=" + entries + "\n";
    byte[] infoBytes = info.getBytes(StandardCharsets.UTF_8);
    int dataOffset = (128 + infoBytes.length + 15) / 16 * 16;
    int dataBytes = 64 * entries;
    int indexOffset = dataOffset + dataBytes;
    int size = indexOffset + 48 * entries;
    ByteBuffer b = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN);
    b.put("C4TEXPAK".getBytes(StandardCharsets.US_ASCII));
    b.putInt(8, 1).putInt(12, 128).putInt(16, entries).putInt(20, 48);
    b.putLong(24, indexOffset).putLong(32, dataOffset).putLong(40, size);
    b.putInt(48, 512).putInt(56, 128).putInt(60, infoBytes.length);
    for (int i = 0; i < infoBytes.length; i++) b.put(128 + i, infoBytes[i]);
    for (int e = 0; e < entries; e++) {
      int at = indexOffset + 48 * e;
      b.putLong(at, 0x1000L + e).putShort(at + 16, (short) 16).putShort(at + 18, (short) 16);
      b.put(at + 20, (byte) 14).put(at + 22, (byte) 1).put(at + 23, (byte) 1);
      b.putShort(at + 24, (short) 16).putShort(at + 26, (short) 16);
      b.putLong(at + 32, dataOffset + 64L * e).putInt(at + 40, 64);
    }
    byte[] out = b.array();
    CRC32 index = new CRC32();
    index.update(out, indexOffset, 48 * entries);
    b.putInt(52, (int) index.getValue());
    CRC32 head = new CRC32();
    head.update(out, 0, 64);
    b.putInt(64, (int) head.getValue());
    return out;
  }

  private static File write(File f, byte[] data) throws IOException {
    try (FileOutputStream out = new FileOutputStream(f)) {
      out.write(data);
    }
    return f;
  }

  @Test public void theHeaderIsChecked() {
    byte[] good = pack(3);
    assertNull(TexturePacks.headerProblem(good, good.length));
    assertNull(TexturePacks.headerProblem(good, -1));
    assertEquals(TexturePacks.INCOMPLETE, TexturePacks.headerProblem(good, good.length - 1).reason);
    byte[] other = good.clone();
    other[0] = 'X';
    assertEquals(TexturePacks.NOT_PACK, TexturePacks.headerProblem(other, good.length).reason);
    assertEquals(TexturePacks.NOT_PACK, TexturePacks.headerProblem(new byte[10], 10).reason);
    byte[] version = good.clone();
    version[8] = 2;
    assertEquals(TexturePacks.OTHER_VERSION, TexturePacks.headerProblem(version, good.length).reason);
    byte[] damaged = good.clone();
    damaged[50] ^= 1;  // the size field, under the header's checksum
    assertEquals(TexturePacks.DAMAGED, TexturePacks.headerProblem(damaged, good.length).reason);
  }

  @Test public void theIndexIsChecked() throws IOException {
    File dir = RuntimeEnvironment.getApplication().getCacheDir();
    TexturePacks.Info info = TexturePacks.check(write(new File(dir, "good.c4tex"), pack(5)));
    assertEquals(5, info.images);
    assertEquals(512, info.maxSide);
    assertEquals("CubeEvil4-textures-512", info.name);
    assertEquals("2026-11-12", info.created);
    byte[] bad = pack(5);
    bad[bad.length - 10] ^= 1;  // in the index
    try {
      TexturePacks.check(write(new File(dir, "bad.c4tex"), bad));
      fail("a damaged index must be found");
    } catch (TexturePacks.PackException e) {
      assertEquals(TexturePacks.DAMAGED, e.reason);
    }
  }

  @Test public void theBudgetFollowsTheMemory() {
    long gb = 1L << 30;
    assertEquals(256, TexturePacks.budgetFor(3 * gb, true));
    assertEquals(256, TexturePacks.budgetFor(4 * gb, true));
    assertEquals(512, TexturePacks.budgetFor(6 * gb, true));
    assertEquals(768, TexturePacks.budgetFor(8 * gb, true));
    assertEquals(192, TexturePacks.budgetFor(8 * gb, false));
    assertEquals(192, TexturePacks.budgetFor(4 * gb, false));
  }

  @Test public void importCopiesIntoTheTexturesFolderOnly() throws IOException {
    Context c = RuntimeEnvironment.getApplication();
    AppStorage.forget();
    File base = AppStorage.base(c);
    File game = new File(base, "game");
    assertTrue(game.isDirectory() || game.mkdirs());
    File disc = write(new File(game, "disc.bin"), new byte[] {1, 2, 3});
    File source = write(new File(c.getCacheDir(), "chosen.c4tex"), pack(4));
    TexturePacks.Info info = TexturePacks.importFrom(c, Uri.fromFile(source), new AtomicBoolean(), null);
    assertEquals(4, info.images);
    assertTrue(TexturePacks.file(c).isFile());
    assertFalse(new File(TexturePacks.folder(c), TexturePacks.PART).exists());
    assertEquals(4, TexturePacks.info(c).images);
    // a cut copy leaves the pack before as it was, and no part file
    byte[] cut = pack(6);
    File cutSource = write(new File(c.getCacheDir(), "cut.c4tex"), java.util.Arrays.copyOf(cut, cut.length - 20));
    try {
      TexturePacks.importFrom(c, Uri.fromFile(cutSource), new AtomicBoolean(), null);
      fail("a cut file must be refused");
    } catch (TexturePacks.PackException e) {
      assertEquals(TexturePacks.INCOMPLETE, e.reason);
    }
    assertEquals(4, TexturePacks.info(c).images);
    assertFalse(new File(TexturePacks.folder(c), TexturePacks.PART).exists());
    // cancelled: nothing changes
    try {
      TexturePacks.importFrom(c, Uri.fromFile(write(new File(c.getCacheDir(), "big.c4tex"), pack(40000))),
          new AtomicBoolean(true), null);
      fail("a cancelled import must stop");
    } catch (TexturePacks.PackException e) {
      assertEquals(TexturePacks.CANCELLED, e.reason);
    }
    assertEquals(4, TexturePacks.info(c).images);
    // deleting: the textures folder goes, the discs stay
    assertTrue(TexturePacks.delete(c));
    assertFalse(TexturePacks.folder(c).exists());
    assertNull(TexturePacks.info(c));
    assertTrue(disc.isFile());
    assertEquals("", CoreMenuLayoutTest.Bridge.texturePack);
  }
}
