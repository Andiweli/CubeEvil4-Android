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
import android.view.KeyEvent;
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
    void shareReport();
    void saveReport();
    void logChanged(boolean on);
    void toolsChanged(boolean on);
    void fpsChanged(boolean on);
    /** The setting "Touch Controls": TouchShowRule.AUTO, FORCE or SUPPRESS (the preferences hold it). */
    void touchModeChanged(int mode);
    /** The switch "Use Gyro-Aiming" (the preferences hold it). */
    void gyroChanged(boolean on);
    /** The switch "Alternate Controls" (the preferences hold it). */
    void alternateChanged(boolean on);
    /** The comfort control "Latch L and R", touch and controller (the preferences hold it). */
    void latchChanged(boolean on);
    /** The comfort control "Latch B", touch and controller (the preferences hold it). */
    void latchBChanged(boolean on);
    /** The slider of gyro aiming: the turn rates times `value` (the preferences hold it). */
    void gyroSensitivityChanged(float value);
    /** The switch "Knife by Swipe" (the preferences hold it). */
    void knifeSwipeChanged(boolean on);
    /** The European discs are imported: the home page offers "Game language". */
    boolean gameLanguages();
    /** The game's language of a "Game language" choice (2..6; 0 the device's). */
    int gameLanguageOf(int choice);
    /** The choice "Game language": 0 the device's, 2..6 (CoreActivity.GAME_LANGUAGE holds it); the
     *  game restarts when its language changes (the menu asked first). */
    void gameLanguageChanged(int choice);
  }

  private interface Changed<T> { void accept(T value); }
  private static final int HOME = 0, DISPLAY = 1, CONTROLS = 2, PERFORMANCE = 3, MANAGEMENT = 4;
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
  private final Button back, resume;
  private final Handler main = new Handler(Looper.getMainLooper());
  private final int[] scrollY = new int[5];
  private final Button[] navigation = new Button[5];
  private int page, requestSerial;
  private boolean working, slotPresent;
  private TextView graphicsLine, cardLine, slotDetails, slotDate, slotStatus;
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
    root.addView(new MenuLayout.Header(context, back, title, resume), new LinearLayout.LayoutParams(-1, -2));
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
  private void heading(int id) {
    TextView v = text(s(id).toUpperCase(Locale.getDefault()), 14, true); v.setTextColor(MUTED); v.setLetterSpacing(.08f);
    v.setPadding(0, dp(16), 0, dp(8)); body.addView(v);
  }
  private void help(int id) { help(body, id); }
  private void help(LinearLayout parent, int id) {
    TextView t = text(s(id), 15, false); t.setPadding(0, dp(4), 0, dp(8)); parent.addView(t);
  }
  /** Row label as tall as one button row (48 dp minimum + 2 x 4 dp margin), centred on the first row. */
  private TextView rowLabel(String label) {
    TextView caption = text(label, 16, true);
    caption.setPadding(0, 0, dp(8), 0); caption.setMinHeight(dp(56)); caption.setMinimumHeight(dp(56));
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
    outer.setPadding(0, dp(3), 0, dp(3)); body.addView(outer);
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
    LinearLayout outer = new MenuLayout.Columns(getContext(), 170, 640, false);
    outer.addView(rowLabel(s(label)));
    MenuLayout.Buttons options = new MenuLayout.Buttons(getContext(), columns);
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
    outer.setPadding(0, dp(3), 0, dp(3)); body.addView(outer);
    return buttons;
  }
  private void select(Button[] buttons, String[] labels, int index) {
    for (int k = 0; k < buttons.length; k++) {
      buttons[k].setSelected(k == index);
      buttons[k].setContentDescription(labels[k] + ", " + s(k == index ? R.string.ingame_on : R.string.ingame_off));
    }
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
  private void toggleLabel(Button b, int label, boolean on) {
    b.setSelected(on);
    b.setText(getContext().getString(R.string.ingame_toggle_value, s(label), s(on ? R.string.ingame_on : R.string.ingame_off)));
  }

  private void showPage(int next, boolean focus) {
    if (working) return;
    scrollY[page] = scroll.getScrollY(); page = next; ++requestSerial;
    body.removeAllViews(); graphicsLine = null; cardLine = null; exportAll = null;
    back.setVisibility(page == HOME ? View.GONE : View.VISIBLE);
    int[] titles = {R.string.ingame_title, R.string.ingame_display, R.string.ingame_controls_page,
        R.string.ingame_performance, R.string.ingame_management};
    if (page == HOME && version() != null) {
      SpannableStringBuilder t = new SpannableStringBuilder(s(titles[page]));
      int start = t.length(); t.append(" (v").append(version()).append(")");
      t.setSpan(new RelativeSizeSpan(.6f), start, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      t.setSpan(new StyleSpan(Typeface.NORMAL), start, t.length(), Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
      title.setText(t);
    } else title.setText(s(titles[page]));
    if (page == HOME) home(); else if (page == DISPLAY) display(); else if (page == CONTROLS) controls();
    else if (page == PERFORMANCE) performance(); else management();
    separator(body);
    TextView footer = text(s(R.string.ingame_navigation), 14, false);
    footer.setPadding(0, dp(8), 0, 0); body.addView(footer);
    main.post(() -> {
      scroll.scrollTo(0, scrollY[page]);
      if (focus) { if (page == HOME) resume.requestFocus(); else back.requestFocus(); }
    });
  }
  private void goBack() {
    if (working) return;
    if (page == HOME) close();
    else {
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
    slot.setPadding(0, dp(8), 0, dp(8));
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
    // The button's 3 dp outer margin plus 9 dp here match the metadata's 12 dp left inset.
    actions.setPadding(0, 0, dp(9), 0);
    actions.setLayoutDirection(getContext().getResources().getConfiguration().getLayoutDirection());
    saveSlot = cellAction(actions, s(R.string.ingame_instant_save), v -> confirmInstant(1)); saveSlot.setSelected(true);
    loadSlot = cellAction(actions, s(R.string.ingame_instant_load), v -> confirmInstant(2));
    saveSlot.setEnabled(false); loadSlot.setEnabled(false); slot.addView(actions); body.addView(slot);
    slotStatus = text(s(R.string.ingame_instant_checking), 15, false); slotStatus.setPadding(0, dp(6), 0, dp(6));
    slotStatus.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE); body.addView(slotStatus);
    heading(R.string.ingame_settings);
    MenuLayout.Buttons first = buttonsRow(body), second = buttonsRow(body);
    navigation[DISPLAY] = nav(first, R.string.ingame_display, R.string.ingame_display_summary, DISPLAY);
    navigation[CONTROLS] = nav(first, R.string.ingame_controls_page, R.string.ingame_controls_summary, CONTROLS);
    navigation[PERFORMANCE] = nav(second, R.string.ingame_performance, R.string.ingame_performance_summary, PERFORMANCE);
    navigation[MANAGEMENT] = nav(second, R.string.ingame_management, R.string.ingame_management_summary, MANAGEMENT);
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
    buttons[0] = choices(R.string.ingame_game_language, labels, values, current[0], Integer.MAX_VALUE, 3, value -> {
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

  private void display() {
    heading(R.string.ingame_graphics);
    int[] info = CoreBridge.graphicsInfo();
    // Unknown GPU capabilities must not enable unsupported samples or anisotropy.
    int samples = info == null ? 1 : Math.max(1, info[0]);
    int anisotropy = info == null ? 1 : Math.max(1, info[1]);
    graphicChoices(R.string.ingame_resolution, "scale", new String[] {s(R.string.ingame_dynamic), "1×", "2×", "3×", "4×"},
        new int[] {0, 1, 2, 3, 4}, 0, Integer.MAX_VALUE);
    graphicsLine = text("", 15, false); graphicsLine.setPadding(0, dp(4), 0, dp(10)); body.addView(graphicsLine); showGraphics();
    graphicChoices(R.string.ingame_antialiasing, "samples", new String[] {s(R.string.ingame_off), "2× MSAA", "4× MSAA"},
        new int[] {1, 2, 4}, 1, samples);
    toggle(buttonsRow(labelledOptions(0)), R.string.ingame_fxaa, graphics.getBoolean("fxaa", false), on -> {
      graphics.edit().putBoolean("fxaa", on).apply(); CoreBridge.setFxaa(on);
    });
    help(R.string.ingame_fxaa_help);
    graphicChoices(R.string.ingame_texture_filter, "anisotropy", new String[] {s(R.string.ingame_off), "2×", "4×", "8×", "16×"},
        new int[] {1, 2, 4, 8, 16}, 1, anisotropy);
    heading(R.string.ingame_picture);
    graphicChoices(R.string.ingame_picture, "aspect", new String[] {s(R.string.ingame_original_43), s(R.string.ingame_43_no_bars), "16:9", s(R.string.ingame_stretched)},
        new int[] {1, 3, 0, 2}, 0, Integer.MAX_VALUE);
    toggle(buttonsRow(labelledOptions(0)), R.string.ingame_dof, graphics.getBoolean("dof", true), on -> {
      graphics.edit().putBoolean("dof", on).apply(); CoreBridge.setDof(on);
    });
  }
  private void controls() {
    choices(R.string.ingame_touch_controls, new String[] {s(R.string.ingame_touch_auto), s(R.string.ingame_touch_force), s(R.string.ingame_touch_suppress)},
        new int[] {TouchShowRule.AUTO, TouchShowRule.FORCE, TouchShowRule.SUPPRESS}, CoreActivity.touchMode(input), Integer.MAX_VALUE,
        mode -> host.touchModeChanged(mode));
    help(R.string.ingame_touch_help); heading(R.string.ingame_comfort);
    MenuLayout.Buttons first = buttonsRow(body);
    toggle(first, R.string.ingame_alternate, input.getBoolean(CoreActivity.ALT_CONTROLS, false), on -> host.alternateChanged(on));
    toggle(first, R.string.ingame_knife_swipe, input.getBoolean(CoreActivity.KNIFE_SWIPE, true), on -> host.knifeSwipeChanged(on));
    MenuLayout.Buttons second = buttonsRow(body);
    toggle(second, R.string.ingame_latch, input.getBoolean(CoreActivity.TOUCH_LATCH, false), on -> host.latchChanged(on));
    toggle(second, R.string.ingame_latch_b, input.getBoolean(CoreActivity.TOUCH_LATCH_B, false), on -> host.latchBChanged(on));
    boolean gyro = GyroAim.available(getContext()), on = gyro && input.getBoolean(CoreActivity.GYRO_AIM, false);
    LinearLayout third = new MenuLayout.Columns(getContext(), 0, 540, false);
    body.addView(third);
    LinearLayout sensitivityBox = column();
    SeekBar sensitivity = new SeekBar(getContext()); TextView valueText = text("", 15, false);
    Button gyroButton = toggle(buttonsRow(third), gyro ? R.string.ingame_gyro : R.string.ingame_gyro_none, on, enabled -> {
      host.gyroChanged(enabled); sensitivity.setEnabled(enabled); valueText.setEnabled(enabled);
    });
    gyroButton.setEnabled(gyro);
    TextView sensitivityTitle = text(s(R.string.ingame_gyro_sensitivity), 15, false);
    sensitivityTitle.setGravity(Gravity.CENTER_HORIZONTAL);
    sensitivityBox.addView(sensitivityTitle, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
        ViewGroup.LayoutParams.WRAP_CONTENT));
    LinearLayout slider = row();
    slider.addView(sensitivity, new LinearLayout.LayoutParams(0, dp(48), 1)); slider.addView(valueText);
    sensitivityBox.addView(slider);
    third.addView(sensitivityBox);
    int steps = Math.round((GyroAim.MAX_SENSITIVITY - GyroAim.MIN_SENSITIVITY) / GyroAim.SENSITIVITY_STEP);
    float stored = GyroAim.clampSensitivity(input.getFloat(CoreActivity.GYRO_SENSITIVITY, 1f));
    sensitivity.setMax(steps); sensitivity.setProgress(Math.round((stored - GyroAim.MIN_SENSITIVITY) / GyroAim.SENSITIVITY_STEP));
    sensitivity.setContentDescription(s(R.string.ingame_gyro_sensitivity));
    sensitivity.setProgressTintList(ColorStateList.valueOf(GOLD)); sensitivity.setThumbTintList(ColorStateList.valueOf(GOLD));
    valueText.setText(times(stored)); sensitivity.setEnabled(on); valueText.setEnabled(on);
    sensitivity.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
        float value = GyroAim.MIN_SENSITIVITY + progress * GyroAim.SENSITIVITY_STEP;
        valueText.setText(times(value)); if (fromUser) host.gyroSensitivityChanged(value);
      }
      @Override public void onStartTrackingTouch(SeekBar bar) {}
      @Override public void onStopTrackingTouch(SeekBar bar) {}
    });
    help(R.string.ingame_alternate_help); help(R.string.ingame_latch_help); help(R.string.ingame_knife_swipe_help);
  }
  private void performance() {
    heading(R.string.ingame_force_performance);
    choices(R.string.ingame_force_performance, new String[] {s(R.string.ingame_perf_default), s(R.string.ingame_perf_cores)},
        new int[] {0, 1}, graphics.getInt("fastCores", 0), 1, value -> {
          graphics.edit().putInt("fastCores", value).apply(); CoreBridge.setFastCores(value == 1);
        });
    help(R.string.ingame_perf_help);
    LinearLayout output = labelledOptions(R.string.ingame_output);
    toggle(buttonsRow(output), R.string.ingame_fps_display, graphics.getBoolean("fps", false), on -> {
      graphics.edit().putBoolean("fps", on).apply(); host.fpsChanged(on);
    });
    // Frame skip leaves pictures out, 60 FPS adds pictures between: one or the other (wish of the
    // project owner, 26.10.2026: both on jerked back and forth); switching one on switches the other off
    final Button[] skipAndSmooth = new Button[2];
    skipAndSmooth[0] = toggle(buttonsRow(output), R.string.ingame_frame_skip, graphics.getBoolean("frameskip", false), on -> {
      graphics.edit().putBoolean("frameskip", on).apply(); CoreBridge.setFrameSkip(on);
      if (on && skipAndSmooth[1].isSelected()) {
        toggleLabel(skipAndSmooth[1], R.string.ingame_smooth60, false);
        graphics.edit().putBoolean("smooth60", false).apply(); CoreBridge.setSmooth(false);
      }
    });
    skipAndSmooth[1] = toggle(buttonsRow(output), R.string.ingame_smooth60, graphics.getBoolean("smooth60", false), on -> {
      graphics.edit().putBoolean("smooth60", on).apply(); CoreBridge.setSmooth(on);
      if (on && skipAndSmooth[0].isSelected()) {
        toggleLabel(skipAndSmooth[0], R.string.ingame_frame_skip, false);
        graphics.edit().putBoolean("frameskip", false).apply(); CoreBridge.setFrameSkip(false);
      }
    });
    help(output, R.string.ingame_frame_skip_help);
    help(output, R.string.ingame_smooth_help);
    help(output, R.string.ingame_skip_or_smooth);
  }
  private void management() {
    heading(R.string.ingame_card); help(R.string.ingame_card_help);
    cardLine = text("", 15, false); body.addView(cardLine);
    MenuLayout.Buttons cardButtons = buttonsRow(body);
    exportAll = cellAction(cardButtons, s(R.string.ingame_export), v -> host.exportAll());
    cellAction(cardButtons, s(R.string.ingame_import), v -> host.importFiles()); refreshCard();
    heading(R.string.ingame_log); help(R.string.ingame_log_help);
    MenuLayout.Buttons log = buttonsRow(body);
    toggle(log, R.string.ingame_log_record, CoreLog.enabled(getContext()), on -> host.logChanged(on));
    toggle(log, R.string.ingame_log_capture, tools, on -> { tools = on; host.toolsChanged(on); });
    MenuLayout.Buttons reports = buttonsRow(body);
    cellAction(reports, s(R.string.ingame_report_send), v -> host.shareReport());
    cellAction(reports, s(R.string.ingame_report_save), v -> host.saveReport());
    heading(R.string.ingame_source);
    cellAction(buttonsRow(body), getContext().getString(R.string.ingame_source_line, SOURCE_URL), v -> openSource());
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

  /** The resolution the chosen level gives. */
  private void showGraphics() {
    int chosen = graphics.getInt("scale", 0), scale = chosen;
    if (scale < 1 || scale > 4) scale = autoScale(screenWidth, screenHeight);
    int aspect = graphics.getInt("aspect", 0);
    boolean original = aspect == 1 || aspect == 3;
    // the numbers as digits 0..9 in every language (Locale.ROOT)
    if (graphicsLine == null) return;
    graphicsLine.setText(String.format(Locale.ROOT, s(chosen == 0 ? R.string.ingame_resolution_line_auto
        : R.string.ingame_resolution_line), 640 * scale, (original ? 448 : 336) * scale, scale, screenWidth, screenHeight));
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
    getWindow().setLayout(Math.min(dp(1000), Math.round(width * .96f)),
        Math.min(dp(650), Math.round(height * .94f)));
  }
  @Override protected void onStop() {
    if (hostDecor != null) hostDecor.removeOnLayoutChangeListener(windowLayout);
    super.onStop();
  }
  @Override public boolean dispatchKeyEvent(KeyEvent event) {
    int key = event.getKeyCode();
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
