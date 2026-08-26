#pragma once
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <Illumo/Rendering/RenderStyle.h>
#include <Illumo/Rendering/ResourceHandlePool.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/ArenaAlloc.h>
#include <Illumo/Services/EnvVars.h>
#include <array>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

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

private:
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

  // Per-frame scratch (immediate-draw pointer list, etc.). Cleared at the
  // start of RenderScene and again after submission so token payload pointers
  // that live only for the frame never outlive the submit window by design.
  ArenaAlloc frameArena{ 8 * 1024 };
  FrameContext frameContext;

  static void copyUniformName(char* dest, size_t destSize, const char* name)
  {
    if (!dest || destSize == 0) {
      return;
    }
    if (!name) {
      dest[0] = '\0';
      return;
    }
    size_t i = 0;
    for (; i + 1 < destSize && name[i] != '\0'; ++i) {
      dest[i] = name[i];
    }
    dest[i] = '\0';
  }

  void beginFrameContext(Camera* camera)
  {
    frameContext.active = false;
    frameContext.hasWorldMvp = false;
    frameContext.worldCamera = camera;
    if (_window == nullptr) {
      return;
    }

    frameContext.windowDimensions = _window->getWindowDimensions();
    frameContext.uiScale = 1.0f;
    if (envVars != nullptr) {
      const EnvVar& scaleVar = envVars->getVar("uiScale");
      if (!scaleVar.value.empty() && scaleVar.valueAsDouble > 0.0) {
        frameContext.uiScale = static_cast<float>(scaleVar.valueAsDouble);
      }
    }
    frameContext.active = true;
    if (camera == nullptr) {
      return;
    }

    const int height = frameContext.windowDimensions[1] > 0
                         ? frameContext.windowDimensions[1]
                         : 1;
    const float aspect = static_cast<float>(frameContext.windowDimensions[0]) /
                         static_cast<float>(height);
    const glm::mat4 matrix = camera->GetMVPMatrix(aspect);
    std::memcpy(frameContext.worldMvp.data(),
                &matrix[0][0],
                frameContext.worldMvp.size() * sizeof(float));
    frameContext.hasWorldMvp = true;
  }

  void endFrameContext()
  {
    frameContext.active = false;
    frameContext.hasWorldMvp = false;
    frameContext.worldCamera = nullptr;
    frameContext.uiScale = 1.0f;
  }

