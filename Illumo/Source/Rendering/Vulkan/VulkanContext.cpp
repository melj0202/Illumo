#include "VulkanContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <Illumo/Services/Logger.h>
#include <cstdlib>
#include <cstring>
#include <vector>

std::string
vulkanResultText(VkResult result)
{
  switch (result) {
    case VK_SUCCESS:
      return "VK_SUCCESS";
    case VK_NOT_READY:
      return "VK_NOT_READY";
    case VK_TIMEOUT:
      return "VK_TIMEOUT";
    case VK_SUBOPTIMAL_KHR:
      return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
      return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
      return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:
      return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
      return "VK_ERROR_OUT_OF_DATE_KHR";
    default:
      return "VkResult " + std::to_string(static_cast<int>(result));
  }
}

// ILLUMO_VULKAN_VALIDATION=1 enables the Khronos validation layer when it is
// installed (development only).
static bool
validationRequested()
{
#ifdef _MSC_VER
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, "ILLUMO_VULKAN_VALIDATION") != 0 ||
      value == nullptr) {
    return false;
  }
  const bool requested = value[0] != '\0' && std::strcmp(value, "0") != 0;
  std::free(value);
  return requested;
#else
  const char* value = std::getenv("ILLUMO_VULKAN_VALIDATION");
  return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
#endif
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
             VkDebugUtilsMessageTypeFlagsEXT,
             const VkDebugUtilsMessengerCallbackDataEXT* data,
             void*)
{
  const std::string text =
    std::string("Vulkan validation: ") +
    (data != nullptr && data->pMessage != nullptr ? data->pMessage : "");
  if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
    Logger::LogError(text);
  } else {
    Logger::LogWarning(text);
  }
  return VK_FALSE;
}

VulkanContext::~VulkanContext()
{
  shutdown();
}

bool
VulkanContext::initialize(GLFWwindow* window, std::string* error)
{
  const VkResult loaded = volkInitialize();
  if (loaded != VK_SUCCESS) {
    *error =
      "The Vulkan loader is not installed (" + vulkanResultText(loaded) + ")";
    return false;
  }
  if (volkGetInstanceVersion() < VK_API_VERSION_1_3) {
    *error = "The Vulkan loader is older than Vulkan 1.3";
    return false;
  }
  if (!createInstance(window, error)) {
    return false;
  }
  if (window != nullptr) {
    const VkResult surface =
      glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
    if (surface != VK_SUCCESS) {
      *error =
        "The window has no Vulkan surface (" + vulkanResultText(surface) + ")";
      return false;
    }
  }
  return selectPhysicalDevice(error) && createDevice(error) &&
         createAllocator(error);
}

bool
VulkanContext::createInstance(GLFWwindow* window, std::string* error)
{
  std::vector<const char*> extensions;
  if (window != nullptr) {
    if (glfwVulkanSupported() != GLFW_TRUE) {
      *error = "GLFW cannot present with Vulkan on this system";
      return false;
    }
    uint32_t count = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&count);
    if (required == nullptr) {
      *error = "GLFW reports no Vulkan surface extensions";
      return false;
    }
    extensions.assign(required, required + count);
  }

  std::vector<const char*> layers;
  if (validationRequested()) {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> available(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, available.data());
    for (const VkLayerProperties& layer : available) {
      if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
        m_validation = true;
      }
    }
    if (m_validation) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else {
      Logger::LogWarning(
        "ILLUMO_VULKAN_VALIDATION is set but the validation layer is missing");
    }
  }

  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.pApplicationName = "Illumo";
  application.pEngineName = "Illumo";
  application.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create.pApplicationInfo = &application;
  create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create.ppEnabledExtensionNames = extensions.data();
  create.enabledLayerCount = static_cast<uint32_t>(layers.size());
  create.ppEnabledLayerNames = layers.data();
  const VkResult result = vkCreateInstance(&create, nullptr, &m_instance);
  if (result != VK_SUCCESS) {
    *error = "vkCreateInstance failed (" + vulkanResultText(result) + ")";
    m_instance = VK_NULL_HANDLE;
    return false;
  }
  volkLoadInstanceOnly(m_instance);

  if (m_validation) {
    VkDebugUtilsMessengerCreateInfoEXT messenger{};
    messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messenger.messageSeverity =
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
      VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messenger.pfnUserCallback = debugMessage;
    vkCreateDebugUtilsMessengerEXT(
      m_instance, &messenger, nullptr, &m_messenger);
  }
  return true;
}

static bool
hasExtension(const std::vector<VkExtensionProperties>& extensions,
             const char* name)
{
  for (const VkExtensionProperties& extension : extensions) {
    if (std::strcmp(extension.extensionName, name) == 0) {
      return true;
    }
  }
  return false;
}

