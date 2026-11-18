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
 * switches; since master 0.99.15 also its particles and shadows), PERFORMANCE (frame skip Off / Normal /
 * Aggressive; wish of the project owner, 07.11.2026: the main app's other
 * performance options do nothing on the OUYA or belong to its GLES 3 renderer), COMFORT CONTROLS
 * (auto aim, always run, far camera) and CHEATS (wish of the project owner, 07.11.2026: the main app's
 * switches and buttons for the player and the weapons, not its characters and extra games). The game
 * pauses while it is open.
 * Exclusive choices are selected buttons, switches show "On" / "Off". D-pad or left stick move, O
 * chooses, A, L3 or the OUYA button go back to the game. Android 4.1: no lambdas on API 17+ calls.
 */
final class OuyaMenu extends Dialog {
    /** The settings (Settings.PREFS); the activity sends them to the core and the renderer. */
    interface Host {
        void settingsChanged();
        /** A cheat button (runtime/cheats.cpp re4_core_cheat_action): 1 done, 2 already there (a weapon:
         *  raised to its highest levels), 0 not now, -1 no room, 3 queued, else unsupported. Any thread. */
        int cheatAction(int what);
        /** "Import other discs": the game ends, the app starts again with its disc installer. */
        void importDiscs();
        /** The game's data folder (disc1, disc2, memcard, instant), null before the game runs. */
        java.io.File gameData();
        /** "Delete All Data": the app starts again, the card and the instant save go before the core opens them. */
        void wipePlayerData();
        /** The game starts over in its process (the extra games' title menu), the language stays. */
        void restartGame();
        /** The European discs are installed: the game has the languages English .. Italian. */
        boolean gameLanguages();
        /** The game's language (0 the device's, 2 English .. 6 Italian, Settings.GAME_LANGUAGE): kept; `restart`: the
         *  game starts over with it now. */
        void gameLanguageChanged(int language, boolean restart);
        /** The device has a touch screen (the section "Touch Controls" shows only then). */
        boolean touchAvailable();
        /** RetroTouch's skin shown (TouchStyle.RE, GC, PS2, XBOX). */
        int touchLook();
        /** The touch settings changed (Settings), `look` the skin chosen (TouchStyle). */
        void touchSettingsChanged(int look);
    }

    /** The preferences and their defaults: the light OUYA version (port/docs/ouya-machbarkeit.md). */
    static final class Settings {
        static final String PREFS = "settings";
        static final String EIGHTHS = "resolutionEighths", FAST_TEV = "fastTev", VIEW = "viewPercent",
                WEATHER = "weatherEffects", SPECIAL = "specialEffects", AUTO_AIM = "autoAim",
                ALWAYS_RUN = "alwaysRun", FAR_CAMERA = "farCamera", FRAME_SKIP = "frameSkip", CHEATS = "cheats",
                // master 0.99.15 (merged 14.11.2026): the Potato mode's particles and shadows, frame skip in three steps
                PARTICLES = "particles", SHADOWS = "shadowsLight", FRAME_SKIP_LEVEL = "frameSkipLevel",
                GENTLE_QTE = "gentleQte", PARTICLES_ON = "particleEffects",
                // "Detailed lighting" (OUYA improvement 3, 15.11.2026): off (the default) lights the room's geometry
                // once per draw (gx_gl2 GxOptions::levelLight), about 19 % less GPU time in the village
                DETAILED_LIGHT = "detailedLight",
                // 1.0.0 (wish of the project owner, 17.11.2026): "Invert vertical" (the game's own option, also with
                // the keyboard), "Use performance cores" (old phones with fast and slow cores; the OUYA's are alike)
                INVERT = "invertVertical", FAST_CORES = "fastCores";
        // the first start's settings (wish of the project owner, 14.11.2026): resolution 75 %, view distance 35 %,
        // colours simplified, every Potato effect off, frame skip Normal
        static final int EIGHTHS_DEFAULT = 6, VIEW_DEFAULT = 35;

