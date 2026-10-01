#include "Rendering/Gpu/GpuTexels.h"
#include "VulkanDevice.h"

#include "Platform/SessionState.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <chrono>
#include <thread>

static const VkComponentMapping kIdentityMapping{
  VK_COMPONENT_SWIZZLE_IDENTITY,
  VK_COMPONENT_SWIZZLE_IDENTITY,
  VK_COMPONENT_SWIZZLE_IDENTITY,
  VK_COMPONENT_SWIZZLE_IDENTITY
};

void
VulkanDevice::framebufferSize(int* width, int* height) const
{
  *width = 0;
  *height = 0;
  if (m_glfwWindow != nullptr) {
    glfwGetFramebufferSize(m_glfwWindow, width, height);
  } else if (m_window != nullptr) {
    const std::array<int, 2> size = m_window->getWindowDimensions();
    *width = size[0];
    *height = size[1];
  }
}

bool
VulkanDevice::ensureBackbuffer()
{
  int width = 0;
  int height = 0;
  framebufferSize(&width, &height);
  if (width <= 0 || height <= 0) {
    // Minimized: keep the old backbuffer, or start with a pixel.
    if (m_backbuffer.color.image != VK_NULL_HANDLE) {
      return true;
    }
    width = 1;
    height = 1;
  }
  if (width == m_backbuffer.width && height == m_backbuffer.height &&
      m_backbuffer.color.image != VK_NULL_HANDLE) {
    return true;
  }
  if (m_renderingActive && m_target.backbuffer) {
    endRendering();
  }
  retireImage(m_backbuffer.color);
  retireImage(m_backbuffer.depth);
  retireImage(m_backbuffer.resolve);
  const uint32_t w = static_cast<uint32_t>(width);
  const uint32_t h = static_cast<uint32_t>(height);
  const VkFormat depthFormat =
    m_context.formatFor(TextureFormat::Depth24Stencil8);
  // Depth written at placed sample locations must be created for them.
  std::array<VkSampleLocationEXT, 16> locations{};
  uint32_t locationCount = 0;
  const VkImageCreateFlags depthFlags =
    m_context.mirroredSampleLocations(
      m_samples, locations.data(), &locationCount)
      ? VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT
      : 0;
  // Stored as OpenGL stores its framebuffer (UNORM, no sRGB encoding); the
  // multisample resolve reads it through sRGB views (see resolvedBackbuffer).
  const bool multisampled = m_samples != VK_SAMPLE_COUNT_1_BIT;
  const VkImageCreateFlags colorFlags =
    multisampled ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0;
  if (!createImage(m_backbuffer.color,
                   VK_FORMAT_R8G8B8A8_UNORM,
                   w,
                   h,
                   1,
                   1,
                   m_samples,
                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   colorFlags) ||
      !createImageView(m_backbuffer.color,
                       VK_IMAGE_VIEW_TYPE_2D,
                       kIdentityMapping,
                       VK_IMAGE_ASPECT_COLOR_BIT,
                       &m_backbuffer.color.attachmentView) ||
      (multisampled && !createImageView(m_backbuffer.color,
                                        VK_IMAGE_VIEW_TYPE_2D,
                                        kIdentityMapping,
                                        VK_IMAGE_ASPECT_COLOR_BIT,
                                        &m_backbuffer.color.srgbView,
                                        VK_FORMAT_R8G8B8A8_SRGB)) ||
      !createImage(m_backbuffer.depth,
                   depthFormat,
                   w,
                   h,
                   1,
                   1,
                   m_samples,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                   depthFlags) ||
      !createImageView(m_backbuffer.depth,
                       VK_IMAGE_VIEW_TYPE_2D,
                       kIdentityMapping,
                       m_backbuffer.depth.aspect,
                       &m_backbuffer.depth.attachmentView)) {
    return false;
  }
  if (multisampled && (!createImage(m_backbuffer.resolve,
                                    VK_FORMAT_R8G8B8A8_UNORM,
                                    w,
                                    h,
                                    1,
                                    1,
                                    VK_SAMPLE_COUNT_1_BIT,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) ||
                       !createImageView(m_backbuffer.resolve,
                                        VK_IMAGE_VIEW_TYPE_2D,
                                        kIdentityMapping,
                                        VK_IMAGE_ASPECT_COLOR_BIT,
                                        &m_backbuffer.resolve.srgbView,
                                        VK_FORMAT_R8G8B8A8_SRGB))) {
    return false;
  }
  m_backbuffer.width = width;
  m_backbuffer.height = height;
  return true;
}

