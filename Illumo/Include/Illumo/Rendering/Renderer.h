#pragma once
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/RenderPass.h>
#include <Illumo/Rendering/RenderStyle.h>
#include <Illumo/Rendering/RenderTargetPool.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <Illumo/Services/ArenaAlloc.h>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Camera;
class IRenderWindow;
class EnvVars;
class Scene;

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
    float uiScale = 1.0f;
    bool active = false;
    bool hasWorldMvp = false;
  };

  struct ShadowCasterDesc
  {
    std::array<float, 3> boundsMin{ 0.0f, 0.0f, 0.0f };
    std::array<float, 3> boundsMax{ 0.0f, 0.0f, 0.0f };
    std::array<float, 3> lightDirection{ 0.5f, 1.0f, 0.3f };
    int mapSize = 1024;
    float minimumRadius = 2.5f;
    float lightDistance = 8.0f;
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
  EnvVars* envVars;
  Scene* currentScene;
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

  // Per-frame scratch (immediate-draw pointer list, etc.). Cleared at the
  // start of RenderScene and again after submission so token payload pointers
  // that live only for the frame never outlive the submit window by design.
  ArenaAlloc frameArena{ 8 * 1024 };
  static constexpr size_t UNIFORM_MATRICES_PER_CHUNK = 128;
  static constexpr size_t MAX_UNIFORM_MATRICES = 65536;
  using UniformMatrix = std::array<float, 16>;
  using UniformMatrixChunk =
    std::array<UniformMatrix, UNIFORM_MATRICES_PER_CHUNK>;
  std::vector<std::unique_ptr<UniformMatrixChunk>> uniformMatrixChunks;
  size_t uniformMatrixCount = 0;
  FrameContext frameContext;
  bool m_strictSubmission = false;
  std::string m_frameError;

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
  ShadowFrameContext shadowFrameContext;

  void beginFrameContext(Camera* camera);
  void endFrameContext();
  void resetShadowFrame();
  bool prepareShadowPass();
  void ensureShadowResources(int mapSize);
  void releaseShadowResources();
  const float* retainUniformMatrix(const float* value);
  void clearCommandQueue();

public:
  // Composition-root path: ownership transferred via unique_ptr (D-R11).
  Renderer(IRenderWindow* window,
           EnvVars* envVars,
           Camera* cam,
           std::unique_ptr<IBackend> backend);

  // Backend-neutral inject: production uses CreateOpenGLBackend +
  // takeOwnership=true; tests inject stack MockBackend with
  // takeOwnership=false.
  Renderer(IRenderWindow* window,
           EnvVars* envVars,
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

  bool destroyFramebuffer(FramebufferHandle handle);

  bool destroyShader(ShaderHandle handle);

  TextureInfo getTextureInfo(TextureHandle handle) const;

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
  void SubmitOnly();

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
  void pushUpdateTexture(TextureHandle handle,
                         int x,
                         int y,
                         int width,
                         int height,
                         int channels,
                         const void* data,
                         int srcRowStride = 0);
  void pushUpdateBuffer(MeshHandle meshHandle,
                        unsigned int offsetBytes,
                        unsigned int sizeBytes,
                        const void* data);
  void pushUpdateIndexBuffer(MeshHandle meshHandle,
                             unsigned int offsetBytes,
                             unsigned int sizeBytes,
                             const void* data);

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
  void ensureFullscreenQuadMesh();
  void executePostProcessPass(const RenderPassDesc& pass,
                              const std::array<int, 2>& targetDims);

  // =========================================================================
  // Scene render (token-first; hybrid immediate only if AppendCommands fails)
  // Production: Canvas / CommandLine / GLString / SplashText are pure-token
  // (D-R10). Immediate Draw() remains for test stubs and any future unmigrated
  // drawable.
  // =========================================================================

  void RenderScene(Scene* scene, Camera* camera);

  // =========================================================================
  // Token proof helpers (test / sample only — not called by Illumo::render)
  // =========================================================================

  void ensureProofResources();
  void RenderProofQuad();
};
