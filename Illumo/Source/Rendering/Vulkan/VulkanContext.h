#pragma once

#include "VulkanCommon.h"
#include <Illumo/Rendering/ITexture.h>
#include <cstdint>
#include <string>

struct GLFWwindow;

// Instance, physical and logical device, the one graphics queue and the
// memory allocator. Needs Vulkan 1.3 (dynamic rendering, synchronization2,
// extended dynamic state) and VK_KHR_push_descriptor.
class VulkanContext
{
public:
  VulkanContext() = default;
  ~VulkanContext();
  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;
  VulkanContext(VulkanContext&&) = delete;
  VulkanContext& operator=(VulkanContext&&) = delete;

  // A window gets a surface and swapchain support; without one the device
  // renders offscreen only (capture and tests).
  bool initialize(GLFWwindow* window, std::string* error);
  void shutdown();

  VkInstance instance() const { return m_instance; }
  VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
  VkDevice device() const { return m_device; }
  VkQueue queue() const { return m_queue; }
  uint32_t queueFamily() const { return m_queueFamily; }
  VmaAllocator allocator() const { return m_allocator; }
  VkSurfaceKHR surface() const { return m_surface; }
  const VkPhysicalDeviceProperties& properties() const { return m_properties; }
  const VkPhysicalDeviceLimits& limits() const { return m_properties.limits; }
  bool fillModeNonSolid() const { return m_fillModeNonSolid; }
  // How OpenGL rasterizes non-smooth lines: rectangles when multisampled,
  // the diamond-exit (Bresenham) rule otherwise. DEFAULT when the device
  // cannot select that mode.
  VkLineRasterizationMode lineRasterization(bool multisampled) const;
  // The standard sample pattern mirrored vertically, for multisampled
  // images stored bottom row first as OpenGL's default framebuffer is: the
  // OpenGL driver keeps that framebuffer top row first, so its samples sit
  // mirrored relative to a bottom-first Vulkan image. False when the device
  // cannot place samples.
  bool mirroredSampleLocations(VkSampleCountFlagBits samples,
                               VkSampleLocationEXT* locations,
                               uint32_t* count) const;

  // Stored formats for OpenGL's sized formats; UNDEFINED when unsupported.
  VkFormat formatFor(TextureFormat format) const;
  // The largest sample count the colour and depth attachments both support
  // that does not exceed the request (OpenGL's GLFW_SAMPLES).
  VkSampleCountFlagBits sampleCountFor(int requested) const;
  bool supportsFormatFeatures(VkFormat format,
                              VkFormatFeatureFlags features) const;
  // Logs the device the way the OpenGL backend logs its context.
  void logDescription() const;

private:
  bool createInstance(GLFWwindow* window, std::string* error);
  bool selectPhysicalDevice(std::string* error);
  bool createDevice(std::string* error);
  bool createAllocator(std::string* error);
  VkFormat firstSupported(const VkFormat* candidates,
                          int count,
                          VkFormatFeatureFlags features) const;

  VkInstance m_instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  VkQueue m_queue = VK_NULL_HANDLE;
  uint32_t m_queueFamily = 0;
  VmaAllocator m_allocator = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties m_properties{};
  bool m_fillModeNonSolid = false;
  const char* m_lineExtension = nullptr;
  bool m_rectangularLines = false;
  bool m_bresenhamLines = false;
  bool m_sampleLocationsExtension = false;
  VkPhysicalDeviceSampleLocationsPropertiesEXT m_sampleLocations{};
  bool m_validation = false;
  VkFormat m_depth24 = VK_FORMAT_UNDEFINED;
  VkFormat m_depth24Stencil8 = VK_FORMAT_UNDEFINED;
};
