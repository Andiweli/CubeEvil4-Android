// The OUYA app's native side (stage O3 of port/docs/ouya-machbarkeit.md; decisions of the project
// owner, 22.10.2026: an app of its own, Android 4.1, ARM32, the game core always in a process of its
// own). OuyaActivity gives the surface, the display's frames and the controller; this side starts the
// core process (runtime/core_ipc_client.cpp, libre4_coreproc.so in the app's library folder), draws its
// GX chunks with the GLES 2 renderer (gx_gl2.cpp) in the light version's settings and plays its sound
// through OpenSL ES.
//
// The light version (port/docs/ouya-machbarkeit.md, "Leichte OUYA-Fassung"): the core's part as the
// environment the core process inherits (kCoreSettings), the renderer's as options. A file
// <data>/settings.txt with lines NAME=VALUE (RE4_* only) sets more or overrides them (tests).
//
// A renderer slower than the game does not slow the game down: a thread of its own takes each chunk the
// core finishes at once (it stays in its slot of the shared memory, 8 of them, until run; with 3 the
// core process waited for the renderer, 22 ms a picture in r100, 07.11.2026), and of the chunks taken
// since the last frame of the display the older ones run as state only (GxRenderer::Skip), the newest
// is drawn.
#include "gx_gl2.h"
#include "re4_core.h"
#include "re4_core_client.h"
#include "re4_gx_frame.h"

#include <GLES2/gl2.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/log.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "RE4OUYA", __VA_ARGS__)

