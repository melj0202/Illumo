#pragma once

#include "D3D12Common.h"
#include "D3D12Context.h"
#include "D3D12ShaderCompiler.h"
#include "Rendering/Gpu/GlslToSpirv.h"
#include "Rendering/Gpu/GpuProgramState.h"
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
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class IRenderWindow;
struct GLFWwindow;

// Work one frame's submissions did, for profiling plots (as GLFrameStats).
struct D3D12FrameStats
{
  size_t submits = 0;
  size_t commands = 0;
  size_t recordedLists = 0;
  size_t recordedCommands = 0;
  size_t drawCalls = 0;
  size_t uploadBytes = 0;
  size_t barriers = 0;
  size_t pipelinesCreated = 0;
};

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

struct D3D12Texture
{
  D3D12Image image;
  int channels = 4;
  bool cubemap = false;
  bool renderTarget = false;
  TextureFormat format = TextureFormat::RGBA8;
  // Bytes per stored texel that updates convert into; 0: not updatable.
  int storageBytes = 4;
  // Index into the shader-visible sampler heap.
  unsigned sampler = 0;
};

struct D3D12Mesh
{
  D3D12Memory vertices;
  D3D12Memory indices;
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

struct D3D12Buffer
{
  D3D12Memory memory;
  BufferUsage usage = BufferUsage::Instance;
  size_t capacity = 0;
};

struct D3D12Framebuffer
{
  std::vector<TextureHandle> colorTextures;
  TextureHandle depthTexture{};
  int width = 0;
  int height = 0;
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

// A linked program, with the uniform state an OpenGL program object keeps:
// values persist across draws and frames and start at zero.
struct D3D12Program
{
  GlslProgram reflection;
  std::vector<unsigned char> vertexCode;
  std::vector<unsigned char> pixelCode;
  // What the vertex code reads, by location (reflectVertexInputs).
  std::vector<GlslVertexInput> vertexInputs;
  D3D12Ref<ID3D12RootSignature> rootSignature;
  std::vector<D3D12RootBinding> rootLayout;
  GpuProgramUniforms uniforms;
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

// Executes the backend-neutral token stream with the OpenGL backend's
// observable semantics (docs/d3d12-backend-plan.md, section 5.3), and owns
// every GPU resource, the submissions and presentation.
class D3D12Device
{
public:
  D3D12Device() = default;
  ~D3D12Device();
  D3D12Device(const D3D12Device&) = delete;
  D3D12Device& operator=(const D3D12Device&) = delete;
  D3D12Device(D3D12Device&&) = delete;
  D3D12Device& operator=(D3D12Device&&) = delete;

  // present: show frames in the window; otherwise render offscreen only.
  bool initialize(IRenderWindow* window, bool present, std::string* error);
  void shutdown();

  void beginFrame();
  void endFrame();
  void executeQueue(CommandQueue& queue);
  void resetFrameError() { m_frameError.clear(); }
  const std::string& frameError() const { return m_frameError; }
  void resetFrameStats() { m_stats = D3D12FrameStats{}; }
  const D3D12FrameStats& frameStats() const { return m_stats; }

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

  struct TextureUnit
  {
    TextureHandle texture2D{};
    TextureHandle cube{};
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
  std::unique_ptr<D3D12Mesh> buildMesh(const void* vertices,
                                       size_t vertexSize,
                                       const void* indices,
                                       size_t indexSize,
                                       MeshVertexLayout layout,
                                       bool dynamic);
  std::unique_ptr<D3D12Program> buildProgram(const ShaderSources& sources,
                                             std::string* error);
  std::unique_ptr<D3D12Texture> buildTexture(const unsigned char* data,
                                             int width,
                                             int height,
                                             int channels,
                                             const TextureOptions& options);
  std::unique_ptr<D3D12Texture> buildCubemap(
    const std::array<const unsigned char*, 6>& faces,
    int width,
    int height,
    int channels);
  std::unique_ptr<D3D12Texture> buildRenderTarget(int width,
                                                  int height,
                                                  TextureFormat format,
                                                  TextureFilter filter,
                                                  TextureWrap wrap);
  void releaseMesh(D3D12Mesh& mesh);
  void releaseProgram(D3D12Program& program);
  void releaseTexture(D3D12Texture& texture);
  TextureHandle registerTexture(std::unique_ptr<D3D12Texture> texture);
  D3D12Mesh* resolveMesh(MeshHandle handle) const;
  D3D12Program* resolveProgram(ShaderHandle handle) const;
  D3D12Texture* resolveTexture(TextureHandle handle) const;
  D3D12Framebuffer* resolveFramebuffer(FramebufferHandle handle) const;
  D3D12Buffer* resolveBuffer(BufferHandle handle) const;

  // Tokens (D3D12DeviceCommands.cpp).
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
  std::string m_frameError;
  D3D12FrameStats m_stats;

  std::vector<Entry<D3D12Mesh>> m_meshes;
  std::vector<Entry<D3D12Program>> m_programs;
  std::vector<Entry<D3D12Texture>> m_textures;
  std::vector<Entry<D3D12Framebuffer>> m_framebuffers;
  std::vector<Entry<D3D12Buffer>> m_buffers;
  ResourceHandlePool<MeshHandle> m_meshHandles;
  ResourceHandlePool<ShaderHandle> m_shaderHandles;
  ResourceHandlePool<TextureHandle> m_textureHandles;
  ResourceHandlePool<FramebufferHandle> m_framebufferHandles;
  ResourceHandlePool<BufferHandle> m_bufferHandles;

  D3D12Texture m_blackTexture;
  D3D12Texture m_blackCube;
  D3D12Memory m_zeroUniforms;
  D3D12Memory m_defaultAttributes;

  std::unordered_map<std::uint32_t, ReadbackStream> m_readbacks;
  uint64_t m_readbackOrder = 0;
};
