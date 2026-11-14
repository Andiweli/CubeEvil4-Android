// Vulkan entry points of the Vulkan renderer (port/docs/vulkan-bauplan.md), loaded at run time: libvulkan.so is
// opened with dlopen and never linked, so the app still starts where there is no Vulkan (Android 7 devices
// without it, emulators) and GLES draws. Every function the renderer calls is a global pointer of the same name
// (re4vk::vkCreateInstance ...: in a namespace, so they never clash with the loader's own exported symbols), filled
// by LoadVulkan, LoadInstance and LoadDevice; extension functions a driver does not have stay null.
#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#if defined(__ANDROID__) && !defined(VK_USE_PLATFORM_ANDROID_KHR)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>

#include <string>

// functions from the loader itself
#define RE4_VK_GLOBAL(X)                                                                                              \
  X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties) X(vkEnumerateInstanceLayerProperties)              \
  X(vkEnumerateInstanceVersion)

// functions of an instance (and its physical devices)
#define RE4_VK_INSTANCE_CORE(X)                                                                                       \
  X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                               \
  X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
  X(vkGetPhysicalDeviceFormatProperties) X(vkGetPhysicalDeviceImageFormatProperties)                                \
  X(vkEnumerateDeviceExtensionProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)
// (Vulkan 1.1 or VK_KHR_get_physical_device_properties2; the surface and debug extensions)
#define RE4_VK_INSTANCE_OPTIONAL(X)                                                                                   \
  X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures2) X(vkCreateDebugUtilsMessengerEXT)               \
  X(vkDestroyDebugUtilsMessengerEXT) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR)                 \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)                              \
  X(vkGetPhysicalDeviceSurfacePresentModesKHR) RE4_VK_INSTANCE_PLATFORM(X)
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
#define RE4_VK_INSTANCE_PLATFORM(X) X(vkCreateAndroidSurfaceKHR)
#else
#define RE4_VK_INSTANCE_PLATFORM(X)
#endif

// functions of a device
#define RE4_VK_DEVICE_CORE(X)                                                                                         \
  X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle)                    \
  X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers)                  \
  X(vkFreeCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer)                     \
  X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkGetFenceStatus)                        \
  X(vkCreateSemaphore) X(vkDestroySemaphore)                                                                       \
  X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkFlushMappedMemoryRanges)                  \
  X(vkInvalidateMappedMemoryRanges)                                                                                \
  X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory)                       \
  X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory)                           \
  X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler)                                 \
  X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer)                       \
  X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineCache) X(vkDestroyPipelineCache)               \
  X(vkGetPipelineCacheData) X(vkMergePipelineCaches) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline)                                      \
  X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateDescriptorSetLayout)                              \
  X(vkDestroyDescriptorSetLayout) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool)     \
  X(vkAllocateDescriptorSets) X(vkFreeDescriptorSets) X(vkUpdateDescriptorSets)                                                            \
  X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkGetQueryPoolResults)                                              \
  X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers)                      \
  X(vkCmdBindIndexBuffer) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants) X(vkCmdDraw) X(vkCmdDrawIndexed)         \
  X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetLineWidth) X(vkCmdClearAttachments)                              \
  X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage)          \
  X(vkCmdCopyImageToBuffer) X(vkCmdCopyImage) X(vkCmdBlitImage) X(vkCmdCopyBuffer) X(vkCmdResolveImage)            \
  X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp)
// (swapchain, debug names)
#define RE4_VK_DEVICE_OPTIONAL(X)                                                                                     \
  X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR)              \
  X(vkQueuePresentKHR) X(vkSetDebugUtilsObjectNameEXT)

namespace re4vk {

#define RE4_VK_DECLARE(name) extern PFN_##name name;
RE4_VK_GLOBAL(RE4_VK_DECLARE)
RE4_VK_INSTANCE_CORE(RE4_VK_DECLARE)
RE4_VK_INSTANCE_OPTIONAL(RE4_VK_DECLARE)
RE4_VK_DEVICE_CORE(RE4_VK_DECLARE)
RE4_VK_DEVICE_OPTIONAL(RE4_VK_DECLARE)
#undef RE4_VK_DECLARE

// Opens the Vulkan loader (libvulkan.so, on Linux libvulkan.so.1) and takes its global functions; false (with
// *error) when there is none. Again after a success: true at once.
bool LoadVulkan(std::string *error);
// The functions of `instance` / `device` (the renderer has one of each at a time).
bool LoadInstance(VkInstance instance, std::string *error);
bool LoadDevice(VkDevice device, std::string *error);
// A Vulkan info structure with its type and everything else zero: Make<VkImageCreateInfo>(VK_STRUCTURE_TYPE_...).
template <class T> T Make(VkStructureType type) {
  T v{};
  v.sType = type;
  return v;
}
// "VK_ERROR_DEVICE_LOST" ...
const char *ResultName(VkResult r);

} // namespace re4vk
