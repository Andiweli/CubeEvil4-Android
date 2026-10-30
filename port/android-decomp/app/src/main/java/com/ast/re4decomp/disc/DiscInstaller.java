package com.ast.re4decomp.disc;

import java.io.*;
import java.util.Properties;

/**
 * Transactional replacement on one filesystem; old data survives failed
 * imports.
 */
public final class DiscInstaller {
  private DiscInstaller() {}
  public static void recover(File root) throws IOException {
    if (!root.isDirectory() && !root.mkdirs())
      throw new IOException("Data directory is unavailable");
    for (int n = 1; n <= 2; n++) {
      File current = new File(root, "disc" + n),
           old = new File(root, ".old-disc" + n);
      if (old.exists()) {
        if (ready(current, n))
          delete(old);
        else {
          delete(current);
          if (!old.renameTo(current))
            throw new IOException(
                "Could not restore previous import");
        }
      }
      delete(new File(root, ".work-disc" + n));
    }
    delete(new File(root, ".incoming"));
  }
  public static boolean ready(File disc, int number) {
    String release = release(disc);
    Properties p = properties(disc);
    if (release == null || p == null)
      return false;
    boolean debug = GameCubeDisc.DEBUG.equals(release);
    String references = p.getProperty("references", "");
    return "1".equals(p.getProperty("format")) &&
        GameCubeDisc.gameId(release).equals(p.getProperty("id")) &&
        Integer.toString(number).equals(p.getProperty("disc")) &&
        (debug ? Integer.toString(number == 1 ? 111 : 5).equals(references) : references.matches("[1-9][0-9]{0,4}")) &&
        new File(disc, "sys/main.dol").length() == GameCubeDisc.dolSize(release) &&
        new File(disc, "manifest.tsv").isFile();
  }
  /**
   * The release of an imported disc folder (GameCubeDisc.DEBUG, EUROPEAN, JAPANESE or KOREAN); imports before the
   * European discs (26.10.2026) name none and are the debug build's. Null without an import.
   */
  public static String release(File disc) {
    Properties p = properties(disc);
    if (p == null)
      return null;
    String release = p.getProperty("release", GameCubeDisc.DEBUG);
    return GameCubeDisc.DEBUG.equals(release) || GameCubeDisc.EUROPEAN.equals(release) ||
        GameCubeDisc.JAPANESE.equals(release) || GameCubeDisc.KOREAN.equals(release) ? release : null;
  }
  private static Properties properties(File disc) {
    Properties p = new Properties();
    try (InputStream r = new FileInputStream(new File(disc, "ready.properties"))) {
      p.load(r);
      return p;
    } catch (IOException e) {
      return null;
    }
  }
  /**
   * Both discs of the import folder ready and of one release (the game reads both; a debug disc
   * next to a European one would mix two different builds' data).
   */
  public static boolean sameRelease(File root) {
    String one = release(new File(root, "disc1")), two = release(new File(root, "disc2"));
    return one != null && one.equals(two);
  }
  /** The app's catalog of a release, for install (references-eu.tsv ...). */
  public interface Catalogs {
    ReferenceCatalog forRelease(String release) throws IOException;
  }
  /**
   * Core mode: a complete import with the disc header and file system table
   * (imports of older versions lack sys/fst.bin and must be repeated).
   */
  public static boolean coreReady(File disc, int number) {
    return ready(disc, number) && new File(disc, "sys/fst.bin").isFile() &&
        new File(disc, "sys/boot.bin").length() == 0x440;
  }
  public static void install(File source, File root, int expectedDisc,
                             ReferenceCatalog catalog,
                             GameCubeDisc.Progress progress)
      throws IOException {
    install(source, root, expectedDisc, release -> catalog, progress);
  }
  public static void install(File source, File root, int expectedDisc,
                             Catalogs catalogs,
                             GameCubeDisc.Progress progress)
      throws IOException {
    File work = new File(root, ".work-disc" + expectedDisc);
    File target = new File(root, "disc" + expectedDisc),
         old = new File(root, ".old-disc" + expectedDisc);
    boolean movedOld = false;
    try (GameCubeDisc disc = new GameCubeDisc(source)) {
      if (disc.discNumber != expectedDisc)
        throw new IOException("Please import Disc " + expectedDisc + " selected");
      disc.validate(catalogs.forRelease(disc.release), progress);
      if (root.getUsableSpace() < disc.extractionSize() + 64L * 1024 * 1024)
        throw new IOException(
            "Not enough free storage for the extracted data");
      delete(work);
      disc.extract(work, progress);
      progress.update("Complete import", 1, 1);
      if (old.exists())
        throw new IOException(
            "Previous import must be recovered");
      if (target.exists()) {
        if (!target.renameTo(old))
          throw new IOException("Could not back up existing disc");
        movedOld = true;
      }
      if (!work.renameTo(target)) {
        if (movedOld && !old.renameTo(target))
          throw new IOException("Import failed; previous data is in " +
                                old.getName());
        throw new IOException("Could not activate import");
      }
      // A crash here is repaired by recover(): the valid new target wins.
      try {
        delete(old);
      } catch (IOException ignored) { /* next launch recovers */
      }
    } finally {
      delete(work);
    }
  }
  public static void delete(File path) throws IOException {
    if (!path.exists())
      return;
    // Only app-owned trees are passed here; never follow symbolic links.
    File parent = path.getAbsoluteFile().getParentFile();
    File resolvedName = new File(parent.getCanonicalFile(), path.getName());
    if (path.isDirectory() && path.getCanonicalFile().equals(resolvedName)) {
      File[] children = path.listFiles();
      if (children == null)
        throw new IOException("Could not read directory");
      for (File f : children)
        delete(f);
    }
    if (!path.delete())
      throw new IOException("Could not delete file: " +
                            path.getName());
  }
}
