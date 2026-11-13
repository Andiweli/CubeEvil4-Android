package com.ast.cubeevil4.texturepacker;

import android.app.ActivityManager;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.Uri;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.os.SystemClock;
import android.util.Log;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * The conversion as a foreground service (a notification with its progress and "Cancel"), so it goes on with the
 * screen off or another app in front; a partial wake lock keeps the CPU awake. One at a time.
 */
public final class ConvertService extends Service {
  private static final String TAG = "TexPacker";
  static final String EXTRA_VOLUME = "volume", EXTRA_DOCUMENT = "document", ACTION_CANCEL = "cancel";
  private static final String CHANNEL = "convert";
  private static final int NOTE = 1, NOTE_DONE = 2;

  private static volatile boolean running;
  private NotificationManager notes;
  private PowerManager.WakeLock wake;
  private long lastNote;

  static boolean running() {
    return running;
  }

  /** Starts the conversion of Job's pack: into the Download folder of `volume`, or (Android 7 to 9) into `document`. */
  static void start(Context c, String volume, Uri document) {
    Intent i = new Intent(c, ConvertService.class);
    if (volume != null) i.putExtra(EXTRA_VOLUME, volume);
    if (document != null) i.putExtra(EXTRA_DOCUMENT, document);
    if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i);
    else c.startService(i);
  }

  static void cancel() {
    if (Job.handle != 0 && Job.state == Job.CONVERTING) PackerNative.cancel(Job.handle);
  }

  @Override
  public IBinder onBind(Intent intent) {
    return null;
  }

  @Override
  public void onCreate() {
    super.onCreate();
    notes = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
    if (Build.VERSION.SDK_INT >= 26)
      notes.createNotificationChannel(new NotificationChannel(CHANNEL, getString(R.string.packer_channel),
          NotificationManager.IMPORTANCE_LOW));
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    if (intent != null && ACTION_CANCEL.equals(intent.getAction())) {
      cancel();
      return START_NOT_STICKY;
    }
    Notification n = progressNote(0, 0);
    if (Build.VERSION.SDK_INT >= 29) startForeground(NOTE, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
    else startForeground(NOTE, n);
    if (running || intent == null || Job.handle == 0 || Job.state != Job.READY) {
      if (!running) stopSelf();
      return START_NOT_STICKY;
    }
    running = true;
    final String volume = intent.getStringExtra(EXTRA_VOLUME);
    final Uri document = intent.getParcelableExtra(EXTRA_DOCUMENT);
    Job.state = Job.CONVERTING;
    Job.done = 0;
    Job.total = Job.summary != null ? Job.summary.images : 0;
    Job.written = 0;
    Job.startedAt = SystemClock.elapsedRealtime();
    Job.failure = null;
    Job.cancelled = false;
    Job.result = null;
    Job.report = null;
    Job.reportName = null;
    Job.changed();
    PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
    wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "CubeEvil4TexturePacker:convert");
    wake.acquire(12 * 60 * 60 * 1000L);
    new Thread(() -> convert(volume, document), "convert").start();
    return START_NOT_STICKY;
  }

  /** Android 15 ends a data-sync service after 6 hours a day: the conversion stops as if cancelled. */
  @Override
  public void onTimeout(int startId, int type) {
    Log.w(TAG, "the system ends the conversion (time limit)");
    cancel();
  }

  private void convert(String volume, Uri document) {
    final int size = Job.size;
    final String name = "CubeEvil4-textures-" + size + ".c4tex";
    Output out = null;
    String failure;
    try {
      out = document != null ? Output.document(this, document) : Output.download(this, volume, name);
      long need = Job.estimates != null ? Job.estimates[indexOf(size)] : 0;
      long free = out.free();
      if (free >= 0 && need > 0 && !Estimates.fits(need, free)) {
        failure = getString(R.string.packer_no_space, MainActivity.bytes(this, need + Estimates.RESERVE),
            MainActivity.bytes(this, free));
      } else {
        String date = new SimpleDateFormat("yyyy-MM-dd", Locale.ROOT).format(new Date());
        failure = PackerNative.convert(Job.handle, size, Job.choices, out.file.getFd(), date, name, memoryBudget(),
            Runtime.getRuntime().availableProcessors(), (done, total, written) -> {
              Job.done = done;
              Job.total = total;
              Job.written = written;
              long now = SystemClock.elapsedRealtime();
              if (now - lastNote >= 1000 || done == total) {
                lastNote = now;
                notes.notify(NOTE, progressNote(done, total));
                Job.changed();
              }
            });
      }
      if (failure.isEmpty()) {
        Job.resultBytes = out.file.getStatSize();
        Job.resultName = out.finish(this);
        Job.result = out.uri;
        out = null;
        Job.report = PackerNative.report(Job.handle);
        if (volume != null)
          Job.reportName = Output.report(this, volume, Job.resultName.replaceAll("\\.c4tex$", "") + ".txt", Job.report);
      }
    } catch (Exception e) {
      Log.w(TAG, "convert", e);
      failure = e.getMessage() != null ? e.getMessage() : e.toString();
    }
    if (out != null) out.discard(this);
    final String problem = failure;
    Job.seconds = (SystemClock.elapsedRealtime() - Job.startedAt) / 1000;
    Log.i(TAG, "size " + size + ": " + (problem.isEmpty() ? "done, " + Job.resultBytes + " bytes" : problem) + " in "
        + Job.seconds + " s (estimate " + Estimates.seconds(Job.summary != null ? Job.summary.bytes : 0,
        Job.estimates != null ? Job.estimates[indexOf(size)] : 0, Estimates.deviceUnits()) + " s)");
    android.os.Handler main = new android.os.Handler(getMainLooper());
    main.post(() -> {
      Job.cancelled = problem.equals("cancelled");
      Job.failure = problem.isEmpty() || Job.cancelled ? null : problem;
      Job.state = problem.isEmpty() ? Job.DONE : Job.cancelled ? Job.READY : Job.FAILED;
      running = false;
      if (wake != null && wake.isHeld()) wake.release();
      stopForeground(true);
      if (!Job.cancelled) notes.notify(NOTE_DONE, doneNote(problem.isEmpty()));
      Job.changed();
      stopSelf();
    });
  }

  static int indexOf(int size) {
    for (int i = 0; i < Estimates.SIZES.length; i++)
      if (Estimates.SIZES[i] == size) return i;
    return 1;
  }

  /** The images being converted at once may take a quarter of the device's memory (at most 1 GB, 512 MB on 32 bit). */
  private long memoryBudget() {
    ActivityManager.MemoryInfo m = new ActivityManager.MemoryInfo();
    ((ActivityManager) getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(m);
    long quarter = m.totalMem > 0 ? m.totalMem / 4 : 512L << 20;
    long cap = android.os.Process.is64Bit() ? 1L << 30 : 512L << 20;
    return Math.max(256L << 20, Math.min(quarter, cap));
  }

  private PendingIntent openApp() {
    Intent i = new Intent(this, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
    return PendingIntent.getActivity(this, 0, i, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
  }

  private Notification.Builder builder() {
    Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL)
        : new Notification.Builder(this);
    return b.setSmallIcon(R.drawable.ic_note).setContentIntent(openApp());
  }

  private Notification progressNote(int done, int total) {
    Intent cancel = new Intent(this, ConvertService.class).setAction(ACTION_CANCEL);
    PendingIntent pc = PendingIntent.getService(this, 1, cancel,
        PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
    return builder().setContentTitle(getString(R.string.packer_note_title))
        .setContentText(total > 0 ? getString(R.string.packer_note_progress, MainActivity.count(done),
            MainActivity.count(total)) : getString(R.string.packer_reading))
        .setProgress(Math.max(total, 1), done, total == 0)
        .setOngoing(true)
        .setOnlyAlertOnce(true)
        .addAction(new Notification.Action.Builder(null, getString(R.string.packer_cancel), pc).build())
        .build();
  }

  private Notification doneNote(boolean ok) {
    return builder().setContentTitle(getString(ok ? R.string.packer_note_done : R.string.packer_note_failed))
        .setContentText(ok ? Job.resultName : Job.failure)
        .setAutoCancel(true)
        .build();
  }
}
