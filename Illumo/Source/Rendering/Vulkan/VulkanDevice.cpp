#include "VulkanDevice.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cstring>
#include <span>

static constexpr VkDeviceSize kStagingChunkBytes = 8u * 1024u * 1024u;

size_t
VulkanPipelineKeyHash::operator()(const VulkanPipelineKey& key) const
{
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
  uint64_t hash = 1469598103934665603ull;
  for (size_t index = 0; index < sizeof(VulkanPipelineKey); ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return static_cast<size_t>(hash);
}

bool
VulkanPipelineKeyEqual::operator()(const VulkanPipelineKey& left,
                                   const VulkanPipelineKey& right) const
{
  return std::memcmp(&left, &right, sizeof(VulkanPipelineKey)) == 0;
}

VulkanDevice::~VulkanDevice()
{
  shutdown();
}

bool
VulkanDevice::initialize(IRenderWindow* window,
                         bool present,
                         std::string* error)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.initialize");
  m_window = window;
  m_present = present;
  m_glfwWindow = window != nullptr ? window->getWindowInstance() : nullptr;
  if (present && m_glfwWindow == nullptr) {
    *error = "Presenting with Vulkan needs a GLFW window";
    return false;
  }
  if (!m_context.initialize(present ? m_glfwWindow : nullptr, error)) {
    return false;
  }
  if (!initializeGlslCompiler()) {
    *error = "The GLSL compiler could not start";
    return false;
  }
  m_compilerStarted = true;
  const int requested = window != nullptr ? window->getMsaaSamples() : 0;
  m_samples = m_context.sampleCountFor(requested > 0 ? requested : 0);

  // The pipeline cache a previous run saved for this device and driver
  // (D-R38); Vulkan also validates the data's own header.
  const VkPhysicalDeviceProperties& properties = m_context.properties();
  const std::uint32_t identity[3] = { properties.vendorID,
                                      properties.deviceID,
                                      properties.driverVersion };
  const std::span<const unsigned char> keyParts[] = {
    { reinterpret_cast<const unsigned char*>(identity), sizeof(identity) },
    { properties.pipelineCacheUUID, VK_UUID_SIZE },
  };
  m_pipelineCacheKey = GpuShaderCache::key("vulkan-pipelines-1", keyParts);
  std::vector<unsigned char> saved;
  m_shaderCache.load(m_pipelineCacheKey, &saved);
  VkPipelineCacheCreateInfo cache{};
  cache.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cache.initialDataSize = saved.size();
  cache.pInitialData = saved.empty() ? nullptr : saved.data();
  if (vkCreatePipelineCache(
        m_context.device(), &cache, nullptr, &m_pipelineCache) != VK_SUCCESS &&
      !saved.empty()) {
    cache.initialDataSize = 0;
    cache.pInitialData = nullptr;
    vkCreatePipelineCache(
      m_context.device(), &cache, nullptr, &m_pipelineCache);
  }
  if (!createSlots(error) || !createDefaults(error) || !ensureBackbuffer()) {
    if (error->empty()) {
      *error = "The Vulkan backbuffer could not be created";
    }
    return false;
  }
  if (m_present) {
    createSwapchain();
  }
  // A fresh OpenGL context: depth testing off, the viewport the window's.
  m_state = PipelineState{};
  m_state.depthTestEnabled = false;
  m_viewport = { 0, 0, m_backbuffer.width, m_backbuffer.height };
  m_context.logDescription();
  Logger::LogTrace("Vulkan backbuffer: " + std::to_string(m_backbuffer.width) +
                   "x" + std::to_string(m_backbuffer.height) + ", " +
                   std::to_string(static_cast<int>(m_samples)) + "x MSAA" +
                   (m_present ? "" : ", offscreen"));
  m_initialized = true;
  return true;
}

bool
VulkanDevice::createSlots(std::string* error)
{
  const VkDevice device = m_context.device();
  for (Slot& slot : m_slots) {
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool.queueFamilyIndex = m_context.queueFamily();
    if (vkCreateCommandPool(device, &pool, nullptr, &slot.pool) != VK_SUCCESS) {
      *error = "Vulkan command pools could not be created";
      return false;
    }
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = slot.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 2;
    VkCommandBuffer buffers[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    if (vkAllocateCommandBuffers(device, &allocate, buffers) != VK_SUCCESS) {
      *error = "Vulkan command buffers could not be allocated";
      return false;
    }
    slot.upload = buffers[0];
    slot.main = buffers[1];
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device, &fence, nullptr, &slot.fence) != VK_SUCCESS) {
      *error = "Vulkan synchronization objects could not be created";
      return false;
    }
  }
  return true;
}

