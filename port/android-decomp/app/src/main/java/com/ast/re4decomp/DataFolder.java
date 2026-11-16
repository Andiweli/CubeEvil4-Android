package com.ast.re4decomp;

import android.Manifest;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Environment;
import android.util.Log;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * A data folder of the player's choice (1.0, the project owner's wish of 16.11.2026: "Daten sollen auf Userwunsch auch
 * außerhalb des /android/data/* Ordners eingelesen werden können beim DVD Import"). Only the flavour "sideload" has it
 * (BuildConfig.CUSTOM_DATA_FOLDER); every other flavour keeps the app's own folder (AppStorage) with no menu entry and
 * no permission. The start screen (MainActivity) chooses it: the storage permission first (all files access on
 * Android 11 and later, the storage permission with the legacy storage on Android 6 to 10; the manifest of
 * src/sideload), then a folder of the system's picker as a real path (pathOfTree), checked (check, prepare) and kept in
 * the preferences. AppStorage.base takes it while it can be used (usable), else the app's own folder with a note on the
 * start screen; nothing is deleted. The app's files in it (ENTRIES) can move along (move).
 */
final class DataFolder {
  private static final String TAG = "RE4Storage";
  /** BuildConfig.CUSTOM_DATA_FOLDER (tests set it; no other flavour turns it on). */
  static boolean enabled = BuildConfig.CUSTOM_DATA_FOLDER;
  private static final String PREFS = "data_folder", PATH = "path";
  /** A storage's root chosen: the data go into this folder of it. */
  static final String SUBFOLDER = "CubeEvil4";
  /**
   * The app's files in its data folder: the discs and the core's files (memory card, instant save, settings: ImportManager's
   * "game"), the texture packs (TexturePacks), the error logs (CoreLog), graphics captures (CoreActivity.capture).
   */
  static final String[] ENTRIES = {"game", "textures", "logs", "gx-dumps"};
  /** check: not on the device's storage, an SD card or a USB drive (a cloud folder, another app's documents). */
  static final String NOT_LOCAL = "not local";
  /** check: Android, Android/data or Android/obb (any app's). */
  static final String ANDROID = "android";
  /** Room kept free on the new folder's storage when copying. */
  static final long SPARE = 64L << 20;
  static final String STORAGE_AUTHORITY = "com.android.externalstorage.documents";
  static final String DOWNLOADS_AUTHORITY = "com.android.providers.downloads.documents";

  private DataFolder() {}

  /** The chosen folder's path; null: the app's own folder (always without the option). */
  static String chosen(Context c) {
    if (!enabled) return null;
    String p = prefs(c).getString(PATH, null);
    return p == null || p.isEmpty() ? null : p;
  }

  /** Keeps `path` as the data folder (null: the app's own folder again). */
  static void choose(Context c, String path) {
    if (path == null) prefs(c).edit().remove(PATH).commit();
    else prefs(c).edit().putString(PATH, path).commit();
  }

  private static SharedPreferences prefs(Context c) {
    return c.getApplicationContext().getSharedPreferences(PREFS, Context.MODE_PRIVATE);
  }

  /** The app may read and write the shared storage outside its own folder. */
  static boolean hasPermission(Context c) {
    if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
    if (Build.VERSION.SDK_INT >= 23)
      return c.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    return true;
  }

  /**
   * The chosen folder while it can be used; null: the app's own folder, none chosen or (why[0]) the permission is gone
   * or the folder cannot be written (an SD card or USB drive missing, the folder deleted). The choice stays.
   */
  static File usable(Context c, String[] why) {
    String p = chosen(c);
    if (p == null) return null;
    File dir = new File(p);
    String problem = !hasPermission(c) ? "no permission to the shared storage" : writable(dir);
    if (problem == null) return dir;
    if (why != null) why[0] = problem;
    Log.w(TAG, "data folder " + p + " cannot be used (" + problem + "): the app's own folder for now");
    return null;
  }

  /** Null when `dir` is a folder the app can read and write in (a file written and deleted), else why not. */
  static String writable(File dir) {
    if (!dir.isDirectory()) return "the folder is missing (an SD card or USB drive not there?)";
    if (!dir.canRead()) return "the folder cannot be read";
    File test = new File(dir, ".cubeevil4-write-test");
    try (OutputStream out = new FileOutputStream(test)) {
      out.write(1);
    } catch (IOException | SecurityException e) {
      return "the folder cannot be written (" + e.getMessage() + ")";
    }
    if (!test.delete()) return "a file in the folder cannot be deleted";
    return null;
  }

