// vk_probe: the first check of the Vulkan renderer's tools (port/docs/vulkan-bauplan.md, E0). On the PC (WSL with
// Mesa's lavapipe and the Khronos validation layer, ~/vk-tools/env.sh): creates the device (vk_context), translates a
// vertex and a fragment shader with glslang (vk_shader), draws a triangle into a 64 x 64 RGBA8 image with a 24-bit
// depth buffer, reads the pixels back and checks them; the pixel rows lie in memory as the GX renderer expects them
// (row 0 = NDC y -1 = the EFB's row 0, as in GL). Exit code 0 when the pictures are right and the validation layer
// reported nothing.
//
//   . ~/vk-tools/env.sh && build/vk_probe [--sync]
#include "vk_context.h"
#include "vk_shader.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace re4vk;

namespace {

int failures = 0;

void Check(bool ok, const char *what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok)
    failures++;
}

void Line(const char *line) { std::printf("    [vk] %s\n", line); }

const char kVs[] = R"(#version 450
layout(location = 0) out vec3 vColor;
void main() {
  // a triangle over the upper left half of the picture (NDC y -1 = row 0) and its corner colours
  const vec2 p[3] = vec2[3](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0));
  const vec3 c[3] = vec3[3](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
  vColor = c[gl_VertexIndex];
  // GX depth 0..1 straight (Vulkan's clip z is 0..w, GL's -w..w)
  gl_Position = vec4(p[gl_VertexIndex], 0.25, 1.0);
}
)";

const char kFs[] = R"(#version 450
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 oColor;
layout(push_constant) uniform P { vec4 tint; } uP;
void main() { oColor = vec4(vColor, 1.0) * uP.tint; }
)";

} // namespace

