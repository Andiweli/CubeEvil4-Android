package com.ast.cubeevil4.texturepacker;

/**
 * The packer's C++ side (src/main/cpp/packer_jni.cpp): the PC tool's core (port/tools/texpack/packer.h), so the phone
 * makes the same .c4tex file as the PC. A handle is one pack (a ZIP or a folder) until close().
 */
final class PackerNative {
  static {
    System.loadLibrary("texpacker");
  }

  private PackerNative() {}

  /** Opens file i of a folder's list read-only and hands its file descriptor over (detached), -1 when it cannot. */
  interface Opener {
    int open(int index);
  }

  /** The conversion's progress, on the converting thread. */
  interface Progress {
    void progress(int done, int total, long written);
  }

  /** A ZIP file (fd detached, closed by the handle); 0 and lastError() when it cannot be read. */
  static native long openZip(int fd, String name);

  /** A folder: every file's path from the folder ('/' between folders), its size, and how to open it. */
  static native long openFolder(String name, String[] paths, long[] sizes, Opener opener);

  static native String lastError();

  /** What the pack holds (Summary.parse). */
  static native String summary(long handle);

  /** The pack file's size at most for 256, 512 and 1024 with these choices (a variant per group, -1: none). */
  static native long[] estimate(long handle, int[] choices);

  /**
   * Converts into fd (open read-write, not closed here). "" when it worked, "cancelled" after cancel(), else why not;
   * report() then has the text for the report file.
   */
  static native String convert(long handle, int size, int[] choices, int fd, String date, String fileName,
      long memoryBudget, int threads, Progress progress);

  static native void cancel(long handle);

  static native String report(long handle);

  static native void close(long handle);
}