namespace {

// (the Potato switches and the view distance come from the menu's settings, nativeSettings)
const char *const kCoreSettings[][2] = {
    // the GX decoder on a thread of its own (runtime/gx_thread.cpp): r100 took the game's thread 42 ms a
    // frame, 41 % of it the decoder (07.11.2026)
    {"RE4_GX_THREAD", "1"},
    {"RE4_BRIDGE_MODELS", "0"},           // no models in the state snapshot (this app reads none)
    {"RE4_GX_COMPACT", "1"},              // the compact vertex, read by the renderer as it is
    {"RE4_GX_TEXTURE_CHECK", "sampled"},  // textures checked by samples from frame to frame
    // the textures the renderer keeps decoded: a movie brings 1.4 MB a picture, the main app's 256 MB
    // filled the OUYA's memory (its film stuttered, 07.11.2026)
    {"RE4_GX_TEXTURE_BUDGET_MB", "64"},
    {"RE4_DEBUG_HUD", "0"},               // none of the debug build's displays
    {"RE4_ROOM_MENU", "0"},               // "New Game" without the debug build's room menu
};

std::mutex gLock;
std::string gProblem;  // why the game does not run ("" while it does or before the start)
bool gStarted = false;
std::atomic<bool> gRunning{false};
std::unique_ptr<re4gx2::GxRenderer> gRenderer;
int gWidth = 0, gHeight = 0;
unsigned gFrames = 0, gChunks = 0, gSkipped = 0, gWaited = 0;
long long gRunMicros = 0, gRunMax = 0;  // the renderer's time for the chunks of the 10 s, the longest frame

// Chunks without draws (re4ipc_set_drawless, RE4_GX_CHUNK_DRAWLESS; 07.11.2026: the core converted every
// picture of r100, the renderer showed about one in three): the renderer tells the core how many it can
// show (from its time for a shown chunk), the core leaves the draws of the others out. Off for a while
// after a key that may open a screen of the game (its picture behind is a copy the game keeps) and when
// a later chunk read a copy a chunk without draws should have made (the check: CopiesReadLater).
std::atomic<long long> gDrawlessHoldUntil{0};  // ms (NowMs): keys of the controller (nativePad)
long long gViolationHoldUntil = 0;             // GL thread
int gDrawlessSent = 0, gDrawlessWant = 0;      // what the core was told, what the renderer's times ask
long long gShowMicros = 0;                     // the time of the shown chunks since the last choice
unsigned gShowCount = 0, gDrawlessRun = 0, gViolations = 0, gDrawlessBehind = 0;
long long gChoiceAt = 0;
re4gx2::GxStats gStatsBefore;           // the renderer's counters at the last log line

// The menu's settings (OuyaMenu.Settings, nativeSettings): the renderer's (a new renderer when they
// change, on the GL thread) and the core's (the environment before its start, then messages)
std::atomic<int> gWantEighths{6};
std::atomic<bool> gWantFastTev{true};
std::atomic<bool> gFrameSkip{true};  // older pictures left out while the renderer lags (off: each drawn)
int gViewPercent = 50, gPotato = 3, gAim = 100 << 8;  // under gLock
bool gWaitReset = false;  // GL thread: a new renderer draws from the next chunk that starts over

// The chunks the fetch thread took and the renderer has not run yet: in the core process's shared
// memory (8 slots, re4_core_ipc.h; until 07.11.2026 copied out, 2.5 MB a picture in r100), given
// back once run
const size_t kQueue = 6;  // chunks taken but not run yet; more: the fetch waits (the game with it)
std::mutex gQueueLock;
std::condition_variable gQueueCv;
std::deque<const Re4GxChunk *> gChunkQueue;
bool gDetach = false;  // the renderer went (a new EGL context): start over

// The fetch thread: every chunk the core finishes, at once
void Fetch() {
  for (;;) {
    {
      std::unique_lock<std::mutex> l(gQueueLock);
      if (gDetach) {
        for (const Re4GxChunk *c : gChunkQueue)
          re4ipc_gx_release(c);
        gChunkQueue.clear();
        re4ipc_gx_detach();  // the next chunk starts over (a full state for the new renderer)
        gDetach = false;
      }
      // without frame skip only two ahead: the game waits for the renderer instead of running ahead
      if (gChunkQueue.size() >= (gFrameSkip ? kQueue : 2)) {
        gWaited++;
        gQueueCv.wait_for(l, std::chrono::milliseconds(20));
        continue;
      }
    }
    const Re4GxChunk *c = re4ipc_gx_acquire();
    if (!c) {
      usleep(2000);
      continue;
    }
    std::lock_guard<std::mutex> l(gQueueLock);
    gChunkQueue.push_back(c);
  }
}
long long gLogAt = 0;

long long NowMs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<long long>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

long long NowMicros() {
  timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}

std::string Text(JNIEnv *env, jstring s) {
  const char *c = env->GetStringUTFChars(s, nullptr);
  std::string out = c ? c : "";
  env->ReleaseStringUTFChars(s, c);
  return out;
}

// <data>/settings.txt: NAME=VALUE per line, RE4_* only
void ReadSettings(const std::string &data) {
  FILE *f = fopen((data + "/settings.txt").c_str(), "r");
  if (!f)
    return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = 0;
    char *eq = strchr(line, '=');
    if (!eq || strncmp(line, "RE4_", 4) != 0)
      continue;
    *eq = 0;
    setenv(line, eq + 1, 1);
    LOG("setting %s=%s", line, eq + 1);
  }
  fclose(f);
}

// --- sound: OpenSL ES, 32 kHz stereo as the core gives it --------------------------------------
const int kSoundFrames = 1024;  // 32 ms
const int kSoundBuffers = 3;
int16_t gSound[kSoundBuffers][kSoundFrames * 2];
int gSoundNext = 0;
SLObjectItf gEngineObject = nullptr, gMixObject = nullptr, gPlayerObject = nullptr;
SLPlayItf gPlay = nullptr;
SLAndroidSimpleBufferQueueItf gQueue = nullptr;

void SoundCallback(SLAndroidSimpleBufferQueueItf queue, void *) {
  int16_t *buffer = gSound[gSoundNext];
  gSoundNext = (gSoundNext + 1) % kSoundBuffers;
  if (gRunning)
    re4ipc_audio(buffer, kSoundFrames);
  else
    memset(buffer, 0, sizeof(gSound[0]));
  (*queue)->Enqueue(queue, buffer, sizeof(gSound[0]));
}

