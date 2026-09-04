#include <Illumo/Rendering/Renderer.h>

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/EnvVars.h>
#include <cstring>
#include <glm/glm.hpp>
#include <vector>

namespace {

void
copyUniformName(char* dest, size_t destSize, const char* name)
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

} // namespace

void
Renderer::beginFrameContext(Camera* camera)
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

  const int height =
    frameContext.windowDimensions[1] > 0 ? frameContext.windowDimensions[1] : 1;
  const float aspect = static_cast<float>(frameContext.windowDimensions[0]) /
                       static_cast<float>(height);
  const glm::mat4 matrix = camera->GetMVPMatrix(aspect);
  std::memcpy(frameContext.worldMvp.data(),
              &matrix[0][0],
              frameContext.worldMvp.size() * sizeof(float));
  frameContext.hasWorldMvp = true;
}

void
Renderer::endFrameContext()
{
  frameContext.active = false;
  frameContext.hasWorldMvp = false;
  frameContext.worldCamera = nullptr;
  frameContext.uiScale = 1.0f;
}

Renderer::Renderer(IRenderWindow* window,
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
  _renderTargetPool.setBackend(_backend);
}

Renderer::Renderer(IRenderWindow* window,
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
  _renderTargetPool.setBackend(_backend);
}

Renderer::~Renderer()
{
  _renderTargetPool.releaseAll();
  if (_ownedBackend) {
    _ownedBackend->Shutdown();
    _ownedBackend.reset();
  }
  _backend = nullptr;
}

float
Renderer::getUiScale() const
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

ShaderHandle
Renderer::enrollShader(const ShaderPaths& paths)
{
  return _backend->CreateShaderProgram(paths);
}

ShaderHandle
Renderer::enrollShader(const ShaderSources& sources)
{
  return _backend->CreateShaderProgram(sources);
}

MeshHandle
Renderer::enrollMesh(const void* vertices,
                     const size_t verticesSize,
                     const void* indices,
                     const size_t indicesSize)
{
  return _backend->CreateMesh(vertices, verticesSize, indices, indicesSize);
}

MeshHandle
Renderer::enrollMesh(const void* vertices,
                     const size_t verticesSize,
                     const void* indices,
                     const size_t indicesSize,
                     MeshVertexLayout layout,
                     bool dynamic)
{
  return _backend->CreateMesh(
    vertices, verticesSize, indices, indicesSize, layout, dynamic);
}

MeshHandle
Renderer::enrollDynamicMesh(size_t vertexCapacityBytes,
                            const void* indices,
                            size_t indicesSize,
                            MeshVertexLayout layout)
{
  return _backend->CreateMesh(
    nullptr, vertexCapacityBytes, indices, indicesSize, layout, true);
}

bool
Renderer::replaceDynamicMesh(MeshHandle handle,
                             size_t vertexCapacityBytes,
                             const void* indices,
                             size_t indicesSize,
                             MeshVertexLayout layout)
{
  return _backend->ReplaceMesh(
    handle, nullptr, vertexCapacityBytes, indices, indicesSize, layout, true);
}

bool
Renderer::destroyMesh(MeshHandle handle)
{
  return _backend->DestroyMesh(handle);
}

TextureHandle
Renderer::enrollTexture(const unsigned char* data,
                        const int width,
                        const int height)
{
  return _backend->CreateTexture(data, width, height);
}

TextureHandle
Renderer::enrollTexture(const unsigned char* data,
                        const int width,
                        const int height,
                        int channels)
{
  TextureOptions options;
  return _backend->CreateTexture(data, width, height, channels, options);
}

TextureHandle
Renderer::enrollTexture(const unsigned char* data,
                        const int width,
                        const int height,
                        int channels,
                        const TextureOptions& options)
{
  return _backend->CreateTexture(data, width, height, channels, options);
}

bool
Renderer::replaceTexture(TextureHandle handle,
                         const unsigned char* data,
                         int width,
                         int height,
                         int channels,
                         const TextureOptions& options)
{
  return _backend->ReplaceTexture(
    handle, data, width, height, channels, options);
}

bool
Renderer::replaceShader(ShaderHandle handle, const ShaderSources& sources)
{
  return _backend->ReplaceShaderProgram(handle, sources);
}

bool
Renderer::destroyTexture(TextureHandle handle)
{
  return _backend->DestroyTexture(handle);
}

