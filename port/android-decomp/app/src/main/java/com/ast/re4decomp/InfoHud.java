package com.ast.re4decomp;

import android.content.SharedPreferences;
import java.util.Locale;

/**
 * The Information HUD over the game (0.99.11, wish of the project owner, 12.11.2026; before the "FPS display"): the
 * menu's button moves it through its places as before (CoreActivity.FPS_PLACE: off, bottom left, bottom centre, top
 * left, top centre), check boxes under it choose what it shows, in this order:
 * - the game's frames per second, with "60 FPS" also the pictures on the screen (as the FPS display showed them);
 * - the screen's rate in Hz;
 * - the resolution the renderer draws at ("1920×1008 (3×)", as the FPS display showed it);
 * - the game's play time, hours:minutes:seconds, two digits each: the game's own (core command 25,
 *   runtime/instant_game.cpp re4_instant_play_seconds), which every save keeps, the typewriter's and the instant
 *   slot's, as the game's own screens show it; the app menu and the app in the background do not count.
 * All four are on until a box is cleared (a FPS display of before shows what it showed, and the time).
 */
final class InfoHud {
  /** The check boxes (the graphics settings), true until cleared. */
  static final String FPS = "hudFps", HZ = "hudHz", RESOLUTION = "hudResolution", TIME = "hudTime";
  static final String[] PARTS = {FPS, HZ, RESOLUTION, TIME};
  /** The core's command for the play time in seconds (CoreBridge.cheatAction), -1 outside a game. */
  static final int PLAY_TIME_COMMAND = 25;
  /** Between the parts. */
  static final String GAP = " · ";

  private InfoHud() {}

  static boolean shows(SharedPreferences graphics, String part) { return graphics.getBoolean(part, true); }

  /** "07:18:43": hours, minutes and seconds, two digits each (more hours from 100 on); "--:--:--" outside a game. */
  static String time(long seconds) {
    if (seconds < 0) return "--:--:--";
    return String.format(Locale.ROOT, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
  }

  /** The HUD's line: the parts shown, in their order, the empty ones left out; "" when none is shown. */
  static String line(boolean fps, String fpsText, boolean hz, String hzText, boolean resolution, String resolutionText,
                     boolean time, String timeText) {
    StringBuilder b = new StringBuilder();
    add(b, fps, fpsText);
    add(b, hz, hzText);
    add(b, resolution, resolutionText);
    add(b, time, timeText);
    return b.toString();
  }

  private static void add(StringBuilder b, boolean on, String text) {
    if (!on || text == null || text.isEmpty()) return;
    if (b.length() > 0) b.append(GAP);
    b.append(text);
  }
}