bool StartSound() {
  SLEngineItf engine;
  if (slCreateEngine(&gEngineObject, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS ||
      (*gEngineObject)->Realize(gEngineObject, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
      (*gEngineObject)->GetInterface(gEngineObject, SL_IID_ENGINE, &engine) != SL_RESULT_SUCCESS)
    return false;
  if ((*engine)->CreateOutputMix(engine, &gMixObject, 0, nullptr, nullptr) != SL_RESULT_SUCCESS ||
      (*gMixObject)->Realize(gMixObject, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS)
    return false;
  SLDataLocator_AndroidSimpleBufferQueue queueLocator = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, kSoundBuffers};
  SLDataFormat_PCM format = {SL_DATAFORMAT_PCM,           2,
                             SL_SAMPLINGRATE_32,          SL_PCMSAMPLEFORMAT_FIXED_16,
                             SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
                             SL_BYTEORDER_LITTLEENDIAN};
  SLDataSource source = {&queueLocator, &format};
  SLDataLocator_OutputMix mixLocator = {SL_DATALOCATOR_OUTPUTMIX, gMixObject};
  SLDataSink sink = {&mixLocator, nullptr};
  const SLInterfaceID ids[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
  const SLboolean required[1] = {SL_BOOLEAN_TRUE};
  if ((*engine)->CreateAudioPlayer(engine, &gPlayerObject, &source, &sink, 1, ids, required) != SL_RESULT_SUCCESS ||
      (*gPlayerObject)->Realize(gPlayerObject, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
      (*gPlayerObject)->GetInterface(gPlayerObject, SL_IID_PLAY, &gPlay) != SL_RESULT_SUCCESS ||
      (*gPlayerObject)->GetInterface(gPlayerObject, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &gQueue) != SL_RESULT_SUCCESS)
    return false;
  (*gQueue)->RegisterCallback(gQueue, SoundCallback, nullptr);
  for (int i = 0; i < kSoundBuffers - 1; i++)
    SoundCallback(gQueue, nullptr);
  return (*gPlay)->SetPlayState(gPlay, SL_PLAYSTATE_PLAYING) == SL_RESULT_SUCCESS;
}

}  // namespace

// The game core in its process (once per app process; the core cannot be restarted): "" when it
// runs, else why not. Any thread but the GL thread (it waits for the core process's start).
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_cubeevil4_ouya_OuyaActivity_nativeStart(JNIEnv *env, jclass,
                                                                                          jstring dataDir,
                                                                                          jstring libraryDir) {
  std::lock_guard<std::mutex> l(gLock);
  if (gStarted)
    return env->NewStringUTF(gProblem.c_str());
  gStarted = true;
  const std::string data = Text(env, dataDir);
  for (const auto &s : kCoreSettings)
    setenv(s[0], s[1], 1);
  char value[32];
  snprintf(value, sizeof(value), "%.2f", gViewPercent / 100.0);
  setenv("RE4_VIEW_DISTANCE", value, 1);  // the distance fog and the far plane (patch 0900)
  snprintf(value, sizeof(value), "%d", gPotato);
  setenv("RE4_POTATO", value, 1);  // bit 0 weather, bit 1 special effects left out
  setenv("RE4_LOG_FILE", (data + "/core.log").c_str(), 1);
  ReadSettings(data);
  // the core process: a program among the app's libraries (extracted at installation; the folder from
  // Java, ApplicationInfo.nativeLibraryDir: Android 4.1's dladdr names a library without its folder)
  re4ipc::Spawn spawn;
  spawn.libraryDir = Text(env, libraryDir);
  spawn.program = spawn.libraryDir + "/libre4_coreproc.so";
  spawn.name = "com.ast.cubeevil4.ouya:core";
  spawn.helloSeconds = 60;
  re4ipc::configure(spawn);
  LOG("core process %s, data %s", spawn.program.c_str(), data.c_str());
  if (!re4ipc_reserve(data.c_str(), "") || !re4ipc_start()) {
    const char *m = re4ipc_message();
    gProblem = m && *m ? m : "the game core did not start";
  } else if (re4ipc_state() == RE4_CORE_FAILED) {
    gProblem = re4ipc_message();
  }
  if (!gProblem.empty()) {
    LOG("core: %s", gProblem.c_str());
    return env->NewStringUTF(gProblem.c_str());
  }
  gRunning = true;
  re4ipc_set_aim(gAim);  // auto aim, always run, far camera (no environment for them)
  std::thread(Fetch).detach();
  if (!StartSound())
    LOG("no sound (OpenSL ES)");
  return env->NewStringUTF("");
}

namespace {

int gHaveEighths = 0;
bool gHaveFastTev = false;

// The renderer with the menu's settings (GL thread)
void MakeRenderer() {
  re4gx2::GxOptions options;
  gHaveFastTev = gWantFastTev.load();
  gHaveEighths = gWantEighths.load();
  options.fastTev = gHaveFastTev;              // the TEV in fractions
  options.resolutionEighths = gHaveEighths;    // 6: 480 x 396
  options.maxLights = 2;                       // the two strongest lights per vertex
  gRenderer.reset(new re4gx2::GxRenderer);
  gRenderer->SetOptions(options);  // RE4_GX2_* in the environment (settings.txt) still override them
  std::string error;
  if (!gRenderer->Init(1, &error)) {
    LOG("renderer: %s", error.c_str());
    gRenderer.reset();
  } else {
    LOG("renderer: resolution %d/8, colour calculation %s", gHaveEighths, gHaveFastTev ? "simplified" : "exact");
  }
}

// The core sends everything again (a full state from the next chunk on); the renderer waits for it
void StartOver() {
  std::lock_guard<std::mutex> l(gQueueLock);
  gDetach = true;
  gWaitReset = true;
}

}  // namespace

// The menu's settings (OuyaMenu.Settings): before the start they go into the core's environment,
// afterwards to the core process at once; the renderer's are taken by the next display frame
extern "C" JNIEXPORT void JNICALL Java_com_ast_cubeevil4_ouya_OuyaActivity_nativeSettings(JNIEnv *, jclass, jint eighths,
                                                                                         jboolean fastTev,
                                                                                         jint viewPercent, jint potato,
                                                                                         jint aim, jboolean frameSkip) {
  gFrameSkip = frameSkip == JNI_TRUE;
  gWantEighths = eighths < 4 ? 4 : eighths > 8 ? 8 : static_cast<int>(eighths);
  gWantFastTev = fastTev == JNI_TRUE;
  std::lock_guard<std::mutex> l(gLock);
  const bool view = viewPercent != gViewPercent, pot = potato != gPotato, aimed = aim != gAim;
  gViewPercent = viewPercent < 20 ? 20 : viewPercent > 100 ? 100 : static_cast<int>(viewPercent);
  gPotato = potato & 3;
  gAim = aim;
  if (!gRunning)
    return;
  if (view)
    re4ipc_set_view_distance(gViewPercent);
  if (pot)
    re4ipc_set_potato(gPotato);
  if (aimed)
    re4ipc_set_aim(gAim);
  LOG("settings: view %d %%, potato %d, aim 0x%x, frame skip %s", gViewPercent, gPotato, gAim,
      gFrameSkip ? "on" : "off");
}

// A new EGL context: a renderer of its own (the chunks begin anew with a full state)
extern "C" JNIEXPORT void JNICALL Java_com_ast_cubeevil4_ouya_OuyaActivity_nativeSurfaceCreated(JNIEnv *, jclass) {
  if (gRenderer) {
    // the GL objects went with the old context; the core sends everything again after a detach
    (void) gRenderer.release();
    StartOver();
  }
  MakeRenderer();
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_cubeevil4_ouya_OuyaActivity_nativeSurfaceChanged(JNIEnv *, jclass,
                                                                                                jint width, jint height) {
  gWidth = width;
  gHeight = height;
}

// One frame of the display (GLSurfaceView's render thread)
extern "C" JNIEXPORT void JNICALL Java_com_ast_cubeevil4_ouya_OuyaActivity_nativeFrame(JNIEnv *, jclass) {
  if (gRenderer && (gWantEighths != gHaveEighths || gWantFastTev != gHaveFastTev)) {
    // the menu changed the resolution or the colour calculation: a new renderer, the state anew
    gRenderer.reset();
    MakeRenderer();
    StartOver();
  }
  if (gRenderer && gRunning) {
    // the chunks taken since the last frame; none yet: wait a little (the game makes about 30 a second)
    std::deque<const Re4GxChunk *> got;
    {
      std::unique_lock<std::mutex> l(gQueueLock);
      if (gChunkQueue.empty())
        gQueueCv.wait_for(l, std::chrono::milliseconds(30));
      got.swap(gChunkQueue);
    }
    gQueueCv.notify_all();  // room in the queue again
    const long long runStart = NowMicros();
    // the newest chunk with its draws is shown; the core's chunks without draws only run as state
    int show = -1;
    for (int i = static_cast<int>(got.size()) - 1; i >= 0 && show < 0; i--)
      if (!(got[i]->flags & RE4_GX_CHUNK_DRAWLESS))
        show = i;
    if (show < 0 && !got.empty())
      gDrawlessBehind++;  // none to show this frame: the picture before stays
    for (size_t i = 0; i < got.size(); i++) {
      if (gWaitReset) {
        // chunks the core built before it started over need the old renderer's state
        if (!(got[i]->flags & RE4_GX_CHUNK_RESET))
          continue;
        gWaitReset = false;
      }
      if (got[i]->flags & RE4_GX_CHUNK_DRAWLESS) {
        // the check: a later chunk reads a copy this one should have made
        if (i + 1 < got.size() && gRenderer->CopiesReadLater(*got[i], *got[i + 1])) {
          gViolations++;
          gViolationHoldUntil = NowMs() + 3000;
        }
        gRenderer->Skip(*got[i]);
        gDrawlessRun++;
      } else if (gFrameSkip && static_cast<int>(i) < show && gRenderer->CanSkip(*got[i], *got[i + 1])) {
        gRenderer->Skip(*got[i]);
        gSkipped++;
      } else {
        const long long t0 = NowMicros();
        gRenderer->Run(*got[i]);
        if (static_cast<int>(i) == show) {
          gShowMicros += NowMicros() - t0;
          gShowCount++;
        }
      }
      gChunks++;
    }
    const long long ran = NowMicros() - runStart;
    gRunMicros += ran;
    gRunMax = ran > gRunMax ? ran : gRunMax;
    for (const Re4GxChunk *c : got)
      re4ipc_gx_release(c);  // the slot back to the core process
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glViewport(0, 0, gWidth, gHeight);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  if (gRenderer) {
    // full screen as the main app's default (core_mode.cpp): the game letterboxes everything, its
    // rows 56..391 of 448 (on a 4:3 television exactly 16:9) fill a 16:9 box
    unsigned xw = 0, xh = 0;
    gRenderer->Xfb(&xw, &xh);
    const int eighths = gRenderer->ResolutionEighths();
    xh = eighths ? xh * 8 / static_cast<unsigned>(eighths) : xh;  // the GameCube's rows
    int w = gWidth, h = gWidth * 9 / 16;
    if (h > gHeight) {
      h = gHeight;
      w = gHeight * 16 / 9;
    }
    gRenderer->Present((gWidth - w) / 2, (gHeight - h) / 2, w, h, xh * 56 / 448, xh * 336 / 448);
  }
  gFrames++;
  const long long now = NowMs();
  if (gRunning && now - gChoiceAt >= 1000) {
    // the renderer's time for a shown chunk against the game's 33 ms a picture: one in so many with
    // its draws (1: all), changed only when two seconds in a row ask for the same
    const double showMs = gShowCount ? gShowMicros / 1000.0 / gShowCount : 0.0;
    int want = showMs > 0.0 ? static_cast<int>(showMs / 33.3 + 0.7) : 1;
    want = want < 1 ? 1 : want > 4 ? 4 : want;
    static int asked = 1;
    if (want == asked)
      gDrawlessWant = want;
    asked = want;
    gShowMicros = 0;
    gShowCount = 0;
    gChoiceAt = now;
  }
  {
    const bool hold = !gFrameSkip || now < gDrawlessHoldUntil.load() || now < gViolationHoldUntil;
    const int every = hold || gDrawlessWant < 2 ? 0 : gDrawlessWant;
    if (gRunning && every != gDrawlessSent) {
      re4ipc_set_drawless(every);
      gDrawlessSent = every;
    }
  }
  if (now - gLogAt >= 10000) {
    if (gLogAt) {
      LOG("10 s: %u display frames, %u game pictures (%u left out, %u without draws from the core: one in %d with "
          "them now), the game waited %u times for the renderer; %u frames with none to show, %u copy checks failed",
          gFrames, gChunks, gSkipped, gDrawlessRun, gDrawlessSent ? gDrawlessSent : 1, gWaited, gDrawlessBehind,
          gViolations);
      if (gRenderer) {
        // where the renderer's time went: the chunks (per display frame, the longest), shaders built,
        // textures decoded (a counter that went down: a new renderer)
        const re4gx2::GxStats &st = gRenderer->Stats();
        auto since = [](unsigned now, unsigned before) { return now >= before ? now - before : now; };
        LOG("renderer: %.1f ms of chunks per display frame (at most %.0f ms), %u shaders built, %u textures "
            "decoded, %u draws",
            gFrames ? gRunMicros / 1000.0 / gFrames : 0.0, gRunMax / 1000.0, since(st.programs, gStatsBefore.programs),
            since(st.textures, gStatsBefore.textures), since(st.draws, gStatsBefore.draws));
        LOG("textures: %.0f ms decoding, %.0f ms into the GPU; %u of skipped pictures kept as they came "
            "(%u of them drawn later)",
            since(st.decodeMicros, gStatsBefore.decodeMicros) / 1000.0,
            since(st.uploadMicros, gStatsBefore.uploadMicros) / 1000.0,
            since(st.texturesDeferred, gStatsBefore.texturesDeferred),
            since(st.deferredUploaded, gStatsBefore.deferredUploaded));
        gStatsBefore = st;
      }
    }
    gFrames = gChunks = gSkipped = gWaited = 0;
    gDrawlessRun = gDrawlessBehind = gViolations = 0;
    gRunMicros = gRunMax = 0;
    gLogAt = now;
  }
}

// The controller as a GameCube pad (OuyaActivity maps the keys and axes)
extern "C" JNIEXPORT void JNICALL Java_com_ast_cubeevil4_ouya_OuyaActivity_nativePad(JNIEnv *, jclass, jint buttons,
                                                                                     jint stickX, jint stickY, jint substickX,
                                                                                     jint substickY, jint triggerL,
                                                                                     jint triggerR) {
  if (!gRunning)
    return;
  auto clampS = [](jint v) { return static_cast<int8_t>(v < -127 ? -127 : v > 127 ? 127 : v); };
  auto clampU = [](jint v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); };
  // START, Y, X, Z, and A without aiming (R): may open a screen of the game (inventory, map, the
  // merchant, the typewriter): every chunk with its draws for a while (its picture behind is a copy)
  static int last = 0;
  const int pressed = buttons & ~last;
  last = buttons;
  if ((pressed & (0x1000 | 0x0800 | 0x0400 | 0x0010)) || ((pressed & 0x0100) && !(buttons & 0x0020)))
    gDrawlessHoldUntil = NowMs() + 800;
  Re4CorePad pad{static_cast<uint16_t>(buttons), clampS(stickX),   clampS(stickY), clampS(substickX),
                 clampS(substickY),              clampU(triggerL), clampU(triggerR)};
  re4ipc_pad(&pad);
}

// The app goes to the background (or comes back): the game and its sound stop (go on)
extern "C" JNIEXPORT void JNICALL Java_com_ast_cubeevil4_ouya_OuyaActivity_nativePause(JNIEnv *, jclass, jboolean on) {
  if (!gRunning)
    return;
  re4ipc_pause(on ? 1 : 0, 500);
  if (gPlay)
    (*gPlay)->SetPlayState(gPlay, on ? SL_PLAYSTATE_PAUSED : SL_PLAYSTATE_PLAYING);
}