int main(int argc, char **argv) {
  bool sync = false;
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], "--sync") == 0)
      sync = true;
  std::printf("vk_probe: %s\n", CompilerName());
  ContextOptions options;
  options.validation = true;
  options.syncValidation = sync;
  options.minApi = VK_API_VERSION_1_0;
  options.log = Line;
  Context vk;
  std::string error;
  if (!vk.Create(options, &error)) {
    std::printf("FAIL Vulkan: %s\n", error.c_str());
    return 1;
  }
  std::printf("  %s\n  validation layer %s%s\n", vk.Describe().c_str(), vk.validation ? "on" : "not installed",
              vk.validation && sync ? " (with synchronization checks)" : "");

  // shaders
  std::vector<std::uint32_t> vsCode, fsCode;
  std::string log;
  const auto t0 = std::chrono::steady_clock::now();
  const bool vsOk = CompileGlsl(ShaderStage::kVertex, kVs, &vsCode, &log);
  if (!vsOk)
    std::printf("%s\n", log.c_str());
  const bool fsOk = CompileGlsl(ShaderStage::kFragment, kFs, &fsCode, &log);
  if (!fsOk)
    std::printf("%s\n", log.c_str());
  const double compileMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  Check(vsOk && fsOk, "GLSL 450 to SPIR-V (glslang)");
  std::printf("    %zu + %zu words, %.1f ms\n", vsCode.size(), fsCode.size(), compileMs);
  // a broken shader is refused with a message
  std::vector<std::uint32_t> badCode;
  std::string badLog;
  Check(!CompileGlsl(ShaderStage::kFragment, "#version 450\nvoid main() { undefined_call(); }\n", &badCode, &badLog) &&
            badLog.find("undefined_call") != std::string::npos,
        "a wrong shader is refused, with glslang's message");
  if (!vsOk || !fsOk)
    return 1;

  const VkDevice d = vk.device;
  const std::uint32_t W = 64, H = 64;
  // the depth format of the EFB: 24 bits when the device has it as an attachment that can be sampled
  const VkFormat depthFormats[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT};
  const VkFormat depthFormat =
      vk.PickFormat(depthFormats, 3, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  Check(depthFormat != VK_FORMAT_UNDEFINED, "a depth format that can be drawn into and read as a texture");
  std::printf("    depth format %d\n", static_cast<int>(depthFormat));

  // images: colour (drawn, copied out) and depth
  const auto makeImage = [&](VkFormat format, VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *mem) {
    auto ici = Make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {W, H, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(d, &ici, nullptr, image);
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(d, *image, &req);
    auto mai = Make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = vk.MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == ~0u)
      mai.memoryTypeIndex = vk.MemoryType(req.memoryTypeBits, 0);
    vkAllocateMemory(d, &mai, nullptr, mem);
    vkBindImageMemory(d, *image, *mem, 0);
  };
  VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE;
  VkDeviceMemory colorMem = VK_NULL_HANDLE, depthMem = VK_NULL_HANDLE;
  makeImage(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, &color,
            &colorMem);
  makeImage(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &depth, &depthMem);
  const auto makeView = [&](VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    auto vci = Make<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vci.image = image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView v = VK_NULL_HANDLE;
    vkCreateImageView(d, &vci, nullptr, &v);
    return v;
  };
  const bool depthStencil = depthFormat == VK_FORMAT_D24_UNORM_S8_UINT;
  const VkImageView colorView = makeView(color, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
  const VkImageView depthView = makeView(depth, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

  // render pass: clear both, keep the colour for the copy
  VkAttachmentDescription att[2]{};
  att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
  att[0].samples = VK_SAMPLE_COUNT_1_BIT;
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  att[1] = att[0];
  att[1].format = depthFormat;
  att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;
  sub.pDepthStencilAttachment = &depthRef;
  VkSubpassDependency dep{};
  dep.srcSubpass = 0;
  dep.dstSubpass = VK_SUBPASS_EXTERNAL;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dep.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  auto rci = Make<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
  rci.attachmentCount = 2;
  rci.pAttachments = att;
  rci.subpassCount = 1;
  rci.pSubpasses = &sub;
  rci.dependencyCount = 1;
  rci.pDependencies = &dep;
  VkRenderPass pass = VK_NULL_HANDLE;
  vkCreateRenderPass(d, &rci, nullptr, &pass);
  VkImageView views[2] = {colorView, depthView};
  auto fci = Make<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
  fci.renderPass = pass;
  fci.attachmentCount = 2;
  fci.pAttachments = views;
  fci.width = W;
  fci.height = H;
  fci.layers = 1;
  VkFramebuffer fb = VK_NULL_HANDLE;
  vkCreateFramebuffer(d, &fci, nullptr, &fb);

  // pipeline
  const auto makeModule = [&](const std::vector<std::uint32_t> &code) {
    auto sci = Make<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    sci.codeSize = code.size() * 4;
    sci.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(d, &sci, nullptr, &m);
    return m;
  };
  const VkShaderModule vsMod = makeModule(vsCode), fsMod = makeModule(fsCode);
  VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
  auto plci = Make<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &pcr;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  vkCreatePipelineLayout(d, &plci, nullptr, &layout);
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vsMod;
  stages[0].pName = "main";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fsMod;
  stages[1].pName = "main";
  auto vin = Make<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
  auto ia = Make<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  auto vp = Make<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  auto rs = Make<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  auto ms = Make<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  auto ds = Make<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = 0xF;
  auto cb = Make<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;
  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  auto dy = Make<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dyn;
  auto gci = Make<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
  gci.stageCount = 2;
  gci.pStages = stages;
  gci.pVertexInputState = &vin;
  gci.pInputAssemblyState = &ia;
  gci.pViewportState = &vp;
  gci.pRasterizationState = &rs;
  gci.pMultisampleState = &ms;
  gci.pDepthStencilState = &ds;
  gci.pColorBlendState = &cb;
  gci.pDynamicState = &dy;
  gci.layout = layout;
  gci.renderPass = pass;
  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult pr = vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gci, nullptr, &pipeline);
  Check(pr == VK_SUCCESS, "graphics pipeline");

  // read-back buffer
  auto bci = Make<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
  bci.size = W * H * 4;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer readBuf = VK_NULL_HANDLE;
  vkCreateBuffer(d, &bci, nullptr, &readBuf);
  VkMemoryRequirements breq{};
  vkGetBufferMemoryRequirements(d, readBuf, &breq);
  auto bmai = Make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
  bmai.allocationSize = breq.size;
  bmai.memoryTypeIndex = vk.MemoryType(breq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  VkDeviceMemory readMem = VK_NULL_HANDLE;
  vkAllocateMemory(d, &bmai, nullptr, &readMem);
  vkBindBufferMemory(d, readBuf, readMem, 0);

  // record, submit, wait
  auto cpci = Make<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
  cpci.queueFamilyIndex = vk.family;
  VkCommandPool pool = VK_NULL_HANDLE;
  vkCreateCommandPool(d, &cpci, nullptr, &pool);
  auto cbai = Make<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
  cbai.commandPool = pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(d, &cbai, &cmd);
  auto cbbi = Make<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
  cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &cbbi);
  VkClearValue clears[2]{};
  clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  clears[1].depthStencil = {1.0f, 0};
  auto rpbi = Make<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
  rpbi.renderPass = pass;
  rpbi.framebuffer = fb;
  rpbi.renderArea = {{0, 0}, {W, H}};
  rpbi.clearValueCount = 2;
  rpbi.pClearValues = clears;
  vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  const VkViewport viewport{0.0f, 0.0f, float(W), float(H), 0.0f, 1.0f};
  const VkRect2D scissor{{0, 0}, {W, H}};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, tint);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRenderPass(cmd);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {W, H, 1};
  vkCmdCopyImageToBuffer(cmd, color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readBuf, 1, &region);
  auto hostRead = Make<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
  hostRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  hostRead.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  hostRead.srcQueueFamilyIndex = hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hostRead.buffer = readBuf;
  hostRead.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hostRead, 0,
                       nullptr);
  vkEndCommandBuffer(cmd);
  auto fnci = Make<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(d, &fnci, nullptr, &fence);
  auto si = Make<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(vk.queue, 1, &si, fence);
  const VkResult wr = vkWaitForFences(d, 1, &fence, VK_TRUE, 5000000000ull);
  Check(wr == VK_SUCCESS, "submitted and finished");

  void *mapped = nullptr;
  vkMapMemory(d, readMem, 0, VK_WHOLE_SIZE, 0, &mapped);
  auto range = Make<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
  range.memory = readMem;
  range.size = VK_WHOLE_SIZE;
  vkInvalidateMappedMemoryRanges(d, 1, &range);
  const std::uint8_t *px = static_cast<const std::uint8_t *>(mapped);
  const auto at = [&](unsigned x, unsigned y) { return px + (y * W + x) * 4; };
  // memory row 0 is NDC y -1: the triangle's red corner sits at the first row's left, green at its right end,
  // blue at the last row's left; the lower right half stays clear
  const std::uint8_t *tl = at(0, 0), *tr = at(W - 2, 0), *bl = at(0, H - 2), *br = at(W - 1, H - 1), *mid = at(16, 16);
  std::printf("    row 0 left %u %u %u, row 0 right %u %u %u, last row left %u %u %u, lower right %u %u %u\n", tl[0], tl[1],
              tl[2], tr[0], tr[1], tr[2], bl[0], bl[1], bl[2], br[0], br[1], br[2]);
  Check(tl[0] > 240 && tl[1] < 16 && tl[2] < 16, "memory row 0 is NDC y -1 (red corner at row 0, column 0)");
  Check(tr[1] > 200 && tr[0] < 40, "green corner at row 0's right end");
  Check(bl[2] > 200 && bl[0] < 40, "blue corner at the last row's left");
  Check(br[0] == 0 && br[1] == 0 && br[2] == 0 && br[3] == 255, "the lower right half keeps the clear colour");
  Check(mid[0] > 0 && mid[1] > 0 && mid[2] > 0, "colours interpolated inside");
  vkUnmapMemory(d, readMem);

  vkDestroyFence(d, fence, nullptr);
  vkDestroyCommandPool(d, pool, nullptr);
  vkDestroyBuffer(d, readBuf, nullptr);
  vkFreeMemory(d, readMem, nullptr);
  vkDestroyPipeline(d, pipeline, nullptr);
  vkDestroyPipelineLayout(d, layout, nullptr);
  vkDestroyShaderModule(d, vsMod, nullptr);
  vkDestroyShaderModule(d, fsMod, nullptr);
  vkDestroyFramebuffer(d, fb, nullptr);
  vkDestroyRenderPass(d, pass, nullptr);
  vkDestroyImageView(d, colorView, nullptr);
  vkDestroyImageView(d, depthView, nullptr);
  vkDestroyImage(d, color, nullptr);
  vkDestroyImage(d, depth, nullptr);
  vkFreeMemory(d, colorMem, nullptr);
  vkFreeMemory(d, depthMem, nullptr);
  (void)depthStencil;
  vk.Destroy();
  const unsigned errors = vk.validationErrors.load(), warnings = vk.validationWarnings.load();
  Check(errors == 0, "validation layer: no errors");
  std::printf("  validation: %u errors, %u warnings\n", errors, warnings);
  std::printf("vk_probe: %s (%d failed)\n", failures ? "FAILED" : "all ok", failures);
  return failures ? 1 : 0;
}
