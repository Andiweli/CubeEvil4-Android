package com.ast.re4decomp;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
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
  private final ExecutorService worker = Executors.newSingleThreadExecutor();
  private final Handler main = new Handler(Looper.getMainLooper());
  private volatile boolean destroyed;
  private boolean loading, startupShown;
  /** Core flavor: the original game core runs the game (CoreActivity). */
  private boolean core;
  private ImportManager imports;
  private TextView status, discs, roomNote, disc1State, disc2State;
  private Button disc1, disc2, cancel, open, start;
  private LinearLayout importPanel, roomBrowser;
  private Spinner category, rooms, mode;
  private RoomCatalog catalog;
  private List<RoomCatalog.Entry> shown = Collections.emptyList();
  @Override public void onCreate(Bundle state) {
    super.onCreate(state); startupShown=state!=null&&state.getBoolean("startupShown",false); imports = ((Re4Application)getApplication()).imports;
    core = getResources().getBoolean(R.bool.core_mode);
    LinearLayout body = new LinearLayout(this); body.setOrientation(LinearLayout.VERTICAL);
    int p = dp(20); body.setPadding(p, core ? dp(12) : p, p, core ? dp(12) : p);
    if (core) {
      // the launcher of the core mode (project owner, 17.10.2026): title and author centred, a line,
      // Start game, then the controls and the disc import, each under a centred heading between lines
      centered(body, getString(R.string.core_title), 28, true, 0);
      centered(body, getString(R.string.core_author), 15, false, dp(4));
      // under the name (CubeEvil4 since 18.10.2026): own discs needed, not the original's maker
      centered(body, getString(R.string.core_notice), 12, false, dp(2));
      line(body, dp(10));
      start = button(body, getString(R.string.core_start), this::startOriginalTitle);
      heading(body, getString(R.string.core_controls_heading));
      // the controller of the core mode (CoreActivity, CoreInput): the layout and its one switch
      centered(body, getString(R.string.core_controls), 14, false, 0);
      final android.content.SharedPreferences input = getSharedPreferences(CoreActivity.INPUT_PREFS, MODE_PRIVATE);
      CheckBox swap = new CheckBox(this); swap.setText(R.string.core_swap_face);
      swap.setChecked(input.getBoolean(CoreActivity.SWAP_FACE, false));
      swap.setOnCheckedChangeListener((v, on) -> input.edit().putBoolean(CoreActivity.SWAP_FACE, on).apply());
      LinearLayout.LayoutParams middle = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
      middle.gravity = Gravity.CENTER_HORIZONTAL; middle.topMargin = dp(4); body.addView(swap, middle);
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
  private void startOriginalTitle(){if(!loading&&!imports.busy&&gameReady()){startupShown=true;Intent start=new Intent(this,core?CoreActivity.class:StartupActivity.class);if(core)start.putExtra("coreSettings",getIntent().getStringExtra("coreSettings"));startActivity(start);}else importPanel.setVisibility(View.VISIBLE);}
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
    // core mode: both discs of one release (port/docs/retail-eu-plan.md: debug build or European discs)
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
  }
  @Override protected void onResume() { super.onResume(); imports.attach(this); }
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
    if (start != null) start.setEnabled(gameReady() && !imports.busy && !loading);
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
  @Override protected void onDestroy() { destroyed = true; worker.shutdownNow(); main.removeCallbacksAndMessages(null); super.onDestroy(); }
}