void
VulkanDevice::ensureRecording()
{
  if (m_recording) {
    return;
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.beginRecording");
  m_slotIndex = (m_slotIndex + 1) % kSlotCount;
  Slot& slot = m_slots[m_slotIndex];
  const VkDevice device = m_context.device();
  if (slot.serial != 0) {
    ILLUMO_PROFILE_ZONE("VulkanDevice.waitSlot");
    vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    m_completedSerial = std::max(m_completedSerial, slot.serial);
  }
  collectRetired();
  vkResetFences(device, 1, &slot.fence);
  vkResetCommandPool(device, slot.pool, 0);
  for (StagingChunk& chunk : slot.staging) {
    chunk.used = 0;
  }
  m_recordingSerial = m_lastSubmittedSerial + 1;
  slot.serial = m_recordingSerial;
  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  // The upload command buffer begins with its first copy (transferCommands).
  vkBeginCommandBuffer(slot.main, &begin);
  m_recording = true;
  m_uploadUsed = false;
  m_mainTransfers = false;
  m_renderingActive = false;
  m_recorded = RecordedState{};
}

static void
globalBarrier(VkCommandBuffer commands,
              VkPipelineStageFlags2 sourceStage,
              VkAccessFlags2 sourceAccess,
              VkPipelineStageFlags2 destinationStage,
              VkAccessFlags2 destinationAccess)
{
  VkMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
  barrier.srcStageMask = sourceStage;
  barrier.srcAccessMask = sourceAccess;
  barrier.dstStageMask = destinationStage;
  barrier.dstAccessMask = destinationAccess;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.memoryBarrierCount = 1;
  dependency.pMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commands, &dependency);
}

