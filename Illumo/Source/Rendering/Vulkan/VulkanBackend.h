#pragma once

#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/IBackend.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

class IRenderWindow;
class VulkanDevice;

// IBackend on Vulkan 1.3, with the OpenGL backend's observable behaviour
// (docs/vulkan-backend-plan.md). The device does the GPU work; this class is
// the token queue, statistics and shader preprocessing around it.
class VulkanBackend : public IBackend
{
public:
  // present: show frames in the window; otherwise render offscreen only
  // (capture windows and GPU tests).
  VulkanBackend(IRenderWindow* window, bool present);
  ~VulkanBackend() override;
  VulkanBackend(const VulkanBackend&) = delete;
  VulkanBackend& operator=(const VulkanBackend&) = delete;
  VulkanBackend(VulkanBackend&&) = delete;
  VulkanBackend& operator=(VulkanBackend&&) = delete;

  void setGpuCacheDirectory(const std::filesystem::path& directory) override;
  bool Initialize() override;
  void Shutdown() override;
  void BeginFrame() override;
  void EndFrame() override;
  void SubmitCommandQueue() override;
  void PushToCommandQueue(RenderCommand command) override;
  void ClearCommandQueue() override;
  size_t rejectedCommandCount() const override;
  size_t commandHighWaterMark() const override;
  std::string submissionError() const override;
  int getFPS() const override { return m_fps; }
  FrameReadback readBackbuffer(int width, int height) override;
  bool requestFramebufferReadback(std::uint32_t stream,
                                  FramebufferHandle framebuffer,
                                  int width,
                                  int height) override;
  bool takeFramebufferReadback(std::uint32_t stream,
                               bool wait,
                               FrameReadback& out) override;
  void releaseReadbackStream(std::uint32_t stream) override;

  MeshHandle CreateMesh(const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize) override;
  MeshHandle CreateMesh(const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize,
                        MeshVertexLayout layout,
                        bool dynamic) override;
  bool ReplaceMesh(MeshHandle handle,
                   const void* vertices,
                   size_t vertexSize,
                   const void* indices,
                   size_t indexSize,
                   MeshVertexLayout layout,
                   bool dynamic) override;
  bool DestroyMesh(MeshHandle handle) override;
  bool IsMeshValid(MeshHandle handle) const override;

  ShaderHandle CreateShaderProgram(const ShaderPaths& paths) override;
  ShaderHandle CreateShaderProgram(const ShaderSources& sources) override;
  bool ReplaceShaderProgram(ShaderHandle handle,
                            const ShaderSources& sources) override;
  bool DestroyShaderProgram(ShaderHandle handle) override;
  bool IsShaderValid(ShaderHandle handle) const override;

  TextureHandle CreateTexture(const unsigned char* data,
                              const int width,
                              const int height) override;
  TextureHandle CreateTexture(const unsigned char* data,
                              const int width,
                              const int height,
                              int channels,
                              const TextureOptions& options) override;
  TextureHandle CreateCubemap(
    const std::array<const unsigned char*, 6>& facesData,
    int width,
    int height,
    int channels = 3) override;
  bool ReplaceTexture(TextureHandle handle,
                      const unsigned char* data,
                      int width,
                      int height,
                      int channels,
                      const TextureOptions& options) override;
  bool ReplaceCubemap(TextureHandle handle,
                      const std::array<const unsigned char*, 6>& faces,
                      int width,
                      int height,
                      int channels) override;
  bool DestroyTexture(TextureHandle handle) override;
  bool IsTextureValid(TextureHandle handle) const override;
  TextureInfo GetTextureInfo(TextureHandle handle) const override;

  FramebufferHandle CreateFramebuffer(
    const FramebufferDesc& desc,
    FramebufferAttachments* outAttachments = nullptr) override;
  FramebufferHandle CreateDepthFramebuffer(
    int width,
    int height,
    TextureHandle* outDepthTexture) override;
  bool DestroyFramebuffer(FramebufferHandle handle) override;
  bool IsFramebufferValid(FramebufferHandle handle) const override;

  BufferHandle CreateBuffer(BufferUsage usage, size_t capacityBytes) override;
  bool DestroyBuffer(BufferHandle handle) override;
  bool IsBufferValid(BufferHandle handle) const override;

private:
  // Preprocesses like GLShaderProgram; false with the reason logged.
  bool preprocess(const ShaderSources& sources, ShaderSources* output) const;
  bool preprocess(const ShaderPaths& paths, ShaderSources* output) const;

  std::unique_ptr<VulkanDevice> m_device;
  std::unique_ptr<CommandQueue> m_commandQueue;
  IRenderWindow* m_window = nullptr;
  bool m_present = true;
  bool m_initialized = false;
  int m_fps = 0;
  size_t m_rejectionsAtFrameStart = 0;
  long m_frameCount = 0;
  std::chrono::steady_clock::time_point m_fpsStart;
};
