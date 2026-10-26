#pragma once

#include "Rendering/Gpu/GlslToSpirv.h"
#include "Rendering/Gpu/GpuProgramState.h"
#include "Rendering/Gpu/GpuShaderCache.h"
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/FrameReadback.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class IRenderWindow;

// Work one frame's submissions did, for profiling plots (as GLFrameStats).
struct GpuFrameStats
{
  size_t submits = 0;
  size_t commands = 0;
  size_t recordedLists = 0;
  size_t recordedCommands = 0;
  size_t drawCalls = 0;
  size_t uploadBytes = 0;
  size_t pipelinesCreated = 0;
  // Vulkan: rendering ended inline for an ordered upload.
  size_t renderPassBreaks = 0;
  // Direct3D 12: resource-state transitions recorded.
  size_t barriers = 0;
};

// The records GpuDevice keeps for each resource. Each API's resource derives
// from its record and adds the API objects; a device registers only its own
// types. Copies are protected so a record is never sliced through its base.
struct GpuMesh
{
  virtual ~GpuMesh() = default;

  MeshVertexLayout layout = MeshVertexLayout::Pos3Color3Uv2;
  bool dynamic = false;
  size_t vertexCapacity = 0;
  size_t indexCapacity = 0;
  unsigned indexCount = 0;
  // The instance stream SetInstanceStream attached, kept as a VAO keeps it.
  BufferHandle instanceBuffer{};
  unsigned instanceOffset = 0;
  InstanceLayout instanceLayout = InstanceLayout::None;

protected:
  GpuMesh() = default;
  GpuMesh(const GpuMesh&) = default;
  GpuMesh& operator=(const GpuMesh&) = default;
  GpuMesh(GpuMesh&&) = default;
  GpuMesh& operator=(GpuMesh&&) = default;
};

// A linked program, with the uniform state an OpenGL program object keeps:
// values persist across draws and frames and start at zero.
struct GpuProgram
{
  virtual ~GpuProgram() = default;

  GlslProgram reflection;
  GpuProgramUniforms uniforms;

protected:
  GpuProgram() = default;
  GpuProgram(const GpuProgram&) = default;
  GpuProgram& operator=(const GpuProgram&) = default;
  GpuProgram(GpuProgram&&) = default;
  GpuProgram& operator=(GpuProgram&&) = default;
};

struct GpuTexture
{
  virtual ~GpuTexture() = default;

  // Set by GpuDevice when it registers the texture.
  int width = 0;
  int height = 0;
  int channels = 4;
  bool cubemap = false;
  bool renderTarget = false;
  TextureFormat format = TextureFormat::RGBA8;
  // Bytes per stored texel that updates convert into; 0: not updatable.
  int storageBytes = 4;

protected:
  GpuTexture() = default;
  GpuTexture(const GpuTexture&) = default;
  GpuTexture& operator=(const GpuTexture&) = default;
  GpuTexture(GpuTexture&&) = default;
  GpuTexture& operator=(GpuTexture&&) = default;
};

struct GpuBuffer
{
  virtual ~GpuBuffer() = default;

  BufferUsage usage = BufferUsage::Instance;
  size_t capacity = 0;

protected:
  GpuBuffer() = default;
  GpuBuffer(const GpuBuffer&) = default;
  GpuBuffer& operator=(const GpuBuffer&) = default;
  GpuBuffer(GpuBuffer&&) = default;
  GpuBuffer& operator=(GpuBuffer&&) = default;
};

// A framebuffer is its attachment textures; every API keeps it the same way.
struct GpuFramebuffer
{
  std::vector<TextureHandle> colorTextures;
  TextureHandle depthTexture{};
  int width = 0;
  int height = 0;
};