FramebufferHandle
Renderer::enrollDepthFramebuffer(int width,
                                 int height,
                                 TextureHandle* outDepthTexture)
{
  return _backend->CreateDepthFramebuffer(width, height, outDepthTexture);
}

bool
Renderer::destroyFramebuffer(FramebufferHandle handle)
{
  return _backend->DestroyFramebuffer(handle);
}

bool
Renderer::destroyShader(ShaderHandle handle)
{
  return _backend->DestroyShaderProgram(handle);
}

TextureInfo
Renderer::getTextureInfo(TextureHandle handle) const
{
  return _backend->GetTextureInfo(handle);
}

void
Renderer::BeginFrame()
{
  _backend->BeginFrame();
  _backend->ClearCommandQueue();
  _currentPassFbo = FramebufferHandle{};
  const std::array<int, 2> dims =
    _window ? _window->getWindowDimensions() : std::array<int, 2>{ 0, 0 };
  _currentPassViewport = { 0, 0, dims[0], dims[1] };
}

void
Renderer::EndFrame()
{
  _backend->SubmitCommandQueue();
  _backend->EndFrame();
}

void
Renderer::SubmitOnly()
{
  _backend->SubmitCommandQueue();
}

void
Renderer::pushClearColor(float r, float g, float b, float a)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::ClearColorBuffer;
  cmd.clear.r = r;
  cmd.clear.g = g;
  cmd.clear.b = b;
  cmd.clear.a = a;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushClearScreen(float r, float g, float b, float a)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::ClearScreen;
  cmd.clear.r = r;
  cmd.clear.g = g;
  cmd.clear.b = b;
  cmd.clear.a = a;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushClearDepth()
{
  RenderCommand cmd;
  cmd.commandType = CommandType::ClearDepthBuffer;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushViewport(int x, int y, int width, int height)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetViewport;
  cmd.viewport.x = x;
  cmd.viewport.y = y;
  cmd.viewport.width = width;
  cmd.viewport.height = height;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushPipelineState(const PipelineState& state)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetPipelineState;
  cmd.pipelineState = state;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushSetShader(ShaderHandle handle)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetShader;
  cmd.bindShader.handle = handle;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushSetMesh(MeshHandle handle)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetMesh;
  cmd.bindMesh.handle = handle;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushSetTexture(TextureHandle handle, unsigned int slot)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetTexture;
  cmd.bindTexture.handle = handle;
  cmd.bindTexture.slot = slot;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushFramebuffer(FramebufferHandle handle)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetFramebuffer;
  cmd.bindFramebuffer.handle = handle;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushUniformInt(const char* name, int value)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetUniformInt;
  copyUniformName(cmd.uniformInt.name, sizeof(cmd.uniformInt.name), name);
  cmd.uniformInt.value = value;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushUniformFloat(const char* name, float value)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetUniformFloat;
  copyUniformName(cmd.uniformFloat.name, sizeof(cmd.uniformFloat.name), name);
  cmd.uniformFloat.value = value;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushUniformVec2(const char* name, float x, float y)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetUniformVec2;
  copyUniformName(cmd.uniformVec2.name, sizeof(cmd.uniformVec2.name), name);
  cmd.uniformVec2.x = x;
  cmd.uniformVec2.y = y;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushUniformVec3(const char* name, float x, float y, float z)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetUniformVec3;
  copyUniformName(cmd.uniformVec3.name, sizeof(cmd.uniformVec3.name), name);
  cmd.uniformVec3.x = x;
  cmd.uniformVec3.y = y;
  cmd.uniformVec3.z = z;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushUniformVec4(const char* name, float x, float y, float z, float w)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetUniformVec4;
  copyUniformName(cmd.uniformVec4.name, sizeof(cmd.uniformVec4.name), name);
  cmd.uniformVec4.x = x;
  cmd.uniformVec4.y = y;
  cmd.uniformVec4.z = z;
  cmd.uniformVec4.w = w;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushUniformMat4(const char* name, const float* m16)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::SetUniformMat4;
  copyUniformName(cmd.uniformMat4.name, sizeof(cmd.uniformMat4.name), name);
  if (m16) {
    std::memcpy(cmd.uniformMat4.m, m16, 16 * sizeof(float));
  }
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushDrawIndexed(unsigned int elementCount, unsigned int firstIndex)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::DrawIndexed;
  cmd.drawIndexed.elementCount = elementCount;
  cmd.drawIndexed.firstIndex = firstIndex;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushScissor(bool enabled, int x, int y, int width, int height)
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

void
Renderer::pushUpdateTexture(TextureHandle handle,
                            int x,
                            int y,
                            int width,
                            int height,
                            int channels,
                            const void* data,
                            int srcRowStride)
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

void
Renderer::pushUpdateBuffer(MeshHandle meshHandle,
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

PooledRenderTarget
Renderer::acquireRenderTarget(const PooledRenderTargetDesc& desc)
{
  const std::array<int, 2>& dims = frameContext.windowDimensions;
  return _renderTargetPool.acquire(desc, dims[0], dims[1]);
}

PooledRenderTarget
Renderer::getRenderTarget(const std::string& name) const
{
  return _renderTargetPool.get(name);
}

void
Renderer::ensureFullscreenQuadMesh()
{
  if (_fullscreenQuadReady && _fullscreenQuadMeshHandle.isValid()) {
    return;
  }
  const float verts[32] = {
    1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f,
    1.0f,  1.0f, 1.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  1.0f,
    0.0f,  0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f,
  };
  const unsigned int indices[6] = { 0, 1, 2, 0, 2, 3 };
  _fullscreenQuadMeshHandle = enrollMesh(
    verts, sizeof(verts), indices, sizeof(indices), MeshVertexLayout::Pos3Color3Uv2, false);
  _fullscreenQuadReady = _fullscreenQuadMeshHandle.isValid();
}

void
Renderer::executePostProcessPass(const RenderPassDesc& pass,
                                 const std::array<int, 2>& targetDims)
{
  (void)targetDims;
  PipelineState ps;
  ps.depthTestEnabled = false;
  ps.blendEnabled = false;
  ps.faceCullingEnabled = false;
  ps.primitives = Primitives::Triangles;
  if (pass.overridePipelineState) {
    ps = pass.pipelineState;
  }
  pushPipelineState(ps);

  if (pass.styleHandle.isValid()) {
    bindStyle(pass.styleHandle);
  } else if (pass.shaderHandle.isValid()) {
    pushSetShader(pass.shaderHandle);
  }

  for (size_t i = 0; i < pass.inputTextures.size(); ++i) {
    const PassTextureBinding& binding = pass.inputTextures[i];
    if (binding.texture.isValid()) {
      pushSetTexture(binding.texture, binding.slot);
    }
  }

  for (size_t i = 0; i < pass.inputTargetTextures.size(); ++i) {
    const PassInputTargetBinding& binding = pass.inputTargetTextures[i];
    if (!binding.targetName.empty()) {
      PooledRenderTarget target = _renderTargetPool.get(binding.targetName);
      if (target.isValid() &&
          binding.attachmentIndex < target.attachments.colorTextures.size()) {
        TextureHandle tex =
          target.attachments.colorTextures[binding.attachmentIndex];
        if (tex.isValid()) {
          pushSetTexture(tex, binding.slot);
          if (!binding.samplerUniformName.empty()) {
            pushUniformInt(binding.samplerUniformName.c_str(),
                           static_cast<int>(binding.slot));
          }
        }
      }
    }
  }

  for (size_t i = 0; i < pass.uniformFloats.size(); ++i) {
    pushUniformFloat(pass.uniformFloats[i].name.c_str(),
                     pass.uniformFloats[i].value);
  }
  for (size_t i = 0; i < pass.uniformInts.size(); ++i) {
    pushUniformInt(pass.uniformInts[i].name.c_str(),
                   pass.uniformInts[i].value);
  }
  for (size_t i = 0; i < pass.uniformMat4s.size(); ++i) {
    pushUniformMat4(pass.uniformMat4s[i].name.c_str(),
                    pass.uniformMat4s[i].matrix.data());
  }

  ensureFullscreenQuadMesh();
  pushSetMesh(_fullscreenQuadMeshHandle);
  pushDrawIndexed(6);
}

void
Renderer::RenderScene(Scene* scene, Camera* camera)
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

      if (!scene->hasCustomPasses(layer)) {
        _currentPassFbo = FramebufferHandle{};
        _currentPassViewport = {
          0, 0, frameContext.windowDimensions[0], frameContext.windowDimensions[1]
        };
        for (size_t i = 0; i < list.size(); ++i) {
          DrawableBase* drawable = list[i];
          if (!drawable) {
            continue;
          }
          if (!drawable->AppendCommands(this)) {
            if (immediateList != nullptr && immediateCount < immediateCap) {
              immediateList[immediateCount] = drawable;
              immediateCount += 1;
            }
          }
        }
      } else {
        const std::vector<RenderPassDesc>& passes = scene->passesIn(layer);
        for (size_t p = 0; p < passes.size(); ++p) {
          const RenderPassDesc& pass = passes[p];

          FramebufferHandle targetFbo{};
          int targetW = frameContext.windowDimensions[0];
          int targetH = frameContext.windowDimensions[1];

          if (!pass.useScreenTarget) {
            if (!pass.pooledTargetName.empty()) {
              PooledRenderTarget pooled =
                _renderTargetPool.acquire(pass.targetDesc,
                                          frameContext.windowDimensions[0],
                                          frameContext.windowDimensions[1]);
              targetFbo = pooled.fboHandle;
              targetW = pooled.width;
              targetH = pooled.height;
            }
          }

          _currentPassFbo = targetFbo;
          if (pass.customViewport) {
            _currentPassViewport = { pass.viewportX,
                                     pass.viewportY,
                                     pass.viewportWidth,
                                     pass.viewportHeight };
          } else {
            _currentPassViewport = { 0, 0, targetW, targetH };
          }

          pushFramebuffer(_currentPassFbo);
          pushViewport(_currentPassViewport[0],
                       _currentPassViewport[1],
                       _currentPassViewport[2],
                       _currentPassViewport[3]);

          if (pass.clear.clearColor && pass.clear.clearDepth) {
            pushClearScreen(pass.clear.clearColorValue[0],
                            pass.clear.clearColorValue[1],
                            pass.clear.clearColorValue[2],
                            pass.clear.clearColorValue[3]);
          } else {
            if (pass.clear.clearColor) {
              pushClearScreen(pass.clear.clearColorValue[0],
                              pass.clear.clearColorValue[1],
                              pass.clear.clearColorValue[2],
                              pass.clear.clearColorValue[3]);
            }
            if (pass.clear.clearDepth) {
              pushClearDepth();
            }
          }

          if (pass.overridePipelineState) {
            pushPipelineState(pass.pipelineState);
          }

          if (pass.type == PassType::Draw) {
            for (size_t i = 0; i < list.size(); ++i) {
              DrawableBase* drawable = list[i];
              if (!drawable) {
                continue;
              }
              if ((drawable->getPassMask() & pass.passMask) == 0) {
                continue;
              }
              if (!drawable->AppendCommands(this)) {
                if (immediateList != nullptr &&
                    immediateCount < immediateCap) {
                  immediateList[immediateCount] = drawable;
                  immediateCount += 1;
                }
              }
            }
          } else if (pass.type == PassType::PostProcess) {
            executePostProcessPass(pass, { targetW, targetH });
          } else if (pass.type == PassType::Custom && pass.customExecution) {
            pass.customExecution(this);
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

  frameArena.Clear();
  endFrameContext();
}

void
Renderer::ensureProofResources()
{
  if (_proofReady) {
    return;
  }

  const float verts[32] = {
    1.0f,  1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f,
    0.0f,  1.0f, 0.0f, 1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  1.0f,
    0.0f,  0.0f, -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
  };
  const unsigned int indices[6] = { 0, 1, 2, 0, 2, 3 };

  _proofMeshHandle =
    enrollMesh(verts, sizeof(verts), indices, sizeof(indices));

  ShaderPaths paths;
  paths.vertexPath = "Shader/triangle_vertex.glsl";
  paths.fragmentPath = "Shader/triangle_frag.glsl";
  _proofShaderHandle = enrollShader(paths);

  const int tw = 2;
  const int th = 2;
  unsigned char tex[2 * 2 * 4] = {
    255, 0, 255, 255, 40, 40, 40, 255, 40, 40, 40, 255, 255, 0, 255, 255,
  };
  _proofTextureHandle = enrollTexture(tex, tw, th, 4);

  _proofReady = true;
}

void
Renderer::RenderProofQuad()
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

  pushClearScreen(0.05f, 0.08f, 0.18f, 1.0f);

  pushSetShader(_proofShaderHandle);
  pushSetMesh(_proofMeshHandle);
  pushSetTexture(_proofTextureHandle, 0);

  float identity[16] = {
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
  };
  pushUniformMat4("uMVP", identity);
  pushUniformInt("ourTexture", 0);
  pushDrawIndexed(6, 0);

  _backend->SubmitCommandQueue();
  _backend->ClearCommandQueue();
}
