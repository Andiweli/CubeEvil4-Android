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
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

} // namespace

struct VkRenderer final : GxCore {
  VkRenderer() : GxCore(GxApi::kVulkan) {}
  ~VkRenderer() override { Release(); }

  Context vk;
  ImageHeap heap;
  VkDevice dev = VK_NULL_HANDLE;
  VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  VkRenderPass efbPass = VK_NULL_HANDLE, copyPass = VK_NULL_HANDLE, screenPass = VK_NULL_HANDLE;
  Image efbColor, efbDepth;
  VkFramebuffer efbFb = VK_NULL_HANDLE;
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
                 VkImageAspectFlags aspect) {
    auto ici = Make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {std::max(1u, w), std::max(1u, h), 1};
    ici.mipLevels = levels;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
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
      vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);
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
  void Submit(bool wait, bool next) {
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
    vkResetFences(dev, 1, &f.fence);
    const VkResult r = vkQueueSubmit(vk.queue, 1, &si, f.fence);
    Check(r, "vkQueueSubmit");
    f.pending = r == VK_SUCCESS;
    recording = false;
    if (wait && f.pending) {
      vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX);
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
    // before: what read or wrote the attachments earlier (the copies that sampled them); after: the reads
    // that follow (a texture, a copy, a transfer)
    VkSubpassDependency dep[2]{};
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
    dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
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
    Transition(cb, efbColor, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Transition(cb, efbDepth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
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
    if (!CompileGlsl(vertex ? ShaderStage::kVertex : ShaderStage::kFragment, glsl, &spirv, log))
      return VK_NULL_HANDLE;
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
    return made;
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
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto cb = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cb.attachmentCount = 1;
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
                 VkColorComponentFlags mask, bool depthWrite, std::string *error, bool vkGlsl = false) {
    VkFixedLayout vl, fl;
    const std::string vs = vkGlsl ? vsEs : VulkanFixedGlsl(vsEs, true, &vl);
    const std::string fs = vkGlsl ? fsEs : VulkanFixedGlsl(fsEs, false, &fl);
    if (vkGlsl) {
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
    std::vector<VkDescriptorSetLayoutBinding> b(fl.samplers.size());
    for (std::size_t i = 0; i < b.size(); i++)
      b[i] = {static_cast<std::uint32_t>(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
              nullptr};
    auto dl = Make<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    dl.bindingCount = static_cast<std::uint32_t>(b.size());
    dl.pBindings = b.data();
    vkCreateDescriptorSetLayout(dev, &dl, nullptr, &f.setLayout);
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, std::max(16u, fl.bytes)};
    auto pl = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &f.setLayout;
    pl.pushConstantRangeCount = fl.bytes ? 1 : 0;
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
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
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
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kTexSetsPerPool * 8};
        auto pci = Make<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pci.maxSets = kTexSetsPerPool;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &size;
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
  void BindFixed(const Fixed &f, const VkImageView *views, const VkSampler *samp, std::uint32_t n,
                 const std::vector<std::uint8_t> &pc) {
    VkCommandBuffer cb = F().draw;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, f.pipeline);
    boundPipeline = VK_NULL_HANDLE;
    if (n) {
      const VkDescriptorSet set = TexSet(f.setLayout);
      WriteImages(set, views, samp, n);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, f.layout, 0, 1, &set, 0, nullptr);
      boundTex = boundUbo = VK_NULL_HANDLE;
    }
    if (!pc.empty())
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
  void FlarePass() override { flareDone = true; }  // E5
  void Autofocus(std::uint32_t) override {}         // E5
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
  void DisplayEffects(int, int, int, int, int) override {}  // E5: FXAA, Sharpen, PSone
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
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT))
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
    Image *xfb = ImageOf(xfbId);
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
  }
  bool Present(int, int, int, int, unsigned, unsigned) override {
    // the screen (E6: the swapchain); for now the frame goes to the GPU
    if (!haveXfb)
      return false;
    stats.presents++;
    Submit(false, true);
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
    Image *img = ImageOf(xfbId);
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
    ContextOptions o;
    const char *validation = std::getenv("RE4_VK_VALIDATION");
    o.validation = validation && *validation >= '1';
    o.syncValidation = validation && *validation == '2';
    o.log = VkLogLine;
    if (!vk.Create(o, error))
      return false;
    dev = vk.device;
    heap.Init(&vk);
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
    samples = 1;  // E4: MSAA
    if (const char *merge = std::getenv("RE4_GX_MERGE"))
      mergeOn = *merge == '1';
    // render passes: the EFB (what it held loaded), a copy's target (drawn over whole), the screen (cleared)
    efbPass = MakePass(VK_FORMAT_R8G8B8A8_UNORM, depthFormat, VK_ATTACHMENT_LOAD_OP_LOAD,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
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
      auto cci = Make<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
      vkCreatePipelineCache(dev, &cci, nullptr, &pipelineCache);
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
    if (!MakeFixed(copy, fixed::kQuadVs, fixed::kCopyFs, copyPass, all, false, error) ||
        !MakeFixed(present, fixed::kQuadVs, presentFs, screenPass, all, false, error) ||
        !MakeFixed(palette, fixed::kQuadVs, fixed::kPaletteFs, copyPass, all, false, error))
      return false;
    ready = true;
    // the EFB: colour and depth at the scale, cleared to black and the far depth
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
    efbFb = MakeFb(efbPass, {efbColor.view, efbDepth.attach ? efbDepth.attach : efbDepth.view}, w, hh);
    Begin();
    VkCommandBuffer cb = F().draw;
    Transition(cb, efbColor, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    Transition(cb, efbDepth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearColorValue clearColor{{0.0f, 0.0f, 0.0f, 0.0f}};
    const VkClearDepthStencilValue clearDepth{1.0f, 0};
    const VkImageSubresourceRange cr{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, dr{efbDepth.aspect, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, efbColor.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &cr);
    vkCmdClearDepthStencilImage(cb, efbDepth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearDepth, 1, &dr);
    // the white stand-in texture
    const std::uint8_t white[4] = {255, 255, 255, 255};
    whiteTex = BeginTexture(1, 1, 1);
    TextureLevel(whiteTex, 0, 1, 1, white);
    EndTexture(whiteTex, 1);
    for (auto &b : bound)
      b.id = whiteTex;
    return true;
  }
  void RescaleHalves(int) override {
    static bool told = false;
    if (!told) {
      told = true;
      GXLOG("Vulkan: a change of the EFB scale while running is not made yet (E4); the scale stays");
    }
  }
  void HandOverCopies(GxCore &) override {}  // E4
  bool TiledMsaa() const override { return false; }
  bool BaseVertex() const override { return true; }
  bool SetProgramCache(const std::string &) override { return false; }  // E8
  bool Prewarm(double) override { return false; }
  void ApplyTestEnvironment() override {
    const char *tv = getenv("RE4_GX_TV_FILTER");
    tvFilter = tv && *tv == '1';
    const char *psx = getenv("RE4_GX_PSX");
    psxOn = psx && *psx == '1';
    const char *flares = getenv("RE4_GX_FLARES");
    flaresOn = flares && *flares == '1';
  }
  void Release() override {
    if (!dev) {
      vk.Destroy();
      return;
    }
    if (recording)
      Submit(true, false);
    vkDeviceWaitIdle(dev);
    if (roomDirty)
      SaveRoom();
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
    if (efbFb)
      vkDestroyFramebuffer(dev, efbFb, nullptr);
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
    if (pipelineCache)
      vkDestroyPipelineCache(dev, pipelineCache, nullptr);
    if (uboPool)
      vkDestroyDescriptorPool(dev, uboPool, nullptr);
    if (gxLayout)
      vkDestroyPipelineLayout(dev, gxLayout, nullptr);
    for (VkDescriptorSetLayout l : {uboLayout, texLayout})
      if (l)
        vkDestroyDescriptorSetLayout(dev, l, nullptr);
    for (VkRenderPass p : {efbPass, copyPass, screenPass})
      if (p)
        vkDestroyRenderPass(dev, p, nullptr);
    const unsigned errors = vk.validationErrors.load(), warnings = vk.validationWarnings.load();
    if (vk.validation)
      GXLOG("Vulkan validation: %u errors, %u warnings", errors, warnings);
    heap.Destroy();
    dev = VK_NULL_HANDLE;
    ready = false;
    vk.Destroy();
  }
};

std::unique_ptr<GxCore> MakeVkCore() { return std::unique_ptr<GxCore>(new VkRenderer()); }

} // namespace re4gx
