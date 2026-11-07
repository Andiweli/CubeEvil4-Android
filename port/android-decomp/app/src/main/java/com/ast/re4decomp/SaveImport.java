package com.ast.re4decomp;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * What the import of the app's menu takes from one chosen file, as .gci files for the core (runtime/gci.cpp): a .gci
 * as it is, the files of this game on a whole Dolphin card (.raw, .gcp; DolphinCard), or a ZIP holding either (a
 * Dolphin GCI folder or an export of the app, packed). Other files in a ZIP are left out.
 */
final class SaveImport {
  static final int MAX_GCI = 4 << 20, MAX_FILE = 16 << 20, MAX_ZIP = 64 << 20;

  private SaveImport() {}

  /** The .gci files in `data`; `log` gets one line on what was found. IllegalArgumentException when none. */
  static List<byte[]> files(byte[] data, StringBuilder log) {
    List<byte[]> out = new ArrayList<>();
    if (isZip(data)) {
      int skipped = 0, cards = 0;
      long total = 0;
      try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(data))) {
        for (ZipEntry e; (e = zip.getNextEntry()) != null; ) {
          String name = e.getName().replace('\\', '/');
          String base = name.substring(name.lastIndexOf('/') + 1).toLowerCase(Locale.ROOT);
          if (e.isDirectory() || name.startsWith("__MACOSX/") || base.startsWith(".")) continue;
          byte[] file = read(zip, MAX_FILE);
          total += file.length;
          if (total > MAX_ZIP) throw new IllegalArgumentException("the ZIP file holds more than 64 MB");
          if (DolphinCard.isCard(file)) {
            out.addAll(DolphinCard.split(file).gci);
            cards++;
          } else if (base.endsWith(".gci") && file.length <= MAX_GCI) {
            out.add(file);
          } else {
            skipped++;
          }
        }
      } catch (IOException e) {
        throw new IllegalArgumentException("the ZIP file cannot be read (" + e.getMessage() + ")");
      }
      log.append("ZIP file: ").append(out.size()).append(" files to import (").append(cards)
          .append(" memory cards in it), ").append(skipped).append(" other files left out");
      if (out.isEmpty()) throw new IllegalArgumentException("no saves (.gci, .raw) in the ZIP file");
      return out;
    }
    if (DolphinCard.isCard(data)) {
      DolphinCard.Files card = DolphinCard.split(data);
      log.append("memory card with ").append(card.gci.size()).append(" files of this game, ")
          .append(card.otherGames).append(" of other games");
      if (card.gci.isEmpty()) throw new IllegalArgumentException("no saves of this game on the memory card");
      return card.gci;
    }
    if (data.length > MAX_GCI) throw new IllegalArgumentException("too big for a .gci");
    out.add(data);
    return out;
  }

  /** "PK\3\4": the start of a ZIP file. */
  static boolean isZip(byte[] b) {
    return b.length >= 4 && b[0] == 'P' && b[1] == 'K' && b[2] == 3 && b[3] == 4;
  }

  private static byte[] read(ZipInputStream zip, int max) throws IOException {
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    byte[] buffer = new byte[65536];
    for (int n; (n = zip.read(buffer)) > 0; ) {
      out.write(buffer, 0, n);
      if (out.size() > max) throw new IOException("a file in it is larger than 16 MB");
    }
    return out.toByteArray();
  }
}
