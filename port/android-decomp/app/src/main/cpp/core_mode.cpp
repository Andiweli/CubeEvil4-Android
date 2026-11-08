// Core mode (armeabi-v7a): JNI of com.ast.re4decomp.CoreBridge. Loads the
// game core (libre4_main.so with libre4_platform.so and the module libraries,
// built by port/tools/whole_build_android.sh), reserves the GameCube main
// memory at 0x80000000 as early as the process allows (JNI_OnLoad, before any
// GL work), starts the game in its own thread, passes the controller and draws
// what the game draws: the GX chunks of the core's emulated graphics processor
// with the GX renderer (gx_gl.cpp, port/docs/gx-plan.md step G4), and until
// the first display copy (or with the setting RE4_VIEW=bridge) the published
// state frames with CoreScene.
//
// On 32 bit with MEM1 taken in this process (ART's Java heap at 0x80000000 on
// Android 14 devices) the core runs in a process of its own instead
// (libre4_coreproc.so, port/docs/32bit-kernprozess.md): the table of core
// calls below then goes to it (runtime/re4_core_client.h), the rest of this
// file stays the same. RE4_CORE_PROCESS=1 asks for that on any 32-bit device
// (tests).
#include "core_scene.h"
#include "gx_auto_level.h"
#include "gx_bench_steps.h"
#include "gx_gl.h"
#include "gx_selfcheck.h"
#include "minimap_view.h"
#include "re4_card_files.h"
#include "re4_core_client.h"
#include "re4_cpu_info.h"
#include "re4_gx_frame.h"
#include "re4_gx_frame_io.h"

#include <EGL/egl.h>
#include <android/log.h>
#ifdef __ANDROID__
#include <android/native_window_jni.h>
#endif
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <ucontext.h>
#include <unistd.h>
#include <jni.h>
#if defined(__aarch64__)
#include <android/dlext.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

#define LOG(...) re4gx::LogTo("RE4Core", __VA_ARGS__)

struct CoreApi {
  void *main{};
  std::string error;
  int (*mem1Init)(void){};
  int (*reserve)(const char *, const char *){};
  int (*start)(void){};
  int (*state)(void){};
  const char *(*message)(void){};
  void (*pad)(const Re4CorePad *){};
  unsigned (*rumble)(void){};
  int (*audio)(int16_t *, int){};
  void (*audioStats)(unsigned *){};
  // optional (cores since 20.10.2026): the four channels (Dolby Pro Logic II) and whether the game uses them
  int (*audio4)(int16_t *, int){};
  int (*audioSurround)(void){};
  // optional: a line of the app into the core's log file (the error log's core-log.txt, also without logcat)
  void (*logFileLine)(const char *){};
  const Re4CoreSnapshot *(*acquire)(void){};
  void (*release)(const Re4CoreSnapshot *){};
  int (*logTail)(char *, int){};
  const Re4GxChunk *(*gxAcquire)(void){};
  void (*gxRelease)(const Re4GxChunk *){};
  void (*gxDetach)(void){};
  void (*vsync)(int64_t){};  // optional (cores since M9)
  // optional (cores since M11): the app's menu
  int (*pause)(int, int){};
  void (*debugMenu)(void){};
  void (*setDof)(int){};
  void (*gyro)(float, float){};  // optional (cores since 19.10.2026): gyro aiming
  void (*depth)(const unsigned *, unsigned){};  // optional (cores since 26.10.2026): the depth for the game's GXPeekZ
  void (*setLetterbox)(int){};  // optional (cores since 22.10.2026): 4:3 without the bars
  void (*setFastCores)(int){};  // optional (cores since 24.10.2026): Force Performance
  void (*setPotato)(int){};     // optional (cores since 30.10.2026): Potato mode (bit 0 weather, bit 1 special effects off)
  void (*setSoundVolume)(int){};  // optional (cores since 30.10.2026): music | effects << 8, percent
  void (*setAim)(int){};        // optional (cores since 30.10.2026): bit 0 invert vertical, bit 1 auto aim, bit 2 always run, bits 8.. aim speed in percent
  int (*aimFromGame)(void){};   // optional (cores since 03.11.2026, patch 0188): the game's own "reverse" changed
  void (*restartGame)(int){};   // optional (cores since 27.10.2026): new game language, restart (European discs)
  const char *(*discProblem)(void){};  // optional (cores since 27.10.2026): the first unreadable disc file
  void (*setCheats)(int){};      // optional (cores since 27.10.2026): the cheat menu's switches
  int (*cheatAction)(int){};     // optional (cores since 27.10.2026): a cheat menu button
  int (*roomOutline)(float *, int, unsigned *){};  // optional (cores since 04.11.2026): the minimap's room outline
  void (*setLaserColour)(int){};  // optional (cores since 04.11.2026, patch 0198): the laser sight's colour
  void (*setViewAspect)(int){};   // optional (cores since 07.11.2026, patch 0214): "Native", the 3D's aspect x 10000
  void (*setHudLayout)(int, int, int){};  // optional (cores since 07.11.2026, patch 0215): the life gauge's corner
  void (*touch)(float, float){};  // optional (cores since 22.10.2026): a tap on the picture
  void (*aimTurn)(float, float){};  // optional (cores since 29.10.2026): swipe aiming
  void (*swipe)(int){};           // optional (cores since 22.10.2026): a swipe across it
  void (*slash)(float){};         // optional (cores since 22.10.2026): the knife by a swipe
  int (*instant)(int, Re4InstantInfo *){};
  int (*cardList)(Re4CardFile *, int){};
  int (*cardFree)(void){};
  int (*cardName)(int, char *, int){};
  int (*cardExport)(int, unsigned char *, unsigned, char *, int){};
  int (*cardImport)(const unsigned char *, unsigned, int, char *, int){};
  unsigned (*frameCount)(void){};
  int (*gameThread)(void){};
  long (*freeAt)(void){};  // optional: the frame the player became free in the room (state_trace.cpp), -1 not yet
  void (*logFile)(const char *){};  // optional (cores since 21.10.2026): the core's lines into a file
} api;

// The core in a process of its own (useCoreProcess): why, for the report; "" in this process.
std::string coreProcessWhy;
void useCoreProcess(const std::string &why);

template <class F> bool bind(F &slot, const char *name) {
  slot = reinterpret_cast<F>(dlsym(api.main, name));
  if (!slot && api.error.empty()) api.error = std::string("missing in libre4_main.so: ") + name;
  return slot != nullptr;
}

// MEM1 (0x80000000..0x83000000) is held with an inaccessible placeholder
// before the core's libraries load (re4_mem1_init maps over it,
// RE4_MEM1_PLACEHOLDER): on 32 bit as well, since a tester's device (19.10.2026)
// found MEM1 taken when the core reserved it after loading its libraries.
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
constexpr unsigned long kMem1Start = 0x80000000ul, kMem1Size = 0x3000000ul;
// the top of the 32-bit address space (the window's end on 64 bit)
constexpr uint64_t kTop32 = 0x100000000ull;

void *reserveFixed(unsigned long at, unsigned long size) {
  void *want = reinterpret_cast<void *>(at);
  void *p = mmap(want, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == want) return p;
  if (p != MAP_FAILED) munmap(p, size);  // an old kernel took the address as a hint
  return nullptr;
}

// What lies below 4 GB when the core loads, for the error report (address-map.txt):
// MEM1 (and on 64 bit the window above it) has to be free; ART, the zygote and
// the drivers decide what is there before the app runs
std::string gAddressMap;
std::string gMem1Owner;  // the first mapping in MEM1 (the reason when it is taken)

// "start-end perms name" of a /proc/self/maps line
std::string shortMapping(const char *line) {
  char range[64] = "", perms[8] = "";
  int name = 0;
  if (sscanf(line, "%63s %7s %*s %*s %*s %n", range, perms, &name) < 2) return line;
  std::string text = std::string(range) + ' ' + perms;
  if (name > 0 && line[name]) text += std::string(" ") + (line + name);
  while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
  return text;
}

void recordLowMappings(const char *when) {
  const uint64_t mem1End = kMem1Start + kMem1Size;
  struct utsname u;
  if (uname(&u) != 0) std::memset(&u, 0, sizeof(u));
  char head[320];
  snprintf(head, sizeof(head), "--- %s (%d-bit process, kernel %s %s; mappings below 4 GB; MEM1 0x80000000..0x83000000%s)\n",
           when, (int)(sizeof(void *) * 8), u.machine, u.release, sizeof(void *) == 8 ? ", window up to 4 GB" : "");
  gAddressMap += head;
  FILE *f = fopen("/proc/self/maps", "re");
  if (!f) {
    gAddressMap += "(cannot read /proc/self/maps)\n";
    return;
  }
  char line[512];
  int lines = 0;
  uint64_t at = kMem1Start, largest = 0, top = 0;
  while (fgets(line, sizeof(line), f)) {
    unsigned long long a, b;
    if (sscanf(line, "%llx-%llx", &a, &b) != 2 || a >= kTop32) continue;
    std::string text = shortMapping(line);
    bool mem1 = a < mem1End && b > kMem1Start;
    if (mem1) {
      text += "   <- MEM1";
      if (gMem1Owner.empty()) gMem1Owner = shortMapping(line);
    } else if (sizeof(void *) == 8 && b > mem1End) {
      text += "   <- window";
    }
    // the vector pages lie in the kernel's part on a 32-bit kernel: no end of a free range
    bool vectors = text.find("[vectors]") != std::string::npos || text.find("[sigpage]") != std::string::npos;
    if (b > at && !vectors) {
      if (a > at && a - at > largest) largest = a - at;
      at = b;
    }
    if (!vectors && b > top) top = b;  // the highest mapping: about where the address space ends
    if (++lines <= 2000) gAddressMap += text + '\n';
    if (mem1 || (sizeof(void *) == 8 && b > kMem1Start)) LOG("below 4 GB: %s", text.c_str());
  }
  fclose(f);
  // above the highest mapping: free on 64 bit; on 32 bit the kernel's part may begin there
  if (sizeof(void *) == 8 && kTop32 > at && kTop32 - at > largest) largest = kTop32 - at;
  char sum[224];
  snprintf(sum, sizeof(sum), "%d mappings below 4 GB; highest ends at 0x%llx; largest free range from 0x80000000 on: %llu MB\n",
           lines, (unsigned long long)top, (unsigned long long)(largest >> 20));
  gAddressMap += sum;
  LOG("%s", sum);
}

#if defined(__aarch64__)
// 64 bit (milestone 12): the game keeps its addresses in 32 bits and tells
// its own from others by bit 31, so everything it reaches lies in the window
// 0x80000000..0xFFFFFFFF. The linker loads the two core libraries into
// ranges reserved at the top of the window. The core puts its module
// libraries and stacks there itself (module_loader.cpp, game_window.cpp).
constexpr unsigned long kWindowStart = 0x83000000ul;
constexpr unsigned long kWindowEnd = 0x100000000ul;

// from the top of the window down, in 16 MB steps (ART and the drivers may
// hold parts of it)
void *reserveInWindow(unsigned long size) {
  for (unsigned long at = kWindowEnd - size; at >= kWindowStart; at -= 16ul << 20) {
    if (void *p = reserveFixed(at, size)) return p;
  }
  return nullptr;
}

// The mapping that holds `addr` (start, end, name), false when none does.
bool mappingAt(unsigned long addr, unsigned long &start, unsigned long &end, std::string &name) {
  FILE *f = fopen("/proc/self/maps", "re");
  if (!f) return false;
  char line[512];
  bool found = false;
  while (!found && fgets(line, sizeof(line), f)) {
    unsigned long a, b;
    int at = 0;
    if (sscanf(line, "%lx-%lx %*s %*s %*s %*s %n", &a, &b, &at) < 2 || addr < a || addr >= b) continue;
    start = a;
    end = b;
    name = at > 0 ? line + at : "";
    while (!name.empty() && (name.back() == '\n' || name.back() == ' ')) name.pop_back();
    found = true;
  }
  fclose(f);
  return found;
}

// ART on arm64 keeps every Java object below 4 GB. Its area for large objects
// ("free list large object space", as large as dalvik.vm.heapsize) comes from
// the 64-bit zygote and lies in the first gap behind the boot image; on the
// Retroid (heapsize 512m) 0x73f8a000..0x93f8a000, over MEM1 (first 64-bit run,
// 17.10.2026). ART fills it from the bottom, objects there never move, and it
// never touches the data of a live byte[]. So the app takes MEM1 from it as the
// data of one array that it holds for the life of the process: arrays are put
// below it until the next one starts at the page before 0x80000000, that one
// reaches to 0x83000000, and the arrays below are let go (ART gets them back as
// free space). Needs the large heap (android:largeHeap): for a moment the
// arrays from the area's fill level up to 0x83000000 count as Java memory.
jobject gMem1Holder;

bool holdMem1InLargeObjectSpace(JNIEnv *env) {
  unsigned long a = 0, b = 0;
  std::string name;
  if (!env || !mappingAt(0x80000000ul, a, b, name) || name.find("large object space") == std::string::npos ||
      b < kWindowStart) {
    return false;
  }
  const unsigned long page = (unsigned long)sysconf(_SC_PAGESIZE);
  const unsigned long holderStart = 0x80000000ul - page;  // the array's own header in the page before MEM1
  // a byte[] taking `bytes` of the area (whole pages: its header is smaller than 64 bytes); its start
  auto take = [&](unsigned long bytes, unsigned long &at) -> jbyteArray {
    jbyteArray array = env->NewByteArray((jsize)(bytes - 64));
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      return nullptr;
    }
    if (!array) return nullptr;
    void *data = env->GetPrimitiveArrayCritical(array, nullptr);
    at = (unsigned long)data & ~(page - 1);
    if (data) env->ReleasePrimitiveArrayCritical(array, data, JNI_ABORT);
    return data ? array : nullptr;
  };
  std::vector<jbyteArray> below;
  jbyteArray holder = nullptr;
  unsigned long size = 32ul << 20, at = 0;  // first a probe: where the area is filled up to
  char note[256];
  const char *problem = nullptr;
  jbyteArray array = take(size, at);
  for (int step = 0; step < 8 && array && !holder && !problem; step++) {
    unsigned long end = at + size;
    if (at >= a && end <= b && at <= holderStart && end >= kWindowStart) {
      holder = array;
    } else if (at < a || at > holderStart || end > holderStart) {
      problem = "the large object space is filled beyond MEM1's start";
      below.push_back(array);
    } else {
      below.push_back(array);
      unsigned long gap = holderStart - end;
      // up to the holder's page, or for a small gap the holder at once from here
      size = gap >= (1ul << 20) ? gap : kWindowStart - end;
      array = take(size, at);
    }
  }
  if (!holder && !problem) problem = array ? "no array landed at MEM1" : "the Java heap refused the arrays";
  unsigned long filler = 0;
  for (jbyteArray x : below) {
    jsize n = env->GetArrayLength(x);
    filler += (unsigned long)n;
    env->DeleteLocalRef(x);
  }
  if (!holder) {
    snprintf(note, sizeof(note), "MEM1 in ART's large object space %lx-%lx: not possible (%s)\n", a, b, problem);
    gAddressMap += note;
    LOG("%s", note);
    return false;
  }
  gMem1Holder = env->NewGlobalRef(holder);
  env->DeleteLocalRef(holder);
  snprintf(note, sizeof(note),
           "MEM1 held in ART's large object space %lx-%lx: the data of a byte[] at 0x%lx..0x%lx (%lu MB below "
           "let go)\n",
           a, b, at, at + size, filler >> 20);
  gAddressMap += note;
  LOG("%s", note);
  // the placeholder over the array's data, which MEM1 then replaces (re4_mem1_init)
  void *want = reinterpret_cast<void *>(0x80000000ul);
  return mmap(want, kWindowStart - 0x80000000ul, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED,
              -1, 0) == want;
}

void *openInWindow(const char *name, unsigned long size) {
  void *range = reserveInWindow(size);
  if (!range) {
    LOG("no %lu MB below 4 GB for %s", size >> 20, name);
    return nullptr;
  }
  android_dlextinfo info;
  std::memset(&info, 0, sizeof(info));
  info.flags = ANDROID_DLEXT_RESERVED_ADDRESS;
  info.reserved_addr = range;
  info.reserved_size = size;
  void *handle = android_dlopen_ext(name, RTLD_NOW | RTLD_LOCAL, &info);
  if (!handle) {
    munmap(range, size);
  } else {
    LOG("%s at %p", name, range);
  }
  char note[192];
  snprintf(note, sizeof(note), "%s: %s at %p (%lu MB reserved)\n", name, handle ? "loaded" : "NOT loaded", range,
           size >> 20);
  gAddressMap += note;
  return handle;
}
#endif

void loadCore(JNIEnv *env) {
  // libre4_main.so brings libre4_platform.so and libc++_shared.so (DT_NEEDED);
  // the module libraries are loaded by name later (re4_module_load).
  // RTLD_LOCAL: the game's symbols must not serve libraries loaded later
#if defined(__aarch64__)
  recordLowMappings("when the core loads");
  if (reserveFixed(0x80000000ul, kWindowStart - 0x80000000ul)) {
    setenv("RE4_MEM1_PLACEHOLDER", "1", 1);
    gAddressMap += "MEM1 placeholder: reserved\n";
  } else if (holdMem1InLargeObjectSpace(env)) {
    setenv("RE4_MEM1_PLACEHOLDER", "1", 1);
    gAddressMap += "MEM1 placeholder: reserved inside the large object space\n";
  } else if (!gMem1Owner.empty()) {
    // ART's moving heap ("dalvik-main space (region space)", Xiaomi 22081212C, Android 15,
    // 31.10.2026): nothing of it can be held; the core runs in a process of its own as on 32 bit
    // (wish of the project owner, 31.10.2026)
    gAddressMap += "MEM1 placeholder: NOT reserved: the core runs in a process of its own\n";
    useCoreProcess("MEM1 is taken in the app's process (by " + gMem1Owner + ")");
    return;
  } else {
    gAddressMap += "MEM1 placeholder: NOT reserved\n";
  }
  // the platform first, each in a range of its own (its size on x86_64 -O0:
  // libre4_platform 8 MB, libre4_main 10 MB)
  if (openInWindow("libre4_platform.so", 32ul << 20)) {
    api.main = openInWindow("libre4_main.so", 64ul << 20);
  }
#else
  (void)env;
  recordLowMappings("when the core loads");
  // the core's libraries load above it (the game tells its own addresses by bit 31, re4_check_game_address)
  if (reserveFixed(kMem1Start, kMem1Size)) {
    setenv("RE4_MEM1_PLACEHOLDER", "1", 1);
    gAddressMap += "MEM1 placeholder: reserved\n";
  } else if (!gMem1Owner.empty()) {
    // something of this process lies there (ART's Java heap, "dalvik-main space", on Android 14
    // devices: it comes from the zygote): the core runs in a process of its own (decision of the
    // project owner, 20.10.2026: only then). Without a mapping there the system refuses the
    // address, in any process: then the message below says so.
    gAddressMap += "MEM1 placeholder: NOT reserved: the core runs in a process of its own\n";
    useCoreProcess("MEM1 is taken in the app's process (by " + gMem1Owner + ")");
    return;
  } else {
    gAddressMap += "MEM1 placeholder: NOT reserved\n";
  }
  api.main = dlopen("libre4_main.so", RTLD_NOW | RTLD_LOCAL);
#endif
  if (!api.main) {
    const char *e = dlerror();
    api.error = std::string("core libraries missing (port/tools/whole_build_android.sh): ") +
                (e ? e : "?");
    LOG("%s", api.error.c_str());
    return;
  }
  bool ok = bind(api.mem1Init, "re4_mem1_init");
  ok = bind(api.reserve, "re4_core_reserve") && ok;
  ok = bind(api.start, "re4_core_start") && ok;
  ok = bind(api.state, "re4_core_state") && ok;
  ok = bind(api.message, "re4_core_message") && ok;
  ok = bind(api.pad, "re4_core_pad") && ok;
  ok = bind(api.rumble, "re4_core_rumble") && ok;
  ok = bind(api.audio, "re4_core_audio") && ok;
  ok = bind(api.audioStats, "re4_core_audio_stats") && ok;
  ok = bind(api.acquire, "re4_core_acquire") && ok;
  ok = bind(api.release, "re4_core_release") && ok;
  ok = bind(api.logTail, "re4_core_log_tail") && ok;
  ok = bind(api.gxAcquire, "re4_core_gx_acquire") && ok;
  ok = bind(api.gxRelease, "re4_core_gx_release") && ok;
  ok = bind(api.gxDetach, "re4_core_gx_detach") && ok;
  api.vsync = reinterpret_cast<void (*)(int64_t)>(dlsym(api.main, "re4_core_vsync"));
  api.audio4 = reinterpret_cast<int (*)(int16_t *, int)>(dlsym(api.main, "re4_core_audio4"));
  api.audioSurround = reinterpret_cast<int (*)(void)>(dlsym(api.main, "re4_core_audio_surround"));
  api.logFileLine = reinterpret_cast<void (*)(const char *)>(dlsym(api.main, "re4_log_file_line"));
  api.pause =reinterpret_cast<int (*)(int, int)>(dlsym(api.main, "re4_core_pause"));
  api.debugMenu = reinterpret_cast<void (*)(void)>(dlsym(api.main, "re4_core_debug_menu"));
  api.setDof = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_dof"));
  api.gyro = reinterpret_cast<void (*)(float, float)>(dlsym(api.main, "re4_core_gyro"));
  api.depth = reinterpret_cast<void (*)(const unsigned *, unsigned)>(dlsym(api.main, "re4_core_gx_depth"));
  api.setLetterbox = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_letterbox"));
  api.setFastCores = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_fast_cores"));
  api.setPotato = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_potato"));
  api.setAim = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_aim"));
  api.aimFromGame = reinterpret_cast<int (*)(void)>(dlsym(api.main, "re4_core_aim_from_game"));
  api.setSoundVolume = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_sound_volume"));
  api.restartGame = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_restart_game"));
  api.discProblem = reinterpret_cast<const char *(*)(void)>(dlsym(api.main, "re4_core_disc_problem"));
  api.setCheats = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_cheats"));
  api.cheatAction = reinterpret_cast<int (*)(int)>(dlsym(api.main, "re4_core_cheat_action"));
  api.roomOutline = reinterpret_cast<int (*)(float *, int, unsigned *)>(dlsym(api.main, "re4_core_room_outline"));
  api.setLaserColour = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_laser_colour"));
  api.setViewAspect = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_set_view_aspect"));
  api.setHudLayout = reinterpret_cast<void (*)(int, int, int)>(dlsym(api.main, "re4_core_set_hud_layout"));
  api.touch = reinterpret_cast<void (*)(float, float)>(dlsym(api.main, "re4_core_touch"));
  api.aimTurn = reinterpret_cast<void (*)(float, float)>(dlsym(api.main, "re4_core_aim_turn"));
  api.swipe = reinterpret_cast<void (*)(int)>(dlsym(api.main, "re4_core_swipe"));
  api.slash = reinterpret_cast<void (*)(float)>(dlsym(api.main, "re4_core_slash"));
  api.instant = reinterpret_cast<int (*)(int, Re4InstantInfo *)>(dlsym(api.main, "re4_core_instant"));
  api.cardList = reinterpret_cast<int (*)(Re4CardFile *, int)>(dlsym(api.main, "re4_core_card_list"));
  api.cardFree = reinterpret_cast<int (*)(void)>(dlsym(api.main, "re4_core_card_free_blocks"));
  api.cardName = reinterpret_cast<int (*)(int, char *, int)>(dlsym(api.main, "re4_core_card_file_name"));
  api.cardExport =
      reinterpret_cast<int (*)(int, unsigned char *, unsigned, char *, int)>(dlsym(api.main, "re4_core_card_export"));
  api.cardImport = reinterpret_cast<int (*)(const unsigned char *, unsigned, int, char *, int)>(
      dlsym(api.main, "re4_core_card_import"));
  api.frameCount = reinterpret_cast<unsigned (*)(void)>(dlsym(api.main, "re4_core_frame_count"));
  api.gameThread = reinterpret_cast<int (*)(void)>(dlsym(api.main, "re4_core_game_thread"));
  api.freeAt = reinterpret_cast<long (*)(void)>(dlsym(api.main, "re4_trace_free_at"));
  api.logFile = reinterpret_cast<void (*)(const char *)>(dlsym(api.main, "re4_log_file"));
  // the app's lines (renderer, core mode) also into the core's log file (cores since 22.10.2026)
  re4gx::SetLogCopy(reinterpret_cast<void (*)(const char *)>(dlsym(api.main, "re4_log_file_line")));
  if (!ok) {
    LOG("%s", api.error.c_str());
    api.main = nullptr;
    return;
  }
#if !defined(__aarch64__)
  {
    Dl_info info;
    char note[160];
    snprintf(note, sizeof(note), "libre4_main.so at %p\n",
             dladdr(reinterpret_cast<void *>(api.mem1Init), &info) ? info.dli_fbase : nullptr);
    gAddressMap += note;
  }
#endif
  // MEM1 before anything else maps memory there (GL drivers, ART growth)
  if (!api.mem1Init()) {
    api.error = "MEM1 0x80000000..0x83000000 is taken in this process";
    // nothing mapped there: the kernel does not give the address to apps (a 32-bit kernel whose
    // apps' part of the address space ends below 0x80000000)
    api.error += gMem1Owner.empty() ? std::string(" (no mapping there: the system refused the address)")
                                    : " (by " + gMem1Owner + ")";
    LOG("%s", api.error.c_str());
  } else {
    LOG("core loaded, MEM1 reserved");
  }
}

