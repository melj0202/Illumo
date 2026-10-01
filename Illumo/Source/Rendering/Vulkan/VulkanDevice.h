#pragma once

#include "VulkanCommon.h"
#include "VulkanContext.h"
#include "VulkanShaderCompiler.h"
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
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IRenderWindow;
struct GLFWwindow;

// Work one frame's submissions did, for profiling plots (as GLFrameStats).
struct VulkanFrameStats
{
  size_t submits = 0;
  size_t commands = 0;
  size_t recordedLists = 0;
  size_t recordedCommands = 0;
  size_t drawCalls = 0;
  size_t uploadBytes = 0;
  size_t renderPassBreaks = 0;
  size_t pipelinesCreated = 0;
};

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

struct VulkanTexture
{
  VulkanImage image;
  int channels = 4;
  bool cubemap = false;
  bool renderTarget = false;
  TextureFormat format = TextureFormat::RGBA8;
  // Bytes per stored texel that updates convert into; 0: not updatable.
  int storageBytes = 4;
  VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanMesh
{
  VulkanBufferMemory vertices;
  VulkanBufferMemory indices;
  MeshVertexLayout layout = MeshVertexLayout::Pos3Color3Uv2;
  bool dynamic = false;
  size_t vertexCapacity = 0;
  size_t indexCapacity = 0;
  unsigned indexCount = 0;
  // The instance stream SetInstanceStream attached, kept as a VAO keeps it.
  BufferHandle instanceBuffer{};
  unsigned instanceOffset = 0;
  InstanceLayout instanceLayout = InstanceLayout::None;
};

struct VulkanBuffer
{
  VulkanBufferMemory memory;
  BufferUsage usage = BufferUsage::Instance;
  size_t capacity = 0;
};

struct VulkanFramebuffer
{
  std::vector<TextureHandle> colorTextures;
  TextureHandle depthTexture{};
  int width = 0;
  int height = 0;
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

struct VulkanUniformSlot
{
  GlslValueType type = GlslValueType::Other;
  unsigned offset = 0;
  unsigned arraySize = 1;
  unsigned arrayStride = 0;
  // Sampler uniforms: index into samplerUnits instead of block storage.
  int sampler = -1;
};

struct TransparentStringHash
{
  using is_transparent = void;
  size_t operator()(std::string_view value) const noexcept
  {
    return std::hash<std::string_view>{}(value);
  }
};

// A linked program, with the uniform state an OpenGL program object keeps:
// values persist across draws and frames and start at zero.
struct VulkanProgram
{
  GlslProgram reflection;
  VkShaderModule vertexModule = VK_NULL_HANDLE;
  VkShaderModule fragmentModule = VK_NULL_HANDLE;
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  std::unordered_map<std::string,
                     VulkanUniformSlot,
                     TransparentStringHash,
                     std::equal_to<>>
    uniforms;
  std::vector<unsigned char> blockData;
  std::vector<int> samplerUnits;
  bool blockDirty = true;
  uint64_t blockSerial = 0;
  VkBuffer blockBuffer = VK_NULL_HANDLE;
  VkDeviceSize blockOffset = 0;
  std::unordered_map<VulkanPipelineKey,
                     VkPipeline,
                     VulkanPipelineKeyHash,
                     VulkanPipelineKeyEqual>
    pipelines;
};

// Executes the backend-neutral token stream with the OpenGL backend's
// observable semantics (docs/vulkan-backend-plan.md, section 5.2), and owns
// every GPU resource, the submissions and presentation.
class VulkanDevice
{
public:
  VulkanDevice() = default;
  ~VulkanDevice();
  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;
  VulkanDevice(VulkanDevice&&) = delete;
  VulkanDevice& operator=(VulkanDevice&&) = delete;

  // present: show frames in the window; otherwise render offscreen only.
  bool initialize(IRenderWindow* window, bool present, std::string* error);
  void shutdown();

