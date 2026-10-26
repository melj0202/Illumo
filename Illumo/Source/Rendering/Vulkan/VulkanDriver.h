#pragma once

#include "Rendering/Gpu/GlslToSpirv.h"
#include "Rendering/Gpu/GpuDriver.h"
#include "Rendering/Gpu/GpuProgramState.h"
#include "Rendering/Gpu/GpuShaderCache.h"
#include "VulkanCommon.h"
#include "VulkanContext.h"
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/FrameReadback.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IRenderWindow;
struct GLFWwindow;

struct VulkanImage
{
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  // The view shaders sample (swizzled like the OpenGL texture) and, for
  // render targets, the identity view attachments need.
  VkImageView sampledView = VK_NULL_HANDLE;
  VkImageView attachmentView = VK_NULL_HANDLE;
  // Backbuffer images only: an sRGB view for the multisample resolve.
  VkImageView srgbView = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mipLevels = 1;
  uint32_t layers = 1;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  // The recording that last used the image in its main command buffer.
  uint64_t useSerial = 0;
};

struct VulkanBufferMemory
{
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = nullptr;
  VkDeviceSize size = 0;
  uint64_t useSerial = 0;
};

struct VulkanTexture : GpuTexture
{
  VulkanImage image;
  VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanMesh : GpuMesh
{
  VulkanBufferMemory vertices;
  VulkanBufferMemory indices;
};

struct VulkanBuffer : GpuBuffer
{
  VulkanBufferMemory memory;
};

static constexpr unsigned kVulkanMaxColorAttachments = 8;

struct VulkanPipelineKey
{
  uint32_t colorFormats[kVulkanMaxColorAttachments];
  uint32_t depthFormat;
  uint32_t stencilFormat;
  uint8_t colorCount;
  uint8_t samples;
  uint8_t topology;
  uint8_t meshLayout;
  uint8_t instanceLayout;
  uint8_t polygonLine;
  uint8_t blend;
  uint8_t blendSrc;
  uint8_t blendDst;
  uint8_t padding[3];
};

struct VulkanPipelineKeyHash
{
  size_t operator()(const VulkanPipelineKey& key) const;
};

struct VulkanPipelineKeyEqual
{
  bool operator()(const VulkanPipelineKey& left,
                  const VulkanPipelineKey& right) const;
};

struct VulkanProgram : GpuProgram
{
  VkShaderModule vertexModule = VK_NULL_HANDLE;
  VkShaderModule fragmentModule = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  uint64_t blockSerial = 0;
  VkBuffer blockBuffer = VK_NULL_HANDLE;
  VkDeviceSize blockOffset = 0;
  std::unordered_map<VulkanPipelineKey,
                     VkPipeline,
                     VulkanPipelineKeyHash,
                     VulkanPipelineKeyEqual>
    pipelines;
};

// GpuDriver on Vulkan 1.3: the resources, recording, submissions and
// presentation behind the shared token interpreter, with the OpenGL backend's
// observable semantics (docs/vulkan-backend-plan.md, section 5.2).
class VulkanDriver final : public GpuDriver
{
public:
  VulkanDriver() = default;
  ~VulkanDriver() override;
  VulkanDriver(const VulkanDriver&) = delete;
  VulkanDriver& operator=(const VulkanDriver&) = delete;
  VulkanDriver(VulkanDriver&&) = delete;
  VulkanDriver& operator=(VulkanDriver&&) = delete;

  const char* apiName() const override;
  bool initialize(IRenderWindow* window,
                  bool present,
                  std::string* error) override;
  void shutdown() override;

  void beginFrame() override;
  void endFrame() override;
  void plotFrameStats(size_t commandQueueHighWater) const override;

  // Synchronous: submits everything recorded and waits for it.
  FrameReadback readBackbuffer(int width, int height) override;
  bool requestFramebufferReadback(std::uint32_t stream,
                                  FramebufferHandle framebuffer,
                                  int width,
                                  int height) override;
  bool takeFramebufferReadback(std::uint32_t stream,
                               bool wait,
                               FrameReadback& out) override;
  void releaseReadbackStream(std::uint32_t stream) override;

private:
  static constexpr uint32_t kSlotCount = 3;