  /** As writable, the folder made first when it is missing. */
  static String prepare(File dir) {
    if (!dir.isDirectory() && !dir.mkdirs()) return "the folder cannot be made";
    return writable(dir);
  }

  /**
   * The system picker's folder (its tree's document id) as a real path: "primary:Games" is primaryRoot/Games,
   * "1234-ABCD:Games" /storage/1234-ABCD/Games (an SD card or USB drive), the Downloads' "raw:/storage/..." that path
   * and its "downloads" primaryRoot/Download; null for anything else (a cloud folder, another app's documents).
   */
  static String pathOfTree(String authority, String documentId, String primaryRoot) {
    if (documentId == null || primaryRoot == null) return null;
    if (DOWNLOADS_AUTHORITY.equals(authority)) {
      if (documentId.startsWith("raw:")) return clean(documentId.substring(4));
      return "downloads".equals(documentId) ? clean(primaryRoot + "/Download") : null;
    }
    if (!STORAGE_AUTHORITY.equals(authority)) return null;
    int colon = documentId.indexOf(':');
    if (colon <= 0) return null;
    String volume = documentId.substring(0, colon), rest = documentId.substring(colon + 1);
    String root;
    if ("primary".equalsIgnoreCase(volume)) root = primaryRoot;
    else if (volume.matches("[A-Za-z0-9][A-Za-z0-9-]*")) root = "/storage/" + volume;
    else return null;
    return clean(rest.isEmpty() ? root : root + "/" + rest);
  }

  /** The path without doubled or trailing "/" (null when it is not absolute). */
  static String clean(String path) {
    if (path == null || !path.startsWith("/")) return null;
    String p = path.replaceAll("/+", "/");
    while (p.length() > 1 && p.endsWith("/")) p = p.substring(0, p.length() - 1);
    return p;
  }

  /**
   * {the folder to use, null} or {null, why not (NOT_LOCAL, ANDROID)}: only folders on the device's storage, an SD card
   * or a USB drive (under /storage); not Android, Android/data or Android/obb, nor anything in them (any app's); a
   * storage's root becomes its folder CubeEvil4.
   */
  static String[] check(String path, String primaryRoot) {
    path = clean(path);
    primaryRoot = clean(primaryRoot);
    if (path == null || primaryRoot == null || !path.startsWith("/storage/")) return new String[] {null, NOT_LOCAL};
    String root;
    if (path.equals(primaryRoot) || path.startsWith(primaryRoot + "/")) {
      root = primaryRoot;
    } else {
      int next = path.indexOf('/', "/storage/".length());
      root = next < 0 ? path : path.substring(0, next);
      // (another user's storage, or the storage's links: not a volume of its own)
      if (root.equals("/storage/emulated") || root.equals("/storage/self")) return new String[] {null, NOT_LOCAL};
    }
    String rel = path.length() > root.length() ? path.substring(root.length() + 1) : "";
    if (rel.isEmpty()) return new String[] {root + "/" + SUBFOLDER, null};
    String lower = rel.toLowerCase(Locale.ROOT);
    if (lower.equals("android") || under(lower, "android/data") || under(lower, "android/obb"))
      return new String[] {null, ANDROID};
    return new String[] {path, null};
  }

  private static boolean under(String rel, String folder) {
    return rel.equals(folder) || rel.startsWith(folder + "/");
  }

  /** The app's files (ENTRIES) in `dir`, those with a file in them (an empty "game" the imports made is no data). */
  static List<String> present(File dir) {
    List<String> found = new ArrayList<>();
    for (String e : ENTRIES) if (hasFile(new File(dir, e))) found.add(e);
    return found;
  }

  /** `f` is a file, or a folder with a file somewhere in it. */
  static boolean hasFile(File f) {
    if (f.isFile()) return true;
    File[] all = f.listFiles();
    if (all != null) for (File c : all) if (hasFile(c)) return true;
    return false;
  }

  /** The progress of move: bytes copied of all to copy. */
  interface Progress {
    void at(long done, long total);
  }

