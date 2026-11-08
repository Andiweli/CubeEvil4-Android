// The Vulkan renderer of the core mode (port/docs/vulkan-bauplan.md, E3): the GPU side of GxCore on Vulkan, next to
// GlRenderer (gx_gl.cpp). The GX emulation itself is GxCore's (gx_core.cpp); this file draws what it asks for.
//
// Frames: kFrames command buffers in flight, each with its own host-visible blocks (uniforms, vertices, texture
// uploads), descriptor pools and a list of objects to destroy once the GPU is through with it. A frame is submitted
// at Present (the app), and when the CPU reads a picture (tests, screenshots). New textures are copied in the
// frame's upload command buffer, which runs before its draws. The EFB is a colour and a depth image drawn in render
// passes that load what they held; an EFB copy ends the pass, reads the EFB as a texture into the copy's image and
// the next draw begins the pass again. The EFB's rows lie in memory as in GL (row 0 = the GameCube's row 0).
//
// Not yet (E4/E5): MSAA, copies read through a palette, the depth map for GXPeekZ, a change of the scale while
// running, FXAA, Sharpen, the PSone pass, the lens flares' pass, the autofocus, the pipeline cache on disk.
#include "gx_core.h"
#include "gx_fixed_shaders.h"
#include "gx_vk_shader.h"
#include "vk_context.h"
#include "vk_memory.h"
#include "vk_shader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <chrono>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <unistd.h>

#ifdef __ANDROID__
#include <android/native_window.h>
#endif

namespace re4gx {

namespace {

using namespace re4vk;

void VkLogLine(const char *line) { LogTo("RE4VK", "%s", line); }

constexpr int kFrames = 2;
constexpr VkDeviceSize kBlockBytes = VkDeviceSize(8) << 20;
constexpr std::uint32_t kTexSetsPerPool = 512;

// the uniform blocks of the GX shaders in set 0 (gx_vk_shader.h) and their sizes
constexpr int kUboCount = 5;
constexpr VkDeviceSize kUboRange[kUboCount] = {
    VkDeviceSize(kXfPartRow[1] - kXfPartRow[0]) * 16,  // 0 XFM
    sizeof(PixelBlock),                                // 1 PX
    VkDeviceSize(kXfPartRow[2] - kXfPartRow[1]) * 16,  // 2 XFP
    VkDeviceSize(kXfPartRow[3] - kXfPartRow[2]) * 16,  // 3 XFL
    sizeof(MergeBlock),                                // 4 XFMM
};
// what a draw's uploads can take at most (DrawProgram keeps that much room in the frame's uniform block)
constexpr VkDeviceSize kDrawUniformsMost = 64 * 1024;

struct Image {
  VkImage image = VK_NULL_HANDLE;
  ImageHeap::Allocation memory;  // in the renderer's ImageHeap
  VkImageView view = VK_NULL_HANDLE;    // sampled (a depth image: its depth only)
  VkImageView attach = VK_NULL_HANDLE;  // as an attachment, when it differs (depth and stencil)
  VkFramebuffer fb = VK_NULL_HANDLE;    // the target of a copy pass (copies, the XFB) or of the screen pass
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  unsigned width = 0, height = 0, levels = 1;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// A host-visible buffer filled from the start (one frame's uniforms, vertices, uploads).
struct Block {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  std::uint8_t *map = nullptr;
  VkDeviceSize size = 0, used = 0;
  VkDescriptorSet uboSet = VK_NULL_HANDLE;  // uniform blocks: set 0 on this buffer
};

struct Frame {
  VkCommandPool cmdPool = VK_NULL_HANDLE;
  VkCommandBuffer upload = VK_NULL_HANDLE, draw = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  bool pending = false;  // submitted, its fence not waited for yet
  std::uint64_t serial = 0;  // of the recording it holds (VkRenderer::serial)
  bool uploads = false;  // the upload command buffer has commands
  std::vector<Block> uniform, vertex, staging;
  std::vector<VkDescriptorPool> pools;  // combined image samplers (the GX textures, the fixed passes)
  std::size_t pool = 0;
  std::uint32_t poolSets = 0;
  std::unordered_map<std::uint64_t, VkDescriptorSet> texSets;
  std::vector<std::function<void()>> trash;  // destroyed once the GPU is through with the frame
};

struct Program {
  VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE, merged = VK_NULL_HANDLE;
  bool ok = false, mergedTried = false;
  std::uint32_t lastUse = 0;
  ShaderKey key{};  // (its pipelines go into the room's list by it)
};

// what a GX pipeline is made of (memset to 0 first: compared and hashed as bytes)
struct PipeKey {
  const Program *program;
  std::uint32_t layout, stride;
  std::uint8_t merged, topology, depthTest, depthWrite, depthFunc, colorWrite, alphaWrite, blend;
  std::uint8_t eqColor, eqAlpha, srcColor, dstColor, srcAlpha, dstAlpha, cull, pad;
};

struct PipeKeyHash {
  std::size_t operator()(const PipeKey &k) const { return static_cast<std::size_t>(Fnv(&k, sizeof(k))); }
};
struct PipeKeyEqual {
  bool operator()(const PipeKey &a, const PipeKey &b) const { return std::memcmp(&a, &b, sizeof(a)) == 0; }
};

// a fixed shader's pipeline (copy, display, clear)
struct Fixed {
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkFixedLayout constants;
};

const char kClearVs[] = "#version 450\n"
                        "layout(push_constant) uniform P { vec4 color; float z; };\n"
                        "void main() {\n"
                        "  vec2 p = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));\n"
                        "  gl_Position = vec4(p * 2.0 - 1.0, z, 1.0);\n"
                        "}\n";
const char kClearFs[] = "#version 450\n"
                        "layout(push_constant) uniform P { vec4 color; float z; };\n"
                        "layout(location = 0) out vec4 o;\n"
                        "void main() { o = color; }\n";

// the screen's picture (Present): the box (clip space, y down), the display's turn (a 2 x 2 matrix), the XFB's rows
[[maybe_unused]] const char kSwapVs[] = "#version 450\n"
                       "layout(push_constant) uniform P { vec4 rect; vec4 rot; vec2 rows; };\n"
                       "layout(location = 0) out vec2 vUv;\n"
                       "void main() {\n"
                       "  vec2 p = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));\n"
                       "  vUv = p;\n"
                       "  vec2 q = mix(rect.xy, rect.zw, p);\n"
                       "  gl_Position = vec4(rot.x * q.x + rot.y * q.y, rot.z * q.x + rot.w * q.y, 0.0, 1.0);\n"
                       "}\n";
[[maybe_unused]] const char kSwapFs[] = "#version 450\n"
                       "layout(push_constant) uniform P { vec4 rect; vec4 rot; vec2 rows; };\n"
                       "layout(set = 0, binding = 0) uniform sampler2D src;\n"
                       "layout(location = 0) in vec2 vUv;\n"
                       "layout(location = 0) out vec4 o;\n"
                       "void main() { o = vec4(texture(src, vec2(vUv.x, rows.x + vUv.y * rows.y)).rgb, 1.0); }\n";

// The SPIR-V of every shader the device compiled (E8): <RE4_GX_PROGRAM_CACHE>/vk-spirv.bin, records of the shader
// text's hash (FNV-1a over the compiler, the stage and the text), the word count and the words, appended as they
// come; the index (hash to place) in memory, the words read when wanted. At most kMaxBytes on disk (a GX shader is
// about 15 KB; a whole game's worth of them would be some hundred MB): beyond that, compiled as before. A record cut
// off at the end (the app ended while writing) is cut away. Shared by the renderers of the process.
struct SpirvStore {
  static constexpr long kMaxBytes = 160L << 20;
  std::mutex lock;
  std::unordered_map<std::uint64_t, std::pair<long, std::uint32_t>> index;
  FILE *file = nullptr;
  std::string path;
  long end = 0;
  bool full = false;
  void Open(const std::string &p) {
    std::lock_guard<std::mutex> g(lock);
    if (p == path)
      return;
    if (file)
      std::fclose(file);
    index.clear();
    path = p;
    full = false;
    end = 0;
    file = std::fopen(p.c_str(), "r+b");
    char magic[8] = {};
    if (!file || std::fread(magic, 8, 1, file) != 1 || std::memcmp(magic, "RE4SPV01", 8) != 0) {
      if (file)
        std::fclose(file);
      file = std::fopen(p.c_str(), "w+b");
      if (!file)
        return;
      std::fwrite("RE4SPV01", 8, 1, file);
      std::fflush(file);
      end = 8;
      return;
    }
    end = 8;
    for (;;) {
      std::uint32_t head[3];
      if (std::fread(head, sizeof(head), 1, file) != 1)
        break;
      const long words = end + 12;
      if (head[2] == 0 || head[2] > (1u << 20) || std::fseek(file, static_cast<long>(head[2]) * 4, SEEK_CUR) != 0 ||
          std::ftell(file) != words + static_cast<long>(head[2]) * 4)
        break;
      // (fseek past the end succeeds: a record cut off shows in the length)
      long size = 0;
      {
        const long at = std::ftell(file);
        std::fseek(file, 0, SEEK_END);
        size = std::ftell(file);
        std::fseek(file, at, SEEK_SET);
      }
      if (words + static_cast<long>(head[2]) * 4 > size)
        break;
      index[head[0] | static_cast<std::uint64_t>(head[1]) << 32] = {words, head[2]};
      end = words + static_cast<long>(head[2]) * 4;
    }
    std::fflush(file);
    if (ftruncate(fileno(file), end) != 0) {
    }  // (the end of a cut-off record goes)
  }
  bool Find(std::uint64_t h, std::vector<std::uint32_t> *out) {
    std::lock_guard<std::mutex> g(lock);
    auto it = index.find(h);
    if (it == index.end() || !file)
      return false;
    out->resize(it->second.second);
    return std::fseek(file, it->second.first, SEEK_SET) == 0 &&
           std::fread(out->data(), 4, out->size(), file) == out->size();
  }
  void Put(std::uint64_t h, const std::vector<std::uint32_t> &w) {
    std::lock_guard<std::mutex> g(lock);
    if (!file || full || w.empty() || index.count(h))
      return;
    const long bytes = 12 + static_cast<long>(w.size()) * 4;
    if (end + bytes > kMaxBytes) {
      full = true;
      return;
    }
    const std::uint32_t head[3] = {static_cast<std::uint32_t>(h), static_cast<std::uint32_t>(h >> 32),
                                   static_cast<std::uint32_t>(w.size())};
    if (std::fseek(file, end, SEEK_SET) != 0 || std::fwrite(head, sizeof(head), 1, file) != 1 ||
        std::fwrite(w.data(), 4, w.size(), file) != w.size()) {
      full = true;  // (a full storage: no more)
      return;
    }
    std::fflush(file);
    index[h] = {end + 12, static_cast<std::uint32_t>(w.size())};
    end += bytes;
  }
  std::size_t Count() {
    std::lock_guard<std::mutex> g(lock);
    return index.size();
  }
};
SpirvStore &Spirv() {
  static SpirvStore store;
  return store;
}

// a file whole into memory (at most `limit` bytes; empty when it is not there)
std::vector<char> ReadFileBytes(const std::string &path, std::size_t limit) {
  std::vector<char> data;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return data;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size > 0 && static_cast<std::size_t>(size) <= limit) {
    data.resize(static_cast<std::size_t>(size));
    if (std::fread(data.data(), 1, data.size(), f) != data.size())
      data.clear();
  }
  std::fclose(f);
  return data;
}
// Whether the driver's pipeline cache of the last run is this device's and this driver's: its header
// (VkPipelineCacheHeaderVersionOne: length, version 1, vendor, device, the driver's pipelineCacheUUID) against the
// device's properties. The driver must refuse another one itself, but since 0.99.4 the file outlives Android and driver
// updates (it is no longer in the cache folder Android empties, CoreActivity / ShaderCache.java), and drivers have
// crashed on foreign data before: a file that does not fit is left out (and replaced at the next save). `why`: the
// reason for the log.
bool PipelineCacheFits(const std::vector<char> &data, const VkPhysicalDeviceProperties &p, const char **why) {
  struct Head {
    std::uint32_t length, version, vendor, device;
    std::uint8_t uuid[VK_UUID_SIZE];
  } h{};
  if (data.size() < sizeof(h)) {
    *why = "too short for its header";
    return false;
  }
  std::memcpy(&h, data.data(), sizeof(h));
  if (h.length < sizeof(h) || h.length > data.size() || h.version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
    *why = "no header of version 1";
    return false;
  }
  if (h.vendor != p.vendorID || h.device != p.deviceID) {
    *why = "another GPU";
    return false;
  }
  if (std::memcmp(h.uuid, p.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
    *why = "another driver (an update)";
    return false;
  }
  return true;
}

// bytes to a file through a temporary one (never half written)
bool WriteFileBytes(const std::string &path, const std::vector<char> &data) {
  const std::string temp = path + ".tmp";
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return false;
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  if (std::fclose(f) != 0 || !ok) {
    std::remove(temp.c_str());
    return false;
  }
  return std::rename(temp.c_str(), path.c_str()) == 0;
}

// The device the renderers of a run share (vulkan-bauplan.md E4): new picture options make a new renderer
// (core_mode.cpp) while the one before still exists; on the same device it takes that one's EFB copies
// (HandOverCopies), with the same image memory and pipeline cache. The device goes with the last renderer.
struct SharedDevice {
  Context vk;
  ImageHeap heap;
  VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  bool diskMerged = false;  // the pipeline cache of the last run merged in (SetProgramCache)
  ~SharedDevice() {
    if (pipelineCache && vk.device)
      vkDestroyPipelineCache(vk.device, pipelineCache, nullptr);
    heap.Destroy();
    vk.Destroy();
  }
};

std::shared_ptr<SharedDevice> TheDevice() {
  static std::weak_ptr<SharedDevice> held;
  std::shared_ptr<SharedDevice> d = held.lock();
  if (!d) {
    d = std::make_shared<SharedDevice>();
    held = d;
  }
  return d;
}

// the overlay over the screen's picture (the minimap, GxOverlay): its square (clip space, y down) and the display's
// turn, the vertices in the square's clip space with y up
const char kOverlayVs[] = "#version 450\n"
                          "layout(push_constant) uniform P { vec4 rect; vec4 rot; };\n"
                          "layout(location = 0) in vec2 aPos;\n"
                          "layout(location = 1) in vec4 aCol;\n"
                          "layout(location = 0) out vec4 vCol;\n"
                          "void main() {\n"
                          "  vCol = aCol;\n"
                          "  vec2 p = mix(rect.xy, rect.zw, vec2(aPos.x, -aPos.y) * 0.5 + 0.5);\n"
                          "  gl_Position = vec4(rot.x * p.x + rot.y * p.y, rot.z * p.x + rot.w * p.y, 0.0, 1.0);\n"
                          "}\n";
const char kOverlayFs[] = "#version 450\n"
                          "layout(location = 0) in vec4 vCol;\n"
                          "layout(location = 0) out vec4 o;\n"
                          "void main() { o = vCol; }\n";

// MSAA: the multisampled EFB depth's sample 0 into the resolved depth image (ResolveDepth; what GL's
// glBlitFramebuffer of the depth gives)
const char kDepthResolveVs[] = "#version 450\n"
                               "void main() {\n"
                               "  vec2 p = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));\n"
                               "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                               "}\n";
const char kDepthResolveFs[] = "#version 450\n"
                               "layout(set = 0, binding = 0) uniform sampler2DMS src;\n"
                               "void main() { gl_FragDepth = texelFetch(src, ivec2(gl_FragCoord.xy), 0).r; }\n";

} // namespace

struct VkRenderer final : GxCore {
  VkRenderer() : GxCore(GxApi::kVulkan), shared(TheDevice()), vk(shared->vk), heap(shared->heap) {}
  ~VkRenderer() override { Release(); }

