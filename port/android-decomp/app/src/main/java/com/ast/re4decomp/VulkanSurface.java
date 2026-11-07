package com.ast.re4decomp;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/**
 * The game's picture through Vulkan (port/docs/vulkan-bauplan.md, E6): a SurfaceView with a render thread of its own,
 * as GLSurfaceView has one (the same calls: requestRender, setRenderMode, onPause, onResume), but without an EGL
 * context. The thread hands the window to the core (CoreBridge.vkWindow) and draws (CoreBridge.draw); the Vulkan
 * renderer presents to the window itself. A window that goes is taken from the renderer before surfaceDestroyed
 * returns (its swapchain must not outlive it). When the renderer could not start (CoreBridge.rendererProblem), the
 * thread stops drawing and tells the activity (Listener), which then goes over to OpenGL ES.
 */
final class VulkanSurface extends SurfaceView implements GameSurface, SurfaceHolder.Callback {
  private static final String TAG = "RE4Decomp";
  /** How long the UI thread waits for the render thread (a window that goes, a pause): below Android's 5 s. */
  private static final long WAIT_MS = 2500;

  interface Listener {
    /** On the main thread: the Vulkan renderer could not start (why); the surface draws no more. */
    void vulkanFailed(String problem);
  }

  private final Handler main = new Handler(Looper.getMainLooper());
  private final Listener listener;
  private final Object lock = new Object();
  private Thread thread;
  // what the UI thread asked for (under lock)
  private Surface window;
  private boolean windowChanged, sizeChanged;
  private int width, height;
  private boolean paused, exit;
  private boolean continuous = true, requested = true;
  private long asked;  // a window or a size the render thread is to take (its serial)
  // what the render thread did (under lock)
  private long taken;
  private boolean threadPaused, failed, ended;

  VulkanSurface(Context context, Listener listener) {
    super(context);
    this.listener = listener;
    getHolder().addCallback(this);
    thread = new Thread(this::run, "RE4-Vulkan");
    thread.start();
  }

  @Override public void requestRender() {
    synchronized (lock) {
      requested = true;
      lock.notifyAll();
    }
  }

  @Override public void setRenderMode(int mode) {
    synchronized (lock) {
      continuous = mode == GLSurfaceView.RENDERMODE_CONTINUOUSLY;
      lock.notifyAll();
    }
  }

  /** The render thread stands (as GLSurfaceView: onPause returns when it does); the window stays. */
  @Override public void onPause() {
    synchronized (lock) {
      paused = true;
      lock.notifyAll();
      await(() -> threadPaused || ended);
    }
  }

  @Override public void onResume() {
    synchronized (lock) {
      paused = false;
      requested = true;
      lock.notifyAll();
    }
  }

  @Override public void surfaceCreated(SurfaceHolder holder) {
    synchronized (lock) {
      window = holder.getSurface();
      windowChanged = true;
      asked++;
      lock.notifyAll();
    }
  }

  @Override public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
    synchronized (lock) {
      width = w;
      height = h;
      sizeChanged = true;
      requested = true;
      final long serial = ++asked;
      lock.notifyAll();
      await(() -> taken >= serial || ended);
    }
  }

  /** The window goes: the renderer lets go of it (its swapchain) before this returns. */
  @Override public void surfaceDestroyed(SurfaceHolder holder) {
    synchronized (lock) {
      window = null;
      windowChanged = true;
      final long serial = ++asked;
      lock.notifyAll();
      if (!await(() -> taken >= serial || ended))
        Log.w(TAG, "Vulkan surface: the render thread did not let go of the window within " + WAIT_MS + " ms");
    }
  }

  /** The render thread ends (the view goes, or the activity goes over to OpenGL ES); waits for it. */
  void stop() {
    Thread t;
    synchronized (lock) {
      exit = true;
      lock.notifyAll();
      t = thread;
      thread = null;
    }
    if (t == null || t == Thread.currentThread()) return;
    try {
      t.join(WAIT_MS);
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
    if (t.isAlive()) Log.w(TAG, "Vulkan surface: the render thread did not end within " + WAIT_MS + " ms");
  }

  @Override protected void onDetachedFromWindow() {
    stop();
    super.onDetachedFromWindow();
  }

  private interface Condition {
    boolean met();
  }

  /** Waits (lock held) until the condition is met, at most WAIT_MS; false when it was not. */
  private boolean await(Condition c) {
    final long until = System.nanoTime() + WAIT_MS * 1_000_000L;
    while (!c.met()) {
      final long left = (until - System.nanoTime()) / 1_000_000L;
      if (left <= 0) return false;
      try {
        lock.wait(left);
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        return c.met();
      }
    }
    return true;
  }

  private void run() {
    boolean handed = false;  // the renderer holds a window
    try {
      while (true) {
        Surface give = null;
        boolean giveWindow = false, resize = false, draw;
        int w = 0, h = 0;
        long serial;
        synchronized (lock) {
          while (true) {
            if (exit) return;
            if (paused != threadPaused) {
              threadPaused = paused;
              lock.notifyAll();
            }
            if (windowChanged || sizeChanged) break;
            if (!paused && !failed && window != null && width > 0 && height > 0 && (continuous || requested)) break;
            try {
              lock.wait();
            } catch (InterruptedException e) {
              return;
            }
          }
          if (windowChanged) {
            giveWindow = true;
            give = window;
            windowChanged = false;
          }
          if (sizeChanged) {
            resize = true;
            w = width;
            h = height;
            sizeChanged = false;
          }
          serial = asked;
          draw = !paused && !failed && window != null && width > 0 && height > 0 && (continuous || requested);
          if (draw) requested = false;
        }
        if (resize) CoreBridge.resize(w, h);
        if (giveWindow) {
          CoreBridge.vkWindow(give);
          handed = give != null;
        }
        synchronized (lock) {
          taken = Math.max(taken, serial);
          lock.notifyAll();
        }
        if (!draw) continue;
        CoreBridge.draw();
        final String problem = CoreBridge.rendererProblem();
        if (!problem.isEmpty()) {
          synchronized (lock) {
            failed = true;
          }
          Log.w(TAG, "Vulkan renderer: " + problem);
          main.post(() -> listener.vulkanFailed(problem));
        }
      }
    } finally {
      if (handed) CoreBridge.vkWindow(null);
      synchronized (lock) {
        ended = true;
        lock.notifyAll();
      }
    }
  }
}
