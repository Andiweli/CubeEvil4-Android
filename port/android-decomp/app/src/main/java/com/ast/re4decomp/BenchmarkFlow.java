package com.ast.re4decomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The benchmark with its suggestion (0.99.10, wishes of the project owner, 11.11.2026; port/docs/leistungsprofil-plan.md):
 * benchmark 1 at the lake's dock with the player's own look settings (depth of field, lens flares, sharpening, the
 * filters: no profile changes them), the profile's settings at their base, the performance cores and the picture
 * format "Native" (the levels that hold 30 and 60 FPS, the processor sign; core_mode.cpp LevelBench mode 0), then popup
 * 2 (the device, the levels, the profile PerfProfile suggests; "Ignore" keeps only the levels for "Dynamic", "Apply"
 * checks it), benchmark 2 (mode 1: 20 s with the settings before, 20 s with the suggestion, each after Dynamic settled,
 * the performance cores as the suggestion has them in both;
 * an effect taken back up to twice while it neither holds its rate nor runs as smoothly as before), popup 3 (before and
 * after; "Ignore" the settings before, "Accept" the profile; a suggestion less smooth than before says so and has
 * "Ignore" focused). On the very first start popup 1 comes first, once the title's menu waits (CoreActivity); a cancel
 * offers it again at the next start.
 */
final class BenchmarkFlow {
  interface Host {
    Activity activity();
    SharedPreferences graphics();
    /** The options of the preferences to the core (picture, filters, 60 FPS, frame skip, cores, Potato, levels). */
    void applyOptions();
    /** Benchmark 1's options: the player's look settings, the profile's settings at their base (no anti-aliasing, no
     *  texture filter, no frame skip, Potato off, the performance cores), "Native"; not stored. */
    void applyClean();
    /** A profile's values to the core over the preferences' others; not stored. */
    void applyValues(int[] values);
    /** The line at the top while it runs (null: hidden). */
    void progress(String text);
    void mute(boolean on);
    /** The game held while a popup is open. */
    void hold(boolean on);
    PerfProfile.Device device();
  }

  private static final String TAG = "RE4Bench";
  private static final int IDLE = 0, FIRST = 1, PASS1 = 2, ASK = 3, PASS2 = 4, RESULT = 5;
  private static final int PANEL = 0xff191f1c, SURFACE = 0xff222a25, BORDER = 0xff414b41, INK = 0xfff0eee5,
      MUTED = 0xffa6afa3, GOLD = 0xffd7bc7d, GO = 0xff2d5233, GO_EDGE = 0xff4f8a57, BOX = 0xff202720;
  private static final int WARMUP_FIRST = 15, WARMUP = 5;  // seconds (programs made on the first start)
  // benchmark 2: a window starts SETTLE_MS after the settings changed at the earliest, once Dynamic's level stood
  // STABLE_MS (a new renderer starts at the benchmark's level and goes down half a level per 4 s where the GPU falls
  // behind; the Retroid's check 11.11.2026 counted it from 3.5x down), SETTLE_MAX_MS at the latest
  private static final int SETTLE_MS = 4000, STABLE_MS = 5000, SETTLE_MAX_MS = 16000, WINDOW_MS = 20000, STEPS_BACK = 2;

  private final Host host;
  private final Handler main = new Handler(Looper.getMainLooper());
  private int state = IDLE;
  private boolean first, offeredThisRun;
  // benchmark 1's outcome
  private int cap30, cap60;
  private boolean cpuSign;
  private PerfProfile.Device device;
  private int recommended;
  private int[] before, proposal, firstProposal;
  // benchmark 2
  private int stage;  // 0 going, 1 settle before, 2 window before, 3 settle after, 4 window after, 5 back
  private long until, settleFrom, levelSince;
  private int lastLevel;
  private int[] measureBefore, measureAfter;
  private int stepsBack;
  private String stepNote;
  private final Runnable poll = this::poll;

  BenchmarkFlow(Host host) { this.host = host; }

