package com.ast.cubeevil4.ouya;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;

import java.io.File;

/**
 * Starts the app again after the game ended ("Import other discs", "Delete All Data"): the game core cannot start a
 * second time in one process, so the app's process ends and this window, in a process of its own (":restart"),
 * waits until it is gone and opens OuyaActivity anew. Until 15.11.2026 an alarm did that; on the OUYA it left the
 * player in Android's menu (report of the project owner), and Android 10 and later do not let an alarm open a window
 * of an app in the background at all. An activity in the foreground may open one.
 */
public class OuyaRestart extends Activity {
    static final String PID = "pid";

    private final Handler handler = new Handler();
    private int tries;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        final int pid = getIntent().getIntExtra(PID, -1);
        handler.post(new Runnable() {
            @Override
            public void run() {
                // the old process gone (or after 5 s anyway): the app anew
                if (pid > 0 && new File("/proc/" + pid).exists() && tries++ < 50) {
                    handler.postDelayed(this, 100);
                    return;
                }
                Intent start = new Intent(OuyaRestart.this, OuyaActivity.class);
                start.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                startActivity(start);
                finish();
                overridePendingTransition(0, 0);
                handler.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        android.os.Process.killProcess(android.os.Process.myPid());
                    }
                }, 500);
            }
        });
    }
}
