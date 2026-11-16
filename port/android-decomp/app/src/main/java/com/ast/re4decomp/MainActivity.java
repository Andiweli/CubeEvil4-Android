package com.ast.re4decomp;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.os.Handler;
import android.os.Looper;
import android.view.*;
import android.widget.*;
import com.ast.re4decomp.disc.DiscInstaller;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

/** One action prepares the verified room bundle and opens gameplay. */
public final class MainActivity extends Activity implements ImportManager.Listener {
  /** Opened from the game (LauncherRestartActivity: "Import other discs"): the launcher stays, the game does not
   *  start at once with the discs there; "Start game" starts it. */
  static final String SHOW_LAUNCHER = "showLauncher";
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private final Handler main = new Handler(Looper.getMainLooper());
  private volatile boolean destroyed;
  private boolean loading, startupShown;
  /** The warning of an unofficial build while it shows (warnFirst). */
  private AlertDialog warning;
  /** Core flavor: the original game core runs the game (CoreActivity). */
  private boolean core;
  private ImportManager imports;
  private TextView status, discs, roomNote, disc1State, disc2State;
  private Button disc1, disc2, cancel, open, start;
  private LinearLayout importPanel, roomBrowser;
  private Spinner category, rooms, mode;
  private RoomCatalog catalog;
  private List<RoomCatalog.Entry> shown = Collections.emptyList();
  // the data folder of the player's choice (the flavour sideload, DataFolder): its line and buttons, the settings of
  // all files access opened (back in onResume), a move running
  private TextView folderPath;
  private Button folderChoose, folderReset;
  private boolean folderPermissionAsked, folderBusy;
  private static boolean folderNoted;  // the note of a folder that cannot be used: once a start
  private static final int REQUEST_TREE = 40, REQUEST_STORAGE = 41;
  @Override public void onCreate(Bundle state) {
    super.onCreate(state); startupShown=state!=null&&state.getBoolean("startupShown",false); imports = ((Re4Application)getApplication()).imports;
    if (getIntent().getBooleanExtra(SHOW_LAUNCHER, false)) startupShown = true;
    core = getResources().getBoolean(R.bool.core_mode);
    LinearLayout body = new LinearLayout(this); body.setOrientation(LinearLayout.VERTICAL);
    int p = dp(20); body.setPadding(p, core ? dp(12) : p, p, core ? dp(12) : p);
    if (core) {
      // the launcher of the core mode (project owner, 17.10.2026): title and author centred, a line,
      // Start game, then the disc import under a centred heading between lines (the controls left
      // the launcher, 27.10.2026; a swap of A/B and X/Y saved before stays in force)
      centered(body, getString(R.string.core_title), 28, true, 0);
      centered(body, getString(R.string.core_author), 15, false, dp(4));
      // under the name (CubeEvil4 since 18.10.2026): own discs needed, not the original's maker
      centered(body, getString(R.string.core_notice), 12, false, dp(2));
      line(body, dp(10));
      start = button(body, getString(R.string.core_start), this::startOriginalTitle);
      // the flavour sideload (1.0, DataFolder): where the data go, before the discs are imported
      if (DataFolder.enabled) dataFolderSection(body);
      heading(body, getString(R.string.core_import_heading));
    } else {
      // the variant "classic" (own game modules): its name and real version, so it is not taken for
      // CubeEvil4 (the title said 0.37.0 for every version until 19.10.2026)
      TextView title = new TextView(this); title.setText(getString(R.string.app_title) + " · " + versionName()); title.setTextSize(25); body.addView(title);
      TextView note = new TextView(this); note.setText(R.string.browser_notice);
      note.setPadding(0, dp(8), 0, dp(12)); body.addView(note);
      button(body,"Start game",this::startOriginalTitle);
    }
    // The room browser opens the app's own game modules; the core mode has none.
    Button browse = button(body, getString(R.string.launch_rooms), () -> {
      roomBrowser.setVisibility(roomBrowser.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE);
      if (roomBrowser.getVisibility() == View.VISIBLE) rooms.requestFocus();
    });
    if (core) browse.setVisibility(View.GONE);
    roomBrowser = new LinearLayout(this); roomBrowser.setOrientation(LinearLayout.VERTICAL); roomBrowser.setVisibility(View.GONE); body.addView(roomBrowser);
    category = spinner(roomBrowser, Arrays.asList(getString(R.string.category_campaign), getString(R.string.category_extras), getString(R.string.category_all)));
    rooms = spinner(roomBrowser, Collections.emptyList()); mode = spinner(roomBrowser, Collections.singletonList(getString(R.string.mode_explore)));
    roomNote = new TextView(this); roomNote.setPadding(0, dp(8), 0, dp(8)); roomBrowser.addView(roomNote);
    open = button(roomBrowser, getString(R.string.room_open_selected), this::openSelected);
    status = new TextView(this); status.setPadding(0, dp(8), 0, dp(8)); body.addView(status);
    // the menu's only entry in the core mode was the disc import, which its launcher shows anyway
    if (!core) button(body, getString(R.string.browser_menu), this::showMenu);
    importPanel = new LinearLayout(this); importPanel.setOrientation(LinearLayout.VERTICAL);
    importPanel.setVisibility(core ? View.VISIBLE : View.GONE); body.addView(importPanel);
    discs = new TextView(this); importPanel.addView(discs);
    if (core) {
      // the core launcher: each import button over the left half, its disc's state right of it
      discs.setVisibility(View.GONE); status.setGravity(Gravity.CENTER_HORIZONTAL);
      disc1State = new TextView(this); disc2State = new TextView(this);
      disc1 = halfButton(importPanel, getString(R.string.import_disc1), () -> choose(1), disc1State);
      disc2 = halfButton(importPanel, getString(R.string.import_disc2), () -> choose(2), disc2State);
      cancel = halfButton(importPanel, getString(R.string.cancel_import), imports::cancel, null);
    } else {
      disc1 = button(importPanel, getString(R.string.import_disc1), () -> choose(1));
      disc2 = button(importPanel, getString(R.string.import_disc2), () -> choose(2));
      cancel = button(importPanel, getString(R.string.cancel_import), imports::cancel);
    }
    try (Reader reader = new InputStreamReader(getAssets().open("rooms.tsv"), StandardCharsets.UTF_8)) { catalog = new RoomCatalog(reader); }
    catch (IOException e) { status.setText(e.getMessage()); }
    category.setOnItemSelectedListener(selected(this::fillRooms)); rooms.setOnItemSelectedListener(selected(this::roomChanged));
    mode.setOnItemSelectedListener(selected(this::updateOpenState)); fillRooms();
    ScrollView scroll = new ScrollView(this); scroll.addView(body); setContentView(scroll);
    findViewById(android.R.id.content).setOnApplyWindowInsetsListener((v, insets) -> {
      v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(), insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom()); return insets;
    });
  }
  private void startOriginalTitle(){if(warnFirst())return;if(!loading&&!imports.busy&&gameReady()){startupShown=true;Intent start=new Intent(this,core?CoreActivity.class:StartupActivity.class);if(core)start.putExtra("coreSettings",getIntent().getStringExtra("coreSettings"));startActivity(start);}else importPanel.setVisibility(View.VISIBLE);}
  @Override protected void onSaveInstanceState(Bundle state){state.putBoolean("startupShown",startupShown);super.onSaveInstanceState(state);}
  private int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
  private String versionName() {
    try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
    catch (android.content.pm.PackageManager.NameNotFoundException e) { return "?"; }
  }
  /** A centred line of text (the core launcher). */
  private TextView centered(LinearLayout body, String text, int sp, boolean bold, int top) {
    TextView t = new TextView(this); t.setText(text); t.setTextSize(sp); t.setGravity(Gravity.CENTER_HORIZONTAL);
    if (bold) t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
    t.setPadding(0, top, 0, 0); body.addView(t); return t;
  }
  /** A release build signed with another key than the project owner's (OfficialBuild, 1.0.0): the red warning at
   *  each start of the app, before the game starts (also the game's own start once the discs are there); confirmed,
   *  the start screen goes on as it would have (changed(): the game starts by itself when its discs are ready).
   *  True while it is due. */
  private boolean warnFirst() {
    if (!OfficialBuild.mustWarn(this)) return false;
    if (warning == null || !warning.isShowing()) {
      warning = UnofficialWarning.show(this, () -> { warning = null; if (!destroyed) changed(); });
    }
    return true;
  }
  /** A thin line in the text colour (the core launcher's separators). */
  private View rule(int color) {
    View v = new View(this); v.setBackgroundColor((color & 0x00FFFFFF) | 0x60000000); return v;
  }
  private void line(LinearLayout body, int margin) {
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, Math.max(1, dp(1)));
    lp.topMargin = margin; lp.bottomMargin = margin; body.addView(rule(new TextView(this).getCurrentTextColor()), lp);
  }
  /** A centred heading with a line on its left and right. */
  private void heading(LinearLayout body, String text) {
    LinearLayout row = new LinearLayout(this); row.setOrientation(LinearLayout.HORIZONTAL); row.setGravity(Gravity.CENTER_VERTICAL);
    TextView t = new TextView(this); t.setText(text); t.setTextSize(16); t.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
    t.setGravity(Gravity.CENTER); t.setPadding(dp(12), 0, dp(12), 0);
    LinearLayout.LayoutParams side = new LinearLayout.LayoutParams(0, Math.max(1, dp(1)), 1f);
    LinearLayout.LayoutParams mid = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    row.addView(rule(t.getCurrentTextColor()), side); row.addView(t, mid); row.addView(rule(t.getCurrentTextColor()), new LinearLayout.LayoutParams(side));
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    lp.topMargin = dp(14); lp.bottomMargin = dp(2); body.addView(row, lp);
  }
  /** A button over the left half of a row, `beside` (or nothing) in the right half. */
  private Button halfButton(LinearLayout body, String label, Runnable action, TextView beside) {
    LinearLayout row = new LinearLayout(this); row.setOrientation(LinearLayout.HORIZONTAL); row.setGravity(Gravity.CENTER_VERTICAL);
    Button b = new Button(this); b.setText(label); b.setOnClickListener(v -> action.run());
    row.addView(b, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
    View right = beside != null ? beside : new View(this);
    if (beside != null) beside.setPadding(dp(16), 0, 0, 0);
    row.addView(right, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
    body.addView(row); return b;
  }
  private Button button(LinearLayout body, String label, Runnable action) {
    Button b = new Button(this); b.setText(label); b.setOnClickListener(v -> action.run()); body.addView(b); return b;
  }
  private Spinner spinner(LinearLayout body, List<String> labels) {
    Spinner s = new Spinner(this); body.addView(s); setChoices(s, labels); return s;
  }
  private void setChoices(Spinner spinner, List<String> labels) {
    ArrayAdapter<String> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, labels);
    adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item); spinner.setAdapter(adapter);
  }
  private AdapterView.OnItemSelectedListener selected(Runnable action) {
    return new AdapterView.OnItemSelectedListener() {
      @Override public void onItemSelected(AdapterView<?> p, View v, int position, long id) { action.run(); }
      @Override public void onNothingSelected(AdapterView<?> p) { updateOpenState(); }
    };
  }
  private RoomCatalog.Entry selectedRoom() {
    int index = rooms.getSelectedItemPosition(); return index >= 0 && index < shown.size() ? shown.get(index) : null;
  }
  private void fillRooms() {
    if (catalog == null) { updateOpenState(); return; }
    String last = getPreferences(MODE_PRIVATE).getString("room", "r100");
    shown = catalog.filter(Math.max(0, category.getSelectedItemPosition())); List<String> labels = new ArrayList<>(); int selected = 0;
    for (int i = 0; i < shown.size(); ++i) { labels.add(shown.get(i).label()); if (shown.get(i).id.equals(last)) selected = i; }
    setChoices(rooms, labels); rooms.setSelection(selected); roomChanged();
  }
  private void roomChanged() {
    RoomCatalog.Entry entry = selectedRoom();
    setChoices(mode, entry != null && entry.combatTest() ? Arrays.asList(getString(R.string.mode_explore), getString(R.string.mode_combat_test))
        : Collections.singletonList(getString(entry != null && !entry.walkable() && entry.explorable() ? R.string.mode_scene_only : R.string.mode_explore)));
    updateOpenState();
  }
  /** The game may start: Disc 1, and in the core mode both discs (project owner, 17.10.2026). */
  private boolean gameReady() {
    // core mode: both discs of one release (port/docs/retail-eu-plan.md, retail-us-plan.md, retail-jp-plan.md:
    // debug build, European, US, Japanese or Korean discs)
    return discReady(1) && (!core || (discReady(2) && DiscInstaller.sameRelease(imports.root)));
  }
  /** "ready (European release)" ... in the core launcher, "missing" without the disc. */
  private String discState(int number) {
    if (!discReady(number)) return getString(R.string.missing);
    return getString(R.string.core_disc_ready, ImportManager.releaseName(DiscInstaller.release(new File(imports.root, "disc" + number))));
  }
  private boolean discReady(int number) {
    if (imports.root == null) return false;
    File disc = new File(imports.root, "disc" + number);
    // the core mounts the disc from sys/fst.bin: older imports must be repeated
    return core ? DiscInstaller.coreReady(disc, number) : DiscInstaller.ready(disc, number);
  }
  private void updateOpenState() {
    if (open == null) return;
    RoomCatalog.Entry entry = selectedRoom();
    boolean available = entry != null && entry.explorable() && discReady(1) && (discReady(entry.disc) ||
        (discReady(2) && new File(imports.root, "disc2/files/" + entry.path).isFile()));
    roomNote.setText(entry == null ? "" : !entry.explorable() ? getString(R.string.browser_room_unsupported)
        : !discReady(1) ? getString(R.string.browser_missing_disc, 1)
        : !available ? getString(R.string.browser_missing_disc, entry.disc)
        : !entry.walkable() ? getString(R.string.browser_room_dynamic_floor)
        : getString(R.string.browser_room_note, entry.area(), entry.id));
    open.setEnabled(available && !loading && !imports.busy); category.setEnabled(!loading && !imports.busy); rooms.setEnabled(!loading && !imports.busy);
    mode.setEnabled(!loading && !imports.busy && entry != null && entry.combatTest());
  }
  private void openSelected() {
    RoomCatalog.Entry entry = selectedRoom(); if (entry == null || !entry.explorable() || loading || imports.busy) return;
    openRoom(entry, mode.getSelectedItemPosition() != 1 || !entry.combatTest(), 0);
  }
  private void openTest(String id, boolean explore, int profile) {
    if (catalog == null || loading || imports.busy || !discReady(1)) return;
    for (RoomCatalog.Entry entry : catalog.filter(2)) if (id.equals(entry.id)) { openRoom(entry, explore, profile); return; }
    status.setText(getString(R.string.browser_load_error, "Test room is missing from the catalog"));
  }
  private void openRoom(RoomCatalog.Entry entry, final boolean explore, final int profile) {
    final File root = imports.root;
    loading = true; updateOpenState(); status.setText(getString(R.string.browser_loading, entry.id));
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); getPreferences(MODE_PRIVATE).edit().putString("room", entry.id).apply();
    worker.execute(() -> {
      File file = null; String error = null;
      try (Reader rows = new InputStreamReader(getAssets().open("rooms.tsv"), StandardCharsets.UTF_8);
           Reader blocks = new InputStreamReader(getAssets().open("blocks.tsv"), StandardCharsets.UTF_8)) {
        RoomRepository repository = new RoomRepository(root, getCacheDir(), rows, blocks,
            (source, output) -> NativeBridge.decodeRoom(source.getAbsolutePath(), output.getAbsolutePath()));
        file = repository.prepare(entry.id);
        if (Thread.currentThread().isInterrupted()) throw new IOException("Loading canceled");
        NativeBridge.inspectScene(file.getAbsolutePath());
      } catch (Exception e) { error = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage(); }
      final File prepared = file; final String failure = error;
      main.post(() -> {
        if (destroyed) return; loading = false; getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); changed();
        if (failure != null || prepared == null) { status.setText(getString(R.string.browser_load_error, failure)); return; }
        status.setText(""); startActivity(new Intent(this, ControllerActivity.class).putExtra("previewScene", prepared.getAbsolutePath())
            .putExtra("sceneRoom", entry.id).putExtra("exploreMode", explore).putExtra("testProfile", profile));
      });
    });
  }
  private void showMenu() {
    new AlertDialog.Builder(this).setTitle(R.string.browser_menu).setItems(new String[]{getString(R.string.browser_data), getString(R.string.browser_diagnostics)}, (dialog, which) -> {
      if (which == 0) { importPanel.setVisibility(importPanel.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE);
        if (importPanel.getVisibility() == View.VISIBLE) disc1.requestFocus(); }
      else new AlertDialog.Builder(this).setTitle(R.string.browser_diagnostics).setItems(new String[]{getString(R.string.open_test), getString(R.string.room_test)},
          (d, test) -> startActivity(new Intent(this, test == 0 ? ControllerActivity.class : RoomCheckActivity.class))).show();
    }).show();
  }
  private void choose(int disc) {
    if (loading) return; Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("*/*");
    i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION); startActivityForResult(i, disc);
  }
  @Override protected void onActivityResult(int request, int result, Intent data) {
    super.onActivityResult(request, result, data);
    if ((request == 1 || request == 2) && result == RESULT_OK && data != null && data.getData() != null) imports.start(data.getData(), request);
    if (request == REQUEST_TREE && result == RESULT_OK && data != null && data.getData() != null) folderChosen(data.getData());
  }
  @Override protected void onResume() {
    super.onResume(); imports.attach(this); warnFirst();
    // back from Android's settings of all files access (Android 11 and later): granted, the folder picker follows
    if (folderPermissionAsked) {
      folderPermissionAsked = false;
      if (DataFolder.hasPermission(this)) openTree(); else note(getString(R.string.data_folder_permission_denied));
    }
  }
  @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
    super.onRequestPermissionsResult(request, permissions, results);
    if (request != REQUEST_STORAGE) return;
    if (DataFolder.hasPermission(this)) openTree(); else note(getString(R.string.data_folder_permission_denied));
  }

  // ---- the data folder of the player's choice (the flavour sideload, 1.0; DataFolder) ----

  /** Its heading, the folder in use and the buttons "Choose folder" and "Restore default". */
  private void dataFolderSection(LinearLayout body) {
    heading(body, getString(R.string.data_folder_heading));
    folderPath = new TextView(this); folderPath.setGravity(Gravity.CENTER_HORIZONTAL); folderPath.setPadding(0, dp(2), 0, dp(2));
    body.addView(folderPath);
    LinearLayout row = new LinearLayout(this);
    folderChoose = new Button(this); folderChoose.setText(R.string.data_folder_choose); folderChoose.setOnClickListener(v -> chooseFolder());
    folderReset = new Button(this); folderReset.setText(R.string.data_folder_reset); folderReset.setOnClickListener(v -> change(null));
    row.addView(folderChoose, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
    row.addView(folderReset, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
    body.addView(row);
    showFolder();
    // the chosen folder cannot be used at this start (permission gone, SD card or USB drive missing): the app's own
    String why = AppStorage.fallback(this);
    if (why != null && !folderNoted) {
      folderNoted = true;
      note(getString(R.string.data_folder_unavailable, DataFolder.chosen(this), why));
    }
  }

  private void showFolder() {
    if (folderPath == null) return;
    String custom = DataFolder.chosen(this);
    File base = AppStorage.base(this);
    folderPath.setText(custom != null && base.getAbsolutePath().equals(custom) ? base.getAbsolutePath()
        : getString(R.string.data_folder_own, base.getAbsolutePath()));
    boolean idle = !imports.busy && !loading && !folderBusy;
    folderChoose.setEnabled(idle);
    folderReset.setEnabled(idle && custom != null);
  }

  /** "Choose folder": the storage permission first (explained), then the system's folder picker. */
  private void chooseFolder() {
    if (imports.busy || loading || folderBusy) return;
    if (DataFolder.hasPermission(this)) { openTree(); return; }
    new AlertDialog.Builder(this).setTitle(Build.VERSION.SDK_INT >= 30 ? R.string.data_folder_permission_title
            : R.string.data_folder_permission_title_storage)
        .setMessage(Build.VERSION.SDK_INT >= 30 ? R.string.data_folder_permission_all_files : R.string.data_folder_permission_storage)
        .setPositiveButton(android.R.string.ok, (d, w) -> askPermission())
        .setNegativeButton(android.R.string.cancel, null).show();
  }

  /** Android 11 and later: its settings of all files access for the app (else of all apps); Android 6 to 10: the
   *  storage permission. Checked again in onResume or onRequestPermissionsResult. */
  private void askPermission() {
    if (Build.VERSION.SDK_INT >= 30) {
      folderPermissionAsked = true;
      try {
        startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
      } catch (ActivityNotFoundException e) {
        try {
          startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        } catch (ActivityNotFoundException e2) {
          folderPermissionAsked = false;
          note(getString(R.string.data_folder_permission_denied));
        }
      }
    } else if (Build.VERSION.SDK_INT >= 23) {
      requestPermissions(new String[] {android.Manifest.permission.READ_EXTERNAL_STORAGE,
          android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQUEST_STORAGE);
    }
  }

  private void openTree() {
    try { startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_TREE); }
    catch (ActivityNotFoundException e) { note(getString(R.string.data_folder_no_picker)); }
  }

  /** The picker's folder as a real path, checked: the app's data go there (change), or a note why not. */
  private void folderChosen(Uri tree) {
    String primary = Environment.getExternalStorageDirectory().getAbsolutePath(), path;
    try { path = DataFolder.pathOfTree(tree.getAuthority(), DocumentsContract.getTreeDocumentId(tree), primary); }
    catch (IllegalArgumentException e) { path = null; }
    String[] checked = DataFolder.check(path, primary);
    if (checked[0] == null) {
      note(getString(DataFolder.ANDROID.equals(checked[1]) ? R.string.data_folder_android : R.string.data_folder_not_local));
      return;
    }
    File to = new File(checked[0]);
    String problem = DataFolder.prepare(to);
    if (problem != null) { note(getString(R.string.data_folder_not_usable, to.getAbsolutePath(), problem)); return; }
    change(to.getAbsolutePath());
  }

  /** The data folder becomes `path` (null: the app's own): its data moved along on request, then a new start. */
  private void change(String path) {
    if (imports.busy || loading || folderBusy) return;
    File from = AppStorage.base(this), to = path == null ? AppStorage.standard(this) : new File(path);
    if (from.getAbsolutePath().equals(to.getAbsolutePath())) {
      // (the chosen folder could not be used and the app's own is in use: only the choice goes)
      if (path == null && DataFolder.chosen(this) != null) commit(null);
      else note(getString(R.string.data_folder_same, to.getAbsolutePath()));
      return;
    }
    if (!DataFolder.present(to).isEmpty()) {
      new AlertDialog.Builder(this).setMessage(getString(R.string.data_folder_existing, to.getAbsolutePath()))
          .setPositiveButton(android.R.string.ok, (d, w) -> commit(path)).setNegativeButton(android.R.string.cancel, null).show();
      return;
    }
    if (DataFolder.present(from).isEmpty()) { commit(path); return; }
    new AlertDialog.Builder(this).setTitle(R.string.data_folder_move_title)
        .setMessage(getString(R.string.data_folder_move_text, from.getAbsolutePath(), to.getAbsolutePath()))
        // (Android's own "yes" and "no" read "OK" and "Cancel" on newer versions: the buttons say what they do)
        .setPositiveButton(R.string.data_folder_move_yes, (d, w) -> move(from, to, path))
        .setNegativeButton(R.string.data_folder_move_no, (d, w) -> commit(path))
        .setNeutralButton(android.R.string.cancel, null).show();
  }

  /** Moves the data (DataFolder.move) with its progress; done, the new folder is kept and the app starts again. */
  private void move(File from, File to, String path) {
    folderBusy = true; showFolder();
    setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LOCKED);
    getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    LinearLayout box = new LinearLayout(this); box.setOrientation(LinearLayout.VERTICAL); box.setPadding(dp(20), dp(8), dp(20), 0);
    ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal); bar.setMax(1000); box.addView(bar);
    TextView text = new TextView(this); box.addView(text);
    AlertDialog dialog = new AlertDialog.Builder(this).setTitle(R.string.data_folder_moving).setView(box).setCancelable(false).show();
    android.content.Context app = getApplicationContext();
    long[] shownAt = {0};
    worker.execute(() -> {
      String problem = DataFolder.move(from, to, (done, total) -> {
        long now = android.os.SystemClock.uptimeMillis();
        if (now - shownAt[0] < 200 && done < total) return;
        shownAt[0] = now;
        main.post(() -> {
          bar.setProgress(total > 0 ? (int) (done * 1000 / total) : 1000);
          text.setText(getString(R.string.data_folder_moved_of, DataFolder.megabytes(done), DataFolder.megabytes(total)));
        });
      });
      // (kept here, so a new start takes the new folder even should this screen be gone)
      if (problem == null) DataFolder.choose(app, path);
      main.post(() -> {
        if (destroyed) return;
        dialog.dismiss();
        folderBusy = false;
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (problem != null) { note(getString(R.string.data_folder_move_failed, problem, from.getAbsolutePath())); showFolder(); }
        else commit(path);
      });
    });
  }

  /** Keeps the choice and starts the app again (LauncherRestartActivity), so the imports, the core and the logs take
   *  the new folder from the start. */
  private void commit(String path) {
    DataFolder.choose(this, path);
    Toast.makeText(this, R.string.data_folder_restart, Toast.LENGTH_LONG).show();
    startActivity(new Intent(this, LauncherRestartActivity.class)
        .putExtra(LauncherRestartActivity.GAME_PID, android.os.Process.myPid()).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
  }

  private void note(String text) {
    if (destroyed) return;
    new AlertDialog.Builder(this).setMessage(text).setPositiveButton(android.R.string.ok, null).show();
  }
  @Override protected void onPause() { imports.detach(this); super.onPause(); }
  @Override public void changed() {
    // the core launcher shows the import prompt as its heading: the status line only for the rest
    if (!loading) status.setText(core && ImportManager.PROMPT.equals(imports.status) ? "" : imports.status);
    discs.setText(getString(R.string.disc_status, getString(discReady(1) ? R.string.ready : R.string.missing), getString(discReady(2) ? R.string.ready : R.string.missing)));
    disc1.setEnabled(imports.root != null && !imports.busy && !loading); disc2.setEnabled(imports.root != null && !imports.busy && !loading); cancel.setEnabled(imports.busy);
    if (core) {
      cancel.setVisibility(imports.busy ? View.VISIBLE : View.GONE);  // the launcher's two buttons, Cancel while importing
      disc1State.setText(getString(R.string.core_disc_state, 1, discState(1)));
      disc2State.setText(getString(R.string.core_disc_state, 2, discState(2)));
      // two discs of different releases: say so, whatever the last import said
      String mixed = imports.busy ? null : imports.mixedReleases();
      if (mixed != null && !loading) status.setText(mixed);
      // no empty line between the import heading and its buttons
      status.setVisibility(status.getText().length() == 0 ? View.GONE : View.VISIBLE);
    }
    if (!gameReady() || imports.busy) importPanel.setVisibility(View.VISIBLE);
    if (start != null) start.setEnabled(gameReady() && !imports.busy && !loading && !folderBusy);
    showFolder();
    if (imports.busy || loading) getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); else getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    updateOpenState();
    if(!startupShown&&gameReady()&&!imports.busy&&!loading)startOriginalTitle();
  }
  @Override public boolean dispatchKeyEvent(KeyEvent e) {
    if ((e.getSource() & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD &&
        (e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_A || e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_B))
      return super.dispatchKeyEvent(new KeyEvent(e.getDownTime(), e.getEventTime(), e.getAction(),
          e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_A ? KeyEvent.KEYCODE_DPAD_CENTER : KeyEvent.KEYCODE_BACK,
          e.getRepeatCount(), e.getMetaState(), e.getDeviceId(), e.getScanCode(), e.getFlags(), InputDevice.SOURCE_DPAD));
    return super.dispatchKeyEvent(e);
  }
  @Override protected void onDestroy() { destroyed = true; if (warning != null) warning.dismiss(); worker.shutdownNow(); main.removeCallbacksAndMessages(null); super.onDestroy(); }
}
