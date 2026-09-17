#include <Illumo/Rendering/Renderer.h>

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/EnvVars.h>
#include <algorithm>
#include <cstring>
#include <glm/glm.hpp>

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
  shadowFrameContext.active = false;
  shadowFrameContext.depthTexture = TextureHandle{};
}

void
Renderer::resetShadowFrame()
{
  shadowBoundsValid = false;
  requestedShadowMapSize = 0;
  requestedShadowMinimumRadius = 0.0f;
  requestedShadowLightDistance = 0.0f;
  shadowFrameContext = ShadowFrameContext{};
}

void
Renderer::registerShadowCaster(const ShadowCasterDesc& caster)
{
  for (size_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(caster.boundsMin[axis]) ||
        !std::isfinite(caster.boundsMax[axis]) ||
        caster.boundsMin[axis] > caster.boundsMax[axis]) {
      return;
    }
  }

  const glm::vec3 requestedDirection(caster.lightDirection[0],
                                     caster.lightDirection[1],
                                     caster.lightDirection[2]);
  const float directionLength = glm::length(requestedDirection);
  if (!std::isfinite(directionLength) || directionLength <= 0.0001f) {
    return;
  }

  if (!shadowBoundsValid) {
    shadowBoundsMin = caster.boundsMin;
    shadowBoundsMax = caster.boundsMax;
    const glm::vec3 normalizedDirection = requestedDirection / directionLength;
    requestedShadowLightDirection = { normalizedDirection.x,
                                      normalizedDirection.y,
                                      normalizedDirection.z };
    shadowBoundsValid = true;
  } else {
    for (size_t axis = 0; axis < 3; ++axis) {
      shadowBoundsMin[axis] =
        std::min(shadowBoundsMin[axis], caster.boundsMin[axis]);
      shadowBoundsMax[axis] =
        std::max(shadowBoundsMax[axis], caster.boundsMax[axis]);
    }
  }

  requestedShadowMapSize =
    std::max(requestedShadowMapSize, std::max(caster.mapSize, 1));
  requestedShadowMinimumRadius = std::max(requestedShadowMinimumRadius,
                                          std::max(caster.minimumRadius, 0.1f));
  requestedShadowLightDistance = std::max(requestedShadowLightDistance,
                                          std::max(caster.lightDistance, 0.5f));
}

void
Renderer::ensureShadowResources(int mapSize)
{
  if (shadowFramebuffer.isValid() && enrolledShadowMapSize == mapSize) {
    return;
  }

  releaseShadowResources();
  shadowFramebuffer =
    enrollDepthFramebuffer(mapSize, mapSize, &shadowDepthTexture);
  if (shadowFramebuffer.isValid()) {
    enrolledShadowMapSize = mapSize;
  }
}

void
Renderer::releaseShadowResources()
{
  if (_backend != nullptr && shadowFramebuffer.isValid()) {
    destroyFramebuffer(shadowFramebuffer);
  }
  shadowFramebuffer = FramebufferHandle{};
  shadowDepthTexture = TextureHandle{};
  enrolledShadowMapSize = 0;
}

