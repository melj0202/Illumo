#pragma once
#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/RenderPass.h>
#include <Illumo/Rendering/RenderStyle.h>
#include <Illumo/Rendering/RenderTargetPool.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Camera;
class DrawableBase;
class IRenderWindow;
class IEnvVars;
class DrawList;

class Renderer
{
public:
  // Stable values captured once for the active RenderScene call. Drawables may
  // use this only while active is true; direct token tests keep their existing
  // local camera/window fallback.
  struct FrameContext
  {
    std::array<int, 2> windowDimensions{ 1280, 720 };
    std::array<float, 16> worldMvp{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                    0.0f, 0.0f, 0.0f, 1.0f };
    Camera* worldCamera = nullptr;
    uint64_t frameSerial = 0;
    float uiScale = 1.0f;
    bool active = false;
    bool hasWorldMvp = false;
    bool sceneSnapshotExtraction = true;
  };

  struct ShadowCasterDesc
  {
    std::array<float, 3> boundsMin{ 0.0f, 0.0f, 0.0f };
    std::array<float, 3> boundsMax{ 0.0f, 0.0f, 0.0f };
    std::array<float, 3> lightDirection{ 0.5f, 1.0f, 0.3f };
    int mapSize = 1024;
    float minimumRadius = 2.5f;
    float lightDistance = 8.0f;
    float casterDistance = 100.0f;
  };

