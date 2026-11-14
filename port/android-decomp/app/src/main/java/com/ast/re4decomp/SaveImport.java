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
 * Dolphin GCI folder or an export of the app, packed). The touch layout of an export (TouchLayoutFile, a file of its
 * own or in the ZIP) goes to the caller's list. Since 0.99.15 also the parts of the app's own ZIP (PlayerBackup): its
 * manifest, its settings file and the instant save. Other files in a ZIP are left out.
 */
final class SaveImport {
  static final int MAX_GCI = 4 << 20, MAX_FILE = 16 << 20, MAX_ZIP = 64 << 20;

  private SaveImport() {}

  /** Everything of one chosen file. */
  static final class Found {
    final List<byte[]> gci = new ArrayList<>();
    final List<byte[]> touch = new ArrayList<>();
    /** PlayerBackup's manifest, settings file and instant save (the last of each kind; null when none). */
    byte[] manifest, settings, instant;

    /** Anything besides saves, a touch layout counted when `withTouch`. */
    boolean extras(boolean withTouch) {
      return (withTouch && !touch.isEmpty()) || settings != null || instant != null;
    }
  }

  /** The .gci files in `data`; `log` gets one line on what was found. IllegalArgumentException when none. */
  static List<byte[]> files(byte[] data, StringBuilder log) {
    return files(data, log, null);
  }

  /**
   * The same, and a touch layout file (TouchLayoutFile.is) into `touch` when it is given: then `data` holding only
   * a touch layout is no error.
   */
  static List<byte[]> files(byte[] data, StringBuilder log, List<byte[]> touch) {
    Found f = read(data, log, touch != null);
    if (touch != null) touch.addAll(f.touch);
    return f.gci;
  }

  /** All of `data` (the app's ZIP with its settings and instant save too); IllegalArgumentException when it holds
   *  nothing to import. */
  static Found read(byte[] data, StringBuilder log) {
    return read(data, log, true);
  }

  private static Found read(byte[] data, StringBuilder log, boolean extras) {
    Found found = new Found();
    if (extras && TouchLayoutFile.is(data)) {
      found.touch.add(data);
      log.append("touch layout file");
      return found;
    }
    if (extras && PlayerBackup.isSettings(data)) {
      found.settings = data;
      log.append("settings file");
      return found;
    }
    if (extras && PlayerBackup.isInstant(data)) {
      found.instant = data;
      log.append("instant save");
      return found;
    }
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
            found.gci.addAll(DolphinCard.split(file).gci);
            cards++;
          } else if (base.endsWith(".gci") && file.length <= MAX_GCI) {
            found.gci.add(file);
          } else if (extras && TouchLayoutFile.is(file)) {
            found.touch.add(file);
          } else if (extras && PlayerBackup.isManifest(file)) {
            found.manifest = file;
          } else if (extras && PlayerBackup.isSettings(file)) {
            found.settings = file;
          } else if (extras && PlayerBackup.isInstant(file)) {
            found.instant = file;
          } else {
            skipped++;
          }
        }
      } catch (IOException e) {
        throw new IllegalArgumentException("the ZIP file cannot be read (" + e.getMessage() + ")");
      }
      log.append("ZIP file: ").append(found.gci.size()).append(" files to import (").append(cards)
          .append(" memory cards in it), ").append(found.touch.size()).append(" touch layouts, ")
          .append(found.instant != null ? "an instant save, " : "")
          .append(found.settings != null ? "the settings, " : "")
          .append(found.manifest != null ? "a CubeEvil4 manifest, " : "")
          .append(skipped).append(" other files left out");
      if (found.gci.isEmpty() && !found.extras(true)) {
        throw new IllegalArgumentException("no saves (.gci, .raw) in the ZIP file");
      }
      return found;
    }
    if (DolphinCard.isCard(data)) {
      DolphinCard.Files card = DolphinCard.split(data);
      log.append("memory card with ").append(card.gci.size()).append(" files of this game, ")
          .append(card.otherGames).append(" of other games");
      if (card.gci.isEmpty()) throw new IllegalArgumentException("no saves of this game on the memory card");
      found.gci.addAll(card.gci);
      return found;
    }
    if (data.length > MAX_GCI) throw new IllegalArgumentException("too big for a .gci");
    found.gci.add(data);
    return found;
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
