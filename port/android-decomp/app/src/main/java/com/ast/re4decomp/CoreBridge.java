package com.ast.re4decomp;

/**
 * Core mode (armeabi-v7a): the original game core (libre4_main.so and its
 * libraries, port/whole) runs the game; native side core_mode.cpp.
 */
public final class CoreBridge {
  private CoreBridge() {}
  private static boolean loaded;
  /** Loads the app library; its JNI_OnLoad loads the core and reserves MEM1. */
  public static synchronized void load() {
    if (!loaded) {
      System.loadLibrary("re4decomp");
      loaded = true;
    }
  }
  /**
   * Starts the game once per process; "" when it runs, else the reason.
   * settings: "NAME=value;NAME=value" for the core's test switches (the same
   * environment variables as the Linux checks, e.g. RE4_START_ROOM=0x100,
   * RE4_PAD_SCRIPT=..., RE4_PAD_UNTIL_ROOM=1), set before the game starts.
   */
  public static native String start(String gameRoot, String settings);
  /**
   * GameCube controller, raw like the hardware (the game applies the dead
   * zones itself): SDK button bits, main stick about -100..100, C stick about
   * -90..90, triggers 0..255 (CoreInput).
   */
  public static native void pad(int buttons, int stickX, int stickY, int substickX,
                                int substickY, int triggerL, int triggerR);
  /**
   * The game's rumble motor: bits 0-3 = on in the last four game frames
   * (bit 0 the newest), bits 8-31 = game frames so far; 0 without the core.
   */
  public static native int rumble();
  /**
   * The game's sound: `frames` stereo frames (left, right) at 32 kHz into
   * `pcm` (silence while there is none); returns the frames from the game.
   */
  public static native int audio(short[] pcm, int frames);
  /**
   * The same sound as four channels per frame (front left, front right, rear left, rear right):
   * the game's own rear in its Dolby Pro Logic II mode, otherwise a silent rear. The same
   * position as audio(): read one of the two.
   */
  public static native int audio4(short[] pcm, int frames);
  /** The game plays in Dolby Pro Logic II (its AUDIO SETUP): the rear of audio4 is its own. */
  public static native boolean audioSurround();
  /** A line of the sound output into the core's log (core-log.txt), with the game frame. */
  public static native void audioNote(String line);
  /**
   * A vsync of the display (the Choreographer's frame time, System.nanoTime):
   * the game's video retraces follow the display (no drift against 60 Hz).
   */
  public static native void vsync(long frameTimeNanos);
  /**
   * The beat's grid (FramePacing.every): the picture drawn at every n-th vsync; on a screen that stays faster than
   * 60 Hz each picture then asks for its own vsync (0.99.13, core_mode.cpp present::beforeSwap).
   */
  public static native void setPictureEvery(int n);
  /** The room the game is in (stage << 8 | room), -1 outside a room. */
  public static native int wantedRoom();
  /** The decoded room archive for a room (RoomRepository), or the error. */
  public static native void roomPrepared(int room, String decodedPath, String error);
  /** The process's mappings below 4 GB when the core loaded (the report's address-map.txt). */
  public static native String addressMap();
  /**
   * The core in a process of its own (32 bit with MEM1 taken in the app's process, or
   * RE4_CORE_PROCESS=1): why and its state; "" when the core runs in the app's process.
   */
  public static native String coreProcess();
  /** Why the core process ended while the app runs (a crash); "" while it runs or without one. */
  public static native String coreProblem();
  /**
   * The first disc file the game could not read: "disc\tpath on the disc\twhy" (St2/r222.das), ""
   * while every read went well (runtime/dvd.cpp re4_core_disc_problem).
   */
  public static native String discProblem();
  /** Diagnostic text: core state, frame, room, models, last log lines. */
  public static native String status();
  /** Saves the graphics data of the next picture into `folder` (diagnosis; R3). */
  public static native void gxDump(String folder);
  public static native void surfaceCreated();
  /**
   * The Vulkan surface's window (VulkanSurface, on its render thread; null when it went): the Vulkan renderer
   * presents to it (core started with RE4_GX_API=vulkan).
   */
  public static native void vkWindow(android.view.Surface surface);
  /** Why the Vulkan renderer could not start; "" while it runs or with OpenGL ES. CoreActivity then goes to GLES. */
  public static native String rendererProblem();
  /** The renderer's API, device and driver ("" before the first picture); the report's renderer.txt. */
  public static native String rendererInfo();
  public static native void resize(int width, int height);
  public static native void draw();

