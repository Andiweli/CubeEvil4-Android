package com.ast.re4decomp;
import android.app.Application;
import com.ast.re4decomp.disc.GameCubeDisc;
public final class Re4Application extends Application {
  public ImportManager imports;
  @Override
  public void onCreate() {
    super.onCreate();
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
}
