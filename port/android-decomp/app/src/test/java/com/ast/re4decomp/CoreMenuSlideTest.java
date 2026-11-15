package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.animation.ValueAnimator;
import android.app.Application;
import android.content.res.TypedArray;
import android.view.animation.Animation;
import android.view.animation.AnimationUtils;
import android.view.animation.TranslateAnimation;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

/**
 * The in-game menu slides up from the bottom and back down (0.99.17, wish of the project owner): about 200-250 ms,
 * none with Android's animations off.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 33, application = Application.class)
public class CoreMenuSlideTest {
  @After public void animationsBackOn() throws Exception {
    durationScale(1f);
  }

  /** ValueAnimator.setDurationScale (hidden; Android's developer setting "Animator duration scale"). */
  private static void durationScale(float scale) throws Exception {
    ValueAnimator.class.getMethod("setDurationScale", float.class).invoke(null, scale);
  }

  @Test public void upFromTheBottomAndBackDown() {
    Application app = (Application) org.robolectric.RuntimeEnvironment.getApplication();
    Animation in = AnimationUtils.loadAnimation(app, R.anim.menu_slide_in);
    Animation out = AnimationUtils.loadAnimation(app, R.anim.menu_slide_out);
    assertTrue(in instanceof TranslateAnimation && out instanceof TranslateAnimation);
    assertTrue("short: " + in.getDuration(), in.getDuration() >= 200 && in.getDuration() <= 250);
    assertTrue("short: " + out.getDuration(), out.getDuration() >= 200 && out.getDuration() <= 250);
    // from (to) one screen height below its place: the window starts (ends) below the screen
    assertEquals(1080f, shift(in, 0), 0.5f);
    assertEquals(0f, shift(in, in.getDuration()), 0.5f);
    assertEquals(0f, shift(out, 0), 0.5f);
    assertEquals(1080f, shift(out, out.getDuration()), 0.5f);
  }

  /** The window's shift down (pixels) at `time` ms of the animation, for a window in a 1920 x 1080 screen. */
  private static float shift(Animation a, long time) {
    a.reset();
    a.initialize(1000, 600, 1920, 1080);
    a.setStartTime(0);
    android.view.animation.Transformation t = new android.view.animation.Transformation();
    a.getTransformation(time, t);
    float[] v = new float[9];
    t.getMatrix().getValues(v);
    return v[android.graphics.Matrix.MTRANS_Y];
  }

  @Test public void theStylesOfTheWindow() {
    Application app = (Application) org.robolectric.RuntimeEnvironment.getApplication();
    assertEquals(R.style.CoreMenuSlide, CoreMenu.windowAnimations(true));
    assertEquals(R.style.CoreMenuNoAnimation, CoreMenu.windowAnimations(false));
    TypedArray slide = app.obtainStyledAttributes(R.style.CoreMenuSlide,
        new int[] {android.R.attr.windowEnterAnimation, android.R.attr.windowExitAnimation});
    assertEquals(R.anim.menu_slide_in, slide.getResourceId(0, 0));
    assertEquals(R.anim.menu_slide_out, slide.getResourceId(1, 0));
    slide.recycle();
    TypedArray none = app.obtainStyledAttributes(R.style.CoreMenuNoAnimation,
        new int[] {android.R.attr.windowEnterAnimation, android.R.attr.windowExitAnimation});
    assertEquals(0, none.getResourceId(0, 0));
    assertEquals(0, none.getResourceId(1, 0));
    none.recycle();
  }

  @Test public void androidsAnimationsOff() throws Exception {
    Application app = (Application) org.robolectric.RuntimeEnvironment.getApplication();
    assertTrue(CoreMenu.animationsOn(app));
    durationScale(0f);
    assertFalse("animator duration scale 0", CoreMenu.animationsOn(app));
  }

}