void
VulkanDevice::flush(bool presentFrame)
{
  if (!m_recording) {
    if (!presentFrame) {
      return;
    }
    ensureRecording();
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.flush");
  endRendering();
  endMainTransfers();
  Slot& slot = m_slots[m_slotIndex];
  uint32_t imageIndex = 0;
  VkSemaphore acquired = VK_NULL_HANDLE;
  const bool presenting =
    presentFrame && recordPresentation(&imageIndex, &acquired);
  if (m_uploadUsed) {
    globalBarrier(slot.upload,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                  VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
  }
  if (m_uploadUsed) {
    vkEndCommandBuffer(slot.upload);
  }
  vkEndCommandBuffer(slot.main);

  VkCommandBufferSubmitInfo commandInfos[2]{};
  uint32_t commandCount = 0;
  if (m_uploadUsed) {
    commandInfos[commandCount].sType =
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfos[commandCount].commandBuffer = slot.upload;
    ++commandCount;
  }
  commandInfos[commandCount].sType =
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  commandInfos[commandCount].commandBuffer = slot.main;
  ++commandCount;
  VkSemaphoreSubmitInfo wait{};
  wait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  wait.semaphore = acquired;
  wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  VkSemaphoreSubmitInfo signal{};
  signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  if (presenting) {
    signal.semaphore = m_swapchain.finished[imageIndex];
  }
  VkSubmitInfo2 submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  submit.commandBufferInfoCount = commandCount;
  submit.pCommandBufferInfos = commandInfos;
  submit.waitSemaphoreInfoCount = presenting ? 1u : 0u;
  submit.pWaitSemaphoreInfos = &wait;
  submit.signalSemaphoreInfoCount = presenting ? 1u : 0u;
  submit.pSignalSemaphoreInfos = &signal;
  VkResult submitted = VK_SUCCESS;
  {
    ILLUMO_PROFILE_ZONE("VulkanDevice.queueSubmit");
    submitted = vkQueueSubmit2(m_context.queue(), 1, &submit, slot.fence);
  }
  m_recording = false;
  m_lastSubmittedSerial = m_recordingSerial;
  if (submitted != VK_SUCCESS) {
    // The fence will never signal; later waits must not hang on it.
    Logger::LogError("Vulkan queue submission failed: " +
                     vulkanResultText(submitted));
    reportFrameError("Vulkan queue submission failed");
    vkDeviceWaitIdle(m_context.device());
    slot.serial = 0;
    m_completedSerial = m_lastSubmittedSerial;
    return;
  }
  if (presenting) {
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &m_swapchain.finished[imageIndex];
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain.swapchain;
    info.pImageIndices = &imageIndex;
    VkResult shown = VK_SUCCESS;
    {
      ILLUMO_PROFILE_ZONE("VulkanDevice.queuePresent");
      shown = vkQueuePresentKHR(m_context.queue(), &info);
    }
    if (shown == VK_ERROR_OUT_OF_DATE_KHR || shown == VK_SUBOPTIMAL_KHR) {
      m_swapchainStale = true;
    } else if (shown != VK_SUCCESS) {
      Logger::LogWarning("Vulkan present failed: " + vulkanResultText(shown));
      m_swapchainStale = true;
    }
  }
}

bool
VulkanDevice::waitForSerial(uint64_t serial, uint64_t timeoutNanoseconds)
{
  if (serial <= m_completedSerial) {
    return true;
  }
  if (serial > m_lastSubmittedSerial) {
    return false;
  }
  for (Slot& slot : m_slots) {
    if (slot.serial != serial) {
      continue;
    }
    const VkResult result = vkWaitForFences(
      m_context.device(), 1, &slot.fence, VK_TRUE, timeoutNanoseconds);
    if (result != VK_SUCCESS) {
      return false;
    }
    m_completedSerial = std::max(m_completedSerial, serial);
    return true;
  }
  // The slot was reused, which waited for this serial first.
  m_completedSerial = std::max(m_completedSerial, serial);
  return true;
}

bool
VulkanDevice::isSerialComplete(uint64_t serial)
{
  return waitForSerial(serial, 0);
}

uint64_t
VulkanDevice::retireSerial() const
{
  return m_recording ? m_recordingSerial : m_lastSubmittedSerial;
}

void
VulkanDevice::retire(Retired retired)
{
  retired.serial = retireSerial();
  m_retired.push_back(std::move(retired));
}

void
VulkanDevice::retireImage(VulkanImage& image)
{
  if (image.image == VK_NULL_HANDLE) {
    return;
  }
  Retired retired;
  retired.image = image.image;
  retired.allocation = image.allocation;
  retired.views[0] = image.sampledView;
  retired.views[1] = image.attachmentView != image.sampledView
                       ? image.attachmentView
                       : VK_NULL_HANDLE;
  retired.views[2] = image.srgbView;
  retire(std::move(retired));
  image = VulkanImage{};
}

void
VulkanDevice::retireBuffer(VulkanBufferMemory& buffer)
{
  if (buffer.buffer == VK_NULL_HANDLE) {
    return;
  }
  Retired retired;
  retired.buffer = buffer.buffer;
  retired.allocation = buffer.allocation;
  retire(std::move(retired));
  buffer = VulkanBufferMemory{};
}

void
VulkanDevice::collectRetired()
{
  const VkDevice device = m_context.device();
  size_t kept = 0;
  for (size_t index = 0; index < m_retired.size(); ++index) {
    Retired& retired = m_retired[index];
    if (retired.serial > m_completedSerial) {
      if (kept != index) {
        m_retired[kept] = std::move(retired);
      }
      ++kept;
      continue;
    }
    for (VkPipeline pipeline : retired.pipelines) {
      vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (retired.pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, retired.pipelineLayout, nullptr);
    }
    if (retired.setLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, retired.setLayout, nullptr);
    }
    for (VkShaderModule module : retired.modules) {
      if (module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, module, nullptr);
      }
    }
    for (VkImageView view : retired.views) {
      if (view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, view, nullptr);
      }
    }
    if (retired.image != VK_NULL_HANDLE) {
      vmaDestroyImage(m_context.allocator(), retired.image, retired.allocation);
    }
    if (retired.buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(
        m_context.allocator(), retired.buffer, retired.allocation);
    }
  }
  m_retired.resize(kept);
}

