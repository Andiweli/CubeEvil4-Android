// Device memory for the Vulkan renderer's images (port/docs/vulkan-bauplan.md: no VMA, a small allocator of our
// own). Android drivers often allow only 4096 allocations (maxMemoryAllocationCount) while the game keeps a few
// thousand textures and copies: images share blocks of 32 MB (larger ones get a block of their own), placed first-fit,
// neighbouring free ranges merged again. Optimal-tiling images only (no buffers in the same blocks, so the buffer /
// image granularity does not matter). One thread.
#pragma once

#include "vk_api.h"

#include <cstdint>
#include <map>
#include <vector>

namespace re4vk {

class Context;

class ImageHeap {
public:
  struct Allocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0, size = 0;
    int block = -1;
  };
  explicit ImageHeap(VkDeviceSize blockBytes = VkDeviceSize(32) << 20) : blockBytes_(blockBytes) {}
  ~ImageHeap() { Destroy(); }
  void Init(const Context *vk) { vk_ = vk; }
  // memory for `image` (device-local where there is such memory), bound; false when there is none left
  bool Bind(VkImage image, Allocation *a);
  void Free(Allocation &a);
  void Destroy();
  // for the log: blocks, their bytes, the bytes in use
  unsigned Blocks() const;
  VkDeviceSize Reserved() const;
  VkDeviceSize Used() const { return used_; }

private:
  struct Block {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    std::uint32_t type = 0;
    bool own = false;                            // a single large image
    std::map<VkDeviceSize, VkDeviceSize> free;  // offset -> size
  };
  const Context *vk_ = nullptr;
  VkDeviceSize blockBytes_;
  VkDeviceSize used_ = 0;
  std::vector<Block> blocks_;
  bool Take(Block &b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *offset);
};

} // namespace re4vk
