#pragma once

#include "Rendering/Gpu/GpuShaderCache.h"
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/FrameReadback.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

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

// The device of an explicit graphics API (Vulkan, Direct3D 12) behind
// GpuBackend: executes the backend-neutral token stream with the OpenGL
// backend's observable semantics and owns every GPU resource, the
// submissions and presentation (docs/gpu-backend-unification-plan.md).
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
  virtual void shutdown() = 0;

  virtual void beginFrame() = 0;
  virtual void endFrame() = 0;
  virtual void executeQueue(CommandQueue& queue) = 0;
  void resetFrameError() { m_frameError.clear(); }
  const std::string& frameError() const { return m_frameError; }
  void resetFrameStats() { m_stats = GpuFrameStats{}; }
  const GpuFrameStats& frameStats() const { return m_stats; }
  // Plots this frame's statistics under the API's profiler names.
  virtual void plotFrameStats(size_t commandQueueHighWater) const = 0;

  virtual MeshHandle createMesh(const void* vertices,
                                size_t vertexSize,
                                const void* indices,
                                size_t indexSize,
                                MeshVertexLayout layout,
                                bool dynamic) = 0;
  virtual bool replaceMesh(MeshHandle handle,
                           const void* vertices,
                           size_t vertexSize,
                           const void* indices,
                           size_t indexSize,
                           MeshVertexLayout layout,
                           bool dynamic) = 0;
  virtual bool destroyMesh(MeshHandle handle) = 0;
  virtual bool isMeshValid(MeshHandle handle) const = 0;

  virtual ShaderHandle createShader(const ShaderSources& sources,
                                    std::string* error) = 0;
  virtual bool replaceShader(ShaderHandle handle,
                             const ShaderSources& sources,
                             std::string* error) = 0;
  virtual bool destroyShader(ShaderHandle handle) = 0;
  virtual bool isShaderValid(ShaderHandle handle) const = 0;

  virtual TextureHandle createTexture(const unsigned char* data,
                                      int width,
                                      int height,
                                      int channels,
                                      const TextureOptions& options) = 0;
  virtual TextureHandle createCubemap(
    const std::array<const unsigned char*, 6>& faces,
    int width,
    int height,
    int channels) = 0;
  virtual bool replaceTexture(TextureHandle handle,
                              const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options) = 0;
  virtual bool replaceCubemap(TextureHandle handle,
                              const std::array<const unsigned char*, 6>& faces,
                              int width,
                              int height,
                              int channels) = 0;
  virtual bool destroyTexture(TextureHandle handle) = 0;
  virtual bool isTextureValid(TextureHandle handle) const = 0;
  virtual TextureInfo textureInfo(TextureHandle handle) const = 0;

  virtual FramebufferHandle createFramebuffer(
    const FramebufferDesc& desc,
    FramebufferAttachments* outAttachments) = 0;
  virtual bool destroyFramebuffer(FramebufferHandle handle) = 0;
  virtual bool isFramebufferValid(FramebufferHandle handle) const = 0;

  virtual BufferHandle createBuffer(BufferUsage usage,
                                    size_t capacityBytes) = 0;
  virtual bool destroyBuffer(BufferHandle handle) = 0;
  virtual bool isBufferValid(BufferHandle handle) const = 0;

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
  GpuShaderCache m_shaderCache;
  std::string m_frameError;
  GpuFrameStats m_stats;
};
