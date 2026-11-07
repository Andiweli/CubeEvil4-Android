package com.ast.re4decomp;

import static org.junit.Assert.*;

import org.junit.Test;

/** DolphinCard on a small card built here the way the SDK lays one out (4 Mbit, 64 blocks, big-endian). */
public class DolphinCardTest {
  private static final int B = DolphinCard.BLOCK;

  private static void half(byte[] b, int at, int v) {
    b[at] = (byte) (v >> 8);
    b[at + 1] = (byte) v;
  }

  private static void sum(byte[] b, int at, int bytes, int stored) {
    int s = 0, n = 0;
    for (int i = 0; i < bytes; i += 2) {
      int w = (b[at + i] & 255) << 8 | b[at + i + 1] & 255;
      s = (s + w) & 0xFFFF;
      n = (n + (~w & 0xFFFF)) & 0xFFFF;
    }
    half(b, stored, s == 0xFFFF ? 0 : s);
    half(b, stored + 2, n == 0xFFFF ? 0 : n);
  }

  /** Two files of this game (one of them over blocks 9, 6), one of another game; directory copy 2 newer. */
  static byte[] card() {
    byte[] c = new byte[0x80000];
    half(c, 0x22, 4);
    sum(c, 0, 0x1FC, 0x1FC);
    for (int copy = 1; copy <= 2; copy++) {
      java.util.Arrays.fill(c, copy * B, copy * B + 127 * 64, (byte) 0xFF);
    }
    entry(c, 2, 0, "G4BP08", "bh4_data00", 9, 2);
    entry(c, 2, 1, "GZLE01", "gczelda", 5, 1);
    entry(c, 2, 2, "G4BE08", "bh4_system", 7, 1);
    entry(c, 1, 0, "G4BE08", "old", 5, 1);  // the older copy
    half(c, 1 * B + 0x1FFA, 4);
    half(c, 2 * B + 0x1FFA, 5);
    sum(c, B, 0x1FFC, B + 0x1FFC);
    sum(c, 2 * B, 0x1FFC, 2 * B + 0x1FFC);
    for (int copy = 3; copy <= 4; copy++) {
      int f = copy * B;
      half(c, f + 4, copy);
      half(c, f + 9 * 2, 6);
      half(c, f + 6 * 2, 0xFFFF);
      half(c, f + 5 * 2, 0xFFFF);
      half(c, f + 7 * 2, 0xFFFF);
      sum(c, f + 4, B - 4, f);
    }
    for (int block = 5; block < 10; block++) c[block * B] = (byte) block;
    return c;
  }

  private static void entry(byte[] c, int copy, int i, String id, String name, int start, int length) {
    int e = copy * B + i * 64;
    java.util.Arrays.fill(c, e, e + 64, (byte) 0);
    System.arraycopy(id.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1), 0, c, e, 6);
    System.arraycopy(name.getBytes(java.nio.charset.StandardCharsets.ISO_8859_1), 0, c, e + 8, name.length());
    half(c, e + 0x36, start);
    half(c, e + 0x38, length);
  }

  @Test public void splitsTheFilesOfThisGame() {
    byte[] c = card();
    assertTrue(DolphinCard.isCard(c));
    DolphinCard.Files f = DolphinCard.split(c);
    assertEquals(2, f.gci.size());
    assertEquals(1, f.otherGames);
    byte[] save = f.gci.get(0);
    assertEquals(0x40 + 2 * B, save.length);
    assertEquals("G4BP08", new String(save, 0, 6, java.nio.charset.StandardCharsets.ISO_8859_1));
    assertEquals(9, save[0x40]);
    assertEquals(6, save[0x40 + B]);
    assertEquals(7, f.gci.get(1)[0x40]);
  }

  @Test public void takesTheCopyWhoseChecksumHolds() {
    byte[] c = card();
    c[2 * B + 0x100] ^= 1;  // the newer directory broken: the older one counts
    DolphinCard.Files f = DolphinCard.split(c);
    assertEquals(1, f.gci.size());
    assertEquals('o', f.gci.get(0)[8]);
  }

  @Test public void aGciOrOtherFileIsNoCard() {
    assertFalse(DolphinCard.isCard(new byte[0x40 + 8 * B]));
    byte[] c = card();
    c[0x10] ^= 1;
    assertFalse(DolphinCard.isCard(c));
  }

  @Test(expected = IllegalArgumentException.class) public void aChainOutOfTheCardIsRefused() {
    byte[] c = card();
    for (int copy = 3; copy <= 4; copy++) {
      half(c, copy * B + 9 * 2, 200);
      sum(c, copy * B + 4, B - 4, copy * B);
    }
    DolphinCard.split(c);
  }
}