        static int eighths(SharedPreferences p) { return p.getInt(EIGHTHS, EIGHTHS_DEFAULT); }
        static boolean fastTev(SharedPreferences p) { return p.getBoolean(FAST_TEV, true); }
        /** The main app's Frame skip: 0 off, 1 Normal (while the graphics lag, older pictures are left out), 2
         *  Aggressive (also the game itself leaves out a picture while it falls behind: runtime/cpu_skip.cpp, patch
         *  0227; saves the processor's time). Before 14.11.2026 a switch (FRAME_SKIP): on is Normal. */
        static int frameSkip(SharedPreferences p) {
            return p.getInt(FRAME_SKIP_LEVEL, p.getBoolean(FRAME_SKIP, true) ? 1 : 0);
        }
        /** "Gentler QTEs (Professional)" (runtime/qte_touch.cpp, patch 0232): on by default, as in the main app
         *  (decision of the project owner, 14.11.2026). */
        static boolean gentleQte(SharedPreferences p) { return p.getBoolean(GENTLE_QTE, true); }
        // RetroTouch (15.11.2026): "Touch Controls" (TouchShowRule AUTO 0, FORCE 1, SUPPRESS 2), the latches
        static final String TOUCH_MODE = "touchMode", TOUCH_LATCH = "touchLatchLR", TOUCH_LATCH_B = "touchLatchB";
        // the game's language with the European discs (15.11.2026): 0 the device's, 2 English .. 6 Italian
        static final String GAME_LANGUAGE = "gameLanguage";
        static int gameLanguage(SharedPreferences p) { return p.getInt(GAME_LANGUAGE, 0); }
        static int touchMode(SharedPreferences p) { return p.getInt(TOUCH_MODE, TouchShowRule.AUTO); }
        static boolean touchLatch(SharedPreferences p) { return p.getBoolean(TOUCH_LATCH, false); }
        static boolean touchLatchB(SharedPreferences p) { return p.getBoolean(TOUCH_LATCH_B, false); }
        /** The room's light once per draw (the renderer's levelLight): when "Detailed lighting" is off. */
        static boolean simpleLight(SharedPreferences p) { return !p.getBoolean(DETAILED_LIGHT, false); }
        /** The Potato mode's particle effects, a switch since 14.11.2026 (wish of the project owner): on Normal (0),
         *  off None (2; fire and explosions stay at half as a warning). */
        static int particles(SharedPreferences p) { return p.getBoolean(PARTICLES_ON, false) ? 0 : 2; }
        /** The cheat switches (runtime/cheats.cpp): 1 health, 2 ammo, 4 one-hit kill, 8 QTEs, 512 invisibility. */
        static int cheats(SharedPreferences p) { return p.getInt(CHEATS, 0); }
        /** "Random items" (runtime/cheats.cpp, the main app's CoreMenu.withRandomItems): bit 1024 and a seed in
         *  bits 11..30, a new one each time it goes on. */
        static final int RANDOM_ITEMS_BIT = 1024, SEED_SHIFT = 11, SEED_BITS = 0xFFFFF << SEED_SHIFT;
        static int withRandomItems(int bits, boolean on, int seed) {
            bits &= ~(RANDOM_ITEMS_BIT | SEED_BITS);
            return on ? bits | RANDOM_ITEMS_BIT | ((seed << SEED_SHIFT) & SEED_BITS) : bits;
        }
        /** "Steady aim" (1.0.0, patch 0233; the main app's CoreMenu.STEADY_AIM_BIT): the laser and the scope still. */
        static final int STEADY_AIM_BIT = 1 << 31;
        /** The special costumes of Leon and Ashley (runtime/cheats.cpp; the main app's CoreMenu.COSTUME_BIT). */
        static final int COSTUME_BIT = 256;
        /** "Costumes · Leon's jacket" (1.0.0, patches 0235/0236; the main app's CoreActivity.LEON_JACKET). */
        static final String LEON_JACKET = "leonJacket";
        static final int COSTUME_NORMAL = 0, COSTUME_JACKET = 1, COSTUME_SPECIAL = 2;
        static boolean leonJacket(SharedPreferences p) { return p.getBoolean(LEON_JACKET, false); }
        /** The costume in force (the main app's CoreMenu.costumeChoice): Special with its bit, else the jacket. */
        static int costume(SharedPreferences p) {
            return (cheats(p) & COSTUME_BIT) != 0 ? COSTUME_SPECIAL : leonJacket(p) ? COSTUME_JACKET : COSTUME_NORMAL;
        }
        static int view(SharedPreferences p) { return p.getInt(VIEW, VIEW_DEFAULT); }
        /** runtime/potato.cpp: bit 0 weather, bit 1 special effects left out (both by default on the OUYA),
         *  bits 2..3 the particle effects (0 normal, 1 less, 2 none), bit 4 shadows and light effects left out. */
        static int potato(SharedPreferences p) {
            return (p.getBoolean(WEATHER, false) ? 0 : 1) | (p.getBoolean(SPECIAL, false) ? 0 : 2)
                    | (particles(p) << 2) | (p.getBoolean(SHADOWS, false) ? 0 : 16);
        }
        /** re4_core_set_aim: speed 100 % << 8, bit 0 invert vertical, bit 1 auto aim, bit 2 always run, bit 3 far
         *  camera. */
        static int aim(SharedPreferences p) {
            return 100 << 8 | (p.getBoolean(INVERT, false) ? 1 : 0) | (p.getBoolean(AUTO_AIM, false) ? 2 : 0)
                    | (p.getBoolean(ALWAYS_RUN, false) ? 4 : 0) | (p.getBoolean(FAR_CAMERA, false) ? 8 : 0);
        }
        static boolean fastCores(SharedPreferences p) { return p.getBoolean(FAST_CORES, false); }
    }

    // the main app's colours (CoreMenu)
    private static final int PANEL = 0xff191f1c, SURFACE = 0xff222a25, BORDER = 0xff414b41;
    private static final int INK = 0xfff0eee5, MUTED = 0xffa6afa3, GOLD = 0xffd7bc7d, ACTIVE = 0xff45432e;
    private static final int GAP = 7;  // dp around each button (MenuLayout.GAP)