  // --- the app's menu (M11; cores since M11, hasMenu says whether this one has it) ---

  /** The core has the menu's functions (pause, memory card). */
  public static native boolean hasMenu();
  /** Instant slot: operation 0 status, 1 save, 2 load. Result and seven fixed-width metadata fields.
   * Called on a worker while the app menu keeps the core paused. */
  public static native long[] instant(int operation);
  /**
   * on: the game stops at its next frame and waits until pause(false);
   * waits up to waitMs for that. True when the game stands.
   */
  public static native boolean pause(boolean on, int waitMs);
  /** The game's debug menu at its next frame (SELECT held, patch 0061). */
  public static native void debugMenu();
  /** The game's depth of field (patch 0065) and, where the game asks for none while the player is free, the
   *  gameplay autofocus of the first Android port: 0 off, 1 light (as until 0.98.3), 2 strong (the autofocus
   *  blurs more and wider; wish of the project owner, 05.11.2026). */
  public static native void setDof(int level);
  /**
   * The menu's "Force Performance": true keeps the game and the graphics thread on the fast cores
   * with Android's performance hint, false (the default) leaves them to Android.
   */
  public static native void setFastCores(boolean on);
  /**
   * The menu's Potato mode: bit 0 leaves the weather effects out (fog banks, rain, water spray), bit 1
   * the special effects (steam, heat shimmer, water); 0 draws everything (the default).
   */
  public static native void setPotato(int mask);
  /**
   * The menu's AUDIO page: the music's and the sound effects' volume, 0 .. 100 percent each (100 as the
   * game). The movies keep their sound as it is.
   */
  public static native void setSoundVolume(int music, int effects);
  /**
   * The menu's "Invert vertical" and "Aim speed" for aiming the knife and the guns by touch or a controller:
   * `invert` the game's own "reverse" option (kept over the system file's), `speed` the stick's turn while
   * aiming (CoreActivity.AIM_SPEED_MIN .. AIM_SPEED_MAX, 1 as the game).
   */
  public static native void setAim(boolean invert, float speed, boolean autoAim, boolean alwaysRun);
  /** The game's own options changed "reverse" since the last call (patch 0188): -1, else 0 / 1 for "Invert vertical". */
  public static native int aimFromGame();
  /** The menu's "Vibration" not Off (1.1.0): the game's own vibration switch follows it (runtime/aim_options.cpp). */
  public static native void setGameVibration(boolean on);
  /** The game's own options changed "vibration" since the last call (1.1.0): -1, else 0 / 1. */
  public static native int vibrationFromGame();
  /** "Enemy health bars" (1.1.0, Cheats & Mods): a bar above each living enemy (runtime/health_bars.cpp). */
  public static native void setHealthBars(boolean on);
  /** The cheat "Stack items" (1.1.0, Cheats & Mods): ammo, grenades and recovery items up to `max` a slot (0 Standard,
   *  250, 500, 999; patch 0254). */
  public static native void setStackItems(int max);
  /** The cheat "Bloody mode" (1.1.3, Cheats & Mods): blood pools on the floor stay until the room changes (patch 0258,
   *  runtime/bloody.cpp). */
  public static native void setBloody(boolean on);
  /** "Rapid fire" (1.1.6, Controls): the guns of every playable character fire twice as fast (patch 0263,
   *  runtime/rapid_fire.cpp). */
  public static native void setRapidFire(boolean on);
  /** The cheats "All weapons at the merchant" (bit 0) and "All tune-ups at the merchant" (bit 1) (1.1.3, Cheats & Mods;
   *  patch 0259): the merchant offers them from the next shop visit, never saved with the game. */
  public static native void setMerchantAll(int bits);
  /** The mouse in the game's title and options menus (1.1.0): the app's own pointer (u, v 0..1 over the game's
   *  picture) at each move, `click` for a left click (runtime/mouse_menu.cpp). */
  public static native void mouseMenu(float u, float v, boolean click);
  /** "Reload without aiming" (1.1.0, keyboard): one reload through the game's own direct reload (patch 0241). */
  public static native void quickReload();
  /** Photo mode (1.1.0): op 1 on with the mask (1 HUD, 2 Leon, 4 the partner, 8 the enemies hidden), 0 off, 2 the
   *  mask alone; false with a core without it (runtime/photo_mode.cpp). */
  public static native boolean photoMode(int op, int mask);
  /** The free camera's input in photo mode: a = move x, move z, rise, turn (bytes from the low one, -127..127);
   *  b = tilt (byte), speed (byte: 0 normal, 1 slow, 2 fast), bit 16 back to the start, bits 17-20 the camera effect; c = turn and tilt by the
   *  mouse or a finger (s16 each, 1/8192 rad, low = turn). */
  public static native void photoInput(int a, int b, int c);
  /** 1: photo mode is on (the pictures say so); 2: a request was refused since the last call. */
  public static native int photoState();
  /** "Prepare shaders at start" (1.1.0): {state (0 not started, 1 running, 2 through), done, total} of the shaders made
   *  ahead (GLES programs or the Vulkan pipeline list). */
  public static native int[] shaderPrep();
  /** The Potato mode bits from the graphics settings ("weatherEffects", "specialEffects", both on by default; since
   *  0.99.3 "particleEffects" 0 normal / 1 less / 2 none in bits 2..3 and "shadowsLight", on by default, bit 4 when
   *  off; the core's runtime/potato.cpp). */
  static int potatoMask(android.content.SharedPreferences g) {
    return (g.getBoolean("weatherEffects", true) ? 0 : 1) | (g.getBoolean("specialEffects", true) ? 0 : 2)
        | (particleEffects(g) << 2) | (g.getBoolean("shadowsLight", true) ? 0 : 16);
  }
  /** The lens flares' step (0.99.6, wish of the project owner, 09.11.2026): 0 off (the default), 1 lens flares, 2 halos +
   *  lens flares; until 0.99.5 the switch "flares", whose "on" was the halos and the lens flares together. */
  static final String FLARE_MODE = "flareMode";
  static int flareMode(android.content.SharedPreferences g) {
    if (g.contains(FLARE_MODE)) return Math.max(0, Math.min(2, g.getInt(FLARE_MODE, 0)));
    return g.getBoolean("flares", false) ? 2 : 0;
  }
  /** The Potato mode's "View distance" (0.99.6, wish of the project owner, 09.11.2026, from the OUYA branch): 0 Normal
   *  (the default, the game's), 1 Medium, 2 Short; since 0.99.18 (the project owner, 15.11.2026) also 3 Very short
   *  (the OUYA's setting; the menu calls it "None", ingame_view_very_short) and 4 Far, so a value saved before stays
   *  what it was; in percent of the room's view
   *  VIEW_PERCENT (the room's fog end times it, runtime/view_distance.cpp; since 1.0.0, the project owner's choice
   *  after the analysis: Medium 65 instead of 75, Far 160 instead of 150 with the fog itself further out). The menu
   *  shows them in VIEW_ORDER, from the farthest to the nearest. */
  static final String VIEW_DISTANCE = "viewDistance";
  static final int VIEW_NORMAL = 0, VIEW_SHORT = 2, VIEW_VERY_SHORT = 3, VIEW_FAR = 4;
  static final int[] VIEW_PERCENT = {100, 65, 50, 35, 160};
  static final int[] VIEW_ORDER = {VIEW_FAR, VIEW_NORMAL, 1, VIEW_SHORT, VIEW_VERY_SHORT};
  static final int[] VIEW_LABEL = {R.string.ingame_view_normal, R.string.ingame_view_medium, R.string.ingame_view_short,
      R.string.ingame_view_very_short, R.string.ingame_view_far};
  static int viewDistance(android.content.SharedPreferences g) {
    return viewLevel(g.getInt(VIEW_DISTANCE, 0));
  }
  /** A saved or profile value as a level (anything unknown: Normal). */
  static int viewLevel(int v) {
    return v >= 0 && v < VIEW_PERCENT.length ? v : VIEW_NORMAL;
  }
  /** The level after `level` when the menu's button is pressed: Normal, Medium, Short, Very short, Far, Normal ...
   *  (VIEW_ORDER from the far end round again). */
  static int nextView(int level) {
    for (int i = 0; i < VIEW_ORDER.length; i++) {
      if (VIEW_ORDER[i] == level) return VIEW_ORDER[(i + 1) % VIEW_ORDER.length];
    }
    return VIEW_NORMAL;
  }
  /** The level the menu shows unlit (the project owner, 15.11.2026: "nur hier nicht gehighlighted"): Very short,
   *  shown as "None" since then (35 % with thick fog, as before); Far, Normal, Medium and Short are lit. */
  static boolean viewOff(int level) {
    return level == VIEW_VERY_SHORT;
  }
  /** The eyes after the menu button's text (the project owner, 15.11.2026): 4 Far, 3 Normal, 2 Medium, 1 Short, 0 None
   *  (a crossed-out eye instead). */
  static int viewEyes(int level) {
    return new int[] {3, 2, 1, 0, 4}[viewLevel(level)];
  }
  /** The Potato mode's "Particle effects" (0.99.3): 0 normal (the default), 1 less, 2 none. */
  static int particleEffects(android.content.SharedPreferences g) {
    return Math.max(0, Math.min(2, g.getInt("particleEffects", 0)));
  }
  /**
   * The AUDIO page's volumes, "musicVolume" / "effectsVolume" in the graphics settings (100 by default); 0 while
   * its button "musicOn" / "effectsOn" is off (both on by default).
   */
  static void sendSoundVolume(android.content.SharedPreferences g) {
    setSoundVolume(g.getBoolean("musicOn", true) ? g.getInt("musicVolume", 100) : 0,
        g.getBoolean("effectsOn", true) ? g.getInt("effectsVolume", 100) : 0);
  }
  /**
   * The menu's "Game language" with the European discs: 2 English, 3 German, 4 French, 5 Spanish,
   * 6 Italian. The core takes it and restarts the game (its own soft reset); the start setting
   * RE4_GAME_LANGUAGE holds the one at the start. 0: a restart with the language as it is (the cheat
   * menu's extra games). Bit 0x200: the player's language pack is on (bit 0x100) or off from the restart on
   * (port/docs/sprachpakete.md); without it the pack stays as it is.
   */
  public static native void restartGame(int language);

