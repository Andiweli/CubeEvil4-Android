package com.ast.cubeevil4.texturepacker;

import android.annotation.TargetApi;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.system.ErrnoException;
import android.system.Os;
import android.system.StructStatVfs;
import android.util.Log;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Where the pack file goes. Android 10 and later: the Download folder of the chosen storage (MediaStore), as a pending
 * file other apps do not see until it is complete; a cancelled or failed conversion deletes it. Android 7 to 9: the
 * file the player chose in Android's save dialog, deleted the same way.
 */
final class Output {
  private static final String TAG = "TexPacker";
  static final String MIME = "application/octet-stream";

  final Uri uri;
  final ParcelFileDescriptor file;
  private final boolean mediaStore;

  private Output(Uri uri, ParcelFileDescriptor file, boolean mediaStore) {
    this.uri = uri;
    this.file = file;
    this.mediaStore = mediaStore;
  }

  /** A new pending file in the Download folder of the storage `volume` (MediaStore's volume name). */
  @TargetApi(29)
  static Output download(Context c, String volume, String name) throws IOException {
    ContentResolver cr = c.getContentResolver();
    ContentValues v = new ContentValues();
    v.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
    v.put(MediaStore.MediaColumns.MIME_TYPE, MIME);
    v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/");
    v.put(MediaStore.MediaColumns.IS_PENDING, 1);
    Uri uri = cr.insert(MediaStore.Downloads.getContentUri(volume), v);
    if (uri == null) throw new IOException("cannot make a file in the Download folder");
    try {
      ParcelFileDescriptor f = cr.openFileDescriptor(uri, "rw");
      if (f == null) throw new IOException("cannot open the new file");
      return new Output(uri, f, true);
    } catch (IOException | RuntimeException e) {
      cr.delete(uri, null, null);
      throw e instanceof IOException ? (IOException) e : new IOException(e.toString());
    }
  }

  /** The file the save dialog made (Android 7 to 9). */
  static Output document(Context c, Uri uri) throws IOException {
    ParcelFileDescriptor f = c.getContentResolver().openFileDescriptor(uri, "rw");
    if (f == null) throw new IOException("cannot open the file");
    return new Output(uri, f, false);
  }

  /** Free space where the file is written, -1 when unknown. */
  long free() {
    try {
      StructStatVfs s = Os.fstatvfs(file.getFileDescriptor());
      return s.f_bavail * s.f_frsize;
    } catch (ErrnoException e) {
      return -1;
    }
  }

  /** Done: closed and visible to every app; its name as Android shows it. */
  String finish(Context c) throws IOException {
    file.close();
    ContentResolver cr = c.getContentResolver();
    if (mediaStore && android.os.Build.VERSION.SDK_INT >= 29) {
      ContentValues v = new ContentValues();
      v.put(MediaStore.MediaColumns.IS_PENDING, 0);
      cr.update(uri, v, null, null);
    }
    return name(c, uri);
  }

  /** Cancelled or failed: nothing left behind. */
  void discard(Context c) {
    try {
      file.close();
    } catch (IOException e) {
      Log.w(TAG, "close: " + e);
    }
    try {
      if (mediaStore) c.getContentResolver().delete(uri, null, null);
      else DocumentsContract.deleteDocument(c.getContentResolver(), uri);
    } catch (RuntimeException | IOException e) {
      Log.w(TAG, "cannot delete the unfinished file: " + e);
    }
  }

  /** The report next to the pack file (Android 10 and later); its name, null when it could not be written. */
  @TargetApi(29)
  static String report(Context c, String volume, String name, String text) {
    ContentResolver cr = c.getContentResolver();
    ContentValues v = new ContentValues();
    v.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
    v.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
    v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/");
    Uri uri = null;
    try {
      uri = cr.insert(MediaStore.Downloads.getContentUri(volume), v);
      if (uri == null) return null;
      try (OutputStream out = cr.openOutputStream(uri)) {
        if (out == null) throw new IOException("cannot open");
        out.write(text.replace("\n", "\r\n").getBytes(StandardCharsets.UTF_8));
      }
      return name(c, uri);
    } catch (IOException | RuntimeException e) {
      Log.w(TAG, "report: " + e);
      if (uri != null) cr.delete(uri, null, null);
      return null;
    }
  }

  static String name(Context c, Uri uri) {
    try (Cursor q = c.getContentResolver().query(uri, new String[] {MediaStore.MediaColumns.DISPLAY_NAME}, null, null,
        null)) {
      if (q != null && q.moveToFirst() && q.getString(0) != null) return q.getString(0);
    } catch (RuntimeException e) {
      Log.w(TAG, "name: " + e);
    }
    String last = uri.getLastPathSegment();
    return last != null ? last : uri.toString();
  }
}
