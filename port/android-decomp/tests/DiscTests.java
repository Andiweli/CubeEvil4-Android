import com.ast.re4decomp.disc.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;

public final class DiscTests {
  private static int checks;
  private static void check(boolean ok, String m) {
    if (!ok)
      throw new AssertionError(m);
    checks++;
  }
  private static void put32(byte[] b, int o, long v) {
    for (int i = 0; i < 4; i++)
      b[o + i] = (byte)(v >> (24 - 8 * i));
  }
  // the file system table where the debug build has it (GameCubeDisc tells the debug build from the
  // US release by its place)
  private static final int FST = 0x28FE00;
  private static byte[] synthetic(String name) {
    return synthetic(name, "G4BE08", FST);
  }
  private static byte[] synthetic(String name, String id, int fst) {
    byte[] b = new byte[Math.max(0x1000, fst + 0x100)];
    System.arraycopy(id.getBytes(StandardCharsets.US_ASCII), 0, b, 0, 6);
    put32(b, 0x1c, 0xc2339f3dL);
    put32(b, 0x420, 0x600);
    put32(b, 0x424, fst);
    put32(b, 0x428, 64);
    b[fst] = 1;
    put32(b, fst + 8, 2);
    put32(b, fst + 0x10, 0x900);
    put32(b, fst + 0x14, 1);
    byte[] n = name.getBytes(StandardCharsets.US_ASCII);
    System.arraycopy(n, 0, b, fst + 0x18, n.length);
    return b;
  }
  private static void writeReady(File disc, String text) throws IOException {
    if (!disc.isDirectory() && !disc.mkdirs())
      throw new IOException("mkdirs " + disc);
    Files.write(new File(disc, "ready.properties").toPath(), text.getBytes(StandardCharsets.UTF_8));
  }
  /** European, US and mixed discs (port/docs/retail-eu-plan.md, R4). */
  private static void releases(File tmp) throws Exception {
    File eu = new File(tmp, "eu.gcm");
    Files.write(eu.toPath(), synthetic("a", "G4BP08", 0x500));
    GameCubeDisc.retailAllowed = false;
    rejected(tmp, synthetic("a", "G4BP08", 0x500), "European disc refused outside the core mode");
    GameCubeDisc.retailAllowed = true;
    try (GameCubeDisc d = new GameCubeDisc(eu)) {
      check(GameCubeDisc.EUROPEAN.equals(d.release) && d.files.size() == 2, "European disc parsed");
    }
    try (GameCubeDisc d = new GameCubeDisc(new File(tmp, "good.gcm"))) {
      check(GameCubeDisc.DEBUG.equals(d.release), "debug build recognised by its FST place");
    }
    // the US release (port/docs/retail-us-plan.md): G4BE08 with the table elsewhere, core mode only
    File us = new File(tmp, "us.gcm");
    Files.write(us.toPath(), synthetic("a", "G4BE08", 0x500));
    try (GameCubeDisc d = new GameCubeDisc(us)) {
      check(GameCubeDisc.US.equals(d.release) && d.files.size() == 2, "US disc parsed");
    }
    GameCubeDisc.retailAllowed = false;
    rejected(tmp, synthetic("a", "G4BE08", 0x500), "US disc refused outside the core mode");
    GameCubeDisc.retailAllowed = true;
    check(GameCubeDisc.catalog("rooms", GameCubeDisc.EUROPEAN).equals("rooms-eu.tsv")
          && GameCubeDisc.catalog("files", GameCubeDisc.US).equals("files-us.tsv")
          && GameCubeDisc.catalog("references", GameCubeDisc.DEBUG).equals("references.tsv"), "catalog names");
    // a European extraction writes its release and id; ready() takes them, sameRelease() compares
    File root = new File(tmp, "releases");
    File d1 = new File(root, "disc1"), d2 = new File(root, "disc2");
    writeReady(d1, "format=1\nlanguage=eng\ndisc=1\nid=G4BP08\nrelease=eu\nreferences=128\n");
    writeReady(d2, "format=1\nlanguage=eng\ndisc=2\nid=G4BE08\nreferences=5\n");  // an import before 26.10.2026
    check(GameCubeDisc.EUROPEAN.equals(DiscInstaller.release(d1)) && GameCubeDisc.DEBUG.equals(DiscInstaller.release(d2)),
          "release of an import (old imports: debug build)");
    check(!DiscInstaller.sameRelease(root), "mixed releases noticed");
    writeReady(d2, "format=1\nlanguage=eng\ndisc=2\nid=G4BP08\nrelease=eu\nreferences=6\n");
    check(DiscInstaller.sameRelease(root), "one release");
    check(!DiscInstaller.ready(d1, 1), "ready needs the European main.dol and manifest");
    writeReady(d1, "format=1\nlanguage=eng\ndisc=1\nid=G4BE08\nrelease=eu\nreferences=128\n");
    check(DiscInstaller.release(d1) != null && !DiscInstaller.ready(d1, 1), "release and id must agree");
    try (GameCubeDisc d = new GameCubeDisc(eu)) {
      File dst = new File(tmp, "eu-extract");
      d.extract(dst, (phase, done, total) -> {});
      String text = new String(Files.readAllBytes(new File(dst, "ready.properties").toPath()), StandardCharsets.UTF_8);
      check(text.contains("id=G4BP08") && text.contains("release=eu"), "European extraction marks its release");
    }
    try (GameCubeDisc d = new GameCubeDisc(us)) {
      File dst = new File(tmp, "us-extract");
      d.extract(dst, (phase, done, total) -> {});
      String text = new String(Files.readAllBytes(new File(dst, "ready.properties").toPath()), StandardCharsets.UTF_8);
      check(text.contains("id=G4BE08") && text.contains("release=us"), "US extraction marks its release");
      check(GameCubeDisc.US.equals(DiscInstaller.release(dst)), "release of a US import");
    }
    // a US disc next to a debug disc: both G4BE08, still two releases
    writeReady(d1, "format=1\nlanguage=eng\ndisc=1\nid=G4BE08\nrelease=us\nreferences=129\n");
    writeReady(d2, "format=1\nlanguage=eng\ndisc=2\nid=G4BE08\nreferences=5\n");
    check(!DiscInstaller.sameRelease(root), "US and debug discs noticed as mixed");
    writeReady(d2, "format=1\nlanguage=eng\ndisc=2\nid=G4BE08\nrelease=us\nreferences=5\n");
    check(DiscInstaller.sameRelease(root), "both US discs");
  }
  private static void rejected(File dir, byte[] b, String label)
      throws Exception {
    File f = new File(dir, "bad.gcm");
    Files.write(f.toPath(), b);
    boolean fail = false;
    try (GameCubeDisc ignored = new GameCubeDisc(f)) {
    } catch (IOException e) {
      fail = true;
    }
    check(fail, label);
  }
  public static void main(String[] args) throws Exception {
    File tmp = Files.createTempDirectory("re4-disc-tests").toFile();
    try {
      File good = new File(tmp, "good.gcm");
      Files.write(good.toPath(), synthetic("a"));
      try (GameCubeDisc d = new GameCubeDisc(good)) {
        check(d.discNumber == 1 && d.files.size() == 2, "synthetic parser");
      }
      rejected(tmp, synthetic(".."), "path traversal rejected");
      byte[] b = synthetic("a");
      put32(b, FST + 8, 100000);
      rejected(tmp, b, "bad entry count");
      b = synthetic("a");
      put32(b, FST + 0x10, 0xffffff00L);
      rejected(tmp, b, "file out of bounds");
      b = synthetic("a");
      put32(b, 0x424, 0xffffff00L);
      rejected(tmp, b, "FST out of bounds");
      b = synthetic("a");
      b[0] = 'P';
      rejected(tmp, b, "wrong disc id");
      rejected(tmp, new byte[100], "short file");
      releases(tmp);
      try (GameCubeDisc d = new GameCubeDisc(good)) {
        File dst = new File(tmp, "cancelled");
        boolean fail = false;
        try {
          d.extract(dst, (phase, done, total) -> {
            throw new IOException("cancel");
          });
        } catch (IOException e) {
          fail = true;
        }
        check(fail && !new File(dst, "ready.properties").exists(),
              "cancelled extraction never ready");
      }
      // real discs: CATALOG DISC1 DISC2 EMPTY-DIR [core] ("core": the core mode's complete import, for
      // the European discs with references-eu.tsv)
      if (args.length == 4 || args.length == 5) {
        boolean coreImport = args.length == 5 && args[4].equals("core");
        GameCubeDisc.importEverything = coreImport;
        GameCubeDisc.retailAllowed = coreImport;
        ReferenceCatalog refs =
            new ReferenceCatalog(new FileInputStream(args[0]));
        File root = new File(args[3]);
        DiscInstaller.recover(root);
        for (int n = 1; n <= 2; n++) {
          File source = new File(args[n]);
          int expectedFiles;
          try (GameCubeDisc d = new GameCubeDisc(source)) {
            d.validate(refs, (phase, done, total) -> {});
            check(d.discNumber == n, "real disc identity");
            // Both supplied debug images have exactly fourteen separate Japanese
            // fonts and five unused CARD language archives; the core mode keeps everything.
            // The manifest also lists sys/boot.bin and sys/fst.bin (written for the core mode).
            expectedFiles = (coreImport ? d.files.size() : d.files.size() - 19) + 2;
          }
          DiscInstaller.install(source, root, n, refs,
                                (phase, done, total) -> {});
          check(DiscInstaller.ready(new File(root, "disc" + n), n),
                "real extraction ready");
          verifyManifest(new File(root, "disc" + n), expectedFiles);
          if (coreImport) {
            // every file as on the original disc (files.tsv / files-eu.tsv next to references*.tsv)
            String release = DiscInstaller.release(new File(root, "disc" + n));
            File files = new File(new File(args[0]).getParentFile(), GameCubeDisc.catalog("files", release));
            RoomFileCheck all = RoomFileCheck.verify(new File(root, "disc" + n), new FileInputStream(files), n);
            check(all.everyFile && all.checked == expectedFiles - 1 && all.bad.isEmpty(),
                "every file of disc " + n + " as in " + files.getName() + " (" + all.checked + " checked, "
                    + all.bad.size() + " differ" + (all.bad.isEmpty() ? "" : ", first " + all.bad.get(0)) + ")");
          }
          if (!coreImport)
            verifyEnglishAssets(new File(root, "disc" + n));
          System.out.println("Disc " + n +
                             (": reference validation, English extraction and " +
                              "SHA-256 manifest PASS"));
        }
        File current = new File(root, "disc1"),
             old = new File(root, ".old-disc1");
        check(current.renameTo(old), "simulate interrupted commit");
        DiscInstaller.recover(root);
        check(DiscInstaller.ready(current, 1), "old disc recovered");
        boolean aborted = false;
        try {
          DiscInstaller.install(new File(args[1]), root, 1, refs,
                                (phase, done, total) -> {
                                  if (phase.contains("Extracting"))
                                    throw new IOException("cancel");
                                });
        } catch (IOException e) {
          aborted = true;
        }
        check(aborted && DiscInstaller.ready(current, 1) &&
                  !new File(root, ".work-disc1").exists(),
              "cancel preserves installed disc");
        boolean wrong = false;
        try {
          DiscInstaller.install(new File(args[2]), root, 1, refs,
                                (phase, done, total) -> {});
        } catch (IOException e) {
          wrong = true;
        }
        check(wrong && DiscInstaller.ready(current, 1),
              "wrong disc preserves installed data");
      }
      System.out.println("Java disc tests: " + checks + " checks PASS");
    } finally {
      DiscInstaller.delete(tmp);
    }
  }
  private static void verifyEnglishAssets(File disc) {
    check(new File(disc, "files/ss/Cmn/save_e.dat").isFile(), "English CARD archive retained");
    check(new File(disc, "files/font/common_p.fnt").isFile(), "English font retained");
    check(new File(disc, "files/etc/core.das").isFile(), "Shared CORE archive retained");
    for (String language : new String[]{"j", "g", "f", "s", "i"})
      check(!new File(disc, "files/ss/Cmn/save_" + language + ".dat").exists(), "Unused CARD archive excluded");
    for (String name : new String[]{"common", "dvd", "event1", "event2", "event3", "event4",
                                   "ss_file", "ss_term", "sscrn", "stage1", "stage2", "stage3", "stage4", "system"})
      check(!new File(disc, "files/font/" + name + "_j.fnt").exists(), "Japanese font excluded");
    for (String name : new String[]{"tel0000_101_f.fcv", "tel0100_101_f.fcv"})
      check(new File(disc, "files/ss/Eng/" + name).isFile(), "English animation suffix retained");
  }
  private static void verifyManifest(File disc, int expectedFiles) throws Exception {
    try (BufferedReader r = new BufferedReader(
             // UTF-8 as GameCubeDisc writes it (a Japanese file name of the debug discs; the
             // platform default of Windows is another charset)
             new InputStreamReader(new FileInputStream(new File(disc, "manifest.tsv")), StandardCharsets.UTF_8))) {
      String line;
      int n = 0;
      byte[] buffer = new byte[1024 * 1024];
      while ((line = r.readLine()) != null) {
        if (line.startsWith("#"))
          continue;
        String[] s = line.split("\t");
        File f = new File(disc, s[2]);
        check(f.length() == Long.parseLong(s[1]), "extracted size");
        java.security.MessageDigest md = GameCubeDisc.newDigest("SHA-256");
        try (InputStream input = new FileInputStream(f)) {
          int size;
          while ((size = input.read(buffer)) >= 0)
            if (size > 0)
              md.update(buffer, 0, size);
        }
        check(GameCubeDisc.hex(md.digest()).equals(s[0]), "extracted digest");
        n++;
      }
      check(n == expectedFiles, "Only the nineteen unused language assets excluded");
    }
  }
}