  /** The language pack in use for the app's own readers of disc files ("" none; the core has RE4_LANGUAGE_PACK). */
  public static native void setLanguagePack(String dir);
  /**
   * The cheat menu's switches (port/docs/cheat-menu-plan.md): 1 infinite health, 2 infinite ammo,
   * 4 one-hit kill, 8 automatic QTEs, 16 the extra games kept unlocked; the game sets them in every
   * frame while on. Bits 32..224: the story's player character (player type << 5: 2 Ada, 3 HUNK,
   * 4 Krauser, 5 Wesker; 0 Leon), taken over at once (the room starts again where the player stands;
   * patch 0195, runtime/cheats.cpp); bit 256: the special costume of Leon and Ashley, the same way.
   */
  public static native void setCheats(int bits);
  /**
   * A cheat menu button: 1 Killer7 with the highest levels, 2 attache case L, 3 +1,000,000 pesetas,
   * 4 unlock the extra games (Assignment Ada, The Mercenaries; at any time).
   * 5..9 Handcannon, Chicago Typewriter, the endless rocket launcher, Mine Thrower with scope, Matilda, each at
   * its highest levels (01.11.2026).
   * 1 done, 2 already so, 0 not now (1-3: not Leon, a sub screen, a movie), -1 no room in the case,
   * 3 done by the next game frame, -2 the core has no cheat menu.
   */
  public static native int cheatAction(int what);
  /**
   * 60 FPS (experimental): the renderer draws an in-between picture of two game pictures at each
   * second display refresh; the game itself stays at 30 pictures a second.
   */
  public static native void setSmooth(boolean on);
  /**
   * Merged draw calls (experimental): draws in a row that differ only in their matrices go to the
   * graphics driver as one call (gx_gl.cpp); for phones whose driver is slow per call. Without
   * effect while 60 FPS is on.
   */
  public static native void setMerge(boolean on);
  /** "Far camera": the shoulder camera further back while walking and standing (patch 0171). */
  public static native void setFarCamera(boolean on);
  /** "Field of view" (PLAYER VIEW): the shoulder camera's vertical field of view changed by `degrees`
   *  (-15 .. +15; 0 as the game; patch 0205), sent with the aim settings. */
  public static native void setFov(int degrees);
  /** The benchmark: the game goes to the lake's dock. `mode` 0: the renderer searches the levels for 30 and 60 FPS,
   *  the 30 pass from `top` (halves) after `warmup` seconds; 1 (0.99.10): the check of a profile with the app's
   *  options (benchmarkMeasure, finishBenchmark). */
  public static native void startBenchmark(int mode, int top, int warmup);
  public static native void cancelBenchmark();
  /** The check (mode 1) ends: back to where the game was. */
  public static native void finishBenchmark();
  /** {phase (0 idle, 1 going, 2 measuring, 3 back, 4 done, 5 cancelled, 6 back after a cancel, -1 refused, -2
   *  failed), run (0: the 30, 1: the 60 FPS pass), level in halves, result with 30, result with 60, pictures a second
   *  x 10, held bits, the processor sign, mode}. */
  public static native int[] benchmarkState();
  /** The check's seconds since the last call: {seconds, game pictures, pictures shown, weak seconds, levels summed}. */
  public static native int[] benchmarkMeasure();
  /** "Dynamic" up to these levels (halves; 0: none), with 30 and with 60 FPS: the benchmark's result. */
  public static native void setDynamicCaps(int cap30, int cap60);
  /** Original TV filter: the game's vertical copy filter on the picture. */
  public static native void setTvFilter(boolean on);
  public static native void setPsx(boolean on);
  /** HD textures (Cheats & Mods, TexturePacks): the player's pack file ("" off), "Sharpness: Half", the replacements'
   *  budget in MB. "" when the pack is open (or off), else why it cannot be used. */
  public static native String setTexturePack(String path, boolean half, int budgetMb);
  /** What the renderer did with the HD textures: {replaced, looked up, bytes of the replacements on the GPU now}. */
  public static native long[] texturePackUse();
  /** Lens flares at the game's lights (the menu, off by default; GxRenderer::SetFlares): 0 off, 1 lens flares (the
   *  ghosts only), 2 halos + lens flares (the glow and star at each light too; 0.99.6). */
  public static native void setFlares(int mode);
  /** The Potato mode's "View distance" (0.99.6, patch 0223): percent of the room's distance fog and far plane. */
  public static native void setViewDistance(int percent);
  /** The minimap: the current room's outline at the bottom left, turning with the player (off by default). */
  public static native void setMinimap(boolean on);
  /** The minimap's "Treasures and items" (1.1.6, Display): 0 off, 1 the room's treasures (gold), items (blue) and blue
   *  medallions, 2 also those hidden in closed chests, crates and enemies (rings; runtime/minimap.cpp). */
  public static native void setMinimapPickups(int mode);
  /** The laser sight's colour (CONTROLS, LASER SIGHT): 0 the game's red, 1 blue, 2 green, 3 yellow. */
  public static native void setLaserColour(int colour);
  /** Vertex ring (experimental, for Mali): a new renderer takes it. */
  public static native void setVertexRing(boolean on);
  /**
   * Frame skip: while the renderer is behind (the core has two pictures finished), the first is not
   * drawn and the second drawn at once, so the game keeps its pace on a slow graphics chip.
   */
  public static native void setFrameSkip(boolean on);
  /**
   * Aggressive frame skip (saves CPU; patch 0227): while the game's frames run late (longer than two fields) it leaves a picture out
   * itself (at most one in a row), so the processor's time for it is saved too; the logic keeps its pace. Not with
   * 60 FPS (the core is told "off" then).
   */
  public static native void setCpuSkip(boolean on);
  /**
   * Frame skip "Auto" (1.1.0): both frame skips (setFrameSkip's and setCpuSkip's) only while the game stays under 30
   * pictures a second for about half a second, off again after two seconds at full pace. Not with 60 FPS.
   */
  public static native void setFrameSkipAuto(boolean on);
  /**
   * "Even frame pacing" (1.1.0): on (the default) the steady rhythms (a picture held back one vsync at 30, each picture
   * asked for its vsync at 60 on screens faster than 60 Hz); off each picture at the next free vsync.
   */
  public static native void setEvenPacing(boolean on);
  /**
   * Save bandwidth (Vulkan; 0.99.8): the clear after an EFB copy goes with the next pass, which then loads nothing
   * there (less memory traffic on a tiling GPU such as Mali or Adreno); the pictures stay the same. OpenGL ES: nothing.
   */
  public static native void setVkBandwidth(boolean on);
  /** FXAA: an edge smoothing pass over every finished picture. */
  public static native void setFxaa(boolean on);
  /** Sharpen (0 off, 1 light, 2 strong): a contrast-adaptive sharpening of every picture after FXAA, not movies. */
  public static native void setSharpen(int level);
  /**
   * The upscaler (1.1.0; 0 off, 1 SGSR, 2 FSR): the picture enlarged by Qualcomm's SGSR or AMD's FSR 1 instead of the
   * plain stretch when it is smaller than its place on the screen (gx_upscale.h).
   */
  public static native void setUpscaler(int kind);
  /**
   * "Sharper menu text" (1.0.0, decision of the project owner, 16.11.2026): the game's own menu words are enlarged with
   * clean edges on a thread of their own and drawn sharper at high resolutions (word_sharpen.h); on by default.
   */
  public static native void setSharperText(boolean on);
  /**
   * The Mods page's "Costumes" and "Weapons" (1.0.0): false keeps the playable characters' (or the weapons') own
   * textures while a texture pack is on; takes effect at once.
   */
  public static native void setTextureGroups(boolean costumesPack, boolean weaponsPack);
  /**
   * CRT (0.99.9; 0 off, 1 light, 2 strong): the picture as a tube TV showed it (lines, a curved glass, a glow); only
   * what the game draws, not the touch controls nor the app's overlays.
   */
  public static native void setCrt(int level);
  /** Real-time shadows (RealtimeShadows; core_mode.cpp, GxRenderer::SetShadows): 0 off, 1 the figures, 2 the
   *  figures and the objects. */
  public static native void setShadows(int level);
  /** Bit 5 of the Potato mask while the real-time shadows are on, bit 6 with the objects (CoreActivity.checkShadows;
   *  always 0 in a release build): the core marks where they go, tags the objects that cast and leaves its foot
   *  shadow blobs out there (runtime/rt_shadow.cpp, runtime/gx_owner.cpp). */
  static volatile int shadowBit;
  /** The Potato mask the core gets: the menu's switches (potatoMask), and bits 5 and 6 of the real-time shadows. */
  static int potatoSent(android.content.SharedPreferences g) {
    return potatoMask(g) | shadowBit;
  }
  /** Bloom (0.99.13; core_mode.cpp, GxRenderer::SetBloom): 0 off, 1 light, 2 strong. */
  public static native void setBloom(int level);
  /** The bloom the renderer gets: the menu's, off while a Potato mode switch is on (0.99.13). */
  static int bloomSent(android.content.SharedPreferences g) {
    // (Far is no Potato setting: the bloom stays, as at Normal)
    int view = viewDistance(g);
    return potatoMask(g) == 0 && (view == VIEW_NORMAL || view == VIEW_FAR) ? CoreMenu.bloomLevel(g) : 0;
  }
  /** Colour (0.99.9): mode 0 off, 1 warm film, 2 survival horror; level 1 light, 2 strong. */
  public static native void setColour(int mode, int level);
  /**
   * Gyro aiming (GyroAim): the device's turn rates in rad/s about the screen's axes (yaw, pitch);
   * the core turns the aim by them while Leon aims a gun. 0, 0: still.
   */
  public static native void gyro(float yaw, float pitch);
  /**
   * Swipe aiming (the menu's "Touch Aiming: Touch Area"): the finger's movement on the touch area while
   * Leon aims, as angles in radians (yaw positive left, pitch positive up); the core turns the aim by
   * them at the next game frame.
   */
  public static native void aimTurn(float yaw, float pitch);
  /**
   * "Turn Leon with mouse / look area" (1.0): the mouse's or the look zone's movement sideways as Leon's turn in
   * radians (positive left); the core adds it to his facing at the next game frame while he stands, walks or runs, and
   * drops it while he aims, climbs, in an event and so on. Cores before 16.11.2026 ignore it.
   */
  public static native void bodyTurn(float yaw);

