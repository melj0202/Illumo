#pragma once
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <string>

class IBackend
{
public:
  IBackend() = default;
  virtual ~IBackend() = default;

  virtual bool Initialize() = 0;
  virtual void Shutdown() = 0;
  virtual void BeginFrame() = 0;
  virtual void EndFrame() = 0;
  virtual void SubmitCommandQueue() = 0;
  virtual void PushToCommandQueue(RenderCommand command) = 0;
  virtual void ClearCommandQueue() = 0;
  virtual int getFPS() const = 0;

  virtual MeshHandle CreateMesh(const void* vertices,
                                size_t vertexSize,
                                const void* indices,
                                size_t indexSize) = 0;
  // layout + dynamic: when dynamic, vertexSize is VBO capacity and vertices may
  // be null.
  virtual MeshHandle CreateMesh(const void* vertices,
                                size_t vertexSize,
                                const void* indices,
                                size_t indexSize,
                                MeshVertexLayout layout,
                                bool dynamic) = 0;
  virtual bool ReplaceMesh(MeshHandle handle,
                           const void* vertices,
                           size_t vertexSize,
                           const void* indices,
                           size_t indexSize,
                           MeshVertexLayout layout,
                           bool dynamic) = 0;
  virtual bool DestroyMesh(MeshHandle handle) = 0;
  virtual bool IsMeshValid(MeshHandle handle) const = 0;

  virtual ShaderHandle CreateShaderProgram(const ShaderPaths& paths) = 0;
  virtual ShaderHandle CreateShaderProgram(const ShaderSources& sources) = 0;
  virtual bool ReplaceShaderProgram(ShaderHandle handle,
                                    const ShaderSources& sources) = 0;
  virtual bool DestroyShaderProgram(ShaderHandle handle) = 0;
  virtual bool IsShaderValid(ShaderHandle handle) const = 0;

  virtual TextureHandle CreateTexture(const unsigned char* data,
                                      const int width,
                                      const int height) = 0;
  // channels: 1 = R8, 3 = RGB, 4 = RGBA
  virtual TextureHandle CreateTexture(const unsigned char* data,
                                      const int width,
                                      const int height,
                                      int channels,
                                      const TextureOptions& options) = 0;
  virtual bool ReplaceTexture(TextureHandle handle,
                              const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options) = 0;
  virtual bool DestroyTexture(TextureHandle handle) = 0;
  virtual bool IsTextureValid(TextureHandle handle) const = 0;
  virtual TextureInfo GetTextureInfo(TextureHandle handle) const = 0;

  // Framebuffer / depth target for shadow mapping & offscreen passes
  virtual FramebufferHandle CreateDepthFramebuffer(
    int width,
    int height,
    TextureHandle* outDepthTexture) = 0;
  virtual bool DestroyFramebuffer(FramebufferHandle handle) = 0;
  virtual bool IsFramebufferValid(FramebufferHandle handle) const = 0;
};
