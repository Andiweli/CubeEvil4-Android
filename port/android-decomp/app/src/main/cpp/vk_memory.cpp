// Device memory for the Vulkan renderer's images (vk_memory.h).
#include "vk_memory.h"

#include "vk_context.h"

#include <algorithm>
#include <iterator>

namespace re4vk {

bool ImageHeap::Take(Block &b, VkDeviceSize size, VkDeviceSize align, VkDeviceSize *offset) {
  for (auto it = b.free.begin(); it != b.free.end(); ++it) {
    const VkDeviceSize start = (it->first + align - 1) / align * align;
    const VkDeviceSize end = it->first + it->second;
    if (start + size > end)
      continue;
    const VkDeviceSize before = start - it->first, after = end - (start + size);
    const VkDeviceSize at = it->first;
    b.free.erase(it);
    if (before)
      b.free[at] = before;
    if (after)
      b.free[start + size] = after;
    *offset = start;
    return true;
  }
  return false;
}

bool ImageHeap::Bind(VkImage image, Allocation *a) {
  if (!vk_)
    return false;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(vk_->device, image, &req);
  std::uint32_t type = vk_->MemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == ~0u)
    type = vk_->MemoryType(req.memoryTypeBits, 0);
  if (type == ~0u)
    return false;
  const VkDeviceSize align = req.alignment ? req.alignment : 1;
  // a block of this type with room
  for (std::size_t i = 0; i < blocks_.size(); i++) {
    Block &b = blocks_[i];
    VkDeviceSize offset = 0;
    if (b.memory && !b.own && b.type == type && Take(b, req.size, align, &offset)) {
      if (vkBindImageMemory(vk_->device, image, b.memory, offset) != VK_SUCCESS) {
        Allocation back{b.memory, offset, req.size, static_cast<int>(i)};
        Free(back);
        return false;
      }
      *a = {b.memory, offset, req.size, static_cast<int>(i)};
      used_ += req.size;
      return true;
    }
  }
  // a new block (a large image: one of its own)
  Block b;
  b.type = type;
  b.own = req.size > blockBytes_ / 2;
  b.size = b.own ? req.size : blockBytes_;
  auto mai = Make<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
  mai.allocationSize = b.size;
  mai.memoryTypeIndex = type;
  if (vkAllocateMemory(vk_->device, &mai, nullptr, &b.memory) != VK_SUCCESS)
    return false;
  if (!b.own)
    b.free[req.size] = b.size - req.size;
  if (vkBindImageMemory(vk_->device, image, b.memory, 0) != VK_SUCCESS) {
    vkFreeMemory(vk_->device, b.memory, nullptr);
    return false;
  }
  // (a slot of a freed block again)
  std::size_t slot = blocks_.size();
  for (std::size_t i = 0; i < blocks_.size(); i++)
    if (!blocks_[i].memory) {
      slot = i;
      break;
    }
  if (slot == blocks_.size())
    blocks_.push_back(b);
  else
    blocks_[slot] = b;
  *a = {b.memory, 0, req.size, static_cast<int>(slot)};
  used_ += req.size;
  return true;
}

void ImageHeap::Free(Allocation &a) {
  if (a.block < 0 || static_cast<std::size_t>(a.block) >= blocks_.size() || !vk_) {
    a = Allocation();
    return;
  }
  Block &b = blocks_[static_cast<std::size_t>(a.block)];
  used_ -= std::min(used_, a.size);
  if (b.own) {
    vkFreeMemory(vk_->device, b.memory, nullptr);
    b = Block();
    a = Allocation();
    return;
  }
  // back into the free list, merged with its neighbours
  VkDeviceSize start = a.offset, size = a.size;
  auto next = b.free.lower_bound(start);
  if (next != b.free.end() && next->first == start + size) {
    size += next->second;
    next = b.free.erase(next);
  }
  if (next != b.free.begin()) {
    auto prev = std::prev(next);
    if (prev->first + prev->second == start) {
      start = prev->first;
      size += prev->second;
      b.free.erase(prev);
    }
  }
  b.free[start] = size;
  // an empty block goes (the device's memory back), unless it is the only one of its type
  if (b.free.size() == 1 && b.free.begin()->first == 0 && b.free.begin()->second == b.size) {
    unsigned same = 0;
    for (const Block &o : blocks_)
      if (o.memory && !o.own && o.type == b.type)
        same++;
    if (same > 1) {
      vkFreeMemory(vk_->device, b.memory, nullptr);
      b = Block();
    }
  }
  a = Allocation();
}

void ImageHeap::Destroy() {
  if (vk_ && vk_->device)
    for (Block &b : blocks_)
      if (b.memory)
        vkFreeMemory(vk_->device, b.memory, nullptr);
  blocks_.clear();
  used_ = 0;
}

unsigned ImageHeap::Blocks() const {
  unsigned n = 0;
  for (const Block &b : blocks_)
    n += b.memory ? 1 : 0;
  return n;
}

VkDeviceSize ImageHeap::Reserved() const {
  VkDeviceSize n = 0;
  for (const Block &b : blocks_)
    n += b.memory ? b.size : 0;
  return n;
}

} // namespace re4vk
