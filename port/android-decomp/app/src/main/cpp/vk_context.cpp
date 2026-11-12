// The Vulkan device of the Vulkan renderer (vk_context.h).
#include "vk_context.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace re4vk {

std::string VersionText(std::uint32_t v) {
  char text[32];
  std::snprintf(text, sizeof(text), "%u.%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v),
                VK_API_VERSION_PATCH(v));
  return text;
}

void Context::Log(const char *fmt, ...) const {
  if (!options_.log)
    return;
  char line[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  options_.log(line);
}

VKAPI_ATTR VkBool32 VKAPI_CALL Context::Message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                void *self) {
  Context &c = *static_cast<Context *>(self);
  const bool error = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
  const unsigned n = error ? ++c.validationErrors : ++c.validationWarnings;
  // every message counts; the first ones of each kind into the log (a broken frame can repeat one thousands of times)
  if (n <= 50)
    c.Log("Vulkan validation %s: %s", error ? "error" : "warning",
          data && data->pMessage ? data->pMessage : "(no text)");
  return VK_FALSE;
}

bool Context::HasExtension(const char *name) const {
  for (const std::string &e : deviceExtensions)
    if (e == name)
      return true;
  return false;
}

std::uint32_t Context::MemoryType(std::uint32_t bits, VkMemoryPropertyFlags need, VkMemoryPropertyFlags prefer) const {
  std::uint32_t found = ~0u;
  for (std::uint32_t i = 0; i < memory.memoryTypeCount; i++) {
    if (!(bits & (1u << i)) || (memory.memoryTypes[i].propertyFlags & need) != need)
      continue;
    if ((memory.memoryTypes[i].propertyFlags & prefer) == prefer)
      return i;
    if (found == ~0u)
      found = i;
  }
  return found;
}

VkFormat Context::PickFormat(const VkFormat *formats, int count, VkFormatFeatureFlags features) const {
  for (int i = 0; i < count; i++) {
    VkFormatProperties p{};
    vkGetPhysicalDeviceFormatProperties(physical, formats[i], &p);
    if ((p.optimalTilingFeatures & features) == features)
      return formats[i];
  }
  return VK_FORMAT_UNDEFINED;
}

std::string Context::Describe() const {
  char text[512];
  // the driver's own version: Vulkan's encoding for most vendors; NVIDIA counts 10/8/8/6 bits
  const std::uint32_t d = properties.driverVersion;
  char driver[64];
  if (properties.vendorID == 0x10DE)
    std::snprintf(driver, sizeof(driver), "%u.%u.%u.%u", d >> 22, (d >> 14) & 0xFF, (d >> 6) & 0xFF, d & 0x3F);
  else
    std::snprintf(driver, sizeof(driver), "%s", VersionText(d).c_str());
  std::snprintf(text, sizeof(text), "Vulkan %s (used as %u.%u), %s (vendor 0x%04x, device 0x%04x), driver %s%s%s%s%s",
                VersionText(properties.apiVersion).c_str(), VK_API_VERSION_MAJOR(apiVersion),
                VK_API_VERSION_MINOR(apiVersion), properties.deviceName, properties.vendorID, properties.deviceID, driver,
                driverName.empty() ? "" : " (", driverName.c_str(), driverInfo.empty() ? "" : " ", driverInfo.c_str());
  std::string s = text;
  if (!driverName.empty())
    s += ")";
  return s;
}

