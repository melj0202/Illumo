#pragma once

#include "D3D12Common.h"
#include "D3D12Context.h"
#include "D3D12ShaderCompiler.h"
#include "Rendering/Gpu/GlslToSpirv.h"
#include "Rendering/Gpu/GpuDevice.h"
#include "Rendering/Gpu/GpuProgramState.h"
#include "Rendering/Gpu/GpuShaderCache.h"
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
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class IRenderWindow;
struct GLFWwindow;

// A GPU resource and the state the recording leaves it in.
struct D3D12Memory
{
  D3D12Ref<ID3D12Resource> resource;
  D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  size_t size = 0;
};

struct D3D12Image
{
  D3D12Memory memory;
  // The resource format (typeless for the backbuffer) and the formats its
  // views use; render targets name the view format in pipeline keys.
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t mipLevels = 1;
  uint32_t layers = 1;
  uint32_t samples = 1;
  bool depth = false;
  // Descriptor indices: shader-visible SRV, RTV, DSV; -1 when absent.
  int srv = -1;
  int rtv = -1;
  int dsv = -1;
};

struct D3D12Texture : GpuTexture
{
  D3D12Image image;
  // Index into the shader-visible sampler heap.
  unsigned sampler = 0;
};

struct D3D12Mesh : GpuMesh
{
  D3D12Memory vertices;
  D3D12Memory indices;
};

struct D3D12Buffer : GpuBuffer
{
  D3D12Memory memory;
};

static constexpr unsigned kD3D12MaxColorAttachments = 8;

// The sampler heap holds every sampler state a texture can have, at this
// index (32 entries).
unsigned
d3d12SamplerIndex(TextureFilter filter,
                  TextureWrap wrapX,
                  TextureWrap wrapY,
                  bool mipmaps,
                  bool depthBorder);

struct D3D12PipelineKey
{
  uint32_t colorFormats[kD3D12MaxColorAttachments];
  uint32_t depthFormat;
  uint8_t colorCount;
  uint8_t samples;
  uint8_t topology;
  uint8_t meshLayout;
  uint8_t instanceLayout;
  uint8_t wireframe;
  uint8_t blend;
  uint8_t blendSrc;
  uint8_t blendDst;
  uint8_t cull;
  uint8_t frontClockwise;
  uint8_t depthTest;
};

struct D3D12PipelineKeyHash
{
  size_t operator()(const D3D12PipelineKey& key) const;
};

struct D3D12PipelineKeyEqual
{
  bool operator()(const D3D12PipelineKey& left,
                  const D3D12PipelineKey& right) const;
};

struct D3D12Program : GpuProgram
{
  std::vector<unsigned char> vertexCode;
  std::vector<unsigned char> pixelCode;
  // What the vertex code reads, by location (reflectVertexInputs).
  std::vector<GlslVertexInput> vertexInputs;
  D3D12Ref<ID3D12RootSignature> rootSignature;
  std::vector<D3D12RootBinding> rootLayout;
  uint64_t blockSerial = 0;
  D3D12_GPU_VIRTUAL_ADDRESS blockAddress = 0;
  std::unordered_map<D3D12PipelineKey,
                     D3D12Ref<ID3D12PipelineState>,
                     D3D12PipelineKeyHash,
                     D3D12PipelineKeyEqual>
    pipelines;
};

// A descriptor heap handing out single descriptors by index.
class D3D12DescriptorHeap
{
public:
  bool create(ID3D12Device* device,
              D3D12_DESCRIPTOR_HEAP_TYPE type,
              unsigned capacity,
              bool shaderVisible);
  void destroy();
  // -1 when the heap is full.
  int allocate();
  void release(int index);
  D3D12_CPU_DESCRIPTOR_HANDLE cpu(int index) const;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu(int index) const;
  ID3D12DescriptorHeap* heap() const { return m_heap.get(); }

private:
  D3D12Ref<ID3D12DescriptorHeap> m_heap;
  unsigned m_increment = 0;
  unsigned m_capacity = 0;
  unsigned m_next = 0;
  std::vector<int> m_free;
};

// GpuDevice on Direct3D 12: the resources, recording, submissions and
// presentation behind the shared token interpreter, with the OpenGL backend's
// observable semantics (docs/d3d12-backend-plan.md, section 5.3).
class D3D12Device final : public GpuDevice
{
public:
  D3D12Device() = default;
  ~D3D12Device() override;
  D3D12Device(const D3D12Device&) = delete;
  D3D12Device& operator=(const D3D12Device&) = delete;
  D3D12Device(D3D12Device&&) = delete;
  D3D12Device& operator=(D3D12Device&&) = delete;

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
  static constexpr unsigned kRootParameters = 64;