  /**
   * Moves the app's files (ENTRIES) from `from` into `to`: on the same storage each renamed where it allows it, else
   * copied, and the originals deleted only when every copy is complete. Null when done; else why not, and everything
   * is as it was (the copies made deleted, the renamed ones back). Nothing is moved over files of the same name.
   */
  static String move(File from, File to, Progress progress) {
    return move(from, to, progress, true);
  }

  /** move; `rename` false: every entry copied (tests: the way between two storages). */
  static String move(File from, File to, Progress progress, boolean rename) {
    List<String> entries = present(from);
    for (String e : entries) if (hasFile(new File(to, e))) return to + " already holds " + e;
    boolean sameVolume = rename && volume(from).equals(volume(to));
    List<String> renamed = new ArrayList<>(), copy = new ArrayList<>();
    for (String e : entries) {
      if (sameVolume && new File(from, e).renameTo(new File(to, e))) renamed.add(e);
      else copy.add(e);
    }
    long total = 0;
    for (String e : copy) total += size(new File(from, e));
    long free = to.getUsableSpace();
    if (total > 0 && free < total + SPARE) {
      renameBack(renamed, from, to);
      return "not enough free space in " + to + ": " + megabytes(total + SPARE) + " needed, " + megabytes(free) + " free";
    }
    List<File> made = new ArrayList<>();
    long[] done = {0};
    try {
      for (String e : copy) copyTree(new File(from, e), new File(to, e), made, done, total, progress);
    } catch (IOException | RuntimeException ex) {
      for (int i = made.size() - 1; i >= 0; i--) made.get(i).delete();
      renameBack(renamed, from, to);
      return ex.getMessage() == null ? ex.toString() : ex.getMessage();
    }
    // every copy complete: the originals go
    for (String e : copy) deleteTree(new File(from, e));
    Log.i(TAG, "data moved from " + from + " to " + to + ": renamed " + renamed + ", copied " + copy + " ("
        + megabytes(total) + ")");
    return null;
  }

  /** The storage a path is on: the device's own (/storage/emulated/N), an SD card or USB drive (/storage/ID), else /. */
  static String volume(File f) {
    String p = f.getAbsolutePath();
    if (p.startsWith("/storage/emulated/")) {
      int end = p.indexOf('/', "/storage/emulated/".length());
      return end < 0 ? p : p.substring(0, end);
    }
    if (p.startsWith("/storage/")) {
      int end = p.indexOf('/', "/storage/".length());
      return end < 0 ? p : p.substring(0, end);
    }
    if (p.startsWith("/sdcard")) return "/storage/emulated/0";
    return p.startsWith("/data/") ? "/data" : "/";
  }

  private static void renameBack(List<String> renamed, File from, File to) {
    for (String e : renamed) {
      if (!new File(to, e).renameTo(new File(from, e))) Log.w(TAG, "could not rename " + e + " back to " + from);
    }
  }

  static long size(File f) {
    if (f.isFile()) return f.length();
    long n = 0;
    File[] all = f.listFiles();
    if (all != null) for (File c : all) n += size(c);
    return n;
  }

  private static void copyTree(File from, File to, List<File> made, long[] done, long total, Progress progress)
      throws IOException {
    if (from.isDirectory()) {
      if (!to.isDirectory()) {
        if (!to.mkdirs()) throw new IOException("cannot make " + to);
        made.add(to);
      }
      File[] all = from.listFiles();
      if (all == null) throw new IOException("cannot list " + from);
      for (File c : all) copyTree(c, new File(to, c.getName()), made, done, total, progress);
      return;
    }
    made.add(to);
    byte[] buffer = new byte[1 << 20];
    try (InputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(to)) {
      int n;
      while ((n = in.read(buffer)) > 0) {
        out.write(buffer, 0, n);
        done[0] += n;
        if (progress != null) progress.at(done[0], total);
      }
      out.getFD().sync();
    }
    if (to.length() != from.length()) throw new IOException("the copy of " + from.getName() + " is incomplete");
    to.setLastModified(from.lastModified());
  }

  private static void deleteTree(File f) {
    File[] all = f.isDirectory() ? f.listFiles() : null;
    if (all != null) for (File c : all) deleteTree(c);
    if (!f.delete()) Log.w(TAG, "could not delete " + f);
  }

  static String megabytes(long bytes) {
    return String.format(Locale.ROOT, "%.0f MB", bytes / 1048576.0);
  }
}
