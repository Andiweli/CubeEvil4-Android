// The Vulkan instance, physical device, device and queue of the Vulkan renderer (port/docs/vulkan-bauplan.md, E0).
// One per renderer; it also says what the device can do (features, limits, formats) and, for the error report,
// what it is ("Vulkan 1.1.128, Adreno (TM) 650, driver Qualcomm 512.502 ...").
#pragma once

#include "vk_api.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace re4vk {

struct ContextOptions {
  // the Khronos validation layer (PC tests, debug APKs) with its messages to `log`; ignored when not installed
  bool validation = false;
  // and its synchronization checks (slower; finds missing barriers)
  bool syncValidation = false;
  // VK_KHR_surface and the platform's surface (Android), VK_KHR_swapchain on the device
  bool surface = false;
  // the oldest Vulkan the device must have (VK_API_VERSION_1_1 for the app, bauplan question 1)
  std::uint32_t minApi = VK_API_VERSION_1_0;
  // a line of the log (the renderer's LogTo); null: none
  void (*log)(const char *line) = nullptr;
};

class Context {
public:
  Context() = default;
  ~Context() { Destroy(); }
  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;

  // false with *error when there is no usable device (no loader, too old, no graphics queue ...)
  bool Create(const ContextOptions &options, std::string *error);
  void Destroy();

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t family = 0;
  std::uint32_t apiVersion = 0;  // of the device, capped by the instance's
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceFeatures enabled{};  // the features turned on (what the renderer may use)
  VkPhysicalDeviceMemoryProperties memory{};
  std::string driverName, driverInfo;  // VK_KHR_driver_properties / Vulkan 1.2 (empty without)
  std::vector<std::string> deviceExtensions;
  bool validation = false;  // the layer is on
  // VK_EXT_depth_clip_control is on: clip space z from -w to w, as GL's (the GX shaders keep GL's depth formula and
  // round as GL does; lavapipe and newer drivers have it)
  bool depthClipControl = false;
  std::atomic<unsigned> validationErrors{0}, validationWarnings{0};

  bool HasExtension(const char *name) const;
  // A memory type of `bits` with all of `need`, preferring one that also has `prefer`; ~0u when none.
  std::uint32_t MemoryType(std::uint32_t bits, VkMemoryPropertyFlags need, VkMemoryPropertyFlags prefer = 0) const;
  // The first of `formats` usable as `features` of optimally tiled images; VK_FORMAT_UNDEFINED when none.
  VkFormat PickFormat(const VkFormat *formats, int count, VkFormatFeatureFlags features) const;
  // For the log and the error report.
  std::string Describe() const;
  void Log(const char *fmt, ...) const __attribute__((format(printf, 2, 3)));

private:
  ContextOptions options_;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  static VKAPI_ATTR VkBool32 VKAPI_CALL Message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                               VkDebugUtilsMessageTypeFlagsEXT types,
                                               const VkDebugUtilsMessengerCallbackDataEXT *data, void *self);
};

// "1.3.275"
std::string VersionText(std::uint32_t version);

} // namespace re4vk