  struct StagingChunk
  {
    D3D12Ref<ID3D12Resource> buffer;
    unsigned char* mapped = nullptr;
    size_t size = 0;
    size_t used = 0;
  };

  struct Slot
  {
    D3D12Ref<ID3D12CommandAllocator> allocator;
    uint64_t serial = 0;
    std::vector<StagingChunk> staging;
  };

  struct StagingSpan
  {
    ID3D12Resource* buffer = nullptr;
    size_t offset = 0;
    unsigned char* mapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS address = 0;
  };

  struct Retired
  {
    uint64_t serial = 0;
    D3D12Ref<ID3D12Resource> resource;
    D3D12Ref<ID3D12RootSignature> rootSignature;
    std::vector<D3D12Ref<ID3D12PipelineState>> pipelines;
    int srv = -1;
    int rtv = -1;
    int dsv = -1;
  };

  // The colour, depth and resolve images standing in for OpenGL's default
  // framebuffer, stored bottom row first as OpenGL stores it.
  struct Backbuffer
  {
    D3D12Image color;
    D3D12Image depth;
    D3D12Image resolve;
    int width = 0;
    int height = 0;
  };

  struct Swapchain
  {
    D3D12Ref<IDXGISwapChain3> swapchain;
    std::vector<D3D12Image> buffers;
    uint32_t width = 0;
    uint32_t height = 0;
  };

  struct RenderTarget
  {
    bool backbuffer = true;
    FramebufferHandle handle{};
    std::array<D3D12Image*, kD3D12MaxColorAttachments> colors{};
    unsigned colorCount = 0;
    D3D12Image* depth = nullptr;
    int width = 0;
    int height = 0;
    uint32_t samples = 1;
  };

  struct ReadbackSlot
  {
    D3D12Ref<ID3D12Resource> buffer;
    size_t size = 0;
    int width = 0;
    int height = 0;
    size_t rowBytes = 0;
    TextureFormat format = TextureFormat::RGBA8;
    uint64_t serial = 0;
    uint64_t order = 0;
    bool pending = false;
  };

  struct ReadbackStream
  {
    std::array<ReadbackSlot, 2> slots;
  };

  // Frames, submissions, memory and deferred destruction (D3D12Device.cpp).
  bool createSlots(std::string* error);
  void ensureRecording();
  void flush(bool present);
  bool waitForSerial(uint64_t serial, DWORD timeoutMilliseconds);
  bool isSerialComplete(uint64_t serial);
  void collectRetired();
  void retire(Retired retired);
  void retireImage(D3D12Image& image);
  void retireMemory(D3D12Memory& memory);
  bool allocateStaging(size_t size, size_t alignment, StagingSpan* span);
  void transition(D3D12Memory& memory, D3D12_RESOURCE_STATES state);
  bool createBufferMemory(D3D12Memory& memory, size_t size);
  bool createImage(D3D12Image& image,
                   DXGI_FORMAT format,
                   DXGI_FORMAT viewFormat,
                   uint32_t width,
                   uint32_t height,
                   uint32_t mipLevels,
                   uint32_t layers,
                   uint32_t samples,
                   D3D12_RESOURCE_FLAGS flags);
  bool createShaderView(D3D12Image& image,
                        DXGI_FORMAT format,
                        bool cube,
                        UINT componentMapping);
  bool createTargetView(D3D12Image& image);
  // Copies into the buffer in recording order; draws move it back to the
  // state they read it in.
  void uploadToBuffer(D3D12Memory& memory,
                      size_t offset,
                      const void* data,
                      size_t size);
  void uploadToImage(D3D12Image& image,
                     uint32_t subresource,
                     int x,
                     int y,
                     int width,
                     int height,
                     const unsigned char* texels,
                     size_t texelBytes,
                     size_t sourceRowBytes);
  bool createSamplers(std::string* error);
  bool createDefaults(std::string* error);

  // Resources (D3D12DeviceResources.cpp).
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
  // This device's resource behind a handle, or nullptr.
  D3D12Mesh* resolveMesh(MeshHandle handle) const
  {
    return static_cast<D3D12Mesh*>(findMesh(handle));
  }
  D3D12Program* resolveProgram(ShaderHandle handle) const
  {
    return static_cast<D3D12Program*>(findProgram(handle));
  }
  D3D12Texture* resolveTexture(TextureHandle handle) const
  {
    return static_cast<D3D12Texture*>(findTexture(handle));
  }
  D3D12Buffer* resolveBuffer(BufferHandle handle) const
  {
    return static_cast<D3D12Buffer*>(findBuffer(handle));
  }

