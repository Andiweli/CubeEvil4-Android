package com.ast.re4decomp;
import android.app.Application;
import com.ast.re4decomp.disc.GameCubeDisc;
public final class Re4Application extends Application {
  public ImportManager imports;
  @Override
  public void onCreate() {
    super.onCreate();
    // the process of LauncherRestartActivity only ends the game's process and opens the launcher: no core there
    if (restartProcess()) return;
    if (getResources().getBoolean(R.bool.core_mode)) {
      // Core mode: complete imports, and the game core is loaded (and its
      // memory at 0x80000000 reserved) before any Activity or GL work.
      GameCubeDisc.importEverything = true;
      // the core runs the European discs too (port/docs/retail-eu-plan.md)
      GameCubeDisc.retailAllowed = true;
      CoreBridge.load();
    }
    imports = new ImportManager(this);
  }

  /** This is the process ":restart" (manifest, LauncherRestartActivity). */
  private static boolean restartProcess() {
    try (java.io.FileInputStream in = new java.io.FileInputStream("/proc/self/cmdline")) {
      byte[] b = new byte[256];
      int n = in.read(b), end = 0;
      while (end < Math.max(n, 0) && b[end] != 0) end++;
      return new String(b, 0, end, java.nio.charset.StandardCharsets.UTF_8).endsWith(":restart");
    } catch (java.io.IOException e) {
      return false;
    }
  }
}