std::string text(JNIEnv *env, jstring value) {
  if (!value) return {};
  const char *chars = env->GetStringUTFChars(value, nullptr);
  std::string result = chars ? chars : "";
  if (chars) env->ReleaseStringUTFChars(value, chars);
  return result;
}

std::mutex sceneLock;  // scene creation (UI thread) against GL/worker threads
std::unique_ptr<re4::CoreScene> scene;
re4::CoreFrame frame;  // GL thread
int width = 1, height = 1;
std::chrono::steady_clock::time_point lastDraw;

// GX view (GL thread): on unless RE4_VIEW=bridge; EFB scale from the menu
// (CoreBridge.setGraphics; automatic: from the screen's height, one level below the closest, at
// most 2),
// else RE4_GX_SCALE
std::unique_ptr<re4gx::GxRenderer> gx;
int gxMode = -1;  // -1 undecided, 0 bridge, 1 GX
// The GX renderer's graphics API (port/docs/vulkan-bauplan.md): Vulkan when CoreActivity made a Vulkan surface (it then
// passes RE4_GX_API=vulkan) and its window came (CoreBridge.vkWindow, on the render thread); else GLES. A Vulkan
// renderer that could not start leaves its reason in vulkanProblem: CoreActivity asks for it (CoreBridge.rendererProblem)
// and goes over to GLES.
bool gxVulkan() {
  static const bool vulkan = getenv("RE4_GX_API") && std::string(getenv("RE4_GX_API")) == "vulkan";
  return vulkan;
}
void *vkWindow = nullptr;  // ANativeWindow of the Vulkan surface (one reference held here)
bool glContextLive = false;  // a GL context is current on the render thread (GLSurfaceView; not the Vulkan surface)
std::mutex vulkanProblemLock;
std::string vulkanProblem;
std::string rendererInfoText;  // the renderer's API, device and driver (GxRenderer::ApiInfo; the report's renderer.txt)
bool vulkanFailed = false;
// the menu's picture options (M11, any thread -> GL thread): a change builds
// the renderer anew at the next picture while the game runs (the core then
// starts over with the full state); 0 scale: automatic
// optScale: the EFB scale in halves (0: dynamic; 2: 1x, 3: 1.5x ... 8: 4x; the menu's in-between levels)
std::atomic<int> optScale{0}, optSamples{1}, optAnisotropy{1}, optAspect{-1};
int letterboxSent = -1;  // what the core was told last (drawGx; -1: nothing yet)
int viewAspectSent = -1;  // "Native": the 3D's aspect the core was told last (drawGx; -1: nothing yet)
re4mm::HudLayout hudSent{-1, -1, -1, -1};  // the life gauge's corner the core was told last (drawGx)
// Where drawGx showed the last picture, for taps (CoreBridge.tap): left and top on the surface (y
// down), size in pixels, and the part of the XFB's rows it shows (first and count, 0..1).
std::atomic<int> picX{0}, picY{0}, picW{0}, picH{0};
std::atomic<float> picTop{0.f}, picRows{1.f};

void notePicture(int x, int y, int w, int h, float top, float rows) {
  picX = x;
  picY = y;
  picW = w;
  picH = h;
  picTop = top;
  picRows = rows;
}
// GPU step b (GxOptions::invalidate): off (it saved nothing on the Retroid) unless
// RE4_GX_INVALIDATE=1; GPU step d2 (GxOptions::tiledMsaa, MSAA on the tiles with the EFB drawn
// back after every pass): off (the Retroid lost Leon's hair and textures with it; decision of the
// project owner, 19.10.2026: MSAA in own buffers), unless RE4_GX_MSAA_TILES=1 in the renderer
// (test-core-app.ps1 -MsaaTiles, experiments); GPU step e
// (GxOptions::halfDof): the depth of field's blur at half the resolution, on unless
// RE4_GX_DOF_HALF=0 in the renderer; the GPU measurement switches all three
std::atomic<int> optInvalidate{-1}, optTiled{0}, optHalfDof{1};
std::atomic<unsigned> optSerial{0};
unsigned optApplied = ~0u;
std::atomic<bool> corePaused{false};
// what the device can do and what the renderer uses (CoreBridge.graphicsInfo)
std::atomic<int> infoMaxSamples{0}, infoMaxAnisotropy{0}, infoScale{0}, infoSamples{0}, infoAnisotropy{0};  // infoScale in halves

// an EFB scale in halves as the log writes it: "2" or "1.5"
std::string ScaleName(int halves) {
  char s[16];
  std::snprintf(s, sizeof(s), halves % 2 ? "%d.5" : "%d", halves / 2);
  return s;
}
// the Potato mode bits (not 0) as the GPU bench's log writes them: ", weather effects off, particle effects less"
std::string PotatoName(int bits) {
  std::string s;
  if (bits & 1) s += ", weather effects off";
  if (bits & 2) s += ", special effects off";
  if ((bits >> 2) & 3) s += ((bits >> 2) & 3) == 1 ? ", particle effects less" : ", particle effects none";
  if (bits & 16) s += ", shadows and light effects off";
  return s;
}
unsigned gxChunks = 0;
// timing of the GX view since the last log line
std::chrono::steady_clock::time_point gxSince;
double gxRunMs = 0, gxRunMax = 0;
long long gxRunCpuNs = 0;        // the render thread's processor time in Run since the last line
double gxRunWaitMs = 0.0;        // the render thread's waiting in Run (run time minus processor time), all along
unsigned gxRunOnKind[3] = {};    // where Run ended: slow, middle, fast cores (re4_cpu_info.h)
unsigned gxRunCount = 0, gxFrames = 0, gxDraws = 0;
re4gx::GxStats gxWork;  // the counters at the last log line
// GX dumps for the diagnosis (RE4_GX_DUMP_DIR); -1: settings not read yet
int gxDumpEvery = -1;
std::string gxDumpDir;
std::chrono::steady_clock::time_point gxDumpNext;
bool gxDumpWanted = false;
unsigned gxDumpCount = 0;
// dumps asked for with the controller (R3, CoreBridge.gxDump), any time, at
// most 40 in all; the folder comes with the request when no test set one
std::mutex gxDumpLock;
std::string gxDumpAskedDir;
unsigned gxDumpAsked = 0;
// a controller dump is a series: the chunk that starts over and the ones right
// after it (gx-geraet-NN-KK.bin), drawn one after another in the cloud, so
// flicker over several pictures shows
constexpr unsigned kGxDumpSeries = 12;
bool gxDumpSeriesWanted = false;
unsigned gxDumpSeriesLeft = 0, gxDumpSeriesIndex = 0, gxDumpSeriesPart = 0;
uint32_t gxDumpSeriesSequence = 0;
// a hit on the player (a dark bar at the bottom showed only then, too briefly
// for R3): the core marks the chunk of that moment (RE4_GX_CHUNK_MARK,
// state_trace.cpp, RE4_GX_SHOTS_ON_HIT hits); it starts over. The app keeps it
// and the 3 after it as GX data (RE4_GX_DUMP_ON_HIT hits) and the next 24
// pictures as shown (treffer-NN-KK.bmp), into RE4_GX_DUMP_FOLDER
constexpr unsigned kGxHitSeries = 4, kGxHitShots = 24;
// shader programs ahead of their first draw on a refresh without a chunk: until this much time is
// spent (at least one; linking one anew took about 16 ms on the Retroid, one from the cache 1-2 ms)
constexpr double kPrewarmMs = 4.0;
// in the game's menus, the title and its loading (the core's rumble bit 4, re4_core.h) up to this much of
// a refresh, also after a chunk with what it left (owner's Retroid reports 30.10.2026: 13 s after loading
// a save the programs were not all made, r104 and its Ganados made theirs at the first draw, 780 / 519 ms)
constexpr double kPrewarmMenuMs = 10.0;
bool gameMenuNow() { return api.rumble && (api.rumble() & 0x10) != 0; }
int gxHitDumps = -1;  // hits still to record as GX data; -1: settings not read yet
unsigned gxHitShotsLeft = 0, gxHitShotPart = 0, gxHitCount = 0;
bool gxHitSeriesPending = false;  // the chunk after a marked one starts over: a series from there
GLuint gxShotFbo = 0, gxShotColor = 0;
unsigned gxShotW = 0, gxShotH = 0;

// The GPU measurement (RE4_GPU_BENCH, test-core-app.ps1 -GpuBench; GPU step (a), 19.10.2026):
// one run steps through picture options at the same place of the game (the test's start in r100,
// Leon standing) and logs for each the GPU time of a picture and of a present on the screen
// ("GPU bench N/M: ..."): what resolution, multisampling, anisotropic filtering and depth of field
// cost on this device. RE4_GPU_BENCH=1 takes the steps below, "S/M/A/D,..." (scale, samples,
// anisotropy, depth of field 0/1) names them. The first step comes RE4_GPU_BENCH_DELAY seconds
// (default 10) after the player is free in the room (the core's re4_trace_free_at: the arrival's
// event and the radio call are over); a core without it: that many seconds (default 60) after the
// first picture. Each step: 5 s to settle (the renderer starts over), then 10 s measured.
// Afterwards the options from before the run come back.
//
// Each step's line also has what lies in the picture (draws per picture: the same place for every
// step?), the GPU's load times its clock (the work, independent of the GPU timer) and how many
// pictures the GPU was behind (fences, GxStats::behind*). The first run on the Retroid
// (19.10.2026) began 60 s after the first picture, during the arrival, and its timer read 110 to
// 490 ms per picture with 2x MSAA and the autofocus while 30 pictures/s came: the timer alone does
// not tell there.
struct BenchStep {
  int scale, samples, anisotropy, dof;  // scale in halves ("2.5" in RE4_GPU_BENCH: 5)
  int invalidate = 0;  // GPU step b: copy targets forgotten before they are drawn over
  int tiled = 0;       // GPU step d: MSAA on the tiles
  int halfDof = 1;     // GPU step e: the depth of field's blur at half the resolution
  int potato = -1;     // the Potato mode bits for the step (-1: as the menu has them; r21d, 30.10.2026)
};
struct Bench {
  std::vector<BenchStep> steps;
  int index = -2;  // -2 settings not read, -3 off or done, -1 waiting for the start, else the step
  bool measuring = false;
  std::chrono::steady_clock::time_point next;  // the next change of phase
  std::chrono::steady_clock::time_point since, nextSample;
  re4gx::GxStats at;  // the renderer's counters when the step's measurement began
  double runMs = 0;   // the renderer's CPU time of the step's pictures
  unsigned runCount = 0;
  double clockSum = 0, busySum = 0, workSum = 0;
  unsigned clockCount = 0, busyCount = 0, workCount = 0;
  bool waitFree = false;  // waiting for the player to be free (api.freeAt)
  int delay = 0;          // seconds from then to the first step
  int keepScale = 0, keepSamples = 1, keepAnisotropy = 1, keepInvalidate = 0, keepTiled = 0, keepHalfDof = 1;
};
Bench bench;
std::atomic<int> optDof{-1};  // the menu's depth of field (CoreBridge.setDof: 0 off, 1 light, 2 strong); -1 not set yet
std::atomic<int> optFxaa{0};   // the menu's FXAA (CoreBridge.setFxaa)
std::atomic<int> optSharpen{0};  // the menu's sharpening (CoreBridge.setSharpen: 0 off, 1 light, 2 strong)
std::atomic<int> optLaserColour{0};  // the menu's laser sight colour (CoreBridge.setLaserColour), sent at the start
std::atomic<int> optMinimap{0};   // the menu's minimap (CoreBridge.setMinimap)
std::atomic<int> optFlares{0};    // the menu's lens flares (CoreBridge.setFlares; or RE4_GX_FLARES=1)
std::atomic<int> optPsx{0};       // the menu's PSone demake (CoreBridge.setPsx; or RE4_GX_PSX=1)
std::atomic<int> optTvFilter{0};  // the menu's original TV filter (CoreBridge.setTvFilter; or RE4_GX_TV_FILTER=1)
std::atomic<int> optSmooth{0};  // the menu's 60 FPS from in-between pictures (CoreBridge.setSmooth)
std::atomic<int> optMerge{0};   // the menu's merged draw calls (CoreBridge.setMerge; or RE4_GX_MERGE=1)
std::atomic<int> optVertexRing{0};  // the menu's vertex ring (CoreBridge.setVertexRing; or RE4_GX_VERTEX_RING=1)
// pictures put on the screen so far, each once: the game's and the in-between ones (the FPS display)
std::atomic<long long> picturesMade{0};
// 60 FPS (experimental): the chunk run last, kept for the in-between picture after it (its
// vertices), and the next one, whose in-between picture is on the screen and which runs at the
// next refresh; off for this renderer when the GPU does not keep up
const Re4GxChunk *smoothPrev = nullptr, *smoothNext = nullptr;
// Frame skip: the chunk after the one that was skipped or drawn (taken to look at it, drawn next)
const Re4GxChunk *skipNext = nullptr;
std::atomic<int> optFrameSkip{0};  // the menu's Frame skip (CoreBridge.setFrameSkip)
re4gx::SmoothPacer smoothPacer;
// when the last chunks came: 60 a second is the game's own pace (movies, title)
re4gx::ChunkRate chunkRate;  // 60 FPS: the in-between pictures pause when the renderer is behind again and again
unsigned gxBetweenDropped = 0;      // in-between pictures left out because the renderer was behind
unsigned gxSkipped = 0;             // chunks left out since the start of this renderer
bool smoothTooSlow = false;
// "Dynamic" in the menu's resolution: the EFB level follows the GPU (gx_auto_level.h); on for a renderer
// that started at the screen's level
re4gx::AutoLevel autoLevel;
bool autoLevelOn = false;
// The render thread off the slowest cores and Android's performance hint for it (decision of the
// project owner, 23.10.2026; re4_cpu_info.h; the core does the same for the game thread): set up
// with the first renderer of each render thread (Android starts a new one after the app was in
// the background), then each chunk's Run against 33.3 ms
re4cpu::PerfHint renderHint;
int renderPlaced = 0;  // the render thread set up
// The menu's "Force Performance" (CoreBridge.setFastCores, wish of the project owner, 24.10.2026):
// 1 "Use performance cores" (the render thread and, through the core, the game thread on the fast
// cores with Android's performance hint), 0 "Default" (the default: Android decides).
std::atomic<int> optFastCores{0};
int fastSent = -1;    // what the core was told last (-1: nothing yet)
// The menu's Potato mode (CoreBridge.setPotato, decision of the project owner, 30.10.2026): bit 0 the weather
// effects off, bit 1 the special effects off, since 0.99.3 bits 2..3 the particle effects (1 less, 2 none) and bit 4
// shadows and light effects off (the core's runtime/potato.cpp); 0 the default.
std::atomic<int> optPotato{0};
int potatoSent = -1;  // what the core was told last (-1: nothing yet; the GPU bench sets its own)
// The menu's "Invert vertical" and "Aim speed" for touch and controllers (CoreBridge.setAim, wish of the
// project owner, 30.10.2026): bit 0 inverted, bits 8.. the speed in percent (the core's runtime/aim_options.cpp).
std::atomic<int> optAim{100 << 8};
std::atomic<int> optFarCamera{0};  // the menu's "Far camera" (CoreBridge.setFarCamera), bit 3 of the aim bits
// the menu's "Field of view" (CoreBridge.setFov, -15 .. +15 degrees, patch 0205): bits 24..29 of the aim bits as
// degrees + 32 (0: as the game; the core's runtime/aim_options.cpp)
std::atomic<int> optFov{0};
// "Dynamic" up to the benchmark's levels (CoreBridge.setDynamicCaps, the app keeps them per device): with 30 FPS,
// with 60 FPS; in halves, 0: none measured (the screen's level as before)
std::atomic<int> optCap30{0}, optCap60{0};

// The benchmark (wish of the project owner, 05.11.2026; the app menu Display -> GRAPHICS -> Benchmark). The game
// goes to the lake's dock in r11b (runtime/benchmark.cpp through the cheat channel: 20 the place, 22 back, 23 the
// state; since 06.11.2026 the project owner's instant save of the place, from the title or any chapter),
// where the enemies stand still, Leon stands and the view goes round him; here, on the render thread, the steps of
// gx_bench_steps.h: a 30 and a 60 FPS pass, each from 4x down half a level every 10 s until a level holds. While it
// runs the level is the benchmark's alone: "Dynamic" does not change it (it set its own over it on every chunk, the
// project owner's device test 06.11.2026), a new renderer (new picture options) starts at it, and the pictures are
// counted here, not in the renderer's statistics, which start over with a new renderer. It is fed on every refresh,
// with or without a chunk, so its clock goes on when the game stalls; at most kBenchLimit seconds in all.
struct LevelBench {
  std::atomic<int> ask{0};    // 1 start, 2 cancel (CoreBridge)
  // 0 idle, 1 going to the place, 2 measuring, 3 going back, 4 done (results), 5 cancelled, 6 going back
  // after a cancel or a stop; -1 refused (neither free play nor the title's menu), -2 failed (the game could not be
  // kept, or the time limit), -3 no place reached, -4 the place's save does not fit these discs
  std::atomic<int> phase{0}, run{0}, level{0}, res30{0}, res60{0}, rate10{0};
  std::atomic<int> held{0};   // bit 0: the 30 result held, bit 1: the 60 result held (1x not held: the result is 1x)
  bool active = false, failed = false;
  bool touched = false;       // the benchmark set levels or 60 FPS: the renderer anew with the menu's at the end
  int keepSmooth = 0;
  re4gx::BenchSteps steps;
  std::chrono::steady_clock::time_point since, start, secondAt;
  unsigned long long chunks = 0, chunksAt = 0, shownAt = 0;
  unsigned droppedAt = 0, polls = 0;
};
LevelBench levelBench;
constexpr double kBenchLimit = 360.0;  // seconds for the whole benchmark at most (the way there and back included)
int aimSent = -1;  // what the core was told last (-1: nothing yet)
// The menu's AUDIO page (CoreBridge.setSoundVolume, wish of the project owner, 30.10.2026): music | effects << 8 in
// percent (the core's runtime/sound_volume.cpp).
std::atomic<int> optSound{100 | 100 << 8};
int soundSent = -1;  // what the core was told last (-1: nothing yet)
int renderFast = -1;  // the setting the render thread runs with (-1: none yet)