bool
VulkanDevice::allocateStaging(VkDeviceSize size,
                              VkDeviceSize alignment,
                              StagingSpan* span)
{
  ensureRecording();
  Slot& slot = m_slots[m_slotIndex];
  const VkDeviceSize align = std::max<VkDeviceSize>(alignment, 16);
  for (StagingChunk& chunk : slot.staging) {
    const VkDeviceSize offset = (chunk.used + align - 1) / align * align;
    if (offset + size <= chunk.size) {
      chunk.used = offset + size;
      span->buffer = chunk.buffer;
      span->offset = offset;
      span->mapped = chunk.mapped + offset;
      return true;
    }
  }
  StagingChunk chunk;
  chunk.size = std::max(kStagingChunkBytes, size + align);
  VkBufferCreateInfo buffer{};
  buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer.size = chunk.size;
  buffer.usage =
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
  allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
  allocation.requiredFlags =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  VmaAllocationInfo info{};
  if (vmaCreateBuffer(m_context.allocator(),
                      &buffer,
                      &allocation,
                      &chunk.buffer,
                      &chunk.allocation,
                      &info) != VK_SUCCESS) {
    return false;
  }
  chunk.mapped = static_cast<unsigned char*>(info.pMappedData);
  chunk.used = size;
  span->buffer = chunk.buffer;
  span->offset = 0;
  span->mapped = chunk.mapped;
  slot.staging.push_back(chunk);
  return true;
}

VkCommandBuffer
VulkanDevice::transferCommands(uint64_t useSerial)
{
  ensureRecording();
  if (useSerial == m_recordingSerial) {
    beginMainTransfers();
    return m_slots[m_slotIndex].main;
  }
  VkCommandBuffer upload = m_slots[m_slotIndex].upload;
  if (!m_uploadUsed) {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(upload, &begin);
    // Earlier submissions may still read or write what these copies
    // overwrite.
    globalBarrier(upload,
                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                  VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT |
                    VK_ACCESS_2_TRANSFER_READ_BIT);
    m_uploadUsed = true;
  }
  return upload;
}

void
VulkanDevice::beginMainTransfers()
{
  if (m_mainTransfers) {
    return;
  }
  if (m_renderingActive) {
    m_stats.renderPassBreaks += 1;
  }
  endRendering();
  globalBarrier(m_slots[m_slotIndex].main,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);
  m_mainTransfers = true;
}

