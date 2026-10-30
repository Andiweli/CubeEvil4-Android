package com.ast.re4decomp.disc;

import java.io.*;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;

/** Bounded reader for raw GameCube ISO/GCM images. Not an ISO9660 reader. */
public final class GameCubeDisc implements Closeable {
  /**
   * The releases the app takes (port/docs/retail-eu-plan.md, retail-jp-plan.md): the debug build of
   * 2004-11-25 (G4BE08, its file system table at 0x28FE00; the US release has the same id, the table
   * elsewhere), the European release (G4BP08), the Japanese release (G4BJ08) and the Korean fan
   * translation built on it (the same header; its main.dol differs in two instructions, KOREAN_DOL).
   * The rule of port/tools/disc_release.py.
   */
  public static final String DEBUG = "debug", EUROPEAN = "eu", JAPANESE = "jp", KOREAN = "kr";
  private static final long DEBUG_FST_OFFSET = 0x28FE00L;
  /** The Korean translation's main.dol: at these file offsets "li r30, 1" / "li r31, 1" (retail-jp-plan.md). */
  private static final long[][] KOREAN_DOL = {{0x14b7cL, 0x3bc00001L}, {0x29cd4L, 0x3be00001L}};
  /** Core mode: the retail discs too (the classic mode's own game modules know the debug build only). */
  public static volatile boolean retailAllowed;
  private static final String[] UNUSED_LANGUAGES = {"jpn", "ger", "fra", "esp", "ita"};
  private static final Set<String> UNUSED_CARD_ASSETS = new HashSet<>(Arrays.asList(
      "files/ss/cmn/save_j.dat", "files/ss/cmn/save_g.dat",
      "files/ss/cmn/save_f.dat", "files/ss/cmn/save_s.dat",
      "files/ss/cmn/save_i.dat"));
  public interface Progress {
    void update(String phase, long done, long total) throws IOException;
  }
  public static final class Entry {
    public final String path;
    public final long offset, size;
    Entry(String path, long offset, long size) {
      this.path = path;
      this.offset = offset;
      this.size = size;
    }
  }
  /**
   * Core mode: the original game core reads every file through its own file
   * system table, so nothing is left out of the import.
   */
  public static volatile boolean importEverything;
  private final RandomAccessFile input;
  private final long length;
  private long fstOffset, fstSize;
  public final int discNumber;
  /** DEBUG, EUROPEAN, JAPANESE or KOREAN. */
  public final String release;
  /** References of this disc checked by validate (0 before). */
  private int validated;
  public final List<Entry> files;
  private final Map<String, Entry> byPath = new HashMap<>();