// Shader programs made ahead on a thread of its own (decision of the project owner, 23.10.2026):
// the cache's keys and the app's list (gx-programs.keys, CoreActivity) that lack a binary are
// linked with a GL context of its own (a 1x1 pbuffer), below the game's priority and on any core;
// the renderer's Prewarm then only loads binaries. Again when the MSAA setting changed;
// RE4_GX_WARM=0 leaves it out.
std::atomic<bool> warmBusy{false};
int warmCentroid = -1;  // the MSAA setting the last one was for

void startWarm(const std::string &cache, const std::string &list, std::uint32_t centroid) {
  if (warmBusy.load() || warmCentroid == static_cast<int>(centroid)) return;
  const char *off = getenv("RE4_GX_WARM");
  if (off && *off == '0') {
    if (warmCentroid < 0) LOG("GX programs made ahead: off (RE4_GX_WARM=0)");
    warmCentroid = static_cast<int>(centroid);
    return;
  }
  warmCentroid = static_cast<int>(centroid);
  warmBusy = true;
  std::thread([cache, list, centroid] {
    pthread_setname_np(pthread_self(), "re4-gx-warm");
    // any core (the render thread that started it keeps off the slow ones), below the game
    cpu_set_t all;
    CPU_ZERO(&all);
    if (sched_getaffinity(getpid(), sizeof(all), &all) == 0) sched_setaffinity(0, sizeof(all), &all);
    setpriority(PRIO_PROCESS, 0, 10);
    const char *problem = nullptr;
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config{};
    EGLint count = 0;
    const EGLint attributes[] = {EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                 EGL_NONE};
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) {
      problem = "no EGL display";
    } else if (!eglChooseConfig(display, attributes, &config, 1, &count) || count < 1) {
      problem = "no GLES 3 pbuffer configuration";
    } else if ((context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes)) == EGL_NO_CONTEXT) {
      problem = "no context";
    } else if ((surface = eglCreatePbufferSurface(display, config, size)) == EGL_NO_SURFACE) {
      problem = "no pbuffer";
    } else if (!eglMakeCurrent(display, surface, surface, context)) {
      problem = "the context not current";
    }
    if (problem) {
      LOG("GX programs made ahead: not possible (%s, EGL error 0x%x); each at its first draw", problem, eglGetError());
    } else {
      const re4gx::GxWarmResult r = re4gx::WarmProgramCache(cache, list, centroid, nullptr);
      LOG("GX programs made ahead on a thread of its own: %u keys (%u from the app's list), %u linked, %u had a "
          "binary, %u failed, %u old files removed, %.0f ms",
          r.keys, r.listed, r.linked, r.had, r.failed, r.stale, r.ms);
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    // (not eglTerminate: the app's own context lives on the same display)
    if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
    if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
    warmBusy = false;
  }).detach();
}
// The chunks the in-between pictures hold, back to the core (a new renderer, the switch off).
void smoothDrop() {
  if (smoothNext && api.gxRelease)
    api.gxRelease(smoothNext);
  if (smoothPrev && api.gxRelease)
    api.gxRelease(smoothPrev);
  if (skipNext && api.gxRelease)
    api.gxRelease(skipNext);
  smoothNext = smoothPrev = skipNext = nullptr;
}

// A number from a file of the kernel (the GPU's clock and load, where the device lets an app read
// them); -1 when it cannot.
double readNumber(const char *path) {
  FILE *f = std::fopen(path, "r");
  if (!f) return -1;
  double v = -1;
  if (std::fscanf(f, "%lf", &v) != 1) v = -1;
  std::fclose(f);
  return v;
}

// The GPU clock in MHz (Adreno: kgsl in Hz; others in MHz), -1 when no file can be read.
double gpuClockMhz() {
  for (const char *path : {"/sys/class/kgsl/kgsl-3d0/gpuclk", "/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq",
                           "/sys/kernel/gpu/gpu_clock"}) {
    const double v = readNumber(path);
    if (v > 0) return v > 100000 ? v / 1e6 : v;
  }
  return -1;
}

// The GPU's load in percent as the kernel reports it, -1 when no file can be read.
double gpuBusyPercent() {
  for (const char *path : {"/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage", "/sys/kernel/gpu/gpu_busy"}) {
    const double v = readNumber(path);
    if (v >= 0) return v;
  }
  return -1;
}

void benchApply(const BenchStep &step) {
  optScale = step.scale;
  optSamples = step.samples;
  optAnisotropy = step.anisotropy;
  optInvalidate = step.invalidate;
  optTiled = step.tiled;
  optHalfDof = step.halfDof;
  optSerial++;
  if (api.setDof) api.setDof(step.dof);
  if (api.setPotato) api.setPotato(step.potato >= 0 ? step.potato : optPotato.load());
}

// Once per displayed frame (GL thread), after the chunk.
void benchTick(std::chrono::steady_clock::time_point now) {
  using std::chrono::seconds;
  if (bench.index == -2) {
    bench.index = -3;
    const char *spec = getenv("RE4_GPU_BENCH");
    if (!spec || !*spec || std::string(spec) == "0") return;
    if (std::string(spec) == "1") {
      // at scale 3: nothing extra; the depth of field; the tester's options (2x MSAA, 8x
      // anisotropy, depth of field; POCO X7 Pro, 18.10.2026); the same with 4x MSAA; 4x at
      // scale 2; nothing extra again. MSAA in own buffers (decision of the project owner,
      // 19.10.2026: on the Retroid MSAA on the tiles, d and d2, lost Leon's hair and textures).
      // The runs before (Retroid, 19.10.2026): MSAA 2x +74 % work, the depth of field +19 %
      // (at the half resolution the same), 8x anisotropy and step b nothing measurable, the same
      // setting twice up to 5 % apart; 4x at scale 3: 24 pictures/s.
      // "S/M/A/D/I/T/H,..." names other steps (I: copy targets forgotten, T: MSAA on the tiles,
      // H: the depth of field at half the resolution, 1 when left out)
      bench.steps = {{6, 1, 1, 0}, {6, 1, 1, 1}, {6, 2, 8, 1}, {6, 4, 8, 1}, {4, 4, 8, 1}, {6, 1, 1, 0}};
    } else {
      for (const char *at = spec; *at;) {
        BenchStep step{};
        int used = 0;
        float scale = 0;  // whole or half levels ("2.5")
        if (std::sscanf(at, "%f/%d/%d/%d%n", &scale, &step.samples, &step.anisotropy, &step.dof, &used) != 4)
          break;
        step.scale = static_cast<int>(scale * 2 + 0.5f);
        int more = 0;
        if (at[used] == '/' && std::sscanf(at + used, "/%d%n", &step.invalidate, &more) == 1) used += more;
        if (at[used] == '/' && std::sscanf(at + used, "/%d%n", &step.tiled, &more) == 1) used += more;
        if (at[used] == '/' && std::sscanf(at + used, "/%d%n", &step.halfDof, &more) == 1) used += more;
        if (at[used] == '/' && std::sscanf(at + used, "/%d%n", &step.potato, &more) == 1) used += more;
        bench.steps.push_back(step);
        at += used;
        if (*at == ',') at++;
      }
    }
    if (bench.steps.empty()) {
      LOG("GPU bench: no steps in RE4_GPU_BENCH=%s", spec);
      return;
    }
    const char *delay = getenv("RE4_GPU_BENCH_DELAY");
    bench.waitFree = api.freeAt != nullptr;
    bench.delay = delay ? std::max(1, std::atoi(delay)) : bench.waitFree ? 10 : 60;
    // waiting for the player: at most 5 minutes, then measured anyway
    bench.next = now + seconds(bench.waitFree ? 300 : bench.delay);
    bench.index = -1;
    const double clock = gpuClockMhz(), busy = gpuBusyPercent();
    LOG("GPU bench: %zu steps, the first %d s after %s (5 s to settle, 10 s measured each); GPU clock %s, load %s",
        bench.steps.size(), bench.delay, bench.waitFree ? "the player is free" : "the first picture",
        clock > 0 ? "readable" : "not readable", busy >= 0 ? "readable" : "not readable");
    return;
  }
  if (bench.index < -1 || !gx) return;
  if (bench.index == -1 && bench.waitFree) {
    const long freeAt = api.freeAt();
    if (freeAt >= 0) {
      bench.waitFree = false;
      bench.next = now + seconds(bench.delay);
      LOG("GPU bench: the player is free (frame %ld), the first step in %d s", freeAt, bench.delay);
    } else if (now >= bench.next) {
      bench.waitFree = false;
      LOG("GPU bench: the player did not get free in 300 s; measured anyway");
    }
  }
  if (bench.measuring && now >= bench.nextSample) {
    bench.nextSample = now + seconds(1);
    const double clock = gpuClockMhz(), busy = gpuBusyPercent();
    if (clock > 0) {
      bench.clockSum += clock;
      bench.clockCount++;
    }
    if (busy >= 0) {
      bench.busySum += busy;
      bench.busyCount++;
    }
    // the work: load times clock of the same moment (MHz the GPU was busy)
    if (clock > 0 && busy >= 0) {
      bench.workSum += clock * busy / 100.0;
      bench.workCount++;
    }
  }
  if (bench.waitFree || now < bench.next) return;
  if (bench.index == -1) {
    bench.keepScale = optScale.load();
    bench.keepSamples = optSamples.load();
    bench.keepAnisotropy = optAnisotropy.load();
    bench.keepInvalidate = optInvalidate.load();
    bench.keepTiled = optTiled.load();
    bench.keepHalfDof = optHalfDof.load();
    bench.index = 0;
    benchApply(bench.steps[0]);
    bench.next = now + seconds(5);
    return;
  }
  if (!bench.measuring) {
    bench.at = gx->Stats();
    bench.since = now;
    bench.runMs = 0;
    bench.runCount = 0;
    bench.clockSum = bench.busySum = bench.workSum = 0;
    bench.clockCount = bench.busyCount = bench.workCount = 0;
    bench.nextSample = now;
    bench.measuring = true;
    bench.next = now + seconds(10);
    return;
  }
  const re4gx::GxStats &s = gx->Stats();
  const BenchStep &step = bench.steps[static_cast<size_t>(bench.index)];
  const double secs = std::chrono::duration<double>(now - bench.since).count();
  const unsigned timed = s.gpuTimed - bench.at.gpuTimed, presentTimed = s.presentTimed - bench.at.presentTimed;
  const unsigned chunks = s.chunks - bench.at.chunks, checks = s.behindChecks - bench.at.behindChecks;
  const double pictures = secs > 0 ? chunks / secs : 0.0;
  char gpu[96] = "not measured", present[48] = "not measured", clock[24] = "-", busy[24] = "-", work[24] = "-";
  char behind[64] = "not checked";
  if (timed) {
    const double ms = (s.gpuMicros - bench.at.gpuMicros) / 1000.0 / timed;
    // longer than a picture's interval while the pictures keep coming: not the GPU's real time
    const bool plausible = pictures <= 0 || ms <= 1000.0 / pictures * 1.05;
    std::snprintf(gpu, sizeof(gpu), "%.1f ms (%u timed%s)", ms, timed,
                  plausible ? "" : ", NOT plausible: longer than a picture's interval");
  }
  if (presentTimed)
    std::snprintf(present, sizeof(present), "%.2f ms (%u timed)",
                  (s.presentMicros - bench.at.presentMicros) / 1000.0 / presentTimed, presentTimed);
  if (bench.clockCount) std::snprintf(clock, sizeof(clock), "%.0f MHz", bench.clockSum / bench.clockCount);
  if (bench.busyCount) std::snprintf(busy, sizeof(busy), "%.0f %%", bench.busySum / bench.busyCount);
  if (bench.workCount) std::snprintf(work, sizeof(work), "%.0f MHz", bench.workSum / bench.workCount);
  if (checks) {
    unsigned most = 0;
    for (unsigned n = 0; n < 9; n++)
      if (s.behindCount[n] != bench.at.behindCount[n]) most = n;
    std::snprintf(behind, sizeof(behind), "%.2f pictures on average, at most %u%s",
                  static_cast<double>(s.behindSum - bench.at.behindSum) / checks, most, most == 8 ? " or more" : "");
  }
  char tiles[64] = "";
  if (gx->TiledMsaa())
    std::snprintf(tiles, sizeof(tiles), " on the tiles (EFB drawn back %.1fx per picture)",
                  chunks ? static_cast<double>(s.efbRestores - bench.at.efbRestores) / chunks : 0.0);
  LOG("GPU bench %d/%zu: scale %s, %d samples%s, anisotropy %.0f, depth of field %s%s, copy targets forgotten %s%s: "
      "GPU %s per picture, "
      "present %s, %.1f pictures/s, %.1f presents/s, renderer CPU %.1f ms, GPU clock %s, load %s, "
      "work (load x clock) %s, GPU behind %s, %.0f draws per picture",
      bench.index + 1, bench.steps.size(), ScaleName(infoScale.load()).c_str(), gx->Samples(),
      gx->Samples() > 1 ? (gx->TiledMsaa() ? tiles : " in own buffers") : "", gx->Anisotropy(),
      step.dof ? "on" : "off",
      step.dof ? (gx->HalfDof() ? " (blur at half resolution)" : " (blur at full resolution)") : "",
      gx->Invalidates() ? "yes" : "no",
      step.potato < 0 ? "" : step.potato == 0 ? ", Potato mode off" : PotatoName(step.potato).c_str(),
      gpu, present, pictures, secs > 0 ? (s.presents - bench.at.presents) / secs : 0.0,
      bench.runCount ? bench.runMs / bench.runCount : 0.0, clock, busy, work, behind,
      chunks ? static_cast<double>(s.draws - bench.at.draws) / chunks : 0.0);
  bench.measuring = false;
  bench.index++;
  if (bench.index >= static_cast<int>(bench.steps.size())) {
    optScale = bench.keepScale;
    optSamples = bench.keepSamples;
    optAnisotropy = bench.keepAnisotropy;
    optInvalidate = bench.keepInvalidate;
    optTiled = bench.keepTiled;
    optHalfDof = bench.keepHalfDof;
    optSerial++;
    if (optDof.load() >= 0 && api.setDof) api.setDof(optDof.load());
    if (api.setLaserColour) api.setLaserColour(optLaserColour.load());
    potatoSent = -1;
    LOG("GPU bench done; the options from before the run are back");
    bench.index = -3;
    return;
  }
  benchApply(bench.steps[static_cast<size_t>(bench.index)]);
  bench.next = now + seconds(5);
}

