package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import com.ast.retrotouch.RetroTouchAction;
import com.ast.retrotouch.RetroTouchButtonShape;
import com.ast.retrotouch.RetroTouchButtonStyle;
import com.ast.retrotouch.RetroTouchColors;
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchIcon;
import com.ast.retrotouch.RetroTouchView;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * The touch controls' two button styles (TouchControls, TouchStyle; RetroTouch 1.0.0-beta.10): LABELS draws as
 * before (the GameCube's colours, RetroTouch's shapes by the labels, no default colours), ICONS has the icons,
 * shapes and colours of TouchStyle; the default layout keeps its positions and sizes.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class TouchStyleTest {
  private Context context;

  @Before public void clean() {
    context = RuntimeEnvironment.getApplication();
    context.getSharedPreferences(TouchLayoutFile.PREFS, Context.MODE_PRIVATE).edit().clear().commit();
  }

  private TouchControls controls() {
    return new TouchControls(context, new CoreInput(), TouchShowRule.FORCE, new TouchControls.Host() {
      @Override public void select(boolean down, boolean finger) {}
      @Override public void editing(boolean on) {}
      @Override public void send() {}
      @Override public void r3(boolean down) {}
    });
  }

  private static RetroTouchAction action(RetroTouchView view, String id) {
    for (RetroTouchAction a : view.getRegisteredActions()) if (a.getId().equals(id)) return a;
    throw new AssertionError("no action " + id);
  }

  @Test public void newPlayerGetsIcons() {
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(TouchStyle.DEFAULT_BUTTON_STYLE, view.getButtonStyle());
    assertTrue(t.icons());
    assertEquals(2, view.getAvailableButtonStyles().size());
  }

  @Test public void labelsAsBefore() {
    TouchControls t = controls();
    t.setIcons(false);
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(RetroTouchButtonStyle.LABELS, view.getButtonStyle());
    assertNull(view.getDefaultColors());
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String id = CoreTouch.ACTIONS[i];
      assertEquals(id, RetroTouchButtonShape.AUTO, view.getButtonShape(id));
      RetroTouchAction a = action(view, id);
      assertEquals(id, CoreTouch.LABELS[i], a.getLabel());
      assertEquals(id, RetroTouchColors.of(CoreTouch.COLOURS[i]).withBorderColor(CoreTouch.BORDER), a.getColors());
    }
    assertTrue(view.isStickAction(CoreTouch.C_STICK));
  }

  @Test public void iconsShapesAndColours() {
    TouchControls t = controls();
    t.setIcons(false);
    t.setIcons(true);
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(TouchStyle.COLOURS, view.getDefaultColors());
    assertEquals(RetroTouchButtonShape.ROUNDED_RECT, view.getButtonShape(CoreTouch.L));
    assertEquals(RetroTouchButtonShape.ROUNDED_RECT, view.getButtonShape(CoreTouch.R));
    assertEquals(RetroTouchButtonShape.CAPSULE, view.getButtonShape(CoreTouch.START));
    assertEquals(RetroTouchButtonShape.CAPSULE, view.getButtonShape(CoreTouch.MENU));
    assertEquals(RetroTouchButtonShape.AUTO, view.getButtonShape(CoreTouch.A));
    assertSame(RetroTouchIcon.HAND, action(view, CoreTouch.A).getIcon());
    assertSame(RetroTouchIcon.KNIFE_HORIZONTAL, action(view, CoreTouch.L).getIcon());
    assertSame(RetroTouchIcon.CROSSHAIR, action(view, CoreTouch.R).getIcon());
    assertNull(action(view, CoreTouch.R3).getIcon());
    for (String id : CoreTouch.ACTIONS) assertNull(id, action(view, id).getColors());
    assertTrue(view.isStickAction(CoreTouch.C_STICK));
  }

  @Test public void layoutKeepsPositionsAndSizes() {
    RetroTouchView view = (RetroTouchView) controls().view();
    for (CoreTouch.Control c : CoreTouch.LAYOUT) {
      RetroTouchControl r = null;
      for (RetroTouchControl k : view.getCurrentLayout().getControls()) if (k.getId().equals(c.id)) r = k;
      assertNotNull(c.id, r);
      assertEquals(c.id, c.x, r.getX(), 0f);
      assertEquals(c.id, c.y, r.getY(), 0f);
      assertEquals(c.id, c.width, r.getWidth(), 0f);
      assertEquals(c.id, c.height, r.getHeight(), 0f);
      assertEquals(c.id, RetroTouchButtonShape.AUTO, r.getShape());
    }
  }

  /** RetroTouch's aspect of a control as it draws it (RetroTouchView.aspectFor, 1.0.0-beta.10). */
  private static float drawnAspect(RetroTouchView view, RetroTouchControl control) throws Exception {
    java.lang.reflect.Method m = RetroTouchView.class.getDeclaredMethod("aspectFor", RetroTouchControl.class);
    m.setAccessible(true);
    return (Float) m.invoke(view, control);
  }

  private static RetroTouchControl defaultButton(RetroTouchView view, String action) {
    for (RetroTouchControl k : view.getCurrentLayout().getControls()) if (action.equals(k.getActionId())) return k;
    throw new AssertionError("no button " + action);
  }

  /** An L added with ADD in the editor (RetroTouch names it button_N, 1:1 of its own) is as wide as the default L in RE. */
  @Test public void addedButtonsShapedAsTheDefaultOnes() throws Exception {
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    t.setIcons(true);
    RetroTouchControl added = RetroTouchControl.button("button_1", CoreTouch.L, "L", 0.3f, 0.3f, 0.12f);
    RetroTouchControl start = RetroTouchControl.button("button_2", CoreTouch.START, "START", 0.5f, 0.3f, 0.085f);
    RetroTouchControl a = RetroTouchControl.button("button_3", CoreTouch.A, "A", 0.6f, 0.3f, 0.12f);
    assertEquals(TouchStyle.SHOULDER_ASPECT, drawnAspect(view, added), 0f);
    assertEquals(drawnAspect(view, defaultButton(view, CoreTouch.L)), drawnAspect(view, added), 0f);
    assertEquals(TouchStyle.CAPSULE_ASPECT, drawnAspect(view, start), 0f);
    assertEquals(1f, drawnAspect(view, a), 0f);
    assertEquals(RetroTouchButtonShape.ROUNDED_RECT, view.getButtonShape(CoreTouch.L));
    // GC (LABELS): as before, an added L 1:1 and RetroTouch's shape by the label; the default L keeps its own
    t.setIcons(false);
    assertEquals(1f, drawnAspect(view, added), 0f);
    assertEquals(1f, drawnAspect(view, start), 0f);
    for (String id : CoreTouch.ACTIONS) assertEquals(id, 1f, view.getButtonAspect(id), 0f);
    assertEquals(TouchStyle.SHOULDER_ASPECT, drawnAspect(view, defaultButton(view, CoreTouch.L)), 0f);
    t.setIcons(true);
    assertEquals(TouchStyle.SHOULDER_ASPECT, drawnAspect(view, added), 0f);
  }

  @Test public void shouldersNarrowerOnlyWhereTheGearNeedsTheRoom() {
    assertEquals(1.95f, TouchStyle.shoulderAspect(1920, 1080, 2f), 0f);    // 16:9
    assertEquals(1.95f, TouchStyle.shoulderAspect(2400, 1080, 2.75f), 0f); // 20:9
    float narrow = TouchStyle.shoulderAspect(1440, 1080, 2f);              // 4:3
    assertTrue(narrow < 1.95f && narrow >= 1.2f);
    assertEquals(1.95f, TouchStyle.shoulderAspect(0, 0, 2f), 0f);          // not laid out yet
  }
}