  struct StagingChunk
  {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    unsigned char* mapped = nullptr;
    VkDeviceSize size = 0;
    VkDeviceSize used = 0;
  };

  struct Slot
  {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer upload = VK_NULL_HANDLE;
    VkCommandBuffer main = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint64_t serial = 0;
    std::vector<StagingChunk> staging;
  };

  struct StagingSpan
  {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    unsigned char* mapped = nullptr;
  };

  struct Retired
  {
    uint64_t serial = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkImageView views[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkShaderModule modules[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<VkPipeline> pipelines;
  };

  // The colour, depth and resolve images standing in for OpenGL's default
  // framebuffer, stored bottom row first as OpenGL stores it.
  struct Backbuffer
  {
    VulkanImage color;
    VulkanImage depth;
    VulkanImage resolve;
    int width = 0;
    int height = 0;
  };

  // One image acquisition: the semaphore a presenting submission waits on,
  // and a fence that shows on the CPU when the image is really free.
  struct Acquisition
  {
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    // The acquire was issued and its fence not yet seen signalled.
    bool outstanding = false;
    // The submission that waited on the semaphore; 0 before any.
    uint64_t waitSerial = 0;
  };

  struct Swapchain
  {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{ 0, 0 };
    bool vsync = true;
    std::vector<VkImage> images;
    std::vector<VkSemaphore> finished;
    std::vector<Acquisition> acquisitions;
    // An acquired image still held by the compositor, presented once free.
    int pendingAcquisition = -1;
    uint32_t pendingImage = 0;
  };

  // One attachment of the render target being drawn into.
  struct TargetAttachment
  {
    VulkanImage* image = nullptr;
    // Render-target textures rest shader-readable between passes.
    bool restsReadable = false;
  };

  struct RenderTarget
  {
    bool backbuffer = true;
    FramebufferHandle handle{};
    std::array<TargetAttachment, kVulkanMaxColorAttachments> colors{};
    unsigned colorCount = 0;
    TargetAttachment depth{};
    std::array<TextureFormat, kVulkanMaxColorAttachments> colorFormats{};
    int width = 0;
    int height = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  };

  struct ReadbackSlot
  {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize size = 0;
    int width = 0;
    int height = 0;
    VkDeviceSize rowBytes = 0;
    TextureFormat format = TextureFormat::RGBA8;
    uint64_t serial = 0;
    uint64_t order = 0;
    bool pending = false;
  };

  struct ReadbackStream
  {
    std::array<ReadbackSlot, 2> slots;
  };

  // Frames, submissions and deferred destruction (VulkanDriver.cpp).
  bool createSlots(std::string* error);
  void ensureRecording();
  void flush(bool present);
  bool waitForSerial(uint64_t serial, uint64_t timeoutNanoseconds);
  bool isSerialComplete(uint64_t serial);
  void collectRetired();
  uint64_t retireSerial() const;
  void retire(Retired retired);
  void retireImage(VulkanImage& image);
  void retireBuffer(VulkanBufferMemory& buffer);
  bool allocateStaging(VkDeviceSize size,
                       VkDeviceSize alignment,
                       StagingSpan* span);
  VkCommandBuffer transferCommands(uint64_t useSerial);
  void beginMainTransfers();
  void endMainTransfers();
  void transition(VkCommandBuffer commands,
                  VulkanImage& image,
                  VkImageLayout layout);
  bool createImage(VulkanImage& image,
                   VkFormat format,
                   uint32_t width,
                   uint32_t height,
                   uint32_t mipLevels,
                   uint32_t layers,
                   VkSampleCountFlagBits samples,
                   VkImageUsageFlags usage,
                   VkImageCreateFlags flags);
  bool createImageView(VulkanImage& image,
                       VkImageViewType type,
                       VkComponentMapping components,
                       VkImageAspectFlags aspect,
                       VkImageView* view,
                       VkFormat format = VK_FORMAT_UNDEFINED);
  bool createBufferMemory(VulkanBufferMemory& memory,
                          VkDeviceSize size,
                          VkBufferUsageFlags usage);
  void uploadToBuffer(VulkanBufferMemory& memory,
                      VkDeviceSize offset,
                      const void* data,
                      VkDeviceSize size);
  void uploadToImage(VulkanImage& image,
                     uint32_t layer,
                     int x,
                     int y,
                     int width,
                     int height,
                     const unsigned char* texels,
                     size_t texelBytes);
  void generateMipmaps(VulkanImage& image);
  VkSampler samplerFor(TextureFilter filter,
                       TextureWrap wrapX,
                       TextureWrap wrapY,
                       bool mipmaps,
                       bool depthBorder);
  bool createDefaults(std::string* error);

  // Resources (VulkanDriverResources.cpp).
  std::unique_ptr<GpuMesh> buildMesh(size_t vertexSize,
                                     size_t indexSize) override;
  void releaseMesh(GpuMesh& mesh) override;
  std::unique_ptr<GpuProgram> buildProgram(const ShaderSources& sources,
                                           std::string* error) override;
  void releaseProgram(GpuProgram& program) override;
  std::unique_ptr<GpuTexture> buildTexture(
    const unsigned char* data,
    int width,
    int height,
    int channels,
    const TextureOptions& options) override;
  std::unique_ptr<GpuTexture> buildCubemap(
    const std::array<const unsigned char*, 6>& faces,
    int width,
    int height,
    int channels) override;
  std::unique_ptr<GpuTexture> buildRenderTarget(int width,
                                                int height,
                                                TextureFormat format,
                                                TextureFilter filter,
                                                TextureWrap wrap) override;
  void releaseTexture(GpuTexture& texture) override;
  std::unique_ptr<GpuBuffer> buildBuffer(size_t capacityBytes) override;
  void releaseBuffer(GpuBuffer& buffer) override;
  unsigned maxColorAttachments() const override;
  void releaseFramebufferTarget(FramebufferHandle handle) override;
  int maxTextureSize() const;
  // This driver's resource behind a handle, or nullptr.
  VulkanMesh* resolveMesh(MeshHandle handle) const
  {
    return static_cast<VulkanMesh*>(findMesh(handle));
  }
  VulkanProgram* resolveProgram(ShaderHandle handle) const
  {
    return static_cast<VulkanProgram*>(findProgram(handle));
  }
  VulkanTexture* resolveTexture(TextureHandle handle) const
  {
    return static_cast<VulkanTexture*>(findTexture(handle));
  }
  VulkanBuffer* resolveBuffer(BufferHandle handle) const
  {
    return static_cast<VulkanBuffer*>(findBuffer(handle));
  }

  // Tokens (VulkanDriverCommands.cpp).
  void clear(bool color,
             bool depth,
             bool stencil,
             bool mrtAware,
             const CmdClearColor& value,
             float depthValue) override;
  void recordDraw(DrawKind kind,
                  unsigned count,
                  unsigned first,
                  unsigned instances,
                  GpuProgram& program,
                  GpuMesh& mesh) override;
  bool uploadTexture(GpuTexture& texture,
                     const CmdUpdateTexture& update,
                     int channels) override;
  void uploadMeshData(GpuMesh& mesh,
                      bool indices,
                      size_t offset,
                      const void* data,
                      size_t size) override;
  void uploadBufferData(GpuBuffer& buffer,
                        size_t offset,
                        const void* data,
                        size_t size) override;
  bool resolveTarget(RenderTarget* target);
  // Begins rendering to the bound target unless it is already active. A
  // caller that has just resolved the target passes it.
  void ensureRendering(const RenderTarget* resolved = nullptr);
  void endRendering();
  VkPipeline pipelineFor(VulkanProgram& program,
                         const RenderTarget& target,
                         const VulkanMesh& mesh,
                         InstanceLayout instanceLayout);
  VkRect2D scissorRect(const RenderTarget& target) const;

  // Presentation and readback (VulkanDriverPresent.cpp).
  void framebufferSize(int* width, int* height) const;
  bool ensureBackbuffer();
  bool createSwapchain();
  void destroySwapchain();
  // Records the copy of the backbuffer into an acquired swapchain image and
  // names the semaphore the submission must wait on; false when nothing can
  // be shown (minimized, the swapchain is lost, or the compositor still holds
  // the image).
  bool recordPresentation(uint32_t* imageIndex, VkSemaphore* acquired);
  // A free acquisition slot of the swapchain, or -1.
  int freeAcquisition();
  VulkanImage& resolvedBackbuffer(VkCommandBuffer commands);
  bool createReadbackBuffer(ReadbackSlot& slot, VkDeviceSize size);

  VulkanContext m_context;
  IRenderWindow* m_window = nullptr;
  GLFWwindow* m_glfwWindow = nullptr;
  bool m_present = false;
  bool m_initialized = false;
  bool m_compilerStarted = false;
  VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
  // The persisted pipeline cache's key: this GPU and its vendor driver
  // (D-R38).
  Sha256::Digest m_pipelineCacheKey{};

  std::array<Slot, kSlotCount> m_slots{};
  uint32_t m_slotIndex = 0;
  bool m_recording = false;
  uint64_t m_recordingSerial = 0;
  uint64_t m_lastSubmittedSerial = 0;
  uint64_t m_completedSerial = 0;
  bool m_uploadUsed = false;
  bool m_mainTransfers = false;
  std::vector<Retired> m_retired;

  Backbuffer m_backbuffer;
  Swapchain m_swapchain;
  bool m_swapchainStale = false;
  // An acquire timed out (a locked desktop keeps every image); later acquires
  // wait one refresh interval until one succeeds.
  bool m_presentationBlocked = false;
  bool m_presentationBlockReported = false;
  // PlatformSessionLocked, polled at most every 250 ms.
  bool m_sessionLocked = false;
  std::chrono::steady_clock::time_point m_nextSessionCheck{};

  // Command-buffer state of the current recording.
  bool m_renderingActive = false;
  RenderTarget m_target;
  // What the main command buffer being recorded already holds, so a draw
  // records only what changed. Reset with each recording.
  static constexpr size_t kSignatureWords = 32 * 3 + 1;
  struct RecordedState
  {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pushedLayout = VK_NULL_HANDLE;
    // The pushed descriptors: layout, then buffer or view, sampler or
    // offset, and binding for each.
    std::array<uint64_t, kSignatureWords> signature{};
    size_t signatureLength = 0;
    bool dynamicKnown = false;
    VkViewport viewport{};
    VkRect2D scissor{};
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
    VkBool32 depthTest = VK_FALSE;
    bool vertexKnown = false;
    std::array<VkBuffer, 3> vertexBuffers{};
    std::array<VkDeviceSize, 3> vertexOffsets{};
    VkBuffer indexBuffer = VK_NULL_HANDLE;
  };
  RecordedState m_recorded;
  // The last pipeline lookup; consecutive draws usually repeat it.
  const VulkanProgram* m_lastPipelineProgram = nullptr;
  VulkanPipelineKey m_lastPipelineKey{};
  VkPipeline m_lastPipeline = VK_NULL_HANDLE;

  std::unordered_map<uint32_t, VkSampler> m_samplers;
  VulkanTexture m_blackTexture;
  VulkanTexture m_blackCube;
  VulkanBufferMemory m_zeroUniforms;
  VulkanBufferMemory m_defaultAttributes;

  std::unordered_map<std::uint32_t, ReadbackStream> m_readbacks;
  uint64_t m_readbackOrder = 0;
};
