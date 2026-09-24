#pragma once

#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <IllumoGuest/Frame.h>
#include <IllumoGuest/Services.h>
#include <map>
#include <set>

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
  // Applies to commands pushed afterwards, so one frame may append World
  // drawables before Ui drawables and submit them together.
  void setLayer(GuestLayer layer);
  // True while any texture acquisition or replacement awaits the host. A
  // replacement requested in that window supersedes the pending one.
  bool hasPendingTextures() const;
  void pump();
  TextureHandle importTexture(GuestResourceId id);
  GuestFrame takeFrame();
  // Frame schema v5: commands submitted between beginSurface and endSurface
  // record into a surface of that logical size instead of the main frame.
  // Surfaces take UI batches only; texture and mesh writes stay frame-level.
  void beginSurface(std::uint32_t surface, float width, float height);
  void endSurface();
  bool Initialize() override;
  void Shutdown() override;
  void BeginFrame() override;
  void EndFrame() override;
  void SubmitCommandQueue() override;
  void PushToCommandQueue(RenderCommand command) override;
  void ClearCommandQueue() override;
  // Scene World maps to GuestLayer::World; UI and Debug map to Ui.
  void BeginLayer(RenderLayerId layer) override;
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

  // Static meshes at least this large are uploaded once to a retained host
  // mesh (frame schema v3) instead of travelling inline every frame.
  static constexpr std::size_t RetainedMeshBytes = 64u * 1024u;
  // Diagnostics: bytes of dynamic mesh writes in the last recorded frame.
  std::size_t lastMeshWriteBytes() const { return m_lastMeshWriteBytes; }

private:
  struct Mesh
  {
    MeshVertexLayout layout = MeshVertexLayout::Pos3Color4U8;
    std::vector<std::byte> vertices;
    std::vector<std::byte> indices;
    // Retained upload state. Draws skip a static mesh until the host copy is
    // complete, and fall back to inline geometry if the upload fails.
    bool retain = false;
    GuestResourceId id;
    std::uint64_t create = 0;
    std::vector<std::uint64_t> writes;
    std::size_t vertexSent = 0;
    std::size_t indexSent = 0;
    bool ready = false;
    bool failed = false;
    // Dynamic retained meshes (frame schema v4) are ready once created and
    // are kept current by the frame's mesh writes: only the byte ranges
    // changed since the last frame travel. They draw inline until ready.
    bool dynamic = false;
    bool drawnThisFrame = false;
    std::size_t vertexDirtyBegin = 0;
    std::size_t vertexDirtyEnd = 0;
    std::size_t indexDirtyBegin = 0;
    std::size_t indexDirtyEnd = 0;
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
    // Virtual shadow depth target: never acquired from the host.
    bool depthOnly = false;
    // Sampled only by Skybox batches; never written after creation.
    bool cubemap = false;
  };
  void consume(const RenderCommand& command);
  void draw(std::uint32_t first, std::uint32_t count);
  // Copies indices [first, first + count) and the vertices they reference
  // into batch as inline geometry. The batch style must already be set.
  static void extractInline(const Mesh& mesh,
                            GuestBatch& batch,
                            std::uint32_t first,
                            std::uint32_t count);
  void release(GuestResourceId id);
  // Drops a mesh's host copy (after replacement, update or destruction).
  void forgetRetained(Mesh& mesh);
  // A dynamic mesh changed after it was drawn by reference this frame.
  // Converts those draws to inline geometry (still the pre-change contents)
  // and keeps the mesh inline from now on.
  void demoteDynamic(Mesh& mesh);
  static void markDirty(std::size_t& begin,
                        std::size_t& end,
                        std::size_t offset,
                        std::size_t size);
  void emitMeshWrites();
  void pumpMeshes();
  GuestServiceQueue& m_services;
  Renderer* m_renderer = nullptr;
  ResourceHandlePool<MeshHandle> m_meshHandles;
  ResourceHandlePool<ShaderHandle> m_shaderHandles;
  ResourceHandlePool<TextureHandle> m_textureHandles;
  // The Renderer's shared shadow pass targets one virtual framebuffer. Its
  // commands record only which meshes cast shadows; the host re-runs the
  // real pass from the frame's casters and world camera.
  ResourceHandlePool<FramebufferHandle> m_framebufferHandles;
  FramebufferHandle m_shadowFramebuffer;
  TextureHandle m_shadowDepth;
  bool m_shadowPass = false;
  std::set<std::uint32_t> m_shadowMeshes;
  GuestLighting m_lighting;
  std::map<std::uint32_t, Mesh> m_meshes;
  // Dynamic meshes drawn by reference in the current frame.
  std::vector<std::uint32_t> m_drawnDynamic;
  std::map<std::uint32_t, Texture> m_textures;
  std::vector<std::uint64_t> m_abandoned;
  std::vector<std::uint64_t> m_releases;
  std::vector<GuestResourceId> m_retirements;
  // Retained meshes: creations whose result must be released on arrival,
  // host copies to release, and requests whose results are only drained.
  std::vector<std::uint64_t> m_abandonedMeshes;
  std::vector<GuestResourceId> m_meshRetirements;
  std::vector<std::uint64_t> m_drained;
  std::size_t m_frameRejections = 0;
  CommandQueue m_commands;
  std::vector<GuestLayer> m_commandLayers;
  GuestFrame m_frame;
  // Index into m_frame.surfaces while recording a surface, else -1.
  std::ptrdiff_t m_surface = -1;
  std::vector<GuestBatch>& targetBatches();
  float targetWidth() const;
  float targetHeight() const;
  GuestLayer m_layer = GuestLayer::Ui;
  MeshHandle m_mesh;
  ShaderHandle m_shader;
  TextureHandle m_texture;
  std::array<float, 16> m_mvp{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  CmdScissor m_clip{};
  PipelineState m_pipeline{};
  std::string m_error;
  std::size_t m_lastMeshWriteBytes = 0;
};