bool
VulkanContext::selectPhysicalDevice(std::string* error)
{
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(m_instance, &count, devices.data());
  int bestScore = -1;
  std::string rejected;
  for (VkPhysicalDevice candidate : devices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    const std::string name = properties.deviceName;
    if (properties.apiVersion < VK_API_VERSION_1_3) {
      rejected += name + " (Vulkan 1.3 missing); ";
      continue;
    }
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(
      candidate, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(
      candidate, nullptr, &extensionCount, extensions.data());
    if (!hasExtension(extensions, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) ||
        (m_surface != VK_NULL_HANDLE &&
         !hasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))) {
      rejected += name + " (required extensions missing); ";
      continue;
    }
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;
    vkGetPhysicalDeviceFeatures2(candidate, &features);
    if (features13.dynamicRendering != VK_TRUE ||
        features13.synchronization2 != VK_TRUE) {
      rejected += name + " (dynamic rendering missing); ";
      continue;
    }
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
      candidate, &familyCount, families.data());
    int family = -1;
    for (uint32_t index = 0; index < familyCount; ++index) {
      if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
        continue;
      }
      if (m_surface != VK_NULL_HANDLE) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(
          candidate, index, m_surface, &present);
        if (present != VK_TRUE) {
          continue;
        }
      }
      family = static_cast<int>(index);
      break;
    }
    if (family < 0) {
      rejected += name + " (no graphics queue that presents); ";
      continue;
    }
    int score = 1;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score = 3;
    } else if (properties.deviceType ==
               VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      score = 2;
    }
    if (score > bestScore) {
      bestScore = score;
      m_physicalDevice = candidate;
      m_queueFamily = static_cast<uint32_t>(family);
      m_properties = properties;
      m_fillModeNonSolid = features.features.fillModeNonSolid == VK_TRUE;
      m_lineExtension = nullptr;
      if (hasExtension(extensions, VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME)) {
        m_lineExtension = VK_KHR_LINE_RASTERIZATION_EXTENSION_NAME;
      } else if (hasExtension(extensions,
                              VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME)) {
        m_lineExtension = VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME;
      }
      m_sampleLocationsExtension =
        hasExtension(extensions, VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME);
    }
  }
  if (m_physicalDevice == VK_NULL_HANDLE) {
    *error = devices.empty() ? "No Vulkan device is available"
                             : "No Vulkan device is suitable: " + rejected;
    return false;
  }
  const VkFormat depth24[] = { VK_FORMAT_X8_D24_UNORM_PACK32,
                               VK_FORMAT_D32_SFLOAT,
                               VK_FORMAT_D24_UNORM_S8_UINT,
                               VK_FORMAT_D32_SFLOAT_S8_UINT };
  const VkFormat depth24Stencil8[] = { VK_FORMAT_D24_UNORM_S8_UINT,
                                       VK_FORMAT_D32_SFLOAT_S8_UINT };
  const VkFormatFeatureFlags depthFeatures =
    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
  m_depth24 = firstSupported(depth24, 4, depthFeatures);
  m_depth24Stencil8 = firstSupported(depth24Stencil8, 2, depthFeatures);
  if (m_depth24 == VK_FORMAT_UNDEFINED ||
      m_depth24Stencil8 == VK_FORMAT_UNDEFINED) {
    *error = "The Vulkan device has no sampled depth-stencil format";
    return false;
  }
  return true;
}

