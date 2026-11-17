package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import com.ast.retrotouch.RetroTouchAction;
import com.ast.retrotouch.RetroTouchButtonShape;
import com.ast.retrotouch.RetroTouchButtonStyle;
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchIcon;
import com.ast.retrotouch.RetroTouchView;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * The touch controls' four looks (TouchControls, TouchStyle; 17.11.2026): RE, GC, PS2 and XBOX, all in RetroTouch's
 * ICONS style; the look is the app's own setting, a player of the old GC look (RetroTouch's LABELS) gets the new GC,
 * the editor's style button goes round; the default layout keeps its positions and sizes.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class TouchStyleTest {
  private Context context;

  @Before public void clean() {
    context = RuntimeEnvironment.getApplication();
    context.getSharedPreferences(TouchLayoutFile.PREFS, Context.MODE_PRIVATE).edit().clear().commit();
    input().edit().clear().commit();
  }

  private android.content.SharedPreferences input() {
    return context.getSharedPreferences(CoreActivity.INPUT_PREFS, Context.MODE_PRIVATE);
  }

  private TouchControls controls() {
    return new TouchControls(context, new CoreInput(), TouchShowRule.FORCE, new TouchControls.Host() {
      @Override public void select(boolean down, boolean finger) {}
      @Override public void editing(boolean on) {}
      @Override public void send() {}
      @Override public void r3(boolean down) {}
      @Override public void zoom(int direction, boolean down) {}
    });
  }

  private static RetroTouchAction action(RetroTouchView view, String id) {
    for (RetroTouchAction a : view.getRegisteredActions()) if (a.getId().equals(id)) return a;
    throw new AssertionError("no action " + id);
  }

  /** The editor's style button (RetroTouchView.toggleButtonStyle, as a tap on it). */
  private static void tapStyleButton(RetroTouchView view) throws Exception {
    java.lang.reflect.Method m = RetroTouchView.class.getDeclaredMethod("toggleButtonStyle");
    m.setAccessible(true);
    m.invoke(view);
  }

  @Test public void newPlayerGetsRe() {
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(TouchStyle.RE, t.look());
    assertEquals(RetroTouchButtonStyle.ICONS, view.getButtonStyle());
    assertEquals(2, view.getAvailableButtonStyles().size());
    assertEquals(TouchStyle.RE, input().getInt(TouchControls.LOOK, -1));
  }

  @Test public void oldGcPlayerGetsTheNewGc() {
    // RetroTouch's LABELS: the GC look of 0.99.6 .. 1.0
    context.getSharedPreferences(TouchLayoutFile.PREFS, Context.MODE_PRIVATE).edit()
        .putString("button_style_" + TouchControls.LAYOUT_ID, "LABELS").commit();
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(TouchStyle.GC, t.look());
    assertEquals("RetroTouch draws every look in ICONS", RetroTouchButtonStyle.ICONS, view.getButtonStyle());
    assertEquals(TouchStyle.GC, input().getInt(TouchControls.LOOK, -1));
    // the next start keeps it, though RetroTouch says ICONS now
    assertEquals(TouchStyle.GC, controls().look());
  }

  @Test public void lookKeptAndReadAgainAfterAnImport() {
    TouchControls t = controls();
    t.setLook(TouchStyle.XBOX);
    assertEquals(TouchStyle.XBOX, controls().look());
    input().edit().putInt(TouchControls.LOOK, TouchStyle.PS2).commit();  // a settings import
    t.reloadSaved();
    assertEquals(TouchStyle.PS2, t.look());
    assertEquals("SELECT", action((RetroTouchView) t.view(), CoreTouch.MENU).getLabel());
    input().edit().putInt(TouchControls.LOOK, 17).commit();  // nonsense: the default
    t.reloadSaved();
    assertEquals(TouchStyle.DEFAULT_LOOK, t.look());
  }

  @Test public void editorStyleButtonGoesRound() throws Exception {
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    int[] order = {TouchStyle.GC, TouchStyle.PS2, TouchStyle.XBOX, TouchStyle.RE};
    for (int want : order) {
      tapStyleButton(view);
      assertEquals(want, t.look());
      assertEquals(RetroTouchButtonStyle.ICONS, view.getButtonStyle());
      assertEquals(want, input().getInt(TouchControls.LOOK, -1));
    }
  }

  @Test public void reAsBefore() {
    TouchControls t = controls();
    t.setLook(TouchStyle.PS2);
    t.setLook(TouchStyle.RE);
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
    for (int i = 0; i < CoreTouch.ACTIONS.length; i++) {
      String id = CoreTouch.ACTIONS[i];
      assertNull(id, action(view, id).getColors());
      assertEquals(id, CoreTouch.LABELS[i], action(view, id).getLabel());
    }
    assertTrue(view.isStickAction(CoreTouch.C_STICK));
    // the aim and knife sticks with R's and L's icons (RetroTouch 1.0.0-beta.12), the C stick keeps its label
    assertTrue(view.isStickAction(CoreTouch.AIM_STICK) && view.isStickAction(CoreTouch.KNIFE_STICK));
    assertSame(RetroTouchIcon.CROSSHAIR, action(view, CoreTouch.AIM_STICK).getIcon());
    assertSame(RetroTouchIcon.KNIFE_HORIZONTAL, action(view, CoreTouch.KNIFE_STICK).getIcon());
    assertNull(action(view, CoreTouch.C_STICK).getIcon());
    assertEquals("AIM", action(view, CoreTouch.AIM_STICK).getLabel());
    assertEquals(1f, view.getButtonAspect(CoreTouch.Z), 0f);
    // four sticks in one layout: the move stick, the C stick, the aim and the knife stick
    assertEquals(CoreTouch.MAX_STICKS, view.getMaxSticks());
    assertEquals(4, CoreTouch.MAX_STICKS);
  }

  @Test public void gcLook() {
    TouchControls t = controls();
    t.setLook(TouchStyle.GC);
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(TouchStyle.GC_DARK, view.getDefaultColors());
    assertEquals(TouchStyle.GC_GREEN, action(view, CoreTouch.A).getColors());
    assertEquals(TouchStyle.GC_RED, action(view, CoreTouch.B).getColors());
    assertEquals(TouchStyle.GC_GREY, action(view, CoreTouch.X).getColors());
    assertEquals(TouchStyle.GC_PURPLE, action(view, CoreTouch.Z).getColors());
    assertEquals(TouchStyle.GC_YELLOW, action(view, CoreTouch.C_STICK).getColors());
    assertEquals("A", action(view, CoreTouch.A).getLabel());
    assertNull(action(view, CoreTouch.A).getIcon());
    assertSame(RetroTouchIcon.MENU, action(view, CoreTouch.MENU).getIcon());
    assertSame(TouchStyle.PLUS, action(view, CoreTouch.ZOOM_IN).getIcon());
    assertEquals("C", action(view, CoreTouch.C_STICK).getLabel());
    assertEquals(RetroTouchButtonShape.ROUND, view.getButtonShape(CoreTouch.A));
    assertEquals(RetroTouchButtonShape.ROUNDED_RECT, view.getButtonShape(CoreTouch.Z));
    assertEquals(TouchStyle.Z_ASPECT, view.getButtonAspect(CoreTouch.Z), 0f);
    assertTrue(view.isStickAction(CoreTouch.C_STICK) && view.isStickAction(CoreTouch.AIM_STICK));
  }

  @Test public void ps2Look() {
    TouchControls t = controls();
    t.setLook(TouchStyle.PS2);
    RetroTouchView view = (RetroTouchView) t.view();
    assertSame(TouchStyle.PS2_CROSS, action(view, CoreTouch.A).getIcon());
    assertSame(TouchStyle.PS2_CIRCLE, action(view, CoreTouch.B).getIcon());
    assertSame(TouchStyle.PS2_SQUARE, action(view, CoreTouch.X).getIcon());
    assertSame(TouchStyle.PS2_TRIANGLE, action(view, CoreTouch.Y).getIcon());
    assertEquals(TouchStyle.PS2_BLUE, action(view, CoreTouch.A).getColors());
    assertEquals("R1", action(view, CoreTouch.Z).getLabel());
    assertEquals("L2", action(view, CoreTouch.L).getLabel());
    assertEquals("R2", action(view, CoreTouch.R).getLabel());
    assertEquals("SELECT", action(view, CoreTouch.MENU).getLabel());
    assertEquals("START", action(view, CoreTouch.START).getLabel());
    assertEquals("R", action(view, CoreTouch.C_STICK).getLabel());
    assertEquals(TouchStyle.PS2_DARK, view.getDefaultColors());
  }

  @Test public void xboxLook() {
    TouchControls t = controls();
    t.setLook(TouchStyle.XBOX);
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(TouchStyle.XBOX_GREEN, action(view, CoreTouch.A).getColors());
    assertEquals(TouchStyle.XBOX_YELLOW, action(view, CoreTouch.Y).getColors());
    assertEquals(TouchStyle.XBOX_WHITE, action(view, CoreTouch.L).getColors());
    assertEquals("LT", action(view, CoreTouch.L).getLabel());
    assertEquals("RT", action(view, CoreTouch.R).getLabel());
    assertEquals("RB", action(view, CoreTouch.Z).getLabel());
    assertEquals("RS", action(view, CoreTouch.C_STICK).getLabel());
    assertSame(RetroTouchIcon.MENU, action(view, CoreTouch.START).getIcon());
    assertSame(TouchStyle.XBOX_VIEW, action(view, CoreTouch.MENU).getIcon());
    assertNull(action(view, CoreTouch.A).getIcon());
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

  /** Added buttons (RetroTouch names them button_N, 1:1 of their own) are as wide as the default ones in every look. */
  @Test public void addedButtonsShapedAsTheDefaultOnes() throws Exception {
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    RetroTouchControl added = RetroTouchControl.button("button_1", CoreTouch.L, "L", 0.3f, 0.3f, 0.12f);
    RetroTouchControl start = RetroTouchControl.button("button_2", CoreTouch.START, "START", 0.5f, 0.3f, 0.085f);
    RetroTouchControl a = RetroTouchControl.button("button_3", CoreTouch.A, "A", 0.6f, 0.3f, 0.12f);
    RetroTouchControl z = RetroTouchControl.button("button_4", CoreTouch.Z, "Z", 0.7f, 0.3f, 0.09f);
    for (int look = 0; look < TouchStyle.LOOKS; look++) {
      t.setLook(look);
      String name = TouchStyle.NAMES[look];
      assertEquals(name, TouchStyle.SHOULDER_ASPECT, drawnAspect(view, added), 0f);
      assertEquals(name, drawnAspect(view, defaultButton(view, CoreTouch.L)), drawnAspect(view, added), 0f);
      assertEquals(name, TouchStyle.CAPSULE_ASPECT, drawnAspect(view, start), 0f);
      assertEquals(name, 1f, drawnAspect(view, a), 0f);
      float zWant = look == TouchStyle.RE ? 1f : TouchStyle.Z_ASPECT;
      assertEquals(name, zWant, drawnAspect(view, z), 0f);
      assertEquals(name + ": the default Z as wide as an added one", zWant, drawnAspect(view, defaultButton(view, CoreTouch.Z)), 0f);
      assertEquals(name, RetroTouchButtonShape.ROUNDED_RECT, view.getButtonShape(CoreTouch.L));
    }
  }

  @Test public void ownIconsParse() {
    for (RetroTouchIcon icon : new RetroTouchIcon[] {TouchStyle.PLUS, TouchStyle.MINUS, TouchStyle.PS2_CROSS,
        TouchStyle.PS2_CIRCLE, TouchStyle.PS2_SQUARE, TouchStyle.PS2_TRIANGLE, TouchStyle.XBOX_VIEW}) {
      assertEquals(24f, icon.getViewBoxSize(), 0f);
    }
  }

  @Test public void lookNumbers() {
    assertEquals(TouchStyle.GC, TouchStyle.next(TouchStyle.RE));
    assertEquals(TouchStyle.RE, TouchStyle.next(TouchStyle.XBOX));
    assertEquals(TouchStyle.DEFAULT_LOOK, TouchStyle.valid(-1));
    assertEquals(TouchStyle.DEFAULT_LOOK, TouchStyle.valid(4));
    assertEquals(TouchStyle.LOOKS, TouchStyle.NAMES.length);
  }

  @Test public void shouldersNarrowerOnlyWhereTheGearNeedsTheRoom() {
    assertEquals(1.95f, TouchStyle.shoulderAspect(1920, 1080, 2f), 0f);    // 16:9
    assertEquals(1.95f, TouchStyle.shoulderAspect(2400, 1080, 2.75f), 0f); // 20:9
    float narrow = TouchStyle.shoulderAspect(1440, 1080, 2f);              // 4:3
    assertTrue(narrow < 1.95f && narrow >= 1.2f);
    assertEquals(1.95f, TouchStyle.shoulderAspect(0, 0, 2f), 0f);          // not laid out yet
  }
}
