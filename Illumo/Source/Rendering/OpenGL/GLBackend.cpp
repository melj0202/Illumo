#include "GLBackend.h"
#include "GLDevice.h"
#include "GLShaderProgram.h"
#include "GLTexture.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Services/Logger.h>
#include <cstring>
#include <string>
#include <tracy/Tracy.hpp>

GLBackend::GLBackend(IRenderWindow* window)
  : device(new GLDevice())
  , commandQueue(new CommandQueue())
  , window(window)
{
}

GLBackend::~GLBackend()
{
  Shutdown();
}

bool
GLBackend::Initialize()
{
  glewExperimental = true;
  const GLenum err = glewInit();
  if (GLEW_OK != err) {
    Logger::LogError("Failed to initialize glew");
    return false;
  }
  Logger::LogTrace("Glew initialized");
  glEnable(GL_MULTISAMPLE);
  const GLubyte* versionGL = glGetString(GL_VERSION);
  std::string versionStr =
    versionGL ? reinterpret_cast<const char*>(versionGL) : "Unknown";
  std::string fullGLString = "OpenGL Context: " + versionStr;
  Logger::LogInfo(fullGLString.c_str());
  return true;
}

void
GLBackend::BeginFrame()
{
}

void
GLBackend::EndFrame()
{
  {
    ZoneScopedN("GLBackend.swapBuffers");
    window->swapBuffers();
  }
  static long frameCount = 0;
  static double lastFpsTime = 0.0;
  frameCount++;
  double currentFpsTime = glfwGetTime();
  if (currentFpsTime - lastFpsTime >= 1.0) {
    this->fps =
      static_cast<int>(frameCount / (currentFpsTime - lastFpsTime) + 0.5);
    frameCount = 0;
    lastFpsTime = currentFpsTime;
  }
}

void
GLBackend::SubmitCommandQueue()
{
  ZoneScopedN("GLBackend.SubmitCommandQueue");
  GLResourceTables tables;
  tables.meshes = &_vaoRegistryLookup;
  tables.programs = &_programRegistryLookup;
  tables.textures = &_textureRegistryLookup;
  tables.framebuffers = &_framebufferRegistryLookup;
  device->ExecuteCommandQueue(*commandQueue, tables);
}

void
GLBackend::PushToCommandQueue(RenderCommand command)
{
  commandQueue->Submit(command);
}

void
GLBackend::ClearCommandQueue()
{
  commandQueue->Reset();
}

void
GLBackend::Shutdown()
{
  for (std::unordered_map<uint32_t, GLMeshResourceEntry>::iterator it =
         _vaoRegistryLookup.begin();
       it != _vaoRegistryLookup.end();
       ++it) {
    if (it->second.resource) {
      it->second.resource->Destroy();
    }
  }
  _vaoRegistryLookup.clear();
  meshHandles.clear();

  for (std::unordered_map<uint32_t, GLShaderResourceEntry>::iterator it =
         _programRegistryLookup.begin();
       it != _programRegistryLookup.end();
       ++it) {
    if (it->second.resource) {
      it->second.resource->Destroy();
    }
  }
  _programRegistryLookup.clear();
  shaderHandles.clear();

  for (std::unordered_map<uint32_t, GLTextureResourceEntry>::iterator it =
         _textureRegistryLookup.begin();
       it != _textureRegistryLookup.end();
       ++it) {
    if (it->second.resource) {
      it->second.resource->Destroy();
    }
  }
  _textureRegistryLookup.clear();
  textureHandles.clear();

  for (std::unordered_map<uint32_t, GLFramebufferResourceEntry>::iterator it =
         _framebufferRegistryLookup.begin();
       it != _framebufferRegistryLookup.end();
       ++it) {
    if (it->second.fboId != 0) {
      glDeleteFramebuffers(1, &it->second.fboId);
    }
  }
  _framebufferRegistryLookup.clear();
  framebufferHandles.clear();

  delete device;
  device = nullptr;
  delete commandQueue;
  commandQueue = nullptr;
}

MeshHandle
GLBackend::CreateMesh(const void* vertices,
                      size_t vertexSize,
                      const void* indices,
                      size_t indexSize)
{
  return CreateMesh(vertices,
                    vertexSize,
                    indices,
                    indexSize,
                    MeshVertexLayout::Pos3Color3Uv2,
                    false);
}

MeshHandle
GLBackend::CreateMesh(const void* vertices,
                      size_t vertexSize,
                      const void* indices,
                      size_t indexSize,
                      MeshVertexLayout layout,
                      bool dynamic)
{
  MeshHandle handle = meshHandles.allocate();
  GLMeshResourceEntry entry;
  entry.generation = handle.generation;
  entry.resource = std::make_unique<GLMesh>(
    vertices, vertexSize, indices, indexSize, layout, dynamic);
  _vaoRegistryLookup[handle.slot] = std::move(entry);
  return handle;
}