void
VulkanDevice::endMainTransfers()
{
  if (!m_mainTransfers) {
    return;
  }
  globalBarrier(m_slots[m_slotIndex].main,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
  m_mainTransfers = false;
}

static void
layoutScope(VkImageLayout layout,
            VkPipelineStageFlags2* stage,
            VkAccessFlags2* access)
{
  switch (layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      *stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
      *access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      *stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
      *access = VK_ACCESS_2_TRANSFER_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      *stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
      *access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
      break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      *stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
      *access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
      *stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
      *access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      *stage = VK_PIPELINE_STAGE_2_NONE;
      *access = VK_ACCESS_2_NONE;
      break;
    default:
      *stage = VK_PIPELINE_STAGE_2_NONE;
      *access = VK_ACCESS_2_NONE;
      break;
  }
}

void
VulkanDevice::transition(VkCommandBuffer commands,
                         VulkanImage& image,
                         VkImageLayout layout)
{
  VkImageMemoryBarrier2 barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  layoutScope(image.layout, &barrier.srcStageMask, &barrier.srcAccessMask);
  layoutScope(layout, &barrier.dstStageMask, &barrier.dstAccessMask);
  if (image.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  }
  if (layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  }
  barrier.oldLayout = image.layout;
  barrier.newLayout = layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image.image;
  barrier.subresourceRange.aspectMask = image.aspect;
  barrier.subresourceRange.levelCount = image.mipLevels;
  barrier.subresourceRange.layerCount = image.layers;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 1;
  dependency.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(commands, &dependency);
  image.layout = layout;
}

static bool
hasStencil(VkFormat format)
{
  return format == VK_FORMAT_D24_UNORM_S8_UINT ||
         format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
         format == VK_FORMAT_D16_UNORM_S8_UINT;
}

static bool
isDepthFormat(VkFormat format)
{
  return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM ||
         format == VK_FORMAT_X8_D24_UNORM_PACK32 || hasStencil(format);
}

bool
VulkanDevice::createImage(VulkanImage& image,
                          VkFormat format,
                          uint32_t width,
                          uint32_t height,
                          uint32_t mipLevels,
                          uint32_t layers,
                          VkSampleCountFlagBits samples,
                          VkImageUsageFlags usage,
                          VkImageCreateFlags flags)
{
  VkImageCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  create.flags = flags;
  create.imageType = VK_IMAGE_TYPE_2D;
  create.format = format;
  create.extent = { width, height, 1 };
  create.mipLevels = mipLevels;
  create.arrayLayers = layers;
  create.samples = samples;
  create.tiling = VK_IMAGE_TILING_OPTIMAL;
  create.usage = usage;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  if (vmaCreateImage(m_context.allocator(),
                     &create,
                     &allocation,
                     &image.image,
                     &image.allocation,
                     nullptr) != VK_SUCCESS) {
    image.image = VK_NULL_HANDLE;
    image.allocation = nullptr;
    return false;
  }
  image.format = format;
  image.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  if (isDepthFormat(format)) {
    image.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasStencil(format)) {
      image.aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
  }
  image.width = width;
  image.height = height;
  image.mipLevels = mipLevels;
  image.layers = layers;
  image.samples = samples;
  image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image.useSerial = 0;
  return true;
}

bool
VulkanDevice::createImageView(VulkanImage& image,
                              VkImageViewType type,
                              VkComponentMapping components,
                              VkImageAspectFlags aspect,
                              VkImageView* view,
                              VkFormat format)
{
  VkImageViewCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  create.image = image.image;
  create.viewType = type;
  create.format = format != VK_FORMAT_UNDEFINED ? format : image.format;
  create.components = components;
  create.subresourceRange.aspectMask = aspect;
  create.subresourceRange.levelCount = image.mipLevels;
  create.subresourceRange.layerCount = image.layers;
  return vkCreateImageView(m_context.device(), &create, nullptr, view) ==
         VK_SUCCESS;
}

bool
VulkanDevice::createBufferMemory(VulkanBufferMemory& memory,
                                 VkDeviceSize size,
                                 VkBufferUsageFlags usage)
{
  VkBufferCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  create.size = size;
  create.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  if (vmaCreateBuffer(m_context.allocator(),
                      &create,
                      &allocation,
                      &memory.buffer,
                      &memory.allocation,
                      nullptr) != VK_SUCCESS) {
    memory = VulkanBufferMemory{};
    return false;
  }
  memory.size = size;
  memory.useSerial = 0;
  return true;
}

void
VulkanDevice::uploadToBuffer(VulkanBufferMemory& memory,
                             VkDeviceSize offset,
                             const void* data,
                             VkDeviceSize size)
{
  if (size == 0 || data == nullptr) {
    return;
  }
  StagingSpan span;
  if (!allocateStaging(size, 4, &span)) {
    reportFrameError("Vulkan staging memory is exhausted");
    return;
  }
  std::memcpy(span.mapped, data, static_cast<size_t>(size));
  VkCommandBuffer commands = transferCommands(memory.useSerial);
  VkBufferCopy region{};
  region.srcOffset = span.offset;
  region.dstOffset = offset;
  region.size = size;
  vkCmdCopyBuffer(commands, span.buffer, memory.buffer, 1, &region);
  m_stats.uploadBytes += static_cast<size_t>(size);
}

void
VulkanDevice::uploadToImage(VulkanImage& image,
                            uint32_t layer,
                            int x,
                            int y,
                            int width,
                            int height,
                            const unsigned char* texels,
                            size_t texelBytes)
{
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) *
                             static_cast<VkDeviceSize>(height) * texelBytes;
  StagingSpan span;
  if (!allocateStaging(bytes, 16, &span)) {
    reportFrameError("Vulkan staging memory is exhausted");
    return;
  }
  std::memcpy(span.mapped, texels, static_cast<size_t>(bytes));
  VkCommandBuffer commands = transferCommands(image.useSerial);
  transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  VkBufferImageCopy region{};
  region.bufferOffset = span.offset;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = layer;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = { x, y, 0 };
  region.imageExtent = { static_cast<uint32_t>(width),
                         static_cast<uint32_t>(height),
                         1 };
  vkCmdCopyBufferToImage(commands,
                         span.buffer,
                         image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1,
                         &region);
  transition(commands, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void
VulkanDevice::generateMipmaps(VulkanImage& image)
{
  if (image.mipLevels <= 1) {
    return;
  }
  VkCommandBuffer commands = transferCommands(image.useSerial);
  transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  int32_t width = static_cast<int32_t>(image.width);
  int32_t height = static_cast<int32_t>(image.height);
  for (uint32_t level = 1; level < image.mipLevels; ++level) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = level - 1;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = image.layers;
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commands, &dependency);

    const int32_t nextWidth = width > 1 ? width / 2 : 1;
    const int32_t nextHeight = height > 1 ? height / 2 : 1;
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = level - 1;
    blit.srcSubresource.layerCount = image.layers;
    blit.srcOffsets[1] = { width, height, 1 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = level;
    blit.dstSubresource.layerCount = image.layers;
    blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
    vkCmdBlitImage(commands,
                   image.image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   image.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1,
                   &blit,
                   VK_FILTER_LINEAR);
    width = nextWidth;
    height = nextHeight;
  }
  // Levels below the last are TRANSFER_SRC, the last TRANSFER_DST.
  VkImageMemoryBarrier2 barriers[2]{};
  for (int index = 0; index < 2; ++index) {
    VkImageMemoryBarrier2& barrier = barriers[index];
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.layerCount = image.layers;
  }
  barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barriers[0].subresourceRange.baseMipLevel = 0;
  barriers[0].subresourceRange.levelCount = image.mipLevels - 1;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barriers[1].subresourceRange.baseMipLevel = image.mipLevels - 1;
  barriers[1].subresourceRange.levelCount = 1;
  VkDependencyInfo dependency{};
  dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dependency.imageMemoryBarrierCount = 2;
  dependency.pImageMemoryBarriers = barriers;
  vkCmdPipelineBarrier2(commands, &dependency);
  image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

VkSampler
VulkanDevice::samplerFor(TextureFilter filter,
                         TextureWrap wrapX,
                         TextureWrap wrapY,
                         bool mipmaps,
                         bool depthBorder)
{
  const uint32_t key = (filter == TextureFilter::Linear ? 1u : 0u) |
                       (wrapX == TextureWrap::Repeat ? 2u : 0u) |
                       (wrapY == TextureWrap::Repeat ? 4u : 0u) |
                       (mipmaps ? 8u : 0u) | (depthBorder ? 16u : 0u);
  std::unordered_map<uint32_t, VkSampler>::const_iterator found =
    m_samplers.find(key);
  if (found != m_samplers.end()) {
    return found->second;
  }
  const VkFilter vkFilter =
    filter == TextureFilter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  VkSamplerCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  create.magFilter = vkFilter;
  create.minFilter = vkFilter;
  create.mipmapMode = mipmaps && filter == TextureFilter::Linear
                        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  create.addressModeU = wrapX == TextureWrap::Repeat
                          ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                          : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  create.addressModeV = wrapY == TextureWrap::Repeat
                          ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                          : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  create.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (depthBorder) {
    create.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    create.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    create.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  }
  create.minLod = 0.0f;
  create.maxLod = mipmaps ? VK_LOD_CLAMP_NONE : 0.0f;
  VkSampler sampler = VK_NULL_HANDLE;
  if (vkCreateSampler(m_context.device(), &create, nullptr, &sampler) !=
      VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  m_samplers[key] = sampler;
  return sampler;
}

bool
VulkanDevice::createDefaults(std::string* error)
{
  // OpenGL samples an incomplete or missing texture as (0, 0, 0, 1).
  const unsigned char black[4] = { 0, 0, 0, 255 };
  std::unique_ptr<VulkanTexture> texture =
    buildTexture(black, 1, 1, 4, TextureOptions{});
  const std::array<const unsigned char*, 6> faces = { black, black, black,
                                                      black, black, black };
  std::unique_ptr<VulkanTexture> cube = buildCubemap(faces, 1, 1, 4);
  if (!texture || !cube) {
    *error = "Vulkan default textures could not be created";
    return false;
  }
  m_blackTexture = std::move(*texture);
  m_blackCube = std::move(*cube);
  const std::vector<unsigned char> zeros(16384, 0);
  const float attribute[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  if (!createBufferMemory(
        m_zeroUniforms, zeros.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ||
      !createBufferMemory(m_defaultAttributes,
                          sizeof(attribute),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
    *error = "Vulkan default buffers could not be created";
    return false;
  }
  uploadToBuffer(m_zeroUniforms, 0, zeros.data(), zeros.size());
  uploadToBuffer(m_defaultAttributes, 0, attribute, sizeof(attribute));
  return true;
}

void
VulkanDevice::shutdown()
{
  if (m_context.device() == VK_NULL_HANDLE) {
    if (m_compilerStarted) {
      finalizeGlslCompiler();
      m_compilerStarted = false;
    }
    m_initialized = false;
    return;
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.shutdown");
  if (m_recording) {
    flush(false);
  }
  const VkDevice device = m_context.device();
  vkDeviceWaitIdle(device);
  m_recording = false;
  for (Entry<VulkanMesh>& entry : m_meshes) {
    if (entry.resource) {
      releaseMesh(*entry.resource);
    }
  }
  for (Entry<VulkanProgram>& entry : m_programs) {
    if (entry.resource) {
      releaseProgram(*entry.resource);
    }
  }
  for (Entry<VulkanTexture>& entry : m_textures) {
    if (entry.resource) {
      releaseTexture(*entry.resource);
    }
  }
  for (Entry<VulkanBuffer>& entry : m_buffers) {
    if (entry.resource) {
      retireBuffer(entry.resource->memory);
    }
  }
  m_meshes.clear();
  m_programs.clear();
  m_textures.clear();
  m_framebuffers.clear();
  m_buffers.clear();
  m_meshHandles.clear();
  m_shaderHandles.clear();
  m_textureHandles.clear();
  m_framebufferHandles.clear();
  m_bufferHandles.clear();
  releaseTexture(m_blackTexture);
  releaseTexture(m_blackCube);
  retireBuffer(m_zeroUniforms);
  retireBuffer(m_defaultAttributes);
  retireImage(m_backbuffer.color);
  retireImage(m_backbuffer.depth);
  retireImage(m_backbuffer.resolve);
  m_backbuffer = Backbuffer{};
  for (std::pair<const std::uint32_t, ReadbackStream>& stream : m_readbacks) {
    for (ReadbackSlot& slot : stream.second.slots) {
      if (slot.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_context.allocator(), slot.buffer, slot.allocation);
      }
    }
  }
  m_readbacks.clear();
  m_completedSerial = UINT64_MAX;
  collectRetired();
  m_completedSerial = m_lastSubmittedSerial;
  destroySwapchain();
  for (std::pair<const uint32_t, VkSampler>& sampler : m_samplers) {
    vkDestroySampler(device, sampler.second, nullptr);
  }
  m_samplers.clear();
  for (Slot& slot : m_slots) {
    for (StagingChunk& chunk : slot.staging) {
      vmaDestroyBuffer(m_context.allocator(), chunk.buffer, chunk.allocation);
    }
    slot.staging.clear();
    if (slot.fence != VK_NULL_HANDLE) {
      vkDestroyFence(device, slot.fence, nullptr);
    }
    if (slot.pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, slot.pool, nullptr);
    }
    slot = Slot{};
  }
  if (m_pipelineCache != VK_NULL_HANDLE) {
    // Keep this run's pipelines for the next (D-R38).
    if (m_shaderCache.enabled()) {
      size_t size = 0;
      if (vkGetPipelineCacheData(device, m_pipelineCache, &size, nullptr) ==
            VK_SUCCESS &&
          size != 0) {
        std::vector<unsigned char> data(size);
        if (vkGetPipelineCacheData(
              device, m_pipelineCache, &size, data.data()) == VK_SUCCESS) {
          data.resize(size);
          m_shaderCache.store(m_pipelineCacheKey, data);
        }
      }
    }
    vkDestroyPipelineCache(device, m_pipelineCache, nullptr);
    m_pipelineCache = VK_NULL_HANDLE;
  }
  m_context.shutdown();
  if (m_compilerStarted) {
    finalizeGlslCompiler();
    m_compilerStarted = false;
  }
  m_initialized = false;
}
