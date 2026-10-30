#include "gx_gl.h"

#include "gx_gl_shader.h"
#include "gx_texture.h"
#include "re4_gx_frame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <dirent.h>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef GL_MAP_PERSISTENT_BIT_EXT
#define GL_MAP_PERSISTENT_BIT_EXT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT_EXT
#define GL_MAP_COHERENT_BIT_EXT 0x0080
#endif
#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_MAX_SAMPLES_EXT
#define GL_MAX_SAMPLES_EXT 0x8D57
#endif
#ifndef GL_TEXTURE_SAMPLES_EXT
#define GL_TEXTURE_SAMPLES_EXT 0x8D6C
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <atomic>
#include <cstdarg>

#define GXLOG(...) re4gx::LogTo("RE4GX", __VA_ARGS__)

namespace re4gx {

namespace {
std::atomic<void (*)(const char *)> gLogCopy{nullptr};
} // namespace

void SetLogCopy(void (*copy)(const char *line)) { gLogCopy = copy; }

void LogTo(const char *tag, const char *fmt, ...) {
  char text[4096];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(text, sizeof(text), fmt, ap);
  va_end(ap);
#ifdef __ANDROID__
  __android_log_write(ANDROID_LOG_INFO, tag, text);
#else
  std::fprintf(stderr, "[%s] %s\n", tag, text);
#endif
  if (auto copy = gLogCopy.load()) {
    char line[sizeof(text) + 32];
    std::snprintf(line, sizeof(line), "%s: %s", tag, text);
    copy(line);
  }
}

namespace {

// `name` in the extension string `ext` as a whole word
bool HasExtension(const char *ext, const char *name) {
  if (!ext)
    return false;
  const std::size_t n = std::strlen(name);
  for (const char *at = std::strstr(ext, name); at; at = std::strstr(at + n, name))
    if ((at == ext || at[-1] == ' ') && (at[n] == ' ' || at[n] == '\0'))
      return true;
  return false;
}

// A function of a GLES extension: not exported by every GLES library, then through EGL (loaded
// with the context)
void *GlExtension(const char *name) {
  void *f = dlsym(RTLD_DEFAULT, name);
  using GetProc = void *(*)(const char *);
  if (!f)
    if (auto get = reinterpret_cast<GetProc>(dlsym(RTLD_DEFAULT, "eglGetProcAddress")))
      f = get(name);
  return f;
}

constexpr int kEfbWidth = 640;
constexpr int kEfbHeight = 528;
// GL_EXT_texture_filter_anisotropic (not in the GLES 3.0 headers)
constexpr GLenum kMaxAnisotropy = 0x84FE, kMaxAnisotropyLimit = 0x84FF;
constexpr unsigned kTlutBytes = 0x80000;

float AsFloat(std::uint32_t v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

std::uint64_t Fnv(const void *data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
  const auto *p = static_cast<const std::uint8_t *>(data);
  for (std::size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

int Signed11(std::uint32_t v) { return (v & 0x400) ? static_cast<int>(v & 0x7FF) - 2048 : static_cast<int>(v & 0x7FF); }

GLuint Compile(GLenum type, const std::string &source, std::string *log) {
  GLuint s = glCreateShader(type);
  const char *text = source.c_str();
  glShaderSource(s, 1, &text, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buffer[4096];
    glGetShaderInfoLog(s, sizeof(buffer), nullptr, buffer);
    if (log)
      *log = buffer;
    glDeleteShader(s);
    return 0;
  }
  return s;
}

GLuint Link(const std::string &vs, const std::string &fs, std::string *log, bool retrievable = false) {
  GLuint v = Compile(GL_VERTEX_SHADER, vs, log);
  if (!v)
    return 0;
  GLuint f = Compile(GL_FRAGMENT_SHADER, fs, log);
  if (!f) {
    glDeleteShader(v);
    return 0;
  }
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  if (retrievable)
    glProgramParameteri(p, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buffer[4096];
    glGetProgramInfoLog(p, sizeof(buffer), nullptr, buffer);
    if (log)
      *log = buffer;
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

// A file written under this name first and renamed when complete: one per thread (the render
// thread and the one of WarmProgramCache may store the same program at once).
std::string TempName(const std::string &path) {
  char tail[32];
  std::snprintf(tail, sizeof(tail), ".part%ld", static_cast<long>(syscall(SYS_gettid)));
  return path + tail;
}

// Linked programs kept on disk (GxRenderer::SetProgramCache): one file per
// shader pair, named by a hash of both sources and the driver; a binary the
// driver no longer takes is linked anew.
struct ProgramFile {
  char magic[4];
  std::uint32_t format, bytes;
  std::uint64_t check; // a second hash of the sources
};

GLuint LoadProgram(const std::string &path, std::uint64_t check) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return 0;
  ProgramFile head{};
  std::vector<std::uint8_t> binary;
  bool ok = std::fread(&head, sizeof(head), 1, f) == 1 && std::memcmp(head.magic, "RE4P", 4) == 0 &&
            head.check == check && head.bytes > 0 && head.bytes < (64u << 20);
  if (ok) {
    binary.resize(head.bytes);
    ok = std::fread(binary.data(), 1, binary.size(), f) == binary.size();
  }
  std::fclose(f);
  if (!ok)
    return 0;
  GLuint p = glCreateProgram();
  glProgramBinary(p, head.format, binary.data(), static_cast<GLsizei>(binary.size()));
  GLint linked = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &linked);
  if (!linked) {
    glDeleteProgram(p);
    return 0;
  }
  return p;
}

void StoreProgram(const std::string &path, std::uint64_t check, GLuint p) {
  GLint bytes = 0;
  glGetProgramiv(p, GL_PROGRAM_BINARY_LENGTH, &bytes);
  if (bytes <= 0)
    return;
  std::vector<std::uint8_t> binary(static_cast<std::size_t>(bytes));
  GLenum format = 0;
  GLsizei written = 0;
  glGetProgramBinary(p, bytes, &written, &format, binary.data());
  if (written <= 0)
    return;
  const std::string temp = TempName(path);
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return;
  ProgramFile head{{'R', 'E', '4', 'P'}, format, static_cast<std::uint32_t>(written), check};
  const bool ok = std::fwrite(&head, sizeof(head), 1, f) == 1 &&
                  std::fwrite(binary.data(), 1, static_cast<std::size_t>(written), f) == static_cast<std::size_t>(written);
  if (std::fclose(f) == 0 && ok)
    std::rename(temp.c_str(), path.c_str());
  else
    std::remove(temp.c_str());
}

// Next to each program binary its ShaderKey (<hash>.key): GxRenderer::Prewarm makes the programs
// this device used before ahead of their first draw. A key file whose struct size differs is
// ignored.
struct KeyFile {
  char magic[4];
  std::uint32_t bytes; // sizeof(ShaderKey)
};

void StoreKey(const std::string &path, const ShaderKey &k) {
  if (access(path.c_str(), F_OK) == 0)
    return;
  const std::string temp = TempName(path);
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return;
  KeyFile head{{'R', 'E', '4', 'K'}, static_cast<std::uint32_t>(sizeof(k))};
  const bool ok = std::fwrite(&head, sizeof(head), 1, f) == 1 && std::fwrite(&k, sizeof(k), 1, f) == 1;
  if (std::fclose(f) == 0 && ok)
    std::rename(temp.c_str(), path.c_str());
  else
    std::remove(temp.c_str());
}

bool LoadKey(const std::string &path, ShaderKey *k) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  KeyFile head{};
  const bool ok = std::fread(&head, sizeof(head), 1, f) == 1 && std::memcmp(head.magic, "RE4K", 4) == 0 &&
                  head.bytes == sizeof(*k) && std::fread(k, sizeof(*k), 1, f) == 1;
  std::fclose(f);
  return ok;
}

struct Program {
  GLuint id = 0;
  GLint mtx = -1;
  mutable std::uint32_t lastMtx[3] = {~0u, ~0u, ~0u}; // uMtx as last set
  GLint widen = -1;           // uWiden (flat draws only)
  mutable float lastWiden = 1.0f;
};

struct KeyHash {
  std::size_t operator()(const ShaderKey &k) const { return static_cast<std::size_t>(Fnv(&k, sizeof(k))); }
};
struct KeyEqual {
  bool operator()(const ShaderKey &a, const ShaderKey &b) const { return std::memcmp(&a, &b, sizeof(a)) == 0; }
};

// The driver a binary belongs to: GL renderer and version of the current context.
std::string DriverTag() {
  const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
  return std::string(renderer ? renderer : "") + "|" + (version ? version : "");
}

// A program's files in `cache` without .bin/.key: a hash of both sources and the driver; in
// *check a second hash of the sources (in the binary's head).
std::string ProgramStem(const std::string &cache, const std::string &tag, const std::string &vs, const std::string &fs,
                        std::uint64_t *check) {
  std::uint64_t h = Fnv(tag.data(), tag.size());
  h = Fnv(vs.data(), vs.size(), h);
  h = Fnv(fs.data(), fs.size(), h ^ 0x5Au);
  *check = Fnv(fs.data(), fs.size(), Fnv(vs.data(), vs.size(), 0x9E3779B97F4A7C15ull));
  char name[32];
  std::snprintf(name, sizeof(name), "/%016llx", static_cast<unsigned long long>(h));
  return cache + name;
}

// A key list file: "RE4L", the size of a key, the count, the keys (WriteProgramKeyList, the app's
// asset gx-programs.keys; port/tools/gx_program_list.py). A list of another key size is ignored.
struct KeyListHead {
  char magic[4];
  std::uint32_t keyBytes, count;
};

bool ReadKeyList(const std::string &path, std::vector<ShaderKey> *keys) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return false;
  KeyListHead head{};
  bool ok = std::fread(&head, sizeof(head), 1, f) == 1 && std::memcmp(head.magic, "RE4L", 4) == 0 &&
            head.keyBytes == sizeof(ShaderKey) && head.count < 1000000;
  if (ok) {
    const std::size_t at = keys->size();
    keys->resize(at + head.count);
    ok = std::fread(keys->data() + at, sizeof(ShaderKey), head.count, f) == head.count;
    if (!ok)
      keys->resize(at);
  }
  std::fclose(f);
  return ok;
}

bool WriteKeyList(const std::string &path, const std::vector<ShaderKey> &keys) {
  const std::string temp = TempName(path);
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return false;
  const KeyListHead head{{'R', 'E', '4', 'L'}, static_cast<std::uint32_t>(sizeof(ShaderKey)),
                         static_cast<std::uint32_t>(keys.size())};
  bool ok = std::fwrite(&head, sizeof(head), 1, f) == 1 &&
            std::fwrite(keys.data(), sizeof(ShaderKey), keys.size(), f) == keys.size();
  ok = std::fclose(f) == 0 && ok;
  if (ok)
    std::rename(temp.c_str(), path.c_str());
  else
    std::remove(temp.c_str());
  return ok;
}

// The cache's key files: each key with its files' stem.
std::vector<std::pair<ShaderKey, std::string>> CacheKeys(const std::string &cache) {
  std::vector<std::pair<ShaderKey, std::string>> out;
  DIR *dir = opendir(cache.c_str());
  if (!dir)
    return out;
  while (const dirent *e = readdir(dir)) {
    const std::string name = e->d_name;
    if (name.size() != 20 || name.compare(16, 4, ".key") != 0)
      continue;
    std::pair<ShaderKey, std::string> k;
    k.second = cache + "/" + name.substr(0, 16);
    if (LoadKey(k.second + ".key", &k.first))
      out.push_back(k);
  }
  closedir(dir);
  return out;
}

std::atomic<int> gWarming{0};  // WarmProgramCache runs

// A texture as the renderer keeps it: decoded RGBA8 with its levels.
struct Texture {
  GLuint id = 0;
  unsigned width = 0, height = 0, levels = 1;
};

// An EFB copy to a texture: the texels as the matching texture format reads
// them (8-bit copies as I8: value in all four channels; two-channel copies as
// IA8; colour copies as their colour), and for paletted reads the raw value.
struct Copy {
  GLuint id = 0;
  unsigned width = 0, height = 0;
  unsigned serial = 0;
  unsigned kind = 0; // 0 colour, 1 one channel (value in .r), 2 two channels (.a high, .r low)
};

} // namespace

struct GxRenderer::Impl {
  // the EFB scale in halves of the GameCube's resolution (2: 1x, 3: 1.5x ... 8: 4x; the app menu's
  // in-between levels, wish of the project owner, 27.10.2026). Lo / Hi: EFB pixels (the
  // GameCube's) to pixels here, rounded down / up. A region runs from Lo(x) to Lo(x + w), so
  // scissors, viewports and clears of neighbouring regions meet without a gap; a copy has Hi(w)
  // pixels (its texture), and the copy shader reads its source rectangle exactly (Sf).
  int halves = 2;
  int Lo(int v) const { return v * halves / 2; }
  int Hi(int v) const { return (v * halves + 1) / 2; }
  float Sf(int v) const { return static_cast<float>(v) * static_cast<float>(halves) * 0.5f; }
  GxStats stats;

  // register state
  std::uint32_t bp[256] = {};
  std::uint32_t xfMem[0x800] = {};
  std::uint32_t xfReg[0x58] = {};
  std::vector<std::uint8_t> tlut = std::vector<std::uint8_t>(kTlutBytes);
  std::int32_t tevColor[4][4] = {};
  std::int32_t tevKonst[4][4] = {};
  // XF block rows (vec4) that changed since the last upload, per part (kXfParts)
  int dirtyLo[kXfParts] = {}, dirtyHi[kXfParts] = {};

  // GL objects
  GLuint efbFbo = 0, efbColor = 0, efbDepth = 0;
  // multisampled EFB (GxOptions.samples > 1): the game draws into it; before
  // a copy out of the EFB the region is resolved into efbColor / efbDepth
  GLuint msaaFbo = 0, msaaColor = 0, msaaDepth = 0;
  // or the samples in the GPU's tile memory, on efbColor / efbDepth themselves (TiledMsaa)
  bool tiledMsaa = false;
  // GPU step d2: the second set of EFB textures on the tiles; each new pass draws into the other
  // set and first draws the EFB back from the one before (UseEfb)
  GLuint spareFbo = 0, spareColor = 0, spareDepth = 0;
  GLuint restoreProgram = 0;
  bool efbLeft = false;         // another framebuffer was bound since the EFB's
  int wantSamples = 1;          // the samples asked for (own buffers when the tiles fail their self-test)
  bool restorePending = false;  // the EFB is to be drawn back before it is drawn into

  GxOptions options;
  int samples = 1;
  float anisotropy = 1.0f;
  GLuint xfbFbo = 0, xfbTex = 0;
  unsigned xfbWidth = 0, xfbHeight = 0;
  bool haveXfb = false;
  // FXAA (GxRenderer::SetFxaa): the smoothed copy of the XFB, shown instead of it while fxaaShown
  bool fxaaOn = false, fxaaShown = false;
  GLuint fxaaTex = 0, fxaaFbo = 0, fxaaProgram = 0;
  unsigned fxaaTexW = 0, fxaaTexH = 0;
  GLint fxaaTexelLoc = -1;
  // the depth map for the game's GXPeekZ (BP 0x5B marker), see GxRenderer::SetDepthSink
  DepthSink depthSink;
  bool peekWanted = false, peekFailed = false;
  // the number of the picture that peeked (the marker's value) and of the picture in each read-back slot
  std::uint32_t peekPicture = 0, peekSlotPicture[2] = {0, 0};
  GLuint peekProgram = 0, peekFbo = 0, peekTex = 0, peekPbo[2] = {0, 0};
  GLsync peekFence[2] = {nullptr, nullptr};
  GLint peekSizeLoc = -1, peekScaleLoc = -1;
  bool skipDraws = false;  // Skip: this chunk's draws, EFB copies and markers are left out
  bool holdXfb = false;  // this chunk's display copy is not shown (RE4_GX_CHUNK_HOLD)
  bool viBlack = false;  // the VI shows black (RE4_GX_CHUNK_BLACK, VISetBlack): Present draws black
  GLuint vao = 0, vbo = 0, ibo = 0;
  // the in-between pass's blended vertices (60 FPS): a buffer of their own, each draw's at a place of
  // its own, written without waiting (the pass draws from vbo meanwhile). Written into vbo they were
  // before 26.10.2026: a buffer the GPU still read from in that picture, the driver copied or waited
  // for it at every figure (Retroid: 74 ms a picture, 3 to 5 pictures a second)
  GLuint blendVbo = 0;
  std::size_t blendCap = 0, blendUsed = 0;
  std::int64_t blendAt = -1;  // the draw's vertices in blendVbo / blendRing (byte offset), -1: from vbo
  // With EXT_buffer_storage the blended vertices go into a ring mapped once for good, a quarter per
  // in-between pass with a fence after it: a memcpy per draw, no GL call. A map and an unmap per
  // blended draw cost the Retroid's driver more than the draws (r104: 233 blended draws per
  // in-between picture; 60 FPS stayed at a few pictures a second more, 26.10.2026).
  static constexpr unsigned kBlendParts = 4;
  static constexpr std::size_t kBlendPart = std::size_t(6) << 20;
  GLuint blendRing = 0;
  std::uint8_t *blendMap = nullptr;
  bool blendRingTried = false;
  unsigned blendPart = 0;
  GLsync blendFence[kBlendParts] = {};
  // the chunk whose vertices and indices the in-between pass uploaded: its Run right after draws
  // from them (vbo is no longer changed by the pass) instead of uploading them again
  const Re4GxChunk *uploadedBy = nullptr;
  std::uint32_t uploadedSequence = 0;
  GLuint xfUbo[kXfParts] = {}, pxUbo = 0;
  GLuint copyProgram = 0, presentProgram = 0, paletteProgram = 0;
  GLint presentRows = -1;
  GLint copyMode = -1, copyRect = -1, copySize = -1, copyHalf = -1, copyGain = -1, copyLimit = -1;
  // lowest EFB row (+1) a draw under a full-width scissor reached since the last display copy: the
  // rows below it were never drawn (the game's letterbox)
  int sceneBottom = 0;
  // the EFB rows the app shows (GxRenderer::SetShownRows): all, or 56..391 in the 16:9 formats
  std::atomic<int> shownTop{0}, shownBottom{kEfbHeight};
  // GxRenderer::SetWiden: how much wider the flat draws of the whole screen are drawn (1: as sent)
  std::atomic<float> widen2d{1.0f};
  GLint paletteSize = -1, paletteKind = -1, paletteFormat = -1;
  GLuint quadVao = 0;
  GLuint whiteTex = 0;
  std::unordered_map<ShaderKey, Program, KeyHash, KeyEqual> programs;
  std::unordered_map<std::uint64_t, Texture> textures;
  std::unordered_map<std::uint32_t, Copy> copies;  // by destination address
  std::unordered_map<std::uint64_t, Texture> paletted; // copies read through a palette
  std::unordered_map<std::uint64_t, GLuint> samplers;
  // the gameplay autofocus (Autofocus): made at its first marker
  GLuint focusProgram = 0, blurProgram = 0;
  GLint focusRegion = -1, focusSize = -1, focusZ = -1, focusPlayer = -1;
  GLint blurRegion = -1, blurSize = -1, blurZ = -1, blurRadius = -1;
  GLuint blurTex = 0, blurFbo = 0;
  // the focus (3 x 1 texels: the focus in use, the last two measurements), two in turn: each
  // autofocus reads the one before and writes the other (focusCur: the one in use)
  GLuint focusTex[2] = {}, focusFbo[2] = {};
  int focusCur = 0;
  unsigned focusChunk = ~0u;  // the chunk of the last autofocus (stats.chunks)
  GLint focusRestart = -1;
  // GPU step e: the blur at half the resolution into halfTex, mixed into the EFB at the full one
  // (colorFbo: the EFB's colour without its depth, which the mix reads)
  GLuint halfProgram = 0, mixProgram = 0, halfTex = 0, halfFbo = 0, colorFbo = 0;
  GLint halfRegion = -1, halfSize = -1, halfZ = -1, halfRadius = -1;
  GLint mixRegion = -1, mixSize = -1, mixZ = -1;
  bool autofocusFailed = false;
  // the last perspective projection (XF 0x1024/0x1025, type 0 at 0x1026):
  // the scene's, for the distances behind the depths
  float perspA = 0.0f, perspB = 0.0f;
  bool havePerspective = false;

  // --- what GL already has (per-draw work is skipped while it holds) --------
  bool keyDirty = true;              // a register that shapes the shader changed
  std::uint32_t keyLayout = ~0u;     // vertex layout of the cached key
  ShaderKey key = {};
  const Program *keyProgram = nullptr;
  bool pxDirty = true;               // a register of the PX block changed
  bool xfRegsDirty = true;           // colours, viewport or projection changed
  std::uint32_t stateSig[10] = {};   // ApplyState's registers as last applied
  std::string programCache;          // folder of linked programs ("" = none)
  std::string driverTag;             // GL renderer and version: binaries only for this driver
  // Prewarm: the cache's key files (read at its first call), the next one, its time so far
  struct AheadKey {
    ShaderKey key;
    bool listed = false; // from the app's list (SetProgramList): for the MSAA setting in use
  };
  std::string programList;  // the app's key list (SetProgramList)
  bool aheadRead = false;
  std::vector<AheadKey> ahead;
  std::size_t aheadNext = 0;
  double aheadMs = 0.0;
  unsigned aheadLoaded = 0, aheadLinked = 0, aheadListed = 0;
  bool stateValid = false;
  GLuint unitTex[8] = {}, unitSampler[8] = {};
  bool unitsValid = false;
  bool vaoBound = false;
  std::uint32_t attribMask = 0;      // enabled vertex attributes of vao
  std::uint32_t attribLayout = ~0u, attribStride = 0, attribBase = ~0u;
  // the counters after the draw before (GxStats::mergeSame / mergeUniform), valid within a chunk
  struct MergeSnap {
    unsigned uniforms, programs, binds, states, setups, copies, displays;
    GLenum mode;
  } mergeLast{};
  bool mergeValid = false, mergeMatrix = false, mergePixel = false;
  GLuint attribBuffer = 0;
  // glDrawElementsBaseVertex (GLES 3.2 / OES / EXT): one attribute setup for
  // all draws of a layout; null: pointers per draw
  using DrawBaseVertex = void (*)(GLenum, GLsizei, GLenum, const void *, GLint);
  DrawBaseVertex drawBaseVertex = nullptr;

  // GxStats::splitNs: the time since the last mark to `kind` (Run marks at its start)
  std::chrono::steady_clock::time_point splitAt;
  void Split(int kind) {
    const auto now = std::chrono::steady_clock::now();
    stats.splitNs[kind] +=
        static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - splitAt).count());
    splitAt = now;
  }

  // Other passes (uploads, copies, palettes, presenting) changed GL state.
  void Touched() {
    stateValid = false;
    unitsValid = false;
    vaoBound = false;
    current = nullptr;
  }

  // the texture bound to each map
  struct Bound {
    GLuint id = 0;
    unsigned width = 1, height = 1, levels = 1;
  } bound[8];

  // --- uniform stream ------------------------------------------------------
  // Every changed XF or PX block goes to a fresh place of one big buffer and is
  // bound there (glBindBufferRange), as Dolphin does: the driver never has to
  // update a block the GPU still reads. glBufferSubData on the two small
  // blocks did that about 1200 times in a village frame (r101); mobile drivers
  // copy the whole buffer for each such update. Persistent mapping
  // (EXT_buffer_storage) when the driver has it, else an unsynchronised map per
  // block; a fence guards each quarter of the ring before it is written again.
  // RE4_GX_UNIFORM_STREAM=0: the two small buffers as before.
  //
  // The ring moves on to the next quarter between chunks, never while a picture is drawn: a
  // fence set in the middle of a picture changed the multisampled pixels of what was drawn
  // around it on the Retroid's Adreno 650 (thin branches in front of a house paler for one
  // picture, about every third picture, with 2x and 4x MSAA only; the replay on the device drew
  // the one picture of a series with the switch differently in every MSAA variant, 20.10.2026).
  // Before a chunk the quarter must have room for the largest chunk so far and half again (at
  // least kStreamReserve); only a chunk larger than that still switches inside
  // (GxStats::streamSplits).
  static constexpr unsigned kStreamParts = 4;
  static constexpr std::size_t kStreamPart = std::size_t(4) << 20;
  static constexpr std::size_t kStreamReserve = std::size_t(2) << 20;
  GLuint stream = 0;
  std::uint8_t *streamMap = nullptr; // persistent mapping (null: a map per block)
  std::size_t streamPos = 0, streamAlign = 256;
  unsigned streamPart = 0;
  GLsync streamFence[kStreamParts] = {};
  bool streamMoved = false;          // StreamNextPart since the last draw's uploads (UploadXf)
  std::size_t streamChunkBytes = 0;  // what the running chunk took of the stream so far
  std::size_t streamChunkMost = 0;   // the most a chunk took

