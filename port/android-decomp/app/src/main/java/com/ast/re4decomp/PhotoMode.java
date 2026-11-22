package com.ast.re4decomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.SystemClock;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Toast;

import java.util.HashSet;
import java.util.Set;

/**
 * Photo mode (1.1.0, wish of the project owner, 19.11.2026; Display, Optional features, off by default). L3 (the
 * keyboard's P, the app menu's "Photo mode" on touch; until the owner's second device test of 19.11.2026 a double
 * press of SELECT, which delayed the menu) asks the core for it (runtime/photo_mode.cpp): the game
 * stands as with its option screen, a free camera moves, the HUD, Leon, Ashley and the enemies can be hidden. This
 * class keeps its state, sends the camera's input (CoreBridge.photoInput) every 16 ms and draws the small overlay:
 * the four switches, a line of controls, a close button for touch; it fades out after four seconds without a button,
 * so screenshots (Android's own) stay clean. The touch controls are hidden meanwhile; on touch the left half moves
 * (a drag like a stick), the right half looks, a second finger goes up and down, a tap brings the overlay back.
 * <p>Controller: left stick moves, right stick turns and tilts, D-pad up/down rises and sinks (swapped after the
 * owner's device test of 19.11.2026), L and R the camera effect (before and next), X HUD, Y Leon,
 * A Ashley, Z enemies, START the camera back, B (or SELECT) leaves. Keyboard: WASD or the arrows move, the mouse looks,
 * E/Space up, Q/C down, Shift fast, Alt slow, 1-4 the switches, 5/6 or the wheel the effect, R back, Esc or P leaves;
 * SELECT (F1) opens the app menu over the standing picture (filters, effects), photo mode goes on after it.
 */
final class PhotoMode {
  interface Host {
    /** The controllers' state as the game would get it (CoreInput.pad). */
    int[] pad();
    /** Nothing held (CoreActivity.releaseAll): before photo mode and after it, so the game sees no stray press. */
    void releaseAll();
    /** The touch controls hidden (photo mode) or back. */
    void touchControlsHidden(boolean hidden);
  }

  static final int HUD = 1, LEON = 2, PARTNER = 4, ENEMIES = 8;
  /** The camera effects (runtime/photo_mode.cpp kFx*): normal, wide angle, telephoto, dolly zoom, Dutch angle, orbit. */
  static final int EFFECTS = 6;
  private static final int[] EFFECT_NAMES = {R.string.ingame_photo_fx_normal, R.string.ingame_photo_fx_wide,
      R.string.ingame_photo_fx_tele, R.string.ingame_photo_fx_dolly, R.string.ingame_photo_fx_dutch,
      R.string.ingame_photo_fx_orbit};
  private static final long ASK_MS = 800, FADE_AFTER_MS = 4000, FADE_MS = 500, TICK_MS = 16;
  private static final int PAD = 0, KEYS = 1, TOUCH = 2;
  private static final String TAG = CoreActivity.TAG;

  private final Context context;
  private final Handler main;
  private final Host host;
  private final Overlay view;
  private int state;  // 0 off, 1 asked, 2 on
  private long askedAt, onAt, shownAt;
  private int mask;  // what is hidden
  private int lastButtons;
  private int effect;
  private boolean lastL, lastR;
  private int kind = PAD;
  private final Set<Integer> keys = new HashSet<>();
  private float yawDelta, pitchDelta;  // radians, from the mouse and a finger, sent with the next tick
  private boolean reset;
  // touch: the finger that moves (from where it went down), the one that looks, the one that rises
  private int moveId = -1, lookId = -1, riseId = -1;
  private float moveX0, moveY0, moveX, moveY, lookX, lookY, riseY0, riseY;
  private float downX, downY;
  private long downAt;
  private boolean dragged;
  private int lines;

  PhotoMode(Context context, Handler main, Host host) {
    this.context = context;
    this.main = main;
    this.host = host;
    view = new Overlay(context);
    view.setVisibility(View.GONE);
  }

  View view() { return view; }

  /** Asked or on: CoreActivity's SELECT, keys and mouse go here. */
  boolean busy() { return state != 0; }

  /** On: the game stands, the camera is free. */
  boolean active() { return state == 2; }