  // Mirrors the std140 `FrameUniforms` block read by instanced styles.
  struct FrameUniforms
  {
    std::array<float, 16> viewProjection{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                          0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                          0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 16> previousViewProjection{ 1.0f, 0.0f, 0.0f, 0.0f,
                                                  0.0f, 1.0f, 0.0f, 0.0f,
                                                  0.0f, 0.0f, 1.0f, 0.0f,
                                                  0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 16> lightSpace{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                      0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                      0.0f, 0.0f, 0.0f, 1.0f };
    // x: 1 when this frame's shared shadow map is active.
    std::array<float, 4> shadowState{ 0.0f, 0.0f, 0.0f, 0.0f };
  };

  struct ShadowFrameContext
  {
    std::array<float, 16> lightSpaceMatrix{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> lightDirection{ 0.5f, 1.0f, 0.3f };
    TextureHandle depthTexture{};
    bool active = false;
  };

private:
  std::shared_ptr<const void> _lifetimeIdentity =
    std::make_shared<const unsigned char>(0);
  // Owned when constructed with unique_ptr or takeOwnership=true; null when the
  // composition root or test fixture retains ownership of the backend.
  std::unique_ptr<IBackend> _ownedBackend;
  IBackend* _backend;
  IRenderWindow* _window;
  Camera* _camera;
  IEnvVars* envVars;
  DrawList* currentScene;
  struct RenderStyleEntry
  {
    uint32_t generation = 0;
    RenderStyle style;
  };
  std::unordered_map<uint32_t, RenderStyleEntry> styleRegistry;
  ResourceHandlePool<RenderStyleHandle> styleHandles;
  std::array<RenderStyleHandle, static_cast<size_t>(RenderStyleId::Count)>
    builtinStyleHandles{};
  bool _builtinStylesReady = false;
  TextureHandle _whiteTextureHandle{};

  // Proof-quad resources.
  bool _proofReady = false;
  MeshHandle _proofMeshHandle{};
  ShaderHandle _proofShaderHandle{};
  TextureHandle _proofTextureHandle{};

  // Pass pipeline resources
  RenderTargetPool _renderTargetPool;
  bool _fullscreenQuadReady = false;
  MeshHandle _fullscreenQuadMeshHandle{};
  FramebufferHandle _currentPassFbo{};
  std::array<int, 4> _currentPassViewport{ 0, 0, 0, 0 };

  struct ScissorState
  {
    bool enabled = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };
  ScissorState currentScissorState;
  std::vector<ScissorState> scissorStateStack;

  // Drawables that fell back to immediate Draw() this frame, drawn after
  // token submission. Retained so steady frames reuse its capacity; unlike
  // the former fixed 8 KiB arena it holds any number of drawables.
  std::vector<DrawableBase*> immediateDrawables;
  static constexpr size_t UNIFORM_MATRICES_PER_CHUNK = 128;
  static constexpr size_t MAX_UNIFORM_MATRICES = 65536;
  using UniformMatrix = std::array<float, 16>;
  using UniformMatrixChunk =
    std::array<UniformMatrix, UNIFORM_MATRICES_PER_CHUNK>;
  std::vector<std::unique_ptr<UniformMatrixChunk>> uniformMatrixChunks;
  size_t uniformMatrixCount = 0;
  FrameContext frameContext;
  uint64_t frameSerial = 0;
  bool m_strictSubmission = false;
  std::string m_frameError;
  size_t m_frameRejectedBaseline = 0;
  std::function<void(Renderer&)> m_beforePresent;
  bool checkCommandRejections();

  FramebufferHandle shadowFramebuffer{};
  TextureHandle shadowDepthTexture{};
  int enrolledShadowMapSize = 0;
  bool shadowBoundsValid = false;
  std::array<float, 3> shadowBoundsMin{ 0.0f, 0.0f, 0.0f };
  std::array<float, 3> shadowBoundsMax{ 0.0f, 0.0f, 0.0f };
  std::array<float, 3> requestedShadowLightDirection{ 0.5f, 1.0f, 0.3f };
  int requestedShadowMapSize = 0;
  float requestedShadowMinimumRadius = 0.0f;
  float requestedShadowLightDistance = 0.0f;
  struct BoundsFrustum
  {
    std::array<Vector4, 6> planes{};
    std::array<Vector3, 8> corners{};
    AxisAlignedBounds3 worldBounds{};
    bool valid = false;
  };
  BoundsFrustum cameraFrustum;
  BoundsFrustum shadowFrustum;
  BoundsFrustum shadowCasterFrustum;
  AxisAlignedBounds3 shadowCasterVolume{};
  bool shadowCasterVolumeValid = false;
  // The culling state the revisions last described.
  static bool sameFrustum(const BoundsFrustum& left,
                          const BoundsFrustum& right);
  mutable uint64_t m_cullRevision = 1;
  mutable bool m_cullActive = false;
  mutable BoundsFrustum m_cullFrustum;
  mutable uint64_t m_shadowCullRevision = 1;
  mutable bool m_shadowCullActive = false;
  mutable bool m_shadowCullVolumeValid = false;
  mutable AxisAlignedBounds3 m_shadowCullVolume{};
  mutable BoundsFrustum m_shadowCullCasterFrustum;
  mutable BoundsFrustum m_shadowCullFrustum;
  std::vector<ShadowCasterDesc> shadowCasters;
  ShadowFrameContext shadowFrameContext;
  std::array<float, 16> m_nextWorldViewProjection{};
  bool m_hasNextWorldViewProjection = false;
  // The frame block is created on first use and written at most once per
  // RenderScene; its storage stays unchanged until the next frame's write.
  BufferHandle m_frameUniformBuffer{};
  bool m_frameUniformBufferAttempted = false;
  FrameUniforms m_frameUniforms;
  uint64_t m_frameUniformsSerial = 0;
  std::array<float, 16> m_viewProjection{};
  std::array<float, 16> m_previousViewProjection{};
  bool m_hasViewProjection = false;
  RecordedCommandList* m_recording = nullptr;

public:
  // Recorded lists executed this frame, next to the queue's own metrics.
  struct RecordedListStats
  {
    size_t lists = 0;
    size_t tokens = 0;
  };

private:
  RecordedListStats m_recordedStats;

  void beginFrameContext(Camera* camera);
  void endFrameContext();
  void resetShadowFrame();
  bool prepareShadowPass();
  static bool buildBoundsFrustum(const Matrix4& matrix, BoundsFrustum* frustum);
  static bool boundsIntersectFrustum(const AxisAlignedBounds3& bounds,
                                     const BoundsFrustum& frustum);
  void ensureShadowResources(int mapSize);
  void releaseShadowResources();
  const float* retainUniformMatrix(const float* value);
  void clearCommandQueue();
  // Every push helper emits through these: into the frame queue, or into the
  // list being recorded.
  void emitCommand(const RenderCommand& command);
  bool emitCheckedCommand(const RenderCommand& command);

public:
  // Composition-root path: ownership transferred via unique_ptr (D-R11).
  Renderer(IRenderWindow* window,
           IEnvVars* envVars,
           Camera* cam,
           std::unique_ptr<IBackend> backend);

  // Backend-neutral inject: production uses CreateOpenGLBackend +
  // takeOwnership=true; tests inject stack MockBackend with
  // takeOwnership=false.
  Renderer(IRenderWindow* window,
           IEnvVars* envVars,
           Camera* cam,
           IBackend* backend,
           bool takeOwnership);

  ~Renderer();

  // Opaque non-owning cache identity; distinct even when an address is reused.
  std::weak_ptr<const void> getLifetimeIdentity() const
  {
    return _lifetimeIdentity;
  }

  IBackend* getBackend() { return _backend; }
  const IBackend* getBackend() const { return _backend; }
  bool ownsBackend() const { return _ownedBackend != nullptr; }
  IRenderWindow* getWindow() { return _window; }
  Camera* getCamera() { return _camera; }
  const FrameContext& getFrameContext() const { return frameContext; }
  const ShadowFrameContext& getShadowFrameContext() const
  {
    return shadowFrameContext;
  }
  float getUiScale() const;

  void registerShadowCaster(const ShadowCasterDesc& caster);
  // Casters registered for the current RenderScene call.
  const std::vector<ShadowCasterDesc>& getShadowCasters() const
  {
    return shadowCasters;
  }
  // The next RenderScene uses this world view-projection for its frame
  // context and shadow fitting instead of the camera's. Hosts presenting a
  // world authored elsewhere (a WASM guest) supply the author's camera.
  void setNextWorldViewProjection(const std::array<float, 16>& matrix);
  bool isWorldBoundsVisible(const AxisAlignedBounds3& bounds) const;
  bool isShadowCasterRelevant(const AxisAlignedBounds3& bounds) const;
  // Change whenever the answers of isWorldBoundsVisible and
  // isShadowCasterRelevant may change (camera, shadow fit, frame state), so a
  // caller may keep a culling result while its revision holds.
  uint64_t getCullRevision() const;
  uint64_t getShadowCullRevision() const;

  // =========================================================================
  // Asset enrollment (not mixed into the per-frame token stream — D-007)
  // =========================================================================

  ShaderHandle enrollShader(const ShaderPaths& paths);
  ShaderHandle enrollShader(const ShaderSources& sources);

  MeshHandle enrollMesh(const void* vertices,
                        size_t verticesSize,
                        const void* indices,
                        size_t indicesSize);

  MeshHandle enrollMesh(const void* vertices,
                        size_t verticesSize,
                        const void* indices,
                        size_t indicesSize,
                        MeshVertexLayout layout,
                        bool dynamic);

  // Dynamic vertex capacity plus an optional index payload or index capacity.
  // A null indices pointer with nonzero indicesSize reserves a dynamic EBO.
  MeshHandle enrollDynamicMesh(size_t vertexCapacityBytes,
                               const void* indices,
                               size_t indicesSize,
                               MeshVertexLayout layout);

  bool replaceDynamicMesh(MeshHandle handle,
                          size_t vertexCapacityBytes,
                          const void* indices,
                          size_t indicesSize,
                          MeshVertexLayout layout);

  bool destroyMesh(MeshHandle handle);

  TextureHandle enrollTexture(const unsigned char* data, int width, int height);

  TextureHandle enrollTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels);

  TextureHandle enrollTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options);