  // The next quarter, once the GPU is done with what used it last.
  void StreamNextPart() {
    streamMoved = true;
    streamFence[streamPart] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    streamPart = (streamPart + 1) % kStreamParts;
    if (GLsync fence = streamFence[streamPart]) {
      if (glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0) == GL_TIMEOUT_EXPIRED) {
        stats.streamWaits++;
        glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
      }
      glDeleteSync(fence);
      streamFence[streamPart] = nullptr;
    }
    streamPos = streamPart * kStreamPart;
    stats.streamSwitches++;
  }

  // Before a chunk's first draw: the next quarter when this one may not hold the chunk. The
  // largest chunk is forgotten slowly (halved in about 45 chunks): one large scene does not
  // keep every later chunk in a quarter of its own.
  void StreamChunkBegin() {
    streamChunkMost = std::max(streamChunkBytes, streamChunkMost - streamChunkMost / 64);
    streamChunkBytes = 0;
    const std::size_t want = std::max(kStreamReserve, streamChunkMost + streamChunkMost / 2);
    if (streamPos + std::min(want, kStreamPart) > (streamPart + 1) * kStreamPart)
      StreamNextPart();
  }

  // Room for `bytes` in the stream: its offset.
  std::size_t StreamAlloc(std::size_t bytes) {
    std::size_t at = (streamPos + streamAlign - 1) / streamAlign * streamAlign;
    if (at + bytes > (streamPart + 1) * kStreamPart) {
      // a chunk larger than the room StreamChunkBegin left
      if (!stats.streamSplits)
        GXLOG("GX uniform stream: a chunk needs more than a quarter's rest (%zu KB in it so far, the most %zu KB): "
              "switched while drawing",
              streamChunkBytes >> 10, streamChunkMost >> 10);
      stats.streamSplits++;
      StreamNextPart();
      at = streamPos;
    }
    streamChunkBytes += at + bytes - streamPos;
    streamPos = at + bytes;
    return at;
  }

  // A uniform block to its binding: in the stream, or into the small buffer.
  void UploadBlock(GLuint binding, GLuint buffer, const void *data, std::size_t offset, std::size_t bytes,
                   std::size_t blockBytes) {
    stats.uniformUploads++;
    if (!stream) {
      glBindBuffer(GL_UNIFORM_BUFFER, buffer);
      glBufferSubData(GL_UNIFORM_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes),
                      static_cast<const std::uint8_t *>(data) + offset);
      stats.uniformBytes += static_cast<unsigned>(bytes);
      return;
    }
    // the stream holds whole blocks
    const std::size_t at = StreamAlloc(blockBytes);
    if (streamMap) {
      std::memcpy(streamMap + at, data, blockBytes);
    } else {
      glBindBuffer(GL_UNIFORM_BUFFER, stream);
      void *dst = glMapBufferRange(GL_UNIFORM_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(blockBytes),
                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
      if (dst) {
        std::memcpy(dst, data, blockBytes);
        glUnmapBuffer(GL_UNIFORM_BUFFER);
      } else {
        glBufferSubData(GL_UNIFORM_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(blockBytes), data);
      }
    }
    glBindBufferRange(GL_UNIFORM_BUFFER, binding, stream, static_cast<GLintptr>(at),
                      static_cast<GLsizeiptr>(blockBytes));
    stats.uniformBytes += static_cast<unsigned>(blockBytes);
  }

  void StreamInit() {
    const char *off = std::getenv("RE4_GX_UNIFORM_STREAM");
    if (off && *off == '0')
      return;
    GLint align = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    streamAlign = static_cast<std::size_t>(std::max(16, align));
    const std::size_t size = kStreamParts * kStreamPart;
    glGenBuffers(1, &stream);
    glBindBuffer(GL_UNIFORM_BUFFER, stream);
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    using BufferStorage = void (*)(GLenum, GLsizeiptr, const void *, GLbitfield);
    BufferStorage storage = nullptr;
    if (ext && std::strstr(ext, "GL_EXT_buffer_storage"))
      storage = reinterpret_cast<BufferStorage>(GlExtension("glBufferStorageEXT"));
    const char *persistent = std::getenv("RE4_GX_PERSISTENT");
    if (storage && !(persistent && *persistent == '0')) {
      const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT;
      storage(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(size), nullptr, flags);
      streamMap = static_cast<std::uint8_t *>(glMapBufferRange(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(size), flags));
    }
    if (!streamMap) {
      glDeleteBuffers(1, &stream);
      glGenBuffers(1, &stream);
      glBindBuffer(GL_UNIFORM_BUFFER, stream);
      glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);
    }
    GXLOG("GX uniform stream: %u MB, %s, offsets aligned to %u", static_cast<unsigned>(size >> 20),
          streamMap ? "persistent mapping" : "a map per block", static_cast<unsigned>(streamAlign));
  }

  // The ring for the in-between passes' blended vertices (EXT_buffer_storage, persistent and
  // coherent); without it blendVbo with a map per draw. RE4_GX_PERSISTENT=0: never.
  void BlendRingInit() {
    blendRingTried = true;
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    using BufferStorage = void (*)(GLenum, GLsizeiptr, const void *, GLbitfield);
    BufferStorage storage = nullptr;
    if (ext && std::strstr(ext, "GL_EXT_buffer_storage"))
      storage = reinterpret_cast<BufferStorage>(GlExtension("glBufferStorageEXT"));
    const char *persistent = std::getenv("RE4_GX_PERSISTENT");
    if (!storage || (persistent && *persistent == '0'))
      return;
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT;
    const std::size_t size = kBlendParts * kBlendPart;
    glGenBuffers(1, &blendRing);
    glBindBuffer(GL_ARRAY_BUFFER, blendRing);
    storage(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr, flags);
    blendMap = static_cast<std::uint8_t *>(glMapBufferRange(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size), flags));
    if (!blendMap) {
      glDeleteBuffers(1, &blendRing);
      blendRing = 0;
    }
    GXLOG("GX in-between vertices: %s", blendMap ? "a ring of 24 MB, mapped for good" : "a map per draw");
  }

  void BlendRingRelease() {
    for (GLsync &f : blendFence) {
      if (f)
        glDeleteSync(f);
      f = nullptr;
    }
    if (blendRing) {
      if (blendMap) {
        glBindBuffer(GL_ARRAY_BUFFER, blendRing);
        glUnmapBuffer(GL_ARRAY_BUFFER);
      }
      glDeleteBuffers(1, &blendRing);
    }
    blendRing = 0;
    blendMap = nullptr;
    blendRingTried = false;
  }

  void StreamRelease() {
    for (GLsync &f : streamFence) {
      if (f)
        glDeleteSync(f);
      f = nullptr;
    }
    if (stream) {
      if (streamMap) {
        glBindBuffer(GL_UNIFORM_BUFFER, stream);
        glUnmapBuffer(GL_UNIFORM_BUFFER);
      }
      glDeleteBuffers(1, &stream);
    }
    stream = 0;
    streamMap = nullptr;
  }

  // --- GPU time (EXT_disjoint_timer_query): the chunks and the presents -----------
  // A few queries in turn for each kind; a result is read when it is ready (frames later), and a
  // chunk or present whose queries are all still running is not timed. The two kinds never nest:
  // Present runs after Run.
  struct TimerPool {
    static constexpr int kQueries = 4;
    GLuint query[kQueries] = {};
    bool pending[kQueries] = {};
    int next = 0;

    // Results that are ready go to micros / timed (left out when the GPU's clock was disjoint).
    void Collect(unsigned long long *micros, unsigned *timed) {
      GLint disjoint = 0;
      glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
      for (int i = 0; i < kQueries; i++) {
        if (!pending[i])
          continue;
        GLuint ready = 0;
        glGetQueryObjectuiv(query[i], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready)
          continue;
        GLuint ns = 0;
        glGetQueryObjectuiv(query[i], GL_QUERY_RESULT, &ns);
        pending[i] = false;
        if (!disjoint) {
          *micros += ns / 1000;
          (*timed)++;
        }
      }
    }

    void Begin() {
      if (pending[next]) {
        next = -1; // all still running: this one is not timed
        return;
      }
      glBeginQuery(GL_TIME_ELAPSED_EXT, query[next]);
    }

    void End() {
      if (next >= 0) {
        glEndQuery(GL_TIME_ELAPSED_EXT);
        pending[next] = true;
      }
      next = 0;
      while (next < kQueries && pending[next])
        next++;
      if (next == kQueries)
        next = 0;
    }
  };
  TimerPool chunkTimer, presentTimer;
  bool timerOn = false;

  // --- how far the GPU is behind (fences, GxStats::behind*) ------------------------
  // The timer of a chunk can read far more than a picture's interval while the pictures keep
  // coming (Retroid, 2x MSAA with the autofocus, 19.10.2026: 450 ms at 30 pictures/s); a fence
  // after every chunk tells, independent of the timer, whether the GPU keeps up: after the next
  // chunk the fences not reached yet are chunks it has not finished.
  static constexpr int kFences = 8;
  GLsync fence[kFences] = {};
  int fenceNext = 0;

  void FenceAfterChunk() {
    unsigned behind = 0;
    for (GLsync &f : fence) {
      if (!f)
        continue;
      const GLenum r = glClientWaitSync(f, 0, 0);
      if (r == GL_TIMEOUT_EXPIRED) {
        behind++;
      } else {
        glDeleteSync(f);
        f = nullptr;
      }
    }
    stats.behindSum += behind;
    stats.behindChecks++;
    stats.behindCount[std::min(behind, 8u)]++;
    // kFences chunks behind: the oldest is no longer looked at (counted above)
    if (fence[fenceNext])
      glDeleteSync(fence[fenceNext]);
    fence[fenceNext] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    fenceNext = (fenceNext + 1) % kFences;
  }

  void FenceRelease() {
    for (GLsync &f : fence) {
      if (f)
        glDeleteSync(f);
      f = nullptr;
    }
  }

  void TimerInit() {
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    timerOn = ext && std::strstr(ext, "GL_EXT_disjoint_timer_query");
    // Mali drivers crash in the timer queries (moto g72, Mali-G57 MC2 / MT6789, 0.40-0.41.3:
    // SIGSEGV at address 0 in libGLES_mali.so under TimerPool::Collect, again and again after
    // minutes to an hour of play); the GPU time is a diagnosis only, so not there
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    if (timerOn && renderer && std::strstr(renderer, "Mali")) {
      timerOn = false;
      GXLOG("GPU time: off on %s (its driver crashes in timer queries)", renderer);
    }
    if (timerOn) {
      glGenQueries(TimerPool::kQueries, chunkTimer.query);
      glGenQueries(TimerPool::kQueries, presentTimer.query);
    }
  }

  // Results that are ready go to the stats; then the query of this chunk starts.
  void TimerBegin() {
    if (!timerOn)
      return;
    chunkTimer.Collect(&stats.gpuMicros, &stats.gpuTimed);
    chunkTimer.Begin();
  }

  void TimerEnd() {
    if (timerOn)
      chunkTimer.End();
  }

  void PresentTimerBegin() {
    if (!timerOn)
      return;
    presentTimer.Collect(&stats.presentMicros, &stats.presentTimed);
    presentTimer.Begin();
  }

  void PresentTimerEnd() {
    if (timerOn)
      presentTimer.End();
  }

  // per-draw caches
  const Program *current = nullptr;
  XfBlock xf = {};
  // the XF parts as last sent (UploadXf): a part the game wrote again with the same values is not
  // sent again (about 5 % of the uploads of a village picture, Retroid series r226, 28.10.2026)
  XfBlock xfSent = {};
  bool xfSentValid[kXfParts] = {};
  PixelBlock px = {};
  PixelBlock pxSent = {};
  bool pxValid = false;
  std::uint32_t copySerial = 0;

  // --- in-between pictures (60 FPS, experimental; GxRenderer::Interpolate) ----------------
  // A real Run notes each draw: what identifies it (DrawSig), the view-space translation of its
  // position matrix, the position and normal rows it used and the projection. The in-between pass
  // runs the next chunk once more; a draw that matches a noted one is drawn with its rows (and,
  // when they changed, its vertices: the figures the game skins on the CPU) blended between the
  // two pictures. Everything the pass changes is put back afterwards (Interpolate).
  std::uint64_t boundKey[8] = {};  // what each map holds: texture key, or copy address | 1 << 63
  struct DrawRec {
    std::uint64_t sig = 0;
    float t[3] = {};          // view-space translation of the draw's (first) position matrix
    std::uint32_t posRow = 0; // first position row noted (pi)
    std::uint32_t rows = 0;   // 3, 30 with matrix indices per vertex, 0 with the identity
    std::uint32_t data = 0;   // into recFloats: rows x 4 position, rows x 3 normal, 6 projection floats
    std::uint32_t vertexOffset = 0, vertexCount = 0, stride = 0, layout = 0;
    bool perspective = false;
    std::uint64_t look = 0;  // a particle's signature without its textures (0: not a particle)
    // the object's draw (RE4_GX_OP_OWNER, patch 0137: object, function, table and the n-th draw of
    // the object; 0: none), its shape (the signature without the textures), a particle's id << 16 |
    // life frames (~0: none), and for an owned draw where its texture matrices (8 x 12 floats), the
    // TEV colours (32 words) and the light colours (8 x 4 floats) lie in recFloats (~0: not kept)
    std::uint64_t owner = 0, shape = 0;
    std::uint32_t life = ~0u, extra = ~0u;
    std::uint64_t base = 0;  // the object (ownerBase) of an owned draw
  };
  bool recordDraws = false;
  bool recValid = false;            // rec describes the chunk run last
  std::uint32_t recSequence = 0;
  std::vector<DrawRec> rec, recNew;
  std::vector<float> recFloats, recFloatsNew;
  bool interpPass = false;
  float interpT = 0.5f;
  const Re4GxChunk *interpPrev = nullptr;  // the chunk rec describes (its vertices)
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> recBySig, recByLook;
  // owner tags (60fps-plan.md S1/S2): the object of the draws that follow (0: none), its tag's life
  // word, how many of its draws came; the draw's own key (object and n-th draw) while it is drawn
  std::uint64_t ownerBase = 0, curOwner = 0;
  std::uint32_t ownerLife = ~0u, ownerDraws = 0, curLife = ~0u;
  std::unordered_map<std::uint64_t, std::uint32_t> recByOwner;
  // draws per object (ownerBase): of the chunk noted (recOwnerDraws, filled while recording) and of the
  // next one (nextOwnerDraws, counted ahead by Interpolate). An object that draws more or fewer quads
  // than before (a flame's trail of sprites growing) is not matched by "n-th draw": its sprites would
  // take each other's places (Retroid 28.10.2026, big torches: now and then a wrong in-between picture)
  std::unordered_map<std::uint64_t, std::uint32_t> recOwnerDraws, recOwnerDrawsNew, nextOwnerDraws;
  // what a blended draw changed beyond its position rows (BlendRestore puts it back): texture
  // matrix rows, 0x100 for the TEV colours (tevSave: before the blend), 0x200 + row for a post-transform
  // matrix, 0x300 for the indirect matrices (indSave: the BP words before the blend)
  std::vector<std::uint32_t> texRowsBlended;
  std::int32_t tevSave[2][4][4] = {};
  std::uint32_t indSave[9] = {};
  std::vector<std::uint8_t> recUsed;
  // noted draws of an object that draws as often in the next picture: they belong to its draws there
  // (by the owner tag), the guessing (MatchDraw) leaves them alone. Taken by a draw it guessed, the
  // object's own draw found its draw gone and guessed in turn, and so on down the line (r307 puzzle,
  // 30.10.2026: a turn starts new sparkles; the pieces' triangles were drawn between two fields,
  // squashed, in the in-between picture: ghosts)
  std::vector<std::uint8_t> recReserved;
  std::unordered_map<std::uint32_t, Copy> interpCopies;  // the pass's own EFB copies
  std::unordered_set<std::uint32_t> interpCopied;         // addresses it copied to
  std::unordered_set<std::uint64_t> interpUploads;        // textures it uploaded (Run(next) skips them)
  bool skipInterpUploads = false;
  std::vector<std::uint8_t> blendVertices;
  // the pass's counts: perspective draws, matched, turned more than 20 degrees
  unsigned ipDraws = 0, ipMatched = 0, ipTurned = 0;
  // The camera's move from the picture before to the next one in the pass (view space: next =
  // cam * before), read off the large draws of a look of their own: each proposes its move, and
  // another object (another matrix) that moved the same way votes for it; the move with the most
  // votes, at least two, counts (the room, of many pieces that stand still). MatchDraw takes it out
  // of a particle's distances, so that it is matched by its own move, not the camera's.
  struct CamGuess {
    float c[3][4];
    float t[3];  // the proposing draw's translation (its parts with the same matrix do not vote)
    unsigned votes;
  };
  CamGuess camGuess[4] = {};
  unsigned camGuesses = 0, camVotes = 0;
  float cam[3][4] = {};
  bool camValid = false;

  // What identifies a draw from one picture to the next: its shader (the program of the same
  // registers), primitive, vertex layout and count, matrix indices and the textures it samples
  // (without them: a particle's look, see BlendDraw).
  std::uint64_t DrawSig(const Re4GxDraw &d, const ShaderKey &k, bool textures = true) const {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](std::uint64_t v) {
      h ^= v;
      h *= 1099511628211ull;
    };
    mix(reinterpret_cast<std::uintptr_t>(keyProgram));
    mix(d.primitive);
    mix(d.layout);
    mix(d.stride);
    mix(d.vertexCount);
    mix(d.indexCount);
    mix(d.matrices[0]);
    mix(d.matrices[1]);
    mix(d.matrices[2]);
    for (int m = 0; textures && m < 8; m++)
      if (k.texMaps & (1u << m))
        mix(boundKey[m]);
    return h;
  }

  // The position rows a draw transforms with: 3 from its matrix index, all 30 when its vertices
  // carry indices, none with the identity (60) or a texture row.
  static void DrawRows(const Re4GxDraw &d, std::uint32_t *first, std::uint32_t *count) {
    if (d.layout & RE4_GX_V_MATRIX) {
      *first = 0;
      *count = 30;
      return;
    }
    const std::uint32_t pi = d.matrices[0] & 63;
    *first = pi;
    *count = pi + 2 < 30 ? 3 : 0;
  }

  // a real Run: the draw as it is drawn now
  void RecordDraw(const Re4GxDraw &d, const ShaderKey &k) {
    DrawRec r;
    r.sig = DrawSig(d, k);
    DrawRows(d, &r.posRow, &r.rows);
    r.data = static_cast<std::uint32_t>(recFloatsNew.size());
    r.vertexOffset = d.vertexOffset;
    r.vertexCount = d.vertexCount;
    r.stride = d.stride;
    r.layout = d.layout;
    r.perspective = !k.orthographic;
    if (r.rows == 3 && d.vertexCount <= 16)
      r.look = DrawSig(d, k, false);
    if (r.rows) {
      for (int c = 0; c < 3; c++)
        r.t[c] = xf.posTex[r.posRow + c][3];
    }
    for (std::uint32_t i = 0; i < r.rows; i++)
      recFloatsNew.insert(recFloatsNew.end(), xf.posTex[r.posRow + i], xf.posTex[r.posRow + i] + 4);
    for (std::uint32_t i = 0; i < r.rows; i++)
      recFloatsNew.insert(recFloatsNew.end(), xf.normal[(r.posRow + i) & 31], xf.normal[(r.posRow + i) & 31] + 3);
    for (int i = 0; i < 6; i++)
      recFloatsNew.push_back(xf.proj[i / 4][i % 4]);
    recFloatsNew.insert(recFloatsNew.end(), &xf.matAmb[0][0], &xf.matAmb[0][0] + 16);
    r.owner = curOwner;
    r.base = curOwner ? ownerBase : 0;
    r.life = curLife;
    r.shape = DrawSig(d, k, false);
    if (curOwner) {
      r.extra = static_cast<std::uint32_t>(recFloatsNew.size());
      for (int n = 0; n < 8; n++) {
        const std::uint32_t row = TexRow(d, n);
        for (std::uint32_t i = 0; i < 3; i++) {
          if (row < 60 && row + i < 64)
            recFloatsNew.insert(recFloatsNew.end(), xf.posTex[row + i], xf.posTex[row + i] + 4);
          else
            recFloatsNew.insert(recFloatsNew.end(), 4, 0.0f);
        }
      }
      float tev[32];
      std::memcpy(tev, tevColor, sizeof(tevColor));
      std::memcpy(tev + 16, tevKonst, sizeof(tevKonst));
      recFloatsNew.insert(recFloatsNew.end(), tev, tev + 32);
      for (int l = 0; l < 8; l++)
        recFloatsNew.insert(recFloatsNew.end(), xf.light[l][0], xf.light[l][0] + 4);
      // the post-transform matrices of the coordinates (dual texture: water and fog scroll there)
      for (int n = 0; n < 8; n++) {
        const std::uint32_t row = k.dualTex ? k.postGen[n] & 63 : 64;
        for (std::uint32_t i = 0; i < 3; i++) {
          if (row + i < 64)
            recFloatsNew.insert(recFloatsNew.end(), xf.post[row + i], xf.post[row + i] + 4);
          else
            recFloatsNew.insert(recFloatsNew.end(), 4, 0.0f);
        }
      }
      // the indirect matrices (BP 0x06..0x0E: the distortion of water and heat haze)
      for (int i = 0; i < 9; i++) {
        float w;
        std::memcpy(&w, &bp[0x06 + i], 4);
        recFloatsNew.push_back(w);
      }
    }
    recNew.push_back(r);
  }

  // The XF row of texture coordinate n's matrix from the draw's defaults (60: the identity; with
  // indices in the vertices: 60, not looked at).
  static std::uint32_t TexRow(const Re4GxDraw &d, int n) {
    if (d.layout & RE4_GX_V_MATRIX)
      return 60;
    const std::uint32_t w = n < 3 ? d.matrices[0] >> (8 * (n + 1)) : n < 7 ? d.matrices[1] >> (8 * (n - 3)) : d.matrices[2];
    return w & 63;
  }

  // the owner tag (RE4_GX_OP_OWNER): object, function, table (0xFF: none), life word
  static std::uint64_t OwnerKey(const std::uint32_t *p) {
    if (!p[0] || (p[2] & 0xFF) == 0xFF)
      return 0;
    std::uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < 3; i++) {
      h ^= p[i];
      h *= 1099511628211ull;
    }
    return h | 1;
  }
  void SetOwner(const std::uint32_t *p) {
    ownerBase = OwnerKey(p);
    ownerLife = p[3];
    ownerDraws = 0;
  }

  // The noted draw of the picture before that this one is: the same signature; the first one not
  // yet taken when it lies near enough (the game draws in the same order from one picture to the
  // next), else of the first eight the nearest that does; none when no one moved less than a
  // quarter of its distance plus 400 units (another object of the same look).
  // A particle (a small draw with a matrix of its own, at most 16 vertices) instead: the first one
  // not taken unless another of the first 32 lies clearly nearer (by more than 2 % of its distance
  // plus 50 units). The particles of smoke and fire look alike, lie a few thousand units apart,
  // and one that dies or starts shifts the order (Retroid report 26.10.2026, r224: neighbours
  // swapped places in the in-between pictures, the smoke flickered). With the camera's move known
  // (cam), their positions before are moved by it first, and a particle may move only 5 % of its
  // distance plus 300 units: one that has just started is drawn as in the next picture instead of
  // taking another's place. Only particles: the camera's move is the room's, and the parts of Leon
  // turn with the camera; matched by it, two alike parts of him swapped places and their blended
  // vertices drew a long bar at the bottom of the picture (Retroid report 26.10.2026, fast turns).
  // (flat: a 2D draw, in screen units; by the game's order)
  const DrawRec *MatchDraw(const std::vector<std::uint32_t> &candidates, const float *t, std::uint32_t rows,
                           std::uint32_t vertexCount, bool flat = false) {
    const float reach = rows ? std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]) : 0.0f;
    const bool particle = !flat && rows == 3 && vertexCount <= 16, moved = particle && camValid;
    const float limit = moved ? 0.05f * reach + 300.0f : 0.25f * reach + 400.0f;
    const float close = 0.02f * reach + 50.0f;
    int first = -1, best = -1;
    float firstDist = 0.0f, bestDist = 0.0f;
    unsigned looked = 0;
    for (std::uint32_t i : candidates) {
      if (recUsed[i] || recReserved[i])
        continue;
      const DrawRec &r = rec[i];
      float p[3] = {r.t[0], r.t[1], r.t[2]};
      if (moved)
        CameraApply(cam, r.t, p);
      const float dx = p[0] - t[0], dy = p[1] - t[1], dz = p[2] - t[2];
      const float dist = rows ? std::sqrt(dx * dx + dy * dy + dz * dz) : 0.0f;
      if (first < 0) {
        first = static_cast<int>(i);
        firstDist = dist;
        if (dist <= (particle ? close : limit))
          break;  // near enough (a particle: where it was): no need to look further
      }
      if (dist <= limit && (best < 0 || dist < bestDist)) {
        best = static_cast<int>(i);
        bestDist = dist;
      }
      if (++looked >= (particle ? 32u : 8u))
        break;
    }
    if (first >= 0 && firstDist <= limit && (!particle || best < 0 || firstDist <= bestDist + close))
      best = first;
    if (best < 0)
      return nullptr;
    recUsed[static_cast<std::size_t>(best)] = 1;
    return &rec[static_cast<std::size_t>(best)];
  }

  // q moved by the camera's move c (a point in view space before -> after).
  static void CameraApply(const float (&c)[3][4], const float *q, float *out) {
    for (int r = 0; r < 3; r++)
      out[r] = c[r][0] * q[0] + c[r][1] * q[1] + c[r][2] * q[2] + c[r][3];
  }

  // c = n * p^-1 for two 3 x 4 modelviews of the same object (p: row-major, 12 floats); false when
  // p has no inverse.
  static bool CameraMove(const float *p, const float (&n)[3][4], float (&c)[3][4]) {
    const float det = p[0] * (p[5] * p[10] - p[6] * p[9]) - p[1] * (p[4] * p[10] - p[6] * p[8]) +
                      p[2] * (p[4] * p[9] - p[5] * p[8]);
    if (!(std::fabs(det) > 1e-12f))
      return false;
    const float inv[3][3] = {{(p[5] * p[10] - p[6] * p[9]) / det, (p[2] * p[9] - p[1] * p[10]) / det,
                              (p[1] * p[6] - p[2] * p[5]) / det},
                             {(p[6] * p[8] - p[4] * p[10]) / det, (p[0] * p[10] - p[2] * p[8]) / det,
                              (p[2] * p[4] - p[0] * p[6]) / det},
                             {(p[4] * p[9] - p[5] * p[8]) / det, (p[1] * p[8] - p[0] * p[9]) / det,
                              (p[0] * p[5] - p[1] * p[4]) / det}};
    for (int r = 0; r < 3; r++) {
      for (int k = 0; k < 3; k++)
        c[r][k] = n[r][0] * inv[0][k] + n[r][1] * inv[1][k] + n[r][2] * inv[2][k];
      c[r][3] = n[r][3] - (c[r][0] * p[3] + c[r][1] * p[7] + c[r][2] * p[11]);
    }
    return true;
  }

  // A large draw of a look of its own (one of it in the picture before), matched: its move is the
  // camera's when it stands still. It votes for a move proposed before that takes its position
  // before to where it is now (when it has a matrix of its own: two parts of one object that moved
  // agreed with each other, Retroid series 26.10.2026 at the merchant), else proposes its own.
  void NoteCamera(std::uint64_t sig, const float *before, const float (&now)[3][4]) {
    auto it = recBySig.find(sig);
    if (it == recBySig.end() || it->second.size() != 1)
      return;
    const float beforeT[3] = {before[3], before[7], before[11]}, nowT[3] = {now[0][3], now[1][3], now[2][3]};
    const float reach = std::sqrt(nowT[0] * nowT[0] + nowT[1] * nowT[1] + nowT[2] * nowT[2]);
    for (unsigned g = 0; g < camGuesses; g++) {
      CamGuess &q = camGuess[g];
      float p[3];
      CameraApply(q.c, beforeT, p);
      const float dx = p[0] - nowT[0], dy = p[1] - nowT[1], dz = p[2] - nowT[2];
      if (std::sqrt(dx * dx + dy * dy + dz * dz) > 0.02f * reach + 50.0f)
        continue;
      const float sx = nowT[0] - q.t[0], sy = nowT[1] - q.t[1], sz = nowT[2] - q.t[2];
      if (sx * sx + sy * sy + sz * sz >= 1.0f && ++q.votes > camVotes) {
        std::memcpy(cam, q.c, sizeof(cam));
        camVotes = q.votes;
        camValid = true;
      }
      return;
    }
    if (camGuesses < 4) {
      CamGuess &q = camGuess[camGuesses];
      if (!CameraMove(before, now, q.c))
        return;
      std::memcpy(q.t, nowT, sizeof(q.t));
      q.votes = 1;
      camGuesses++;
    }
  }

  // The in-between pass, before a draw: its rows (and vertices) between the picture before (a)
  // and now; how many rows to put back afterwards (BlendRestore).
  // 2D (the HUD, full-screen passes, a glow laid over a flame in screen space) as in the next picture,
  // unless it has a matrix of its own that only moved, by at most a quarter of the picture
  // (kFlatMove, screen units): then between the two too, without lights and projection. Retroid
  // series 26.10.2026: the glow of the merchant's brazier, a 2D square 418 units wide, moved 23
  // units a game picture while the camera turned and jumped in the in-between pictures. Not when it
  // turned or changed its size: r224 lays a copy of the picture over itself through a square that
  // turns 10 degrees a game picture (its texture coordinates come from the square's corners), and
  // half turned it showed the room twice.
  static constexpr float kFlatMove = 128.0f;
  std::uint32_t BlendDraw(const Re4GxDraw &d, const ShaderKey &k, const Re4GxChunk &chunk) {
    const bool flat = k.orthographic;
    std::uint32_t first = 0, rows = 0;
    bool lights = false;
    DrawRows(d, &first, &rows);
    if (flat && rows != 3)
      return 0;
    if (!flat)
      ipDraws++;
    float t[3] = {};
    if (rows)
      for (int c = 0; c < 3; c++)
        t[c] = xf.posTex[first + c][3];
    const std::uint64_t sig = DrawSig(d, k);
    // its kind for the counts (60fps-plan.md S0): 0 a particle, 1 a figure (matrices per vertex),
    // 2 the room and other 3D, 3 2D
    const int kind = flat ? 3 : rows == 30 ? 1 : rows == 3 && d.vertexCount <= 16 ? 0 : 2;
    stats.interpKind[kind]++;
    // the object's draw of the picture before (S2): the same object, the same shape; a particle's
    // slot used anew (another effect, fewer life frames) is a new one, drawn as in the next picture
    const DrawRec *a = nullptr;
    bool owned = false;
    bool sameCount = false;
    if (curOwner) {
      auto pc = recOwnerDraws.find(ownerBase), nc = nextOwnerDraws.find(ownerBase);
      sameCount = pc != recOwnerDraws.end() && nc != nextOwnerDraws.end() && pc->second == nc->second;
      if (!sameCount)
        stats.interpOwnedRecount++;
    }
    if (curOwner && sameCount) {
      auto o = recByOwner.find(curOwner);
      if (o != recByOwner.end() && !recUsed[o->second] && rec[o->second].shape == DrawSig(d, k, false)) {
        const DrawRec &c = rec[o->second];
        recUsed[o->second] = 1;
        if (c.life != ~0u && curLife != ~0u &&
            ((c.life >> 16) != (curLife >> 16) || (curLife & 0xFFFF) < (c.life & 0xFFFF))) {
          stats.interpOwnedNew++;
          stats.interpKindUnmatched[kind]++;
          return 0;
        }
        a = &c;
        owned = true;
        stats.interpOwned++;
      }
    }
    // a particle whose slot drew nothing in the picture before: a new one (a turn of the r307 puzzle
    // starts sparkles), drawn as in the next picture; guessed, it took another's place
    if (!a && curOwner && curLife != ~0u && !recOwnerDraws.count(ownerBase)) {
      stats.interpOwnedNew++;
      stats.interpKindUnmatched[kind]++;
      return 0;
    }
    auto it = recBySig.find(sig);
    if (!a)
      a = it != recBySig.end() ? MatchDraw(it->second, t, rows, d.vertexCount, flat) : nullptr;
    // a particle with none of its look: by the look without its textures. Flames show a new picture
    // of their animation, a texture of its own, every game picture (Retroid report 26.10.2026, the
    // merchant's brazier: unmatched, the flames were drawn where they are in the next picture and
    // lagged behind the brazier while the camera turned). The texture stays the next picture's.
    if (!a && !flat && rows == 3 && d.vertexCount <= 16) {
      auto look = recByLook.find(DrawSig(d, k, false));
      if (look != recByLook.end())
        a = MatchDraw(look->second, t, rows, d.vertexCount);
    }
    if (a && !flat && rows == 3 && a->rows == 3 && a->posRow == first && d.vertexCount >= 64) {
      float now[3][4];
      std::memcpy(now, xf.posTex[first], sizeof(now));
      NoteCamera(sig, recFloats.data() + a->data, now);
    }
    if (!a) {
      stats.interpKindUnmatched[kind]++;
      return 0;
    }
    const float s = interpT;
    const float *src = recFloats.data() + a->data;
    if (flat) {
      const float dx = xf.posTex[first][3] - src[3], dy = xf.posTex[first + 1][3] - src[7];
      if (a->rows != 3 || a->posRow != first || !(dx * dx + dy * dy <= kFlatMove * kFlatMove))
        return 0;
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
          if (!(std::fabs(xf.posTex[first + r][c] - src[r * 4 + c]) <= 1e-3f * std::max(1.0f, std::fabs(src[r * 4 + c]))))
            return 0;
    } else {
      ipMatched++;
    }
    if (rows && a->rows == rows && a->posRow == first) {
      // turned more than 20 degrees: a cut of the camera, most likely (Interpolate counts them)
      if (rows == 3 && !flat) {
        float lowest = 1.0f;
        for (int r = 0; r < 3; r++) {
          const float *pa = src + r * 4, *pb = xf.posTex[first + r];
          const float la = std::sqrt(pa[0] * pa[0] + pa[1] * pa[1] + pa[2] * pa[2]);
          const float lb = std::sqrt(pb[0] * pb[0] + pb[1] * pb[1] + pb[2] * pb[2]);
          if (la > 0.0f && lb > 0.0f)
            lowest = std::min(lowest, (pa[0] * pb[0] + pa[1] * pb[1] + pa[2] * pb[2]) / (la * lb));
        }
        if (lowest < 0.94f)
          ipTurned++;
      }
      float now[3][4] = {};
      if (rows == 3)
        std::memcpy(now, xf.posTex[first], sizeof(now));
      for (std::uint32_t r = 0; r < rows; r++)
        for (int c = 0; c < 4; c++)
          xf.posTex[first + r][c] = src[r * 4 + c] + (xf.posTex[first + r][c] - src[r * 4 + c]) * s;
      // the lights (in view space, as the game loaded them for the next picture) moved with the
      // camera between the two: tester's report 24.10.2026, light pools that swam in 60 FPS
      if (rows == 3 && !flat && BlendLights(now, xf.posTex[first]))
        lights = true;
      const float *nrm = src + rows * 4;
      for (std::uint32_t r = 0; r < rows; r++)
        for (int c = 0; c < 3; c++)
          xf.normal[(first + r) & 31][c] = nrm[r * 3 + c] + (xf.normal[(first + r) & 31][c] - nrm[r * 3 + c]) * s;
      Dirty(static_cast<int>(first), static_cast<int>(first + rows));
      Dirty(64, 96);
    }
    // the projection (a zoom): between the two
    const float *proj = src + rows * 4 + rows * 3;
    if (a->perspective && (a->rows == rows)) {
      for (int i = 0; i < 6; i++)
        xf.proj[i / 4][i % 4] = proj[i] + (xf.proj[i / 4][i % 4] - proj[i]) * s;
      Dirty(206, 208);
    }
    // the material and ambient colours: between the two (particles fade in and out by their
    // material colour; Retroid report 26.10.2026: the flames changed only with the game's 30
    // pictures a second). FillXfRegs puts them back after the draw.
    bool colors = false;
    const float *mat = src + a->rows * 4 + a->rows * 3 + 6;
    if (std::memcmp(mat, &xf.matAmb[0][0], sizeof(xf.matAmb)) != 0) {
      for (int i = 0; i < 16; i++)
        xf.matAmb[i / 4][i % 4] = mat[i] + (xf.matAmb[i / 4][i % 4] - mat[i]) * s;
      Dirty(200, 204);
      colors = true;
    }
    // with the object sure (an owned draw, S3): its texture matrices (flowing water, lava, drifting
    // fog) unless a coordinate jumped by more than half the texture (it ran over its edge and
    // starts again), the TEV colours (a glow) and the light colours (torchlight) between the two
    bool extras = false;
    if (owned && a->extra != ~0u) {
      const float *ex = recFloats.data() + a->extra;
      texRowsBlended.clear();
      for (int n = 0; n < 8; n++) {
        const std::uint32_t row = TexRow(d, n);
        if (row >= 60 || row + 3 > 64)
          continue;
        const float *was = ex + n * 12;
        bool same = true, jump = false;
        for (int r = 0; r < 3; r++) {
          for (int c = 0; c < 4; c++) {
            const float diff = xf.posTex[row + r][c] - was[r * 4 + c];
            same = same && diff == 0.0f;
            if (c == 3 && r < 2 && std::fabs(diff) > 0.5f)
              jump = true;
          }
        }
        if (same || std::find(texRowsBlended.begin(), texRowsBlended.end(), row) != texRowsBlended.end())
          continue;
        if (jump) {
          stats.interpTexWraps++;
          continue;
        }
        for (int r = 0; r < 3; r++)
          for (int c = 0; c < 4; c++)
            xf.posTex[row + r][c] = was[r * 4 + c] + (xf.posTex[row + r][c] - was[r * 4 + c]) * s;
        texRowsBlended.push_back(row);
        Dirty(static_cast<int>(row), static_cast<int>(row + 3));
        stats.interpTexBlends++;
        extras = true;
      }
      std::int32_t tev[2][4][4];
      std::memcpy(tev, ex + 96, sizeof(tev));
      if (std::memcmp(tev[0], tevColor, sizeof(tevColor)) != 0 || std::memcmp(tev[1], tevKonst, sizeof(tevKonst)) != 0) {
        std::memcpy(tevSave[0], tevColor, sizeof(tevColor));
        std::memcpy(tevSave[1], tevKonst, sizeof(tevKonst));
        for (int i = 0; i < 4; i++) {
          for (int c = 0; c < 4; c++) {
            tevColor[i][c] = static_cast<std::int32_t>(std::lround(tev[0][i][c] + (tevColor[i][c] - tev[0][i][c]) * s));
            tevKonst[i][c] = static_cast<std::int32_t>(std::lround(tev[1][i][c] + (tevKonst[i][c] - tev[1][i][c]) * s));
          }
        }
        pxDirty = true;
        stats.interpTevBlends++;
        extras = true;
        texRowsBlended.push_back(0x100);
      }
      // post-transform matrices, as the texture matrices
      for (int n = 0; k.dualTex && n < 8; n++) {
        const std::uint32_t row = k.postGen[n] & 63;
        if (row + 3 > 64 ||
            std::find(texRowsBlended.begin(), texRowsBlended.end(), 0x200 + row) != texRowsBlended.end())
          continue;
        const float *was = ex + 160 + n * 12;
        bool same = true, jump = false;
        for (int r = 0; r < 3; r++) {
          for (int c = 0; c < 4; c++) {
            const float diff = xf.post[row + r][c] - was[r * 4 + c];
            same = same && diff == 0.0f;
            if (c == 3 && r < 2 && std::fabs(diff) > 0.5f)
              jump = true;
          }
        }
        if (same)
          continue;
        if (jump) {
          stats.interpTexWraps++;
          continue;
        }
        for (int r = 0; r < 3; r++)
          for (int c = 0; c < 4; c++)
            xf.post[row + r][c] = was[r * 4 + c] + (xf.post[row + r][c] - was[r * 4 + c]) * s;
        texRowsBlended.push_back(0x200 + row);
        Dirty(static_cast<int>(96 + row), static_cast<int>(96 + row + 3));
        stats.interpTexBlends++;
        extras = true;
      }
      // the indirect matrices, each element between the two when the scale stayed (it is shared by
      // the matrix's three words; a new scale is a new matrix: as in the next picture)
      {
        std::uint32_t was[9];
        std::memcpy(was, ex + 256, sizeof(was));
        bool changed = false;
        for (int i = 0; i < 9; i++)
          changed = changed || (was[i] & 0xFFFFFF) != (bp[0x06 + i] & 0xFFFFFF);
        if (changed) {
          std::memcpy(indSave, &bp[0x06], sizeof(indSave));
          for (int m = 0; m < 3; m++) {
            bool sameScale = true;
            for (int w = 0; w < 3; w++)
              sameScale = sameScale && ((was[3 * m + w] >> 22) & 3) == ((bp[0x06 + 3 * m + w] >> 22) & 3);
            if (!sameScale)
              continue;
            for (int w = 0; w < 3; w++) {
              const std::uint32_t a = was[3 * m + w], b = bp[0x06 + 3 * m + w];
              std::uint32_t v = b & ~0x3FFFFFu;
              for (int half = 0; half < 2; half++) {
                const int ea = Signed11(a >> (11 * half)), eb = Signed11(b >> (11 * half));
                const int e = static_cast<int>(std::lround(ea + (eb - ea) * s));
                v |= (static_cast<std::uint32_t>(e) & 0x7FFu) << (11 * half);
              }
              bp[0x06 + 3 * m + w] = v;
            }
          }
          pxDirty = true;
          texRowsBlended.push_back(0x300);
          stats.interpIndBlends++;
          extras = true;
        }
      }
      const float *lc = ex + 128;
      bool lightsDiffer = false;
      for (int l = 0; l < 8 && !lightsDiffer; l++)
        lightsDiffer = std::memcmp(lc + l * 4, xf.light[l][0], 4 * sizeof(float)) != 0;
      if (lightsDiffer) {
        for (int l = 0; l < 8; l++)
          for (int c = 0; c < 4; c++)
            xf.light[l][0][c] = lc[l * 4 + c] + (xf.light[l][0][c] - lc[l * 4 + c]) * s;
        Dirty(160, 200);
        lights = true;
        stats.interpLightBlends++;
      }
    }
    // vertices the game moved itself (skinning): positions and normals between the two
    if (interpPrev && a->vertexCount == d.vertexCount && a->stride == d.stride && a->layout == d.layout &&
        d.stride >= 12 && a->vertexOffset + static_cast<std::uint64_t>(a->vertexCount) * a->stride <=
                              interpPrev->vertexBytes) {
      const std::uint8_t *pa = interpPrev->vertices + a->vertexOffset, *pb = chunk.vertices + d.vertexOffset;
      const std::size_t bytes = static_cast<std::size_t>(d.vertexCount) * d.stride;
      if (std::memcmp(pa, pb, bytes) != 0) {
        blendVertices.assign(pb, pb + bytes);
        const bool normals = (d.layout & RE4_GX_V_NORMAL) != 0 && d.stride >= 24;
        float far = 0.0f;
        for (std::uint32_t v = 0; v < d.vertexCount; v++) {
          float fa[6], fb[6];
          std::memcpy(fa, pa + v * d.stride, normals ? 24 : 12);
          std::memcpy(fb, pb + v * d.stride, normals ? 24 : 12);
          for (int c = 0; c < 3; c++)
            far = std::max(far, std::fabs(fb[c] - fa[c]));
          for (int c = 0; c < (normals ? 6 : 3); c++)
            fb[c] = fa[c] + (fb[c] - fa[c]) * s;
          std::memcpy(blendVertices.data() + v * d.stride, fb, normals ? 24 : 12);
        }
        // more than 1500 units in one game picture (2D: kFlatMove): not the same figure. The blended
        // vertices go to a place of blendVbo no draw has used in this pass: no waiting, no copy of
        // the buffer
        if (far <= (flat ? kFlatMove : 1500.0f)) {
          const std::size_t at = (blendUsed + d.stride - 1) / d.stride * d.stride;
          if (blendMap && at + bytes <= blendCap) {
            std::memcpy(blendMap + at, blendVertices.data(), bytes);
            blendUsed = at + bytes;
            blendAt = static_cast<std::int64_t>(at);
            stats.interpVertexBlends++;
          } else if (!blendMap && at + bytes <= blendCap) {
            glBindBuffer(GL_ARRAY_BUFFER, blendVbo);
            void *dst = glMapBufferRange(GL_ARRAY_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(bytes),
                                         GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            if (dst) {
              std::memcpy(dst, blendVertices.data(), bytes);
              glUnmapBuffer(GL_ARRAY_BUFFER);
            } else {
              glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(bytes),
                              blendVertices.data());
            }
            blendUsed = at + bytes;
            blendAt = static_cast<std::int64_t>(at);
            stats.interpVertexBlends++;
          }
        }
      }
    }
    // 0x100000: only the projection; 0x200000: the lights too; 0x400000: the colours; 0x800000:
    // texture matrices or TEV colours (texRowsBlended)
    return (rows ? first << 8 | rows : 0x100000) | (lights ? 0x200000u : 0u) | (colors ? 0x400000u : 0u) |
           (extras ? 0x800000u : 0u);
  }

  // The lights of the next picture into the in-between one: the modelview `mid` of a draw (blended)
  // against `now` (the next picture's) gives the camera's move between the two, C = mid * now^-1;
  // positions take all of it, directions its turn. False when `now` has no inverse.
  bool BlendLights(const float (&now)[3][4], const float *mid) {
    const float(&n)[3][4] = now;
    const float det = n[0][0] * (n[1][1] * n[2][2] - n[1][2] * n[2][1]) -
                      n[0][1] * (n[1][0] * n[2][2] - n[1][2] * n[2][0]) +
                      n[0][2] * (n[1][0] * n[2][1] - n[1][1] * n[2][0]);
    if (!(std::fabs(det) > 1e-12f))
      return false;
    float inv[3][3];
    inv[0][0] = (n[1][1] * n[2][2] - n[1][2] * n[2][1]) / det;
    inv[0][1] = (n[0][2] * n[2][1] - n[0][1] * n[2][2]) / det;
    inv[0][2] = (n[0][1] * n[1][2] - n[0][2] * n[1][1]) / det;
    inv[1][0] = (n[1][2] * n[2][0] - n[1][0] * n[2][2]) / det;
    inv[1][1] = (n[0][0] * n[2][2] - n[0][2] * n[2][0]) / det;
    inv[1][2] = (n[0][2] * n[1][0] - n[0][0] * n[1][2]) / det;
    inv[2][0] = (n[1][0] * n[2][1] - n[1][1] * n[2][0]) / det;
    inv[2][1] = (n[0][1] * n[2][0] - n[0][0] * n[2][1]) / det;
    inv[2][2] = (n[0][0] * n[1][1] - n[0][1] * n[1][0]) / det;
    // C's turn: mid's 3x3 times inv; C's shift: mid's shift minus C's turn of now's shift
    float rot[3][3], shift[3];
    for (int r = 0; r < 3; r++) {
      for (int c = 0; c < 3; c++)
        rot[r][c] = mid[r * 4 + 0] * inv[0][c] + mid[r * 4 + 1] * inv[1][c] + mid[r * 4 + 2] * inv[2][c];
    }
    for (int r = 0; r < 3; r++)
      shift[r] = mid[r * 4 + 3] - (rot[r][0] * n[0][3] + rot[r][1] * n[1][3] + rot[r][2] * n[2][3]);
    for (int l = 0; l < 8; l++) {
      float p[3], dir[3];
      std::memcpy(p, xf.light[l][3], sizeof(p));
      std::memcpy(dir, xf.light[l][4], sizeof(dir));
      for (int r = 0; r < 3; r++) {
        xf.light[l][3][r] = rot[r][0] * p[0] + rot[r][1] * p[1] + rot[r][2] * p[2] + shift[r];
        xf.light[l][4][r] = rot[r][0] * dir[0] + rot[r][1] * dir[1] + rot[r][2] * dir[2];
      }
    }
    Dirty(160, 200);
    return true;
  }

  // After the draw: the rows from XF memory again (the projection from the registers).
  void BlendRestore(std::uint32_t blended) {
    const std::uint32_t first = (blended >> 8) & 0xFF, rows = blended & 0xFF;
    for (std::uint32_t r = 0; r < rows; r++)
      for (std::uint32_t c = 0; c < 4; c++)
        XfMemToBlock((first + r) * 4 + c);
    for (std::uint32_t r = 0; r < rows; r++)
      for (std::uint32_t c = 0; c < 3; c++)
        XfMemToBlock(0x400 + ((first + r) & 31) * 3 + c);
    if (rows) {
      Dirty(static_cast<int>(first), static_cast<int>(first + rows));
      Dirty(64, 96);
    }
    if (blended & 0x200000) {
      for (std::uint32_t a = 0x600; a < 0x680; a++)
        XfMemToBlock(a);
      Dirty(160, 200);
    }
    if (blended & 0x400000)
      Dirty(200, 204);
    if (blended & 0x800000) {
      for (std::uint32_t row : texRowsBlended) {
        if (row == 0x100) {
          std::memcpy(tevColor, tevSave[0], sizeof(tevColor));
          std::memcpy(tevKonst, tevSave[1], sizeof(tevKonst));
          pxDirty = true;
          continue;
        }
        if (row == 0x300) {
          std::memcpy(&bp[0x06], indSave, sizeof(indSave));
          pxDirty = true;
          continue;
        }
        if (row >= 0x200) {
          const std::uint32_t pr = row - 0x200;
          for (std::uint32_t r = 0; r < 3; r++)
            for (std::uint32_t c = 0; c < 4; c++)
              XfMemToBlock(0x500 + (pr + r) * 4 + c);
          Dirty(static_cast<int>(96 + pr), static_cast<int>(96 + pr + 3));
          continue;
        }
        for (std::uint32_t r = 0; r < 3; r++)
          for (std::uint32_t c = 0; c < 4; c++)
            XfMemToBlock((row + r) * 4 + c);
        Dirty(static_cast<int>(row), static_cast<int>(row + 3));
      }
      texRowsBlended.clear();
    }
    xfRegsDirty = true; // FillXfRegs puts the projection (and the colours) back
    Dirty(206, 208);
  }

  // --- state -----------------------------------------------------------------

  // The copy filter (BP 0x53/0x54, GXSetCopyFilter): seven vertical taps in
  // 1/64 that the GameCube applies to EFB copies. Their sum scales the colour:
  // the game's brightness option (main_sub.cpp Bg_brightness_set) raises or
  // lowers it around 64. The vertical blur of the taps (the deflicker filter
  // for the TV picture) is left out: the picture stays sharp.
  float CopyFilterGain() const {
    const std::uint32_t c0 = bp[0x53], c1 = bp[0x54];
    unsigned sum = 0;
    for (unsigned i = 0; i < 4; i++)
      sum += (c0 >> (6 * i)) & 63;
    for (unsigned i = 0; i < 3; i++)
      sum += (c1 >> (6 * i)) & 63;
    return sum == 0 ? 1.0f : static_cast<float>(sum) / 64.0f;
  }

  void BpWrite(std::uint32_t word, bool action) {
    const unsigned reg = word >> 24;
    const std::uint32_t value = word & 0xFFFFFF;
    if (bp[reg] != value || !action) {
      // registers the generated shader depends on (MakeKey)
      if (reg == 0x00 || (reg >= 0x10 && reg <= 0x1F) || (reg >= 0x27 && reg <= 0x2F) ||
          (reg >= 0xC0 && reg <= 0xDF) || reg == 0xF1 || reg == 0xF3 || (reg >= 0xF6 && reg <= 0xFD))
        keyDirty = true;
      // registers of the PX block (FillPixel)
      if ((reg >= 0x06 && reg <= 0x0E) || reg == 0x25 || reg == 0x26 || (reg >= 0x30 && reg <= 0x3F) ||
          (reg >= 0x80 && reg <= 0x87) || (reg >= 0xA0 && reg <= 0xA7) || (reg >= 0xE0 && reg <= 0xE7) ||
          (reg >= 0xEE && reg <= 0xF3))
        pxDirty = true;
    }
    bp[reg] = value;
    if (reg >= 0xE0 && reg <= 0xE7) {
      const unsigned i = (reg - 0xE0) / 2;
      std::int32_t *dst = (value >> 23) & 1 ? tevKonst[i] : tevColor[i];
      if ((reg & 1) == 0) { // RA
        dst[0] = Signed11(value);
        dst[3] = Signed11(value >> 12);
      } else { // BG
        dst[2] = Signed11(value);
        dst[1] = Signed11(value >> 12);
      }
    }
    if (action && reg == 0x52) {
      if (!skipDraws) {
        EfbCopy(value);
      } else if ((value >> 14) & 1) {
        // a skipped picture: nothing to show, only the clear of the EFB that the next picture needs
        stats.skippedChunks++;
        SaveAndPlainState();
        BindFbo(DrawFbo());
        ClearAfterCopy(value, static_cast<int>(bp[0x49] & 0x3FF), static_cast<int>((bp[0x49] >> 10) & 0x3FF),
                       static_cast<int>(bp[0x4A] & 0x3FF) + 1, static_cast<int>((bp[0x4A] >> 10) & 0x3FF) + 1);
        sceneBottom = 0;
      } // a copy for a texture: CanSkip said the next chunk copies there first, nobody reads this one
    }
    if (action && reg == 0x5A && !skipDraws)
      Autofocus(value);
    if (action && reg == 0x5B && !interpPass && !skipDraws) {
      peekWanted = true;  // the game's GXPeekZ is used in this picture: its depth goes to the core
      peekPicture = value & 0xFFFFFF;  // with the picture's number (the core answers by the map's age)
    }
  }

  void Dirty(int lo, int hi) {
    for (int i = 0; i < kXfParts; i++) {
      const int a = std::max(lo, kXfPartRow[i]), b = std::min(hi, kXfPartRow[i + 1]);
      if (a < b) {
        dirtyLo[i] = std::min(dirtyLo[i], a);
        dirtyHi[i] = std::max(dirtyHi[i], b);
      }
    }
  }

  // The parts of the XF block that changed since the last draw, to the GPU. In the stream a part
  // goes whole into a new place; after the stream moved to its next quarter every part goes again
  // (a part bound in an old quarter could be written over before the GPU reads it).
  void UploadXf() {
    for (int round = 0; round < 2; round++) {
      if (streamMoved) {
        streamMoved = false;
        Dirty(0, kXfPartRow[kXfParts]);
        pxValid = false;
        for (bool &v : xfSentValid)
          v = false;
      }
      for (int i = 0; i < kXfParts; i++) {
        if (dirtyLo[i] >= dirtyHi[i])
          continue;
        const std::size_t first = static_cast<std::size_t>(kXfPartRow[i]) * 16;
        const std::size_t partBytes = static_cast<std::size_t>(kXfPartRow[i + 1] - kXfPartRow[i]) * 16;
        const auto *now = reinterpret_cast<const std::uint8_t *>(&xf) + first;
        auto *sent = reinterpret_cast<std::uint8_t *>(&xfSent) + first;
        // the rows that changed are as sent: the part bound now still holds them (in the stream
        // a part is never written again in place; the small buffers keep what they got)
        const std::size_t lo = static_cast<std::size_t>(dirtyLo[i]) * 16, hi = static_cast<std::size_t>(dirtyHi[i]) * 16;
        // RE4_GX_UNIFORM_SAME=0: every changed part sent (the counter-check)
        static const bool sendSame = std::getenv("RE4_GX_UNIFORM_SAME") && *std::getenv("RE4_GX_UNIFORM_SAME") == '0';
        if (!sendSame && xfSentValid[i] && std::memcmp(now + (lo - first), sent + (lo - first), hi - lo) == 0) {
          stats.uniformSame++;
          dirtyLo[i] = kXfPartRow[kXfParts];
          dirtyHi[i] = 0;
          continue;
        }
        std::memcpy(sent, now, partBytes);
        xfSentValid[i] = true;
        UploadBlock(kXfPartBinding[i], xfUbo[i], reinterpret_cast<const std::uint8_t *>(&xf) + first,
                    static_cast<std::size_t>(dirtyLo[i]) * 16 - first,
                    static_cast<std::size_t>(dirtyHi[i] - dirtyLo[i]) * 16,
                    static_cast<std::size_t>(kXfPartRow[i + 1] - kXfPartRow[i]) * 16);
        dirtyLo[i] = kXfPartRow[kXfParts];
        dirtyHi[i] = 0;
      }
      if (!streamMoved)
        break;
    }
  }

  // The XF block rows an XF address touches.
  void DirtyAddress(std::uint32_t a) {
    constexpr int kNormal = 64, kPost = 96, kLight = 160, kRegs = 200; // row offsets in XfBlock
    if (a < 0x100)
      Dirty(static_cast<int>(a / 4), static_cast<int>(a / 4) + 1);
    else if (a >= 0x400 && a < 0x460)
      Dirty(kNormal + static_cast<int>((a - 0x400) / 3), kNormal + static_cast<int>((a - 0x400) / 3) + 1);
    else if (a >= 0x500 && a < 0x600)
      Dirty(kPost + static_cast<int>((a - 0x500) / 4), kPost + static_cast<int>((a - 0x500) / 4) + 1);
    else if (a >= 0x600 && a < 0x680)
      Dirty(kLight + static_cast<int>((a - 0x600) / 16) * 5, kLight + static_cast<int>((a - 0x600) / 16) * 5 + 5);
    else if (a >= 0x1000)
      Dirty(kRegs, static_cast<int>(sizeof(XfBlock) / 16));
  }

  void XfWrite(std::uint32_t address, const std::uint32_t *words, std::uint32_t n) {
    bool projection = false;
    for (std::uint32_t i = 0; i < n; i++) {
      const std::uint32_t a = address + i;
      if (a < 0x800) {
        xfMem[a] = words[i];
        XfMemToBlock(a);
      } else if (a >= 0x1000 && a < 0x1058) {
        const std::uint32_t r = a - 0x1000;
        xfReg[r] = words[i];
        if (r == 0x09 || (r >= 0x0E && r <= 0x12) || r == 0x26 || r >= 0x3F)
          keyDirty = true;
        if (r >= 0x20 && r <= 0x26)
          projection = true;
        xfRegsDirty = true;
      } else {
        continue;
      }
      DirtyAddress(a);
    }
    if (projection && xfReg[0x26] == 0) {
      perspA = AsFloat(xfReg[0x24]);
      perspB = AsFloat(xfReg[0x25]);
      havePerspective = true;
    }
  }

  // One word of XF memory into the XF block (FillXf for all of it).
  void XfMemToBlock(std::uint32_t a) {
    if (a < 0x100) {
      xf.posTex[a / 4][a % 4] = AsFloat(xfMem[a]);
    } else if (a >= 0x400 && a < 0x460) {
      xf.normal[(a - 0x400) / 3][(a - 0x400) % 3] = AsFloat(xfMem[a]);
    } else if (a >= 0x500 && a < 0x600) {
      xf.post[(a - 0x500) / 4][(a - 0x500) % 4] = AsFloat(xfMem[a]);
    } else if (a >= 0x600 && a < 0x680) {
      const unsigned l = (a - 0x600) / 16, w = (a - 0x600) % 16;
      if (w == 3) {
        for (int c = 0; c < 4; c++)
          xf.light[l][0][c] = static_cast<float>((xfMem[a] >> (24 - 8 * c)) & 255) / 255.0f;
      } else if (w >= 4) {
        xf.light[l][1 + (w - 4) / 3][(w - 4) % 3] = AsFloat(xfMem[a]);
      }
    }
  }

  // The XF registers of the block: material and ambient colours, viewport, projection.
  void FillXfRegs() {
    const std::uint32_t colorRegs[4] = {xfReg[0x0C], xfReg[0x0D], xfReg[0x0A], xfReg[0x0B]};
    for (int i = 0; i < 4; i++)
      for (int c = 0; c < 4; c++)
        xf.matAmb[i][c] = static_cast<float>((colorRegs[i] >> (24 - 8 * c)) & 255) / 255.0f;
    xf.viewport[0][0] = AsFloat(xfReg[0x1A]);
    xf.viewport[0][1] = AsFloat(xfReg[0x1B]);
    xf.viewport[0][2] = AsFloat(xfReg[0x1C]);
    xf.viewport[0][3] = AsFloat(xfReg[0x1D]);
    xf.viewport[1][0] = AsFloat(xfReg[0x1E]);
    xf.viewport[1][1] = AsFloat(xfReg[0x1F]);
    xf.viewport[1][2] = kEfbWidth;
    xf.viewport[1][3] = kEfbHeight;
    for (int i = 0; i < 6; i++)
      xf.proj[i / 4][i % 4] = AsFloat(xfReg[0x20 + i]);
    xf.proj[1][2] = static_cast<float>(xfReg[0x26]);
    xfRegsDirty = false;
  }

  // the frame buffer the game draws into
  GLuint DrawFbo() const { return msaaFbo ? msaaFbo : efbFbo; }

  // GPU step d (19.10.2026): MSAA on the tiles (EXT_multisampled_render_to_texture2). The EFB's
  // own textures take `want` samples: the GPU keeps them in its tile memory and writes only the
  // resolved colour and depth out when a pass ends (an EFB copy, the autofocus, the picture's end);
  // the next pass reads them back into every sample, and Resolve has nothing to do. Separate
  // multisampled buffers instead were written out, resolved with a blit and read in again at every
  // copy (the Retroid, 19.10.2026: MSAA +74 % work, with the depth of field +114 %). What was
  // drawn before a copy keeps its resolved colour and depth, not its samples, after it. False:
  // the device cannot (efbFbo has its plain textures again). Not used unless asked for
  // (GxOptions::tiledMsaa): the extension leaves the samples undefined after a pass, and the
  // Adreno 650 then had the colour but not the depth (R3 capture, 19.10.2026). Since d2 a second
  // set of textures (spareFbo) and UseEfb draw the EFB back at the start of every new pass.
  bool TiledMsaa(int want) {
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    if (!HasExtension(ext, "GL_EXT_multisampled_render_to_texture") ||
        !HasExtension(ext, "GL_EXT_multisampled_render_to_texture2"))
      return false;
    using Attach = void (*)(GLenum, GLenum, GLenum, GLuint, GLint, GLsizei);
    const auto attach = reinterpret_cast<Attach>(GlExtension("glFramebufferTexture2DMultisampleEXT"));
    GLint most = 1;
    glGetIntegerv(GL_MAX_SAMPLES_EXT, &most);
    want = std::min(want, static_cast<int>(most));
    if (!attach || want < 2)
      return false;
    while (glGetError() != GL_NO_ERROR) {
    }
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efbColor, 0, want);
    attach(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, efbDepth, 0, want);
    GLint got = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_SAMPLES_EXT, &got);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR &&
              got > 1;
    // d2: the second set, the same samples
    if (ok) {
      const int w = Lo(kEfbWidth), h = Lo(kEfbHeight);
      MakeEfbTextures(w, h, &spareColor, &spareDepth);
      glGenFramebuffers(1, &spareFbo);
      glBindFramebuffer(GL_FRAMEBUFFER, spareFbo);
      attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, spareColor, 0, want);
      attach(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, spareDepth, 0, want);
      GLint spare = 0;
      glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_SAMPLES_EXT, &spare);
      ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR &&
           spare == got;
      glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    }
    if (ok) {
      samples = got;
      tiledMsaa = true;
      return true;
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efbColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, efbDepth, 0);
    const GLuint spareTextures[] = {spareColor, spareDepth};
    glDeleteTextures(2, spareTextures);
    glDeleteFramebuffers(1, &spareFbo);
    spareFbo = spareColor = spareDepth = 0;
    while (glGetError() != GL_NO_ERROR) {
    }
    return false;
  }

  // The EFB's colour (RGBA8, linear) and depth (24 bits, nearest) textures.
  static void MakeEfbTextures(int w, int h, GLuint *color, GLuint *depth) {
    glGenTextures(1, color);
    glBindTexture(GL_TEXTURE_2D, *color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, depth);
    glBindTexture(GL_TEXTURE_2D, *depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  // Every binding of a framebuffer to draw into (GL_FRAMEBUFFER) goes through here. MSAA on the
  // tiles (d2): the EFB bound again after another framebuffer starts a new pass, and the
  // extension left its samples undefined; it is drawn back before it is next drawn into
  // (UseEfb). Until then it stays as it is, and copies, the autofocus and the tests read it.
  void BindFbo(GLuint fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (fbo != efbFbo) {
      efbLeft = true;
      return;
    }
    if (efbLeft && tiledMsaa)
      restorePending = true;
    efbLeft = false;
  }

  // Before the EFB is drawn into or partly cleared (bound): with MSAA on the tiles after a pass
  // ended (d2), the new pass draws into the other set of textures and first draws the EFB back
  // into it from the set before (colour and depth, the same into every sample; what was drawn
  // before keeps its resolved colour and depth at the edges, not its samples). The GPU need not
  // load anything for the new pass (glInvalidateFramebuffer): the restore draws over all of it.
  void UseEfb() {
    if (!restorePending)
      return;
    restorePending = false;
    std::swap(efbFbo, spareFbo);
    std::swap(efbColor, spareColor);
    std::swap(efbDepth, spareDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    SaveAndPlainState();
    const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, both);
    if (options.loseTiles >= 1) {
      glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
      glClearDepthf(0.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    if (options.loseTiles >= 2) {
      // a device that never writes the resolved depth into the texture: 0 there
      GLuint fbo = 0;
      glGenFramebuffers(1, &fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, spareDepth, 0);
      glClearDepthf(0.0f);
      glClear(GL_DEPTH_BUFFER_BIT);
      glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
      glDeleteFramebuffers(1, &fbo);
    }
    glViewport(0, 0, Lo(kEfbWidth), Lo(kEfbHeight));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glUseProgram(restoreProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, spareColor);
    glBindSampler(0, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, spareDepth);
    glBindSampler(1, 0);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_DEPTH_TEST);
    Touched();
    stats.efbRestores++;
  }

  // MSAA in separate multisampled buffers (msaaFbo), resolved into efbColor / efbDepth before
  // every copy; false when the device cannot (single-sampled then).
  bool OwnMsaa(int want) {
    const int w = Lo(kEfbWidth), h = Lo(kEfbHeight);
    while (glGetError() != GL_NO_ERROR) {
    }
    glGenRenderbuffers(1, &msaaColor);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_RGBA8, w, h);
    glGenRenderbuffers(1, &msaaDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, msaaDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, want, GL_DEPTH_COMPONENT24, w, h);
    GLint got = 0;
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &got);
    glGenFramebuffers(1, &msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaaDepth);
    const bool ok =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR && got > 1;
    if (ok) {
      samples = got;
      glViewport(0, 0, w, h);
      glClearColor(0, 0, 0, 0);
      glClearDepthf(1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else {
      glDeleteFramebuffers(1, &msaaFbo);
      glDeleteRenderbuffers(1, &msaaColor);
      glDeleteRenderbuffers(1, &msaaDepth);
      msaaFbo = msaaColor = msaaDepth = 0;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    return ok;
  }

  // MSAA on the tiles given up: the EFB's textures plain again, the second set gone.
  void DropTiles() {
    glBindFramebuffer(GL_FRAMEBUFFER, efbFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efbColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, efbDepth, 0);
    const GLuint spareTextures[] = {spareColor, spareDepth};
    glDeleteTextures(2, spareTextures);
    glDeleteFramebuffers(1, &spareFbo);
    spareFbo = spareColor = spareDepth = 0;
    tiledMsaa = false;
    samples = 1;
    efbLeft = restorePending = false;
  }

  // GPU step d2 on this device (once, at Init): a pass on the tiles draws red at depth 0.25 into
  // an 8x8 corner and ends; what the device kept of it is read back (the resolved colour and
  // depth in the EFB's textures); the next pass draws the EFB back (UseEfb), then green at depth
  // 0.5 with LESS (must stay red: the depth came back) and blue at depth 0.25 with EQUAL (must
  // pass: the same depth exactly). False (logged): MSAA on the tiles does not keep the EFB here.
  // The Retroid's Adreno 650 showed Leon without hair with d and d2 (19.10.2026).
  bool TilesSelfTest() {
    static const char kVs[] = "#version 300 es\n"
                              "void main() {\n"
                              "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                              "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                              "}\n";
    static const char kDrawFs[] = "#version 300 es\nprecision highp float;\n"
                                  "uniform vec4 color;\nuniform float depth;\nout vec4 o;\n"
                                  "void main() { o = color; gl_FragDepth = depth; }\n";
    static const char kProbeFs[] = "#version 300 es\nprecision highp float;\n"
                                   "uniform sampler2D color;\nuniform highp sampler2D depth;\nout vec4 o;\n"
                                   "void main() {\n"
                                   "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
                                   "  o = vec4(texelFetch(color, p, 0).rgb, texelFetch(depth, p, 0).r);\n"
                                   "}\n";
    constexpr int n = 8;
    std::string log;
    const GLuint draw = Link(kVs, kDrawFs, &log), probe = Link(kVs, kProbeFs, &log);
    GLuint probeTex = 0, probeFbo = 0;
    glGenTextures(1, &probeTex);
    glBindTexture(GL_TEXTURE_2D, probeTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, n, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &probeFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, probeFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, probeTex, 0);
    if (!draw || !probe || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      GXLOG("MSAA on the tiles, self-test not possible (%s); own buffers", log.c_str());
      glDeleteProgram(draw);
      glDeleteProgram(probe);
      glDeleteTextures(1, &probeTex);
      glDeleteFramebuffers(1, &probeFbo);
      return false;
    }
    SaveAndPlainState();
    glBindVertexArray(quadVao);
    for (int i = 0; i < 2; i++)
      glBindSampler(static_cast<GLuint>(i), 0);
    glUseProgram(probe);
    glUniform1i(glGetUniformLocation(probe, "color"), 0);
    glUniform1i(glGetUniformLocation(probe, "depth"), 1);
    glUseProgram(draw);
    const GLint color = glGetUniformLocation(draw, "color"), depth = glGetUniformLocation(draw, "depth");
    auto quad = [&](int x, int w, float r, float g, float b, float z, GLenum func) {
      glViewport(x, 0, w, n);
      glDepthFunc(func);
      glUniform4f(color, r, g, b, 1.0f);
      glUniform1f(depth, z);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    // the corner's pixels as the EFB's textures hold them now (rgb, depth in a)
    std::uint8_t px[n * n * 4];
    auto read = [&]() {
      BindFbo(probeFbo);
      glDisable(GL_DEPTH_TEST);
      glViewport(0, 0, n, n);
      glUseProgram(probe);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, efbColor);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, efbDepth);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glReadPixels(0, 0, n, n, GL_RGBA, GL_UNSIGNED_BYTE, px);
      glUseProgram(draw);
    };
    auto at = [&](int x, int c) { return static_cast<int>(px[(4 * n + x) * 4 + c]); };
    // the first pass: red at depth 0.25
    efbLeft = false;
    BindFbo(efbFbo);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    quad(0, n, 1.0f, 0.0f, 0.0f, 0.25f, GL_ALWAYS);
    read();
    const bool colorKept = at(2, 0) > 240 && at(2, 1) < 16;
    const int storedDepth = at(2, 3);  // 0.25: 64
    const bool depthKept = std::abs(storedDepth - 64) <= 2;
    // the next pass: drawn back, then green behind (LESS) and blue at the same depth (EQUAL)
    BindFbo(efbFbo);
    UseEfb();
    glUseProgram(draw);
    glBindVertexArray(quadVao);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    quad(0, n / 2, 0.0f, 1.0f, 0.0f, 0.5f, GL_LESS);
    quad(n / 2, n / 2, 0.0f, 0.0f, 1.0f, 0.25f, GL_EQUAL);
    read();
    const bool behindHidden = at(1, 0) > 240 && at(1, 1) < 16;
    const bool equalPassed = at(6, 2) > 240 && at(6, 0) < 16;
    const bool ok = behindHidden && equalPassed;
    GXLOG("MSAA on the tiles, self-test: the device keeps the colour %s, the depth %s (read %d of 255, 64 "
          "expected); the EFB drawn back: what lies behind %s, the same depth %s -> %s",
          colorKept ? "yes" : "NO", depthKept ? "yes" : "NO", storedDepth,
          behindHidden ? "hidden" : "NOT hidden (the depth lost)", equalPassed ? "passes EQUAL" : "FAILS EQUAL",
          ok ? "MSAA on the tiles" : "own buffers instead");
    glDeleteProgram(draw);
    glDeleteProgram(probe);
    glDeleteTextures(1, &probeTex);
    glDeleteFramebuffers(1, &probeFbo);
    glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    Touched();
    stats.efbRestores = 0;
    return ok;
  }

  // a multisampled EFB: the region (EFB pixels times the scale, with a pixel
  // around it for the copy filter) into the single-sample textures the
  // copies read
  void Resolve(int x, int y, int w, int h, bool depth) {
    if (!msaaFbo)
      return;
    const int fw = Lo(kEfbWidth), fh = Lo(kEfbHeight);
    const int pad = Hi(1);
    int x0 = std::max(0, x - pad), y0 = std::max(0, y - pad);
    int x1 = std::min(fw, x + w + pad), y1 = std::min(fh, y + h + pad);
    if (options.fullResolve) {
      x0 = y0 = 0;
      x1 = fw;
      y1 = fh;
    }
    if (x1 <= x0 || y1 <= y0)
      return;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, efbFbo);
    glBlitFramebuffer(x0, y0, x1, y1, x0, y0, x1, y1, depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo);
  }

  // --- textures ----------------------------------------------------------------

  GLuint Sampler(std::uint32_t mode0, std::uint32_t mode1, unsigned levels) {
    const std::uint64_t key = (static_cast<std::uint64_t>(mode1 & 0xFFFF) << 32) | (mode0 & 0x1FF) |
                              (static_cast<std::uint64_t>(levels > 1) << 48);
    auto it = samplers.find(key);
    if (it != samplers.end())
      return it->second;
    GLuint s = 0;
    glGenSamplers(1, &s);
    static const GLenum wrap[4] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT};
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap[mode0 & 3]);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap[(mode0 >> 2) & 3]);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (mode0 >> 4) & 1 ? GL_LINEAR : GL_NEAREST);
    const unsigned minFilter = (mode0 >> 5) & 7;
    static const GLenum minTable[8] = {GL_NEAREST,
                                       GL_NEAREST_MIPMAP_NEAREST,
                                       GL_NEAREST_MIPMAP_LINEAR,
                                       GL_NEAREST,
                                       GL_LINEAR,
                                       GL_LINEAR_MIPMAP_NEAREST,
                                       GL_LINEAR_MIPMAP_LINEAR,
                                       GL_LINEAR};
    GLenum min = minTable[minFilter];
    if (levels <= 1)
      min = (minFilter >= 4) ? GL_LINEAR : GL_NEAREST;
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min);
    glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, static_cast<float>(mode1 & 0xFF) / 16.0f);
    glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, static_cast<float>((mode1 >> 8) & 0xFF) / 16.0f);
    // anisotropic filtering (the menu's option): only where the game filters
    // linearly through mipmaps; point-sampled effects keep their look
    if (anisotropy > 1.0f && levels > 1 && ((mode0 >> 4) & 1) && minFilter >= 5 && minFilter <= 6)
      glSamplerParameterf(s, kMaxAnisotropy, anisotropy);
    samplers[key] = s;
    return s;
  }

  void Upload(const Re4GxTexture &t, const Re4GxChunk &chunk) {
    const std::uint64_t key = static_cast<std::uint64_t>(t.keyHi) << 32 | t.keyLo;
    if (t.dataBytes == 0 || t.dataOffset + t.dataBytes > chunk.blobBytes)
      return;
    Touched(); // binds the new texture
    Texture tex;
    tex.width = t.width;
    tex.height = t.height;
    tex.levels = t.levels ? t.levels : 1;
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const std::uint8_t *data = chunk.blob + t.dataOffset;
    std::size_t left = t.dataBytes;
    const std::uint8_t *palette = nullptr;
    unsigned entries = 0;
    if (GxIsPaletted(t.format)) {
      const unsigned offset = (t.tlut & 0x3FF) << 9;
      palette = tlut.data() + offset;
      entries = std::min<unsigned>(GxPaletteEntries(t.format), (kTlutBytes - offset) / 2);
    }
    unsigned w = t.width, h = t.height, levels = 0;
    std::vector<std::uint8_t> rgba;
    for (unsigned l = 0; l < tex.levels; l++) {
      const std::size_t bytes = GxTextureLevelBytes(t.format, w, h);
      if (bytes > left)
        break;
      GxDecodeTexture(data, bytes, t.format, w, h, palette, entries, (t.tlut >> 10) & 3, rgba);
      glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                   0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
      levels++;
      data += bytes;
      left -= bytes;
      w = std::max(1u, w / 2);
      h = std::max(1u, h / 2);
    }
    tex.levels = std::max(1u, levels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(tex.levels - 1));
    auto old = textures.find(key);
    if (old != textures.end())
      glDeleteTextures(1, &old->second.id);
    textures[key] = tex;
    stats.textures++;
  }

  // A texture copy read through a palette (C4/C8/C14X2 over a copy): the copy's
  // values looked up in TMEM's palette, on the GPU.
  GLuint PalettedCopy(const Copy &copy, const Re4GxTexture &t) {
    Touched(); // its own pass
    const unsigned offset = (t.tlut & 0x3FF) << 9;
    const unsigned entries = std::min<unsigned>(GxPaletteEntries(t.format), (kTlutBytes - offset) / 2);
    const std::uint64_t key =
        Fnv(tlut.data() + offset, entries * 2, (static_cast<std::uint64_t>(copy.serial) << 32) ^ t.address ^ t.tlut);
    auto it = paletted.find(key);
    if (it != paletted.end())
      return it->second.id;
    // the palette as a 256 x (entries / 256) RGBA texture
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(entries) * 4);
    std::vector<std::uint8_t> idx(entries);
    for (unsigned i = 0; i < entries; i++)
      idx[i] = static_cast<std::uint8_t>(i);
    // decode the palette entries through the C8 path (one entry per texel)
    for (unsigned i = 0; i < entries; i++) {
      std::vector<std::uint8_t> one;
      std::uint8_t tile[32] = {};
      tile[0] = 0;
      GxDecodeTexture(tile, 32, kTexC8, 1, 1, tlut.data() + offset + i * 2, 1, (t.tlut >> 10) & 3, one);
      std::memcpy(rgba.data() + i * 4, one.data(), 4);
    }
    const unsigned pw = std::min(entries, 256u), ph = std::max(1u, entries / 256);
    GLuint pal = 0;
    glGenTextures(1, &pal);
    glBindTexture(GL_TEXTURE_2D, pal);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(pw), static_cast<GLsizei>(ph), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    Texture out;
    out.width = copy.width;
    out.height = copy.height;
    glGenTextures(1, &out.id);
    glBindTexture(GL_TEXTURE_2D, out.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(copy.width), static_cast<GLsizei>(copy.height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    BindFbo(fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out.id, 0);
    SaveAndPlainState();
    glViewport(0, 0, static_cast<GLsizei>(copy.width), static_cast<GLsizei>(copy.height));
    glUseProgram(paletteProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, copy.id);
    glBindSampler(0, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, pal);
    glBindSampler(1, 0);
    glUniform2f(paletteSize, static_cast<float>(pw), static_cast<float>(ph));
    glUniform1i(paletteKind, static_cast<GLint>(copy.kind));
    glUniform1i(paletteFormat, static_cast<GLint>(t.format));
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &pal);
    BindFbo(DrawFbo());
    current = nullptr;
    paletted[key] = out;
    return out.id;
  }

  void BindTexture(const Re4GxTexture &t, const Re4GxChunk &chunk) {
    const unsigned map = t.map & 7;
    pxDirty = true; // the map's size is in the PX block
    Bound &b = bound[map];
    b.id = whiteTex;
    b.width = t.width ? t.width : 1;
    b.height = t.height ? t.height : 1;
    b.levels = 1;
    if (t.flags & RE4_GX_TEX_COPY) {
      boundKey[map] = 1ull << 63 | t.address;
      // the in-between pass reads its own copies, and the real ones of the picture before
      const bool own = interpPass && interpCopied.count(t.address);
      auto &from = own ? interpCopies : copies;
      auto it = from.find(t.address);
      if (it == from.end()) {
        stats.missingTextures++;
        return;
      }
      b.id = GxIsPaletted(t.format) ? PalettedCopy(it->second, t) : it->second.id;
      // a texture of the copy's width but another height reads the copy's
      // rows one to one, as the texels lie in memory (the heat haze over the
      // fires of r101 copies rows 56-447 and reads 336 of them as 256x168):
      // texel coordinates go by the copy's size, not by the texture's; scaled
      // to it, the rows below the picture (black) showed as a dark band
      if (it->second.width == b.width && it->second.height) {
        b.height = it->second.height;
      }
      return;
    }
    const std::uint64_t key = static_cast<std::uint64_t>(t.keyHi) << 32 | t.keyLo;
    boundKey[map] = key;
    if (key == 0)
      return;
    // a texture the in-between pass uploaded already (the same key: the same texels)
    if (t.dataBytes && !(skipInterpUploads && interpUploads.count(key)))
      Upload(t, chunk);
    if (interpPass && t.dataBytes)
      interpUploads.insert(key);
    auto it = textures.find(key);
    if (it == textures.end()) {
      stats.missingTextures++;
      return;
    }
    b.id = it->second.id;
    b.levels = it->second.levels;
  }

  void Forget(std::uint64_t key) {
    Touched(); // a deleted texture is unbound
    auto it = textures.find(key);
    if (it != textures.end()) {
      glDeleteTextures(1, &it->second.id);
      textures.erase(it);
    }
  }

  void DropTextures() {
    Touched();
    for (auto &t : textures)
      glDeleteTextures(1, &t.second.id);
    textures.clear();
    for (auto &t : paletted)
      glDeleteTextures(1, &t.second.id);
    paletted.clear();
  }

  // --- GL state ---------------------------------------------------------------

  // GL state for the renderer's own passes (copies, palettes, presenting)
  void SaveAndPlainState() {
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
  }

  // The colour of the bound framebuffer is about to be drawn over completely (or only that part
  // is read later): the GPU may forget what it held. A tile-based GPU (Adreno, Mali) otherwise
  // reads it into its tiles before drawing (GxOptions::invalidate). Poisoned: magenta instead.
  void ForgetColor() {
    if (!options.invalidate)
      return;
    if (options.poison) {
      glDisable(GL_SCISSOR_TEST);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      return;
    }
    const GLenum color = GL_COLOR_ATTACHMENT0;
    glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &color);
  }

  void ApplyState(unsigned primitive) {
    // the picture's extent: the full-width scissors the draws of this picture ran under
    {
      const int xo = static_cast<int>(bp[0x59] & 0x3FF) * 2, yo = static_cast<int>((bp[0x59] >> 10) & 0x3FF) * 2;
      const int sx0 = static_cast<int>((bp[0x20] >> 12) & 0x7FF) - xo, sx1 = static_cast<int>((bp[0x21] >> 12) & 0x7FF) - xo + 1;
      if (sx1 - sx0 >= 500)
        sceneBottom = std::max(sceneBottom, std::min(kEfbHeight, static_cast<int>(bp[0x21] & 0x7FF) - yo + 1));
    }
    // the registers below, as last applied: nothing to do while they hold
    const std::uint32_t sig[10] = {bp[0x20],     bp[0x21],     bp[0x59],       bp[0x40] & 0x1F, bp[0x41] & 0xFFFF,
                                   bp[0x43] & 7, (bp[0x00] >> 14) & 3, bp[0x22] & 0xFF, primitive, bp[0x42] & 0x1FF};
    if (stateValid && std::memcmp(sig, stateSig, sizeof(sig)) == 0)
      return;
    std::memcpy(stateSig, sig, sizeof(sig));
    stateValid = true;
    stats.stateChanges++;
    BindFbo(DrawFbo());
    glViewport(0, 0, Lo(kEfbWidth), Lo(kEfbHeight));
    // scissor: registers in EFB pixels + 342, minus the scissor offset (BP 0x59, in 2 pixels)
    const int xoff = static_cast<int>(bp[0x59] & 0x3FF) * 2, yoff = static_cast<int>((bp[0x59] >> 10) & 0x3FF) * 2;
    const int x0 = std::max(0, static_cast<int>((bp[0x20] >> 12) & 0x7FF) - xoff);
    const int y0 = std::max(0, static_cast<int>(bp[0x20] & 0x7FF) - yoff);
    const int x1 = std::min(kEfbWidth, static_cast<int>((bp[0x21] >> 12) & 0x7FF) - xoff + 1);
    const int y1 = std::min(kEfbHeight, static_cast<int>(bp[0x21] & 0x7FF) - yoff + 1);
    glEnable(GL_SCISSOR_TEST);
    glScissor(Lo(x0), Lo(y0), std::max(0, Lo(x1) - Lo(x0)), std::max(0, Lo(y1) - Lo(y0)));
    // depth
    const std::uint32_t z = bp[0x40];
    if (z & 1) {
      static const GLenum func[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(func[(z >> 1) & 7]);
      glDepthMask((z >> 4) & 1 ? GL_TRUE : GL_FALSE);
    } else {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
    }
    // blending, colour and alpha update
    const std::uint32_t c = bp[0x41];
    const bool alphaFormat = (bp[0x43] & 7) == 1; // RGBA6_Z24 keeps alpha
    const bool alphaWrite = ((c >> 4) & 1) && alphaFormat, colorWrite = (c >> 3) & 1;
    glColorMask(colorWrite, colorWrite, colorWrite, alphaWrite);
    // destination alpha (GXSetDstAlpha, BP 0x42): the EFB alpha gets this
    // constant instead of the TEV alpha, which still drives the blending; the
    // shader writes it (PX alphaRef.z) unless the colour blending needs the
    // TEV alpha (colour written with SRC_ALPHA factors), then the alpha blend
    // factors make it (0 only: other values would need dual-source blending;
    // counted in Stats().dstAlphaApprox)
    const bool dstAlpha = alphaWrite && ((bp[0x42] >> 8) & 1);
    const int dstValue = static_cast<int>(bp[0x42] & 0xFF);
    int shaderAlpha = -1;
    if ((c >> 11) & 1) { // subtract: dst - src
      glEnable(GL_BLEND);
      glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, dstAlpha ? GL_FUNC_ADD : GL_FUNC_REVERSE_SUBTRACT);
      glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, dstAlpha ? GL_ZERO : GL_ONE);
      if (dstAlpha)
        shaderAlpha = dstValue;
    } else if (c & 1) {
      static const GLenum src[8] = {GL_ZERO,      GL_ONE,           GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
      static const GLenum dst[8] = {GL_ZERO,      GL_ONE,           GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
      const unsigned sf = (c >> 8) & 7, df = (c >> 5) & 7;
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      if (!dstAlpha) {
        glBlendFunc(src[sf], dst[df]);
      } else if (!colorWrite || (sf != 4 && sf != 5 && df != 4 && df != 5)) {
        glBlendFuncSeparate(src[sf], dst[df], GL_ONE, GL_ZERO); // TEV alpha not needed: the shader writes it
        shaderAlpha = dstValue;
      } else {
        glBlendFuncSeparate(src[sf], dst[df], GL_ZERO, GL_ZERO);
        if (dstValue != 0)
          stats.dstAlphaApprox++;
      }
    } else if ((c >> 1) & 1) { // logic operations: the common ones
      const unsigned op = (c >> 12) & 15;
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      if (op == 0) // clear
        glBlendFunc(GL_ZERO, GL_ZERO);
      else if (op == 5) // noop
        glBlendFunc(GL_ZERO, GL_ONE);
      else if (op == 15) // set: approximated
        glBlendFunc(GL_ONE, GL_ONE);
      else // copy and the rest
        glBlendFunc(GL_ONE, GL_ZERO);
      if (dstAlpha && op != 0 && op != 5 && op != 15)
        shaderAlpha = dstValue;
    } else {
      glDisable(GL_BLEND);
      if (dstAlpha)
        shaderAlpha = dstValue;
    }
    if (px.alphaRef[2] != shaderAlpha) {
      px.alphaRef[2] = shaderAlpha;
      pxDirty = true;
    }
    // culling: only triangles (hardware 1 = back, 2 = front, 3 = all)
    const unsigned cull = (bp[0x00] >> 14) & 3;
    if (primitive != RE4_GX_TRIANGLES || cull == 0) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(cull == 1 ? GL_BACK : cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK);
    }
    // lines and points: width in 1/6 pixel
    const float lineWidth = static_cast<float>(bp[0x22] & 0xFF) / 6.0f * Sf(1);
    glLineWidth(std::max(1.0f, lineWidth));
  }

  // --- draws --------------------------------------------------------------------

  ShaderKey MakeKey(const Re4GxDraw &d) {
    ShaderKey k;
    std::memset(&k, 0, sizeof(k));
    k.centroid = options.centroid && samples > 1 ? 1 : 0;
    k.layout = d.layout;
    k.numChans = xfReg[0x09] & 3;
    k.numTexGens = std::min(8u, xfReg[0x3F] & 15);
    for (int i = 0; i < 4; i++)
      k.chan[i] = i % 2 < static_cast<int>(k.numChans) ? xfReg[0x0E + i] & 0x7FFF : 0;
    k.dualTex = xfReg[0x12] & 1;
    for (unsigned i = 0; i < k.numTexGens; i++) {
      k.texGen[i] = xfReg[0x40 + i] & 0x3FFFF;
      k.postGen[i] = k.dualTex ? xfReg[0x50 + i] & 0x13F : 0;
    }
    k.orthographic = xfReg[0x26] & 1;
    const std::uint32_t gen = bp[0x00];
    k.numStages = ((gen >> 10) & 15) + 1;
    k.numIndStages = (gen >> 16) & 7;
    for (unsigned s = 0; s < k.numStages; s++) {
      k.colorEnv[s] = bp[0xC0 + 2 * s] & 0xFFFFFF;
      k.alphaEnv[s] = bp[0xC1 + 2 * s] & 0xFFFFFF;
      const std::uint32_t o = bp[0x28 + s / 2] >> ((s & 1) ? 12 : 0);
      k.order[s] = (o & 7) | ((o >> 3) & 7) << 3 | ((o >> 6) & 1) << 6 | ((o >> 7) & 7) << 7;
      const std::uint32_t ks = bp[0xF6 + s / 2];
      k.konst[s] = ((s & 1) ? (ks >> 14) & 31 : (ks >> 4) & 31) | ((s & 1) ? (ks >> 19) & 31 : (ks >> 9) & 31) << 8;
      k.indCmd[s] = k.numIndStages ? bp[0x10 + s] & 0x1FFFFF : 0;
      if ((o >> 6) & 1)
        k.texMaps |= 1u << (o & 7);
    }
    for (int t = 0; t < 4; t++)
      k.swap[t] = (bp[0xF6 + 2 * t] & 3) | ((bp[0xF6 + 2 * t] >> 2) & 3) << 2 | (bp[0xF7 + 2 * t] & 3) << 4 |
                  ((bp[0xF7 + 2 * t] >> 2) & 3) << 6;
    if (k.numIndStages) {
      k.indRef = bp[0x27] & 0xFFFFFF;
      for (unsigned s = 0; s < k.numIndStages && s < 4; s++)
        k.texMaps |= 1u << ((k.indRef >> (6 * s)) & 7);
    }
    k.alphaTest = (bp[0xF3] >> 16) & 0xFF;
    k.fog = (bp[0xF1] >> 20) & 15;
    return k;
  }

  // A program's files in the cache without .bin/.key: a hash of both sources and the driver; in
  // *check a second hash of the sources (in the binary's head).
  std::string CacheStem(const std::string &vs, const std::string &fs, std::uint64_t *check) const {
    return ProgramStem(programCache, driverTag, vs, fs, check);
  }

  // The cache's key files and the app's list, once (Prewarm).
  void ReadAhead() {
    aheadRead = true;
    std::unordered_set<ShaderKey, KeyHash, KeyEqual> seen;
    for (const auto &k : CacheKeys(programCache)) {
      if (seen.insert(k.first).second) {
        AheadKey a;
        a.key = k.first;
        ahead.push_back(a);
      }
    }
    std::vector<ShaderKey> listed;
    if (!programList.empty())
      ReadKeyList(programList, &listed);
    for (const ShaderKey &k : listed) {
      AheadKey a;
      a.key = k;
      a.listed = true;
      ahead.push_back(a);
    }
    aheadListed = static_cast<unsigned>(listed.size());
  }

  const Program *ProgramFor(const ShaderKey &k) {
    auto it = programs.find(k);
    if (it != programs.end())
      return it->second.id ? &it->second : nullptr;
    Program p;
    std::string log;
    const std::string vs = GxVertexShader(k), fs = GxFragmentShader(k);
    std::string cachePath;
    std::uint64_t check = 0;
    if (!programCache.empty()) {
      cachePath = CacheStem(vs, fs, &check) + ".bin";
      p.id = LoadProgram(cachePath, check);
      if (p.id)
        stats.programsLoaded++;
    }
    if (!p.id) {
      p.id = Link(vs, fs, &log, !cachePath.empty());
      if (p.id && !cachePath.empty())
        StoreProgram(cachePath, check, p.id);
    }
    if (p.id && !cachePath.empty())
      StoreKey(cachePath.substr(0, cachePath.size() - 4) + ".key", k);
    if (!p.id) {
      stats.shaderErrors++;
      if (stats.shaderErrors <= 4)
        GXLOG("shader failed: %s\n--- vertex ---\n%s\n--- fragment ---\n%s", log.c_str(), vs.c_str(), fs.c_str());
    } else {
      stats.programs++;
      for (int i = 0; i < kXfParts; i++) {
        const GLuint xfIndex = glGetUniformBlockIndex(p.id, kXfPartName[i]);
        if (xfIndex != GL_INVALID_INDEX)
          glUniformBlockBinding(p.id, xfIndex, kXfPartBinding[i]);
      }
      GLuint pxIndex = glGetUniformBlockIndex(p.id, "PX");
      if (pxIndex != GL_INVALID_INDEX)
        glUniformBlockBinding(p.id, pxIndex, kPixelBinding);
      glUseProgram(p.id);
      for (int m = 0; m < 8; m++) {
        char name[8];
        std::snprintf(name, sizeof(name), "s%d", m);
        GLint loc = glGetUniformLocation(p.id, name);
        if (loc >= 0)
          glUniform1i(loc, m);
      }
      p.mtx = glGetUniformLocation(p.id, "uMtx");
      p.widen = glGetUniformLocation(p.id, "uWiden");
      if (p.widen >= 0)
        glUniform1f(p.widen, 1.0f);
      current = nullptr;
    }
    auto &slot = programs[k];
    slot = p;
    return slot.id ? &slot : nullptr;
  }

  void FillPixel(const ShaderKey &k) {
    std::memcpy(px.colors, tevColor, sizeof(tevColor));
    std::memcpy(px.konst, tevKonst, sizeof(tevKonst));
    px.alphaRef[0] = static_cast<std::int32_t>(bp[0xF3] & 0xFF);
    px.alphaRef[1] = static_cast<std::int32_t>((bp[0xF3] >> 8) & 0xFF);
    const auto fogFloat = [](std::uint32_t v) {
      const std::uint32_t bits = ((v >> 19) & 1) << 31 | ((v >> 11) & 0xFF) << 23 | (v & 0x7FF) << 12;
      return AsFloat(bits);
    };
    px.fog[0][0] = fogFloat(bp[0xEE]);
    px.fog[0][1] = fogFloat(bp[0xF1]);
    px.fog[0][2] = static_cast<float>(bp[0xEF] & 0xFFFFFF);
    px.fog[0][3] = static_cast<float>(bp[0xF0] & 31);
    px.fog[1][0] = static_cast<float>((bp[0xF2] >> 16) & 255);
    px.fog[1][1] = static_cast<float>((bp[0xF2] >> 8) & 255);
    px.fog[1][2] = static_cast<float>(bp[0xF2] & 255);
    px.fog[1][3] = 0.0f;
    for (int i = 0; i < 8; i++) {
      px.coordSize[i][0] = static_cast<float>((bp[0x30 + 2 * i] & 0xFFFF) + 1);
      px.coordSize[i][1] = static_cast<float>((bp[0x31 + 2 * i] & 0xFFFF) + 1);
      px.coordSize[i][2] = px.coordSize[i][3] = 0.0f;
      const unsigned mode0 = bp[(i < 4 ? 0x80 : 0xA0) + (i & 3)];
      int bias = static_cast<int>((mode0 >> 9) & 0xFF);
      if (bias & 0x80)
        bias -= 256;
      px.mapSize[i][0] = static_cast<float>(bound[i].width);
      px.mapSize[i][1] = static_cast<float>(bound[i].height);
      px.mapSize[i][2] = static_cast<float>(bias) / 32.0f;
      px.mapSize[i][3] = 0.0f;
    }
    for (int s = 0; s < 4; s++) {
      const std::uint32_t r = bp[0x25 + s / 2] >> ((s & 1) ? 8 : 0);
      px.indScale[s][0] = static_cast<std::int32_t>(r & 15);
      px.indScale[s][1] = static_cast<std::int32_t>((r >> 4) & 15);
      px.indScale[s][2] = px.indScale[s][3] = 0;
    }
    for (int m = 0; m < 3; m++) {
      const std::uint32_t a = bp[0x06 + 3 * m], b = bp[0x07 + 3 * m], c = bp[0x08 + 3 * m];
      const int scale = static_cast<int>(((a >> 22) & 3) | ((b >> 22) & 3) << 2 | ((c >> 22) & 3) << 4);
      px.indMtx[2 * m][0] = Signed11(a);
      px.indMtx[2 * m][1] = Signed11(b);
      px.indMtx[2 * m][2] = Signed11(c);
      px.indMtx[2 * m][3] = 17 - scale;
      px.indMtx[2 * m + 1][0] = Signed11(a >> 11);
      px.indMtx[2 * m + 1][1] = Signed11(b >> 11);
      px.indMtx[2 * m + 1][2] = Signed11(c >> 11);
      px.indMtx[2 * m + 1][3] = 17 - scale;
    }
    (void)k;
  }

  // Vertex attribute pointers of vao for a layout (re4_gx_frame.h) and stride,
  // the first vertex at byte `base` of vbo; only changed enables are switched.
  void SetAttributes(std::uint32_t layout, std::uint32_t stride, std::uint32_t base, GLuint buffer) {
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const GLsizei s = static_cast<GLsizei>(stride);
    const auto at = [&](unsigned offset) { return reinterpret_cast<const void *>(static_cast<std::uintptr_t>(base + offset)); };
    std::uint32_t mask = 1;
    unsigned off = 0;
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, s, at(off));
    off += 12;
    if (layout & RE4_GX_V_NORMAL) {
      mask |= 1u << 1;
      glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, s, at(off));
      off += 12;
      if (layout & RE4_GX_V_BINORMAL_TANGENT) {
        mask |= 3u << 2;
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, s, at(off));
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, s, at(off + 12));
        off += 24;
      }
    }
    for (int c = 0; c < 2; c++) {
      if (layout & (c == 0 ? RE4_GX_V_COLOR0 : RE4_GX_V_COLOR1)) {
        mask |= 1u << (4 + c);
        glVertexAttribPointer(4 + c, 4, GL_UNSIGNED_BYTE, GL_TRUE, s, at(off));
        off += 4;
      }
    }
    for (int t = 0; t < 8; t++) {
      if (layout & (RE4_GX_V_TEX0 << t)) {
        mask |= 1u << (6 + t);
        glVertexAttribPointer(6 + t, 2, GL_FLOAT, GL_FALSE, s, at(off));
        off += 8;
      }
    }
    if (layout & RE4_GX_V_MATRIX) {
      mask |= 1u << 14;
      glVertexAttribIPointer(14, 3, GL_UNSIGNED_INT, s, at(off));
    }
    for (GLuint a = 0; a < 15; a++) {
      const std::uint32_t bit = 1u << a;
      if ((mask & bit) && !(attribMask & bit))
        glEnableVertexAttribArray(a);
      else if (!(mask & bit) && (attribMask & bit))
        glDisableVertexAttribArray(a);
    }
    attribMask = mask;
    attribLayout = layout;
    attribStride = stride;
    attribBase = base;
    attribBuffer = buffer;
  }

  void Draw(const Re4GxDraw &d, const Re4GxChunk &chunk) {
    // (every draw op counts for "n-th draw of its object", as Interpolate counts them ahead)
    curOwner = ownerBase ? (ownerBase ^ (static_cast<std::uint64_t>(ownerDraws++) + 1) * 0x9E3779B97F4A7C15ull) | 1 : 0;
    curLife = ownerLife;
    if (ownerBase && recordDraws && !interpPass)
      recOwnerDrawsNew[ownerBase]++;
    Split(GxStats::kSplitRest);
    if (d.indexCount == 0 || d.vertexOffset + static_cast<std::uint64_t>(d.vertexCount) * d.stride > chunk.vertexBytes ||
        d.indexOffset + d.indexCount > chunk.indexCount)
      return;
    // the shader: the one of the last draw while no register that shapes it changed
    if (keyDirty || d.layout != keyLayout) {
      key = MakeKey(d);
      keyProgram = ProgramFor(key);
      keyLayout = d.layout;
      keyDirty = false;
    }
    Split(GxStats::kSplitProgram);
    const Program *p = keyProgram;
    if (!p)
      return;
    const ShaderKey &k = key;
    const unsigned primitive = d.primitive & 0xFF;
    UseEfb();
    ApplyState(primitive);
    const float pointSize = static_cast<float>((bp[0x22] >> 8) & 0xFF) / 6.0f * Sf(1);
    if (xf.proj[1][3] != pointSize) {
      xf.proj[1][3] = pointSize;
      Dirty(207, 208);
    }
    if (xfRegsDirty)
      FillXfRegs();
    Split(GxStats::kSplitState);
    std::uint32_t blended = 0;
    if (interpPass)
      blended = BlendDraw(d, k, chunk);
    else if (recordDraws)
      RecordDraw(d, k);
    Split(GxStats::kSplitRest);
    UploadXf();
    if (pxDirty || !pxValid) {
      FillPixel(k);
      pxDirty = false;
      if (!pxValid || std::memcmp(&px, &pxSent, sizeof(px)) != 0) {
        UploadBlock(kPixelBinding, pxUbo, &px, 0, sizeof(PixelBlock), sizeof(PixelBlock));
        mergePixel = true;
        pxSent = px;
        pxValid = true;
      }
    }
    Split(GxStats::kSplitUniform);
    if (current != p) {
      glUseProgram(p->id);
      current = p;
      stats.programSwitches++;
    }
    if (p->mtx >= 0 && (p->lastMtx[0] != d.matrices[0] || p->lastMtx[1] != d.matrices[1] ||
                        p->lastMtx[2] != d.matrices[2])) {
      glUniform3ui(p->mtx, d.matrices[0], d.matrices[1], d.matrices[2]);
      std::memcpy(p->lastMtx, d.matrices, sizeof(p->lastMtx));
      mergeMatrix = true;
    }
    if (p->widen >= 0) {
      const float w = WidenFor(k);
      if (p->lastWiden != w) {
        glUniform1f(p->widen, w);
        p->lastWiden = w;
        mergeMatrix = true;
      }
    }
    if (!unitsValid) {
      for (int m = 0; m < 8; m++)
        unitTex[m] = unitSampler[m] = ~0u;
      unitsValid = true;
    }
    for (int m = 0; m < 8; m++) {
      if (!(k.texMaps & (1u << m)))
        continue;
      const GLuint id = bound[m].id ? bound[m].id : whiteTex;
      if (unitTex[m] != id) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(m));
        glBindTexture(GL_TEXTURE_2D, id);
        unitTex[m] = id;
        stats.textureBinds++;
      }
      const std::uint32_t mode0 = bp[(m < 4 ? 0x80 : 0xA0) + (m & 3)], mode1 = bp[(m < 4 ? 0x84 : 0xA4) + (m & 3)];
      const GLuint sampler = Sampler(mode0, mode1, bound[m].levels);
      if (unitSampler[m] != sampler) {
        glBindSampler(static_cast<GLuint>(m), sampler);
        unitSampler[m] = sampler;
        stats.textureBinds++;
      }
    }
    // vertex attributes: with a base vertex the same setup serves every draw
    // of a layout and stride (the vertex block starts at base + n * stride)
    if (!vaoBound) {
      glBindVertexArray(vao);
      vaoBound = true;
    }
    // (an in-between picture's blended vertices: from blendVbo)
    const GLuint buffer = blendAt >= 0 ? (blendMap ? blendRing : blendVbo) : vbo;
    const std::uint32_t at = blendAt >= 0 ? static_cast<std::uint32_t>(blendAt) : d.vertexOffset;
    blendAt = -1;
    std::uint32_t base = at;
    GLint baseVertex = 0;
    if (drawBaseVertex && d.stride) {
      base = at % d.stride;
      baseVertex = static_cast<GLint>(at / d.stride);
    }
    if (d.layout != attribLayout || d.stride != attribStride || base != attribBase || buffer != attribBuffer) {
      SetAttributes(d.layout, d.stride, base, buffer);
      stats.attributeSetups++;
    }
    const GLenum mode = primitive == RE4_GX_LINES ? GL_LINES : primitive == RE4_GX_POINTS ? GL_POINTS : GL_TRIANGLES;
    const void *indices = reinterpret_cast<const void *>(static_cast<std::uintptr_t>(d.indexOffset) * 2);
    Split(GxStats::kSplitBind);
    if (drawBaseVertex)
      drawBaseVertex(mode, static_cast<GLsizei>(d.indexCount), GL_UNSIGNED_SHORT, indices, baseVertex);
    else
      glDrawElements(mode, static_cast<GLsizei>(d.indexCount), GL_UNSIGNED_SHORT, indices);
    Split(GxStats::kSplitDraw);
    if (!interpPass) {
      const MergeSnap now{stats.uniformUploads, stats.programSwitches, stats.textureBinds, stats.stateChanges,
                          stats.attributeSetups, stats.textureCopies, stats.displayCopies, mode};
      if (mergeValid && !blended && now.programs == mergeLast.programs && now.binds == mergeLast.binds &&
          now.states == mergeLast.states && now.setups == mergeLast.setups && now.copies == mergeLast.copies &&
          now.displays == mergeLast.displays && now.mode == mergeLast.mode) {
        if (now.uniforms == mergeLast.uniforms && !mergeMatrix)
          stats.mergeSame++;
        else {
          stats.mergeUniform++;
          if (!mergePixel)
            stats.mergeMatrixOnly++;
        }
      }
      mergeLast = now;
      mergeValid = !blended;
      mergeMatrix = mergePixel = false;
    }
    stats.draws++;
    if (mode == GL_TRIANGLES)
      stats.triangles += d.indexCount / 3;
    if (blended)
      BlendRestore(blended);
  }

  // "4:3 (exact)": a flat draw over the whole screen (its viewport starts at the left edge and is
  // at least 500 of the game's 512 pixels wide: HUD, menus, text, fades, the letterbox) gets the
  // factor; not one that reads an EFB copy (heat haze, water, the pause filter, blurs: they lay the
  // picture under them back over it and must stay on it), nor one into a part of the EFB (a
  // picture for a texture)
  float WidenFor(const ShaderKey &k) const {
    const float f = widen2d.load(std::memory_order_relaxed);
    if (f == 1.0f || !k.orthographic)
      return 1.0f;
    const float half = AsFloat(xfReg[0x1A]), centre = AsFloat(xfReg[0x1D]) - 342.0f;
    if (half * 2.0f < 500.0f || std::fabs(centre - half) > 1.0f)
      return 1.0f;
    for (int m = 0; m < 8; m++)
      if ((k.texMaps & (1u << m)) && (boundKey[m] >> 63))
        return 1.0f;
    return f;
  }

  // --- EFB copies -------------------------------------------------------------------

  // Copy format and conversion of BP 0x52 (and the EFB pixel format): mode for
  // the copy shader (0 RGBA8, 1 RGB565, 2 RGB5A3, 3 intensity I, 4 intensity IA,
  // 5 R, 6 G, 7 B, 8 A, 9 RG, 10 GB, 11 RA, 12 depth high byte, 13 depth middle,
  // 14 depth low, 15 depth 16 bit, 16 depth 24 bit) and the kind of the result.
  static void CopyMode(std::uint32_t control, bool depth, int *mode, unsigned *kind) {
    const unsigned format = ((control >> 4) & 7) | ((control >> 3) & 1) << 3;
    const bool intensity = (control >> 15) & 1;
    *kind = 0;
    if (depth) {
      switch (format) {
      case 0x9: *mode = 13; *kind = 1; return;  // Z8M
      case 0xA: *mode = 14; *kind = 1; return;  // Z8L
      case 0xB:                                 // Z16
      case 0x3:
      case 0xC: *mode = 15; *kind = 2; return;  // Z16L (approx.)
      case 0x6: *mode = 16; *kind = 0; return;  // Z24X8
      default: *mode = 12; *kind = 1; return;   // Z4, Z8
      }
    }
    if (intensity) {
      if (format == 2 || format == 3) {
        *mode = 4;
        *kind = 2;
      } else {
        *mode = 3;
        *kind = 1;
      }
      return;
    }
    switch (format) {
    case 0x0:
    case 0x1:
    case 0x8: *mode = 5; *kind = 1; return; // R4, R8
    case 0x2:
    case 0x3: *mode = 11; *kind = 2; return; // RA4, RA8
    case 0x4: *mode = 1; return;
    case 0x5: *mode = 2; return;
    case 0x7: *mode = 8; *kind = 1; return; // A8
    case 0x9: *mode = 6; *kind = 1; return; // G8
    case 0xA: *mode = 7; *kind = 1; return; // B8
    case 0xB: *mode = 9; *kind = 2; return; // RG8
    case 0xC: *mode = 10; *kind = 2; return; // GB8
    default: *mode = 0; return;             // RGBA8 (and YUVA8)
    }
  }

  void EfbCopy(std::uint32_t control) {
    Touched(); // its own pass
    const int x = static_cast<int>(bp[0x49] & 0x3FF), y = static_cast<int>((bp[0x49] >> 10) & 0x3FF);
    const int w = static_cast<int>(bp[0x4A] & 0x3FF) + 1, h = static_cast<int>((bp[0x4A] >> 10) & 0x3FF) + 1;
    const std::uint32_t dest = (bp[0x4B] & 0x1FFFFF) << 5;
    const bool toXfb = (control >> 14) & 1;
    const bool half = !toXfb && ((control >> 9) & 1);
    const bool depth = !toXfb && (bp[0x43] & 7) == 3;
    int mode = 0;
    unsigned kind = 0;
    if (!toXfb)
      CopyMode(control, depth, &mode, &kind);
    const int ow = half ? std::max(1, w / 2) : w, oh = half ? std::max(1, h / 2) : h;
    if (toXfb && holdXfb) {
      // the VI stays on the picture before (RE4_GX_CHUNK_HOLD, SYS_SCREEN_STOP):
      // nothing to show, only the clear of the EFB
      stats.heldCopies++;
      SaveAndPlainState();
      BindFbo(DrawFbo());
      if (peekWanted)
        SnapshotDepth();
      ClearAfterCopy(control, x, y, w, h);
      sceneBottom = 0;
      return;
    }
    GLuint target = 0;
    if (toXfb) {
      if (!xfbTex || xfbWidth != static_cast<unsigned>(w) || xfbHeight != static_cast<unsigned>(h)) {
        if (xfbTex)
          glDeleteTextures(1, &xfbTex);
        glGenTextures(1, &xfbTex);
        glBindTexture(GL_TEXTURE_2D, xfbTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Hi(w), Hi(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        xfbWidth = static_cast<unsigned>(w);
        xfbHeight = static_cast<unsigned>(h);
      }
      target = xfbTex;
      haveXfb = true;
      stats.displayCopies++;
    } else {
      // the in-between pass copies into its own textures: the next real Run still reads what the
      // picture before copied (effects that blend the last picture in)
      if (interpPass)
        interpCopied.insert(dest);
      Copy &c = interpPass ? interpCopies[dest] : copies[dest];
      if (!c.id || c.width != static_cast<unsigned>(ow) || c.height != static_cast<unsigned>(oh)) {
        if (c.id)
          glDeleteTextures(1, &c.id);
        glGenTextures(1, &c.id);
        glBindTexture(GL_TEXTURE_2D, c.id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Hi(ow), Hi(oh), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        c.width = static_cast<unsigned>(ow);
        c.height = static_cast<unsigned>(oh);
      }
      c.serial = ++copySerial;
      c.kind = kind;
      target = c.id;
      stats.textureCopies++;
      // copies read through a palette are made anew from this copy (their key holds the copy's
      // serial: those of the in-between pass's own copies stay apart)
      for (auto it = paletted.begin(); !interpPass && it != paletted.end();) {
        glDeleteTextures(1, &it->second.id);
        it = paletted.erase(it);
      }
    }
    // render the EFB region into the target
    Resolve(Lo(x), Lo(y), Hi(x + w) - Lo(x), Hi(y + h) - Lo(y), depth);
    BindFbo(xfbFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    SaveAndPlainState();
    // the copy draws over all of its target (the viewport is the texture)
    ForgetColor();
    const int tw = toXfb ? w : ow, th = toXfb ? h : oh;
    glViewport(0, 0, Hi(tw), Hi(th));
    glUseProgram(copyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depth ? efbDepth : efbColor);
    glBindSampler(0, 0);
    glUniform1i(copyMode, toXfb ? 0 : mode);
    glUniform4f(copyRect, Sf(x), Sf(y), Sf(w), Sf(h));
    glUniform2f(copySize, static_cast<float>(Lo(kEfbWidth)), static_cast<float>(Lo(kEfbHeight)));
    glUniform1i(copyHalf, half ? 1 : 0);
    glUniform1f(copyGain, depth ? 1.0f : CopyFilterGain());
    // The game reads some copies (water, heat haze: EFB rows 56..447 at half size) through a texture
    // object that is shorter than the copy, so on the GameCube a lookup below the drawn picture
    // clamps to the last drawn row. The copy here is read whole: rows the letterbox never drew
    // (black) would show as dark waves at the bottom edge. Repeat the last drawn row below it.
    // In the 16:9 formats the app shows rows 56..391 only: there a copy of the whole picture (the
    // shimmer of the logos at the start, IdShimmerTrans: rows 0..447, a movie drawn into 56..391
    // under a full scissor; the depth of field at the edge) repeats the edge rows of what is shown
    // instead of reading the unseen bars (black waves at the bottom of the intro, 0.91.0 test).
    int top = 0, bottom = kEfbHeight;
    if (!toXfb && !depth && x == 0 && w >= 500) {
      if (half && sceneBottom > y + h / 2 && sceneBottom < y + h)
        bottom = sceneBottom;
      const int shownT = shownTop.load(std::memory_order_relaxed), shownB = shownBottom.load(std::memory_order_relaxed);
      if (shownB > y + h / 2 && shownB < y + h)
        bottom = std::min(bottom, shownB);
      if (shownT > y && shownT < y + h / 2)
        top = shownT;
    }
    const bool limited = top > 0 || bottom < kEfbHeight;
    glUniform2f(copyLimit, limited ? Sf(top) : -1e9f, limited && bottom < kEfbHeight ? Sf(bottom) : 1e9f);
    if (limited)
      stats.limitedCopies++;
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (limited)
      glUniform2f(copyLimit, -1e9f, 1e9f);
    current = nullptr;
    BindFbo(DrawFbo());
    if (toXfb)
      Fxaa(Hi(w), Hi(h));
    if (toXfb && peekWanted)
      SnapshotDepth();  // the picture is complete, the clear comes next
    ClearAfterCopy(control, x, y, w, h);
    if (toXfb)
      sceneBottom = 0;
  }

  // --- FXAA ------------------------------------------------------------------------------------

  bool MakeFxaa() {
    static const char kVs[] = "#version 300 es\n"
                              "out vec2 vUv;\n"
                              "void main() {\n"
                              "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                              "  vUv = p;\n"
                              "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                              "}\n";
    // Edge smoothing along the luma gradient: the direction of the edge from the four corners'
    // luma, two taps along it (each between samples) and a wider pair; the wider mix is dropped when
    // it leaves the range of the neighbourhood's luma (a thin line stays a line).
    static const char kFs[] =
        "#version 300 es\nprecision highp float;\n"
        "uniform sampler2D src;\nuniform vec2 texel;\nin vec2 vUv;\nout vec4 o;\n"
        "const vec3 kLuma = vec3(0.299, 0.587, 0.114);\n"
        "void main() {\n"
        "  vec3 nw = texture(src, vUv + vec2(-1.0, -1.0) * texel).rgb;\n"
        "  vec3 ne = texture(src, vUv + vec2(1.0, -1.0) * texel).rgb;\n"
        "  vec3 sw = texture(src, vUv + vec2(-1.0, 1.0) * texel).rgb;\n"
        "  vec3 se = texture(src, vUv + vec2(1.0, 1.0) * texel).rgb;\n"
        "  vec3 m = texture(src, vUv).rgb;\n"
        "  float lnw = dot(nw, kLuma), lne = dot(ne, kLuma), lsw = dot(sw, kLuma), lse = dot(se, kLuma), lm = dot(m, kLuma);\n"
        "  float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));\n"
        "  float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));\n"
        "  vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));\n"
        "  float reduce = max((lnw + lne + lsw + lse) * 0.25 * 0.125, 1.0 / 128.0);\n"
        "  dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-8.0), vec2(8.0)) * texel;\n"
        "  vec3 a = 0.5 * (texture(src, vUv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(src, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
        "  vec3 b = a * 0.5 + 0.25 * (texture(src, vUv + dir * -0.5).rgb + texture(src, vUv + dir * 0.5).rgb);\n"
        "  float lb = dot(b, kLuma);\n"
        "  o = vec4((lb < lmin || lb > lmax) ? a : b, 1.0);\n"
        "}\n";
    std::string log;
    fxaaProgram = Link(kVs, kFs, &log);
    if (!fxaaProgram) {
      GXLOG("FXAA: shader failed: %s", log.c_str());
      return false;
    }
    glUseProgram(fxaaProgram);
    glUniform1i(glGetUniformLocation(fxaaProgram, "src"), 0);
    fxaaTexelLoc = glGetUniformLocation(fxaaProgram, "texel");
    glGenFramebuffers(1, &fxaaFbo);
    return true;
  }

  // The display copy just made (xfbTex, w x h texels) through FXAA into fxaaTex: that is shown now.
  void Fxaa(int w, int h) {
    fxaaShown = false;
    if (!fxaaOn || !xfbTex || w < 16 || h < 16)
      return;
    if (!fxaaProgram && !MakeFxaa()) {
      fxaaOn = false;  // no shader on this driver: off for good (the menu's switch stays)
      return;
    }
    if (!fxaaTex || fxaaTexW != static_cast<unsigned>(w) || fxaaTexH != static_cast<unsigned>(h)) {
      if (fxaaTex)
        glDeleteTextures(1, &fxaaTex);
      glGenTextures(1, &fxaaTex);
      glBindTexture(GL_TEXTURE_2D, fxaaTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      fxaaTexW = static_cast<unsigned>(w);
      fxaaTexH = static_cast<unsigned>(h);
    }
    Touched();
    SaveAndPlainState();
    BindFbo(fxaaFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fxaaTex, 0);
    ForgetColor();  // all of it is drawn over
    glViewport(0, 0, w, h);
    glUseProgram(fxaaProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, xfbTex);
    glBindSampler(0, 0);
    glUniform2f(fxaaTexelLoc, 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    BindFbo(DrawFbo());
    current = nullptr;
    fxaaShown = true;
    stats.fxaaPasses++;
  }

  GLuint ShownXfb() const { return fxaaShown && fxaaTex ? fxaaTex : xfbTex; }

  void FxaaRelease() {
    if (fxaaTex)
      glDeleteTextures(1, &fxaaTex);
    if (fxaaFbo)
      glDeleteFramebuffers(1, &fxaaFbo);
    if (fxaaProgram)
      glDeleteProgram(fxaaProgram);
    fxaaTex = fxaaFbo = fxaaProgram = 0;
    fxaaTexW = fxaaTexH = 0;
    fxaaShown = false;
  }

  // --- the depth for the game's GXPeekZ ------------------------------------------------------

  bool MakePeek() {
    static const char kVs[] = "#version 300 es\n"
                              "void main() {\n"
                              "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                              "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                              "}\n";
    // cell (x, y) of kDepthWidth x kDepthHeight: the depth at the EFB pixel (2x + 1, 2y + 1) (the
    // game peeks single pixels; the centre of a 2 x 2 block stands for it), as GX's 24 bits in
    // r (low byte), g, b (a 0: a cell is the 24-bit value)
    static const char kFs[] =
        "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        "uniform highp sampler2D depths;\nuniform vec2 size;\nuniform float scale;\nout vec4 o;\n"
        "void main() {\n"
        "  vec2 c = floor(gl_FragCoord.xy);\n"
        "  ivec2 t = ivec2(min((c * 2.0 + 1.0) * scale, size - 1.0));\n"
        "  uint z = min(uint(texelFetch(depths, t, 0).r * 16777215.0 + 0.5), 16777215u);\n"
        "  o = vec4(float(z & 255u), float((z >> 8) & 255u), float((z >> 16) & 255u), 0.0) / 255.0;\n"
        "}\n";
    std::string log;
    peekProgram = Link(kVs, kFs, &log);
    if (!peekProgram) {
      GXLOG("depth map for GXPeekZ: shader failed: %s", log.c_str());
      return false;
    }
    glUseProgram(peekProgram);
    glUniform1i(glGetUniformLocation(peekProgram, "depths"), 0);
    peekSizeLoc = glGetUniformLocation(peekProgram, "size");
    peekScaleLoc = glGetUniformLocation(peekProgram, "scale");
    glGenTextures(1, &peekTex);
    glBindTexture(GL_TEXTURE_2D, peekTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kDepthWidth, kDepthHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &peekFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, peekFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, peekTex, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenBuffers(2, peekPbo);
    for (GLuint pbo : peekPbo) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
      glBufferData(GL_PIXEL_PACK_BUFFER, kDepthWidth * kDepthHeight * 4, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!ok)
      GXLOG("depth map for GXPeekZ: the frame buffer is not complete");
    return ok;
  }

  // The depth maps the GPU is through with go to the core (a fence per map; no waiting).
  void DeliverDepth() {
    for (int i = 0; i < 2; i++) {
      if (!peekFence[i])
        continue;
      const GLenum r = glClientWaitSync(peekFence[i], 0, 0);
      if (r != GL_ALREADY_SIGNALED && r != GL_CONDITION_SATISFIED)
        continue;
      glDeleteSync(peekFence[i]);
      peekFence[i] = nullptr;
      glBindBuffer(GL_PIXEL_PACK_BUFFER, peekPbo[i]);
      const void *data = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, kDepthWidth * kDepthHeight * 4, GL_MAP_READ_BIT);
      if (data) {
        if (depthSink)
          depthSink(static_cast<const std::uint32_t *>(data), peekSlotPicture[i]);
        stats.depthDelivered++;
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
  }

  // At the display copy of a picture whose chunk peeked (the marker): the depth of the picture.
  void SnapshotDepth() {
    peekWanted = false;
    if (!depthSink || peekFailed)
      return;
    if (!peekProgram && !MakePeek()) {
      peekFailed = true;
      return;
    }
    DeliverDepth();
    const int slot = peekFence[0] ? (peekFence[1] ? -1 : 1) : 0;
    if (slot < 0)
      return;  // the GPU is two maps behind: the core keeps the last one (it ages out there)
    Touched();
    // MSAA in own buffers: the depth of the picture's 512 x 448 (the game peeks inside it)
    Resolve(0, 0, Lo(512), Lo(448), true);
    SaveAndPlainState();
    BindFbo(peekFbo);
    glViewport(0, 0, kDepthWidth, kDepthHeight);
    glUseProgram(peekProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, efbDepth);
    glBindSampler(0, 0);
    glUniform2f(peekSizeLoc, static_cast<float>(Lo(kEfbWidth)), static_cast<float>(Lo(kEfbHeight)));
    glUniform1f(peekScaleLoc, Sf(1));
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, peekPbo[slot]);
    glReadPixels(0, 0, kDepthWidth, kDepthHeight, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    peekFence[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    peekSlotPicture[slot] = peekPicture;
    stats.depthSnapshots++;
    BindFbo(DrawFbo());
    current = nullptr;
  }

  void PeekRelease() {
    for (GLsync &f : peekFence) {
      if (f)
        glDeleteSync(f);
      f = nullptr;
    }
    if (peekPbo[0])
      glDeleteBuffers(2, peekPbo);
    peekPbo[0] = peekPbo[1] = 0;
    if (peekFbo)
      glDeleteFramebuffers(1, &peekFbo);
    if (peekTex)
      glDeleteTextures(1, &peekTex);
    if (peekProgram)
      glDeleteProgram(peekProgram);
    peekFbo = peekTex = peekProgram = 0;
  }

  // The app's gameplay autofocus (BP 0x5A: the core marks the place,
  // port/whole/runtime/dof.cpp, in the game's filter table after the scene
  // and before the interface): the blur of the first Android port
  // (depth_of_field_gl.cpp, RE4-Decomp) on the EFB. The distance at the centre
  // of the picture (median of nine depths) is the focus; what lies well
  // behind it gets soft (at most 60 %), nearer parts stay sharp, and only taps
  // behind the focus are mixed in, so the sharp foreground does not bleed into
  // the background. A focus beyond 6 m (looking into the distance) leaves the
  // picture as it is. Distances from the depths through the scene's
  // perspective projection and the XF viewport (GL depth = (zRange * (A + B /
  // distance) + zFar) / 2^24-1, see the vertex shader); the picture is the
  // XF viewport (the game's 512x448 at the top left of the EFB). `player`
  // (the marker's value): how deep Leon and his weapon reach into the
  // picture; only what lies deeper can be the focus (standing, the centre is
  // often on Leon): the nine depths at the centre, or, when fewer than five
  // of them lie behind Leon, 35 around it (15 % across, 12 % up and down);
  // none behind him: no blur.
  void Autofocus(std::uint32_t player) {
    if (autofocusFailed || !havePerspective)
      return;
    Touched(); // its own passes
    const int fw = Lo(kEfbWidth), fh = Lo(kEfbHeight);
    if (!blurProgram && !MakeAutofocus(fw, fh)) {
      autofocusFailed = true;
      GXLOG("gameplay autofocus could not be set up; left out");
      BindFbo(DrawFbo());
      return;
    }
    // the picture: the XF viewport, in EFB pixels
    const float halfW = AsFloat(xfReg[0x1A]), halfH = -AsFloat(xfReg[0x1B]);
    const float cx = AsFloat(xfReg[0x1D]) - 342.0f, cy = AsFloat(xfReg[0x1E]) - 342.0f;
    int x0 = static_cast<int>(std::lround(cx - halfW)), y0 = static_cast<int>(std::lround(cy - halfH));
    int x1 = static_cast<int>(std::lround(cx + halfW)), y1 = static_cast<int>(std::lround(cy + halfH));
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(kEfbWidth, x1);
    y1 = std::min(kEfbHeight, y1);
    if (!(halfW > 0.0f && halfH > 0.0f) || x1 - x0 < 16 || y1 - y0 < 16) {
      x0 = y0 = 0;
      x1 = kEfbWidth;
      y1 = kEfbHeight;
    }
    const float rx = static_cast<float>(Lo(x0)), ry = static_cast<float>(Lo(y0));
    const float rw = static_cast<float>(Lo(x1) - Lo(x0)), rh = static_cast<float>(Lo(y1) - Lo(y0));
    float zRange = AsFloat(xfReg[0x1C]);
    if (!(std::fabs(zRange) > 1.0f))
      zRange = 16777215.0f;
    const float zFar = AsFloat(xfReg[0x1F]);
    // only the picture is read (the focus, the blur's taps stay inside it)
    Resolve(Lo(x0), Lo(y0), Lo(x1) - Lo(x0), Lo(y1) - Lo(y0), false);
    Resolve(Lo(x0), Lo(y0), Lo(x1) - Lo(x0), Lo(y1) - Lo(y0), true);
    SaveAndPlainState();
    glDepthMask(GL_FALSE);
    glBindVertexArray(quadVao);
    for (int i = 0; i < 3; i++)
      glBindSampler(i, 0);
    // the focus: distance (16 bits in r, g), strength (b) in the first texel, the last two
    // measurements behind it. It follows the measurements step by step from the one before when
    // the autofocus ran in the chunk before, else (the first, after a pause) it starts anew
    // (the in-between pass is not counted: it follows the measurements as Run(next) will, and
    // Interpolate puts focusCur back afterwards)
    const bool restart = focusChunk + 1 != stats.chunks + (interpPass ? 1u : 0u);
    if (!interpPass)
      focusChunk = stats.chunks;
    const int next = focusCur ^ 1;
    BindFbo(focusFbo[next]);
    ForgetColor();  // its texels are drawn over
    glViewport(0, 0, 3, 1);
    glUseProgram(focusProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, efbDepth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, focusTex[focusCur]);
    glUniform4f(focusRegion, rx, ry, rw, rh);
    glUniform2f(focusSize, static_cast<float>(fw), static_cast<float>(fh));
    glUniform4f(focusZ, perspA, perspB, zRange, zFar);
    glUniform1f(focusPlayer, static_cast<float>(player));
    glUniform1f(focusRestart, restart ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    focusCur = next;
    // the first port's tap distance: 1.5 pixels of a 480-line picture
    const float radius = std::min(4.0f, std::max(1.0f, rh / 480.0f * 1.5f));
    if (options.halfDof && halfProgram) {
      // GPU step e: the blur at half the resolution (a quarter of the pixels, each of them the
      // four EFB texels around its centre and the taps behind the focus)
      const int hx0 = Lo(x0) / 2, hy0 = Lo(y0) / 2;
      const int hx1 = (Lo(x1) + 1) / 2, hy1 = (Lo(y1) + 1) / 2;
      BindFbo(halfFbo);
      ForgetColor();  // only the picture's region is drawn, and the mix reads only it
      glViewport(hx0, hy0, hx1 - hx0, hy1 - hy0);
      glUseProgram(halfProgram);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, efbColor);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, efbDepth);
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, focusTex[focusCur]);
      glUniform4f(halfRegion, rx, ry, rw, rh);
      glUniform2f(halfSize, static_cast<float>(fw), static_cast<float>(fh));
      glUniform4f(halfZ, perspA, perspB, zRange, zFar);
      glUniform1f(halfRadius, radius);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      // mixed into the EFB at the full resolution: each pixel's own distance says how much of the
      // blur (sharp in front of the focus: nothing drawn), blended over the picture; colour only,
      // alpha and depth stay. The mix reads the depth texture, so not through efbFbo, whose depth
      // it is (the multisampled EFB has its own). On the tiles (d2) the EFB's textures alternate:
      // colorFbo takes the current one, which the next pass draws back
      BindFbo(msaaFbo ? msaaFbo : colorFbo);
      if (!msaaFbo)
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efbColor, 0);
      glViewport(Lo(x0), Lo(y0), Lo(x1) - Lo(x0), Lo(y1) - Lo(y0));
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glUseProgram(mixProgram);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, halfTex);
      glUniform4f(mixRegion, rx, ry, rw, rh);
      glUniform2f(mixSize, static_cast<float>(fw), static_cast<float>(fh));
      glUniform4f(mixZ, perspA, perspB, zRange, zFar);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisable(GL_BLEND);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      BindFbo(DrawFbo());
      current = nullptr;
      stats.autofocus++;
      return;
    }
    // the blurred picture into blurTex
    BindFbo(blurFbo);
    ForgetColor();  // only the picture's region is drawn, and only it is copied back
    glViewport(Lo(x0), Lo(y0), Lo(x1) - Lo(x0), Lo(y1) - Lo(y0));
    glUseProgram(blurProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, efbColor);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, efbDepth);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, focusTex[focusCur]);
    glUniform4f(blurRegion, rx, ry, rw, rh);
    glUniform2f(blurSize, static_cast<float>(fw), static_cast<float>(fh));
    glUniform4f(blurZ, perspA, perspB, zRange, zFar);
    glUniform1f(blurRadius, radius);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    // back into the EFB: colour only, alpha and depth stay
    BindFbo(DrawFbo());
    UseEfb();
    glViewport(Lo(x0), Lo(y0), Lo(x1) - Lo(x0), Lo(y1) - Lo(y0));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glUseProgram(copyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, blurTex);
    glUniform1i(copyMode, 0);
    glUniform4f(copyRect, rx, ry, rw, rh);
    glUniform2f(copySize, static_cast<float>(fw), static_cast<float>(fh));
    glUniform1i(copyHalf, 0);
    glUniform1f(copyGain, 1.0f);
    glUniform2f(copyLimit, -1e9f, 1e9f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    current = nullptr;
    stats.autofocus++;
  }

  bool MakeAutofocus(int fw, int fh) {
    static const char kQuadVs[] = "#version 300 es\n"
                                  "out vec2 vUv;\n"
                                  "void main() {\n"
                                  "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                                  "  vUv = p;\n"
                                  "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                  "}\n";
    // region: the picture in EFB texels (x, y, w, h); size: the EFB texture;
    // zmap: projection A, B, viewport zRange, zFar
    static const char kDistance[] =
        "uniform highp sampler2D depths;\nuniform vec4 region;\nuniform vec2 size;\nuniform vec4 zmap;\n"
        "float distanceAt(vec2 t) {\n"
        "  float q = (texture(depths, t / size).r * 16777215.0 - zmap.w) / zmap.z + zmap.x;\n"
        "  return q < 0.0 ? zmap.y / q : 1.0e9;\n"
        "}\n";
    // The measurement: the median of nine depths at the centre (the first port's), or of 35 around
    // it when fewer than five lie behind the player; 65535 (far: no blur) when none does. The
    // focus in use (texel 0) follows the median of the last three measurements by a quarter of the
    // way each picture: one picture measured wrong (a twig or the gun at the centre for a moment,
    // a GPU's depth of an edge; the Retroid with MSAA, 19.10.2026: thin branches went soft for
    // one picture) changes nothing, a new distance is taken over in about a quarter of a second.
    // Texels 1 and 2: the last two measurements. restart: no picture before (all three anew).
    const std::string focusFs =
        std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n") + kDistance +
        "uniform float player;\nuniform highp sampler2D history;\nuniform float restart;\nout vec4 o;\n"
        "float measure() {\n"
        "  float z[35];\n"
        "  for (int i = 0; i < 35; ++i) z[i] = 0.0;\n"
        "  int n = 0;\n"
        "  vec2 c = region.xy + 0.5 * region.zw;\n"
        "  for (int y = -1; y <= 1; ++y)\n"
        "    for (int x = -1; x <= 1; ++x) {\n"
        "      float d = distanceAt(c + vec2(float(x), float(y)) * 0.006 * region.zw);\n"
        "      if (d > player) z[n++] = d;\n"
        "    }\n"
        "  if (n < 5) {\n"
        "    n = 0;\n"
        "    for (int y = -2; y <= 2; ++y)\n"
        "      for (int x = -3; x <= 3; ++x) {\n"
        "        float d = distanceAt(c + vec2(float(x) * 0.05, float(y) * 0.06) * region.zw);\n"
        "        if (d > player) z[n++] = d;\n"
        "      }\n"
        "  }\n"
        "  if (n == 0) return 65535.0;\n"
        "  for (int i = 0; i < 34; ++i)\n"
        "    for (int j = i + 1; j < 35; ++j)\n"
        "      if (j < n && z[j] < z[i]) { float t = z[i]; z[i] = z[j]; z[j] = t; }\n"
        "  return clamp(round(z[n / 2]), 0.0, 65535.0);\n"
        "}\n"
        "float stored(int i) {\n"
        "  vec4 t = texelFetch(history, ivec2(i, 0), 0);\n"
        "  return round(t.r * 255.0) * 256.0 + round(t.g * 255.0);\n"
        "}\n"
        "vec4 focus(float f) {\n"
        "  uint d = uint(clamp(round(f), 0.0, 65535.0));\n"
        "  return vec4(float(d >> 8u) / 255.0, float(d & 255u) / 255.0, 1.0 - smoothstep(3000.0, 6000.0, f), 1.0);\n"
        "}\n"
        "void main() {\n"
        "  int i = int(gl_FragCoord.x);\n"
        "  if (i == 2 && restart < 0.5) { o = texelFetch(history, ivec2(1, 0), 0); return; }\n"
        "  float m = measure();\n"
        "  if (i != 0 || restart > 0.5) { o = focus(m); return; }\n"
        "  float b = stored(1), c = stored(2);\n"
        "  float target = max(min(m, b), min(max(m, b), c));\n"
        "  float before = stored(0);\n"
        "  o = focus(abs(target - before) < 4.0 ? target : before + (target - before) * 0.25);\n"
        "}\n";
    const std::string blurFs =
        std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n") + kDistance +
        "uniform sampler2D frame;\nuniform sampler2D focus;\nuniform float radius;\nout vec4 o;\n"
        "void main() {\n"
        "  vec2 t = gl_FragCoord.xy;\n"
        "  vec4 original = texelFetch(frame, ivec2(t), 0);\n"
        "  vec4 f = texelFetch(focus, ivec2(0), 0);\n"
        "  float focal = round(f.r * 255.0) * 256.0 + round(f.g * 255.0);\n"
        "  float start = focal + max(800.0, focal * 0.25), end = focal + max(2500.0, focal);\n"
        "  float amount = 0.6 * f.b * smoothstep(start, end, distanceAt(t));\n"
        "  if (amount <= 0.0) { o = original; return; }\n"
        "  vec3 sum = original.rgb * 2.0;\n"
        "  float weight = 2.0;\n"
        "  const vec2 taps[8] = vec2[8](vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1), vec2(-0.707, -0.707),\n"
        "                               vec2(0.707, 0.707), vec2(-0.707, 0.707), vec2(0.707, -0.707));\n"
        "  vec2 lo = region.xy + 0.5, hi = region.xy + region.zw - 0.5;\n"
        "  for (int i = 0; i < 8; ++i) {\n"
        "    vec2 p = clamp(t + taps[i] * radius, lo, hi);\n"
        "    if (distanceAt(p) >= start) { sum += texture(frame, p / size).rgb; weight += 1.0; }\n"
        "  }\n"
        "  o = vec4(mix(original.rgb, sum / weight, amount), original.a);\n"
        "}\n";
    // GPU step e: the blur above at half the resolution, without the mix. Its centre is the four
    // EFB texels under the texel (together weighted as the one above), the taps as above, and only
    // what lies behind the focus counts, the centre too: the sharp foreground does not bleed into
    // the background through the coarser texels. Alpha: something behind the focus was there
    // (1, the colour as it is) or not (0, black), so the mix can leave the latter out.
    const std::string halfFs =
        std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n") + kDistance +
        "uniform sampler2D frame;\nuniform sampler2D focus;\nuniform float radius;\nout vec4 o;\n"
        "void main() {\n"
        "  vec2 t = gl_FragCoord.xy * 2.0;\n"
        "  vec4 f = texelFetch(focus, ivec2(0), 0);\n"
        "  if (f.b <= 0.0) { o = vec4(0.0); return; }\n"
        "  float focal = round(f.r * 255.0) * 256.0 + round(f.g * 255.0);\n"
        "  float start = focal + max(800.0, focal * 0.25);\n"
        "  vec2 lo = region.xy + 0.5, hi = region.xy + region.zw - 0.5;\n"
        "  vec3 sum = vec3(0.0);\n"
        "  float weight = 0.0;\n"
        "  for (int i = 0; i < 4; ++i) {\n"
        "    vec2 p = clamp(t + vec2(float(i & 1) - 0.5, float(i >> 1) - 0.5), lo, hi);\n"
        "    if (distanceAt(p) >= start) { sum += texture(frame, p / size).rgb * 0.5; weight += 0.5; }\n"
        "  }\n"
        "  const vec2 taps[8] = vec2[8](vec2(-1, 0), vec2(1, 0), vec2(0, -1), vec2(0, 1), vec2(-0.707, -0.707),\n"
        "                               vec2(0.707, 0.707), vec2(-0.707, 0.707), vec2(0.707, -0.707));\n"
        "  for (int i = 0; i < 8; ++i) {\n"
        "    vec2 p = clamp(t + taps[i] * radius, lo, hi);\n"
        "    if (distanceAt(p) >= start) { sum += texture(frame, p / size).rgb; weight += 1.0; }\n"
        "  }\n"
        "  o = weight > 0.0 ? vec4(sum / weight, 1.0) : vec4(0.0);\n"
        "}\n";
    // ... and the mix at the full resolution: each pixel's strength from its own distance, the
    // blur from halfTex (linear, inside the region; its colour divided by its alpha leaves the
    // texels without a background out), blended over the picture by the strength, less where
    // the texels around are mostly foreground
    const std::string mixFs =
        std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n") + kDistance +
        "uniform sampler2D blurred;\nuniform sampler2D focus;\nout vec4 o;\n"
        "void main() {\n"
        "  vec2 t = gl_FragCoord.xy;\n"
        "  vec4 f = texelFetch(focus, ivec2(0), 0);\n"
        "  if (f.b <= 0.0) discard;\n"
        "  float focal = round(f.r * 255.0) * 256.0 + round(f.g * 255.0);\n"
        "  float start = focal + max(800.0, focal * 0.25), end = focal + max(2500.0, focal);\n"
        "  float amount = 0.6 * f.b * smoothstep(start, end, distanceAt(t));\n"
        "  if (amount <= 0.0) discard;\n"
        "  vec2 lo = region.xy + 1.0, hi = region.xy + region.zw - 1.0;\n"
        "  vec4 b = texture(blurred, clamp(t, lo, hi) / size);\n"
        "  if (b.a <= 0.0) discard;\n"
        "  o = vec4(b.rgb / b.a, amount * min(1.0, b.a * 4.0));\n"
        "}\n";
    std::string log;
    focusProgram = Link(kQuadVs, focusFs, &log);
    blurProgram = focusProgram ? Link(kQuadVs, blurFs, &log) : 0;
    halfProgram = blurProgram ? Link(kQuadVs, halfFs, &log) : 0;
    mixProgram = halfProgram ? Link(kQuadVs, mixFs, &log) : 0;
    if (!mixProgram) {
      GXLOG("autofocus shader: %s", log.c_str());
      return false;
    }
    glUseProgram(halfProgram);
    glUniform1i(glGetUniformLocation(halfProgram, "frame"), 0);
    glUniform1i(glGetUniformLocation(halfProgram, "depths"), 1);
    glUniform1i(glGetUniformLocation(halfProgram, "focus"), 2);
    halfRegion = glGetUniformLocation(halfProgram, "region");
    halfSize = glGetUniformLocation(halfProgram, "size");
    halfZ = glGetUniformLocation(halfProgram, "zmap");
    halfRadius = glGetUniformLocation(halfProgram, "radius");
    glUseProgram(mixProgram);
    glUniform1i(glGetUniformLocation(mixProgram, "blurred"), 0);
    glUniform1i(glGetUniformLocation(mixProgram, "depths"), 1);
    glUniform1i(glGetUniformLocation(mixProgram, "focus"), 2);
    mixRegion = glGetUniformLocation(mixProgram, "region");
    mixSize = glGetUniformLocation(mixProgram, "size");
    mixZ = glGetUniformLocation(mixProgram, "zmap");
    glUseProgram(focusProgram);
    glUniform1i(glGetUniformLocation(focusProgram, "depths"), 0);
    glUniform1i(glGetUniformLocation(focusProgram, "history"), 1);
    focusRestart = glGetUniformLocation(focusProgram, "restart");
    focusRegion = glGetUniformLocation(focusProgram, "region");
    focusSize = glGetUniformLocation(focusProgram, "size");
    focusZ = glGetUniformLocation(focusProgram, "zmap");
    focusPlayer = glGetUniformLocation(focusProgram, "player");
    glUseProgram(blurProgram);
    glUniform1i(glGetUniformLocation(blurProgram, "frame"), 0);
    glUniform1i(glGetUniformLocation(blurProgram, "depths"), 1);
    glUniform1i(glGetUniformLocation(blurProgram, "focus"), 2);
    blurRegion = glGetUniformLocation(blurProgram, "region");
    blurSize = glGetUniformLocation(blurProgram, "size");
    blurZ = glGetUniformLocation(blurProgram, "zmap");
    blurRadius = glGetUniformLocation(blurProgram, "radius");
    glGenTextures(1, &blurTex);
    glBindTexture(GL_TEXTURE_2D, blurTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &blurFbo);
    BindFbo(blurFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurTex, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenTextures(2, focusTex);
    glGenFramebuffers(2, focusFbo);
    for (int i = 0; i < 2; i++) {
      glBindTexture(GL_TEXTURE_2D, focusTex[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 3, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      BindFbo(focusFbo[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, focusTex[i], 0);
      ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    // GPU step e: the half-resolution blur (linear: the mix reads between its texels) and the EFB's
    // colour without its depth
    glGenTextures(1, &halfTex);
    glBindTexture(GL_TEXTURE_2D, halfTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw / 2, fh / 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &halfFbo);
    BindFbo(halfFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, halfTex, 0);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenFramebuffers(1, &colorFbo);
    BindFbo(colorFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efbColor, 0);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok)
      GXLOG("autofocus framebuffers incomplete");
    return ok;
  }

  // clear the copied region: colour if colour/alpha update is on, depth if
  // depth update is on (GXCopyDisp/GXCopyTex set the registers for it)
  void ClearAfterCopy(std::uint32_t control, int x, int y, int w, int h) {
    if ((control >> 11) & 1) {
      const std::uint32_t c = bp[0x41];
      const bool color = (c >> 3) & 1, alpha = (c >> 4) & 1, zupdate = (bp[0x40] >> 4) & 1;
      if (color || alpha || zupdate)
        UseEfb();  // MSAA on the tiles: the EFB back first, the clear is only the copy's region
      glEnable(GL_SCISSOR_TEST);
      glScissor(Lo(x), Lo(y), Lo(x + w) - Lo(x), Lo(y + h) - Lo(y));
      glColorMask(color, color, color, alpha);
      glDepthMask(zupdate ? GL_TRUE : GL_FALSE);
      const float r = static_cast<float>(bp[0x4F] & 255) / 255.0f, a = static_cast<float>((bp[0x4F] >> 8) & 255) / 255.0f;
      const float b = static_cast<float>(bp[0x50] & 255) / 255.0f, g = static_cast<float>((bp[0x50] >> 8) & 255) / 255.0f;
      glClearColor(r, g, b, a);
      glClearDepthf(static_cast<float>(bp[0x51] & 0xFFFFFF) / 16777215.0f);
      GLbitfield mask = 0;
      if (color || alpha)
        mask |= GL_COLOR_BUFFER_BIT;
      if (zupdate)
        mask |= GL_DEPTH_BUFFER_BIT;
      if (mask)
        glClear(mask);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDepthMask(GL_TRUE);
    }
  }

  // --- programs of the renderer itself --------------------------------------------------

  bool MakeUtilityPrograms(std::string *error) {
    static const char kQuadVs[] = "#version 300 es\n"
                                  "out vec2 vUv;\n"
                                  "void main() {\n"
                                  "  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                                  "  vUv = p;\n"
                                  "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                  "}\n";
    // EFB to a copy: the source rectangle (EFB texels), optionally the 2x2 box
    // filter, then the conversion of the copy format
    static const char kCopyFs[] =
        "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        "uniform sampler2D src;\nuniform int mode;\nuniform vec4 rect;\nuniform vec2 size;\nuniform int halfScale;\n"
        "uniform float gain;\nuniform vec2 limitY;\n"
        "in vec2 vUv;\nout vec4 o;\n"
        "vec4 fetch(vec2 p) { return texture(src, p / size); }\n"
        "void main() {\n"
        "  vec2 p = rect.xy + vUv * rect.zw;\n"
        "  p.y = clamp(p.y, limitY.x + 1.0, limitY.y - 1.0);\n"
        "  vec4 c = halfScale != 0 ? 0.25 * (fetch(p + vec2(-0.5, -0.5)) + fetch(p + vec2(0.5, -0.5)) + fetch(p + "
        "vec2(-0.5, 0.5)) + fetch(p + vec2(0.5, 0.5))) : fetch(p);\n"
        "  if (mode < 12) c.rgb = min(c.rgb * gain, vec3(1.0));\n"
        "  float y = clamp(0.257 * c.r + 0.504 * c.g + 0.098 * c.b + 16.0 / 255.0, 0.0, 1.0);\n"
        "  uint z = uint(c.r * 16777215.0);\n"
        "  float zh = float((z >> 16) & 255u) / 255.0, zm = float((z >> 8) & 255u) / 255.0, zl = float(z & 255u) / "
        "255.0;\n"
        "  if (mode == 0) o = c;\n"
        "  else if (mode == 1) o = vec4(floor(c.rgb * vec3(31.0, 63.0, 31.0) + 0.5) / vec3(31.0, 63.0, 31.0), 1.0);\n"
        "  else if (mode == 2) o = c.a >= 0.999 ? vec4(floor(c.rgb * 31.0 + 0.5) / 31.0, 1.0) : "
        "vec4(floor(c * vec4(15.0, 15.0, 15.0, 7.0) + 0.5) / vec4(15.0, 15.0, 15.0, 7.0));\n"
        "  else if (mode == 3) o = vec4(y);\n"
        "  else if (mode == 4) o = vec4(y, y, y, c.a);\n"
        "  else if (mode == 5) o = vec4(c.r);\n"
        "  else if (mode == 6) o = vec4(c.g);\n"
        "  else if (mode == 7) o = vec4(c.b);\n"
        "  else if (mode == 8) o = vec4(c.a);\n"
        "  else if (mode == 9) o = vec4(c.g, c.g, c.g, c.r);\n"
        "  else if (mode == 10) o = vec4(c.b, c.b, c.b, c.g);\n"
        "  else if (mode == 11) o = vec4(c.r, c.r, c.r, c.a);\n"
        "  else if (mode == 12) o = vec4(zh);\n"
        "  else if (mode == 13) o = vec4(zm);\n"
        "  else if (mode == 14) o = vec4(zl);\n"
        "  else if (mode == 15) o = vec4(zm, zm, zm, zh);\n"
        "  else o = vec4(zh, zm, zl, 1.0);\n"
        "}\n";
    // a copy read through a palette: the copy's value (one channel: .r; two
    // channels: .a high byte, .r low byte) indexes the palette texture
    static const char kPaletteFs[] =
        "#version 300 es\nprecision highp float;\nprecision highp int;\n"
        "uniform sampler2D src;\nuniform sampler2D pal;\nuniform vec2 palSize;\nuniform int kind;\nuniform int "
        "format;\n"
        "in vec2 vUv;\nout vec4 o;\n"
        "void main() {\n"
        "  vec4 c = texture(src, vUv);\n"
        "  int index = kind == 2 ? (int(round(c.a * 255.0)) << 8 | int(round(c.r * 255.0))) & 16383 : "
        "int(round(c.r * 255.0));\n"
        "  if (format == 8) index = index >> 4;\n"
        "  int w = int(palSize.x);\n"
        "  o = texelFetch(pal, ivec2(index % w, index / w), 0);\n"
        "}\n";
    // rows: XFB texture rows shown (offset, span; 0..1), row 0 at the top
    static const char kPresentFs[] = "#version 300 es\nprecision highp float;\n"
                                     "uniform sampler2D src;\nuniform vec2 rows;\nin vec2 vUv;\nout vec4 o;\n"
                                     "void main() { o = vec4(texture(src, vec2(vUv.x, rows.x + (1.0 - vUv.y) * rows.y)).rgb, 1.0); }\n";
    std::string log;
    copyProgram = Link(kQuadVs, kCopyFs, &log);
    if (!copyProgram) {
      *error = "copy shader: " + log;
      return false;
    }
    glUseProgram(copyProgram);
    glUniform1i(glGetUniformLocation(copyProgram, "src"), 0);
    copyMode = glGetUniformLocation(copyProgram, "mode");
    copyRect = glGetUniformLocation(copyProgram, "rect");
    copySize = glGetUniformLocation(copyProgram, "size");
    copyHalf = glGetUniformLocation(copyProgram, "halfScale");
    copyGain = glGetUniformLocation(copyProgram, "gain");
    copyLimit = glGetUniformLocation(copyProgram, "limitY");
    glUniform2f(copyLimit, -1e9f, 1e9f);
    paletteProgram = Link(kQuadVs, kPaletteFs, &log);
    if (!paletteProgram) {
      *error = "palette shader: " + log;
      return false;
    }
    glUseProgram(paletteProgram);
    glUniform1i(glGetUniformLocation(paletteProgram, "src"), 0);
    glUniform1i(glGetUniformLocation(paletteProgram, "pal"), 1);
    paletteSize = glGetUniformLocation(paletteProgram, "palSize");
    paletteKind = glGetUniformLocation(paletteProgram, "kind");
    paletteFormat = glGetUniformLocation(paletteProgram, "format");
    presentProgram = Link(kQuadVs, kPresentFs, &log);
    if (!presentProgram) {
      *error = "present shader: " + log;
      return false;
    }
    glUseProgram(presentProgram);
    glUniform1i(glGetUniformLocation(presentProgram, "src"), 0);
    presentRows = glGetUniformLocation(presentProgram, "rows");
    // d2: the EFB drawn back from the other set of textures, colour and depth texel for texel
    // (the depth as sampled, no arithmetic: the 24 bits come back as they were)
    static const char kRestoreFs[] = "#version 300 es\nprecision highp float;\n"
                                     "uniform sampler2D color;\nuniform highp sampler2D depth;\nout vec4 o;\n"
                                     "void main() {\n"
                                     "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
                                     "  o = texelFetch(color, p, 0);\n"
                                     "  gl_FragDepth = texelFetch(depth, p, 0).r;\n"
                                     "}\n";
    restoreProgram = Link(kQuadVs, kRestoreFs, &log);
    if (!restoreProgram) {
      *error = "EFB restore shader: " + log;
      return false;
    }
    glUseProgram(restoreProgram);
    glUniform1i(glGetUniformLocation(restoreProgram, "color"), 0);
    glUniform1i(glGetUniformLocation(restoreProgram, "depth"), 1);
    return true;
  }
};

GxRenderer::GxRenderer() : impl_(new Impl) {}

GxRenderer::~GxRenderer() = default;

bool GxRenderer::Init(int scale, std::string *error) { return InitHalves(2 * std::max(1, scale), error); }

bool GxRenderer::InitHalves(int halves, std::string *error) {
  Impl &m = *impl_;
  std::string dummy;
  if (!error)
    error = &dummy;
  m.halves = std::max(2, halves);
  // glDrawElementsBaseVertex: GLES 3.2, or the OES / EXT extension
  // (RE4_GX_BASEVERTEX=0 keeps the per-draw attribute setup)
  {
    const char *off = std::getenv("RE4_GX_BASEVERTEX");
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    int major = 0, minor = 0;
    if (version)
      std::sscanf(version, "OpenGL ES %d.%d", &major, &minor);
    void *f = nullptr;
    if (!(off && *off == '0')) {
      if (major > 3 || (major == 3 && minor >= 2))
        f = dlsym(RTLD_DEFAULT, "glDrawElementsBaseVertex");
      if (!f && ext && std::strstr(ext, "GL_OES_draw_elements_base_vertex"))
        f = dlsym(RTLD_DEFAULT, "glDrawElementsBaseVertexOES");
      if (!f && ext && std::strstr(ext, "GL_EXT_draw_elements_base_vertex"))
        f = dlsym(RTLD_DEFAULT, "glDrawElementsBaseVertexEXT");
    }
    m.drawBaseVertex = reinterpret_cast<Impl::DrawBaseVertex>(f);
  }
  // once per process: the GPU and what step d (MSAA on the tiles) needs of it
  static bool described = false;
  if (!described) {
    described = true;
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    auto has = [ext](const char *name) { return HasExtension(ext, name) ? "yes" : "no"; };
    GXLOG("GPU: %s, %s; EXT_multisampled_render_to_texture %s, ..._texture2 %s, "
          "EXT_shader_framebuffer_fetch %s, QCOM_tiled_rendering %s",
          renderer ? renderer : "?", version ? version : "?", has("GL_EXT_multisampled_render_to_texture"),
          has("GL_EXT_multisampled_render_to_texture2"), has("GL_EXT_shader_framebuffer_fetch"),
          has("GL_QCOM_tiled_rendering"));
    // all of them, in pieces a log line holds
    const std::string all = ext ? ext : "";
    const std::size_t piece = 800, pieces = (all.size() + piece - 1) / piece;
    for (std::size_t i = 0; i < pieces; i++)
      GXLOG("GPU extensions (%zu/%zu): %s", i + 1, pieces, all.substr(i * piece, piece).c_str());
  }
  const int w = m.Lo(kEfbWidth), h = m.Lo(kEfbHeight);
  Impl::MakeEfbTextures(w, h, &m.efbColor, &m.efbDepth);
  glGenFramebuffers(1, &m.efbFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, m.efbFbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m.efbColor, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m.efbDepth, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    *error = "EFB framebuffer incomplete";
    return false;
  }
  // the menu's options, as far as the device goes
  {
    int maxSamples = 1;
    float maxAnisotropy = 1.0f;
    Limits(&maxSamples, &maxAnisotropy);
    m.anisotropy = std::max(1.0f, std::min(m.options.anisotropy, maxAnisotropy));
    const int want = std::min(m.options.samples, maxSamples);
    // RE4_GX_DOF_HALF=0 / 1: the depth of field's blur at the full or half resolution (experiments)
    const char *halfDof = std::getenv("RE4_GX_DOF_HALF");
    if (halfDof && (*halfDof == '0' || *halfDof == '1'))
      m.options.halfDof = *halfDof == '1';
    // RE4_GX_MSAA_TILES=1 / 0: on the tiles or not, whatever the options say (experiments)
    const char *tiles = std::getenv("RE4_GX_MSAA_TILES");
    const bool tiled = tiles && (*tiles == '0' || *tiles == '1') ? *tiles == '1' : m.options.tiledMsaa;
    m.wantSamples = want;
    if (want > 1 && tiled)
      m.TiledMsaa(want);
    if (want > 1 && !m.tiledMsaa)
      m.OwnMsaa(want);
  }
  glViewport(0, 0, w, h);
  // the EFB as every display copy of the game leaves it: cleared to the copy
  // clear colour, black with alpha 0 (bio4_GXSetCopyClear). The shadow maps
  // (shadow.cpp) draw only the casters' alpha and rely on that 0 around them;
  // alpha 255 here made a single dumped frame shade the whole light cone.
  glClearColor(0, 0, 0, 0);
  glClearDepthf(1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (m.spareFbo) {
    // d2: the other set the same, the first pass draws the EFB back from one of them
    glBindFramebuffer(GL_FRAMEBUFFER, m.spareFbo);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, m.efbFbo);
  }
  glGenFramebuffers(1, &m.xfbFbo);
  glGenVertexArrays(1, &m.vao);
  glGenVertexArrays(1, &m.quadVao);
  glGenBuffers(1, &m.vbo);
  glGenBuffers(1, &m.ibo);
  glBindVertexArray(m.vao);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
  glBindVertexArray(0);
  glGenBuffers(kXfParts, m.xfUbo);
  for (int i = 0; i < kXfParts; i++) {
    glBindBuffer(GL_UNIFORM_BUFFER, m.xfUbo[i]);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(kXfPartRow[i + 1] - kXfPartRow[i]) * 16, nullptr,
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kXfPartBinding[i], m.xfUbo[i]);
  }
  glGenBuffers(1, &m.pxUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, m.pxUbo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(PixelBlock), nullptr, GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_UNIFORM_BUFFER, kPixelBinding, m.pxUbo);
  const std::uint8_t white[4] = {255, 255, 255, 255};
  glGenTextures(1, &m.whiteTex);
  glBindTexture(GL_TEXTURE_2D, m.whiteTex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
  for (auto &b : m.bound)
    b.id = m.whiteTex;
  m.StreamInit();
  m.TimerInit();
  if (!m.MakeUtilityPrograms(error))
    return false;
  // d2: does this device keep the EFB over a pass's end, drawn back? Else own buffers
  if (m.tiledMsaa && !m.TilesSelfTest()) {
    m.DropTiles();
    m.OwnMsaa(m.wantSamples);
  }
  // both sets (or the plain EFB) as the game's display copies leave them
  for (GLuint fbo : {m.efbFbo, m.spareFbo}) {
    if (!fbo)
      continue;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, m.DrawFbo());
  m.efbLeft = m.restorePending = false;
  return true;
}

void GxRenderer::HandOverCopies(GxRenderer &to) {
  Impl &m = *impl_, &n = *to.impl_;
  if (&m == &n || m.copies.empty())
    return;
  m.Touched();
  n.Touched();
  GLuint from = 0, dst = 0;
  glGenFramebuffers(1, &from);
  glGenFramebuffers(1, &dst);
  unsigned moved = 0;
  for (auto &c : m.copies) {
    Copy copy = c.second;
    if (!copy.id || n.copies.count(c.first))
      continue;
    if (m.halves != n.halves) {
      // at the new scale: a texture of the new size, the old one drawn over it scaled
      GLuint made = 0;
      glGenTextures(1, &made);
      glBindTexture(GL_TEXTURE_2D, made);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, n.Hi(static_cast<int>(copy.width)),
                   n.Hi(static_cast<int>(copy.height)), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy.id, 0);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst);
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, made, 0);
      glBlitFramebuffer(0, 0, m.Hi(static_cast<int>(copy.width)), m.Hi(static_cast<int>(copy.height)), 0, 0,
                        n.Hi(static_cast<int>(copy.width)), n.Hi(static_cast<int>(copy.height)), GL_COLOR_BUFFER_BIT,
                        GL_LINEAR);
      glDeleteTextures(1, &copy.id);
      copy.id = made;
    }
    copy.serial = ++n.copySerial;
    n.copies[c.first] = copy;
    c.second.id = 0;  // the new renderer's now: this one's Release leaves it
    moved++;
  }
  m.copies.clear();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &from);
  glDeleteFramebuffers(1, &dst);
  n.stats.handedCopies += moved;
}

