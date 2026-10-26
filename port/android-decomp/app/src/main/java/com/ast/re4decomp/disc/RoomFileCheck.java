package com.ast.re4decomp.disc;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/**
 * Compares the room files an import wrote (manifest.tsv, the SHA-256 of every extracted file) with
 * the hashes of the verified disc (assets/rooms.tsv). A room file of a damaged disc image makes the
 * game stand at that room (reports of 25.10.2026, St2/r222.das), so the import says so at once.
 */
public final class RoomFileCheck {
  public final int checked;
  /** Paths (as in rooms.tsv) of room files that differ from the verified disc or are missing. */
  public final List<String> bad;

  private RoomFileCheck(int checked, List<String> bad) {
    this.checked = checked;
    this.bad = Collections.unmodifiableList(bad);
  }

  /** `manifest`: the manifest.tsv of the imported disc, `rooms`: rooms.tsv, `disc`: 1 or 2. */
  public static RoomFileCheck verify(Reader manifest, Reader rooms, int disc) throws IOException {
    Map<String, String> actual = new HashMap<>();
    try (BufferedReader r = new BufferedReader(manifest)) {
      String line;
      while ((line = r.readLine()) != null) {
        if (line.isEmpty() || line.startsWith("#")) continue;
        String[] f = line.split("\t");
        if (f.length == 3) actual.put(key(f[2]), f[0]);
      }
    }
    List<String> bad = new ArrayList<>();
    int checked = 0;
    try (BufferedReader r = new BufferedReader(rooms)) {
      String line;
      while ((line = r.readLine()) != null) {
        if (line.isEmpty() || line.startsWith("#")) continue;
        String[] f = line.split("\t");
        if (f.length != 5) throw new IOException("Invalid room verification catalog");
        if (!f[0].equals(Integer.toString(disc))) continue;
        checked++;
        String hash = actual.get(key(f[1]));
        if (hash == null || !hash.equalsIgnoreCase(f[4])) bad.add(f[1]);
      }
    }
    return new RoomFileCheck(checked, bad);
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

  public static RoomFileCheck verify(File discDir, InputStream rooms, int disc) throws IOException {
    try (Reader manifest = new InputStreamReader(new FileInputStream(new File(discDir, "manifest.tsv")),
                                                  StandardCharsets.UTF_8);
         Reader catalog = new InputStreamReader(rooms, StandardCharsets.UTF_8)) {
      return verify(manifest, catalog, disc);
    }
  }

  /** The line shown after an import, or null when every room file matches. */
  public String warning(int disc) {
    if (bad.isEmpty()) return null;
    return "Disc " + disc + " imported, but " + bad.size() + " of " + checked + " room files differ from the original disc (first: "
        + bad.get(0) + "). The disc image may be damaged: dump the disc again and import it again, or the game can stand at those rooms.";
  }
}
