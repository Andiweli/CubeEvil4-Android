package com.ast.re4decomp;

import android.app.ActivityManager;
import android.content.ContentResolver;
import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import java.io.EOFException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.CRC32;

/**
 * HD texture packs of the player (wish of the project owner, 12.11.2026; port/docs/textur-lader-plan.md): the one
 * .c4tex file the PC tool "CubeEvil4 Texture Packer" made (port/tools/texpack, layout c4tex.h and
 * port/docs/textur-paket-format.md), imported into a folder of its own, "textures" in the app's data folder, next to
 * the discs ("game") and never inside them. Nothing here writes or deletes anywhere else. CubeEvil4 brings no
 * textures and no links to any; players convert packs they obtained themselves.
 *
 * <p>The switches (GRAPHICS_PREFS): ON (default on once a pack is there) and HALF ("Sharpness: Half", the pack's
 * next smaller level). The renderer gets them through CoreBridge.setTexturePack (apply).
 */
final class TexturePacks {
  private static final String TAG = "RE4Textures";
  static final String FOLDER = "textures", FILE = "pack.c4tex", PART = "pack.c4tex.part";
  static final String ON = "hdTextures", HALF = "hdHalf";
  /** Free space kept besides the pack. */
  static final long RESERVE = 100L << 20;
  static final int HEADER = 128, ENTRY = 48, VERSION = 1;
  static final byte[] MAGIC = "C4TEXPAK".getBytes(StandardCharsets.US_ASCII);

  // what can go wrong at an import (PackException.reason), each a text of its own in the menu
  static final int NOT_PACK = 1, OTHER_VERSION = 2, NO_SPACE = 3, INCOMPLETE = 4, DAMAGED = 5, CANCELLED = 6,
      READ_FAILED = 7;

  static final class PackException extends IOException {
    final int reason;
    final long need, free;
    PackException(int reason, String message) { this(reason, message, 0, 0); }
    PackException(int reason, String message, long need, long free) {
      super(message);
      this.reason = reason;
      this.need = need;
      this.free = free;
    }
  }

  /** The imported pack as its header and info text say. */
  static final class Info {
    String name = "", created = "", source = "";
    int images, maxSide;
    long bytes;
  }

  /** An import's progress, on the main thread (CoreActivity). */
  interface Listener {
    void progress(long done, long total);
    /** info: the pack now in use; failure: why not (null when it worked) */
    void done(Info info, PackException failure);
  }

  // an import running now (the menu shows it again when it is opened again)
  static volatile boolean importing;
  static volatile long importDone, importTotal;

  private TexturePacks() {}

  static File folder(Context c) { return new File(AppStorage.base(c), FOLDER); }
  static File file(Context c) { return new File(folder(c), FILE); }