void GxRenderer::Rescale(int scale) { RescaleHalves(2 * std::max(1, scale)); }

void GxRenderer::RescaleHalves(int halves) {
  Impl &m = *impl_;
  halves = std::max(2, halves);
  if (halves == m.halves)
    return;
  const int old = m.halves;
  m.stats.rescales++;
  // the MSAA buffers go (as Init found them: on the tiles or in own buffers)
  const bool wasTiled = m.tiledMsaa;
  if (m.tiledMsaa)
    m.DropTiles();
  if (m.msaaFbo) {
    glDeleteFramebuffers(1, &m.msaaFbo);
    const GLuint renderbuffers[] = {m.msaaColor, m.msaaDepth};
    glDeleteRenderbuffers(2, renderbuffers);
    m.msaaFbo = m.msaaColor = m.msaaDepth = 0;
    m.samples = 1;
  }
  // the EFB's textures at the new size (the same names: the framebuffers that hold them stay)
  m.halves = halves;
  const int w = m.Lo(kEfbWidth), h = m.Lo(kEfbHeight);
  glBindTexture(GL_TEXTURE_2D, m.efbColor);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, m.efbDepth);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
  if (m.wantSamples > 1 && wasTiled)
    m.TiledMsaa(m.wantSamples);
  if (m.wantSamples > 1 && !m.tiledMsaa)
    m.OwnMsaa(m.wantSamples);
  for (GLuint fbo : {m.efbFbo, m.spareFbo, m.msaaFbo}) {
    if (!fbo)
      continue;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 0);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  // the autofocus's blur at the new size (its focus stays)
  if (m.blurTex) {
    glBindTexture(GL_TEXTURE_2D, m.blurTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }
  if (m.halfTex) {
    glBindTexture(GL_TEXTURE_2D, m.halfTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w / 2, h / 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }
  // the display copy and the EFB copies: new textures, the old ones drawn over scaled (an effect
  // that reads the picture before, the picture the VI holds)
  GLuint from = 0, to = 0;
  glGenFramebuffers(1, &from);
  glGenFramebuffers(1, &to);
  auto rescaled = [&](GLuint id, unsigned cw, unsigned ch, bool linear) {
    GLuint made = 0;
    glGenTextures(1, &made);
    glBindTexture(GL_TEXTURE_2D, made);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m.Hi(static_cast<int>(cw)), m.Hi(static_cast<int>(ch)), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    if (linear) {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, id, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, made, 0);
    glBlitFramebuffer(0, 0, (static_cast<GLint>(cw) * old + 1) / 2, (static_cast<GLint>(ch) * old + 1) / 2, 0, 0,
                      m.Hi(static_cast<int>(cw)), m.Hi(static_cast<int>(ch)), GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glDeleteTextures(1, &id);
    return made;
  };
  if (m.xfbTex)
    m.xfbTex = rescaled(m.xfbTex, m.xfbWidth, m.xfbHeight, true);
  if (m.fxaaTex) { // the smoothed copy is of the old size: the next display copy makes it anew
    glDeleteTextures(1, &m.fxaaTex);
    m.fxaaTex = 0;
    m.fxaaTexW = m.fxaaTexH = 0;
    m.fxaaShown = false;
  }
  for (auto &c : m.copies) {
    if (!c.second.id)
      continue;
    const GLuint was = c.second.id;
    c.second.id = rescaled(was, c.second.width, c.second.height, false);
    // a map still bound to the copy (the game loads a texture once and draws on with it)
    for (auto &b : m.bound)
      if (b.id == was)
        b.id = c.second.id;
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &from);
  glDeleteFramebuffers(1, &to);
  // (the copies read through a palette have the copy's size in EFB pixels, at every scale)
  for (auto &c : m.interpCopies)
    glDeleteTextures(1, &c.second.id);
  m.interpCopies.clear();
  m.stateValid = false;
  m.efbLeft = m.restorePending = false;
  glBindFramebuffer(GL_FRAMEBUFFER, m.DrawFbo());
  m.Touched();
}

void GxRenderer::Run(const Re4GxChunk &chunk) {
  Impl &m = *impl_;
  if (chunk.version != RE4_GX_FRAME_VERSION)
    return;
  if (!m.interpPass)
    m.stats.chunks++;
  m.mergeValid = false;
  m.splitAt = std::chrono::steady_clock::now();
  // a real Run notes its draws for the in-between picture after it
  const bool record = m.recordDraws && !m.interpPass;
  if (record) {
    m.recNew.clear();
    m.recFloatsNew.clear();
    m.recOwnerDrawsNew.clear();
  }
  m.Touched();
  if (!m.interpPass && m.peekProgram)
    m.DeliverDepth();  // the depth maps the GPU finished since the last chunk go to the core
  if (m.stream)
    m.StreamChunkBegin();
  m.holdXfb = (chunk.flags & RE4_GX_CHUNK_HOLD) != 0;
  if (chunk.flags & RE4_GX_CHUNK_XFB)
    m.viBlack = (chunk.flags & RE4_GX_CHUNK_BLACK) != 0;
  if ((chunk.flags & RE4_GX_CHUNK_RESET) && !m.interpPass) {
    m.stats.resets++;
    m.DropTextures();
  }
  // this chunk's vertices and indices
  glBindVertexArray(m.vao);
  const bool uploaded = !m.interpPass && m.uploadedBy == &chunk && m.uploadedSequence == chunk.sequence;
  m.uploadedBy = nullptr;
  if (!m.skipDraws && !uploaded) {
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, chunk.vertexBytes ? chunk.vertexBytes : 4, chunk.vertices, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunk.indexCount ? chunk.indexCount * 2 : 4, chunk.indices, GL_STREAM_DRAW);
    if (m.interpPass) {
      m.uploadedBy = &chunk;
      m.uploadedSequence = chunk.sequence;
    }
  }
  m.Split(GxStats::kSplitVertices);
  // the app drew the screen since the last chunk: a new pass
  m.efbLeft = true;
  m.BindFbo(m.DrawFbo());
  // state other GL code of the app may have left
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  glDisable(GL_DITHER);
  glFrontFace(GL_CCW);
  if (m.stream) {
    // both blocks again into the stream at the first draw: the places bound
    // last may since have been written over
    m.Dirty(0, static_cast<int>(sizeof(XfBlock) / 16));
    for (bool &v : m.xfSentValid)
      v = false;
    m.pxDirty = true;
    m.pxValid = false;
  } else {
    for (int i = 0; i < kXfParts; i++)
      glBindBufferBase(GL_UNIFORM_BUFFER, kXfPartBinding[i], m.xfUbo[i]);
    glBindBufferBase(GL_UNIFORM_BUFFER, kPixelBinding, m.pxUbo);
  }
  m.TimerBegin();
  m.current = nullptr;
  m.ownerBase = 0;
  m.ownerLife = ~0u;
  m.Split(GxStats::kSplitState);
  std::uint32_t at = 0;
  while (at < chunk.opWords) {
    const std::uint32_t head = chunk.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    const std::uint32_t *p = chunk.ops + at + 1;
    if (at + 1 + n > chunk.opWords)
      break;
    switch (op) {
    case RE4_GX_OP_BP:
      if (n >= 1)
        m.BpWrite(p[0], true);
      break;
    case RE4_GX_OP_BP_STATE:
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        m.BpWrite(r << 24 | (p[r] & 0xFFFFFF), false);
      break;
    case RE4_GX_OP_XF:
      if (n >= 1)
        m.XfWrite(p[0], p + 1, n - 1);
      break;
    case RE4_GX_OP_TLUT:
      if (n >= 3 && p[0] < kTlutBytes && p[2] + p[1] <= chunk.blobBytes)
        std::memcpy(m.tlut.data() + p[0], chunk.blob + p[2], std::min<std::uint32_t>(p[1], kTlutBytes - p[0]));
      break;
    case RE4_GX_OP_TEXTURE:
      if (n * 4 >= sizeof(Re4GxTexture)) {
        Re4GxTexture t;
        std::memcpy(&t, p, sizeof(t));
        m.BindTexture(t, chunk);
      }
      break;
    case RE4_GX_OP_OWNER:
      if (n >= 4)
        m.SetOwner(p);
      break;
    case RE4_GX_OP_FORGET:
      // the real Run forgets (the in-between pass runs before it). A texture the core sends again in
      // this chunk (it forgot it for its budget) the in-between pass uploaded already: after the forget
      // it is uploaded again, else it stays gone and draws white (itel P661N, 28.10.2026: Ganados with
      // white weapons after some 20 minutes with 60 FPS)
      if (n >= 2 && !m.interpPass) {
        const std::uint64_t key = static_cast<std::uint64_t>(p[1]) << 32 | p[0];
        m.Forget(key);
        m.interpUploads.erase(key);
      }
      break;
    case RE4_GX_OP_DRAW:
      if (!m.skipDraws && n * 4 >= sizeof(Re4GxDraw)) {
        Re4GxDraw d;
        std::memcpy(&d, p, sizeof(d));
        m.Draw(d, chunk);
      }
      break;
    default:
      break;
    }
    m.Split(op == RE4_GX_OP_BP || op == RE4_GX_OP_BP_STATE ? GxStats::kSplitBp
            : op == RE4_GX_OP_XF                          ? GxStats::kSplitXf
            : op == RE4_GX_OP_TEXTURE                     ? GxStats::kSplitTexture
                                                          : GxStats::kSplitRest);
    at += 1 + n;
  }
  m.TimerEnd();
  m.FenceAfterChunk();
  if (record) {
    m.rec.swap(m.recNew);
    m.recOwnerDraws.swap(m.recOwnerDrawsNew);
    m.recFloats.swap(m.recFloatsNew);
    m.recSequence = chunk.sequence;
    m.recValid = true;
  } else if (!m.interpPass) {
    m.recValid = false;
  }
  if (!m.interpPass) {
    // the in-between pass's uploads served this Run
    m.skipInterpUploads = false;
    m.interpUploads.clear();
  }
  // leave GL as the app's other renderers expect it
  glBindVertexArray(0);
  for (GLuint unit = 0; unit < 8; unit++)
    glBindSampler(unit, 0);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(0);
  m.Touched();
  m.SaveAndPlainState();
  m.BindFbo(0);
  m.Split(GxStats::kSplitRest);
}

bool GxRenderer::CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const {
  const Impl &m = *impl_;
  const std::uint32_t refuse = RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_HOLD | RE4_GX_CHUNK_BLACK | RE4_GX_CHUNK_MARK;
  if (m.interpPass || a.version != RE4_GX_FRAME_VERSION || b.version != RE4_GX_FRAME_VERSION ||
      (a.flags & refuse) || !(a.flags & RE4_GX_CHUNK_XFB) || m.viBlack)
    return false;
  // the EFB copies for textures in `a`: their targets (the BP state at that moment decides them); and
  // which copy each texture map holds (0: none), from the chunk run last on
  std::vector<std::uint32_t> bp(m.bp, m.bp + 256), pending;
  std::uint32_t mapCopy[8];
  for (int i = 0; i < 8; i++)
    mapCopy[i] = (m.boundKey[i] >> 63) ? static_cast<std::uint32_t>(m.boundKey[i]) : 0;
  auto texture = [&](const std::uint32_t *p, std::uint32_t n, Re4GxTexture *t) {
    if (n * 4 < sizeof(Re4GxTexture))
      return false;
    std::memcpy(t, p, sizeof(*t));
    mapCopy[t->map & 7] = (t->flags & RE4_GX_TEX_COPY) ? t->address : 0;
    return true;
  };
  for (std::uint32_t at = 0; at < a.opWords;) {
    const std::uint32_t head = a.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    if (at + 1 + n > a.opWords)
      return false;
    const std::uint32_t *p = a.ops + at + 1;
    Re4GxTexture t;
    if (op == RE4_GX_OP_BP && n >= 1) {
      const unsigned reg = p[0] >> 24;
      bp[reg] = p[0] & 0xFFFFFF;
      if (reg == 0x52 && !((bp[0x52] >> 14) & 1))
        pending.push_back((bp[0x4B] & 0x1FFFFF) << 5);
    } else if (op == RE4_GX_OP_BP_STATE) {
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        bp[r] = p[r] & 0xFFFFFF;
    } else if (op == RE4_GX_OP_TEXTURE) {
      texture(p, n, &t);
    }
    at += 1 + n;
  }
  if (pending.empty())
    return true;
  auto isPending = [&](std::uint32_t address) {
    return address && std::find(pending.begin(), pending.end(), address) != pending.end();
  };
  // `b` must copy to each of them again (else a later chunk reads a copy `a` never made), and must not
  // read one before: a texture op for it, or a draw whose TEV stages sample a map still holding it
  // (until 26.10.2026 any draw counted: almost every picture of a room makes a copy, shadows and the
  // depth of field, and draws before it copies, so Frame skip almost never skipped)
  for (std::uint32_t at = 0; at < b.opWords && !pending.empty();) {
    const std::uint32_t head = b.ops[at];
    const std::uint32_t op = head & 0xFF, n = head >> 8;
    if (at + 1 + n > b.opWords)
      return false;
    const std::uint32_t *p = b.ops + at + 1;
    Re4GxTexture t;
    if (op == RE4_GX_OP_DRAW) {
      const std::uint32_t gen = bp[0x00];
      const unsigned stages = ((gen >> 10) & 15) + 1, indStages = (gen >> 16) & 7;
      for (unsigned st = 0; st < stages; st++) {
        const std::uint32_t o = bp[0x28 + st / 2] >> ((st & 1) ? 12 : 0);
        if (((o >> 6) & 1) && isPending(mapCopy[o & 7]))
          return false;
      }
      for (unsigned st = 0; st < indStages && st < 4; st++)
        if (isPending(mapCopy[(bp[0x27] >> (6 * st)) & 7]))
          return false;
    } else if (op == RE4_GX_OP_TEXTURE) {
      if (!texture(p, n, &t))
        return false;
      if ((t.flags & RE4_GX_TEX_COPY) && isPending(t.address))
        return false;
    } else if (op == RE4_GX_OP_BP && n >= 1) {
      const unsigned reg = p[0] >> 24;
      bp[reg] = p[0] & 0xFFFFFF;
      if (reg == 0x52 && !((bp[0x52] >> 14) & 1)) {
        const std::uint32_t dest = (bp[0x4B] & 0x1FFFFF) << 5;
        pending.erase(std::remove(pending.begin(), pending.end(), dest), pending.end());
      }
    } else if (op == RE4_GX_OP_BP_STATE) {
      for (std::uint32_t r = 0; r < n && r < 256; r++)
        bp[r] = p[r] & 0xFFFFFF;
    }
    at += 1 + n;
  }
  return pending.empty();
}

void GxRenderer::Skip(const Re4GxChunk &a) {
  impl_->skipDraws = true;
  Run(a);
  impl_->skipDraws = false;
  impl_->recValid = false;  // 60 FPS: no in-between picture across a chunk left out
}

void GxRenderer::SetRecording(bool on) {
  Impl &m = *impl_;
  m.recordDraws = on;
  if (!on) {
    m.recValid = false;
    m.rec.clear();
    m.recFloats.clear();
  }
}

bool GxRenderer::Interpolate(const Re4GxChunk &prev, const Re4GxChunk &next, float t) {
  Impl &m = *impl_;
  // (STILL: a menu screen of the game, its scrolling text lists; also when prev was one: the cut out of it)
  const std::uint32_t refuse =
      RE4_GX_CHUNK_RESET | RE4_GX_CHUNK_HOLD | RE4_GX_CHUNK_BLACK | RE4_GX_CHUNK_MARK | RE4_GX_CHUNK_STILL;
  if (prev.flags & RE4_GX_CHUNK_STILL)
    return false;
  if (!m.recordDraws || !m.recValid || m.recSequence != prev.sequence || next.sequence != prev.sequence + 1 ||
      (next.flags & refuse) || !(next.flags & RE4_GX_CHUNK_XFB) || next.version != RE4_GX_FRAME_VERSION ||
      m.viBlack || m.rec.empty())
    return false;
  // nothing in perspective noted (title, movies, 2D menus): nothing to place between
  bool any3d = false;
  for (const Impl::DrawRec &r : m.rec)
    any3d = any3d || r.perspective;
  if (!any3d)
    return false;
  // what the pass changes and Run(next) expects as after prev
  std::vector<std::uint32_t> bp(m.bp, m.bp + 256), xfMem(m.xfMem, m.xfMem + 0x800), xfReg(m.xfReg, m.xfReg + 0x58);
  std::int32_t tevColor[4][4], tevKonst[4][4];
  std::memcpy(tevColor, m.tevColor, sizeof(tevColor));
  std::memcpy(tevKonst, m.tevKonst, sizeof(tevKonst));
  Impl::Bound bound[8];
  std::uint64_t boundKey[8];
  for (int i = 0; i < 8; i++) {
    bound[i] = m.bound[i];
    boundKey[i] = m.boundKey[i];
  }
  const float perspA = m.perspA, perspB = m.perspB;
  const bool havePerspective = m.havePerspective, holdXfb = m.holdXfb, viBlack = m.viBlack;
  const int focusCur = m.focusCur;
  const unsigned focusChunk = m.focusChunk;
  // palette loads of next (TMEM): only then the palettes
  bool tlutLoads = false;
  for (std::uint32_t at = 0; at < next.opWords;) {
    const std::uint32_t head = next.ops[at];
    tlutLoads = tlutLoads || (head & 0xFF) == RE4_GX_OP_TLUT;
    at += 1 + (head >> 8);
  }
  std::vector<std::uint8_t> tlut;
  if (tlutLoads)
    tlut = m.tlut;
  // the noted draws by signature, in their order
  m.recBySig.clear();
  m.recByLook.clear();
  m.recByOwner.clear();
  m.nextOwnerDraws.clear();
  {
    std::uint64_t owner = 0;
    for (std::uint32_t at = 0; at < next.opWords;) {
      const std::uint32_t head = next.ops[at], op = head & 0xFF, n = head >> 8;
      if (at + 1 + n > next.opWords)
        break;
      if (op == RE4_GX_OP_OWNER && n >= 4)
        owner = Impl::OwnerKey(next.ops + at + 1);
      else if (op == RE4_GX_OP_DRAW && owner)
        m.nextOwnerDraws[owner]++;
      at += 1 + n;
    }
  }
  for (std::uint32_t i = 0; i < m.rec.size(); i++) {
    if (m.rec[i].owner)
      m.recByOwner.emplace(m.rec[i].owner, i);
    m.recBySig[m.rec[i].sig].push_back(i);
    if (m.rec[i].look)
      m.recByLook[m.rec[i].look].push_back(i);
  }
  m.recUsed.assign(m.rec.size(), 0);
  m.recReserved.assign(m.rec.size(), 0);
  for (std::uint32_t i = 0; i < m.rec.size(); i++) {
    if (!m.rec[i].base)
      continue;
    auto pc = m.recOwnerDraws.find(m.rec[i].base), nc = m.nextOwnerDraws.find(m.rec[i].base);
    m.recReserved[i] = pc != m.recOwnerDraws.end() && nc != m.nextOwnerDraws.end() && pc->second == nc->second;
  }
  m.ipDraws = m.ipMatched = m.ipTurned = 0;
  m.camValid = false;
  m.camGuesses = m.camVotes = 0;
  m.interpCopied.clear();
  m.interpUploads.clear();
  // the store for the pass's blended vertices: the next quarter of the mapped ring once the GPU is
  // through with it, else a fresh buffer (the last pass's may still be read by the GPU)
  if (!m.blendRingTried)
    m.BlendRingInit();
  if (m.blendMap) {
    m.blendPart = (m.blendPart + 1) % Impl::kBlendParts;
    if (GLsync fence = m.blendFence[m.blendPart]) {
      if (glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0) == GL_TIMEOUT_EXPIRED)
        glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
      glDeleteSync(fence);
      m.blendFence[m.blendPart] = nullptr;
    }
    m.blendUsed = m.blendPart * Impl::kBlendPart;
    m.blendCap = m.blendUsed + Impl::kBlendPart;
  } else {
    if (!m.blendVbo)
      glGenBuffers(1, &m.blendVbo);
    m.blendCap = std::max<std::size_t>(next.vertexBytes, 65536);
    glBindBuffer(GL_ARRAY_BUFFER, m.blendVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m.blendCap), nullptr, GL_STREAM_DRAW);
    m.blendUsed = 0;
  }
  m.blendAt = -1;
  m.interpPass = true;
  m.interpT = t;
  m.interpPrev = &prev;
  Run(next);
  m.blendAt = -1;
  if (m.blendMap)
    m.blendFence[m.blendPart] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  m.interpPass = false;
  m.interpPrev = nullptr;
  m.skipInterpUploads = true;
  // back to the state after prev
  std::memcpy(m.bp, bp.data(), sizeof(m.bp));
  std::memcpy(m.xfMem, xfMem.data(), sizeof(m.xfMem));
  std::memcpy(m.xfReg, xfReg.data(), sizeof(m.xfReg));
  std::memcpy(m.tevColor, tevColor, sizeof(tevColor));
  std::memcpy(m.tevKonst, tevKonst, sizeof(tevKonst));
  for (int i = 0; i < 8; i++) {
    m.bound[i] = bound[i];
    m.boundKey[i] = boundKey[i];
  }
  if (tlutLoads)
    m.tlut.swap(tlut);
  m.perspA = perspA;
  m.perspB = perspB;
  m.havePerspective = havePerspective;
  m.holdXfb = holdXfb;
  m.viBlack = viBlack;
  m.focusCur = focusCur;
  m.focusChunk = focusChunk;
  for (std::uint32_t a = 0; a < 0x800; a++)
    m.XfMemToBlock(a);
  m.FillXfRegs();
  m.Dirty(0, static_cast<int>(sizeof(XfBlock) / 16));
  m.keyDirty = true;
  m.keyLayout = ~0u;
  m.pxDirty = true;
  m.pxValid = false;
  m.Touched();
  // a cut: few draws found again, or most of them turned (the camera jumped)
  m.stats.interpDraws += m.ipDraws;
  m.stats.interpMatched += m.ipMatched;
  const bool cut = m.ipDraws == 0 || m.ipMatched * 2 < m.ipDraws || m.ipTurned * 10 > m.ipMatched * 3;
  if (cut) {
    m.stats.interpCuts++;
    return false;
  }
  m.stats.interpPictures++;
  return true;
}