  /** The double press: asks the core (no answer from a core without photo mode: a note). */
  void request(String how) {
    if (state != 0) return;
    host.releaseAll();
    if (!CoreBridge.photoMode(1, mask)) {
      Log.i(TAG, "photo mode: this core has none");
      Toast.makeText(context, R.string.ingame_photo_not_here, Toast.LENGTH_SHORT).show();
      return;
    }
    state = 1;
    askedAt = SystemClock.uptimeMillis();
    Log.i(TAG, "photo mode: asked (" + how + ")");
    main.removeCallbacks(tick);
    main.post(tick);
  }

  /** B, SELECT, Esc, F1, the close button, Back, the app going to the background. */
  void leave(String why) {
    if (state == 0) return;
    CoreBridge.photoMode(0, 0);
    stop("left (" + why + ")");
  }

  private void stop(String why) {
    state = 0;
    main.removeCallbacks(tick);
    keys.clear();
    moveId = lookId = riseId = -1;
    view.setVisibility(View.GONE);
    host.touchControlsHidden(false);
    host.releaseAll();
    Log.i(TAG, "photo mode: " + why);
  }

  private void on() {
    state = 2;
    onAt = SystemClock.uptimeMillis();
    int[] p = host.pad();
    lastButtons = p[0];
    lastL = (p[0] & CoreInput.GC_L) != 0 || p[5] > 127;
    lastR = (p[0] & CoreInput.GC_R) != 0 || p[6] > 127;
    effect = 0;
    host.touchControlsHidden(true);
    view.setVisibility(View.VISIBLE);
    show();
    Log.i(TAG, "photo mode: on after " + (onAt - askedAt) + " ms");
  }

  private void show() {
    shownAt = SystemClock.uptimeMillis();
    view.invalidate();
  }

  /** The next (+1) or the one before (-1) camera effect. */
  void effect(int step) {
    effect = (effect + step + EFFECTS) % EFFECTS;
    if (lines++ < 40) Log.i(TAG, "photo mode: camera effect " + effect);
    show();
  }

  private void toggle(int bit) {
    mask ^= bit;
    CoreBridge.photoMode(2, mask);
    if (lines++ < 40) Log.i(TAG, "photo mode: hidden now " + names(mask));
    show();
  }

  private static String names(int m) {
    if (m == 0) return "nothing";
    StringBuilder b = new StringBuilder();
    if ((m & HUD) != 0) b.append("HUD ");
    if ((m & LEON) != 0) b.append("Leon ");
    if ((m & PARTNER) != 0) b.append("partner ");
    if ((m & ENEMIES) != 0) b.append("enemies");
    return b.toString().trim();
  }

  /** A key of a keyboard while photo mode is on (all of them stay here). */
  void keyboardKey(int code, boolean down, boolean first) {
    kind = KEYS;
    if (down) keys.add(code); else keys.remove(code);
    if (!first) return;
    switch (code) {
      case KeyEvent.KEYCODE_ESCAPE: leave("Esc"); return;
      case KeyEvent.KEYCODE_1: toggle(HUD); return;
      case KeyEvent.KEYCODE_2: toggle(LEON); return;
      case KeyEvent.KEYCODE_3: toggle(PARTNER); return;
      case KeyEvent.KEYCODE_4: toggle(ENEMIES); return;
      case KeyEvent.KEYCODE_R: reset = true; show(); return;
      case KeyEvent.KEYCODE_5: effect(-1); return;
      case KeyEvent.KEYCODE_6: effect(1); return;
      case KeyEvent.KEYCODE_H: show(); return;
      default: break;
    }
  }

  /** The captured mouse moved (counts): it looks around. */
  void mouse(float dx, float dy, float radiansPerCount) {
    kind = KEYS;
    yawDelta += dx * radiansPerCount;
    pitchDelta -= dy * radiansPerCount;
  }

  /** A controller's button went down or up (CoreInput has it): the tick reads the buttons. */
  void padKey() { kind = PAD; }

  private boolean held(int... codes) {
    for (int c : codes) if (keys.contains(c)) return true;
    return false;
  }

  private static int s8(float v) {
    return Math.round(Math.max(-1f, Math.min(1f, v)) * 127f) & 0xFF;
  }

  private static int s16(float radians) {
    return Math.round(Math.max(-30000f, Math.min(30000f, radians * 8192f))) & 0xFFFF;
  }

