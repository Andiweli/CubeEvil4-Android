package com.ast.re4decomp;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Language packs of the player (wish of the project owner, 26.11.2026; port/docs/sprachpakete.md): a fan translation
 * made for the European discs as replacement disc files, imported from a folder or a ZIP into a folder of its own,
 * "language/pack" in the app's data folder, next to the discs ("game") and never inside them. The import
 * (language_pack.cpp, the library re4langpack) keeps only the files that differ from the player's own discs, and
 * only when they change texts, subtitles and menu pictures; the core reads them instead of the disc's while the
 * game language is the pack's choice ("Game language", RE4_LANGUAGE_PACK). CubeEvil4 brings no language packs;
 * nothing of a pack goes into a report but its name and sizes.
 */
final class LanguagePacks {
  private static final String TAG = "RE4Language";
  static final String FOLDER = "language", PACK = "pack";
  /** The "Game language" choice of the pack (GAME_LANGUAGE pref; the languages are 0 and 2..6). */
  static final int CHOICE = 7;
  /** Free space an import needs at least before it starts. */
  static final long RESERVE = 100L << 20;

  // why an import did not work (language_pack.h re4lang::Reason; PackException.reason), each a text of its own
  static final int NO_DISCS = 1, NOT_EUROPEAN = 2, PROGRAM_FILE = 3, NOT_TEXT = 4, MORE_THAN_TEXT = 5,
      NOT_ON_DISC = 6, DAMAGED = 7, NO_CHANGES = 8, NO_SPACE = 9, READ_FAILED = 10, NOT_PACK = 11, CANCELLED = 12,
      TWO_COPIES = 13, UNSAFE_PATH = 14, NO_LANGUAGE = 15, BAD_ZIP = 16;

  static final class PackException extends IOException {
    final int reason;
    final String file;
    PackException(int reason, String file, String detail) {
      super(detail);
      this.reason = reason;
      this.file = file == null ? "" : file;
    }
  }

  /** The imported pack (pack.properties). */
  static final class Info {
    String name = "", source = "", imported = "";
    int language, replaced, files, identical, ignored;
    long bytes;
  }

  interface Listener {
    void progress(long done, long total);
    /** info: the pack now there; failure: why not (null when it worked) */
    void done(Info info, PackException failure);
  }

  // an import running now (the menu shows it again when it is opened again)
  static volatile boolean importing;
  static volatile long importDone, importTotal;

  private LanguagePacks() {}

  static File folder(Context c) { return new File(new File(AppStorage.base(c), FOLDER), PACK); }

  /** The pack, or null when none is imported (or it is incomplete). */
  static Info info(Context c) {
    File dir = folder(c);
    File props = new File(dir, "pack.properties");
    if (!new File(dir, "overlay.tsv").isFile() || !props.isFile()) return null;
    Properties p = new Properties();
    try (InputStream in = new FileInputStream(props)) {
      p.load(in);
    } catch (IOException e) {
      return null;
    }
    Info i = new Info();
    i.name = p.getProperty("name", "");
    i.source = p.getProperty("source", "");
    i.imported = p.getProperty("imported", "");
    i.language = number(p, "language");
    i.replaced = number(p, "replaced");
    i.files = number(p, "files");
    i.identical = number(p, "identical");
    i.ignored = number(p, "ignored");
    try {
      i.bytes = Long.parseLong(p.getProperty("bytes", "0"));
    } catch (NumberFormatException e) {
      i.bytes = 0;
    }
    if (i.language < 2 || i.language > 6) return null;
    if (i.name.trim().isEmpty()) i.name = i.source;
    return i;
  }

  private static int number(Properties p, String key) {
    try {
      return Integer.parseInt(p.getProperty(key, "0").trim());
    } catch (NumberFormatException e) {
      return 0;
    }
  }