GLuint GxRenderer::Xfb(unsigned *width, unsigned *height) const {
  if (width)
    *width = impl_->xfbWidth;
  if (height)
    *height = impl_->xfbHeight;
  return impl_->haveXfb ? impl_->ShownXfb() : 0;
}

bool GxRenderer::Present(int x, int y, int w, int h, unsigned top, unsigned rows) {
  Impl &m = *impl_;
  if (!m.haveXfb)
    return false;
  m.stats.presents++;
  m.PresentTimerBegin();
  m.SaveAndPlainState();
  if (m.viBlack) {
    // VISetBlack: the GameCube's video output is black, whatever the XFB holds
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    m.Touched();
    m.PresentTimerEnd();
    return true;
  }
  glViewport(x, y, w, h);
  glUseProgram(m.presentProgram);
  const float lines = static_cast<float>(m.xfbHeight ? m.xfbHeight : 1);
  if (rows == 0 || top + rows > m.xfbHeight)
    glUniform2f(m.presentRows, 0.0f, 1.0f);
  else
    glUniform2f(m.presentRows, static_cast<float>(top) / lines, static_cast<float>(rows) / lines);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m.ShownXfb());
  glBindSampler(0, 0);
  glBindVertexArray(m.quadVao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  m.Touched();
  m.PresentTimerEnd();
  return true;
}