  private final Runnable tick = new Runnable() {
    @Override public void run() {
      if (state == 0) return;
      long now = SystemClock.uptimeMillis();
      int st = CoreBridge.photoState();
      if (state == 1) {
        if ((st & 1) != 0) {
          on();
        } else if ((st & 2) != 0 || now - askedAt > ASK_MS) {
          CoreBridge.photoMode(0, 0);
          Toast.makeText(context, R.string.ingame_photo_not_here, Toast.LENGTH_SHORT).show();
          stop("not here" + ((st & 2) != 0 ? " (the game said no)" : " (no answer)"));
          return;
        } else {
          main.postDelayed(this, TICK_MS);
          return;
        }
      } else if ((st & 1) == 0 && now - onAt > 1000) {
        stop("ended by the game");
        return;
      }
      // the controllers: buttons by their edges, the sticks as they stand
      int[] p = host.pad();
      int b = p[0], pressed = b & ~lastButtons;
      lastButtons = b;
      if (pressed != 0) kind = PAD;
      if ((pressed & CoreInput.GC_B) != 0) {
        leave("B");
        return;
      }
      if ((pressed & CoreInput.GC_X) != 0) toggle(HUD);
      if ((pressed & CoreInput.GC_Y) != 0) toggle(LEON);
      if ((pressed & CoreInput.GC_A) != 0) toggle(PARTNER);
      if ((pressed & CoreInput.GC_Z) != 0) toggle(ENEMIES);
      if ((pressed & CoreInput.GC_START) != 0) {
        reset = true;
        show();
      }
      float mx = p[1] / CoreInput.MAIN_RAW, mz = p[2] / CoreInput.MAIN_RAW;
      // the right stick turns and tilts, the D-pad's up and down raise and lower the camera
      float turn = p[3] / CoreInput.C_RAW, tilt = p[4] / CoreInput.C_RAW;
      float rise = ((b & CoreInput.GC_UP) != 0 ? 1f : 0f) - ((b & CoreInput.GC_DOWN) != 0 ? 1f : 0f);
      // L and R: the digital click or the analog trigger past half way
      // L and R (the digital click or the analog trigger past half way): the camera effect before / next
      boolean nowL = (b & CoreInput.GC_L) != 0 || p[5] > 127, nowR = (b & CoreInput.GC_R) != 0 || p[6] > 127;
      if (nowL && !lastL) {
        kind = PAD;
        effect(-1);
      }
      if (nowR && !lastR) {
        kind = PAD;
        effect(1);
      }
      lastL = nowL;
      lastR = nowR;
      boolean slow = false, fast = false;
      // the keyboard
      if (!keys.isEmpty()) {
        float kx = (held(KeyEvent.KEYCODE_D, KeyEvent.KEYCODE_DPAD_RIGHT) ? 1f : 0f)
            - (held(KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_DPAD_LEFT) ? 1f : 0f);
        float kz = (held(KeyEvent.KEYCODE_W, KeyEvent.KEYCODE_DPAD_UP) ? 1f : 0f)
            - (held(KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_DPAD_DOWN) ? 1f : 0f);
        float ky = (held(KeyEvent.KEYCODE_E, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_PAGE_UP) ? 1f : 0f)
            - (held(KeyEvent.KEYCODE_Q, KeyEvent.KEYCODE_C, KeyEvent.KEYCODE_PAGE_DOWN) ? 1f : 0f);
        if (kx != 0) mx = kx;
        if (kz != 0) mz = kz;
        if (ky != 0) rise = ky;
        slow |= held(KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT, KeyEvent.KEYCODE_CTRL_LEFT);
        fast |= held(KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT);
      }
      // a finger
      float radius = Math.max(1f, Math.min(view.getWidth(), view.getHeight()) * 0.12f);
      if (moveId >= 0) {
        mx = (moveX - moveX0) / radius;
        mz = -(moveY - moveY0) / radius;
      }
      if (riseId >= 0) rise = -(riseY - riseY0) / radius;
      int speed = slow && !fast ? 1 : fast && !slow ? 2 : 0;
      int a = s8(mx) | s8(mz) << 8 | s8(rise) << 16 | s8(turn) << 24;
      int bw = s8(tilt) | speed << 8 | (reset ? 0x10000 : 0) | effect << 17;
      int c = s16(yawDelta) | s16(pitchDelta) << 16;
      reset = false;
      yawDelta = pitchDelta = 0;
      CoreBridge.photoInput(a, bw, c);
      // the overlay fades
      float alpha = view.alphaAt(now);
      if (alpha != view.drawnAlpha) view.invalidate();
      main.postDelayed(this, TICK_MS);
    }
  };

