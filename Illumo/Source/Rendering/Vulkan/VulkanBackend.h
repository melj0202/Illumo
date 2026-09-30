#pragma once
#include "VulkanDevice.h"
#include <Illumo/Rendering/IBackend.h>
#include <array>
#include <cstdint>

class IRenderWindow;
class CommandQueue;

// Vulkan backend stub: declares the full IBackend contract without a Vulkan
// instance, device, or swapchain yet. Initialize() reports failure so a
// program selecting this backend fails its launch instead of running with a
// silently broken renderer (Illumo/Source/Rendering/AGENTS.md: "A program
// that fails to start fails the launch").
class VulkanBackend : public IBackend
{
public:
  explicit VulkanBackend(IRenderWindow* window);
  ~VulkanBackend() override;
  VulkanBackend(const VulkanBackend&) = delete;
  VulkanBackend& operator=(const VulkanBackend&) = delete;
  VulkanBackend(VulkanBackend&&) = delete;
  VulkanBackend& operator=(VulkanBackend&&) = delete;

  bool Initialize() override;
  void Shutdown() override;
  void BeginFrame() override;
  void EndFrame() override;
  void SubmitCommandQueue() override;
  void PushToCommandQueue(RenderCommand command) override;
  void ClearCommandQueue() override;
  size_t rejectedCommandCount() const override;
  size_t commandHighWaterMark() const override;
  int getFPS() const override { return fps; }

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

private:
  VulkanDevice* device;
  CommandQueue* commandQueue;
  IRenderWindow* window;
  int fps = 0;
};
