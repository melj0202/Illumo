#pragma once
#include "GLDevice.h"
#include "GLMesh.h"
#include "GLShaderProgram.h"
#include "GLTexture.h"
#include <GL/glew.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <memory>
#include <unordered_map>

class GLBackend : public IBackend
{
private:
  GLDevice* device;
  CommandQueue* commandQueue;
  IRenderWindow* window;
  int fps = 0;

  std::unordered_map<uint32_t, GLMeshResourceEntry> _vaoRegistryLookup;
  std::unordered_map<uint32_t, GLShaderResourceEntry> _programRegistryLookup;
  std::unordered_map<uint32_t, GLTextureResourceEntry> _textureRegistryLookup;
  std::unordered_map<uint32_t, GLFramebufferResourceEntry>
    _framebufferRegistryLookup;
  ResourceHandlePool<MeshHandle> meshHandles;
  ResourceHandlePool<ShaderHandle> shaderHandles;
  ResourceHandlePool<TextureHandle> textureHandles;
  ResourceHandlePool<FramebufferHandle> framebufferHandles;

public:
  GLBackend(IRenderWindow* window);
  ~GLBackend();

  bool Initialize() override;
  void Shutdown() override;
  void BeginFrame() override;
  void EndFrame() override;
  void SubmitCommandQueue() override;
  void PushToCommandQueue(RenderCommand command) override;
  void ClearCommandQueue() override;
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
  bool ReplaceTexture(TextureHandle handle,
                      const unsigned char* data,
                      int width,
                      int height,
                      int channels,
                      const TextureOptions& options) override;
  bool DestroyTexture(TextureHandle handle) override;
  bool IsTextureValid(TextureHandle handle) const override;
  TextureInfo GetTextureInfo(TextureHandle handle) const override;

  FramebufferHandle CreateDepthFramebuffer(
    int width,
    int height,
    TextureHandle* outDepthTexture) override;
  bool DestroyFramebuffer(FramebufferHandle handle) override;
  bool IsFramebufferValid(FramebufferHandle handle) const override;
};