  TextureHandle enrollCubemap(
    const std::array<const unsigned char*, 6>& facesData,
    int width,
    int height,
    int channels = 3);

  bool replaceCubemap(TextureHandle handle,
                      const std::array<const unsigned char*, 6>& faces,
                      int width,
                      int height,
                      int channels);
  bool replaceTexture(TextureHandle handle,
                      const unsigned char* data,
                      int width,
                      int height,
                      int channels,
                      const TextureOptions& options);

  bool replaceShader(ShaderHandle handle, const ShaderSources& sources);

  bool destroyTexture(TextureHandle handle);

  FramebufferHandle enrollDepthFramebuffer(int width,
                                           int height,
                                           TextureHandle* outDepthTexture);

  // One RGBA8 colour attachment, nearest-filtered; invalid on failure.
  FramebufferHandle enrollColorFramebuffer(int width,
                                           int height,
                                           TextureHandle* outColorTexture);

  bool destroyFramebuffer(FramebufferHandle handle);

  bool destroyShader(ShaderHandle handle);

  TextureInfo getTextureInfo(TextureHandle handle) const;

  // Invalid when the backend has no instance or uniform buffers.
  BufferHandle enrollBuffer(BufferUsage usage, size_t capacityBytes);
  bool destroyBuffer(BufferHandle handle);

