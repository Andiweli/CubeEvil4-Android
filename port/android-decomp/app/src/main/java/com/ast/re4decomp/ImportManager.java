package com.ast.re4decomp;

import android.content.Context;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import com.ast.re4decomp.disc.*;
import java.io.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Application-owned worker survives Activity rotation. No Activity is retained
 * while detached.
 */
public final class ImportManager {
  public interface Listener {
    void changed();
  }
  private final Context context;
  private final Handler main = new Handler(Looper.getMainLooper());
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private final AtomicBoolean cancelled = new AtomicBoolean();
  private Listener listener;
  public volatile boolean busy;
  /** The status before anything happened (the core launcher shows its own heading instead). */
  public static final String PROMPT = "Select ISO/GCM/RVZ files of the debug build, the European or the Japanese release. "
      + "RVZ files are unpacked first and need extra free storage (about 3 GB per disc).";
  public volatile String status = PROMPT;
  public final File root;
  public ImportManager(Context context) {
    this.context = context.getApplicationContext();
    File external = context.getExternalFilesDir(null);
    root = external == null ? null : new File(external, "game");
    if (root == null)
      status = "App data storage is unavailable. Please check the storage device.";
    else
      try {
        DiscInstaller.recover(root);
        status = installedWarning(status);
      } catch (IOException e) {
        status = e.getMessage();
      }
  }
  /** Imports of older versions never compared the room files with the verified disc: do it from their manifest. */
  private String installedWarning(String otherwise) {
    String mixed = mixedReleases();
    if (mixed != null)
      return mixed;
    for (int n = 1; n <= 2; n++) {
      File disc = new File(root, "disc" + n);
      if (!DiscInstaller.ready(disc, n))
        continue;
      try {
        String warning = RoomFileCheck.verify(disc, roomCatalog(disc), n).warning(n);
        if (warning != null)
          return warning;
      } catch (IOException | RuntimeException ignored) { /* the check is optional */
      }
    }
    return otherwise;
  }
  /**
   * The verified hashes of the release of an imported disc: every file (files.tsv, files-eu.tsv) when the
   * import takes every file (core mode), else the room files (rooms.tsv, rooms-eu.tsv).
   */
  public InputStream roomCatalog(File disc) throws IOException {
    String release = DiscInstaller.release(disc);
    return context.getAssets().open(GameCubeDisc.catalog(GameCubeDisc.importEverything ? "files" : "rooms",
        release == null ? GameCubeDisc.DEBUG : release));
  }
  /** The name of a release for the launcher's texts. */
  public static String releaseName(String release) {
    return GameCubeDisc.DEBUG.equals(release) || release == null ? "debug build" : GameCubeDisc.releaseTitle(release);
  }
  /** Null, or the line saying that the two imported discs are of different releases. */
  public String mixedReleases() {
    if (root == null)
      return null;
    String one = DiscInstaller.release(new File(root, "disc1")), two = DiscInstaller.release(new File(root, "disc2"));
    if (one == null || two == null || one.equals(two))
      return null;
    return "Disc 1 is the " + releaseName(one) + ", Disc 2 the " + releaseName(two)
        + ": import both discs of one release.";
  }
  public void attach(Listener l) {
    listener = l;
    l.changed();
  }
  public void detach(Listener l) {
    if (listener == l)
      listener = null;
  }
  private void notifyUi() {
    main.post(() -> {
      if (listener != null)
        listener.changed();
    });
  }
  public void cancel() { cancelled.set(true); }
  private void check() throws IOException {
    if (cancelled.get())
      throw new IOException(
          "Import canceled; existing data has been preserved.");
  }
  private static int readFully(InputStream in, byte[] b) throws IOException {
    int n = 0, r;
    while (n < b.length && (r = in.read(b, n, b.length - n)) >= 0)
      n += r;
    return n;
  }
  private static long be64(byte[] b, int at) {
    long v = 0;
    for (int i = 0; i < 8; i++)
      v = v << 8 | (b[at + i] & 0xFF);
    return v;
  }
  /**
   * Refuses an RVZ import the storage cannot hold: the RVZ copy, the unpacked image and the extracted
   * files (RvzImage.storageNeeded), less the `staged` bytes of it already there.
   */
  private void rvzStorage(long disc, long rvzBytes, long staged) throws IOException {
    if (disc <= 0 || disc > 2L * 1024 * 1024 * 1024)
      throw new IOException("The RVZ file is damaged or holds no GameCube disc");
    long need = RvzImage.storageNeeded(disc, rvzBytes) - staged, free = root.getUsableSpace();
    if (free < need)
      throw new IOException("Import not possible: not enough free storage to unpack the RVZ file (needs about "
          + RvzImage.gigabytes(need) + " free, " + RvzImage.gigabytes(free) + " are free). Free some storage and "
          + "try again, or import the disc as an ISO/GCM file (the import copies it first and needs nearly as much).");
  }
  public void start(Uri uri, int expectedDisc) {
    if (busy || root == null)
      return;
    busy = true;
    cancelled.set(false);
    status = "Disc " + expectedDisc + ": Reading …";
    notifyUi();
    worker.execute(() -> {
      File incoming = new File(root, ".incoming");
      try {
        DiscInstaller.delete(incoming);
        if (!incoming.mkdirs())
          throw new IOException("Could not create temporary directory");
        File source = new File(incoming, "source.gcm");
        // an RVZ (Dolphin's compressed image) is staged as it is and unpacked to source.gcm
        File rvz = null;
        // Stage locally: document providers may supply non-seekable cloud/pipe
        // descriptors.
        try (ParcelFileDescriptor pfd =
                 context.getContentResolver().openFileDescriptor(uri, "r")) {
          if (pfd == null)
            throw new IOException("Could not open disc file");
          long total = pfd.getStatSize();
          if (total > 2L * 1024 * 1024 * 1024)
            throw new IOException(
                "File is not a supported raw GameCube disc");
          if (total > 0 && root.getUsableSpace() < total + 64L * 1024 * 1024)
            throw new IOException("Not enough storage to read the disc");
          try (InputStream r =
                   new ParcelFileDescriptor.AutoCloseInputStream(pfd)) {
            byte[] head = new byte[0x48];
            int h = readFully(r, head);
            if (RvzImage.isRvz(head, h)) {
              rvz = new File(incoming, "source.rvz");
              // before copying it: room for the unpacked image and the extracted files
              long disc = h >= 0x2C ? be64(head, 0x24) : 0;
              rvzStorage(disc, total > 0 ? total : disc, 0);
            }
            try (FileOutputStream w = new FileOutputStream(rvz != null ? rvz : source)) {
              w.write(head, 0, h);
              byte[] b = new byte[1024 * 1024];
              long done = h, last = 0;
              int n;
              while ((n = r.read(b)) >= 0) {
                check();
                if (n == 0)
                  continue;
                done += n;
                if (done > 2L * 1024 * 1024 * 1024)
                  throw new IOException("Disc file is too large");
                w.write(b, 0, n);
                long now = System.nanoTime();
                if (now - last > 200_000_000L) {
                  last = now;
                  status = "Reading disc: " + done / 1048576 + " MiB";
                  notifyUi();
                }
              }
              w.getFD().sync();
            }
          }
        }
        if (rvz != null) {
          long[] sizes = RvzImage.sizes(rvz);
          rvzStorage(sizes[0], sizes[1], sizes[1]);  // the RVZ is on the storage already
          final long[] shown = {0};
          final int disc = expectedDisc;
          RvzImage.unpack(rvz, source, (done, total) -> {
            if (cancelled.get())
              return false;
            long now = System.nanoTime();
            if (now - shown[0] > 200_000_000L || done == total) {
              shown[0] = now;
              status = "Disc " + disc + ": unpacking RVZ: " + (total > 0 ? done * 100 / total : 0) + " %";
              notifyUi();
            }
            return true;
          });
          check();
          if (!rvz.delete())
            throw new IOException("Could not remove the RVZ copy");
        }
        final long[] last = {0};
        // the code check of the disc's own release (references.tsv / references-eu.tsv)
        DiscInstaller.install(
            source, root, expectedDisc,
            release -> new ReferenceCatalog(context.getAssets().open(GameCubeDisc.catalog("references", release))),
            (phase, done, total) -> {
              check();
              long now = System.nanoTime();
              if (now - last[0] > 200_000_000L || done == total) {
                last[0] = now;
                status =
                    phase + ": " + (total > 0 ? done * 100 / total : 0) + " %";
                notifyUi();
              }
            });
        File imported = new File(root, "disc" + expectedDisc);
        status = "Disc " + expectedDisc + " (" + releaseName(DiscInstaller.release(imported))
            + ") verified and imported successfully.";
        // The code check covers the program only; a damaged image shows only when the game reads a
        // file that is wrong (St2/r222.das): every file against the catalog. Non-blocking: say it, do not refuse.
        try {
          RoomFileCheck rooms = RoomFileCheck.verify(imported, roomCatalog(imported), expectedDisc);
          String warning = rooms.warning(expectedDisc);
          if (warning != null) status = warning;
        } catch (IOException | RuntimeException ignored) { /* the check is optional */
        }
        // a debug disc next to a European one: the game does not start until both match
        String mixed = mixedReleases();
        if (mixed != null) status = mixed;
      } catch (IOException | RuntimeException e) {
        status =
            e.getMessage() == null ? "Import failed" : e.getMessage();
      } finally {
        try {
          DiscInstaller.delete(incoming);
        } catch (IOException ignored) { /* cleaned next launch */
        }
        busy = false;
        notifyUi();
      }
    });
  }
}