// The picture as the screen shows it (the XFB, the game's own size) into a 24-bit BMP.
void SaveShot(re4gx::GxRenderer &renderer, const char *path) {
  unsigned w = 0, h = 0;
  if (!renderer.Xfb(&w, &h) || !w || !h)
    return;
  std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
  if (renderer.Api() == re4gx::GxApi::kVulkan) {
    // Vulkan: the same picture read back (rows from the top: turned to GL's order, bottom up, for the BMP)
    std::vector<std::uint8_t> top;
    if (!renderer.ReadPresented(static_cast<int>(w), static_cast<int>(h), 0, 0, static_cast<int>(w),
                                static_cast<int>(h), 0, 0, &top) || top.size() != rgba.size())
      return;
    const size_t row = static_cast<size_t>(w) * 4;
    for (unsigned y = 0; y < h; y++)
      std::memcpy(rgba.data() + (h - 1 - y) * row, top.data() + y * row, row);
  } else {
  if (!gxShotFbo || gxShotW != w || gxShotH != h) {
    if (!gxShotFbo)
      glGenFramebuffers(1, &gxShotFbo);
    if (gxShotColor)
      glDeleteTextures(1, &gxShotColor);
    glGenTextures(1, &gxShotColor);
    glBindTexture(GL_TEXTURE_2D, gxShotColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, gxShotFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gxShotColor, 0);
    gxShotW = w;
    gxShotH = h;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, gxShotFbo);
  glDisable(GL_SCISSOR_TEST);
  renderer.Present(0, 0, static_cast<int>(w), static_cast<int>(h));
  glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  // BMP rows go bottom up, as glReadPixels returns them
  const unsigned rowBytes = (w * 3 + 3) & ~3u;
  const uint32_t dataBytes = rowBytes * h, fileBytes = 54 + dataBytes;
  unsigned char head[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) {
    for (int i = 0; i < 4; i++)
      head[at + i] = static_cast<unsigned char>(v >> (8 * i));
  };
  put32(2, fileBytes);
  put32(10, 54);
  put32(14, 40);
  put32(18, w);
  put32(22, h);
  head[26] = 1;
  head[28] = 24;
  put32(34, dataBytes);
  if (FILE *f = std::fopen(path, "wb")) {
    std::fwrite(head, 1, sizeof(head), f);
    std::vector<unsigned char> row(rowBytes, 0);
    for (unsigned y = 0; y < h; y++) {
      for (unsigned x = 0; x < w; x++) {
        const unsigned char *p = &rgba[(static_cast<size_t>(y) * w + x) * 4];
        row[x * 3] = p[2];
        row[x * 3 + 1] = p[1];
        row[x * 3 + 2] = p[0];
      }
      std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
  }
}

void SaveShot(const char *path) { SaveShot(*gx, path); }

// Diagnosis (RE4_GX_REPLAY: a folder with R3 series, gx-geraet-NN-KK.bin; the test script's
// -GxReplay): the series drawn again on this device before the game's first picture, with the
// options of each variant, the first one three times, each picture kept as this GPU drew it
// (replay-VARIANT-ROUND-NN-KK.bmp in that folder; the anisotropy of the menu). The cloud then sees
// whether this GPU draws the same chunks the same every time and with which options a picture
// differs (thin branches went paler for one picture with 2x MSAA on the Retroid only,
// 20.10.2026). Once per process; the game waits meanwhile.
// Measurement (RE4_GX_REPLAY_WORK=1 with RE4_GX_REPLAY; a user's ideas on EFB copies and merged draw calls,
// 31.10.2026): every series at 1x and 1.5x, at 1.5x also with merged draw calls, each picture finished
// (glFinish) and timed with the menu's MSAA, anisotropy and depth of field; then twice more with the EFB
// copies, the display copy and the autofocus timed one by one (RE4_GX_COPY_TIME, glFinish around each). The
// first 5 pictures of a series (programs, uploads) are not counted. One "GX work" line per series and variant;
// no pictures kept, the game waits meanwhile.
static void replayWork(const std::vector<std::unique_ptr<re4gxio::Loaded>> &chunks,
                       const std::vector<std::string> &stems) {
  struct Work {
    const char *name;
    int halves;
    bool merge, copyTime;
  };
  static const Work kWork[] = {{"1x", 2, false, false},        {"1.5x", 3, false, false},
                               {"1.5x-merge", 3, true, false}, {"1x-copies", 2, false, true},
                               {"1.5x-copies", 3, false, true}};
  static const char *kKinds[re4gx::GxStats::kCopyKinds] = {"whole", "half", "depth", "small", "display", "focus"};
  for (const Work &v : kWork) {
    if (v.copyTime)
      setenv("RE4_GX_COPY_TIME", "1", 1);
    else
      unsetenv("RE4_GX_COPY_TIME");
    std::unique_ptr<re4gx::GxRenderer> r;
    uint32_t lastSequence = 0, lastFrame = 0;
    std::string series;
    re4gx::GxStats at;
    double ms = 0.0;
    unsigned timed = 0, run = 0;
    const auto report = [&]() {
      if (!r || !timed) return;
      const re4gx::GxStats &s = r->Stats();
      char copies[512];
      int o = 0;
      for (int k = 0; k < re4gx::GxStats::kCopyKinds && o < static_cast<int>(sizeof(copies)); k++)
        o += std::snprintf(copies + o, sizeof(copies) - o, "%s %s %.1f (%.2f ms)", k ? "," : "", kKinds[k],
                           (s.copyCount[k] - at.copyCount[k]) / static_cast<double>(timed),
                           (s.copyNs[k] - at.copyNs[k]) / 1e6 / timed);
      const unsigned draws = s.draws - at.draws;
      const unsigned calls = draws - (s.mergeTaken - at.mergeTaken) + (s.mergeBatches - at.mergeBatches);
      LOG("GX work %s series %s: %u pictures, %.2f ms each (Run + glFinish), %.0f draws in %.0f GL calls; "
          "copies%s; read %.1f, drawn over unread %.1f",
          v.name, series.c_str(), timed, ms / timed, draws / static_cast<double>(timed),
          calls / static_cast<double>(timed), copies, (s.copyRead - at.copyRead) / static_cast<double>(timed),
          (s.copyOverwrittenUnread - at.copyOverwrittenUnread) / static_cast<double>(timed));
    };
    for (size_t i = 0; i < chunks.size(); i++) {
      Re4GxChunk chunk = chunks[i]->chunk;
      const bool follows = r && chunk.sequence == lastSequence + 1 && chunks[i]->frame == lastFrame + 1;
      if (!follows) {
        if (!(chunk.flags & RE4_GX_CHUNK_RESET)) continue;
        report();
        if (r) r->Release();
        r = std::make_unique<re4gx::GxRenderer>();
        re4gx::GxOptions options;
        options.samples = std::max(1, optSamples.load());
        options.anisotropy = static_cast<float>(std::max(1, optAnisotropy.load()));
        options.halfDof = optHalfDof.load() != 0;
        r->SetOptions(options);
        std::string error;
        if (!r->InitHalves(v.halves, &error)) {
          LOG("GX work %s: renderer: %s", v.name, error.c_str());
          r.reset();
          continue;
        }
        r->SetMerge(v.merge);
        chunk.flags &= ~RE4_GX_CHUNK_HOLD;
        series = stems[i].substr(0, 2);
        ms = 0.0;
        timed = run = 0;
      }
      lastSequence = chunk.sequence;
      lastFrame = chunks[i]->frame;
      if (run == 5) at = r->Stats();
      glFinish();
      const auto start = std::chrono::steady_clock::now();
      r->Run(chunk);
      glFinish();
      const double took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      if (run++ >= 5) {
        ms += took;
        timed++;
      }
    }
    report();
    if (r) r->Release();
  }
  unsetenv("RE4_GX_COPY_TIME");
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool gxReplayDone = false;

void replayGx(int halves) {
  if (gxReplayDone) return;
  gxReplayDone = true;
  const char *dir = getenv("RE4_GX_REPLAY");
  if (!dir || !*dir) return;
  if (gxVulkan() && !vulkanFailed) {
    LOG("GX replay: GLES only (the app draws with Vulkan)");
    return;
  }
  std::vector<std::string> names;
  if (DIR *d = opendir(dir)) {
    while (const dirent *e = readdir(d)) {
      const std::string n = e->d_name;
      if (n.rfind("gx-geraet-", 0) == 0 && n.size() > 14 && n.compare(n.size() - 4, 4, ".bin") == 0) names.push_back(n);
    }
    closedir(d);
  }
  std::sort(names.begin(), names.end());
  std::vector<std::unique_ptr<re4gxio::Loaded>> chunks;
  std::vector<std::string> stems;  // "NN-KK"
  for (const std::string &n : names) {
    auto loaded = std::make_unique<re4gxio::Loaded>();
    FILE *f = std::fopen((std::string(dir) + "/" + n).c_str(), "rb");
    const bool ok = f && re4gxio::Read(f, *loaded);
    if (f) std::fclose(f);
    if (!ok) {
      LOG("GX replay: %s not readable", n.c_str());
      continue;
    }
    chunks.push_back(std::move(loaded));
    stems.push_back(n.substr(10, n.size() - 14));
  }
  struct Variant {
    const char *name;
    int samples;
    bool centroid, fullResolve;
    int rounds;
  };
  // as the R3 series of the flicker was made (2x MSAA, without centroid: before 20.10.2026), then
  // with centroid (since), with the whole EFB resolved each time, without MSAA, 4x with centroid
  static const Variant kVariants[] = {{"s2old", 2, false, false, 3}, {"s2cen", 2, true, false, 2},
                                      {"s2full", 2, false, true, 1}, {"s1", 1, true, false, 1},
                                      {"s4cen", 4, true, false, 1}};
  LOG("GX replay: %zu chunks from %s, EFB scale %s", chunks.size(), dir, ScaleName(halves).c_str());
  if (const char *work = getenv("RE4_GX_REPLAY_WORK")) {
    if (*work == '1') {
      replayWork(chunks, stems);
      LOG("GX replay: measurement done");
      return;
    }
  }
  const char *cache = getenv("RE4_GX_PROGRAM_CACHE");
  unsigned pictures = 0;
  for (const Variant &v : kVariants) {
    for (int round = 1; round <= v.rounds; round++) {
      std::unique_ptr<re4gx::GxRenderer> r;
      uint32_t lastSequence = 0, lastFrame = 0;
      for (size_t i = 0; i < chunks.size(); i++) {
        Re4GxChunk chunk = chunks[i]->chunk;
        const bool follows = r && chunk.sequence == lastSequence + 1 && chunks[i]->frame == lastFrame + 1;
        if (!follows) {
          if (!(chunk.flags & RE4_GX_CHUNK_RESET)) {
            LOG("GX replay: %s follows no picture before it, left out", stems[i].c_str());
            continue;
          }
          // each series on a renderer of its own, as after R3 in the game
          if (r) r->Release();
          r = std::make_unique<re4gx::GxRenderer>();
          re4gx::GxOptions options;
          options.samples = v.samples;
          options.anisotropy = static_cast<float>(std::max(1, optAnisotropy.load()));
          options.centroid = v.centroid;
          options.fullResolve = v.fullResolve;
          options.halfDof = optHalfDof.load() != 0;
          r->SetOptions(options);
          std::string error;
          if (!r->InitHalves(halves, &error)) {
            LOG("GX replay %s: renderer: %s", v.name, error.c_str());
            r.reset();
            continue;
          }
          if (cache && *cache) r->SetProgramCache(cache);
          chunk.flags &= ~RE4_GX_CHUNK_HOLD;
        }
        lastSequence = chunk.sequence;
        lastFrame = chunks[i]->frame;
        r->Run(chunk);
        char path[512];
        std::snprintf(path, sizeof(path), "%s/replay-%s-%d-%s.bmp", dir, v.name, round, stems[i].c_str());
        SaveShot(*r, path);
        pictures++;
      }
      if (r) {
        LOG("GX replay %s round %d: %d samples%s, anisotropy %.0f", v.name, round, r->Samples(),
            r->Samples() > 1 ? (r->TiledMsaa() ? " on the tiles" : " in own buffers") : "", r->Anisotropy());
        r->Release();
      }
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  LOG("GX replay: done, %u pictures", pictures);
}

// --- the minimap (CoreBridge.setMinimap; wish of the project owner, 04.11.2026: optional, the current room's
// outline only, turning with the player) --------------------------------------------------------------------
// The outline comes from the core (re4_core_room_outline: the room's floor collision, asked again when the room
// changes and once a second), the player from the frame (the player model's matrix). Drawn after the picture,
// at its bottom left (the game's HUD is at the bottom right), only while the player plays: the main loop, no
// event, no sub screen. A dark disc, the outline (other storeys fainter), a ring and the player's arrow.
struct MinimapState {
  bool failed = false;
  GLuint program = 0, vbo = 0, vao = 0;
  GLint scaleLoc = -1;
  std::vector<float> edges;  // x0 z0 x1 z1 height
  std::vector<float> lines;  // the projected segments (re4mm::Project)
  std::vector<float> verts;  // x y r g b a
  unsigned serial = ~0u, room = 0xFFFF, askedRoom = 0xFFFF;
  float radius = 10000.0f;  // world units to the map's edge (re4mm::RoomRadius)
  std::chrono::steady_clock::time_point asked;
  bool logged = false;
  long long placeLogged = -1;  // the place last logged (minimapDraw)
} minimap;

bool minimapProgram() {
  static const char kVs[] = "#version 300 es\n"
                            "layout(location = 0) in vec2 aPos;\nlayout(location = 1) in vec4 aCol;\n"
                            "out vec4 vCol;\n"
                            "void main() { vCol = aCol; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
  static const char kFs[] = "#version 300 es\nprecision mediump float;\n"
                            "in vec4 vCol;\nout vec4 o;\n"
                            "void main() { o = vCol; }\n";
  auto compile = [](GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      glDeleteShader(s);
      return 0u;
    }
    return s;
  };
  const GLuint vs = compile(GL_VERTEX_SHADER, kVs), fs = compile(GL_FRAGMENT_SHADER, kFs);
  if (!vs || !fs)
    return false;
  minimap.program = glCreateProgram();
  glAttachShader(minimap.program, vs);
  glAttachShader(minimap.program, fs);
  glLinkProgram(minimap.program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(minimap.program, GL_LINK_STATUS, &ok);
  if (!ok)
    return false;
  glGenBuffers(1, &minimap.vbo);
  glGenVertexArrays(1, &minimap.vao);
  glBindVertexArray(minimap.vao);
  glBindBuffer(GL_ARRAY_BUFFER, minimap.vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<const void *>(2 * sizeof(float)));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

// The map in the picture: the menu's picture format and the XFB's lines (re4mm::PictureRects / Place, where the
// game's own gauge sits; owner's report 05.11.2026)
// The minimap of this picture: its vertices and its place (false: none now); minimapDraw draws it with GLES, the
// Vulkan renderer over its picture (GxRenderer::SetOverlay)
bool minimapBuild(int format, unsigned xh, bool wide, re4gx::GxOverlay *out) {
  if (!optMinimap.load() || minimap.failed || !api.roomOutline || !frame.valid || frame.step[0] != 3 ||
      (frame.screen & (RE4_CORE_SCREEN_SUB | RE4_CORE_SCREEN_EVENT)))
    return false;
  const Re4CoreModel *player = nullptr;
  for (const Re4CoreModel &mdl : frame.models)
    if (mdl.manager == 0 && mdl.id == 0 && (mdl.beFlag & 6) == 6) {
      player = &mdl;
      break;
    }
  re4mm::Pose pose;
  if (!player || !re4mm::PoseFromMatrix(player->matrix, &pose))
    return false;
  // the outline: again when the room changed, else once a second (the room's pieces may change)
  const auto now = std::chrono::steady_clock::now();
  if (frame.room != minimap.askedRoom || now - minimap.asked > std::chrono::seconds(1)) {
    minimap.askedRoom = frame.room;
    minimap.asked = now;
    static std::vector<float> buffer(12000 * 5);
    unsigned info[2] = {0, 0xFFFF};
    const int n = api.roomOutline(buffer.data(), 12000, info);
    if (info[0] != minimap.serial) {
      minimap.serial = info[0];
      minimap.room = info[1];
      minimap.edges.assign(buffer.begin(), buffer.begin() + std::max(0, n) * 5);
      minimap.radius = re4mm::RoomRadius(minimap.edges.data(), std::max(0, n));
      LOG("minimap: r%03x, %d edges, %.0f m around the player", info[1], n, minimap.radius / 1000.0f);
    }
  }
  if (minimap.room != frame.room || minimap.edges.empty())
    return false;
  // around the player as far as the room's size asks (re4mm::RoomRadius: 10 to 24 m)
  re4mm::Project(minimap.edges.data(), static_cast<int>(minimap.edges.size() / 5), pose, minimap.radius, minimap.lines);
  std::vector<float> &v = minimap.verts;
  v.clear();
  auto put = [&](float x, float y, float r, float g, float b, float a) {
    v.insert(v.end(), {x, y, r, g, b, a});
  };
  // the disc (a fan as triangles) and the ring
  const int kSides = 48;
  for (int i = 0; i < kSides; i++) {
    const float a0 = 6.2831853f * i / kSides, a1 = 6.2831853f * (i + 1) / kSides;
    put(0, 0, 0.0f, 0.0f, 0.0f, 0.45f);
    put(std::cos(a0), std::sin(a0), 0.0f, 0.0f, 0.0f, 0.45f);
    put(std::cos(a1), std::sin(a1), 0.0f, 0.0f, 0.0f, 0.45f);
  }
  const int discVerts = static_cast<int>(v.size() / 6);
  // the player: an arrow pointing up
  put(0.0f, 0.13f, 1.0f, 0.78f, 0.25f, 0.95f);
  put(-0.08f, -0.09f, 1.0f, 0.78f, 0.25f, 0.95f);
  put(0.0f, -0.04f, 1.0f, 0.78f, 0.25f, 0.95f);
  put(0.0f, 0.13f, 1.0f, 0.78f, 0.25f, 0.95f);
  put(0.0f, -0.04f, 1.0f, 0.78f, 0.25f, 0.95f);
  put(0.08f, -0.09f, 1.0f, 0.78f, 0.25f, 0.95f);
  const int triVerts = static_cast<int>(v.size() / 6);
  for (size_t i = 0; i + 4 < minimap.lines.size(); i += 5) {
    const float a = 0.85f * minimap.lines[i + 4];
    put(minimap.lines[i], minimap.lines[i + 1], 0.86f, 0.92f, 0.86f, a);
    put(minimap.lines[i + 2], minimap.lines[i + 3], 0.86f, 0.92f, 0.86f, a);
  }
  for (int i = 0; i < kSides; i++) {
    const float a0 = 6.2831853f * i / kSides, a1 = 6.2831853f * (i + 1) / kSides;
    put(std::cos(a0) * 0.99f, std::sin(a0) * 0.99f, 0.86f, 0.92f, 0.86f, 0.5f);
    put(std::cos(a1) * 0.99f, std::sin(a1) * 0.99f, 0.86f, 0.92f, 0.86f, 0.5f);
  }
  const int lineVerts = static_cast<int>(v.size() / 6) - triVerts;
  // its square at the bottom left of the game's HUD, mirroring the gauge at the bottom right in every format
  re4mm::Rect hud, pic;
  re4mm::PictureRects(format, width, height, static_cast<int>(xh), &hud, &pic, wide);
  const re4mm::Rect at = re4mm::Place(hud, re4mm::Shown(format, hud, pic));
  const int size = at.w;
  out->verts = v;
  out->parts = {{false, 0, discVerts}, {true, triVerts, lineVerts}, {false, discVerts, triVerts - discVerts}};
  out->x = at.x;
  out->y = at.y;
  out->size = size;
  out->lineWidth = std::max(1.0f, static_cast<float>(size) / 160.0f);
  // once, and again when the place changes (another picture format, the screen turned or resized)
  const long long key = (static_cast<long long>(format + 1) << 48) ^ (static_cast<long long>(at.x) << 32) ^
                        (static_cast<long long>(at.y) << 16) ^ size;
  if (!minimap.logged || key != minimap.placeLogged) {
    minimap.logged = true;
    minimap.placeLogged = key;
    LOG("minimap: drawn (%d x %d at %d %d, picture format %d)", size, size, at.x, at.y, format);
  }
  return true;
}

// The minimap with GLES (after the picture, at its bottom left)
void minimapDraw(int format, unsigned xh, bool wide) {
  re4gx::GxOverlay o;
  if (!minimapBuild(format, xh, wide, &o))
    return;
  if (!minimap.program && !minimapProgram()) {
    minimap.failed = true;
    LOG("minimap: no shader; left out");
    return;
  }
  const std::vector<float> &v = o.verts;
  const int discVerts = o.parts[0].count, triVerts = o.parts[1].first, lineVerts = o.parts[1].count;
  const int size = o.size;
  const re4mm::Rect at{o.x, o.y, o.size, o.size};
  glViewport(at.x, at.y, size, size);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glUseProgram(minimap.program);
  glBindVertexArray(minimap.vao);
  glBindBuffer(GL_ARRAY_BUFFER, minimap.vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(v.size() * sizeof(float)), v.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, discVerts);
  glLineWidth(std::max(1.0f, static_cast<float>(size) / 160.0f));
  glDrawArrays(GL_LINES, triVerts, lineVerts);
  glDrawArrays(GL_TRIANGLES, discVerts, triVerts - discVerts);
  glLineWidth(1.0f);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  glDisable(GL_BLEND);
  glViewport(0, 0, width, height);
}

// Runs the chunks the core has finished and shows the latest display copy.
// False while there is nothing to show yet (the caller draws the bridge).
// The benchmark's level on the renderer (no in-between picture across the change); each level gets its own chance
// with in-between pictures (a GPU too slow at 4x may keep up at 2x).
void levelBenchSet(int halves) {
  if (smoothPrev) {
    api.gxRelease(smoothPrev);
    smoothPrev = nullptr;
  }
  if (gx) gx->RescaleHalves(halves);
  infoScale = halves;
  levelBench.level = halves;
  smoothTooSlow = false;
  // a pause of the in-between pictures from the level before does not carry over (60 FPS pass)
  smoothPacer.Restart(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The benchmark's results and the way back.
void levelBenchResults(std::chrono::steady_clock::time_point now) {
  LevelBench &b = levelBench;
  const re4gx::BenchSteps &s = b.steps;
  b.res30 = s.Result(0);
  b.res60 = s.Result(1);
  b.held = (s.Held(0) ? 1 : 0) | (s.Held(1) ? 2 : 0);
  api.cheatAction(22);
  b.phase = 3;
  b.since = now;
  LOG("benchmark: result %s with 30 pictures a second (%s), %s with 60 (%s)", re4gx::BenchSteps::Name(s.Result(0)).c_str(),
      s.Held(0) ? "stable" : "not stable even there", re4gx::BenchSteps::Name(s.Result(1)).c_str(),
      s.Held(1) ? "stable" : "not stable even there");
}

// The benchmark's next step, at every refresh (`chunk`: a real chunk of the game ran in it).
void levelBenchTick(std::chrono::steady_clock::time_point now, bool chunk) {
  LevelBench &b = levelBench;
  const int ask = b.ask.exchange(0);
  auto finish = [&](int phase) {
    b.phase = phase;
    b.active = false;
    optSmooth = b.keepSmooth;
    // the renderer anew with the menu's level, only when the benchmark changed it: a refused start (the title's logos,
    // an event) made a new renderer for nothing, which lost the title's EFB copies ("3 missing", the project owner's
    // log 06.11.2026)
    if (b.touched) optSerial++;
    LOG("benchmark: %s", phase == 4 ? "done" : phase == 5 ? "cancelled" : "stopped");
  };
  auto goBack = [&](const char *why) {
    api.cheatAction(22);
    b.phase = 6;
    b.since = now;
    LOG("benchmark: %s, back without a result", why);
  };
  if (ask == 1 && !b.active) {
    if (!api.cheatAction) {
      b.phase = -2;
      return;
    }
    b.run = 0;
    b.level = 0;
    b.res30 = b.res60 = b.rate10 = b.held = 0;
    b.chunks = 0;
    b.polls = 0;
    b.failed = false;
    b.touched = false;
    b.active = true;
    b.keepSmooth = optSmooth.load();
    b.phase = 1;
    b.since = b.start = now;
    api.cheatAction(20);
    LOG("benchmark: started (the lake's dock; a 30 and a 60 FPS pass, each from 4x down half a level every %.0f s)",
        re4gx::BenchSteps::kHold);
    return;
  }
  if (!b.active) return;
  if (chunk) b.chunks++;
  if (ask == 2 && b.phase.load() != 6 && b.phase.load() != 3) {
    goBack("cancelled");
    return;
  }
  const double inPhase = std::chrono::duration<double>(now - b.since).count();
  const double inAll = std::chrono::duration<double>(now - b.start).count();
  if ((b.phase.load() == 1 || b.phase.load() == 2) && inAll > kBenchLimit) {
    b.failed = true;
    goBack("the time limit is up");
    return;
  }
  // the game's state (a call to the core; not every refresh)
  int state = 0;
  if (b.phase.load() != 2 || (++b.polls % 30) == 0) state = api.cheatAction(23);
  switch (b.phase.load()) {
  case 1: {  // going to the place
    if (state < 0) {
      finish(state == -1 || state == -4 ? state : -2);
      return;
    }
    if ((state & 15) == 2) {
      b.run = 0;
      b.touched = true;
      optSmooth = 0;
      b.steps.Start(std::chrono::duration<double>(now - b.start).count());
      levelBenchSet(b.steps.Level());
      b.phase = 2;
      b.secondAt = now;
      b.chunksAt = b.chunks;
      b.shownAt = picturesMade.load();
      b.droppedAt = gxBetweenDropped;
      LOG("benchmark: at the place; the 30 FPS pass from %s", re4gx::BenchSteps::Name(b.steps.Level()).c_str());
    } else if (inPhase > 60.0) {
      b.failed = true;
      goBack("the place was not ready in 60 s");
    }
    return;
  }
  case 2: {  // measuring
    if (state < 0 || (b.polls % 30 == 0 && (state & 15) != 2)) {
      b.failed = true;
      goBack("the place was left");
      return;
    }
    const double sec = std::chrono::duration<double>(now - b.secondAt).count();
    if (sec < 1.0) return;
    const int c = static_cast<int>(std::lround((b.chunks - b.chunksAt) / sec));
    const int sh = static_cast<int>(std::lround((picturesMade.load() - b.shownAt) / sec));
    const unsigned dropped = gxBetweenDropped - b.droppedAt;
    b.chunksAt = b.chunks;
    b.shownAt = picturesMade.load();
    b.droppedAt = gxBetweenDropped;
    b.secondAt = now;
    std::string line;
    const re4gx::BenchSteps::Step step =
        b.steps.Second(std::chrono::duration<double>(now - b.start).count(), c, sh, dropped, &line);
    b.rate10 = b.steps.Rate10();
    if (step != re4gx::BenchSteps::kNone) {
      LOG("%s", line.c_str());
      // the cores' clocks at each step: a device that grows hot (the clock it may reach falls) or switches its
      // fastest core off shows in the report (the project owner's log 06.11.2026: the render thread at 1286 MHz)
      char clocks[320];
      re4cpu::DescribeClocks(re4cpu::GetLayout(), clocks, sizeof(clocks));
      char may[96];
      re4cpu::AffinityText(may, sizeof(may));
      LOG("benchmark: %s; the render thread on core %d, may use %s", clocks, sched_getcpu(), may);
    }
    switch (step) {
    case re4gx::BenchSteps::kLevel:
      levelBenchSet(b.steps.Level());
      LOG("benchmark: %d FPS pass, now %s", b.steps.Pass() ? 60 : 30, re4gx::BenchSteps::Name(b.steps.Level()).c_str());
      break;
    case re4gx::BenchSteps::kPassDone:
      b.run = 1;
      optSmooth = 1;
      levelBenchSet(b.steps.Level());
      LOG("benchmark: 30 FPS pass: %s (%s); the 60 FPS pass from %s", re4gx::BenchSteps::Name(b.steps.Result(0)).c_str(),
          b.steps.Held(0) ? "stable" : "not stable even there", re4gx::BenchSteps::Name(b.steps.Level()).c_str());
      break;
    case re4gx::BenchSteps::kDone:
      levelBenchResults(now);
      break;
    default:
      break;
    }
    return;
  }
  case 3:
  case 6:  // going back
    if ((state & 15) == 4 || state < 0 || inPhase > 90.0)
      finish(b.phase.load() == 3 ? 4 : b.failed ? -3 : 5);
    return;
  }
}

bool drawGx() {
  if (gxMode < 0) {
    const char *view = getenv("RE4_VIEW");
    gxMode = (view && std::string(view) == "bridge") || !api.gxAcquire ? 0 : 1;
    LOG("view: %s", gxMode ? "GX" : "bridge");
  }
  if (gxMode == 0) return false;
  // the renderer before new picture options: it hands its EFB copies to the new one, then goes
  std::unique_ptr<re4gx::GxRenderer> previous;
  if (gx && optSerial.load() != optApplied && !corePaused.load()) {
    // new picture options: the renderer anew, the core sends everything again (not the EFB copies:
    // those the game made once, at a room's start, come from the renderer before)
    previous = std::move(gx);
    smoothDrop();
    if (api.gxDetach) api.gxDetach();
  }
  if (!gx) {
    optApplied = optSerial.load();
    smoothTooSlow = false;
    // a new renderer counts from 0: the log's figures per chunk start over too
    gxWork = re4gx::GxStats();
    gxDraws = 0;
    gxRunMs = gxRunMax = 0;
    gxRunCount = gxFrames = 0;
    gxRunCpuNs = 0;
    gxRunOnKind[0] = gxRunOnKind[1] = gxRunOnKind[2] = 0;
    gxSince = std::chrono::steady_clock::now();
    // in halves (2: 1x, 3: 1.5x ... 8: 4x); RE4_GX_SCALE (tests) as a multiple, "1.5" too
    int scale = optScale.load();
    if (scale < 2 || scale > 8) {
      const char *scaleText = getenv("RE4_GX_SCALE");
      scale = scaleText ? static_cast<int>(std::lround(std::atof(scaleText) * 2.0)) : 0;
    }
    autoLevelOn = scale < 2 || scale > 8;
    if (scale < 2 || scale > 8) {
      // automatic: one level below the one whose 336 picture rows come closest to the screen's
      // rows (wish of the project owner, 19.10.2026: the Retroid's 1080 rows take 2, not 3;
      // CoreMenu.autoScale is the same)
      const int rows = std::min(height, width * 9 / 16);
      scale = 2 * std::max(1, std::min(3, (rows + 168) / 336) - 1);
      // measured by the benchmark on this device: its level for 30 or 60 pictures a second and half a level more
      // (wish of the project owner, 06.11.2026: the rainy dock is one of the heaviest places of the game; Dynamic
      // starts there and goes down half a level where the GPU falls behind, up again where it keeps up), 4x at most
      const int cap = optSmooth.load() ? optCap60.load() : optCap30.load();
      if (cap >= 2 && cap <= 8) {
        scale = std::min(8, cap + 1);
        LOG("dynamic resolution: up to %sx, half a level above the benchmark's %sx for %d pictures a second",
            ScaleName(scale).c_str(), ScaleName(cap).c_str(), optSmooth.load() ? 60 : 30);
      }
    }
    // once per run: does this GPU compute the movie colours like the GameCube (gx_selfcheck.cpp;
    // the Galaxy A36 showed them green and brown)? Before any renderer, on a renderer of its own;
    // on wrong pixels it picks a way of writing the TEV's lerp that this GPU computes right.
    static bool gpuChecked = false;
    if (!gpuChecked && !(gxVulkan() && !vulkanFailed)) {
      gpuChecked = true;
      LOG("%s", re4gx::MovieColourCheck(nullptr).c_str());
    }
    if (levelBench.active && levelBench.phase.load() == 2) {
      scale = levelBench.level.load();
      autoLevelOn = false;
      LOG("benchmark: the new renderer at the benchmark's level %s", re4gx::BenchSteps::Name(scale).c_str());
    }
    replayGx(scale);
    const bool vulkan = gxVulkan() && !vulkanFailed;
    if (vulkan && !vkWindow)
      return false;  // the Vulkan surface's window has not come yet
    gx = std::make_unique<re4gx::GxRenderer>(vulkan ? re4gx::GxApi::kVulkan : re4gx::GxApi::kGl);
    if (vulkan && gx->Api() != re4gx::GxApi::kVulkan) {
      // a build without the Vulkan renderer: the surface has no GL context, CoreActivity goes over to GLES
      LOG("GX renderer (Vulkan): not in this build; over to OpenGL ES");
      {
        std::lock_guard<std::mutex> lock(vulkanProblemLock);
        vulkanProblem = "this build has no Vulkan renderer";
      }
      vulkanFailed = true;
      gx.reset();
      if (previous) previous->Release();
      return false;
    }
    if (vulkan)
      gx->SetWindow(vkWindow, width, height);
    re4gx::GxOptions options;
    options.samples = std::max(1, optSamples.load());
    options.anisotropy = static_cast<float>(std::max(1, optAnisotropy.load()));
    if (optInvalidate.load() < 0) {
      const char *invalidate = getenv("RE4_GX_INVALIDATE");
      optInvalidate = invalidate && *invalidate == '1' ? 1 : 0;
    }
    options.invalidate = optInvalidate.load() != 0;
    options.tiledMsaa = optTiled.load() != 0;
    options.halfDof = optHalfDof.load() != 0;
    const char *ringEnv = getenv("RE4_GX_VERTEX_RING");
    options.vertexRing = optVertexRing.load() != 0 || (ringEnv && *ringEnv == '1');
    gx->SetOptions(options);
    // the depth of the pictures whose chunks peeked (GXPeekZ), to the core: the glow of torches stays
    // behind walls (a GameCube; before 26.10.2026 it shone through them)
    if (api.depth && !getenv("RE4_GX_NO_DEPTH_MAP"))
      gx->SetDepthSink([](const std::uint32_t *cells, std::uint32_t serial) { api.depth(cells, serial); });
    std::string error;
    if (!gx->InitHalves(scale, &error)) {
      if (vulkan) {
        // Vulkan could not start: CoreActivity hears it (rendererProblem) and goes over to GLES
        LOG("GX renderer (Vulkan): %s; over to OpenGL ES", error.c_str());
        {
          std::lock_guard<std::mutex> lock(vulkanProblemLock);
          vulkanProblem = error.empty() ? "Vulkan could not start" : error;
        }
        vulkanFailed = true;
        gx.reset();
        if (previous) previous->Release();
        return false;
      }
      LOG("GX renderer: %s; the bridge draws", error.c_str());
      gx.reset();
      if (previous) previous->Release();
      gxMode = 0;
      return false;
    }
    if (previous) {
      previous->HandOverCopies(*gx);
      previous->Release();
      previous.reset();
      if (gx->Stats().handedCopies) LOG("GX: %u EFB copies taken over from the renderer before", gx->Stats().handedCopies);
    }
    // linked shader programs from earlier runs (RE4_GX_PROGRAM_CACHE, set by CoreActivity)
    const char *cache = getenv("RE4_GX_PROGRAM_CACHE");
    bool cached = false;
    if (cache && *cache) {
      mkdir(cache, 0775);
      cached = gx->SetProgramCache(cache);
    }
    // the app's list of programs (its asset gx-programs.keys, CoreActivity)
    const char *programList = getenv("RE4_GX_PROGRAM_LIST");
    if (cached && programList && *programList) gx->SetProgramList(programList);
    // (GL's programs on a thread of its own; the Vulkan renderer keeps its SPIR-V and pipelines itself)
    if (cached && !vulkan)
      startWarm(cache, programList ? programList : "", gx->Samples() > 1 && options.centroid ? 1u : 0u);
    int maxSamples = 1;
    float maxAnisotropy = 1.0f;
    if (vulkan) {
      maxSamples = 4;  // every Vulkan device draws and samples 4 (the renderer takes fewer where it must)
      maxAnisotropy = gx->Anisotropy();
    } else
      re4gx::GxRenderer::Limits(&maxSamples, &maxAnisotropy);
    LOG("GX renderer: %s", gx->ApiInfo().c_str());
    {
      std::lock_guard<std::mutex> lock(vulkanProblemLock);
      rendererInfoText = gx->ApiInfo();
    }
    infoMaxSamples = maxSamples;
    infoMaxAnisotropy = static_cast<int>(maxAnisotropy);
    infoScale = scale;
    infoSamples = gx->Samples();
    infoAnisotropy = static_cast<int>(gx->Anisotropy());
    LOG("GX renderer ready, EFB scale %s, %d samples (of %d)%s, anisotropy %.0f (of %.0f), depth of field's blur "
        "at %s resolution, program cache %s",
        ScaleName(scale).c_str(), gx->Samples(), maxSamples,
        gx->Samples() > 1 ? (gx->TiledMsaa() ? " on the tiles" : " in own buffers") : "", gx->Anisotropy(),
        maxAnisotropy, gx->HalfDof() ? "half" : "full", cached ? cache : "off");
    char layout[256];
    re4cpu::GetLayout().Describe(layout, sizeof(layout));
    LOG("processor: %s", layout);
    const int tid = re4cpu::ThreadId();
    if (renderPlaced != tid) {
      // a new render thread: placed by the menu's setting at its next run
      renderPlaced = tid;
      renderFast = -1;
    }
    if (autoLevelOn) {
      // in half steps down to 1x (wish of the project owner, 27.10.2026: 2x straight to 1x lost too much)
      autoLevel.Start(scale, std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                      2, true);
      LOG("dynamic resolution: from level %sx (the screen's), half a level lower while the GPU falls behind",
          ScaleName(scale).c_str());
    }
  }
  // one chunk per displayed frame: every picture the game makes is shown (a
  // renderer slower than the game slows the game down, the core waits for it,
  // instead of skipping pictures)
  const auto now = std::chrono::steady_clock::now();
  const long long cpuNow = re4cpu::ThreadNs();
  if (gxSince.time_since_epoch().count() == 0) gxSince = now;
  gxFrames++;
  // diagnosis (RE4_GX_DUMP_DIR, RE4_GX_DUMP_EVERY seconds, at most 6): the
  // renderer detaches, so the next chunk starts over with the full state and
  // every texture, and that chunk goes to a file (re4_gx_frame_io.h, drawn in
  // the cloud with gx_chunk_render)
  if (gxDumpEvery < 0) {
    const char *dir = getenv("RE4_GX_DUMP_DIR");
    const char *every = getenv("RE4_GX_DUMP_EVERY");
    gxDumpDir = dir ? dir : "";
    gxDumpEvery = gxDumpDir.empty() ? 0 : (every ? std::max(5, std::atoi(every)) : 20);
    if (gxDumpEvery) {
      mkdir(gxDumpDir.c_str(), 0775);
      gxDumpNext = now + std::chrono::seconds(gxDumpEvery);
      LOG("GX dumps every %d s into %s", gxDumpEvery, gxDumpDir.c_str());
    }
  }
  if (gxDumpEvery && !gxDumpWanted && !gxDumpSeriesLeft && gxDumpCount < 6 && now >= gxDumpNext) {
    smoothDrop();
    api.gxDetach();
    gxDumpWanted = true;
    gxDumpNext = now + std::chrono::seconds(gxDumpEvery);
  }
  if (!gxDumpWanted && !gxDumpSeriesLeft && gxDumpCount < 40) {
    std::lock_guard<std::mutex> lock(gxDumpLock);
    if (gxDumpAsked) {
      gxDumpAsked--;
      if (gxDumpDir.empty()) {
        gxDumpDir = gxDumpAskedDir;
        mkdir(gxDumpDir.c_str(), 0775);
      }
      if (!gxDumpDir.empty()) {
        smoothDrop();
        api.gxDetach();
        gxDumpWanted = true;
        gxDumpSeriesWanted = true;
        LOG("GX dump asked for (controller)");
        // where the player stands, for checks on the PC (RE4_TEST_PLACE)
        for (const Re4CoreModel &mdl : frame.models) {
          if (mdl.manager == 0 && mdl.id == 0) {
            LOG("R3: room 0x%03x, player at %.0f %.0f %.0f, camera %.0f %.0f %.0f -> %.0f %.0f %.0f", frame.room,
                mdl.matrix[3], mdl.matrix[7], mdl.matrix[11], frame.eye[0], frame.eye[1], frame.eye[2],
                frame.target[0], frame.target[1], frame.target[2]);
            break;
          }
        }
      }
    }
  }
  // a hit on the player: the settings
  if (gxHitDumps < 0) {
    const char *hits = getenv("RE4_GX_DUMP_ON_HIT");
    const char *folder = getenv("RE4_GX_DUMP_FOLDER");
    gxHitDumps = hits && folder && *folder ? std::max(0, std::atoi(hits)) : 0;
    if (folder && *folder && gxDumpDir.empty()) {
      gxDumpDir = folder;
      mkdir(gxDumpDir.c_str(), 0775);
    }
  }
  // 60 FPS (experimental): records the draws while on; a chunk whose in-between picture is on
  // the screen runs now (it was taken, dumped and counted at the refresh before)
  const bool smoothOn = optSmooth.load() != 0 && !smoothTooSlow;
  gx->SetRecording(smoothOn);
  gx->SetFxaa(optFxaa.load() != 0);
  static const int sharpenEnv = getenv("RE4_GX_SHARPEN") ? atoi(getenv("RE4_GX_SHARPEN")) : 0;
  gx->SetSharpen(sharpenEnv > 0 ? sharpenEnv : optSharpen.load());
  static const bool tvEnv = getenv("RE4_GX_TV_FILTER") && *getenv("RE4_GX_TV_FILTER") == '1';
  gx->SetTvFilter(optTvFilter.load() != 0 || tvEnv);
  static const bool psxEnv = getenv("RE4_GX_PSX") && *getenv("RE4_GX_PSX") == '1';
  gx->SetPsx(optPsx.load() != 0 || psxEnv);
  static const bool flaresEnv = getenv("RE4_GX_FLARES") && *getenv("RE4_GX_FLARES") == '1';
  gx->SetFlares(optFlares.load() != 0 || flaresEnv);
  gx->SetDofStrong(optDof.load() == 2);
  static const bool mergeEnv = getenv("RE4_GX_MERGE") && *getenv("RE4_GX_MERGE") == '1';
  gx->SetMerge(optMerge.load() != 0 || mergeEnv);
  if (!smoothOn && smoothPrev) {
    api.gxRelease(smoothPrev);
    smoothPrev = nullptr;
  }
  // the room the game is in: Prewarm takes the programs this room drew with before (01.11.2026)
  gx->SetRoom(frame.room);
  // a break for Dynamic and the 60 FPS pacer: a room starts (its loading, its programs made ahead) or the core ran
  // again after a pause (the app menu); their windows start anew after a few quiet seconds (the project owner's log
  // 06.11.2026: 2x -> 1.5x 1.6 s after r11b started, the window before had spanned the menu)
  {
    static int breakRoom = -1;
    static bool breakPaused = false;
    const bool paused = corePaused.load();
    const bool roomStart = breakRoom >= 0 && static_cast<int>(frame.room) != breakRoom;
    if (roomStart || (breakPaused && !paused)) {
      const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      autoLevel.Restart(t);
      if (!levelBench.active) smoothPacer.Restart(t, re4gx::AutoLevel::kQuiet);
    }
    breakRoom = static_cast<int>(frame.room);
    breakPaused = paused;
  }
  const Re4GxChunk *deferred = smoothNext;
  smoothNext = nullptr;
  const Re4GxChunk *chunk = deferred ? deferred : skipNext ? skipNext : api.gxAcquire();
  if (chunk == skipNext)
    skipNext = nullptr;
  // Behind: the core has finished the next chunk already. Then the in-between picture (60 FPS) is
  // left out first: it is a picture more for the screen, and drawing it while behind made the game
  // wait for the renderer (Retroid 26.10.2026: 17-19, with the stalls of the blended vertices 3-5
  // game pictures a second). With Frame skip the chunk is also not drawn (its state and textures are
  // applied, GxRenderer::Skip) and the next one drawn at once: the game keeps its pace instead of
  // waiting for a slow GPU. Not while a graphics capture is on its way.
  bool behind = false;
  if (chunk && !deferred && (optFrameSkip.load() != 0 || smoothOn) && !gxDumpWanted && !gxDumpSeriesLeft &&
      !gxHitShotsLeft && !gxHitSeriesPending) {
    if (const Re4GxChunk *next = api.gxAcquire()) {
      behind = true;
      // (Frame skip and 60 FPS are one or the other, CoreMenu; with both set, 60 FPS wins)
      if (optFrameSkip.load() != 0 && !smoothOn && gx->CanSkip(*chunk, *next)) {
        gx->Skip(*chunk);
        api.gxRelease(chunk);
        chunk = next;
        gxSkipped++;
        if (smoothPrev) {
          api.gxRelease(smoothPrev);  // no in-between picture across the chunk left out
          smoothPrev = nullptr;
        }
        if (gxSkipped == 1 || gxSkipped % 500 == 0)
          LOG("frame skip: %u chunks left out so far (the renderer was behind)", gxSkipped);
      } else {
        skipNext = next;  // drawn at the next refresh
      }
    }
  }
  if (chunk) {
    char path[512];
    path[0] = 0;
    char seriesShot[512];
    seriesShot[0] = 0;
    if (!deferred) {
      if (chunk->flags & RE4_GX_CHUNK_MARK) {
        LOG("GX: a hit on the player marked (chunk %u)", gxChunks);
        gxHitCount++;
        if (gxHitDumps > 0) {
          gxHitDumps--;
          gxHitSeriesPending = true;
        }
        if (!gxDumpDir.empty()) {
          gxHitShotsLeft = kGxHitShots;  // from this picture on
          gxHitShotPart = 0;
        }
      } else if (gxHitSeriesPending && (chunk->flags & RE4_GX_CHUNK_RESET)) {
        gxHitSeriesPending = false;
        if (!gxDumpSeriesLeft && !gxDumpWanted && !gxDumpDir.empty() && gxDumpCount < 40) {
          // this chunk starts over: a series from here (continued below)
          gxDumpSeriesIndex = gxDumpCount++;
          gxDumpSeriesPart = 0;
          gxDumpSeriesLeft = kGxHitSeries;
          gxDumpSeriesSequence = chunk->sequence - 1;
        }
      }
      if (gxDumpWanted && (chunk->flags & RE4_GX_CHUNK_RESET)) {
        gxDumpWanted = false;
        if (gxDumpSeriesWanted) {
          gxDumpSeriesWanted = false;
          gxDumpSeriesIndex = gxDumpCount++;
          gxDumpSeriesPart = 0;
          gxDumpSeriesLeft = kGxDumpSeries;
        } else {
          std::snprintf(path, sizeof(path), "%s/gx-geraet-%02u.bin", gxDumpDir.c_str(), gxDumpCount++);
        }
      } else if (gxDumpSeriesLeft && chunk->sequence != gxDumpSeriesSequence + 1) {
        LOG("GX dump series %02u ends after %u chunks (chunk sequence %u after %u)", gxDumpSeriesIndex,
            gxDumpSeriesPart, chunk->sequence, gxDumpSeriesSequence);
        gxDumpSeriesLeft = 0;
      }
      // a series also keeps each picture as this GPU drew it (gx-geraet-NN-KK.bmp):
      // flicker the cloud renderer does not show is then this device's (FILES, M9)
      if (gxDumpSeriesLeft) {
        std::snprintf(seriesShot, sizeof(seriesShot), "%s/gx-geraet-%02u-%02u.bmp", gxDumpDir.c_str(),
                      gxDumpSeriesIndex, gxDumpSeriesPart);
        std::snprintf(path, sizeof(path), "%s/gx-geraet-%02u-%02u.bin", gxDumpDir.c_str(), gxDumpSeriesIndex,
                      gxDumpSeriesPart++);
        gxDumpSeriesSequence = chunk->sequence;
        gxDumpSeriesLeft--;
      }
      if (path[0]) {
        if (FILE *f = std::fopen(path, "wb")) {
          const bool ok = re4gxio::Write(f, *chunk, gxChunks);
          std::fclose(f);
          LOG("GX dump %s (%u draws, chunk %u)%s", path, chunk->draws, gxChunks, ok ? "" : ": write failed");
        }
      }
    }
    // the in-between picture first when the switch is on and nothing records this chunk's picture;
    // Run(chunk) then at the next refresh
    bool between = false;
    // the game's own pace: 60 chunks a second (its movies and the title follow the screen, 60 Hz)
    // leave no room for in-between pictures; the next chunk is always there already, which is not
    // the renderer behind (Retroid 28.10.2026: in the opening movie every chunk counted as left out,
    // 60 FPS paused again and again and the dynamic resolution went from 2x to 1x)
    // (each chunk once: one with an in-between picture comes here again for its own picture at the
    // next refresh, `deferred`; counted twice, 30 game pictures a second looked like 60 and every
    // second in-between picture was left out: Retroid 28.10.2026, 45 pictures a second)
    const double chunkAt = std::chrono::duration<double>(now.time_since_epoch()).count();
    if (!deferred)
      chunkRate.Note(chunkAt);
    const bool stream60 = chunkRate.Sixty(chunkAt);
    if (smoothOn && !deferred && smoothPrev && behind && !stream60)
      gxBetweenDropped++;  // behind: this chunk at once, without the picture between
    // an even pace (60fps-plan.md S4): behind again and again, the in-between pictures pause
    bool paced = !stream60;
    // (not while the benchmark measures: its 5 s pause ate half of a level's window, the project owner's log
    // 06.11.2026; there every in-between picture the renderer manages counts, the left-out ones too)
    if (smoothOn && !deferred && smoothPrev && !stream60 && !levelBench.active) {
      std::string why;
      paced = smoothPacer.Chunk(std::chrono::duration<double>(now.time_since_epoch()).count(), behind, &why);
      if (!why.empty())
        LOG("%s", why.c_str());
    }
    if (smoothOn && !deferred && smoothPrev && !behind && paced && !path[0] && !seriesShot[0] && !gxHitShotsLeft) {
      between = gx->Interpolate(*smoothPrev, *chunk, 0.5f);
      if (between) {
        api.gxRelease(smoothPrev);
        smoothPrev = nullptr;
        smoothNext = chunk;
        picturesMade++;
      }
    }
    if (!between) {
      gx->Run(*chunk);
      picturesMade++;
      if (smoothPrev) {
        api.gxRelease(smoothPrev);
        smoothPrev = nullptr;
      }
      // kept while the switch is on: the in-between picture after it reads its vertices
      if (smoothOn)
        smoothPrev = chunk;
      else
        api.gxRelease(chunk);
    }
    if (seriesShot[0]) {
      SaveShot(seriesShot);
    }
    if (gxHitShotsLeft) {
      char shot[512];
      std::snprintf(shot, sizeof(shot), "%s/treffer-%02u-%02u.bmp", gxDumpDir.c_str(), gxHitCount,
                    gxHitShotPart++);
      SaveShot(shot);
      gxHitShotsLeft--;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - now).count();
    renderHint.Report(static_cast<long long>(ms * 1e6));
    // the menu's "Force Performance": to the core (the game thread) and for this thread
    const int fast = optFastCores.load();
    if (api.setFastCores && fast != fastSent) {
      api.setFastCores(fast);
      fastSent = fast;
    }
    const int potato = optPotato.load();
    // (while the GPU bench runs its steps set the Potato mode; afterwards the menu's again)
    if (api.setPotato && potato != potatoSent && bench.index < 0) {
      api.setPotato(potato);
      potatoSent = potato;
    }
    const int aim = optAim.load() | (optFarCamera.load() ? 8 : 0)  // bit 3 "Far camera" (patch 0171)
                    | (optFov.load() + 32) << 24;                     // bits 24.. "Field of view" (patch 0205)
    if (api.setAim && aim != aimSent) {
      api.setAim(aim);
      aimSent = aim;
    }
    const int sound = optSound.load();
    if (api.setSoundVolume && sound != soundSent) {
      api.setSoundVolume(sound);
      soundSent = sound;
    }
    if (fast != renderFast) {
      const bool first = renderFast < 0;
      renderFast = fast;
      if (fast) {
        char cores[160];
        re4cpu::KeepOffSlowCores(cores, sizeof(cores), re4cpu::kRenderThread);
        LOG("cores: the render thread %s", cores);
        const int tid = re4cpu::ThreadId();
        const char *why = "";
        renderHint.Open(&tid, 1, 33333333LL, &why);
        LOG("performance hint: %s (the render thread, 33.3 ms a picture)", why);
      } else {
        renderHint.Close();
        if (!first) re4cpu::ReleaseCores();
        LOG("cores: the render thread left to Android (Force Performance: Default)");
      }
    }
    // about once a second: back on the fast cores if Android moved the thread off them
    static unsigned coreChecks, coreMoves;
    if (fast && ++coreChecks % 30 == 0 && re4cpu::KeepOnFastCores() && ++coreMoves <= 4)
      LOG("cores: the render thread put back on its cores (Android had moved it, %u times so far)", coreMoves);
    gxRunMs += ms;
    // a Run this long is a stall (programs made, textures in), not a GPU too slow: a break as above
    if (ms > 250.0) {
      const double t = std::chrono::duration<double>(now.time_since_epoch()).count();
      autoLevel.Restart(t);
      if (!levelBench.active) smoothPacer.Restart(t, re4gx::AutoLevel::kQuiet);
    }
    gxRunMax = std::max(gxRunMax, ms);
    gxRunCount++;
    // the title runs 60 chunks a second, so no refresh comes without one: the programs ahead after it
    if (ms + 1.0 < kPrewarmMenuMs && gameMenuNow()) gx->Prewarm(kPrewarmMenuMs - ms);
    const long long cpuNs = re4cpu::ThreadNs() - cpuNow;
    gxRunCpuNs += cpuNs;
    if (cpuNow)  // (0: no thread clock on this device, no waiting known)
      gxRunWaitMs += std::max(0.0, ms - cpuNs / 1e6);
    gxRunOnKind[re4cpu::GetLayout().Kind(sched_getcpu())]++;
    if (bench.measuring) {
      bench.runMs += ms;
      bench.runCount++;
    }
    levelBenchTick(now, !between);
    // the FPS display's level: the one the renderer really draws at (the benchmark's or Dynamic's steps, a new renderer)
    infoScale = gx->Halves();
    // Dynamic: another level from the next chunk on (not while an R3 series or the hit pictures are
    // recorded, the app's menu is open or the GPU measurement runs; never while the benchmark runs: it would set
    // its own level over the benchmark's)
    if (autoLevelOn && !between && !levelBench.active) {
      const auto &st = gx->Stats();
      std::string why;
      const int level = autoLevel.Chunk(std::chrono::duration<double>(now.time_since_epoch()).count(), st.behindSum,
                                        st.behindChecks,
                                        !corePaused.load() && !gxDumpSeriesLeft && !gxHitShotsLeft && !bench.measuring &&
                                            !levelBench.active,
                                        &why, gxBetweenDropped,  // (counts only while 60 FPS is on; never falls back)
                                        gxRunWaitMs);
      if (level != gx->Halves()) {
        LOG("%s", why.c_str());
        // no in-between picture across the change (a chunk taken ahead, skipNext, is not run yet:
        // it stays and is drawn at the next refresh; the core sends only what changed)
        if (smoothPrev) {
          api.gxRelease(smoothPrev);
          smoothPrev = nullptr;
        }
        gx->RescaleHalves(level);
        infoScale = level;
      }
    }
    if ((++gxChunks % 300) == 1) {
      const auto &s = gx->Stats();
      const double seconds = std::chrono::duration<double>(now - gxSince).count();
      LOG("GX: %u chunks, %u draws (%u per chunk), run %.1f ms (max %.1f), shown %.1f/s, %u programs, %u textures, "
          "%u copies, %u held pictures, %u missing, %u shader errors, %u inexact dst alpha, %u programs from the cache, "
          "%u ahead of their first draw, %u kept, %u deleted unused, base vertex %s, %u autofocus passes, %u EFB "
          "restores (MSAA on the tiles), %u skipped",
          s.chunks, s.draws, gxRunCount ? (s.draws - gxDraws) / gxRunCount : 0, gxRunCount ? gxRunMs / gxRunCount : 0.0,
          gxRunMax, seconds > 0 ? gxFrames / seconds : 0.0, s.programs, s.textures, s.textureCopies, s.heldCopies,
          s.missingTextures, s.shaderErrors, s.dstAlphaApprox, s.programsLoaded, s.programsAhead,
          s.programsLive, s.programsDeleted,
          gx->BaseVertex() ? "yes" : "no",
          s.autofocus, s.efbRestores, s.skippedChunks);
      // the GL work per chunk since the last line, and the GPU time when the driver measures it
      if (gxRunCount) {
        const double n = gxRunCount;
        const unsigned timed = s.gpuTimed - gxWork.gpuTimed;
        char gpu[32] = "not measured";
        if (timed)
          std::snprintf(gpu, sizeof(gpu), "%.1f ms", (s.gpuMicros - gxWork.gpuMicros) / 1000.0 / timed);
        LOG("GX work per chunk: %.0f uniform uploads (%.0f KB, %.0f the same not sent), %.0f program switches, %.0f texture binds, "
            "%.0f state changes, %u stream waits, %u stream switches (%u inside a chunk), %u vertex ring waits, GPU %s",
            (s.uniformUploads - gxWork.uniformUploads) / n, (s.uniformBytes - gxWork.uniformBytes) / n / 1024.0,
            (s.uniformSame - gxWork.uniformSame) / n,
            (s.programSwitches - gxWork.programSwitches) / n, (s.textureBinds - gxWork.textureBinds) / n,
            (s.stateChanges - gxWork.stateChanges) / n, s.streamWaits - gxWork.streamWaits,
            s.streamSwitches - gxWork.streamSwitches, s.streamSplits - gxWork.streamSplits,
            s.ringWaits - gxWork.ringWaits, gpu);
        // the render thread's processor time in Run: about the run time when the thread computes
        // (driver calls), less when it waits for the GPU; and the kind of core it got
        const int cpu = sched_getcpu();
        const unsigned khz = cpu >= 0 ? re4cpu::CurrentKHz(cpu) : 0;
        LOG("GX render thread: %.1f ms of processor time per chunk of %.1f ms run, on fast/middle/slow cores "
            "%.0f/%.0f/%.0f %%, now on core %d at %u MHz",
            gxRunCpuNs / 1e6 / n, gxRunMs / n, gxRunOnKind[2] * 100.0 / n, gxRunOnKind[1] * 100.0 / n,
            gxRunOnKind[0] * 100.0 / n, cpu, khz / 1000);
        // where the run time went (GxStats::splitNs): the driver's calls first, the renderer's own work after
        // (per real chunk; the in-between pictures' Runs of 60 FPS are in it as well)
        using re4gx::GxStats;
        double split[GxStats::kSplitKinds];
        for (int k = 0; k < GxStats::kSplitKinds; k++)
          split[k] = (s.splitNs[k] - gxWork.splitNs[k]) / 1e6 / n;
        LOG("GX render thread split per chunk: draw calls %.1f ms, uniform blocks %.1f, binds (programs, textures, "
            "vertex setup) %.1f, render state %.1f, vertex buffers %.1f; shader keys and programs %.1f, textures in "
            "(decode, upload) %.1f, BP (EFB copies, clears, effects) %.1f, XF %.1f, the rest %.1f",
            split[GxStats::kSplitDraw], split[GxStats::kSplitUniform], split[GxStats::kSplitBind],
            split[GxStats::kSplitState], split[GxStats::kSplitVertices], split[GxStats::kSplitProgram],
            split[GxStats::kSplitTexture], split[GxStats::kSplitBp], split[GxStats::kSplitXf],
            split[GxStats::kSplitRest]);
        // Vulkan: its waits, submits and presents per picture, the shaders and pipelines it made (vulkan-bauplan E8)
        const std::string vkLine = gx->TimingLine();
        if (!vkLine.empty())
          LOG("%s", vkLine.c_str());
        // merged draw calls (RE4_GX_MERGE=1, an experiment for low-end phones)
        if (gx->Merging())
          LOG("GX merged draw calls per chunk: %.0f GL draw calls for %.0f draws, %.0f of them in %.0f merged calls",
              (s.draws - gxWork.draws - (s.mergeTaken - gxWork.mergeTaken) + (s.mergeBatches - gxWork.mergeBatches)) / n,
              (s.draws - gxWork.draws) / n, (s.mergeTaken - gxWork.mergeTaken) / n,
              (s.mergeBatches - gxWork.mergeBatches) / n);
      }
      // 60 FPS: how far the GPU lags behind (fences after every Run, the in-between ones too); more
      // than 2.5 Runs on average: off for this renderer, the menu's switch stays (a lower
      // resolution helps)
      if (optSmooth.load() && !smoothTooSlow) {
        const unsigned checks = s.behindChecks - gxWork.behindChecks;
        const double behind = checks ? static_cast<double>(s.behindSum - gxWork.behindSum) / checks : 0.0;
        LOG("60 FPS: %u in-between pictures, %u cuts, %u of %u 3D draws matched so far, %u left out (the renderer "
            "behind), %u pauses; the GPU %.1f Runs behind",
            s.interpPictures, s.interpCuts, s.interpMatched, s.interpDraws, gxBetweenDropped, smoothPacer.Offs(), behind);
        // (60fps-plan.md S0) without a counterpart, by kind; found by their object, slots used anew;
        // texture matrices blended / left out at their edge, TEV and light colours blended
        LOG("60 FPS draws without a counterpart: particles %u of %u, figures %u of %u, room %u of %u, 2D %u of %u "
            "(%u moved with the camera); by their object %u (%u new in an old slot); texture matrices %u (%u over the "
            "edge), TEV colours %u, light colours %u",
            s.interpKindUnmatched[0], s.interpKind[0], s.interpKindUnmatched[1], s.interpKind[1],
            s.interpKindUnmatched[2], s.interpKind[2], s.interpKindUnmatched[3], s.interpKind[3], s.interpCameraMoved,
            s.interpOwned, s.interpOwnedNew, s.interpTexBlends, s.interpTexWraps, s.interpTevBlends,
            s.interpLightBlends);
        if (behind > 2.5 && !levelBench.active) {
          smoothTooSlow = true;
          LOG("60 FPS: the GPU does not keep up, in-between pictures off until the renderer starts anew");
        }
      }
      gxWork = s;
      gxDraws = s.draws;
      gxRunMs = gxRunMax = 0;
      gxRunCount = gxFrames = 0;
      gxRunCpuNs = 0;
      gxRunOnKind[0] = gxRunOnKind[1] = gxRunOnKind[2] = 0;
      gxSince = now;
    }
  } else {
    // a refresh without a new chunk (the game draws 30 pictures a second): the shader programs
    // this device made before, ahead of their first draw, a few milliseconds' worth at a time
    // (Retroid report 22.10.2026 23:42: chunks of up to 435 ms while r10b's effects made theirs)
    gx->Prewarm(gameMenuNow() ? kPrewarmMenuMs : kPrewarmMs);
    levelBenchTick(now, false);  // the benchmark's clock goes on without a chunk too
  }
  benchTick(now);
  unsigned xw = 0, xh = 0;
  if (!gx->Xfb(&xw, &xh)) return false;
  if (gx->Api() == re4gx::GxApi::kVulkan) {
    gx->SetWindow(vkWindow, width, height);  // (Present clears the screen itself)
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  // the menu's picture format (1: 4:3, 0: 16:9, 2: stretched, 3: 4:3 without the game's
  // letterbox bars while playing, Anbernic RG 477V report 22.10.2026, in an exact 4:3 box since
  // 29.10.2026; 4, the exact box's first own number, is 3; 5: "Native", the screen's own aspect,
  // 07.11.2026), else RE4_GX_ASPECT
  const char *aspect = getenv("RE4_GX_ASPECT");
  const int format = optAspect.load() == 4 ? 3 : optAspect.load();
  // "Native": the screen's aspect decides (re4mm::Native, minimap_view.h); a picture whose 3D the core drew wider
  // (GxRenderer::XfbWide) fills a screen wider than 16:9, any other goes into the 16:9 box
  const bool xfbWide = gx->XfbWide();
  const re4mm::NativeView native = re4mm::Native(width, height, xfbWide);
  const int noBars = format == 3 || (format == 5 && native.mode == -1) ? 1 : 0;
  if (api.setLetterbox && noBars != letterboxSent) {
    api.setLetterbox(noBars);
    letterboxSent = noBars;
  }
  const int aspect3d = format == 5 ? re4mm::NativeAspect3d(width, height) : 0;
  if (api.setViewAspect && aspect3d != viewAspectSent) {
    api.setViewAspect(aspect3d);
    viewAspectSent = aspect3d;
    if (format == 5)
      LOG("picture format Native: screen %dx%d (%.3f:1), %s", width, height,
          height ? static_cast<double>(width) / height : 0.0,
          native.mode == 1    ? "the 3D wider while playing, menus in the 16:9 box"
          : native.mode == -1 ? "no letterbox while playing, more rows above and below"
                              : "as 16:9");
  }
  // the game's life gauge in the bottom right corner of the shown part (patch 0215; the map in the bottom left
  // corner, minimapDraw): its rows and how the screen shows them, for every format
  {
    const re4mm::HudLayout hud =
        re4mm::Hud(format < 0 && aspect && std::string(aspect) == "4:3" ? 1 : format, width, height, native);
    if (api.setHudLayout && (hud.top != hudSent.top || hud.bottom != hudSent.bottom ||
                             hud.stretch != hudSent.stretch || hud.flags != hudSent.flags)) {
      api.setHudLayout(hud.top | hud.bottom << 16, hud.stretch, hud.flags);
      hudSent = hud;
      LOG("life gauge: bottom right corner of the rows %d..%d, %.4f wider across%s", hud.top, hud.bottom,
          hud.stretch / 10000.0, hud.flags & 1 ? ", drawn narrower" : "");
    }
  }
  // the rows shown: in the 16:9 formats (0, 2) the crop below leaves the game's letterbox bars out,
  // and texture copies repeat the shown edge rows instead of reading the bars (GxRenderer::SetShownRows)
  const bool crop = !(format == 3 || format == 1 || (format < 0 && aspect && std::string(aspect) == "4:3"));
  if (format == 5)
    gx->SetShownRows(native.top, native.top + native.rows);
  else
    gx->SetShownRows(crop ? 56 : 0, crop ? 392 : 1 << 16);
  // the game computes its 3D for 4:3 (camera.cpp) into 512 x 448 pixels that a television showed
  // as 10:7; in an exact 4:3 box the 3D is right and the flat draws (HUD, menus, text) are drawn
  // 15/14 wider by the renderer, so they keep the shape they had on the television
  gx->SetWiden(format == 3 ? 15.0f / 14.0f : 1.0f);
  // the minimap: the Vulkan renderer draws it over the picture in Present (GLES: minimapDraw after it)
  const int mapFormat = format < 0 && aspect && std::string(aspect) == "4:3" ? 1 : format;
  if (gx->Api() == re4gx::GxApi::kVulkan) {
    re4gx::GxOverlay map;
    if (!minimapBuild(mapFormat, xh, xfbWide, &map))
      map = re4gx::GxOverlay();  // (none this picture)
    gx->SetOverlay(map);
  }
  if (format == 5) {
    // "Native": the shown rows over the box (whole screen, or the 16:9 box for a menu, the scope, a movie)
    const unsigned top = xh * static_cast<unsigned>(native.top) / 448,
                   rows = xh * static_cast<unsigned>(native.rows) / 448;
    gx->Present(native.pic.x, native.pic.y, native.pic.w, native.pic.h, top, rows);
    notePicture(native.pic.x, native.pic.y, native.pic.w, native.pic.h, xh ? float(top) / float(xh) : 0.f,
                xh ? float(rows) / float(xh) : 1.f);
  } else if (format == 3) {
    // 4:3 without the bars: the picture's lines fill the height of an exact 4:3 box (wish of the
    // project owner, 29.10.2026: no bars, nothing cut off, the user interface in its shape; before,
    // a 10:7 box in the lines' own shape); a 4:3 screen is full
    int boxW = width, boxH = static_cast<int>(width * 3.0 / 4.0 + 0.5);
    if (boxH > height) {
      boxH = height;
      boxW = static_cast<int>(height * 4.0 / 3.0 + 0.5);
    }
    gx->Present((width - boxW) / 2, (height - boxH) / 2, boxW, boxH);
    // taps go by the flat layer (the sub screens): its 512 pixels span the box 15/14 times
    const int layerW = static_cast<int>(boxW * 15.0 / 14.0 + 0.5);
    notePicture((width - layerW) / 2, (height - boxH) / 2, layerW, boxH, 0.f, 1.f);
  } else if (format == 1 || (format < 0 && aspect && std::string(aspect) == "4:3")) {
    // the whole picture: 640 wide in a 4:3 frame of 480 lines (the XFB holds xh of them)
    int boxW = width, boxH = width * 3 / 4;
    if (boxH > height) {
      boxH = height;
      boxW = height * 4 / 3;
    }
    const int picH = boxH * static_cast<int>(xh) / 480;
    gx->Present((width - boxW) / 2, (height - picH) / 2, boxW, picH);
    notePicture((width - boxW) / 2, (height - picH) / 2, boxW, picH, 0.f, 1.f);
  } else {
    // full screen: the game letterboxes everything (rows 56..391 of its 448,
    // on a 4:3 television exactly 16:9); those rows fill a 16:9 box, or with
    // "Stretched" (2, wish of the project owner, 22.10.2026) the whole screen
    const unsigned top = xh * 56 / 448, rows = xh * 336 / 448;
    int boxW = width, boxH = width * 9 / 16;
    if (format == 2) {
      boxH = height;
    } else if (boxH > height) {
      boxH = height;
      boxW = height * 16 / 9;
    }
    gx->Present((width - boxW) / 2, (height - boxH) / 2, boxW, boxH, top, rows);
    notePicture((width - boxW) / 2, (height - boxH) / 2, boxW, boxH, xh ? float(top) / float(xh) : 0.f,
                xh ? float(rows) / float(xh) : 1.f);
  }
  // the map: "4:3 (GC)" also when RE4_GX_ASPECT asks for 4:3 (as above); the Vulkan renderer drew it in Present
  if (gx->Api() != re4gx::GxApi::kVulkan)
    minimapDraw(mapFormat, xh, xfbWide);
  return true;
}

re4::CoreScene *currentScene() {
  std::lock_guard<std::mutex> lock(sceneLock);
  return scene.get();
}

// A crash (M11, the testers' error log): where it happened into the log and
// into the crash file of the log folder (CoreBridge.setCrashFile), then the
// system's handler as before (its crash report). Signal-safe as far as it
// goes: write() into the file opened in advance; the log call and dladdr are
// what crash handlers of Android apps commonly use.
int crashFd = -1;
struct sigaction oldActions[NSIG];
const int kCrashSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

void crashLine(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void crashLine(const char *fmt, ...) {
  char line[320];
  va_list ap;
  va_start(ap, fmt);
  const int n = std::vsnprintf(line, sizeof(line) - 1, fmt, ap);
  va_end(ap);
  __android_log_print(ANDROID_LOG_ERROR, "RE4Crash", "%s", line);
  if (crashFd >= 0 && n > 0) {
    line[std::min<int>(n, sizeof(line) - 2)] = '\n';
    (void)!write(crashFd, line, static_cast<size_t>(std::min<int>(n, sizeof(line) - 2) + 1));
  }
}

void crashWhere(const char *what, uintptr_t address) {
  Dl_info info{};
  if (address && dladdr(reinterpret_cast<void *>(address), &info) && info.dli_fname) {
    const char *file = std::strrchr(info.dli_fname, '/');
    crashLine("  %s 0x%08lx  %s+0x%lx  %s", what, static_cast<unsigned long>(address), file ? file + 1 : info.dli_fname,
              static_cast<unsigned long>(address - reinterpret_cast<uintptr_t>(info.dli_fbase)),
              info.dli_sname ? info.dli_sname : "");
  } else {
    crashLine("  %s 0x%08lx", what, static_cast<unsigned long>(address));
  }
}

// pc, lr and the words on the stack that point into a library (a guess at
// the return addresses), from a signal's context
void crashContext(void *context) {
#if defined(__arm__) || defined(__aarch64__)
  const auto *uc = static_cast<const ucontext_t *>(context);
#if defined(__arm__)
  const uintptr_t pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr, sp = uc->uc_mcontext.arm_sp;
#else
  // 64 bit (milestone 12): x30 is the link register
  const uintptr_t pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[30], sp = uc->uc_mcontext.sp;
#endif
  crashWhere("pc", pc);
  crashWhere("lr", lr);
  // the words above sp as far as they can be read: the game's stacks end at their mapping, and on
  // 64 bit nothing readable follows (a read past the end crashed the handler itself, 18.10.2026);
  // process_vm_readv stops at the first chunk it cannot read instead of faulting
  static uintptr_t word[512];
  struct iovec local {word, sizeof(word)};
  struct iovec remote[16];
  for (int c = 0; c < 16; c++) {
    remote[c].iov_base = reinterpret_cast<void *>(sp + c * sizeof(word) / 16);
    remote[c].iov_len = sizeof(word) / 16;
  }
  const ssize_t got = process_vm_readv(getpid(), &local, 1, remote, 16, 0);
  const int words = got > 0 ? static_cast<int>(got / sizeof(uintptr_t)) : 0;
  int found = 0;
  for (int i = 0; i < words && found < 24; i++) {
    Dl_info d{};
    if (word[i] > 0x10000 && dladdr(reinterpret_cast<void *>(word[i]), &d) && d.dli_fname &&
        std::strstr(d.dli_fname, ".so")) {
      char what[24];
      std::snprintf(what, sizeof(what), "sp+%03x", static_cast<unsigned>(i * sizeof(uintptr_t)));
      crashWhere(what, word[i]);
      found++;
    }
  }
#else
  (void)context;
#endif
}

void onCrash(int sig, siginfo_t *info, void *context) {
  static volatile sig_atomic_t inside = 0;
  if (!inside) {
    inside = 1;
    crashLine("crash: signal %d (%s), fault address %p, thread %d", sig, strsignal(sig), info ? info->si_addr : nullptr,
              static_cast<int>(gettid()));
    crashContext(context);
    if (crashFd >= 0) fsync(crashFd);
  }
  // the handler before (the system's crash report), with the signal again
  for (std::size_t i = 0; i < sizeof(kCrashSignals) / sizeof(kCrashSignals[0]); i++)
    sigaction(kCrashSignals[i], &oldActions[kCrashSignals[i]], nullptr);
  if (sig == SIGABRT) raise(sig);
}

// The game stands without a pause (CoreBridge.reportHang): the game thread
// says where it is (SIGUSR2 to that thread), the game runs on.
void onHangSample(int, siginfo_t *, void *context) {
  crashLine("hang: the game thread (%d) is at", static_cast<int>(gettid()));
  crashContext(context);
}

// The table's calls to the core process (runtime/re4_core_client.h); it starts with the first
// re4ipc_reserve (CoreBridge.start). Its program lies in this library's folder: the libraries
// are extracted at installation (build.gradle useLegacyPackaging, decision of the project owner,
// 20.10.2026); Android runs programs only from there.
void useCoreProcess(const std::string &why) {
  coreProcessWhy = why;
  re4ipc::Spawn spawn;
  Dl_info info{};
  const std::string lib =
      dladdr(reinterpret_cast<void *>(&useCoreProcess), &info) && info.dli_fname ? info.dli_fname : "";
  const auto slash = lib.rfind('/');
  spawn.libraryDir = slash == std::string::npos ? std::string() : lib.substr(0, slash);
  spawn.program = spawn.libraryDir + "/libre4_coreproc.so";
  // its name in the process list: this process's (the package) with ":core"
  if (FILE *f = std::fopen("/proc/self/cmdline", "re")) {
    char name[160] = "";
    if (std::fgets(name, sizeof(name), f) && name[0]) spawn.name = std::string(name) + ":core";
    std::fclose(f);
  }
  re4ipc::configure(spawn);
  if (crashFd >= 0) re4ipc::setCrashFd(crashFd);
  // the core process writes the core's log file (RE4_LOG_FILE from this process's environment)
  if (api.logFile) {
    api.logFile(nullptr);
    api.logFile = nullptr;
  }
  re4gx::SetLogCopy(nullptr);
  api.error.clear();
  if (lib.find("!/") != std::string::npos) {
    api.error = "the core process needs the app's libraries extracted (this APK keeps them packed)";
  }
  api.mem1Init = re4ipc_mem1_init;
  api.reserve = re4ipc_reserve;
  api.start = re4ipc_start;
  api.state = re4ipc_state;
  api.message = re4ipc_message;
  api.pad = re4ipc_pad;
  api.rumble = re4ipc_rumble;
  api.audio = re4ipc_audio;
  api.audio4 = re4ipc_audio4;
  api.audioSurround = re4ipc_audio_surround;
  api.audioStats = re4ipc_audio_stats;
  api.acquire = re4ipc_acquire;
  api.release = re4ipc_release;
  api.logTail = re4ipc_log_tail;
  api.gxAcquire = re4ipc_gx_acquire;
  api.gxRelease = re4ipc_gx_release;
  api.gxDetach = re4ipc_gx_detach;
  api.vsync = re4ipc_vsync;
  api.pause = re4ipc_pause;
  api.debugMenu = re4ipc_debug_menu;
  api.setDof = re4ipc_set_dof;
  api.gyro = re4ipc_gyro;
  api.depth = re4ipc_gx_depth;
  api.setLetterbox = re4ipc_set_letterbox;
  api.setFastCores = re4ipc_set_fast_cores;
  api.setPotato = re4ipc_set_potato;
  api.setAim = re4ipc_set_aim;
  api.setSoundVolume = re4ipc_set_sound_volume;
  api.restartGame = re4ipc_restart_game;
  api.discProblem = re4ipc_disc_problem;
  api.setCheats = re4ipc_set_cheats;
  api.cheatAction = re4ipc_cheat_action;
  api.roomOutline = re4ipc_room_outline;
  api.setLaserColour = re4ipc_set_laser_colour;
  api.setViewAspect = re4ipc_set_view_aspect;
  api.setHudLayout = re4ipc_set_hud_layout;
  api.touch = re4ipc_touch;
  api.aimTurn = re4ipc_aim_turn;
  api.swipe = re4ipc_swipe;
  api.slash = re4ipc_slash;
  api.instant = re4ipc_instant;
  api.cardList = re4ipc_card_list;
  api.cardFree = re4ipc_card_free_blocks;
  api.cardName = re4ipc_card_file_name;
  api.cardExport = re4ipc_card_export;
  api.cardImport = re4ipc_card_import;
  api.frameCount = re4ipc_frame_count;
  api.gameThread = re4ipc_game_thread;
  api.freeAt = re4ipc_free_at;
  char note[512];
  snprintf(note, sizeof(note), "core: in a process of its own (%s): %s\n", why.c_str(), spawn.program.c_str());
  gAddressMap += note;
  LOG("%s", note);
}

void installCrashHandler() {
  struct sigaction action {};
  action.sa_sigaction = onCrash;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&action.sa_mask);
  for (int sig : kCrashSignals) sigaction(sig, &action, &oldActions[sig]);
  struct sigaction sample {};
  sample.sa_sigaction = onHangSample;
  sample.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sample.sa_mask);
  sigaction(SIGUSR2, &sample, nullptr);
}

}  // namespace

// The mappings below 4 GB when the core loaded
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_addressMap(JNIEnv *env, jclass) {
  std::string map = gAddressMap;
  if (!coreProcessWhy.empty()) map += "core process: " + re4ipc::describe() + "\n" + re4ipc::addressMap();
  return env->NewStringUTF(map.c_str());
}

// The core in a process of its own: why and how it is ("" when the core runs in this process).
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_coreProcess(JNIEnv *env, jclass) {
  std::string text;
  if (!coreProcessWhy.empty()) {
    const std::string state = re4ipc::describe();
    text = coreProcessWhy + (state.empty() ? std::string("; not started yet") : "; " + state);
  }
  return env->NewStringUTF(text.c_str());
}

// Why the core process ended while the app runs ("" while it runs, or the core runs in this process).
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_coreProblem(JNIEnv *env, jclass) {
  return env->NewStringUTF(coreProcessWhy.empty() ? "" : re4ipc::ended().c_str());
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
  JNIEnv *env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) env = nullptr;
  loadCore(env);
  installCrashHandler();
  return JNI_VERSION_1_6;
}

// Starts the game once per process (the core cannot be restarted). Returns ""
// when it runs, else the reason.
extern "C" JNIEXPORT jstring JNICALL
Java_com_ast_re4decomp_CoreBridge_start(JNIEnv *env, jclass, jstring gameRoot, jstring settings) {
  const auto root = text(env, gameRoot);
  // the core's test switches: environment variables read when the game uses them
  if (api.state && api.state() == RE4_CORE_IDLE) {
    // defaults of the app: no debug displays of the debug build (RE4_DEBUG_HUD=1 shows them);
    // "New Game" starts the game without the debug build's start menu (room selection,
    // patch 0043; wish of the project owner, 17.10.2026: rooms are chosen in the debug
    // menu, SELECT held). RE4_ROOM_MENU=1 shows it (test-core-app.ps1 -StartRoom).
    setenv("RE4_DEBUG_HUD", "0", 0);
    setenv("RE4_ROOM_MENU", "0", 0);
    std::string list = text(env, settings);
    std::size_t pos = 0;
    while (pos < list.size()) {
      auto end = list.find(';', pos);
      if (end == std::string::npos) end = list.size();
      const auto item = list.substr(pos, end - pos);
      const auto eq = item.find('=');
      if (eq != std::string::npos && eq > 0 && item.compare(0, 4, "RE4_") == 0) {
        setenv(item.substr(0, eq).c_str(), item.substr(eq + 1).c_str(), 1);
        LOG("setting %s", item.c_str());
      }
      pos = end + 1;
    }
    // the core in a process of its own also with MEM1 free (tests; test-core-app.ps1 -CoreProcess)
    const char *own = getenv("RE4_CORE_PROCESS");
    if (own && std::string(own) == "1" && coreProcessWhy.empty()) {
      useCoreProcess("RE4_CORE_PROCESS=1");
    }
  }
  {
    std::lock_guard<std::mutex> lock(sceneLock);
    if (!scene) scene = std::make_unique<re4::CoreScene>(root);
  }
  std::string problem = api.error;
  if (problem.empty() && api.state() == RE4_CORE_IDLE) {
    // the disc folders of the import: <root>/disc1, <root>/disc2
    if (!api.reserve(root.c_str(), "") || !api.start()) problem = api.message();
  }
  if (problem.empty() && api.state() == RE4_CORE_FAILED) problem = api.message();
  return env->NewStringUTF(problem.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_pad(
    JNIEnv *, jclass, jint buttons, jint stickX, jint stickY, jint substickX, jint substickY,
    jint triggerL, jint triggerR) {
  if (!api.pad) return;
  auto clampS = [](jint v) { return static_cast<std::int8_t>(v < -127 ? -127 : v > 127 ? 127 : v); };
  auto clampU = [](jint v) { return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); };
  Re4CorePad pad{static_cast<std::uint16_t>(buttons), clampS(stickX), clampS(stickY),
                 clampS(substickX), clampS(substickY), clampU(triggerL), clampU(triggerR)};
  if (levelBench.active) pad = Re4CorePad{};  // the benchmark: no player's input (Leon just stands)
  api.pad(&pad);
}

// A tap at (x, y) pixels of the surface (CoreActivity.watchTap): as a place in the game's picture
// (0..1 across its width and down its 448 rows) to the core, which uses it in the sub screens.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_tap(JNIEnv *, jclass, jfloat x, jfloat y) {
  const int w = picW.load(), h = picH.load();
  if (!api.touch || w <= 0 || h <= 0) return;
  const float u = (x - static_cast<float>(picX.load())) / static_cast<float>(w);
  const float inBox = (y - static_cast<float>(picY.load())) / static_cast<float>(h);
  if (u < 0.f || u > 1.f || inBox < 0.f || inBox > 1.f) return;
  api.touch(u, picTop.load() + inBox * picRows.load());
}

// A swipe across the picture (CoreActivity.watchTap): 1 to the left (the next tab of a sub
// screen), -1 to the right.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_swipe(JNIEnv *, jclass, jint dir) {
  if (api.swipe) api.swipe(dir > 0 ? 1 : -1);
}

// A swipe while playing (CoreActivity.watchTap, the menu's switch "Knife by Swipe"): the knife
// draws and slashes, tilt 1 straight up .. -1 straight down (runtime/knife_swipe.cpp).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_slash(JNIEnv *, jclass, jfloat tilt) {
  if (!api.slash || !(tilt == tilt)) return;
  api.slash(tilt < -1.f ? -1.f : tilt > 1.f ? 1.f : tilt);
}

// The game's rumble motor (re4_core_rumble) for the vibrator (CoreActivity).
extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_rumble(JNIEnv *, jclass) {
  return api.rumble ? static_cast<jint>(api.rumble()) : 0;
}

// The game's sound for the app's audio thread (CoreAudio): `frames` stereo
// frames at 32 kHz into `pcm`; the frames that came from the game.
extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_audio(JNIEnv *env, jclass,
                                                                          jshortArray pcm, jint frames) {
  const jsize length = env->GetArrayLength(pcm);
  if (frames <= 0 || frames * 2 > length) return 0;
  auto *out = static_cast<jshort *>(env->GetPrimitiveArrayCritical(pcm, nullptr));
  if (!out) return 0;
  int got = 0;
  if (api.audio) {
    got = api.audio(reinterpret_cast<int16_t *>(out), frames);
  } else {
    std::fill(out, out + frames * 2, 0);
  }
  env->ReleasePrimitiveArrayCritical(pcm, out, 0);
  return got;
}

// The same sound as four channels (front left, front right, rear left, rear right): the game's
// own rear in its Dolby Pro Logic II mode (CoreAudio, for Android's spatializer). A core without
// it gives the stereo with a silent rear.
extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_audio4(JNIEnv *env, jclass,
                                                                           jshortArray pcm, jint frames) {
  const jsize length = env->GetArrayLength(pcm);
  if (frames <= 0 || frames * 4 > length) return 0;
  auto *out = static_cast<jshort *>(env->GetPrimitiveArrayCritical(pcm, nullptr));
  if (!out) return 0;
  int got = 0;
  if (api.audio4) {
    got = api.audio4(reinterpret_cast<int16_t *>(out), frames);
  } else if (api.audio) {
    // stereo into the front, the rear silent (spread from the end so nothing is overwritten)
    got = api.audio(reinterpret_cast<int16_t *>(out), frames);
    for (int i = frames - 1; i >= 0; i--) {
      out[i * 4 + 1] = out[i * 2 + 1];
      out[i * 4] = out[i * 2];
      out[i * 4 + 2] = out[i * 4 + 3] = 0;
    }
  } else {
    std::fill(out, out + frames * 4, 0);
  }
  env->ReleasePrimitiveArrayCritical(pcm, out, 0);
  return got;
}

// A line of the sound output (CoreAudio: the AudioTrack ran out) into the core's log file, with the
// game frame: core-log.txt is in every error log, logcat not on every device. The core in a process
// of its own (32 bit fallback) writes its file there: only logcat then.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_audioNote(JNIEnv *env, jclass, jstring line) {
  if (!api.logFileLine || !coreProcessWhy.empty() || !line) return;
  const char *text = env->GetStringUTFChars(line, nullptr);
  if (!text) return;
  char buf[256];
  std::snprintf(buf, sizeof(buf), "%s (game frame %u)", text, api.frameCount ? api.frameCount() : 0u);
  env->ReleaseStringUTFChars(line, text);
  api.logFileLine(buf);
}

// 1 while the game plays in Dolby Pro Logic II (its rear in the four channels).
extern "C" JNIEXPORT jboolean JNICALL Java_com_ast_re4decomp_CoreBridge_audioSurround(JNIEnv *, jclass) {
  return api.audioSurround && api.audioSurround() ? JNI_TRUE : JNI_FALSE;
}

// A vsync of the display (CoreActivity, the Choreographer's frame time in ns of
// System.nanoTime = CLOCK_MONOTONIC): the core's retraces follow the display.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_vsync(JNIEnv *, jclass, jlong ns) {
  if (api.vsync) api.vsync(static_cast<int64_t>(ns));
}

// The room the game is in (for the Java room loader), -1 outside a room.
extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_wantedRoom(JNIEnv *, jclass) {
  auto *s = currentScene();
  return s ? s->wantedRoom() : -1;
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_roomPrepared(
    JNIEnv *env, jclass, jint room, jstring decodedPath, jstring error) {
  if (auto *s = currentScene())
    s->roomPrepared(static_cast<std::uint16_t>(room), text(env, decodedPath), text(env, error));
}

extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_status(JNIEnv *env, jclass) {
  std::string result;
  if (!api.error.empty()) {
    result = api.error;
  } else {
    static const char *states[] = {"not started", "running", "ended", "failed"};
    const int st = api.state();
    result = std::string("core ") + (st >= 0 && st <= 3 ? states[st] : "?") + ": " + api.message();
    if (auto *s = currentScene()) result += "\n" + s->status();
    if (api.audioStats) {
      unsigned a[5] = {};
      api.audioStats(a);
      char line[160];
      std::snprintf(line, sizeof(line), "\nsound: %u blocks, %u ms waiting, %u lost, %u ran dry, %u skips",
                    a[0], a[1] / 32, a[2], a[3], a[4]);
      result += line;
    }
    char tail[2048];
    api.logTail(tail, sizeof(tail));
    // the last few log lines
    std::string lines(tail);
    std::size_t pos = lines.size();
    for (int n = 0; n < 7 && pos > 0; ++n) {
      const auto nl = lines.rfind('\n', pos - 1);
      pos = nl == std::string::npos ? 0 : nl;
    }
    result += "\n" + lines.substr(pos == 0 ? 0 : pos + 1);
  }
  return env->NewStringUTF(result.c_str());
}

// One GX dump of the next picture (diagnosis, see drawGx); `folder` is used
// when no test switch named one.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_gxDump(JNIEnv *env, jclass, jstring folder) {
  std::lock_guard<std::mutex> lock(gxDumpLock);
  gxDumpAskedDir = text(env, folder);
  gxDumpAsked++;
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_surfaceCreated(JNIEnv *, jclass) {
  glContextLive = true;
  if (auto *s = currentScene()) s->contextCreated();
  lastDraw = {};
  letterboxSent = -1;
  viewAspectSent = -1;
  hudSent = {-1, -1, -1, -1};
  fastSent = -1;
  potatoSent = -1;
  aimSent = -1;
  soundSent = -1;
  // a new GL context: the renderer's objects are gone; the core starts over
  // (full state, every texture again) once chunks are taken again
  gx.reset();
  smoothDrop();
  if (api.gxDetach) api.gxDetach();
}

// The Vulkan surface's window (CoreActivity's VulkanSurface, on its render thread; null when it went): the renderer
// presents to it. The renderer and its textures stay while the window is away (no new start of the core).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_vkWindow(JNIEnv *env, jclass, jobject surface) {
#ifdef __ANDROID__
  ANativeWindow *w = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
#else
  void *w = nullptr;  // (the host's link check: no windows)
  (void)env;
  (void)surface;
#endif
  if (gx && gx->Api() == re4gx::GxApi::kVulkan)
    gx->SetWindow(w, width, height);  // (it holds a reference of its own)
#ifdef __ANDROID__
  if (vkWindow)
    ANativeWindow_release(static_cast<ANativeWindow *>(vkWindow));
#endif
  vkWindow = w;
  glContextLive = false;
  LOG("Vulkan surface: %s", w ? "window came" : "window went");
}

// Why the Vulkan renderer could not start ("" when it runs or GLES was chosen); CoreActivity then goes over to GLES.
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_rendererProblem(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(vulkanProblemLock);
  return env->NewStringUTF(vulkanProblem.c_str());
}

// The renderer's API, device and driver ("" before the first one; the report's renderer.txt).
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_rendererInfo(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(vulkanProblemLock);
  return env->NewStringUTF(rendererInfoText.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_resize(JNIEnv *, jclass, jint w,
                                                                          jint h) {
  width = w > 0 ? w : 1;
  height = h > 0 ? h : 1;
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_draw(JNIEnv *, jclass) {
  auto *s = currentScene();
  if (!s) return;
  if (api.acquire) {
    if (const Re4CoreSnapshot *snapshot = api.acquire()) {
      re4::CopyCoreFrame(*snapshot, frame);
      api.release(snapshot);
    }
  }
  const auto now = std::chrono::steady_clock::now();
  const double seconds = lastDraw.time_since_epoch().count() == 0
                             ? 0.0
                             : std::chrono::duration<double>(now - lastDraw).count();
  lastDraw = now;
  if (drawGx()) return;
  if (gxVulkan() && !glContextLive) return;  // the Vulkan surface: no GL context for the bridge
  try {
    s->draw(frame, width, height, seconds);
  } catch (const std::exception &e) {
    LOG("draw: %s", e.what());
  }
}

// --- the app's menu (M11) ----------------------------------------------------

extern "C" JNIEXPORT jboolean JNICALL Java_com_ast_re4decomp_CoreBridge_hasMenu(JNIEnv *, jclass) {
  return api.pause && api.cardList ? JNI_TRUE : JNI_FALSE;
}

// Result, present, canSave, canLoad, room, chapter, playTime, Unix save time (seconds), saving refused here (1).
extern "C" JNIEXPORT jlongArray JNICALL Java_com_ast_re4decomp_CoreBridge_instant(JNIEnv *env, jclass, jint operation) {
  Re4InstantInfo info = {};
  const int result = api.instant ? api.instant(operation, &info) : RE4_INSTANT_UNSUPPORTED;
  const jlong fields[] = {result, info.present, info.canSave, info.canLoad, info.room, info.chapter,
                         info.playTime, info.savedAt, info.reserved};
  jlongArray out = env->NewLongArray(9);
  if (out) env->SetLongArrayRegion(out, 0, 9, fields);
  return out;
}

// The game stops at its next frame (on) or runs on; true when it stands.
extern "C" JNIEXPORT jboolean JNICALL Java_com_ast_re4decomp_CoreBridge_pause(JNIEnv *, jclass, jboolean on,
                                                                             jint waitMs) {
  corePaused = on == JNI_TRUE;
  if (!api.pause) return JNI_FALSE;
  return api.pause(on ? 1 : 0, waitMs) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_debugMenu(JNIEnv *, jclass) {
  if (api.debugMenu) api.debugMenu();
}

// The depth of field of the menu (cores since 17.10.2026; older ones keep it on): 0 off, 1 light, 2 strong (since
// 0.98.4: the app's gameplay autofocus blurs more and wider, GxRenderer::SetDofStrong; the core only hears on/off).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setDof(JNIEnv *, jclass, jint level) {
  const int want = level >= 0 && level <= 2 ? static_cast<int>(level) : 1;
  if (optDof.exchange(want) != want) LOG("depth of field: %s", want == 0 ? "off" : want == 1 ? "light" : "strong");
  if (api.setDof) api.setDof(want != 0 ? 1 : 0);
}

// "Field of view" (the menu, CONTROLS, PLAYER VIEW; wish of the project owner, 05.11.2026): degrees added to the
// shoulder camera's vertical field of view (patch 0205), sent with the aim settings (bits 24..29).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setFov(JNIEnv *, jclass, jint degrees) {
  const int want = std::clamp(static_cast<int>(degrees), -15, 15);
  if (optFov.exchange(want) != want) LOG("field of view: %+d degrees", want);
}

// The menu's "Force Performance" (drawGx tells the core and places the render thread).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setFastCores(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optFastCores.exchange(want) != want) LOG("Force Performance: %s", want ? "use performance cores" : "default");
}

// The menu's Potato mode (drawGx tells the core): bit 0 weather effects off, bit 1 special effects off.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setPotato(JNIEnv *, jclass, jint mask) {
  const int want = static_cast<int>(mask) & 3;
  if (optPotato.exchange(want) != want)
    LOG("Potato mode: weather effects %s, special effects %s", want & 1 ? "off" : "on", want & 2 ? "off" : "on");
}

// The menu's "Invert vertical" and "Aim speed" (drawGx tells the core): stick aiming with touch and controllers.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setAim(JNIEnv *, jclass, jboolean invert, jfloat speed,
                                                                         jboolean autoAim, jboolean alwaysRun) {
  int percent = static_cast<int>(speed * 100.0f + 0.5f);
  if (!(speed == speed) || percent < 50 || percent > 300) percent = 100;
  // bit 1 "Auto aim", bit 2 "Always run" (wish of the project owner, 30.10.2026; runtime/aim_options.cpp)
  const int want = percent << 8 | (invert == JNI_TRUE ? 1 : 0) | (autoAim == JNI_TRUE ? 2 : 0) |
                   (alwaysRun == JNI_TRUE ? 4 : 0);
  if (optAim.exchange(want) != want)
    LOG("aim: stick aiming %s, speed %d %%, auto aim %s, always run %s", invert == JNI_TRUE ? "inverted" : "normal",
        percent, autoAim == JNI_TRUE ? "on" : "off", alwaysRun == JNI_TRUE ? "on" : "off");
}

// Patch 0188: "reverse" changed in the game's own options since the last call (-1: no; with the core in its own
// process always -1).
extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_aimFromGame(JNIEnv *, jclass) {
  return api.aimFromGame ? api.aimFromGame() : -1;
}

// "Far camera" (the menu, Controls): the shoulder camera further back while walking and standing (patch 0171),
// sent with the aim settings (bit 3).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setFarCamera(JNIEnv *, jclass, jboolean on) {
  const int want = on == JNI_TRUE ? 1 : 0;
  if (optFarCamera.exchange(want) != want) LOG("far camera: %s", want ? "on" : "off");
}

