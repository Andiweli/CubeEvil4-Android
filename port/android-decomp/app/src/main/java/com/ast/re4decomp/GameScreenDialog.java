package com.ast.re4decomp;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.os.Build;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import java.util.ArrayList;

/** Full viewport game-owned presentation with a uniform source unit scale. */
abstract class GameScreenDialog extends Dialog {
  static final int PAPER = Color.rgb(221, 215, 194), GOLD = Color.rgb(212, 183, 121);
  static final int MUTED = Color.rgb(144, 145, 130), PANEL = Color.rgb(29, 31, 27);
  final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final ArrayList<Hit> hits = new ArrayList<>();
  private final GameMenuNavigation navigation = new GameMenuNavigation();
  private Screen screen;
  private float scale = 1;
  private int heldPageDirection;
  private final Runnable pageTick=new Runnable(){public void run(){
    if(screen==null || !isShowing() || heldPageDirection==0)return;
    if(!continuousPageInput()){heldPage(0);return;}
    turnPage(heldPageDirection);screen.postDelayed(this,33);
  }};
  GameMenuLayout layout = new GameMenuLayout(640,480);
  private final float[] modelViewport = {-1,-1,-1,-1};
  private static final class Hit {
    final RectF bounds; final float[] polygon; final Runnable action;
    Hit(RectF bounds, Runnable action) { this.bounds = bounds; polygon=null;this.action = action; }
    Hit(float[] polygon,Runnable action) { this.polygon=polygon.clone();bounds=null;this.action=action; }
    boolean contains(float x,float y) { return polygon==null?bounds.contains(x,y):GameMenuLayout.insideQuad(polygon,x,y); }
  }
  GameScreenDialog(Activity activity) { super(activity, android.R.style.Theme_Material_NoActionBar_Fullscreen); }
  @Override protected void onCreate(Bundle state) {
    super.onCreate(state);
    requestWindowFeature(Window.FEATURE_NO_TITLE);
    setCancelable(false);
    screen = new Screen(getContext());
    setContentView(screen);
    screen.setFocusable(true); screen.setFocusableInTouchMode(true); screen.requestFocus();
    Window w = getWindow();
    if (w != null) {
      w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
      w.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
      w.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
      w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
          View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
          View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN |
          View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
      if (Build.VERSION.SDK_INT >= 33)
        getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
            android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, this::back);
      CarDisplay.fit(getContext(), w);
    }
  }
  @Override protected void onStart() {
    super.onStart();
    Window w = getWindow();
    if (w != null) w.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
  }
  final void refreshScreen() { if (screen != null) screen.invalidate(); }
  final void refreshScreenOnAnimation() { if(screen!=null)screen.postInvalidateOnAnimation(); }
  final void refreshScreenAfter(long milliseconds) { if(screen!=null)screen.postInvalidateDelayed(milliseconds); }
  abstract void drawScreen(Canvas canvas);
  abstract void direction(int x, int y);
  abstract void confirm();
  abstract void back();
  @Override public void onBackPressed() { back(); }
  void auxiliary(int key) {}
  void turnPage(int step) {}
  boolean continuousPageInput() {return false;}
  boolean hardwareDrawing() {return false;}
  float sourceHeight() {return 480;}
  float sourceTop() {return 0;}
  private void heldPage(int direction) {
    if(direction==heldPageDirection)return;
    heldPageDirection=direction;if(screen!=null)screen.removeCallbacks(pageTick);
    if(direction!=0 && continuousPageInput()){turnPage(direction);if(screen!=null)screen.postDelayed(pageTick,33);}
  }
  @Override public void dismiss(){heldPage(0);super.dismiss();}
  void rightStick(float x, float y) {}
  void touchBackground(float x, float y) {}
  final void text(Canvas canvas, String text, float x, float y, float size, int color) {
    paint.setStyle(Paint.Style.FILL); paint.setColor(color); paint.setTextSize(size);
    paint.setTypeface(Typeface.SANS_SERIF); paint.setTextAlign(Paint.Align.LEFT);
    canvas.drawText(text == null ? "" : text, x, y, paint);
  }
  final void rightText(Canvas canvas, String text, float x, float y, float size, int color) {
    paint.setStyle(Paint.Style.FILL); paint.setColor(color); paint.setTextSize(size);
    paint.setTypeface(Typeface.SANS_SERIF); paint.setTextAlign(Paint.Align.RIGHT);
    canvas.drawText(text, x, y, paint); paint.setTextAlign(Paint.Align.LEFT);
  }
  final void centered(Canvas canvas, String value, float x, float y, float size, int color) {
    paint.setStyle(Paint.Style.FILL); paint.setColor(color); paint.setTextSize(size);
    paint.setTypeface(Typeface.SANS_SERIF); paint.setTextAlign(Paint.Align.CENTER);
    canvas.drawText(value, x, y, paint); paint.setTextAlign(Paint.Align.LEFT);
  }
  final void panel(Canvas canvas, RectF rectangle, boolean selected) {
    paint.setStyle(Paint.Style.FILL); paint.setColor(selected ? Color.rgb(52, 51, 39) : PANEL);
    canvas.drawRect(rectangle, paint);
    paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(selected ? 2 : 1);
    paint.setColor(selected ? GOLD : Color.rgb(72, 77, 63)); canvas.drawRect(rectangle, paint);
    paint.setStyle(Paint.Style.FILL);
  }
  final void modelWindow(Canvas canvas, RectF rectangle) {
    modelWindow(canvas,rectangle,true);
  }
  final void modelWindow(Canvas canvas, RectF rectangle,boolean border) {
    float[] normalized = layout.normalized(new float[]{rectangle.left,rectangle.top,rectangle.right,rectangle.bottom});
    boolean changed=false;
    for(int n=0;n<4;++n)if(Math.abs(normalized[n]-modelViewport[n])>.00001f) { changed=true; break; }
    if(changed) {
      System.arraycopy(normalized,0,modelViewport,0,4);
      NativeBridge.setSubscreenModelViewport(normalized[0],normalized[1],normalized[2],normalized[3]);
    }
    paint.setStyle(Paint.Style.FILL);
    paint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.CLEAR));
    canvas.drawRect(rectangle, paint); paint.setXfermode(null);
    if(border) {
      paint.setStyle(Paint.Style.STROKE); paint.setColor(GOLD); paint.setStrokeWidth(1);
      canvas.drawRect(rectangle, paint); paint.setStyle(Paint.Style.FILL);
    }
  }
  final void button(Canvas canvas, String label, RectF bounds, boolean selected, boolean enabled, Runnable action) {
    panel(canvas, bounds, selected);
    centered(canvas, label, bounds.centerX(), bounds.centerY() + 5, 15, enabled ? PAPER : MUTED);
    if (enabled && action != null) hits.add(new Hit(new RectF(bounds), action));
  }
  final void hotspot(RectF bounds, Runnable action) { hits.add(new Hit(new RectF(bounds), action)); }
  final void hotspot(float[] polygon,Runnable action) { hits.add(new Hit(polygon,action)); }
  final RectF centeredBounds(float left,float top,float right,float bottom) {
    return new RectF(left+layout.extraX*.5f,top+layout.extraY*.5f,right+layout.extraX*.5f,bottom+layout.extraY*.5f);
  }
  final String shorten(String value, float width, float size) {
    paint.setTextSize(size); paint.setTypeface(Typeface.SANS_SERIF);
    if (paint.measureText(value) <= width) return value;
    int end = value.length();
    while (end > 0 && paint.measureText(value.substring(0, end) + "…") > width) --end;
    return value.substring(0, end) + "…";
  }
  final float paragraph(Canvas canvas, String value, float x, float top, float width, float size, float bottom, int color) {
    paint.setTextSize(size); paint.setTypeface(Typeface.SANS_SERIF);
    float y = top;
    for (String line : value.split("\\n", -1)) {
      String remaining = line;
      if (remaining.isEmpty()) { y += size * 1.4f; continue; }
      while (!remaining.isEmpty() && y <= bottom) {
        int count = paint.breakText(remaining, true, width, null);
        if (count <= 0) break;
        if (count < remaining.length()) {
          int space = remaining.lastIndexOf(' ', count - 1);
          if (space > 0) count = space;
        }
        text(canvas, remaining.substring(0, count), x, y, size, color);
        remaining = remaining.substring(count).trim();
        y += size * 1.4f;
      }
    }
    return y;
  }
  @Override public boolean dispatchKeyEvent(KeyEvent e) {
    int key = e.getKeyCode(); boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
    boolean once = down && e.getRepeatCount() == 0;
    if (key == KeyEvent.KEYCODE_BUTTON_A || key == KeyEvent.KEYCODE_DPAD_CENTER || key == KeyEvent.KEYCODE_ENTER) {
      if (once) confirm(); return true;
    }
    if (key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_ESCAPE || key == KeyEvent.KEYCODE_BUTTON_START) {
      if (once) back(); return true;
    }
    if (key == KeyEvent.KEYCODE_BUTTON_L2 || key == KeyEvent.KEYCODE_BUTTON_R2) {
      int direction=key==KeyEvent.KEYCODE_BUTTON_L2?-1:1;
      if(!down && heldPageDirection==direction)heldPage(0);
      else if(continuousPageInput()){if(once)heldPage(direction);}
      else if (once) turnPage(direction); return true;
    }
    if (key == KeyEvent.KEYCODE_DPAD_LEFT || key == KeyEvent.KEYCODE_DPAD_RIGHT || key == KeyEvent.KEYCODE_DPAD_UP || key == KeyEvent.KEYCODE_DPAD_DOWN) {
      if (down) direction(key == KeyEvent.KEYCODE_DPAD_LEFT ? -1 : key == KeyEvent.KEYCODE_DPAD_RIGHT ? 1 : 0,
          key == KeyEvent.KEYCODE_DPAD_UP ? -1 : key == KeyEvent.KEYCODE_DPAD_DOWN ? 1 : 0);
      return true;
    }
    if (key == KeyEvent.KEYCODE_BUTTON_X || key == KeyEvent.KEYCODE_BUTTON_Y || key == KeyEvent.KEYCODE_BUTTON_L1 || key == KeyEvent.KEYCODE_BUTTON_R1 || key == KeyEvent.KEYCODE_BUTTON_SELECT) {
      if (once) auxiliary(key); return true;
    }
    // A released or repeated gamepad key must never reach the gameplay Activity.
    if ((e.getSource() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) return true;
    return super.dispatchKeyEvent(e);
  }
  @Override public boolean onGenericMotionEvent(MotionEvent e) {
    if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != InputDevice.SOURCE_JOYSTICK &&
        (e.getSource() & InputDevice.SOURCE_DPAD) != InputDevice.SOURCE_DPAD) return super.onGenericMotionEvent(e);
    float x = e.getAxisValue(MotionEvent.AXIS_HAT_X), y = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
    if (Math.abs(x) < .25f && Math.abs(y) < .25f) {
      x = e.getAxisValue(MotionEvent.AXIS_X); y = e.getAxisValue(MotionEvent.AXIS_Y);
    }
    int move = navigation.sample(x, y, SystemClock.uptimeMillis());
    if (move != 0) direction(Math.abs(move) == 1 ? move : 0, Math.abs(move) == 2 ? move / 2 : 0);
    if(continuousPageInput()) {
      float l=e.getAxisValue(MotionEvent.AXIS_LTRIGGER),r=e.getAxisValue(MotionEvent.AXIS_RTRIGGER);
      if(l==0)l=e.getAxisValue(MotionEvent.AXIS_BRAKE);if(r==0)r=e.getAxisValue(MotionEvent.AXIS_GAS);
      heldPage(r>.5f?1:l>.5f?-1:0);
    }
    InputDevice device = e.getDevice();
    boolean zr = device != null && device.getMotionRange(MotionEvent.AXIS_Z, e.getSource()) != null &&
        device.getMotionRange(MotionEvent.AXIS_RZ, e.getSource()) != null;
    float rx = e.getAxisValue(zr ? MotionEvent.AXIS_Z : MotionEvent.AXIS_RX);
    float ry = e.getAxisValue(zr ? MotionEvent.AXIS_RZ : MotionEvent.AXIS_RY);
    if (!Float.isNaN(rx) && !Float.isInfinite(rx) && !Float.isNaN(ry) && !Float.isInfinite(ry))
      rightStick(Math.abs(rx) < .15f ? 0 : Math.max(-1, Math.min(1, rx)),
          Math.abs(ry) < .15f ? 0 : Math.max(-1, Math.min(1, -ry)));
    return true;
  }
  private final class Screen extends View {
    Screen(Context context) { super(context); setLayerType(View.LAYER_TYPE_SOFTWARE, null); }
    @Override protected void onDraw(Canvas canvas) {
      super.onDraw(canvas);
      int mode=hardwareDrawing()?View.LAYER_TYPE_NONE:View.LAYER_TYPE_SOFTWARE;
      if(getLayerType()!=mode)setLayerType(mode,null);
      if(getWidth()<=0 || getHeight()<=0)return;
      layout = new GameMenuLayout(getWidth(),getHeight(),sourceHeight(),sourceTop()); scale = layout.scale;
      canvas.drawColor(Color.BLACK);
      int save = canvas.save(); canvas.scale(scale, scale);canvas.translate(0,-layout.top);
      canvas.clipRect(0, layout.top, layout.width, layout.top+layout.visibleHeight); hits.clear(); drawScreen(canvas);
      // The original SS row already has an Exit entry. Keep a touch back
      // region without painting an Android-style button over original art.
      float backBottom=layout.top+layout.visibleHeight;
      hotspot(new RectF(layout.right(544),backBottom-23,layout.right(624),backBottom-2), GameScreenDialog.this::back);
      canvas.restoreToCount(save);
    }
    @Override public boolean onTouchEvent(MotionEvent e) {
      if (e.getActionMasked() != MotionEvent.ACTION_UP) return true;
      if (scale <= 0) return true;
      float x = e.getX() / scale, y = e.getY() / scale+layout.top;
      for (int n = hits.size() - 1; n >= 0; --n) {
        Hit hit = hits.get(n); if (hit.contains(x, y)) { hit.action.run(); return true; }
      }
      touchBackground(x, y); return true;
    }
  }
}
