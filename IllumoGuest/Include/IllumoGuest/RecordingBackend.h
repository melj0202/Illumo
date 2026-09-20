#pragma once

#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <IllumoGuest/Frame.h>
#include <IllumoGuest/Services.h>
#include <map>

// Guest-only backend: consumes borrowed CPU tokens synchronously and records
// explicit wire values. It owns no native resources and executes no shaders.
class GuestRecordingBackend final : public IBackend
{
public:
  explicit GuestRecordingBackend(GuestServiceQueue& services,
                                 std::size_t commandCeiling = 65536);
  ~GuestRecordingBackend() override;
  GuestRecordingBackend(const GuestRecordingBackend&) = delete;
  GuestRecordingBackend& operator=(const GuestRecordingBackend&) = delete;
  GuestRecordingBackend(GuestRecordingBackend&&) = delete;
  GuestRecordingBackend& operator=(GuestRecordingBackend&&) = delete;
  void setRenderer(Renderer& renderer);
  void setFrame(float width, float height);
  void setLayer(GuestLayer layer);
  void pump();
  TextureHandle importTexture(GuestResourceId id);
  GuestFrame takeFrame();

  bool Initialize() override;
  void Shutdown() override;
  void BeginFrame() override;
  void EndFrame() override;
  void SubmitCommandQueue() override;
  void PushToCommandQueue(RenderCommand command) override;
  void ClearCommandQueue() override;
  std::size_t rejectedCommandCount() const override;
  std::size_t commandHighWaterMark() const override;
  std::string submissionError() const override;
  int getFPS() const override;
  MeshHandle CreateMesh(const void*,
                        std::size_t,
                        const void*,
                        std::size_t) override;
  MeshHandle CreateMesh(const void*,
                        std::size_t,
                        const void*,
                        std::size_t,
                        MeshVertexLayout,
                        bool) override;
  bool ReplaceMesh(MeshHandle,
                   const void*,
                   std::size_t,
                   const void*,
                   std::size_t,
                   MeshVertexLayout,
                   bool) override;
  bool DestroyMesh(MeshHandle) override;
  bool IsMeshValid(MeshHandle) const override;
  ShaderHandle CreateShaderProgram(const ShaderPaths&) override;
  ShaderHandle CreateShaderProgram(const ShaderSources&) override;
  bool ReplaceShaderProgram(ShaderHandle, const ShaderSources&) override;
  bool DestroyShaderProgram(ShaderHandle) override;
  bool IsShaderValid(ShaderHandle) const override;
  TextureHandle CreateTexture(const unsigned char*, int, int) override;
  TextureHandle CreateTexture(const unsigned char*,
                              int,
                              int,
                              int,
                              const TextureOptions&) override;
  TextureHandle CreateCubemap(const std::array<const unsigned char*, 6>&,
                              int,
                              int,
                              int) override;
  bool ReplaceTexture(TextureHandle,
                      const unsigned char*,
                      int,
                      int,
                      int,
                      const TextureOptions&) override;
  bool DestroyTexture(TextureHandle) override;
  bool IsTextureValid(TextureHandle) const override;
  TextureInfo GetTextureInfo(TextureHandle) const override;
  FramebufferHandle CreateFramebuffer(const FramebufferDesc&,
                                      FramebufferAttachments*) override;
  FramebufferHandle CreateDepthFramebuffer(int, int, TextureHandle*) override;
  bool DestroyFramebuffer(FramebufferHandle) override;
  bool IsFramebufferValid(FramebufferHandle) const override;

private:
  struct Mesh
  {
    MeshVertexLayout layout = MeshVertexLayout::Pos3Color4U8;
    std::vector<std::byte> vertices;
    std::vector<std::byte> indices;
  };
  struct Texture
  {
    TextureInfo info;
    GuestResourceId id;
    std::uint64_t pending = 0;
    std::vector<std::byte> pixels;
    TextureInfo pendingInfo;
    std::vector<std::byte> pendingPixels;
    bool changed = false;
  };
  void consume(const RenderCommand& command);
  void draw(std::uint32_t first, std::uint32_t count);
  void release(GuestResourceId id);
  GuestServiceQueue& m_services;
  Renderer* m_renderer = nullptr;
  ResourceHandlePool<MeshHandle> m_meshHandles;
  ResourceHandlePool<ShaderHandle> m_shaderHandles;
  ResourceHandlePool<TextureHandle> m_textureHandles;
  std::map<std::uint32_t, Mesh> m_meshes;
  std::map<std::uint32_t, Texture> m_textures;
  std::vector<std::uint64_t> m_abandoned;
  std::vector<std::uint64_t> m_releases;
  std::vector<GuestResourceId> m_retirements;
  std::size_t m_frameRejections = 0;
  CommandQueue m_commands;
  GuestFrame m_frame;
  GuestLayer m_layer = GuestLayer::Ui;
  MeshHandle m_mesh;
  ShaderHandle m_shader;
  TextureHandle m_texture;
  std::array<float, 16> m_mvp{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  CmdScissor m_clip{};
  PipelineState m_pipeline{};
  std::string m_error;
};