// The menu's AUDIO page (drawGx tells the core): music and sound effects volume in percent.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setSoundVolume(JNIEnv *, jclass, jint music, jint effects) {
  const int m = std::clamp(static_cast<int>(music), 0, 100), e = std::clamp(static_cast<int>(effects), 0, 100);
  const int want = m | e << 8;
  if (optSound.exchange(want) != want) LOG("sound: music %d %%, sound effects %d %%", m, e);
}

// The menu's "Game language" with the European discs (decisions of the project owner, 26./27.10.2026;
// port/docs/retail-eu-plan.md R7): the core takes the language and restarts the game (its soft reset).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_restartGame(JNIEnv *, jclass, jint language) {
  LOG("game language %d: restart", static_cast<int>(language));
  if (api.restartGame) api.restartGame(language);
}

// The first disc file the game could not read: "<disc>\t<path>\t<why>", "" while all went well.
extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_discProblem(JNIEnv *env, jclass) {
  const char *problem = api.discProblem ? api.discProblem() : "";
  return env->NewStringUTF(problem ? problem : "");
}

// The cheat menu (port/docs/cheat-menu-plan.md): the switches (1 health, 2 ammo, 4 one-hit kill) and a
// button (1 Killer7, 2 case L, 3 money -> 1 done, 2 already, 0 not now, -1 no room, 3 queued; -2 without
// a core that has it).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setCheats(JNIEnv *, jclass, jint bits) {
  if (api.setCheats) api.setCheats(bits);
}

extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_cheatAction(JNIEnv *, jclass, jint what) {
  return api.cheatAction ? api.cheatAction(what) : -2;
}

// Frame skip (the menu): while the renderer is behind, chunks are left out (GxRenderer::Skip).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setFrameSkip(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optFrameSkip.exchange(want) != want) LOG("frame skip: %s", want ? "on (chunks are left out while the renderer is behind)" : "off");
}

// Original TV filter (the menu): the game's vertical copy filter on the picture (GxRenderer::SetTvFilter).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setTvFilter(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optTvFilter.exchange(want) != want) LOG("original TV filter: %s", want ? "on" : "off");
}

// PSone demake (the menu): the 3D as the PlayStation drew it and a 320-wide, 15-bit picture (GxRenderer::SetPsx).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setPsx(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optPsx.exchange(want) != want) LOG("PSone demake: %s", want ? "on" : "off");
}

// The laser sight's colour (the menu, CONTROLS, LASER SIGHT; wish of the project owner, 04.11.2026): 0 red .. 3 yellow.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setLaserColour(JNIEnv *, jclass, jint colour) {
  const int want = colour >= 0 && colour <= 3 ? colour : 0;
  if (optLaserColour.exchange(want) != want) LOG("laser sight colour: %d", want);
  if (api.setLaserColour) api.setLaserColour(want);
}