bool
Renderer::prepareShadowPass()
{
  if (!shadowBoundsValid || requestedShadowMapSize <= 0) {
    return false;
  }

  ensureBuiltinStyles();
  if (!getBuiltinStyleHandle(RenderStyleId::ShadowDepth).isValid()) {
    return false;
  }

  ensureShadowResources(requestedShadowMapSize);
  if (!shadowFramebuffer.isValid() || !shadowDepthTexture.isValid()) {
    return false;
  }

  const glm::vec3 boundsMin(
    shadowBoundsMin[0], shadowBoundsMin[1], shadowBoundsMin[2]);
  const glm::vec3 boundsMax(
    shadowBoundsMax[0], shadowBoundsMax[1], shadowBoundsMax[2]);
  const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
  const float sceneRadius = glm::length(boundsMax - boundsMin) * 0.5f;
  const glm::vec3 lightDirection(requestedShadowLightDirection[0],
                                 requestedShadowLightDirection[1],
                                 requestedShadowLightDirection[2]);
  glm::vec3 lightUp(0.0f, 1.0f, 0.0f);
  if (std::abs(glm::dot(lightDirection, lightUp)) > 0.98f) {
    lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
  }

  const float lightDistance =
    std::max(requestedShadowLightDistance, sceneRadius + 0.5f);
  const glm::mat4 lightView =
    glm::lookAt(center + lightDirection * lightDistance, center, lightUp);

  glm::vec3 lightBoundsMin(std::numeric_limits<float>::max());
  glm::vec3 lightBoundsMax(std::numeric_limits<float>::lowest());
  for (int x = 0; x < 2; ++x) {
    for (int y = 0; y < 2; ++y) {
      for (int z = 0; z < 2; ++z) {
        const glm::vec3 corner(x == 0 ? boundsMin.x : boundsMax.x,
                               y == 0 ? boundsMin.y : boundsMax.y,
                               z == 0 ? boundsMin.z : boundsMax.z);
        const glm::vec3 lightCorner =
          glm::vec3(lightView * glm::vec4(corner, 1.0f));
        lightBoundsMin = glm::min(lightBoundsMin, lightCorner);
        lightBoundsMax = glm::max(lightBoundsMax, lightCorner);
      }
    }
  }

  const glm::vec2 lightCenter =
    glm::vec2(lightBoundsMin + lightBoundsMax) * 0.5f;
  const glm::vec2 lightHalfExtents =
    glm::vec2(lightBoundsMax - lightBoundsMin) * 0.5f;
  const float fitPadding = std::max(0.05f, sceneRadius * 0.02f);
  const float halfExtent =
    std::max(requestedShadowMinimumRadius,
             std::max(lightHalfExtents.x, lightHalfExtents.y) + fitPadding);
  const float depthPadding = std::max(0.05f, sceneRadius * 0.05f);
  const float nearPlane = std::max(0.01f, -lightBoundsMax.z - depthPadding);
  const float farPlane =
    std::max(nearPlane + 0.1f, -lightBoundsMin.z + depthPadding);
  const glm::mat4 lightProjection = glm::ortho(lightCenter.x - halfExtent,
                                               lightCenter.x + halfExtent,
                                               lightCenter.y - halfExtent,
                                               lightCenter.y + halfExtent,
                                               nearPlane,
                                               farPlane);
  const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

  std::memcpy(shadowFrameContext.lightSpaceMatrix.data(),
              glm::value_ptr(lightSpaceMatrix),
              shadowFrameContext.lightSpaceMatrix.size() * sizeof(float));
  shadowFrameContext.lightDirection = requestedShadowLightDirection;
  shadowFrameContext.depthTexture = shadowDepthTexture;
  shadowFrameContext.active = true;
  return true;
}

const float*
Renderer::retainUniformMatrix(const float* value)
{
  if (value == nullptr) {
    return nullptr;
  }
  if (uniformMatrixCount >= MAX_UNIFORM_MATRICES) {
    return nullptr;
  }

  const size_t chunkIndex = uniformMatrixCount / UNIFORM_MATRICES_PER_CHUNK;
  const size_t matrixIndex = uniformMatrixCount % UNIFORM_MATRICES_PER_CHUNK;
  if (chunkIndex == uniformMatrixChunks.size()) {
    uniformMatrixChunks.push_back(std::make_unique<UniformMatrixChunk>());
  }

  UniformMatrix& retained = (*uniformMatrixChunks[chunkIndex])[matrixIndex];
  std::memcpy(retained.data(), value, retained.size() * sizeof(float));
  uniformMatrixCount += 1;
  return retained.data();
}

