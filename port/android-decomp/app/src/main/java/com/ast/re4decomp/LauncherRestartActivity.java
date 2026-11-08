package com.ast.re4decomp;
import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.os.SystemClock;
import java.io.File;

/**
 * The way back to the start screen from a running game (Management -> "Import other discs", the disc problems;
 * wish of the project owner, 06.11.2026: with both discs imported the launcher started the game at once, and the
 * game only ended, so another release could not be imported over the discs). The core cannot be restarted inside
 * the process that ran it: this activity runs in a process of its own (":restart", manifest), ends the game's
 * process (GAME_PID), waits until it is gone and opens MainActivity in a new process with
 * MainActivity.SHOW_LAUNCHER, which keeps it from starting the game again at once. Then it ends its own process.
 * Discs, saves, the memory card and the settings stay (they are files of the app).
 */
public final class LauncherRestartActivity extends Activity {
  /** The game's process (android.os.Process.myPid() there). */
  static final String GAME_PID = "gamePid";
  /** Back into the game instead of the start screen (the menu's renderer, port/docs/vulkan-bauplan.md E7). */
  static final String BACK_TO_GAME = "backToGame";
  /** How long the game's process may take to go (in steps of 20 ms). */
  private static final int WAIT_STEPS = 100;

  @Override protected void onCreate(Bundle state) {
    super.onCreate(state);
    int pid = getIntent().getIntExtra(GAME_PID, 0);
    if (pid > 0 && pid != Process.myPid()) {
      Process.killProcess(pid);
      // until it is gone, so Android starts the launcher in a new process (the core process ends with it)
      File proc = new File("/proc/" + pid);
      for (int i = 0; i < WAIT_STEPS && proc.exists(); i++) SystemClock.sleep(20);
    }
    final boolean toGame = getIntent().getBooleanExtra(BACK_TO_GAME, false);
    startActivity(new Intent(this, MainActivity.class).putExtra(MainActivity.SHOW_LAUNCHER, !toGame)
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
    finish();
    overridePendingTransition(0, 0);
    Runtime.getRuntime().exit(0);
  }
}