// The benchmark (the menu, Display -> GRAPHICS; LevelBench): start and cancel; the state as {phase, run (0: the 30,
// 1: the 60 FPS pass), level in halves, result with 30, result with 60, pictures a second * 10, held bits}.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_startBenchmark(JNIEnv *, jclass) {
  levelBench.ask = 1;
}

extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_cancelBenchmark(JNIEnv *, jclass) {
  levelBench.ask = 2;
}

extern "C" JNIEXPORT jintArray JNICALL Java_com_ast_re4decomp_CoreBridge_benchmarkState(JNIEnv *env, jclass) {
  const LevelBench &b = levelBench;
  const jint v[7] = {b.phase.load(), b.run.load(), b.level.load(), b.res30.load(), b.res60.load(), b.rate10.load(),
                     b.held.load()};
  jintArray out = env->NewIntArray(7);
  if (out) env->SetIntArrayRegion(out, 0, 7, v);
  return out;
}

// The benchmark's levels kept by the app (per device): "Dynamic" goes up to them (halves, 0: none).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setDynamicCaps(JNIEnv *, jclass, jint cap30, jint cap60) {
  const int a = cap30 >= 2 && cap30 <= 8 ? cap30 : 0, b = cap60 >= 2 && cap60 <= 8 ? cap60 : 0;
  const bool changed = optCap30.exchange(a) != a;
  if ((optCap60.exchange(b) != b || changed) && optScale.load() == 0) optSerial++;  // Dynamic starts anew
  if (changed || a || b) LOG("dynamic resolution: up to %d / %d halves with 30 / 60 pictures a second (benchmark)", a, b);
}

