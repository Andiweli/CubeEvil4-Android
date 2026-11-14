package com.ast.re4decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.text.format.Formatter;
import android.util.Log;
import android.util.TypedValue;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * A texture pack handed over by the CubeEvil4 Texture Packer app (its button "Import into CubeEvil4",
 * port/docs/textur-packer-android-plan.md): asks first, then imports it as the menu's "Import texture pack" does
 * (TexturePacks.importFrom: its own folder, the pack before it replaced only when the new one is complete) and switches
 * HD textures on; a running game uses it at its next picture. Only content the other app granted is read.
 */
public final class TextureImportActivity extends Activity {
  private static final String TAG = "RE4Textures";
  /** the Texture Packer's intent (its MainActivity.CUBEEVIL4_IMPORT) */
  static final String ACTION = "com.ast.cubeevil4.action.IMPORT_TEXTURE_PACK";

  private final AtomicBoolean cancel = new AtomicBoolean();
  private final Handler main = new Handler(Looper.getMainLooper());
  private AlertDialog dialog;

  @Override
  protected void onCreate(Bundle saved) {
    super.onCreate(saved);
    Uri uri = getIntent() != null ? getIntent().getData() : null;
    if (uri == null || !"content".equals(uri.getScheme()) || !getResources().getBoolean(R.bool.core_mode)) {
      Log.w(TAG, "nothing to import: " + uri);
      finish();
      return;
    }
    if (TexturePacks.importing) {
      done(getString(R.string.ingame_hd_import_busy));
      return;
    }
    String name = getIntent().getStringExtra(android.content.Intent.EXTRA_TITLE);
    long bytes = -1;
    try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE},
        null, null, null)) {
      if (c != null && c.moveToFirst()) {
        if (c.getString(0) != null) name = c.getString(0);
        if (!c.isNull(1)) bytes = c.getLong(1);
      }
    } catch (RuntimeException e) {
      Log.w(TAG, "the handed-over pack: " + e);
    }
    if (name == null) name = String.valueOf(uri.getLastPathSegment());
    String message = getString(R.string.ingame_hd_import_from, name,
        bytes >= 0 ? Formatter.formatShortFileSize(this, bytes) : "?");
    TexturePacks.Info before = TexturePacks.info(this);
    if (before != null) message += "\n\n" + getString(R.string.ingame_hd_replace_text, before.name);
    dialog = builder().setTitle(R.string.ingame_hd_textures).setMessage(message)
        .setNegativeButton(R.string.ingame_cancel, (d, w) -> finish())
        .setPositiveButton(R.string.ingame_hd_import, (d, w) -> start(uri))
        .setOnCancelListener(d -> finish())
        .create();
    dialog.show();
  }

  private AlertDialog.Builder builder() {
    return new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert);
  }

  private void start(Uri uri) {
    if (TexturePacks.importing) {
      done(getString(R.string.ingame_hd_import_busy));
      return;
    }
    TexturePacks.importing = true;
    TexturePacks.importDone = 0;
    TexturePacks.importTotal = 0;
    float density = getResources().getDisplayMetrics().density;
    LinearLayout box = new LinearLayout(this);
    box.setOrientation(LinearLayout.VERTICAL);
    int pad = Math.round(24 * density);
    box.setPadding(pad, pad / 2, pad, 0);
    ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
    bar.setMax(1000);
    box.addView(bar);
    TextView text = new TextView(this);
    text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
    text.setText(getString(R.string.ingame_hd_importing, "0", "?"));
    box.addView(text);
    dialog = builder().setTitle(R.string.ingame_hd_textures).setView(box)
        .setNegativeButton(R.string.ingame_hd_cancel, null)
        .setCancelable(false)
        .create();
    dialog.show();
    // "Cancel import" keeps the dialog until the copy stopped
    dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setOnClickListener(v -> cancel.set(true));
    final Context app = getApplicationContext();
    new Thread(() -> {
      TexturePacks.Info info = null;
      TexturePacks.PackException failure = null;
      try {
        info = TexturePacks.importFrom(app, uri, cancel, new TexturePacks.Listener() {
          @Override public void progress(long done, long total) {
            main.post(() -> {
              bar.setProgress(total > 0 ? (int) (done * 1000 / total) : 0);
              text.setText(getString(R.string.ingame_hd_importing, Formatter.formatShortFileSize(app, done),
                  Formatter.formatShortFileSize(app, total)));
            });
          }
          @Override public void done(TexturePacks.Info i, TexturePacks.PackException f) {}
        });
      } catch (TexturePacks.PackException e) {
        failure = e;
      } catch (java.io.IOException | RuntimeException e) {
        failure = new TexturePacks.PackException(TexturePacks.READ_FAILED, String.valueOf(e.getMessage()));
      }
      final TexturePacks.Info result = info;
      final TexturePacks.PackException problem = failure;
      main.post(() -> {
        SharedPreferences g = getSharedPreferences(CoreActivity.GRAPHICS_PREFS, MODE_PRIVATE);
        if (result != null) g.edit().putBoolean(TexturePacks.ON, true).apply();
        TexturePacks.apply(app, g);
        TexturePacks.importing = false;
        Log.i(TAG, "texture pack from another app: " + (problem == null ? "imported" : problem.getMessage()));
        if (dialog != null) dialog.dismiss();
        done(problem == null ? getString(R.string.ingame_hd_imported,
            java.text.NumberFormat.getIntegerInstance().format(result.images)) + "\n\n"
            + getString(R.string.ingame_hd_import_on) : CoreMenu.packProblem(this, problem));
      });
    }, "texture-import").start();
  }

  private void done(String message) {
    if (isFinishing() || isDestroyed()) return;
    dialog = builder().setTitle(R.string.ingame_hd_textures).setMessage(message)
        .setPositiveButton(android.R.string.ok, (d, w) -> finish())
        .setOnCancelListener(d -> finish())
        .create();
    dialog.show();
  }

  @Override
  protected void onDestroy() {
    // leaving while it copies (Home) lets it finish; the menu shows the result
    if (dialog != null) dialog.dismiss();
    super.onDestroy();
  }
}
