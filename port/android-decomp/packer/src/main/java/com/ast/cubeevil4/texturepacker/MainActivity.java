package com.ast.cubeevil4.texturepacker;

import android.Manifest;
import android.annotation.TargetApi;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.os.StatFs;
import android.os.SystemClock;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.text.format.Formatter;
import android.util.Log;
import android.util.TypedValue;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import java.io.File;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * The Texture Packer's one screen (port/docs/textur-packer-android-plan.md): what it does, the pack (a ZIP file or a
 * folder, through Android's file picker), its choices, the size with the file's size and the time on this device,
 * the storage, Start; then the progress, and at the end the file in Download with "Import into CubeEvil4".
 */
public final class MainActivity extends Activity {
  private static final String TAG = "TexPacker";
  private static final int REQUEST_ZIP = 1, REQUEST_FOLDER = 2, REQUEST_SAVE = 3, REQUEST_NOTIFY = 4;
  /** CubeEvil4's import of a pack another app hands over (TextureImportActivity in CubeEvil4) */
  static final String CUBEEVIL4_IMPORT = "com.ast.cubeevil4.action.IMPORT_TEXTURE_PACK";

  /** A storage with a Download folder (Android 10 and later). */
  static final class Volume {
    String name, label;
    long free;
  }

  /**
   * Text sizes in sp, a step above Android's defaults, and light text on a near-black ground (wish of the project owner,
   * 13.11.2026: "minimal größerer Text, er ist schwer lesbar"): body text 17 sp instead of 15; #EDEDED on #121212 is
   * about 16:1, the dimmed text of switched-off rows #8E8E8E about 5.6:1 (WCAG asks 4.5:1).
   */
  static final float TITLE_SP = 24, HEADING_SP = 19, BODY_SP = 17, BUTTON_SP = 17, REPORT_SP = 14;
  static final int BACKGROUND = 0xFF121212, TEXT = 0xFFEDEDED, HEADING = 0xFFFFFFFF, DIMMED = 0xFF8E8E8E;

  private final List<Volume> volumes = new ArrayList<>();
  private double units;
  private Summary shownChoices;

  private Button zip, folder, start, cancel, importButton, reportButton;
  private TextView status, progressText, doneText, spaceNote;
  private ProgressBar busy, bar;
  private LinearLayout choicesBox, variants, actions, progressBox, doneBox;
  private RadioGroup sizes, where;
  private final RadioButton[] sizeButtons = new RadioButton[Estimates.SIZES.length];

  @Override
  protected void onCreate(Bundle saved) {
    super.onCreate(saved);
    units = Estimates.deviceUnits();
    build();
    Job.listen(this::render);
    render();
  }

  @Override
  protected void onResume() {
    super.onResume();
    findVolumes();
    render();
  }

  @Override
  protected void onDestroy() {
    Job.listen(null);
    super.onDestroy();
  }

  // ---------------- the screen ----------------

  private int dp(float v) {
    return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
  }

  /** Light text, dimmed while its row is switched off. */
  static android.content.res.ColorStateList textColors(int color) {
    return new android.content.res.ColorStateList(new int[][] {{-android.R.attr.state_enabled}, {}},
        new int[] {DIMMED, color});
  }

  private TextView text(ViewGroup parent, String s, float sp, boolean bold) {
    TextView t = new TextView(this);
    t.setText(s);
    t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
    t.setTextColor(textColors(bold ? HEADING : TEXT));
    if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
    t.setLineSpacing(0, 1.1f);
    t.setPadding(0, dp(6), 0, dp(6));
    parent.addView(t);
    return t;
  }

  /** A radio button or check box with the larger text. */
  private <T extends android.widget.CompoundButton> T choice(T c) {
    c.setTextSize(TypedValue.COMPLEX_UNIT_SP, BODY_SP);
    c.setTextColor(textColors(TEXT));
    c.setPadding(c.getPaddingLeft(), dp(6), c.getPaddingRight(), dp(6));
    return c;
  }

  private Button button(ViewGroup parent, int text, View.OnClickListener click) {
    Button b = new Button(this);
    b.setText(text);
    b.setAllCaps(false);
    b.setTextSize(TypedValue.COMPLEX_UNIT_SP, BUTTON_SP);
    b.setOnClickListener(click);
    parent.addView(b);
    return b;
  }

