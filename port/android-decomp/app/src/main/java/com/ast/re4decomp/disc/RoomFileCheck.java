package com.ast.re4decomp.disc;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;

/**
 * Compares the files an import wrote (manifest.tsv, the SHA-256 of every extracted file) with the
 * hashes of the verified disc: assets/files.tsv (every file of the disc, since 27.10.2026) or
 * assets/rooms.tsv (the room files only). A damaged file of the disc image makes the game stand at
 * the room or screen that reads it (reports of 25./26.10.2026, St2/r222.das), so the import says so
 * at once. {@link #diagnose} tells, for a file the game could not read, whether the image or the
 * copy on the device is at fault.
 */
public final class RoomFileCheck {
  public final int checked;
  /** Paths (as in the catalog, without files/) of files that differ from the verified disc or are missing. */
  public final List<String> bad;
  /** The catalog lists every file of the disc (files.tsv), not only the room files. */
  public final boolean everyFile;

  private RoomFileCheck(int checked, List<String> bad, boolean everyFile) {
    this.checked = checked;
    this.bad = Collections.unmodifiableList(bad);
    this.everyFile = everyFile;
  }

  /** The manifest of an import: path key -> SHA-256. */
  static Map<String, String> manifest(Reader manifest) throws IOException {
    Map<String, String> actual = new HashMap<>();
    try (BufferedReader r = new BufferedReader(manifest)) {
      String line;
      while ((line = r.readLine()) != null) {
        if (line.isEmpty() || line.startsWith("#")) continue;
        String[] f = line.split("\t");
        if (f.length == 3) actual.put(key(f[2]), f[0]);
      }
    }
    return actual;
  }

  /** A catalog row: [path, SHA-256 of the file], or null for another disc. */
  private static String[] row(String[] f, int disc) throws IOException {
    if (f.length == 4) {  // files.tsv: disc, path, bytes, sha256
      return f[0].equals(Integer.toString(disc)) ? new String[] {f[1], f[3]} : null;
    }
    if (f.length == 5) {  // rooms.tsv: disc, path, decoded size, decoded sha256, source sha256
      return f[0].equals(Integer.toString(disc)) ? new String[] {f[1], f[4]} : null;
    }
    throw new IOException("Invalid disc verification catalog");
  }

  /** `manifest`: the manifest.tsv of the imported disc, `catalog`: files.tsv or rooms.tsv, `disc`: 1 or 2. */
  public static RoomFileCheck verify(Reader manifest, Reader catalog, int disc) throws IOException {
    Map<String, String> actual = manifest(manifest);
    List<String> bad = new ArrayList<>();
    int checked = 0;
    boolean every = false;
    try (BufferedReader r = new BufferedReader(catalog)) {
      String line;
      while ((line = r.readLine()) != null) {
        if (line.isEmpty() || line.startsWith("#")) continue;
        String[] f = line.split("\t");
        every = f.length == 4;
        String[] row = row(f, disc);
        if (row == null) continue;
        checked++;
        String hash = actual.get(key(row[0]));
        if (hash == null || !hash.equalsIgnoreCase(row[1])) bad.add(shown(row[0]));
      }
    }
    return new RoomFileCheck(checked, bad, every);
  }

  /**
   * A path of either list in one form: the manifest names the extracted files as they lie in the disc
   * folder (files/St1/r100.das, GameCubeDisc), rooms.tsv as on the disc (St1/r100.das); case aside.
   */
  static String key(String path) {
    String p = path.replace('\\', '/');
    if (p.startsWith("files/")) p = p.substring(6);
    return p.toLowerCase(Locale.ROOT);
  }

  /** A path for the player: as on the disc (St2/r222.das), sys/ files as they are. */
  static String shown(String path) {
    String p = path.replace('\\', '/');
    return p.startsWith("files/") ? p.substring(6) : p;
  }

  public static RoomFileCheck verify(File discDir, InputStream catalog, int disc) throws IOException {
    try (Reader manifest = new InputStreamReader(new FileInputStream(new File(discDir, "manifest.tsv")),
                                                  StandardCharsets.UTF_8);
         Reader rows = new InputStreamReader(catalog, StandardCharsets.UTF_8)) {
      return verify(manifest, rows, disc);
    }
  }

  /** The line shown after an import, or null when every file matches. */
  public String warning(int disc) {
    if (bad.isEmpty()) return null;
    return "Disc " + disc + " imported, but " + bad.size() + " of " + checked + (everyFile ? " files" : " room files")
        + " differ from the original disc (first: " + bad.get(0) + "). The disc image is damaged: dump the disc"
        + " again and import it again, or the game stands where it reads those files.";
  }

