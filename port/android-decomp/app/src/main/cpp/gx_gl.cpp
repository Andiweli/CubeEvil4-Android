#include "gx_gl.h"

#include "gx_core.h"
#include "gx_fixed_shaders.h"

#include "gx_gl_shader.h"
#include "gx_texture.h"
#include "re4_gx_frame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
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

using fixed::kSharpLight;  // (gx_fixed_shaders.h)
using fixed::kSharpStrong;
// GL_EXT_texture_filter_anisotropic (not in the GLES 3.0 headers)
constexpr GLenum kMaxAnisotropy = 0x84FE, kMaxAnisotropyLimit = 0x84FF;

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
  mutable std::uint32_t lastUse = 0;  // stats.chunks of its last draw (EvictPrograms)
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

} // namespace

struct GlRenderer final : GxCore {
  GlRenderer() : GxCore(GxApi::kGl) {}
  static constexpr int kDepthWidth = GxRenderer::kDepthWidth, kDepthHeight = GxRenderer::kDepthHeight;
  using DepthSink = GxRenderer::DepthSink;



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

  GLuint xfbFbo = 0, xfbTex = 0;
  GLuint fxaaTex = 0, fxaaFbo = 0, fxaaProgram = 0;
  unsigned fxaaTexW = 0, fxaaTexH = 0;
  GLint fxaaTexelLoc = -1;
  GLuint sharpTex = 0, sharpFbo = 0, sharpProgram = 0;
  unsigned sharpTexW = 0, sharpTexH = 0;
  GLint sharpAmountLoc = -1, sharpLimitLoc = -1;
  GLuint psxTex = 0, psxFbo = 0, psxProgram = 0;
  unsigned psxTexW = 0, psxTexH = 0;
  GLint psxGridLoc = -1;
  // the picture before (its cells' colour levels hold while the new ones are near them): psxPrevTex, valid
  // while the pass ran for the picture before at the same size
  GLuint psxPrevTex = 0;
  bool psxPrevValid = false;
  GLint psxUsePrevLoc = -1;
  // Colour and the CRT (0.99.9): their programs, pictures and uniforms; one framebuffer for their passes
  GLuint filterFbo = 0, colourProgram = 0, colourTex = 0;
  int colourTexW = 0, colourTexH = 0;
  GLint colourModeLoc = -1, colourStrengthLoc = -1;
  GLuint crtLineProgram = 0, crtGlowProgram = 0, crtProgram = 0, crtLineTex = 0, crtGlowTex = 0;
  int crtLineW = 0, crtLineH = 0, crtGlowW = 0, crtGlowH = 0;
  GLint crtBleedLoc = -1, crtStepLoc = -1, crtRowsLoc = -1, crtLinesLoc = -1, crtBoxLoc = -1, crtShapeLoc = -1,
        crtBeamLoc = -1, crtMaskLoc = -1;
  int psxGridW = 0, psxGridH = 0;
  GLuint peekProgram = 0, peekFbo = 0, peekTex = 0, peekPbo[2] = {0, 0};
  GLsync peekFence[2] = {nullptr, nullptr};
  GLint peekSizeLoc = -1, peekScaleLoc = -1;
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
  GLuint xfUbo[kXfParts] = {}, pxUbo = 0;
  GLuint copyProgram = 0, presentProgram = 0, paletteProgram = 0;
  GLint presentRows = -1;
  GLint copyMode = -1, copyRect = -1, copySize = -1, copyHalf = -1, copyGain = -1, copyLimit = -1;
  GLint copyBox = -1;  // a half copy's box (the game's depth of field, kCopyFs)
  GLint copyTaps = -1, copyStep = -1;  // the TV filter (SetTvFilter): taps of the lines above/at/below, a line
  GLint paletteSize = -1, paletteKind = -1, paletteFormat = -1;
  GLuint quadVao = 0;
  std::unordered_map<ShaderKey, Program, KeyHash, KeyEqual> programs;
  std::unordered_map<std::uint64_t, GLuint> samplers;
  // the gameplay autofocus (Autofocus): made at its first marker
  GLuint focusProgram = 0, blurProgram = 0;
  GLint focusRegion = -1, focusSize = -1, focusZ = -1, focusPlayer = -1;
  GLint blurRegion = -1, blurSize = -1, blurZ = -1, blurRadius = -1;
  GLuint blurTex = 0, blurFbo = 0;
  // the focus (3 x 1 texels: the focus in use, the last two measurements), two in turn: each
  // autofocus reads the one before and writes the other (focusCur: the one in use)
  GLuint focusTex[2] = {}, focusFbo[2] = {};
  GLint focusRestart = -1;
  // GPU step e: the blur at half the resolution into halfTex, mixed into the EFB at the full one
  // (colorFbo: the EFB's colour without its depth, which the mix reads)
  GLuint halfProgram = 0, mixProgram = 0, halfTex = 0, halfFbo = 0, colorFbo = 0;
  GLint halfRegion = -1, halfSize = -1, halfZ = -1, halfRadius = -1;
  GLint mixRegion = -1, mixSize = -1, mixZ = -1;
  // the menu's depth of field "Strong" (0.98.4): more of the blur (blurStrength, mixStrength: 0.8 instead of 0.6)
  // and the taps further apart
  GLint blurStrength = -1, mixStrength = -1;

  const Program *keyProgram = nullptr;
  std::string driverTag;             // GL renderer and version: binaries only for this driver
  GLuint unitTex[8] = {}, unitSampler[8] = {};
  bool unitsValid = false;
  bool vaoBound = false;
  std::uint32_t attribMask = 0;      // enabled vertex attributes of vao
  std::uint32_t attribLayout = ~0u, attribStride = 0, attribBase = ~0u;
  GLuint attribBuffer = 0;
  // glDrawElementsBaseVertex (GLES 3.2 / OES / EXT): one attribute setup for
  // all draws of a layout; null: pointers per draw
  using DrawBaseVertex = void (*)(GLenum, GLsizei, GLenum, const void *, GLint);
  DrawBaseVertex drawBaseVertex = nullptr;

  GLuint mergeUbo = 0;  // without the uniform stream
  std::unordered_map<ShaderKey, Program, KeyHash, KeyEqual> mergedPrograms;


  // Other passes (uploads, copies, palettes, presenting) changed GL state.
  void Touched() override {
    stateValid = false;
    unitsValid = false;
    vaoBound = false;
    current = nullptr;
  }


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

