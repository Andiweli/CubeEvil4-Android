package com.ast.re4decomp;

import static org.junit.Assert.*;

import android.app.Application;
import android.content.Context;
import com.ast.retrotouch.RetroTouchAction;
import com.ast.retrotouch.RetroTouchButtonShape;
import com.ast.retrotouch.RetroTouchButtonStyle;
import com.ast.retrotouch.RetroTouchControl;
import com.ast.retrotouch.RetroTouchDpadStyle;
import com.ast.retrotouch.RetroTouchIcon;
import com.ast.retrotouch.RetroTouchLook;
import com.ast.retrotouch.RetroTouchSkin;
import com.ast.retrotouch.RetroTouchStickGate;
import com.ast.retrotouch.RetroTouchView;
import java.util.Arrays;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

/**
 * The touch controls' four skins (TouchControls, TouchStyle; RetroTouch 1.0.0-beta.13, 17.11.2026): RE, GC, PS2 and
 * XBOX; RetroTouch keeps the choice, a player of the old GC style (RetroTouch's LABELS) gets GC, the editor's style
 * button goes round, the app menu sets it; every skin sets every action's content; the default layout keeps its
 * positions and sizes.
 */
@RunWith(org.robolectric.RobolectricTestRunner.class)
@Config(sdk = 28, application = Application.class)
public class TouchStyleTest {
  private Context context;

  @Before public void clean() {
    context = RuntimeEnvironment.getApplication();
    prefs().edit().clear().commit();
  }