  // Writes (once per RenderScene) and binds the FrameUniforms block that
  // instanced styles read. Call in each pass before their draws, after
  // shadow fitting. False when the backend has no uniform buffers.
  bool useFrameUniforms();
  const FrameUniforms& getFrameUniforms() const { return m_frameUniforms; }

  // Between these, push helpers append to `list` instead of the frame queue
  // (bindStyle included). Matrices are copied into the list. Record outside
  // any other recording; the list is cleared by its owner beforehand.
  void beginRecording(RecordedCommandList* list);
  void endRecording();
  bool isRecording() const { return m_recording != nullptr; }
  // Queues one token that runs `list` in place at this point of the frame.
  bool pushExecuteList(const RecordedCommandList* list);
  const RecordedListStats& getRecordedListStats() const
  {
    return m_recordedStats;
  }

  // Built-in styles: enroll shaders once; bind emits pipeline + SetShader.
  // Implemented in RendererStyles.cpp.
  void ensureBuiltinStyles();
  RenderStyleHandle createStyle(const RenderStyle& style);
  bool updateStyle(RenderStyleHandle handle, const RenderStyle& style);
  bool destroyStyle(RenderStyleHandle handle);
  const RenderStyle* getStyle(RenderStyleHandle handle) const;
  RenderStyle* getStyle(RenderStyleHandle handle);
  bool bindStyle(RenderStyleHandle handle);
  RenderStyleHandle getBuiltinStyleHandle(RenderStyleId id) const;
  const RenderStyle* getStyle(RenderStyleId id) const;
  RenderStyle* getStyle(RenderStyleId id);
  bool bindStyle(RenderStyleId id);
  bool builtinStylesReady() const { return _builtinStylesReady; }

  // 1x1 opaque white RGBA texture, created on first call and owned by the
  // renderer. Sprite-style draws sample it to draw flat colour (D-R35).
  // Invalid when the backend cannot create it or cannot draw a texture in the
  // frame that creates it (IBackend::TexturesDrawWhenCreated).
  TextureHandle whiteTexture();

  // =========================================================================
  // Frame lifecycle
  // =========================================================================

  void BeginFrame();
  void setStrictSubmission(bool enabled) { m_strictSubmission = enabled; }
  const std::string& frameError() const { return m_frameError; }
  void reportFrameError(const std::string& message)
  {
    if (m_frameError.empty()) {
      m_frameError = message;
    }
  }
  void EndFrame();
  // Optional CPU timestamp after submission, before backend presentation.
  // This measures an elapsed-time boundary, not GPU execution.
  void EndFrame(std::chrono::steady_clock::time_point* presentationStart);
  // Runs once per successful frame after submission and before presentation,
  // where IBackend::readBackbuffer sees the finished image. Empty clears it.
  void setBeforePresent(std::function<void(Renderer&)> hook)
  {
    m_beforePresent = std::move(hook);
  }
  void SubmitOnly();
  // Draws drawables into an offscreen framebuffer of width x height outside
  // RenderScene and submits them immediately (detached panel windows). The
  // target is cleared to clearColor first; pixel-space drawables lay out for
  // the target size at uiScale; the screen target is restored afterwards.
  // Call between frames, never from inside RenderScene. False when the
  // arguments are invalid, a render is already active, or submission fails.
  bool renderOffscreen(FramebufferHandle target,
                       int width,
                       int height,
                       const std::vector<DrawableBase*>& drawables,
                       const std::array<float, 4>& clearColor,
                       float uiScale);

  // =========================================================================
  // Typed token helpers (push into backend queue)
  // =========================================================================