bool
GLBackend::ReplaceMesh(MeshHandle handle,
                       const void* vertices,
                       size_t vertexSize,
                       const void* indices,
                       size_t indexSize,
                       MeshVertexLayout layout,
                       bool dynamic)
{
  std::unordered_map<uint32_t, GLMeshResourceEntry>::iterator it =
    _vaoRegistryLookup.find(handle.slot);
  if (it == _vaoRegistryLookup.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("ReplaceMesh: stale mesh handle ignored");
    return false;
  }
  std::unique_ptr<GLMesh> replacement = std::make_unique<GLMesh>(
    vertices, vertexSize, indices, indexSize, layout, dynamic);
  if (it->second.resource) {
    it->second.resource->Destroy();
  }
  it->second.resource = std::move(replacement);
  return true;
}

bool
GLBackend::DestroyMesh(MeshHandle handle)
{
  std::unordered_map<uint32_t, GLMeshResourceEntry>::iterator it =
    _vaoRegistryLookup.find(handle.slot);
  if (it == _vaoRegistryLookup.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("DestroyMesh: stale mesh handle ignored");
    return false;
  }
  if (it->second.resource) {
    it->second.resource->Destroy();
  }
  _vaoRegistryLookup.erase(it);
  return meshHandles.release(handle);
}

bool
GLBackend::IsMeshValid(MeshHandle handle) const
{
  std::unordered_map<uint32_t, GLMeshResourceEntry>::const_iterator it =
    _vaoRegistryLookup.find(handle.slot);
  return meshHandles.isCurrent(handle) && it != _vaoRegistryLookup.end() &&
         it->second.generation == handle.generation;
}

ShaderHandle
GLBackend::CreateShaderProgram(const ShaderPaths& paths)
{
  ShaderHandle handle = shaderHandles.allocate();
  GLShaderResourceEntry entry;
  entry.generation = handle.generation;
  entry.resource = std::make_unique<GLShaderProgram>(paths);
  if (!entry.resource->isValid()) {
    shaderHandles.release(handle);
    return ShaderHandle{};
  }
  _programRegistryLookup[handle.slot] = std::move(entry);
  return handle;
}

ShaderHandle
GLBackend::CreateShaderProgram(const ShaderSources& sources)
{
  ShaderHandle handle = shaderHandles.allocate();
  GLShaderResourceEntry entry;
  entry.generation = handle.generation;
  entry.resource = std::make_unique<GLShaderProgram>(sources);
  if (!entry.resource->isValid()) {
    shaderHandles.release(handle);
    return ShaderHandle{};
  }
  _programRegistryLookup[handle.slot] = std::move(entry);
  return handle;
}

bool
GLBackend::ReplaceShaderProgram(ShaderHandle handle,
                                const ShaderSources& sources)
{
  std::unordered_map<uint32_t, GLShaderResourceEntry>::iterator it =
    _programRegistryLookup.find(handle.slot);
  if (it == _programRegistryLookup.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("ReplaceShaderProgram: stale shader handle ignored");
    return false;
  }
  std::unique_ptr<GLShaderProgram> replacement =
    std::make_unique<GLShaderProgram>(sources);
  if (!replacement->isValid()) {
    replacement->Destroy();
    return false;
  }
  if (it->second.resource) {
    it->second.resource->Destroy();
  }
  it->second.resource = std::move(replacement);
  return true;
}

bool
GLBackend::DestroyShaderProgram(ShaderHandle handle)
{
  std::unordered_map<uint32_t, GLShaderResourceEntry>::iterator it =
    _programRegistryLookup.find(handle.slot);
  if (it == _programRegistryLookup.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("DestroyShaderProgram: stale shader handle ignored");
    return false;
  }
  if (it->second.resource) {
    it->second.resource->Destroy();
  }
  _programRegistryLookup.erase(it);
  return shaderHandles.release(handle);
}

bool
GLBackend::IsShaderValid(ShaderHandle handle) const
{
  std::unordered_map<uint32_t, GLShaderResourceEntry>::const_iterator it =
    _programRegistryLookup.find(handle.slot);
  return shaderHandles.isCurrent(handle) &&
         it != _programRegistryLookup.end() &&
         it->second.generation == handle.generation && it->second.resource &&
         it->second.resource->isValid();
}

TextureHandle
GLBackend::CreateTexture(const unsigned char* data,
                         const int width,
                         const int height)
{
  TextureOptions options;
  return CreateTexture(data, width, height, 4, options);
}

TextureHandle
GLBackend::CreateTexture(const unsigned char* data,
                         const int width,
                         const int height,
                         int channels,
                         const TextureOptions& options)
{
  if (data == nullptr || width <= 0 || height <= 0) {
    return TextureHandle{};
  }
  TextureHandle handle = textureHandles.allocate();
  GLTextureResourceEntry entry;
  entry.generation = handle.generation;
  entry.resource =
    std::make_unique<GLTexture>(data, width, height, channels, options);
  _textureRegistryLookup[handle.slot] = std::move(entry);
  return handle;
}