  /** "Move and aim" (1.1.5): the left stick while L or R is held (x right, y forward, -1 .. 1); it walks Leon while
   *  he aims (the core, runtime/move_aim.cpp). */
  public static native void moveAim(float x, float y);
  /**
   * The mouse wheel (CoreActivity, 0.99.12): whole notches, up positive. While the scope or the binoculars are up
   * the core zooms by them (an eighth of the zoom a notch, up zooms in, as the C stick does); elsewhere it drops
   * them. Cores before 12.11.2026 ignore it.
   */
  public static native void zoom(int notches);
  /** The cheat "Random enemies" (CoreMenu, 0.99.12): its seed (20 bits), 0 off; from the next room on. */
  public static native void setEnemyRandom(int seed);
  /** The cheat "More enemies" (1.1.0): about 40 % more of a room's ordinary Ganados at random places; its seed, 0 off. */
  public static native void setMoreEnemies(int seed);
  /** "Gentler QTEs" (CoreMenu, comfort controls, 0.99.15): Professional's quick-time events as Normal's
   *  (runtime/qte_touch.cpp re4_qte_level, patch 0232); cores before 0.99.15 leave it out. */
  public static native void setGentleQte(boolean on);
  /** The cheat "Costumes · Leon's jacket" (1.0, runtime/cheats.cpp, patch 0235): Leon keeps the brown jacket of chapter
   *  1 for the whole story; the story's Leon changes at once (his room starts again where he stands). */
  public static native void setLeonJacket(boolean on);
  /**
   * A tap on the picture at (x, y) pixels of the surface (CoreActivity): the core uses it in the
   * game's sub screens (runtime/menu_touch.cpp: tabs, the attache case, its command menu); cores
   * before 22.10.2026 ignore it.
   */
  public static native void tap(float x, float y);
  /** A finger touching down beside the touch controls (0.99.15): a quick-time prompt's press at once (the tap at the
   *  finger's lift then only goes to the sub screens). */
  public static native void touchDown();
  /** The finger beside the touch controls as it moves (1.1.2: dragging an item of the attache case, runtime/menu_touch.cpp):
   *  phase 0 down, 1 moved, 2 lifted, 3 cancelled, at (x, y) pixels of the surface; older cores ignore it. */
  public static native void drag(int phase, float x, float y);
  /** Android's thermal status (-1 unknown) and headroom (NaN unknown) for the lines of Dynamic (ThermalWatch, 0.99.15). */
  public static native void setThermal(int status, float headroom);
  /** A swipe across the picture: 1 to the left (the next tab of the game's sub screen), -1 to the right. */
  public static native void swipe(int dir);
  /**
   * A swipe while playing (CoreActivity, the menu's switch "Knife by Swipe"): the core draws the
   * knife and slashes (runtime/knife_swipe.cpp), `tilt` 1 straight up .. -1 straight down moves
   * the slash up or down; ignored in the game's sub screens and by cores before 22.10.2026.
   */
  public static native void slash(float tilt);
  /**
   * The memory card's files, one tab-separated row each (index, name,
   * blocks, save, intact, room, chapter, times saved, play time, year,
   * month, day, hour, minute, comment 1, comment 2); null when the game
   * uses the card or there is none (cardNote: "busy" / "nocard"). Only
   * while the game stands (pause).
   */
  public static native String[] cardList();
  /** Free blocks of the card, negative when it cannot be read now. */
  public static native int cardFreeBlocks();
  /** The name Dolphin gives the file's .gci ("08-G4BE-bh4_data00.gci"). */
  public static native String cardFileName(int index);
  /** The .gci of a file (GameCube byte order), null on an error (cardNote). */
  public static native byte[] cardExport(int index);
  /** A .gci onto the card: CARD_OK or an error (cardNote). */
  public static native int cardImport(byte[] gci, boolean replace);
  /** What the last card function did or why not. */
  public static native String cardNote();
  /** The keys of the shader programs this device made, into one file (the error report); how many. */
  public static native int writeProgramKeys(String path);
  /** Results of cardImport (runtime/re4_card_files.h). */
  public static final int CARD_OK = 0, CARD_NOCARD = -1, CARD_EXISTS = -2, CARD_NOSPACE = -3, CARD_BAD = -4,
      CARD_BUSY = -5;
  /**
   * The picture options: scale 0 (automatic) or 1-4 (EFB resolution
   * multiple), samples 1/2/4 (multisampling), anisotropy 1-16, aspect 0
   * (16:9), 1 (4:3), 2 (stretched over the whole screen), 3 (4:3 without
   * the game's letterbox bars while playing) or 5 (Native: the screen's own
   * aspect, core_mode.cpp re4mm::Native); applied at the
   * next picture while the game runs.
   */
  /** scale in halves: 0 Dynamic, 2 = 1×, 3 = 1.5× … 8 = 4× (CoreMenu.scaleHalves). */
  public static native void setGraphics(int scale, int samples, int anisotropy, int aspect);
  /** {most samples, strongest anisotropy, scale, samples, anisotropy in use}; null before the first picture. */
  public static native int[] graphicsInfo();
  /** Where a crash writes where it happened (the error log's folder). */
  public static native void setCrashFile(String path);
  /** The core's own log file (its lines without logcat; the error log's folder), "" stops it. */
  public static native void setLogFile(String path);
  /** Game frames so far (one per controller read), -1 without the counter. */
  public static native long gameFrames();
  /** Pictures put on the screen so far: each game picture once, with "60 FPS" also the in-between ones. */
  public static native long pictures();
  /** The game stands without a pause for `seconds`: where its thread is, into the error log. */
  public static native void reportHang(int seconds);
}