  /** The name the menu shows for the pack (the player's, given after the import). */
  static void rename(Context c, String name) throws IOException {
    File props = new File(folder(c), "pack.properties");
    Properties p = new Properties();
    try (InputStream in = new FileInputStream(props)) {
      p.load(in);
    }
    p.setProperty("name", name.trim());
    File part = new File(props.getPath() + ".part");
    try (OutputStream out = new FileOutputStream(part)) {
      p.store(out, "CubeEvil4 language pack (port/docs/sprachpakete.md)");
    }
    if (!part.renameTo(props)) throw new IOException("cannot rename " + part);
  }

  /** Deletes the pack (only the language folder: discs, saves and cards stay). */
  static void delete(Context c) {
    File base = new File(AppStorage.base(c), FOLDER);
    deleteTree(new File(base, PACK + ".work"));
    deleteTree(new File(base, PACK + ".old"));
    deleteTree(new File(base, PACK));
    Log.i(TAG, "language pack deleted");
  }

  private static void deleteTree(File f) {
    File[] in = f.listFiles();
    if (in != null) for (File x : in) deleteTree(x);
    if (f.exists() && !f.delete()) Log.w(TAG, "cannot delete " + f);
  }

  /** "Name · replaces Español · 315 files · 602 MB" for the report (names and sizes only). */
  static String reportLine(Context c) {
    Info i = info(c);
    if (i == null) return "language pack: none";
    return "language pack: \"" + i.name + "\", language " + i.language + ", " + i.replaced + " disc files from "
        + i.files + " files, " + i.bytes + " bytes, imported " + i.imported;
  }

  // ---- the import ----------------------------------------------------------------------------------------------

  private static boolean loaded;

  private static synchronized void load() {
    if (!loaded) {
      System.loadLibrary("re4langpack");
      loaded = true;
    }
  }

  /**
   * Imports the pack behind `uri`: a folder (tree, ACTION_OPEN_DOCUMENT_TREE) or a ZIP file. gameRoot: the imported
   * discs ("game"). The old pack is replaced only once the new one is complete.
   */
  static Info importFrom(Context c, File gameRoot, Uri uri, boolean tree, AtomicBoolean cancel, Listener listener)
      throws IOException {
    load();
    File base = new File(AppStorage.base(c), FOLDER);
    if (!base.isDirectory() && !base.mkdirs()) throw new PackException(READ_FAILED, "", "cannot make " + base);
    if (base.getUsableSpace() < RESERVE)
      throw new PackException(NO_SPACE, "", "only " + base.getUsableSpace() + " bytes free");
    ContentResolver resolver = c.getContentResolver();
    String source = displayName(resolver, tree ? DocumentsContract.buildDocumentUriUsingTree(uri,
        DocumentsContract.getTreeDocumentId(uri)) : uri);
    long h = nativeOpen(gameRoot.getAbsolutePath(), folder(c).getAbsolutePath());
    try {
      check(h, nativeBegin(h));
      if (tree) walkTree(resolver, uri, h, cancel, listener);
      else walkZip(resolver, uri, h, cancel, listener);
      String name = source.replaceFirst("(?i)\\.zip$", "");
      String result = nativeFinish(h, name, source);
      check(h, result.startsWith("0\t") ? "" : result);
      Log.i(TAG, "language pack imported: " + result.replace('\t', ' '));
      return info(c);
    } finally {
      nativeClose(h);
    }
  }

  private static final class Entry {
    final String path, id;
    final long size;
    Entry(String path, String id, long size) { this.path = path; this.id = id; this.size = size; }
  }