  std::shared_ptr<SharedDevice> shared;
  Context &vk;
  ImageHeap &heap;
  VkDevice dev = VK_NULL_HANDLE;
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  VkRenderPass efbPass = VK_NULL_HANDLE, copyPass = VK_NULL_HANDLE, screenPass = VK_NULL_HANDLE;
  Image efbColor, efbDepth;
  VkFramebuffer efbFb = VK_NULL_HANDLE;
  // MSAA (E4): the EFB drawn into multisampled images; their colour resolved into efbColor at each EFB pass's end
  // (a resolve attachment, on the tiles of a tiled GPU), their depth into efbDepth when something reads it
  // (ResolveDepth: a copy of the depth, the depth map)
  Image efbMsColor, efbMsDepth;
  // the display copy's filters (DisplayEffects): their pictures and pipelines
  Image fxaaImg, sharpImg, psxImg, psxPrevImg;
  // the lens flares (FlarePass): the slots' visibility, this picture's and the one before
  Image flareVis[2];
  Fixed flareVisFx, flareFx;
  int flareVisCur = 0;
  // the gameplay autofocus (Autofocus): the focus (this picture's and the one before), the blur at the full or at
  // half the resolution, their pipelines; the EFB's colour alone (the mix without MSAA)
  Image focusImg[2], blurImg, halfImg;
  // the overlay over the screen's picture (SetOverlay: the app's minimap), its pipelines (triangles, lines)
  GxOverlay overlay;
  VkPipeline overlayPipe[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE}, overlayShotPipe[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkPipelineLayout overlayLayout = VK_NULL_HANDLE;
  bool overlayFailed = false;
  Fixed focusFx, blurFx, halfFx, mixFx, dofBackFx;
  VkRenderPass efbColorPass = VK_NULL_HANDLE;
  VkFramebuffer efbColorFb = VK_NULL_HANDLE;
  Fixed fxaaFx, sharpFx, psxFx;
  bool psxPrevValid = false;
  int psxGridW = 0, psxGridH = 0;
  VkRenderPass depthResolvePass = VK_NULL_HANDLE;
  VkFramebuffer depthResolveFb = VK_NULL_HANDLE;
  Fixed depthResolve;
  bool depthResolved = true;
  unsigned depthResolveCount = 0;  // (the log at the end)
  VkDescriptorSetLayout uboLayout = VK_NULL_HANDLE, texLayout = VK_NULL_HANDLE;
  VkPipelineLayout gxLayout = VK_NULL_HANDLE;
  VkDescriptorPool uboPool = VK_NULL_HANDLE;
  VkPipelineCache pipelineCache = VK_NULL_HANDLE;
  VkSampler linearClamp = VK_NULL_HANDLE, nearestClamp = VK_NULL_HANDLE;
  Fixed copy, present, palette, clearFixed[8];
  std::unordered_map<ShaderKey, Program, KeyHash, KeyEqual> programs;
  std::unordered_map<PipeKey, VkPipeline, PipeKeyHash, PipeKeyEqual> pipelines;
  std::unordered_map<std::uint64_t, VkSampler> samplers;
  std::vector<std::unique_ptr<Image>> images;  // a texture's handle is its index + 1
  std::vector<unsigned> freeIds;
  Frame frames[kFrames];
  int cur = 0;
  bool recording = false, continuing = false;
  enum { kNoPass, kEfbPass, kOtherPass } pass = kNoPass;
  bool ready = false;

  // what the draw command buffer has bound
  DrawState state;
  const Program *keyProgram = nullptr;
  VkPipeline boundPipeline = VK_NULL_HANDLE;
  VkDescriptorSet boundUbo = VK_NULL_HANDLE, boundTex = VK_NULL_HANDLE;
  std::uint32_t boundOffsets[kUboCount] = {};
  VkBuffer boundVb = VK_NULL_HANDLE, boundIb = VK_NULL_HANDLE;
  VkDeviceSize boundVbOffset = 0, boundIbOffset = 0;
  VkDrawConstants boundConstants{};
  bool constantsValid = false, scissorValid = false;
  VkRect2D boundScissor{};
  float boundLineWidth = -1.0f;
  // where each uniform block lies now: the frame's uniform block and the offset per binding
  std::size_t uboBlock = 0;
  std::uint32_t uboOffset[kUboCount] = {};
  // this chunk's vertices and indices, an in-between draw's blended vertices
  VkBuffer vtxBuffer = VK_NULL_HANDLE, idxBuffer = VK_NULL_HANDLE, blendBuffer = VK_NULL_HANDLE;
  VkDeviceSize vtxBase = 0, idxBase = 0;
  std::int64_t blendAt = -1;
  unsigned xfbId = 0;
  // read-back (ReadXfb, ReadPresented)
  Block readback;
  Image screen;
  unsigned pipelineLog = 0;
  // the screen (Android): the window, the app's screen size (as drawGx counts), its surface and swapchain
  void *window = nullptr;
  int screenW = 0, screenH = 0;
  VkSurfaceKHR surfaceKhr = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat swapFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D swapExtent{};
  VkSurfaceTransformFlagBitsKHR swapTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  std::vector<VkImage> swapImages;
  std::vector<VkImageView> swapViews;
  std::vector<VkFramebuffer> swapFbs;
  std::vector<VkSemaphore> renderSem;  // per swapchain image: the picture done (the present waits)
  VkSemaphore acquireSem[kFrames] = {};  // per frame: the image acquired (the frame's submit waits)
  VkRenderPass swapPass = VK_NULL_HANDLE;
  Fixed swapPresent;
  bool swapStale = false;
  // "suboptimal" from acquire or present: the surface is looked at the next picture (swapCheck); made anew only when
  // its turn or size changed (swapSeenTurn, swapSeenExtent: as at the last making); after kSuboptimalLooks looks in
  // a row without a change, no more looks (an answer the swapchain cannot change)
  bool swapCheck = false;
  unsigned swapSeenTurn = 0, suboptimalLooks = 0, suboptimalSkipped = 0;
  unsigned presentsSinceLook = 0;  // the surface's turn looked at once a second as well (LookAtTurn)
  VkExtent2D swapSeenExtent{};
  static constexpr unsigned kSuboptimalLooks = 8;
  unsigned swapMade = 0;          // swapchains made (the timing line)
  std::string swapLogged;         // the screen's line as last logged
  // every recording a number; done: the highest whose fence was seen signalled
  std::uint64_t serial = 0, done = 0;
  // the depth map for GXPeekZ (SnapshotDepth): its image, the pass, two read-back places
  Image peekImage;
  Fixed peek;
  bool peekReady = false;
  struct PeekSlot {
    Block buffer;
    std::uint64_t serial = 0;  // recorded in; 0: free
    std::uint32_t picture = 0;
  } peekSlot[2];

  // ---- objects ----
  bool Check(VkResult r, const char *what) {
    if (r == VK_SUCCESS)
      return true;
    static unsigned lines = 0;
    if (lines++ < 20)
      GXLOG("Vulkan: %s: %s", what, ResultName(r));
    return false;
  }
  Frame &F() { return frames[cur]; }

  bool MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Block *b) {
    auto bci = Make<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bci.size = size;
    bci.usage = usage;
    if (!Check(vkCreateBuffer(dev, &bci, nullptr, &b->buffer), "vkCreateBuffer"))
      return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(dev, b->buffer, &req);
    auto mai = Make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = vk.MemoryType(req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mai.memoryTypeIndex == ~0u || !Check(vkAllocateMemory(dev, &mai, nullptr, &b->memory), "vkAllocateMemory")) {
      vkDestroyBuffer(dev, b->buffer, nullptr);
      b->buffer = VK_NULL_HANDLE;
      return false;
    }
    vkBindBufferMemory(dev, b->buffer, b->memory, 0);
    void *map = nullptr;
    vkMapMemory(dev, b->memory, 0, VK_WHOLE_SIZE, 0, &map);
    b->map = static_cast<std::uint8_t *>(map);
    b->size = size;
    b->used = 0;
    return true;
  }
  void FreeBuffer(Block &b) {
    if (b.uboSet)
      vkFreeDescriptorSets(dev, uboPool, 1, &b.uboSet);
    if (b.buffer)
      vkDestroyBuffer(dev, b.buffer, nullptr);
    if (b.memory)
      vkFreeMemory(dev, b.memory, nullptr);
    b = Block();
  }
  // the uniform blocks' set 0 on a new block's buffer
  bool MakeUboSet(Block &b) {
    auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = uboPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &uboLayout;
    if (!Check(vkAllocateDescriptorSets(dev, &ai, &b.uboSet), "vkAllocateDescriptorSets (uniforms)"))
      return false;
    VkDescriptorBufferInfo info[kUboCount];
    VkWriteDescriptorSet w[kUboCount];
    for (int i = 0; i < kUboCount; i++) {
      info[i] = {b.buffer, 0, kUboRange[i]};
      w[i] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
      w[i].dstSet = b.uboSet;
      w[i].dstBinding = static_cast<std::uint32_t>(i);
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
      w[i].pBufferInfo = &info[i];
    }
    vkUpdateDescriptorSets(dev, kUboCount, w, 0, nullptr);
    return true;
  }
  // room for `bytes` in the frame's blocks of a kind: its block and offset (a new block when the last is full)
  bool Alloc(std::vector<Block> &blocks, VkDeviceSize bytes, VkDeviceSize align, VkBufferUsageFlags usage,
             std::size_t *block, VkDeviceSize *offset, bool ubo = false) {
    if (!blocks.empty()) {
      Block &b = blocks.back();
      const VkDeviceSize at = (b.used + align - 1) / align * align;
      if (at + bytes <= b.size) {
        b.used = at + bytes;
        *block = blocks.size() - 1;
        *offset = at;
        return true;
      }
    }
    Block b;
    if (!MakeBuffer(std::max(kBlockBytes, bytes * 2), usage, &b) || (ubo && !MakeUboSet(b)))
      return false;
    b.used = bytes;
    blocks.push_back(b);
    *block = blocks.size() - 1;
    *offset = 0;
    return true;
  }

