package com.ast.re4decomp;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.Spatializer;
import android.os.Build;
import android.os.Process;
import android.util.Log;

/**
 * The sound of the core mode: a thread of its own plays what the game mixes
 * (32 kHz, CoreBridge.audio) through an AudioTrack; the blocking
 * writes pace it to the device's clock (the core matches its rate to that).
 * CoreActivity starts it when the game is in front and stops it otherwise.
 *
 * Stereo, unless the game plays in Dolby Pro Logic II (its AUDIO SETUP) and
 * Android can spatialize 5.1 for the output in use (its Spatializer, Android
 * 12L and later, e.g. for headphones): then the game's four channels
 * (CoreBridge.audio4) go out as 5.1 and Android renders them as surround
 * (decision of the project owner, 20.10.2026: way 1). Checked about once a
 * second; a change builds the track anew.
 *
 * Of the track's buffer only what the output needs is kept filled (LatencyTuner: small at first,
 * larger after underruns, API 24 and later): on Bluetooth Android asks for a large buffer, which
 * kept full delayed the sound by about 280 ms on top of the headphones' own delay.
 */
final class CoreAudio {
  private static final String TAG = "RE4Audio";
  private static final int RATE = 32000;
  private static final int FRAMES = 256;  // 8 ms per write
  private static final int CHECK_WRITES = 125;  // about once a second
  private final Context context;
  private Thread worker;
  private volatile boolean running;
  private String lastState = "";
  // the latency tuning of the current track (API 24 and later); what it grew to is kept per
  // output (the track's capacity) in the preferences: the next track there, also after the
  // next start of the app, begins with it
  private static final String PREFS = "core_audio";
  private LatencyTuner tuner;

  CoreAudio(Context context) {
    this.context = context;
  }

  synchronized void start() {
    if (worker != null) return;
    running = true;
    worker = new Thread(this::run, "RE4-Core-Audio");
    worker.start();
  }

  synchronized void stop() {
    running = false;
    if (worker == null) return;
    worker.interrupt();
    try {
      worker.join(300);
    } catch (InterruptedException interrupted) {
      Thread.currentThread().interrupt();
    }
    worker = null;
  }

  private static AudioAttributes attributes() {
    return new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build();
  }

  private static AudioFormat format(int mask) {
    return new AudioFormat.Builder().setSampleRate(RATE).setChannelMask(mask)
        .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build();
  }

  /** The game plays in Dolby Pro Logic II and Android spatializes 5.1 for the output in use. */
  private boolean surroundWanted() {
    final boolean game = CoreBridge.audioSurround();
    String state;
    boolean spatial = false;
    if (Build.VERSION.SDK_INT >= 32) {
      state = Spatial.state(context);
      spatial = state.endsWith("5.1 spatialized: yes");
    } else {
      state = "spatializer: none before Android 12L";
    }
    state = "sound: game " + (game ? "in Dolby Pro Logic II" : "not in Dolby Pro Logic II") + "; " + state;
    if (!state.equals(lastState)) {
      lastState = state;
      Log.i(TAG, state);
    }
    return game && spatial;
  }

  /** Android's Spatializer (API 32): its level and whether it takes our 5.1 for the output now. */
  @android.annotation.TargetApi(32)
  private static final class Spatial {
    static String state(Context context) {
      final AudioManager manager = context.getSystemService(AudioManager.class);
      final Spatializer s = manager == null ? null : manager.getSpatializer();
      if (s == null) return "spatializer: none";
      final int level = s.getImmersiveAudioLevel();
      if (level == Spatializer.SPATIALIZER_IMMERSIVE_LEVEL_NONE) return "spatializer: not on this device";
      final boolean can = s.isAvailable() && s.isEnabled()
          && s.canBeSpatialized(attributes(), format(AudioFormat.CHANNEL_OUT_5POINT1));
      return "spatializer level " + level + ", available " + s.isAvailable() + ", enabled " + s.isEnabled()
          + ", 5.1 spatialized: " + (can ? "yes" : "no");
    }
  }