  // Tokens (D3D12DeviceCommands.cpp).
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
  // Transitions the target's attachments and binds them unless bound.
  void bindTarget(const RenderTarget& target);
  ID3D12PipelineState* pipelineFor(D3D12Program& program,
                                   const RenderTarget& target,
                                   const D3D12Mesh& mesh,
                                   InstanceLayout instanceLayout);
  D3D12_RECT scissorRect(const RenderTarget& target) const;
  // The recorded command list no longer knows what it has bound.
  void forgetRecordedState();

  // Presentation and readback (D3D12DevicePresent.cpp).
  void framebufferSize(int* width, int* height) const;
  bool ensureBackbuffer();
  bool createSwapchain(int width, int height);
  bool resizeSwapchain(int width, int height);
  void releaseSwapchainBuffers();
  bool createPresentPipeline(std::string* error);
  // Records the flipped copy of the backbuffer into the swapchain's current
  // buffer; false when nothing can be shown (minimized, no swapchain, or the
  // session is locked).
  bool recordPresentation();
  // The backbuffer as a single-sample image: the colour image itself, or
  // the multisample resolve averaged in linear light.
  D3D12Image& resolvedBackbuffer();
  bool createReadbackBuffer(ReadbackSlot& slot, size_t size);

  D3D12Context m_context;
  IRenderWindow* m_window = nullptr;
  GLFWwindow* m_glfwWindow = nullptr;
  HWND m_hwnd = nullptr;
  bool m_present = false;
  bool m_initialized = false;
  bool m_compilerStarted = false;
  uint32_t m_samples = 1;

  D3D12Ref<ID3D12GraphicsCommandList1> m_commands;
  D3D12Ref<ID3D12Fence> m_fence;
  HANDLE m_fenceEvent = nullptr;
  std::array<Slot, kSlotCount> m_slots{};
  uint32_t m_slotIndex = 0;
  bool m_recording = false;
  uint64_t m_recordingSerial = 0;
  uint64_t m_lastSubmittedSerial = 0;
  uint64_t m_completedSerial = 0;
  std::vector<Retired> m_retired;

  D3D12DescriptorHeap m_shaderViews;
  D3D12DescriptorHeap m_samplerHeap;
  D3D12DescriptorHeap m_targetViews;
  D3D12DescriptorHeap m_depthViews;

  Backbuffer m_backbuffer;
  Swapchain m_swapchain;
  D3D12Ref<ID3D12RootSignature> m_presentRoot;
  D3D12Ref<ID3D12PipelineState> m_presentPipeline;
  bool m_presentationPaused = false;
  bool m_presentationPauseReported = false;
  // PlatformSessionLocked, polled at most every 250 ms.
  bool m_sessionLocked = false;
  std::chrono::steady_clock::time_point m_nextSessionCheck{};

  // What the command list being recorded already holds, so a draw records
  // only what changed. Reset with each recording and by presentation.
  struct RecordedState
  {
    bool targetKnown = false;
    bool targetBackbuffer = true;
    FramebufferHandle target{};
    bool samplePositions = false;
    ID3D12PipelineState* pipeline = nullptr;
    ID3D12RootSignature* rootSignature = nullptr;
    std::array<uint64_t, kRootParameters> rootValues{};
    bool dynamicKnown = false;
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bool vertexKnown = false;
    std::array<D3D12_VERTEX_BUFFER_VIEW, 3> vertexBuffers{};
    D3D12_GPU_VIRTUAL_ADDRESS indexBuffer = 0;
  };
  RecordedState m_recorded;
  // The last pipeline lookup; consecutive draws usually repeat it.
  const D3D12Program* m_lastPipelineProgram = nullptr;
  D3D12PipelineKey m_lastPipelineKey{};
  ID3D12PipelineState* m_lastPipeline = nullptr;

  D3D12Texture m_blackTexture;
  D3D12Texture m_blackCube;
  D3D12Memory m_zeroUniforms;
  D3D12Memory m_defaultAttributes;

  std::unordered_map<std::uint32_t, ReadbackStream> m_readbacks;
  uint64_t m_readbackOrder = 0;
};