    private final Host host;
    private final SharedPreferences prefs;
    private final android.os.Handler main = new android.os.Handler();
    private TextView cheatStatus;
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
        // the version, as in the main app's menu ("v0.99-Ouya"), left of the button with some room (wish of the
        // project owner, 15.11.2026)
        try {
            String version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
            if (version != null) {
                TextView v = text("v" + version, 14, false);
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.leftMargin = dp(GAP);
                lp.rightMargin = dp(18);
                header.addView(v, lp);
            }
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            // no version line
        }
        // the update button left of "Back to the game", as in the main app (wish of the project owner, 15.11.2026;
        // OuyaUpdate): "Up to date" until GitHub names a newer OUYA APK, also without the internet
        update = button("", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                openUpdate();
            }
        });
        LinearLayout.LayoutParams gap = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        gap.rightMargin = dp(GAP);
        header.addView(update, gap);
        showUpdate(OuyaUpdate.state(context));
        OuyaUpdate.check(context, new OuyaUpdate.Listener() {
            @Override
            public void checked(int state) {
                if (isShowing())
                    showUpdate(state);
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
        instantSave();
        heading(R.string.ingame_graphics);
        Button resolution = choices(s(R.string.ingame_resolution), new String[] {"62 %", "75 %", "100 %"},
                new int[] {5, 6, 8}, Settings.eighths(prefs), Settings.EIGHTHS)[0];
        if (first == null)
            first = resolution;
        // 35 %: fewer vertices for the core and the GPU (wish of the project owner, 07.11.2026: a steady
        // 30, whatever it takes)
        choices(s(R.string.ouya_view_distance), new String[] {"35 %", "50 %", "75 %", "100 %"},
                new int[] {35, 50, 75, 100}, Settings.view(prefs), Settings.VIEW);
        // the two as wide as the rows above together (wish of the project owner, 14.11.2026)
        final Button[] colours = choices(s(R.string.ouya_colours),
                new String[] {s(R.string.ouya_colours_simple), s(R.string.ouya_colours_exact)}, new int[] {1, 0},
                Settings.fastTev(prefs) ? 1 : 0, null, false);
        colours[0].setOnClickListener(choose(colours, 0, Settings.FAST_TEV, true));
        colours[1].setOnClickListener(choose(colours, 1, Settings.FAST_TEV, false));
        help(R.string.ouya_graphics_help);
        // four switches, the particle effects one of them (wish of the project owner, 14.11.2026)
        heading(R.string.ingame_potato);
        LinearLayout potato = buttonsRow();
        toggle(potato, R.string.ingame_potato_weather, Settings.WEATHER);
        toggle(potato, R.string.ingame_potato_special, Settings.SPECIAL);
        LinearLayout light = buttonsRow();
        toggle(light, R.string.ingame_potato_shadows, Settings.SHADOWS);
        toggle(light, R.string.ingame_potato_particles, Settings.PARTICLES_ON);
        LinearLayout detail = buttonsRow();
        toggle(detail, R.string.ouya_light_detail, Settings.DETAILED_LIGHT);
        detail.addView(new View(getContext()), cell());
        help(R.string.ingame_potato_help);
        help(R.string.ouya_light_help);
        heading(R.string.ingame_perf_section);
        choices(s(R.string.ingame_frame_skip), new String[] {s(R.string.ingame_off), s(R.string.ingame_skip_normal),
                s(R.string.ingame_skip_aggressive)}, new int[] {0, 1, 2}, Settings.frameSkip(prefs),
                Settings.FRAME_SKIP_LEVEL);
        // "Use performance cores" (1.0.0, wish of the project owner, 17.11.2026; off by default)
        LinearLayout cores = buttonsRow();
        toggle(cores, R.string.ingame_perf_cores, Settings.FAST_CORES);
        cores.addView(new View(getContext()), cell());
        help(R.string.ingame_skip_normal_help);
        help(R.string.ingame_skip_aggressive_help);
        help(R.string.ingame_perf_help);
        heading(R.string.ingame_comfort);
        LinearLayout comfort = buttonsRow();
        toggle(comfort, R.string.ingame_auto_aim, Settings.AUTO_AIM);
        toggle(comfort, R.string.ingame_always_run, Settings.ALWAYS_RUN);
        LinearLayout camera = buttonsRow();
        toggle(camera, R.string.ingame_far_camera, Settings.FAR_CAMERA);
        toggle(camera, R.string.ingame_gentle_qte, Settings.GENTLE_QTE, true);
        LinearLayout invert = buttonsRow();
        toggle(invert, R.string.ingame_aim_invert_v, Settings.INVERT);
        invert.addView(new View(getContext()), cell());
        help(R.string.ingame_auto_aim_help);
        help(R.string.ingame_always_run_help);
        help(R.string.ingame_far_camera_help);
        help(R.string.ouya_gentle_qte_help);
        help(R.string.ouya_invert_help);
        // keyboard and mouse (1.0.0): the main app's default keys (OuyaKeyboard)
        subheading(R.string.ingame_map_keyboard);
        help(R.string.ouya_keyboard_help);
        if (host.touchAvailable())
            touch();
        cheats();
        management();
    }

    // --- RetroTouch (wish of the project owner, 15.11.2026; the main app's touch settings) ----------------------

    private void touch() {
        heading(R.string.ingame_touch_controls);
        choices(s(R.string.ingame_touch_controls), new String[] {s(R.string.ingame_touch_auto),
                s(R.string.ingame_touch_force), s(R.string.ingame_touch_suppress)},
                new int[] {TouchShowRule.AUTO, TouchShowRule.FORCE, TouchShowRule.SUPPRESS}, Settings.touchMode(prefs),
                null);
        final Button[] mode = lastChoices;
        for (int i = 0; i < mode.length; i++) {
            final int index = i, value = i == 0 ? TouchShowRule.AUTO : i == 1 ? TouchShowRule.FORCE : TouchShowRule.SUPPRESS;
            mode[i].setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    select(mode, index);
                    prefs.edit().putInt(Settings.TOUCH_MODE, value).apply();
                    host.touchSettingsChanged(host.touchLook());
                }
            });
        }
        // the four skins of 1.0.0 across the row (RE, GC, PS2, XBOX; wish of the project owner, 17.11.2026)
        final Button[] style = choices(s(R.string.ingame_button_style), TouchStyle.NAMES,
                new int[] {TouchStyle.RE, TouchStyle.GC, TouchStyle.PS2, TouchStyle.XBOX}, host.touchLook(), null,
                false);
        for (int i = 0; i < style.length; i++) {
            final int index = i;
            style[i].setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    select(style, index);
                    host.touchSettingsChanged(index);
                }
            });
        }
        // the two switches beside the label "Comfort Controls", as wide as the rows above (wish of the project
        // owner, 15.11.2026)
        LinearLayout latches = captionRow(s(R.string.ingame_comfort));
        touchToggle(latches, R.string.ingame_latch, Settings.TOUCH_LATCH);
        touchToggle(latches, R.string.ingame_latch_b, Settings.TOUCH_LATCH_B);
        help(R.string.ingame_touch_help);
        help(R.string.ingame_button_style_help);
        help(R.string.ingame_latch_help);
    }

    private void touchToggle(LinearLayout parent, final int label, final String key) {
        final Button b = button("", null);
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean next = !b.isSelected();
                toggleLabel(b, label, next);
                prefs.edit().putBoolean(key, next).apply();
                host.touchSettingsChanged(host.touchLook());
            }
        });
        toggleLabel(b, label, prefs.getBoolean(key, false));
        parent.addView(b, cell());
    }

    // --- the cheats (the main app's CoreMenu.cheats, without the characters and the extra games) ---

    private void cheats() {
        heading(R.string.ouya_cheats);
        subheading(R.string.ingame_cheat_section_player);
        LinearLayout player = buttonsRow();
        cheatSwitch(player, R.string.ingame_cheat_health, 1);
        cheatSwitch(player, R.string.ingame_cheat_ammo, 2);
        cheatSwitch(player, R.string.ingame_cheat_invisible, 512);
        // the second row as in the main app's 1.0.0: one-hit kill, steady aim, the costumes (wish of the project
        // owner, 17.11.2026)
        LinearLayout player2 = buttonsRow();
        cheatSwitch(player2, R.string.ingame_cheat_onehit, 4);
        cheatSwitch(player2, R.string.ingame_cheat_steady, Settings.STEADY_AIM_BIT);
        costumeSwitch(player2);
        subheading(R.string.ingame_cheat_section_weapons);
        LinearLayout weapons = buttonsRow();
        cheatButton(weapons, 5, R.string.ingame_cheat_handcannon);
        cheatButton(weapons, 1, R.string.ingame_cheat_killer7);
        cheatButton(weapons, 9, R.string.ingame_cheat_matilda);
        LinearLayout launchers = buttonsRow();
        cheatButton(launchers, 6, R.string.ingame_cheat_chicago);
        cheatButton(launchers, 8, R.string.ingame_cheat_mine);
        cheatButton(launchers, 7, R.string.ingame_cheat_rocket);
        subheading(R.string.ingame_cheat_section_game);
        // beside "Automatic QTEs": "Random items" and "Unlock extra games", as in the main app (wish of the project
        // owner, 14.11.2026)
        LinearLayout game = buttonsRow();
        cheatSwitch(game, R.string.ingame_cheat_qte, 8);
        final Button items = button("", null);
        items.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean next = !items.isSelected();
                toggleLabel(items, R.string.ingame_cheat_random_items, next);
                prefs.edit().putInt(Settings.CHEATS, Settings.withRandomItems(Settings.cheats(prefs), next,
                        new java.util.Random().nextInt(1 << 20))).apply();
                host.settingsChanged();
            }
        });
        toggleLabel(items, R.string.ingame_cheat_random_items, (Settings.cheats(prefs) & Settings.RANDOM_ITEMS_BIT) != 0);
        game.addView(items, cell());
        final Button extras = button(s(R.string.ingame_cheat_extras), null);
        extras.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                flash(extras);
                unlockExtras();
            }
        });
        game.addView(extras, cell());
        // the case and the money under them (the main app's GENERAL & INVENTORY)
        LinearLayout inventory = buttonsRow();
        cheatButton(inventory, 2, R.string.ingame_cheat_case);
        cheatButton(inventory, 3, R.string.ingame_cheat_money);
        cheatStatus = text("", 15, false);
        cheatStatus.setTextColor(GOLD);
        cheatStatus.setPadding(dp(GAP), dp(6), dp(GAP), dp(2));
        cheatStatus.setVisibility(View.GONE);
        body.addView(cheatStatus);
        help(R.string.ingame_cheats_help);
        help(R.string.ingame_cheat_random_items_help);
    }

    /** "Costumes · Normal / Leon's jacket / Special" (the main app's CoreMenu.costumeSwitch, 1.0.0): each press the
     *  next. Normal: the game's own. Leon's jacket: Leon keeps his brown jacket for the whole story (patches 0235,
     *  0236). Special: the special costumes of Leon and Ashley (COSTUME_BIT). */
    private void costumeSwitch(LinearLayout parent) {
        final int[] labels = {R.string.ingame_cheat_costume_normal, R.string.ingame_cheat_costume_jacket,
                R.string.ingame_cheat_costume_special};
        final Button b = button("", null);
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                int next = (Settings.costume(prefs) + 1) % 3;
                int bits = Settings.cheats(prefs);
                bits = next == Settings.COSTUME_SPECIAL ? bits | Settings.COSTUME_BIT : bits & ~Settings.COSTUME_BIT;
                prefs.edit().putInt(Settings.CHEATS, bits)
                        .putBoolean(Settings.LEON_JACKET, next == Settings.COSTUME_JACKET).apply();
                costumeLabel(b, labels[next], next != Settings.COSTUME_NORMAL);
                host.settingsChanged();
            }
        });
        int now = Settings.costume(prefs);
        costumeLabel(b, labels[now], now != Settings.COSTUME_NORMAL);
        parent.addView(b, cell());
    }

    private void costumeLabel(Button b, int value, boolean lit) {
        b.setSelected(lit);
        b.setText(getContext().getString(R.string.ingame_toggle_value, s(R.string.ingame_cheat_costume), s(value)));
    }

    private void flash(final Button b) {
        b.setSelected(true);
        main.postDelayed(new Runnable() {
            @Override
            public void run() {
                b.setSelected(false);
            }
        }, 600);
    }

    private void cheatLine(String text) {
        if (cheatStatus == null)
            return;
        cheatStatus.setText(text);
        cheatStatus.setVisibility(View.VISIBLE);
    }

    /** "Unlock extra games" (re4_core_cheat_action 4): Assignment Ada and The Mercenaries; the title menu shows them
     *  after a restart of the game, now or later (the main app's CoreMenu.askRestartForExtras). */
    private void unlockExtras() {
        new Thread(new Runnable() {
            @Override
            public void run() {
                final int done = host.cheatAction(4);
                main.post(new Runnable() {
                    @Override
                    public void run() {
                        if (!isShowing())
                            return;
                        if (done == 1 || done == 2 || done == 3) {
                            // kept by the app as well: a system file read without it (no save since) gets it back
                            prefs.edit().putInt(Settings.CHEATS, Settings.cheats(prefs) | 16).apply();
                            host.settingsChanged();
                            if (done == 2)
                                cheatLine(s(R.string.ingame_cheat_extras_already));
                            else
                                askRestartForExtras();
                        } else {
                            showCheatResult(4, R.string.ingame_cheat_extras, done);
                        }
                    }
                });
            }
        }, "cheat").start();
    }

    private void askRestartForExtras() {
        android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(getContext())
                .setTitle(R.string.ingame_language_restart_title)
                .setMessage(R.string.ingame_cheat_extras_text)
                .setNegativeButton(R.string.ingame_later, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int which) {
                        cheatLine(s(R.string.ingame_cheat_extras_later));
                    }
                })
                .setPositiveButton(R.string.ingame_restart, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int which) {
                        dismiss();
                        host.restartGame();
                    }
                }).create();
        showDialog(dialog, true);
    }

    /** A section's second level: its name in the muted colour, without the line */
    private void subheading(int id) {
        TextView v = text(s(id), 15, true);
        v.setTextColor(MUTED);
        v.setPadding(dp(GAP), dp(10), dp(GAP), dp(2));
        body.addView(v);
    }

    private void cheatSwitch(LinearLayout parent, final int label, final int bit) {
        final Button b = button("", null);
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean next = !b.isSelected();
                toggleLabel(b, label, next);
                int bits = Settings.cheats(prefs);
                prefs.edit().putInt(Settings.CHEATS, next ? bits | bit : bits & ~bit).apply();
                host.settingsChanged();
            }
        });
        toggleLabel(b, label, (Settings.cheats(prefs) & bit) != 0);
        parent.addView(b, cell());
    }

    /** A button that changes the game once (the weapon, the case and the money stay in the next save): lit
     *  up for a moment, its answer below the buttons */
    private void cheatButton(LinearLayout parent, final int what, final int label) {
        final Button b = button(s(label), null);
        b.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                b.setSelected(true);
                main.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        b.setSelected(false);
                    }
                }, 600);
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        final int done = host.cheatAction(what);
                        main.post(new Runnable() {
                            @Override
                            public void run() {
                                showCheatResult(what, label, done);
                            }
                        });
                    }
                }, "cheat").start();
            }
        });
        parent.addView(b, cell());
    }

    private void showCheatResult(int what, int label, int done) {
        if (!isShowing() || cheatStatus == null)
            return;
        final String text;
        if (done == 1)
            text = getContext().getString(R.string.ingame_cheat_done, s(label));
        else if (done == 2 && (what == 1 || what >= 5))
            text = getContext().getString(R.string.ingame_cheat_raised, s(label));
        else if (done == 2)
            text = getContext().getString(R.string.ingame_cheat_already, s(label));
        else if (done == 0)
            text = s(R.string.ingame_cheat_not_now);
        else if (done == -1)
            text = s(R.string.ingame_cheat_no_room);
        else if (done == 3)
            text = s(R.string.ingame_cheat_queued);
        else
            text = s(R.string.ingame_cheat_unsupported);
        cheatStatus.setText(text);
        cheatStatus.setVisibility(View.VISIBLE);
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (first != null)
            first.requestFocus();
    }

    // --- the instant save (wish of the project owner, 14.11.2026; the main app's CoreMenu.home) ------------

    private Button[] lastChoices;  // the buttons of the last choices() row
    private Button update;         // the update button in the header (OuyaUpdate)
    private static final int UPDATE_GREEN = 0xff2d5233, UPDATE_GREEN_EDGE = 0xff4f8a57;  // the main app's
    private static final int UPDATE_RED = 0xff7a2a26, UPDATE_RED_EDGE = 0xffb2463f;

    /** Green "Up to date" (off) or red "Update" (opens the release's page), as in the main app. */
    private void showUpdate(int state) {
        boolean older = state == OuyaUpdate.OLDER;
        int fill = older ? UPDATE_RED : UPDATE_GREEN, stroke = older ? UPDATE_RED_EDGE : UPDATE_GREEN_EDGE;
        StateListDrawable look = new StateListDrawable();
        look.addState(new int[] {android.R.attr.state_focused}, focused(fill));
        look.addState(new int[] {android.R.attr.state_pressed}, shape(fill, INK, 6));
        look.addState(new int[0], shape(fill, stroke, 6));
        update.setBackgroundDrawable(look);
        update.setTextColor(INK);
        update.setText(s(older ? R.string.ingame_update : R.string.ingame_up_to_date));
        update.setEnabled(older);
        update.setFocusable(older);
    }

    /** The newest release's page on GitHub in the browser; the OUYA may have none: then its address */
    private void openUpdate() {
        String page = OuyaUpdate.page(getContext());
        try {
            android.content.Intent view = new android.content.Intent(android.content.Intent.ACTION_VIEW,
                    android.net.Uri.parse(page));
            view.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
            getContext().startActivity(view);
            android.widget.Toast.makeText(getContext(), R.string.ingame_update_page, android.widget.Toast.LENGTH_LONG)
                    .show();
        } catch (android.content.ActivityNotFoundException e) {
            android.widget.Toast.makeText(getContext(), getContext().getString(R.string.ouya_no_browser, page),
                    android.widget.Toast.LENGTH_LONG).show();
        }
    }
    private Button saveSlot, loadSlot;
    private TextView slotDetails, slotDate, slotStatus;
    private boolean slotPresent, working;

    private void instantSave() {
        heading(R.string.ingame_instant);
        LinearLayout slot = row();
        slot.setBackgroundDrawable(shape(0xff202720, BORDER, 6));
        LinearLayout metadata = column();
        metadata.setPadding(dp(12), dp(6), dp(12), dp(6));
        metadata.addView(text(s(R.string.ingame_slot), 18, true));
        slotDetails = text("", 15, false);
        slotDate = text("", 15, false);
        metadata.addView(slotDetails);
        metadata.addView(slotDate);
        slot.addView(metadata, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        saveSlot = button(s(R.string.ingame_instant_save), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                confirmInstant(1);
            }
        });
        loadSlot = button(s(R.string.ingame_instant_load), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                confirmInstant(2);
            }
        });
        for (Button b : new Button[] {saveSlot, loadSlot}) {
            b.setEnabled(false);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(170), ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.setMargins(dp(GAP), dp(GAP), dp(GAP), dp(GAP));
            slot.addView(b, lp);
        }
        LinearLayout.LayoutParams frame = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        frame.setMargins(dp(GAP), dp(GAP), dp(GAP), dp(GAP));
        body.addView(slot, frame);
        slotStatus = text(s(R.string.ingame_instant_checking), 15, false);
        slotStatus.setPadding(dp(GAP), dp(4), dp(GAP), dp(4));
        body.addView(slotStatus);
        main.post(new Runnable() {
            @Override
            public void run() {
                instant(0);
            }
        });
    }

    private void confirmInstant(final int operation) {
        if (working)
            return;
        if (operation == 1 && !slotPresent) {
            instant(1);
            return;
        }
        android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(getContext())
                .setTitle(operation == 1 ? R.string.ingame_instant_overwrite_title : R.string.ingame_instant_load_title)
                .setMessage(operation == 1 ? R.string.ingame_instant_overwrite : R.string.ingame_instant_load_warning)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(operation == 1 ? R.string.ingame_replace : R.string.ingame_instant_load,
                        new android.content.DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(android.content.DialogInterface d, int which) {
                                instant(operation);
                            }
                        }).create();
        showDialog(dialog, true);
    }

    /** 0 the slot's state, 1 save, 2 load (re4_core_instant), on a thread of its own */
    private void instant(final int operation) {
        if (working)
            return;
        working = true;
        saveSlot.setEnabled(false);
        loadSlot.setEnabled(false);
        showSlotStatus(operation == 0 ? R.string.ingame_instant_checking : R.string.ingame_instant_working);
        new Thread(new Runnable() {
            @Override
            public void run() {
                long[] result;
                try {
                    result = OuyaActivity.nativeInstant(operation);
                } catch (RuntimeException e) {
                    result = null;
                }
                final long[] done = result;
                main.post(new Runnable() {
                    @Override
                    public void run() {
                        working = false;
                        if (!isShowing())
                            return;
                        if (done == null || done.length < 9) {
                            showSlotStatus(R.string.ingame_instant_failed);
                            return;
                        }
                        showInstant(done);
                        if (operation == 2 && done[0] == 0)
                            dismiss();
                        else if (operation == 1 && done[0] == 0)
                            showSlotStatus(R.string.ingame_instant_saved);
                        if (!saveSlot.hasFocus() && !loadSlot.hasFocus() && getCurrentFocus() == null && first != null)
                            first.requestFocus();
                    }
                });
            }
        }, "InstantSlot").start();
    }

    private void showInstant(long[] data) {
        saveSlot.setEnabled(data[2] != 0);
        loadSlot.setEnabled(data[3] != 0);
        if (data[0] == -1) {
            showSlotStatus(R.string.ingame_instant_unavailable);
            return;
        }
        slotPresent = data[1] != 0;
        if (slotPresent) {
            String room = String.format(Locale.ROOT, "r%03x", data[4]);
            String play = String.format(Locale.ROOT, "%02d:%02d:%02d", data[6] / 3600, data[6] / 60 % 60, data[6] % 60);
            slotDetails.setText(getContext().getString(R.string.ingame_slot_details,
                    String.format(Locale.ROOT, "%d–%d", data[5] / 100, data[5] % 100), room, play));
            String when = java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.SHORT,
                    java.text.DateFormat.SHORT).format(new java.util.Date(data[7] * 1000));
            slotDate.setText(getContext().getString(R.string.ingame_slot_date, when));
        } else {
            slotDetails.setText(s(R.string.ingame_instant_empty));
            slotDate.setText("");
        }
        int status = R.string.ingame_instant_ready;
        if (data[0] == -5)
            status = R.string.ingame_instant_unsupported;
        else if (data[0] == -3)
            status = R.string.ingame_instant_damaged;
        else if (data[0] == -4)
            status = R.string.ingame_instant_failed;
        else if (data[2] == 0 && data[8] != 0)
            status = R.string.ingame_instant_no_save;
        else if (data[2] == 0 && data[3] == 0)
            status = R.string.ingame_instant_unavailable;
        showSlotStatus(status);
    }

    private void showSlotStatus(int message) {
        slotStatus.setText(s(message));
        slotStatus.setVisibility(message == R.string.ingame_instant_ready ? View.GONE : View.VISIBLE);
    }

    // --- MANAGEMENT and SOURCE (wish of the project owner, 14.11.2026; the main app's CoreMenu.management) -----
    // Android 4.1 has no file dialog: the exports go to the folder CubeEvil4-Potato on the drive with the game data,
    // the import lists the .gci and .zip files there and in the Download folders (OuyaPlayerData).

    static final String SOURCE_URL = "https://github.com/Andiweli/CubeEvil4-Android";
    private static final int WIPE_RED = 0xff4a2323, WIPE_RED_EDGE = 0xff7d3b3b, WIPE_TEXT = 0xffffd9d2;
    // re4_card_files.h
    private static final int CARD_OK = 0, CARD_NOCARD = -1, CARD_EXISTS = -2, CARD_NOSPACE = -3, CARD_BAD = -4,
            CARD_BUSY = -5;
    private TextView cardLine, sourceLine;

    private void management() {
        heading(R.string.ingame_management);
        help(R.string.ouya_card_help);
        cardLine = text("", 15, false);
        cardLine.setTextColor(GOLD);
        cardLine.setPadding(dp(GAP), dp(2), dp(GAP), dp(2));
        cardLine.setVisibility(View.GONE);
        body.addView(cardLine);
        LinearLayout exports = buttonsRow();
        exports.addView(button(s(R.string.ingame_export), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                export(false);
            }
        }), cell());
        exports.addView(button(s(R.string.ingame_export_zip), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                export(true);
            }
        }), cell());
        LinearLayout imports = buttonsRow();
        imports.addView(button(s(R.string.ingame_import), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                chooseImport();
            }
        }), cell());
        imports.addView(wipeButton(), cell());
        // "Import other discs" across the whole width
        LinearLayout discs = buttonsRow();
        discs.addView(button(s(R.string.ingame_import_discs), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                dismiss();
                host.importDiscs();
            }
        }), cell());
        help(R.string.ingame_discs_help);
        // the game's language before "Source and updates", as in the main app (wish of the project owner, 15.11.2026)
        if (host.gameLanguages())
            gameLanguage();
        heading(R.string.ingame_source);
        LinearLayout source = buttonsRow();
        source.addView(button(getContext().getString(R.string.ingame_source_line, SOURCE_URL),
                new View.OnClickListener() {
                    @Override
                    public void onClick(View v) {
                        openSource();
                    }
                }), cell());
        sourceLine = text("", 15, false);
        sourceLine.setPadding(dp(GAP), dp(2), dp(GAP), dp(2));
        sourceLine.setVisibility(View.GONE);
        body.addView(sourceLine);
        // below the link, centred (wish of the project owner, 15.11.2026): where the app comes from
        TextView only = text(s(R.string.ouya_source_only), 15, false);
        only.setGravity(Gravity.CENTER_HORIZONTAL);
        only.setPadding(dp(GAP), dp(6), dp(GAP), dp(10));
        body.addView(only, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
    }

    /** The game's language with the European discs, as in the main app (CoreMenu.gameLanguage): the device's or one
     *  of the five; another language than now restarts the game (asked first). At the bottom of the menu (wish of
     *  the project owner, 15.11.2026). */
    private void gameLanguage() {
        heading(R.string.ingame_game_language);
        final String[] labels = {s(R.string.ingame_language_device), "English", "Deutsch", "Français", "Español",
                "Italiano"};
        final int[] values = {0, 2, 3, 4, 5, 6};
        final Button[] buttons = new Button[labels.length];
        final int now = Settings.gameLanguage(prefs);
        LinearLayout row = null;
        for (int i = 0; i < labels.length; i++) {
            if (i % 3 == 0)
                row = buttonsRow();
            final int index = i;
            buttons[i] = button(labels[i], new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    chooseLanguage(buttons, values, index);
                }
            });
            buttons[i].setSelected(values[i] == now);
            row.addView(buttons[i], cell());
        }
        help(R.string.ingame_language_help);
    }

    private void chooseLanguage(final Button[] buttons, final int[] values, final int index) {
        final int was = Settings.gameLanguage(prefs), want = values[index];
        if (want == was)
            return;
        if (OuyaActivity.effectiveLanguage(want) == OuyaActivity.effectiveLanguage(was)) {
            // the same language for the game (the device's is that one): kept, no restart
            select(buttons, index);
            host.gameLanguageChanged(want, false);
            return;
        }
        android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(getContext())
                .setTitle(R.string.ingame_language_restart_title)
                .setMessage(R.string.ingame_language_restart_text)
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(R.string.ingame_restart, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int which) {
                        select(buttons, index);
                        dismiss();
                        host.gameLanguageChanged(want, true);
                    }
                }).create();
        showDialog(dialog, true);
    }

    private void cardMessage(final String text) {
        main.post(new Runnable() {
            @Override
            public void run() {
                if (cardLine == null)
                    return;
                cardLine.setText(text);
                cardLine.setVisibility(View.VISIBLE);
            }
        });
    }

    /** "Delete All Data" in a quiet red, as the main app's */
    private Button wipeButton() {
        Button wipe = button(s(R.string.ingame_wipe), new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                askWipe();
            }
        });
        StateListDrawable quiet = new StateListDrawable();
        quiet.addState(new int[] {android.R.attr.state_focused}, focused(WIPE_RED));
        quiet.addState(new int[] {android.R.attr.state_pressed}, shape(WIPE_RED, GOLD, 6));
        quiet.addState(new int[0], shape(WIPE_RED, WIPE_RED_EDGE, 6));
        wipe.setBackgroundDrawable(quiet);
        wipe.setTextColor(WIPE_TEXT);
        return wipe;
    }

    private void askWipe() {
        android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(getContext())
                .setTitle(R.string.ingame_wipe)
                .setMessage(R.string.ouya_wipe_text)
                .setNegativeButton(R.string.ingame_wipe_cancel, null)
                .setPositiveButton(R.string.ingame_wipe_yes, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int which) {
                        dismiss();
                        host.wipePlayerData();
                    }
                }).create();
        showDialog(dialog, true);
    }

    /** The card's files as .gci (Dolphin's names), or with the instant save in one .zip (the main app's format) */
    private void export(final boolean zip) {
        final java.io.File data = host.gameData();
        if (data == null)
            return;
        new Thread(new Runnable() {
            @Override
            public void run() {
                String[] rows = OuyaActivity.nativeCardList();
                if (rows == null) {
                    cardMessage(s("busy".equals(OuyaActivity.nativeCardNote()) ? R.string.ingame_card_busy
                            : R.string.ingame_card_none));
                    return;
                }
                java.util.List<String> names = new java.util.ArrayList<String>();
                java.util.List<byte[]> files = new java.util.ArrayList<byte[]>();
                for (String row : rows) {
                    String[] f = row.split("\t");
                    int index;
                    try {
                        index = Integer.parseInt(f[0]);
                    } catch (NumberFormatException e) {
                        continue;
                    }
                    byte[] gci = OuyaActivity.nativeCardExport(index);
                    if (gci == null)
                        continue;
                    String name = OuyaActivity.nativeCardFileName(index);
                    names.add(OuyaPlayerData.safeName(name != null ? name : (f.length > 1 ? f[1] : "file" + index)
                            + (name != null && name.endsWith(".gci") ? "" : ".gci")));
                    files.add(gci);
                }
                java.io.File folder = OuyaPlayerData.exportFolder(data);
                java.io.File target = folder;
                try {
                    if (zip) {
                        target = new java.io.File(folder, OuyaPlayerData.zipName(System.currentTimeMillis()));
                        OuyaPlayerData.writeZip(target, names, files, OuyaPlayerData.instant(data),
                                appVersion(), android.os.Build.MODEL);
                    } else {
                        for (int i = 0; i < files.size(); i++)
                            OuyaPlayerData.write(new java.io.File(folder, names.get(i)), files.get(i));
                    }
                } catch (java.io.IOException e) {
                    cardMessage(getContext().getString(R.string.ouya_export_failed, target.getPath()) + " ("
                            + e.getMessage() + ")");
                    return;
                }
                String count = getContext().getResources().getQuantityString(R.plurals.ingame_exported,
                        zip ? 1 : files.size(), zip ? 1 : files.size());
                cardMessage(count + " " + getContext().getString(R.string.ouya_exported_to, target.getPath()));
            }
        }, "Export").start();
    }

    private String appVersion() {
        try {
            return "CubeEvil4 Potato " + getContext().getPackageManager()
                    .getPackageInfo(getContext().getPackageName(), 0).versionName;
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            return "CubeEvil4 Potato";
        }
    }

    /** The .gci and .zip files of the export folder and the Download folders, newest first; one is imported */
    private void chooseImport() {
        final java.io.File data = host.gameData();
        if (data == null)
            return;
        final java.util.List<java.io.File> found = OuyaPlayerData.candidates(data);
        if (found.isEmpty()) {
            cardMessage(getContext().getString(R.string.ouya_import_none, OuyaPlayerData.exportFolder(data).getPath()));
            return;
        }
        String[] labels = new String[found.size()];
        for (int i = 0; i < labels.length; i++)
            labels[i] = found.get(i).getName() + "\n" + found.get(i).getParent();
        android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(getContext())
                .setTitle(R.string.ingame_import)
                .setItems(labels, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int which) {
                        importFile(found.get(which), data);
                    }
                })
                .setNegativeButton(android.R.string.cancel, null).create();
        showDialog(dialog, false);
    }

    private void importFile(final java.io.File file, final java.io.File data) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                final java.util.List<String> names = new java.util.ArrayList<String>();
                final java.util.List<byte[]> files = new java.util.ArrayList<byte[]>();
                byte[] instant = null;
                try {
                    if (file.getName().toLowerCase(Locale.ROOT).endsWith(".zip")) {
                        OuyaPlayerData.Zip z = OuyaPlayerData.readZip(file);
                        names.addAll(z.names);
                        files.addAll(z.files);
                        instant = z.instant;
                    } else {
                        names.add(file.getName());
                        files.add(OuyaPlayerData.read(file, 4 << 20));
                    }
                } catch (java.io.IOException e) {
                    cardMessage(getContext().getString(R.string.ingame_import_bad, file.getName(), e.getMessage()));
                    return;
                }
                StringBuilder report = new StringBuilder();
                final java.util.List<Integer> exist = new java.util.ArrayList<Integer>();
                for (int i = 0; i < files.size(); i++) {
                    int r = OuyaActivity.nativeCardImport(files.get(i), false);
                    if (r == CARD_EXISTS)
                        exist.add(i);
                    else
                        line(report, importResult(r, names.get(i)));
                }
                if (instant != null) {
                    try {
                        OuyaPlayerData.writeInstant(data, instant);
                        line(report, getContext().getString(R.string.ingame_imported, s(R.string.ingame_instant)));
                    } catch (java.io.IOException e) {
                        line(report, getContext().getString(R.string.ingame_import_bad, s(R.string.ingame_instant),
                                e.getMessage()));
                    }
                }
                if (report.length() > 0)
                    cardMessage(report.toString());
                if (!exist.isEmpty()) {
                    final String before = report.toString();
                    main.post(new Runnable() {
                        @Override
                        public void run() {
                            askReplace(names, files, exist, before);
                        }
                    });
                } else if (report.length() == 0) {
                    cardMessage(getContext().getString(R.string.ingame_import_bad, file.getName(), "-"));
                }
                if (instant != null)
                    main.post(new Runnable() {
                        @Override
                        public void run() {
                            if (isShowing() && !working)
                                instant(0);
                        }
                    });
            }
        }, "Import").start();
    }

    private static void line(StringBuilder report, String text) {
        if (report.length() > 0)
            report.append('\n');
        report.append(text);
    }

    private String importResult(int r, String name) {
        switch (r) {
        case CARD_OK: return getContext().getString(R.string.ingame_imported, name);
        case CARD_NOSPACE: return getContext().getString(R.string.ingame_import_no_space, name);
        case CARD_BUSY: return getContext().getString(R.string.ingame_import_busy, name);
        case CARD_NOCARD: return getContext().getString(R.string.ingame_import_no_card, name);
        default: return getContext().getString(R.string.ingame_import_bad, name, OuyaActivity.nativeCardNote());
        }
    }

    /** Files of that name are on the card already: replace them all, or keep the card's */
    private void askReplace(final java.util.List<String> names, final java.util.List<byte[]> files,
            final java.util.List<Integer> exist, final String before) {
        StringBuilder which = new StringBuilder();
        for (int i : exist) {
            if (which.length() > 0)
                which.append(", ");
            which.append(names.get(i));
        }
        android.app.AlertDialog dialog = new android.app.AlertDialog.Builder(getContext())
                .setMessage(getContext().getString(R.string.ingame_import_exists, which.toString()))
                .setNegativeButton(android.R.string.cancel, null)
                .setPositiveButton(R.string.ingame_replace, new android.content.DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(android.content.DialogInterface d, int w) {
                        new Thread(new Runnable() {
                            @Override
                            public void run() {
                                StringBuilder report = new StringBuilder(before);
                                for (int i : exist)
                                    line(report, importResult(OuyaActivity.nativeCardImport(files.get(i), true),
                                            names.get(i)));
                                cardMessage(report.toString());
                            }
                        }, "Import").start();
                    }
                }).create();
        showDialog(dialog, true);
    }

    /** The source on GitHub; the OUYA may have no browser: then the address below the button */
    private void openSource() {
        try {
            android.content.Intent view = new android.content.Intent(android.content.Intent.ACTION_VIEW,
                    android.net.Uri.parse(SOURCE_URL));
            view.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
            getContext().startActivity(view);
        } catch (android.content.ActivityNotFoundException e) {
            sourceLine.setText(getContext().getString(R.string.ouya_no_browser, SOURCE_URL));
            sourceLine.setVisibility(View.VISIBLE);
        }
    }

    /** A dialog over the menu with the OUYA controller: O chooses, A cancels; the focus on "Cancel" first */
    private void showDialog(final android.app.AlertDialog dialog, final boolean focusCancel) {
        dialog.setOnKeyListener(new android.content.DialogInterface.OnKeyListener() {
            @Override
            public boolean onKey(android.content.DialogInterface d, int key, KeyEvent event) {
                if (key == KeyEvent.KEYCODE_BUTTON_A)
                    return dialog.getWindow().superDispatchKeyEvent(new KeyEvent(event.getDownTime(),
                            event.getEventTime(), event.getAction(), KeyEvent.KEYCODE_DPAD_CENTER,
                            event.getRepeatCount()));
                if (key == KeyEvent.KEYCODE_BUTTON_B) {
                    if (event.getAction() == KeyEvent.ACTION_UP)
                        dialog.cancel();
                    return true;
                }
                return false;
            }
        });
        if (focusCancel)
            dialog.setOnShowListener(new android.content.DialogInterface.OnShowListener() {
                @Override
                public void onShow(android.content.DialogInterface d) {
                    Button cancel = dialog.getButton(android.app.AlertDialog.BUTTON_NEGATIVE);
                    if (cancel != null)
                        cancel.requestFocus();
                }
            });
        dialog.show();
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
        return choices(label, labels, values, selected, key, true);
    }

    /** A row with its label in the column of 170 dp (as choices), for buttons added after it. */
    private LinearLayout captionRow(String label) {
        LinearLayout r = buttonsRow();
        TextView caption = text(label, 16, true);
        caption.setPadding(dp(GAP), 0, dp(8), 0);
        caption.setMinHeight(dp(48 + 2 * GAP));
        caption.setGravity(Gravity.LEFT | Gravity.CENTER_VERTICAL);
        r.addView(caption, new LinearLayout.LayoutParams(dp(170), ViewGroup.LayoutParams.WRAP_CONTENT));
        return r;
    }

    /** lineUp false: the buttons share the whole row, however few (the colour calculation's two). */
    private Button[] choices(String label, String[] labels, int[] values, int selected, final String key,
            boolean lineUp) {
        LinearLayout r = captionRow(label);
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
        for (int i = labels.length; lineUp && i < 3; i++)
            r.addView(new View(getContext()), cell());
        lastChoices = buttons;
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
        // A (KEYCODE_BUTTON_B), L3, SELECT, the OUYA button (only its release arrives: KEYCODE_MENU), START: back;
        // the keyboard's F1 and Esc too (1.0.0, OuyaKeyboard)
        if (key == KeyEvent.KEYCODE_BUTTON_B || key == KeyEvent.KEYCODE_BACK || key == KeyEvent.KEYCODE_BUTTON_THUMBL
                || key == KeyEvent.KEYCODE_BUTTON_SELECT || key == KeyEvent.KEYCODE_BUTTON_START
                || key == KeyEvent.KEYCODE_MENU || key == OuyaKeyboard.KEY_F1 || key == OuyaKeyboard.KEY_ESCAPE) {
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