  void pushClearColor(float r, float g, float b, float a);
  void pushClearScreen(float r, float g, float b, float a);
  void pushClearDepth();
  void pushClearDepth(float value);
  void pushViewport(int x, int y, int width, int height);
  void pushPipelineState(const PipelineState& state);
  void pushSetShader(ShaderHandle handle);
  void pushSetMesh(MeshHandle handle);
  void pushSetTexture(TextureHandle handle, unsigned int slot);
  void pushFramebuffer(FramebufferHandle handle);
  void pushUniformInt(const char* name, int value);
  void pushUniformFloat(const char* name, float value);
  void pushUniformVec2(const char* name, float x, float y);
  void pushUniformVec3(const char* name, float x, float y, float z);
  void pushUniformVec4(const char* name, float x, float y, float z, float w);
  void pushUniformMat4(const char* name, const float* m16);
  void pushDrawIndexed(unsigned int elementCount, unsigned int firstIndex = 0);
  void pushScissor(bool enabled, int x, int y, int width, int height);
  // Intersects with the active scissor and restores it on pop. Coordinates
  // use the backend viewport's bottom-left pixel convention.
  void pushClipRect(int x, int y, int width, int height);
  void popClipRect();
  bool pushUpdateTexture(TextureHandle handle,
                         int x,
                         int y,
                         int width,
                         int height,
                         int channels,
                         const void* data,
                         int srcRowStride = 0);
  bool pushUpdateBuffer(MeshHandle meshHandle,
                        unsigned int offsetBytes,
                        unsigned int sizeBytes,
                        const void* data);
  bool pushUpdateIndexBuffer(MeshHandle meshHandle,
                             unsigned int offsetBytes,
                             unsigned int sizeBytes,
                             const void* data);
  bool pushWriteBuffer(BufferHandle handle,
                       unsigned int offsetBytes,
                       unsigned int sizeBytes,
                       const void* data);
  // Resolves a canvas fade texture (two RGBA8 texels per cell, frame schema
  // v9) into `target`, cellsWide x cellsHigh, one settled colour per cell, so
  // the canvas shader samples plain colours (D-R32). Restores the current
  // pass framebuffer, viewport and scissor; the caller binds its style again.
  // False while recording or when the resolve style is unavailable.
  bool pushCanvasFadeResolve(TextureHandle source,
                             FramebufferHandle target,
                             int cellsWide,
                             int cellsHigh,
                             float clock,
                             float speed);
  void pushBindUniformBuffer(BufferHandle handle, unsigned int binding);
  void pushInstanceStream(BufferHandle handle,
                          unsigned int offsetBytes,
                          InstanceLayout layout);
  void pushDrawIndexedInstanced(unsigned int elementCount,
                                unsigned int firstIndex,
                                unsigned int instanceCount);

  // =========================================================================
  // Render targets & pass execution helpers
  // =========================================================================

  RenderTargetPool& getRenderTargetPool() { return _renderTargetPool; }
  const RenderTargetPool& getRenderTargetPool() const
  {
    return _renderTargetPool;
  }

  PooledRenderTarget acquireRenderTarget(const PooledRenderTargetDesc& desc);
  PooledRenderTarget getRenderTarget(const std::string& name) const;
  FramebufferHandle getCurrentPassFramebuffer() const
  {
    return _currentPassFbo;
  }
  std::array<int, 4> getCurrentPassViewport() const
  {
    return _currentPassViewport;
  }
  // The enclosing clip as { enabled, x, y, width, height }. A recording that
  // pushes and pops clips is only valid under the clip it was recorded in.
  std::array<int, 5> getClipState() const
  {
    return { currentScissorState.enabled ? 1 : 0,
             currentScissorState.x,
             currentScissorState.y,
             currentScissorState.width,
             currentScissorState.height };
  }
  void ensureFullscreenQuadMesh();
  void executePostProcessPass(const RenderPassDesc& pass,
                              const std::array<int, 2>& targetDims);

  // =========================================================================
  // Draw list render (token-first; hybrid immediate only if AppendCommands
  // fails)
  // Production: Canvas / CommandLine / GLString / SplashText are pure-token
  // (D-R10). Immediate Draw() remains for test stubs and any future unmigrated
  // drawable.
  // =========================================================================

  void RenderScene(DrawList* scene, Camera* camera);

  // =========================================================================
  // Token proof helpers (test / sample only — not called by Illumo::render)
  // =========================================================================

  void ensureProofResources();
  void RenderProofQuad();
};