// The device of an explicit graphics API (Vulkan, Direct3D 12) behind
// GpuBackend (docs/gpu-backend-unification-plan.md). This class executes the
// backend-neutral token stream with the OpenGL backend's observable semantics:
// it keeps OpenGL's context state, the handle registries, and every check a
// token or resource call makes, in the same order for every API. A derived
// device implements the protected hooks, which the base calls only with
// validated arguments, and owns the submissions and presentation.
class GpuDevice
{
public:
  GpuDevice() = default;
  virtual ~GpuDevice() = default;
  GpuDevice(const GpuDevice&) = delete;
  GpuDevice& operator=(const GpuDevice&) = delete;
  GpuDevice(GpuDevice&&) = delete;
  GpuDevice& operator=(GpuDevice&&) = delete;

  // The API's name in messages, such as "Vulkan" or "Direct3D 12".
  virtual const char* apiName() const = 0;

  // Compiled shaders (and pipeline data) persist here between runs
  // (D-R38); call before initialize. Empty keeps none.
  void setCacheDirectory(const std::filesystem::path& directory)
  {
    m_shaderCache.setDirectory(directory);
  }
  // present: show frames in the window; otherwise render offscreen only.
  virtual bool initialize(IRenderWindow* window,
                          bool present,
                          std::string* error) = 0;
  // Releases everything; the device may be initialized again. A derived
  // destructor calls it, and it calls releaseResources.
  virtual void shutdown() = 0;

  virtual void beginFrame() = 0;
  virtual void endFrame() = 0;
  void executeQueue(CommandQueue& queue);
  void resetFrameError() { m_frameError.clear(); }
  const std::string& frameError() const { return m_frameError; }
  void resetFrameStats() { m_stats = GpuFrameStats{}; }
  const GpuFrameStats& frameStats() const { return m_stats; }
  // Plots this frame's statistics under the API's profiler names.
  virtual void plotFrameStats(size_t commandQueueHighWater) const = 0;

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
  virtual FrameReadback readBackbuffer(int width, int height) = 0;
  virtual bool requestFramebufferReadback(std::uint32_t stream,
                                          FramebufferHandle framebuffer,
                                          int width,
                                          int height) = 0;
  virtual bool takeFramebufferReadback(std::uint32_t stream,
                                       bool wait,
                                       FrameReadback& out) = 0;
  virtual void releaseReadbackStream(std::uint32_t stream) = 0;

protected:
  static constexpr unsigned kTextureUnits = 32;
  static constexpr unsigned kUniformBindings = 36;

  struct TextureUnit
  {
    TextureHandle texture2D{};
    TextureHandle cube{};
  };

  enum class DrawKind
  {
    Arrays,
    Indexed,
    ArraysInstanced,
    IndexedInstanced,
  };

  // Resource hooks (GpuDevice.cpp calls them). A build hook returns nullptr
  // when the API cannot create the resource; the front end has checked
  // handles and the arguments its OpenGL counterpart checks. A release hook
  // retires the API objects once the GPU is done with them.
  //
  // buildMesh creates the API's mesh with a vertex buffer of vertexSize bytes
  // and, when indexSize is not zero, an index buffer of indexSize bytes;
  // GpuDevice fills the record and uploads through uploadMeshData.
  virtual std::unique_ptr<GpuMesh> buildMesh(size_t vertexSize,
                                             size_t indexSize) = 0;
  virtual void releaseMesh(GpuMesh& mesh) = 0;
  virtual std::unique_ptr<GpuProgram> buildProgram(const ShaderSources& sources,
                                                   std::string* error) = 0;
  virtual void releaseProgram(GpuProgram& program) = 0;
  virtual std::unique_ptr<GpuTexture> buildTexture(
    const unsigned char* data,
    int width,
    int height,
    int channels,
    const TextureOptions& options) = 0;
  virtual std::unique_ptr<GpuTexture> buildCubemap(
    const std::array<const unsigned char*, 6>& faces,
    int width,
    int height,
    int channels) = 0;
  virtual std::unique_ptr<GpuTexture> buildRenderTarget(int width,
                                                        int height,
                                                        TextureFormat format,
                                                        TextureFilter filter,
                                                        TextureWrap wrap) = 0;
  virtual void releaseTexture(GpuTexture& texture) = 0;
  // capacityBytes is not zero; usage and capacity are set by the caller.
  virtual std::unique_ptr<GpuBuffer> buildBuffer(size_t capacityBytes) = 0;
  virtual void releaseBuffer(GpuBuffer& buffer) = 0;
  // The most colour attachments a framebuffer may have.
  virtual unsigned maxColorAttachments() const = 0;
  // The framebuffer is about to be destroyed; stop drawing into it.
  virtual void releaseFramebufferTarget(FramebufferHandle handle) = 0;

