package com.ast.re4decomp;

import android.app.AlertDialog;
import android.app.Activity;
import android.app.Dialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.CheckBox;
import android.widget.SeekBar;
import android.widget.TextView;
import java.text.DateFormat;
import java.util.Date;
import java.util.Locale;

/** Five pages in one paused dialog. Existing preference keys and host callbacks stay intact.
 * Exclusive choices use selected buttons; independent functions use AN/AUS-style toggle buttons.
 * The original game captures/restores the separate instant slot on its own thread, via a worker.
 */
final class CoreMenu extends Dialog {
  interface Host {
    /** Picture options changed (the preferences hold them). */
    void graphicsChanged();
    void exportAll();
    /** "Export Player Data (.zip)" (0.99.15, PlayerBackup): the memory card's files, the touch layout, the instant save
     *  and the settings in one ZIP the player names. */
    void exportZip();
    void importFiles();
    /** "Delete All Data" (the menu asked first, 0.99.15, PlayerWipe): the game restarts and the memory card and the
     *  instant save are deleted before the core opens them. */
    void wipePlayerData();
    /** "Import other discs" (the menu asked first): the game ends and the start screen opens without starting it. */
    void importDiscs();
    /** "Import texture pack" (Cheats & Mods, TexturePacks): the file picker, then the copy into the app's textures
     *  folder on a thread of its own; `listener` hears its progress and its end on the main thread. */
    void importTexturePack(TexturePacks.Listener listener);
    /** The menu shown again while an import runs: `listener` hears the rest of it. */
    void textureListener(TexturePacks.Listener listener);
    /** Stops a running import (its copy is deleted, the pack before stays). */
    void cancelTexturePackImport();
    void shareReport();
    void saveReport();
    void logChanged(boolean on);
    void toolsChanged(boolean on);
    void fpsChanged(int place);  // CoreActivity.FPS_PLACE: 0 off, 1..4 where
    /** The setting "Touch Controls": TouchShowRule.AUTO, FORCE or SUPPRESS (the preferences hold it). */
    void touchModeChanged(int mode);
    /** The touch controls' look: TouchStyle.RE, GC, PS2 or XBOX (TouchControls keeps it). */
    int touchLook();
    /** The choice "Button style": a look, at once. */
    void touchLookChanged(int look);
    /** The switch "Use Gyro-Aiming" (the preferences hold it). */
    void gyroChanged(boolean on);
    /** "Gyro from" (1.1.0): GyroAim.SOURCE_AUTO / _DEVICE / _CONTROLLER (the preferences hold it). */
    void gyroSourceChanged(int source);
    /** The switch "Alternate Controls" (the preferences hold it). */
    void alternateChanged(boolean on);
    /** The button mapping of the controller or the keyboard (ActionMap.kind); the host keeps it. */
    void mappingChanged(ActionMap map);
    /** The button mapping in use for ActionMap.KIND_CONTROLLER or KIND_KEYBOARD. */
    ActionMap mapping(int kind);
    /** A controller's key as its label (CoreInput.faceLabel: "swap A/B and X/Y"). */
    int faceLabel(int keyCode);
    /** A physical game controller is connected (the button mapping can be used). */
    boolean controllerConnected();
    /** A keyboard or a mouse is connected (the button mapping can be used). */
    boolean keyboardConnected();
    /** The mouse's sensitivity (CoreActivity.MOUSE_MIN .. MOUSE_MAX). */
    float mouseSensitivity();
    /** The mouse's sensitivity slider (the preferences hold it). */
    void mouseSensitivityChanged(float value);
    /** The comfort control "Latch L and R", touch and controller (the preferences hold it). */
    void latchChanged(boolean on);
    /** The comfort control "Latch B", touch and controller (the preferences hold it). */
    void latchBChanged(boolean on);
    /** The choice "Touch Aiming": CoreInput.AIM_MOVE_STICK, AIM_LOOK or AIM_C_STICK (the preferences hold it). */
    void touchAimChanged(int mode);
    /** The slider of gyro aiming: the turn rates times `value` (the preferences hold it). */
    void gyroSensitivityChanged(float value);
    /** The switches "Invert vertical" (`vertical`) and "Invert horizontal" of gyro aiming (the preferences hold them). */
    void gyroInvertChanged(boolean vertical, boolean on);
    /** The switch "Knife by Swipe" (the preferences hold it). */
    void knifeSwipeChanged(boolean on);
    /** The switch "Invert vertical" of aiming by touch and controllers (the preferences hold it). */
    void aimInvertChanged(boolean on);
    /** The slider "Aim speed" of aiming by touch and controllers (the preferences hold it). */
    void aimSpeedChanged(float value);
    /** The sliders "Stick dead zone" and "Stick range" of the controller sticks in percent (CoreInput.setStickShape;
     *  the host keeps them). */
    void stickShapeChanged(int deadZone, int range);
    /** The choice "Vibration": RumbleTarget.AUTO, DEVICE, CONTROLLER or OFF (the host keeps it). */
    void vibrationChanged(int mode);
    /** "Auto aim": the game's own lock-on (CoreBridge.setAim). */
    void autoAimChanged(boolean on);
    /** "Always run": Leon runs without the run key, which walks instead (CoreBridge.setAim). */
    void alwaysRunChanged(boolean on);
    /** "Turn Leon with mouse / look area" (1.0): the movement sideways turns Leon himself (CoreInput.setBodyTurn). */
    void bodyTurnChanged(boolean on);
    /** "Turn speed" beside "Turn Leon with mouse / look area" (1.1.3; CoreActivity.BODY_TURN_SPEED holds it). */
    void bodyTurnSpeedChanged(float value);
    /** "Reload without aiming" (1.1.0, keyboard; CoreActivity.QUICK_RELOAD holds it). */
    void quickReloadChanged(boolean on);
    /** "Gentler QTEs" (0.99.15): Professional's quick-time events as Normal's (CoreBridge.setGentleQte). */
    void gentleQteChanged(boolean on);
    /** The cheat "Costumes · Leon's jacket" (1.0): Leon keeps his brown jacket (CoreBridge.setLeonJacket). */
    void leonJacketChanged(boolean on);
    /** The European discs are imported: the home page offers "Game language". */
    boolean gameLanguages();
    /** The debug build's discs are imported: SELECT held opens the game's debug menu (the button mapping says so). */
    boolean debugMenu();
    /** The game's language of a "Game language" choice (2..6; 0 the device's). */
    int gameLanguageOf(int choice);
    /** The benchmark (Display -> GRAPHICS, below "Dynamic"): the menu closes and the game measures. */
    void startBenchmark();
    /** The choice "Game language": 0 the device's, 2..6 (CoreActivity.GAME_LANGUAGE holds it); the
     *  game restarts when its language changes (the menu asked first). */
    void gameLanguageChanged(int choice);
    /** The cheat switches: bit 1 infinite health, 2 infinite ammo, 4 one-hit kill, 8 automatic QTEs, 16 the
     *  extra games kept unlocked (CoreActivity.CHEATS holds them). */
    void cheatsChanged(int bits);
    /** The cheat "Random enemies" (0.99.12): a new seed each time it goes on, 0 off (CoreActivity.ENEMY_SEED holds
     *  it). */
    void enemyRandomChanged(int seed);
    /** The cheat "Stack items" (1.1.0, GENERAL AND INVENTORY; CoreActivity.STACK_MAX holds it): 0 Standard, 250, 500, 999. */
    void stackItemsChanged(int max);
    /** "Enemy health bars" (1.1.0, ENEMIES AND WORLD; CoreActivity.HEALTH_BARS holds it). */
    void healthBarsChanged(boolean on);
    /** The cheats "All weapons at the merchant" (`weapons`) and "All tune-ups at the merchant" (1.1.3; CoreActivity
     *  MERCHANT_WEAPONS / MERCHANT_TUNES hold them). */
    void merchantChanged(boolean weapons, boolean on);
    /** The cheat "Bloody mode" (1.1.3, ENEMIES AND WORLD; CoreActivity.BLOODY holds it). */
    void bloodyChanged(boolean on);
    /** "More enemies" (1.1.0, ENEMIES AND WORLD): a new seed each time it goes on, 0 off (CoreActivity.MORE_ENEMIES). */
    void moreEnemiesChanged(int seed);
    /** "Photo mode" switched on and photo mode not on yet: the header's button starts it (1.1.0, the way on touch). */
    default boolean photoAvailable() { return false; }
    /** The header's "Photo mode": the menu closes, photo mode comes. */
    default void startPhotoMode() { }
    /** A cheat button (1 Killer7, 2 attache case L, 3 money, 4 extra games, 5..9 weapons), off the main thread:
     *  CoreBridge.cheatAction's answer. */
    int cheatAction(int what);
    /** The game restarts with its language as it is (the extra games; the menu asked first). */
    void restartGame();
    /** The renderer (port/docs/vulkan-bauplan.md): Vulkan can run here (Android 10, Vulkan 1.1). */
    boolean vulkanSupported();
    /** Vulkan is the menu's choice (it may not run: not supported, or it fell back to OpenGL ES). */
    boolean vulkanChosen();
    /** The menu's new choice: kept, and the app restarts into the game with it (the menu asked first). */
    void rendererChanged(boolean vulkan);
    /** Management, "Clear shader cache" (the menu asked first): emptied at the restart into the game (ShaderCache). */
    void clearShaderCache();
    /** Asks GitHub for the newest release (AppUpdate.check), once per run; `listener` hears it. */
    void checkUpdate(AppUpdate.Listener listener);
    /** The update button: the newest release's page on GitHub in the browser (AppUpdate.open, since 0.99.6). */
    void openUpdate();
    /** This run draws with Vulkan (the renderer the game started with, not only the menu's choice). */
    boolean vulkanRunning();
    /** The display's rate as measured (FramePacing), else as Android says; 0 unknown. */
    double displayHz();
    /** What the app knows of this device for the performance profiles (0.99.10, PerfProfile). */
    PerfProfile.Device perfDevice();
    /** A performance profile chosen on the start page (PerfProfile.CUSTOM .. HIGH): stored and applied. */
    void profileChosen(int profile);
    /** A switch the real-time shadows depend on changed (RealtimeShadows): their level anew. */
    void shadowsChanged();
  }

  private interface Changed<T> { void accept(T value); }
  // the pages, the main menu's buttons in this order (wish of the project owner, 30.10.2026)
  private static final int HOME = 0, DISPLAY = 1, AUDIO = 2, CONTROLS = 3, PERFORMANCE = 4, CHEATS = 5,
      MANAGEMENT = 6, CONTROLLER = 7;
  private static final long CHEAT_FLASH_MS = 1500;
  /** The story's player character in the cheat switches: its player type << 5 (runtime/cheats.cpp). */
  static final int CHARACTER_SHIFT = 5, CHARACTER_BITS = 7 << CHARACTER_SHIFT;
  /** The special costume of Leon and Ashley in the cheat switches (runtime/cheats.cpp). */
  static final int COSTUME_BIT = 256;
  /** "Invisibility" in the cheat switches (wish of the project owner, 05.11.2026; runtime/cheats.cpp). */
  static final int INVISIBLE_BIT = 512;
  /** "Random items" in the cheat switches (wish of the project owner, 07.11.2026, GitHub issue #161; runtime/cheats.cpp)
   *  and its seed in bits 11..30, a new one each time the switch goes on (the rooms dealt alike until then). */
  static final int RANDOM_ITEMS_BIT = 1024, SEED_SHIFT = 11, SEED_BITS = 0xFFFFF << SEED_SHIFT;
  /** "Steady aim" in the cheat switches (wish of the project owner, 16.11.2026: "Atemstopp beim Zielen"; bit 31,
   *  runtime/cheats.cpp, patch 0233): the laser sight and the scope do not sway. */
  static final int STEADY_AIM_BIT = 1 << 31;
  private static final int PANEL = 0xff191f1c, SURFACE = 0xff222a25, BORDER = 0xff414b41;
  static final int INK = 0xfff0eee5, MUTED = 0xffa6afa3, GOLD = 0xffd7bc7d, ACTIVE = 0xff45432e;
  private final Host host;
  private final SharedPreferences graphics, input;
  private boolean tools;
  private final int screenWidth, screenHeight;
  private final View hostDecor;
  private final LinearLayout root, body;
  private final ScrollView scroll;
  private final TextView title;
  private final Button back, resume, update;
  private static final int UPDATE_GREEN = 0xff2d5233, UPDATE_GREEN_EDGE = 0xff4f8a57;
  static final int UPDATE_RED = 0xff7a2a26, UPDATE_RED_EDGE = 0xffb2463f;
  // the warning before another player character (08.11.2026): a dark red under the text, the "Update" button's edge
  static final int WARN_RED = 0xff4e1f1c;
  // "Delete All Data" in a quiet red (wish of the project owner, 14.11.2026: "dezent rötlich"), as the Benchmark's green
  private static final int WIPE_RED = 0xff3a2321, WIPE_RED_EDGE = 0xff6e3c37, WIPE_TEXT = 0xffe2c7c2;
  // the Benchmark button: the green of "Up to date", quieter (wish of the project owner, 06.11.2026)
  private static final int BENCH_GREEN = 0xff223a27, BENCH_GREEN_EDGE = 0xff3b6343, BENCH_TEXT = 0xffc3d6c5;
  private final Handler main = new Handler(Looper.getMainLooper());
  private final int[] scrollY = new int[8];
  private final Button[] navigation = new Button[7];
  // the button mapping: the button of each action, the device shown, the action whose keys are being
  // pressed (-1: none) with a combination's first key (0 before it), what the last change did
  private Button[] bindingButtons;
  private Button controllerButton;
  private int mapKind = -1;
  private int captureAction = -1, captureFirst;
  private boolean captureL, captureR;  // the triggers past half way while waiting
  private TextView mapStatus;
  private String pendingStatus;
  private final Runnable captureTimeout = this::cancelCapture;
  private int page, requestSerial;
  private boolean working, slotPresent;
  private TextView graphicsLine, cardLine, slotDetails, slotDate, slotStatus, cheatStatus;
  // Cheats & Mods, HD textures (mods()): the pack's line, the import button (Cancel while it runs)
  private TextView hdStatus;
  private Button hdImport;
  private boolean updateAsked;  // the question after an update (PerfProfile.FIRST_ASK) shown in this menu
  private TextView versionLabel;  // "v0.99.18-rc" left of the update button (0.99.18); null without a versionName
  private int cheatBits;  // the cheat switches as the page sent them last (CoreActivity.CHEATS)
  private Button exportAll, exportZip, saveSlot, loadSlot;
  private final View.OnLayoutChangeListener windowLayout = (v, l, t, r, b, oldL, oldT, oldR, oldB) -> {
    if (r - l != oldR - oldL || b - t != oldB - oldT) resizeWindow();
  };