  void beginFrame();
  void endFrame();
  void executeQueue(CommandQueue& queue);
  void resetFrameError() { m_frameError.clear(); }
  const std::string& frameError() const { return m_frameError; }
  void resetFrameStats() { m_stats = VulkanFrameStats{}; }
  const VulkanFrameStats& frameStats() const { return m_stats; }

  MeshHandle createMesh(const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize,
                        MeshVertexLayout layout,
                        bool dynamic);
  bool replaceMesh(MeshHandle handle,
                   const void* vertices,
                   size_t vertexSize,
                   const void* indices,
                   size_t indexSize,
                   MeshVertexLayout layout,
                   bool dynamic);
  bool destroyMesh(MeshHandle handle);
  bool isMeshValid(MeshHandle handle) const;

  ShaderHandle createShader(const ShaderSources& sources, std::string* error);
  bool replaceShader(ShaderHandle handle,
                     const ShaderSources& sources,
                     std::string* error);
  bool destroyShader(ShaderHandle handle);
  bool isShaderValid(ShaderHandle handle) const;

  TextureHandle createTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options);
  TextureHandle createCubemap(const std::array<const unsigned char*, 6>& faces,
                              int width,
                              int height,
                              int channels);
  bool replaceTexture(TextureHandle handle,
                      const unsigned char* data,
                      int width,
                      int height,
                      int channels,
                      const TextureOptions& options);
  bool replaceCubemap(TextureHandle handle,
                      const std::array<const unsigned char*, 6>& faces,
                      int width,
                      int height,
                      int channels);
  bool destroyTexture(TextureHandle handle);
  bool isTextureValid(TextureHandle handle) const;
  TextureInfo textureInfo(TextureHandle handle) const;
  int maxTextureSize() const;

  FramebufferHandle createFramebuffer(const FramebufferDesc& desc,
                                      FramebufferAttachments* outAttachments);
  bool destroyFramebuffer(FramebufferHandle handle);
  bool isFramebufferValid(FramebufferHandle handle) const;

  BufferHandle createBuffer(BufferUsage usage, size_t capacityBytes);
  bool destroyBuffer(BufferHandle handle);
  bool isBufferValid(BufferHandle handle) const;

  // Synchronous: submits everything recorded and waits for it.
  FrameReadback readBackbuffer(int width, int height);
  bool requestFramebufferReadback(std::uint32_t stream,
                                  FramebufferHandle framebuffer,
                                  int width,
                                  int height);
  bool takeFramebufferReadback(std::uint32_t stream,
                               bool wait,
                               FrameReadback& out);
  void releaseReadbackStream(std::uint32_t stream);

private:
  static constexpr uint32_t kSlotCount = 3;
  static constexpr unsigned kTextureUnits = 32;
  static constexpr unsigned kUniformBindings = 36;

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

  struct TextureUnit
  {
    TextureHandle texture2D{};
    TextureHandle cube{};
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

  template<typename T>
  struct Entry
  {
    uint32_t generation = 0;
    std::unique_ptr<T> resource;
  };

  // Handle slots are small and dense (ResourceHandlePool), so each registry
  // is a vector indexed by slot; a released slot holds an empty entry.
  template<typename T>
  static Entry<T>& slotEntry(std::vector<Entry<T>>& table, uint32_t slot)
  {
    if (slot >= table.size()) {
      table.resize(static_cast<size_t>(slot) + 1);
    }
    return table[slot];
  }
  template<typename T>
  static T* slotResource(const std::vector<Entry<T>>& table,
                         uint32_t slot,
                         uint32_t generation)
  {
    return slot < table.size() && table[slot].generation == generation
             ? table[slot].resource.get()
             : nullptr;
  }

  // Frames, submissions and deferred destruction (VulkanDevice.cpp).
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