void
VulkanDevice::beginFrame()
{
  int width = 0;
  int height = 0;
  framebufferSize(&width, &height);
  if (width > 0 && height > 0 &&
      (width != m_backbuffer.width || height != m_backbuffer.height)) {
    ensureBackbuffer();
    // The OpenGL window resets the viewport when it resizes.
    m_viewport = { 0, 0, width, height };
  }
}

void
VulkanDevice::endFrame()
{
  flush(m_present);
}

bool
VulkanDevice::createSwapchain()
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.createSwapchain");
  const VkPhysicalDevice physical = m_context.physicalDevice();
  const VkSurfaceKHR surface = m_context.surface();
  VkSurfaceCapabilitiesKHR capabilities{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
  int width = 0;
  int height = 0;
  framebufferSize(&width, &height);
  VkExtent2D extent = capabilities.currentExtent;
  if (extent.width == UINT32_MAX) {
    extent.width = std::clamp(static_cast<uint32_t>(std::max(width, 0)),
                              capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(static_cast<uint32_t>(std::max(height, 0)),
                               capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
  }
  if (extent.width == 0 || extent.height == 0 ||
      (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ==
        0) {
    m_swapchainStale = true;
    return false;
  }
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(
    physical, surface, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(
    physical, surface, &formatCount, formats.data());
  if (formats.empty()) {
    m_swapchainStale = true;
    return false;
  }
  // OpenGL's default framebuffer is not sRGB-encoded; neither is this one.
  VkSurfaceFormatKHR chosen = formats[0];
  for (const VkSurfaceFormatKHR& format : formats) {
    if ((format.format == VK_FORMAT_B8G8R8A8_UNORM ||
         format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = format;
      break;
    }
  }
  uint32_t modeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(
    physical, surface, &modeCount, nullptr);
  std::vector<VkPresentModeKHR> modes(modeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
    physical, surface, &modeCount, modes.data());
  const bool vsync = m_window == nullptr || m_window->isFramePaced();
  VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
  if (!vsync) {
    // Swap interval 0: uncapped. A composited OpenGL window shows the newest
    // frame without tearing, which is mailbox; immediate is the fallback.
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) !=
        modes.end()) {
      mode = VK_PRESENT_MODE_MAILBOX_KHR;
    } else if (std::find(modes.begin(),
                         modes.end(),
                         VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) {
      mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
  }
  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount != 0) {
    imageCount = std::min(imageCount, capabilities.maxImageCount);
  }
  VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if ((capabilities.supportedCompositeAlpha & alpha) == 0) {
    alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  }
  const VkSwapchainKHR previous = m_swapchain.swapchain;
  VkSwapchainCreateInfoKHR create{};
  create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create.surface = surface;
  create.minImageCount = imageCount;
  create.imageFormat = chosen.format;
  create.imageColorSpace = chosen.colorSpace;
  create.imageExtent = extent;
  create.imageArrayLayers = 1;
  create.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create.preTransform = capabilities.currentTransform;
  create.compositeAlpha = alpha;
  create.presentMode = mode;
  create.clipped = VK_TRUE;
  create.oldSwapchain = previous;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  const VkResult result =
    vkCreateSwapchainKHR(m_context.device(), &create, nullptr, &swapchain);
  destroySwapchain();
  if (result != VK_SUCCESS) {
    Logger::LogWarning("Vulkan swapchain creation failed: " +
                       vulkanResultText(result));
    m_swapchainStale = true;
    return false;
  }
  m_swapchain.swapchain = swapchain;
  m_swapchain.format = chosen.format;
  m_swapchain.extent = extent;
  m_swapchain.vsync = vsync;
  uint32_t count = 0;
  vkGetSwapchainImagesKHR(m_context.device(), swapchain, &count, nullptr);
  m_swapchain.images.resize(count);
  vkGetSwapchainImagesKHR(
    m_context.device(), swapchain, &count, m_swapchain.images.data());
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  m_swapchain.finished.resize(count, VK_NULL_HANDLE);
  for (VkSemaphore& semaphore : m_swapchain.finished) {
    vkCreateSemaphore(m_context.device(), &semaphoreInfo, nullptr, &semaphore);
  }
  // One acquisition per image plus one, so a new acquire never waits for a
  // semaphore still in use.
  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  m_swapchain.acquisitions.resize(static_cast<size_t>(count) + 1);
  for (Acquisition& acquisition : m_swapchain.acquisitions) {
    vkCreateSemaphore(
      m_context.device(), &semaphoreInfo, nullptr, &acquisition.semaphore);
    vkCreateFence(m_context.device(), &fenceInfo, nullptr, &acquisition.fence);
  }
  m_swapchainStale = false;
  Logger::LogTrace("Vulkan swapchain: " + std::to_string(extent.width) + "x" +
                   std::to_string(extent.height) + ", " +
                   std::to_string(count) + " images, " +
                   (mode == VK_PRESENT_MODE_FIFO_KHR ? "vsync" : "uncapped"));
  return true;
}

void
VulkanDevice::destroySwapchain()
{
  const VkDevice device = m_context.device();
  if (device == VK_NULL_HANDLE) {
    return;
  }
  if (m_swapchain.swapchain == VK_NULL_HANDLE && m_swapchain.finished.empty()) {
    return;
  }
  vkDeviceWaitIdle(device);
  // The swapchain goes first: an acquire it never completed still refers to
  // its acquisition's semaphore and fence.
  if (m_swapchain.swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, m_swapchain.swapchain, nullptr);
  }
  for (VkSemaphore semaphore : m_swapchain.finished) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, semaphore, nullptr);
    }
  }
  for (const Acquisition& acquisition : m_swapchain.acquisitions) {
    if (acquisition.semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, acquisition.semaphore, nullptr);
    }
    if (acquisition.fence != VK_NULL_HANDLE) {
      vkDestroyFence(device, acquisition.fence, nullptr);
    }
  }
  m_swapchain = Swapchain{};
}