  boolean running() { return state == PASS1 || state == PASS2; }
  boolean busy() { return state != IDLE; }

  /** The first start's benchmark is to come (FIRST_PENDING) and was not offered in this run yet. */
  boolean firstDue() {
    return state == IDLE && !offeredThisRun
        && host.graphics().getInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE) == PerfProfile.FIRST_PENDING;
  }

  private String s(int id) { return host.activity().getString(id); }
  private String s(int id, Object... args) { return host.activity().getString(id, args); }

  // ---- popup 1 (the first start) --------------------------------------------------------------------------------

  /** The title's menu waits on the very first start: welcome, one button. */
  void offerFirst() {
    offeredThisRun = true;
    state = FIRST;
    host.hold(true);
    LinearLayout box = box();
    title(box, s(R.string.ingame_bench_welcome));
    paragraph(box, s(R.string.ingame_bench_text), INK);
    final AlertDialog[] d = new AlertDialog[1];
    Button go = button(s(R.string.ingame_bench_start), true, v -> { d[0].dismiss(); host.hold(false); start(true); });
    buttons(box, go);
    d[0] = dialog(box, go, () -> {
      // B / Back: later (at the next start)
      state = IDLE;
      host.hold(false);
      Log.i(TAG, "benchmark: the first start's offer put off (B / Back)");
      message(s(R.string.ingame_bench_first_note));
    });
  }

  // ---- benchmark 1 --------------------------------------------------------------------------------------------

  /** Benchmark 1 (the menu's buttons, popup 1). */
  void start(boolean firstStart) {
    if (running()) return;
    first = firstStart;
    device = host.device();
    before = PerfProfile.current(host.graphics());
    state = PASS1;
    host.mute(true);
    host.applyClean();
    int top = PerfProfile.benchTop(device);
    host.progress(s(R.string.ingame_benchmark_starting));
    CoreBridge.startBenchmark(0, top, first ? WARMUP_FIRST : WARMUP);
    Log.i(TAG, "benchmark 1: started (" + (first ? "the first start, " : "") + "the 30 FPS pass from " + top
        + " halves; " + PerfProfile.gpuName(device.gpu) + ", " + device.cores + " cores, " + device.ramMb + " MB, "
        + Math.round(device.hz) + " Hz)");
    main.removeCallbacks(poll);
    main.postDelayed(poll, 500);
  }

  /** B / Back while it runs: back to where the game was, nothing changed. */
  void cancel() {
    if (!running()) return;
    CoreBridge.cancelBenchmark();
    host.progress(s(R.string.ingame_benchmark_back));
  }

  private void poll() {
    int[] st = CoreBridge.benchmarkState();
    if (st == null || st.length < 9) { fail(R.string.ingame_benchmark_failed); return; }
    if (state == PASS1) pollPass1(st);
    else if (state == PASS2) pollPass2(st);
  }

  private void pollPass1(int[] st) {
    switch (st[0]) {
      case 1: host.progress(s(R.string.ingame_benchmark_going)); break;
      case 2:
        host.progress(s(R.string.ingame_benchmark_running, st[1] != 0 ? 60 : 30, PerfProfile.level(st[2]),
            Math.round(st[5] / 10f)));
        break;
      case 3: case 6: host.progress(s(R.string.ingame_benchmark_back)); break;
      case 4: {
        cap30 = st[3];
        cap60 = st[4];
        cpuSign = st[7] != 0;
        SharedPreferences g = host.graphics();
        g.edit().putInt(CoreMenu.DYN_CAP_30, cap30).putInt(CoreMenu.DYN_CAP_60, cap60)
            .putLong(PerfProfile.BENCH_DATE, System.currentTimeMillis()).putBoolean(PerfProfile.BENCH_CPU, cpuSign)
            .putInt(PerfProfile.FIRST, PerfProfile.FIRST_NONE).apply();
        CoreBridge.setDynamicCaps(cap30, cap60);
        host.mute(false);
        host.progress(null);
        host.applyOptions();
        PerfProfile.Bench b = PerfProfile.Bench.of(g);
        recommended = PerfProfile.recommend(device, b);
        proposal = PerfProfile.values(recommended, device, b);
        firstProposal = proposal.clone();
        Log.i(TAG, "benchmark 1: " + PerfProfile.level(cap30) + " with 30 FPS" + ((st[6] & 1) == 0 ? " (not held)" : "")
            + ", " + PerfProfile.level(cap60) + " with 60 FPS" + ((st[6] & 2) == 0 ? " (not held)" : "")
            + (cpuSign ? ", the processor holds it back" : "") + "; suggested: " + name(recommended) + " "
            + describe(proposal));
        askApply((st[6] & 1) == 0, (st[6] & 2) == 0);
        return;
      }
      case 5: fail(first ? -1 : R.string.ingame_benchmark_cancelled); return;
      case -1: fail(R.string.ingame_benchmark_refused); return;
      case -3: fail(R.string.ingame_benchmark_unreachable); return;
      case -4: fail(R.string.ingame_benchmark_discs); return;
      case 0: break;
      default: fail(R.string.ingame_benchmark_failed); return;
    }
    main.postDelayed(poll, 250);
  }

  /** It ended without a result: the settings as they were; `message` 0 none, -1 the first start's note. */
  private void fail(int message) {
    main.removeCallbacks(poll);
    state = IDLE;
    host.mute(false);
    host.progress(null);
    host.applyOptions();
    String text = message == -1 ? s(R.string.ingame_benchmark_cancelled) + " " + s(R.string.ingame_bench_first_note)
        : message != 0 ? s(message) : null;
    Log.i(TAG, "benchmark: ended without a result" + (text != null ? ": " + text : ""));
    if (text != null) message(text);
  }

  // ---- popup 2 ------------------------------------------------------------------------------------------------

  private void askApply(boolean low30, boolean low60) {
    state = ASK;
    host.hold(true);
    LinearLayout box = box();
    title(box, s(R.string.ingame_bench_device));
    PerfProfile.Device d = device;
    String ghz = String.format(Locale.getDefault(), "%.1f", d.maxMhz / 1000.0);
    String lines = s(R.string.ingame_bench_device_lines, PerfProfile.gpuName(d.gpu), d.cores, d.fastCores, ghz,
        Math.max(1, Math.round(d.ramMb / 1024f)), (int) Math.round(d.hz), PerfProfile.level(cap30),
        PerfProfile.level(cap60), limit(d, new PerfProfile.Bench(cap30, cap60, cpuSign, true)));
    TextView table = paragraph(box, lines, INK);
    table.setBackground(shape(BOX, BORDER, 6));
    table.setPadding(dp(12), dp(10), dp(12), dp(10));
    StringBuilder notes = new StringBuilder();
    if (low30) notes.append(s(R.string.ingame_benchmark_low, 30));
    if (low60) notes.append(notes.length() > 0 ? " " : "").append(s(R.string.ingame_benchmark_low, 60));
    if (notes.length() > 0) paragraph(box, notes.toString(), MUTED);
    TextView rec = paragraph(box, s(R.string.ingame_bench_recommended, name(recommended)), INK);
    rec.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    paragraph(box, bullets(lines(proposal, true)), INK);
    paragraph(box, s(R.string.ingame_bench_apply_note), MUTED);
    final AlertDialog[] dlg = new AlertDialog[1];
    Runnable ignore = () -> {
      // only the levels for "Dynamic" (stored already), the resolution Dynamic as the benchmark of before did
      host.graphics().edit().putInt("scaleHalves", 0).apply();
      host.applyOptions();
      host.hold(false);
      state = IDLE;
      Log.i(TAG, "popup 2: ignored (only the levels for Dynamic)");
    };
    Button no = button(s(R.string.ingame_bench_ignore), false, v -> { dlg[0].dismiss(); ignore.run(); });
    Button yes = button(s(R.string.ingame_bench_apply), true, v -> { dlg[0].dismiss(); host.hold(false); check(); });
    buttons(box, no, yes);
    dlg[0] = dialog(box, yes, ignore);
  }

  // ---- benchmark 2 --------------------------------------------------------------------------------------------

  private void check() {
    state = PASS2;
    stage = 0;
    stepsBack = 0;
    stepNote = null;
    measureBefore = measureAfter = null;
    host.mute(true);
    host.applyOptions();  // the settings before (the preferences)
    // the performance cores in both windows as the suggestion has them: one thing changed at a time (the Retroid's
    // check 11.11.2026 compared "before" with the cores against the suggestion without them)
    boolean cores = proposal[PerfProfile.CORES] == 1;
    CoreBridge.setFastCores(cores);
    host.progress(s(R.string.ingame_benchmark_starting));
    CoreBridge.startBenchmark(1, 0, 0);
    Log.i(TAG, "benchmark 2: started (before: " + describe(before) + "; with " + name(recommended) + ": "
        + describe(proposal) + "; the performance cores " + (cores ? "on" : "off") + " in both windows)");
    main.removeCallbacks(poll);
    main.postDelayed(poll, 500);
  }

  private void pollPass2(int[] st) {
    final long now = System.currentTimeMillis();
    switch (st[0]) {
      case 1: host.progress(s(R.string.ingame_benchmark_going)); break;
      case 2: {
        int fps = Math.round(st[5] / 10f);
        if (st[2] != lastLevel) {
          lastLevel = st[2];
          levelSince = now;
        }
        if (stage == 0) {
          CoreBridge.benchmarkMeasure();
          settle(now, 1);
        } else if (stage == 1 || stage == 3) {
          if (now - settleFrom >= SETTLE_MS && (now - levelSince >= STABLE_MS || now - settleFrom >= SETTLE_MAX_MS)) {
            CoreBridge.benchmarkMeasure();  // the settling seconds do not count
            Log.i(TAG, "benchmark 2: settled after " + (now - settleFrom) / 1000 + " s at "
                + PerfProfile.level(lastLevel) + (now - levelSince < STABLE_MS ? " (still moving)" : ""));
            stage++;
            until = now + WINDOW_MS;
          }
        } else if (now >= until) {
          if (stage == 2) {
            measureBefore = CoreBridge.benchmarkMeasure();
            Log.i(TAG, "benchmark 2: before " + window(measureBefore));
            host.applyValues(proposal);
            settle(now, 3);
          } else if (stage == 4) {
            measureAfter = CoreBridge.benchmarkMeasure();
            int room = PerfProfile.headroom(proposal, device, new PerfProfile.Bench(cap30, cap60, cpuSign, true));
            boolean holds = PerfProfile.holds(measureAfter, proposal[PerfProfile.SMOOTH] != 0, room);
            boolean keeps = PerfProfile.keepsUp(measureAfter, measureBefore, room);
            Log.i(TAG, "benchmark 2: with " + name(recommended) + " " + describe(proposal) + ": " + window(measureAfter)
                + (holds ? ", holds" : keeps ? ", as smooth as before (not its full rate)"
                : ", does not hold, less smooth than before (headroom " + room + " halves)"));
            int[] less = holds || keeps || stepsBack >= STEPS_BACK ? null : PerfProfile.stepBack(proposal);
            if (less != null) {
              stepNote = s(R.string.ingame_bench_stepped, effects(firstProposal), effects(less));
              proposal = less;
              stepsBack++;
              host.applyValues(proposal);
              settle(now, 3);
            } else {
              CoreBridge.finishBenchmark();
              stage = 5;
            }
          }
        }
        if (stage <= 2) host.progress(s(R.string.ingame_bench_check, s(R.string.ingame_bench_before), fps));
        else if (stage <= 4) host.progress(s(R.string.ingame_bench_check, name(recommended), fps));
        else host.progress(s(R.string.ingame_benchmark_back));
        break;
      }
      case 3: case 6: host.progress(s(R.string.ingame_benchmark_back)); break;
      case 4:
        main.removeCallbacks(poll);
        host.mute(false);
        host.progress(null);
        host.applyOptions();  // the settings before until popup 3 is answered
        if (measureBefore == null || measureAfter == null) { fail(R.string.ingame_benchmark_failed); return; }
        showResult();
        return;
      case 5: fail(R.string.ingame_benchmark_cancelled); return;
      case -1: fail(R.string.ingame_benchmark_refused); return;
      case -3: fail(R.string.ingame_benchmark_unreachable); return;
      case -4: fail(R.string.ingame_benchmark_discs); return;
      case 0: break;
      default: fail(R.string.ingame_benchmark_failed); return;
    }
    main.postDelayed(poll, 250);
  }

  /** A settling of benchmark 2 from `now` (stage 1 or 3). */
  private void settle(long now, int next) {
    stage = next;
    settleFrom = now;
    levelSince = now;
  }

  // ---- popup 3 ------------------------------------------------------------------------------------------------

  private void showResult() {
    state = RESULT;
    host.hold(true);
    LinearLayout box = box();
    title(box, s(R.string.ingame_bench_result));
    int fpsBefore = fps(measureBefore), fpsAfter = fps(measureAfter);
    NumberFormat pct = NumberFormat.getPercentInstance();
    TextView table = paragraph(box, s(R.string.ingame_bench_result_lines, fpsBefore, pct.format(smooth(measureBefore)),
        name(recommended), fpsAfter, pct.format(smooth(measureAfter))), INK);
    table.setBackground(shape(BOX, BORDER, 6));
    table.setPadding(dp(12), dp(10), dp(12), dp(10));
    String gain = fpsAfter > fpsBefore
        ? s(R.string.ingame_bench_gain, fpsAfter - fpsBefore, pct.format(fpsBefore > 0 ? (fpsAfter - fpsBefore) / (double) fpsBefore : 0))
        : s(R.string.ingame_bench_same, fpsAfter);
    TextView big = paragraph(box, gain, GOLD);
    big.setTextSize(19);
    big.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    big.setGravity(Gravity.CENTER_HORIZONTAL);
    List<String> extra = newlyOn(before, proposal);
    if (!extra.isEmpty()) paragraph(box, s(R.string.ingame_bench_extra, join(extra)), INK);
    if (stepNote != null) paragraph(box, stepNote, MUTED);
    int room = PerfProfile.headroom(proposal, device, new PerfProfile.Bench(cap30, cap60, cpuSign, true));
    boolean holds = PerfProfile.holds(measureAfter, proposal[PerfProfile.SMOOTH] != 0, room);
    // as smooth as before, not its full rate: said; less smooth than before: said, and "Ignore" has the focus (the
    // Retroid's check 11.11.2026 offered a last step that did not hold as if it were fine)
    boolean better = holds || PerfProfile.keepsUp(measureAfter, measureBefore, room);
    if (!holds && better) paragraph(box, s(R.string.ingame_bench_not_held), MUTED);
    if (!better) {
      TextView worse = paragraph(box, s(R.string.ingame_bench_worse), GOLD);
      worse.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    }
    paragraph(box, s(R.string.ingame_bench_before_note), MUTED);
    final AlertDialog[] dlg = new AlertDialog[1];
    Runnable ignore = () -> {
      host.applyOptions();
      host.hold(false);
      state = IDLE;
      Log.i(TAG, "popup 3: ignored (the settings before)");
    };
    Button no = button(s(R.string.ingame_bench_ignore), false, v -> { dlg[0].dismiss(); ignore.run(); });
    Button yes = button(s(R.string.ingame_bench_accept), true, v -> {
      dlg[0].dismiss();
      PerfProfile.choose(host.graphics(), recommended, proposal);
      host.applyOptions();
      host.hold(false);
      state = IDLE;
      Log.i(TAG, "popup 3: accepted: " + name(recommended) + " " + describe(proposal));
    });
    buttons(box, no, yes);
    dlg[0] = dialog(box, better ? yes : no, ignore);
  }

  // ---- texts --------------------------------------------------------------------------------------------------

  String name(int profile) {
    switch (profile) {
      case PerfProfile.LOW: return s(R.string.ingame_profile_low);
      case PerfProfile.MEDIUM: return s(R.string.ingame_profile_medium);
      case PerfProfile.HIGH: return s(R.string.ingame_profile_high);
      default: return s(R.string.ingame_profile_custom);
    }
  }

  static int limitText(PerfProfile.Device d, PerfProfile.Bench b) {
    if (PerfProfile.cpuWeak(d, b)) return R.string.ingame_limit_cpu;
    if (b.cap30 >= 8 && b.cap60 >= 8) return R.string.ingame_limit_none;
    return R.string.ingame_limit_gpu;
  }

  private String limit(PerfProfile.Device d, PerfProfile.Bench b) { return s(limitText(d, b)); }

  private String toggle(int label, String value) { return s(R.string.ingame_toggle_value, s(label), value); }

  /** The suggestion's settings as lines ("60 FPS · On", …); `all`: the Potato ones too when they are normal. */
  private List<String> lines(int[] v, boolean all) {
    List<String> out = new ArrayList<>();
    out.add(toggle(R.string.ingame_smooth60, s(v[PerfProfile.SMOOTH] != 0 ? R.string.ingame_on : R.string.ingame_off)));
    int cap = v[PerfProfile.SMOOTH] != 0 ? cap60 : cap30;
    out.add(toggle(R.string.ingame_resolution, s(R.string.ingame_dynamic) + " (≤ " + PerfProfile.level(Math.min(8, cap + 1)) + ")"));
    int[] skip = {R.string.ingame_off, R.string.ingame_skip_normal, R.string.ingame_skip_aggressive};
    out.add(toggle(R.string.ingame_frame_skip, s(skip[Math.max(0, Math.min(2, v[PerfProfile.SKIP]))])));
    out.add(effects(v));
    out.add(toggle(R.string.ingame_perf_cores, s(v[PerfProfile.CORES] != 0 ? R.string.ingame_on : R.string.ingame_off)));
    boolean normal = v[PerfProfile.WEATHER] != 0 && v[PerfProfile.SPECIAL] != 0 && v[PerfProfile.PARTICLES] == 0
        && v[PerfProfile.SHADOWS] != 0 && v[PerfProfile.VIEW] == 0;
    if (normal) {
      if (all) out.add(toggle(R.string.ingame_potato, s(R.string.ingame_off)));
    } else {
      if (v[PerfProfile.WEATHER] == 0) out.add(toggle(R.string.ingame_potato_weather, s(R.string.ingame_off)));
      if (v[PerfProfile.SPECIAL] == 0) out.add(toggle(R.string.ingame_potato_special, s(R.string.ingame_off)));
      int[] particles = {R.string.ingame_particles_normal, R.string.ingame_particles_less, R.string.ingame_particles_none};
      if (v[PerfProfile.PARTICLES] != 0)
        out.add(toggle(R.string.ingame_potato_particles, s(particles[Math.min(2, v[PerfProfile.PARTICLES])])));
      if (v[PerfProfile.SHADOWS] == 0) out.add(toggle(R.string.ingame_potato_shadows, s(R.string.ingame_off)));
      int[] view = {R.string.ingame_view_normal, R.string.ingame_view_medium, R.string.ingame_view_short};
      if (v[PerfProfile.VIEW] != 0) out.add(toggle(R.string.ingame_potato_view, s(view[Math.min(2, v[PerfProfile.VIEW])])));
    }
    return out;
  }

  /** "Anti-aliasing · 2× MSAA, Texture filter · 16×" or with FXAA; the effects of a suggestion in one line. */
  private String effects(int[] v) {
    String aa = v[PerfProfile.SAMPLES] >= 2 ? v[PerfProfile.SAMPLES] + "× MSAA"
        : v[PerfProfile.FXAA] != 0 ? s(R.string.ingame_fxaa) : s(R.string.ingame_off);
    String tf = v[PerfProfile.ANISO] >= 2 ? v[PerfProfile.ANISO] + "×" : s(R.string.ingame_off);
    return toggle(R.string.ingame_antialiasing, aa) + ", " + toggle(R.string.ingame_texture_filter, tf);
  }

  /** What the suggestion switches on that was off before (60 FPS, MSAA, FXAA, a stronger texture filter, the
   *  performance cores: measured in both windows, so popup 3 names them). */
  private List<String> newlyOn(int[] was, int[] now) {
    List<String> out = new ArrayList<>();
    if (now[PerfProfile.SMOOTH] != 0 && was[PerfProfile.SMOOTH] == 0) out.add(s(R.string.ingame_smooth60));
    if (now[PerfProfile.SAMPLES] > was[PerfProfile.SAMPLES])
      out.add(toggle(R.string.ingame_antialiasing, now[PerfProfile.SAMPLES] + "× MSAA"));
    if (now[PerfProfile.FXAA] != 0 && was[PerfProfile.FXAA] == 0) out.add(s(R.string.ingame_fxaa));
    if (now[PerfProfile.ANISO] > was[PerfProfile.ANISO])
      out.add(toggle(R.string.ingame_texture_filter, now[PerfProfile.ANISO] + "×"));
    if (now[PerfProfile.CORES] == 1 && was[PerfProfile.CORES] != 1) out.add(s(R.string.ingame_perf_cores));
    return out;
  }

  private static String join(List<String> parts) {
    StringBuilder b = new StringBuilder();
    for (String p : parts) b.append(b.length() > 0 ? ", " : "").append(p);
    return b.toString();
  }

  private static String bullets(List<String> lines) {
    StringBuilder b = new StringBuilder();
    for (String l : lines) b.append(b.length() > 0 ? "\n" : "").append("•  ").append(l);
    return b.toString();
  }

  private static int fps(int[] m) { return m == null || m[0] <= 0 ? 0 : Math.round(m[2] / (float) m[0]); }

  private static double smooth(int[] m) { return m == null || m[0] <= 0 ? 0 : Math.max(0, m[0] - m[3]) / (double) m[0]; }

  private static String window(int[] m) {
    if (m == null || m[0] <= 0) return "no seconds";
    return String.format(Locale.ROOT, "%d s, %.1f game pictures and %.1f shown a second, %d weak, level %.2f halves",
        m[0], m[1] / (double) m[0], m[2] / (double) m[0], m[3], m[4] / (double) m[0]);
  }

  static String describe(int[] v) {
    if (v == null) return "-";
    StringBuilder b = new StringBuilder("{");
    for (int i = 0; i < v.length; i++) b.append(i > 0 ? " " : "").append(PerfProfile.KEYS[i]).append('=').append(v[i]);
    return b.append('}').toString();
  }

  // ---- the popups' look (the app menu's, CoreMenu) ------------------------------------------------------------

  private int dp(int n) { return Math.round(n * host.activity().getResources().getDisplayMetrics().density); }

  private GradientDrawable shape(int fill, int stroke, int radius) {
    GradientDrawable d = new GradientDrawable();
    d.setColor(fill);
    d.setCornerRadius(dp(radius));
    d.setStroke(dp(1), stroke);
    return d;
  }

  private LinearLayout box() {
    LinearLayout v = new LinearLayout(host.activity());
    v.setOrientation(LinearLayout.VERTICAL);
    v.setPadding(dp(22), dp(18), dp(22), dp(16));
    return v;
  }

  private void title(LinearLayout box, String text) {
    TextView t = new TextView(host.activity());
    t.setText(text);
    t.setTextSize(20);
    t.setTextColor(INK);
    t.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    t.setGravity(Gravity.CENTER_HORIZONTAL);
    t.setTextAlignment(View.TEXT_ALIGNMENT_GRAVITY);
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
    lp.bottomMargin = dp(10);
    box.addView(t, lp);
  }

  private TextView paragraph(LinearLayout box, String text, int colour) {
    TextView t = new TextView(host.activity());
    t.setText(text);
    t.setTextSize(15);
    t.setTextColor(colour);
    t.setLineSpacing(0, 1.1f);
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
    lp.bottomMargin = dp(8);
    box.addView(t, lp);
    return t;
  }

  private Button button(String label, boolean go, View.OnClickListener click) {
    Button b = new Button(host.activity());
    b.setText(label);
    b.setAllCaps(false);
    b.setTextSize(16);
    b.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    b.setTextColor(FocusLook.text(android.content.res.ColorStateList.valueOf(INK)));
    int fill = go ? GO : SURFACE, edge = go ? GO_EDGE : BORDER;
    StateListDrawable look = new StateListDrawable();
    // the controller's focus golden as in the app menu (FocusLook, 0.99.11)
    look.addState(new int[] {android.R.attr.state_focused}, FocusLook.focused(host.activity()));
    look.addState(new int[] {android.R.attr.state_pressed}, shape(fill, INK, 6));
    look.addState(new int[0], shape(fill, edge, 6));
    b.setBackground(look);
    b.setMinHeight(dp(48));
    b.setMinimumHeight(dp(48));
    b.setPadding(dp(16), dp(8), dp(16), dp(8));
    b.setOnClickListener(click);
    return b;
  }

  private void buttons(LinearLayout box, Button... list) {
    LinearLayout row = new LinearLayout(host.activity());
    row.setOrientation(LinearLayout.HORIZONTAL);
    row.setBaselineAligned(false);
    for (int i = 0; i < list.length; i++) {
      LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
      if (i > 0) lp.setMarginStart(dp(12));
      row.addView(list[i], lp);
    }
    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(-1, -2);
    lp.topMargin = dp(8);
    box.addView(row, lp);
  }

  /** The popup in the app menu's look, the game's full screen (Immersive); `focus` gets the focus; B / Back / Esc run
   *  `back`. A presses the focused button (as in the app menu). */
  private AlertDialog dialog(LinearLayout box, View focus, Runnable back) {
    ScrollView scroll = new ScrollView(host.activity());
    scroll.addView(box);
    AlertDialog d = new AlertDialog.Builder(host.activity()).setView(scroll).create();
    GradientDrawable frame = shape(PANEL, BORDER, 10);
    frame.setStroke(dp(2), BORDER);
    if (d.getWindow() != null) d.getWindow().setBackgroundDrawable(frame);
    d.setCanceledOnTouchOutside(false);
    d.setOnCancelListener(x -> back.run());
    d.setOnKeyListener((x, key, event) -> {
      if (key == KeyEvent.KEYCODE_BUTTON_A) {
        return d.getWindow() != null && d.getWindow().superDispatchKeyEvent(new KeyEvent(event.getDownTime(),
            event.getEventTime(), event.getAction(), KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount()));
      }
      if (key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_ESCAPE) {
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) d.cancel();
        return true;
      }
      return false;
    });
    d.setOnShowListener(x -> focus.requestFocus());
    Immersive.show(d);
    // about as wide as the app menu's questions on a handheld, at most 560 dp
    if (d.getWindow() != null) {
      int w = Math.min(dp(560), Math.round(host.activity().getResources().getDisplayMetrics().widthPixels * 0.9f));
      d.getWindow().setLayout(w, ViewGroup.LayoutParams.WRAP_CONTENT);
    }
    return d;
  }

  /** A plain message in the same look, one button. */
  private void message(String text) {
    LinearLayout box = box();
    title(box, s(R.string.ingame_benchmark));
    paragraph(box, text, INK);
    final AlertDialog[] d = new AlertDialog[1];
    Button ok = button(s(android.R.string.ok), true, v -> d[0].dismiss());
    buttons(box, ok);
    d[0] = dialog(box, ok, () -> {});
  }
}