  private static void walkTree(ContentResolver resolver, Uri tree, long h, AtomicBoolean cancel, Listener listener)
      throws IOException {
    List<Entry> files = new ArrayList<>();
    list(resolver, tree, DocumentsContract.getTreeDocumentId(tree), "", files, 0);
    long total = 0, done = 0;
    for (Entry e : files) total += e.size;
    importTotal = total;
    for (Entry e : files) {
      if (cancel.get()) throw new PackException(CANCELLED, "", "cancelled");
      int want = nativeWant(h, e.path);
      if (want == 2) check(h, nativeResult(h));
      if (want == 1) {
        try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(
            DocumentsContract.buildDocumentUriUsingTree(tree, e.id), "r")) {
          if (pfd == null) throw new PackException(READ_FAILED, e.path, "cannot open " + e.path);
          check(h, nativeAdd(h, e.path, pfd.getFd()));
        }
      }
      done += e.size;
      importDone = done;
      if (listener != null) listener.progress(done, total);
    }
  }

  // every file below the folder `id`, its path from the chosen folder ('/')
  private static void list(ContentResolver resolver, Uri tree, String id, String prefix, List<Entry> out, int depth)
      throws IOException {
    if (depth > 16) return;
    Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
    String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        DocumentsContract.Document.COLUMN_MIME_TYPE, DocumentsContract.Document.COLUMN_SIZE};
    List<String[]> dirs = new ArrayList<>();
    try (Cursor cur = resolver.query(children, columns, null, null, null)) {
      if (cur == null) throw new PackException(READ_FAILED, prefix, "cannot list " + prefix);
      while (cur.moveToNext()) {
        String child = cur.getString(0), name = cur.getString(1), mime = cur.getString(2);
        long size = cur.isNull(3) ? 0 : cur.getLong(3);
        String path = prefix.isEmpty() ? name : prefix + "/" + name;
        if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) dirs.add(new String[] {child, path});
        else out.add(new Entry(path, child, size));
      }
    }
    for (String[] d : dirs) list(resolver, tree, d[0], d[1], out, depth + 1);
  }

  private static void walkZip(ContentResolver resolver, Uri uri, long h, AtomicBoolean cancel, Listener listener)
      throws IOException {
    try (ParcelFileDescriptor pfd = resolver.openFileDescriptor(uri, "r")) {
      if (pfd == null) throw new PackException(READ_FAILED, "", "cannot open the ZIP");
      long size = pfd.getStatSize();
      check(h, nativeZipList(h, pfd.getFd(), size));
      int n = nativeZipCount(h);
      long total = 0, done = 0;
      for (int i = 0; i < n; i++) total += nativeZipSize(h, i);
      importTotal = total;
      for (int i = 0; i < n; i++) {
        if (cancel.get()) throw new PackException(CANCELLED, "", "cancelled");
        String name = nativeZipName(h, i);
        if (!name.endsWith("/")) {
          int want = nativeWant(h, name);
          if (want == 2) check(h, nativeResult(h));
          if (want == 1) check(h, nativeZipAdd(h, i));
        }
        done += nativeZipSize(h, i);
        importDone = done;
        if (listener != null) listener.progress(done, total);
      }
    }
  }

  // "" goes on; a refusal ("reason\tfile\tdetail\t...") throws
  private static void check(long h, String answer) throws PackException {
    if (answer == null || answer.isEmpty()) return;
    String[] f = answer.split("\t", -1);
    int reason;
    try {
      reason = Integer.parseInt(f[0]);
    } catch (NumberFormatException e) {
      reason = READ_FAILED;
    }
    if (reason == 0) return;
    Log.w(TAG, "language pack refused: " + answer.replace('\t', ' '));
    throw new PackException(reason, f.length > 1 ? f[1] : "", f.length > 2 ? f[2] : "");
  }

  private static String displayName(ContentResolver resolver, Uri uri) {
    try (Cursor c = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
      if (c != null && c.moveToFirst() && !c.isNull(0)) return c.getString(0);
    } catch (RuntimeException e) {
      Log.w(TAG, "name of " + uri + ": " + e);
    }
    String last = uri.getLastPathSegment();
    return last == null ? "language pack" : last.substring(last.lastIndexOf('/') + 1);
  }

  private static native long nativeOpen(String gameRoot, String dest);
  private static native String nativeBegin(long h);
  private static native int nativeWant(long h, String path);
  private static native String nativeAdd(long h, String path, int fd);
  private static native String nativeZipList(long h, int fd, long size);
  private static native int nativeZipCount(long h);
  private static native String nativeZipName(long h, int i);
  private static native long nativeZipSize(long h, int i);
  private static native String nativeZipAdd(long h, int i);
  private static native String nativeFinish(long h, String name, String source);
  private static native String nativeResult(long h);
  private static native void nativeClose(long h);
}