  /** The overlay: the switches top left, the close button top right, a line of controls at the bottom; it fades. */
  private final class Overlay extends View {
    private final Paint box = new Paint(Paint.ANTI_ALIAS_FLAG), chip = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint text = new TextPaint(Paint.ANTI_ALIAS_FLAG), small = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final RectF[] chips = {new RectF(), new RectF(), new RectF(), new RectF(), new RectF()};
    private final RectF close = new RectF();
    private final int[] bits = {HUD, LEON, PARTNER, ENEMIES};
    private final int[] labels = {R.string.ingame_photo_hud, R.string.ingame_photo_leon, R.string.ingame_photo_partner,
        R.string.ingame_photo_enemies};
    float drawnAlpha = 1f;

    Overlay(Context c) {
      super(c);
      box.setColor(0xB0000000);
      text.setColor(Color.WHITE);
      small.setColor(0xFFE0E0E0);
    }

    float alphaAt(long now) {
      long t = now - shownAt - FADE_AFTER_MS;
      if (t <= 0) return 1f;
      return Math.max(0f, 1f - t / (float) FADE_MS);
    }

    private boolean visibleNow() { return alphaAt(SystemClock.uptimeMillis()) > 0.3f; }

    @SuppressWarnings("deprecation")
    @Override protected void onDraw(Canvas canvas) {
      float alpha = alphaAt(SystemClock.uptimeMillis());
      drawnAlpha = alpha;
      if (alpha <= 0f) return;
      int a = Math.round(alpha * 255f);
      float d = getResources().getDisplayMetrics().density;
      int w = getWidth(), h = getHeight();
      float pad = 10 * d, gap = 8 * d;
      text.setTextSize(15 * d);
      small.setTextSize(12.5f * d);
      box.setAlpha(Math.round(alpha * 0xB0));
      text.setAlpha(a);
      small.setAlpha(a);
      // the title and the switches
      String title = getResources().getString(R.string.ingame_photo_title);
      float x = 16 * d, y = 16 * d;
      float cw = text.measureText(title) + 2 * pad;
      float chipH = 30 * d;
      float[] widths = new float[5];
      String fxLabel = "◆ " + getResources().getString(EFFECT_NAMES[effect]);
      for (int i = 0; i < 5; i++) {
        widths[i] = small.measureText(i < 4 ? "○ " + getResources().getString(labels[i]) : fxLabel) + 2 * pad;
        cw += widths[i] + gap;
      }
      canvas.drawRoundRect(new RectF(x, y, x + cw + gap, y + chipH + 2 * gap), 10 * d, 10 * d, box);
      float tx = x + pad, ty = y + gap + chipH / 2 - (text.ascent() + text.descent()) / 2;
      canvas.drawText(title, tx, ty, text);
      float cx = tx + text.measureText(title) + pad + gap;
      for (int i = 0; i < 4; i++) {
        boolean hiddenNow = (mask & bits[i]) != 0;
        chips[i].set(cx, y + gap, cx + widths[i], y + gap + chipH);
        chip.setColor(hiddenNow ? 0xFF555555 : 0xFF2E7D32);
        chip.setAlpha(Math.round(alpha * 0xE0));
        canvas.drawRoundRect(chips[i], chipH / 2, chipH / 2, chip);
        String label = (hiddenNow ? "○ " : "● ") + getResources().getString(labels[i]);
        canvas.drawText(label, cx + pad, y + gap + chipH / 2 - (small.ascent() + small.descent()) / 2, small);
        cx += widths[i] + gap;
      }
      // the camera effect (a tap: the next one)
      chips[4].set(cx, y + gap, cx + widths[4], y + gap + chipH);
      chip.setColor(effect == 0 ? 0xFF555555 : 0xFF1E5AA8);
      chip.setAlpha(Math.round(alpha * 0xE0));
      canvas.drawRoundRect(chips[4], chipH / 2, chipH / 2, chip);
      canvas.drawText(fxLabel, cx + pad, y + gap + chipH / 2 - (small.ascent() + small.descent()) / 2, small);
      // the close button (any input: it is the touch way out, harmless otherwise)
      float r = 20 * d;
      close.set(w - 16 * d - 2 * r, 16 * d, w - 16 * d, 16 * d + 2 * r);
      canvas.drawOval(close, box);
      Paint cross = new Paint(Paint.ANTI_ALIAS_FLAG);
      cross.setColor(Color.WHITE);
      cross.setAlpha(a);
      cross.setStrokeWidth(2.5f * d);
      float c0 = close.centerX(), c1 = close.centerY(), k = r * 0.42f;
      canvas.drawLine(c0 - k, c1 - k, c0 + k, c1 + k, cross);
      canvas.drawLine(c0 - k, c1 + k, c0 + k, c1 - k, cross);
      // the controls
      int hint = kind == KEYS ? R.string.ingame_photo_hint_keys : kind == TOUCH ? R.string.ingame_photo_hint_touch
          : R.string.ingame_photo_hint_pad;
      int lw = Math.max(1, Math.round(Math.min(w - 32 * d, 900 * d)));
      StaticLayout layout = new StaticLayout(getResources().getString(hint), small, lw - Math.round(2 * pad),
          Layout.Alignment.ALIGN_CENTER, 1f, 0f, false);  // (the Builder needs Android 6)
      float bh = layout.getHeight() + 2 * gap;
      float bx = (w - lw) / 2f, by = h - 16 * d - bh;
      canvas.drawRoundRect(new RectF(bx, by, bx + lw, by + bh), 10 * d, 10 * d, box);
      canvas.save();
      canvas.translate(bx + pad, by + gap);
      layout.draw(canvas);
      canvas.restore();
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
      kind = TOUCH;
      int action = e.getActionMasked();
      int i = e.getActionIndex();
      int id = e.getPointerId(i);
      float x = e.getX(i), y = e.getY(i);
      switch (action) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN:
          if (action == MotionEvent.ACTION_DOWN) {
            downX = x;
            downY = y;
            downAt = SystemClock.uptimeMillis();
            dragged = false;
          }
          if (visibleNow()) {
            if (close.contains(x, y)) {
              leave("the close button");
              return true;
            }
            for (int k = 0; k < 4; k++) {
              if (chips[k].contains(x, y)) {
                toggle(bits[k]);
                return true;
              }
            }
            if (chips[4].contains(x, y)) {
              effect(1);
              return true;
            }
          }
          if (moveId >= 0 || lookId >= 0) {
            if (riseId < 0) {
              riseId = id;
              riseY0 = riseY = y;
            }
          } else if (x < getWidth() / 2f) {
            moveId = id;
            moveX0 = moveX = x;
            moveY0 = moveY = y;
          } else {
            lookId = id;
            lookX = x;
            lookY = y;
          }
          return true;
        case MotionEvent.ACTION_MOVE:
          for (int k = 0; k < e.getPointerCount(); k++) {
            int pid = e.getPointerId(k);
            float px = e.getX(k), py = e.getY(k);
            if (Math.abs(px - downX) + Math.abs(py - downY) > 24 * getResources().getDisplayMetrics().density) {
              dragged = true;
            }
            if (pid == moveId) {
              moveX = px;
              moveY = py;
            } else if (pid == lookId) {
              // a drag across the picture's width turns by about 180 degrees
              float per = (float) Math.PI / Math.max(1, getWidth());
              yawDelta += (px - lookX) * per;
              pitchDelta -= (py - lookY) * per;
              lookX = px;
              lookY = py;
            } else if (pid == riseId) {
              riseY = py;
            }
          }
          return true;
        case MotionEvent.ACTION_POINTER_UP:
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_CANCEL:
          if (id == moveId) moveId = -1;
          if (id == lookId) lookId = -1;
          if (id == riseId) riseId = -1;
          if (action != MotionEvent.ACTION_POINTER_UP) {
            moveId = lookId = riseId = -1;
            // a tap: the overlay again
            if (action == MotionEvent.ACTION_UP && !dragged && SystemClock.uptimeMillis() - downAt < 400) show();
          }
          return true;
        default:
          return true;
      }
    }
  }
}