void
Renderer::clearCommandQueue()
{
  _backend->ClearCommandQueue();
  uniformMatrixCount = 0;
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
  _lifetimeIdentity.reset();
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

TextureHandle
Renderer::enrollCubemap(const std::array<const unsigned char*, 6>& facesData,
                        const int width,
                        const int height,
                        int channels)
{
  return _backend->CreateCubemap(facesData, width, height, channels);
}

bool
Renderer::replaceCubemap(TextureHandle handle,
                         const std::array<const unsigned char*, 6>& faces,
                         int width,
                         int height,
                         int channels)
{
  return _backend->ReplaceCubemap(handle, faces, width, height, channels);
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
  m_frameError.clear();
  _backend->BeginFrame();
  clearCommandQueue();
  _currentPassFbo = FramebufferHandle{};
  const std::array<int, 2> dims =
    _window ? _window->getWindowDimensions() : std::array<int, 2>{ 0, 0 };
  _currentPassViewport = { 0, 0, dims[0], dims[1] };
  currentScissorState = ScissorState{};
  scissorStateStack.clear();
}

void
Renderer::EndFrame()
{
  EndFrame(nullptr);
}

void
Renderer::EndFrame(std::chrono::steady_clock::time_point* presentationStart)
{
  _backend->SubmitCommandQueue();
  if (presentationStart != nullptr) {
    *presentationStart = std::chrono::steady_clock::now();
  }
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
  pushClearDepth(1.0f);
}

void
Renderer::pushClearDepth(float value)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::ClearDepthBuffer;
  cmd.clearDepthValue = value;
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
  if (m16 == nullptr) {
    reportFrameError("SetUniformMat4: null matrix value");
    return;
  }
  const float* retained = retainUniformMatrix(m16);
  if (retained == nullptr) {
    reportFrameError("SetUniformMat4: retained matrix ceiling reached");
    return;
  }
  RenderCommand cmd;
  cmd.commandType = CommandType::SetUniformMat4;
  copyUniformName(cmd.uniformMat4.name, sizeof(cmd.uniformMat4.name), name);
  cmd.uniformMat4.value = retained;
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
  currentScissorState.enabled = enabled;
  currentScissorState.x = x;
  currentScissorState.y = y;
  currentScissorState.width = std::max(width, 0);
  currentScissorState.height = std::max(height, 0);
  RenderCommand cmd;
  cmd.commandType = CommandType::SetScissorState;
  cmd.scissor.enabled = enabled;
  cmd.scissor.x = x;
  cmd.scissor.y = y;
  cmd.scissor.width = currentScissorState.width;
  cmd.scissor.height = currentScissorState.height;
  _backend->PushToCommandQueue(cmd);
}

void
Renderer::pushClipRect(int x, int y, int width, int height)
{
  scissorStateStack.push_back(currentScissorState);
  int clippedX = x;
  int clippedY = y;
  int clippedWidth = std::max(width, 0);
  int clippedHeight = std::max(height, 0);
  if (currentScissorState.enabled) {
    const long long left = std::max(
      static_cast<long long>(x), static_cast<long long>(currentScissorState.x));
    const long long bottom = std::max(
      static_cast<long long>(y), static_cast<long long>(currentScissorState.y));
    const long long right =
      std::min(static_cast<long long>(x) + clippedWidth,
               static_cast<long long>(currentScissorState.x) +
                 currentScissorState.width);
    const long long top =
      std::min(static_cast<long long>(y) + clippedHeight,
               static_cast<long long>(currentScissorState.y) +
                 currentScissorState.height);
    clippedX = static_cast<int>(left);
    clippedY = static_cast<int>(bottom);
    clippedWidth = static_cast<int>(std::max(0LL, right - left));
    clippedHeight = static_cast<int>(std::max(0LL, top - bottom));
  }
  pushScissor(true, clippedX, clippedY, clippedWidth, clippedHeight);
}