  /** The header's problem (null: fine): the magic, the version, its checksum, the size (length < 0: unknown). */
  static PackException headerProblem(byte[] head, long length) {
    if (head == null || head.length < HEADER) return new PackException(NOT_PACK, "too short");
    for (int i = 0; i < MAGIC.length; i++)
      if (head[i] != MAGIC[i]) return new PackException(NOT_PACK, "not a CubeEvil4 texture pack");
    ByteBuffer b = ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN);
    if (b.getInt(8) != VERSION) return new PackException(OTHER_VERSION, "version " + b.getInt(8));
    CRC32 crc = new CRC32();
    crc.update(head, 0, 64);
    if ((int) crc.getValue() != b.getInt(64)) return new PackException(DAMAGED, "header checksum");
    long fileBytes = b.getLong(40), indexOffset = b.getLong(24);
    long count = b.getInt(16) & 0xFFFFFFFFL;
    if (b.getInt(20) != ENTRY || fileBytes < HEADER || indexOffset < HEADER || indexOffset + count * ENTRY > fileBytes)
      return new PackException(DAMAGED, "index outside the file");
    if (length >= 0 && length != fileBytes)
      return new PackException(INCOMPLETE, "the file has " + length + " bytes, the pack " + fileBytes);
    return null;
  }

  /** The pack file's header, info text and index checked (the index's checksum); its Info. */
  static Info check(File f) throws IOException {
    try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
      byte[] head = new byte[HEADER];
      if (r.length() < HEADER) throw new PackException(NOT_PACK, "too short");
      r.readFully(head);
      PackException p = headerProblem(head, r.length());
      if (p != null) throw p;
      ByteBuffer b = ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN);
      int count = b.getInt(16);
      long indexOffset = b.getLong(24);
      CRC32 crc = new CRC32();
      byte[] buf = new byte[ENTRY * 1024];
      r.seek(indexOffset);
      long left = (count & 0xFFFFFFFFL) * ENTRY;
      while (left > 0) {
        int n = (int) Math.min(buf.length, left);
        r.readFully(buf, 0, n);
        crc.update(buf, 0, n);
        left -= n;
      }
      if ((int) crc.getValue() != b.getInt(52)) throw new PackException(DAMAGED, "index checksum");
      Info info = new Info();
      info.images = count;
      info.maxSide = b.getInt(48);
      info.bytes = b.getLong(40);
      int infoOffset = b.getInt(56), infoBytes = b.getInt(60);
      if (infoBytes > 0 && infoBytes <= 65536 && infoOffset >= HEADER && infoOffset + (long) infoBytes <= r.length()) {
        byte[] text = new byte[infoBytes];
        r.seek(infoOffset);
        r.readFully(text);
        readInfo(new String(text, StandardCharsets.UTF_8), info);
      }
      return info;
    }
  }

  static void readInfo(String text, Info info) {
    for (String line : text.split("\n")) {
      int eq = line.indexOf('=');
      if (eq < 0) continue;
      String key = line.substring(0, eq), value = line.substring(eq + 1).trim();
      if (key.equals("name")) info.name = value;
      else if (key.equals("created")) info.created = value;
      else if (key.equals("source")) info.source = value;
    }
  }

  /** The imported pack, null when there is none or it cannot be used. */
  static Info info(Context c) {
    File f = file(c);
    if (!f.isFile()) return null;
    try {
      return check(f);
    } catch (IOException e) {
      Log.w(TAG, "the imported pack: " + e.getMessage());
      return null;
    }
  }

  /** The replacements' budget on the GPU in MB, by the device's memory (port/docs/textur-lader-plan.md, section 3). */
  static int budgetFor(long totalMemory, boolean is64) {
    long gb = 1L << 30;
    int mb = totalMemory <= 4 * gb + gb / 2 ? 256 : totalMemory <= 6 * gb + gb / 2 ? 512 : 768;
    return is64 ? mb : Math.min(mb, 192);
  }

  static int budgetMb(Context c) {
    ActivityManager.MemoryInfo m = new ActivityManager.MemoryInfo();
    ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
    if (am != null) am.getMemoryInfo(m);
    boolean is64 = android.os.Build.VERSION.SDK_INT >= 23 && android.os.Process.is64Bit();
    return budgetFor(m.totalMem > 0 ? m.totalMem : 3L << 30, is64);
  }

  /** The switches to the renderer: the pack while ON and imported, else none. */
  static void apply(Context c, SharedPreferences g) {
    File f = file(c);
    boolean on = g.getBoolean(ON, true) && f.isFile();
    String error = CoreBridge.setTexturePack(on ? f.getAbsolutePath() : "", g.getBoolean(HALF, false), budgetMb(c));
    if (error != null && !error.isEmpty()) Log.w(TAG, "the pack cannot be used: " + error);
  }

  /**
   * Copies the chosen file into the textures folder (as PART, renamed at the end, so a broken import leaves nothing
   * that looks like a pack) after checking its header and the free space, then checks the copy. The pack before it is
   * replaced only when the new one is complete. Runs off the main thread.
   */
  static Info importFrom(Context c, Uri uri, AtomicBoolean cancel, Listener progress) throws IOException {
    ContentResolver cr = c.getContentResolver();
    File dir = folder(c);
    File part = new File(dir, PART);
    try (ParcelFileDescriptor pfd = cr.openFileDescriptor(uri, "r")) {
      if (pfd == null) throw new PackException(READ_FAILED, "cannot open the file");
      try (FileInputStream in = new FileInputStream(pfd.getFileDescriptor())) {
        byte[] head = new byte[HEADER];
        try {
          readFully(in, head);
        } catch (EOFException e) {
          throw new PackException(NOT_PACK, "too short");
        }
        PackException p = headerProblem(head, pfd.getStatSize());
        if (p != null) throw p;
        long size = ByteBuffer.wrap(head).order(ByteOrder.LITTLE_ENDIAN).getLong(40);
        if (!dir.isDirectory() && !dir.mkdirs()) throw new PackException(READ_FAILED, "cannot make " + dir);
        long free = dir.getUsableSpace();
        if (free < size + RESERVE) throw new PackException(NO_SPACE, "not enough space", size + RESERVE, free);
        long done = 0;
        try (FileOutputStream out = new FileOutputStream(part)) {
          out.write(head);
          done = head.length;
          byte[] buf = new byte[1 << 20];
          long lastReport = 0;
          while (done < size) {
            if (cancel.get()) throw new PackException(CANCELLED, "cancelled");
            int n = in.read(buf, 0, (int) Math.min(buf.length, size - done));
            if (n < 0) throw new PackException(INCOMPLETE, "the file ends at " + done + " of " + size + " bytes");
            out.write(buf, 0, n);
            done += n;
            if (done - lastReport >= (8L << 20) || done == size) {
              lastReport = done;
              importDone = done;
              importTotal = size;
              if (progress != null) progress.progress(done, size);
            }
          }
          out.getFD().sync();
        }
      }
      Info info = check(part);
      // the renderer lets go of the pack before, then the new one takes its place
      CoreBridge.setTexturePack("", false, 16);
      File pack = new File(dir, FILE);
      if (pack.exists() && !pack.delete()) throw new PackException(READ_FAILED, "cannot replace the old pack");
      if (!part.renameTo(pack)) throw new PackException(READ_FAILED, "cannot rename the copy");
      Log.i(TAG, "imported " + info.name + ": " + info.images + " images, " + info.bytes + " bytes");
      return info;
    } catch (IOException | RuntimeException e) {
      if (part.exists() && !part.delete()) Log.w(TAG, "cannot delete " + part);
      if (e instanceof PackException) throw (PackException) e;
      throw new PackException(READ_FAILED, e.getMessage() != null ? e.getMessage() : e.toString());
    }
  }

  /** Deletes the textures folder's files (only there) after the renderer let go of the pack. */
  static boolean delete(Context c) {
    CoreBridge.setTexturePack("", false, 16);
    File dir = folder(c);
    boolean ok = true;
    File[] files = dir.listFiles();
    if (files != null)
      for (File f : files)
        if (f.isFile() && !f.delete()) ok = false;
    if (dir.isDirectory() && !dir.delete()) ok = false;
    return ok;
  }

  private static void readFully(InputStream in, byte[] b) throws IOException {
    int at = 0;
    while (at < b.length) {
      int n = in.read(b, at, b.length - at);
      if (n < 0) throw new EOFException();
      at += n;
    }
  }
}