bool
VulkanContext::createDevice(std::string* error)
{
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue{};
  queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue.queueFamilyIndex = m_queueFamily;
  queue.queueCount = 1;
  queue.pQueuePriorities = &priority;

  VkPhysicalDeviceLineRasterizationFeatures lineFeatures{};
  lineFeatures.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES;
  if (m_lineExtension != nullptr) {
    VkPhysicalDeviceFeatures2 query{};
    query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    query.pNext = &lineFeatures;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &query);
    m_rectangularLines = lineFeatures.rectangularLines == VK_TRUE;
    m_bresenhamLines = lineFeatures.bresenhamLines == VK_TRUE;
    lineFeatures = VkPhysicalDeviceLineRasterizationFeatures{};
    lineFeatures.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES;
    lineFeatures.rectangularLines = m_rectangularLines ? VK_TRUE : VK_FALSE;
    lineFeatures.bresenhamLines = m_bresenhamLines ? VK_TRUE : VK_FALSE;
  }
  VkPhysicalDeviceVulkan13Features features13{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  features13.pNext = m_lineExtension != nullptr ? &lineFeatures : nullptr;
  VkPhysicalDeviceFeatures2 features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features.pNext = &features13;
  features.features.fillModeNonSolid = m_fillModeNonSolid ? VK_TRUE : VK_FALSE;

  std::vector<const char*> extensions;
  extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
  if (m_surface != VK_NULL_HANDLE) {
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }
  if (m_lineExtension != nullptr) {
    extensions.push_back(m_lineExtension);
  }
  if (m_sampleLocationsExtension) {
    m_sampleLocations.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLE_LOCATIONS_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &m_sampleLocations;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties);
    m_sampleLocations.pNext = nullptr;
    extensions.push_back(VK_EXT_SAMPLE_LOCATIONS_EXTENSION_NAME);
  }

  VkDeviceCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create.pNext = &features;
  create.queueCreateInfoCount = 1;
  create.pQueueCreateInfos = &queue;
  create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create.ppEnabledExtensionNames = extensions.data();
  const VkResult result =
    vkCreateDevice(m_physicalDevice, &create, nullptr, &m_device);
  if (result != VK_SUCCESS) {
    *error = "vkCreateDevice failed (" + vulkanResultText(result) + ")";
    m_device = VK_NULL_HANDLE;
    return false;
  }
  volkLoadDevice(m_device);
  vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
  return true;
}

bool
VulkanContext::createAllocator(std::string* error)
{
  VmaVulkanFunctions functions{};
  functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
  VmaAllocatorCreateInfo create{};
  create.vulkanApiVersion = VK_API_VERSION_1_3;
  create.physicalDevice = m_physicalDevice;
  create.device = m_device;
  create.instance = m_instance;
  create.pVulkanFunctions = &functions;
  const VkResult result = vmaCreateAllocator(&create, &m_allocator);
  if (result != VK_SUCCESS) {
    *error =
      "The Vulkan memory allocator failed (" + vulkanResultText(result) + ")";
    m_allocator = VK_NULL_HANDLE;
    return false;
  }
  return true;
}

void
VulkanContext::shutdown()
{
  if (m_device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_device);
  }
  if (m_allocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(m_allocator);
    m_allocator = VK_NULL_HANDLE;
  }
  if (m_device != VK_NULL_HANDLE) {
    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
  }
  if (m_surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
  }
  if (m_messenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    m_messenger = VK_NULL_HANDLE;
  }
  if (m_instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
  }
  m_physicalDevice = VK_NULL_HANDLE;
  m_queue = VK_NULL_HANDLE;
}

VkFormat
VulkanContext::firstSupported(const VkFormat* candidates,
                              int count,
                              VkFormatFeatureFlags features) const
{
  for (int index = 0; index < count; ++index) {
    if (supportsFormatFeatures(candidates[index], features)) {
      return candidates[index];
    }
  }
  return VK_FORMAT_UNDEFINED;
}

bool
VulkanContext::supportsFormatFeatures(VkFormat format,
                                      VkFormatFeatureFlags features) const
{
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
  return (properties.optimalTilingFeatures & features) == features;
}