  // Token hooks, called after the interpreter validated the token.
  // glClear on the bound target; honours the scissor box.
  virtual void clear(bool color,
                     bool depth,
                     bool stencil,
                     bool mrtAware,
                     const CmdClearColor& value,
                     float depthValue) = 0;
  // A draw with the bound program and mesh, a non-zero count and instance
  // count, a non-empty viewport and its vertex or index range in bounds.
  virtual void recordDraw(DrawKind kind,
                          unsigned count,
                          unsigned first,
                          unsigned instances,
                          GpuProgram& program,
                          GpuMesh& mesh) = 0;
  // Converts and copies a validated rectangle; false when it reported a
  // frame error and copied nothing.
  virtual bool uploadTexture(GpuTexture& texture,
                             const CmdUpdateTexture& update,
                             int channels) = 0;
  // Copies a validated range into the mesh's vertex or index buffer.
  virtual void uploadMeshData(GpuMesh& mesh,
                              bool indices,
                              size_t offset,
                              const void* data,
                              size_t size) = 0;
  // Copies a validated range into the buffer.
  virtual void uploadBufferData(GpuBuffer& buffer,
                                size_t offset,
                                const void* data,
                                size_t size) = 0;

  // Records the frame's first error and logs every one.
  void reportFrameError(const char* message);
  // Releases every registered resource through the hooks and forgets every
  // handle; shutdown calls it while the API device is still alive.
  void releaseResources();

  GpuMesh* findMesh(MeshHandle handle) const;
  GpuProgram* findProgram(ShaderHandle handle) const;
  GpuTexture* findTexture(TextureHandle handle) const;
  GpuFramebuffer* resolveFramebuffer(FramebufferHandle handle) const;
  GpuBuffer* findBuffer(BufferHandle handle) const;

  GpuShaderCache m_shaderCache;

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

  std::string m_frameError;
  GpuFrameStats m_stats;

private:
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

  // A mesh as GLMesh enrolls it, or nullptr.
  std::unique_ptr<GpuMesh> makeMesh(const void* vertices,
                                    size_t vertexSize,
                                    const void* indices,
                                    size_t indexSize,
                                    MeshVertexLayout layout,
                                    bool dynamic);
  TextureHandle registerTexture(std::unique_ptr<GpuTexture> texture,
                                int width,
                                int height);

  // The token interpreter (GpuDeviceCommands.cpp).
  void executeCommand(const RenderCommand& command);
  void executeList(const RecordedCommandList* list);
  void setUniform(const char* name,
                  const UniformKey& key,
                  GlslValueType given,
                  const void* value,
                  size_t bytes);
  void updateTexture(const CmdUpdateTexture& update);
  void updateMeshBuffer(const CmdUpdateBuffer& update, bool indices);
  void writeBuffer(const CmdWriteBuffer& write);
  void draw(DrawKind kind,
            unsigned count,
            unsigned first,
            unsigned instances,
            const char* label);

  std::vector<Entry<GpuMesh>> m_meshes;
  std::vector<Entry<GpuProgram>> m_programs;
  std::vector<Entry<GpuTexture>> m_textures;
  std::vector<Entry<GpuFramebuffer>> m_framebuffers;
  std::vector<Entry<GpuBuffer>> m_buffers;
  ResourceHandlePool<MeshHandle> m_meshHandles;
  ResourceHandlePool<ShaderHandle> m_shaderHandles;
  ResourceHandlePool<TextureHandle> m_textureHandles;
  ResourceHandlePool<FramebufferHandle> m_framebufferHandles;
  ResourceHandlePool<BufferHandle> m_bufferHandles;
};
