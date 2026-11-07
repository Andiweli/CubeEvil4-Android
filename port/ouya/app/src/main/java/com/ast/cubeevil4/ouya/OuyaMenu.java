package com.ast.cubeevil4.ouya;

import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Locale;

/**
 * The OUYA app's menu (L3; wish of the project owner, 07.11.2026): one page in the look of the main
 * app's in-game menu (port/android-decomp/.../CoreMenu.java: its colours, buttons, headings), with what
 * the owner chose for the OUYA: GRAPHICS (resolution, view distance, colour calculation, the two Potato
 * switches), PERFORMANCE (frame skip; wish of the project owner, 07.11.2026: the main app's other
 * performance options do nothing on the OUYA or belong to its GLES 3 renderer) and COMFORT CONTROLS
 * (auto aim, always run, far camera). The game pauses while it is open.
 * Exclusive choices are selected buttons, switches show "On" / "Off". D-pad or left stick move, O
 * chooses, A, L3 or the OUYA button go back to the game. Android 4.1: no lambdas on API 17+ calls.
 */
final class OuyaMenu extends Dialog {
    /** The settings (Settings.PREFS); the activity sends them to the core and the renderer. */
    interface Host {
        void settingsChanged();
    }

    /** The preferences and their defaults: the light OUYA version (port/docs/ouya-machbarkeit.md). */
    static final class Settings {
        static final String PREFS = "settings";
        static final String EIGHTHS = "resolutionEighths", FAST_TEV = "fastTev", VIEW = "viewPercent",
                WEATHER = "weatherEffects", SPECIAL = "specialEffects", AUTO_AIM = "autoAim",
                ALWAYS_RUN = "alwaysRun", FAR_CAMERA = "farCamera", FRAME_SKIP = "frameSkip";
        static final int EIGHTHS_DEFAULT = 6, VIEW_DEFAULT = 50;

        static int eighths(SharedPreferences p) { return p.getInt(EIGHTHS, EIGHTHS_DEFAULT); }
        static boolean fastTev(SharedPreferences p) { return p.getBoolean(FAST_TEV, true); }
        /** On: while the graphics lag, older pictures are left out (the main app's Frame skip). */
        static boolean frameSkip(SharedPreferences p) { return p.getBoolean(FRAME_SKIP, true); }
        static int view(SharedPreferences p) { return p.getInt(VIEW, VIEW_DEFAULT); }
        /** Bit 0 weather, bit 1 special effects left out (runtime/potato.cpp); both off on the OUYA. */
        static int potato(SharedPreferences p) {
            return (p.getBoolean(WEATHER, false) ? 0 : 1) | (p.getBoolean(SPECIAL, false) ? 0 : 2);
        }
        /** re4_core_set_aim: speed 100 % << 8, bit 1 auto aim, bit 2 always run, bit 3 far camera. */
        static int aim(SharedPreferences p) {
            return 100 << 8 | (p.getBoolean(AUTO_AIM, false) ? 2 : 0) | (p.getBoolean(ALWAYS_RUN, false) ? 4 : 0)
                    | (p.getBoolean(FAR_CAMERA, false) ? 8 : 0);
        }
    }

    // the main app's colours (CoreMenu)
    private static final int PANEL = 0xff191f1c, SURFACE = 0xff222a25, BORDER = 0xff414b41;
    private static final int INK = 0xfff0eee5, MUTED = 0xffa6afa3, GOLD = 0xffd7bc7d, ACTIVE = 0xff45432e;
    private static final int GAP = 7;  // dp around each button (MenuLayout.GAP)

    private final Host host;
    private final SharedPreferences prefs;
    private final LinearLayout body;
    private final Button resume;
    private Button first;
    private int stickX, stickY;  // the left stick as D-pad steps: -1, 0, 1 each axis

    OuyaMenu(Context context, Host host) {
        super(context);
        this.host = host;
        prefs = context.getSharedPreferences(Settings.PREFS, Context.MODE_PRIVATE);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        LinearLayout root = column();
        root.setPadding(dp(20), dp(16), dp(20), dp(12));
        root.setBackgroundDrawable(shape(PANEL, BORDER, 10));
        LinearLayout header = row();
        TextView title = text(s(R.string.ingame_title), 22, true);
        title.setPadding(dp(GAP), 0, dp(8), 0);
        header.addView(title, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        resume = button(s(R.string.ingame_back), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dismiss();
            }
        });
        header.addView(resume);
        root.addView(header, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        separator(root);
        ScrollView scroll = new ScrollView(context);
        body = column();
        body.setPadding(0, dp(4), 0, dp(8));
        scroll.addView(body);
        root.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        setContentView(root);
        setCanceledOnTouchOutside(false);
        build();
        Window w = getWindow();
        w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        DisplayMetrics m = context.getResources().getDisplayMetrics();
        w.setLayout(Math.min(dp(1000), Math.round(m.widthPixels * .96f)),
                Math.min(dp(650), Math.round(m.heightPixels * .94f)));
    }