VkFormat
VulkanContext::formatFor(TextureFormat format) const
{
  switch (format) {
    case TextureFormat::RGBA8:
    case TextureFormat::RGB8:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::R8:
      return VK_FORMAT_R8_UNORM;
    case TextureFormat::RGBA16F:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::RG16F:
      return VK_FORMAT_R16G16_SFLOAT;
    case TextureFormat::R16F:
      return VK_FORMAT_R16_SFLOAT;
    case TextureFormat::Depth24:
      return m_depth24;
    case TextureFormat::Depth24Stencil8:
      return m_depth24Stencil8;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

VkLineRasterizationMode
VulkanContext::lineRasterization(bool multisampled) const
{
  if (multisampled && m_rectangularLines) {
    return VK_LINE_RASTERIZATION_MODE_RECTANGULAR;
  }
  if (!multisampled && m_bresenhamLines) {
    return VK_LINE_RASTERIZATION_MODE_BRESENHAM;
  }
  return VK_LINE_RASTERIZATION_MODE_DEFAULT;
}

bool
VulkanContext::mirroredSampleLocations(VkSampleCountFlagBits samples,
                                       VkSampleLocationEXT* locations,
                                       uint32_t* count) const
{
  if (!m_sampleLocationsExtension ||
      (m_sampleLocations.sampleLocationSampleCounts & samples) == 0 ||
      m_sampleLocations.sampleLocationSubPixelBits < 4 ||
      m_sampleLocations.maxSampleLocationGridSize.width < 1 ||
      m_sampleLocations.maxSampleLocationGridSize.height < 1) {
    return false;
  }
  // The standard sample locations of the Vulkan specification.
  static const VkSampleLocationEXT kTwo[] = { { 0.75f, 0.75f },
                                              { 0.25f, 0.25f } };
  static const VkSampleLocationEXT kFour[] = { { 0.375f, 0.125f },
                                               { 0.875f, 0.375f },
                                               { 0.125f, 0.625f },
                                               { 0.625f, 0.875f } };
  static const VkSampleLocationEXT kEight[] = {
    { 0.5625f, 0.3125f }, { 0.4375f, 0.6875f }, { 0.8125f, 0.5625f },
    { 0.3125f, 0.1875f }, { 0.1875f, 0.8125f }, { 0.0625f, 0.4375f },
    { 0.6875f, 0.9375f }, { 0.9375f, 0.0625f }
  };
  static const VkSampleLocationEXT kSixteen[] = {
    { 0.5625f, 0.5625f }, { 0.4375f, 0.3125f }, { 0.3125f, 0.625f },
    { 0.75f, 0.4375f },   { 0.1875f, 0.375f },  { 0.625f, 0.8125f },
    { 0.8125f, 0.6875f }, { 0.6875f, 0.1875f }, { 0.375f, 0.875f },
    { 0.5f, 0.0625f },    { 0.25f, 0.125f },    { 0.125f, 0.75f },
    { 0.0f, 0.5f },       { 0.9375f, 0.25f },   { 0.875f, 0.9375f },
    { 0.0625f, 0.0f }
  };
  const VkSampleLocationEXT* standard = nullptr;
  switch (samples) {
    case VK_SAMPLE_COUNT_2_BIT:
      standard = kTwo;
      break;
    case VK_SAMPLE_COUNT_4_BIT:
      standard = kFour;
      break;
    case VK_SAMPLE_COUNT_8_BIT:
      standard = kEight;
      break;
    case VK_SAMPLE_COUNT_16_BIT:
      standard = kSixteen;
      break;
    default:
      return false;
  }
  const float highest = m_sampleLocations.sampleLocationCoordinateRange[1];
  *count = static_cast<uint32_t>(samples);
  for (uint32_t index = 0; index < *count; ++index) {
    locations[index].x = standard[index].x;
    const float mirrored = 1.0f - standard[index].y;
    locations[index].y = mirrored > highest ? highest : mirrored;
  }
  return true;
}

VkSampleCountFlagBits
VulkanContext::sampleCountFor(int requested) const
{
  const VkSampleCountFlags supported =
    m_properties.limits.framebufferColorSampleCounts &
    m_properties.limits.framebufferDepthSampleCounts &
    m_properties.limits.framebufferStencilSampleCounts;
  const VkSampleCountFlagBits counts[] = { VK_SAMPLE_COUNT_16_BIT,
                                           VK_SAMPLE_COUNT_8_BIT,
                                           VK_SAMPLE_COUNT_4_BIT,
                                           VK_SAMPLE_COUNT_2_BIT };
  for (VkSampleCountFlagBits count : counts) {
    if (requested >= static_cast<int>(count) && (supported & count) != 0) {
      return count;
    }
  }
  return VK_SAMPLE_COUNT_1_BIT;
}

void
VulkanContext::logDescription() const
{
  const uint32_t api = m_properties.apiVersion;
  Logger::LogInfo(std::string("GPU: ") + m_properties.deviceName);
  Logger::LogInfo("Vulkan device: API " +
                  std::to_string(VK_API_VERSION_MAJOR(api)) + "." +
                  std::to_string(VK_API_VERSION_MINOR(api)) + "." +
                  std::to_string(VK_API_VERSION_PATCH(api)) + ", driver " +
                  std::to_string(m_properties.driverVersion) +
                  (m_validation ? ", validation on" : ""));
  const VkSampleCountFlagBits samples = sampleCountFor(64);
  Logger::LogTrace(
    "GPU limits: " + std::to_string(m_properties.limits.maxImageDimension2D) +
    " px textures, " +
    std::to_string(m_properties.limits.maxPerStageDescriptorSamplers) +
    " texture units, " + std::to_string(static_cast<int>(samples)) +
    "x multisampling");
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memory);
  VkDeviceSize local = 0;
  for (uint32_t index = 0; index < memory.memoryHeapCount; ++index) {
    if ((memory.memoryHeaps[index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) !=
        0) {
      local = local > memory.memoryHeaps[index].size
                ? local
                : memory.memoryHeaps[index].size;
    }
  }
  if (local > 0) {
    Logger::LogInfo("Video memory: " + std::to_string(local / (1024u * 1024u)) +
                    " MiB dedicated");
  }
}