  private android.content.SharedPreferences prefs() {
    return context.getSharedPreferences(TouchLayoutFile.PREFS, Context.MODE_PRIVATE);
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

  /** The editor's style button (RetroTouchView.cycleSkin, as a tap on it). */
  private static void tapStyleButton(RetroTouchView view) throws Exception {
    java.lang.reflect.Method m = RetroTouchView.class.getDeclaredMethod("cycleSkin");
    m.setAccessible(true);
    m.invoke(view);
  }

  @Test public void newPlayerGetsRe() {
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    assertEquals(Arrays.asList(TouchStyle.IDS), view.getSkins());
    assertEquals("re", view.getSkin());
    assertEquals(TouchStyle.RE, t.look());
    assertEquals(RetroTouchButtonStyle.ICONS, view.getButtonStyle());
    // the actions carry RE's icons and labels (the skins set their own on top)
    assertSame(RetroTouchIcon.HAND, action(view, CoreTouch.A).getIcon());
    assertNull(action(view, CoreTouch.A).getColors());
    assertTrue(view.isStickAction(CoreTouch.C_STICK) && view.isStickAction(CoreTouch.AIM_STICK)
        && view.isStickAction(CoreTouch.KNIFE_STICK));
    assertEquals(CoreTouch.MAX_STICKS, view.getMaxSticks());
  }

  @Test public void oldGcPlayerGetsTheGcSkin() {
    prefs().edit().putString(TouchControls.STYLE_KEY, "LABELS").commit();  // the GC style of 0.99.6 .. 1.0
    TouchControls t = controls();
    assertEquals("gc", ((RetroTouchView) t.view()).getSkin());
    assertEquals(TouchStyle.GC, t.look());
    assertEquals("gc", prefs().getString(TouchControls.SKIN_KEY, null));
    assertEquals(TouchStyle.GC, controls().look());
  }

  @Test public void aChosenSkinWinsOverTheOldStyle() {
    prefs().edit().putString(TouchControls.STYLE_KEY, "LABELS").putString(TouchControls.SKIN_KEY, "xbox").commit();
    assertEquals(TouchStyle.XBOX, controls().look());
    assertFalse(TouchControls.migrateOldGc(prefs()));
  }

  @Test public void appMenuSetsTheSkin() {
    TouchControls t = controls();
    t.setLook(TouchStyle.PS2);
    assertEquals("ps2", ((RetroTouchView) t.view()).getSkin());
    assertEquals(TouchStyle.PS2, controls().look());
    t.setLook(17);  // nonsense: the default
    assertEquals(TouchStyle.RE, t.look());
  }

  @Test public void editorStyleButtonGoesRound() throws Exception {
    TouchControls t = controls();
    RetroTouchView view = (RetroTouchView) t.view();
    for (int want : new int[] {TouchStyle.GC, TouchStyle.PS2, TouchStyle.XBOX, TouchStyle.RE}) {
      tapStyleButton(view);
      assertEquals(want, t.look());
      assertEquals(TouchStyle.IDS[want], prefs().getString(TouchControls.SKIN_KEY, null));
    }
  }

  @Test public void skinReadAgainAfterAnImport() {
    TouchControls t = controls();
    prefs().edit().putString(TouchControls.SKIN_KEY, "gc").commit();  // a settings import
    t.reloadSaved();
    assertEquals(TouchStyle.GC, t.look());
    // a backup of before skins: the old GC style
    prefs().edit().remove(TouchControls.SKIN_KEY).putString(TouchControls.STYLE_KEY, "LABELS").commit();
    t.setLook(TouchStyle.XBOX);
    prefs().edit().remove(TouchControls.SKIN_KEY).commit();
    t.reloadSaved();
    assertEquals(TouchStyle.GC, t.look());
    // neither: RE
    prefs().edit().clear().commit();
    t.reloadSaved();
    assertEquals(TouchStyle.RE, t.look());
  }

  /** Every skin but RE sets every action's content (a label or an icon) and colours; RE keeps the actions' own. */
  @Test public void everySkinSetsEveryAction() {
    RetroTouchSkin[] skins = TouchStyle.skins(TouchStyle.SHOULDER_ASPECT);
    assertEquals(TouchStyle.LOOKS, skins.length);
    for (int i = 0; i < skins.length; i++) {
      assertEquals(TouchStyle.IDS[i], skins[i].getId());
      assertEquals(TouchStyle.NAMES[i], skins[i].getName());
      if (i == TouchStyle.RE) continue;
      for (String id : CoreTouch.ACTIONS) {
        RetroTouchLook look = skins[i].getLook(id);
        assertNotNull(skins[i].getName() + " " + id, look);
        assertTrue(skins[i].getName() + " " + id, look.getLabel() != null || look.getIcon() != null);
        assertNotNull(skins[i].getName() + " " + id, look.getColors());
      }
    }
    assertNull(skins[TouchStyle.RE].getLook(CoreTouch.A));
    assertEquals(TouchStyle.COLOURS, skins[TouchStyle.RE].getDefaultColors());
    assertEquals(RetroTouchButtonShape.ROUNDED_RECT, skins[TouchStyle.RE].getLook(CoreTouch.L).getShape());
    assertEquals(TouchStyle.SHOULDER_ASPECT, skins[TouchStyle.RE].getLook(CoreTouch.L).getAspect(), 0f);
    assertEquals(RetroTouchButtonShape.CAPSULE, skins[TouchStyle.RE].getLook(CoreTouch.START).getShape());
  }

  @Test public void gcSkin() {
    RetroTouchSkin gc = TouchStyle.gc(1.6f);
    assertEquals(RetroTouchButtonStyle.LABELS, gc.getBaseStyle());
    assertEquals(RetroTouchStickGate.OCTAGON, gc.getStickGate());
    assertEquals(TouchStyle.GC_GREEN, gc.getLook(CoreTouch.A).getColors());
    assertEquals(TouchStyle.GC_RED, gc.getLook(CoreTouch.B).getColors());
    assertEquals(RetroTouchButtonShape.KIDNEY, gc.getLook(CoreTouch.X).getShape());
    assertEquals(TouchStyle.X_TURN, gc.getLook(CoreTouch.X).getRotation(), 0f);
    assertEquals(RetroTouchButtonShape.KIDNEY, gc.getLook(CoreTouch.Y).getShape());
    assertEquals(RetroTouchButtonShape.BUMPER, gc.getLook(CoreTouch.L).getShape());
    assertEquals("the screen's shoulder width", 1.6f, gc.getLook(CoreTouch.R).getAspect(), 0f);
    assertEquals(TouchStyle.GC_PURPLE, gc.getLook(CoreTouch.Z).getColors());
    assertEquals(TouchStyle.GC_YELLOW, gc.getLook(CoreTouch.C_STICK).getColors());
    assertEquals("C", gc.getLook(CoreTouch.C_STICK).getLabel());
    // the D-pad, START, MENU and the move stick grey (the project owner, 17.11.2026), Z purple
    assertEquals(TouchStyle.GC_GREY, gc.getDpadColors());
    assertEquals(TouchStyle.GC_GREY, gc.getLook(CoreTouch.START).getColors());
    assertEquals(TouchStyle.GC_GREY, gc.getLook(CoreTouch.MENU).getColors());
    assertEquals("the move stick grey", TouchStyle.GC_STICK, gc.getDefaultColors());
    assertEquals(TouchStyle.GC_PURPLE, gc.getLook(CoreTouch.Z).getColors());
  }

  @Test public void ps2Skin() {
    RetroTouchSkin ps2 = TouchStyle.ps2();
    assertSame(RetroTouchIcon.SYMBOL_CROSS, ps2.getLook(CoreTouch.A).getIcon());
    assertSame(RetroTouchIcon.SYMBOL_CIRCLE, ps2.getLook(CoreTouch.B).getIcon());
    assertSame(RetroTouchIcon.SYMBOL_SQUARE, ps2.getLook(CoreTouch.X).getIcon());
    assertSame(RetroTouchIcon.SYMBOL_TRIANGLE, ps2.getLook(CoreTouch.Y).getIcon());
    assertEquals(0xFF6E96E6, ps2.getLook(CoreTouch.A).getColors().getIconColor());
    assertEquals("R1", ps2.getLook(CoreTouch.Z).getLabel());
    assertEquals(RetroTouchButtonShape.BUMPER, ps2.getLook(CoreTouch.Z).getShape());
    assertEquals("L2", ps2.getLook(CoreTouch.L).getLabel());
    assertEquals(RetroTouchButtonShape.TRIGGER, ps2.getLook(CoreTouch.R).getShape());
    assertEquals("SELECT", ps2.getLook(CoreTouch.MENU).getLabel());
    assertEquals(RetroTouchDpadStyle.FOUR_BUTTONS, ps2.getDpadStyle());
    assertSame(RetroTouchIcon.ARROW_UP, ps2.getDpadIcons()[0]);
    assertEquals("L3", ps2.getMoveStickLook().getLabel());
    assertEquals("R3", ps2.getLook(CoreTouch.C_STICK).getLabel());
  }

  @Test public void xboxSkin() {
    RetroTouchSkin xbox = TouchStyle.xbox();
    assertEquals(0xFF6FCB4E, xbox.getLook(CoreTouch.A).getColors().getTextColor());
    assertEquals(0xFFF0605A, xbox.getLook(CoreTouch.B).getColors().getTextColor());
    assertEquals(0xFF5B8FF0, xbox.getLook(CoreTouch.X).getColors().getTextColor());
    assertEquals(0xFFF2C94C, xbox.getLook(CoreTouch.Y).getColors().getTextColor());
    assertEquals("LT", xbox.getLook(CoreTouch.L).getLabel());
    assertEquals(RetroTouchButtonShape.TRIGGER, xbox.getLook(CoreTouch.L).getShape());
    assertEquals("RB", xbox.getLook(CoreTouch.Z).getLabel());
    assertEquals(RetroTouchButtonShape.BUMPER, xbox.getLook(CoreTouch.Z).getShape());
    assertSame(TouchStyle.XBOX_VIEW, xbox.getLook(CoreTouch.MENU).getIcon());
    assertEquals(RetroTouchDpadStyle.CROSS, xbox.getDpadStyle());
    assertSame(RetroTouchIcon.ARROW_LEFT, xbox.getDpadIcons()[2]);
    assertEquals("LS", xbox.getMoveStickLook().getLabel());
    assertEquals("RS", xbox.getLook(CoreTouch.C_STICK).getLabel());
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

  @Test public void ownIconsParse() {
    for (RetroTouchIcon icon : new RetroTouchIcon[] {TouchStyle.PLUS, TouchStyle.MINUS, TouchStyle.XBOX_VIEW}) {
      assertEquals(24f, icon.getViewBoxSize(), 0f);
    }
  }

  @Test public void lookNumbers() {
    assertEquals(TouchStyle.GC, TouchStyle.look("gc"));
    assertEquals(TouchStyle.DEFAULT_LOOK, TouchStyle.look("nonsense"));
    assertEquals(TouchStyle.DEFAULT_LOOK, TouchStyle.look(null));
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