  /** Why the game could not read a file: the answer of {@link #diagnose}. */
  public enum Cause {
    /** The imported disc image differs from the original disc in this file. */
    IMAGE,
    /** The file on the device is missing or differs from what the import wrote. */
    DEVICE,
    /** The file on the device is the original's: the read itself failed. */
    READ,
    /** Neither the import's manifest nor the catalog knows the file. */
    UNKNOWN
  }

  /** A diagnosis: the cause and one line for the log and the error report. */
  public static final class Diagnosis {
    public final Cause cause;
    public final String detail;

    Diagnosis(Cause cause, String detail) {
      this.cause = cause;
      this.detail = detail;
    }
  }

  /**
   * For `path` (as on the disc, St2/r222.das) of the imported disc in `discDir`: hashes the file on the
   * device and compares it with the import's manifest (what the import wrote) and the catalog (the
   * original disc). `catalog` may be null.
   */
  public static Diagnosis diagnose(File discDir, String path, InputStream catalog, int disc) {
    String k = key(path);
    String wrote = null, original = null;
    try (Reader m = new InputStreamReader(new FileInputStream(new File(discDir, "manifest.tsv")), StandardCharsets.UTF_8)) {
      wrote = manifest(m).get(k);
    } catch (IOException ignored) { /* an import without a manifest */
    }
    if (catalog != null) {
      try (BufferedReader r = new BufferedReader(new InputStreamReader(catalog, StandardCharsets.UTF_8))) {
        String line;
        while ((line = r.readLine()) != null && original == null) {
          if (line.isEmpty() || line.startsWith("#")) continue;
          String[] row = row(line.split("\t"), disc);
          if (row != null && key(row[0]).equals(k)) original = row[1];
        }
      } catch (IOException ignored) { /* the check is optional */
      }
    }
    File f = findIgnoringCase(discDir, "files/" + shown(path));
    if (f == null) f = new File(discDir, "files/" + shown(path));
    String device = null, content = "missing";
    if (f.isFile()) {
      long[] zeros = new long[2];
      device = sha256(f, zeros);
      content = device == null ? "unreadable (" + f.length() + " bytes)"
          : f.length() + " bytes, " + (zeros[0] == f.length() ? "all zeros" : zeros[1] < 0 ? "no zeros at the start"
             : "zeros from the start to 0x" + Long.toHexString(zeros[1]));
    }
    String detail = "disc " + disc + " " + shown(path) + ": on the device " + content + ", sha256 " + device
        + "; the import wrote " + wrote + "; the original disc " + original;
    if (original != null && wrote != null && !original.equalsIgnoreCase(wrote))
      return new Diagnosis(Cause.IMAGE, detail);
    if (wrote != null && (device == null || !device.equalsIgnoreCase(wrote)))
      return new Diagnosis(Cause.DEVICE, detail);
    if (wrote == null && original != null && (device == null || !device.equalsIgnoreCase(original)))
      return new Diagnosis(Cause.DEVICE, detail);
    if (device != null && (device.equalsIgnoreCase(wrote) || device.equalsIgnoreCase(original)))
      return new Diagnosis(Cause.READ, detail);
    return new Diagnosis(Cause.UNKNOWN, detail);
  }

  /** The file's SHA-256, or null when it cannot be read; zeros[0] = zero bytes, zeros[1] = first other byte (-1: none at 0). */
  static String sha256(File f, long[] zeros) {
    MessageDigest md = GameCubeDisc.newDigest("SHA-256");
    byte[] b = new byte[1 << 20];
    long at = 0, first = -1;
    boolean leading = true;
    try (InputStream in = new FileInputStream(f)) {
      int n;
      while ((n = in.read(b)) >= 0) {
        md.update(b, 0, n);
        for (int i = 0; i < n; i++) {
          if (b[i] == 0) zeros[0]++;
          else if (leading) {
            leading = false;
            first = at + i;
          }
        }
        at += n;
      }
    } catch (IOException e) {
      return null;
    }
    zeros[1] = first == 0 ? -1 : first;
    return GameCubeDisc.hex(md.digest());
  }

  /** The file below `dir` whose path matches `path` but for case (the core's lookup is case-blind), or null. */
  private static File findIgnoringCase(File dir, String path) {
    File at = dir;
    for (String part : path.split("/")) {
      File[] list = at.listFiles();
      if (list == null) return null;
      File next = null;
      for (File c : list)
        if (c.getName().equalsIgnoreCase(part)) next = c;
      if (next == null) return null;
      at = next;
    }
    return at;
  }
}