  // A uniform block to its binding: in the stream, or into the small buffer. `copyBytes` (0: the
  // whole block): in the stream only the block's first bytes are written (the merged block's slots
  // in use), the rest of its place is left as it is.
  void UploadBlock(GLuint binding, GLuint buffer, const void *data, std::size_t offset, std::size_t bytes,
                   std::size_t blockBytes, std::size_t copyBytes = 0) {
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
    const std::size_t copy = copyBytes ? std::min(copyBytes, blockBytes) : blockBytes;
    if (streamMap) {
      std::memcpy(streamMap + at, data, copy);
    } else {
      glBindBuffer(GL_UNIFORM_BUFFER, stream);
      void *dst = glMapBufferRange(GL_UNIFORM_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(copy),
                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
      if (dst) {
        std::memcpy(dst, data, copy);
        glUnmapBuffer(GL_UNIFORM_BUFFER);
      } else {
        glBufferSubData(GL_UNIFORM_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(copy), data);
      }
    }
    glBindBufferRange(GL_UNIFORM_BUFFER, binding, stream, static_cast<GLintptr>(at),
                      static_cast<GLsizeiptr>(blockBytes));
    stats.uniformBytes += static_cast<unsigned>(copy);
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

  // --- vertex ring (GxOptions::vertexRing) -----------------------------------------
  // The chunk's vertices (vring) and indices with the merged chains' (iring) are copied to the next free
  // place of a buffer mapped for good, in quarters like the uniform stream; a fence guards a quarter
  // before it is written again. The place is taken before the chunk's first draw, never while drawing
  // (see the uniform stream: a fence in the middle of a picture). A quarter holds at least the largest
  // chunk; a larger one makes the ring anew (the GL buffer before is freed once the GPU is done with it).
  static constexpr unsigned kRingParts = 4;
  struct Ring {
    GLuint buffer = 0;
    std::uint8_t *map = nullptr;
    std::size_t part = 0;  // bytes per quarter
    unsigned cur = 0;
    std::size_t pos = 0;
    GLsync fence[kRingParts] = {};
  };
  Ring vring, iring;
  bool ringTried = false, ringOn = false;
  // where this chunk's vertices and indices lie (bytes) and the buffer of the vertices
  std::size_t vtxBase = 0, idxBase = 0;
  GLuint drawVbo = 0;

  static void *BufferStorage() {
    const char *ext = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    return ext && std::strstr(ext, "GL_EXT_buffer_storage") ? GlExtension("glBufferStorageEXT") : nullptr;
  }

  bool RingMake(Ring &r, GLenum target, std::size_t part) {
    using Storage = void (*)(GLenum, GLsizeiptr, const void *, GLbitfield);
    const auto storage = reinterpret_cast<Storage>(BufferStorage());
    if (!storage)
      return false;
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT;
    const std::size_t size = kRingParts * part;
    glGenBuffers(1, &r.buffer);
    glBindBuffer(target, r.buffer);
    storage(target, static_cast<GLsizeiptr>(size), nullptr, flags);
    r.map = static_cast<std::uint8_t *>(glMapBufferRange(target, 0, static_cast<GLsizeiptr>(size), flags));
    if (!r.map) {
      glDeleteBuffers(1, &r.buffer);
      r.buffer = 0;
      return false;
    }
    r.part = part;
    r.cur = 0;
    r.pos = 0;
    return true;
  }

  void RingRelease(Ring &r, GLenum target) {
    for (GLsync &f : r.fence) {
      if (f)
        glDeleteSync(f);
      f = nullptr;
    }
    if (r.buffer) {
      glBindBuffer(target, r.buffer);
      glUnmapBuffer(target);
      glDeleteBuffers(1, &r.buffer);
    }
    r = Ring();
  }

  // Room for `bytes` (at most a quarter) in `r`: its offset; the next quarter, once the GPU is done with
  // it, when this one has no room left.
  std::size_t RingAlloc(Ring &r, std::size_t bytes) {
    std::size_t at = (r.pos + 63) / 64 * 64;
    if (at + bytes > (r.cur + 1) * r.part) {
      r.fence[r.cur] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      r.cur = (r.cur + 1) % kRingParts;
      if (GLsync f = r.fence[r.cur]) {
        if (glClientWaitSync(f, GL_SYNC_FLUSH_COMMANDS_BIT, 0) == GL_TIMEOUT_EXPIRED) {
          stats.ringWaits++;
          glClientWaitSync(f, GL_SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
        }
        glDeleteSync(f);
        r.fence[r.cur] = nullptr;
      }
      at = r.cur * r.part;
    }
    r.pos = at + bytes;
    return at;
  }

  // A quarter of `r` holds `bytes`: the ring anew when it is smaller (half again, in whole MB)
  bool RingFit(Ring &r, GLenum target, std::size_t bytes, std::size_t least) {
    if (r.buffer && bytes <= r.part)
      return true;
    const std::size_t part = std::max(least, (bytes + bytes / 2 + (1u << 20) - 1) >> 20 << 20);
    const bool grow = r.buffer != 0;
    RingRelease(r, target);
    if (!RingMake(r, target, part))
      return false;
    if (grow) {
      stats.ringGrows++;
      GXLOG("GX vertex ring: %s grown to 4 x %zu KB for a chunk of %zu KB", target == GL_ARRAY_BUFFER ? "vertices" : "indices",
            part >> 10, bytes >> 10);
    }
    return true;
  }

  // The chunk's vertices and indices (and the merged chains' after them) into the rings, the index ring bound
  // to vao (bound); false: not possible here, the caller uploads as before.
  bool RingUpload(const Re4GxChunk &chunk) {
    if (!ringTried) {
      ringTried = true;
      const char *off = std::getenv("RE4_GX_VERTEX_RING"), *persistent = std::getenv("RE4_GX_PERSISTENT");
      ringOn = options.vertexRing && !(off && *off == '0') && !(persistent && *persistent == '0') && BufferStorage();
      GXLOG("GX vertex ring: %s", ringOn ? "on (vertices and indices into buffers mapped for good)"
                                         : "off (a buffer per chunk)");
    }
    if (!ringOn)
      return false;
    const std::size_t vbytes = std::max<std::size_t>(chunk.vertexBytes, 4);
    const std::size_t own = static_cast<std::size_t>(chunk.indexCount) * 2, moved = mergeIndices.size() * 2;
    const std::size_t ibytes = std::max<std::size_t>(own + moved, 4);
    if (!RingFit(vring, GL_ARRAY_BUFFER, vbytes, std::size_t(2) << 20) ||
        !RingFit(iring, GL_ELEMENT_ARRAY_BUFFER, ibytes, std::size_t(512) << 10)) {
      GXLOG("GX vertex ring: could not be made; a buffer per chunk from now on");
      RingRelease(vring, GL_ARRAY_BUFFER);
      RingRelease(iring, GL_ELEMENT_ARRAY_BUFFER);
      ringOn = false;
      return false;
    }
    vtxBase = RingAlloc(vring, vbytes);
    if (chunk.vertexBytes)
      std::memcpy(vring.map + vtxBase, chunk.vertices, chunk.vertexBytes);
    idxBase = RingAlloc(iring, ibytes);
    if (own)
      std::memcpy(iring.map + idxBase, chunk.indices, own);
    if (moved)
      std::memcpy(iring.map + idxBase + own, mergeIndices.data(), moved);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iring.buffer);
    drawVbo = vring.buffer;
    stats.ringChunks++;
    return true;
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















  // --- state -----------------------------------------------------------------









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
  void UseEfb() override {
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

  // point: the PSone demake's 3D textures, every texel a hard square (the PlayStation filtered nothing)
  GLuint Sampler(std::uint32_t mode0, std::uint32_t mode1, unsigned levels, bool point = false) {
    const std::uint64_t key = (static_cast<std::uint64_t>(mode1 & 0xFFFF) << 32) | (mode0 & 0x1FF) |
                              (static_cast<std::uint64_t>(levels > 1) << 48) | (static_cast<std::uint64_t>(point) << 49);
    auto it = samplers.find(key);
    if (it != samplers.end())
      return it->second;
    const GxSamplerDesc d = SamplerFor(mode0, mode1, levels, point);
    GLuint s = 0;
    glGenSamplers(1, &s);
    static const GLenum wrap[3] = {GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT};
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, wrap[d.wrapS]);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, wrap[d.wrapT]);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, d.magLinear ? GL_LINEAR : GL_NEAREST);
    // [linear][mip]: none, nearest, linear
    static const GLenum min[2][3] = {{GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR},
                                     {GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR}};
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, min[d.minLinear ? 1 : 0][d.mip]);
    glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, d.minLod);
    glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, d.maxLod);
    if (d.anisotropy > 1.0f)
      glSamplerParameterf(s, kMaxAnisotropy, d.anisotropy);
    samplers[key] = s;
    return s;
  }



  // A texture copy read through a palette (C4/C8/C14X2 over a copy): the copy's
  // values looked up in TMEM's palette, on the GPU.
  // a copy read through a palette (GxCore::PalettedCopy): the copy's values through the palette's colours
  // (rgba, pw x ph) into a new texture of the copy's size
  unsigned DrawPaletted(const Copy &copy, const Re4GxTexture &t, const std::uint8_t *rgba, unsigned pw,
                        unsigned ph) override {
    GLuint pal = 0;
    glGenTextures(1, &pal);
    glBindTexture(GL_TEXTURE_2D, pal);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(pw), static_cast<GLsizei>(ph), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLuint out = 0;
    glGenTextures(1, &out);
    glBindTexture(GL_TEXTURE_2D, out);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(copy.width), static_cast<GLsizei>(copy.height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    BindFbo(fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out, 0);
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
    return out;
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

  void ApplyDrawState(const DrawState &s) override {
    BindFbo(DrawFbo());
    glViewport(0, 0, Lo(kEfbWidth), Lo(kEfbHeight));
    glEnable(GL_SCISSOR_TEST);
    glScissor(s.scissor[0], s.scissor[1], s.scissor[2], s.scissor[3]);
    if (s.depthTest) {
      static const GLenum func[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(func[s.depthFunc & 7]);
      glDepthMask(s.depthWrite ? GL_TRUE : GL_FALSE);
    } else {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
    }
    glColorMask(s.colorWrite, s.colorWrite, s.colorWrite, s.alphaWrite);
    if (s.blend) {
      static const GLenum factor[10] = {GL_ZERO,      GL_ONE,           GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                        GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                        GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
      static const GLenum eq[2] = {GL_FUNC_ADD, GL_FUNC_REVERSE_SUBTRACT};
      glEnable(GL_BLEND);
      glBlendEquationSeparate(eq[s.eqColor], eq[s.eqAlpha]);
      glBlendFuncSeparate(factor[s.srcColor], factor[s.dstColor], factor[s.srcAlpha], factor[s.dstAlpha]);
    } else {
      glDisable(GL_BLEND);
    }
    if (!s.cull) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(s.cull == 1 ? GL_BACK : s.cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK);
    }
    glLineWidth(std::max(1.0f, s.lineWidth));
  }

  // --- draws --------------------------------------------------------------------


  // A program's files in the cache without .bin/.key: a hash of both sources and the driver; in
  // *check a second hash of the sources (in the binary's head).
  std::string CacheStem(const std::string &vs, const std::string &fs, std::uint64_t *check) const {
    return ProgramStem(programCache, driverTag, vs, fs, check);
  }

  // The programs of `map` beyond `keep` that went longest without a draw (not this chunk's) are deleted.
  void EvictPrograms(std::unordered_map<ShaderKey, Program, KeyHash, KeyEqual> &map, std::size_t keep) {
    std::vector<std::pair<std::uint32_t, ShaderKey>> order;
    order.reserve(map.size());
    for (const auto &e : map)
      if (e.second.id && e.second.lastUse != stats.chunks)
        order.emplace_back(e.second.lastUse, e.first);
    std::sort(order.begin(), order.end(),
              [](const std::pair<std::uint32_t, ShaderKey> &a, const std::pair<std::uint32_t, ShaderKey> &b) {
                return a.first < b.first;
              });
    unsigned n = 0;
    for (const auto &o : order) {
      if (map.size() <= keep)
        break;
      const auto it = map.find(o.second);
      if (&it->second == keyProgram) {
        keyProgram = nullptr;
        keyDirty = true;
      }
      glDeleteProgram(it->second.id);
      map.erase(it);
      n++;
    }
    current = nullptr;  // glUseProgram again before the next draw
    evicted += n;
    stats.programsDeleted += n;
    static unsigned lines = 0;
    if (n && lines++ < 20)
      GXLOG("GX programs: %u deleted that went longest unused (%zu kept, %u deleted so far)", n, map.size(), evicted);
  }

  // `merged`: the program of a merged draw call (GxMergedVertexShader). Its binary goes into the
  // cache, its key does not (Prewarm makes the single draws' programs).
  const Program *ProgramFor(const ShaderKey &k, bool merged = false) {
    auto &map = merged ? mergedPrograms : programs;
    auto it = map.find(k);
    if (it != map.end()) {
      it->second.lastUse = stats.chunks;
      return it->second.id ? &it->second : nullptr;
    }
    const std::size_t cap = merged ? maxMerged : maxPrograms;
    if (map.size() >= cap)
      EvictPrograms(map, cap * 3 / 4);
    Program p;
    std::string log;
    const std::string vs = merged ? GxMergedVertexShader(k) : GxVertexShader(k), fs = GxFragmentShader(k);
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
    if (p.id && !cachePath.empty() && !merged)
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
      const GLuint mergeIndex = glGetUniformBlockIndex(p.id, "XFMM");
      if (mergeIndex != GL_INVALID_INDEX)
        glUniformBlockBinding(p.id, mergeIndex, kMergeBinding);
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
    p.lastUse = stats.chunks;
    auto &slot = map[k];
    slot = p;
    stats.programsLive = static_cast<unsigned>(programs.size() + mergedPrograms.size());
    return slot.id ? &slot : nullptr;
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

  // `merge`: the merged draw call of a run that starts with `d` (DrawMerged; the XFM parts and
  // uMtx of its draws in mergeBlock).
  void SubmitDraw(const Re4GxDraw &d, const void *program, const MergeCall *merge) override {
    const Program *p = static_cast<const Program *>(program);
    const ShaderKey &k = key;
    const unsigned primitive = d.primitive & 0xFF;
    if (current != p) {
      glUseProgram(p->id);
      current = p;
      stats.programSwitches++;
    }
    if (!merge && p->mtx >= 0 && (p->lastMtx[0] != d.matrices[0] || p->lastMtx[1] != d.matrices[1] ||
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
      const GLuint sampler = Sampler(mode0, mode1, bound[m].levels, (k.centroid & 2) != 0);
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
    // (the chunk's vertices: from the vertex ring at vtxBase, or from vbo)
    const GLuint buffer = blendAt >= 0 ? (blendMap ? blendRing : blendVbo) : drawVbo;
    const std::uint32_t at = blendAt >= 0 ? static_cast<std::uint32_t>(blendAt) : d.vertexOffset;
    const std::uint32_t shift = blendAt >= 0 ? 0 : static_cast<std::uint32_t>(vtxBase);
    blendAt = -1;
    std::uint32_t base = shift + at;
    GLint baseVertex = 0;
    if (drawBaseVertex && d.stride) {
      base = shift + at % d.stride;
      baseVertex = static_cast<GLint>(merge ? merge->base : at / d.stride);
    }
    if (d.layout != attribLayout || d.stride != attribStride || base != attribBase || buffer != attribBuffer) {
      SetAttributes(d.layout, d.stride, base, buffer);
      stats.attributeSetups++;
    }
    const GLenum mode = primitive == RE4_GX_LINES ? GL_LINES : primitive == RE4_GX_POINTS ? GL_POINTS : GL_TRIANGLES;
    const std::uint32_t indexAt = merge ? merge->index : d.indexOffset;
    const std::uint32_t indexCount = merge ? merge->count : d.indexCount;
    const void *indices = reinterpret_cast<const void *>(idxBase + static_cast<std::uintptr_t>(indexAt) * 2);
    Split(GxStats::kSplitBind);
    if (drawBaseVertex)
      drawBaseVertex(mode, static_cast<GLsizei>(indexCount), GL_UNSIGNED_SHORT, indices, baseVertex);
    else
      glDrawElements(mode, static_cast<GLsizei>(indexCount), GL_UNSIGNED_SHORT, indices);
    Split(GxStats::kSplitDraw);
  }
  bool KeyProgram() override {
    keyProgram = ProgramFor(key);
    return keyProgram != nullptr;
  }
  const void *DrawProgram(bool merged) override {
    const Program *p = merged ? ProgramFor(key, true) : keyProgram;
    if (p)
      p->lastUse = stats.chunks;
    return p;
  }
  void UploadUniform(int binding, const void *data, std::size_t offset, std::size_t bytes, std::size_t blockBytes,
                     std::size_t copyBytes) override {
    const GLuint buffer = binding == kPixelBinding   ? pxUbo
                          : binding == kMergeBinding ? mergeUbo
                                                     : xfUbo[binding == kXfPartBinding[0] ? 0 : binding == kXfPartBinding[1] ? 1 : 2];
    UploadBlock(static_cast<GLuint>(binding), buffer, data, offset, bytes, blockBytes, copyBytes);
  }


  // --- merged draw calls ---------------------------------------------------------------




  // The merged draw calls' program of `k`; null while Prewarm has yet to make it (mergeWanted).
  const Program *MergedProgram(const ShaderKey &k) {
    const auto it = mergedPrograms.find(k);
    if (it != mergedPrograms.end())
      return it->second.id ? &it->second : nullptr;
    if (std::find_if(mergeWanted.begin(), mergeWanted.end(), [&](const ShaderKey &w) { return KeyEqual()(w, k); }) ==
        mergeWanted.end())
      mergeWanted.push_back(k);
    return nullptr;
  }



  // --- EFB copies -------------------------------------------------------------------



  // --- FXAA ------------------------------------------------------------------------------------

  bool MakeFxaa() {
    std::string log;
    fxaaProgram = Link(fixed::kFxaaVs, fixed::kFxaaFs, &log);
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

  // --- Sharpen ---------------------------------------------------------------------------------

  bool MakeSharpen() {
    std::string log;
    sharpProgram = Link(fixed::kSharpenVs, fixed::kSharpenFs, &log);
    if (!sharpProgram) {
      GXLOG("sharpen: shader failed: %s", log.c_str());
      return false;
    }
    glUseProgram(sharpProgram);
    glUniform1i(glGetUniformLocation(sharpProgram, "src"), 0);
    sharpAmountLoc = glGetUniformLocation(sharpProgram, "amount");
    sharpLimitLoc = glGetUniformLocation(sharpProgram, "lim");
    glGenFramebuffers(1, &sharpFbo);
    return true;
  }

  // The display copy just made (after FXAA; w x h texels of the EFB rows y..y+rows) sharpened into sharpTex: that
  // is shown now. Not for a movie's pictures: sharpened, the blocks of their compression would stand out.
  void Sharpen(int w, int h, int y, int rows) {
    sharpShown = false;
    if (sharpLevel <= 0 || chunkMovie || !xfbTex || w < 16 || h < 16 || rows < 1)
      return;
    if (!sharpProgram && !MakeSharpen()) {
      sharpLevel = 0;  // no shader on this driver: off for good (the menu's switch stays)
      return;
    }
    if (!sharpTex || sharpTexW != static_cast<unsigned>(w) || sharpTexH != static_cast<unsigned>(h)) {
      if (sharpTex)
        glDeleteTextures(1, &sharpTex);
      glGenTextures(1, &sharpTex);
      glBindTexture(GL_TEXTURE_2D, sharpTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      sharpTexW = static_cast<unsigned>(w);
      sharpTexH = static_cast<unsigned>(h);
    }
    // the rows shown, in texels of this copy (row 0 its top)
    const int shownT = shownTop.load(std::memory_order_relaxed), shownB = shownBottom.load(std::memory_order_relaxed);
    const int first = std::max(0, std::min(h - 1, (shownT - y) * h / rows));
    const int last = std::max(first, std::min(h - 1, (shownB - y) * h / rows - 1));
    const GLuint from = fxaaShown && fxaaTex ? fxaaTex : xfbTex;
    Touched();
    SaveAndPlainState();
    BindFbo(sharpFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sharpTex, 0);
    ForgetColor();  // all of it is drawn over
    glViewport(0, 0, w, h);
    glUseProgram(sharpProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, from);
    glBindSampler(0, 0);
    static float amounts[2] = {-1.0f, -1.0f};
    if (amounts[0] < 0.0f) {
      amounts[0] = kSharpLight;
      amounts[1] = kSharpStrong;
      if (const char *test = std::getenv("RE4_GX_SHARPEN_AMOUNT"))
        std::sscanf(test, "%f,%f", &amounts[0], &amounts[1]);
    }
    glUniform1f(sharpAmountLoc, amounts[sharpLevel >= 2 ? 1 : 0]);
    glUniform3i(sharpLimitLoc, w - 1, first, last);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    BindFbo(DrawFbo());
    current = nullptr;
    sharpShown = true;
    stats.sharpenPasses++;
  }

  void SharpenRelease() {
    if (sharpTex)
      glDeleteTextures(1, &sharpTex);
    if (sharpFbo)
      glDeleteFramebuffers(1, &sharpFbo);
    if (sharpProgram)
      glDeleteProgram(sharpProgram);
    sharpTex = sharpFbo = sharpProgram = 0;
    sharpTexW = sharpTexH = 0;
    sharpShown = false;
  }

  GLuint ShownXfb() const {
    return colourShown && colourTex ? colourTex
           : psxShown && psxTex ? psxTex
           : sharpShown && sharpTex ? sharpTex
           : fxaaShown && fxaaTex ? fxaaTex
                                  : xfbTex;
  }

  // --- PSone demake ----------------------------------------------------------------------------

  bool MakePsx() {
    std::string log;
    psxProgram = Link(fixed::kPsxVs, fixed::kPsxFs, &log);
    if (!psxProgram) {
      GXLOG("PSone demake: shader failed: %s", log.c_str());
      return false;
    }
    glUseProgram(psxProgram);
    glUniform1i(glGetUniformLocation(psxProgram, "src"), 0);
    glUniform1i(glGetUniformLocation(psxProgram, "prev"), 1);
    psxGridLoc = glGetUniformLocation(psxProgram, "grid");
    psxUsePrevLoc = glGetUniformLocation(psxProgram, "usePrev");
    glGenFramebuffers(1, &psxFbo);
    return true;
  }

  // The display copy as shown so far (w x h texels; gw x gh of the game's pixels) through the demake pass into
  // psxTex: a grid of half the game's pixels, as the PlayStation's 320 x 240.
  void Psx(int w, int h, int gw, int gh) {
    const bool wasShown = psxShown;
    psxShown = false;
    if (!psxOn || !xfbTex || w < 16 || h < 16 || gw < 4 || gh < 4) {
      psxPrevValid = false;
      return;
    }
    if (!psxProgram && !MakePsx()) {
      psxOn = false;  // no shader on this driver: off for good (the menu's switch stays)
      return;
    }
    if (!psxTex || !psxPrevTex || psxTexW != static_cast<unsigned>(w) || psxTexH != static_cast<unsigned>(h)) {
      for (GLuint *t : {&psxTex, &psxPrevTex}) {
        if (*t)
          glDeleteTextures(1, t);
        glGenTextures(1, t);
        glBindTexture(GL_TEXTURE_2D, *t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      }
      psxTexW = static_cast<unsigned>(w);
      psxTexH = static_cast<unsigned>(h);
      psxPrevValid = false;
    }
    const GLuint from = ShownXfb();  // the XFB, or FXAA's copy of it
    // the picture before becomes psxPrevTex, the new one goes to the other texture
    std::swap(psxTex, psxPrevTex);
    const bool usePrev = psxPrevValid && wasShown && psxGridW == gw / 2 && psxGridH == gh / 2;
    psxGridW = gw / 2;
    psxGridH = gh / 2;
    Touched();
    SaveAndPlainState();
    BindFbo(psxFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, psxTex, 0);
    ForgetColor();  // all of it is drawn over
    glViewport(0, 0, w, h);
    glUseProgram(psxProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, from);
    glBindSampler(0, 0);
    glUniform2f(psxGridLoc, static_cast<float>(gw / 2), static_cast<float>(gh / 2));
    glUniform1f(psxUsePrevLoc, usePrev ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, psxPrevTex);
    glBindSampler(1, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    BindFbo(DrawFbo());
    current = nullptr;
    psxShown = true;
    psxPrevValid = true;
  }

  // --- Colour and the CRT (wishes of the project owner, 11.11.2026) ------------------------------------------

  // a pass's picture (RGBA8, linear, clamped), made anew at another size
  static void PassTexture(GLuint *tex, int *tw, int *th, int w, int h) {
    if (*tex && *tw == w && *th == h)
      return;
    if (*tex)
      glDeleteTextures(1, tex);
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    *tw = w;
    *th = h;
  }
  // one pass of `program` (its uniforms set before): `from` read, `to` (w x h) drawn whole
  void FilterDraw(GLuint program, GLuint to, int w, int h, GLuint from) {
    Touched();
    SaveAndPlainState();
    if (!filterFbo)
      glGenFramebuffers(1, &filterFbo);
    BindFbo(filterFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, to, 0);
    ForgetColor();  // all of it is drawn over
    glViewport(0, 0, w, h);
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, from);
    glBindSampler(0, 0);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    BindFbo(DrawFbo());
    current = nullptr;
  }
  // the display copy as shown so far (w x h texels) graded into colourTex
  void Colour(int w, int h) {
    colourShown = false;
    if (colourMode <= 0 || colourFailed || !xfbTex || w < 16 || h < 16)
      return;
    if (!colourProgram) {
      std::string log;
      colourProgram = Link(fixed::kQuadVs, fixed::kColourFs, &log);
      if (!colourProgram) {
        GXLOG("colour: shader failed: %s", log.c_str());
        colourFailed = true;  // off for good on this driver (the menu's choice stays)
        return;
      }
      glUseProgram(colourProgram);
      glUniform1i(glGetUniformLocation(colourProgram, "src"), 0);
      colourModeLoc = glGetUniformLocation(colourProgram, "mode");
      colourStrengthLoc = glGetUniformLocation(colourProgram, "strength");
    }
    PassTexture(&colourTex, &colourTexW, &colourTexH, w, h);
    const GLuint from = ShownXfb();
    glUseProgram(colourProgram);
    glUniform1i(colourModeLoc, colourMode);
    glUniform1f(colourStrengthLoc, colourStrength);
    FilterDraw(colourProgram, colourTex, w, h, from);
    colourShown = true;
    stats.colourPasses++;
  }
  // the CRT's pictures of the display copy as shown (pw x ph texels; gw x gh game pixels): its game lines with the
  // colour bleed, their glow (Present draws the screen from them)
  void Crt(int pw, int ph, int gw, int gh) {
    crtReady = false;
    if (crtLevel <= 0 || crtFailed || !xfbTex || pw < 16 || ph < 16 || gw < 16 || gh < 16)
      return;
    if (!crtProgram) {
      std::string log;
      crtLineProgram = Link(fixed::kQuadVs, fixed::kCrtLineFs, &log);
      crtGlowProgram = crtLineProgram ? Link(fixed::kQuadVs, fixed::kCrtGlowFs, &log) : 0;
      crtProgram = crtGlowProgram ? Link(fixed::kQuadVs, fixed::kCrtFs, &log) : 0;
      if (!crtProgram) {
        GXLOG("CRT: shader failed: %s", log.c_str());
        CrtRelease();
        crtFailed = true;  // off for good on this driver (the menu's choice stays)
        return;
      }
      glUseProgram(crtLineProgram);
      glUniform1i(glGetUniformLocation(crtLineProgram, "src"), 0);
      crtBleedLoc = glGetUniformLocation(crtLineProgram, "bleed");
      glUseProgram(crtGlowProgram);
      glUniform1i(glGetUniformLocation(crtGlowProgram, "src"), 0);
      crtStepLoc = glGetUniformLocation(crtGlowProgram, "glowStep");
      glUseProgram(crtProgram);
      glUniform1i(glGetUniformLocation(crtProgram, "crtLine"), 0);
      glUniform1i(glGetUniformLocation(crtProgram, "crtGlow"), 1);
      crtRowsLoc = glGetUniformLocation(crtProgram, "rows");
      crtLinesLoc = glGetUniformLocation(crtProgram, "lines");
      crtBoxLoc = glGetUniformLocation(crtProgram, "box");
      crtShapeLoc = glGetUniformLocation(crtProgram, "shape");
      crtBeamLoc = glGetUniformLocation(crtProgram, "beam");
      crtMaskLoc = glGetUniformLocation(crtProgram, "mask");
    }
    const fixed::CrtPreset &p = fixed::kCrtPresets[crtLevel >= 2 ? 1 : 0];
    const int lw = std::min(pw, 1280), ww = std::max(1, gw / 4), wh = std::max(1, gh / 4);
    PassTexture(&crtLineTex, &crtLineW, &crtLineH, lw, gh);
    PassTexture(&crtGlowTex, &crtGlowW, &crtGlowH, ww, wh);
    const GLuint from = ShownXfb();
    glUseProgram(crtLineProgram);
    glUniform1f(crtBleedLoc, p.bleed / static_cast<float>(gw));
    FilterDraw(crtLineProgram, crtLineTex, lw, gh, from);
    glUseProgram(crtGlowProgram);
    glUniform2f(crtStepLoc, 1.0f / static_cast<float>(ww), 1.0f / static_cast<float>(wh));
    FilterDraw(crtGlowProgram, crtGlowTex, ww, wh, crtLineTex);
    crtLines = gh;
    crtReady = true;
    stats.crtPasses++;
  }
  // Present's CRT: the box (w x h screen pixels; the viewport set) from the CRT's pictures, rows r0 + r1 of the copy
  void CrtPresent(int w, int h, float r0, float r1) {
    const fixed::CrtPreset &p = fixed::kCrtPresets[crtLevel >= 2 ? 1 : 0];
    glUseProgram(crtProgram);
    glUniform2f(crtRowsLoc, r0, r1);
    glUniform1f(crtLinesLoc, static_cast<float>(crtLines));
    glUniform2f(crtBoxLoc, static_cast<float>(w), static_cast<float>(h));
    glUniform4f(crtShapeLoc, p.warpX, p.warpY, p.corner, p.vignette);
    glUniform4f(crtBeamLoc, p.sigmaDark, p.sigmaBright, p.boost, p.glow);
    glUniform2f(crtMaskLoc, p.maskKind, p.maskStrength);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, crtGlowTex);
    glBindSampler(1, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, crtLineTex);
    glBindSampler(0, 0);
  }
  void CrtRelease() {
    for (GLuint *t : {&colourTex, &crtLineTex, &crtGlowTex})
      if (*t)
        glDeleteTextures(1, t);
    for (GLuint *prog : {&colourProgram, &crtLineProgram, &crtGlowProgram, &crtProgram})
      if (*prog)
        glDeleteProgram(*prog);
    if (filterFbo)
      glDeleteFramebuffers(1, &filterFbo);
    filterFbo = colourTex = crtLineTex = crtGlowTex = 0;
    colourProgram = crtLineProgram = crtGlowProgram = crtProgram = 0;
    colourTexW = colourTexH = crtLineW = crtLineH = crtGlowW = crtGlowH = 0;
    colourShown = crtReady = false;
  }

  void PsxRelease() {
    if (psxTex)
      glDeleteTextures(1, &psxTex);
    if (psxPrevTex)
      glDeleteTextures(1, &psxPrevTex);
    psxPrevTex = 0;
    psxPrevValid = false;
    if (psxFbo)
      glDeleteFramebuffers(1, &psxFbo);
    if (psxProgram)
      glDeleteProgram(psxProgram);
    psxTex = psxFbo = psxProgram = 0;
    psxTexW = psxTexH = 0;
    psxShown = false;
  }

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

  GLuint flareVisTex[2] = {}, flareVisFbo[2] = {}, flareVisProgram = 0, flareProgram = 0;
  GLuint flareColorFbo = 0;  // the EFB's colour without its depth (no MSAA): the flares go there
  int flareVisCur = 0;
  GLint flareVisSlotLoc = -1, flareVisSizeLoc = -1, flareSlotLoc = -1, flareSizeLoc = -1, flareGhostLoc = -1, flareCrowdLoc = -1;



  bool MakeFlares() {
    std::string log;
    flareVisProgram = Link(fixed::kFlareQuadVs, fixed::kFlareVisFs, &log);
    if (flareVisProgram)
      flareProgram = Link(fixed::kFlareVs, fixed::kFlareFs, &log);
    if (!flareVisProgram || !flareProgram) {
      GXLOG("lens flares: shader failed: %s", log.c_str());
      return false;
    }
    glUseProgram(flareVisProgram);
    glUniform1i(glGetUniformLocation(flareVisProgram, "depths"), 0);
    glUniform1i(glGetUniformLocation(flareVisProgram, "colors"), 1);
    glUniform1i(glGetUniformLocation(flareVisProgram, "before"), 2);
    flareVisSlotLoc = glGetUniformLocation(flareVisProgram, "slot");
    flareVisSizeLoc = glGetUniformLocation(flareVisProgram, "size");
    glUseProgram(flareProgram);
    glUniform1i(glGetUniformLocation(flareProgram, "vis"), 0);
    flareSlotLoc = glGetUniformLocation(flareProgram, "slot");
    flareGhostLoc = glGetUniformLocation(flareProgram, "ghost");
    flareCrowdLoc = glGetUniformLocation(flareProgram, "crowd");
    flareSizeLoc = glGetUniformLocation(flareProgram, "size");
    glGenTextures(2, flareVisTex);
    glGenFramebuffers(2, flareVisFbo);
    bool ok = true;
    for (int i = 0; i < 2; i++) {
      glBindTexture(GL_TEXTURE_2D, flareVisTex[i]);
      const std::uint32_t zero[kFlareSlots * 2] = {};
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kFlareSlots, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, flareVisFbo[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, flareVisTex[i], 0);
      ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glGenFramebuffers(1, &flareColorFbo);
    if (!ok)
      GXLOG("lens flares: the visibility frame buffer is not complete");
    flareLog = std::getenv("RE4_GX_FLARE_LOG") && *std::getenv("RE4_GX_FLARE_LOG") >= '1';
    flareLog2 = flareLog && *std::getenv("RE4_GX_FLARE_LOG") == '2';
    return ok;
  }


  // After the scene's last 3D draw (Run): the visibility pass, then the flares into the EFB.
  void FlarePass() override {
    flareDone = true;
    FlareTrack();
    flareCands.clear();
    bool any = false;
    for (const FlareSlot &s : flareSlot)
      any = any || s.lost >= 0;
    if (!any || flareFailed)
      return;
    Touched();
    if (!flareProgram && !MakeFlares()) {
      flareFailed = true;
      BindFbo(DrawFbo());
      return;
    }
    const int fw = Lo(kEfbWidth), fh = Lo(kEfbHeight);
    const float sc = Sf(1);
    float slots[kFlareSlots][4];
    for (int i = 0; i < kFlareSlots; i++) {
      const FlareSlot &s = flareSlot[i];
      slots[i][0] = s.x * sc;
      slots[i][1] = s.y * sc;
      slots[i][2] = s.z;
      // state (0 free, 1 seen, 2 new, 3 lost, 4 seen but not yet in 4 pictures) + 8 x the radius in EFB texels
      // (the taps reach 0.4 of it)
      slots[i][3] = (s.lost < 0 ? 0.0f : s.lost > 0 ? 3.0f : s.fresh ? 2.0f : s.age < 4 ? 4.0f : 1.0f) +
                    (s.lost < 0 ? 0.0f : 8.0f * std::floor(s.r * sc));
    }
    // the picture's depth and colour (MSAA in own buffers: resolved), as the autofocus reads them
    Resolve(0, 0, Lo(512), Lo(448), false);
    Resolve(0, 0, Lo(512), Lo(448), true);
    SaveAndPlainState();
    glDepthMask(GL_FALSE);
    glBindVertexArray(quadVao);
    for (int i = 0; i < 3; i++)
      glBindSampler(i, 0);
    const int next = flareVisCur ^ 1;
    BindFbo(flareVisFbo[next]);
    glViewport(0, 0, kFlareSlots, 2);
    glUseProgram(flareVisProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, efbDepth);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, efbColor);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, flareVisTex[flareVisCur]);
    glUniform4fv(flareVisSlotLoc, kFlareSlots, &slots[0][0]);
    glUniform4f(flareVisSizeLoc, static_cast<float>(fw), static_cast<float>(fh), std::max(1.0f, 1.5f * sc),
                flareLog2 ? 1.0f : 0.0f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    flareVisCur = next;
    if (flareLog && flarePictures <= 400) {
      // (tests) each slot's followed visibility and brightness as the flares will take them
      std::uint8_t vis[kFlareSlots * 4];
      glReadPixels(0, 0, kFlareSlots, 1, GL_RGBA, GL_UNSIGNED_BYTE, vis);
      for (int i = 0; i < kFlareSlots; i++)
        if (flareSlot[i].lost >= 0)
          GXLOG("lens flares:   slot %d at (%.0f %.0f) r %.1f depth %.5f %s: shown %.2f, colour %u %u %u", i,
                flareSlot[i].x, flareSlot[i].y, flareSlot[i].r, flareSlot[i].z,
                flareSlot[i].lost > 0 ? "lost" : flareSlot[i].fresh ? "new" : "seen", vis[i * 4 + 3] / 255.0,
                vis[i * 4], vis[i * 4 + 1], vis[i * 4 + 2]);
    }
    // the flares: their radius in EFB texels in slot.z; strength: at most about a third of white
    float ghosts[kFlareSlots], crowds[kFlareSlots];
    for (int i = 0; i < kFlareSlots; i++) {
      slots[i][2] = flareSlot[i].r * sc;
      ghosts[i] = flareSlot[i].ghost;
      crowds[i] = flareSlot[i].crowd;
    }
    BindFbo(msaaFbo ? msaaFbo : flareColorFbo);
    if (!msaaFbo)
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, efbColor, 0);
    glViewport(0, 0, fw, fh);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(flareProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, flareVisTex[flareVisCur]);
    glUniform4fv(flareSlotLoc, kFlareSlots, &slots[0][0]);
    glUniform1fv(flareGhostLoc, kFlareSlots, ghosts);
    glUniform1fv(flareCrowdLoc, kFlareSlots, crowds);
    glUniform4f(flareSizeLoc, static_cast<float>(fw), static_cast<float>(fh), flareHalos ? 0.0f : 1.0f, 0.5f);
    glDrawArrays(GL_TRIANGLES, 0, 36 * kFlareSlots);
    glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    BindFbo(DrawFbo());
    current = nullptr;
  }

  void FlareRelease() {
    if (flareVisFbo[0])
      glDeleteFramebuffers(2, flareVisFbo);
    if (flareVisTex[0])
      glDeleteTextures(2, flareVisTex);
    if (flareVisProgram)
      glDeleteProgram(flareVisProgram);
    if (flareProgram)
      glDeleteProgram(flareProgram);
    if (flareColorFbo)
      glDeleteFramebuffers(1, &flareColorFbo);
    flareVisFbo[0] = flareVisFbo[1] = flareVisTex[0] = flareVisTex[1] = flareVisProgram = flareProgram = 0;
    flareColorFbo = 0;
  }

  // --- the depth for the game's GXPeekZ ------------------------------------------------------

  bool MakePeek() {
    using fixed::kPeekVs;  // gx_fixed_shaders.h
    using fixed::kPeekFs;
    std::string log;
    peekProgram = Link(kPeekVs, kPeekFs, &log);
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
  void SnapshotDepth() override {
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
  void Autofocus(std::uint32_t player) override {
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
    // the first port's tap distance: 1.5 pixels of a 480-line picture; "Strong" (the menu, 0.98.4) 2.5, and up to
    // 80 % of the blur instead of 60 %
    const float radius = dofStrong ? std::min(6.0f, std::max(1.5f, rh / 480.0f * 2.5f))
                                   : std::min(4.0f, std::max(1.0f, rh / 480.0f * 1.5f));
    const float strength = dofStrong ? 0.8f : 0.6f;
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
      glUniform1f(mixStrength, strength);
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
    glUniform1f(blurStrength, strength);
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
    glUniform3f(copyTaps, 0.0f, -1.0f, 0.0f);  // no TV filter
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    current = nullptr;
    stats.autofocus++;
  }

  bool MakeAutofocus(int fw, int fh) {
    const std::string focusFs = std::string(fixed::kDofHead) + fixed::kDofDistance + fixed::kDofFocusFs;
    const std::string blurFs = std::string(fixed::kDofHead) + fixed::kDofDistance + fixed::kDofBlurFs;
    const std::string halfFs = std::string(fixed::kDofHead) + fixed::kDofDistance + fixed::kDofHalfFs;
    const std::string mixFs = std::string(fixed::kDofHead) + fixed::kDofDistance + fixed::kDofMixFs;
    std::string log;
    focusProgram = Link(fixed::kDofQuadVs, focusFs, &log);
    blurProgram = focusProgram ? Link(fixed::kDofQuadVs, blurFs, &log) : 0;
    halfProgram = blurProgram ? Link(fixed::kDofQuadVs, halfFs, &log) : 0;
    mixProgram = halfProgram ? Link(fixed::kDofQuadVs, mixFs, &log) : 0;
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
    mixStrength = glGetUniformLocation(mixProgram, "strength");
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
    blurStrength = glGetUniformLocation(blurProgram, "strength");
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


  // --- programs of the renderer itself --------------------------------------------------

  bool MakeUtilityPrograms(std::string *error) {
    using namespace fixed;  // gx_fixed_shaders.h
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
    copyTaps = glGetUniformLocation(copyProgram, "taps");
    copyStep = glGetUniformLocation(copyProgram, "lineStep");
    copyBox = glGetUniformLocation(copyProgram, "box");
    glUniform3f(copyTaps, 0.0f, -1.0f, 0.0f);  // no TV filter until a display copy asks for it
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
  // The blended vertices of the draw BlendDraw is on (an in-between picture's figure the game skins itself) to a
  // place of their own, for that draw only (blendAt): false when the pass's room is full (drawn unblended)
  bool PutBlendedVertices(const std::uint8_t *data, std::size_t bytes, std::uint32_t stride) override {
    const std::size_t at = (blendUsed + stride - 1) / stride * stride;
    if (at + bytes > blendCap)
      return false;
    if (blendMap) {
      std::memcpy(blendMap + at, data, bytes);
    } else {
      glBindBuffer(GL_ARRAY_BUFFER, blendVbo);
      void *dst = glMapBufferRange(GL_ARRAY_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(bytes),
                                   GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
      if (dst) {
        std::memcpy(dst, data, bytes);
        glUnmapBuffer(GL_ARRAY_BUFFER);
      } else {
        glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(at), static_cast<GLsizeiptr>(bytes), data);
      }
    }
    blendUsed = at + bytes;
    blendAt = static_cast<std::int64_t>(at);
    return true;
  }
  // ---- the GPU side of the BP writes and texture ops (GxCore::BpWrite, BindTexture, Forget) ----
  // the clear after a copy, on the EFB with the plain state (a skipped picture's display copy)
  void ClearAfterCopyPlain(std::uint32_t control, int x, int y, int w, int h) override {
    SaveAndPlainState();
    BindFbo(DrawFbo());
    ClearAfterCopy(control, x, y, w, h);
  }
  void GpuFinish() override { glFinish(); }
  void DeleteTexture(unsigned id) override { glDeleteTextures(1, &id); }
  // ---- the GPU side of the EFB copies (GxCore::EfbCopy) ----
  // the display copy's texture at w x h GameCube pixels (made anew when the size changes)
  unsigned DisplayTarget(int w, int h) override {
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
    return xfbTex;
  }
  // a copy's texture at w x h GameCube pixels (made anew when it has none or another size)
  void CopyTarget(Copy &c, int w, int h) override {
    if (!c.id || c.width != static_cast<unsigned>(w) || c.height != static_cast<unsigned>(h)) {
      if (c.id)
        glDeleteTextures(1, &c.id);
      glGenTextures(1, &c.id);
      glBindTexture(GL_TEXTURE_2D, c.id);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Hi(w), Hi(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      c.width = static_cast<unsigned>(w);
      c.height = static_cast<unsigned>(h);
    }
  }
  // the EFB region into the target with the copy shader (the format's conversion, half size, the copy filter)
  void CopyPass(const CopyParams &p) override {
    const int x = p.x, y = p.y, w = p.w, h = p.h;
    Resolve(Lo(x), Lo(y), Hi(x + w) - Lo(x), Hi(y + h) - Lo(y), p.depth);
    BindFbo(xfbFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.target, 0);
    SaveAndPlainState();
    // the copy draws over all of its target (the viewport is the texture)
    ForgetColor();
    glViewport(0, 0, Hi(p.tw), Hi(p.th));
    glUseProgram(copyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.depth ? efbDepth : efbColor);
    glBindSampler(0, 0);
    glUniform1i(copyMode, p.mode);
    glUniform4f(copyRect, Sf(x), Sf(y), Sf(w), Sf(h));
    glUniform2f(copySize, static_cast<float>(Lo(kEfbWidth)), static_cast<float>(Lo(kEfbHeight)));
    glUniform1i(copyHalf, p.half ? 1 : 0);
    glUniform1f(copyGain, p.gain);
    glUniform3f(copyTaps, p.taps[0], p.taps[1], p.taps[2]);
    glUniform1f(copyStep, p.step);
    glUniform1f(copyBox, p.box);
    glUniform2f(copyLimit, p.limit[0], p.limit[1]);
    glBindVertexArray(quadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (p.limited)
      glUniform2f(copyLimit, -1e9f, 1e9f);
    current = nullptr;
    BindFbo(DrawFbo());
  }
  // after a display copy: FXAA, Sharpen, the PSone demake, the colour, the CRT's pictures (each when on)
  void DisplayEffects(int pw, int ph, int y, int h, int w) override {
    Fxaa(pw, ph);
    Sharpen(pw, ph, y, h);
    Psx(pw, ph, w, h);
    Colour(pw, ph);
    Crt(pw, ph, w, h);
  }
  // the EFB bound with the plain state
  void PlainEfb() override {
    SaveAndPlainState();
    BindFbo(DrawFbo());
  }
  // ---- the GPU side of the in-between pass (GxCore::Interpolate) ----
  // before the pass: the store of its blended vertices; after it: their fence
  void InterpBegin(const Re4GxChunk &next) override {
    GlRenderer &m = *this;
    // the store for the pass's blended vertices: the next quarter of the mapped ring once the GPU is
    // through with it, else a fresh buffer (the last pass's may still be read by the GPU)
    if (!m.blendRingTried)
      m.BlendRingInit();
    if (m.blendMap) {
      m.blendPart = (m.blendPart + 1) % GlRenderer::kBlendParts;
      if (GLsync fence = m.blendFence[m.blendPart]) {
        if (glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0) == GL_TIMEOUT_EXPIRED)
          glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
        glDeleteSync(fence);
        m.blendFence[m.blendPart] = nullptr;
      }
      m.blendUsed = m.blendPart * GlRenderer::kBlendPart;
      m.blendCap = m.blendUsed + GlRenderer::kBlendPart;
    } else {
      if (!m.blendVbo)
        glGenBuffers(1, &m.blendVbo);
      m.blendCap = std::max<std::size_t>(next.vertexBytes, 65536);
      glBindBuffer(GL_ARRAY_BUFFER, m.blendVbo);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m.blendCap), nullptr, GL_STREAM_DRAW);
      m.blendUsed = 0;
    }
    m.blendAt = -1;
  }
  void InterpEnd() override {
    GlRenderer &m = *this;
    m.blendAt = -1;
    if (m.blendMap)
      m.blendFence[m.blendPart] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  }
  bool KeyProgramReady() const override { return keyProgram != nullptr; }
  bool MergedProgramReady() override { return MergedProgram(key) != nullptr; }
  // ---- the GPU side of the textures (GxCore::Upload) ----
  unsigned BeginTexture(unsigned, unsigned, unsigned) override {
    Touched(); // binds the new texture
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    return id;
  }
  void TextureLevel(unsigned, unsigned level, unsigned w, unsigned h, const std::uint8_t *rgba) override {
    glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA8, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  }
  void EndTexture(unsigned, unsigned levels) override {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels - 1));
  }
  // the clear after a copy (GxCore::ClearAfterCopy)
  void ClearRect(const GxClearRect &r) override {
    glEnable(GL_SCISSOR_TEST);
    glScissor(r.rect[0], r.rect[1], r.rect[2], r.rect[3]);
    glColorMask(r.color, r.color, r.color, r.alpha);
    glDepthMask(r.depth ? GL_TRUE : GL_FALSE);
    glClearColor(r.rgba[0], r.rgba[1], r.rgba[2], r.rgba[3]);
    glClearDepthf(r.z);
    GLbitfield mask = 0;
    if (r.color || r.alpha)
      mask |= GL_COLOR_BUFFER_BIT;
    if (r.depth)
      mask |= GL_DEPTH_BUFFER_BIT;
    if (mask)
      glClear(mask);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
  }
  // ---- Run's GPU side (GxCore::Run) ----
  void RunBegin() override {
    GlRenderer &m = *this;
    m.Touched();
    if (!m.interpPass && m.peekProgram)
      m.DeliverDepth();  // the depth maps the GPU finished since the last chunk go to the core
    if (m.stream)
      m.StreamChunkBegin();
    // this chunk's vertices and indices
    glBindVertexArray(m.vao);
  }
  void UploadChunk(const Re4GxChunk &chunk) override {
    GlRenderer &m = *this;
    if (m.RingUpload(chunk))
      return;
    m.drawVbo = m.vbo;
    m.vtxBase = m.idxBase = 0;
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, chunk.vertexBytes ? chunk.vertexBytes : 4, chunk.vertices, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    if (m.mergeMarks.empty()) {
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, chunk.indexCount ? chunk.indexCount * 2 : 4, chunk.indices, GL_STREAM_DRAW);
    } else {
      const std::size_t own = static_cast<std::size_t>(chunk.indexCount) * 2, moved = m.mergeIndices.size() * 2;
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(own + moved), nullptr, GL_STREAM_DRAW);
      glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(own), chunk.indices);
      glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(own), static_cast<GLsizeiptr>(moved),
                      m.mergeIndices.data());
    }
  }
  void RunState() override {
    GlRenderer &m = *this;
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
      if (m.mergeUbo)
        glBindBufferBase(GL_UNIFORM_BUFFER, kMergeBinding, m.mergeUbo);
    }
    m.TimerBegin();
    m.current = nullptr;
  }
  void RunEnd() override {
    GlRenderer &m = *this;
    m.TimerEnd();
    m.FenceAfterChunk();
  }
  void RunLeave() override {
    GlRenderer &m = *this;
    // leave GL as the app's other renderers expect it
    glBindVertexArray(0);
    for (GLuint unit = 0; unit < 8; unit++)
      glBindSampler(unit, 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
    m.Touched();
    m.SaveAndPlainState();
    m.BindFbo(0);
  }

  // GxCore (the calls of GxRenderer, defined below)
  bool InitHalves(int halves, std::string *error) override;
  void RescaleHalves(int halves) override;
  void HandOverCopies(GxCore &to) override;
  bool TiledMsaa() const override;
  unsigned Xfb(unsigned *width, unsigned *height) const override;
  bool Present(int x, int y, int w, int h, unsigned top, unsigned rows) override;
  unsigned EfbColor() const override;
  bool ReadXfb(std::vector<std::uint8_t> *rgba, unsigned *width, unsigned *height) override {
    if (!haveXfb || !xfbWidth || !xfbHeight)
      return false;
    const unsigned w = static_cast<unsigned>(Hi(static_cast<int>(xfbWidth))),
                   h = static_cast<unsigned>(Hi(static_cast<int>(xfbHeight)));
    rgba->resize(static_cast<std::size_t>(w) * h * 4);
    if (!readFbo)
      glGenFramebuffers(1, &readFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ShownXfb(), 0);
    glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA, GL_UNSIGNED_BYTE, rgba->data());
    BindFbo(0);
    Touched();
    *width = w;
    *height = h;
    return true;
  }
  bool ReadPresented(int screenW, int screenH, int x, int y, int w, int h, unsigned top, unsigned rows,
                     std::vector<std::uint8_t> *out) override {
    if (!haveXfb || screenW < 1 || screenH < 1)
      return false;
    if (!screenTex || screenTexW != screenW || screenTexH != screenH) {
      if (screenTex)
        glDeleteTextures(1, &screenTex);
      glGenTextures(1, &screenTex);
      glBindTexture(GL_TEXTURE_2D, screenTex);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, screenW, screenH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      screenTexW = screenW;
      screenTexH = screenH;
    }
    if (!readFbo)
      glGenFramebuffers(1, &readFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTex, 0);
    glViewport(0, 0, screenW, screenH);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    Present(x, y, w, h, top, rows);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(screenW) * screenH * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
    glReadPixels(0, 0, screenW, screenH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    BindFbo(0);
    Touched();
    // the screen's rows go bottom up in GL
    out->resize(rgba.size());
    const std::size_t row = static_cast<std::size_t>(screenW) * 4;
    for (int r = 0; r < screenH; r++)
      std::memcpy(out->data() + static_cast<std::size_t>(r) * row, rgba.data() + static_cast<std::size_t>(screenH - 1 - r) * row, row);
    return true;
  }
  // the pictures' read-back (ReadXfb, ReadPresented): a framebuffer of their own, the screen's texture
  GLuint readFbo = 0, screenTex = 0;
  int screenTexW = 0, screenTexH = 0;
  bool BaseVertex() const override;
  bool SetProgramCache(const std::string &folder) override;
  bool Prewarm(double budgetMs) override;
  void Release() override;
  void ApplyTestEnvironment() override;
  std::string ApiInfo() const override {
    const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
    const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
    return std::string(version ? version : "OpenGL ES") + ", " + (renderer ? renderer : "?");
  }
  std::uintptr_t ProgramTag() const override { return reinterpret_cast<std::uintptr_t>(keyProgram); }
};

std::unique_ptr<GxCore> MakeGlCore() { return std::unique_ptr<GxCore>(new GlRenderer()); }

void GlRenderer::ApplyTestEnvironment() {
  const char *tv = getenv("RE4_GX_TV_FILTER");  // the TV filter from the start (gx_chunk_render, tests)
  tvFilter = tv && *tv == '1';
  const char *psx = getenv("RE4_GX_PSX");  // the PSone demake from the start (gx_chunk_render, tests)
  psxOn = psx && *psx == '1';
  const char *flares = getenv("RE4_GX_FLARES");  // the lens flares from the start (gx_chunk_render, tests)
  flaresOn = flares && *flares == '1';
  const char *halos = getenv("RE4_GX_FLARE_HALOS");  // 0: the lens flares without the halos (gx_chunk_render, tests)
  flareHalos = !(halos && *halos == '0');
  if (const char *limit = getenv("RE4_GX_MAX_PROGRAMS")) {
    const long n = std::strtol(limit, nullptr, 10);
    if (n >= 4) {
      maxPrograms = static_cast<std::size_t>(n);
      maxMerged = static_cast<std::size_t>(n);
    }
  }
}

bool GlRenderer::InitHalves(int halves, std::string *error) {
  GlRenderer &m = *this;
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
    m.drawBaseVertex = reinterpret_cast<GlRenderer::DrawBaseVertex>(f);
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
  GlRenderer::MakeEfbTextures(w, h, &m.efbColor, &m.efbDepth);
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
    GxRenderer::Limits(&maxSamples, &maxAnisotropy);
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
  m.drawVbo = m.vbo;
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
  // merged draw calls (SetMerge): RE4_GX_MERGE=1 turns them on; the block's own buffer (without the stream)
  if (const char *merge = std::getenv("RE4_GX_MERGE"))
    m.mergeOn = *merge == '1';
  glGenBuffers(1, &m.mergeUbo);
  glBindBuffer(GL_UNIFORM_BUFFER, m.mergeUbo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(MergeBlock), nullptr, GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_UNIFORM_BUFFER, kMergeBinding, m.mergeUbo);
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

void GlRenderer::HandOverCopies(GxCore &to) {
  if (to.api != GxApi::kGl)
    return;
  GlRenderer &m = *this, &n = static_cast<GlRenderer &>(to);
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

void GlRenderer::RescaleHalves(int halves) {
  GlRenderer &m = *this;
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
  // the smoothed and the sharpened copies (and the CRT's, the colour filter's) stay shown until the next display copy
  // makes them anew at the new size (FilterFxaa / FilterSharpen compare their size): dropped here, the refreshes in
  // between showed the picture without them (0.99.10, as the Vulkan renderer, whose CRT flashed white all round)
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



unsigned GlRenderer::Xfb(unsigned *width, unsigned *height) const {
  if (width)
    *width = this->xfbWidth;
  if (height)
    *height = this->xfbHeight;
  return this->haveXfb ? this->ShownXfb() : 0;
}

bool GlRenderer::Present(int x, int y, int w, int h, unsigned top, unsigned rows) {
  GlRenderer &m = *this;
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
  const float lines = static_cast<float>(m.xfbHeight ? m.xfbHeight : 1);
  float r0 = 0.0f, r1 = 1.0f;
  if (rows != 0 && top + rows <= m.xfbHeight) {
    r0 = static_cast<float>(top) / lines;
    r1 = static_cast<float>(rows) / lines;
  }
  if (m.crtLevel > 0 && m.crtReady && m.crtProgram) {
    m.CrtPresent(w, h, r0, r1);  // the CRT (0.99.9)
  } else {
    glUseProgram(m.presentProgram);
    glUniform2f(m.presentRows, r0, r1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m.ShownXfb());
    glBindSampler(0, 0);
  }
  glBindVertexArray(m.quadVao);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  m.Touched();
  m.PresentTimerEnd();
  return true;
}

unsigned GlRenderer::EfbColor() const { return this->efbColor; }

bool GlRenderer::TiledMsaa() const { return this->tiledMsaa; }

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

bool GlRenderer::BaseVertex() const { return this->drawBaseVertex != nullptr; }

bool GlRenderer::SetProgramCache(const std::string &folder) {
  GlRenderer &m = *this;
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

bool GlRenderer::Prewarm(double budgetMs) {
  GlRenderer &m = *this;
  const auto start = std::chrono::steady_clock::now();
  double ms = 0.0;
  bool made = false;
  // the programs of merged draw calls the chunks so far wanted (SetMerge)
  while (!m.mergeWanted.empty() && !(made && ms >= budgetMs)) {
    const ShaderKey k = m.mergeWanted.back();
    m.mergeWanted.pop_back();
    m.ProgramFor(k, true);
    made = true;
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  if (m.programCache.empty() || (made && ms >= budgetMs))
    return !m.mergeWanted.empty();
  if (!m.aheadRead) {
    m.ReadAhead();
    if (!m.ahead.empty())
      GXLOG("GX programs ahead of their first draw: %zu keys of room r%03x", m.ahead.size(), m.room);
  }
  if (m.aheadNext >= m.ahead.size())
    return false;
  const std::uint32_t centroid = m.options.centroid && m.samples > 1 ? 1 : 0;
  while (m.aheadNext < m.ahead.size() && !(made && ms >= budgetMs)) {
    GlRenderer::AheadKey &a = m.ahead[m.aheadNext];
    if (a.listed)
      a.key.centroid = centroid;  // the list holds each program once, for every MSAA setting
    // another MSAA setting's program, the PSone demake's (bit 1) while it is off, or drawn already (until 0.99.5 the
    // whole field against bit 0: with the PSone demake on its programs were never made ahead)
    if ((a.key.centroid & 1) != centroid || ((a.key.centroid & 2) && !m.psxOn) || m.programs.count(a.key)) {
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

void GlRenderer::Release() {
  GlRenderer &m = *this;
  if (m.roomDirty)
    m.SaveRoom();
  m.DropTextures();
  for (auto &c : m.copies)
    glDeleteTextures(1, &c.second.id);
  m.copies.clear();
  for (auto &p : m.programs)
    if (p.second.id)
      glDeleteProgram(p.second.id);
  m.programs.clear();
  for (auto &p : m.mergedPrograms)
    if (p.second.id)
      glDeleteProgram(p.second.id);
  m.mergedPrograms.clear();
  m.mergeWanted.clear();
  for (auto &s : m.samplers)
    glDeleteSamplers(1, &s.second);
  m.samplers.clear();
  glDeleteProgram(m.restoreProgram);
  const GLuint spareTextures[] = {m.spareColor, m.spareDepth};
  glDeleteTextures(2, spareTextures);
  glDeleteFramebuffers(1, &m.spareFbo);
  const GLuint textures[] = {m.efbColor, m.efbDepth, m.xfbTex, m.whiteTex};
  glDeleteTextures(4, textures);
  if (m.readFbo)
    glDeleteFramebuffers(1, &m.readFbo);
  if (m.screenTex)
    glDeleteTextures(1, &m.screenTex);
  const GLuint fbos[] = {m.efbFbo, m.xfbFbo, m.msaaFbo};
  glDeleteFramebuffers(3, fbos);
  const GLuint renderbuffers[] = {m.msaaColor, m.msaaDepth};
  glDeleteRenderbuffers(2, renderbuffers);
  const GLuint buffers[] = {m.vbo, m.ibo, m.pxUbo};
  glDeleteBuffers(3, buffers);
  if (m.mergeUbo)
    glDeleteBuffers(1, &m.mergeUbo);
  m.mergeUbo = 0;
  if (m.blendVbo)
    glDeleteBuffers(1, &m.blendVbo);
  m.blendVbo = 0;
  m.BlendRingRelease();
  m.RingRelease(m.vring, GL_ARRAY_BUFFER);
  m.RingRelease(m.iring, GL_ELEMENT_ARRAY_BUFFER);
  glDeleteBuffers(kXfParts, m.xfUbo);
  m.StreamRelease();
  m.FenceRelease();
  m.PeekRelease();
  m.FxaaRelease();
  m.SharpenRelease();
  m.PsxRelease();
  m.CrtRelease();
  m.FlareRelease();
  if (m.timerOn) {
    glDeleteQueries(GlRenderer::TimerPool::kQueries, m.chunkTimer.query);
    glDeleteQueries(GlRenderer::TimerPool::kQueries, m.presentTimer.query);
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
}

} // namespace re4gx
