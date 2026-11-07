package com.ast.re4decomp;

import static org.junit.Assert.*;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import org.junit.Test;

/** SaveImport: a .gci, a whole card and ZIP files of both, without the core. */
public class SaveImportTest {
  private static byte[] gci(String name) {
    byte[] g = new byte[0x40 + DolphinCard.BLOCK];
    System.arraycopy("G4BP08".getBytes(java.nio.charset.StandardCharsets.ISO_8859_1), 0, g, 0, 6);
    System.arraycopy(name.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1), 0, g, 8, name.length());
    return g;
  }

  private static byte[] zip(Object... nameAndBytes) throws IOException {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    try (ZipOutputStream z = new ZipOutputStream(out)) {
      for (int i = 0; i < nameAndBytes.length; i += 2) {
        z.putNextEntry(new ZipEntry((String) nameAndBytes[i]));
        z.write((byte[]) nameAndBytes[i + 1]);
        z.closeEntry();
      }
    }
    return out.toByteArray();
  }

  @Test public void aGciStaysAsItIs() {
    byte[] g = gci("bh4_data00");
    List<byte[]> f = SaveImport.files(g, new StringBuilder());
    assertEquals(1, f.size());
    assertSame(g, f.get(0));
  }

  @Test public void aCardGivesItsFiles() {
    StringBuilder log = new StringBuilder();
    assertEquals(2, SaveImport.files(DolphinCardTest.card(), log).size());
    assertTrue(log.toString().contains("1 of other games"));
  }

  @Test public void aZipOfAGciFolder() throws IOException {
    byte[] z = zip("Card A/08-G4BP-bh4_data00.gci", gci("bh4_data00"), "Card A/08-G4BP-bh4_system.GCI", gci("bh4_system"),
        "__MACOSX/Card A/._08-G4BP-bh4_data00.gci", new byte[100], "Card A/.DS_Store", new byte[10],
        "readme.txt", "hello".getBytes(java.nio.charset.StandardCharsets.US_ASCII));
    StringBuilder log = new StringBuilder();
    List<byte[]> f = SaveImport.files(z, log);
    assertEquals(2, f.size());
    assertEquals('b', f.get(0)[8]);
    assertTrue(log.toString(), log.toString().contains("1 other files left out"));
  }

  @Test public void aZipWithACard() throws IOException {
    List<byte[]> f = SaveImport.files(zip("MemoryCardA.EUR.raw", DolphinCardTest.card()), new StringBuilder());
    assertEquals(2, f.size());
  }

  @Test public void aZipWithoutSavesIsRefused() throws IOException {
    byte[] z = zip("readme.txt", new byte[5]);
    try {
      SaveImport.files(z, new StringBuilder());
      fail();
    } catch (IllegalArgumentException e) {
      assertTrue(e.getMessage().contains("ZIP"));
    }
  }

  // the touch layout of an export (TouchLayoutFile; wish of the project owner, 07.11.2026)
  private static final byte[] TOUCH = ("{\n  \"" + TouchLayoutFile.MARK + "\": 1,\n  \"layout\": {}\n}")
      .getBytes(java.nio.charset.StandardCharsets.UTF_8);

  @Test public void aTouchLayoutAlone() {
    List<byte[]> touch = new java.util.ArrayList<>();
    StringBuilder log = new StringBuilder();
    assertTrue(SaveImport.files(TOUCH, log, touch).isEmpty());
    assertEquals(1, touch.size());
    assertSame(TOUCH, touch.get(0));
    assertEquals("touch layout file", log.toString());
  }

  @Test public void aTouchLayoutInAnExportZip() throws IOException {
    List<byte[]> touch = new java.util.ArrayList<>();
    StringBuilder log = new StringBuilder();
    List<byte[]> f = SaveImport.files(zip("export/08-G4BP-bh4_data00.gci", gci("bh4_data00"),
        "export/" + TouchLayoutFile.NAME, TOUCH), log, touch);
    assertEquals(1, f.size());
    assertEquals(1, touch.size());
    assertTrue(log.toString(), log.toString().contains("1 touch layouts, 0 other files left out"));
    // a ZIP of the layout alone is no error when the caller takes layouts
    assertTrue(SaveImport.files(zip(TouchLayoutFile.NAME, TOUCH), new StringBuilder(), touch).isEmpty());
    assertEquals(2, touch.size());
  }

  @Test public void withoutTheListTheLayoutIsNoSave() throws IOException {
    try {
      SaveImport.files(zip(TouchLayoutFile.NAME, TOUCH), new StringBuilder());
      fail();
    } catch (IllegalArgumentException e) {
      assertTrue(e.getMessage().contains("ZIP"));
    }
    // any other JSON is no touch layout
    assertFalse(TouchLayoutFile.is("{\"format\": 2}".getBytes(java.nio.charset.StandardCharsets.UTF_8)));
  }
}