    private void build() {
        heading(R.string.ingame_graphics);
        first = choices(s(R.string.ingame_resolution), new String[] {"62 %", "75 %", "100 %"}, new int[] {5, 6, 8},
                Settings.eighths(prefs), Settings.EIGHTHS)[0];
        choices(s(R.string.ouya_view_distance), new String[] {"50 %", "75 %", "100 %"}, new int[] {50, 75, 100},
                Settings.view(prefs), Settings.VIEW);
        final Button[] colours = choices(s(R.string.ouya_colours),
                new String[] {s(R.string.ouya_colours_simple), s(R.string.ouya_colours_exact)}, new int[] {1, 0},
                Settings.fastTev(prefs) ? 1 : 0, null);
        colours[0].setOnClickListener(choose(colours, 0, Settings.FAST_TEV, true));
        colours[1].setOnClickListener(choose(colours, 1, Settings.FAST_TEV, false));
        help(R.string.ouya_graphics_help);
        heading(R.string.ingame_potato);
        LinearLayout potato = buttonsRow();
        toggle(potato, R.string.ingame_potato_weather, Settings.WEATHER);
        toggle(potato, R.string.ingame_potato_special, Settings.SPECIAL);
        help(R.string.ingame_potato_help);
        heading(R.string.ingame_perf_section);
        LinearLayout performance = buttonsRow();
        toggle(performance, R.string.ingame_frame_skip, Settings.FRAME_SKIP, true);
        performance.addView(new View(getContext()), cell());
        help(R.string.ingame_frame_skip_help);
        heading(R.string.ingame_comfort);
        LinearLayout comfort = buttonsRow();
        toggle(comfort, R.string.ingame_auto_aim, Settings.AUTO_AIM);
        toggle(comfort, R.string.ingame_always_run, Settings.ALWAYS_RUN);
        LinearLayout camera = buttonsRow();
        toggle(camera, R.string.ingame_far_camera, Settings.FAR_CAMERA);
        camera.addView(new View(getContext()), cell());  // half the row, as the switches above
        help(R.string.ingame_auto_aim_help);
        help(R.string.ingame_always_run_help);
        help(R.string.ingame_far_camera_help);
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (first != null)
            first.requestFocus();
    }

    // --- the main app's building blocks (CoreMenu), for Android 4.1 --------------------------------

    private String s(int id) { return getContext().getString(id); }

    private int dp(int n) { return Math.round(n * getContext().getResources().getDisplayMetrics().density); }

    private LinearLayout column() {
        LinearLayout v = new LinearLayout(getContext());
        v.setOrientation(LinearLayout.VERTICAL);
        return v;
    }

    private LinearLayout row() {
        LinearLayout v = new LinearLayout(getContext());
        v.setBaselineAligned(false);
        v.setGravity(Gravity.CENTER_VERTICAL);
        return v;
    }

    private TextView text(String label, int size, boolean bold) {
        TextView v = new TextView(getContext());
        v.setText(label);
        v.setTextSize(size);
        v.setTextColor(bold ? INK : MUTED);
        if (bold)
            v.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return v;
    }

