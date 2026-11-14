package com.ast.re4decomp;

import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;

/**
 * Management -> "Delete All Data" (wish of the project owner, 14.11.2026, 0.99.15): the memory card emptied as if it
 * were formatted, and the instant save gone; RetroTouch (its layout and switches), the discs, the HD textures and the
 * app's settings stay.
 *
 * <p>A running game holds the card's directory and its own system data in memory and would write them back with its
 * next save, so the files go while no game runs: the menu marks the wish (request) and restarts the app into the game
 * as "Import other discs" ends it (CoreActivity.restartIntoGame: a new process; the core's process ends with the
 * game's), and the next start deletes them before the core opens the card (prepare). The core then makes a new card
 * file, formatted and empty, as on the very first start (runtime/memcard.cpp insert): the game finds an empty card,
 * not a broken one. Should the restart not come, the next start of the game does it.
 */
final class PlayerWipe {
  static final String PREFS = "player_wipe", AT_START = "wipeAtStart";

  private PlayerWipe() {}

  /** The wish, kept until the next start of the game (written at once: the process ends right after). */
  static void request(Context c) {
    prefs(c).edit().putBoolean(AT_START, true).commit();
  }

  static boolean pending(Context c) { return prefs(c).getBoolean(AT_START, false); }

  /** At the start, before the core: when the menu asked for it, the card's file and the instant save deleted; one
   *  line for the log ("" when nothing was asked). */
  static String prepare(Context c, File gameRoot) {
    if (!pending(c) || gameRoot == null) return "";
    String note = wipe(gameRoot);
    prefs(c).edit().remove(AT_START).commit();
    return note;
  }

  /** The memory card's file (runtime/memcard.cpp: memcard/card-a.bin) and the instant save with its unfinished copies
   *  (runtime/instant_store.cpp, PlayerBackup.writeAtomically) under `gameRoot`; what was done, for the log. */
  static String wipe(File gameRoot) {
    File card = cardFile(gameRoot), instant = PlayerBackup.instantFile(gameRoot);
    StringBuilder note = new StringBuilder("delete all data as asked in the menu: ");
    note.append(delete(card, "memory card")).append(", ").append(delete(instant, "instant save"));
    for (String part : new String[] {".tmp", ".import"}) {
      File f = new File(instant.getPath() + part);
      if (f.exists()) f.delete();
    }
    return note.append("; the core makes a new, formatted card").toString();
  }

  static File cardFile(File gameRoot) { return new File(new File(gameRoot, "memcard"), "card-a.bin"); }

  /** Deleted, else emptied (an empty card file is a new card to the core, an empty slot file no instant save). */
  private static String delete(File f, String what) {
    if (!f.exists()) return what + " not there";
    if (f.delete()) return what + " deleted (" + f.getName() + ")";
    try {
      new java.io.FileOutputStream(f).close();
      return what + " emptied (" + f.getName() + " could not be deleted)";
    } catch (java.io.IOException e) {
      return what + " NOT deleted (" + f + ": " + e.getMessage() + ")";
    }
  }

  private static SharedPreferences prefs(Context c) {
    return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
  }
}