GLuint GxRenderer::EfbColor() const { return impl_->efbColor; }

void GxRenderer::SetOptions(const GxOptions &options) { impl_->options = options; }
void GxRenderer::SetDepthSink(DepthSink sink) { impl_->depthSink = std::move(sink); }
void GxRenderer::SetFxaa(bool on) { impl_->fxaaOn = on; }
void GxRenderer::SetWiden(float factor) {
  impl_->widen2d.store(factor > 0.5f && factor < 2.0f ? factor : 1.0f, std::memory_order_relaxed);
}
void GxRenderer::SetShownRows(int top, int bottom) {
  impl_->shownTop.store(std::max(0, std::min(top, kEfbHeight)), std::memory_order_relaxed);
  impl_->shownBottom.store(std::max(0, std::min(bottom, kEfbHeight)), std::memory_order_relaxed);
}

int GxRenderer::Samples() const { return impl_->samples; }

float GxRenderer::Anisotropy() const { return impl_->anisotropy; }

bool GxRenderer::Invalidates() const { return impl_->options.invalidate; }

bool GxRenderer::TiledMsaa() const { return impl_->tiledMsaa; }

bool GxRenderer::HalfDof() const { return impl_->options.halfDof; }

void GxRenderer::Limits(int *maxSamples, float *maxAnisotropy) {
  GLint samples = 1;
  glGetIntegerv(GL_MAX_SAMPLES, &samples);
  *maxSamples = std::max(1, static_cast<int>(samples));
  *maxAnisotropy = 1.0f;
  const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
  if (ext && std::strstr(ext, "GL_EXT_texture_filter_anisotropic")) {
    GLfloat most = 1.0f;
    glGetFloatv(kMaxAnisotropyLimit, &most);
    *maxAnisotropy = std::max(1.0f, static_cast<float>(most));
  }
}