void
Renderer::popClipRect()
{
  if (scissorStateStack.empty()) {
    reportFrameError("Scissor clip stack underflow");
    return;
  }
  const ScissorState previous = scissorStateStack.back();
  scissorStateStack.pop_back();
  pushScissor(
    previous.enabled, previous.x, previous.y, previous.width, previous.height);
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

void
Renderer::pushUpdateIndexBuffer(MeshHandle meshHandle,
                                unsigned int offsetBytes,
                                unsigned int sizeBytes,
                                const void* data)
{
  RenderCommand cmd;
  cmd.commandType = CommandType::UpdateIndexBuffer;
  cmd.updateIndexBuffer.handle = meshHandle;
  cmd.updateIndexBuffer.offsetBytes = offsetBytes;
  cmd.updateIndexBuffer.sizeBytes = sizeBytes;
  cmd.updateIndexBuffer.data = data;
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
    1.0f, 1.0f, 0.0f,  1.0f, 1.0f, 1.0f,  1.0f,  1.0f, 1.0f, -1.0f, 0.0f,
    1.0f, 1.0f, 1.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 1.0f, 1.0f,  1.0f,
    0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  1.0f, 0.0f, 1.0f,
  };
  const unsigned int indices[6] = { 0, 1, 2, 0, 2, 3 };
  _fullscreenQuadMeshHandle = enrollMesh(verts,
                                         sizeof(verts),
                                         indices,
                                         sizeof(indices),
                                         MeshVertexLayout::Pos3Color3Uv2,
                                         false);
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
    pushUniformInt(pass.uniformInts[i].name.c_str(), pass.uniformInts[i].value);
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
  resetShadowFrame();
  _currentPassFbo = FramebufferHandle{};
  _currentPassViewport = {
    0, 0, frameContext.windowDimensions[0], frameContext.windowDimensions[1]
  };

  if (scene != nullptr) {
    const std::vector<DrawableBase*>& worldDrawables =
      scene->drawablesIn(RenderLayerId::World);
    for (size_t i = 0; i < worldDrawables.size(); ++i) {
      DrawableBase* drawable = worldDrawables[i];
      if (drawable != nullptr && drawable->isVisible()) {
        drawable->CollectShadowCasters(this);
      }
    }

    if (prepareShadowPass()) {
      const FramebufferHandle restoredFramebuffer = _currentPassFbo;
      const std::array<int, 4> restoredViewport = _currentPassViewport;
      pushFramebuffer(shadowFramebuffer);
      pushViewport(0, 0, requestedShadowMapSize, requestedShadowMapSize);
      pushClearDepth();
      bindStyle(RenderStyleId::ShadowDepth);
      for (size_t i = 0; i < worldDrawables.size(); ++i) {
        DrawableBase* drawable = worldDrawables[i];
        if (drawable != nullptr && drawable->isVisible()) {
          drawable->AppendShadowCommands(this);
        }
      }
      pushFramebuffer(restoredFramebuffer);
      pushViewport(restoredViewport[0],
                   restoredViewport[1],
                   restoredViewport[2],
                   restoredViewport[3]);
    }
  }

  // Main rendering follows the shared world-shadow pass.
  const std::array<int, 2>& dims = frameContext.windowDimensions;
  pushFramebuffer(FramebufferHandle{});
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
        _currentPassViewport = { 0,
                                 0,
                                 frameContext.windowDimensions[0],
                                 frameContext.windowDimensions[1] };
        pushFramebuffer(_currentPassFbo);
        pushViewport(_currentPassViewport[0],
                     _currentPassViewport[1],
                     _currentPassViewport[2],
                     _currentPassViewport[3]);
        for (size_t i = 0; i < list.size(); ++i) {
          DrawableBase* drawable = list[i];
          if (!drawable) {
            continue;
          }
          if (!drawable->AppendCommands(this)) {
            if (m_strictSubmission) {
              reportFrameError(
                "Drawable did not emit a complete token submission");
              continue;
            }
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

          if (pass.clear.clearColor) {
            pushClearColor(pass.clear.clearColorValue[0],
                           pass.clear.clearColorValue[1],
                           pass.clear.clearColorValue[2],
                           pass.clear.clearColorValue[3]);
          }
          if (pass.clear.clearDepth) {
            pushClearDepth(pass.clear.clearDepthValue);
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
                if (m_strictSubmission) {
                  reportFrameError(
                    "Drawable did not emit a complete token submission");
                  continue;
                }
                if (immediateList != nullptr && immediateCount < immediateCap) {
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
  clearCommandQueue();

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
    1.0f, 1.0f, 0.0f,  1.0f, 0.0f, 0.0f,  1.0f,  1.0f, 1.0f, -1.0f, 0.0f,
    0.0f, 1.0f, 0.0f,  1.0f, 0.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,  1.0f,
    0.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  0.0f, 0.0f, 1.0f,
  };
  const unsigned int indices[6] = { 0, 1, 2, 0, 2, 3 };

  _proofMeshHandle = enrollMesh(verts, sizeof(verts), indices, sizeof(indices));

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

  clearCommandQueue();

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
  clearCommandQueue();
}
