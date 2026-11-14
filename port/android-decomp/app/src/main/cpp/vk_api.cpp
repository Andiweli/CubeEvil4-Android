// Run-time loading of the Vulkan entry points (vk_api.h).
#include "vk_api.h"

#include <dlfcn.h>

namespace re4vk {

#define RE4_VK_DEFINE(name) PFN_##name name = nullptr;
RE4_VK_GLOBAL(RE4_VK_DEFINE)
RE4_VK_INSTANCE_CORE(RE4_VK_DEFINE)
RE4_VK_INSTANCE_OPTIONAL(RE4_VK_DEFINE)
RE4_VK_DEVICE_CORE(RE4_VK_DEFINE)
RE4_VK_DEVICE_OPTIONAL(RE4_VK_DEFINE)
#undef RE4_VK_DEFINE

namespace {
void *gLibrary = nullptr;
PFN_vkGetInstanceProcAddr gGetInstanceProcAddr = nullptr;
} // namespace

bool LoadVulkan(std::string *error) {
  if (gGetInstanceProcAddr)
    return true;
  // Android: libvulkan.so (API 24 and later); Linux: the loader's soname
  const char *const names[] = {"libvulkan.so", "libvulkan.so.1"};
  for (const char *n : names) {
    gLibrary = dlopen(n, RTLD_NOW | RTLD_LOCAL);
    if (gLibrary)
      break;
  }
  if (!gLibrary) {
    if (error)
      *error = "no Vulkan loader (libvulkan.so)";
    return false;
  }
  gGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(gLibrary, "vkGetInstanceProcAddr"));
  if (!gGetInstanceProcAddr) {
    if (error)
      *error = "the Vulkan loader has no vkGetInstanceProcAddr";
    dlclose(gLibrary);
    gLibrary = nullptr;
    return false;
  }
#define RE4_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(gGetInstanceProcAddr(nullptr, #name));
  RE4_VK_GLOBAL(RE4_VK_LOAD)
#undef RE4_VK_LOAD
  if (!vkCreateInstance || !vkEnumerateInstanceExtensionProperties) {
    if (error)
      *error = "the Vulkan loader lacks vkCreateInstance";
    gGetInstanceProcAddr = nullptr;
    return false;
  }
  return true;
}

bool LoadInstance(VkInstance instance, std::string *error) {
  if (!gGetInstanceProcAddr) {
    if (error)
      *error = "Vulkan not loaded";
    return false;
  }
  bool ok = true;
  std::string missing;
#define RE4_VK_LOAD(name)                                                                                             \
  name = reinterpret_cast<PFN_##name>(gGetInstanceProcAddr(instance, #name));                                        \
  if (!name) {                                                                                                        \
    ok = false;                                                                                                       \
    missing += missing.empty() ? #name : ", " #name;                                                                  \
  }
  RE4_VK_INSTANCE_CORE(RE4_VK_LOAD)
#undef RE4_VK_LOAD
#define RE4_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(gGetInstanceProcAddr(instance, #name));
  RE4_VK_INSTANCE_OPTIONAL(RE4_VK_LOAD)
#undef RE4_VK_LOAD
  if (!ok && error)
    *error = "Vulkan instance functions missing: " + missing;
  return ok;
}

bool LoadDevice(VkDevice device, std::string *error) {
  if (!vkGetDeviceProcAddr) {
    if (error)
      *error = "Vulkan instance not loaded";
    return false;
  }
  bool ok = true;
  std::string missing;
#define RE4_VK_LOAD(name)                                                                                             \
  name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));                                           \
  if (!name) {                                                                                                        \
    ok = false;                                                                                                       \
    missing += missing.empty() ? #name : ", " #name;                                                                  \
  }
  RE4_VK_DEVICE_CORE(RE4_VK_LOAD)
#undef RE4_VK_LOAD
#define RE4_VK_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
  RE4_VK_DEVICE_OPTIONAL(RE4_VK_LOAD)
#undef RE4_VK_LOAD
  if (!ok && error)
    *error = "Vulkan device functions missing: " + missing;
  return ok;
}

const char *ResultName(VkResult r) {
  switch (r) {
#define RE4_VK_CASE(x)                                                                                                \
  case x:                                                                                                             \
    return #x;
    RE4_VK_CASE(VK_SUCCESS)
    RE4_VK_CASE(VK_NOT_READY)
    RE4_VK_CASE(VK_TIMEOUT)
    RE4_VK_CASE(VK_INCOMPLETE)
    RE4_VK_CASE(VK_SUBOPTIMAL_KHR)
    RE4_VK_CASE(VK_ERROR_OUT_OF_HOST_MEMORY)
    RE4_VK_CASE(VK_ERROR_OUT_OF_DEVICE_MEMORY)
    RE4_VK_CASE(VK_ERROR_INITIALIZATION_FAILED)
    RE4_VK_CASE(VK_ERROR_DEVICE_LOST)
    RE4_VK_CASE(VK_ERROR_MEMORY_MAP_FAILED)
    RE4_VK_CASE(VK_ERROR_LAYER_NOT_PRESENT)
    RE4_VK_CASE(VK_ERROR_EXTENSION_NOT_PRESENT)
    RE4_VK_CASE(VK_ERROR_FEATURE_NOT_PRESENT)
    RE4_VK_CASE(VK_ERROR_INCOMPATIBLE_DRIVER)
    RE4_VK_CASE(VK_ERROR_TOO_MANY_OBJECTS)
    RE4_VK_CASE(VK_ERROR_FORMAT_NOT_SUPPORTED)
    RE4_VK_CASE(VK_ERROR_SURFACE_LOST_KHR)
    RE4_VK_CASE(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
    RE4_VK_CASE(VK_ERROR_OUT_OF_DATE_KHR)
#undef RE4_VK_CASE
  default:
    return "VK_ERROR_(other)";
  }
}

} // namespace re4vk