  public GameCubeDisc(File source) throws IOException {
    input = new RandomAccessFile(source, "r");
    length = input.length();
    try {
      byte[] header = read(0, 0x440);
      if (be32(header, 0x1c) != 0xc2339f3dL)
        throw new IOException("Not a raw GameCube ISO/GCM file");
      String id = new String(header, 0, 6, StandardCharsets.US_ASCII);
      if (id.equals("G4BP08")) {
        if (!retailAllowed)
          throw new IOException("European discs (G4BP08) need CubeEvil4's core mode");
        release = EUROPEAN;
      } else if (id.equals("G4BJ08")) {
        if (!retailAllowed)
          throw new IOException("Japanese discs (G4BJ08) need CubeEvil4's core mode");
        release = koreanDol(be32(header, 0x420)) ? KOREAN : JAPANESE;
      } else if (id.equals("G4BE08")) {
        if (be32(header, 0x424) != DEBUG_FST_OFFSET)
          throw new IOException("This is the US release (G4BE08): only the debug build of November 25, 2004"
              + " and the European release (G4BP08) are supported");
        release = DEBUG;
      } else
        throw new IOException("Wrong disc ID " + id.replaceAll("[^A-Za-z0-9]", "?")
            + "; the debug build (G4BE08), the European (G4BP08) or the Japanese release (G4BJ08) is required");
      discNumber = (header[6] & 255) + 1;
      if (discNumber < 1 || discNumber > 2 || header[7] != 0)
        throw new IOException("Unsupported disc/revision");
      long dol = be32(header, 0x420);
      fstOffset = be32(header, 0x424);
      fstSize = be32(header, 0x428);
      if (fstSize < 12 || fstSize > 32 * 1024 * 1024)
        throw new IOException("Invalid FST size");
      byte[] fst = read(fstOffset, (int)fstSize);
      long n = be32(fst, 8);
      if ((fst[0] & 255) != 1 || n < 1 || n > 100000 || n * 12 > fst.length)
        throw new IOException("Invalid FST root");
      int count = (int)n, namesOffset = count * 12;
      ArrayDeque<Integer> ends = new ArrayDeque<>(), indexes =
                                                         new ArrayDeque<>();
      ArrayDeque<String> paths = new ArrayDeque<>();
      ends.push(count);
      indexes.push(0);
      paths.push("");
      List<Entry> result = new ArrayList<>();
      for (int i = 1; i < count; i++) {
        while (i >= ends.peek()) {
          ends.pop();
          paths.pop();
          indexes.pop();
        }
        int o = i * 12;
        int nameOffset = namesOffset + (int)(be32(fst, o) & 0xffffff);
        if (nameOffset < namesOffset || nameOffset >= fst.length)
          throw new IOException("Invalid FST name");
        int end = nameOffset;
        while (end < fst.length && fst[end] != 0)
          end++;
        if (end == fst.length || end - nameOffset > 255)
          throw new IOException("Invalid FST name");
        String name = new String(fst, nameOffset, end - nameOffset,
                                 Charset.forName("Shift_JIS"));
        safeComponent(name);
        String path = paths.peek().isEmpty() ? name : paths.peek() + "/" + name;
        long a = be32(fst, o + 4), b = be32(fst, o + 8);
        int kind = fst[o] & 255;
        if (kind == 1) {
          if (a != indexes.peek() || b <= i || b > ends.peek())
            throw new IOException("Invalid FST directories");
          ends.push((int)b);
          indexes.push(i);
          paths.push(path);
        } else if (kind == 0) {
          range(a, b);
          Entry entry = new Entry("files/" + path, a, b);
          String key = entry.path.toLowerCase(Locale.ROOT);
          if (byPath.put(key, entry) != null)
            throw new IOException("Duplicate disc path");
          result.add(entry);
        } else
          throw new IOException("Unknown FST entry");
      }
      byte[] dh = read(dol, 0x100);
      long dolSize = 0x100;
      for (int i = 0; i < 18; i++) {
        long offset = be32(dh, i * 4), size = be32(dh, 0x90 + i * 4);
        if (size > 0)
          dolSize = Math.max(dolSize, offset + size);
      }
      range(dol, dolSize);
      Entry main = new Entry("sys/main.dol", dol, dolSize);
      byPath.put(main.path, main);
      result.add(main);
      files = Collections.unmodifiableList(result);
    } catch (IOException | RuntimeException e) {
      input.close();
      throw e;
    }
  }
  /** The Korean translation's main.dol at `dol` (the Japanese one otherwise; a short image is not). */
  private boolean koreanDol(long dol) throws IOException {
    for (long[] w : KOREAN_DOL) {
      if (dol + w[0] + 4 > length || be32(read(dol + w[0], 4), 0) != w[1])
        return false;
    }
    return true;
  }
  public static long be32(byte[] b, int o) throws IOException {
    if (o < 0 || o > b.length - 4)
      throw new IOException("Truncated data field");
    return ((long)(b[o] & 255) << 24) | ((long)(b[o + 1] & 255) << 16) |
        ((b[o + 2] & 255) << 8) | (b[o + 3] & 255);
  }
  private static void safeComponent(String name) throws IOException {
    if (name.isEmpty() || name.equals(".") || name.equals("..") ||
        name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 ||
        name.indexOf(':') >= 0 || name.indexOf('\ufffd') >= 0)
      throw new IOException("Unsafe disc path");
    for (int i = 0; i < name.length(); i++)
      if (name.charAt(i) < 32)
        throw new IOException("Unsafe disc path");
  }
  private void range(long offset, long size) throws IOException {
    if (offset < 0 || size < 0 || offset > length || size > length - offset)
      throw new IOException("Disc range exceeds file bounds");
  }
  public byte[] read(long offset, int size) throws IOException {
    range(offset, size);
    byte[] b = new byte[size];
    input.seek(offset);
    input.readFully(b);
    return b;
  }
  public Entry find(String path) throws IOException {
    Entry e = byPath.get(path.toLowerCase(Locale.ROOT));
    if (e == null)
      throw new IOException("File missing: " + path);
    return e;
  }
  public void validate(ReferenceCatalog catalog, Progress progress)
      throws IOException {
    int total = 0, done = 0;
    for (ReferenceCatalog.Entry r : catalog.entries)
      if (r.disc == discNumber)
        total++;
    if (total == 0)
      throw new IOException("Missing references");
    for (ReferenceCatalog.Entry r : catalog.entries) {
      if (r.disc != discNumber)
        continue;
      Entry e = find(r.path);
      if (r.offset < 0 || r.size <= 0 || r.offset > e.size ||
          r.size > e.size - r.offset)
        throw new IOException("Truncated original module: " + r.path);
      String hash = digest(e.offset + r.offset, r.size, "SHA-1", null);
      if (!hash.equals(r.sha1))
        throw new IOException(releaseTitle(release) + " mismatch: " + r.path);
      progress.update(DEBUG.equals(release) ? "Verify debug build" : "Verify " + releaseTitle(release), ++done, total);
    }
    validated = total;
  }
  /** "Debug build", "European release", "Japanese release" or "Korean translation". */
  public static String releaseTitle(String release) {
    return EUROPEAN.equals(release) ? "European release" : JAPANESE.equals(release) ? "Japanese release"
        : KOREAN.equals(release) ? "Korean translation" : "Debug build";
  }
  /** The game id a release's discs carry. */
  public static String gameId(String release) {
    return EUROPEAN.equals(release) ? "G4BP08" : JAPANESE.equals(release) || KOREAN.equals(release) ? "G4BJ08" : "G4BE08";
  }
  /** The size of a release's sys/main.dol. */
  public static long dolSize(String release) {
    return EUROPEAN.equals(release) ? 2187552 : JAPANESE.equals(release) || KOREAN.equals(release) ? 2182944 : 2554432;
  }
  /**
   * The app's catalog `name` of a release: rooms.tsv for the debug build, rooms-eu.tsv for the European,
   * rooms-jp.tsv / rooms-kr.tsv for the Japanese release and the Korean translation.
   */
  public static String catalog(String name, String release) {
    return EUROPEAN.equals(release) ? name + "-eu.tsv" : JAPANESE.equals(release) ? name + "-jp.tsv"
        : KOREAN.equals(release) ? name + "-kr.tsv" : name + ".tsv";
  }
  public long extractionSize() {
    long n = 0x440 + fstSize;  // sys/boot.bin, sys/fst.bin
    for (Entry e : files)
      if (importFile(e))
        n += e.size;
    return n;
  }
  private static boolean importFile(Entry entry) {
    if (importEverything)
      return true;
    String path = entry.path.toLowerCase(Locale.ROOT);
    if (path.startsWith("files/font/") && path.endsWith("_j.fnt"))
      return false;
    if (UNUSED_CARD_ASSETS.contains(path))
      return false;
    for (String language : UNUSED_LANGUAGES)
      if (path.startsWith("files/ss/" + language + "/") ||
          path.startsWith("files/etc/" + language + "/"))
        return false;
    // Shared archives contain the required English MDT slots and original
    // offset tables. Keep them byte-for-byte; suffixes on other resources
    // (for example *_f.fcv animations) are not language identifiers.
    return true;
  }
  public void extract(File destination, Progress progress) throws IOException {
    if (destination.exists())
      throw new IOException("Import destination already exists");
    if (!destination.mkdirs())
      throw new IOException("Could not create import destination");
    long total = extractionSize(), done = 0;
    try (Writer manifest = new OutputStreamWriter(
             new FileOutputStream(new File(destination, "manifest.tsv")),
             StandardCharsets.UTF_8)) {
      manifest.write("# sha256\tbytes\tpath\n");
      byte[] buffer = new byte[1024 * 1024];
      for (Entry e : files) {
        if (!importFile(e))
          continue;
        File f = new File(destination, e.path);
        if (!f.getCanonicalPath().startsWith(destination.getCanonicalPath() +
                                             File.separator))
          throw new IOException("Unsafe destination path");
        if (!f.getParentFile().isDirectory() && !f.getParentFile().mkdirs())
          throw new IOException("Could not create directory");
        String hash = null;
        // read back what the device stored: a file the storage did not keep is written once more,
        // then the import stops (St2/r222.das read as zeros on three phones, 25./26.10.2026)
        for (int attempt = 0; attempt < 2 && hash == null; attempt++) {
          MessageDigest md = newDigest("SHA-256");
          input.seek(e.offset);
          long remaining = e.size;
          try (FileOutputStream w = new FileOutputStream(f)) {
            while (remaining > 0) {
              int n = (int)Math.min(buffer.length, remaining);
              input.readFully(buffer, 0, n);
              w.write(buffer, 0, n);
              md.update(buffer, 0, n);
              remaining -= n;
              if (attempt == 0) done += n;
              progress.update("Disc " + discNumber + ": Extracting", done, total);
            }
            w.getFD().sync();
          }
          String wrote = hex(md.digest());
          if (wrote.equals(stored(f, e.size, buffer)))
            hash = wrote;
        }
        if (hash == null)
          throw new IOException("This device did not store " + e.path.replaceFirst("^files/", "")
              + " correctly (it reads back differently than it was written). Free some storage, restart"
              + " the phone and import Disc " + discNumber + " again; if it happens again, send the error report.");
        manifest.write(hash + "\t" + e.size + "\t" + e.path + "\n");
      }
      // The disc header and the file system table, as Dolphin's "Extract
      // Entire Disc" writes them: the original game core (core mode) mounts
      // the disc from them (port/whole/runtime/dvd.cpp).
      writeSystem(destination, "sys/boot.bin", 0, 0x440, manifest);
      writeSystem(destination, "sys/fst.bin", fstOffset, (int)fstSize, manifest);
    }
    try (FileOutputStream w =
             new FileOutputStream(new File(destination, "ready.properties"))) {
      w.write(("format=1\nlanguage=eng\ndisc=" + discNumber + "\nid=" + gameId(release)
               + "\nrelease=" + release + "\nreferences=" + validated + "\n")
                  .getBytes(StandardCharsets.UTF_8));
      w.getFD().sync();
    }
  }
  private void writeSystem(File destination, String path, long offset, int size,
                           Writer manifest) throws IOException {
    byte[] bytes = read(offset, size);
    File f = new File(destination, path);
    if (!f.getParentFile().isDirectory() && !f.getParentFile().mkdirs())
      throw new IOException("Could not create directory");
    try (FileOutputStream w = new FileOutputStream(f)) {
      w.write(bytes);
      w.getFD().sync();
    }
    MessageDigest md = newDigest("SHA-256");
    md.update(bytes);
    manifest.write(hex(md.digest()) + "\t" + size + "\t" + path + "\n");
  }
  /** The SHA-256 of the file as the device returns it, or null when it is not `size` bytes long. */
  private static String stored(File f, long size, byte[] buffer) throws IOException {
    if (f.length() != size)
      return null;
    MessageDigest md = newDigest("SHA-256");
    long n = 0;
    try (InputStream in = new FileInputStream(f)) {
      int k;
      while ((k = in.read(buffer)) >= 0) {
        md.update(buffer, 0, k);
        n += k;
      }
    }
    return n == size ? hex(md.digest()) : null;
  }
  private String digest(long offset, long size, String algorithm,
                        Progress progress) throws IOException {
    MessageDigest md = newDigest(algorithm);
    input.seek(offset);
    byte[] b = new byte[65536];
    while (size > 0) {
      int n = (int)Math.min(size, b.length);
      input.readFully(b, 0, n);
      md.update(b, 0, n);
      size -= n;
    }
    return hex(md.digest());
  }
  public static MessageDigest newDigest(String algorithm) {
    try {
      return MessageDigest.getInstance(algorithm);
    } catch (NoSuchAlgorithmException e) {
      throw new AssertionError(e);
    }
  }
  public static String hex(byte[] b) {
    StringBuilder s = new StringBuilder();
    for (byte v : b)
      s.append(String.format(Locale.ROOT, "%02x", v & 255));
    return s.toString();
  }
  @Override
  public void close() throws IOException {
    input.close();
  }
}