  Image *ImageOf(unsigned id) { return id && id <= images.size() ? images[id - 1].get() : nullptr; }
  unsigned NewId(std::unique_ptr<Image> img) {
    if (!freeIds.empty()) {
      const unsigned id = freeIds.back();
      freeIds.pop_back();
      images[id - 1] = std::move(img);
      return id;
    }
    images.push_back(std::move(img));
    return static_cast<unsigned>(images.size());
  }
  bool MakeImage(Image &img, unsigned w, unsigned h, unsigned levels, VkFormat format, VkImageUsageFlags usage,
                 VkImageAspectFlags aspect, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT) {
    auto ici = Make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {std::max(1u, w), std::max(1u, h), 1};
    ici.mipLevels = levels;
    ici.arrayLayers = 1;
    ici.samples = sampleCount;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!Check(vkCreateImage(dev, &ici, nullptr, &img.image), "vkCreateImage"))
      return false;
    if (!heap.Bind(img.image, &img.memory)) {
      static unsigned lines = 0;
      if (lines++ < 10)
        GXLOG("Vulkan: no device memory for a %ux%u image (%u MB in use)", w, h,
              static_cast<unsigned>(heap.Used() >> 20));
      return false;
    }
    img.format = format;
    img.width = std::max(1u, w);
    img.height = std::max(1u, h);
    img.levels = levels;
    img.aspect = aspect;
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!(usage & VK_IMAGE_USAGE_SAMPLED_BIT) && !(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
        !(usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
      return true;
    img.view = MakeView(img, aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : aspect, levels);
    if (aspect != (aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_ASPECT_DEPTH_BIT : aspect))
      img.attach = MakeView(img, aspect, levels);
    return img.view != VK_NULL_HANDLE;
  }
  VkImageView MakeView(const Image &img, VkImageAspectFlags aspect, unsigned levels) {
    auto vci = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vci.image = img.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = img.format;
    vci.subresourceRange = {aspect, 0, levels, 0, 1};
    VkImageView v = VK_NULL_HANDLE;
    Check(vkCreateImageView(dev, &vci, nullptr, &v), "vkCreateImageView");
    return v;
  }
  // into the trash of the frame being recorded (the GPU may still read it)
  void Trash(Image &img) {
    const VkDevice d = dev;
    ImageHeap *h = &heap;
    Image i = img;
    img = Image();
    F().trash.push_back([d, h, i]() mutable {
      if (i.fb)
        vkDestroyFramebuffer(d, i.fb, nullptr);
      if (i.attach)
        vkDestroyImageView(d, i.attach, nullptr);
      if (i.view)
        vkDestroyImageView(d, i.view, nullptr);
      if (i.image)
        vkDestroyImage(d, i.image, nullptr);
      h->Free(i.memory);
    });
  }
  void Destroy(Image &img) {
    if (img.fb)
      vkDestroyFramebuffer(dev, img.fb, nullptr);
    if (img.attach)
      vkDestroyImageView(dev, img.attach, nullptr);
    if (img.view)
      vkDestroyImageView(dev, img.view, nullptr);
    if (img.image)
      vkDestroyImage(dev, img.image, nullptr);
    heap.Free(img.memory);
    img = Image();
  }

  // a layout change of `img` outside a render pass, waiting for what used it before
  static void Access(VkImageLayout l, VkAccessFlags *access, VkPipelineStageFlags *stage) {
    switch (l) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      *access = VK_ACCESS_TRANSFER_WRITE_BIT;
      *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      return;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      *access = VK_ACCESS_TRANSFER_READ_BIT;
      *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
      return;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      *access = VK_ACCESS_SHADER_READ_BIT;
      *stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
      return;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      *access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      *stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      return;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      *stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      return;
    default:
      *access = 0;
      *stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      return;
    }
  }
  void Transition(VkCommandBuffer cb, Image &img, VkImageLayout to) {
    if (img.layout == to)
      return;
    auto b = Make<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    VkPipelineStageFlags src = 0, dst = 0;
    Access(img.layout, &b.srcAccessMask, &src);
    Access(to, &b.dstAccessMask, &dst);
    b.oldLayout = img.layout;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img.image;
    b.subresourceRange = {img.aspect, 0, img.levels, 0, 1};
    vkCmdPipelineBarrier(cb, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    img.layout = to;
  }

  // ---- frames ----
  void ResetBound() {
    boundPipeline = VK_NULL_HANDLE;
    boundUbo = boundTex = VK_NULL_HANDLE;
    boundVb = boundIb = VK_NULL_HANDLE;
    constantsValid = scissorValid = false;
    boundLineWidth = -1.0f;
  }
  // the frame being recorded: its command buffers begun (after the GPU finished what it last held)
  void Begin() {
    if (recording || !ready)
      return;
    Frame &f = F();
    if (f.pending) {
      WaitFence(f.fence);
      f.pending = false;
      done = std::max(done, f.serial);
    }
    for (auto &t : f.trash)
      t();
    f.trash.clear();
    f.serial = ++serial;
    if (!continuing) {
      // the frame's blocks from the start; extra blocks of a heavy frame go
      for (std::vector<Block> *list : {&f.uniform, &f.vertex, &f.staging}) {
        while (list->size() > 1) {
          FreeBuffer(list->back());
          list->pop_back();
        }
        for (Block &b : *list)
          b.used = 0;
      }
      for (VkDescriptorPool p : f.pools)
        vkResetDescriptorPool(dev, p, 0);
      f.pool = 0;
      f.poolSets = 0;
      f.texSets.clear();
      // a chunk the in-between pass uploaded into another frame is uploaded again
      uploadedBy = nullptr;
    }
    continuing = false;
    vkResetCommandPool(dev, f.cmdPool, 0);
    auto bi = Make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(f.upload, &bi);
    vkBeginCommandBuffer(f.draw, &bi);
    f.uploads = false;
    recording = true;
    pass = kNoPass;
    ResetBound();
    // every uniform block again (its place in another frame's block may be written over)
    streamMoved = true;
  }
  void EndPass() {
    if (pass != kNoPass)
      vkCmdEndRenderPass(F().draw);
    pass = kNoPass;
  }
  // The frame to the GPU. wait: until it is done; next: the next frame is another one (else this frame goes on
  // with its blocks as they are: a submit in the middle of a chunk)
  // (waitSem: the swapchain's image acquired, before the colour output; signalSem: the picture done, for the present)
  void Submit(bool wait, bool next, VkSemaphore waitSem = VK_NULL_HANDLE, VkSemaphore signalSem = VK_NULL_HANDLE) {
    if (!recording)
      return;
    Frame &f = F();
    EndPass();
    vkEndCommandBuffer(f.upload);
    vkEndCommandBuffer(f.draw);
    VkCommandBuffer cbs[2] = {f.upload, f.draw};
    auto si = Make<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    si.commandBufferCount = f.uploads ? 2 : 1;
    si.pCommandBuffers = f.uploads ? cbs : cbs + 1;
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (waitSem) {
      si.waitSemaphoreCount = 1;
      si.pWaitSemaphores = &waitSem;
      si.pWaitDstStageMask = &waitStage;
    }
    if (signalSem) {
      si.signalSemaphoreCount = 1;
      si.pSignalSemaphores = &signalSem;
    }
    vkResetFences(dev, 1, &f.fence);
    const auto t0 = std::chrono::steady_clock::now();
    const VkResult r = vkQueueSubmit(vk.queue, 1, &si, f.fence);
    timing.submitMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    Check(r, "vkQueueSubmit");
    f.pending = r == VK_SUCCESS;
    recording = false;
    if (wait && f.pending) {
      WaitFence(f.fence);
      f.pending = false;
      done = std::max(done, f.serial);
      for (auto &t : f.trash)
        t();
      f.trash.clear();
    }
    if (next)
      cur = (cur + 1) % kFrames;
    else
      continuing = true;
  }

  // ---- render passes ----
  VkRenderPass MakePass(VkFormat color, VkFormat depth, VkAttachmentLoadOp load, VkImageLayout before,
                        VkImageLayout after) {
    VkAttachmentDescription a[2]{};
    a[0].format = color;
    a[0].samples = VK_SAMPLE_COUNT_1_BIT;
    a[0].loadOp = load;
    a[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a[0].initialLayout = before;
    a[0].finalLayout = after;
    a[1] = a[0];
    a[1].format = depth;
    a[1].initialLayout = a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = depth != VK_FORMAT_UNDEFINED ? &dref : nullptr;
    VkSubpassDependency dep[2]{};
    PassDependencies(dep);
    auto rci = Make<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    rci.attachmentCount = depth != VK_FORMAT_UNDEFINED ? 2 : 1;
    rci.pAttachments = a;
    rci.subpassCount = 1;
    rci.pSubpasses = &sub;
    rci.dependencyCount = 2;
    rci.pDependencies = dep;
    VkRenderPass p = VK_NULL_HANDLE;
    Check(vkCreateRenderPass(dev, &rci, nullptr, &p), "vkCreateRenderPass");
    return p;
  }
  // a pass's dependencies. Before: what read or wrote the attachments earlier (the copies that sampled them);
  // after: the reads that follow (a texture, a copy, a transfer)
  static void PassDependencies(VkSubpassDependency dep[2]) {
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                          VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_TRANSFER_WRITE_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep[1].dstStageMask =
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
  }
  // the EFB pass with MSAA: the multisampled colour and depth loaded and kept, the colour resolved into the EFB's
  // colour image (attachment 2) at the pass's end
  VkRenderPass MakeMsaaPass(VkFormat color, VkFormat depth, VkSampleCountFlagBits n) {
    VkAttachmentDescription a[3]{};
    for (VkAttachmentDescription &d : a) {
      d.samples = n;
      d.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      d.initialLayout = d.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      d.format = color;
    }
    a[1].format = depth;
    a[1].initialLayout = a[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    a[2].samples = VK_SAMPLE_COUNT_1_BIT;
    a[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // written whole by the resolve
    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference rref{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pResolveAttachments = &rref;
    sub.pDepthStencilAttachment = &dref;
    VkSubpassDependency dep[2]{};
    PassDependencies(dep);
    auto rci = Make<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    rci.attachmentCount = 3;
    rci.pAttachments = a;
    rci.subpassCount = 1;
    rci.pSubpasses = &sub;
    rci.dependencyCount = 2;
    rci.pDependencies = dep;
    VkRenderPass p = VK_NULL_HANDLE;
    Check(vkCreateRenderPass(dev, &rci, nullptr, &p), "vkCreateRenderPass (MSAA)");
    return p;
  }
  // the depth resolve's pass: the resolved depth image alone, written whole
  VkRenderPass MakeDepthPass(VkFormat depth) {
    VkAttachmentDescription a{};
    a.format = depth;
    a.samples = VK_SAMPLE_COUNT_1_BIT;
    a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference dref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &dref;
    VkSubpassDependency dep[2]{};
    PassDependencies(dep);
    auto rci = Make<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    rci.attachmentCount = 1;
    rci.pAttachments = &a;
    rci.subpassCount = 1;
    rci.pSubpasses = &sub;
    rci.dependencyCount = 2;
    rci.pDependencies = dep;
    VkRenderPass p = VK_NULL_HANDLE;
    Check(vkCreateRenderPass(dev, &rci, nullptr, &p), "vkCreateRenderPass (depth resolve)");
    return p;
  }
  // MSAA: the depth resolved into efbDepth before something reads it (sample 0 of each pixel)
  void ResolveDepth() {
    if (samples <= 1 || depthResolved || !depthResolvePass || !depthResolve.pipeline)
      return;
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    Transition(cb, efbMsDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cb, efbDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    auto rbi = Make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    rbi.renderPass = depthResolvePass;
    rbi.framebuffer = depthResolveFb;
    rbi.renderArea = {{0, 0}, {efbDepth.width, efbDepth.height}};
    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    pass = kOtherPass;
    const VkViewport vp{0.0f, 0.0f, float(efbDepth.width), float(efbDepth.height), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {efbDepth.width, efbDepth.height}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    scissorValid = false;
    const VkImageView view = efbMsDepth.view;
    BindFixed(depthResolve, &view, &nearestClamp, 1, {});
    vkCmdDraw(cb, 4, 1, 0, 0);
    EndPass();
    depthResolved = true;
    depthResolveCount++;
  }
  VkFramebuffer MakeFb(VkRenderPass p, std::initializer_list<VkImageView> views, unsigned w, unsigned h) {
    std::vector<VkImageView> v(views);
    auto fci = Make<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    fci.renderPass = p;
    fci.attachmentCount = static_cast<std::uint32_t>(v.size());
    fci.pAttachments = v.data();
    fci.width = w;
    fci.height = h;
    fci.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    Check(vkCreateFramebuffer(dev, &fci, nullptr, &fb), "vkCreateFramebuffer");
    return fb;
  }
  void BeginEfbPass() {
    Begin();
    if (pass == kEfbPass)
      return;
    EndPass();
    VkCommandBuffer cb = F().draw;
    Transition(cb, efbColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);  // (with MSAA: the resolve's target)
    if (samples > 1) {
      Transition(cb, efbMsColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
      Transition(cb, efbMsDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
      depthResolved = false;
    } else {
      Transition(cb, efbDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    auto rbi = Make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    rbi.renderPass = efbPass;
    rbi.framebuffer = efbFb;
    rbi.renderArea = {{0, 0}, {efbColor.width, efbColor.height}};
    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport vp{0.0f, 0.0f, float(efbColor.width), float(efbColor.height), 0.0f, 1.0f};
    vkCmdSetViewport(cb, 0, 1, &vp);
    pass = kEfbPass;
    boundPipeline = VK_NULL_HANDLE;
    scissorValid = false;
    boundLineWidth = -1.0f;
  }
  // a pass of a fixed shader into `target` (its whole image; load: clear to black, else don't care)
  void BeginTargetPass(VkRenderPass p, Image &target) {
    Begin();
    EndPass();
    if (!target.fb)
      target.fb = MakeFb(p, {target.attach ? target.attach : target.view}, target.width, target.height);
    auto rbi = Make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    rbi.renderPass = p;
    rbi.framebuffer = target.fb;
    rbi.renderArea = {{0, 0}, {target.width, target.height}};
    VkClearValue black{};
    black.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &black;
    vkCmdBeginRenderPass(F().draw, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    pass = kOtherPass;
    boundPipeline = VK_NULL_HANDLE;
    scissorValid = false;
  }

  // ---- shaders and pipelines ----
  VkShaderModule Module(const std::string &glsl, bool vertex, std::string *log) {
    std::vector<std::uint32_t> spirv;
    // the SPIR-V a run before compiled (E8), else compiled now and kept
    static const std::uint64_t compiler = Fnv(CompilerName(), std::strlen(CompilerName()));
    const std::uint64_t h = Fnv(glsl.data(), glsl.size(), compiler ^ (vertex ? 0x5653ull : 0x4653ull));
    if (Spirv().Find(h, &spirv)) {
      timing.spirvFound++;
    } else {
      const auto t0 = std::chrono::steady_clock::now();
      if (!CompileGlsl(vertex ? ShaderStage::kVertex : ShaderStage::kFragment, glsl, &spirv, log))
        return VK_NULL_HANDLE;
      timing.compiled++;
      timing.compileMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      Spirv().Put(h, spirv);
    }
    auto sci = Make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    sci.codeSize = spirv.size() * 4;
    sci.pCode = spirv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(dev, &sci, nullptr, &m), "vkCreateShaderModule");
    return m;
  }
  Program *ProgramFor(const ShaderKey &k) {
    auto it = programs.find(k);
    if (it != programs.end()) {
      it->second.lastUse = stats.chunks;
      return it->second.ok ? &it->second : nullptr;
    }
    Program &p = programs[k];
    p.lastUse = stats.chunks;
    p.key = k;
    std::string log;
    const std::string vs = VulkanGlsl(GxVertexShader(k), true, vk.depthClipControl), fs = VulkanGlsl(GxFragmentShader(k), false);
    p.vs = Module(vs, true, &log);
    if (p.vs)
      p.fs = Module(fs, false, &log);
    p.ok = p.vs && p.fs;
    if (!p.ok) {
      stats.shaderErrors++;
      if (stats.shaderErrors <= 4)
        GXLOG("Vulkan shader failed: %s\n--- vertex ---\n%s\n--- fragment ---\n%s", log.c_str(), vs.c_str(), fs.c_str());
      return nullptr;
    }
    stats.programs++;
    stats.programsLive = static_cast<unsigned>(programs.size());
    return &p;
  }
  bool MergedModule(Program &p) {
    if (!p.mergedTried) {
      p.mergedTried = true;
      std::string log;
      p.merged = Module(VulkanGlsl(GxMergedVertexShader(key), true, vk.depthClipControl), true, &log);
      if (!p.merged)
        GXLOG("Vulkan merged vertex shader failed: %s", log.c_str());
    }
    return p.merged != VK_NULL_HANDLE;
  }
  VkPipeline PipelineFor(const Program *p, bool merged, unsigned primitive, std::uint32_t layout, std::uint32_t stride) {
    PipeKey k;
    std::memset(&k, 0, sizeof(k));
    k.program = p;
    k.layout = layout;
    k.stride = stride;
    k.merged = merged;
    k.topology = static_cast<std::uint8_t>(primitive);
    k.depthTest = state.depthTest;
    k.depthWrite = state.depthWrite;
    k.depthFunc = static_cast<std::uint8_t>(state.depthTest ? state.depthFunc : 7);
    k.colorWrite = state.colorWrite;
    k.alphaWrite = state.alphaWrite;
    k.blend = state.blend;
    if (state.blend) {
      k.eqColor = state.eqColor;
      k.eqAlpha = state.eqAlpha;
      k.srcColor = state.srcColor;
      k.dstColor = state.dstColor;
      k.srcAlpha = state.srcAlpha;
      k.dstAlpha = state.dstAlpha;
    }
    k.cull = static_cast<std::uint8_t>(primitive == RE4_GX_TRIANGLES ? state.cull : 0);
    auto it = pipelines.find(k);
    if (it != pipelines.end())
      return it->second;
    return MakePipeline(k);
  }
  // a GX pipeline from its key (a draw's, or a room's list ahead of its first draw: Prewarm)
  VkPipeline MakePipeline(const PipeKey &k) {
    const auto t0 = std::chrono::steady_clock::now();
    const Program *p = k.program;
    const bool merged = k.merged != 0;
    const unsigned primitive = k.topology;
    const std::uint32_t layout = k.layout, stride = k.stride;
    // vertex attributes as GL's SetAttributes (gx_gl.cpp)
    VkVertexInputAttributeDescription attrs[15];
    std::uint32_t n = 0, off = 0;
    attrs[n++] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, off};
    off += 12;
    if (layout & RE4_GX_V_NORMAL) {
      attrs[n++] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, off};
      off += 12;
      if (layout & RE4_GX_V_BINORMAL_TANGENT) {
        attrs[n++] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, off};
        attrs[n++] = {3, 0, VK_FORMAT_R32G32B32_SFLOAT, off + 12};
        off += 24;
      }
    }
    for (int c = 0; c < 2; c++)
      if (layout & (c == 0 ? RE4_GX_V_COLOR0 : RE4_GX_V_COLOR1)) {
        attrs[n++] = {static_cast<std::uint32_t>(4 + c), 0, VK_FORMAT_R8G8B8A8_UNORM, off};
        off += 4;
      }
    for (int t = 0; t < 8; t++)
      if (layout & (RE4_GX_V_TEX0 << t)) {
        attrs[n++] = {static_cast<std::uint32_t>(6 + t), 0, VK_FORMAT_R32G32_SFLOAT, off};
        off += 8;
      }
    if (layout & RE4_GX_V_MATRIX)
      attrs[n++] = {14, 0, VK_FORMAT_R32G32B32_UINT, off};
    const VkVertexInputBindingDescription binding{0, std::max<std::uint32_t>(stride, 4), VK_VERTEX_INPUT_RATE_VERTEX};
    auto vin = Make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = n;
    vin.pVertexAttributeDescriptions = attrs;
    auto ia = Make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ia.topology = primitive == RE4_GX_LINES    ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                  : primitive == RE4_GX_POINTS ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
                                               : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto rs = Make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    static const VkCullModeFlags cull[4] = {VK_CULL_MODE_NONE, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT,
                                            VK_CULL_MODE_FRONT_AND_BACK};
    rs.cullMode = cull[k.cull & 3];
    // GL draws with glFrontFace(GL_CCW) in window coordinates that run upwards; Vulkan's run downwards with the same
    // numbers (the EFB's rows lie in memory alike): the same triangles face the front clockwise here. (Rendering the
    // EFB upside down for GL's edge rule was tried, 08.11.2026: Mesa keeps GL's FBOs unflipped with another edge rule,
    // and the flip moved the vertices' rounding instead: the pictures differed more from GL's.)
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    std::vector<VkPipelineShaderStageCreateInfo> stages(2);
    for (auto &s : stages)
      s = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = merged ? p->merged : p->vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p->fs;
    stages[1].pName = "main";
    auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthTestEnable = k.depthTest;
    ds.depthWriteEnable = k.depthTest && k.depthWrite;
    ds.depthCompareOp = static_cast<VkCompareOp>(k.depthFunc);
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = (k.colorWrite ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT : 0) |
                         (k.alphaWrite ? VK_COLOR_COMPONENT_A_BIT : 0);
    cba.blendEnable = k.blend;
    // DrawState's factors are in VkBlendFactor's order
    cba.srcColorBlendFactor = static_cast<VkBlendFactor>(k.srcColor);
    cba.dstColorBlendFactor = static_cast<VkBlendFactor>(k.dstColor);
    cba.srcAlphaBlendFactor = static_cast<VkBlendFactor>(k.srcAlpha);
    cba.dstAlphaBlendFactor = static_cast<VkBlendFactor>(k.dstAlpha);
    cba.colorBlendOp = k.eqColor == DrawState::kReverseSubtract ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;
    cba.alphaBlendOp = k.eqAlpha == DrawState::kReverseSubtract ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;
    const VkPipeline made = MakeGraphics(stages, vin, ia, rs, ds, cba, gxLayout, efbPass, true, vk.depthClipControl);
    pipelines[k] = made;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    timing.pipelines++;
    timing.pipelineMs += ms;
    timing.pipelineMax = std::max(timing.pipelineMax, ms);
    if (made)
      NotePipe(p->key, k);
    // the driver's cache to disk now and then (the app may end without Release)
    if (++pipelinesSinceSave >= 64 &&
        std::chrono::steady_clock::now() - pipelinesSavedAt > std::chrono::seconds(20))
      SavePipelines(false);
    return made;
  }
  // ---- E8: the caches on disk, the room's pipelines ahead ----
  // a room's pipelines: the program's key and the pipeline's state (the key without its program)
  struct PipeRecord {
    ShaderKey key;
    PipeKey k;
  };
  static constexpr std::size_t kPipeStateBytes = 16;  // PipeKey's bytes from `merged` on
  std::string pipelineFile;
  unsigned pipelinesSinceSave = 0;
  std::chrono::steady_clock::time_point pipelinesSavedAt{};
  unsigned pipesRoom = ~0u;
  std::vector<PipeRecord> pipesAhead, roomPipes;
  std::size_t pipesNext = 0;
  std::unordered_set<std::string> roomPipeSeen;
  bool roomPipesDirty = false, aheadLogged = false;
  unsigned aheadPipes = 0, aheadPrograms = 0;
  double aheadSpentMs = 0.0;
  std::string RoomPipesFile(unsigned r) const {
    char name[40];
    std::snprintf(name, sizeof(name), "/room-%03x.vkpipes", r);
    return programCache + name;
  }
  static std::string PipeBytes(const ShaderKey &key, const PipeKey &k) {
    std::string b(reinterpret_cast<const char *>(&key), sizeof(key));
    b.append(reinterpret_cast<const char *>(&k.layout), 4);
    b.append(reinterpret_cast<const char *>(&k.stride), 4);
    b.append(reinterpret_cast<const char *>(&k.merged), kPipeStateBytes);
    return b;
  }
  void NotePipe(const ShaderKey &key, const PipeKey &k) {
    if (programCache.empty() || room == ~0u)
      return;
    if (room != pipesRoom)
      SwitchPipesRoom();
    if (!roomPipeSeen.insert(PipeBytes(key, k)).second)
      return;
    PipeRecord r{key, k};
    r.k.program = nullptr;
    roomPipes.push_back(r);
    roomPipesDirty = true;
  }
  void SaveRoomPipes() {
    roomPipesDirty = false;
    if (programCache.empty() || pipesRoom == ~0u || roomPipes.empty())
      return;
    std::vector<char> data;
    const std::uint32_t head[2] = {static_cast<std::uint32_t>(sizeof(ShaderKey)),
                                   static_cast<std::uint32_t>(roomPipes.size())};
    static const char kMagic[] = "RE4VKP01";
    data.insert(data.end(), kMagic, kMagic + 8);
    data.insert(data.end(), reinterpret_cast<const char *>(head), reinterpret_cast<const char *>(head) + sizeof(head));
    for (const PipeRecord &r : roomPipes) {
      const std::string b = PipeBytes(r.key, r.k);
      data.insert(data.end(), b.begin(), b.end());
    }
    WriteFileBytes(RoomPipesFile(pipesRoom), data);
  }
  // another room: the one before's list saved, the new one's read (its pipelines ahead, and kept when it grows)
  void SwitchPipesRoom() {
    if (roomPipesDirty)
      SaveRoomPipes();
    pipesRoom = room;
    roomPipes.clear();
    roomPipeSeen.clear();
    pipesAhead.clear();
    pipesNext = 0;
    aheadLogged = false;
    aheadPipes = aheadPrograms = 0;
    aheadSpentMs = 0.0;
    if (programCache.empty() || room == ~0u)
      return;
    const std::vector<char> data = ReadFileBytes(RoomPipesFile(room), 16u << 20);
    const std::size_t recordBytes = sizeof(ShaderKey) + 8 + kPipeStateBytes;
    if (data.size() < 16 || std::memcmp(data.data(), "RE4VKP01", 8) != 0)
      return;
    std::uint32_t head[2];
    std::memcpy(head, data.data() + 8, sizeof(head));
    if (head[0] != sizeof(ShaderKey) || data.size() < 16 + static_cast<std::size_t>(head[1]) * recordBytes)
      return;
    for (std::uint32_t i = 0; i < head[1]; i++) {
      const char *at = data.data() + 16 + static_cast<std::size_t>(i) * recordBytes;
      PipeRecord r;
      std::memset(&r.k, 0, sizeof(r.k));
      std::memcpy(&r.key, at, sizeof(ShaderKey));
      std::memcpy(&r.k.layout, at + sizeof(ShaderKey), 4);
      std::memcpy(&r.k.stride, at + sizeof(ShaderKey) + 4, 4);
      std::memcpy(&r.k.merged, at + sizeof(ShaderKey) + 8, kPipeStateBytes);
      if (roomPipeSeen.insert(PipeBytes(r.key, r.k)).second) {
        roomPipes.push_back(r);
        pipesAhead.push_back(r);
      }
    }
  }
  // the driver's pipeline cache to disk (now: at once, at the end; else on a thread of its own)
  void SavePipelines(bool now) {
    pipelinesSinceSave = 0;
    pipelinesSavedAt = std::chrono::steady_clock::now();
    if (pipelineFile.empty() || !pipelineCache || !dev)
      return;
    std::size_t size = 0;
    if (vkGetPipelineCacheData(dev, pipelineCache, &size, nullptr) != VK_SUCCESS || !size || size > (64u << 20))
      return;
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(dev, pipelineCache, &size, data.data()) != VK_SUCCESS)
      return;
    data.resize(size);
    if (roomPipesDirty)
      SaveRoomPipes();
    if (now) {
      WriteFileBytes(pipelineFile, data);
      return;
    }
    const std::string file = pipelineFile;
    std::thread([file, data] { WriteFileBytes(file, data); }).detach();
  }
  VkPipeline MakeGraphics(const std::vector<VkPipelineShaderStageCreateInfo> &stages,
                          const VkPipelineVertexInputStateCreateInfo &vin,
                          const VkPipelineInputAssemblyStateCreateInfo &ia,
                          const VkPipelineRasterizationStateCreateInfo &rs,
                          const VkPipelineDepthStencilStateCreateInfo &ds, const VkPipelineColorBlendAttachmentState &cba,
                          VkPipelineLayout layout, VkRenderPass renderPass, bool lines, bool glDepth = false) {
    auto vp = Make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    // GL's clip range for the depth (the GX shaders' depth as GL computes it, VK_EXT_depth_clip_control)
    auto clip = Make<VkPipelineViewportDepthClipControlCreateInfoEXT>(
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_DEPTH_CLIP_CONTROL_CREATE_INFO_EXT);
    clip.negativeOneToOne = VK_TRUE;
    if (glDepth)
      vp.pNext = &clip;
    auto ms = Make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    ms.rasterizationSamples =
        renderPass == efbPass && samples > 1 ? static_cast<VkSampleCountFlagBits>(samples) : VK_SAMPLE_COUNT_1_BIT;
    auto cb = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cb.attachmentCount = renderPass == depthResolvePass ? 0 : 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH};
    auto dy = Make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dy.dynamicStateCount = lines && vk.enabled.wideLines ? 3 : 2;
    dy.pDynamicStates = dyn;
    auto gci = Make<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    gci.stageCount = static_cast<std::uint32_t>(stages.size());
    gci.pStages = stages.data();
    gci.pVertexInputState = &vin;
    gci.pInputAssemblyState = &ia;
    gci.pViewportState = &vp;
    gci.pRasterizationState = &rs;
    gci.pMultisampleState = &ms;
    gci.pDepthStencilState = &ds;
    gci.pColorBlendState = &cb;
    gci.pDynamicState = &dy;
    gci.layout = layout;
    gci.renderPass = renderPass;
    VkPipeline p = VK_NULL_HANDLE;
    if (!Check(vkCreateGraphicsPipelines(dev, pipelineCache, 1, &gci, nullptr, &p), "vkCreateGraphicsPipelines"))
      return VK_NULL_HANDLE;
    return p;
  }
  // a fixed shader's pipeline: the quad (4 vertices, a strip) with `fs`, its constants as push constants, its
  // textures in set 0; plain state with the colour mask `mask`, depth always (written when `depthWrite`)
  bool MakeFixed(Fixed &f, const std::string &vsEs, const std::string &fsEs, VkRenderPass renderPass,
                 VkColorComponentFlags mask, bool depthWrite, std::string *error, bool vkGlsl = false,
                 const VkFixedLayout *given = nullptr, int blend = 0,
                 VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP) {
    VkFixedLayout fl;
    std::string vs = vsEs, fs = fsEs;
    if (!vkGlsl) {
      fl = VulkanFixedProgram(vsEs, fsEs, &vs, &fs);
    } else if (given) {
      fl = *given;
    } else {
      fl.constants = {{"color", 0, 16}, {"z", 16, 4}};
      fl.bytes = 32;
    }
    f.constants = fl;
    std::string log;
    VkShaderModule v = Module(vs, true, &log), m = v ? Module(fs, false, &log) : VK_NULL_HANDLE;
    if (!v || !m) {
      *error = "a fixed Vulkan shader: " + log + "\n" + (v ? fs : vs);
      if (v)
        vkDestroyShaderModule(dev, v, nullptr);
      return false;
    }
    // the textures (both stages may read them), then the uniform block (VulkanFixedProgram)
    const VkShaderStageFlags both = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    std::vector<VkDescriptorSetLayoutBinding> b(fl.samplers.size() + (fl.ubo ? 1 : 0));
    for (std::size_t i = 0; i < fl.samplers.size(); i++)
      b[i] = {static_cast<std::uint32_t>(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, both, nullptr};
    if (fl.ubo)
      b.back() = {static_cast<std::uint32_t>(fl.samplers.size()), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, both, nullptr};
    auto dl = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    dl.bindingCount = static_cast<std::uint32_t>(b.size());
    dl.pBindings = b.data();
    vkCreateDescriptorSetLayout(dev, &dl, nullptr, &f.setLayout);
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, std::max(16u, fl.bytes)};
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &f.setLayout;
    pl.pushConstantRangeCount = fl.bytes && !fl.ubo ? 1 : 0;
    pl.pPushConstantRanges = &range;
    vkCreatePipelineLayout(dev, &pl, nullptr, &f.layout);
    std::vector<VkPipelineShaderStageCreateInfo> stages(2);
    for (auto &s : stages)
      s = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = v;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m;
    stages[1].pName = "main";
    auto vin = Make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto ia = Make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ia.topology = topology;
    auto rs = Make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthTestEnable = depthWrite;
    ds.depthWriteEnable = depthWrite;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = mask;
    if (blend == 1) {  // added to what is there (the lens flares)
      cba.blendEnable = VK_TRUE;
      cba.srcColorBlendFactor = cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.srcAlphaBlendFactor = cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (blend == 2) {  // over what is there by the source's alpha (the autofocus's mix)
      cba.blendEnable = VK_TRUE;
      cba.srcColorBlendFactor = cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
      cba.dstColorBlendFactor = cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    f.pipeline = MakeGraphics(stages, vin, ia, rs, ds, cba, f.layout, renderPass, false);
    vkDestroyShaderModule(dev, v, nullptr);
    vkDestroyShaderModule(dev, m, nullptr);
    if (!f.pipeline)
      *error = "a fixed Vulkan pipeline";
    return f.pipeline != VK_NULL_HANDLE;
  }
  void FreeFixed(Fixed &f) {
    if (f.pipeline)
      vkDestroyPipeline(dev, f.pipeline, nullptr);
    if (f.layout)
      vkDestroyPipelineLayout(dev, f.layout, nullptr);
    if (f.setLayout)
      vkDestroyDescriptorSetLayout(dev, f.setLayout, nullptr);
    f = Fixed();
  }
  // a set of combined image samplers from the frame's pools
  VkDescriptorSet TexSet(VkDescriptorSetLayout layout) {
    Frame &f = F();
    if (f.pool >= f.pools.size() || f.poolSets >= kTexSetsPerPool) {
      if (f.pool < f.pools.size() && f.poolSets >= kTexSetsPerPool)
        f.pool++;
      if (f.pool >= f.pools.size()) {
        // (the fixed passes' uniform blocks too, VulkanFixedProgram)
        const VkDescriptorPoolSize size[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kTexSetsPerPool * 8},
                                              {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kTexSetsPerPool / 4}};
        auto pci = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pci.maxSets = kTexSetsPerPool;
        pci.poolSizeCount = 2;
        pci.pPoolSizes = size;
        VkDescriptorPool p = VK_NULL_HANDLE;
        if (!Check(vkCreateDescriptorPool(dev, &pci, nullptr, &p), "vkCreateDescriptorPool"))
          return VK_NULL_HANDLE;
        f.pools.push_back(p);
      }
      f.poolSets = 0;
    }
    auto ai = Make<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool = f.pools[f.pool];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet s = VK_NULL_HANDLE;
    if (!Check(vkAllocateDescriptorSets(dev, &ai, &s), "vkAllocateDescriptorSets (textures)"))
      return VK_NULL_HANDLE;
    f.poolSets++;
    return s;
  }
  void WriteImages(VkDescriptorSet set, const VkImageView *views, const VkSampler *samp, std::uint32_t n) {
    VkDescriptorImageInfo info[8];
    VkWriteDescriptorSet w[8];
    for (std::uint32_t i = 0; i < n; i++) {
      info[i] = {samp[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      w[i] = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
      w[i].dstSet = set;
      w[i].dstBinding = i;
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(dev, n, w, 0, nullptr);
  }
  VkSampler SamplerOf(std::uint32_t mode0, std::uint32_t mode1, unsigned levels, bool point) {
    const std::uint64_t k = (static_cast<std::uint64_t>(mode1 & 0xFFFF) << 32) | (mode0 & 0x1FF) |
                            (static_cast<std::uint64_t>(levels > 1) << 48) | (static_cast<std::uint64_t>(point) << 49);
    auto it = samplers.find(k);
    if (it != samplers.end())
      return it->second;
    const GxSamplerDesc d = SamplerFor(mode0, mode1, levels, point);
    auto sci = Make<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    static const VkSamplerAddressMode wrap[3] = {VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                 VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
    sci.addressModeU = wrap[d.wrapS];
    sci.addressModeV = wrap[d.wrapT];
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.magFilter = d.magLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.minFilter = d.minLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.mipmapMode = d.mip == 2 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.minLod = d.minLod;
    sci.maxLod = d.maxLod;
    if (d.mip == 0) {
      // only the base level (Vulkan has no such mode): the level of the nearest below 0.5, the choice between
      // magnification and minification as with GL's clamp
      sci.maxLod = std::min(d.maxLod, 0.25f);
      sci.minLod = std::min(d.minLod, sci.maxLod);
    }
    sci.anisotropyEnable = d.anisotropy > 1.0f && vk.enabled.samplerAnisotropy;
    sci.maxAnisotropy = std::min(d.anisotropy, vk.properties.limits.maxSamplerAnisotropy);
    VkSampler s = VK_NULL_HANDLE;
    Check(vkCreateSampler(dev, &sci, nullptr, &s), "vkCreateSampler");
    samplers[k] = s;
    return s;
  }
  VkSampler PlainSampler(bool linear) {
    auto sci = Make<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sci.magFilter = sci.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.25f;
    VkSampler s = VK_NULL_HANDLE;
    Check(vkCreateSampler(dev, &sci, nullptr, &s), "vkCreateSampler");
    return s;
  }
  // a fixed pass's constants by name (VkFixedLayout)
  template <class T> static void Put(std::vector<std::uint8_t> &pc, const Fixed &f, const char *name, const T &v) {
    if (const VkFixedLayout::Constant *c = f.constants.Find(name))
      std::memcpy(pc.data() + c->offset, &v, std::min<std::size_t>(sizeof(T), c->size));
  }
  // a fixed pass's textures by their names (VulkanFixedProgram numbers them as both stages declare them)
  struct NamedTexture {
    const char *name;
    VkImageView view;
    VkSampler sampler;
  };
  void BindFixedNamed(const Fixed &f, std::initializer_list<NamedTexture> textures, const std::vector<std::uint8_t> &pc) {
    VkImageView views[8]{};
    VkSampler samp[8]{};
    const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::size_t>(8, f.constants.samplers.size()));
    for (std::uint32_t i = 0; i < n; i++)
      for (const NamedTexture &t : textures)
        if (f.constants.samplers[i] == t.name) {
          views[i] = t.view;
          samp[i] = t.sampler;
        }
    BindFixed(f, views, samp, n, pc);
  }
  // an array of `n` elements of `elem` bytes each, at the array's stride (VkFixedLayout)
  static void PutArray(std::vector<std::uint8_t> &pc, const Fixed &f, const char *name, const void *data,
                       std::size_t elem, std::size_t n) {
    if (const VkFixedLayout::Constant *c = f.constants.Find(name))
      for (std::size_t i = 0; i < n && i < c->count; i++)
        std::memcpy(pc.data() + c->offset + i * c->stride, static_cast<const std::uint8_t *>(data) + i * elem,
                    std::min<std::size_t>(elem, c->size));
  }
  void BindFixed(const Fixed &f, const VkImageView *views, const VkSampler *samp, std::uint32_t n,
                 const std::vector<std::uint8_t> &pc) {
    VkCommandBuffer cb = F().draw;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, f.pipeline);
    boundPipeline = VK_NULL_HANDLE;
    if (n || f.constants.ubo) {
      const VkDescriptorSet set = TexSet(f.setLayout);
      if (n)
        WriteImages(set, views, samp, n);
      if (f.constants.ubo) {
        // the constants as a uniform block in the frame's uniform stream (a new buffer there: the GX blocks follow)
        Frame &fr = F();
        const VkDeviceSize align = std::max<VkDeviceSize>(16, vk.properties.limits.minUniformBufferOffsetAlignment);
        std::size_t b = 0;
        VkDeviceSize at = 0;
        const std::size_t before = fr.uniform.size();
        if (!Alloc(fr.uniform, f.constants.bytes, align, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &b, &at, true))
          return;
        if (fr.uniform.size() != before)
          streamMoved = true;
        std::memcpy(fr.uniform[b].map + at, pc.data(), std::min<std::size_t>(pc.size(), f.constants.bytes));
        VkDescriptorBufferInfo info{fr.uniform[b].buffer, at, f.constants.bytes};
        auto w = Make<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet = set;
        w.dstBinding = static_cast<std::uint32_t>(f.constants.samplers.size());
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &info;
        vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
      }
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, f.layout, 0, 1, &set, 0, nullptr);
      boundTex = boundUbo = VK_NULL_HANDLE;
    }
    if (!pc.empty() && !f.constants.ubo)
      vkCmdPushConstants(cb, f.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         static_cast<std::uint32_t>(pc.size()), pc.data());
    constantsValid = false;
  }

  // ---- GxCore: Run's GPU side ----
  std::uintptr_t ProgramTag() const override { return reinterpret_cast<std::uintptr_t>(keyProgram); }
  // the frames the GPU finished (their fences), without waiting
  void Poll() {
    for (Frame &f : frames)
      if (f.pending && vkGetFenceStatus(dev, f.fence) == VK_SUCCESS)
        done = std::max(done, f.serial);
  }
  void RunBegin() override {
    if (!interpPass && peekReady)
      DeliverDepth();  // the depth maps the GPU finished since the last chunk go to the core
    Begin();
  }
  void UploadChunk(const Re4GxChunk &chunk) override {
    Begin();
    Frame &f = F();
    const VkDeviceSize vbytes = std::max<VkDeviceSize>(chunk.vertexBytes, 4);
    const VkDeviceSize own = static_cast<VkDeviceSize>(chunk.indexCount) * 2, moved = mergeIndices.size() * 2;
    std::size_t vb = 0, ib = 0;
    VkDeviceSize vo = 0, io = 0;
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (!Alloc(f.vertex, vbytes, 64, usage, &vb, &vo) || !Alloc(f.vertex, std::max<VkDeviceSize>(own + moved, 4), 64, usage, &ib, &io))
      return;
    if (chunk.vertexBytes)
      std::memcpy(f.vertex[vb].map + vo, chunk.vertices, chunk.vertexBytes);
    if (own)
      std::memcpy(f.vertex[ib].map + io, chunk.indices, own);
    if (moved)
      std::memcpy(f.vertex[ib].map + io + own, mergeIndices.data(), moved);
    vtxBuffer = f.vertex[vb].buffer;
    vtxBase = vo;
    idxBuffer = f.vertex[ib].buffer;
    idxBase = io;
    stats.ringChunks++;
  }
  void RunState() override {
    // every uniform block again at its first draw (as GL's uniform stream)
    Dirty(0, static_cast<int>(sizeof(XfBlock) / 16));
    for (bool &v : xfSentValid)
      v = false;
    pxDirty = true;
    pxValid = false;
  }
  void RunEnd() override {
    // how far the GPU is behind: frames submitted and not finished
    unsigned behind = 0;
    for (Frame &f : frames)
      if (f.pending && vkGetFenceStatus(dev, f.fence) == VK_NOT_READY)
        behind++;
    stats.behindSum += behind;
    stats.behindChecks++;
    stats.behindCount[std::min(behind, 8u)]++;
  }
  void RunLeave() override {}
  // ---- the lens flares (E5, as GlRenderer::FlarePass): after the scene's last 3D draw the visibility pass (each
  // slot's followed visibility and colour from the picture's depth and colour, into a 32 x 2 image), then the
  // flares added into the EFB's colour ----
  bool MakeFlares() {
    std::string error;
    const VkColorComponentFlags rgb = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    if (!MakeFixed(flareVisFx, fixed::kFlareQuadVs, fixed::kFlareVisFs, copyPass, rgb | VK_COLOR_COMPONENT_A_BIT,
                   false, &error) ||
        !MakeFixed(flareFx, fixed::kFlareVs, fixed::kFlareFs, efbPass, rgb, false, &error, false, nullptr, 1,
                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)) {
      GXLOG("lens flares (Vulkan): shader failed: %s", error.c_str());
      return false;
    }
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    const VkClearColorValue zero{{0.0f, 0.0f, 0.0f, 0.0f}};
    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Image &v : flareVis) {
      if (!MakeImage(v, kFlareSlots, 2, 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
      Transition(cb, v, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdClearColorImage(cb, v.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
      Transition(cb, v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return true;
  }
  void FlarePass() override {
    flareDone = true;
    FlareTrack();
    flareCands.clear();
    bool any = false;
    for (const FlareSlot &sl : flareSlot)
      any = any || sl.lost >= 0;
    if (!any || flareFailed)
      return;
    if (!flareFx.pipeline && !MakeFlares()) {
      flareFailed = true;
      return;
    }
    static const char *flareLogEnv = std::getenv("RE4_GX_FLARE_LOG");
    const bool log2 = flareLogEnv && *flareLogEnv == '2';
    const int fw = Lo(kEfbWidth), fh = Lo(kEfbHeight);
    const float sc = Sf(1);
    float slots[kFlareSlots][4];
    for (int i = 0; i < kFlareSlots; i++) {
      const FlareSlot &sl = flareSlot[i];
      slots[i][0] = sl.x * sc;
      slots[i][1] = sl.y * sc;
      slots[i][2] = sl.z;
      // state (0 free, 1 seen, 2 new, 3 lost, 4 seen but not yet in 4 pictures) + 8 x the radius in EFB texels
      slots[i][3] = (sl.lost < 0 ? 0.0f : sl.lost > 0 ? 3.0f : sl.fresh ? 2.0f : sl.age < 4 ? 4.0f : 1.0f) +
                    (sl.lost < 0 ? 0.0f : 8.0f * std::floor(sl.r * sc));
    }
    // the picture's depth and colour (MSAA: resolved), as the autofocus reads them
    ResolveDepth();
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    Transition(cb, efbDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cb, efbColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const int next = flareVisCur ^ 1;
    Transition(cb, flareVis[flareVisCur], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    BeginTargetPass(copyPass, flareVis[next]);
    flareVis[next].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkViewport vvp{0.0f, 0.0f, float(kFlareSlots), 2.0f, 0.0f, 1.0f};
    const VkRect2D vsc{{0, 0}, {static_cast<std::uint32_t>(kFlareSlots), 2}};
    vkCmdSetViewport(cb, 0, 1, &vvp);
    vkCmdSetScissor(cb, 0, 1, &vsc);
    std::vector<std::uint8_t> pc(flareVisFx.constants.bytes, 0);
    PutArray(pc, flareVisFx, "slot", slots, sizeof(slots[0]), kFlareSlots);
    const float visSize[4] = {static_cast<float>(fw), static_cast<float>(fh), std::max(1.0f, 1.5f * sc),
                              log2 ? 1.0f : 0.0f};
    Put(pc, flareVisFx, "size", visSize);
    const VkImageView views[3] = {efbDepth.view, efbColor.view, flareVis[flareVisCur].view};
    const VkSampler samp[3] = {nearestClamp, nearestClamp, nearestClamp};
    BindFixed(flareVisFx, views, samp, 3, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    EndPass();
    flareVisCur = next;
    // the flares: their radius in EFB texels in slot.z; strength: at most about a third of white
    float ghosts[kFlareSlots], crowds[kFlareSlots];
    for (int i = 0; i < kFlareSlots; i++) {
      slots[i][2] = flareSlot[i].r * sc;
      ghosts[i] = flareSlot[i].ghost;
      crowds[i] = flareSlot[i].crowd;
    }
    BeginEfbPass();
    const VkRect2D whole{{0, 0}, {efbColor.width, efbColor.height}};
    vkCmdSetScissor(cb, 0, 1, &whole);
    scissorValid = false;
    std::vector<std::uint8_t> pf(flareFx.constants.bytes, 0);
    PutArray(pf, flareFx, "slot", slots, sizeof(slots[0]), kFlareSlots);
    PutArray(pf, flareFx, "ghost", ghosts, sizeof(float), kFlareSlots);
    PutArray(pf, flareFx, "crowd", crowds, sizeof(float), kFlareSlots);
    const float flareSize[4] = {static_cast<float>(fw), static_cast<float>(fh), 0.0f, 0.5f};
    Put(pf, flareFx, "size", flareSize);
    const VkImageView vis = flareVis[flareVisCur].view;
    BindFixed(flareFx, &vis, &nearestClamp, 1, pf);
    vkCmdDraw(cb, 36 * kFlareSlots, 1, 0, 0);
  }
  // ---- the gameplay autofocus (E5, as GlRenderer::Autofocus; the comment there says what it does) ----
  bool MakeAutofocus() {
    std::string error;
    const VkColorComponentFlags rgb = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    const VkColorComponentFlags all = rgb | VK_COLOR_COMPONENT_A_BIT;
    const std::string head = std::string(fixed::kDofHead) + fixed::kDofDistance;
    // the mix reads the depth image: without MSAA not in the EFB pass, whose attachment it is
    if (samples <= 1 && !efbColorPass)
      efbColorPass = MakePass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_LOAD,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const VkRenderPass mixPass = samples > 1 ? efbPass : efbColorPass;
    if (!MakeFixed(focusFx, fixed::kDofQuadVs, head + fixed::kDofFocusFs, copyPass, all, false, &error) ||
        !MakeFixed(blurFx, fixed::kDofQuadVs, head + fixed::kDofBlurFs, copyPass, all, false, &error) ||
        !MakeFixed(halfFx, fixed::kDofQuadVs, head + fixed::kDofHalfFs, copyPass, all, false, &error) ||
        !MakeFixed(mixFx, fixed::kDofQuadVs, head + fixed::kDofMixFs, mixPass, rgb, false, &error, false, nullptr, 2) ||
        !MakeFixed(dofBackFx, fixed::kQuadVs, fixed::kCopyFs, efbPass, rgb, false, &error)) {
      GXLOG("autofocus shader (Vulkan): %s", error.c_str());
      return false;
    }
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    const VkClearColorValue zero{{0.0f, 0.0f, 0.0f, 0.0f}};
    const VkImageSubresourceRange whole{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Image &f : focusImg) {
      if (!MakeImage(f, 3, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
      Transition(cb, f, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdClearColorImage(cb, f.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &whole);
      Transition(cb, f, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return true;
  }
  // a pass of the autofocus into `target` (a region of it: the viewport x, y, w, h)
  void DofPass(const Fixed &f, Image &target, int x, int y, int w, int h, std::initializer_list<NamedTexture> textures,
               const std::vector<std::uint8_t> &pc) {
    VkCommandBuffer cb = F().draw;
    BeginTargetPass(copyPass, target);
    target.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkViewport vp{float(x), float(y), float(w), float(h), 0.0f, 1.0f};
    const VkRect2D sc{{x, y}, {static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h)}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    BindFixedNamed(f, textures, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    EndPass();
  }
  // the region, the size and the depth mapping into a pass's constants
  static void DofConstants(std::vector<std::uint8_t> &pc, const Fixed &f, const float region[4], const float size[2],
                           const float zmap[4]) {
    pc.assign(f.constants.bytes, 0);
    Put(pc, f, "region", *reinterpret_cast<const float(*)[4]>(region));
    Put(pc, f, "size", *reinterpret_cast<const float(*)[2]>(size));
    Put(pc, f, "zmap", *reinterpret_cast<const float(*)[4]>(zmap));
  }
  void Autofocus(std::uint32_t player) override {
    if (autofocusFailed || !havePerspective)
      return;
    const int fw = Lo(kEfbWidth), fh = Lo(kEfbHeight);
    if (!dofBackFx.pipeline && !MakeAutofocus()) {
      autofocusFailed = true;
      GXLOG("gameplay autofocus could not be set up; left out");
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
    const float region[4] = {rx, ry, rw, rh}, size[2] = {static_cast<float>(fw), static_cast<float>(fh)};
    const float zmap[4] = {perspA, perspB, zRange, zFar};
    // the picture's depth and colour (MSAA: resolved)
    ResolveDepth();
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    Transition(cb, efbDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cb, efbColor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // the focus (as GL: it follows the measurements from the chunk before, else it starts anew)
    const bool restart = focusChunk + 1 != stats.chunks + (interpPass ? 1u : 0u);
    if (!interpPass)
      focusChunk = stats.chunks;
    const int next = focusCur ^ 1;
    std::vector<std::uint8_t> pc;
    DofConstants(pc, focusFx, region, size, zmap);
    const float playerDepth = static_cast<float>(player), again = restart ? 1.0f : 0.0f;
    Put(pc, focusFx, "player", playerDepth);
    Put(pc, focusFx, "restart", again);
    DofPass(focusFx, focusImg[next], 0, 0, 3, 1,
            {{"depths", efbDepth.view, nearestClamp}, {"history", focusImg[focusCur].view, nearestClamp}}, pc);
    focusCur = next;
    const float radius = dofStrong ? std::min(6.0f, std::max(1.5f, rh / 480.0f * 2.5f))
                                   : std::min(4.0f, std::max(1.0f, rh / 480.0f * 1.5f));
    const float strength = dofStrong ? 0.8f : 0.6f;
    const VkViewport efbVp{0.0f, 0.0f, float(efbColor.width), float(efbColor.height), 0.0f, 1.0f};
    const VkViewport regionVp{rx, ry, rw, rh, 0.0f, 1.0f};
    const VkRect2D regionSc{{Lo(x0), Lo(y0)}, {static_cast<std::uint32_t>(rw), static_cast<std::uint32_t>(rh)}};
    if (options.halfDof) {
      // the blur at half the resolution, then mixed in at the full one by each pixel's own distance
      if (!DofImage(halfImg, fw / 2, fh / 2))
        return;
      const int hx0 = Lo(x0) / 2, hy0 = Lo(y0) / 2;
      const int hx1 = (Lo(x1) + 1) / 2, hy1 = (Lo(y1) + 1) / 2;
      DofConstants(pc, halfFx, region, size, zmap);
      Put(pc, halfFx, "radius", radius);
      DofPass(halfFx, halfImg, hx0, hy0, hx1 - hx0, hy1 - hy0,
              {{"frame", efbColor.view, linearClamp},
               {"depths", efbDepth.view, nearestClamp},
               {"focus", focusImg[focusCur].view, nearestClamp}},
              pc);
      if (samples > 1) {
        BeginEfbPass();  // (the multisampled EFB: the resolved depth is another image)
      } else {
        Transition(cb, efbColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (!efbColorFb)
          efbColorFb = MakeFb(efbColorPass, {efbColor.view}, efbColor.width, efbColor.height);
        auto rbi = Make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
        rbi.renderPass = efbColorPass;
        rbi.framebuffer = efbColorFb;
        rbi.renderArea = {{0, 0}, {efbColor.width, efbColor.height}};
        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        pass = kOtherPass;
      }
      vkCmdSetViewport(cb, 0, 1, &regionVp);
      vkCmdSetScissor(cb, 0, 1, &regionSc);
      DofConstants(pc, mixFx, region, size, zmap);
      Put(pc, mixFx, "strength", strength);
      BindFixedNamed(mixFx,
                     {{"blurred", halfImg.view, linearClamp},
                      {"depths", efbDepth.view, nearestClamp},
                      {"focus", focusImg[focusCur].view, nearestClamp}},
                     pc);
      vkCmdDraw(cb, 4, 1, 0, 0);
      if (samples > 1)
        vkCmdSetViewport(cb, 0, 1, &efbVp);  // (the EFB pass goes on with the full viewport)
      else
        EndPass();
      scissorValid = false;
      stats.autofocus++;
      return;
    }
    // the blurred picture at the full resolution, then back into the EFB: colour only, alpha and depth stay
    if (!DofImage(blurImg, fw, fh))
      return;
    DofConstants(pc, blurFx, region, size, zmap);
    Put(pc, blurFx, "radius", radius);
    Put(pc, blurFx, "strength", strength);
    DofPass(blurFx, blurImg, Lo(x0), Lo(y0), static_cast<int>(rw), static_cast<int>(rh),
            {{"frame", efbColor.view, linearClamp},
             {"depths", efbDepth.view, nearestClamp},
             {"focus", focusImg[focusCur].view, nearestClamp}},
            pc);
    BeginEfbPass();
    vkCmdSetViewport(cb, 0, 1, &regionVp);
    vkCmdSetScissor(cb, 0, 1, &regionSc);
    pc.assign(dofBackFx.constants.bytes, 0);
    const std::int32_t mode = 0, half = 0;
    const float gain = 1.0f, limit[2] = {-1e9f, 1e9f}, taps[3] = {0.0f, -1.0f, 0.0f}, step = 0.0f;
    Put(pc, dofBackFx, "mode", mode);
    Put(pc, dofBackFx, "rect", region);
    Put(pc, dofBackFx, "size", size);
    Put(pc, dofBackFx, "halfScale", half);
    Put(pc, dofBackFx, "gain", gain);
    Put(pc, dofBackFx, "limitY", limit);
    Put(pc, dofBackFx, "taps", taps);  // no TV filter
    Put(pc, dofBackFx, "lineStep", step);
    const VkImageView view = blurImg.view;
    BindFixed(dofBackFx, &view, &nearestClamp, 1, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    vkCmdSetViewport(cb, 0, 1, &efbVp);
    scissorValid = false;
    stats.autofocus++;
  }
  bool DofImage(Image &img, int w, int h) {
    if (img.image && img.width == static_cast<unsigned>(w) && img.height == static_cast<unsigned>(h))
      return true;
    if (img.image)
      Trash(img);
    return MakeImage(img, static_cast<unsigned>(std::max(1, w)), static_cast<unsigned>(std::max(1, h)), 1,
                     VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT);
  }
  void ClearAfterCopyPlain(std::uint32_t control, int x, int y, int w, int h) override {
    ClearAfterCopy(control, x, y, w, h);
  }
  void GpuFinish() override { Submit(true, false); }
  // a copy read through a palette (GxCore::PalettedCopy): the palette as a small texture, the copy through it into a
  // new image of the copy's size in GameCube pixels (as GL)
  unsigned DrawPaletted(const Copy &copy, const Re4GxTexture &t, const std::uint8_t *rgba, unsigned pw,
                        unsigned ph) override {
    Image *src = ImageOf(copy.id);
    if (!src || !src->view)
      return whiteTex;
    const unsigned pal = BeginTexture(pw, ph, 1);
    TextureLevel(pal, 0, pw, ph, rgba);
    EndTexture(pal, 1);
    const unsigned out = NewTarget(copy.width, copy.height);
    Image *dst = ImageOf(out), *p = ImageOf(pal);
    if (!dst || !p) {
      DeleteTexture(pal);
      return out ? out : whiteTex;
    }
    VkCommandBuffer cb = F().draw;
    BeginTargetPass(copyPass, *dst);
    dst->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkViewport vp{0.0f, 0.0f, float(copy.width), float(copy.height), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {copy.width, copy.height}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    std::vector<std::uint8_t> pc(palette.constants.bytes, 0);
    const float size[2] = {static_cast<float>(pw), static_cast<float>(ph)};
    const std::int32_t kind = static_cast<std::int32_t>(copy.kind), format = static_cast<std::int32_t>(t.format);
    Put(pc, palette, "palSize", size);
    Put(pc, palette, "kind", kind);
    Put(pc, palette, "format", format);
    const VkImageView views[2] = {src->view, p->view};
    const VkSampler samp[2] = {nearestClamp, nearestClamp};
    BindFixed(palette, views, samp, 2, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    EndPass();
    DeleteTexture(pal);
    return out;
  }
  void Touched() override {}
  void DeleteTexture(unsigned id) override {
    if (Image *img = ImageOf(id)) {
      Begin();
      Trash(*img);
      images[id - 1].reset();
      freeIds.push_back(id);
    }
  }

  // ---- draws ----
  bool KeyProgram() override {
    keyProgram = ProgramFor(key);
    return keyProgram != nullptr;
  }
  bool KeyProgramReady() const override { return keyProgram != nullptr; }
  bool MergedProgramReady() override {
    Program *p = const_cast<Program *>(keyProgram);
    return p && MergedModule(*p);
  }
  const void *DrawProgram(bool merged) override {
    Begin();
    // room for all of this draw's uniform blocks in one block (set 0 holds one buffer)
    Frame &f = F();
    if (f.uniform.empty() || f.uniform.back().used + kDrawUniformsMost > f.uniform.back().size) {
      std::size_t b = 0;
      VkDeviceSize o = 0;
      if (Alloc(f.uniform, kDrawUniformsMost, 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &b, &o, true))
        f.uniform.back().used = 0;
      streamMoved = true;
    }
    Program *p = const_cast<Program *>(keyProgram);
    if (!p)
      return nullptr;
    p->lastUse = stats.chunks;
    if (merged && !MergedModule(*p))
      return nullptr;
    return p;
  }
  void UseEfb() override { BeginEfbPass(); }
  void ApplyDrawState(const DrawState &s) override { state = s; }
  void UploadUniform(int binding, const void *data, std::size_t, std::size_t, std::size_t blockBytes,
                     std::size_t copyBytes) override {
    if (binding < 0 || binding >= kUboCount)
      return;
    Begin();
    Frame &f = F();
    const VkDeviceSize align = std::max<VkDeviceSize>(16, vk.properties.limits.minUniformBufferOffsetAlignment);
    std::size_t b = 0;
    VkDeviceSize at = 0;
    const std::size_t before = f.uniform.size();
    if (!Alloc(f.uniform, kUboRange[binding], align, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &b, &at, true))
      return;
    if (f.uniform.size() != before)
      streamMoved = true;  // a new buffer: the other blocks must follow into it (DrawProgram keeps room, so rarely)
    const std::size_t copyN = copyBytes ? std::min(copyBytes, blockBytes) : blockBytes;
    std::memcpy(f.uniform[b].map + at, data, std::min<std::size_t>(copyN, kUboRange[binding]));
    uboBlock = b;
    uboOffset[binding] = static_cast<std::uint32_t>(at);
    stats.uniformUploads++;
    stats.uniformBytes += static_cast<unsigned>(copyN);
  }
  bool PutBlendedVertices(const std::uint8_t *data, std::size_t bytes, std::uint32_t) override {
    Begin();
    Frame &f = F();
    std::size_t b = 0;
    VkDeviceSize at = 0;
    if (!Alloc(f.vertex, bytes, 64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &b, &at))
      return false;
    std::memcpy(f.vertex[b].map + at, data, bytes);
    blendBuffer = f.vertex[b].buffer;
    blendAt = static_cast<std::int64_t>(at);
    return true;
  }
  void SubmitDraw(const Re4GxDraw &d, const void *program, const MergeCall *merge) override {
    const Program *p = static_cast<const Program *>(program);
    const ShaderKey &k = key;
    const unsigned primitive = d.primitive & 0xFF;
    BeginEfbPass();
    VkCommandBuffer cb = F().draw;
    const VkPipeline pipeline = PipelineFor(p, merge != nullptr, primitive, d.layout, d.stride);
    if (!pipeline)
      return;
    if (pipeline != boundPipeline) {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      boundPipeline = pipeline;
      stats.programSwitches++;
      // (a pipeline of another layout before: the sets again)
      boundUbo = boundTex = VK_NULL_HANDLE;
      constantsValid = false;
    }
    const VkRect2D sc{{state.scissor[0], state.scissor[1]},
                      {static_cast<std::uint32_t>(std::max(0, state.scissor[2])),
                       static_cast<std::uint32_t>(std::max(0, state.scissor[3]))}};
    if (!scissorValid || std::memcmp(&sc, &boundScissor, sizeof(sc)) != 0) {
      vkCmdSetScissor(cb, 0, 1, &sc);
      boundScissor = sc;
      scissorValid = true;
    }
    if (vk.enabled.wideLines && (primitive == RE4_GX_LINES)) {
      const float lw = std::max(1.0f, std::min(state.lineWidth, vk.properties.limits.lineWidthRange[1]));
      if (lw != boundLineWidth) {
        vkCmdSetLineWidth(cb, lw);
        boundLineWidth = lw;
      }
    } else if (vk.enabled.wideLines && boundLineWidth != 1.0f) {
      vkCmdSetLineWidth(cb, 1.0f);
      boundLineWidth = 1.0f;
    }
    // the uniform blocks: set 0 of the block they lie in, their places as dynamic offsets
    Frame &f = F();
    if (uboBlock < f.uniform.size()) {
      const VkDescriptorSet ubo = f.uniform[uboBlock].uboSet;
      if (ubo != boundUbo || std::memcmp(boundOffsets, uboOffset, sizeof(uboOffset)) != 0) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gxLayout, kVkSetBlocks, 1, &ubo, kUboCount,
                                uboOffset);
        boundUbo = ubo;
        std::memcpy(boundOffsets, uboOffset, sizeof(uboOffset));
      }
    }
    // the textures: the maps the shader samples, white elsewhere
    VkImageView views[8];
    VkSampler samp[8];
    const Image *white = ImageOf(whiteTex);
    std::uint64_t h = 1469598103934665603ull;
    for (int m = 0; m < 8; m++) {
      const Image *img = nullptr;
      if (k.texMaps & (1u << m))
        img = ImageOf(bound[m].id ? bound[m].id : whiteTex);
      if (!img || !img->view)
        img = white;
      views[m] = img->view;
      if (k.texMaps & (1u << m)) {
        const std::uint32_t mode0 = bp[(m < 4 ? 0x80 : 0xA0) + (m & 3)], mode1 = bp[(m < 4 ? 0x84 : 0xA4) + (m & 3)];
        samp[m] = SamplerOf(mode0, mode1, bound[m].levels, (k.centroid & 2) != 0);
      } else {
        samp[m] = nearestClamp;
      }
      h = Fnv(&views[m], sizeof(views[m]), h);
      h = Fnv(&samp[m], sizeof(samp[m]), h);
    }
    auto ts = f.texSets.find(h);
    VkDescriptorSet tex = VK_NULL_HANDLE;
    if (ts != f.texSets.end()) {
      tex = ts->second;
    } else {
      tex = TexSet(texLayout);
      if (!tex)
        return;
      WriteImages(tex, views, samp, 8);
      f.texSets[h] = tex;
    }
    if (tex != boundTex) {
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gxLayout, kVkSetTextures, 1, &tex, 0, nullptr);
      boundTex = tex;
      stats.textureBinds++;
    }
    // uMtx and uWiden
    VkDrawConstants c{};
    if (!merge)
      std::memcpy(c.mtx, d.matrices, sizeof(c.mtx));
    c.widen = WidenFor(k);
    if (!constantsValid || std::memcmp(&c, &boundConstants, sizeof(c)) != 0) {
      vkCmdPushConstants(cb, gxLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(c), &c);
      if (constantsValid)
        mergeMatrix = true;
      boundConstants = c;
      constantsValid = true;
    }
    // the vertices: the chunk's, or an in-between picture's blended ones; a base vertex as GL's
    const VkBuffer vbuf = blendAt >= 0 ? blendBuffer : vtxBuffer;
    const std::uint32_t at = blendAt >= 0 ? 0 : d.vertexOffset;
    VkDeviceSize voff = blendAt >= 0 ? static_cast<VkDeviceSize>(blendAt) : vtxBase + at;
    std::int32_t baseVertex = 0;
    if (d.stride && blendAt < 0) {
      voff = vtxBase + at % d.stride;
      baseVertex = static_cast<std::int32_t>(merge ? merge->base : at / d.stride);
    }
    blendAt = -1;
    if (vbuf != boundVb || voff != boundVbOffset) {
      vkCmdBindVertexBuffers(cb, 0, 1, &vbuf, &voff);
      boundVb = vbuf;
      boundVbOffset = voff;
      stats.attributeSetups++;
    }
    if (idxBuffer != boundIb || idxBase != boundIbOffset) {
      vkCmdBindIndexBuffer(cb, idxBuffer, idxBase, VK_INDEX_TYPE_UINT16);
      boundIb = idxBuffer;
      boundIbOffset = idxBase;
    }
    const std::uint32_t indexAt = merge ? merge->index : d.indexOffset;
    const std::uint32_t indexCount = merge ? merge->count : d.indexCount;
    Split(GxStats::kSplitBind);
    vkCmdDrawIndexed(cb, indexCount, 1, indexAt, baseVertex, 0);
    Split(GxStats::kSplitDraw);
  }
  void InterpBegin(const Re4GxChunk &) override { blendAt = -1; }
  void InterpEnd() override { blendAt = -1; }

  // ---- textures ----
  unsigned BeginTexture(unsigned w, unsigned h, unsigned levels) override {
    Begin();
    auto img = std::make_unique<Image>();
    if (!MakeImage(*img, w, h, std::max(1u, levels), VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) {
      Destroy(*img);
      return 0;
    }
    // (its view again at the end, with the levels that came)
    vkDestroyImageView(dev, img->view, nullptr);
    img->view = VK_NULL_HANDLE;
    Transition(F().upload, *img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    F().uploads = true;
    return NewId(std::move(img));
  }
  void TextureLevel(unsigned id, unsigned level, unsigned w, unsigned h, const std::uint8_t *rgba) override {
    Image *img = ImageOf(id);
    if (!img || level >= img->levels)
      return;
    Frame &f = F();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;
    std::size_t b = 0;
    VkDeviceSize at = 0;
    if (!Alloc(f.staging, bytes, 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &b, &at))
      return;
    std::memcpy(f.staging[b].map + at, rgba, bytes);
    VkBufferImageCopy region{};
    region.bufferOffset = at;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(f.upload, f.staging[b].buffer, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }
  void EndTexture(unsigned id, unsigned levels) override {
    Image *img = ImageOf(id);
    if (!img)
      return;
    Transition(F().upload, *img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    img->view = MakeView(*img, VK_IMAGE_ASPECT_COLOR_BIT, std::max(1u, std::min(levels, img->levels)));
  }
  void ClearRect(const GxClearRect &r) override {
    if (!(r.color || r.alpha || r.depth))
      return;
    BeginEfbPass();
    VkCommandBuffer cb = F().draw;
    VkRect2D rect{{std::max(0, r.rect[0]), std::max(0, r.rect[1])}, {0, 0}};
    const int x1 = std::min<int>(static_cast<int>(efbColor.width), r.rect[0] + r.rect[2]),
              y1 = std::min<int>(static_cast<int>(efbColor.height), r.rect[1] + r.rect[3]);
    if (x1 <= rect.offset.x || y1 <= rect.offset.y)
      return;
    rect.extent = {static_cast<std::uint32_t>(x1 - rect.offset.x), static_cast<std::uint32_t>(y1 - rect.offset.y)};
    if (r.color == r.alpha) {
      VkClearAttachment a[2]{};
      std::uint32_t n = 0;
      if (r.color) {
        a[n].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        a[n].colorAttachment = 0;
        a[n].clearValue.color = {{r.rgba[0], r.rgba[1], r.rgba[2], r.rgba[3]}};
        n++;
      }
      if (r.depth) {
        a[n].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        a[n].clearValue.depthStencil = {r.z, 0};
        n++;
      }
      const VkClearRect cr{rect, 0, 1};
      vkCmdClearAttachments(cb, n, a, 1, &cr);
      return;
    }
    // colour without alpha or alpha without colour: a quad with that mask (vkCmdClearAttachments ignores it)
    const int which = (r.color ? 1 : 0) | (r.alpha ? 2 : 0) | (r.depth ? 4 : 0);
    Fixed &f = clearFixed[which];
    if (!f.pipeline) {
      std::string error;
      const VkColorComponentFlags mask =
          (r.color ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT : 0) |
          (r.alpha ? VK_COLOR_COMPONENT_A_BIT : 0);
      if (!MakeFixed(f, kClearVs, kClearFs, efbPass, mask, r.depth, &error, true)) {
        GXLOG("Vulkan: %s", error.c_str());
        return;
      }
    }
    std::vector<std::uint8_t> pc(32, 0);
    std::memcpy(pc.data(), r.rgba, 16);
    std::memcpy(pc.data() + 16, &r.z, 4);
    BindFixed(f, nullptr, nullptr, 0, pc);
    vkCmdSetScissor(cb, 0, 1, &rect);
    scissorValid = false;
    vkCmdDraw(cb, 4, 1, 0, 0);
  }

  // ---- EFB copies ----
  unsigned DisplayTarget(int w, int h) override {
    Image *img = ImageOf(xfbId);
    if (!img || xfbWidth != static_cast<unsigned>(w) || xfbHeight != static_cast<unsigned>(h)) {
      if (xfbId)
        DeleteTexture(xfbId);
      xfbId = NewTarget(static_cast<unsigned>(Hi(w)), static_cast<unsigned>(Hi(h)));
      xfbWidth = static_cast<unsigned>(w);
      xfbHeight = static_cast<unsigned>(h);
    }
    return xfbId;
  }
  unsigned NewTarget(unsigned w, unsigned h) {
    auto img = std::make_unique<Image>();
    if (!MakeImage(*img, w, h, 1, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT)) {
      Destroy(*img);
      return 0;
    }
    return NewId(std::move(img));
  }
  void CopyTarget(Copy &c, int w, int h) override {
    if (!c.id || c.width != static_cast<unsigned>(w) || c.height != static_cast<unsigned>(h)) {
      if (c.id)
        DeleteTexture(c.id);
      c.id = NewTarget(static_cast<unsigned>(Hi(w)), static_cast<unsigned>(Hi(h)));
      c.width = static_cast<unsigned>(w);
      c.height = static_cast<unsigned>(h);
    }
  }
  void CopyPass(const CopyParams &p) override {
    Image *dst = ImageOf(p.target);
    if (!dst)
      return;
    if (p.depth)
      ResolveDepth();
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    Image &src = p.depth ? efbDepth : efbColor;
    Transition(cb, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    BeginTargetPass(copyPass, *dst);
    dst->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // the pass's final layout
    const VkViewport vp{0.0f, 0.0f, float(Hi(p.tw)), float(Hi(p.th)), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {static_cast<std::uint32_t>(Hi(p.tw)), static_cast<std::uint32_t>(Hi(p.th))}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    std::vector<std::uint8_t> pc(copy.constants.bytes, 0);
    const float rect[4] = {Sf(p.x), Sf(p.y), Sf(p.w), Sf(p.h)};
    const float size[2] = {static_cast<float>(Lo(kEfbWidth)), static_cast<float>(Lo(kEfbHeight))};
    const std::int32_t mode = p.mode, half = p.half ? 1 : 0;
    Put(pc, copy, "mode", mode);
    Put(pc, copy, "rect", rect);
    Put(pc, copy, "size", size);
    Put(pc, copy, "halfScale", half);
    Put(pc, copy, "gain", p.gain);
    Put(pc, copy, "limitY", p.limit);
    Put(pc, copy, "taps", p.taps);
    Put(pc, copy, "lineStep", p.step);
    const VkImageView view = src.view;
    const VkSampler s = p.depth ? nearestClamp : linearClamp;
    BindFixed(copy, &view, &s, 1, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    EndPass();
  }
  // ---- the display copy's filters (E5, as GlRenderer's): FXAA, Sharpen, the PSone demake; each a pass of the
  // shared shaders (gx_fixed_shaders.h) into an image of the copy's size, shown instead of the XFB ----
  void DisplayEffects(int pw, int ph, int y, int h, int w) override {
    FilterFxaa(pw, ph);
    FilterSharpen(pw, ph, y, h);
    FilterPsx(pw, ph, w, h);
  }
  // the picture shown: the last filter's, else the XFB
  Image *ShownImage() {
    if (psxShown && psxImg.image)
      return &psxImg;
    if (sharpShown && sharpImg.image)
      return &sharpImg;
    if (fxaaShown && fxaaImg.image)
      return &fxaaImg;
    return ImageOf(xfbId);
  }
  bool FilterImage(Image &img, int w, int h) {
    if (img.image && img.width == static_cast<unsigned>(w) && img.height == static_cast<unsigned>(h))
      return true;
    if (img.image)
      Trash(img);
    return MakeImage(img, static_cast<unsigned>(w), static_cast<unsigned>(h), 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT);
  }
  bool FilterProgram(Fixed &f, const char *vs, const char *fs, const char *what) {
    if (f.pipeline)
      return true;
    std::string error;
    const VkColorComponentFlags all = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT;
    if (MakeFixed(f, vs, fs, copyPass, all, false, &error))
      return true;
    GXLOG("%s (Vulkan): shader failed: %s", what, error.c_str());
    FreeFixed(f);
    return false;
  }
  // one pass of a filter: `srcs` read (linear, clamped), `dst` drawn whole
  void FilterPass(const Fixed &f, Image &dst, Image *const *srcs, std::uint32_t n, const std::vector<std::uint8_t> &pc) {
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    VkImageView views[2]{};
    VkSampler samp[2] = {linearClamp, linearClamp};
    for (std::uint32_t i = 0; i < n && i < 2; i++) {
      Transition(cb, *srcs[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      views[i] = srcs[i]->view;
    }
    BeginTargetPass(copyPass, dst);
    dst.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // the pass's final layout
    const VkViewport vp{0.0f, 0.0f, float(dst.width), float(dst.height), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {dst.width, dst.height}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    BindFixed(f, views, samp, n, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    EndPass();
  }
  // FXAA: the display copy just made (w x h texels) smoothed (GlRenderer::Fxaa)
  void FilterFxaa(int w, int h) {
    fxaaShown = false;
    Image *x = ImageOf(xfbId);
    if (!fxaaOn || !x || w < 16 || h < 16)
      return;
    if (!FilterProgram(fxaaFx, fixed::kFxaaVs, fixed::kFxaaFs, "FXAA")) {
      fxaaOn = false;  // no shader on this driver: off for good (the menu's switch stays)
      return;
    }
    if (!FilterImage(fxaaImg, w, h))
      return;
    std::vector<std::uint8_t> pc(fxaaFx.constants.bytes, 0);
    const float texel[2] = {1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h)};
    Put(pc, fxaaFx, "texel", texel);
    Image *src[1] = {x};
    FilterPass(fxaaFx, fxaaImg, src, 1, pc);
    fxaaShown = true;
    stats.fxaaPasses++;
  }
  // Sharpen: the display copy (after FXAA; w x h texels of the EFB rows y..y+rows), not a movie's
  // (GlRenderer::Sharpen)
  void FilterSharpen(int w, int h, int y, int rows) {
    sharpShown = false;
    Image *x = ImageOf(xfbId);
    if (sharpLevel <= 0 || chunkMovie || !x || w < 16 || h < 16 || rows < 1)
      return;
    if (!FilterProgram(sharpFx, fixed::kSharpenVs, fixed::kSharpenFs, "sharpen")) {
      sharpLevel = 0;
      return;
    }
    if (!FilterImage(sharpImg, w, h))
      return;
    // the rows shown, in texels of this copy (row 0 its top)
    const int shownT = shownTop.load(std::memory_order_relaxed), shownB = shownBottom.load(std::memory_order_relaxed);
    const int first = std::max(0, std::min(h - 1, (shownT - y) * h / rows));
    const int last = std::max(first, std::min(h - 1, (shownB - y) * h / rows - 1));
    static float amounts[2] = {-1.0f, -1.0f};
    if (amounts[0] < 0.0f) {
      amounts[0] = fixed::kSharpLight;
      amounts[1] = fixed::kSharpStrong;
      if (const char *test = std::getenv("RE4_GX_SHARPEN_AMOUNT"))
        std::sscanf(test, "%f,%f", &amounts[0], &amounts[1]);
    }
    std::vector<std::uint8_t> pc(sharpFx.constants.bytes, 0);
    const float amount = amounts[sharpLevel >= 2 ? 1 : 0];
    const std::int32_t lim[3] = {w - 1, first, last};
    Put(pc, sharpFx, "amount", amount);
    Put(pc, sharpFx, "lim", lim);
    Image *src[1] = {fxaaShown && fxaaImg.image ? &fxaaImg : x};
    FilterPass(sharpFx, sharpImg, src, 1, pc);
    sharpShown = true;
    stats.sharpenPasses++;
  }
  // the PSone demake: the picture shown so far (w x h texels; gw x gh of the game's pixels) on a grid of half the
  // game's pixels, a cell held at its level of the picture before (GlRenderer::Psx)
  void FilterPsx(int w, int h, int gw, int gh) {
    const bool wasShown = psxShown;
    psxShown = false;
    Image *x = ImageOf(xfbId);
    if (!psxOn || !x || w < 16 || h < 16 || gw < 4 || gh < 4) {
      psxPrevValid = false;
      return;
    }
    if (!FilterProgram(psxFx, fixed::kPsxVs, fixed::kPsxFs, "PSone demake")) {
      psxOn = false;
      return;
    }
    if (!psxImg.image || !psxPrevImg.image || psxImg.width != static_cast<unsigned>(w) ||
        psxImg.height != static_cast<unsigned>(h)) {
      if (!FilterImage(psxImg, w, h) || !FilterImage(psxPrevImg, w, h))
        return;
      psxPrevValid = false;
    }
    Image *from = ShownImage();  // the XFB, or a filter's picture of it
    // the picture before becomes psxPrevImg, the new one goes to the other image
    std::swap(psxImg, psxPrevImg);
    const bool usePrev = psxPrevValid && wasShown && psxGridW == gw / 2 && psxGridH == gh / 2;
    psxGridW = gw / 2;
    psxGridH = gh / 2;
    std::vector<std::uint8_t> pc(psxFx.constants.bytes, 0);
    const float grid[2] = {static_cast<float>(gw / 2), static_cast<float>(gh / 2)};
    const float use = usePrev ? 1.0f : 0.0f;
    Put(pc, psxFx, "grid", grid);
    Put(pc, psxFx, "usePrev", use);
    Image *src[2] = {from, &psxPrevImg};
    FilterPass(psxFx, psxImg, src, 2, pc);
    psxShown = true;
    psxPrevValid = true;
  }
  // ---- the depth map for GXPeekZ (as GlRenderer::SnapshotDepth / DeliverDepth) ----
  void DeliverDepth() {
    Poll();
    for (PeekSlot &s : peekSlot) {
      if (!s.serial || s.serial > done)
        continue;
      if (depthSink)
        depthSink(reinterpret_cast<const std::uint32_t *>(s.buffer.map), s.picture);
      stats.depthDelivered++;
      s.serial = 0;
    }
  }
  bool MakePeek() {
    std::string fs = fixed::kPeekFs, error;
    const VkColorComponentFlags all = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT;
    if (!MakeFixed(peek, fixed::kPeekVs, fs, copyPass, all, false, &error)) {
      GXLOG("depth map for GXPeekZ (Vulkan): %s", error.c_str());
      return false;
    }
    if (!MakeImage(peekImage, kDepthWidth, kDepthHeight, 1, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT))  // (sampled: the copy pass leaves it as a texture)
      return false;
    for (PeekSlot &s : peekSlot)
      if (!MakeBuffer(VkDeviceSize(kDepthWidth) * kDepthHeight * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &s.buffer))
        return false;
    return true;
  }
  void SnapshotDepth() override {
    peekWanted = false;
    if (!depthSink || peekFailed)
      return;
    if (!peekReady) {
      if (!MakePeek()) {
        peekFailed = true;
        return;
      }
      peekReady = true;
    }
    DeliverDepth();
    PeekSlot *slot = !peekSlot[0].serial ? &peekSlot[0] : !peekSlot[1].serial ? &peekSlot[1] : nullptr;
    if (!slot)
      return;  // the GPU is two maps behind: the core keeps the last one (it ages out there)
    ResolveDepth();
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    Transition(cb, efbDepth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    BeginTargetPass(copyPass, peekImage);
    peekImage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkViewport vp{0.0f, 0.0f, float(kDepthWidth), float(kDepthHeight), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {static_cast<std::uint32_t>(kDepthWidth), static_cast<std::uint32_t>(kDepthHeight)}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    std::vector<std::uint8_t> pc(peek.constants.bytes, 0);
    const float size[2] = {static_cast<float>(Lo(kEfbWidth)), static_cast<float>(Lo(kEfbHeight))};
    const float scale = Sf(1);
    Put(pc, peek, "size", size);
    Put(pc, peek, "scale", scale);
    const VkImageView view = efbDepth.view;
    BindFixed(peek, &view, &nearestClamp, 1, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    EndPass();
    Transition(cb, peekImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<std::uint32_t>(kDepthWidth), static_cast<std::uint32_t>(kDepthHeight), 1};
    vkCmdCopyImageToBuffer(cb, peekImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot->buffer.buffer, 1, &region);
    auto mb = Make<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0,
                         nullptr);
    slot->serial = F().serial;
    slot->picture = peekPicture;
    stats.depthSnapshots++;
  }
  void PlainEfb() override {}

  // ---- the picture ----
  unsigned Xfb(unsigned *width, unsigned *height) const override {
    if (width)
      *width = xfbWidth;
    if (height)
      *height = xfbHeight;
    return haveXfb ? xfbId : 0;
  }
  // the latest display copy into `target` (whose pass is begun), at (x, y, w, h) counted from its top left
  void DrawPresent(Image &target, int x, int y, int w, int h, unsigned top, unsigned rows) {
    VkCommandBuffer cb = F().draw;
    BeginTargetPass(screenPass, target);
    target.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    if (viBlack)
      return;  // VISetBlack: black, whatever the XFB holds
    Image *xfb = ShownImage();
    if (!xfb)
      return;
    const VkViewport vp{float(x), float(y), float(w), float(h), 0.0f, 1.0f};
    const VkRect2D sc{{std::max(0, x), std::max(0, y)}, {target.width, target.height}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
    std::vector<std::uint8_t> pc(present.constants.bytes, 0);
    const float lines = static_cast<float>(xfbHeight ? xfbHeight : 1);
    float r[2] = {0.0f, 1.0f};
    if (rows && top + rows <= xfbHeight) {
      r[0] = static_cast<float>(top) / lines;
      r[1] = static_cast<float>(rows) / lines;
    }
    Put(pc, present, "rows", r);
    const VkImageView view = xfb->view;
    const VkSampler s = linearClamp;
    BindFixed(present, &view, &s, 1, pc);
    vkCmdDraw(cb, 4, 1, 0, 0);
    // the overlay as on the screen (GL's screenshot has the minimap too)
    const VkViewport whole{0.0f, 0.0f, float(target.width), float(target.height), 0.0f, 1.0f};
    const VkRect2D all{{0, 0}, {target.width, target.height}};
    vkCmdSetViewport(cb, 0, 1, &whole);
    vkCmdSetScissor(cb, 0, 1, &all);
    const float identity[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    DrawOverlay(cb, identity, screenPass, overlayShotPipe, static_cast<int>(target.width),
                static_cast<int>(target.height));
  }
  // ---- the overlay (GxOverlay, the minimap): drawn in the screen's pass after the picture ----
  void SetOverlay(const GxOverlay &o) override { overlay = o; }
  // the overlay's pipelines (triangles, lines) for a pass: the swapchain's or the screenshot's
  bool MakeOverlay(VkRenderPass rp, VkPipeline out[2]) {
    std::string log;
    VkShaderModule vs = Module(kOverlayVs, true, &log), fs = vs ? Module(kOverlayFs, false, &log) : VK_NULL_HANDLE;
    if (!vs || !fs) {
      GXLOG("Vulkan overlay: %s", log.c_str());
      if (vs)
        vkDestroyShaderModule(dev, vs, nullptr);
      return false;
    }
    if (!overlayLayout) {
      VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, 32};
      auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
      pl.pushConstantRangeCount = 1;
      pl.pPushConstantRanges = &range;
      vkCreatePipelineLayout(dev, &pl, nullptr, &overlayLayout);
    }
    std::vector<VkPipelineShaderStageCreateInfo> stages(2);
    for (auto &st : stages)
      st = Make<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkVertexInputBindingDescription bind{0, 6 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                                  {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 2 * sizeof(float)}};
    auto vin = Make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 2;
    vin.pVertexAttributeDescriptions = attrs;
    auto rs = Make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = VK_TRUE;  // as GL's glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
    cba.srcColorBlendFactor = cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    for (int lines = 0; lines < 2; lines++) {
      auto ia = Make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
      ia.topology = lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      out[lines] = MakeGraphics(stages, vin, ia, rs, ds, cba, overlayLayout, rp, lines != 0);
    }
    vkDestroyShaderModule(dev, vs, nullptr);
    vkDestroyShaderModule(dev, fs, nullptr);
    return out[0] && out[1];
  }
  // the pipelines of the swapchain's pass (with it: its format may change), or all of them with the layout
  void FreeOverlay(bool all = false) {
    for (VkPipeline &pp : overlayPipe) {
      if (pp)
        vkDestroyPipeline(dev, pp, nullptr);
      pp = VK_NULL_HANDLE;
    }
    if (!all)
      return;
    for (VkPipeline &pp : overlayShotPipe) {
      if (pp)
        vkDestroyPipeline(dev, pp, nullptr);
      pp = VK_NULL_HANDLE;
    }
    if (overlayLayout)
      vkDestroyPipelineLayout(dev, overlayLayout, nullptr);
    overlayLayout = VK_NULL_HANDLE;
  }
  // the overlay into a screen pass (begun; the picture is drawn) of W x H pixels: rot is the display's turn
  void DrawOverlay(VkCommandBuffer cb, const float rot[4], VkRenderPass rp, VkPipeline pipes[2], int W, int H) {
    const GxOverlay &o = overlay;
    if (o.verts.empty() || o.parts.empty() || o.size <= 0 || W <= 0 || H <= 0 || overlayFailed)
      return;
    if (!pipes[0] && !MakeOverlay(rp, pipes)) {
      overlayFailed = true;
      return;
    }
    Frame &f = F();
    std::size_t b = 0;
    VkDeviceSize at = 0;
    const VkDeviceSize bytes = o.verts.size() * sizeof(float);
    if (!Alloc(f.vertex, bytes, 16, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &b, &at))
      return;
    std::memcpy(f.vertex[b].map + at, o.verts.data(), bytes);
    const VkBuffer buf = f.vertex[b].buffer;
    vkCmdBindVertexBuffers(cb, 0, 1, &buf, &at);
    boundVb = VK_NULL_HANDLE;
    // the square in clip space with y down (GL's viewport counts from the bottom)
    const float fw = float(W), fh = float(H);
    const float pc[8] = {2.0f * o.x / fw - 1.0f, 2.0f * (fh - o.y - o.size) / fh - 1.0f,
                         2.0f * (o.x + o.size) / fw - 1.0f, 2.0f * (fh - o.y) / fh - 1.0f, rot[0], rot[1], rot[2], rot[3]};
    for (const GxOverlay::Part &part : o.parts) {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[part.lines ? 1 : 0]);
      vkCmdPushConstants(cb, overlayLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
      if (part.lines && vk.enabled.wideLines)
        vkCmdSetLineWidth(cb, std::max(1.0f, std::min(o.lineWidth, vk.properties.limits.lineWidthRange[1])));
      vkCmdDraw(cb, static_cast<std::uint32_t>(part.count), 1, static_cast<std::uint32_t>(part.first), 0);
    }
    boundPipeline = VK_NULL_HANDLE;
    constantsValid = false;
    boundLineWidth = -1.0f;
  }
  // ---- the screen: the Android window's swapchain (E6) ----
  void SetWindow(void *w, int width, int height) override {
    if (w == window) {
      if (width != screenW || height != screenH)
        swapStale = true;
      screenW = width;
      screenH = height;
      return;
    }
    DropSwapchain(true);
#ifdef __ANDROID__
    if (window)
      ANativeWindow_release(static_cast<ANativeWindow *>(window));
    if (w)
      ANativeWindow_acquire(static_cast<ANativeWindow *>(w));
#endif
    window = w;
    screenW = width;
    screenH = height;
    swapStale = true;
  }
  // the swapchain, its images and the semaphores gone (andSurface: the window's surface too)
  void DropSwapchain(bool andSurface) {
    if (!dev)
      return;
    if (recording)
      Submit(false, true);
    vkDeviceWaitIdle(dev);
    for (VkFramebuffer fb : swapFbs)
      vkDestroyFramebuffer(dev, fb, nullptr);
    for (VkImageView v : swapViews)
      vkDestroyImageView(dev, v, nullptr);
    for (VkSemaphore s : renderSem)
      vkDestroySemaphore(dev, s, nullptr);
    swapFbs.clear();
    swapViews.clear();
    swapImages.clear();
    renderSem.clear();
    if (swapchain)
      vkDestroySwapchainKHR(dev, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
    if (andSurface && surfaceKhr) {
      vkDestroySurfaceKHR(vk.instance, surfaceKhr, nullptr);
      surfaceKhr = VK_NULL_HANDLE;
    }
  }
  bool MakeSwapchain() {
#ifdef __ANDROID__
    if (!window || !vkCreateAndroidSurfaceKHR || !vkCreateSwapchainKHR)
      return false;
    DropSwapchain(false);
    swapStale = false;
    if (!surfaceKhr) {
      auto sci = Make<VkAndroidSurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR);
      sci.window = static_cast<ANativeWindow *>(window);
      if (!Check(vkCreateAndroidSurfaceKHR(vk.instance, &sci, nullptr, &surfaceKhr), "vkCreateAndroidSurfaceKHR"))
        return false;
    }
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, vk.family, surfaceKhr, &supported);
    if (!supported) {
      GXLOG("Vulkan: the queue cannot present to this window");
      return false;
    }
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, surfaceKhr, &caps);
    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, surfaceKhr, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, surfaceKhr, &n, formats.data());
    if (formats.empty())
      return false;
    // the picture as GL's window shows it: 8 bits, no sRGB conversion
    VkSurfaceFormatKHR format = formats[0];
    for (VkFormat want : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM})
      for (const VkSurfaceFormatKHR &f : formats)
        if (f.format == want && format.format != VK_FORMAT_R8G8B8A8_UNORM) {
          format = f;
          break;
        }
    // pre-rotation (the default since the Retroid's report of 08.11.2026): the picture drawn turned as the display is,
    // so Android neither turns it nor answers "suboptimal"; RE4_VK_PRETRANSFORM=0: the compositor turns it
    const char *pre = std::getenv("RE4_VK_PRETRANSFORM");
    const bool preRotate = !(pre && *pre == '0');
    const VkSurfaceTransformFlagBitsKHR current = caps.currentTransform;
    swapSeenTurn = static_cast<unsigned>(current);
    swapSeenExtent = caps.currentExtent;
    swapCheck = false;
    swapMade++;
    const bool turned = current & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
    swapTransform = preRotate || !(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                        ? current
                        : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu) {
      extent = {static_cast<std::uint32_t>(std::max(1, screenW)), static_cast<std::uint32_t>(std::max(1, screenH))};
    } else if (swapTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
      // drawn as the app sees the screen (the compositor turns it): the app's size, in whichever order the driver
      // gave it
      if (screenW != screenH && extent.width == static_cast<std::uint32_t>(screenH) &&
          extent.height == static_cast<std::uint32_t>(screenW))
        std::swap(extent.width, extent.height);
    } else if (turned && extent.width == static_cast<std::uint32_t>(screenW) &&
               extent.height == static_cast<std::uint32_t>(screenH)) {
      std::swap(extent.width, extent.height);  // pre-rotated: the display's own orientation
    }
    extent.width = std::max(caps.minImageExtent.width, std::min(extent.width, std::max(caps.maxImageExtent.width, 1u)));
    extent.height =
        std::max(caps.minImageExtent.height, std::min(extent.height, std::max(caps.maxImageExtent.height, 1u)));
    std::uint32_t images = std::max(caps.minImageCount, 3u);
    if (caps.maxImageCount)
      images = std::min(images, caps.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                          VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
      if (caps.supportedCompositeAlpha & a) {
        alpha = a;
        break;
      }
    auto ci = Make<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    ci.surface = surfaceKhr;
    ci.minImageCount = images;
    ci.imageFormat = format.format;
    ci.imageColorSpace = format.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = swapTransform;
    ci.compositeAlpha = alpha;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // waits for the display's refresh, as eglSwapBuffers
    ci.clipped = VK_TRUE;
    if (!Check(vkCreateSwapchainKHR(dev, &ci, nullptr, &swapchain), "vkCreateSwapchainKHR"))
      return false;
    swapExtent = extent;
    vkGetSwapchainImagesKHR(dev, swapchain, &n, nullptr);
    swapImages.resize(n);
    vkGetSwapchainImagesKHR(dev, swapchain, &n, swapImages.data());
    if (format.format != swapFormat || !swapPass) {
      if (swapPass)
        vkDestroyRenderPass(dev, swapPass, nullptr);
      FreeFixed(swapPresent);
      FreeOverlay();
      swapFormat = format.format;
      swapPass = MakePass(swapFormat, VK_FORMAT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
      VkFixedLayout l;
      l.constants = {{"rect", 0, 16}, {"rot", 16, 16}, {"rows", 32, 8}};
      l.samplers = {"src"};
      l.bytes = 48;
      std::string error;
      const VkColorComponentFlags all = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
      if (!swapPass || !MakeFixed(swapPresent, kSwapVs, kSwapFs, swapPass, all, false, &error, true, &l)) {
        GXLOG("Vulkan: the screen's pass: %s", error.c_str());
        return false;
      }
    }
    for (VkImage image : swapImages) {
      Image tmp;
      tmp.image = image;
      tmp.format = swapFormat;
      const VkImageView v = MakeView(tmp, VK_IMAGE_ASPECT_COLOR_BIT, 1);
      swapViews.push_back(v);
      swapFbs.push_back(MakeFb(swapPass, {v}, extent.width, extent.height));
      VkSemaphore s = VK_NULL_HANDLE;
      auto sem = Make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
      vkCreateSemaphore(dev, &sem, nullptr, &s);
      renderSem.push_back(s);
    }
    // (logged when it differs from the last: a turn, a new size; not for each making)
    char line[256];
    std::snprintf(line, sizeof(line),
                  "Vulkan screen: %ux%u, %u images, format %d, the display's turn %u (%s), app screen %dx%d",
                  extent.width, extent.height, n, static_cast<int>(swapFormat), static_cast<unsigned>(current),
                  swapTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ? "turned by the compositor" : "pre-rotated",
                  screenW, screenH);
    if (swapLogged != line) {
      swapLogged = line;
      GXLOG("%s (swapchain %u)", line, swapMade);
    }
    return true;
#else
    return false;
#endif
  }
  // "suboptimal" before: the surface's turn or size changed (then anew), or not (then left; after a few such looks in
  // a row, no more)
  void LookAtSuboptimal() {
    if (!swapCheck || !surfaceKhr)
      return;
    swapCheck = false;
    // (given up: a look every 120 pictures all the same, for a turn of the display)
    if (suboptimalLooks >= kSuboptimalLooks && ++suboptimalSkipped % 120 != 0)
      return;
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, surfaceKhr, &caps) != VK_SUCCESS)
      return;
    if (static_cast<unsigned>(caps.currentTransform) != swapSeenTurn ||
        caps.currentExtent.width != swapSeenExtent.width || caps.currentExtent.height != swapSeenExtent.height) {
      swapStale = true;
      suboptimalLooks = 0;
      return;
    }
    if (++suboptimalLooks == kSuboptimalLooks)
      GXLOG("Vulkan screen: \"suboptimal\" %u times without a change of the display; the swapchain stays",
            kSuboptimalLooks);
  }
  // once a second: did the display turn (a turn by 180 degrees changes no size, and Android need not answer it with
  // "suboptimal"; the device report of 08.11.2026 showed none)? Then the swapchain anew, turned as the display
  void LookAtTurn() {
    if (!surfaceKhr || !swapchain || ++presentsSinceLook < 60)
      return;
    presentsSinceLook = 0;
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, surfaceKhr, &caps) != VK_SUCCESS)
      return;
    if (static_cast<unsigned>(caps.currentTransform) != swapSeenTurn) {
      GXLOG("Vulkan screen: the display turned (its turn %u, before %u)", static_cast<unsigned>(caps.currentTransform),
            swapSeenTurn);
      swapStale = true;
    }
  }
  bool Present(int x, int y, int w, int h, unsigned top, unsigned rows) override {
    if (!haveXfb)
      return false;
    stats.presents++;
    LookAtSuboptimal();
    LookAtTurn();
    if (!window || ((!swapchain || swapStale) && !MakeSwapchain())) {
      Submit(false, true);  // no screen: the frame to the GPU all the same
      return true;
    }
    Begin();
    const int slot = cur;
    std::uint32_t index = 0;
    const auto acquired = std::chrono::steady_clock::now();
    VkResult r = vkAcquireNextImageKHR(dev, swapchain, 1000000000ull, acquireSem[slot], VK_NULL_HANDLE, &index);
    timing.acquireMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - acquired).count();
    timing.presents++;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)) {
      swapStale = true;
      if (r != VK_ERROR_OUT_OF_DATE_KHR)
        Check(r, "vkAcquireNextImageKHR");
      Submit(false, true);
      return true;
    }
    if (r == VK_SUBOPTIMAL_KHR)
      swapCheck = true;
    EndPass();
    VkCommandBuffer cb = F().draw;
    auto rbi = Make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    rbi.renderPass = swapPass;
    rbi.framebuffer = swapFbs[index];
    rbi.renderArea = {{0, 0}, swapExtent};
    VkClearValue black{};
    black.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &black;
    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    pass = kOtherPass;
    boundPipeline = VK_NULL_HANDLE;
    scissorValid = false;
    Image *xfb = ShownImage();
    if (!viBlack && xfb && xfb->view && screenW > 0 && screenH > 0) {
      const VkViewport vp{0.0f, 0.0f, float(swapExtent.width), float(swapExtent.height), 0.0f, 1.0f};
      const VkRect2D sc{{0, 0}, swapExtent};
      vkCmdSetViewport(cb, 0, 1, &vp);
      vkCmdSetScissor(cb, 0, 1, &sc);
      // the box in the app's screen (GL's viewport: from the bottom left) as Vulkan's clip space (y down)
      const float W = float(screenW), H = float(screenH);
      float pc[12] = {2.0f * x / W - 1.0f, 1.0f - 2.0f * (y + h) / H, 2.0f * (x + w) / W - 1.0f, 1.0f - 2.0f * y / H,
                      1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f};
      // pre-rotation: the clip space turned as the display (x' = x cos - y sin, y' = x sin + y cos)
      if (swapTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) {
        pc[4] = 0.0f, pc[5] = -1.0f, pc[6] = 1.0f, pc[7] = 0.0f;
      } else if (swapTransform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) {
        pc[4] = -1.0f, pc[5] = 0.0f, pc[6] = 0.0f, pc[7] = -1.0f;
      } else if (swapTransform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
        pc[4] = 0.0f, pc[5] = 1.0f, pc[6] = -1.0f, pc[7] = 0.0f;
      }
      const float lines = static_cast<float>(xfbHeight ? xfbHeight : 1);
      if (rows && top + rows <= xfbHeight) {
        pc[8] = static_cast<float>(top) / lines;
        pc[9] = static_cast<float>(rows) / lines;
      }
      std::vector<std::uint8_t> bytes(48);
      std::memcpy(bytes.data(), pc, 48);
      const VkImageView view = xfb->view;
      BindFixed(swapPresent, &view, &linearClamp, 1, bytes);
      vkCmdDraw(cb, 4, 1, 0, 0);
      DrawOverlay(cb, pc + 4, swapPass, overlayPipe, screenW, screenH);
    }
    EndPass();
    const VkSemaphore done = renderSem[index];
    Submit(false, true, acquireSem[slot], done);
    auto pi = Make<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &index;
    const auto presented = std::chrono::steady_clock::now();
    r = vkQueuePresentKHR(vk.queue, &pi);
    timing.presentMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - presented).count();
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
      swapStale = true;
    else if (r == VK_SUBOPTIMAL_KHR)
      swapCheck = true;
    else if (r == VK_SUCCESS)
      suboptimalLooks = 0;  // (the streak ended)
    else if (r != VK_SUCCESS)
      Check(r, "vkQueuePresentKHR");
    return true;
  }
  unsigned EfbColor() const override { return 0; }
  // `img` to the CPU (RGBA8 rows from the top), the frame submitted and waited for
  bool ReadImage(Image &img, std::vector<std::uint8_t> *rgba) {
    Begin();
    EndPass();
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(img.width) * img.height * 4;
    if (readback.size < bytes) {
      FreeBuffer(readback);
      if (!MakeBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback))
        return false;
    }
    VkCommandBuffer cb = F().draw;
    const VkImageLayout was = img.layout;
    Transition(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {img.width, img.height, 1};
    vkCmdCopyImageToBuffer(cb, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &region);
    if (was == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
      Transition(cb, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    auto mb = Make<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0,
                         nullptr);
    Submit(true, true);
    rgba->assign(readback.map, readback.map + bytes);
    return true;
  }
  bool ReadXfb(std::vector<std::uint8_t> *rgba, unsigned *width, unsigned *height) override {
    Image *img = ShownImage();
    if (!haveXfb || !img)
      return false;
    if (!ReadImage(*img, rgba))
      return false;
    *width = img->width;
    *height = img->height;
    return true;
  }
  bool ReadPresented(int screenW, int screenH, int x, int y, int w, int h, unsigned top, unsigned rows,
                     std::vector<std::uint8_t> *rgba) override {
    if (!haveXfb || screenW < 1 || screenH < 1)
      return false;
    if (screen.width != static_cast<unsigned>(screenW) || screen.height != static_cast<unsigned>(screenH)) {
      if (screen.image) {
        Begin();
        Trash(screen);
      }
      if (!MakeImage(screen, static_cast<unsigned>(screenW), static_cast<unsigned>(screenH), 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    }
    Begin();
    stats.presents++;
    // Present's rectangle counts from the bottom left (GL's viewport): from the top here
    DrawPresent(screen, x, screenH - y - h, w, h, top, rows);
    EndPass();
    return ReadImage(screen, rgba);
  }

  // ---- the renderer ----
  bool InitHalves(int h, std::string *error) override {
    std::string dummy;
    if (!error)
      error = &dummy;
    halves = std::max(2, h);
    // the fallback to GLES tried on purpose (test switch, port/docs/vulkan-bauplan.md E6)
    if (const char *fail = std::getenv("RE4_VK_FAIL"); fail && *fail == '1') {
      *error = "made to fail on purpose (RE4_VK_FAIL=1)";
      return false;
    }
    ContextOptions o;
    const char *validation = std::getenv("RE4_VK_VALIDATION");
    o.validation = validation && *validation >= '1';
    o.syncValidation = validation && *validation == '2';
#ifdef __ANDROID__
    o.surface = true;  // the window's swapchain (Present)
#endif
    o.log = VkLogLine;
    if (!vk.device) {
      // the run's first renderer (or one after a failed start): the device; the next ones take it as it is
      vk.Destroy();
      if (!vk.Create(o, error))
        return false;
      heap.Init(&vk);
    }
    dev = vk.device;
    GXLOG("GPU (Vulkan): %s; %s; depth %s", vk.Describe().c_str(), CompilerName(),
          vk.depthClipControl ? "in GL's clip range (VK_EXT_depth_clip_control)" : "mapped to 0..1");
    const VkFormat depths[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT};
    depthFormat = vk.PickFormat(depths, 3, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    if (depthFormat == VK_FORMAT_UNDEFINED) {
      *error = "no depth format that can be drawn into and sampled";
      return false;
    }
    anisotropy = std::max(1.0f, std::min(options.anisotropy, vk.enabled.samplerAnisotropy
                                                                 ? vk.properties.limits.maxSamplerAnisotropy
                                                                 : 1.0f));
    // MSAA: the samples asked for where the device draws and samples them (colour, depth; 4 always can), else
    // fewer
    samples = 1;
    {
      const VkPhysicalDeviceLimits &l = vk.properties.limits;
      const VkSampleCountFlags can =
          l.framebufferColorSampleCounts & l.framebufferDepthSampleCounts & l.sampledImageDepthSampleCounts;
      for (int n = std::min(8, std::max(1, options.samples)); n > 1; n /= 2)
        if (can & static_cast<VkSampleCountFlags>(n)) {
          samples = n;
          break;
        }
    }
    if (const char *merge = std::getenv("RE4_GX_MERGE"))
      mergeOn = *merge == '1';
    // render passes: the EFB (what it held loaded), a copy's target (drawn over whole), the screen (cleared)
    efbPass = samples > 1 ? MakeMsaaPass(VK_FORMAT_R8G8B8A8_UNORM, depthFormat, static_cast<VkSampleCountFlagBits>(samples))
                          : MakePass(VK_FORMAT_R8G8B8A8_UNORM, depthFormat, VK_ATTACHMENT_LOAD_OP_LOAD,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (samples > 1)
      depthResolvePass = MakeDepthPass(depthFormat);
    copyPass = MakePass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    screenPass = MakePass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (!efbPass || !copyPass || !screenPass) {
      *error = "Vulkan render passes";
      return false;
    }
    // the layouts of the GX draws: set 0 the uniform blocks (dynamic offsets), set 1 the eight textures
    {
      VkDescriptorSetLayoutBinding b[kUboCount];
      for (int i = 0; i < kUboCount; i++)
        b[i] = {static_cast<std::uint32_t>(i), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
      auto dl = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
      dl.bindingCount = kUboCount;
      dl.pBindings = b;
      vkCreateDescriptorSetLayout(dev, &dl, nullptr, &uboLayout);
      VkDescriptorSetLayoutBinding t[8];
      for (int i = 0; i < 8; i++)
        t[i] = {static_cast<std::uint32_t>(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                nullptr};
      dl.bindingCount = 8;
      dl.pBindings = t;
      vkCreateDescriptorSetLayout(dev, &dl, nullptr, &texLayout);
      const VkDescriptorSetLayout sets[2] = {uboLayout, texLayout};
      VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkDrawConstants)};
      auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
      pl.setLayoutCount = 2;
      pl.pSetLayouts = sets;
      pl.pushConstantRangeCount = 1;
      pl.pPushConstantRanges = &range;
      vkCreatePipelineLayout(dev, &pl, nullptr, &gxLayout);
      VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 64 * kUboCount};
      auto pci = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
      pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
      pci.maxSets = 64;
      pci.poolSizeCount = 1;
      pci.pPoolSizes = &size;
      vkCreateDescriptorPool(dev, &pci, nullptr, &uboPool);
      if (!shared->pipelineCache) {
        auto cci = Make<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
        vkCreatePipelineCache(dev, &cci, nullptr, &shared->pipelineCache);
      }
      pipelineCache = shared->pipelineCache;
    }
    for (Frame &f : frames) {
      auto cpi = Make<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
      cpi.queueFamilyIndex = vk.family;
      vkCreateCommandPool(dev, &cpi, nullptr, &f.cmdPool);
      auto cai = Make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
      cai.commandPool = f.cmdPool;
      cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cai.commandBufferCount = 2;
      VkCommandBuffer cbs[2];
      vkAllocateCommandBuffers(dev, &cai, cbs);
      f.upload = cbs[0];
      f.draw = cbs[1];
      auto fci = Make<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
      vkCreateFence(dev, &fci, nullptr, &f.fence);
    }
    for (VkSemaphore &s : acquireSem) {
      auto sem = Make<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
      vkCreateSemaphore(dev, &sem, nullptr, &s);
    }
    linearClamp = PlainSampler(true);
    nearestClamp = PlainSampler(false);
    // the fixed shaders: the copy (the EFB into a copy), the display (row 0 at the top here: no flip)
    std::string presentFs = fixed::kPresentFs;
    const std::string flip = "rows.x + (1.0 - vUv.y) * rows.y";
    const std::size_t at = presentFs.find(flip);
    if (at != std::string::npos)
      presentFs.replace(at, flip.size(), "rows.x + vUv.y * rows.y");
    const VkColorComponentFlags all = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                      VK_COLOR_COMPONENT_A_BIT;
    if (samples > 1) {
      VkFixedLayout l;
      l.samplers = {"src"};
      if (!depthResolvePass ||
          !MakeFixed(depthResolve, kDepthResolveVs, kDepthResolveFs, depthResolvePass, 0, true, error, true, &l))
        return false;
    }
    if (!MakeFixed(copy, fixed::kQuadVs, fixed::kCopyFs, copyPass, all, false, error) ||
        !MakeFixed(present, fixed::kQuadVs, presentFs, screenPass, all, false, error) ||
        !MakeFixed(palette, fixed::kQuadVs, fixed::kPaletteFs, copyPass, all, false, error))
      return false;
    ready = true;
    if (!MakeEfb(error))
      return false;
    // the white stand-in texture
    const std::uint8_t white[4] = {255, 255, 255, 255};
    whiteTex = BeginTexture(1, 1, 1);
    TextureLevel(whiteTex, 0, 1, 1, white);
    EndTexture(whiteTex, 1);
    for (auto &b : bound)
      b.id = whiteTex;
    return true;
  }
  // the EFB: colour and depth at the scale, cleared to black and the far depth
  bool MakeEfb(std::string *error) {
    const unsigned w = static_cast<unsigned>(Lo(kEfbWidth)), hh = static_cast<unsigned>(Lo(kEfbHeight));
    if (!MakeImage(efbColor, w, hh, 1, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   VK_IMAGE_ASPECT_COLOR_BIT) ||
        !MakeImage(efbDepth, w, hh, 1, depthFormat,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                              : VK_IMAGE_ASPECT_DEPTH_BIT)) {
      *error = "the EFB's images";
      return false;
    }
    if (samples > 1) {
      const auto n = static_cast<VkSampleCountFlagBits>(samples);
      if (!MakeImage(efbMsColor, w, hh, 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, n) ||
          !MakeImage(efbMsDepth, w, hh, 1, depthFormat,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     efbDepth.aspect, n)) {
        *error = "the EFB's multisampled images";
        return false;
      }
      efbFb = MakeFb(efbPass, {efbMsColor.view, efbMsDepth.attach ? efbMsDepth.attach : efbMsDepth.view, efbColor.view},
                     w, hh);
      depthResolveFb = MakeFb(depthResolvePass, {efbDepth.attach ? efbDepth.attach : efbDepth.view}, w, hh);
    } else {
      efbFb = MakeFb(efbPass, {efbColor.view, efbDepth.attach ? efbDepth.attach : efbDepth.view}, w, hh);
    }
    Begin();
    VkCommandBuffer cb = F().draw;
    const VkClearColorValue clearColor{{0.0f, 0.0f, 0.0f, 0.0f}};
    const VkClearDepthStencilValue clearDepth{1.0f, 0};
    const VkImageSubresourceRange cr{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, dr{efbDepth.aspect, 0, 1, 0, 1};
    for (Image *c : {&efbColor, &efbMsColor})
      if (c->image) {
        Transition(cb, *c, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(cb, c->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &cr);
      }
    for (Image *d : {&efbDepth, &efbMsDepth})
      if (d->image) {
        Transition(cb, *d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearDepthStencilImage(cb, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearDepth, 1, &dr);
      }
    depthResolved = true;
    return true;
  }
  // `img` (a copy's or the XFB's, `cw` x `ch` in EFB pixels) as an image of this renderer's scale with the old
  // picture drawn over it scaled (GL: glBlitFramebuffer, linear); recorded in this frame, the old image to its trash
  bool Rescaled(Image &img, unsigned cw, unsigned ch) {
    Image made;
    const unsigned w = static_cast<unsigned>(Hi(static_cast<int>(cw))), h = static_cast<unsigned>(Hi(static_cast<int>(ch)));
    if (!img.image || !MakeImage(made, w, h, 1, VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT)) {
      Destroy(made);
      return false;
    }
    Begin();
    EndPass();
    VkCommandBuffer cb = F().draw;
    Transition(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    Transition(cb, made, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit b{};
    b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    b.dstSubresource = b.srcSubresource;
    b.srcOffsets[1] = {static_cast<std::int32_t>(img.width), static_cast<std::int32_t>(img.height), 1};
    b.dstOffsets[1] = {static_cast<std::int32_t>(w), static_cast<std::int32_t>(h), 1};
    vkCmdBlitImage(cb, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, made.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &b, VK_FILTER_LINEAR);
    Transition(cb, made, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Trash(img);
    img = made;
    return true;
  }
  // a step of "Dynamic" or a new scale while the game runs (GL's RescaleHalves): the EFB anew at the new size and
  // cleared, the XFB and the EFB copies scaled over (an effect that reads the picture before, a copy the game made
  // once at a room's start), the in-between pictures' own copies gone
  void RescaleHalves(int h) override {
    h = std::max(2, h);
    if (!ready || h == halves)
      return;
    stats.rescales++;
    Begin();
    EndPass();
    halves = h;
    for (Image *img : {&efbColor, &efbDepth, &efbMsColor, &efbMsDepth})
      Trash(*img);
    for (Image *img : {&blurImg, &halfImg})
      Trash(*img);
    for (VkFramebuffer *fbp : {&efbFb, &depthResolveFb, &efbColorFb})
      if (*fbp) {
        const VkDevice d = dev;
        const VkFramebuffer fb = *fbp;
        F().trash.push_back([d, fb]() { vkDestroyFramebuffer(d, fb, nullptr); });
        *fbp = VK_NULL_HANDLE;
      }
    std::string error;
    if (!MakeEfb(&error))
      GXLOG("Vulkan: the EFB at %d/2: %s", h, error.c_str());
    if (Image *x = ImageOf(xfbId))
      Rescaled(*x, xfbWidth, xfbHeight);
    // the filters' pictures are of the old size: the next display copy makes them anew
    for (Image *img : {&fxaaImg, &sharpImg, &psxImg, &psxPrevImg})
      Trash(*img);
    fxaaShown = sharpShown = psxShown = psxPrevValid = false;
    for (auto &c : copies)
      if (Image *img = ImageOf(c.second.id))
        Rescaled(*img, c.second.width, c.second.height);
    // (the copies read through a palette have the copy's size in EFB pixels, at every scale)
    for (auto &c : interpCopies)
      DeleteTexture(c.second.id);
    interpCopies.clear();
    stateValid = false;
    ResetBound();
  }
  // new picture options made a new renderer (core_mode.cpp): this one's EFB copies go over to it on the shared
  // device, at its scale (GL: HandOverCopies); a copy the game made once (a room's start) is read on
  void HandOverCopies(GxCore &to) override {
    if (to.api != GxApi::kVulkan)
      return;
    VkRenderer &n = static_cast<VkRenderer &>(to);
    if (&n == this || copies.empty() || !n.ready || n.dev != dev)
      return;
    unsigned moved = 0;
    for (auto &c : copies) {
      Copy copy = c.second;
      if (!ImageOf(copy.id) || n.copies.count(c.first))
        continue;
      std::unique_ptr<Image> img = std::move(images[copy.id - 1]);
      freeIds.push_back(copy.id);
      if (halves != n.halves)
        n.Rescaled(*img, copy.width, copy.height);
      copy.id = n.NewId(std::move(img));
      copy.serial = ++n.copySerial;
      n.copies[c.first] = copy;
      c.second.id = 0;  // the new renderer's now: this one's Release leaves it
      moved++;
    }
    copies.clear();
    n.stats.handedCopies += moved;
  }
  bool TiledMsaa() const override { return false; }
  std::string ApiInfo() const override { return dev ? vk.Describe() : "Vulkan (not started)"; }
  bool BaseVertex() const override { return true; }
  // the caches on disk (E8): the SPIR-V store, the driver's pipeline cache of the last run, the rooms' lists
  bool SetProgramCache(const std::string &folder) override {
    programCache.clear();
    if (folder.empty() || !dev)
      return false;
    programCache = folder;
    Spirv().Open(folder + "/vk-spirv.bin");
    pipelineFile = folder + "/vk-pipelines.bin";
    pipelinesSavedAt = std::chrono::steady_clock::now();
    if (!shared->diskMerged && pipelineCache) {
      shared->diskMerged = true;
      std::vector<char> data = ReadFileBytes(pipelineFile, 64u << 20);
      const char *why = nullptr;
      if (!data.empty() && !PipelineCacheFits(data, vk.properties, &why)) {
        GXLOG("Vulkan caches: the driver's pipeline cache of the last run (%zu KB) left out: %s", data.size() / 1024,
              why);
        data.clear();
      }
      if (!data.empty()) {
        auto cci = Make<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
        cci.initialDataSize = data.size();
        cci.pInitialData = data.data();
        VkPipelineCache last = VK_NULL_HANDLE;
        // (the driver takes it only when it is its own: another device or driver gives an empty cache)
        if (vkCreatePipelineCache(dev, &cci, nullptr, &last) == VK_SUCCESS) {
          vkMergePipelineCaches(dev, pipelineCache, 1, &last);
          vkDestroyPipelineCache(dev, last, nullptr);
        }
      }
      GXLOG("Vulkan caches: %zu shaders' SPIR-V, the driver's pipeline cache %zu KB from the last run",
            Spirv().Count(), data.size() / 1024);
    }
    return true;
  }
  // the room's programs and then its pipelines, made before their first draw a few milliseconds at a time (as GL's
  // Prewarm; the programs' SPIR-V comes from the store when a run before compiled it)
  bool Prewarm(double budgetMs) override {
    mergeWanted.clear();  // (merged draws make their programs at their first draw under Vulkan)
    if (programCache.empty() || !ready || room == ~0u)
      return false;
    const auto start = std::chrono::steady_clock::now();
    double ms = 0.0;
    bool made = false;
    auto spent = [&] {
      ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      return made && ms >= budgetMs;
    };
    if (room != pipesRoom)
      SwitchPipesRoom();
    if (!aheadRead)
      ReadAhead();
    const std::uint32_t centroid = options.centroid && samples > 1 ? 1 : 0;
    while (aheadNext < ahead.size() && !spent()) {
      AheadKey &a = ahead[aheadNext++];
      if (a.key.centroid != centroid || programs.count(a.key))
        continue;
      if (ProgramFor(a.key)) {
        stats.programsAhead++;
        aheadPrograms++;
      }
      made = true;
    }
    while (aheadNext >= ahead.size() && pipesNext < pipesAhead.size() && !spent()) {
      PipeRecord &r = pipesAhead[pipesNext++];
      if (r.k.merged || r.key.centroid != centroid)
        continue;
      Program *p = ProgramFor(r.key);
      if (!p)
        continue;
      PipeKey k = r.k;
      k.program = p;
      if (pipelines.count(k))
        continue;
      MakePipeline(k);
      aheadPipes++;
      made = true;
    }
    aheadSpentMs += ms;
    const bool more = aheadNext < ahead.size() || pipesNext < pipesAhead.size();
    if (!more && !aheadLogged && (aheadPrograms || aheadPipes)) {
      aheadLogged = true;
      GXLOG("Vulkan ahead of their first draw in r%03x: %u programs, %u pipelines, %.0f ms in all", room,
            aheadPrograms, aheadPipes, aheadSpentMs);
    }
    return more;
  }
  // ---- the timing line (TimingLine, the log every 300 chunks) ----
  struct Timing {
    double acquireMs = 0, fenceMs = 0, submitMs = 0, presentMs = 0, compileMs = 0, pipelineMs = 0, pipelineMax = 0;
    unsigned presents = 0, compiled = 0, spirvFound = 0, pipelines = 0, swapchains = 0;
  } timing;
  std::string TimingLine() override {
    Timing &t = timing;
    const double n = std::max(1u, t.presents);
    char line[512];
    std::snprintf(line, sizeof(line),
                  "Vulkan per picture: acquire %.2f ms, waits for the GPU %.2f ms, submit %.2f ms, present %.2f ms "
                  "(%u pictures); %u shaders compiled (%.0f ms), %u from the SPIR-V store (%zu kept); %u pipelines "
                  "made (%.0f ms, the longest %.1f ms); %u swapchains made; images %.0f of %.0f MB",
                  t.acquireMs / n, t.fenceMs / n, t.submitMs / n, t.presentMs / n, t.presents, t.compiled,
                  t.compileMs, t.spirvFound, Spirv().Count(), t.pipelines, t.pipelineMs, t.pipelineMax,
                  swapMade - t.swapchains, heap.Used() / 1048576.0, heap.Reserved() / 1048576.0);
    const unsigned made = swapMade;
    t = Timing();
    t.swapchains = made;
    return line;
  }
  void WaitFence(VkFence fence) {
    const auto t0 = std::chrono::steady_clock::now();
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
    timing.fenceMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
  void ApplyTestEnvironment() override {
    const char *tv = getenv("RE4_GX_TV_FILTER");
    tvFilter = tv && *tv == '1';
    const char *psx = getenv("RE4_GX_PSX");
    psxOn = psx && *psx == '1';
    const char *flares = getenv("RE4_GX_FLARES");
    flaresOn = flares && *flares == '1';
  }
  void Release() override {
    if (!dev)
      return;  // (the shared device goes with the last renderer)
    if (recording)
      Submit(true, false);
    vkDeviceWaitIdle(dev);
    DropSwapchain(true);
#ifdef __ANDROID__
    if (window)
      ANativeWindow_release(static_cast<ANativeWindow *>(window));
#endif
    window = nullptr;
    if (swapPass)
      vkDestroyRenderPass(dev, swapPass, nullptr);
    swapPass = VK_NULL_HANDLE;
    FreeFixed(swapPresent);
    FreeOverlay(true);
    for (VkSemaphore &s : acquireSem) {
      if (s)
        vkDestroySemaphore(dev, s, nullptr);
      s = VK_NULL_HANDLE;
    }
    if (roomDirty)
      SaveRoom();
    if (roomPipesDirty)
      SaveRoomPipes();
    SavePipelines(true);
    for (Frame &f : frames) {
      for (auto &t : f.trash)
        t();
      f.trash.clear();
      for (std::vector<Block> *list : {&f.uniform, &f.vertex, &f.staging})
        for (Block &b : *list)
          FreeBuffer(b);
      for (VkDescriptorPool p : f.pools)
        vkDestroyDescriptorPool(dev, p, nullptr);
      if (f.fence)
        vkDestroyFence(dev, f.fence, nullptr);
      if (f.cmdPool)
        vkDestroyCommandPool(dev, f.cmdPool, nullptr);
      f = Frame();
    }
    FreeBuffer(readback);
    Destroy(screen);
    for (auto &img : images)
      if (img)
        Destroy(*img);
    images.clear();
    freeIds.clear();
    textures.clear();
    copies.clear();
    paletted.clear();
    interpCopies.clear();
    Destroy(efbColor);
    Destroy(efbDepth);
    Destroy(efbMsColor);
    Destroy(efbMsDepth);
    if (efbFb)
      vkDestroyFramebuffer(dev, efbFb, nullptr);
    efbFb = VK_NULL_HANDLE;
    if (depthResolveFb)
      vkDestroyFramebuffer(dev, depthResolveFb, nullptr);
    depthResolveFb = VK_NULL_HANDLE;
    FreeFixed(depthResolve);
    for (Image *img : {&fxaaImg, &sharpImg, &psxImg, &psxPrevImg})
      Destroy(*img);
    for (Fixed *f : {&fxaaFx, &sharpFx, &psxFx, &flareVisFx, &flareFx, &focusFx, &blurFx, &halfFx, &mixFx, &dofBackFx})
      FreeFixed(*f);
    for (Image &v : flareVis)
      Destroy(v);
    for (Image *img : {&focusImg[0], &focusImg[1], &blurImg, &halfImg})
      Destroy(*img);
    if (efbColorFb)
      vkDestroyFramebuffer(dev, efbColorFb, nullptr);
    efbColorFb = VK_NULL_HANDLE;
    flareVisCur = 0;
    fxaaShown = sharpShown = psxShown = psxPrevValid = false;
    for (auto &p : pipelines)
      if (p.second)
        vkDestroyPipeline(dev, p.second, nullptr);
    pipelines.clear();
    for (auto &p : programs) {
      for (VkShaderModule m : {p.second.vs, p.second.fs, p.second.merged})
        if (m)
          vkDestroyShaderModule(dev, m, nullptr);
    }
    programs.clear();
    for (auto &s : samplers)
      vkDestroySampler(dev, s.second, nullptr);
    samplers.clear();
    for (VkSampler s : {linearClamp, nearestClamp})
      if (s)
        vkDestroySampler(dev, s, nullptr);
    FreeFixed(copy);
    FreeFixed(present);
    FreeFixed(palette);
    FreeFixed(peek);
    Destroy(peekImage);
    for (PeekSlot &s : peekSlot) {
      FreeBuffer(s.buffer);
      s.serial = 0;
    }
    peekReady = false;
    for (Fixed &f : clearFixed)
      FreeFixed(f);
    pipelineCache = VK_NULL_HANDLE;  // (the shared device's)
    if (uboPool)
      vkDestroyDescriptorPool(dev, uboPool, nullptr);
    if (gxLayout)
      vkDestroyPipelineLayout(dev, gxLayout, nullptr);
    for (VkDescriptorSetLayout l : {uboLayout, texLayout})
      if (l)
        vkDestroyDescriptorSetLayout(dev, l, nullptr);
    for (VkRenderPass p : {efbPass, copyPass, screenPass, depthResolvePass, efbColorPass})
      if (p)
        vkDestroyRenderPass(dev, p, nullptr);
    efbPass = copyPass = screenPass = depthResolvePass = efbColorPass = VK_NULL_HANDLE;
    const unsigned errors = vk.validationErrors.load(), warnings = vk.validationWarnings.load();
    if (vk.validation)
      GXLOG("Vulkan validation: %u errors, %u warnings", errors, warnings);
    if (samples > 1)
      GXLOG("Vulkan MSAA: %d samples, the depth resolved %u times", samples, depthResolveCount);
    dev = VK_NULL_HANDLE;
    ready = false;
  }
};

std::unique_ptr<GxCore> MakeVkCore() { return std::unique_ptr<GxCore>(new VkRenderer()); }

} // namespace re4gx