public:
  // Composition-root path: ownership transferred via unique_ptr (D-R11).
  Renderer(IRenderWindow* window,
           EnvVars* envVars,
           Camera* cam,
           std::unique_ptr<IBackend> backend)
    : _ownedBackend(std::move(backend))
    , _backend(_ownedBackend.get())
    , _window(window)
    , _camera(cam)
    , envVars(envVars)
    , currentScene(nullptr)
  {
    (void)envVars;
  }

  // Backend-neutral inject: production uses CreateOpenGLBackend +
  // takeOwnership=true; tests inject stack MockBackend with
  // takeOwnership=false.
  Renderer(IRenderWindow* window,
           EnvVars* envVars,
           Camera* cam,
           IBackend* backend,
           bool takeOwnership)
    : _ownedBackend(takeOwnership ? std::unique_ptr<IBackend>(backend)
                                  : std::unique_ptr<IBackend>())
    , _backend(takeOwnership ? _ownedBackend.get() : backend)
    , _window(window)
    , _camera(cam)
    , envVars(envVars)
    , currentScene(nullptr)
  {
  }

  ~Renderer()
  {
    if (_ownedBackend) {
      _ownedBackend->Shutdown();
      _ownedBackend.reset();
    }
    _backend = nullptr;
  }

  IBackend* getBackend() { return _backend; }
  const IBackend* getBackend() const { return _backend; }
  bool ownsBackend() const { return _ownedBackend != nullptr; }
  IRenderWindow* getWindow() { return _window; }
  Camera* getCamera() { return _camera; }
  const FrameContext& getFrameContext() const { return frameContext; }
  float getUiScale() const
  {
    if (frameContext.active) {
      return frameContext.uiScale;
    }
    if (envVars != nullptr) {
      const EnvVar& scaleVar = envVars->getVar("uiScale");
      if (!scaleVar.value.empty() && scaleVar.valueAsDouble > 0.0) {
        return static_cast<float>(scaleVar.valueAsDouble);
      }
    }
    return 1.0f;
  }

  // =========================================================================
  // Asset enrollment (not mixed into the per-frame token stream — D-007)
  // =========================================================================

  ShaderHandle enrollShader(const ShaderPaths& paths)
  {
    return _backend->CreateShaderProgram(paths);
  }

  ShaderHandle enrollShader(const ShaderSources& sources)
  {
    return _backend->CreateShaderProgram(sources);
  }

  MeshHandle enrollMesh(const void* vertices,
                        const size_t verticesSize,
                        const void* indices,
                        const size_t indicesSize)
  {
    return _backend->CreateMesh(vertices, verticesSize, indices, indicesSize);
  }

  MeshHandle enrollMesh(const void* vertices,
                        const size_t verticesSize,
                        const void* indices,
                        const size_t indicesSize,
                        MeshVertexLayout layout,
                        bool dynamic)
  {
    return _backend->CreateMesh(
      vertices, verticesSize, indices, indicesSize, layout, dynamic);
  }

  // Dynamic VBO (capacityBytes) + static index buffer; for UI text/console.
  MeshHandle enrollDynamicMesh(size_t vertexCapacityBytes,
                               const void* indices,
                               size_t indicesSize,
                               MeshVertexLayout layout)
  {
    return _backend->CreateMesh(
      nullptr, vertexCapacityBytes, indices, indicesSize, layout, true);
  }

  bool replaceDynamicMesh(MeshHandle handle,
                          size_t vertexCapacityBytes,
                          const void* indices,
                          size_t indicesSize,
                          MeshVertexLayout layout)
  {
    return _backend->ReplaceMesh(
      handle, nullptr, vertexCapacityBytes, indices, indicesSize, layout, true);
  }

  bool destroyMesh(MeshHandle handle) { return _backend->DestroyMesh(handle); }

  TextureHandle enrollTexture(const unsigned char* data,
                              const int width,
                              const int height)
  {
    return _backend->CreateTexture(data, width, height);
  }

  TextureHandle enrollTexture(const unsigned char* data,
                              const int width,
                              const int height,
                              int channels)
  {
    TextureOptions options;
    return _backend->CreateTexture(data, width, height, channels, options);
  }

  TextureHandle enrollTexture(const unsigned char* data,
                              const int width,
                              const int height,
                              int channels,
                              const TextureOptions& options)
  {
    return _backend->CreateTexture(data, width, height, channels, options);
  }

  bool replaceTexture(TextureHandle handle,
                      const unsigned char* data,
                      int width,
                      int height,
                      int channels,
                      const TextureOptions& options)
  {
    return _backend->ReplaceTexture(
      handle, data, width, height, channels, options);
  }

  bool replaceShader(ShaderHandle handle, const ShaderSources& sources)
  {
    return _backend->ReplaceShaderProgram(handle, sources);
  }

  bool destroyTexture(TextureHandle handle)
  {
    return _backend->DestroyTexture(handle);
  }

  bool destroyShader(ShaderHandle handle)
  {
    return _backend->DestroyShaderProgram(handle);
  }

  TextureInfo getTextureInfo(TextureHandle handle) const
  {
    return _backend->GetTextureInfo(handle);
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

  // =========================================================================
  // Frame lifecycle
  // =========================================================================

  void BeginFrame()
  {
    _backend->BeginFrame();
    _backend->ClearCommandQueue();
  }

  void EndFrame()
  {
    // RenderScene usually already submitted; avoid a redundant empty walk.
    // (Backend still may receive an empty queue if callers only push then
    // EndFrame.)
    _backend->SubmitCommandQueue();
    _backend->EndFrame();
  }

  void SubmitOnly() { _backend->SubmitCommandQueue(); }

  // =========================================================================
  // Typed token helpers (push into backend queue)
  // =========================================================================

  void pushClearColor(float r, float g, float b, float a)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::ClearColorBuffer;
    cmd.clear.r = r;
    cmd.clear.g = g;
    cmd.clear.b = b;
    cmd.clear.a = a;
    _backend->PushToCommandQueue(cmd);
  }

  void pushClearScreen(float r, float g, float b, float a)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::ClearScreen;
    cmd.clear.r = r;
    cmd.clear.g = g;
    cmd.clear.b = b;
    cmd.clear.a = a;
    _backend->PushToCommandQueue(cmd);
  }

  void pushViewport(int x, int y, int width, int height)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetViewport;
    cmd.viewport.x = x;
    cmd.viewport.y = y;
    cmd.viewport.width = width;
    cmd.viewport.height = height;
    _backend->PushToCommandQueue(cmd);
  }

  void pushPipelineState(const PipelineState& state)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetPipelineState;
    cmd.pipelineState = state;
    _backend->PushToCommandQueue(cmd);
  }

  void pushSetShader(ShaderHandle handle)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetShader;
    cmd.bindShader.handle = handle;
    _backend->PushToCommandQueue(cmd);
  }

  void pushSetMesh(MeshHandle handle)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetMesh;
    cmd.bindMesh.handle = handle;
    _backend->PushToCommandQueue(cmd);
  }

  void pushSetTexture(TextureHandle handle, unsigned int slot)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetTexture;
    cmd.bindTexture.handle = handle;
    cmd.bindTexture.slot = slot;
    _backend->PushToCommandQueue(cmd);
  }

  void pushUniformInt(const char* name, int value)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetUniformInt;
    copyUniformName(cmd.uniformInt.name, sizeof(cmd.uniformInt.name), name);
    cmd.uniformInt.value = value;
    _backend->PushToCommandQueue(cmd);
  }

  void pushUniformFloat(const char* name, float value)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetUniformFloat;
    copyUniformName(cmd.uniformFloat.name, sizeof(cmd.uniformFloat.name), name);
    cmd.uniformFloat.value = value;
    _backend->PushToCommandQueue(cmd);
  }

  void pushUniformVec2(const char* name, float x, float y)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetUniformVec2;
    copyUniformName(cmd.uniformVec2.name, sizeof(cmd.uniformVec2.name), name);
    cmd.uniformVec2.x = x;
    cmd.uniformVec2.y = y;
    _backend->PushToCommandQueue(cmd);
  }

  void pushUniformMat4(const char* name, const float* m16)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetUniformMat4;
    copyUniformName(cmd.uniformMat4.name, sizeof(cmd.uniformMat4.name), name);
    if (m16) {
      std::memcpy(cmd.uniformMat4.m, m16, 16 * sizeof(float));
    }
    _backend->PushToCommandQueue(cmd);
  }

  void pushDrawIndexed(unsigned int elementCount, unsigned int firstIndex = 0)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::DrawIndexed;
    cmd.drawIndexed.elementCount = elementCount;
    cmd.drawIndexed.firstIndex = firstIndex;
    _backend->PushToCommandQueue(cmd);
  }

  void pushScissor(bool enabled, int x, int y, int width, int height)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::SetScissorState;
    cmd.scissor.enabled = enabled;
    cmd.scissor.x = x;
    cmd.scissor.y = y;
    cmd.scissor.width = width;
    cmd.scissor.height = height;
    _backend->PushToCommandQueue(cmd);
  }

  void pushUpdateTexture(TextureHandle handle,
                         int x,
                         int y,
                         int width,
                         int height,
                         int channels,
                         const void* data,
                         int srcRowStride = 0)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::UpdateTexture;
    cmd.updateTexture.handle = handle;
    cmd.updateTexture.x = x;
    cmd.updateTexture.y = y;
    cmd.updateTexture.width = width;
    cmd.updateTexture.height = height;
    cmd.updateTexture.channels = channels;
    cmd.updateTexture.srcRowStride = srcRowStride;
    cmd.updateTexture.data = data;
    _backend->PushToCommandQueue(cmd);
  }

  void pushUpdateBuffer(MeshHandle meshHandle,
                        unsigned int offsetBytes,
                        unsigned int sizeBytes,
                        const void* data)
  {
    RenderCommand cmd;
    cmd.commandType = CommandType::UpdateBuffer;
    cmd.updateBuffer.handle = meshHandle;
    cmd.updateBuffer.offsetBytes = offsetBytes;
    cmd.updateBuffer.sizeBytes = sizeBytes;
    cmd.updateBuffer.data = data;
    _backend->PushToCommandQueue(cmd);
  }

  // =========================================================================
  // Scene render (token-first; hybrid immediate only if AppendCommands fails)
  // Production: Canvas / CommandLine / GLString / SplashText are pure-token
  // (D-R10). Immediate Draw() remains for test stubs and any future unmigrated
  // drawable.
  // =========================================================================

  void RenderScene(Scene* scene, Camera* camera)
  {
    currentScene = scene;
    if (_camera == nullptr) {
      _camera = camera;
    }

    frameArena.Clear();
    beginFrameContext(_camera);

    // Single main pass (default FB): clear once, then World → UI → Debug.
    const std::array<int, 2>& dims = frameContext.windowDimensions;
    pushViewport(0, 0, dims[0], dims[1]);

    PipelineState defaultState;
    defaultState.depthTestEnabled = true;
    defaultState.blendEnabled = false;
    defaultState.faceCullingEnabled = false;
    defaultState.primitives = Primitives::Triangles;
    pushPipelineState(defaultState);

    pushClearScreen(0.1f, 0.1f, 0.1f, 1.0f);

    DrawableBase** immediateList = nullptr;
    size_t immediateCount = 0;
    size_t immediateCap = 0;
    if (scene) {
      immediateCap = scene->drawableCount();
      if (immediateCap > 0) {
        immediateList = static_cast<DrawableBase**>(
          frameArena.AllocateBytes(sizeof(DrawableBase*) * immediateCap));
      }
      for (unsigned layerIndex = 0; layerIndex < renderLayerCount();
           ++layerIndex) {
        const RenderLayerId layer = static_cast<RenderLayerId>(layerIndex);
        const std::vector<DrawableBase*>& list = scene->drawablesIn(layer);
        for (size_t i = 0; i < list.size(); ++i) {
          DrawableBase* drawable = list[i];
          if (!drawable) {
            continue;
          }
          // Pure-token: returns true. Immediate fallback if false (tests /
          // stubs).
          if (!drawable->AppendCommands(this)) {
            if (immediateList != nullptr && immediateCount < immediateCap) {
              immediateList[immediateCount] = drawable;
              immediateCount += 1;
            }
          }
        }
      }
    }

    // Submit clear + token drawables before any immediate overlays.
    _backend->SubmitCommandQueue();
    _backend->ClearCommandQueue();

    for (size_t i = 0; i < immediateCount; ++i) {
      immediateList[i]->Draw();
    }

    // Immediate draws finished; release frame scratch (not used as token
    // payload storage — command queue owns those pointers until submit).
    frameArena.Clear();
    endFrameContext();
  }

  // =========================================================================
  // Token proof helpers (test / sample only — not called by Illumo::render)
  // =========================================================================

  void ensureProofResources()
  {
    if (_proofReady) {
      return;
    }

    // NDC-ish quad in pixel-ish space with identity MVP; shader multiplies by
    // uMVP. Layout: pos3 | color3 | uv2  (same as Canvas / triangle shaders)
    const float verts[32] = {
      1.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f,  1.0f, 1.0f, -1.0f, 0.0f,
      0.0f, 1.0f, 0.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  1.0f,
      0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  0.0f, 0.0f, 1.0f,
    };
    const unsigned int indices[6] = { 0, 1, 2, 0, 2, 3 };

    _proofMeshHandle =
      enrollMesh(verts, sizeof(verts), indices, sizeof(indices));

    ShaderPaths paths;
    paths.vertexPath = "Shader/triangle_vertex.glsl";
    paths.fragmentPath = "Shader/triangle_frag.glsl";
    _proofShaderHandle = enrollShader(paths);

    // 2x2 RGBA checkerboard (magenta / dark)
    const int tw = 2;
    const int th = 2;
    unsigned char tex[2 * 2 * 4] = {
      255, 0, 255, 255, 40, 40, 40, 255, 40, 40, 40, 255, 255, 0, 255, 255,
    };
    _proofTextureHandle = enrollTexture(tex, tw, th, 4);

    _proofReady = true;
  }

  // Clears + draws a fullscreen-ish textured quad entirely via tokens.
  // Caller must still swap buffers (or call EndFrame which submits+swaps).
  void RenderProofQuad()
  {
    ensureProofResources();

    _backend->ClearCommandQueue();

    std::array<int, 2> dims = _window->getWindowDimensions();
    pushViewport(0, 0, dims[0], dims[1]);

    PipelineState ps;
    ps.depthTestEnabled = false;
    ps.blendEnabled = false;
    ps.faceCullingEnabled = false;
    ps.primitives = Primitives::Triangles;
    pushPipelineState(ps);

    // Dark blue clear so we can tell token clear worked.
    pushClearScreen(0.05f, 0.08f, 0.18f, 1.0f);

    pushSetShader(_proofShaderHandle);
    pushSetMesh(_proofMeshHandle);
    pushSetTexture(_proofTextureHandle, 0);

    // Identity MVP → NDC quad fills clip space.
    float identity[16] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    pushUniformMat4("uMVP", identity);
    pushUniformInt("ourTexture", 0);
    pushDrawIndexed(6, 0);

    _backend->SubmitCommandQueue();
    // Prevent EndFrame from re-executing the same tokens.
    _backend->ClearCommandQueue();
  }
};
