package com.ast.re4decomp.disc;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;

/**
 * RVZ, Dolphin's compressed disc image: unpacked once at the import into the plain disc image
 * (ISO/GCM) the import reads; native side disc_rvz.cpp in libre4rvz.so (zstd).
 */
public final class RvzImage {
  private RvzImage() {}
  private static boolean loaded;
  private static synchronized void load() {
    if (!loaded) {
      System.loadLibrary("re4rvz");
      loaded = true;
    }
  }

  public interface Progress {
    /** False cancels. */
    boolean step(long done, long total);
  }

  /** True when the bytes start with the RVZ magic. */
  public static boolean isRvz(byte[] head, int n) {
    return n >= 4 && head[0] == 'R' && head[1] == 'V' && head[2] == 'Z' && head[3] == 1;
  }

  public static boolean isRvz(File file) throws IOException {
    try (InputStream in = new FileInputStream(file)) {
      byte[] head = new byte[4];
      return isRvz(head, in.read(head));
    }
  }

  /** {disc size, file size}; IOException with the reason when it cannot be unpacked. */
  public static long[] sizes(File file) throws IOException {
    load();
    long[] sizes = new long[2];
    String error = check(file.getAbsolutePath(), sizes);
    if (error != null)
      throw new IOException(error);
    return sizes;
  }

  /** Free storage the import of an RVZ needs: the unpacked image next to the RVZ, then the extracted files next to the image. */
  public static long storageNeeded(long discSize, long fileSize) {
    return discSize + Math.max(fileSize, discSize) + 64L * 1024 * 1024;
  }

  public static String gigabytes(long bytes) {
    return String.format(Locale.ROOT, "%.1f GB", bytes / 1e9);
  }

  public static void unpack(File rvz, File iso, Progress progress) throws IOException {
    load();
    String error = unpack(rvz.getAbsolutePath(), iso.getAbsolutePath(), progress);
    if (error != null)
      throw new IOException(error);
  }

  private static native String check(String path, long[] sizes);
  private static native String unpack(String in, String out, Progress progress);
}