bool Context::Create(const ContextOptions &options, std::string *error) {
  Destroy();
  options_ = options;
  const auto fail = [&](const std::string &why) {
    if (error)
      *error = why;
    Destroy();
    return false;
  };
  std::string e;
  if (!LoadVulkan(&e))
    return fail(e);
  // the instance: Vulkan 1.1 when the loader has it (vkEnumerateInstanceVersion is 1.1 itself)
  std::uint32_t loaderVersion = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion)
    vkEnumerateInstanceVersion(&loaderVersion);
  const std::uint32_t instanceApi = loaderVersion >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
  if (instanceApi < options.minApi)
    return fail("the Vulkan loader is " + VersionText(loaderVersion) + ", older than " + VersionText(options.minApi));
  std::uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> instanceExtensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, instanceExtensions.data());
  const auto haveInstanceExtension = [&](const char *name) {
    for (const VkExtensionProperties &p : instanceExtensions)
      if (std::strcmp(p.extensionName, name) == 0)
        return true;
    return false;
  };
  std::vector<const char *> extensions, layers;
  if (options.surface) {
    if (!haveInstanceExtension(VK_KHR_SURFACE_EXTENSION_NAME))
      return fail("no VK_KHR_surface");
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    if (!haveInstanceExtension(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME))
      return fail("no VK_KHR_android_surface");
    extensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
  }
  if (instanceApi < VK_API_VERSION_1_1 && haveInstanceExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  bool debugUtils = false;
  if (options.validation && vkEnumerateInstanceLayerProperties) {
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());
    for (const VkLayerProperties &l : available)
      if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        validation = true;
    if (validation) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      // the layer brings VK_EXT_debug_utils itself
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      if (options.syncValidation)
        extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
      debugUtils = true;
    } else {
      Log("Vulkan validation layer: not installed");
    }
  }
  auto app = Make<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
  app.pApplicationName = "CubeEvil4";
  app.applicationVersion = 1;
  app.pEngineName = "CubeEvil4 GX";
  app.engineVersion = 1;
  app.apiVersion = instanceApi;
  auto ici = Make<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  ici.ppEnabledExtensionNames = extensions.data();
  ici.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
  ici.ppEnabledLayerNames = layers.data();
  auto dci = Make<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
  dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dci.pfnUserCallback = &Context::Message;
  dci.pUserData = this;
  VkValidationFeatureEnableEXT syncCheck = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  auto features = Make<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
  features.enabledValidationFeatureCount = 1;
  features.pEnabledValidationFeatures = &syncCheck;
  if (debugUtils) {
    // messages of vkCreateInstance itself too
    ici.pNext = &dci;
    if (options.syncValidation) {
      features.pNext = ici.pNext;
      ici.pNext = &features;
    }
  }
  VkResult r = vkCreateInstance(&ici, nullptr, &instance);
  if (r != VK_SUCCESS) {
    instance = VK_NULL_HANDLE;
    return fail(std::string("vkCreateInstance: ") + ResultName(r));
  }
  if (!LoadInstance(instance, &e))
    return fail(e);
  if (debugUtils && vkCreateDebugUtilsMessengerEXT)
    vkCreateDebugUtilsMessengerEXT(instance, &dci, nullptr, &messenger_);

  // the physical device: a GPU before a processor's (lavapipe on the PC is only one when nothing else is)
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  if (!count)
    return fail("no Vulkan device");
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());
  int best = -1;
  int bestRank = -1;
  for (std::uint32_t i = 0; i < count; i++) {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(devices[i], &p);
    const int rank = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 4
                     : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                     : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 2
                                                                              : 1;
    if (p.apiVersion >= options.minApi && rank > bestRank) {
      best = static_cast<int>(i);
      bestRank = rank;
    }
  }
  if (best < 0) {
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(devices[0], &p);
    return fail(std::string("the Vulkan device ") + p.deviceName + " is " + VersionText(p.apiVersion) + ", older than " +
                VersionText(options.minApi));
  }
  physical = devices[static_cast<std::size_t>(best)];
  vkGetPhysicalDeviceProperties(physical, &properties);
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  apiVersion = std::min(properties.apiVersion, instanceApi);

  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, available.data());
  deviceExtensions.clear();
  for (const VkExtensionProperties &p : available)
    deviceExtensions.push_back(p.extensionName);

  // the driver's name and version text (Vulkan 1.2 or VK_KHR_driver_properties), for the error report
  if (vkGetPhysicalDeviceProperties2 && (properties.apiVersion >= VK_API_VERSION_1_2 ||
                                         HasExtension(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))) {
    auto driver = Make<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
    auto p2 = Make<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    p2.pNext = &driver;
    vkGetPhysicalDeviceProperties2(physical, &p2);
    driverName = driver.driverName;
    driverInfo = driver.driverInfo;
  }

  // a queue that draws (and presents: the swapchain checks its surface against it, vk_swapchain)
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
  family = ~0u;
  for (std::uint32_t i = 0; i < count; i++)
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      family = i;
      break;
    }
  if (family == ~0u)
    return fail(std::string("no graphics queue on ") + properties.deviceName);
  timestampBits = families[family].timestampValidBits;

  // features: only what the renderer uses, where the device has it
  VkPhysicalDeviceFeatures have{};
  vkGetPhysicalDeviceFeatures(physical, &have);
  enabled = VkPhysicalDeviceFeatures{};
  enabled.samplerAnisotropy = have.samplerAnisotropy;  // the menu's anisotropic filtering
  enabled.textureCompressionETC2 = have.textureCompressionETC2;  // HD textures of the player's pack (tex_pack.h)
  enabled.wideLines = have.wideLines;                  // GX lines wider than a pixel
  enabled.largePoints = have.largePoints;              // GX points
  enabled.fragmentStoresAndAtomics = VK_FALSE;
  std::vector<const char *> want;
  // GL's depth range in clip space (RE4_VK_DEPTH_CLIP_CONTROL=0: never, the shaders map the depth to 0..1)
  auto clip = Make<VkPhysicalDeviceDepthClipControlFeaturesEXT>(
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT);
  {
    const char *off = std::getenv("RE4_VK_DEPTH_CLIP_CONTROL");
    if (vkGetPhysicalDeviceFeatures2 && HasExtension(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME) && !(off && *off == '0')) {
      auto f2 = Make<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
      f2.pNext = &clip;
      vkGetPhysicalDeviceFeatures2(physical, &f2);
      depthClipControl = clip.depthClipControl == VK_TRUE;
    }
    if (depthClipControl) {
      want.push_back(VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME);
      clip.pNext = nullptr;
    }
  }
  if (options.surface) {
    if (!HasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
      return fail(std::string("no VK_KHR_swapchain on ") + properties.deviceName);
    want.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }
  const float priority = 1.0f;
  auto qci = Make<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
  qci.queueFamilyIndex = family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;
  auto dci2 = Make<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
  dci2.queueCreateInfoCount = 1;
  dci2.pQueueCreateInfos = &qci;
  dci2.enabledExtensionCount = static_cast<std::uint32_t>(want.size());
  dci2.ppEnabledExtensionNames = want.data();
  dci2.pEnabledFeatures = &enabled;
  if (depthClipControl)
    dci2.pNext = &clip;
  r = vkCreateDevice(physical, &dci2, nullptr, &device);
  if (r != VK_SUCCESS) {
    device = VK_NULL_HANDLE;
    return fail(std::string("vkCreateDevice: ") + ResultName(r));
  }
  if (!LoadDevice(device, &e))
    return fail(e);
  vkGetDeviceQueue(device, family, 0, &queue);
  return true;
}

void Context::Destroy() {
  if (device) {
    vkDeviceWaitIdle(device);
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }
  queue = VK_NULL_HANDLE;
  if (messenger_ && vkDestroyDebugUtilsMessengerEXT)
    vkDestroyDebugUtilsMessengerEXT(instance, messenger_, nullptr);
  messenger_ = VK_NULL_HANDLE;
  if (instance) {
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
  physical = VK_NULL_HANDLE;
  validation = false;
}

} // namespace re4vk
