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
  public static final String PROMPT = "Select ISO/GCM files of the debug build or of the European release.";
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
  /** The verified room hashes of the release of an imported disc (rooms.tsv, rooms-eu.tsv). */
  public InputStream roomCatalog(File disc) throws IOException {
    String release = DiscInstaller.release(disc);
    return context.getAssets().open(GameCubeDisc.catalog("rooms", release == null ? GameCubeDisc.DEBUG : release));
  }
  /** The name of a release for the launcher's texts. */
  public static String releaseName(String release) {
    return GameCubeDisc.EUROPEAN.equals(release) ? "European release" : "debug build";
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
                   new ParcelFileDescriptor.AutoCloseInputStream(pfd);
               FileOutputStream w = new FileOutputStream(source)) {
            byte[] b = new byte[1024 * 1024];
            long done = 0, last = 0;
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
        // The room files are not part of the code check; a damaged image shows only when the game
        // enters a room whose file is wrong (St2/r222.das). Non-blocking: say it, do not refuse.
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