bool
GLBackend::ReplaceTexture(TextureHandle handle,
                          const unsigned char* data,
                          int width,
                          int height,
                          int channels,
                          const TextureOptions& options)
{
  std::unordered_map<uint32_t, GLTextureResourceEntry>::iterator it =
    _textureRegistryLookup.find(handle.slot);
  if (it == _textureRegistryLookup.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("ReplaceTexture: stale texture handle ignored");
    return false;
  }
  if (data == nullptr || width <= 0 || height <= 0) {
    Logger::LogWarning("ReplaceTexture: invalid texture data ignored");
    return false;
  }
  std::unique_ptr<GLTexture> replacement =
    std::make_unique<GLTexture>(data, width, height, channels, options);
  if (it->second.resource) {
    it->second.resource->Destroy();
  }
  it->second.resource = std::move(replacement);
  return true;
}

bool
GLBackend::DestroyTexture(TextureHandle handle)
{
  std::unordered_map<uint32_t, GLTextureResourceEntry>::iterator it =
    _textureRegistryLookup.find(handle.slot);
  if (it == _textureRegistryLookup.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("DestroyTexture: stale texture handle ignored");
    return false;
  }
  if (it->second.resource) {
    it->second.resource->Destroy();
  }
  _textureRegistryLookup.erase(it);
  return textureHandles.release(handle);
}

bool
GLBackend::IsTextureValid(TextureHandle handle) const
{
  std::unordered_map<uint32_t, GLTextureResourceEntry>::const_iterator it =
    _textureRegistryLookup.find(handle.slot);
  return textureHandles.isCurrent(handle) &&
         it != _textureRegistryLookup.end() &&
         it->second.generation == handle.generation;
}

TextureInfo
GLBackend::GetTextureInfo(TextureHandle handle) const
{
  TextureInfo info;
  std::unordered_map<uint32_t, GLTextureResourceEntry>::const_iterator it =
    _textureRegistryLookup.find(handle.slot);
  if (!textureHandles.isCurrent(handle) || it == _textureRegistryLookup.end() ||
      it->second.generation != handle.generation || !it->second.resource) {
    return info;
  }
  const std::array<int, 2> size = it->second.resource->getSize();
  info.width = size[0];
  info.height = size[1];
  info.channels = it->second.resource->getChannels();
  return info;
}

FramebufferHandle
GLBackend::CreateDepthFramebuffer(int width,
                                  int height,
                                  TextureHandle* outDepthTexture)
{
  if (width <= 0 || height <= 0) {
    Logger::LogError("CreateDepthFramebuffer: invalid dimensions");
    return FramebufferHandle{};
  }

  std::unique_ptr<GLTexture> depthTex =
    GLTexture::CreateDepthTexture(width, height);
  if (!depthTex || depthTex->getID() == 0) {
    Logger::LogError("CreateDepthFramebuffer: failed to create depth texture");
    return FramebufferHandle{};
  }

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(
    GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex->getID(), 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (status != GL_FRAMEBUFFER_COMPLETE) {
    Logger::LogError("CreateDepthFramebuffer: framebuffer is incomplete (" +
                     std::to_string(status) + ")");
    glDeleteFramebuffers(1, &fbo);
    return FramebufferHandle{};
  }

  TextureHandle texHandle = textureHandles.allocate();
  GLTextureResourceEntry texEntry;
  texEntry.generation = texHandle.generation;
  texEntry.resource = std::move(depthTex);
  _textureRegistryLookup[texHandle.slot] = std::move(texEntry);

  if (outDepthTexture) {
    *outDepthTexture = texHandle;
  }

  FramebufferHandle fbHandle = framebufferHandles.allocate();
  GLFramebufferResourceEntry fbEntry;
  fbEntry.generation = fbHandle.generation;
  fbEntry.fboId = fbo;
  fbEntry.depthTexture = texHandle;
  fbEntry.width = width;
  fbEntry.height = height;
  _framebufferRegistryLookup[fbHandle.slot] = fbEntry;

  return fbHandle;
}

bool
GLBackend::DestroyFramebuffer(FramebufferHandle handle)
{
  std::unordered_map<uint32_t, GLFramebufferResourceEntry>::iterator it =
    _framebufferRegistryLookup.find(handle.slot);
  if (it == _framebufferRegistryLookup.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("DestroyFramebuffer: stale framebuffer handle ignored");
    return false;
  }
  if (it->second.fboId != 0) {
    glDeleteFramebuffers(1, &it->second.fboId);
  }
  if (it->second.depthTexture.isValid()) {
    DestroyTexture(it->second.depthTexture);
  }
  _framebufferRegistryLookup.erase(it);
  return framebufferHandles.release(handle);
}

bool
GLBackend::IsFramebufferValid(FramebufferHandle handle) const
{
  std::unordered_map<uint32_t, GLFramebufferResourceEntry>::const_iterator it =
    _framebufferRegistryLookup.find(handle.slot);
  return framebufferHandles.isCurrent(handle) &&
         it != _framebufferRegistryLookup.end() &&
         it->second.generation == handle.generation;
}