  private AudioTrack track(boolean surround) {
    final int mask = surround ? AudioFormat.CHANNEL_OUT_5POINT1 : AudioFormat.CHANNEL_OUT_STEREO;
    final int frameBytes = surround ? 12 : 4;
    final int minimum = AudioTrack.getMinBufferSize(RATE, mask, AudioFormat.ENCODING_PCM_16BIT);
    if (minimum <= 0) throw new IllegalStateException("no PCM output at 32 kHz");
    final int bufferBytes = Math.max(minimum, FRAMES * frameBytes * 3);
    final AudioTrack track = new AudioTrack(attributes(), format(mask), bufferBytes, AudioTrack.MODE_STREAM,
        AudioManager.AUDIO_SESSION_ID_GENERATE);
    if (track.getState() != AudioTrack.STATE_INITIALIZED) {
      track.release();
      throw new IllegalStateException("AudioTrack did not start");
    }
    // only as much of the buffer as the output needs (LatencyTuner): Bluetooth asks for a large one
    String used = "all of it";
    tuner = null;
    if (Build.VERSION.SDK_INT >= 24) {
      final AudioManager manager = context.getSystemService(AudioManager.class);
      final int burst = LatencyTuner.burst(
          manager == null ? null : manager.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER),
          manager == null ? null : manager.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE), RATE);
      final int capacity = track.getBufferCapacityInFrames();
      final int learned = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getInt("frames" + capacity, 0);
      tuner = new LatencyTuner(capacity, burst, FRAMES, learned);
      final int set = track.setBufferSizeInFrames(tuner.size());
      used = (set > 0 ? set : tuner.size()) + " frames (" + (set > 0 ? set : tuner.size()) * 1000 / RATE
          + " ms), grows on underruns";
    }
    // with the state of the last check: every report has it, also after a resume
    Log.i(TAG, "sound: AudioTrack 32000 Hz " + (surround ? "5.1 (Dolby Pro Logic II, spatialized)" : "stereo")
        + ", buffer " + bufferBytes / frameBytes + " frames (minimum " + minimum / frameBytes + "), used " + used
        + "; " + lastState.replaceFirst("^sound: ", ""));
    return track;
  }

  /** After a write: a larger part of the buffer when the track ran out since the last check. */
  private void tune(AudioTrack track) {
    if (tuner == null || Build.VERSION.SDK_INT < 24) return;
    final int underruns = track.getUnderrunCount();
    if (!tuner.check(underruns)) return;
    final int set = track.setBufferSizeInFrames(tuner.size());
    final int frames = set > 0 ? set : tuner.size();
    context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
        .putInt("frames" + track.getBufferCapacityInFrames(), frames).apply();
    Log.i(TAG, "sound: buffer " + frames + " frames (" + frames * 1000 / RATE + " ms) after " + underruns
        + " underruns");
  }

  private static void release(AudioTrack track) {
    if (track == null) return;
    try {
      track.pause();
      track.flush();
    } catch (IllegalStateException ignored) {
      // the output device went away
    }
    track.release();
  }

  private void run() {
    Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO);
    AudioTrack track = null;
    try {
      final short[] stereo = new short[FRAMES * 2];
      final short[] four = new short[FRAMES * 4];
      final short[] six = new short[FRAMES * 6];
      boolean surround = surroundWanted();
      track = track(surround);
      track.play();
      int writes = 0;
      while (running) {
        if (++writes >= CHECK_WRITES) {
          writes = 0;
          final boolean want = surroundWanted();
          if (want != surround) {
            release(track);
            track = null;
            surround = want;
            track = track(surround);
            track.play();
          }
        }
        final short[] pcm;
        if (surround) {
          CoreBridge.audio4(four, FRAMES);
          SurroundPcm.fivePointOne(four, six, FRAMES);
          pcm = six;
        } else {
          CoreBridge.audio(stereo, FRAMES);
          pcm = stereo;
        }
        int offset = 0;
        while (offset < pcm.length && running) {
          final int written = track.write(pcm, offset, pcm.length - offset);
          if (written < 0) throw new IllegalStateException("AudioTrack write error " + written);
          offset += written;
        }
        tune(track);
      }
    } catch (RuntimeException failure) {
      Log.w(TAG, "sound output ended", failure);
    } finally {
      release(track);
    }
  }
}