// Minimap (the menu, wish of the project owner, 04.11.2026): the current room's outline at the picture's bottom left.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setMinimap(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optMinimap.exchange(want) != want) LOG("minimap: %s", want ? "on" : "off");
}

// Lens flares (the menu, wish of the project owner, 04.11.2026): a faint glow at the game's lights (GxRenderer::SetFlares).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setFlares(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optFlares.exchange(want) != want) LOG("lens flares: %s", want ? "on" : "off");
}

// FXAA (the menu): an edge smoothing pass over every picture (GxRenderer::SetFxaa).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setFxaa(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optFxaa.exchange(want) != want) LOG("FXAA: %s", want ? "on" : "off");
}

// Sharpen (the menu, 0.99.2): a contrast-adaptive sharpening of every picture after FXAA, movies left out
// (GxRenderer::SetSharpen); 0 off, 1 light, 2 strong.
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setSharpen(JNIEnv *, jclass, jint level) {
  const int want = level < 0 ? 0 : level > 2 ? 2 : level;
  if (optSharpen.exchange(want) != want)
    LOG("sharpen: %s", want == 2 ? "strong" : want == 1 ? "light" : "off");
}

// 60 FPS (experimental): in-between pictures of the renderer (drawGx).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setSmooth(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optSmooth.exchange(want) != want) {
    LOG("60 FPS from in-between pictures: %s", want ? "on" : "off");
    // Dynamic with the benchmark's levels: the other one applies now
    if (optScale.load() == 0 && optCap30.load() != optCap60.load()) optSerial++;
  }
}

// Merged draw calls (experimental): draws that differ only in their matrices in one call (drawGx).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setMerge(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optMerge.exchange(want) != want) LOG("merged draw calls: %s", want ? "on" : "off");
}

// Vertex ring (experimental, for Mali): every chunk's vertices into buffers mapped for good; a new
// renderer takes it (GxOptions::vertexRing, wish of the project owner, 01.11.2026).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setVertexRing(JNIEnv *, jclass, jboolean on) {
  const int want = on ? 1 : 0;
  if (optVertexRing.exchange(want) != want) {
    LOG("vertex ring: %s", want ? "on" : "off");
    optSerial++;
  }
}

// Swipe aiming (CoreInput.takeAimSwipe): the touch area's finger as angles (cores since 29.10.2026).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_aimTurn(JNIEnv *, jclass, jfloat yaw, jfloat pitch) {
  if (api.aimTurn) api.aimTurn(yaw, pitch);
}

// Gyro aiming (GyroAim.java): the turn rates to the core (cores since 19.10.2026; older ones: none).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_gyro(JNIEnv *, jclass, jfloat yaw, jfloat pitch) {
  if (api.gyro) api.gyro(yaw, pitch);
}

std::mutex cardLock;
std::string cardNote;

// The card's files, one row each: index, name, blocks, save, intact, room,
// chapter, times saved, play time, year, month, day, hour, minute, the two
// comment lines (tab-separated); null when the game uses the card or there is
// none (cardNote says which).
extern "C" JNIEXPORT jobjectArray JNICALL Java_com_ast_re4decomp_CoreBridge_cardList(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(cardLock);
  static Re4CardFile files[127];
  const int n = api.cardList ? api.cardList(files, 127) : RE4_CARD_NOCARD;
  if (n < 0) {
    cardNote = n == RE4_CARD_BUSY ? "busy" : "nocard";
    return nullptr;
  }
  jobjectArray rows = env->NewObjectArray(n, env->FindClass("java/lang/String"), nullptr);
  for (int i = 0; i < n; i++) {
    const Re4CardFile &f = files[i];
    char row[256];
    std::snprintf(row, sizeof(row), "%d\t%s\t%u\t%d\t%d\t%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%s\t%s", f.index,
                  f.name, f.blocks, f.save, f.intact, f.room, f.chapter, f.saveCount, f.playTime, f.year, f.month,
                  f.day, f.hour, f.minute, f.comment[0], f.comment[1]);
    for (char *c = row; *c; c++)  // the comments of other games may hold anything
      if (static_cast<unsigned char>(*c) >= 0x80 || (*c < 0x20 && *c != '\t')) *c = '?';
    jstring s = env->NewStringUTF(row);
    env->SetObjectArrayElement(rows, i, s);
    env->DeleteLocalRef(s);
  }
  return rows;
}

extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_cardFreeBlocks(JNIEnv *, jclass) {
  std::lock_guard<std::mutex> lock(cardLock);
  return api.cardFree ? api.cardFree() : RE4_CARD_NOCARD;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_cardFileName(JNIEnv *env, jclass,
                                                                                   jint index) {
  std::lock_guard<std::mutex> lock(cardLock);
  char name[80] = "";
  if (!api.cardName || api.cardName(index, name, sizeof(name)) != RE4_CARD_OK) return nullptr;
  return env->NewStringUTF(name);
}

// The .gci of file `index` (GameCube byte order), null on an error (cardNote).
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_ast_re4decomp_CoreBridge_cardExport(JNIEnv *env, jclass,
                                                                                    jint index) {
  std::lock_guard<std::mutex> lock(cardLock);
  if (!api.cardExport) return nullptr;
  std::vector<unsigned char> out(RE4_GCI_HEADER + 251 * RE4_CARD_BLOCK);
  char note[256] = "";
  const int bytes = api.cardExport(index, out.data(), static_cast<unsigned>(out.size()), note, sizeof(note));
  cardNote = note;
  if (bytes <= 0) return nullptr;
  jbyteArray result = env->NewByteArray(bytes);
  env->SetByteArrayRegion(result, 0, bytes, reinterpret_cast<const jbyte *>(out.data()));
  return result;
}

// A .gci onto the card: RE4_CARD_OK (0) or an error (re4_card_files.h; cardNote).
extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_cardImport(JNIEnv *env, jclass, jbyteArray gci,
                                                                              jboolean replace) {
  std::lock_guard<std::mutex> lock(cardLock);
  if (!api.cardImport) return RE4_CARD_NOCARD;
  const jsize size = env->GetArrayLength(gci);
  std::vector<unsigned char> in(static_cast<size_t>(size));
  env->GetByteArrayRegion(gci, 0, size, reinterpret_cast<jbyte *>(in.data()));
  char note[256] = "";
  const int r = api.cardImport(in.data(), static_cast<unsigned>(size), replace ? 1 : 0, note, sizeof(note));
  cardNote = note;
  return r;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_ast_re4decomp_CoreBridge_cardNote(JNIEnv *env, jclass) {
  std::lock_guard<std::mutex> lock(cardLock);
  return env->NewStringUTF(cardNote.c_str());
}

// The picture options: scale 0 (automatic) or 1-4, samples 1/2/4, anisotropy
// 1-16, aspect 0 (16:9), 1 (4:3), 2 (stretched), 3 (4:3 without the bars while playing, in an exact
// 4:3 box; 4 is the same) or 5 (Native: the screen's own aspect, re4mm::Native).
// Applied at the next picture while the game runs.
// scale: in halves (0: dynamic; 2: 1x, 3: 1.5x ... 8: 4x)
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setGraphics(JNIEnv *, jclass, jint scale,
                                                                               jint samples, jint anisotropy,
                                                                               jint aspect) {
  const bool rebuild = optScale.load() != scale || optSamples.load() != samples || optAnisotropy.load() != anisotropy;
  optScale = scale;
  optSamples = samples;
  optAnisotropy = anisotropy;
  optAspect = aspect;
  if (rebuild) optSerial++;
  LOG("picture options: scale %s, %d samples, anisotropy %d, %s", scale ? ScaleName(scale).c_str() : "dynamic",
      samples, anisotropy,
      aspect == 1   ? "4:3"
      : aspect == 2 ? "stretched"
      : aspect == 3 || aspect == 4 ? "4:3 without the bars"
      : aspect == 5                ? "Native"
                                   : "16:9");
}

// {most samples, strongest anisotropy, scale (in halves), samples, anisotropy in use}; null before the renderer started.
extern "C" JNIEXPORT jintArray JNICALL Java_com_ast_re4decomp_CoreBridge_graphicsInfo(JNIEnv *env, jclass) {
  if (!infoMaxSamples.load()) return nullptr;
  const jint v[5] = {infoMaxSamples.load(), infoMaxAnisotropy.load(), infoScale.load(), infoSamples.load(),
                     infoAnisotropy.load()};
  jintArray result = env->NewIntArray(5);
  env->SetIntArrayRegion(result, 0, 5, v);
  return result;
}

// The file a crash writes where it happened (the error log's folder).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setCrashFile(JNIEnv *env, jclass,
                                                                                jstring path) {
  const auto file = text(env, path);
  const int fd = file.empty() ? -1 : open(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  const int old = crashFd;
  crashFd = fd;
  // the core process writes its crashes there as well (it gets its own copy of the file)
  if (!coreProcessWhy.empty()) re4ipc::setCrashFd(fd);
  if (old >= 0) close(old);
}

// The core's own log file (CoreLog, "Record the error log"): its lines straight into the file,
// without logcat (a tester's Samsung recorded nothing through logcat, 21.10.2026); "" stops it.
// The core process takes the file from the environment when it starts (RE4_LOG_FILE).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_setLogFile(JNIEnv *env, jclass, jstring path) {
  const auto file = text(env, path);
  if (file.empty()) {
    unsetenv("RE4_LOG_FILE");
  } else {
    setenv("RE4_LOG_FILE", file.c_str(), 1);
  }
  if (coreProcessWhy.empty() && api.logFile) api.logFile(file.empty() ? nullptr : file.c_str());
}

// The keys of the shader programs this device made, into one file (the error report: they go into
// the app's list, port/tools/gx_program_list.py); how many.
extern "C" JNIEXPORT jint JNICALL Java_com_ast_re4decomp_CoreBridge_writeProgramKeys(JNIEnv *env, jclass,
                                                                                    jstring path) {
  const char *cache = getenv("RE4_GX_PROGRAM_CACHE");
  if (!cache || !*cache) return 0;
  return static_cast<jint>(re4gx::WriteProgramKeyList(cache, text(env, path)));
}

// Pictures made for the screen so far: each game picture once, with "60 FPS" also the in-between
// ones (the FPS display, CoreActivity.updateFps).
extern "C" JNIEXPORT jlong JNICALL Java_com_ast_re4decomp_CoreBridge_pictures(JNIEnv *, jclass) {
  return picturesMade.load();
}

// Game frames so far (one per PADRead), -1 without the core's counter.
extern "C" JNIEXPORT jlong JNICALL Java_com_ast_re4decomp_CoreBridge_gameFrames(JNIEnv *, jclass) {
  return api.frameCount ? static_cast<jlong>(api.frameCount()) : -1;
}

// The game stands without a pause: three samples of where its thread is, 200 ms apart
// (a loop shows as places in one function, a wait as the same place).
extern "C" JNIEXPORT void JNICALL Java_com_ast_re4decomp_CoreBridge_reportHang(JNIEnv *, jclass, jint seconds) {
  if (!coreProcessWhy.empty()) {
    re4ipc::reportHang(static_cast<int>(seconds));  // the core process samples its game thread itself
    return;
  }
  const int tid = api.gameThread ? api.gameThread() : 0;
  crashLine("hang: no game frame for %d s (frame %u)", static_cast<int>(seconds),
            api.frameCount ? api.frameCount() : 0u);
  if (tid <= 0) return;
  for (int i = 0; i < 3; i++) {
    syscall(SYS_tgkill, getpid(), tid, SIGUSR2);
    usleep(200000);
  }
  if (crashFd >= 0) fsync(crashFd);
}
