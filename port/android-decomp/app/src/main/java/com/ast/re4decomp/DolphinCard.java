package com.ast.re4decomp;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * A whole memory card as Dolphin keeps it (.raw, .gcp: the card's bytes as a GameCube card holds them, big-endian)
 * split into the .gci files of this game, which the import then takes one by one (runtime/gci.cpp: G4BE08 and the
 * European G4BP08). The card's layout is the SDK's (src/lib CARDCheck.c, CARDDir.c, CARDBlock.c): block 0 the header,
 * blocks 1/2 two copies of the directory, 3/4 two copies of the FAT; the copy whose checksum holds and whose check code
 * is newer counts. A file is its 64-byte directory entry (the .gci header as it is) and its blocks along the FAT.
 */
final class DolphinCard {
  static final int BLOCK = 0x2000;
  private static final int ENTRIES = 127, FIRST_DATA = 5;
  private static final String[] GAMES = {"G4BE08", "G4BP08"};

  /** The .gci files of this game, and how many files of other games the card holds. */
  static final class Files {
    final List<byte[]> gci = new ArrayList<>();
    int otherGames;
  }

  private DolphinCard() {}

  /**
   * Whether the bytes are a card image: 4 to 128 Mbit (512 KB .. 16 MB in steps of 512 KB), the header's checksum
   * holds and its size (Mbit) fits the file. A .gci is never that long and starts with a game code instead.
   */
  static boolean isCard(byte[] b) {
    if (b == null || b.length < 0x80000 || b.length > 0x1000000 || b.length % 0x80000 != 0) return false;
    if (!sumHolds(b, 0, 0x1FC, 0x1FC)) return false;
    int mbit = half(b, 0x22);
    return mbit > 0 && mbit * 0x20000 <= b.length;
  }

  /** The files of this game on the card; IllegalArgumentException when its directory or FAT is broken. */
  static Files split(byte[] card) {
    int blocks = half(card, 0x22) * 0x20000 / BLOCK;
    int dir = current(card, 1, true), fat = current(card, 3, false);
    if (dir < 0) throw new IllegalArgumentException("memory card: both copies of the directory are broken");
    if (fat < 0) throw new IllegalArgumentException("memory card: both copies of the FAT are broken");
    Files out = new Files();
    for (int i = 0; i < ENTRIES; i++) {
      int e = dir * BLOCK + i * 64;
      if (word(card, e) == 0xFFFFFFFF) continue;
      String id = new String(card, e, 6, java.nio.charset.StandardCharsets.ISO_8859_1);
      if (!Arrays.asList(GAMES).contains(id)) {
        out.otherGames++;
        continue;
      }
      int start = half(card, e + 0x36), length = half(card, e + 0x38);
      byte[] gci = new byte[0x40 + length * BLOCK];
      System.arraycopy(card, e, gci, 0, 0x40);
      int n = start;
      for (int k = 0; k < length; k++) {
        if (n < FIRST_DATA || n >= blocks) {
          throw new IllegalArgumentException("memory card: the blocks of file " + i + " leave the card");
        }
        System.arraycopy(card, n * BLOCK, gci, 0x40 + k * BLOCK, BLOCK);
        n = half(card, fat * BLOCK + n * 2);
      }
      out.gci.add(gci);
    }
    return out;
  }

  /** The current copy (first or first + 1) of the directory or the FAT, -1 when both are broken. */
  private static int current(byte[] b, int first, boolean directory) {
    int[] code = new int[2];
    boolean[] ok = new boolean[2];
    for (int i = 0; i < 2; i++) {
      int at = (first + i) * BLOCK;
      if (directory) {
        ok[i] = sumHolds(b, at, 0x1FFC, at + 0x1FFC);
        code[i] = half(b, at + 0x1FFA);
      } else {
        ok[i] = sumHolds(b, at + 4, BLOCK - 4, at);
        code[i] = half(b, at + 4);
      }
    }
    if (ok[0] && ok[1]) return (short) (code[0] - code[1]) >= 0 ? first : first + 1;
    return ok[0] ? first : ok[1] ? first + 1 : -1;
  }

  /** __CARDCheckSum: the sum of the u16 words and of their complements (0xFFFF counts as 0). */
  private static boolean sumHolds(byte[] b, int at, int bytes, int stored) {
    int sum = 0, inv = 0;
    for (int i = 0; i < bytes; i += 2) {
      int w = half(b, at + i);
      sum = (sum + w) & 0xFFFF;
      inv = (inv + (~w & 0xFFFF)) & 0xFFFF;
    }
    if (sum == 0xFFFF) sum = 0;
    if (inv == 0xFFFF) inv = 0;
    return sum == half(b, stored) && inv == half(b, stored + 2);
  }

  private static int half(byte[] b, int at) {
    return (b[at] & 255) << 8 | b[at + 1] & 255;
  }

  private static int word(byte[] b, int at) {
    return half(b, at) << 16 | half(b, at + 2);
  }
}
