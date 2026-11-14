package com.ast.re4decomp;

import android.animation.Animator;
import android.animation.AnimatorSet;
import java.lang.reflect.Field;
import java.util.List;
import org.robolectric.shadows.ShadowLooper;

/**
 * The main looper's work done in a test, bounded (0.99.10). Robolectric's idle() runs until no task is due; with an
 * animation running, each Choreographer frame moves its clock on by a frame, the next frame is due at once, and an
 * animation that never ends kept idle() busy for good: full test runs hung in CoreMenuLayoutTest three times
 * (11.11.2026, jstack: Choreographer.doFrame -> AnimationHandler -> AnimatorSet). Here at most MAX_TASKS tasks; is the
 * looper still busy then, the animations still running are named on stderr (the test report keeps them) and stopped.
 */
final class TestLooper {
  static final int MAX_TASKS = 3000;

  private TestLooper() {}

  /** What idle() did, at most MAX_TASKS tasks. */
  static void idle() {
    ShadowLooper looper = ShadowLooper.shadowMainLooper();
    int n = 0;
    while (!looper.isIdle() && n < MAX_TASKS) {
      looper.runOneTask();
      n++;
    }
    if (!looper.isIdle()) stopAnimations("the main looper still busy after " + MAX_TASKS + " tasks");
  }

  /** The animations the main thread's animation handler runs: named on stderr and stopped. */
  static void stopAnimations(String why) {
    try {
      Class<?> c = Class.forName("android.animation.AnimationHandler");
      Object handler = c.getMethod("getInstance").invoke(null);
      Field f = c.getDeclaredField("mAnimationCallbacks");
      f.setAccessible(true);
      List<?> callbacks = (List<?>) f.get(handler);
      for (Object o : callbacks) if (o != null) System.err.println("TestLooper: " + why + ": " + describe(o));
      callbacks.clear();
    } catch (ReflectiveOperationException e) {
      System.err.println("TestLooper: " + why + ": the animations not reachable: " + e);
    }
  }

  private static String describe(Object o) {
    StringBuilder b = new StringBuilder(o.toString());
    if (o instanceof AnimatorSet) {
      b.append(" [");
      for (Animator a : ((AnimatorSet) o).getChildAnimations()) b.append(' ').append(describe(a));
      b.append(" ]");
    }
    if (o instanceof Animator) {
      Animator a = (Animator) o;
      b.append(" (duration ").append(a.getDuration()).append(", running ").append(a.isRunning()).append(')');
    }
    return b.toString();
  }
}
