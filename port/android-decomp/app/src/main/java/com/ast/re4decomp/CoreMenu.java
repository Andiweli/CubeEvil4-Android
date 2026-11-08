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
    void importFiles();
    /** "Import other discs" (the menu asked first): the game ends and the start screen opens without starting it. */
    void importDiscs();
    void shareReport();
    void saveReport();
    void logChanged(boolean on);
    void toolsChanged(boolean on);
    void fpsChanged(int place);  // CoreActivity.FPS_PLACE: 0 off, 1..4 where
    /** The setting "Touch Controls": TouchShowRule.AUTO, FORCE or SUPPRESS (the preferences hold it). */
    void touchModeChanged(int mode);
    /** The switch "Use Gyro-Aiming" (the preferences hold it). */
    void gyroChanged(boolean on);
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
    /** The update button: downloads the newest APK and hands it to Android's installer (AppUpdate.install). */
    void startUpdate(AppUpdate.Listener listener);
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
  private static final int PANEL = 0xff191f1c, SURFACE = 0xff222a25, BORDER = 0xff414b41;
  private static final int INK = 0xfff0eee5, MUTED = 0xffa6afa3, GOLD = 0xffd7bc7d, ACTIVE = 0xff45432e;
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
  private static final int UPDATE_RED = 0xff7a2a26, UPDATE_RED_EDGE = 0xffb2463f;
  // the warning before another player character (08.11.2026): a dark red under the text, the "Update" button's edge
  private static final int WARN_RED = 0xff4e1f1c;
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
  private int cheatBits;  // the cheat switches as the page sent them last (CoreActivity.CHEATS)
  private Button exportAll, saveSlot, loadSlot;
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
    update = button(s(R.string.ingame_update), v -> { if (host != null) host.startUpdate(updates); });
    update.setVisibility(View.GONE);
    LinearLayout actions = row();
    LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
        ViewGroup.LayoutParams.WRAP_CONTENT);
    gap.setMarginEnd(dp(8));
    actions.addView(update, gap); actions.addView(resume);
    root.addView(new MenuLayout.Header(context, back, title, actions), new LinearLayout.LayoutParams(-1, -2));
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

  /** The update button's look: green "Up to date" (off), red "Update", the download's progress on it. */
  private final AppUpdate.Listener updates = new AppUpdate.Listener() {
    @Override public void checked(int state) { showUpdate(state); }
    @Override public void progress(int percent, String problem) {
      if (percent < 0) {
        if (problem != null) android.widget.Toast.makeText(getContext(), problem, android.widget.Toast.LENGTH_LONG).show();
        showUpdate(AppUpdate.OLDER);
        return;
      }
      update.setEnabled(false);
      update.setText(percent >= 100 ? s(R.string.ingame_update_install)
          : getContext().getString(R.string.ingame_update_loading, percent));
    }
  };

  void showUpdate(int state) {
    if (state == AppUpdate.UNKNOWN) { update.setVisibility(View.GONE); return; }
    boolean older = state == AppUpdate.OLDER;
    int fill = older ? UPDATE_RED : UPDATE_GREEN, stroke = older ? UPDATE_RED_EDGE : UPDATE_GREEN_EDGE;
    StateListDrawable look = new StateListDrawable();
    look.addState(new int[] {android.R.attr.state_focused}, focused(fill));
    look.addState(new int[] {android.R.attr.state_pressed}, shape(fill, INK, 6));
    look.addState(new int[0], shape(fill, stroke, 6));
    update.setBackground(look);
    update.setTextColor(INK);
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
  private GradientDrawable focused(int fill) {
    GradientDrawable d = shape(fill, INK, 6); d.setStroke(dp(2), INK); return d;
  }
  private void style(Button b) {
    StateListDrawable state = new StateListDrawable();
    state.addState(new int[] {android.R.attr.state_focused, android.R.attr.state_selected}, focused(ACTIVE));
    state.addState(new int[] {android.R.attr.state_focused}, focused(SURFACE));
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
  private void help(int id) { help(body, id); }
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
    Button b = button(label, click); parent.addButton(b); return b;
  }
  private void choices(int label, String[] labels, int[] values, int selected, int limit, Changed<Integer> changed) {
    choices(label, labels, values, selected, limit, labels.length, changed);
  }
  /** At most `columns` buttons a row (fewer when narrow); further rows line up under the first. */
  private Button[] choices(int label, String[] labels, int[] values, int selected, int limit, int columns,
                           Changed<Integer> changed) {
    return choices(label, labels, values, selected, limit, columns, false, changed);
  }
  /**
   * Three options a row in quarters (wish of the project owner, 28.10.2026): the label takes the first
   * quarter and each option one, so two options side by side are as wide as one button of the rows of
   * two above them, and the rows keep the same gap as those rows.
   */
  private Button[] quarterChoices(int label, String[] labels, int[] values, int selected, Changed<Integer> changed) {
    return choices(label, labels, values, selected, Integer.MAX_VALUE, 3, true, changed);
  }
  private Button[] choices(int label, String[] labels, int[] values, int selected, int limit, int columns,
                           boolean quarters, Changed<Integer> changed) {
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
    MenuLayout.Buttons row = new MenuLayout.Buttons(getContext(), columns, 2);
    body.addView(row, new LinearLayout.LayoutParams(-1, -2));
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
    LinearLayout outer = new MenuLayout.Columns(getContext(), 170, 640, false);
    outer.addView(rowLabel(s(R.string.ingame_resolution)));
    int[] values = {0, 2, 4, 6, 8, 3, 5, 7};
    // the buttons' order by level when the row does not fit
    MenuLayout.Keys keys = new MenuLayout.Keys(getContext(), 5, 1, new int[] {0, 1, 5, 2, 6, 3, 7, 4});
    outer.addView(keys);
    String[] labels = new String[values.length];
    for (int i = 0; i < values.length; i++) labels[i] = values[i] == 0 ? s(R.string.ingame_dynamic) : scaleLabel(values[i]);
    Button[] buttons = new Button[values.length];
    int selected = scaleHalves(graphics);
    for (int i = 0; i < values.length; i++) {
      final int index = i;
      Button b = button(labels[i], v -> {
        select(buttons, labels, index);
        graphics.edit().putInt("scaleHalves", values[index]).apply(); host.graphicsChanged(); showGraphics();
      });
      b.setSelected(values[i] == selected);
      b.setContentDescription(labels[i] + ", " + s(b.isSelected() ? R.string.ingame_on : R.string.ingame_off));
      keys.addButton(b); buttons[i] = b;
    }
    // the benchmark under "Dynamic" in a quiet green (wish of the project owner, 05./06.11.2026): the game goes to the
    // lake's dock (the project owner's save there, 06.11.2026) and measures which level holds 30 and 60 FPS with the
    // other options as they are; then the resolution becomes Dynamic, up to those levels
    Button bench = button(s(R.string.ingame_benchmark), v -> askBenchmark());
    StateListDrawable quiet = new StateListDrawable();
    quiet.addState(new int[] {android.R.attr.state_focused}, focused(BENCH_GREEN));
    quiet.addState(new int[] {android.R.attr.state_pressed}, shape(BENCH_GREEN, UPDATE_GREEN_EDGE, 6));
    quiet.addState(new int[0], shape(BENCH_GREEN, BENCH_GREEN_EDGE, 6));
    bench.setBackground(quiet);
    bench.setTextColor(BENCH_TEXT);
    bench.setTypeface(Typeface.DEFAULT, Typeface.NORMAL);
    bench.setEnabled(host != null);
    keys.addUnderFirst(bench);
    body.addView(outer);
  }
  /** "Import other discs": the game ends for it (progress not saved is lost), so the menu asks first. */
  private void askImportDiscs() {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_import_discs)
        .setMessage(R.string.ingame_import_discs_text)
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_start_screen, (d, w) -> host.importDiscs()).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    dialog.show();
  }
  /** "Clear shader cache": the game restarts for it (progress not saved is lost), so the menu asks first. */
  private void askClearShaders() {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_shader_clear)
        .setMessage(R.string.ingame_shader_clear_text)
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_restart, (d, w) -> { close(); host.clearShaderCache(); }).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    dialog.show();
  }
  /** "Start the benchmark?" with what it does; the menu closes for it. */
  private void askBenchmark() {
    AlertDialog dialog = new AlertDialog.Builder(getContext())
        .setTitle(R.string.ingame_benchmark)
        .setMessage(R.string.ingame_benchmark_text)
        .setNegativeButton(R.string.ingame_cancel, null)
        .setPositiveButton(R.string.ingame_benchmark_start, (d, w) -> { close(); host.startBenchmark(); }).create();
    dialog.setOnShowListener(d -> dialog.getButton(AlertDialog.BUTTON_NEGATIVE).requestFocus());
    dialog.show();
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
  private void graphicChoices(int label, String key, String[] labels, int[] values, int fallback, int limit) {
    choices(label, labels, values, graphics.getInt(key, fallback), limit, value -> {
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
  private void fpsLabel(Button b, int place, boolean on) { valueLabel(b, R.string.ingame_fps_display, place, on); }
  /** "Name · value" on a button that moves through its values with each press, lit while not off. */
  private void valueLabel(Button b, int name, int value, boolean on) {
    b.setSelected(on);
    b.setText(getContext().getString(R.string.ingame_toggle_value, s(name), s(value)));
  }
  /** The depth of field's level (0 off, 1 light, 2 strong); until 0.98.3 the switch "dof" (on: light). */
  static final String DOF_LEVEL = "dofLevel";
  /** The benchmark's levels on this device (halves): "Dynamic" goes up to them with 30 / 60 FPS. */
  static final String DYN_CAP_30 = "dynamicCap30", DYN_CAP_60 = "dynamicCap60";
  static int dofLevel(SharedPreferences g) {
    if (g.contains(DOF_LEVEL)) return Math.max(0, Math.min(2, g.getInt(DOF_LEVEL, 1)));
    return g.getBoolean("dof", true) ? 1 : 0;
  }
  /** The filter "Sharpen" (0.99.2): 0 off (the default), 1 light, 2 strong. */
  static final String SHARPEN_LEVEL = "sharpenLevel";
  static int sharpenLevel(SharedPreferences g) {
    return Math.max(0, Math.min(2, g.getInt(SHARPEN_LEVEL, 0)));
  }
  private void toggleLabel(Button b, int label, boolean on) {
    b.setSelected(on);
    b.setText(getContext().getString(R.string.ingame_toggle_value, s(label), s(on ? R.string.ingame_on : R.string.ingame_off)));
  }

  private void showPage(int next, boolean focus) {
    if (working) return;
    scrollY[page] = scroll.getScrollY(); page = next; ++requestSerial;
    body.removeAllViews(); graphicsLine = null; cardLine = null; exportAll = null; cheatStatus = null;
    bindingButtons = null; controllerButton = null; mapStatus = null;
    if (next != CONTROLLER) { captureAction = -1; main.removeCallbacks(captureTimeout); }
    back.setVisibility(page == HOME ? View.GONE : View.VISIBLE);
    int[] titles = {R.string.ingame_title, R.string.ingame_display, R.string.ingame_audio,
        R.string.ingame_controls_page, R.string.ingame_performance, R.string.ingame_cheats,
        R.string.ingame_management, R.string.ingame_controller_config};
    if (page == HOME && version() != null) {
      SpannableStringBuilder t = new SpannableStringBuilder(s(titles[page]));
      int start = t.length(); t.append(" (v").append(version()).append(")");
      t.setSpan(new RelativeSizeSpan(.6f), start, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      t.setSpan(new StyleSpan(Typeface.NORMAL), start, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      title.setText(t);
    } else title.setText(s(titles[page]));
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
    heading(R.string.ingame_settings);
    MenuLayout.Buttons first = buttonsRow(body), second = buttonsRow(body), third = buttonsRow(body);
    navigation[DISPLAY] = nav(first, R.string.ingame_display, R.string.ingame_display_summary, DISPLAY);
    navigation[AUDIO] = nav(first, R.string.ingame_audio, R.string.ingame_audio_summary, AUDIO);
    navigation[CONTROLS] = nav(second, R.string.ingame_controls_page, R.string.ingame_controls_summary, CONTROLS);
    navigation[PERFORMANCE] = nav(second, R.string.ingame_performance, R.string.ingame_performance_summary, PERFORMANCE);
    navigation[CHEATS] = nav(third, R.string.ingame_cheats, R.string.ingame_cheats_summary, CHEATS);
    navigation[MANAGEMENT] = nav(third, R.string.ingame_management, R.string.ingame_management_summary, MANAGEMENT);
    if (host != null && host.gameLanguages()) gameLanguage();
    main.post(() -> { if (isShowing() && page == HOME && !working) refreshInstant(); });
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
      dialog.show();
    });
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
  // (effects: depth of field, lens flares; the two picture filters), then OPTIONAL FEATURES (minimap, FPS display)
  private void display() {
    heading(R.string.ingame_graphics);
    renderer();
    int[] info = CoreBridge.graphicsInfo();
    // Unknown GPU capabilities must not enable unsupported samples or anisotropy.
    int samples = info == null ? 1 : Math.max(1, info[0]);
    int anisotropy = info == null ? 1 : Math.max(1, info[1]);
    // "Native" (5, wish of the project owner, 07.11.2026): the screen's own aspect (core_mode.cpp, re4mm::Native)
    graphicChoices(R.string.ingame_picture, "aspect", new String[] {s(R.string.ingame_original_43), s(R.string.ingame_43_no_bars), "16:9", s(R.string.ingame_native), s(R.string.ingame_stretched)},
        new int[] {1, 3, 0, 5, 2}, 0, Integer.MAX_VALUE);
    resolutionChoices();
    graphicsLine = indentedHelp(""); graphicsLine.setPadding(dp(MenuLayout.GAP), dp(4), dp(MenuLayout.GAP), dp(10)); showGraphics();
    graphicChoices(R.string.ingame_antialiasing, "samples", new String[] {s(R.string.ingame_off), "2× MSAA", "4× MSAA"},
        new int[] {1, 2, 4}, 1, samples);
    toggle(buttonsRow(labelledOptions(0)), R.string.ingame_fxaa, graphics.getBoolean("fxaa", false), on -> {
      graphics.edit().putBoolean("fxaa", on).apply(); CoreBridge.setFxaa(on);
    });
    indentedHelp(s(R.string.ingame_fxaa_help));
    graphicChoices(R.string.ingame_texture_filter, "anisotropy", new String[] {s(R.string.ingame_off), "2×", "4×", "8×", "16×"},
        new int[] {1, 2, 4, 8, 16}, 1, anisotropy);
    heading(R.string.ingame_postprocessing);
    LinearLayout effects = labelledOptions(R.string.ingame_effects);
    // each press moves the depth of field on: off, light (as before), strong (wish of the project owner, 05.11.2026)
    final int[] dofLabels = {R.string.ingame_off, R.string.ingame_dof_light, R.string.ingame_dof_strong};
    Button dof = cellAction(buttonsRow(effects), "", v -> {
      int next = (dofLevel(graphics) + 1) % 3;
      graphics.edit().putInt(DOF_LEVEL, next).putBoolean("dof", next != 0).apply();
      valueLabel((Button) v, R.string.ingame_dof, dofLabels[next], next != 0);
      CoreBridge.setDof(next);
    });
    valueLabel(dof, R.string.ingame_dof, dofLabels[dofLevel(graphics)], dofLevel(graphics) != 0);
    // a faint glow at torches, candles and fires (wish of the project owner, 04.11.2026; off by default)
    toggle(buttonsRow(effects), R.string.ingame_flares, graphics.getBoolean("flares", false), on -> {
      graphics.edit().putBoolean("flares", on).apply(); CoreBridge.setFlares(on);
    });
    // the picture filters one under the other, one general line under both (wish of the project owner, 02.11.2026)
    LinearLayout filters = labelledOptions(R.string.ingame_filter);
    // the GameCube's own softening for the TV (wish of the project owner, 01.11.2026)
    toggle(buttonsRow(filters), R.string.ingame_tv_filter, graphics.getBoolean("tvFilter", false), on -> {
      graphics.edit().putBoolean("tvFilter", on).apply(); CoreBridge.setTvFilter(on);
    });
    // the 3D as a PlayStation would have drawn it (wish of the project owner, 02.11.2026)
    toggle(buttonsRow(filters), R.string.ingame_psx, graphics.getBoolean("psx", false), on -> {
      graphics.edit().putBoolean("psx", on).apply(); CoreBridge.setPsx(on);
    });
    // a sharpening for the 16:9 formats, whose 336 rows are enlarged to the screen's height (wish of the project
    // owner, 08.11.2026); each press moves it on: off, light, strong, as the depth of field
    final int[] sharpenLabels = {R.string.ingame_off, R.string.ingame_dof_light, R.string.ingame_dof_strong};
    Button sharpen = cellAction(buttonsRow(filters), "", v -> {
      int next = (sharpenLevel(graphics) + 1) % 3;
      graphics.edit().putInt(SHARPEN_LEVEL, next).apply();
      valueLabel((Button) v, R.string.ingame_sharpen, sharpenLabels[next], next != 0);
      CoreBridge.setSharpen(next);
    });
    valueLabel(sharpen, R.string.ingame_sharpen, sharpenLabels[sharpenLevel(graphics)], sharpenLevel(graphics) != 0);
    indentedHelp(s(R.string.ingame_filter_help) + " " + s(R.string.ingame_sharpen_help));
    heading(R.string.ingame_optional_features);
    // the current room's outline at the bottom left, turning with the player (wish of the project owner, 04.11.2026)
    toggle(buttonsRow(labelledOptions(0)), R.string.ingame_minimap, graphics.getBoolean("minimap", false), on -> {
      graphics.edit().putBoolean("minimap", on).apply(); CoreBridge.setMinimap(on);
    });
    // the FPS display (here since 04.11.2026, before on PERFORMANCE under EXPERIMENTAL); each press moves it on
    // (wish of the project owner, 05.11.2026: it covered the minimap): off, bottom left, bottom centre, top left,
    // top centre, off again
    final int[] fpsLabels = {R.string.ingame_off, R.string.ingame_fps_bottom_left, R.string.ingame_fps_bottom_centre,
        R.string.ingame_fps_top_left, R.string.ingame_fps_top_centre};
    Button fps = cellAction(buttonsRow(labelledOptions(0)), "", v -> {
      int next = (CoreActivity.fpsPlace(graphics) + 1) % CoreActivity.FPS_PLACES;
      graphics.edit().putInt(CoreActivity.FPS_PLACE, next).putBoolean("fps", next != 0).apply();
      fpsLabel((Button) v, fpsLabels[next], next != 0);
      host.fpsChanged(next);
    });
    int fpsNow = CoreActivity.fpsPlace(graphics);
    fpsLabel(fps, fpsLabels[fpsNow], fpsNow != 0);
  }
  /** "Renderer": OpenGL ES or Vulkan (experimental, port/docs/vulkan-bauplan.md E7); a change restarts the game
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
      dialog.show();
    });
    if (!supported && buttons[0].length > 1) buttons[0][1].setEnabled(false);
    indentedHelp(s(supported ? R.string.ingame_renderer_help : R.string.ingame_renderer_unavailable));
  }
  private void controls() {
    // the touch controls under a heading of their own, a third of the row each (wish of the project owner, 05.11.2026)
    heading(R.string.ingame_touch_controls);
    rowChoices(new String[] {s(R.string.ingame_touch_auto), s(R.string.ingame_touch_force), s(R.string.ingame_touch_suppress)},
        new int[] {TouchShowRule.AUTO, TouchShowRule.FORCE, TouchShowRule.SUPPRESS}, CoreActivity.touchMode(input), 3,
        mode -> host.touchModeChanged(mode));
    help(R.string.ingame_touch_help);
    // the gyro in a section of its own, above the comfort controls (wishes of the project owner, 30.10.2026)
    heading(R.string.ingame_motion);
    boolean gyro = GyroAim.available(getContext()), gyroOn = gyro && input.getBoolean(CoreActivity.GYRO_AIM, false);
    Button[] invert = new Button[2];
    View[][] sensitivity = new View[1][];
    sensitivity[0] = switchWithSlider(gyro ? R.string.ingame_gyro : R.string.ingame_gyro_none, gyroOn, enabled -> {
      host.gyroChanged(enabled);
      sensitivity[0][1].setEnabled(enabled); sensitivity[0][2].setEnabled(enabled);
      for (Button b : invert) b.setEnabled(enabled);
    }, R.string.ingame_gyro_sensitivity, GyroAim.MIN_SENSITIVITY, GyroAim.MAX_SENSITIVITY, GyroAim.SENSITIVITY_STEP,
        GyroAim.clampSensitivity(input.getFloat(CoreActivity.GYRO_SENSITIVITY, 1f)), value -> host.gyroSensitivityChanged(value));
    sensitivity[0][0].setEnabled(gyro);
    sensitivity[0][1].setEnabled(gyroOn); sensitivity[0][2].setEnabled(gyroOn);
    // the gyro's directions (wish of the project owner, 27.10.2026): at once in GyroAim
    MenuLayout.Buttons directions = buttonsRow(body);
    invert[0] = toggle(directions, R.string.ingame_gyro_invert_v, input.getBoolean(CoreActivity.GYRO_INVERT_V, false),
        value -> host.gyroInvertChanged(true, value));
    invert[1] = toggle(directions, R.string.ingame_gyro_invert_h, input.getBoolean(CoreActivity.GYRO_INVERT_H, false),
        value -> host.gyroInvertChanged(false, value));
    for (Button b : invert) b.setEnabled(gyroOn);
    heading(R.string.ingame_comfort);
    MenuLayout.Buttons first = buttonsRow(body);
    toggle(first, R.string.ingame_alternate, input.getBoolean(CoreActivity.ALT_CONTROLS, false), on -> host.alternateChanged(on));
    toggle(first, R.string.ingame_knife_swipe, input.getBoolean(CoreActivity.KNIFE_SWIPE, true), on -> host.knifeSwipeChanged(on));
    MenuLayout.Buttons second = buttonsRow(body);
    toggle(second, R.string.ingame_latch, input.getBoolean(CoreActivity.TOUCH_LATCH, false), on -> host.latchChanged(on));
    toggle(second, R.string.ingame_latch_b, input.getBoolean(CoreActivity.TOUCH_LATCH_B, false), on -> host.latchBChanged(on));
    // "Auto aim" and "Always run" (wish of the project owner, 30.10.2026): after the latches, before the aiming row
    MenuLayout.Buttons third = buttonsRow(body);
    toggle(third, R.string.ingame_auto_aim, input.getBoolean(CoreActivity.AUTO_AIM, false), on -> host.autoAimChanged(on));
    toggle(third, R.string.ingame_always_run, input.getBoolean(CoreActivity.ALWAYS_RUN, false), on -> host.alwaysRunChanged(on));
    // aiming the knife and the guns by touch or a controller (wish of the project owner, 30.10.2026): the game's
    // own "reverse" option and the stick's speed while aiming, right under the latches
    switchWithSlider(R.string.ingame_aim_invert_v, input.getBoolean(CoreActivity.AIM_INVERT_V, false),
        on -> host.aimInvertChanged(on), R.string.ingame_aim_speed, CoreActivity.AIM_SPEED_MIN,
        CoreActivity.AIM_SPEED_MAX, CoreActivity.AIM_SPEED_STEP,
        CoreActivity.clampAimSpeed(input.getFloat(CoreActivity.AIM_SPEED, 1f)), value -> host.aimSpeedChanged(value));
    help(R.string.ingame_alternate_help); help(R.string.ingame_latch_help); help(R.string.ingame_auto_aim_help);
    help(R.string.ingame_always_run_help); help(R.string.ingame_aim_help); help(R.string.ingame_knife_swipe_help);
    // touch aiming (wish of the project owner, 28.10.2026): what aims while L or R is held; since 05.11.2026 a section
    // of its own, a third of the row each
    heading(R.string.ingame_touch_aim);
    rowChoices(new String[] {s(R.string.ingame_aim_move_stick), s(R.string.ingame_aim_touch_area),
        s(R.string.ingame_aim_c_stick)}, new int[] {CoreInput.AIM_MOVE_STICK, CoreInput.AIM_LOOK, CoreInput.AIM_C_STICK},
        input.getInt(CoreActivity.TOUCH_AIM, CoreInput.AIM_MOVE_STICK), 3, mode -> host.touchAimChanged(mode));
    help(R.string.ingame_touch_aim_help);
    // PLAYER VIEW above the laser sight (wish of the project owner, 05.11.2026): the far camera (a user's wish,
    // 01.11.2026; patch 0171) in the left half, the field of view (patch 0205) as a slider in the right half
    heading(R.string.ingame_player_view);
    View[] view = switchWithSlider(R.string.ingame_far_camera, input.getBoolean(CoreActivity.FAR_CAMERA, false), on -> {
      input.edit().putBoolean(CoreActivity.FAR_CAMERA, on).apply(); CoreBridge.setFarCamera(on);
    }, R.string.ingame_fov, -CoreActivity.FOV_RANGE, CoreActivity.FOV_RANGE, 1,
        CoreActivity.fovOffset(input), value -> {
          int degrees = Math.round(value);
          input.edit().putInt(CoreActivity.FOV_OFFSET, degrees).apply(); CoreBridge.setFov(degrees);
        }, CoreMenu::degrees);
    view[1].setContentDescription(s(R.string.ingame_fov));
    help(R.string.ingame_far_camera_help); help(R.string.ingame_fov_help);
    // the laser sight's colour, beam and dot (wish of the project owner, 04.11.2026); since 05.11.2026 without a
    // row label, a quarter of the row each
    heading(R.string.ingame_laser_section);
    rowChoices(new String[] {s(R.string.ingame_laser_red), s(R.string.ingame_laser_blue),
        s(R.string.ingame_laser_green), s(R.string.ingame_laser_yellow)}, new int[] {0, 1, 2, 3},
        input.getInt(CoreActivity.LASER_COLOUR, 0), 4, colour -> {
          input.edit().putInt(CoreActivity.LASER_COLOUR, colour).apply();
          CoreBridge.setLaserColour(colour);
        });
    // the controller configuration (wish of the project owner, 27.10.2026): at the bottom, only with a controller;
    // since 30.10.2026 under a heading of its own
    heading(R.string.ingame_controller_section);
    // the controller sticks' dead zone and range (wish of the project owner, 07.11.2026, GitHub issue #168: a left stick
    // that stuttered while walking) and where the game's vibration goes (07.11.2026: a Bluetooth controller's
    // motors instead of the phone's)
    final int[] shape = {CoreInput.clampDeadZone(input.getInt(CoreActivity.STICK_DEAD_ZONE, 0)),
        CoreInput.clampRange(input.getInt(CoreActivity.STICK_RANGE, 100))};
    percentSlider(R.string.ingame_stick_dead_zone, 0, CoreInput.DEAD_ZONE_MAX, shape[0], value -> {
      shape[0] = value; host.stickShapeChanged(shape[0], shape[1]);
    });
    percentSlider(R.string.ingame_stick_range, CoreInput.RANGE_MIN, 100, shape[1], value -> {
      shape[1] = value; host.stickShapeChanged(shape[0], shape[1]);
    });
    help(R.string.ingame_stick_help);
    choices(R.string.ingame_vibration, new String[] {s(R.string.ingame_touch_auto), s(R.string.ingame_vibration_device),
        s(R.string.ingame_vibration_controller), s(R.string.ingame_off)}, new int[] {RumbleTarget.AUTO,
        RumbleTarget.DEVICE, RumbleTarget.CONTROLLER, RumbleTarget.OFF},
        RumbleTarget.clampMode(input.getInt(CoreActivity.VIBRATION, RumbleTarget.AUTO)), Integer.MAX_VALUE, 4,
        mode -> host.vibrationChanged(mode));
    help(R.string.ingame_vibration_help);
    controllerButton = cellAction(buttonsRow(body), s(R.string.ingame_controller_config) + "   ›",
        v -> showPage(CONTROLLER, true));
    controllerButton.setEnabled(host != null && (host.controllerConnected() || host.keyboardConnected()));
    help(R.string.ingame_controller_config_help);
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
    LinearLayout line = new MenuLayout.Columns(getContext(), 0, 540, false);
    line.setGravity(Gravity.CENTER_VERTICAL);
    body.addView(line);
    Button toggle = toggle(buttonsRow(line), label, on, toggled);
    TextView heading = text(s(title), 15, false);
    heading.setGravity(Gravity.CENTER_HORIZONTAL);
    SeekBar bar = new SeekBar(getContext()); TextView value = text("", 15, false);
    LinearLayout.LayoutParams barSize = new LinearLayout.LayoutParams(0, dp(48), 1);
    LinearLayout box = new LinearLayout(getContext()) {
      @Override protected void onMeasure(int widthSpec, int heightSpec) {
        heading.measure(widthSpec, MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
        int row = Math.max(dp(48), toggle.getMeasuredHeight()) + dp(2 * MenuLayout.GAP);
        barSize.height = Math.max(dp(32), row - heading.getMeasuredHeight());
        super.onMeasure(widthSpec, heightSpec);
      }
    };
    box.setOrientation(LinearLayout.VERTICAL);
    box.addView(heading, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT));
    LinearLayout slider = row();
    slider.addView(bar, barSize); slider.addView(value); slider.setPaddingRelative(0, 0, dp(MenuLayout.GAP), 0);
    box.addView(slider);
    line.addView(box);
    bar.setMax(Math.round((max - min) / step)); bar.setProgress(Math.round((stored - min) / step));
    bar.setContentDescription(s(title));
    bar.setProgressTintList(ColorStateList.valueOf(GOLD)); bar.setThumbTintList(ColorStateList.valueOf(GOLD));
    value.setText(format.of(stored));
    bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      @Override public void onProgressChanged(SeekBar b, int progress, boolean fromUser) {
        float now = min + progress * step;
        value.setText(format.of(now)); if (fromUser) moved.accept(now);
      }
      @Override public void onStartTrackingTouch(SeekBar b) {}
      @Override public void onStopTrackingTouch(SeekBar b) {}
    });
    return new View[] {toggle, bar, value};
  }

  /** The actions' names, in ActionMap's order. */
  private static final int[] ACTION_LABELS = {R.string.ingame_act_forward, R.string.ingame_act_back,
      R.string.ingame_act_left, R.string.ingame_act_right, R.string.ingame_act_action, R.string.ingame_act_run,
      R.string.ingame_act_aim, R.string.ingame_act_fire, R.string.ingame_act_reload, R.string.ingame_act_knife,
      R.string.ingame_act_slash, R.string.ingame_act_ashley, R.string.ingame_act_inventory, R.string.ingame_act_map,
      R.string.ingame_act_options, R.string.ingame_act_menu};

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
    if (!keyboard && input.getBoolean(CoreActivity.ALT_CONTROLS, false)) help(R.string.ingame_controller_alternate_note);
    help(keyboard ? R.string.ingame_map_fixed_keyboard : R.string.ingame_map_fixed_controller);
    if (keyboard) mouseSlider();
    ActionMap def = ActionMap.defaults(mapKind), m = host == null ? null : host.mapping(mapKind);
    if (m == null) m = def;
    bindingButtons = new Button[ActionMap.ACTIONS];
    for (int a = 0; a < ActionMap.ACTIONS; a++) {
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

  /** The mouse's sensitivity (wish of the project owner, 29.10.2026), as the gyro's slider. */
  private void mouseSlider() {
    float min = CoreActivity.MOUSE_MIN, step = CoreActivity.MOUSE_STEP;
    LinearLayout line = new MenuLayout.Columns(getContext(), 170, 520, false);
    line.addView(rowLabel(s(R.string.ingame_mouse_sensitivity)));
    LinearLayout slider = row();
    SeekBar bar = new SeekBar(getContext());
    TextView value = text("", 15, false);
    slider.addView(bar, new LinearLayout.LayoutParams(0, dp(48), 1));
    slider.addView(value);
    slider.setPaddingRelative(0, 0, dp(MenuLayout.GAP), 0);  // the value ends where the buttons end
    line.addView(slider);
    float now = host == null ? 1f : host.mouseSensitivity();
    bar.setMax(Math.round((CoreActivity.MOUSE_MAX - min) / step));
    bar.setProgress(Math.round((now - min) / step));
    bar.setContentDescription(s(R.string.ingame_mouse_sensitivity));
    bar.setProgressTintList(ColorStateList.valueOf(GOLD)); bar.setThumbTintList(ColorStateList.valueOf(GOLD));
    value.setText(times(now));
    bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      @Override public void onProgressChanged(SeekBar b, int progress, boolean fromUser) {
        float v = min + progress * step;
        value.setText(times(v));
        if (fromUser && host != null) host.mouseSensitivityChanged(v);
      }
      @Override public void onStartTrackingTouch(SeekBar b) {}
      @Override public void onStopTrackingTouch(SeekBar b) {}
    });
    body.addView(line);
    help(R.string.ingame_mouse_help);
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
      boolean l = Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE)) > 0.6f;
      boolean r = Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS)) > 0.6f;
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
    volumeSlider(R.string.ingame_music_volume, "musicVolume");
    volumeSlider(R.string.ingame_effects_volume, "effectsVolume");
    MenuLayout.Buttons sound = buttonsRow(body);
    toggle(sound, R.string.ingame_music_volume, graphics.getBoolean("musicOn", true), on -> {
      graphics.edit().putBoolean("musicOn", on).apply(); CoreBridge.sendSoundVolume(graphics);
    });
    toggle(sound, R.string.ingame_effects_volume, graphics.getBoolean("effectsOn", true), on -> {
      graphics.edit().putBoolean("effectsOn", on).apply(); CoreBridge.sendSoundVolume(graphics);
    });
    help(R.string.ingame_volume_help);
  }
  /** A percentage as the volumes' sliders: `min` .. `max` in steps of 5 from `now`, each move to `moved`. */
  private void percentSlider(int label, int min, int max, int now, Changed<Integer> moved) {
    LinearLayout line = new MenuLayout.Columns(getContext(), 170, 520, false);
    line.addView(rowLabel(s(label)));
    LinearLayout slider = row();
    SeekBar bar = new SeekBar(getContext());
    TextView value = text("", 15, false);
    slider.addView(bar, new LinearLayout.LayoutParams(0, dp(48), 1));
    slider.addView(value);
    slider.setPaddingRelative(0, 0, dp(MenuLayout.GAP), 0);  // the value ends where the buttons end
    line.addView(slider);
    bar.setMax((max - min) / 5);
    bar.setProgress(Math.round((Math.max(min, Math.min(max, now)) - min) / 5f));
    bar.setContentDescription(s(label));
    bar.setProgressTintList(ColorStateList.valueOf(GOLD)); bar.setThumbTintList(ColorStateList.valueOf(GOLD));
    value.setText(now + " %");
    bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      @Override public void onProgressChanged(SeekBar b, int progress, boolean fromUser) {
        int v = min + progress * 5;
        value.setText(v + " %");
        if (fromUser && host != null) moved.accept(v);
      }
      @Override public void onStartTrackingTouch(SeekBar b) {}
      @Override public void onStopTrackingTouch(SeekBar b) {}
    });
    body.addView(line);
  }
  /** A volume of the AUDIO page as the mouse's slider: 0 .. 100 % in steps of 5, kept in `key`. */
  private void volumeSlider(int label, String key) {
    LinearLayout line = new MenuLayout.Columns(getContext(), 170, 520, false);
    line.addView(rowLabel(s(label)));
    LinearLayout slider = row();
    SeekBar bar = new SeekBar(getContext());
    TextView value = text("", 15, false);
    slider.addView(bar, new LinearLayout.LayoutParams(0, dp(48), 1));
    slider.addView(value);
    slider.setPaddingRelative(0, 0, dp(MenuLayout.GAP), 0);  // the value ends where the buttons end
    line.addView(slider);
    int now = Math.max(0, Math.min(100, graphics.getInt(key, 100)));
    bar.setMax(20);
    bar.setProgress(Math.round(now / 5f));
    bar.setContentDescription(s(label));
    bar.setProgressTintList(ColorStateList.valueOf(GOLD)); bar.setThumbTintList(ColorStateList.valueOf(GOLD));
    value.setText(now + " %");
    bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      @Override public void onProgressChanged(SeekBar b, int progress, boolean fromUser) {
        value.setText(progress * 5 + " %");
        if (fromUser) { graphics.edit().putInt(key, progress * 5).apply(); CoreBridge.sendSoundVolume(graphics); }
      }
      @Override public void onStartTrackingTouch(SeekBar b) {}
      @Override public void onStopTrackingTouch(SeekBar b) {}
    });
    body.addView(line);
  }

  private void performance() {
    heading(R.string.ingame_perf_section);
    // without a row label, half the row each (wish of the project owner, 05.11.2026)
    rowChoices(new String[] {s(R.string.ingame_perf_default), s(R.string.ingame_perf_cores)},
        new int[] {0, 1}, graphics.getInt("fastCores", 0), 2, value -> {
          graphics.edit().putInt("fastCores", value).apply(); CoreBridge.setFastCores(value == 1);
        });
    help(R.string.ingame_perf_help);
    // Potato mode (decision of the project owner, 30.10.2026): two switches that leave the expensive
    // surroundings effects out; the explanation only below them (runtime/potato.cpp in the core)
    heading(R.string.ingame_potato);
    MenuLayout.Buttons potato = buttonsRow(body);
    toggle(potato, R.string.ingame_potato_weather, graphics.getBoolean("weatherEffects", true), on -> {
      graphics.edit().putBoolean("weatherEffects", on).apply(); CoreBridge.setPotato(CoreBridge.potatoMask(graphics));
    });
    toggle(potato, R.string.ingame_potato_special, graphics.getBoolean("specialEffects", true), on -> {
      graphics.edit().putBoolean("specialEffects", on).apply(); CoreBridge.setPotato(CoreBridge.potatoMask(graphics));
    });
    // two more on a user's wish (08.11.2026, through the project owner): the particles of fights, weapons and
    // fires in three steps (each press moves it on: normal, less, none, as the sharpening), and shadows with the
    // light effects
    final int[] particleLabels = {R.string.ingame_particles_normal, R.string.ingame_particles_less,
        R.string.ingame_particles_none};
    MenuLayout.Buttons morePotato = buttonsRow(body);
    Button particles = cellAction(morePotato, "", v -> {
      int next = (CoreBridge.particleEffects(graphics) + 1) % 3;
      graphics.edit().putInt("particleEffects", next).apply();
      valueLabel((Button) v, R.string.ingame_potato_particles, particleLabels[next], next == 0);
      CoreBridge.setPotato(CoreBridge.potatoMask(graphics));
    });
    valueLabel(particles, R.string.ingame_potato_particles, particleLabels[CoreBridge.particleEffects(graphics)],
        CoreBridge.particleEffects(graphics) == 0);
    toggle(morePotato, R.string.ingame_potato_shadows, graphics.getBoolean("shadowsLight", true), on -> {
      graphics.edit().putBoolean("shadowsLight", on).apply(); CoreBridge.setPotato(CoreBridge.potatoMask(graphics));
    });
    help(R.string.ingame_potato_help);
    // the picture output under a heading of its own (wish of the project owner, 30.10.2026); since 05.11.2026
    // without a row label in two columns, frame skip | 60 FPS over merged draws | vertex ring, the explanations
    // over the whole width
    heading(R.string.ingame_experimental);
    MenuLayout.Buttons output = buttonsRow(body), more = buttonsRow(body);
    // Frame skip leaves pictures out, 60 FPS adds pictures between: one or the other (wish of the
    // project owner, 26.10.2026: both on jerked back and forth); switching one on switches the other off
    final Button[] skipAndSmooth = new Button[2];
    skipAndSmooth[0] = toggle(output, R.string.ingame_frame_skip, graphics.getBoolean("frameskip", false), on -> {
      graphics.edit().putBoolean("frameskip", on).apply(); CoreBridge.setFrameSkip(on);
      if (on && skipAndSmooth[1].isSelected()) {
        toggleLabel(skipAndSmooth[1], R.string.ingame_smooth60, false);
        graphics.edit().putBoolean("smooth60", false).apply(); CoreBridge.setSmooth(false);
      }
    });
    skipAndSmooth[1] = toggle(output, R.string.ingame_smooth60, graphics.getBoolean("smooth60", false), on -> {
      graphics.edit().putBoolean("smooth60", on).apply(); CoreBridge.setSmooth(on);
      if (on && skipAndSmooth[0].isSelected()) {
        toggleLabel(skipAndSmooth[0], R.string.ingame_frame_skip, false);
        graphics.edit().putBoolean("frameskip", false).apply(); CoreBridge.setFrameSkip(false);
      }
    });
    // merged draw calls (an experiment for phones with a slow graphics driver; wish of the project
    // owner, 30.10.2026: below 60 FPS); the renderer leaves it out while 60 FPS is on
    toggle(more, R.string.ingame_merge_draws, graphics.getBoolean("mergeDraws", false), on -> {
      graphics.edit().putBoolean("mergeDraws", on).apply(); CoreBridge.setMerge(on);
    });
    // the vertex ring at the bottom (wish of the project owner, 01.11.2026: off by default, for Mali)
    toggle(more, R.string.ingame_vertex_ring, graphics.getBoolean("vertexRing", false), on -> {
      graphics.edit().putBoolean("vertexRing", on).apply(); CoreBridge.setVertexRing(on);
    });
    help(R.string.ingame_frame_skip_help);
    help(R.string.ingame_smooth_help);
    help(R.string.ingame_skip_or_smooth);
    help(R.string.ingame_merge_help);
    help(R.string.ingame_vertex_ring_help);
  }
  private void management() {
    heading(R.string.ingame_card); help(R.string.ingame_card_help);
    cardLine = text("", 15, false); cardLine.setPadding(dp(MenuLayout.GAP), 0, dp(MenuLayout.GAP), 0); body.addView(cardLine);
    MenuLayout.Buttons cardButtons = buttonsRow(body);
    exportAll = cellAction(cardButtons, s(R.string.ingame_export), v -> host.exportAll());
    cellAction(cardButtons, s(R.string.ingame_import), v -> host.importFiles()); refreshCard();
    // another release over the imported discs (wish of the project owner, 06.11.2026)
    heading(R.string.ingame_discs); help(R.string.ingame_discs_help);
    cellAction(buttonsRow(body), s(R.string.ingame_import_discs), v -> askImportDiscs());
    heading(R.string.ingame_log); help(R.string.ingame_log_help);
    MenuLayout.Buttons log = buttonsRow(body);
    toggle(log, R.string.ingame_log_record, CoreLog.enabled(getContext()), on -> host.logChanged(on));
    toggle(log, R.string.ingame_log_capture, tools, on -> { tools = on; host.toolsChanged(on); });
    MenuLayout.Buttons reports = buttonsRow(body);
    cellAction(reports, s(R.string.ingame_report_send), v -> host.shareReport());
    cellAction(reports, s(R.string.ingame_report_save), v -> host.saveReport());
    // since 0.99.4 the shaders are kept where Android's "Clear cache" does not reach them (decision of the project
    // owner, 08.11.2026): emptied by hand here
    heading(R.string.ingame_shader_cache); help(R.string.ingame_shader_cache_help);
    cellAction(buttonsRow(body), s(R.string.ingame_shader_clear), v -> askClearShaders());
    heading(R.string.ingame_source);
    cellAction(buttonsRow(body), getContext().getString(R.string.ingame_source_line, SOURCE_URL), v -> openSource());
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
  private MenuLayout.Buttons cheatRow(int columns) {
    MenuLayout.Buttons row = new MenuLayout.Buttons(getContext(), columns);
    body.addView(row, new LinearLayout.LayoutParams(-1, -2));
    return row;
  }
  /** The CHEATS page (out of MANAGEMENT since 30.10.2026, wish of the project owner). Since 05.11.2026 in three
   *  sections (wish of the project owner): PLAYER & INVENTORY (the switches health, ammo, invisibility; one-hit
   *  kill, the case, the money), WEAPONS (each at its highest levels), GAME (automatic QTEs, the extra games). */
  private void cheats() {
    cheatBits = getContext().getSharedPreferences(CoreActivity.GAME_PREFS, Context.MODE_PRIVATE).getInt(CoreActivity.CHEATS, 0);
    heading(R.string.ingame_cheat_section_player);
    MenuLayout.Buttons player = cheatRow(3);
    cheatSwitch(player, R.string.ingame_cheat_health, 1);
    cheatSwitch(player, R.string.ingame_cheat_ammo, 2);
    cheatSwitch(player, R.string.ingame_cheat_invisible, INVISIBLE_BIT);
    MenuLayout.Buttons inventory = cheatRow(3);
    cheatSwitch(inventory, R.string.ingame_cheat_onehit, 4);
    cheatButton(inventory, 2, R.string.ingame_cheat_case);
    cheatButton(inventory, 3, R.string.ingame_cheat_money);
    heading(R.string.ingame_cheat_section_weapons);
    MenuLayout.Buttons weapons = cheatRow(3);
    cheatButton(weapons, 5, R.string.ingame_cheat_handcannon);
    cheatButton(weapons, 1, R.string.ingame_cheat_killer7);
    cheatButton(weapons, 9, R.string.ingame_cheat_matilda);
    MenuLayout.Buttons launchers = cheatRow(3);
    cheatButton(launchers, 6, R.string.ingame_cheat_chicago);
    cheatButton(launchers, 8, R.string.ingame_cheat_mine);
    cheatButton(launchers, 7, R.string.ingame_cheat_rocket);
    // the switch "Automatic QTEs" and the button "Unlock extra games" (Assignment Ada, The Mercenaries), half the
    // row each (wish of the project owner, 29.10.2026)
    heading(R.string.ingame_cheat_section_game);
    MenuLayout.Buttons more = cheatRow(2);
    cheatSwitch(more, R.string.ingame_cheat_qte, 8);
    cellAction(more, s(R.string.ingame_cheat_extras), v -> {
      v.setSelected(true); main.postDelayed(() -> v.setSelected(false), CHEAT_FLASH_MS);
      cheatAction(4, R.string.ingame_cheat_extras);
    });
    // "Random items" (wish of the project owner, 07.11.2026, GitHub issue #161): a new seed each time it goes on
    MenuLayout.Buttons items = cheatRow(2);
    toggle(items, R.string.ingame_cheat_random_items, (cheatBits & RANDOM_ITEMS_BIT) != 0, on -> {
      cheatBits = withRandomItems(cheatBits, on, new java.util.Random().nextInt(1 << 20));
      host.cheatsChanged(cheatBits);
    });
    cheatStatus = text("", 15, false); cheatStatus.setPadding(dp(MenuLayout.GAP), dp(6), dp(MenuLayout.GAP), dp(2));
    cheatStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE); cheatStatus.setVisibility(View.GONE);
    body.addView(cheatStatus);
    help(R.string.ingame_cheats_help);
    help(R.string.ingame_cheat_random_items_help);
    playerCharacter();
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
    MenuLayout.Buttons row = new MenuLayout.Buttons(getContext(), 3);
    body.addView(row, new LinearLayout.LayoutParams(-1, -2));
    Button[] buttons = new Button[labels.length];
    for (int i = 0; i < labels.length; i++) {
      final int index = i;
      Button b = cellAction(row, labels[i], v -> {
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
    costumeSwitch(row);
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
    look.addState(new int[] {android.R.attr.state_focused}, focused(UPDATE_RED));
    look.addState(new int[] {android.R.attr.state_pressed}, shape(UPDATE_RED, INK, 6));
    look.addState(new int[0], shape(UPDATE_RED, UPDATE_RED_EDGE, 6));
    understood.setBackground(look); understood.setTextColor(INK);
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
    dialog.show();
  }
  /** "Special costumes · On/Off" over a smaller "(Leon/Ashley only)", as tall as one line's button (the
   *  owner, 03.11.2026: the second row as high as the first). */
  private void costumeSwitch(MenuLayout.Buttons parent) {
    final Button[] b = new Button[1];
    Runnable label = () -> {
      boolean on = (cheatBits & COSTUME_BIT) != 0;
      SpannableStringBuilder t = new SpannableStringBuilder(getContext().getString(R.string.ingame_toggle_value,
          s(R.string.ingame_cheat_costume), s(on ? R.string.ingame_on : R.string.ingame_off)));
      int from = t.length(); t.append('\n').append(s(R.string.ingame_cheat_costume_note));
      t.setSpan(new RelativeSizeSpan(.75f), from, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      t.setSpan(new StyleSpan(Typeface.NORMAL), from, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      b[0].setSelected(on); b[0].setText(t);
    };
    b[0] = cellAction(parent, "", v -> {
      cheatBits ^= COSTUME_BIT; host.cheatsChanged(cheatBits); label.run();
    });
    b[0].setPadding(dp(12), dp(2), dp(12), dp(2));
    b[0].setAccessibilityDelegate(new View.AccessibilityDelegate() {
      @Override public void onInitializeAccessibilityNodeInfo(View view, AccessibilityNodeInfo info) {
        super.onInitializeAccessibilityNodeInfo(view, info); info.setCheckable(true); info.setChecked(view.isSelected());
      }
    });
    label.run();
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
        else if (done == 2 && (what == 1 || what >= 5)) text = getContext().getString(R.string.ingame_cheat_raised, s(label));
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
    dialog.show();
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
    dialog.show();
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
    if (rows == null) {
      cardLine.setText(s("busy".equals(CoreBridge.cardNote()) ? R.string.ingame_card_busy : R.string.ingame_card_none));
      cardLine.setVisibility(View.VISIBLE); exportAll.setEnabled(false);
    } else { cardLine.setVisibility(View.GONE); exportAll.setEnabled(rows.length > 0); }
  }
  private String version() {
    try {
      String v = getContext().getPackageManager().getPackageInfo(getContext().getPackageName(), 0).versionName;
      return v != null && v.endsWith("-core") ? v.substring(0, v.length() - 5) : v;
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

  /** The resolution the chosen level gives. */
  private void showGraphics() {
    int chosen = scaleHalves(graphics), halves = chosen;
    if (halves < 2 || halves > 8) {
      // Dynamic: half a level above the benchmark's level for 30 or 60 FPS (4x at most; core_mode.cpp), else the screen's
      int cap = graphics.getInt(graphics.getBoolean("smooth60", false) ? DYN_CAP_60 : DYN_CAP_30, 0);
      halves = cap >= 2 && cap <= 8 ? Math.min(8, cap + 1) : 2 * autoScale(screenWidth, screenHeight);
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

  @Override protected void onStart() {
    super.onStart();
    if (getWindow() != null) {
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