int GxRenderer::Scale() const { return impl_->halves / 2; }
int GxRenderer::Halves() const { return impl_->halves; }

const GxStats &GxRenderer::Stats() const { return impl_->stats; }

bool GxRenderer::BaseVertex() const { return impl_->drawBaseVertex != nullptr; }

bool GxRenderer::SetProgramCache(const std::string &folder) {
  Impl &m = *impl_;
  m.programCache.clear();
  GLint formats = 0;
  glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
  if (folder.empty() || formats <= 0)
    return false;
  const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
  const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
  m.driverTag = std::string(renderer ? renderer : "") + "|" + (version ? version : "");
  m.programCache = folder;
  return true;
}

bool GxRenderer::Prewarm(double budgetMs) {
  Impl &m = *impl_;
  if (m.programCache.empty())
    return false;
  if (!m.aheadRead) {
    m.ReadAhead();
    if (!m.ahead.empty())
      GXLOG("GX programs ahead of their first draw: %zu keys (%u of them from the app's list)", m.ahead.size(),
            m.aheadListed);
  }
  if (m.aheadNext >= m.ahead.size())
    return false;
  const auto start = std::chrono::steady_clock::now();
  const std::uint32_t centroid = m.options.centroid && m.samples > 1 ? 1 : 0;
  double ms = 0.0;
  bool made = false;
  while (m.aheadNext < m.ahead.size() && !(made && ms >= budgetMs)) {
    Impl::AheadKey &a = m.ahead[m.aheadNext];
    if (a.listed)
      a.key.centroid = centroid;  // the list holds each program once, for every MSAA setting
    // another MSAA setting's program, or drawn already
    if (a.key.centroid != centroid || m.programs.count(a.key)) {
      m.aheadNext++;
      continue;
    }
    // only from the cache: WarmProgramCache links on a thread of its own; while it runs, its binary
    // may come yet (this key again later), else the program is made at its first draw
    std::uint64_t check = 0;
    if (access((m.CacheStem(GxVertexShader(a.key), GxFragmentShader(a.key), &check) + ".bin").c_str(), F_OK) != 0) {
      if (ProgramCacheWarming())
        break;
      m.aheadNext++;
      continue;
    }
    m.aheadNext++;
    const unsigned loaded = m.stats.programsLoaded;
    m.ProgramFor(a.key);
    made = true;
    m.stats.programsAhead++;
    if (m.stats.programsLoaded != loaded)
      m.aheadLoaded++;
    else
      m.aheadLinked++;
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  m.aheadMs += ms;
  if (m.aheadNext >= m.ahead.size())
    GXLOG("GX programs ahead of their first draw: %u from the cache, %u linked anew, %.0f ms in all", m.aheadLoaded,
          m.aheadLinked, m.aheadMs);
  return m.aheadNext < m.ahead.size();
}

void GxRenderer::SetProgramList(const std::string &path) { impl_->programList = path; }

bool ProgramCacheWarming() { return gWarming.load() > 0; }

unsigned WriteProgramKeyList(const std::string &cache, const std::string &path) {
  std::vector<ShaderKey> keys;
  std::unordered_set<ShaderKey, KeyHash, KeyEqual> seen;
  for (const auto &k : CacheKeys(cache))
    if (seen.insert(k.first).second)
      keys.push_back(k.first);
  return WriteKeyList(path, keys) ? static_cast<unsigned>(keys.size()) : 0;
}

GxWarmResult WarmProgramCache(const std::string &cache, const std::string &list, std::uint32_t centroid,
                              const std::atomic<bool> *stop) {
  GxWarmResult r;
  GLint formats = 0;
  glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
  if (cache.empty() || formats <= 0)
    return r;
  gWarming++;
  const auto start = std::chrono::steady_clock::now();
  const std::string tag = DriverTag();
  // the device's own keys (each with the stem it was kept under) and the app's list
  std::vector<std::pair<ShaderKey, std::string>> work = CacheKeys(cache);
  std::vector<ShaderKey> listed;
  if (!list.empty())
    ReadKeyList(list, &listed);
  for (const ShaderKey &k : listed)
    work.emplace_back(k, std::string());
  std::unordered_set<ShaderKey, KeyHash, KeyEqual> done;
  for (auto &w : work) {
    if (stop && stop->load())
      break;
    ShaderKey &k = w.first;
    const bool fromList = w.second.empty();
    if (fromList)
      k.centroid = centroid;
    if (k.centroid != centroid)
      continue;  // another MSAA setting's program: left as it is
    const std::string vs = GxVertexShader(k), fs = GxFragmentShader(k);
    std::uint64_t check = 0;
    const std::string stem = ProgramStem(cache, tag, vs, fs, &check);
    if (done.insert(k).second) {
      r.keys++;
      r.listed += fromList ? 1 : 0;
      if (access((stem + ".bin").c_str(), F_OK) == 0) {
        r.had++;
      } else {
        std::string log;
        const GLuint p = Link(vs, fs, &log, true);
        if (p) {
          StoreProgram(stem + ".bin", check, p);
          glDeleteProgram(p);
          r.linked++;
        } else {
          r.failed++;
        }
      }
      if (access((stem + ".bin").c_str(), F_OK) == 0)
        StoreKey(stem + ".key", k);
    }
    // kept under another name (the sources or the driver changed since): the old files go
    if (!fromList && w.second != stem) {
      std::remove((w.second + ".bin").c_str());
      std::remove((w.second + ".key").c_str());
      r.stale++;
    }
  }
  r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  gWarming--;
  return r;
}

void GxRenderer::Release() {
  Impl &m = *impl_;
  m.DropTextures();
  for (auto &c : m.copies)
    glDeleteTextures(1, &c.second.id);
  m.copies.clear();
  for (auto &p : m.programs)
    if (p.second.id)
      glDeleteProgram(p.second.id);
  m.programs.clear();
  for (auto &s : m.samplers)
    glDeleteSamplers(1, &s.second);
  m.samplers.clear();
  glDeleteProgram(m.restoreProgram);
  const GLuint spareTextures[] = {m.spareColor, m.spareDepth};
  glDeleteTextures(2, spareTextures);
  glDeleteFramebuffers(1, &m.spareFbo);
  const GLuint textures[] = {m.efbColor, m.efbDepth, m.xfbTex, m.whiteTex};
  glDeleteTextures(4, textures);
  const GLuint fbos[] = {m.efbFbo, m.xfbFbo, m.msaaFbo};
  glDeleteFramebuffers(3, fbos);
  const GLuint renderbuffers[] = {m.msaaColor, m.msaaDepth};
  glDeleteRenderbuffers(2, renderbuffers);
  const GLuint buffers[] = {m.vbo, m.ibo, m.pxUbo};
  glDeleteBuffers(3, buffers);
  if (m.blendVbo)
    glDeleteBuffers(1, &m.blendVbo);
  m.blendVbo = 0;
  m.BlendRingRelease();
  glDeleteBuffers(kXfParts, m.xfUbo);
  m.StreamRelease();
  m.FenceRelease();
  m.PeekRelease();
  m.FxaaRelease();
  if (m.timerOn) {
    glDeleteQueries(Impl::TimerPool::kQueries, m.chunkTimer.query);
    glDeleteQueries(Impl::TimerPool::kQueries, m.presentTimer.query);
  }
  const GLuint vaos[] = {m.vao, m.quadVao};
  glDeleteVertexArrays(2, vaos);
  glDeleteProgram(m.copyProgram);
  glDeleteProgram(m.paletteProgram);
  glDeleteProgram(m.presentProgram);
  glDeleteProgram(m.focusProgram);
  glDeleteProgram(m.blurProgram);
  glDeleteProgram(m.halfProgram);
  glDeleteProgram(m.mixProgram);
  const GLuint halfTextures[] = {m.halfTex};
  glDeleteTextures(1, halfTextures);
  const GLuint halfFbos[] = {m.halfFbo, m.colorFbo};
  glDeleteFramebuffers(2, halfFbos);
  const GLuint autofocusTextures[] = {m.blurTex, m.focusTex[0], m.focusTex[1]};
  glDeleteTextures(3, autofocusTextures);
  const GLuint autofocusFbos[] = {m.blurFbo, m.focusFbo[0], m.focusFbo[1]};
  glDeleteFramebuffers(3, autofocusFbos);
  impl_.reset(new Impl);
}

} // namespace re4gx