VulkanImage&
VulkanDevice::resolvedBackbuffer(VkCommandBuffer commands)
{
  if (m_samples == VK_SAMPLE_COUNT_1_BIT) {
    transition(
      commands, m_backbuffer.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    return m_backbuffer.color;
  }
  // The resolve averages samples in linear light, through sRGB views, as
  // the NVIDIA OpenGL driver resolves its default framebuffer ("gamma
  // correct antialiasing", on by default). The stored bytes stay OpenGL's.
  transition(
    commands, m_backbuffer.color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  transition(
    commands, m_backbuffer.resolve, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  VkRenderingAttachmentInfo attachment{};
  attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  attachment.imageView = m_backbuffer.color.srgbView;
  attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
  attachment.resolveImageView = m_backbuffer.resolve.srgbView;
  attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea.extent = { m_backbuffer.color.width,
                                  m_backbuffer.color.height };
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachments = &attachment;
  vkCmdBeginRendering(commands, &rendering);
  vkCmdEndRendering(commands);
  transition(
    commands, m_backbuffer.resolve, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  return m_backbuffer.resolve;
}

int
VulkanDevice::freeAcquisition()
{
  for (size_t index = 0; index < m_swapchain.acquisitions.size(); ++index) {
    const Acquisition& acquisition = m_swapchain.acquisitions[index];
    // Its semaphore is free once the submission that waited on it is done.
    if (!acquisition.outstanding &&
        (acquisition.waitSerial == 0 ||
         isSerialComplete(acquisition.waitSerial))) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool
VulkanDevice::recordPresentation(uint32_t* imageIndex, VkSemaphore* acquired)
{
  if (!m_present || m_context.surface() == VK_NULL_HANDLE) {
    return false;
  }
  int width = 0;
  int height = 0;
  framebufferSize(&width, &height);
  if (width <= 0 || height <= 0) {
    return false;
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.recordPresentation");
  const bool vsync = m_window == nullptr || m_window->isFramePaced();
  const int reportedRate = m_window != nullptr ? m_window->getRefreshRate() : 0;
  const uint64_t refreshNanoseconds =
    1000000000ull / static_cast<uint64_t>(reportedRate > 0 ? reportedRate : 60);
  // On a locked session the driver's present call itself can stall with no
  // way to bound it, where OpenGL's swap keeps pacing. Presents stop until the
  // session unlocks; a vsynced loop still waits a refresh per frame.
  const std::chrono::steady_clock::time_point now =
    std::chrono::steady_clock::now();
  if (now >= m_nextSessionCheck) {
    ILLUMO_PROFILE_ZONE("VulkanDevice.sessionCheck");
    m_sessionLocked = PlatformSessionLocked();
    m_nextSessionCheck = now + std::chrono::milliseconds(250);
  }
  if (m_sessionLocked) {
    if (!m_presentationBlockReported) {
      Logger::LogWarning("Vulkan presentation is paused while the session is "
                         "locked");
      m_presentationBlockReported = true;
    }
    m_presentationBlocked = true;
    if (vsync) {
      std::this_thread::sleep_for(std::chrono::nanoseconds(refreshNanoseconds));
    }
    return false;
  }
  if (m_swapchainStale || m_swapchain.swapchain == VK_NULL_HANDLE ||
      m_swapchain.vsync != vsync ||
      m_swapchain.extent.width != static_cast<uint32_t>(width) ||
      m_swapchain.extent.height != static_cast<uint32_t>(height)) {
    if (!createSwapchain()) {
      return false;
    }
  }
  Slot& slot = m_slots[m_slotIndex];
  // OpenGL's swap never stalls the loop while the compositor holds the
  // images, and an acquire may then "succeed" with a semaphore that never
  // signals. So every wait here is bounded: an image not yet free stays
  // pending and the frame goes unshown, and while that lasts each wait paces
  // the loop like a refresh would. Under FIFO a free image arrives within two
  // refreshes; longer means the compositor is holding them.
  const uint64_t timeout =
    m_presentationBlocked ? refreshNanoseconds : 2 * refreshNanoseconds;
  const VkDevice device = m_context.device();
  bool blocked = false;
  if (m_swapchain.pendingAcquisition < 0) {
    const int available = freeAcquisition();
    if (available < 0) {
      return false;
    }
    Acquisition& acquisition =
      m_swapchain.acquisitions[static_cast<size_t>(available)];
    ILLUMO_PROFILE_ZONE("VulkanDevice.acquire");
    vkResetFences(device, 1, &acquisition.fence);
    const VkResult result = vkAcquireNextImageKHR(device,
                                                  m_swapchain.swapchain,
                                                  timeout,
                                                  acquisition.semaphore,
                                                  acquisition.fence,
                                                  &m_swapchain.pendingImage);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      m_swapchainStale = true;
      return false;
    }
    if (result == VK_TIMEOUT || result == VK_NOT_READY) {
      blocked = true;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      Logger::LogWarning("Vulkan could not acquire a swapchain image: " +
                         vulkanResultText(result));
      m_swapchainStale = true;
      return false;
    } else {
      m_swapchainStale = m_swapchainStale || result == VK_SUBOPTIMAL_KHR;
      acquisition.outstanding = true;
      m_swapchain.pendingAcquisition = available;
    }
  }
  if (!blocked) {
    ILLUMO_PROFILE_ZONE("VulkanDevice.acquireWait");
    Acquisition& acquisition =
      m_swapchain
        .acquisitions[static_cast<size_t>(m_swapchain.pendingAcquisition)];
    blocked = vkWaitForFences(
                device, 1, &acquisition.fence, VK_TRUE, timeout) != VK_SUCCESS;
    if (!blocked) {
      acquisition.outstanding = false;
      acquisition.waitSerial = m_recordingSerial;
      *acquired = acquisition.semaphore;
      *imageIndex = m_swapchain.pendingImage;
      m_swapchain.pendingAcquisition = -1;
    }
  }
  if (blocked) {
    // A held-up compositor lets an image through now and then; one warning.
    if (!m_presentationBlocked && !m_presentationBlockReported) {
      Logger::LogWarning("Vulkan presentation is blocked; frames go unshown "
                         "until the window can present");
      m_presentationBlockReported = true;
    }
    m_presentationBlocked = true;
    return false;
  }
  m_presentationBlocked = false;
  VkCommandBuffer commands = slot.main;
  VulkanImage& source = resolvedBackbuffer(commands);
  VulkanImage target;
  target.image = m_swapchain.images[*imageIndex];
  target.format = m_swapchain.format;
  target.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  transition(commands, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  // Rows are stored bottom first, as in OpenGL; the screen shows the top
  // row first, so the copy flips them.
  VkImageBlit blit{};
  blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.layerCount = 1;
  blit.srcOffsets[0] = { 0, static_cast<int32_t>(source.height), 0 };
  blit.srcOffsets[1] = { static_cast<int32_t>(source.width), 0, 1 };
  blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.layerCount = 1;
  blit.dstOffsets[1] = { static_cast<int32_t>(m_swapchain.extent.width),
                         static_cast<int32_t>(m_swapchain.extent.height),
                         1 };
  vkCmdBlitImage(commands,
                 source.image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 target.image,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 1,
                 &blit,
                 VK_FILTER_NEAREST);
  transition(commands, target, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  return true;
}

bool
VulkanDevice::createReadbackBuffer(ReadbackSlot& slot, VkDeviceSize size)
{
  if (slot.buffer != VK_NULL_HANDLE && slot.size >= size) {
    return true;
  }
  if (slot.buffer != VK_NULL_HANDLE) {
    vmaDestroyBuffer(m_context.allocator(), slot.buffer, slot.allocation);
    slot.buffer = VK_NULL_HANDLE;
    slot.allocation = nullptr;
  }
  VkBufferCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  create.size = size;
  create.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
  if (vmaCreateBuffer(m_context.allocator(),
                      &create,
                      &allocation,
                      &slot.buffer,
                      &slot.allocation,
                      nullptr) != VK_SUCCESS) {
    slot.buffer = VK_NULL_HANDLE;
    slot.allocation = nullptr;
    slot.size = 0;
    return false;
  }
  slot.size = size;
  return true;
}

static void
hostReadBarrier(VkCommandBuffer commands)
{
  VkMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
  barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
  barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
  barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.memoryBarrierCount = 1;
  dependency.pMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commands, &dependency);
}

FrameReadback
VulkanDevice::readBackbuffer(int width, int height)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.readBackbuffer");
  FrameReadback result;
  if (width > m_backbuffer.width || height > m_backbuffer.height) {
    result.error = "Readback dimensions exceed the backbuffer";
    return result;
  }
  ReadbackSlot staging;
  const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(width) * 4u;
  if (!createReadbackBuffer(staging,
                            rowBytes * static_cast<VkDeviceSize>(height))) {
    result.error = "Vulkan readback memory could not be allocated";
    return result;
  }
  ensureRecording();
  endRendering();
  endMainTransfers();
  VkCommandBuffer commands = m_slots[m_slotIndex].main;
  VulkanImage& source = resolvedBackbuffer(commands);
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = { static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height),
                         1 };
  vkCmdCopyImageToBuffer(commands,
                         source.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         staging.buffer,
                         1,
                         &region);
  hostReadBarrier(commands);
  const uint64_t serial = m_recordingSerial;
  flush(false);
  if (!m_frameError.empty() || !waitForSerial(serial, UINT64_MAX)) {
    vmaDestroyBuffer(m_context.allocator(), staging.buffer, staging.allocation);
    result.error =
      m_frameError.empty() ? "Vulkan readback did not complete" : m_frameError;
    return result;
  }
  VmaAllocationInfo info{};
  vmaGetAllocationInfo(m_context.allocator(), staging.allocation, &info);
  vmaInvalidateAllocation(
    m_context.allocator(), staging.allocation, 0, VK_WHOLE_SIZE);
  result.pixels.resize(static_cast<size_t>(rowBytes) *
                       static_cast<size_t>(height));
  convertReadbackTexels(static_cast<const unsigned char*>(info.pMappedData),
                        static_cast<size_t>(rowBytes),
                        width,
                        height,
                        TextureFormat::RGBA8,
                        result.pixels.data());
  vmaDestroyBuffer(m_context.allocator(), staging.buffer, staging.allocation);
  result.width = width;
  result.height = height;
  return result;
}

bool
VulkanDevice::requestFramebufferReadback(std::uint32_t stream,
                                         FramebufferHandle framebuffer,
                                         int width,
                                         int height)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.requestFramebufferReadback");
  const VulkanFramebuffer* target = resolveFramebuffer(framebuffer);
  if (!isFramebufferValid(framebuffer) || target == nullptr ||
      target->colorTextures.empty() || width < 1 || height < 1 ||
      width > target->width || height > target->height) {
    return false;
  }
  VulkanTexture* texture = resolveTexture(target->colorTextures[0]);
  if (texture == nullptr) {
    return false;
  }
  const unsigned texelBytes = storedTexelBytes(texture->format);
  if (texelBytes == 0) {
    return false;
  }
  ReadbackStream& readback = m_readbacks[stream];
  ReadbackSlot* available = nullptr;
  for (ReadbackSlot& slot : readback.slots) {
    if (!slot.pending) {
      available = &slot;
      break;
    }
  }
  if (available == nullptr) {
    return false;
  }
  const VkDeviceSize rowBytes = static_cast<VkDeviceSize>(width) * texelBytes;
  if (!createReadbackBuffer(*available,
                            rowBytes * static_cast<VkDeviceSize>(height))) {
    return false;
  }
  ensureRecording();
  endRendering();
  endMainTransfers();
  VkCommandBuffer commands = m_slots[m_slotIndex].main;
  VulkanImage& image = texture->image;
  transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = { static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height),
                         1 };
  vkCmdCopyImageToBuffer(commands,
                         image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         available->buffer,
                         1,
                         &region);
  transition(commands, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  hostReadBarrier(commands);
  image.useSerial = m_recordingSerial;
  available->pending = true;
  available->serial = m_recordingSerial;
  available->width = width;
  available->height = height;
  available->rowBytes = rowBytes;
  available->format = texture->format;
  available->order = ++m_readbackOrder;
  return true;
}

bool
VulkanDevice::takeFramebufferReadback(std::uint32_t stream,
                                      bool wait,
                                      FrameReadback& out)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.takeFramebufferReadback");
  out = FrameReadback{};
  std::unordered_map<std::uint32_t, ReadbackStream>::iterator found =
    m_readbacks.find(stream);
  if (found == m_readbacks.end()) {
    out.error = "No readback is pending";
    return false;
  }
  ReadbackSlot* oldest = nullptr;
  for (ReadbackSlot& slot : found->second.slots) {
    if (slot.pending && (oldest == nullptr || slot.order < oldest->order)) {
      oldest = &slot;
    }
  }
  if (oldest == nullptr) {
    out.error = "No readback is pending";
    return false;
  }
  // A copy still being recorded is submitted now, as OpenGL's poll flushes
  // its fence (GL_SYNC_FLUSH_COMMANDS_BIT), so a poll is never a frame late.
  if (m_recording && oldest->serial == m_recordingSerial) {
    flush(false);
  }
  // One second bounds a blocking wait; a lost GPU must not hang the frame.
  if (!waitForSerial(oldest->serial, wait ? 1000000000ull : 0ull)) {
    out.error = "Readback is not complete yet";
    return false;
  }
  VmaAllocationInfo info{};
  vmaGetAllocationInfo(m_context.allocator(), oldest->allocation, &info);
  vmaInvalidateAllocation(
    m_context.allocator(), oldest->allocation, 0, VK_WHOLE_SIZE);
  out.pixels.resize(static_cast<size_t>(oldest->width) *
                    static_cast<size_t>(oldest->height) * 4u);
  convertReadbackTexels(static_cast<const unsigned char*>(info.pMappedData),
                        static_cast<size_t>(oldest->rowBytes),
                        oldest->width,
                        oldest->height,
                        oldest->format,
                        out.pixels.data());
  oldest->pending = false;
  out.width = oldest->width;
  out.height = oldest->height;
  return true;
}

void
VulkanDevice::releaseReadbackStream(std::uint32_t stream)
{
  std::unordered_map<std::uint32_t, ReadbackStream>::iterator found =
    m_readbacks.find(stream);
  if (found == m_readbacks.end()) {
    return;
  }
  for (ReadbackSlot& slot : found->second.slots) {
    if (slot.buffer == VK_NULL_HANDLE) {
      continue;
    }
    VulkanBufferMemory memory;
    memory.buffer = slot.buffer;
    memory.allocation = slot.allocation;
    retireBuffer(memory);
  }
  m_readbacks.erase(found);
}