  // Resources (VulkanDeviceResources.cpp).
  std::unique_ptr<VulkanMesh> buildMesh(const void* vertices,
                                        size_t vertexSize,
                                        const void* indices,
                                        size_t indexSize,
                                        MeshVertexLayout layout,
                                        bool dynamic);
  std::unique_ptr<VulkanProgram> buildProgram(const ShaderSources& sources,
                                              std::string* error);
  std::unique_ptr<VulkanTexture> buildTexture(const unsigned char* data,
                                              int width,
                                              int height,
                                              int channels,
                                              const TextureOptions& options);
  std::unique_ptr<VulkanTexture> buildCubemap(
    const std::array<const unsigned char*, 6>& faces,
    int width,
    int height,
    int channels);
  std::unique_ptr<VulkanTexture> buildRenderTarget(int width,
                                                   int height,
                                                   TextureFormat format,
                                                   TextureFilter filter,
                                                   TextureWrap wrap);
  void releaseMesh(VulkanMesh& mesh);
  void releaseProgram(VulkanProgram& program);
  void releaseTexture(VulkanTexture& texture);
  TextureHandle registerTexture(std::unique_ptr<VulkanTexture> texture);
  VulkanMesh* resolveMesh(MeshHandle handle) const;
  VulkanProgram* resolveProgram(ShaderHandle handle) const;
  VulkanTexture* resolveTexture(TextureHandle handle) const;
  VulkanFramebuffer* resolveFramebuffer(FramebufferHandle handle) const;
  VulkanBuffer* resolveBuffer(BufferHandle handle) const;

  // Tokens (VulkanDeviceCommands.cpp).
  void executeCommand(const RenderCommand& command);
  void executeList(const RecordedCommandList* list);
  void reportFrameError(const char* message);
  void setUniform(const char* name,
                  GlslValueType given,
                  const void* value,
                  size_t bytes);
  void updateTexture(const CmdUpdateTexture& update);
  void updateMeshBuffer(const CmdUpdateBuffer& update, bool indices);
  void writeBuffer(const CmdWriteBuffer& write);
  void clear(bool color,
             bool depth,
             bool stencil,
             bool mrtAware,
             const CmdClearColor& value,
             float depthValue);
  enum class DrawKind
  {
    Arrays,
    Indexed,
    ArraysInstanced,
    IndexedInstanced,
  };
  void draw(DrawKind kind,
            unsigned count,
            unsigned first,
            unsigned instances,
            const char* label);
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

  // Presentation and readback (VulkanDevicePresent.cpp).
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

  // OpenGL's context state, which persists across submissions.
  PipelineState m_state;
  CullMode m_cullFace = CullMode::Back;
  WindingOrder m_frontFace = WindingOrder::CounterClockwise;
  CmdViewport m_viewport{ 0, 0, 0, 0 };
  CmdScissor m_scissor{ false, 0, 0, 0, 0 };
  FramebufferHandle m_framebuffer{};
  std::array<TextureUnit, kTextureUnits> m_units{};
  std::array<BufferHandle, kUniformBindings> m_uniformBindings{};
  // GLDevice forgets these at every submission.
  ShaderHandle m_program{};
  MeshHandle m_mesh{};
  bool m_framebufferKnown = false;
  MeshHandle m_instanceMesh{};
  size_t m_instanceCapacity = 0;

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
  std::string m_frameError;
  VulkanFrameStats m_stats;

  std::vector<Entry<VulkanMesh>> m_meshes;
  std::vector<Entry<VulkanProgram>> m_programs;
  std::vector<Entry<VulkanTexture>> m_textures;
  std::vector<Entry<VulkanFramebuffer>> m_framebuffers;
  std::vector<Entry<VulkanBuffer>> m_buffers;
  ResourceHandlePool<MeshHandle> m_meshHandles;
  ResourceHandlePool<ShaderHandle> m_shaderHandles;
  ResourceHandlePool<TextureHandle> m_textureHandles;
  ResourceHandlePool<FramebufferHandle> m_framebufferHandles;
  ResourceHandlePool<BufferHandle> m_bufferHandles;

  std::unordered_map<uint32_t, VkSampler> m_samplers;
  VulkanTexture m_blackTexture;
  VulkanTexture m_blackCube;
  VulkanBufferMemory m_zeroUniforms;
  VulkanBufferMemory m_defaultAttributes;

  std::unordered_map<std::uint32_t, ReadbackStream> m_readbacks;
  uint64_t m_readbackOrder = 0;
};