  CoreMenu(Context context, Host host, boolean tools, int screenWidth, int screenHeight) {
    super(context);
    this.host = host;
    this.tools = tools;
    this.screenWidth = screenWidth;
    this.screenHeight = screenHeight;
    hostDecor = context instanceof Activity ? ((Activity) context).getWindow().getDecorView() : null;
    graphics = context.getSharedPreferences(CoreActivity.GRAPHICS_PREFS, Context.MODE_PRIVATE);
    input = context.getSharedPreferences(CoreActivity.INPUT_PREFS, Context.MODE_PRIVATE);
    requestWindowFeature(android.view.Window.FEATURE_NO_TITLE);
    root = column();
    root.setPadding(dp(20), dp(16), dp(20), dp(12));
    root.setBackground(shape(PANEL, BORDER, 10));
    back = button(s(R.string.ingame_return), v -> goBack());
    title = text("", 22, true);
    resume = button(s(R.string.ingame_back), v -> close());
    // the update button left of "Back to the game" (wish of the project owner, 30.10.2026; AppUpdate): hidden
    // until GitHub answered
    update = button(s(R.string.ingame_update), v -> { if (host != null) host.openUpdate(); });
    update.setVisibility(View.GONE);
    // "Photo mode" left of them (1.1.0, the owner's second device test of 19.11.2026): with the option on, the way into
    // photo mode on touch (L3 and P on a controller and a keyboard); not while photo mode is on already
    Button photoStart = button(s(R.string.ingame_photo_start), v -> {
      if (host == null) return;
      dismiss();
      host.startPhotoMode();
    });
    photoStart.setVisibility(host != null && host.photoAvailable() ? View.VISIBLE : View.GONE);
    LinearLayout actions = row();
    LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
        ViewGroup.LayoutParams.WRAP_CONTENT);
    gap.setMarginEnd(dp(8));
    LinearLayout.LayoutParams gap2 = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
        ViewGroup.LayoutParams.WRAP_CONTENT);
    gap2.setMarginEnd(dp(8));
    actions.addView(photoStart, gap2); actions.addView(update, gap); actions.addView(resume);
    // the installed version left of them, small, with some room (the project owner, 15.11.2026, "wie in der OUYA
    // Version links vom Button"; until 0.99.17 in brackets after the start page's title): "v0.99.18-rc", also while the
    // update button is hidden (no internet); MenuLayout.Header places it (under the buttons where a narrow window
    // leaves no room beside them)
    String installed = version();
    if (installed != null) versionLabel = text("v" + installed, 14, false);
    root.addView(new MenuLayout.Header(context, back, title, versionLabel, actions), new LinearLayout.LayoutParams(-1, -2));
    separator(root);
    scroll = new ScrollView(context);
    scroll.setFillViewport(false);
    body = column();
    body.setPadding(0, dp(4), 0, dp(8));
    scroll.addView(body);
    root.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
    setContentView(root);
    setCanceledOnTouchOutside(false);
    showPage(HOME, false);
    if (host != null) host.checkUpdate(updates);
  }

  /** The update button's look: green "Up to date" (off), red "Update" (opens the release's page). */
  private final AppUpdate.Listener updates = this::showUpdate;

  void showUpdate(int state) {
    if (state == AppUpdate.UNKNOWN) { update.setVisibility(View.GONE); return; }
    boolean older = state == AppUpdate.OLDER;
    int fill = older ? UPDATE_RED : UPDATE_GREEN, stroke = older ? UPDATE_RED_EDGE : UPDATE_GREEN_EDGE;
    StateListDrawable look = new StateListDrawable();
    look.addState(new int[] {android.R.attr.state_focused}, focused());
    look.addState(new int[] {android.R.attr.state_pressed}, shape(fill, INK, 6));
    look.addState(new int[0], shape(fill, stroke, 6));
    update.setBackground(look);
    update.setTextColor(FocusLook.text(ColorStateList.valueOf(INK)));
    update.setText(s(older ? R.string.ingame_update : R.string.ingame_up_to_date));
    update.setEnabled(older);
    update.setFocusable(older);
    update.setVisibility(View.VISIBLE);
  }

  private String s(int id) { return getContext().getString(id); }
  private int dp(int n) { return Math.round(n * getContext().getResources().getDisplayMetrics().density); }
  private LinearLayout column() { LinearLayout v = new LinearLayout(getContext()); v.setOrientation(LinearLayout.VERTICAL); return v; }
  private LinearLayout row() { LinearLayout v = new LinearLayout(getContext()); v.setBaselineAligned(false); v.setGravity(Gravity.CENTER_VERTICAL); return v; }
  private TextView text(String label, int size, boolean bold) {
    TextView v = new TextView(getContext());
    v.setText(label); v.setTextSize(size); v.setTextColor(bold ? INK : MUTED);
    if (bold) v.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    return v;
  }
  private GradientDrawable shape(int fill, int stroke, int radius) {
    GradientDrawable d = new GradientDrawable(); d.setColor(fill); d.setCornerRadius(dp(radius));
    d.setStroke(dp(1), stroke); return d;
  }
  /** The controller's focus, the same golden look on every button and every phone (FocusLook, 0.99.11; before a
   *  white frame, which Android's dark mode for apps turned almost black on a Xiaomi). */
  private GradientDrawable focused() { return FocusLook.focused(getContext()); }
  /** A slider: the golden box of the focus around it while it has the focus (FocusLook). */
  private static void focusBox(SeekBar bar) { FocusLook.over(bar); }
  private void style(Button b) {
    StateListDrawable state = new StateListDrawable();
    // focused and selected: the focus's look with the selected one's golden text (the colours below)
    state.addState(new int[] {android.R.attr.state_focused}, focused());
    state.addState(new int[] {android.R.attr.state_pressed}, shape(ACTIVE, GOLD, 6));
    state.addState(new int[] {android.R.attr.state_selected}, shape(ACTIVE, GOLD, 6));
    state.addState(new int[0], shape(SURFACE, BORDER, 6));
    b.setBackground(state); b.setAllCaps(false); b.setTextSize(16); b.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    b.setTextColor(new ColorStateList(new int[][] {new int[] {-android.R.attr.state_enabled},
        new int[] {android.R.attr.state_selected}, new int[0]}, new int[] {0xff70796f, GOLD, INK}));
    b.setPadding(dp(12), dp(8), dp(12), dp(8)); b.setMinHeight(dp(48)); b.setMinimumHeight(dp(48));
    b.setMinWidth(0); b.setMinimumWidth(0); b.setId(View.generateViewId());
  }
  private Button button(String label, View.OnClickListener click) {
    Button b = new Button(getContext()); style(b); b.setText(label); b.setOnClickListener(click);
    return b;
  }
  private void separator(LinearLayout parent) {
    View v = new View(getContext()); v.setBackgroundColor(BORDER);
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
    lp.setMargins(0, dp(10), 0, dp(8)); parent.addView(v, lp);
  }
  /** A section's heading with a faint line to its right up to the end of the row, so the sections stand
   *  apart (wish of the project owner, 05.11.2026); the line is the separators' colour, centred on the text. */
  private void heading(int id) {
    LinearLayout line = new Heading(getContext());
    // the text and the line within the buttons' grid: the text starts where the buttons and the row labels
    // start, the line ends where the buttons end (wish of the project owner, 05.11.2026: one left edge)
    line.setPaddingRelative(dp(MenuLayout.GAP), dp(16), dp(MenuLayout.GAP), dp(8));
    TextView v = text(s(id).toUpperCase(Locale.getDefault()), 14, true); v.setTextColor(MUTED); v.setLetterSpacing(.08f);
    // a little air at its end: joined scripts (Arabic) measure the letter spacing a little short
    v.setPaddingRelative(0, 0, dp(3), 0);
    line.addView(v, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    View rule = new View(getContext()); rule.setBackgroundColor(BORDER);
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(1), 1);
    lp.setMarginStart(dp(12)); lp.gravity = Gravity.CENTER_VERTICAL;
    line.addView(rule, lp);
    body.addView(line);
  }
  /** A section heading's row (its text, then the line): the tests find the headings by it. */
  static final class Heading extends LinearLayout {
    Heading(Context context) { super(context); setBaselineAligned(false); setGravity(Gravity.CENTER_VERTICAL); }
    /** The heading's text as shown (upper case). */
    String title() { return ((TextView) getChildAt(0)).getText().toString(); }
  }
  private void help(int id) {
    // (1.1.0, the project owner's fourth menu round) on the pages with side labels under the option column
    if (sideLabels()) indentedHelp(s(id)); else help(body, id);
  }
  /** The pages laid out as DISPLAY (1.1.0, the project owner's fourth menu round, 21.11.2026: "alle Unterseiten ... mit
   *  den Beschriftungen links"): labels on the left beside their rows, the rows and helps in the option column. */
  private boolean sideLabels() {
    return page == DISPLAY || page == AUDIO || page == CONTROLS || page == PERFORMANCE || page == CHEATS;
  }
  /** A row without a side label: in the option column on those pages, else over the body's width. */
  private MenuLayout.Buttons plainRow() { return sideLabels() ? buttonsRow(labelledOptions(0)) : buttonsRow(body); }
  /** A view (a status line) in the option column, as the helps. */
  private void indentedView(View v) {
    MenuLayout.Columns outer = new MenuLayout.Columns(getContext(), 170, 640, true);
    outer.addView(new View(getContext()));
    outer.addView(v);
    body.addView(outer);
  }
  /** A line of text inside a side label's group, under its rows (as a help). */
  private TextView groupText(LinearLayout group, String label) {
    TextView t = text(label, 15, false); t.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(8));
    group.addView(t);
    return t;
  }
  private void help(LinearLayout parent, int id) {
    TextView t = text(s(id), 15, false); t.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(8)); parent.addView(t);
  }
  /**
   * An explanation on DISPLAY that starts below the buttons' left edge (wish of the project owner, 05.11.2026):
   * beside the rows' labels the option column (where the buttons stand, plus their margin), stacked on narrow
   * windows the buttons' margin.
   */
  private TextView indentedHelp(String label) {
    MenuLayout.Columns outer = new MenuLayout.Columns(getContext(), 170, 640, true);
    outer.addView(new View(getContext()));
    TextView t = text(label, 15, false); t.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(8));
    outer.addView(t);
    body.addView(outer);
    return t;
  }
  /** Row label as tall as one button row (48 dp minimum + the margins above and below), centred on the first row. */
  private TextView rowLabel(String label) {
    TextView caption = text(label, 16, true);
    caption.setPaddingRelative(dp(MenuLayout.GAP), 0, dp(8), 0); caption.setMinHeight(dp(48 + 2 * MenuLayout.GAP)); caption.setMinimumHeight(dp(48 + 2 * MenuLayout.GAP));
    caption.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
    return caption;
  }
  /** Align a stack of buttons with the choices' option column; label 0 leaves the caption empty. */
  private LinearLayout labelledOptions(int label) {
    LinearLayout outer = new MenuLayout.Columns(getContext(), 170, 640, label == 0);
    outer.setGravity(Gravity.TOP);
    if (label != 0) {
      outer.addView(rowLabel(s(label)));
    } else outer.addView(new View(getContext()));
    LinearLayout options = column();
    outer.addView(options);
    body.addView(outer);
    return options;
  }
  private MenuLayout.Buttons buttonsRow(LinearLayout parent) {
    MenuLayout.Buttons buttons = new MenuLayout.Buttons(getContext(), 2);
    parent.addView(buttons, new LinearLayout.LayoutParams(-1, -2)); return buttons;
  }
  private Button cellAction(MenuLayout.Buttons parent, String label, View.OnClickListener click) {
    Button b = button(label, click);
    // (1.1.0, the project owner's feedback of 21.11.2026) every cell of the grids as high as the arrow cells
    b.setMinHeight(dp(64)); b.setMinimumHeight(dp(64));
    parent.addButton(b); return b;
  }
  /** An arrow selector (ArrowSelector, 1.1.0, the project owner's design of 21.11.2026) in `parent`'s grid: its name
   *  (0: none, a choice under a heading of its own), the values' labels, the step shown, the default step (golden
   *  value when another is chosen); `changed` gets the new step. */
  private ArrowSelector selector(MenuLayout.Buttons parent, int name, String[] labels, int index, int def,
                                 ArrowSelector.Listener changed) {
    ArrowSelector a = new ArrowSelector(getContext(), name == 0 ? null : s(name), labels, index, def);
    style(a); a.finishLook(); a.setListener(changed);
    // lit as an "On" switch while the value is not the off one (the project owner, 21.11.2026: Off, None; Standard of
    // "Stack items" and Normal of the costumes, the cheats' off)
    String[] off = {s(R.string.ingame_off), s(R.string.ingame_view_very_short), s(R.string.ingame_particles_none),
        s(R.string.ingame_stack_standard), s(R.string.ingame_cheat_costume_normal)};
    for (int i = 0; i < labels.length; i++) for (String o : off) if (labels[i].equals(o)) a.setOffIndex(i);
    parent.addButton(a);
    return a;
  }
  /** A slider's row on a card (1.1.0, the project owner's design of 21.11.2026): a faint, half-transparent surface like a
   *  button's (not a button: it takes no focus and no press), so the row and its slider read as one; the margins of
   *  the buttons' grid. */
  /** A slider cell (SliderCell, 1.1.0, the project owner's feedback of 21.11.2026) in `parent`'s grid: its name, the steps
   *  0 .. `steps`, the one shown, the value's text and what a move by the player does. */
  private SliderCell sliderCell(MenuLayout.Buttons parent, int name, int steps, int progress,
                                java.util.function.IntFunction<String> text, java.util.function.IntConsumer moved) {
    SliderCell cell = new SliderCell(getContext(), s(name), cellLook());
    cell.setUp(steps, progress, text, moved);
    parent.addButton(cell);
    return cell;
  }
  /** A cell's look as the buttons': the focus's golden frame, else the surface with its border. */
  private StateListDrawable cellLook() {
    StateListDrawable state = new StateListDrawable();
    state.addState(new int[] {android.R.attr.state_focused}, focused());
    state.addState(new int[0], shape(SURFACE, BORDER, 6));
    return state;
  }
  /** The labels of string resources. */
  private String[] texts(int[] ids) {
    String[] t = new String[ids.length];
    for (int i = 0; i < ids.length; i++) t[i] = s(ids[i]);
    return t;
  }
  /** The step of `value` in `values` (0 when it is not there). */
  static int stepOf(int[] values, int value) {
    for (int i = 0; i < values.length; i++) if (values[i] == value) return i;
    return 0;
  }
  private void choices(int label, String[] labels, int[] values, int selected, int limit, Changed<Integer> changed) {
    choices(label, labels, values, selected, limit, labels.length, changed);
  }
  /** The row the next arrow selector of choices() goes into (1.1.0, the project owner's third menu round, 21.11.2026:
   *  two cells side by side), null: a row of its own. */
  private MenuLayout.Buttons into;
  /** At most `columns` buttons a row (fewer when narrow); further rows line up under the first. */
  private Button[] choices(int label, String[] labels, int[] values, int selected, int limit, int columns,
                           Changed<Integer> changed) {
    return choices(label, labels, values, selected, limit, columns, false, values[0], changed);
  }
  /**
   * Three options a row in quarters (wish of the project owner, 28.10.2026): the label takes the first
   * quarter and each option one, so two options side by side are as wide as one button of the rows of
   * two above them, and the rows keep the same gap as those rows.
   */
  private Button[] quarterChoices(int label, String[] labels, int[] values, int selected, Changed<Integer> changed) {
    return choices(label, labels, values, selected, Integer.MAX_VALUE, 3, true, values[0], changed);
  }
  /** More than two values (1.1.0): one arrow selector with the choice's name, half the row (on DISPLAY in the option
   *  column, beside the rows' labels, as its switches); steps above `limit` passed over. Two values: buttons as before.
   *  The selector comes back alone in the array (select() moves it). */
  private Button[] choices(int label, String[] labels, int[] values, int selected, int limit, int columns,
                           boolean quarters, int def, Changed<Integer> changed) {
    if (labels.length > 2) {
      MenuLayout.Buttons row = into != null ? into : plainRow();
      into = null;
      ArrowSelector a = selector(row, label, labels, stepOf(values, selected), stepOf(values, def),
          i -> changed.accept(values[i]));
      for (int i = 0; i < values.length; i++) a.setAllowed(i, limit >= values[i]);
      return new Button[] {a};
    }
    MenuLayout.Columns outer = new MenuLayout.Columns(getContext(), quarters ? 0 : 170, 640, false);
    if (quarters) outer.setWeights(1, 3);
    outer.addView(rowLabel(s(label)));
    // in quarters a long translation wraps onto two lines before the row loses a column
    MenuLayout.Buttons options = new MenuLayout.Buttons(getContext(), columns, quarters ? 2 : 1);
    outer.addView(options);
    Button[] buttons = new Button[labels.length];
    for (int i = 0; i < labels.length; i++) {
      final int index = i;
      Button b = cellAction(options, labels[i], v -> {
        select(buttons, labels, index);
        changed.accept(values[index]);
      });
      b.setSelected(values[i] == selected); b.setEnabled(limit >= values[i]);
      b.setContentDescription(labels[i] + ", " + s(b.isSelected() ? R.string.ingame_on : R.string.ingame_off));
      buttons[i] = b;
    }
    body.addView(outer);
    return buttons;
  }
  /**
   * Choices without a row label over the whole row, `columns` a row (each a half, a third or a quarter of the
   * menu's width; wishes of the project owner, 05.11.2026); a long translation wraps onto two lines before the
   * row loses a column.
   */
  private Button[] rowChoices(String[] labels, int[] values, int selected, int columns, Changed<Integer> changed) {
    if (labels.length > 2) {
      // (1.1.0) an arrow selector without a name (the heading above names it, or the side label: `into`)
      MenuLayout.Buttons row = into != null ? into : plainRow();
      into = null;
      ArrowSelector a = selector(row, 0, labels, stepOf(values, selected), 0, i -> changed.accept(values[i]));
      return new Button[] {a};
    }
    MenuLayout.Buttons row = into;
    into = null;
    if (row == null) {
      row = new MenuLayout.Buttons(getContext(), columns, 2);
      body.addView(row, new LinearLayout.LayoutParams(-1, -2));
    }
    Button[] buttons = new Button[labels.length];
    for (int i = 0; i < labels.length; i++) {
      final int index = i;
      Button b = cellAction(row, labels[i], v -> {
        select(buttons, labels, index);
        changed.accept(values[index]);
      });
      b.setSelected(values[i] == selected);
      b.setContentDescription(labels[i] + ", " + s(b.isSelected() ? R.string.ingame_on : R.string.ingame_off));
      buttons[i] = b;
    }
    return buttons;
  }
  private void select(Button[] buttons, String[] labels, int index) {
    if (buttons.length == 1 && buttons[0] instanceof ArrowSelector) { ((ArrowSelector) buttons[0]).setIndex(index); return; }
    for (int k = 0; k < buttons.length; k++) {
      buttons[k].setSelected(k == index);
      buttons[k].setContentDescription(labels[k] + ", " + s(k == index ? R.string.ingame_on : R.string.ingame_off));
    }
  }
  /**
   * The resolution as a piano (wish of the project owner, 27.10.2026): Dynamic and the whole levels
   * in a row, the in-between levels 1.5×, 2.5×, 3.5× below, each between the two it lies between.
   * Kept in halves ("scaleHalves").
   */
  private void resolutionChoices() {
    // (1.1.0) an arrow selector by level, Dynamic first (the default), the benchmark beside it
    int[] values = {0, 2, 3, 4, 5, 6, 7, 8};
    String[] labels = new String[values.length];
    for (int i = 0; i < values.length; i++) labels[i] = values[i] == 0 ? s(R.string.ingame_dynamic) : scaleLabel(values[i]);
    MenuLayout.Buttons row = into != null ? into : buttonsRow(labelledOptions(0));
    into = null;
    selector(row, R.string.ingame_resolution, labels, stepOf(values, scaleHalves(graphics)), 0, i -> {
      graphics.edit().putInt("scaleHalves", values[i]).apply(); host.graphicsChanged(); showGraphics();
    });
    // the benchmark beside it in a quiet green (wish of the project owner, 05./06.11.2026): the game goes to the
    // lake's dock (the project owner's save there, 06.11.2026) and measures which level holds 30 and 60 FPS with the
    // other options as they are; then the resolution becomes Dynamic, up to those levels
    row.addButton(benchButton());
  }
  /** "Import other discs": the game ends for it (progress not saved is lost), so the menu asks first. */
  private void askImportDiscs() {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_import_discs)
        .setMessage(R.string.ingame_import_discs_text)
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_start_screen, (d, w) -> host.importDiscs()).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    Immersive.show(dialog);
  }
  /** The device's RAM in bytes (0: unknown). */
  /** "Clear shader cache": the game restarts for it (progress not saved is lost), so the menu asks first. */
  private void askClearShaders() {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_shader_clear)
        .setMessage(R.string.ingame_shader_clear_text)
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_restart, (d, w) -> { close(); host.clearShaderCache(); }).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    Immersive.show(dialog);
  }
  /** "Start the benchmark?" with what it does; the menu closes for it (since 0.99.10 the benchmark with its suggestion,
   *  BenchmarkFlow). */
  private void askBenchmark() {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_benchmark)
        .setMessage(R.string.ingame_bench_text)
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_benchmark_start, (d, w) -> { close(); host.startBenchmark(); }).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    Immersive.show(dialog);
  }
  /** The chosen resolution in halves (0: Dynamic; 2: 1×, 3: 1.5× … 8: 4×); until 27.10.2026 "scale" in whole levels. */
  static int scaleHalves(SharedPreferences g) {
    if (g.contains("scaleHalves")) return g.getInt("scaleHalves", 0);
    int whole = g.getInt("scale", 0);
    return whole >= 1 && whole <= 4 ? 2 * whole : 0;
  }
  /** A level in halves as a number: "2", "1.5" (Locale.ROOT) or with the language's decimal separator. */
  static String scaleNumber(int halves, Locale locale) {
    if (halves % 2 == 0) return String.valueOf(halves / 2);
    char separator = locale == null ? '.' : java.text.DecimalFormatSymbols.getInstance(locale).getDecimalSeparator();
    return (halves / 2) + String.valueOf(separator) + "5";
  }
  private String scaleLabel(int halves) {
    return scaleNumber(halves, Locale.getDefault()) + "×";
  }
  /** graphicChoices() as a cell of `row` (beside another one). */
  private void graphicChoices(MenuLayout.Buttons row, int label, String key, String[] labels, int[] values, int fallback,
                              int limit) {
    into = row;
    graphicChoices(label, key, labels, values, fallback, limit);
    into = null;
  }
  private void graphicChoices(int label, String key, String[] labels, int[] values, int fallback, int limit) {
    choices(label, labels, values, graphics.getInt(key, fallback), limit, labels.length, false, fallback, value -> {
      graphics.edit().putInt(key, value).apply(); host.graphicsChanged(); showGraphics();
    });
  }
  private Button toggle(MenuLayout.Buttons parent, int label, boolean on, Changed<Boolean> changed) {
    Button b = cellAction(parent, "", v -> {
      Button pressed = (Button) v; boolean next = !pressed.isSelected();
      toggleLabel(pressed, label, next); changed.accept(next);
    });
    toggleLabel(b, label, on);
    b.setAccessibilityDelegate(new View.AccessibilityDelegate() {
      @Override public void onInitializeAccessibilityNodeInfo(View view, AccessibilityNodeInfo info) {
        super.onInitializeAccessibilityNodeInfo(view, info); info.setCheckable(true); info.setChecked(view.isSelected());
      }
    });
    return b;
  }
  private void fpsLabel(Button b, int place, boolean on) { valueLabel(b, R.string.ingame_info_hud, place, on); }
  /** The real-time shadows' value (RealtimeShadows.choice): Off, Characters, Characters and objects. */
  static int shadowsName(int choice) {
    return choice <= 0 ? R.string.ingame_off : choice >= 2 ? R.string.ingame_rt_shadows_objects
        : R.string.ingame_rt_shadows_figures;
  }
  /** Why the real-time shadows are not recommended on this device (RealtimeShadows.deviceCheck), for the note. */
  static int shadowsWhy(int check) {
    switch (check) {
      case RealtimeShadows.GPU_UNKNOWN: return R.string.ingame_rt_shadows_why_gpu_unknown;
      case RealtimeShadows.GPU_WEAK: return R.string.ingame_rt_shadows_why_gpu;
      case RealtimeShadows.NO_BENCHMARK: return R.string.ingame_rt_shadows_why_benchmark;
      case RealtimeShadows.BENCHMARK_LOW: return R.string.ingame_rt_shadows_why_benchmark_low;
      case RealtimeShadows.CPU_LIMIT: return R.string.ingame_rt_shadows_why_cpu;
      default: return R.string.ingame_rt_shadows_why_memory;
    }
  }
  /** "Name · value" on a button that moves through its values with each press, lit while not off. */
  private void valueLabel(Button b, int name, int value, boolean on) {
    b.setSelected(on);
    b.setText(twoLines(s(name), s(value)));
    b.setContentDescription(getContext().getString(R.string.ingame_toggle_value, s(name), s(value)));
  }
  /** "View distance · Normal" and its eyes after the text (the project owner, 15.11.2026): 4 Far, 3 Normal, 2 Medium,
   *  1 Short, a crossed-out eye None; lit but at None. The eyes are one piece after a space: a narrow button puts them
   *  on the next line, never cuts the text; TalkBack reads the text alone. */
  private void viewLabel(Button b, int level) {
    valueLabel(b, R.string.ingame_potato_view, CoreBridge.VIEW_LABEL[level], !CoreBridge.viewOff(level));
    String text = b.getText().toString();
    int eyes = CoreBridge.viewEyes(level);
    SpannableStringBuilder t = new SpannableStringBuilder(text).append(' ');
    int start = t.length(); t.append('\uFFFC');
    t.setSpan(new Eyes(getContext().getDrawable(eyes > 0 ? R.drawable.ic_eye : R.drawable.ic_eye_off), Math.max(1, eyes)),
        start, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    b.setText(t);
    b.setContentDescription(text);
  }
  /** `count` icons side by side in the text (CoreMenu.viewLabel): Lucide's "eye" / "eye-off" (res/drawable/ic_eye*.xml,
   *  ISC, app/THIRD_PARTY_NOTICES.md), each as high as the text's letters with a little room, drawn in the paint's
   *  colour, so they follow the button's state as its text does. Identical icons: the same in either direction. */
  static final class Eyes extends android.text.style.ReplacementSpan {
    private final android.graphics.drawable.Drawable icon;
    final int count;

    Eyes(android.graphics.drawable.Drawable icon, int count) {
      this.icon = icon.mutate();
      this.count = count;
    }
    private static int size(android.graphics.Paint paint) { return Math.round(paint.getTextSize() * 1.15f); }
    private static int gap(android.graphics.Paint paint) { return Math.round(paint.getTextSize() * .25f); }

    @Override public int getSize(android.graphics.Paint paint, CharSequence text, int start, int end,
                                 android.graphics.Paint.FontMetricsInt fm) {
      if (fm != null) {
        android.graphics.Paint.FontMetricsInt m = paint.getFontMetricsInt();
        fm.ascent = m.ascent; fm.descent = m.descent; fm.top = m.top; fm.bottom = m.bottom; fm.leading = m.leading;
      }
      return count * size(paint) + (count - 1) * gap(paint);
    }

    @Override public void draw(android.graphics.Canvas canvas, CharSequence text, int start, int end, float x, int top,
                               int y, int bottom, android.graphics.Paint paint) {
      int size = size(paint), gap = gap(paint);
      android.graphics.Paint.FontMetricsInt m = paint.getFontMetricsInt();
      int iconTop = y + (m.ascent + m.descent) / 2 - size / 2;  // centred on the letters' middle
      icon.setTint(paint.getColor());
      for (int i = 0; i < count; i++) {
        int left = Math.round(x) + i * (size + gap);
        icon.setBounds(left, iconTop, left + size, iconTop + size);
        icon.draw(canvas);
      }
    }
  }
  /**
   * Frame skip (0.99.8, one button; wish of the project owner, 10.11.2026): 0 off, 1 Normal (the renderer leaves out a
   * picture while it is behind, CoreBridge.setFrameSkip), 2 Aggressive (the game itself, CoreBridge.setCpuSkip), since
   * 1.1.0 3 Auto (both, only while the game stays under 30, CoreBridge.setFrameSkipAuto; the project owner's decision
   * of 20.11.2026). Before it two switches, "frameskip" and "cpuSkip"; both on: Aggressive.
   */
  static final String FRAME_SKIP = "frameSkipMode";
  /** The button's order: Off > Auto > Normal > Aggressive (the stored values stay 0, 3, 1, 2). */
  static final int[] SKIP_ORDER = {0, 3, 1, 2};
  /** The label of each stored value. */
  static final int[] SKIP_LABELS = {R.string.ingame_off, R.string.ingame_skip_normal, R.string.ingame_skip_aggressive,
      R.string.ingame_skip_auto};
  /** The button's next value after `now` (SKIP_ORDER). */
  static int nextFrameSkip(int now) {
    int at = 0;
    for (int i = 0; i < SKIP_ORDER.length; i++) if (SKIP_ORDER[i] == now) at = i;
    return SKIP_ORDER[(at + 1) % SKIP_ORDER.length];
  }
  /** The frame skip `mode` to the core (with 60 FPS on the caller sends 0). */
  static void sendFrameSkip(int mode) {
    CoreBridge.setFrameSkip(mode == 1);
    CoreBridge.setCpuSkip(mode == 2);
    CoreBridge.setFrameSkipAuto(mode == 3);
  }
  /** "Even frame pacing" (1.1.0, Performance; on by default; CoreBridge.setEvenPacing). */
  static final String EVEN_PACING = "evenPacing";
  static boolean evenPacing(SharedPreferences g) { return g.getBoolean(EVEN_PACING, true); }
  static int frameSkipMode(SharedPreferences g) {
    if (g.contains(FRAME_SKIP)) return Math.max(0, Math.min(3, g.getInt(FRAME_SKIP, 0)));
    if (g.getBoolean("cpuSkip", false)) return 2;
    return g.getBoolean("frameskip", false) ? 1 : 0;
  }
  /** Frame skip stored as `mode` (frameSkipMode), the two switches of before gone. */
  static void storeFrameSkipMode(SharedPreferences g, int mode) {
    g.edit().putInt(FRAME_SKIP, Math.max(0, Math.min(3, mode))).remove("frameskip").remove("cpuSkip").apply();
  }
  /** The depth of field's level (0 off, 1 light, 2 strong); until 0.98.3 the switch "dof" (on: light). */
  static final String DOF_LEVEL = "dofLevel";
  /** The benchmark's levels on this device (halves): "Dynamic" goes up to them with 30 / 60 FPS. */
  static final String DYN_CAP_30 = "dynamicCap30", DYN_CAP_60 = "dynamicCap60";
  static int dofLevel(SharedPreferences g) {
    if (g.contains(DOF_LEVEL)) return Math.max(0, Math.min(2, g.getInt(DOF_LEVEL, 1)));
    return g.getBoolean("dof", true) ? 1 : 0;
  }
  /** "Sharper menu text" (1.0.0): the game's own menu words drawn sharper at high resolutions; on by default. */
  static final String SHARPER_TEXT = "sharperText";
  static boolean sharperText(SharedPreferences g) {
    return g.getBoolean(SHARPER_TEXT, true);
  }
  /** The upscaler (1.1.0, gx_upscale.h): 0 off (the default), 1 SGSR, 2 FSR. */
  static final String UPSCALER = "upscaler";
  static int upscaler(SharedPreferences g) {
    return Math.max(0, Math.min(2, g.getInt(UPSCALER, 0)));
  }
  /** The filter "Sharpen" (0.99.2): 0 off (the default), 1 light, 2 strong. */
  static final String SHARPEN_LEVEL = "sharpenLevel";
  static int sharpenLevel(SharedPreferences g) {
    return Math.max(0, Math.min(2, g.getInt(SHARPEN_LEVEL, 0)));
  }
  /** The filter "CRT" (0.99.9, wish of the project owner, 11.11.2026): 0 off (the default), 1 light, 2 strong. */
  /** Bloom (0.99.13): 0 off (the default), 1 light, 2 strong (CoreBridge.setBloom; off while a Potato mode switch
   *  is on: CoreBridge.bloomSent). */
  static final String BLOOM_LEVEL = "bloomLevel";
  static int bloomLevel(SharedPreferences g) {
    return Math.max(0, Math.min(2, g.getInt(BLOOM_LEVEL, 0)));
  }
  static final String CRT_LEVEL = "crtLevel";
  static int crtLevel(SharedPreferences g) {
    return Math.max(0, Math.min(2, g.getInt(CRT_LEVEL, 0)));
  }
  /**
   * The row "Colour" (0.99.9, wish of the project owner, 11.11.2026): 0 off (the default), 1 warm film, 2 survival
   * horror; its strength 1 light (the default), 2 strong.
   */
  static final String COLOUR_MODE = "colourMode", COLOUR_LEVEL = "colourLevel";
  static int colourMode(SharedPreferences g) {
    return Math.max(0, Math.min(2, g.getInt(COLOUR_MODE, 0)));
  }
  static int colourLevel(SharedPreferences g) {
    return g.getInt(COLOUR_LEVEL, 1) >= 2 ? 2 : 1;
  }
  private void toggleLabel(Button b, int label, boolean on) {
    b.setSelected(on);
    b.setText(twoLines(s(label), s(on ? R.string.ingame_on : R.string.ingame_off)));
    b.setContentDescription(getContext().getString(R.string.ingame_toggle_value, s(label),
        s(on ? R.string.ingame_on : R.string.ingame_off)));
  }
  /** A switch's text since 1.1.0 (the project owner's feedback of 21.11.2026), as the arrow cells': the name small on
   *  top, the value big below. */
  private CharSequence twoLines(String name, String value) {
    SpannableStringBuilder t = new SpannableStringBuilder(name);
    t.setSpan(new RelativeSizeSpan(.78f), 0, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    t.setSpan(new StyleSpan(Typeface.NORMAL), 0, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    t.append('\n');
    int from = t.length();
    t.append(value);
    t.setSpan(new StyleSpan(Typeface.BOLD), from, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    return t;
  }

  private void showPage(int next, boolean focus) {
    if (working) return;
    scrollY[page] = scroll.getScrollY(); page = next; ++requestSerial;
    body.removeAllViews(); graphicsLine = null; cardLine = null; exportAll = null; cheatStatus = null;
    hdStatus = null; hdImport = null;
    bindingButtons = null; controllerButton = null; mapStatus = null;
    if (next != CONTROLLER) { captureAction = -1; main.removeCallbacks(captureTimeout); }
    back.setVisibility(page == HOME ? View.GONE : View.VISIBLE);
    int[] titles = {R.string.ingame_title, R.string.ingame_display, R.string.ingame_audio,
        R.string.ingame_controls_page, R.string.ingame_performance, R.string.ingame_cheats,
        R.string.ingame_management, R.string.ingame_controller_config};
    // only the page's name; the installed version is left of the update button since 0.99.18 (versionLabel); the main
    // page with the app's icon before "CubeEvil4 Options" (1.1.0, the project owner, 21.11.2026: "App Icon und 'CubeEvil4
    // Optionen' nur auf der Hauptseite, bei den Subseiten bleiben die bestehenden Titel")
    title.setText(s(titles[page]));
    if (page == HOME) {
      android.graphics.drawable.Drawable icon = getContext().getDrawable(R.drawable.icon_cubeevil4);
      if (icon != null) icon.setBounds(0, 0, dp(30), dp(30));
      // (absolute sides by the language's direction: the relative ones stay unresolved on a title laid out before)
      boolean rtl = getContext().getResources().getConfiguration().getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
      title.setCompoundDrawables(rtl ? null : icon, null, rtl ? icon : null, null);
      title.setCompoundDrawablePadding(dp(12));
    } else {
      title.setCompoundDrawables(null, null, null, null);
    }
    if (page == HOME) home(); else if (page == DISPLAY) display(); else if (page == AUDIO) audio();
    else if (page == CONTROLS) controls(); else if (page == PERFORMANCE) performance();
    else if (page == CHEATS) cheats(); else if (page == CONTROLLER) controller();
    else management();
    separator(body);
    TextView footer = text(s(R.string.ingame_navigation), 14, false);
    footer.setPadding(dp(MenuLayout.GAP), dp(8), dp(MenuLayout.GAP), 0); body.addView(footer);
    main.post(() -> {
      scroll.scrollTo(0, scrollY[page]);
      if (focus) { if (page == HOME) resume.requestFocus(); else back.requestFocus(); }
    });
  }
  private void goBack() {
    if (working) return;
    if (page == HOME) close();
    else if (page == CONTROLLER) {
      // back to the controls, on the button that opened it
      showPage(CONTROLS, false);
      main.post(() -> { if (controllerButton != null) controllerButton.requestFocus(); });
    } else {
      int from = page; showPage(HOME, false);
      main.post(() -> { if (navigation[from] != null) navigation[from].requestFocus(); });
    }
  }
  private void close() { if (!working) dismiss(); }
  @Override public void onBackPressed() { goBack(); }

  @android.annotation.SuppressLint("RtlHardcoded") // requested physical left/right slot metadata
  private void home() {
    heading(R.string.ingame_instant);
    MenuLayout.Columns slot = new MenuLayout.Columns(getContext(), 0, 620, false);
    slot.setFillEndHeight(true);
    // with the buttons' margin above and below: the metadata's 12 dp inset
    slot.setPadding(0, dp(12 - MenuLayout.GAP), 0, dp(12 - MenuLayout.GAP));
    slot.setGravity(Gravity.CENTER_VERTICAL);
    slot.setBackground(shape(0xff202720, BORDER, 6));
    LinearLayout metadata = column(); metadata.setPadding(dp(12), dp(4), dp(12), dp(4));
    metadata.addView(text(s(R.string.ingame_slot), 18, true));
    slotDetails = text("", 15, false); slotDate = text("", 15, false);
    // Metadata stays physically left aligned; Arabic text still uses its locale.
    slotDetails.setGravity(Gravity.LEFT); slotDate.setGravity(Gravity.LEFT);
    slot.setLayoutDirection(View.LAYOUT_DIRECTION_LTR);
    // Keep the requested physical columns; text itself still follows the selected locale.
    metadata.setLayoutDirection(getContext().getResources().getConfiguration().getLayoutDirection());
    metadata.addView(slotDetails); metadata.addView(slotDate); slot.addView(metadata);
    MenuLayout.Buttons actions = new MenuLayout.Buttons(getContext(), 2);
    // The button's outer margin (MenuLayout.GAP) plus this match the metadata's 12 dp left inset.
    actions.setPadding(0, 0, dp(12 - MenuLayout.GAP), 0);
    actions.setLayoutDirection(getContext().getResources().getConfiguration().getLayoutDirection());
    saveSlot = cellAction(actions, s(R.string.ingame_instant_save), v -> confirmInstant(1)); saveSlot.setSelected(true);
    loadSlot = cellAction(actions, s(R.string.ingame_instant_load), v -> confirmInstant(2));
    saveSlot.setEnabled(false); loadSlot.setEnabled(false); slot.addView(actions);
    // the frame as wide as the buttons' grid below it (wish of the project owner, 05.11.2026: it stuck out)
    LinearLayout.LayoutParams frame = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT);
    frame.setMargins(dp(MenuLayout.GAP), dp(MenuLayout.GAP), dp(MenuLayout.GAP), dp(MenuLayout.GAP));
    body.addView(slot, frame);
    slotStatus = text(s(R.string.ingame_instant_checking), 15, false);
    slotStatus.setPadding(dp(MenuLayout.GAP), dp(6), dp(MenuLayout.GAP), dp(6));
    slotStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE); body.addView(slotStatus);
    profileRow();
    heading(R.string.ingame_settings);
    MenuLayout.Buttons first = buttonsRow(body), second = buttonsRow(body), third = buttonsRow(body);
    navigation[DISPLAY] = nav(first, R.string.ingame_display, R.string.ingame_display_summary, DISPLAY);
    navigation[AUDIO] = nav(first, R.string.ingame_audio, R.string.ingame_audio_summary, AUDIO);
    navigation[CONTROLS] = nav(second, R.string.ingame_controls_page, R.string.ingame_controls_summary, CONTROLS);
    navigation[PERFORMANCE] = nav(second, R.string.ingame_performance, R.string.ingame_performance_summary, PERFORMANCE);
    navigation[CHEATS] = nav(third, R.string.ingame_cheats, R.string.ingame_cheats_summary, CHEATS);
    navigation[MANAGEMENT] = nav(third, R.string.ingame_management, R.string.ingame_management_summary, MANAGEMENT);
    if (host != null && host.gameLanguages()) gameLanguage();
    // "Source and updates" at the bottom of the main page too (1.1.0, the project owner, 20.11.2026: "Baue bitte den
    // Abschnitt QUELLE UND UPDATES auch auf der Hauptseite des Ingame Menüs unten ein")
    sourceSection();
    main.post(() -> { if (isShowing() && page == HOME && !working) refreshInstant(); });
    // an update to 0.99.10: the menu asks once whether to measure now (decision of the project owner, 11.11.2026)
    if (host != null && !updateAsked && graphics.getInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE) == PerfProfile.FIRST_ASK) {
      updateAsked = true;
      main.post(this::askMeasureAfterUpdate);
    }
  }

  /** "Performance profile" between the instant save and the settings (wish of the project owner, 11.11.2026;
   *  port/docs/leistungsprofil-plan.md, PerfProfile): Custom, Low, Medium, High and the green benchmark button in one
   *  row (the project owner after the Retroid test, 11.11.2026), the active profile lit, the benchmark's suggestion
   *  marked; under them the last measurement and the help over the whole width. A hand change of a profile's setting
   *  makes Custom active (PerfProfile.sync, each time the start page is shown). */
  private void profileRow() {
    heading(R.string.ingame_profile);
    final int active = PerfProfile.sync(graphics);
    PerfProfile.Device d = host != null ? host.perfDevice() : null;
    if (d == null) d = new PerfProfile.Device("", 0, 0, 0, 0, 0, 1, 1);
    final PerfProfile.Bench b = PerfProfile.Bench.of(graphics);
    final int recommended = PerfProfile.recommend(d, b);
    final String[] names = {s(R.string.ingame_profile_custom), s(R.string.ingame_profile_low),
        s(R.string.ingame_profile_medium), s(R.string.ingame_profile_high)};
    // five a row, a name over two lines at most; a narrow window or a big font puts the rest in a second row
    MenuLayout.Buttons row = new MenuLayout.Buttons(getContext(), 5, 2);
    body.addView(row, new LinearLayout.LayoutParams(-1, -2));
    final Button[] buttons = new Button[names.length];
    for (int i = 0; i < names.length; i++) {
      final int profile = i;
      Button btn = cellAction(row, names[i], v -> {
        if (host == null) return;
        host.profileChosen(profile);
        select(buttons, names, profile);
      });
      if (i == recommended && i != PerfProfile.CUSTOM) {
        // the suggestion: a smaller second line under the name, as the costume switch's note
        SpannableStringBuilder t = new SpannableStringBuilder(names[i]);
        int from = t.length(); t.append('\n').append(s(R.string.ingame_profile_recommended));
        t.setSpan(new RelativeSizeSpan(.75f), from, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        t.setSpan(new StyleSpan(Typeface.NORMAL), from, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        btn.setText(t);
        btn.setPadding(dp(8), dp(2), dp(8), dp(2));
      }
      btn.setSelected(i == active);
      btn.setContentDescription(names[i] + ", " + s(i == active ? R.string.ingame_on : R.string.ingame_off));
      buttons[i] = btn;
    }
    row.addButton(benchButton());
    // the last measurement over the whole width, as the help under it
    TextView measured = text(b.measured ? getContext().getString(R.string.ingame_profile_measured,
        PerfProfile.level(b.cap30), PerfProfile.level(b.cap60), s(BenchmarkFlow.limitText(d, b)))
        : s(R.string.ingame_profile_not_measured), 15, false);
    measured.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(2));
    body.addView(measured, new LinearLayout.LayoutParams(-1, -2));
    help(R.string.ingame_profile_help);
  }

  /** The benchmark's button in a quiet green (wish of the project owner, 06.11.2026). */
  private Button benchButton() {
    Button bench = button(s(R.string.ingame_benchmark), v -> askBenchmark());
    StateListDrawable quiet = new StateListDrawable();
    quiet.addState(new int[] {android.R.attr.state_focused}, focused());
    quiet.addState(new int[] {android.R.attr.state_pressed}, shape(BENCH_GREEN, UPDATE_GREEN_EDGE, 6));
    quiet.addState(new int[0], shape(BENCH_GREEN, BENCH_GREEN_EDGE, 6));
    bench.setBackground(quiet);
    bench.setTextColor(FocusLook.text(ColorStateList.valueOf(BENCH_TEXT)));
    bench.setTypeface(Typeface.DEFAULT, Typeface.NORMAL);
    bench.setEnabled(host != null);
    return bench;
  }

  /** After an update to 0.99.10, once: measure now (the menu closes, the benchmark starts) or later. */
  private void askMeasureAfterUpdate() {
    if (!isShowing() || working) return;
    Runnable done = () -> graphics.edit().putInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE).apply();
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_bench_update)
        .setMessage(R.string.ingame_bench_update_text)
        .setNegativeButton(R.string.ingame_later, (d, w) -> done.run())
        .setPositiveButton(R.string.ingame_bench_now, (d, w) -> { done.run(); close(); host.startBenchmark(); }).create();
    dialog.setOnCancelListener(d -> done.run());
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).requestFocus());
    Immersive.show(dialog);
  }
  /** "Game language" with the European discs: the languages each in its own language, three a row; a
   *  new language restarts the game after a question, "Cancel" keeps the one before (decisions of the
   *  project owner, 27.10.2026). */
  private void gameLanguage() {
    heading(R.string.ingame_game_language);
    final String[] labels = {s(R.string.ingame_language_device), "English", "Deutsch", "Français", "Español", "Italiano"};
    final int[] values = {0, 2, 3, 4, 5, 6};
    final int[] current = {getContext().getSharedPreferences(CoreActivity.GAME_PREFS, Context.MODE_PRIVATE)
        .getInt(CoreActivity.GAME_LANGUAGE, 0)};
    final Button[][] buttons = new Button[1][];
    // without a row label under its heading, a third of the row each (wish of the project owner, 05.11.2026)
    // (1.1.0, the project owner, 21.11.2026) ‹ / › and L1 / R1 only show a language, the middle or A takes it (a restart)
    buttons[0] = rowChoices(labels, values, current[0], 3, value -> {
      if (value == current[0]) return;
      if (host.gameLanguageOf(value) == host.gameLanguageOf(current[0])) {
        current[0] = value; host.gameLanguageChanged(value); return;
      }
      Runnable keep = () -> { for (int i = 0; i < values.length; i++) if (values[i] == current[0]) select(buttons[0], labels, i); };
      AlertDialog dialog = new AlertDialog.Builder(getContext())
          .setTitle(R.string.ingame_language_restart_title)
          .setMessage(R.string.ingame_language_restart_text)
          .setNegativeButton(R.string.ingame_cancel, (d, w) -> keep.run())
          .setPositiveButton(R.string.ingame_restart, (d, w) -> {
            current[0] = value; host.gameLanguageChanged(value); close();
          }).create();
      dialog.setOnCancelListener(d -> keep.run());
      dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
      Immersive.show(dialog);
    });
    if (buttons[0].length == 1 && buttons[0][0] instanceof ArrowSelector) ((ArrowSelector) buttons[0][0]).setConfirm(true);
    help(R.string.ingame_language_help);
  }
  private Button nav(MenuLayout.Buttons parent, int title, int summary, int target) {
    SpannableStringBuilder label = new SpannableStringBuilder(s(title) + "   ›\n");
    int from = label.length(); label.append(s(summary));
    label.setSpan(new StyleSpan(Typeface.BOLD), 0, from, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    label.setSpan(new RelativeSizeSpan(15f / 19f), from, label.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    label.setSpan(new StyleSpan(Typeface.NORMAL), from, label.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    Button b = cellAction(parent, label.toString(), v -> showPage(target, true));
    b.setTypeface(Typeface.DEFAULT, Typeface.NORMAL); b.setTextSize(19);
    b.setText(label); b.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
    b.setMinHeight(dp(66)); b.setMinimumHeight(dp(66)); return b;
  }

  // DISPLAY (order of the project owner, 04.11.2026): GRAPHICS with the picture format first, then POST-PROCESSING
  // (effects: depth of field, lens flares, bloom, the real-time shadows; the picture filters), then
  // OPTIONAL FEATURES (minimap, FPS display)
  private void display() {
    heading(R.string.ingame_graphics);
    renderer();
    int[] info = CoreBridge.graphicsInfo();
    // Unknown GPU capabilities must not enable unsupported samples or anisotropy.
    int samples = info == null ? 1 : Math.max(1, info[0]);
    int anisotropy = info == null ? 1 : Math.max(1, info[1]);
    // (1.1.0, the project owner's fourth menu round, 21.11.2026) side labels: "Resolution" (picture format, resolution |
    // benchmark, display resolution | upscaler), "Filtering" (MSAA | FXAA, texture filter | sharper menu text)
    LinearLayout resolution = labelledOptions(R.string.ingame_resolution);
    // "Native" (5, wish of the project owner, 07.11.2026): the screen's own aspect (core_mode.cpp, re4mm::Native)
    graphicChoices(buttonsRow(resolution), R.string.ingame_picture, "aspect", new String[] {s(R.string.ingame_original_43), s(R.string.ingame_43_no_bars), "16:9", s(R.string.ingame_native), s(R.string.ingame_stretched)},
        new int[] {1, 3, 0, 5, 2}, 0, Integer.MAX_VALUE);
    into = buttonsRow(resolution);
    resolutionChoices();
    graphicsLine = groupText(resolution, ""); graphicsLine.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(10)); showGraphics();
    // "Display resolution" (1.1.0): the size sent to the screen, the display's scaler stretches it (CoreActivity)
    // (1.1.0, the project owner's third menu round, 21.11.2026) side by side: display resolution | upscaler, MSAA | FXAA,
    // texture filter | sharper menu text
    MenuLayout.Buttons outputRow = buttonsRow(resolution);
    graphicChoices(outputRow, R.string.ingame_output_res, CoreActivity.OUTPUT_RES, new String[] {s(R.string.ingame_native),
        "1080p", "900p", "720p"}, new int[] {0, 1080, 900, 720}, 0, Integer.MAX_VALUE);
    // "Upscaler" (1.1.0): SGSR or FSR instead of the plain stretch when the picture is smaller than its box
    graphicChoices(outputRow, R.string.ingame_upscaler, UPSCALER, new String[] {s(R.string.ingame_off),
        s(R.string.ingame_upscaler_sgsr), s(R.string.ingame_upscaler_fsr)},  // (the order as cost: light, strong)
        new int[] {0, 1, 2}, 0, Integer.MAX_VALUE);
    indentedHelp(s(R.string.ingame_output_res_help));
    indentedHelp(s(R.string.ingame_upscaler_help));
    LinearLayout filtering = labelledOptions(R.string.ingame_side_filtering);
    MenuLayout.Buttons aaRow = buttonsRow(filtering);
    graphicChoices(aaRow, R.string.ingame_aa_msaa, "samples", new String[] {s(R.string.ingame_off), "2×", "4×"},
        new int[] {1, 2, 4}, 1, samples);
    toggle(aaRow, R.string.ingame_aa_fxaa, graphics.getBoolean("fxaa", false), on -> {
      graphics.edit().putBoolean("fxaa", on).apply(); CoreBridge.setFxaa(on);
    });
    MenuLayout.Buttons textureRow = buttonsRow(filtering);
    graphicChoices(textureRow, R.string.ingame_texture_filter, "anisotropy", new String[] {s(R.string.ingame_off), "2×", "4×",
        "8×", "16×"}, new int[] {1, 2, 4, 8, 16}, 1, anisotropy);
    // "Sharper menu text" (1.0.0, decision of the project owner, 16.11.2026: "Bitte baue den Schärfefilter für die
    // Schrift auch in 1.0.0 ein."): the game's own menu words drawn sharper at high resolutions; on by default
    toggle(textureRow, R.string.ingame_sharper_text, sharperText(graphics), on -> {
      graphics.edit().putBoolean(SHARPER_TEXT, on).apply(); CoreBridge.setSharperText(on);
    });
    indentedHelp(s(R.string.ingame_fxaa_help));
    indentedHelp(s(R.string.ingame_sharper_text_help));
    heading(R.string.ingame_postprocessing);
    LinearLayout effects = labelledOptions(R.string.ingame_effects);
    // each press moves the depth of field on: off, light (as before), strong (wish of the project owner, 05.11.2026)
    final int[] dofLabels = {R.string.ingame_off, R.string.ingame_dof_light, R.string.ingame_dof_strong};
    MenuLayout.Buttons effectRow = buttonsRow(effects);
    selector(effectRow, R.string.ingame_dof, texts(dofLabels), dofLevel(graphics), 0, next -> {
      graphics.edit().putInt(DOF_LEVEL, next).putBoolean("dof", next != 0).apply();
      CoreBridge.setDof(next);
    });
    // a faint glow at torches, candles and fires (wish of the project owner, 04.11.2026; off by default); since 0.99.6
    // each press moves it on (wish of the project owner, 09.11.2026): off, lens flares (the coloured rings only), halos +
    // lens flares (the glow and star at the lights too, as the switch's "on" before)
    final int[] flareLabels = {R.string.ingame_off, R.string.ingame_flares_lens, R.string.ingame_flares_both};
    selector(effectRow, R.string.ingame_flares, texts(flareLabels), CoreBridge.flareMode(graphics), 0, next -> {
      graphics.edit().putInt(CoreBridge.FLARE_MODE, next).putBoolean("flares", next != 0).apply();
      CoreBridge.setFlares(next);
    });
    // bloom (0.99.13, the project owner: "Ja, einbauen"): the scene's bright parts glow softly; off, light, strong
    final int[] bloomLabels = {R.string.ingame_off, R.string.ingame_dof_light, R.string.ingame_dof_strong};
    MenuLayout.Buttons effectRow2 = buttonsRow(effects);
    selector(effectRow2, R.string.ingame_bloom, texts(bloomLabels), bloomLevel(graphics), 0, next -> {
      graphics.edit().putInt(BLOOM_LEVEL, next).apply();
      CoreBridge.setBloom(CoreBridge.bloomSent(graphics));
    });
    // Real-time shadows (RealtimeShadows; until 16.11.2026 debug builds only, since then every build, decisions of the
    // project owner): "Off > Characters > Characters and objects" (the project owner, 15.11.2026), since 16.11.2026
    // under the effects (the project owner: "Außerdem sollte der Echtzeitschatten unter ANZEIGE rein unter 'Effekte'"),
    // always shown (the project owner: "Yes, always show it"): a device that is not capable enough shows "Off" until
    // the player chooses, with a note why
    final boolean shadowsRow = host != null;
    final int shadowsCheck = shadowsRow ? RealtimeShadows.deviceCheck(host.perfDevice(), PerfProfile.Bench.of(graphics))
        : RealtimeShadows.CAPABLE;
    if (shadowsRow) {
      final boolean capable = shadowsCheck == RealtimeShadows.CAPABLE;
      selector(effectRow2, R.string.ingame_rt_shadows, texts(new int[] {shadowsName(0), shadowsName(1), shadowsName(2)}),
          RealtimeShadows.choice(graphics, capable), capable ? RealtimeShadows.DEFAULT_LEVEL : 0, next -> {
            graphics.edit().putInt(RealtimeShadows.KEY, next).apply();
            host.shadowsChanged();
          });
    }
    indentedHelp(s(R.string.ingame_flares_help));
    indentedHelp(s(R.string.ingame_bloom_help));
    if (shadowsRow) indentedHelp(s(R.string.ingame_rt_shadows_help));
    if (shadowsRow && shadowsCheck != RealtimeShadows.CAPABLE) {
      indentedHelp(getContext().getString(R.string.ingame_rt_shadows_not_recommended, s(shadowsWhy(shadowsCheck))));
    }
    // the picture filters one under the other, one general line under both (wish of the project owner, 02.11.2026)
    LinearLayout filters = labelledOptions(R.string.ingame_filter);
    // the GameCube's own softening for the TV (wish of the project owner, 01.11.2026)
    // (1.1.0, the project owner's third menu round) the two side by side
    MenuLayout.Buttons tvRow = buttonsRow(filters);
    toggle(tvRow, R.string.ingame_tv_filter, graphics.getBoolean("tvFilter", false), on -> {
      graphics.edit().putBoolean("tvFilter", on).apply(); CoreBridge.setTvFilter(on);
    });
    // the 3D as a PlayStation would have drawn it (wish of the project owner, 02.11.2026)
    toggle(tvRow, R.string.ingame_psx, graphics.getBoolean("psx", false), on -> {
      graphics.edit().putBoolean("psx", on).apply(); CoreBridge.setPsx(on);
    });
    // a sharpening for the 16:9 formats, whose 336 rows are enlarged to the screen's height (wish of the project
    // owner, 08.11.2026); each press moves it on: off, light, strong, as the depth of field
    final int[] sharpenLabels = {R.string.ingame_off, R.string.ingame_dof_light, R.string.ingame_dof_strong};
    MenuLayout.Buttons filterRow = buttonsRow(filters);
    selector(filterRow, R.string.ingame_sharpen, texts(sharpenLabels), sharpenLevel(graphics), 0, next -> {
      graphics.edit().putInt(SHARPEN_LEVEL, next).apply();
      CoreBridge.setSharpen(next);
    });
    // the picture as a tube TV showed it (wish of the project owner, 11.11.2026): off, light, strong
    selector(filterRow, R.string.ingame_crt, texts(sharpenLabels), crtLevel(graphics), 0, next -> {
      graphics.edit().putInt(CRT_LEVEL, next).apply();
      CoreBridge.setCrt(next);
    });
    indentedHelp(s(R.string.ingame_filter_help) + " " + s(R.string.ingame_sharpen_help) + " "
        + s(R.string.ingame_crt_help));
    // the colour (wish of the project owner, 11.11.2026): its look (off, warm film, survival horror) and beside it
    // its strength (light, strong); each press moves on
    MenuLayout.Buttons colourRow = buttonsRow(labelledOptions(R.string.ingame_colour));
    final int[] colourLabels = {R.string.ingame_off, R.string.ingame_colour_warm, R.string.ingame_colour_horror};
    final Button[] strength = new Button[1];
    selector(colourRow, R.string.ingame_colour, texts(colourLabels), colourMode(graphics), 0, next -> {
      graphics.edit().putInt(COLOUR_MODE, next).apply();
      valueLabel(strength[0], R.string.ingame_colour_strength, sharpenLabels[colourLevel(graphics)], next != 0);
      CoreBridge.setColour(next, colourLevel(graphics));
    });
    strength[0] = cellAction(colourRow, "", v -> {
      int next = colourLevel(graphics) == 2 ? 1 : 2;
      graphics.edit().putInt(COLOUR_LEVEL, next).apply();
      valueLabel((Button) v, R.string.ingame_colour_strength, sharpenLabels[next], colourMode(graphics) != 0);
      CoreBridge.setColour(colourMode(graphics), next);
    });
    valueLabel(strength[0], R.string.ingame_colour_strength, sharpenLabels[colourLevel(graphics)], colourMode(graphics) != 0);
    indentedHelp(s(R.string.ingame_colour_help));
    heading(R.string.ingame_optional_features);
    // the current room's outline at the bottom left, turning with the player (wish of the project owner, 04.11.2026)
    // (1.1.0, the project owner's third menu round, 21.11.2026) minimap | photo mode on top, the Information HUD under
    // them with "Skip intro" beside it
    // (the fourth menu round) the side label "Features", the heading "Optional"
    LinearLayout features = labelledOptions(R.string.ingame_side_features);
    MenuLayout.Buttons topRow = buttonsRow(features);
    toggle(topRow, R.string.ingame_minimap, graphics.getBoolean("minimap", false), on -> {
      graphics.edit().putBoolean("minimap", on).apply(); CoreBridge.setMinimap(on);
    });
    // "Photo mode" (1.1.0, wish of the project owner, 19.11.2026): L3, the keyboard's P or this menu's button freeze the
    // game and free the camera (PhotoMode)
    toggle(topRow, R.string.ingame_photo_mode,
        graphics.getBoolean(CoreActivity.PHOTO_MODE, false), on -> graphics.edit().putBoolean(CoreActivity.PHOTO_MODE, on).apply());
    // the Information HUD (0.99.11, wish of the project owner, 12.11.2026; before the FPS display, here since
    // 04.11.2026): each press moves it on (wish of the project owner, 05.11.2026: it covered the minimap): off, bottom
    // left, bottom centre, top left, top centre, off again; while it is on, check boxes under it choose what it shows
    // (InfoHud), with a sentence on the play time
    final int[] fpsLabels = {R.string.ingame_off, R.string.ingame_fps_bottom_left, R.string.ingame_fps_bottom_centre,
        R.string.ingame_fps_top_left, R.string.ingame_fps_top_centre};
    LinearLayout hud = features;
    MenuLayout.Buttons hudRow = buttonsRow(hud);
    MenuLayout.Buttons parts = new MenuLayout.Buttons(getContext(), 2);
    TextView hudHelp = text(s(R.string.ingame_hud_help), 15, false);
    hudHelp.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(8));
    int fpsNow = CoreActivity.fpsPlace(graphics);
    selector(hudRow, R.string.ingame_info_hud, texts(fpsLabels), fpsNow, 0, next -> {
      graphics.edit().putInt(CoreActivity.FPS_PLACE, next).putBoolean("fps", next != 0).apply();
      parts.setVisibility(next != 0 ? View.VISIBLE : View.GONE);
      hudHelp.setVisibility(next != 0 ? View.VISIBLE : View.GONE);
      host.fpsChanged(next);
    });
    // "Skip intro" (1.1.0, wish of the project owner, 19.11.2026: "Beim Start sollen die Introscreens gar nicht gezeigt
    // werden sondern sofort das Menu"): the game's logos before the title menu, from the next start (RE4_SKIP_INTRO)
    toggle(hudRow, R.string.ingame_skip_intro,
        graphics.getBoolean(CoreActivity.SKIP_INTRO, false), on -> graphics.edit().putBoolean(CoreActivity.SKIP_INTRO, on).apply());
    hud.addView(parts, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
    hudBox(parts, R.string.ingame_hud_fps, InfoHud.FPS);
    hudBox(parts, R.string.ingame_hud_hz, InfoHud.HZ);
    hudBox(parts, R.string.ingame_resolution, InfoHud.RESOLUTION);
    hudBox(parts, R.string.ingame_hud_time, InfoHud.TIME);
    hud.addView(hudHelp);
    parts.setVisibility(fpsNow != 0 ? View.VISIBLE : View.GONE);
    hudHelp.setVisibility(fpsNow != 0 ? View.VISIBLE : View.GONE);
    help(R.string.ingame_skip_intro_help);
    help(R.string.ingame_photo_mode_help);
  }
  /** One of the Information HUD's check boxes (InfoHud): on until cleared, the HUD takes it at once. */
  private CheckBox hudBox(MenuLayout.Buttons parent, int label, String part) {
    CheckBox box = new CheckBox(getContext());
    box.setText(s(label)); box.setTextSize(15); box.setTextColor(FocusLook.text(ColorStateList.valueOf(INK)));
    box.setButtonTintList(new ColorStateList(new int[][] {new int[] {android.R.attr.state_checked}, new int[0]},
        new int[] {GOLD, MUTED}));
    StateListDrawable look = new StateListDrawable();
    look.addState(new int[] {android.R.attr.state_focused}, focused());
    look.addState(new int[] {android.R.attr.state_pressed}, shape(ACTIVE, GOLD, 6));
    look.addState(new int[0], shape(SURFACE, BORDER, 6));
    box.setBackground(look);
    box.setPaddingRelative(dp(8), dp(8), dp(12), dp(8)); box.setMinHeight(dp(48)); box.setMinimumHeight(dp(48));
    box.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
    box.setChecked(InfoHud.shows(graphics, part));
    box.setOnCheckedChangeListener((v, on) -> {
      graphics.edit().putBoolean(part, on).apply();
      if (host != null) host.fpsChanged(CoreActivity.fpsPlace(graphics));
    });
    parent.addButton(box);
    return box;
  }
  /** "Renderer": OpenGL ES or Vulkan (port/docs/vulkan-bauplan.md E7; not experimental since 16.11.2026, the project
   *  owner: "Vulkan kannst du auch nicht mehr experimental machen"); a change restarts the game
   *  after a question, "Cancel" keeps the one before (as the game's language). Without Vulkan here: OpenGL ES only. */
  private void renderer() {
    final String[] labels = {s(R.string.ingame_renderer_gles), s(R.string.ingame_renderer_vulkan)};
    final int[] values = {0, 1};
    final boolean supported = host != null && host.vulkanSupported();
    final int[] current = {supported && host.vulkanChosen() ? 1 : 0};
    final Button[][] buttons = new Button[1][];
    buttons[0] = choices(R.string.ingame_renderer, labels, values, current[0], Integer.MAX_VALUE, labels.length, value -> {
      if (value == current[0]) return;
      Runnable keep = () -> select(buttons[0], labels, current[0]);
      AlertDialog dialog = new AlertDialog.Builder(getContext())
          .setTitle(R.string.ingame_language_restart_title)
          .setMessage(R.string.ingame_renderer_restart_text)
          .setNegativeButton(R.string.ingame_cancel, (d, w) -> keep.run())
          .setPositiveButton(R.string.ingame_restart, (d, w) -> {
            current[0] = value; close(); host.rendererChanged(value == 1);
          }).create();
      dialog.setOnCancelListener(d -> keep.run());
      dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
      Immersive.show(dialog);
    });
    if (!supported && buttons[0].length > 1) buttons[0][1].setEnabled(false);
    indentedHelp(s(supported ? R.string.ingame_renderer_help : R.string.ingame_renderer_unavailable));
  }
  private void controls() {
    // the touch controls under a heading of their own, a third of the row each (wish of the project owner, 05.11.2026)
    heading(R.string.ingame_touch_controls);
    // (1.1.0, the project owner's third menu round, 21.11.2026) the touch controls and the button style side by side,
    // both with their names, the helps under them
    // (the fourth menu round, 21.11.2026) beside the side label "Touch style"
    MenuLayout.Buttons touchRow = buttonsRow(labelledOptions(R.string.ingame_side_touch_style));
    final int[] touchModes = {TouchShowRule.AUTO, TouchShowRule.FORCE, TouchShowRule.SUPPRESS};
    selector(touchRow, R.string.ingame_touch_controls, new String[] {s(R.string.ingame_touch_auto),
        s(R.string.ingame_touch_force), s(R.string.ingame_touch_suppress)}, stepOf(touchModes, CoreActivity.touchMode(input)),
        0, i -> host.touchModeChanged(touchModes[i]));
    // the touch buttons' look (wishes of the project owner, 09.11.2026 and 17.11.2026): RE, GC, PS2 and XBOX, the same
    // choice as the editor's style button, read each time the page opens
    into = touchRow;
    choices(R.string.ingame_button_style, new String[] {s(R.string.ingame_button_icons), s(R.string.ingame_button_labels),
        s(R.string.ingame_button_ps2), s(R.string.ingame_button_xbox)},
        new int[] {TouchStyle.RE, TouchStyle.GC, TouchStyle.PS2, TouchStyle.XBOX},
        host == null ? TouchStyle.DEFAULT_LOOK : host.touchLook(), Integer.MAX_VALUE, look -> host.touchLookChanged(look));
    help(R.string.ingame_touch_help);
    help(R.string.ingame_button_style_help);
    // the gyro in a section of its own, above the comfort controls (wishes of the project owner, 30.10.2026)
    heading(R.string.ingame_motion);
    boolean gyro = GyroAim.available(getContext()), gyroOn = gyro && input.getBoolean(CoreActivity.GYRO_AIM, false);
    Button[] invert = new Button[2];
    Button[][] sources = new Button[1][];
    View[][] sensitivity = new View[1][];
    // (the fourth menu round, 21.11.2026) "Gyroscope": gyro aiming | its sensitivity, "Gyro from" under them (a row up,
    // above the axes); "Axes": the two directions
    LinearLayout gyroGroup = labelledOptions(R.string.ingame_side_gyro);
    into = buttonsRow(gyroGroup);
    sensitivity[0] = switchWithSlider(gyro ? R.string.ingame_gyro : R.string.ingame_gyro_none, gyroOn, enabled -> {
      host.gyroChanged(enabled);
      sensitivity[0][1].setEnabled(enabled); sensitivity[0][2].setEnabled(enabled);
      for (Button b : invert) b.setEnabled(enabled);
      if (sources[0] != null) for (Button b : sources[0]) b.setEnabled(enabled);
    }, R.string.ingame_gyro_sensitivity, GyroAim.MIN_SENSITIVITY, GyroAim.MAX_SENSITIVITY, GyroAim.SENSITIVITY_STEP,
        GyroAim.clampSensitivity(input.getFloat(CoreActivity.GYRO_SENSITIVITY, 1f)), value -> host.gyroSensitivityChanged(value));
    sensitivity[0][0].setEnabled(gyro);
    sensitivity[0][1].setEnabled(gyroOn); sensitivity[0][2].setEnabled(gyroOn);
    // "Gyro from" (1.1.0, the project owner's choice "Gyro A"): this device's gyroscope or a controller's own
    into = buttonsRow(gyroGroup);
    sources[0] = quarterChoices(R.string.ingame_gyro_source, new String[] {s(R.string.ingame_touch_auto),
        s(R.string.ingame_vibration_device), s(R.string.ingame_vibration_controller)},
        new int[] {GyroAim.SOURCE_AUTO, GyroAim.SOURCE_DEVICE, GyroAim.SOURCE_CONTROLLER},
        GyroAim.clampSource(input.getInt(CoreActivity.GYRO_SOURCE, GyroAim.SOURCE_AUTO)),
        value -> host.gyroSourceChanged(value));
    for (Button b : sources[0]) b.setEnabled(gyroOn);
    // the gyro's directions (wish of the project owner, 27.10.2026): at once in GyroAim
    MenuLayout.Buttons directions = buttonsRow(labelledOptions(R.string.ingame_side_axes));
    invert[0] = toggle(directions, R.string.ingame_gyro_invert_v, input.getBoolean(CoreActivity.GYRO_INVERT_V, false),
        value -> host.gyroInvertChanged(true, value));
    invert[1] = toggle(directions, R.string.ingame_gyro_invert_h, input.getBoolean(CoreActivity.GYRO_INVERT_H, false),
        value -> host.gyroInvertChanged(false, value));
    for (Button b : invert) b.setEnabled(gyroOn);
    help(R.string.ingame_gyro_source_help);
    heading(R.string.ingame_comfort);
    // (the fourth menu round, 21.11.2026) "Control type": the four rows; "Assists": Turn Leon | gentler QTEs
    LinearLayout controlType = labelledOptions(R.string.ingame_side_control_type);
    MenuLayout.Buttons first = buttonsRow(controlType);
    toggle(first, R.string.ingame_alternate, input.getBoolean(CoreActivity.ALT_CONTROLS, false), on -> host.alternateChanged(on));
    toggle(first, R.string.ingame_knife_swipe, input.getBoolean(CoreActivity.KNIFE_SWIPE, true), on -> host.knifeSwipeChanged(on));
    MenuLayout.Buttons second = buttonsRow(controlType);
    toggle(second, R.string.ingame_latch, input.getBoolean(CoreActivity.TOUCH_LATCH, false), on -> host.latchChanged(on));
    toggle(second, R.string.ingame_latch_b, input.getBoolean(CoreActivity.TOUCH_LATCH_B, false), on -> host.latchBChanged(on));
    // "Auto aim" and "Always run" (wish of the project owner, 30.10.2026): after the latches, before the aiming row
    MenuLayout.Buttons third = buttonsRow(controlType);
    toggle(third, R.string.ingame_auto_aim, input.getBoolean(CoreActivity.AUTO_AIM, false), on -> host.autoAimChanged(on));
    toggle(third, R.string.ingame_always_run, input.getBoolean(CoreActivity.ALWAYS_RUN, false), on -> host.alwaysRunChanged(on));
    // aiming the knife and the guns by touch or a controller (wish of the project owner, 30.10.2026): the game's
    // own "reverse" option and the stick's speed while aiming, right under the latches
    into = buttonsRow(controlType);
    switchWithSlider(R.string.ingame_aim_invert_v, input.getBoolean(CoreActivity.AIM_INVERT_V, false),
        on -> host.aimInvertChanged(on), R.string.ingame_aim_speed, CoreActivity.AIM_SPEED_MIN,
        CoreActivity.AIM_SPEED_MAX, CoreActivity.AIM_SPEED_STEP,
        CoreActivity.clampAimSpeed(input.getFloat(CoreActivity.AIM_SPEED, 1f)), value -> host.aimSpeedChanged(value));
    // "Turn Leon with mouse / look area" (1.0, a player's wish the project owner took, 16.11.2026): off by default, under
    // aiming; since the project owner's third menu round (1.1.0) beside "Gentler QTEs"
    // 1.1.3 (a player's wish: very slow on some devices): its "Turn speed" beside it, "Gentler QTEs" in the row below
    LinearLayout assists = labelledOptions(R.string.ingame_side_assists);
    into = buttonsRow(assists);
    switchWithSlider(R.string.ingame_body_turn, input.getBoolean(CoreActivity.BODY_TURN, false),
        on -> host.bodyTurnChanged(on), R.string.ingame_body_turn_speed, CoreActivity.BODY_TURN_MIN,
        CoreActivity.BODY_TURN_MAX, CoreActivity.BODY_TURN_STEP,
        CoreActivity.clampBodyTurn(input.getFloat(CoreActivity.BODY_TURN_SPEED, 1f)),
        value -> host.bodyTurnSpeedChanged(value));
    MenuLayout.Buttons lastRow = buttonsRow(assists);
    // "Gentler QTEs" (wish of the project owner, 14.11.2026: Professional's quick-time events "not to be done by touch,
    // just about with a controller"): on by default, at the comfort controls' end
    toggle(lastRow, R.string.ingame_gentle_qte,
        input.getBoolean(CoreActivity.GENTLE_QTE, CoreActivity.GENTLE_QTE_DEFAULT), on -> host.gentleQteChanged(on));
    help(R.string.ingame_alternate_help); help(R.string.ingame_latch_help); help(R.string.ingame_auto_aim_help);
    help(R.string.ingame_always_run_help); help(R.string.ingame_gentle_qte_help); help(R.string.ingame_aim_help);
    help(R.string.ingame_body_turn_help);
    help(R.string.ingame_knife_swipe_help);
    // touch aiming (wish of the project owner, 28.10.2026): what aims while L or R is held; since 05.11.2026 a section
    // of its own, a third of the row each
    heading(R.string.ingame_touch_aim);
    into = buttonsRow(labelledOptions(R.string.ingame_side_aim_with));  // (the fourth menu round: "Aim with")
    rowChoices(new String[] {s(R.string.ingame_aim_move_stick), s(R.string.ingame_aim_touch_area),
        s(R.string.ingame_aim_c_stick)}, new int[] {CoreInput.AIM_MOVE_STICK, CoreInput.AIM_LOOK, CoreInput.AIM_C_STICK},
        input.getInt(CoreActivity.TOUCH_AIM, CoreInput.AIM_MOVE_STICK), 3, mode -> host.touchAimChanged(mode));
    help(R.string.ingame_touch_aim_help);
    help(R.string.ingame_touch_zoom_help);  // (0.99.15, GitHub #247)
    // PLAYER VIEW above the laser sight (wish of the project owner, 05.11.2026): the far camera (a user's wish,
    // 01.11.2026; patch 0171) in the left half, the field of view (patch 0205) as a slider in the right half
    heading(R.string.ingame_player_view);
    // (the fourth menu round, 21.11.2026) "View": far camera | field of view; under it "Laser sight" (its section gone)
    into = buttonsRow(labelledOptions(R.string.ingame_side_view));
    View[] view = switchWithSlider(R.string.ingame_far_camera, input.getBoolean(CoreActivity.FAR_CAMERA, false), on -> {
      input.edit().putBoolean(CoreActivity.FAR_CAMERA, on).apply(); CoreBridge.setFarCamera(on);
    }, R.string.ingame_fov, -CoreActivity.FOV_RANGE, CoreActivity.FOV_RANGE, 1,
        CoreActivity.fovOffset(input), value -> {
          int degrees = Math.round(value);
          input.edit().putInt(CoreActivity.FOV_OFFSET, degrees).apply(); CoreBridge.setFov(degrees);
        }, CoreMenu::degrees);
    view[1].setContentDescription(s(R.string.ingame_fov));
    // the laser sight's colour, beam and dot (wish of the project owner, 04.11.2026)
    into = buttonsRow(labelledOptions(R.string.ingame_laser_section));
    rowChoices(new String[] {s(R.string.ingame_laser_red), s(R.string.ingame_laser_blue),
        s(R.string.ingame_laser_green), s(R.string.ingame_laser_yellow)}, new int[] {0, 1, 2, 3},
        input.getInt(CoreActivity.LASER_COLOUR, 0), 4, colour -> {
          input.edit().putInt(CoreActivity.LASER_COLOUR, colour).apply();
          CoreBridge.setLaserColour(colour);
        });
    help(R.string.ingame_far_camera_help); help(R.string.ingame_fov_help);
    // the controller configuration (wish of the project owner, 27.10.2026): at the bottom, only with a controller;
    // since 30.10.2026 under a heading of its own
    heading(R.string.ingame_controller_section);
    // the controller sticks' dead zone and range (wish of the project owner, 07.11.2026, GitHub issue #168: a left stick
    // that stuttered while walking) and where the game's vibration goes (07.11.2026: a Bluetooth controller's
    // motors instead of the phone's)
    final int[] shape = {CoreInput.clampDeadZone(input.getInt(CoreActivity.STICK_DEAD_ZONE, 0)),
        CoreInput.clampRange(input.getInt(CoreActivity.STICK_RANGE, 100))};
    // the dead zone and the range side by side (the project owner, 21.11.2026)
    // (the fourth menu round, 21.11.2026) "Stick options": dead zone | range, the vibration under them
    LinearLayout stickGroup = labelledOptions(R.string.ingame_side_stick);
    MenuLayout.Buttons sticks = buttonsRow(stickGroup);
    percentSlider(sticks, R.string.ingame_stick_dead_zone, 0, CoreInput.DEAD_ZONE_MAX, shape[0], value -> {
      shape[0] = value; host.stickShapeChanged(shape[0], shape[1]);
    });
    percentSlider(sticks, R.string.ingame_stick_range, CoreInput.RANGE_MIN, 100, shape[1], value -> {
      shape[1] = value; host.stickShapeChanged(shape[0], shape[1]);
    });
    into = buttonsRow(stickGroup);
    choices(R.string.ingame_vibration, new String[] {s(R.string.ingame_touch_auto), s(R.string.ingame_vibration_device),
        s(R.string.ingame_vibration_controller), s(R.string.ingame_off)}, new int[] {RumbleTarget.AUTO,
        RumbleTarget.DEVICE, RumbleTarget.CONTROLLER, RumbleTarget.OFF},
        RumbleTarget.clampMode(input.getInt(CoreActivity.VIBRATION, RumbleTarget.AUTO)), Integer.MAX_VALUE, 4,
        mode -> host.vibrationChanged(mode));
    help(R.string.ingame_stick_help);
    help(R.string.ingame_vibration_help);
    // the button mapping in a section of its own (the fourth menu round): "Map buttons and keys", the side label "Assign"
    heading(R.string.ingame_mapping_section);
    controllerButton = cellAction(buttonsRow(labelledOptions(R.string.ingame_side_assign)),
        s(R.string.ingame_controller_config) + "   ›",
        v -> showPage(CONTROLLER, true));
    controllerButton.setEnabled(host != null && (host.controllerConnected() || host.keyboardConnected()));
    help(R.string.ingame_controller_config_help);
    // 1.1.0: holding Ashley's key for 2 seconds puts her behind Leon (runtime/ashley_catchup.cpp)
    help(R.string.ingame_ashley_hold_help);
  }

  /**
   * A switch with a slider beside it (the gyro's row, the stick aiming's): the switch in the middle of the row,
   * as far from the row above as from the row below (wish of the project owner, 27.10.2026), and the row as
   * tall as a row of buttons, so its gaps are theirs (30.10.2026: the slider takes what its title leaves, at
   * least 32 dp); the slider's title above it, `min` + n × `step` up to `max`, its value as "1.25×".
   * The switch, the slider and its value, for the caller to enable.
   */
  private interface Format { String of(float value); }
  private View[] switchWithSlider(int label, boolean on, Changed<Boolean> toggled, int title, float min, float max,
      float step, float stored, Changed<Float> moved) {
    return switchWithSlider(label, on, toggled, title, min, max, step, stored, moved, CoreMenu::times);
  }
  private View[] switchWithSlider(int label, boolean on, Changed<Boolean> toggled, int title, float min, float max,
      float step, float stored, Changed<Float> moved, Format format) {
    // (1.1.0, the project owner's feedback of 21.11.2026) the switch and the slider as two cells of a row (`into`: a
    // side label's group)
    MenuLayout.Buttons row = into != null ? into : plainRow();
    into = null;
    Button toggle = toggle(row, label, on, toggled);
    SliderCell cell = sliderCell(row, title, Math.round((max - min) / step), Math.round((stored - min) / step),
        p -> format.of(min + p * step), p -> moved.accept(min + p * step));
    return new View[] {toggle, cell.bar, cell.value};
  }

  /** The actions' names, in ActionMap's order. */
  private static final int[] ACTION_LABELS = {R.string.ingame_act_forward, R.string.ingame_act_back,
      R.string.ingame_act_left, R.string.ingame_act_right, R.string.ingame_act_action, R.string.ingame_act_run,
      R.string.ingame_act_aim, R.string.ingame_act_fire, R.string.ingame_act_reload, R.string.ingame_act_knife,
      R.string.ingame_act_slash, R.string.ingame_act_ashley, R.string.ingame_act_inventory, R.string.ingame_act_map,
      R.string.ingame_act_options, R.string.ingame_act_menu, R.string.ingame_act_c_up, R.string.ingame_act_c_down,
      R.string.ingame_act_c_left, R.string.ingame_act_c_right};

  /** An action's name; the app menu's says "held: debug menu" only with the debug build's discs (the retail discs
   *  have none, 06.11.2026). */
  private String actionLabel(int a) {
    int id = ACTION_LABELS[a];
    if (id == R.string.ingame_act_menu && (host == null || !host.debugMenu())) id = R.string.ingame_act_menu_app;
    return s(id);
  }

  /** A key's name on its button: the controller's labels, the D-pad as arrows, else Android's name. */
  static String keyName(int keyCode) {
    switch (keyCode) {
      case 0: return "—";  // no key
      case ActionMap.MOUSE_LEFT: return "Mouse L";
      case ActionMap.MOUSE_RIGHT: return "Mouse R";
      case ActionMap.MOUSE_MIDDLE: return "Mouse M";
      case ActionMap.MOUSE_BACK: return "Mouse 4";
      case ActionMap.MOUSE_FORWARD: return "Mouse 5";
      case KeyEvent.KEYCODE_SPACE: return "Space";
      case KeyEvent.KEYCODE_SHIFT_LEFT: return "Shift (L)";
      case KeyEvent.KEYCODE_SHIFT_RIGHT: return "Shift (R)";
      case KeyEvent.KEYCODE_CTRL_LEFT: return "Ctrl (L)";
      case KeyEvent.KEYCODE_CTRL_RIGHT: return "Ctrl (R)";
      case KeyEvent.KEYCODE_ALT_LEFT: return "Alt (L)";
      case KeyEvent.KEYCODE_ALT_RIGHT: return "Alt (R)";
      case KeyEvent.KEYCODE_ESCAPE: return "Esc";
      case KeyEvent.KEYCODE_TAB: return "Tab";
      case KeyEvent.KEYCODE_ENTER: return "Enter";
      case KeyEvent.KEYCODE_DEL: return "Backspace";
      case KeyEvent.KEYCODE_BUTTON_A: return "A";
      case KeyEvent.KEYCODE_BUTTON_B: return "B";
      case KeyEvent.KEYCODE_BUTTON_X: return "X";
      case KeyEvent.KEYCODE_BUTTON_Y: return "Y";
      case KeyEvent.KEYCODE_BUTTON_L1: return "L1";
      case KeyEvent.KEYCODE_BUTTON_R1: return "R1";
      case KeyEvent.KEYCODE_BUTTON_L2: return "L2";
      case KeyEvent.KEYCODE_BUTTON_R2: return "R2";
      case KeyEvent.KEYCODE_BUTTON_THUMBL: return "L3";
      case KeyEvent.KEYCODE_BUTTON_THUMBR: return "R3";
      case KeyEvent.KEYCODE_BUTTON_START: return "START";
      case KeyEvent.KEYCODE_BUTTON_SELECT: return "SELECT";
      case KeyEvent.KEYCODE_DPAD_UP: return "✚ ↑";
      case KeyEvent.KEYCODE_DPAD_DOWN: return "✚ ↓";
      case KeyEvent.KEYCODE_DPAD_LEFT: return "✚ ←";
      case KeyEvent.KEYCODE_DPAD_RIGHT: return "✚ →";
      default: return KeyEvent.keyCodeToString(keyCode).replace("KEYCODE_BUTTON_", "").replace("KEYCODE_", "");
    }
  }

  /** Keys as text: "R2 + A", "—" for none. */
  static String keysText(int[] keys) {
    if (keys.length == 0) return keyName(0);
    StringBuilder t = new StringBuilder();
    for (int k : keys) t.append(t.length() == 0 ? "" : " + ").append(keyName(k));
    return t.toString();
  }

  /**
   * "Button Mapping" (wish of the project owner, 29.10.2026; ActionMap): the controller's or the keyboard's
   * mapping, each action on the left, its keys on the right (and the default where they differ); a press
   * on them waits for the key, a combination's for the key to hold and then the second (startCapture).
   */
  private void controller() {
    if (mapKind < 0) {
      mapKind = host != null && !host.controllerConnected() && host.keyboardConnected()
          ? ActionMap.KIND_KEYBOARD : ActionMap.KIND_CONTROLLER;
    }
    final boolean keyboard = mapKind == ActionMap.KIND_KEYBOARD;
    quarterChoices(R.string.ingame_map_device, new String[] {s(R.string.ingame_map_controller), s(R.string.ingame_map_keyboard)},
        new int[] {ActionMap.KIND_CONTROLLER, ActionMap.KIND_KEYBOARD}, mapKind, kind -> {
          captureAction = -1; main.removeCallbacks(captureTimeout); mapKind = kind; showPage(CONTROLLER, false);
        });
    // right under "Device", as wide as its two buttons, its explanation under it: with the controller "Force
    // controller detection" (1.1.0, the project owner, 20.11.2026: "Die Position bei 3 passt"; the idea from a fork),
    // with the keyboard "Reload without aiming" (the project owner, 20.11.2026: "nur zu sehen sein wenn 'Controller'
    // ... aktiviert ist. Hingegen sollte 'Nachladen ohne Zielen' oben sein wenn 'Tastatur & Maus' aktiviert ist")
    MenuLayout.Buttons under = underDevice();
    if (keyboard) {
      toggle(under, R.string.ingame_quick_reload, input.getBoolean(CoreActivity.QUICK_RELOAD, false),
          on -> host.quickReloadChanged(on));
      help(R.string.ingame_quick_reload_help);
    } else {
      toggle(under, R.string.ingame_force_controller, input.getBoolean(CoreActivity.FORCE_CONTROLLER, false), on -> {
        input.edit().putBoolean(CoreActivity.FORCE_CONTROLLER, on).apply();
        CoreActivity.forceController = on;
        android.util.Log.i("RE4Input", "force controller detection: " + (on ? "on" : "off"));
      });
      help(R.string.ingame_force_controller_help);
    }
    if (!keyboard && input.getBoolean(CoreActivity.ALT_CONTROLS, false)) help(R.string.ingame_controller_alternate_note);
    help(keyboard ? R.string.ingame_map_fixed_keyboard : R.string.ingame_map_fixed_controller);
    if (keyboard) mouseSlider();
    ActionMap def = ActionMap.defaults(mapKind), m = host == null ? null : host.mapping(mapKind);
    if (m == null) m = def;
    bindingButtons = new Button[ActionMap.ACTIONS];
    // (in the page's order: the C stick's keys after the moves, 0.99.15)
    for (int a : ActionMap.PAGE) {
      if (!ActionMap.available(mapKind, a)) continue;
      final int action = a;
      LinearLayout line = new MenuLayout.Columns(getContext(), -300, 420, false);
      line.addView(rowLabel(actionLabel(a)));
      MenuLayout.Buttons box = new MenuLayout.Buttons(getContext(), 1);
      line.addView(box);
      String keys = keysText(m.keys(a));
      if (!java.util.Arrays.equals(m.keys(a), def.keys(a))) {
        keys += "\n" + getContext().getString(R.string.ingame_map_default, keysText(def.keys(a)));
      }
      bindingButtons[a] = cellAction(box, keys, v -> startCapture(action));
      line.setPadding(0, dp(1), 0, dp(1)); body.addView(line);
    }
    mapStatus = text("", 15, true); mapStatus.setTextColor(GOLD);
    mapStatus.setPadding(dp(MenuLayout.GAP), dp(6), dp(MenuLayout.GAP), dp(4));
    mapStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
    mapStatus.setVisibility(pendingStatus == null ? View.GONE : View.VISIBLE);
    if (pendingStatus != null) mapStatus.setText(pendingStatus);
    pendingStatus = null;
    body.addView(mapStatus);
    cellAction(buttonsRow(body), s(R.string.ingame_controller_default), v -> {
      captureAction = -1; main.removeCallbacks(captureTimeout);
      host.mappingChanged(ActionMap.defaults(mapKind)); showPage(CONTROLLER, false);
    });
    help(R.string.ingame_controller_help);
  }

  /** The mouse's sensitivity (wish of the project owner, 29.10.2026), as the gyro's slider; its label in the
   *  device row's quarter, the slider under the device's buttons (1.1.0, the project owner, 20.11.2026: "Der Regler
   *  von 'Maus-Empfindlichkeit' beginnt zu weit links. Dadurch bricht 'Maus-Empfindlichkeit' um."). */
  private void mouseSlider() {
    float min = CoreActivity.MOUSE_MIN, step = CoreActivity.MOUSE_STEP;
    float now = host == null ? 1f : host.mouseSensitivity();
    sliderCell(buttonsRow(body), R.string.ingame_mouse_sensitivity, Math.round((CoreActivity.MOUSE_MAX - min) / step),
        Math.round((now - min) / step), p -> times(min + p * step), p -> {
          if (host != null) host.mouseSensitivityChanged(min + p * step);
        });
    help(R.string.ingame_mouse_help);
    help(R.string.ingame_keyboard_c_help);  // (0.99.15, GitHub #247: the knife by the mouse, the C stick's keys)
  }

  /** A row right under "Device" as wide as its two buttons (the project owner, 20.11.2026: "nur so breit sein wie die
   *  beiden Buttons darüber, also links eingerückt"): the device row's quarters, the label's quarter left empty. */
  private MenuLayout.Buttons underDevice() {
    MenuLayout.Columns under = new MenuLayout.Columns(getContext(), 0, 640, true);
    under.setWeights(1, 3);
    under.addView(new View(getContext()));
    MenuLayout.Buttons box = new MenuLayout.Buttons(getContext(), 1);
    under.addView(box);
    body.addView(under);
    return box;
  }

  /** Waits for the key of `action` (a second press on its button: no change); after 8 s it gives up. */
  private void startCapture(int action) {
    if (captureAction >= 0) {
      boolean same = captureAction == action;
      cancelCapture();
      if (same) return;
    }
    captureAction = action; captureFirst = 0; captureL = captureR = true;  // a trigger held now does not count
    Button b = bindingButtons[action];
    b.setText(s(ActionMap.combination(action) ? R.string.ingame_map_hold : R.string.ingame_map_press));
    b.setSelected(true);
    if (mapStatus != null) mapStatus.setVisibility(View.GONE);
    main.removeCallbacks(captureTimeout); main.postDelayed(captureTimeout, 8000);
  }

  private void cancelCapture() {
    main.removeCallbacks(captureTimeout);
    if (captureAction < 0) return;
    int action = captureAction;
    captureAction = -1;
    if (page == CONTROLLER && isShowing()) {
      showPage(CONTROLLER, false);
      main.post(() -> { if (bindingButtons != null && bindingButtons[action] != null) bindingButtons[action].requestFocus(); });
    }
  }

  /** A key for the action waited for (a controller's as its label, ActionMap's key codes). */
  private void captured(int code) {
    int action = captureAction;
    if (ActionMap.combination(action) && captureFirst == 0) {
      captureFirst = code;
      bindingButtons[action].setText(getContext().getString(R.string.ingame_map_second, keyName(code)));
      main.removeCallbacks(captureTimeout); main.postDelayed(captureTimeout, 8000);
      return;
    }
    if (code == captureFirst) return;  // the held key again: still the second one
    int[] keys = ActionMap.combination(action) ? new int[] {captureFirst, code} : new int[] {code};
    ActionMap before = host.mapping(mapKind);
    if (before == null) before = ActionMap.defaults(mapKind);
    ActionMap after = before.assign(action, keys);
    captureAction = -1;
    main.removeCallbacks(captureTimeout);
    if (after == null) {
      pendingStatus = s(R.string.ingame_map_fixed_key);
    } else {
      for (int a = 0; a < ActionMap.ACTIONS; a++) {
        if (a != action && ActionMap.available(mapKind, a) && !java.util.Arrays.equals(before.keys(a), after.keys(a))) {
          pendingStatus = getContext().getString(R.string.ingame_map_swapped, actionLabel(a), keysText(after.keys(a)));
        }
      }
      host.mappingChanged(after);
    }
    showPage(CONTROLLER, false);
    main.post(() -> { if (bindingButtons != null && bindingButtons[action] != null) bindingButtons[action].requestFocus(); });
  }

  /** While waiting: a key of the device shown (not the fixed ones, not Android's back) is taken. */
  private boolean captureKey(KeyEvent event) {
    int key = event.getKeyCode();
    if (key == KeyEvent.KEYCODE_BACK) {
      if (event.getAction() == KeyEvent.ACTION_DOWN) cancelCapture();
      return true;
    }
    if (event.getAction() != KeyEvent.ACTION_DOWN || event.getRepeatCount() > 0) return true;
    boolean keyboard = !KeyEvent.isGamepadButton(key) && CoreActivity.keyboardDevice(event.getDevice());
    boolean wantKeyboard = mapKind == ActionMap.KIND_KEYBOARD;
    if (keyboard != wantKeyboard) {
      showMapStatus(s(wantKeyboard ? R.string.ingame_map_use_keyboard : R.string.ingame_map_use_controller));
    } else if (wantKeyboard ? ActionMap.fixedKeyboardButton(key) != 0 : CoreInput.dpad(key) != 0) {
      showMapStatus(s(R.string.ingame_map_fixed_key));
    } else {
      captured(wantKeyboard ? key : host.faceLabel(key));
    }
    return true;
  }

  private void showMapStatus(String text) {
    if (mapStatus == null) return;
    mapStatus.setText(text); mapStatus.setVisibility(View.VISIBLE);
  }

  /**
   * While waiting for a key of the keyboard and mouse: a mouse button pressed is that key (the pointer is
   * free in the menu; the click that started the wait has ended).
   */
  private boolean captureMouse(MotionEvent e) {
    if (captureAction < 0 || mapKind != ActionMap.KIND_KEYBOARD || !e.isFromSource(InputDevice.SOURCE_MOUSE)) return false;
    int bit = 0;
    if (e.getActionMasked() == MotionEvent.ACTION_BUTTON_PRESS && android.os.Build.VERSION.SDK_INT >= 23) {
      bit = e.getActionButton();
    } else if (e.getActionMasked() == MotionEvent.ACTION_DOWN) {
      bit = Integer.lowestOneBit(e.getButtonState());
    }
    int code = ActionMap.mouseButton(bit);
    if (code != 0) captured(code);
    return true;  // nothing else of the mouse while waiting
  }

  @Override public boolean dispatchTouchEvent(MotionEvent e) {
    if (captureMouse(e)) return true;
    return super.dispatchTouchEvent(e);
  }

  @Override public boolean dispatchGenericMotionEvent(MotionEvent e) {
    if (captureMouse(e)) return true;
    return super.dispatchGenericMotionEvent(e);
  }

  /** While waiting for a controller's key: an analog trigger past half way is L2 or R2. */
  @Override public boolean onGenericMotionEvent(MotionEvent e) {
    if (captureAction >= 0 && mapKind == ActionMap.KIND_CONTROLLER && e.getAction() == MotionEvent.ACTION_MOVE
        && (e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
      // the controller's trigger axes (ControllerAxes: Z/RZ on a wired Xbox pad Android has no key layout for)
      CoreActivity.EventAxes axes = new CoreActivity.EventAxes().set(e, e.getDevice(), e.getSource());
      ControllerAxes layout = ControllerAxes.of(axes);
      boolean l = layout.triggerNow(axes, false) > 0.6f;
      boolean r = layout.triggerNow(axes, true) > 0.6f;
      boolean newL = l && !captureL, newR = r && !captureR;
      captureL = l;
      captureR = r;
      if (newL) captured(KeyEvent.KEYCODE_BUTTON_L2);
      else if (newR) captured(KeyEvent.KEYCODE_BUTTON_R2);
      return true;
    }
    return super.onGenericMotionEvent(e);
  }

  /**
   * The AUDIO page (wish of the project owner, 30.10.2026): the music's and the sound effects' volume, at once
   * in the core (runtime/sound_volume.cpp); the movies keep their sound. Below the sliders two buttons switch the
   * music or the effects off entirely (wish of the project owner, 31.10.2026), "musicOn" / "effectsOn": the core
   * then gets 0, the slider keeps its value for switching back on.
   */
  private void audio() {
    heading(R.string.ingame_volume);
    // (1.1.0, the project owner's fourth menu round) side labels "Levels" and "Mute"
    MenuLayout.Buttons volumes = buttonsRow(labelledOptions(R.string.ingame_side_levels));
    volumeSlider(volumes, R.string.ingame_music_volume, "musicVolume");
    volumeSlider(volumes, R.string.ingame_effects_volume, "effectsVolume");
    MenuLayout.Buttons sound = buttonsRow(labelledOptions(R.string.ingame_side_mute));
    toggle(sound, R.string.ingame_music_volume, graphics.getBoolean("musicOn", true), on -> {
      graphics.edit().putBoolean("musicOn", on).apply(); CoreBridge.sendSoundVolume(graphics);
    });
    toggle(sound, R.string.ingame_effects_volume, graphics.getBoolean("effectsOn", true), on -> {
      graphics.edit().putBoolean("effectsOn", on).apply(); CoreBridge.sendSoundVolume(graphics);
    });
    help(R.string.ingame_volume_help);
  }
  /** A percentage as the volumes' sliders: `min` .. `max` in steps of 5 from `now`, each move to `moved`. */
  private void percentSlider(MenuLayout.Buttons row, int label, int min, int max, int now, Changed<Integer> moved) {
    sliderCell(row, label, (max - min) / 5, Math.round((Math.max(min, Math.min(max, now)) - min) / 5f),
        p -> (min + p * 5) + " %", p -> { if (host != null) moved.accept(min + p * 5); });
  }
  /** A volume of the AUDIO page as the mouse's slider: 0 .. 100 % in steps of 5, kept in `key`. */
  private void volumeSlider(MenuLayout.Buttons row, int label, String key) {
    int now = Math.max(0, Math.min(100, graphics.getInt(key, 100)));
    sliderCell(row, label, 20, Math.round(now / 5f), p -> p * 5 + " %", p -> {
      graphics.edit().putInt(key, p * 5).apply(); CoreBridge.sendSoundVolume(graphics);
    });
  }

  private void performance() {
    heading(R.string.ingame_perf_section);
    // half the row each (wish of the project owner, 05.11.2026); since the fourth menu round (1.1.0) beside the side
    // label "Profile"
    into = buttonsRow(labelledOptions(R.string.ingame_side_profile));
    rowChoices(new String[] {s(R.string.ingame_perf_default), s(R.string.ingame_perf_cores)},
        new int[] {0, 1}, graphics.getInt("fastCores", 0), 2, value -> {
          graphics.edit().putInt("fastCores", value).apply(); CoreBridge.setFastCores(value == 1);
        });
    help(R.string.ingame_perf_help);
    // Potato mode (decision of the project owner, 30.10.2026): two switches that leave the expensive
    // surroundings effects out; the explanation only below them (runtime/potato.cpp in the core)
    heading(R.string.ingame_potato);
    LinearLayout potatoGroup = labelledOptions(R.string.ingame_effects);  // (the fourth menu round: "Effects")
    MenuLayout.Buttons potato = buttonsRow(potatoGroup);
    toggle(potato, R.string.ingame_potato_weather, graphics.getBoolean("weatherEffects", true), on -> {
      graphics.edit().putBoolean("weatherEffects", on).apply(); CoreBridge.setPotato(CoreBridge.potatoSent(graphics)); CoreBridge.setBloom(CoreBridge.bloomSent(graphics));
    });
    toggle(potato, R.string.ingame_potato_special, graphics.getBoolean("specialEffects", true), on -> {
      graphics.edit().putBoolean("specialEffects", on).apply(); CoreBridge.setPotato(CoreBridge.potatoSent(graphics)); CoreBridge.setBloom(CoreBridge.bloomSent(graphics));
    });
    // two more on a user's wish (08.11.2026, through the project owner): the particles of fights, weapons and
    // fires in three steps (each press moves it on: normal, less, none, as the sharpening), and shadows with the
    // light effects
    final int[] particleLabels = {R.string.ingame_particles_normal, R.string.ingame_particles_less,
        R.string.ingame_particles_none};
    MenuLayout.Buttons morePotato = buttonsRow(potatoGroup);
    selector(morePotato, R.string.ingame_potato_particles, texts(particleLabels), CoreBridge.particleEffects(graphics), 0,
        next -> {
          graphics.edit().putInt("particleEffects", next).apply();
          CoreBridge.setPotato(CoreBridge.potatoSent(graphics)); CoreBridge.setBloom(CoreBridge.bloomSent(graphics));
        });
    toggle(morePotato, R.string.ingame_potato_shadows, graphics.getBoolean("shadowsLight", true), on -> {
      graphics.edit().putBoolean("shadowsLight", on).apply(); CoreBridge.setPotato(CoreBridge.potatoSent(graphics)); CoreBridge.setBloom(CoreBridge.bloomSent(graphics));
      if (host != null) host.shadowsChanged();
    });
    // a third row (wish of the project owner, 09.11.2026, from the OUYA branch): "View distance", each press moves it
    // on, the distance fog nearer and nothing drawn beyond it (patch 0223, in the core: OpenGL ES and Vulkan alike);
    // since 0.99.18 (the project owner, 15.11.2026) five steps, Normal, Medium, Short, None (the 35 % with thick fog),
    // Far; lit but at None, with eyes after the text (viewLabel). Alone in a plain row over the whole width, as the
    // buttons' rows lay out one button (MenuLayout.GAP all round): a GridLayout measures its height at the unbroken
    // text's width, and the eyes put on the next line in a narrow window were cut off
    // (1.1.0) an arrow selector from the farthest to the nearest (VIEW_ORDER), Normal the default
    final String[] viewLabels = new String[CoreBridge.VIEW_ORDER.length];
    for (int i = 0; i < viewLabels.length; i++) viewLabels[i] = s(CoreBridge.VIEW_LABEL[CoreBridge.VIEW_ORDER[i]]);
    selector(buttonsRow(potatoGroup), R.string.ingame_potato_view, viewLabels,
        stepOf(CoreBridge.VIEW_ORDER, CoreBridge.viewDistance(graphics)), stepOf(CoreBridge.VIEW_ORDER, CoreBridge.VIEW_NORMAL),
        i -> {
          int next = CoreBridge.VIEW_ORDER[i];
          graphics.edit().putInt(CoreBridge.VIEW_DISTANCE, next).apply();
          CoreBridge.setViewDistance(CoreBridge.VIEW_PERCENT[next]);
          CoreBridge.setBloom(CoreBridge.bloomSent(graphics));
        });
    help(R.string.ingame_potato_help);
    help(R.string.ingame_view_help);
    // the picture output under a heading of its own (wish of the project owner, 30.10.2026); since 05.11.2026
    // without a row label in two columns, the explanations over the whole width. Since 10.11.2026 (wish of the
    // project owner): Frame skip | 60 FPS, under them merged draws | vertex ring as before, then "Save bandwidth".
    // Since 1.1.0 (the project owner, 20.11.2026: "Frame Skip als 4 kleine Buttons nebeneinander in der ersten
    // Buttonreihe und die anderen 4 Buttons als je 2 nebeneinander darunter"): the frame skip as four choices in the
    // first row (No / Auto / Normal / Aggressive), then 60 FPS | Even frame pacing, merged draws | vertex ring, then
    // "Save bandwidth"
    heading(R.string.ingame_experimental);
    // Frame skip leaves pictures out, 60 FPS adds pictures between: one or the other (wish of the
    // project owner, 26.10.2026: both on jerked back and forth); switching one on switches the other off.
    // (Off > Auto (1.1.0) > Normal (the renderer's) > Aggressive (the game's own, patch 0227); the stored values
    // SKIP_ORDER)
    final String[] skipNames = {s(R.string.ingame_fs_off), s(R.string.ingame_fs_auto), s(R.string.ingame_fs_normal),
        s(R.string.ingame_fs_aggressive)};
    final Button[] skipAndSmooth = new Button[2];
    final Button[][] skipChoices = new Button[1][];
    // 0.99.9 (frame pacing): with 60 FPS on a screen that 60 does not divide evenly (90 Hz) a line suggests 30
    final double screenHz = host != null ? host.displayHz() : 0;
    final TextView unevenHint = text(getContext().getString(R.string.ingame_smooth60_uneven,
        (int) Math.round(screenHz)), 15, false);
    unevenHint.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(8));
    final Runnable showHint = () -> unevenHint.setVisibility(
        FramePacing.sixtyUneven(screenHz) && graphics.getBoolean("smooth60", false) ? View.VISIBLE : View.GONE);
    // (1.1.0, the project owner's design of 21.11.2026) one arrow selector "Frame skip": Off, Auto, Normal,
    // Aggressive, beside 60 FPS; since the fourth menu round beside the side label "Options", then Even frame pacing |
    // save bandwidth (Vulkan), under them the two that Vulkan greys out: merged draws | vertex ring
    LinearLayout options = labelledOptions(R.string.ingame_side_options);
    MenuLayout.Buttons output = buttonsRow(options), more = buttonsRow(options), vkRow = buttonsRow(options);
    final String[] skipValues = {s(R.string.ingame_off), s(R.string.ingame_touch_auto), s(R.string.ingame_skip_normal),
        s(R.string.ingame_skip_aggressive)};
    final ArrowSelector skip = selector(output, R.string.ingame_frame_skip, skipValues,
        stepOf(SKIP_ORDER, frameSkipMode(graphics)), 0, i -> {
      int next = SKIP_ORDER[i];
      storeFrameSkipMode(graphics, next);
      sendFrameSkip(next);
      if (next != 0 && skipAndSmooth[1].isSelected()) {
        toggleLabel(skipAndSmooth[1], R.string.ingame_smooth60, false);
        graphics.edit().putBoolean("smooth60", false).apply(); CoreBridge.setSmooth(false);
        showHint.run();
      }
    });
    skipChoices[0] = new Button[] {skip};
    skipAndSmooth[0] = skip;
    skipAndSmooth[1] = toggle(output, R.string.ingame_smooth60, graphics.getBoolean("smooth60", false), on -> {
      graphics.edit().putBoolean("smooth60", on).apply(); CoreBridge.setSmooth(on);
      if (on && frameSkipMode(graphics) != 0) {
        storeFrameSkipMode(graphics, 0);
        sendFrameSkip(0);
        select(skipChoices[0], skipNames, 0);
      }
      showHint.run();
    });
    // merged draw calls (an experiment for phones with a slow graphics driver; wish of the project
    // owner, 30.10.2026: below 60 FPS); the renderer leaves it out while 60 FPS is on
    // with Vulkan both greyed out and shown off (question of the project owner, 09.11.2026): Vulkan always streams
    // the vertices through mapped buffers (gx_vk.cpp, the vertex ring has no effect there) and its draw calls are
    // cheap, merging only adds pipelines to build (core_mode.cpp leaves it out); the choice stays for OpenGL ES
    final boolean vulkanRun = host != null && host.vulkanRunning();
    // "Even frame pacing" (1.1.0, the project owner's decision of 20.11.2026): under 60 FPS since the arrow selector
    toggle(more, R.string.ingame_even_pacing, evenPacing(graphics), on -> {
      graphics.edit().putBoolean(EVEN_PACING, on).apply(); CoreBridge.setEvenPacing(on);
    });
    Button merge = toggle(vkRow, R.string.ingame_merge_draws, graphics.getBoolean("mergeDraws", false), on -> {
      graphics.edit().putBoolean("mergeDraws", on).apply(); CoreBridge.setMerge(on);
    });
    // the vertex ring at the bottom (wish of the project owner, 01.11.2026: off by default, for Mali)
    Button ring = toggle(vkRow, R.string.ingame_vertex_ring, graphics.getBoolean("vertexRing", false), on -> {
      graphics.edit().putBoolean("vertexRing", on).apply(); CoreBridge.setVertexRing(on);
    });
    // "Save bandwidth (Vulkan)" (wish of the project owner, 10.11.2026): under them; Vulkan only (with OpenGL ES greyed
    // out and shown off, the choice kept), off by default until a Mali or Adreno report shows it helps
    Button bandwidth = toggle(more, R.string.ingame_vk_bandwidth, graphics.getBoolean("vkBandwidth", false), on -> {
      graphics.edit().putBoolean("vkBandwidth", on).apply(); CoreBridge.setVkBandwidth(on);
    });
    if (vulkanRun) {
      toggleLabel(merge, R.string.ingame_merge_draws, false); merge.setEnabled(false);
      toggleLabel(ring, R.string.ingame_vertex_ring, false); ring.setEnabled(false);
    } else {
      toggleLabel(bandwidth, R.string.ingame_vk_bandwidth, false); bandwidth.setEnabled(false);
    }
    indentedView(unevenHint);
    showHint.run();
    help(R.string.ingame_skip_normal_help);
    help(R.string.ingame_skip_aggressive_help);
    help(R.string.ingame_skip_auto_help);
    help(R.string.ingame_even_pacing_help);
    help(R.string.ingame_vk_bandwidth_help);
    help(R.string.ingame_smooth_help);
    help(R.string.ingame_skip_or_smooth);
    help(R.string.ingame_merge_help);
    help(R.string.ingame_vertex_ring_help);
    if (vulkanRun) help(R.string.ingame_vulkan_perf_help);
  }
  private void management() {
    heading(R.string.ingame_card); help(R.string.ingame_card_help);
    cardLine = text("", 15, false); cardLine.setPadding(dp(MenuLayout.GAP), 0, dp(MenuLayout.GAP), 0); body.addView(cardLine);
    // the .zip export beside the .gci one, then the import and "Delete All Data" (wish of the project owner,
    // 14.11.2026, 0.99.15)
    MenuLayout.Buttons cardButtons = buttonsRow(body);
    exportAll = cellAction(cardButtons, s(R.string.ingame_export), v -> host.exportAll());
    exportZip = cellAction(cardButtons, s(R.string.ingame_export_zip), v -> host.exportZip());
    cellAction(cardButtons, s(R.string.ingame_import), v -> host.importFiles());
    cardButtons.addButton(wipeButton()); refreshCard();
    // another release over the imported discs (wish of the project owner, 06.11.2026)
    heading(R.string.ingame_discs); help(R.string.ingame_discs_help);
    cellAction(buttonsRow(body), s(R.string.ingame_import_discs), v -> askImportDiscs());
    heading(R.string.ingame_log); help(R.string.ingame_log_help);
    MenuLayout.Buttons log = buttonsRow(body);
    // (on by default: CoreLog.enabled; the project owner, 21.11.2026: "soll per default aktiv sein")
    toggle(log, R.string.ingame_log_record, CoreLog.enabled(getContext()), on -> host.logChanged(on));
    toggle(log, R.string.ingame_log_capture, tools, on -> { tools = on; host.toolsChanged(on); });
    MenuLayout.Buttons reports = buttonsRow(body);
    cellAction(reports, s(R.string.ingame_report_send), v -> host.shareReport());
    cellAction(reports, s(R.string.ingame_report_save), v -> host.saveReport());
    // since 0.99.4 the shaders are kept where Android's "Clear cache" does not reach them (decision of the project
    // owner, 08.11.2026): emptied by hand here
    heading(R.string.ingame_shader_cache); help(R.string.ingame_shader_cache_help);
    MenuLayout.Buttons shaders = buttonsRow(body);
    cellAction(shaders, s(R.string.ingame_shader_clear), v -> askClearShaders());
    // "Prepare shaders at start" beside it (1.1.0, the project owner's design of 20.11.2026): off by default; below 4 GB
    // of RAM greyed out and shown off, with a line why (the project owner, 20.11.2026: "Hat das Gerät weniger als 4 GB
    // kann man die Option nicht aktivieren"; ShaderPrepScreen)
    final boolean prepAllowed = CoreActivity.shaderPrepAllowed(getContext());
    Button prep = toggle(shaders, R.string.ingame_shader_prep,
        prepAllowed && graphics.getBoolean(CoreActivity.SHADER_PREP, false),
        on -> graphics.edit().putBoolean(CoreActivity.SHADER_PREP, on).apply());
    if (!prepAllowed) {
      prep.setEnabled(false);
      TextView why = text(getContext().getString(R.string.ingame_shader_prep_low_ram,
          String.format(java.util.Locale.getDefault(), "%.1f", CoreActivity.totalRam(getContext()) / 1e9)), 15, false);
      why.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(0));
      body.addView(why);
    }
    help(R.string.ingame_shader_prep_help);
    sourceSection();
  }

  /** "Source and updates" (wish of the project owner, 14.11.2026): the releases are on the same page; at the bottom
   *  of Management and, since 1.1.0, of the main page. */
  private void sourceSection() {
    heading(R.string.ingame_source);
    cellAction(buttonsRow(body), getContext().getString(R.string.ingame_source_line, SOURCE_URL), v -> openSource());
    // under the link a quiet note, its two lines centred (wish of the project owner, 15.11.2026): CubeEvil4 is only
    // on this page, not on other sites or game platforms
    body.addView(sourceNote(), new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT));
  }

  /** The note under "Source and updates": two lines, centred, small and muted as the other help texts. */
  TextView sourceNote() {
    TextView note = text(s(R.string.ingame_source_only) + "\n" + s(R.string.ingame_source_avoid), 14, false);
    note.setGravity(Gravity.CENTER_HORIZONTAL);
    note.setTextAlignment(View.TEXT_ALIGNMENT_CENTER);
    note.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(8));
    return note;
  }

  /** "Delete All Data" in a quiet red (the Benchmark button's way, in red). */
  private Button wipeButton() {
    Button wipe = quietRed(button(s(R.string.ingame_wipe), v -> askWipe()));
    wipe.setEnabled(host != null);
    return wipe;
  }

  /** The quiet red of "Delete All Data" (also "Delete texture pack", wish of the project owner, 17.11.2026). */
  private Button quietRed(Button b) {
    StateListDrawable quiet = new StateListDrawable();
    quiet.addState(new int[] {android.R.attr.state_focused}, focused());
    quiet.addState(new int[] {android.R.attr.state_pressed}, shape(WIPE_RED, UPDATE_RED_EDGE, 6));
    quiet.addState(new int[0], shape(WIPE_RED, WIPE_RED_EDGE, 6));
    b.setBackground(quiet);
    b.setTextColor(FocusLook.text(ColorStateList.valueOf(WIPE_TEXT)));
    return b;
  }

  /**
   * Before "Delete All Data" (wish of the project owner, 14.11.2026): the red question of the player characters
   * (warnCharacter) with two buttons, "Cancel" and "Delete" in red; the text says what goes and what stays. The focus
   * starts on "Cancel"; Back, B, Esc, START, SELECT or a touch beside it cancel, A or Enter press the focused button.
   * "Delete" closes the menu, and the game restarts without the memory card's data (Host.wipePlayerData).
   */
  private void askWipe() {
    LinearLayout box = column();
    box.setPadding(dp(24), dp(20), dp(24), dp(18));
    TextView title = text(s(R.string.ingame_wipe), 20, true);
    title.setGravity(Gravity.CENTER); title.setTextAlignment(View.TEXT_ALIGNMENT_GRAVITY);
    box.addView(title, new LinearLayout.LayoutParams(-1, -2));
    TextView message = text(s(R.string.ingame_wipe_text), 16, false);
    message.setTextColor(INK); message.setLineSpacing(0, 1.1f);
    message.setGravity(Gravity.CENTER_HORIZONTAL); message.setTextAlignment(View.TEXT_ALIGNMENT_GRAVITY);
    ScrollView scroller = new ScrollView(getContext());
    scroller.addView(message, new ViewGroup.LayoutParams(-1, -2));
    LinearLayout.LayoutParams between = new LinearLayout.LayoutParams(-1, -2, 1);
    between.setMargins(0, dp(12), 0, dp(18));
    box.addView(scroller, between);
    final AlertDialog[] shown = new AlertDialog[1];
    Button cancel = button(s(R.string.ingame_wipe_cancel), v -> shown[0].cancel());
    Button delete = button(s(R.string.ingame_wipe_yes), v -> {
      shown[0].dismiss(); close();
      if (host != null) host.wipePlayerData();
    });
    StateListDrawable look = new StateListDrawable();
    look.addState(new int[] {android.R.attr.state_focused}, focused());
    look.addState(new int[] {android.R.attr.state_pressed}, shape(UPDATE_RED, INK, 6));
    look.addState(new int[0], shape(UPDATE_RED, UPDATE_RED_EDGE, 6));
    delete.setBackground(look); delete.setTextColor(FocusLook.text(ColorStateList.valueOf(INK)));
    for (Button b : new Button[] {cancel, delete}) b.setPadding(dp(24), dp(8), dp(24), dp(8));
    LinearLayout buttons = row();
    buttons.setGravity(Gravity.CENTER);
    LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(0, -2, 1);
    half.setMarginEnd(dp(6));
    buttons.addView(cancel, half);
    half = new LinearLayout.LayoutParams(0, -2, 1);
    half.setMarginStart(dp(6));
    buttons.addView(delete, half);
    box.addView(buttons, new LinearLayout.LayoutParams(-1, -2));
    AlertDialog dialog = new AlertDialog.Builder(getContext()).setView(box).create();
    shown[0] = dialog;
    GradientDrawable frame = shape(WARN_RED, UPDATE_RED_EDGE, 10); frame.setStroke(dp(2), UPDATE_RED_EDGE);
    dialog.getWindow().setBackgroundDrawable(frame);
    dialog.setOnKeyListener((d, key, event) -> {
      if (key == KeyEvent.KEYCODE_BUTTON_A) {
        return dialog.getWindow().superDispatchKeyEvent(new KeyEvent(event.getDownTime(), event.getEventTime(),
            event.getAction(), KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount()));
      }
      if (key != KeyEvent.KEYCODE_ESCAPE && !KeyEvent.isGamepadButton(key)) return false;
      if ((key == KeyEvent.KEYCODE_ESCAPE || key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BUTTON_START
          || key == KeyEvent.KEYCODE_BUTTON_SELECT) && event.getAction() == KeyEvent.ACTION_DOWN
          && event.getRepeatCount() == 0) dialog.cancel();
      return true;
    });
    dialog.setOnShowListener(d -> cancel.requestFocus());
    Immersive.show(dialog);
  }

  /** "Cheats" at the top of Management (wish and decisions of the project owner, 27.10.2026;
   *  port/docs/cheat-menu-plan.md): three switches, kept like the other settings, and three buttons
   *  that change the game once (the weapon, the case and the money stay in the next save). */
  private void cheatSwitch(MenuLayout.Buttons parent, int label, int bit) {
    toggle(parent, label, (cheatBits & bit) != 0, on -> {
      cheatBits = on ? cheatBits | bit : cheatBits & ~bit; host.cheatsChanged(cheatBits);
    });
  }
  /** A cheat button that changes the game once: lit up for a moment after a press (wish of the project owner,
   *  27.10.2026: they gave no sign); `what` is the core's button number (runtime/cheats.cpp). */
  private void cheatButton(MenuLayout.Buttons parent, int what, int label) {
    cellAction(parent, s(label), v -> {
      v.setSelected(true); main.postDelayed(() -> v.setSelected(false), CHEAT_FLASH_MS);
      cheatAction(what, label);
    });
  }
  /** The cheat switches with "Random items" on (with `seed`, 0 .. 2^20 - 1) or off (no seed). */
  static int withRandomItems(int bits, boolean on, int seed) {
    bits &= ~(RANDOM_ITEMS_BIT | SEED_BITS);
    return on ? bits | RANDOM_ITEMS_BIT | ((seed << SEED_SHIFT) & SEED_BITS) : bits;
  }
  /** A row of two in a side label's group (1.1.0, the project owner's fourth menu round: "nie mehr 3 Buttons in einer
   *  Reihe, immer 2 Spalten"). */
  private MenuLayout.Buttons cheatRow(LinearLayout group) {
    MenuLayout.Buttons row = new MenuLayout.Buttons(getContext(), 2);
    group.addView(row, new LinearLayout.LayoutParams(-1, -2));
    return row;
  }
  /** The CHEATS page (out of MANAGEMENT since 30.10.2026, wish of the project owner). Since 05.11.2026 in three
   *  sections (wish of the project owner), since 16.11.2026 in this order (the project owner): PLAYER (health, ammo,
   *  invisibility; one-hit kill, steady aim, the special costumes), WEAPONS (each at its highest levels), GENERAL &
   *  INVENTORY (automatic QTEs, the extra games; random items and enemies; the case, the money), then the player
   *  character and the mods. */
  private void cheats() {
    cheatBits = getContext().getSharedPreferences(CoreActivity.GAME_PREFS, Context.MODE_PRIVATE).getInt(CoreActivity.CHEATS, 0);
    heading(R.string.ingame_cheat_section_player);
    // (1.1.0, the project owner's fourth menu round, 21.11.2026) two columns and side labels: "Options"; "Basic
    // weapons" and "Explosives"; "Unlocks" and "Stacking"; "Randomizer" and "Advanced"
    LinearLayout playerGroup = labelledOptions(R.string.ingame_side_options);
    MenuLayout.Buttons player = cheatRow(playerGroup);
    cheatSwitch(player, R.string.ingame_cheat_health, 1);
    cheatSwitch(player, R.string.ingame_cheat_ammo, 2);
    MenuLayout.Buttons player2 = cheatRow(playerGroup);
    cheatSwitch(player2, R.string.ingame_cheat_invisible, INVISIBLE_BIT);
    cheatSwitch(player2, R.string.ingame_cheat_onehit, 4);
    MenuLayout.Buttons player3 = cheatRow(playerGroup);
    cheatSwitch(player3, R.string.ingame_cheat_steady, STEADY_AIM_BIT);
    // the special costumes, out of the player character's section (the project owner, 16.11.2026)
    costumeSwitch(player3);
    heading(R.string.ingame_cheat_section_weapons);
    LinearLayout basic = labelledOptions(R.string.ingame_side_basic_weapons);
    MenuLayout.Buttons weapons = cheatRow(basic);
    cheatButton(weapons, 5, R.string.ingame_cheat_handcannon);
    cheatButton(weapons, 1, R.string.ingame_cheat_killer7);
    MenuLayout.Buttons weapons2 = cheatRow(basic);
    cheatButton(weapons2, 9, R.string.ingame_cheat_matilda);
    cheatButton(weapons2, 6, R.string.ingame_cheat_chicago);
    MenuLayout.Buttons launchers = cheatRow(labelledOptions(R.string.ingame_side_explosive_weapons));
    cheatButton(launchers, 8, R.string.ingame_cheat_mine);
    cheatButton(launchers, 7, R.string.ingame_cheat_rocket);
    // the switch "Automatic QTEs" and the button "Unlock extra games" (Assignment Ada, The Mercenaries), half the
    // row each (wish of the project owner, 29.10.2026)
    heading(R.string.ingame_cheat_section_game);
    LinearLayout unlocks = labelledOptions(R.string.ingame_side_unlocks);
    MenuLayout.Buttons more = cheatRow(unlocks);
    cheatSwitch(more, R.string.ingame_cheat_qte, 8);
    cellAction(more, s(R.string.ingame_cheat_extras), v -> {
      v.setSelected(true); main.postDelayed(() -> v.setSelected(false), CHEAT_FLASH_MS);
      cheatAction(4, R.string.ingame_cheat_extras);
    });
    // the case and the money under them (the project owner, 16.11.2026), then "Stack items" (1.1.0, the project owner,
    // 21.11.2026: "nach +1,000,000 PTAS"; patch 0254)
    final SharedPreferences game = getContext().getSharedPreferences(CoreActivity.GAME_PREFS, Context.MODE_PRIVATE);
    MenuLayout.Buttons inventory = cheatRow(unlocks);
    cheatButton(inventory, 2, R.string.ingame_cheat_case);
    cheatButton(inventory, 3, R.string.ingame_cheat_money);
    // "Refill ammo" beside it (1.1.0, the project owner, 21.11.2026: "Munition auffüllen", runtime/cheats.cpp action 10)
    MenuLayout.Buttons ammo = cheatRow(labelledOptions(R.string.ingame_side_stacking));
    // (the project owner's design of 21.11.2026) an arrow selector: Standard > 250 > 500 > 999 > Standard
    final int[] stackSteps = CoreActivity.STACK_STEPS;
    selector(ammo, R.string.ingame_cheat_stack, new String[] {s(R.string.ingame_stack_standard), "250", "500", "999"},
        stepOf(stackSteps, CoreActivity.stackMax(game)), 0, i -> host.stackItemsChanged(stackSteps[i]));
    cheatButton(ammo, 10, R.string.ingame_cheat_refill);
    help(R.string.ingame_cheat_stack_help);
    help(R.string.ingame_cheat_refill_help);
    // "Merchant" (1.1.3, the project owner: "Ja bitte die beiden Cheat Buttons einbauen"): all weapons | all tune-ups
    MenuLayout.Buttons merchant = cheatRow(labelledOptions(R.string.ingame_side_merchant));
    toggle(merchant, R.string.ingame_cheat_merchant_weapons, game.getBoolean(CoreActivity.MERCHANT_WEAPONS, false),
        on -> host.merchantChanged(true, on));
    toggle(merchant, R.string.ingame_cheat_merchant_tunes, game.getBoolean(CoreActivity.MERCHANT_TUNES, false),
        on -> host.merchantChanged(false, on));
    help(R.string.ingame_cheat_merchant_help);
    cheatStatus = text("", 15, false); cheatStatus.setPadding(dp(MenuLayout.GAP), dp(6), dp(MenuLayout.GAP), dp(2));
    cheatStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE); cheatStatus.setVisibility(View.GONE);
    indentedView(cheatStatus);
    // ENEMIES AND WORLD (1.1.0, wish of the project owner, 19.11.2026): "Enemy health bars", off by default
    heading(R.string.ingame_cheat_section_world);
    // "Random items" (wish of the project owner, 07.11.2026, GitHub issue #161): a new seed each time it goes on; with
    // "Random enemies" the first row of ENEMIES AND WORLD (1.1.0, the project owner, 21.11.2026)
    MenuLayout.Buttons items = cheatRow(labelledOptions(R.string.ingame_side_random));
    toggle(items, R.string.ingame_cheat_random_items, (cheatBits & RANDOM_ITEMS_BIT) != 0, on -> {
      cheatBits = withRandomItems(cheatBits, on, new java.util.Random().nextInt(1 << 20));
      host.cheatsChanged(cheatBits);
    });
    // "Random enemies" next to it (decision of the project owner, 12.11.2026; port/docs/gegner-randomizer-plan.md
    // stage 1): a seed of its own, a new one each time it goes on
    toggle(items, R.string.ingame_cheat_random_enemies, game.getInt(CoreActivity.ENEMY_SEED, 0) != 0, on -> {
      int seed = on ? 1 + new java.util.Random().nextInt((1 << 20) - 1) : 0;
      game.edit().putInt(CoreActivity.ENEMY_SEED, seed).apply();
      host.enemyRandomChanged(seed);
    });
    LinearLayout advanced = labelledOptions(R.string.ingame_side_advanced);
    MenuLayout.Buttons world = cheatRow(advanced);
    toggle(world, R.string.ingame_health_bars, game.getBoolean(CoreActivity.HEALTH_BARS, false),
        on -> host.healthBarsChanged(on));
    // "More enemies" beside it (1.1.0, wish of the project owner, 19.11.2026): a seed of its own, a new one each time
    toggle(world, R.string.ingame_more_enemies, game.getInt(CoreActivity.MORE_ENEMIES, 0) != 0, on ->
        host.moreEnemiesChanged(on ? 1 + new java.util.Random().nextInt((1 << 20) - 1) : 0));
    // "Bloody mode" in the row under them (1.1.3, the project owner: the floor's blood pools stay)
    MenuLayout.Buttons blood = cheatRow(advanced);
    toggle(blood, R.string.ingame_cheat_bloody, game.getBoolean(CoreActivity.BLOODY, false),
        on -> host.bloodyChanged(on));
    help(R.string.ingame_cheat_random_items_help);
    help(R.string.ingame_cheat_random_enemies_help);
    help(R.string.ingame_health_bars_help);
    help(R.string.ingame_more_enemies_help);
    help(R.string.ingame_cheat_bloody_help);
    help(R.string.ingame_cheats_help);
    playerCharacter();
    mods();
  }

  /** "Mods" under the cheats (wish of the project owner, 12.11.2026, the page is "Cheats & Mods" since;
   *  port/docs/textur-lader-plan.md): the player's HD texture pack, converted on a PC (CubeEvil4 Texture Packer) and
   *  imported into the app's textures folder (TexturePacks). A line with the pack (or the import's progress), then
   *  Import (Cancel while it runs) and the switch, Sharpness and Delete; the help says that CubeEvil4 brings no
   *  textures. */
  private void mods() {
    heading(R.string.ingame_mods);
    final TexturePacks.Info info = TexturePacks.info(getContext());
    hdStatus = text(hdLine(info), 15, false);
    hdStatus.setPadding(dp(MenuLayout.GAP), dp(6), dp(MenuLayout.GAP), dp(6));
    hdStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
    indentedView(hdStatus);
    // the layout (the project owner's fourth menu round, 21.11.2026): "Manage" with Import | Delete (in the quiet red of
    // "Delete All Data"); "Texture options" with HD textures | Sharpness, under them Costumes | Weapons
    MenuLayout.Buttons first = cheatRow(labelledOptions(R.string.ingame_side_manage));
    LinearLayout textureOptions = labelledOptions(R.string.ingame_side_texture_options);
    MenuLayout.Buttons second = cheatRow(textureOptions), third = cheatRow(textureOptions);
    hdImport = cellAction(first, s(TexturePacks.importing ? R.string.ingame_hd_cancel : R.string.ingame_hd_import), v -> {
      if (host == null) return;
      if (TexturePacks.importing) host.cancelTexturePackImport();
      else if (info != null) askReplacePack(info);
      else host.importTexturePack(textureListener);
    });
    Button delete = quietRed(cellAction(first, s(R.string.ingame_hd_delete), v -> askDeletePack()));
    Button onOff = toggle(second, R.string.ingame_hd_textures, info != null && graphics.getBoolean(TexturePacks.ON, true), on -> {
      graphics.edit().putBoolean(TexturePacks.ON, on).apply();
      if (host != null) host.graphicsChanged();
      main.postDelayed(this::refreshHd, 500);
    });
    final boolean half = graphics.getBoolean(TexturePacks.HALF, false);
    Button sharpness = cellAction(second, "", v -> {
      boolean next = !graphics.getBoolean(TexturePacks.HALF, false);
      graphics.edit().putBoolean(TexturePacks.HALF, next).apply();
      // (1.1.0, the project owner's third menu round) "Full" lit, "Half" not
      valueLabel((Button) v, R.string.ingame_hd_sharpness, next ? R.string.ingame_hd_half : R.string.ingame_hd_full, !next);
      if (host != null) host.graphicsChanged();
      main.postDelayed(this::refreshHd, 500);
    });
    valueLabel(sharpness, R.string.ingame_hd_sharpness, half ? R.string.ingame_hd_half : R.string.ingame_hd_full, !half);
    // "Costumes" and "Weapons" (1.0.0, wish of the project owner, 17.11.2026): each press moves it between "Texture
    // pack" (the default) and "Original", the playable characters' (or the weapons') own textures with a pack on
    Button costumes = groupSwitch(third, R.string.ingame_hd_costumes, TexturePacks.COSTUMES);
    Button weapons = groupSwitch(third, R.string.ingame_hd_weapons, TexturePacks.WEAPONS);
    onOff.setEnabled(info != null);
    sharpness.setEnabled(info != null);
    costumes.setEnabled(info != null);
    weapons.setEnabled(info != null);
    delete.setEnabled(info != null && !TexturePacks.importing);
    if (TexturePacks.importing && host != null) host.textureListener(textureListener);
    help(R.string.ingame_hd_help);
    help(R.string.ingame_hd_groups_help);
  }

  /** "Costumes" / "Weapons" on the Mods page: "Texture pack" (true, the default) or "Original", sent at once. */
  private Button groupSwitch(MenuLayout.Buttons row, int label, String key) {
    Button b = cellAction(row, "", v -> {
      boolean pack = !graphics.getBoolean(key, true);
      graphics.edit().putBoolean(key, pack).apply();
      valueLabel((Button) v, label, pack ? R.string.ingame_hd_pack : R.string.ingame_hd_original, pack);
      if (host != null) host.graphicsChanged();
      main.postDelayed(this::refreshHd, 500);
    });
    boolean pack = graphics.getBoolean(key, true);
    valueLabel(b, label, pack ? R.string.ingame_hd_pack : R.string.ingame_hd_original, pack);
    return b;
  }

  /** The HD textures' line: the import's progress, the pack (and what the renderer replaced so far), or none. */
  private String hdLine(TexturePacks.Info info) {
    if (TexturePacks.importing)
      return getContext().getString(R.string.ingame_hd_importing, size(TexturePacks.importDone), size(TexturePacks.importTotal));
    if (info == null) return s(R.string.ingame_hd_none);
    String line = getContext().getString(R.string.ingame_hd_info, info.name,
        java.text.NumberFormat.getIntegerInstance().format(info.images), size(info.bytes), info.created);
    long[] use = graphics.getBoolean(TexturePacks.ON, true) ? CoreBridge.texturePackUse() : null;
    if (use != null && use.length >= 3 && use[1] > 0)
      line += "\n" + getContext().getString(R.string.ingame_hd_in_use, java.text.NumberFormat.getIntegerInstance().format(use[0]),
          java.text.NumberFormat.getIntegerInstance().format(use[1]), size(use[2]));
    return line;
  }
  private String size(long bytes) { return android.text.format.Formatter.formatShortFileSize(getContext(), bytes); }
  private void refreshHd() {
    if (hdStatus != null && isShowing() && page == CHEATS) hdStatus.setText(hdLine(TexturePacks.info(getContext())));
  }

  // the import's progress and end (CoreActivity.importTextures, main thread)
  private final TexturePacks.Listener textureListener = new TexturePacks.Listener() {
    @Override public void progress(long done, long total) {
      if (hdStatus != null && isShowing() && page == CHEATS)
        hdStatus.setText(getContext().getString(R.string.ingame_hd_importing, size(done), size(total)));
      if (hdImport != null) hdImport.setText(s(R.string.ingame_hd_cancel));
    }
    @Override public void done(TexturePacks.Info info, TexturePacks.PackException failure) {
      if (!isShowing() || page != CHEATS) return;
      showPage(CHEATS, false);
      if (hdStatus == null) return;
      if (failure == null && info != null)
        hdStatus.setText(getContext().getString(R.string.ingame_hd_imported,
            java.text.NumberFormat.getIntegerInstance().format(info.images)) + "\n" + hdLine(info));
      else if (failure != null)
        hdStatus.setText(packProblem(failure));
    }
  };

  private String packProblem(TexturePacks.PackException e) { return packProblem(getContext(), e); }

  /** Why an import did not work, for the player (also TextureImportActivity). */
  static String packProblem(Context c, TexturePacks.PackException e) {
    switch (e.reason) {
      case TexturePacks.NOT_PACK: return c.getString(R.string.ingame_hd_not_pack);
      case TexturePacks.OTHER_VERSION: return c.getString(R.string.ingame_hd_other_version);
      case TexturePacks.NO_SPACE:
        return c.getString(R.string.ingame_hd_no_space, android.text.format.Formatter.formatShortFileSize(c, e.need),
            android.text.format.Formatter.formatShortFileSize(c, e.free));
      case TexturePacks.INCOMPLETE: return c.getString(R.string.ingame_hd_incomplete);
      case TexturePacks.DAMAGED: return c.getString(R.string.ingame_hd_damaged);
      case TexturePacks.CANCELLED: return c.getString(R.string.ingame_hd_cancelled);
      default: return c.getString(R.string.ingame_hd_read_failed, String.valueOf(e.getMessage()));
    }
  }

  /** A pack is there: the new one replaces it once it is copied completely (the menu asks first). */
  private void askReplacePack(TexturePacks.Info info) {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_hd_replace)
        .setMessage(getContext().getString(R.string.ingame_hd_replace_text, info.name))
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_hd_import, (d, w) -> { if (host != null) host.importTexturePack(textureListener); })
        .create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    Immersive.show(dialog);
  }

  /** "Delete texture pack" (the menu asks first): only the textures folder; discs, saves and cards stay. */
  private void askDeletePack() {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_hd_delete)
        .setMessage(R.string.ingame_hd_delete_text)
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_hd_delete_yes, (d, w) -> {
          final Context app = getContext().getApplicationContext();
          new Thread(() -> {
            TexturePacks.delete(app);
            main.post(() -> {
              if (!isShowing() || page != CHEATS) return;
              showPage(CHEATS, false);
              if (hdStatus != null) hdStatus.setText(s(R.string.ingame_hd_deleted));
            });
          }, "texture-delete").start();
        }).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    Immersive.show(dialog);
  }
  /** The story's player character below the cheat buttons (wish of the project owner, 03.11.2026;
   *  port/docs/cheat-menu-plan.md): one of the characters that play the story cleanly takes Leon's place
   *  at once (the room starts again where the player stands); bits 32..224 of the switches hold its
   *  player type (runtime/cheats.cpp). Saves keep Leon. Three buttons a row without a row label, as wide as
   *  the cheat buttons above (the owner, 03.11.2026), the sixth the switch for the special costumes of Leon
   *  and Ashley (bit 256). */
  private void playerCharacter() {
    heading(R.string.ingame_cheat_character);
    final String[] labels = {"Leon", "Ada", "HUNK", "Krauser", "Wesker"};
    final int[] values = {0, 2, 3, 4, 5};
    int selected = (cheatBits & CHARACTER_BITS) >> CHARACTER_SHIFT;
    // (1.1.0, the project owner's fourth menu round, 21.11.2026) "Main character" with Leon over the row, "Mercenaries"
    // with Ada | HUNK and Krauser | Wesker under it
    MenuLayout.Buttons leon = cheatRow(labelledOptions(R.string.ingame_side_main_character));
    LinearLayout mercenaries = labelledOptions(R.string.ingame_side_mercenaries);
    MenuLayout.Buttons[] rows = {leon, cheatRow(mercenaries), null, cheatRow(mercenaries), null};
    rows[2] = rows[1];
    rows[4] = rows[3];
    Button[] buttons = new Button[labels.length];
    for (int i = 0; i < labels.length; i++) {
      final int index = i;
      Button b = cellAction(rows[i], labels[i], v -> {
        Runnable choose = () -> {
          select(buttons, labels, index);
          cheatBits = (cheatBits & ~CHARACTER_BITS) | (values[index] << CHARACTER_SHIFT); host.cheatsChanged(cheatBits);
        };
        // a character other than Leon warns first, also from one of them to another (wish of the project owner,
        // 08.11.2026); the character already chosen and Leon switch at once as before
        int current = (cheatBits & CHARACTER_BITS) >> CHARACTER_SHIFT;
        if (values[index] == 0 || values[index] == current) choose.run(); else warnCharacter(labels[index], choose);
      });
      b.setSelected(values[i] == selected);
      b.setContentDescription(labels[i] + ", " + s(b.isSelected() ? R.string.ingame_on : R.string.ingame_off));
      buttons[i] = b;
    }
    help(R.string.ingame_cheat_character_help);
  }
  /** Before Ada, HUNK, Krauser or Wesker take Leon's place (wish of the project owner, 08.11.2026): a reddish
   *  warning that they are not made for the main game, the menu's other questions' dialog in red: the chosen
   *  character's name centred on top, the warning centred under it, the one button "I understand" centred below
   *  (the owner, 08.11.2026). Only that button switches (touch, click, A, Enter); Back, B, Esc, START, SELECT or
   *  a touch beside it keep the character (the buttons' choice is only changed by it). The other gamepad
   *  buttons do nothing here: Android would take X and START for a confirmation. */
  private void warnCharacter(String name, Runnable choose) {
    LinearLayout box = column();
    box.setPadding(dp(24), dp(20), dp(24), dp(18));
    TextView title = text(name, 20, true);
    title.setGravity(Gravity.CENTER); title.setTextAlignment(View.TEXT_ALIGNMENT_GRAVITY);
    box.addView(title, new LinearLayout.LayoutParams(-1, -2));
    // the text scrolls if a big font leaves too little room, the button stays below it
    TextView message = text(s(R.string.ingame_cheat_character_warning), 16, false);
    message.setTextColor(INK); message.setLineSpacing(0, 1.1f);
    message.setGravity(Gravity.CENTER_HORIZONTAL); message.setTextAlignment(View.TEXT_ALIGNMENT_GRAVITY);
    ScrollView scroller = new ScrollView(getContext());
    scroller.addView(message, new ViewGroup.LayoutParams(-1, -2));
    LinearLayout.LayoutParams between = new LinearLayout.LayoutParams(-1, -2, 1);
    between.setMargins(0, dp(12), 0, dp(18));
    box.addView(scroller, between);
    final AlertDialog[] shown = new AlertDialog[1];
    Button understood = button(s(R.string.ingame_understood), v -> { shown[0].dismiss(); choose.run(); });
    StateListDrawable look = new StateListDrawable();
    look.addState(new int[] {android.R.attr.state_focused}, focused());
    look.addState(new int[] {android.R.attr.state_pressed}, shape(UPDATE_RED, INK, 6));
    look.addState(new int[0], shape(UPDATE_RED, UPDATE_RED_EDGE, 6));
    understood.setBackground(look); understood.setTextColor(FocusLook.text(ColorStateList.valueOf(INK)));
    understood.setPadding(dp(24), dp(8), dp(24), dp(8));
    LinearLayout.LayoutParams centred = new LinearLayout.LayoutParams(-2, -2);
    centred.gravity = Gravity.CENTER_HORIZONTAL;
    box.addView(understood, centred);
    AlertDialog dialog = new AlertDialog.Builder(getContext()).setView(box).create();
    shown[0] = dialog;
    GradientDrawable frame = shape(WARN_RED, UPDATE_RED_EDGE, 10); frame.setStroke(dp(2), UPDATE_RED_EDGE);
    dialog.getWindow().setBackgroundDrawable(frame);
    dialog.setOnKeyListener((d, key, event) -> {
      if (key == KeyEvent.KEYCODE_BUTTON_A) {
        return dialog.getWindow().superDispatchKeyEvent(new KeyEvent(event.getDownTime(), event.getEventTime(),
            event.getAction(), KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount()));
      }
      if (key != KeyEvent.KEYCODE_ESCAPE && !KeyEvent.isGamepadButton(key)) return false;
      if ((key == KeyEvent.KEYCODE_ESCAPE || key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BUTTON_START
          || key == KeyEvent.KEYCODE_BUTTON_SELECT) && event.getAction() == KeyEvent.ACTION_DOWN
          && event.getRepeatCount() == 0) dialog.cancel();
      return true;
    });
    dialog.setOnShowListener(d -> understood.requestFocus());
    Immersive.show(dialog);
  }
  /** The costumes' choices (costumeSwitch): the game's own, Leon's jacket, the special costumes. */
  static final int COSTUME_NORMAL = 0, COSTUME_JACKET = 1, COSTUME_SPECIAL = 2;

  /** The costume in force: Special with the cheats' bit (COSTUME_BIT, the switch "Special costumes on" of before
   *  0.99.19 as well), Leon's jacket with its setting (CoreActivity.LEON_JACKET), else Normal. */
  static int costumeChoice(int bits, boolean jacket) {
    return (bits & COSTUME_BIT) != 0 ? COSTUME_SPECIAL : jacket ? COSTUME_JACKET : COSTUME_NORMAL;
  }

  /** The cheat bits of a costume choice (Special: COSTUME_BIT, the others without it). */
  static int costumeBits(int bits, int choice) {
    return choice == COSTUME_SPECIAL ? bits | COSTUME_BIT : bits & ~COSTUME_BIT;
  }

  /** "Costumes · Normal / Leon's jacket / Special" (1.0, the project owner, 16.11.2026: "ein Toggle-Button ... und nur
   *  mehr 'Kostüme'", without the note "(Leon/Ashley only)"): each press the next, as Frame skip; lit but at Normal.
   *  Normal: the game's own costumes. Leon's jacket: Leon keeps his brown jacket for the whole game (patch 0235,
   *  CoreBridge.setLeonJacket). Special: the special costumes of Leon and Ashley (COSTUME_BIT). */
  private void costumeSwitch(MenuLayout.Buttons parent) {
    final int[] labels = {R.string.ingame_cheat_costume_normal, R.string.ingame_cheat_costume_jacket,
        R.string.ingame_cheat_costume_special};
    final android.content.SharedPreferences game =
        getContext().getSharedPreferences(CoreActivity.GAME_PREFS, Context.MODE_PRIVATE);
    final int now = costumeChoice(cheatBits, game.getBoolean(CoreActivity.LEON_JACKET, false));
    selector(parent, R.string.ingame_cheat_costume, texts(labels), now, COSTUME_NORMAL, next -> {
      final boolean jacket = game.getBoolean(CoreActivity.LEON_JACKET, false);
      final int bits = costumeBits(cheatBits, next);
      if (bits != cheatBits) {
        cheatBits = bits;
        host.cheatsChanged(cheatBits);
      }
      if ((next == COSTUME_JACKET) != jacket) host.leonJacketChanged(next == COSTUME_JACKET);
    });
  }
  /** The game is held while the menu is open: the core does a button at once and answers. */
  private void cheatAction(int what, int label) {
    if (working) return;
    final int serial = requestSerial;
    new Thread(() -> {
      int result;
      try { result = host.cheatAction(what); }
      catch (RuntimeException | UnsatisfiedLinkError e) { result = -2; }
      final int done = result;
      main.post(() -> {
        if (!isShowing() || serial != requestSerial || page != CHEATS || cheatStatus == null) return;
        if (what == 4 && (done == 1 || done == 2 || done == 3)) {
          // kept by the app as well: a system file read without it (no save since) gets it back
          cheatBits |= 16; host.cheatsChanged(cheatBits);
          if (done == 2) {
            cheatStatus.setText(R.string.ingame_cheat_extras_already); cheatStatus.setVisibility(View.VISIBLE);
          } else {
            askRestartForExtras();
          }
          return;
        }
        String text;
        if (done == 1) text = getContext().getString(R.string.ingame_cheat_done, s(label));
        else if (done == 2 && what == 10) text = s(R.string.ingame_cheat_refill_full);
        else if (done == 2 && (what == 1 || (what >= 5 && what <= 9))) text = getContext().getString(R.string.ingame_cheat_raised, s(label));
        else if (done == 2) text = getContext().getString(R.string.ingame_cheat_already, s(label));
        else if (done == 0) text = s(R.string.ingame_cheat_not_now);
        else if (done == -1) text = s(R.string.ingame_cheat_no_room);
        else if (done == 3) text = s(R.string.ingame_cheat_queued);
        else text = s(R.string.ingame_cheat_unsupported);
        cheatStatus.setText(text); cheatStatus.setVisibility(View.VISIBLE);
      });
    }, "Cheat").start();
  }

  /** The extra games are unlocked: the title menu shows them after a restart, now or later. */
  private void askRestartForExtras() {
    Runnable later = () -> {
      if (cheatStatus == null) return;
      cheatStatus.setText(R.string.ingame_cheat_extras_later); cheatStatus.setVisibility(View.VISIBLE);
    };
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_language_restart_title)
        .setMessage(R.string.ingame_cheat_extras_text)
        .setNegativeButton(R.string.ingame_later, (d, w) -> later.run())
        .setPositiveButton(R.string.ingame_restart, (d, w) -> { host.restartGame(); close(); }).create();
    dialog.setOnCancelListener(d -> later.run());
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    Immersive.show(dialog);
  }

  private void refreshInstant() { instant(0); }
  private void confirmInstant(int operation) {
    if (working) return;
    if (operation == 1 && !slotPresent) { instant(1); return; }
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(operation == 1 ? R.string.ingame_instant_overwrite_title : R.string.ingame_instant_load_title)
        .setMessage(operation == 1 ? R.string.ingame_instant_overwrite : R.string.ingame_instant_load_warning)
        .setNegativeButton(android.R.string.cancel, null)
        .setPositiveButton(operation == 1 ? R.string.ingame_replace : R.string.ingame_instant_load, (d, w) -> instant(operation)).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    Immersive.show(dialog);
  }
  private void instant(int operation) {
    if (working) return;
    final int serial = ++requestSerial;
    // Disabling a focused button drops the controller focus; remember it and give it back afterwards.
    final View focused = getCurrentFocus();
    working = true; saveSlot.setEnabled(false); loadSlot.setEnabled(false);
    showSlotStatus(operation == 0 ? R.string.ingame_instant_checking : R.string.ingame_instant_working);
    new Thread(() -> {
      long[] result;
      try { result = CoreBridge.instant(operation); }
      catch (RuntimeException | UnsatisfiedLinkError e) { result = new long[] {-5, 0, 0, 0, 0, 0, 0, 0}; }
      final long[] done = result;
      main.post(() -> {
        working = false;
        if (!isShowing() || serial != requestSerial || page != HOME) return;
        if (done == null || done.length < 8) showSlotStatus(R.string.ingame_instant_failed);
        else showInstant(done);
        if (focused != null) { if (focused.isEnabled()) focused.requestFocus(); else resume.requestFocus(); }
        if (done == null || done.length < 8) return;
        if (operation == 2 && done[0] == 0) close();
        else if (operation == 1 && done[0] == 0) showSlotStatus(R.string.ingame_instant_saved);
      });
    }, "InstantSlot").start();
  }
  private void showInstant(long[] data) {
    saveSlot.setEnabled(data[2] != 0); loadSlot.setEnabled(data[3] != 0);
    if (data[0] == -1) { showSlotStatus(R.string.ingame_instant_unavailable); return; } // busy: keep the slot details shown
    slotPresent = data[1] != 0;
    if (slotPresent) {
      String room = String.format(Locale.ROOT, "r%03x", data[4]);
        String play = String.format(Locale.ROOT, "%02d:%02d:%02d", data[6] / 3600, data[6] / 60 % 60, data[6] % 60);
      slotDetails.setText(getContext().getString(R.string.ingame_slot_details, String.format(Locale.ROOT, "%d–%d", data[5] / 100, data[5] % 100), room, play));
      String when = DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(new Date(data[7] * 1000));
      slotDate.setText(getContext().getString(R.string.ingame_slot_date, when));
    } else { slotDetails.setText(s(R.string.ingame_instant_empty)); slotDate.setText(""); }
    int status = R.string.ingame_instant_ready;
    if (data[0] == -5) status = R.string.ingame_instant_unsupported;
    else if (data[0] == -3) status = R.string.ingame_instant_damaged;
    else if (data[0] == -4) status = R.string.ingame_instant_failed;
    // saving refused at this place: said so even when the slot is empty (loading is not possible then either)
    else if (data[0] != -1 && data[2] == 0 && data.length > 8 && data[8] != 0) status = R.string.ingame_instant_no_save;
    else if (data[0] == -1 || (data[2] == 0 && data[3] == 0)) status = R.string.ingame_instant_unavailable;
    showSlotStatus(status);
  }
  private void showSlotStatus(int message) {
    slotStatus.setText(s(message));
    // Ready adds no information beyond the enabled buttons; keep useful progress/errors visible.
    slotStatus.setVisibility(message == R.string.ingame_instant_ready ? View.GONE : View.VISIBLE);
  }
  void refreshCard() {
    if (cardLine == null || exportAll == null) return;
    String[] rows = CoreBridge.cardList();
    // the .zip also without saves (the touch layout, the instant save and the settings), not while the card is busy
    if (rows == null) {
      cardLine.setText(s("busy".equals(CoreBridge.cardNote()) ? R.string.ingame_card_busy : R.string.ingame_card_none));
      cardLine.setVisibility(View.VISIBLE); exportAll.setEnabled(false);
      if (exportZip != null) exportZip.setEnabled(false);
    } else {
      cardLine.setVisibility(View.GONE); exportAll.setEnabled(rows.length > 0);
      if (exportZip != null) exportZip.setEnabled(true);
    }
  }
  private String version() {
    try {
      return AppUpdate.plain(getContext().getPackageManager().getPackageInfo(getContext().getPackageName(), 0).versionName);
    } catch (android.content.pm.PackageManager.NameNotFoundException e) { return null; }
  }
  /** "+5°", "0°", "−5°": the field of view's change in degrees, digits 0..9 in every language. */
  static String degrees(float value) {
    int d = Math.round(value);
    return (d > 0 ? "+" : d < 0 ? "\u2212" : "") + Math.abs(d) + "°";
  }
  /** "1×", "1.25×", "3×": the gyro's sensitivity, digits 0..9 in every language (Locale.ROOT). */
  static String times(float value) {
    String v = String.format(Locale.ROOT, "%.2f", value);
    if (v.contains(".")) {
      while (v.endsWith("0")) v = v.substring(0, v.length() - 1);
      if (v.endsWith(".")) v = v.substring(0, v.length() - 1);
    }
    return v + "×";
  }

  /** The port's page (wish of the project owner, 19.10.2026). */
  static final String SOURCE_URL = "https://github.com/Andiweli/CubeEvil4-Android";

  private void openSource() {
    try {
      getContext().startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(SOURCE_URL)));
    } catch (ActivityNotFoundException e) {
      android.widget.Toast.makeText(getContext(), s(R.string.ingame_no_browser), android.widget.Toast.LENGTH_SHORT).show();
    }
  }

  /**
   * Auto (as core_mode.cpp chooses it): one level below the one whose 336 picture rows come
   * closest to the screen's rows (wish of the project owner, 19.10.2026: 1080 rows take 2).
   */
  static int autoScale(int screenWidth, int screenHeight) {
    int rows = Math.min(screenHeight, screenWidth * 9 / 16);
    return Math.max(1, Math.min(3, (rows + 168) / 336) - 1);
  }

  /**
   * The game's picture rows the screen shows for a picture format: all 448 with "4:3 (GC)" and "4:3", the 336 of
   * the letterbox with the 16:9 formats; "Native" on a screen narrower than 16:9 the rows that fill its width
   * (336 x (16/9) / aspect, at most 448; core_mode.cpp, re4mm::Native), else 336.
   */
  static int pictureRows(int aspect, int screenWidth, int screenHeight) {
    if (aspect == 1 || aspect == 3 || aspect == 4) return 448;
    if (aspect != 5 || screenWidth <= 0 || screenHeight <= 0) return 336;
    double a = (double) screenWidth / screenHeight;
    if (a >= 16.0 / 9.0 / 1.005) return 336;
    return Math.min(448, (int) (336.0 * 16.0 / 9.0 / a + 0.5) & ~1);
  }

  /** "Dynamic"'s highest level (halves), as core_mode.cpp takes it: the benchmark's level `cap` for 30 or 60 FPS
   *  (since 0.99.14 nothing above it; until 0.99.13 half a level more), without one the screen's (`screen`, whole
   *  levels, autoScale). */
  static int dynamicTop(int cap, int screen) {
    return cap >= 2 && cap <= 8 ? cap : 2 * screen;
  }

  /** The resolution the chosen level gives. */
  private void showGraphics() {
    int chosen = scaleHalves(graphics), halves = chosen;
    if (halves < 2 || halves > 8) {
      int cap = graphics.getInt(graphics.getBoolean("smooth60", false) ? DYN_CAP_60 : DYN_CAP_30, 0);
      halves = dynamicTop(cap, autoScale(screenWidth, screenHeight));
    }
    int rows = pictureRows(graphics.getInt("aspect", 0), screenWidth, screenHeight);
    // the numbers as digits 0..9 in every language (Locale.ROOT), the level with the language's separator
    if (graphicsLine == null) return;
    graphicsLine.setText(String.format(Locale.ROOT, s(chosen == 0 ? R.string.ingame_resolution_line_auto
        : R.string.ingame_resolution_line), 640 * halves / 2, rows * halves / 2,
        scaleNumber(halves, Locale.getDefault()), screenWidth, screenHeight));
  }

  /**
   * "File 01 · r100 · play time 0:12:34 · 2026-10-17 00:48" for a save, the system file by name; for
   * the error report, so in English in every language.
   */
  static String describe(String[] f) {
    String name = f[1];
    boolean intact = "1".equals(f[4]);
    if ("bh4_system".equals(name)) {
      return "System file (options)" + (intact ? "" : " – damaged");
    }
    if ("1".equals(f[3])) {
      int no = 0;
      try {
        no = Integer.parseInt(name.substring(8)) + 1;
      } catch (RuntimeException e) {
        no = 0;
      }
      int room = Integer.parseInt(f[5]), play = Integer.parseInt(f[8]);
      String date = String.format(Locale.ROOT, "%04d-%02d-%02d %02d:%02d", Integer.parseInt(f[9]),
          Integer.parseInt(f[10]), Integer.parseInt(f[11]), Integer.parseInt(f[12]), Integer.parseInt(f[13]));
      return String.format(Locale.ROOT, "File %02d · r%03x · play time %d:%02d:%02d · %s%s", no, room, play / 3600, play / 60 % 60, play % 60, date,
          intact ? "" : " – damaged");
    }
    return name + " (" + f[2] + " blocks)";
  }

  /**
   * The menu's window animation (0.99.17, wish of the project owner): up from the screen's bottom as it opens, back down
   * as it closes, however it opens and closes (Android plays it on the window, the game does not wait for it); none
   * when Android's animations are off.
   */
  static int windowAnimations(boolean animationsOn) {
    return animationsOn ? R.style.CoreMenuSlide : R.style.CoreMenuNoAnimation;
  }

  /** Android's animations are on: the animator duration scale is not 0 ("Remove animations" sets it to 0). */
  static boolean animationsOn(Context c) {
    if (android.os.Build.VERSION.SDK_INT >= 26) return android.animation.ValueAnimator.areAnimatorsEnabled();
    return android.provider.Settings.Global.getFloat(c.getContentResolver(),
        android.provider.Settings.Global.ANIMATOR_DURATION_SCALE, 1f) != 0f;
  }

  @Override protected void onStart() {
    super.onStart();
    if (getWindow() != null) {
      // before the window is added (Dialog.show calls onStart first): the opening slides too
      getWindow().setWindowAnimations(windowAnimations(animationsOn(getContext())));
      getWindow().setBackgroundDrawableResource(android.R.color.transparent);
      resizeWindow();
      getWindow().addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
      WindowManager.LayoutParams attrs = getWindow().getAttributes(); attrs.dimAmount = .72f;
      attrs.alpha = .90f; // Entire app-menu window: 10% transparency, 90% opacity.
      getWindow().setAttributes(attrs);
    }
    if (hostDecor != null) hostDecor.addOnLayoutChangeListener(windowLayout);
    resume.requestFocus();
    if (page == HOME && !working) refreshInstant();
  }
  private void resizeWindow() {
    if (getWindow() == null) return;
    android.util.DisplayMetrics metrics = getContext().getResources().getDisplayMetrics();
    int width = hostDecor != null && hostDecor.getWidth() > 0 ? hostDecor.getWidth() : metrics.widthPixels;
    int height = hostDecor != null && hostDecor.getHeight() > 0 ? hostDecor.getHeight() : metrics.heightPixels;
    // in a car: within the game's area, without the bars the car keeps (CarDisplay)
    View area = hostDecor != null && CarDisplay.is(getContext()) ? hostDecor.findViewById(android.R.id.content) : null;
    if (area != null && area.getWidth() > 0 && area.getHeight() > 0) {
      width = area.getWidth() - area.getPaddingLeft() - area.getPaddingRight();
      height = area.getHeight() - area.getPaddingTop() - area.getPaddingBottom();
    }
    // the game's window reaches over the camera's cutout (0.99.17), the menu's window does not (Cutout)
    final int[] cutout = Cutout.safe(hostDecor);
    width -= cutout[0] + cutout[2];
    height -= cutout[1] + cutout[3];
    getWindow().setLayout(Math.min(dp(1000), Math.round(width * .96f)),
        Math.min(dp(650), Math.round(height * .94f)));
  }
  @Override protected void onStop() {
    if (hostDecor != null) hostDecor.removeOnLayoutChangeListener(windowLayout);
    super.onStop();
  }
  @Override public boolean dispatchKeyEvent(KeyEvent event) {
    if (captureAction >= 0 && page == CONTROLLER) return captureKey(event);
    int key = event.getKeyCode();
    if (key == KeyEvent.KEYCODE_ESCAPE) {
      if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) goBack(); return true;
    }
    if (key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BACK) {
      if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) goBack(); return true;
    }
    if (key == KeyEvent.KEYCODE_BUTTON_SELECT || key == KeyEvent.KEYCODE_BUTTON_START) {
      if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) close(); return true;
    }
    if (key == KeyEvent.KEYCODE_BUTTON_A) return super.dispatchKeyEvent(new KeyEvent(event.getDownTime(), event.getEventTime(),
        event.getAction(), KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount()));
    return super.dispatchKeyEvent(event);
  }
}