  /**
   * Two buttons side by side, each on one half of the width (wish of the project owner, 13.11.2026); a longer text
   * wraps to a second line, both buttons then grow to the same height.
   */
  private Button[] pair(ViewGroup parent, int first, View.OnClickListener firstClick, int second,
      View.OnClickListener secondClick) {
    LinearLayout row = new LinearLayout(this);
    row.setOrientation(LinearLayout.HORIZONTAL);
    row.setBaselineAligned(false);
    parent.addView(row, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT));
    Button a = button(row, first, firstClick), b = button(row, second, secondClick);
    a.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
    b.setLayoutParams(new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f));
    return new Button[] {a, b};
  }

  private LinearLayout column(ViewGroup parent) {
    LinearLayout l = new LinearLayout(this);
    l.setOrientation(LinearLayout.VERTICAL);
    parent.addView(l);
    return l;
  }

  private void build() {
    ScrollView scroll = new ScrollView(this);
    scroll.setFitsSystemWindows(true);
    scroll.setBackgroundColor(BACKGROUND);
    LinearLayout page = new LinearLayout(this);
    page.setOrientation(LinearLayout.VERTICAL);
    page.setPadding(dp(16), dp(12), dp(16), dp(24));
    scroll.addView(page);
    text(page, getString(R.string.app_name), TITLE_SP, true);
    text(page, getString(R.string.packer_intro), BODY_SP, false);
    text(page, getString(R.string.packer_legal), BODY_SP, false);

    Button[] pick = pair(page, R.string.packer_choose_zip, v -> choose(false), R.string.packer_choose_folder,
        v -> choose(true));
    zip = pick[0];
    folder = pick[1];
    busy = new ProgressBar(this);
    busy.setIndeterminate(true);
    page.addView(busy, new LinearLayout.LayoutParams(dp(36), dp(36)));
    status = text(page, "", BODY_SP, false);

    choicesBox = column(page);
    variants = column(choicesBox);
    text(choicesBox, getString(R.string.packer_size), HEADING_SP, true);
    sizes = new RadioGroup(this);
    choicesBox.addView(sizes);
    for (int i = 0; i < Estimates.SIZES.length; i++) {
      RadioButton r = choice(new RadioButton(this));
      r.setId(View.generateViewId());
      final int size = Estimates.SIZES[i];
      r.setOnClickListener(v -> {
        Job.size = size;
        render();
      });
      sizes.addView(r);
      sizeButtons[i] = r;
    }
    text(choicesBox, getString(R.string.packer_where), HEADING_SP, true);
    where = new RadioGroup(this);
    choicesBox.addView(where);
    spaceNote = text(choicesBox, "", BODY_SP, false);

    // Start and Cancel side by side; the progress below them
    actions = new LinearLayout(this);
    actions.setOrientation(LinearLayout.VERTICAL);
    page.addView(actions);
    Button[] run = pair(actions, R.string.packer_start, v -> start(), R.string.packer_cancel,
        v -> ConvertService.cancel());
    start = run[0];
    cancel = run[1];

    progressBox = column(page);
    bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
    progressBox.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(24)));
    progressText = text(progressBox, "", BODY_SP, false);

    doneBox = column(page);
    doneText = text(doneBox, "", BODY_SP, false);
    Button[] end = pair(doneBox, R.string.packer_report, v -> showReport(), R.string.packer_import,
        v -> importIntoCubeEvil4());
    reportButton = end[0];
    importButton = end[1];
    setContentView(scroll);
  }

  private void show(View v, boolean shown) {
    v.setVisibility(shown ? View.VISIBLE : View.GONE);
  }

  /** The screen as Job is now. */
  private void render() {
    int state = Job.state;
    boolean converting = state == Job.CONVERTING;
    boolean ready = state == Job.READY || state == Job.DONE || state == Job.FAILED;
    zip.setEnabled(state != Job.READING && !converting);
    folder.setEnabled(state != Job.READING && !converting);
    show(busy, state == Job.READING);
    Summary s = Job.summary;
    if (state == Job.READING) {
      status.setText(Job.sourceIsFolder && Job.listed > 0
          ? getString(R.string.packer_reading_folder, count(Job.listed)) : getString(R.string.packer_reading));
    } else if (s != null) {
      String line = getString(R.string.packer_found, s.name, count(s.images), bytes(this, s.bytes));
      if (s.skipped > 0) line += "\n" + getString(R.string.packer_left_out, count(s.skipped));
      if (state == Job.FAILED && Job.failure != null) line += "\n\n" + failureText(Job.failure);
      if (state == Job.READY && Job.cancelled) line += "\n\n" + getString(R.string.packer_cancelled);
      status.setText(line);
    } else {
      status.setText(Job.failure != null ? Job.failure : "");
    }
    show(status, status.length() > 0);

    show(choicesBox, ready || converting);
    if (s != null && shownChoices != s) {
      buildVariants(s);
      shownChoices = s;
    }
    setEnabled(variants, ready);
    for (int i = 0; i < sizeButtons.length; i++) {
      RadioButton r = sizeButtons[i];
      String label = getString(i == 0 ? R.string.packer_size_256 : i == 1 ? R.string.packer_size_512
          : R.string.packer_size_1024);
      String estimate = Job.estimates == null ? getString(R.string.packer_estimating)
          : getString(R.string.packer_estimate, bytes(this, Job.estimates[i]),
              duration(Estimates.seconds(s != null ? s.bytes : 0, Job.estimates[i], units)));
      r.setText(label + "\n" + estimate);
      r.setChecked(Estimates.SIZES[i] == Job.size);
      r.setEnabled(ready);
    }
    renderWhere(ready);
    show(actions, ready || converting);
    cancel.setEnabled(converting);

    show(progressBox, converting);
    if (converting) {
      int done = Job.done, total = Job.total;
      bar.setMax(Math.max(total, 1));
      bar.setProgress(done);
      String line = getString(R.string.packer_converting, count(done), count(total), bytes(this, Job.written));
      long left = secondsLeft();
      if (left > 0) line += "\n" + getString(R.string.packer_time_left, duration(left));
      progressText.setText(line);
    }

    show(doneBox, state == Job.DONE);
    if (state == Job.DONE) {
      String line = getString(R.string.packer_done, Job.resultName, bytes(this, Job.resultBytes), duration(Job.seconds))
          + "\n" + getString(R.string.packer_done_hint);
      doneText.setText(line);
      reportButton.setEnabled(Job.report != null);
    }
  }

  private static void setEnabled(ViewGroup g, boolean on) {
    for (int i = 0; i < g.getChildCount(); i++) {
      View v = g.getChildAt(i);
      v.setEnabled(on);
      if (v instanceof ViewGroup) setEnabled((ViewGroup) v, on);
    }
  }

  /** A choice per group of the pack: a list of its variants, or (an add-on) a box to add it. */
  private void buildVariants(Summary s) {
    variants.removeAllViews();
    if (s.groups.isEmpty()) return;
    text(variants, getString(R.string.packer_choices), HEADING_SP, true);
    for (int g = 0; g < s.groups.size(); g++) {
      final int group = g;
      Summary.Group sg = s.groups.get(g);
      if (sg.addOn) {
        CheckBox add = choice(new CheckBox(this));
        add.setText(sg.name + ": " + getString(R.string.packer_add));
        add.setChecked(Job.choices.length > g && Job.choices[g] == 0);
        add.setOnCheckedChangeListener((b, on) -> choose(group, on ? 0 : -1));
        variants.addView(add);
        continue;
      }
      text(variants, sg.name, BODY_SP, false);
      List<String> names = new ArrayList<>();
      names.add(getString(R.string.packer_as_main));
      for (int v = 0; v < sg.variants.size(); v++)
        names.add(sg.sameAsMain.get(v) ? getString(R.string.packer_same_as_main, sg.variants.get(v)) : sg.variants.get(v));
      Spinner spinner = new Spinner(this);
      ArrayAdapter<String> list = new ArrayAdapter<String>(this, android.R.layout.simple_spinner_item, names) {
        @Override public View getView(int position, View convert, ViewGroup parent) {
          return styled(super.getView(position, convert, parent));
        }
        @Override public View getDropDownView(int position, View convert, ViewGroup parent) {
          return styled(super.getDropDownView(position, convert, parent));
        }
      };
      list.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
      spinner.setAdapter(list);
      spinner.setSelection(Job.choices.length > g ? Job.choices[g] + 1 : 0, false);
      spinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
        @Override public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
          choose(group, position - 1);
        }
        @Override public void onNothingSelected(AdapterView<?> parent) {}
      });
      variants.addView(spinner);
    }
  }

  /** A list entry of a choice with the larger text (it may wrap: long variant names stay whole). */
  private View styled(View v) {
    if (v instanceof TextView) {
      TextView t = (TextView) v;
      t.setTextSize(TypedValue.COMPLEX_UNIT_SP, BODY_SP);
      t.setTextColor(textColors(TEXT));
      t.setSingleLine(false);
      t.setEllipsize(null);
      t.setMinHeight(dp(48));
      t.setGravity(android.view.Gravity.CENTER_VERTICAL | android.view.Gravity.START);
      ViewGroup.LayoutParams lp = t.getLayoutParams();
      if (lp != null && lp.height != ViewGroup.LayoutParams.WRAP_CONTENT) {
        lp.height = ViewGroup.LayoutParams.WRAP_CONTENT;
        t.setLayoutParams(lp);
      }
    }
    return v;
  }

  private void choose(int group, int variant) {
    if (group >= Job.choices.length || Job.choices[group] == variant) return;
    Job.choices[group] = variant;
    Job.estimate();
  }

  // ---------------- storage ----------------

  /** The storages with a Download folder MediaStore writes to, with their free space (Android 10 and later). */
  private void findVolumes() {
    volumes.clear();
    if (Build.VERSION.SDK_INT < 29) return;
    try {
      findVolumes29();
    } catch (RuntimeException e) {
      Log.w(TAG, "storages: " + e);
    }
  }

  /** The storages to offer (the screen tests, PackerScreenTest). */
  void useVolumes(List<Volume> list) {
    volumes.clear();
    volumes.addAll(list);
    render();
  }

  @TargetApi(29)
  private void findVolumes29() {
    StorageManager sm = getSystemService(StorageManager.class);
    Set<String> names = MediaStore.getExternalVolumeNames(this);
    for (File f : getExternalFilesDirs(null)) {
      if (f == null) continue;
      try {
        StorageVolume v = sm.getStorageVolume(f);
        if (v == null || !Environment.MEDIA_MOUNTED.equals(v.getState())) continue;
        String name = Build.VERSION.SDK_INT >= 30 ? v.getMediaStoreVolumeName()
            : v.isPrimary() ? MediaStore.VOLUME_EXTERNAL_PRIMARY
            : v.getUuid() != null ? v.getUuid().toLowerCase(Locale.ROOT) : null;
        if (name == null || !names.contains(name)) continue;
        Volume vol = new Volume();
        vol.name = name;
        vol.label = v.getDescription(this);
        vol.free = new StatFs(f.getPath()).getAvailableBytes();
        volumes.add(vol);
      } catch (RuntimeException e) {
        Log.w(TAG, "storage " + f + ": " + e);
      }
    }
  }

  private int chosenVolume() {
    int id = where.getCheckedRadioButtonId();
    for (int i = 0; i < where.getChildCount(); i++)
      if (where.getChildAt(i).getId() == id) return i;
    return 0;
  }

  private void renderWhere(boolean ready) {
    int chosen = chosenVolume();
    if (where.getChildCount() != volumes.size()) {
      where.removeAllViews();
      for (int i = 0; i < volumes.size(); i++) {
        RadioButton r = choice(new RadioButton(this));
        r.setId(View.generateViewId());
        r.setOnClickListener(v -> render());
        where.addView(r);
      }
      chosen = 0;
    }
    for (int i = 0; i < volumes.size(); i++) {
      RadioButton r = (RadioButton) where.getChildAt(i);
      r.setText(getString(R.string.packer_volume, volumes.get(i).label, bytes(this, volumes.get(i).free)));
      r.setEnabled(ready);
      if (i == chosen) r.setChecked(true);
    }
    String note = "";
    boolean canStart = ready && Job.estimates != null;
    if (volumes.isEmpty()) {
      note = getString(R.string.packer_save_dialog);
    } else if (Job.estimates != null) {
      long need = Job.estimates[ConvertService.indexOf(Job.size)], free = volumes.get(chosen).free;
      if (!Estimates.fits(need, free)) {
        canStart = false;
        note = getString(R.string.packer_no_space, bytes(this, need + Estimates.RESERVE), bytes(this, free));
        int fits = Estimates.largestFitting(Job.estimates, free);
        if (fits >= 0) note += " " + getString(R.string.packer_smaller_fits, String.valueOf(Estimates.SIZES[fits]));
      }
    }
    spaceNote.setText(note);
    show(spaceNote, !note.isEmpty());
    start.setEnabled(canStart);
  }

  // ---------------- the pack ----------------

  private void choose(boolean isFolder) {
    Intent i;
    if (isFolder) {
      i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
    } else {
      i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
      i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed",
          "application/x-zip", "application/octet-stream"});
    }
    try {
      startActivityForResult(i, isFolder ? REQUEST_FOLDER : REQUEST_ZIP);
    } catch (ActivityNotFoundException e) {
      Job.failure = e.toString();
      render();
    }
  }

  @Override
  protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if (result != RESULT_OK || data == null || data.getData() == null) return;
    Uri uri = data.getData();
    if (request == REQUEST_SAVE) {
      ConvertService.start(this, null, uri);
      return;
    }
    if (request != REQUEST_ZIP && request != REQUEST_FOLDER) return;
    if (Job.state == Job.CONVERTING || Job.state == Job.READING) return;
    Job.closePack();
    Job.failure = null;
    Job.cancelled = false;
    Job.state = Job.READING;
    Job.sourceIsFolder = request == REQUEST_FOLDER;
    Job.source = uri;
    Job.listed = 0;
    render();
    final Context app = getApplicationContext();
    final boolean isFolder = request == REQUEST_FOLDER;
    // on the one worker thread, after the pack before it was closed there (Job.closePack)
    Job.WORKER.execute(() -> {
      long handle = 0;
      String problem = null;
      try {
        handle = isFolder ? openFolder(app, uri) : openZip(app, uri);
        if (handle == 0) problem = Job.failure;
      } catch (Exception e) {
        Log.w(TAG, "open", e);
        problem = app.getString(R.string.packer_open_failed, String.valueOf(e.getMessage()));
      }
      Summary s = handle != 0 ? Summary.parse(PackerNative.summary(handle)) : null;
      if (s != null && s.images == 0) {
        problem = app.getString(R.string.packer_no_images);
        PackerNative.close(handle);
        handle = 0;
        s = null;
      }
      final long h = handle;
      final Summary summary = s;
      final String why = problem;
      Job.onMain(() -> {
        Job.handle = h;
        Job.summary = summary;
        Job.failure = why;
        Job.choices = summary != null ? summary.defaultChoices() : new int[0];
        Job.state = h != 0 ? Job.READY : Job.EMPTY;
        if (h != 0) Job.estimate();
      });
    });
  }

  /** The ZIP file read where it is (no copy): its file descriptor goes to the core. */
  private long openZip(Context app, Uri uri) throws Exception {
    ContentResolver cr = app.getContentResolver();
    String name = displayName(cr, uri);
    ParcelFileDescriptor pfd = cr.openFileDescriptor(uri, "r");
    if (pfd == null) throw new java.io.IOException("cannot open the file");
    if (pfd.getStatSize() < 0) {
      pfd.close();
      Job.failure = app.getString(R.string.packer_cloud);
      return 0;
    }
    long h = PackerNative.openZip(pfd.detachFd(), name);
    if (h == 0) Job.failure = app.getString(R.string.packer_zip_failed, PackerNative.lastError());
    return h;
  }

  /** Every file of the folder and its subfolders; the core opens each through the file picker's permission. */
  private long openFolder(Context app, Uri tree) {
    ContentResolver cr = app.getContentResolver();
    String rootId = DocumentsContract.getTreeDocumentId(tree);
    String name = displayName(cr, DocumentsContract.buildDocumentUriUsingTree(tree, rootId));
    List<String> paths = new ArrayList<>();
    List<String> ids = new ArrayList<>();
    List<Long> sizes = new ArrayList<>();
    walk(cr, tree, rootId, "", paths, ids, sizes);
    final Uri[] docs = new Uri[ids.size()];
    for (int i = 0; i < docs.length; i++) docs[i] = DocumentsContract.buildDocumentUriUsingTree(tree, ids.get(i));
    long[] sz = new long[sizes.size()];
    for (int i = 0; i < sz.length; i++) sz[i] = sizes.get(i);
    PackerNative.Opener opener = index -> {
      try {
        ParcelFileDescriptor p = cr.openFileDescriptor(docs[index], "r");
        return p == null ? -1 : p.detachFd();
      } catch (Exception e) {
        Log.w(TAG, "open " + docs[index] + ": " + e);
        return -1;
      }
    };
    return PackerNative.openFolder(name, paths.toArray(new String[0]), sz, opener);
  }

  private static void walk(ContentResolver cr, Uri tree, String id, String prefix, List<String> paths,
      List<String> ids, List<Long> sizes) {
    Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, id);
    List<String[]> folders = new ArrayList<>();
    try (Cursor c = cr.query(children, new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
        DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
        DocumentsContract.Document.COLUMN_SIZE}, null, null, null)) {
      if (c == null) return;
      while (c.moveToNext()) {
        String child = c.getString(0), name = c.getString(1), mime = c.getString(2);
        if (child == null || name == null) continue;
        if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
          folders.add(new String[] {child, name});
        } else {
          paths.add(prefix + name);
          ids.add(child);
          sizes.add(c.isNull(3) ? 0L : c.getLong(3));
          Job.listed = paths.size();
          if (paths.size() % 500 == 0) Job.changed();
        }
      }
    }
    for (String[] f : folders) walk(cr, tree, f[0], prefix + f[1] + "/", paths, ids, sizes);
  }

  static String displayName(ContentResolver cr, Uri uri) {
    try (Cursor c = cr.query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
      if (c != null && c.moveToFirst() && c.getString(0) != null) return c.getString(0);
    } catch (RuntimeException e) {
      Log.w(TAG, "name: " + e);
    }
    String last = uri.getLastPathSegment();
    return last != null ? last.substring(last.lastIndexOf('/') + 1) : "pack";
  }

  // ---------------- converting ----------------

  private void start() {
    if (Job.state == Job.CONVERTING || Job.handle == 0 || Job.estimates == null) return;
    if (Build.VERSION.SDK_INT >= 33
        && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
      requestPermissions(new String[] {Manifest.permission.POST_NOTIFICATIONS}, REQUEST_NOTIFY);
    Job.state = Job.READY;
    if (volumes.isEmpty()) {
      // Android 7 to 9: Android's save dialog
      Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
          .setType(Output.MIME).putExtra(Intent.EXTRA_TITLE, "CubeEvil4-textures-" + Job.size + ".c4tex");
      try {
        startActivityForResult(i, REQUEST_SAVE);
      } catch (ActivityNotFoundException e) {
        Job.failure = e.toString();
        Job.state = Job.FAILED;
        render();
      }
      return;
    }
    ConvertService.start(this, volumes.get(chosenVolume()).name, null);
  }

  private long secondsLeft() {
    long elapsed = (SystemClock.elapsedRealtime() - Job.startedAt) / 1000;
    int done = Job.done, total = Job.total;
    if (done >= 20 && elapsed >= 30 && total > done) return elapsed * (total - done) / done;
    if (Job.estimates == null || Job.summary == null) return 0;
    long estimate = Estimates.seconds(Job.summary.bytes, Job.estimates[ConvertService.indexOf(Job.size)], units);
    return Math.max(60, estimate - elapsed);
  }

  private String failureText(String failure) {
    if (failure.equals("the disk is full")) failure = getString(R.string.packer_disk_full);
    return getString(R.string.packer_failed, failure);
  }

  private void importIntoCubeEvil4() {
    if (Job.result == null) return;
    Intent i = new Intent(CUBEEVIL4_IMPORT).setDataAndType(Job.result, Output.MIME)
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION).putExtra(Intent.EXTRA_TITLE, Job.resultName);
    try {
      startActivity(i);
    } catch (ActivityNotFoundException | SecurityException e) {
      Log.w(TAG, "CubeEvil4: " + e);
      new AlertDialog.Builder(this).setMessage(R.string.packer_no_cubeevil4)
          .setPositiveButton(android.R.string.ok, null).show();
    }
  }

  private void showReport() {
    TextView t = new TextView(this);
    t.setText(Job.report);
    t.setTypeface(Typeface.MONOSPACE);
    t.setTextSize(TypedValue.COMPLEX_UNIT_SP, REPORT_SP);
    t.setTextIsSelectable(true);
    t.setPadding(dp(16), dp(8), dp(16), dp(8));
    ScrollView s = new ScrollView(this);
    s.addView(t);
    new AlertDialog.Builder(this).setTitle(Job.reportName != null ? Job.reportName : getString(R.string.packer_report))
        .setView(s).setPositiveButton(android.R.string.ok, null).show();
  }

  // ---------------- numbers ----------------

  static String bytes(Context c, long b) {
    return Formatter.formatFileSize(c, b);
  }

  static String count(long n) {
    return NumberFormat.getIntegerInstance().format(n);
  }

  private String duration(long seconds) {
    long minutes = Estimates.roundedMinutes(seconds);
    return minutes < 60 ? getString(R.string.packer_minutes, (int) minutes)
        : getString(R.string.packer_hours, (int) (minutes / 60), (int) (minutes % 60));
  }
}