    private GradientDrawable shape(int fill, int stroke, int radius) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(radius));
        d.setStroke(dp(1), stroke);
        return d;
    }

    private GradientDrawable focused(int fill) {
        GradientDrawable d = shape(fill, INK, 6);
        d.setStroke(dp(2), INK);
        return d;
    }

    private Button button(String label, View.OnClickListener click) {
        Button b = new Button(getContext());
        StateListDrawable state = new StateListDrawable();
        state.addState(new int[] {android.R.attr.state_focused, android.R.attr.state_selected}, focused(ACTIVE));
        state.addState(new int[] {android.R.attr.state_focused}, focused(SURFACE));
        state.addState(new int[] {android.R.attr.state_pressed}, shape(ACTIVE, GOLD, 6));
        state.addState(new int[] {android.R.attr.state_selected}, shape(ACTIVE, GOLD, 6));
        state.addState(new int[0], shape(SURFACE, BORDER, 6));
        b.setBackgroundDrawable(state);
        b.setTextSize(16);
        b.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        b.setTextColor(new ColorStateList(new int[][] {new int[] {-android.R.attr.state_enabled},
                new int[] {android.R.attr.state_selected}, new int[0]}, new int[] {0xff70796f, GOLD, INK}));
        b.setPadding(dp(12), dp(8), dp(12), dp(8));
        b.setMinHeight(dp(48));
        b.setMinimumHeight(dp(48));
        b.setMinWidth(0);
        b.setMinimumWidth(0);
        b.setText(label);
        b.setOnClickListener(click);
        return b;
    }

    private LinearLayout.LayoutParams cell() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1);
        lp.setMargins(dp(GAP), dp(GAP), dp(GAP), dp(GAP));
        return lp;
    }

    private void separator(LinearLayout parent) {
        View v = new View(getContext());
        v.setBackgroundColor(BORDER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1));
        lp.setMargins(0, dp(10), 0, dp(8));
        parent.addView(v, lp);
    }

    /** A section's heading (upper case, muted) with a faint line to the row's end, as in the main app. */
    private void heading(int id) {
        LinearLayout line = row();
        line.setPadding(dp(GAP), dp(16), dp(GAP), dp(8));
        TextView v = text(s(id).toUpperCase(Locale.getDefault()), 14, true);
        v.setTextColor(MUTED);
        v.setPadding(0, 0, dp(3), 0);
        line.addView(v);
        View rule = new View(getContext());
        rule.setBackgroundColor(BORDER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(1), 1);
        lp.leftMargin = dp(12);
        lp.gravity = Gravity.CENTER_VERTICAL;
        line.addView(rule, lp);
        body.addView(line);
    }

    private void help(int id) {
        TextView t = text(s(id), 15, false);
        t.setPadding(dp(GAP), dp(4), dp(GAP), dp(8));
        body.addView(t);
    }

    private LinearLayout buttonsRow() {
        LinearLayout r = row();
        body.addView(r, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        return r;
    }

    /** A row: its label (a column of 170 dp, as in the main app), then one button per choice. */
    private Button[] choices(String label, String[] labels, int[] values, int selected, final String key) {
        LinearLayout r = buttonsRow();
        TextView caption = text(label, 16, true);
        caption.setPadding(dp(GAP), 0, dp(8), 0);
        caption.setMinHeight(dp(48 + 2 * GAP));
        caption.setGravity(Gravity.LEFT | Gravity.CENTER_VERTICAL);
        r.addView(caption, new LinearLayout.LayoutParams(dp(170), ViewGroup.LayoutParams.WRAP_CONTENT));
        final Button[] buttons = new Button[labels.length];
        for (int i = 0; i < labels.length; i++) {
            final int value = values[i], index = i;
            buttons[i] = button(labels[i], key == null ? null : new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    select(buttons, index);
                    prefs.edit().putInt(key, value).apply();
                    host.settingsChanged();
                }
            });
            buttons[i].setSelected(value == selected);
            r.addView(buttons[i], cell());
        }
        // three columns of choices whatever their count (the rows line up)
        for (int i = labels.length; i < 3; i++)
            r.addView(new View(getContext()), cell());
        return buttons;
    }

    private View.OnClickListener choose(final Button[] buttons, final int index, final String key, final boolean on) {
        return new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                select(buttons, index);
                prefs.edit().putBoolean(key, on).apply();
                host.settingsChanged();
            }
        };
    }

    private static void select(Button[] buttons, int index) {
        for (int k = 0; k < buttons.length; k++)
            buttons[k].setSelected(k == index);
    }

    /** A switch "Label · On" (selected) or "Label · Off", as the main app's. */
    private void toggle(LinearLayout parent, int label, String key) {
        toggle(parent, label, key, false);
    }

    private void toggle(LinearLayout parent, final int label, final String key, boolean fallback) {
        final Button b = button("", null);
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean next = !b.isSelected();
                toggleLabel(b, label, next);
                prefs.edit().putBoolean(key, next).apply();
                host.settingsChanged();
            }
        });
        toggleLabel(b, label, prefs.getBoolean(key, fallback));
        parent.addView(b, cell());
    }

    private void toggleLabel(Button b, int label, boolean on) {
        b.setSelected(on);
        b.setText(getContext().getString(R.string.ingame_toggle_value, s(label),
                s(on ? R.string.ingame_on : R.string.ingame_off)));
    }

    // --- the OUYA controller ---------------------------------------------------------------------

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        final int key = event.getKeyCode();
        // A (KEYCODE_BUTTON_B), L3, the OUYA button (only its release arrives: KEYCODE_MENU), START: back
        if (key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_BUTTON_THUMBL
                || key == KeyEvent.KEYCODE_BUTTON_START || key == KeyEvent.KEYCODE_MENU) {
            if (event.getRepeatCount() == 0 && (event.getAction() == KeyEvent.ACTION_DOWN || key == KeyEvent.KEYCODE_MENU))
                dismiss();
            return true;
        }
        // O chooses
        if (key == KeyEvent.KEYCODE_BUTTON_A)
            return super.dispatchKeyEvent(new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(),
                    KeyEvent.KEYCODE_DPAD_CENTER, event.getRepeatCount()));
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) == 0 || event.getAction() != MotionEvent.ACTION_MOVE)
            return super.dispatchGenericMotionEvent(event);
        // the left stick as D-pad steps: one step each time it leaves the middle (the D-pad sends keys)
        float x = event.getAxisValue(MotionEvent.AXIS_X), y = event.getAxisValue(MotionEvent.AXIS_Y);
        int sx = x < -0.6f ? -1 : x > 0.6f ? 1 : 0, sy = y < -0.6f ? -1 : y > 0.6f ? 1 : 0;
        if (sx != stickX && sx != 0)
            step(sx < 0 ? KeyEvent.KEYCODE_DPAD_LEFT : KeyEvent.KEYCODE_DPAD_RIGHT);
        if (sy != stickY && sy != 0)
            step(sy < 0 ? KeyEvent.KEYCODE_DPAD_UP : KeyEvent.KEYCODE_DPAD_DOWN);
        stickX = sx;
        stickY = sy;
        return true;
    }

    private void step(int keyCode) {
        super.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, keyCode));
        super.dispatchKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, keyCode));
    }
}
