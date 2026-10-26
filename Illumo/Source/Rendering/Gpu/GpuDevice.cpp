#include "GpuDevice.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <utility>

void
GpuDevice::releaseResources()
{
  for (Entry<GpuMesh>& entry : m_meshes) {
    if (entry.resource) {
      releaseMesh(*entry.resource);
    }
  }
  for (Entry<GpuProgram>& entry : m_programs) {
    if (entry.resource) {
      releaseProgram(*entry.resource);
    }
  }
  for (Entry<GpuTexture>& entry : m_textures) {
    if (entry.resource) {
      releaseTexture(*entry.resource);
    }
  }
  for (Entry<GpuBuffer>& entry : m_buffers) {
    if (entry.resource) {
      releaseBuffer(*entry.resource);
    }
  }
  m_meshes.clear();
  m_programs.clear();
  m_textures.clear();
  m_framebuffers.clear();
  m_buffers.clear();
  m_meshHandles.clear();
  m_shaderHandles.clear();
  m_textureHandles.clear();
  m_framebufferHandles.clear();
  m_bufferHandles.clear();
}

std::unique_ptr<GpuMesh>
GpuDevice::makeMesh(const void* vertices,
                    size_t vertexSize,
                    const void* indices,
                    size_t indexSize,
                    MeshVertexLayout layout,
                    bool dynamic)
{
  if (vertexSize == 0 || (vertices == nullptr && !dynamic)) {
    return nullptr;
  }
  std::unique_ptr<GpuMesh> mesh = buildMesh(vertexSize, indexSize);
  if (!mesh) {
    return nullptr;
  }
  mesh->layout = layout;
  mesh->dynamic = dynamic;
  mesh->vertexCapacity = vertexSize;
  mesh->indexCapacity = indexSize;
  if (indexSize > 0) {
    mesh->indexCount = static_cast<unsigned>(indexSize / sizeof(unsigned int));
  }
  if (vertices != nullptr) {
    uploadMeshData(*mesh, false, 0, vertices, vertexSize);
  }
  if (indices != nullptr && indexSize > 0) {
    if (indexSize % sizeof(unsigned int) == 0) {
      uploadMeshData(*mesh, true, 0, indices, indexSize);
    } else {
      // GLMesh widens byte indices, then uploads the enrolled byte count.
      std::vector<unsigned int> widened(indexSize);
      const unsigned char* bytes = static_cast<const unsigned char*>(indices);
      for (size_t index = 0; index < indexSize; ++index) {
        widened[index] = bytes[index];
      }
      uploadMeshData(*mesh, true, 0, widened.data(), indexSize);
    }
  }
  return mesh;
}

MeshHandle
GpuDevice::createMesh(const void* vertices,
                      size_t vertexSize,
                      const void* indices,
                      size_t indexSize,
                      MeshVertexLayout layout,
                      bool dynamic)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.createMesh");
  std::unique_ptr<GpuMesh> mesh =
    makeMesh(vertices, vertexSize, indices, indexSize, layout, dynamic);
  if (!mesh) {
    Logger::LogWarning("CreateMesh: the GPU mesh could not be created (" +
                       std::to_string(vertexSize) + " vertex bytes, " +
                       std::to_string(indexSize) + " index bytes)");
    return {};
  }
  const MeshHandle handle = m_meshHandles.allocate();
  Entry<GpuMesh>& entry = slotEntry(m_meshes, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(mesh);
  return handle;
}

bool
GpuDevice::replaceMesh(MeshHandle handle,
                       const void* vertices,
                       size_t vertexSize,
                       const void* indices,
                       size_t indexSize,
                       MeshVertexLayout layout,
                       bool dynamic)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.replaceMesh");
  GpuMesh* current = findMesh(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceMesh: stale mesh handle ignored");
    return false;
  }
  std::unique_ptr<GpuMesh> replacement =
    makeMesh(vertices, vertexSize, indices, indexSize, layout, dynamic);
  if (!replacement) {
    Logger::LogWarning(
      "ReplaceMesh: the replacement mesh is invalid; keeping the old one");
    return false;
  }
  releaseMesh(*current);
  m_meshes[handle.slot].resource = std::move(replacement);
  return true;
}

bool
GpuDevice::destroyMesh(MeshHandle handle)
{
  GpuMesh* mesh = findMesh(handle);
  if (mesh == nullptr) {
    Logger::LogWarning("DestroyMesh: stale mesh handle ignored");
    return false;
  }
  releaseMesh(*mesh);
  m_meshes[handle.slot] = Entry<GpuMesh>{};
  return m_meshHandles.release(handle);
}

bool
GpuDevice::isMeshValid(MeshHandle handle) const
{
  return m_meshHandles.isCurrent(handle) && findMesh(handle) != nullptr;
}

ShaderHandle
GpuDevice::createShader(const ShaderSources& sources, std::string* error)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.createShader");
  std::unique_ptr<GpuProgram> program = buildProgram(sources, error);
  if (!program) {
    return {};
  }
  const ShaderHandle handle = m_shaderHandles.allocate();
  Entry<GpuProgram>& entry = slotEntry(m_programs, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(program);
  return handle;
}

bool
GpuDevice::replaceShader(ShaderHandle handle,
                         const ShaderSources& sources,
                         std::string* error)
{
  GpuProgram* current = findProgram(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceShaderProgram: stale shader handle ignored");
    return false;
  }
  std::unique_ptr<GpuProgram> replacement = buildProgram(sources, error);
  if (!replacement) {
    return false;
  }
  releaseProgram(*current);
  m_programs[handle.slot].resource = std::move(replacement);
  return true;
}

bool
GpuDevice::destroyShader(ShaderHandle handle)
{
  GpuProgram* program = findProgram(handle);
  if (program == nullptr) {
    Logger::LogWarning("DestroyShaderProgram: stale shader handle ignored");
    return false;
  }
  releaseProgram(*program);
  m_programs[handle.slot] = Entry<GpuProgram>{};
  return m_shaderHandles.release(handle);
}

bool
GpuDevice::isShaderValid(ShaderHandle handle) const
{
  return m_shaderHandles.isCurrent(handle) && findProgram(handle) != nullptr;
}

TextureHandle
GpuDevice::registerTexture(std::unique_ptr<GpuTexture> texture,
                           int width,
                           int height)
{
  texture->width = width;
  texture->height = height;
  const TextureHandle handle = m_textureHandles.allocate();
  Entry<GpuTexture>& entry = slotEntry(m_textures, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(texture);
  return handle;
}

TextureHandle
GpuDevice::createTexture(const unsigned char* data,
                         int width,
                         int height,
                         int channels,
                         const TextureOptions& options)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.createTexture");
  if (data == nullptr || width <= 0 || height <= 0) {
    Logger::LogWarning("CreateTexture: missing pixels or invalid size " +
                       std::to_string(width) + "x" + std::to_string(height));
    return {};
  }
  std::unique_ptr<GpuTexture> texture =
    buildTexture(data, width, height, channels, options);
  if (!texture) {
    Logger::LogWarning("CreateTexture: the GPU texture could not be created (" +
                       std::to_string(width) + "x" + std::to_string(height) +
                       ", " + std::to_string(channels) + " channels)");
    return {};
  }
  const TextureHandle handle =
    registerTexture(std::move(texture), width, height);
  // OpenGL leaves a new 2D texture bound to unit 0.
  m_units[0].texture2D = handle;
  return handle;
}

TextureHandle
GpuDevice::createCubemap(const std::array<const unsigned char*, 6>& faces,
                         int width,
                         int height,
                         int channels)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.createCubemap");
  for (size_t index = 0; index < faces.size(); ++index) {
    if (faces[index] == nullptr) {
      Logger::LogWarning("CreateCubemap: face " + std::to_string(index) +
                         " has no pixels");
      return {};
    }
  }
  if (width <= 0 || height <= 0 || width != height ||
      (channels != 1 && channels != 3 && channels != 4)) {
    Logger::LogWarning("CreateCubemap: faces must be square with 1, 3 or 4 "
                       "channels (got " +
                       std::to_string(width) + "x" + std::to_string(height) +
                       ", " + std::to_string(channels) + " channels)");
    return {};
  }
  std::unique_ptr<GpuTexture> texture =
    buildCubemap(faces, width, height, channels);
  if (!texture) {
    Logger::LogWarning("CreateCubemap: the GPU cubemap could not be created");
    return {};
  }
  return registerTexture(std::move(texture), width, height);
}

bool
GpuDevice::replaceTexture(TextureHandle handle,
                          const unsigned char* data,
                          int width,
                          int height,
                          int channels,
                          const TextureOptions& options)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.replaceTexture");
  GpuTexture* current = findTexture(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceTexture: stale texture handle ignored");
    return false;
  }
  if (current->cubemap || data == nullptr || width <= 0 || height <= 0) {
    Logger::LogWarning("ReplaceTexture: invalid texture data ignored");
    return false;
  }
  std::unique_ptr<GpuTexture> replacement =
    buildTexture(data, width, height, channels, options);
  if (!replacement) {
    return false;
  }
  replacement->width = width;
  replacement->height = height;
  releaseTexture(*current);
  m_textures[handle.slot].resource = std::move(replacement);
  m_units[0].texture2D = handle;
  return true;
}

bool
GpuDevice::replaceCubemap(TextureHandle handle,
                          const std::array<const unsigned char*, 6>& faces,
                          int width,
                          int height,
                          int channels)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.replaceCubemap");
  GpuTexture* current = findTexture(handle);
  if (current == nullptr || !current->cubemap || width <= 0 ||
      width != height || (channels != 1 && channels != 3 && channels != 4)) {
    return false;
  }
  std::unique_ptr<GpuTexture> replacement =
    buildCubemap(faces, width, height, channels);
  if (!replacement) {
    return false;
  }
  replacement->width = width;
  replacement->height = height;
  releaseTexture(*current);
  m_textures[handle.slot].resource = std::move(replacement);
  return true;
}

bool
GpuDevice::destroyTexture(TextureHandle handle)
{
  GpuTexture* texture = findTexture(handle);
  if (texture == nullptr) {
    Logger::LogWarning("DestroyTexture: stale texture handle ignored");
    return false;
  }
  releaseTexture(*texture);
  m_textures[handle.slot] = Entry<GpuTexture>{};
  return m_textureHandles.release(handle);
}

bool
GpuDevice::isTextureValid(TextureHandle handle) const
{
  return m_textureHandles.isCurrent(handle) && findTexture(handle) != nullptr;
}

TextureInfo
GpuDevice::textureInfo(TextureHandle handle) const
{
  TextureInfo info;
  const GpuTexture* texture = findTexture(handle);
  if (texture == nullptr) {
    return info;
  }
  info.width = texture->width;
  info.height = texture->height;
  info.channels = texture->channels;
  return info;
}

FramebufferHandle
GpuDevice::createFramebuffer(const FramebufferDesc& desc,
                             FramebufferAttachments* outAttachments)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.createFramebuffer");
  if (desc.width <= 0 || desc.height <= 0) {
    Logger::LogError("CreateFramebuffer: invalid dimensions");
    return {};
  }
  if (desc.colorAttachments.size() > maxColorAttachments() ||
      (desc.colorAttachments.empty() &&
       desc.depthStencilFormat == TextureFormat::None)) {
    Logger::LogError("CreateFramebuffer: framebuffer is incomplete");
    return {};
  }
  std::unique_ptr<GpuFramebuffer> framebuffer =
    std::make_unique<GpuFramebuffer>();
  framebuffer->width = desc.width;
  framebuffer->height = desc.height;
  bool failed = false;
  for (size_t index = 0; index < desc.colorAttachments.size(); ++index) {
    const FramebufferAttachmentDesc& attachment = desc.colorAttachments[index];
    const bool colorFormat =
      attachment.format != TextureFormat::Depth24 &&
      attachment.format != TextureFormat::Depth24Stencil8 &&
      attachment.format != TextureFormat::None;
    std::unique_ptr<GpuTexture> texture =
      colorFormat ? buildRenderTarget(desc.width,
                                      desc.height,
                                      attachment.format,
                                      attachment.filter,
                                      attachment.wrap)
                  : nullptr;
    if (!texture) {
      Logger::LogError(
        "CreateFramebuffer: failed to create color texture attachment " +
        std::to_string(index));
      failed = true;
      break;
    }
    framebuffer->colorTextures.push_back(
      registerTexture(std::move(texture), desc.width, desc.height));
  }
  if (!failed && desc.depthStencilFormat != TextureFormat::None) {
    const bool depthFormat =
      desc.depthStencilFormat == TextureFormat::Depth24 ||
      desc.depthStencilFormat == TextureFormat::Depth24Stencil8;
    std::unique_ptr<GpuTexture> texture =
      depthFormat ? buildRenderTarget(desc.width,
                                      desc.height,
                                      desc.depthStencilFormat,
                                      desc.depthFilter,
                                      TextureWrap::ClampToEdge)
                  : nullptr;
    if (!texture) {
      Logger::LogError(
        "CreateFramebuffer: failed to create depth texture attachment");
      failed = true;
    } else {
      framebuffer->depthTexture =
        registerTexture(std::move(texture), desc.width, desc.height);
    }
  }
  if (failed) {
    for (TextureHandle texture : framebuffer->colorTextures) {
      destroyTexture(texture);
    }
    if (framebuffer->depthTexture.isValid()) {
      destroyTexture(framebuffer->depthTexture);
    }
    return {};
  }
  if (outAttachments != nullptr) {
    outAttachments->colorTextures = framebuffer->colorTextures;
    outAttachments->depthStencilTexture = framebuffer->depthTexture;
  }
  const FramebufferHandle handle = m_framebufferHandles.allocate();
  Entry<GpuFramebuffer>& entry = slotEntry(m_framebuffers, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(framebuffer);
  // OpenGL creation leaves the default framebuffer bound and texture unit
  // 0's 2D target empty.
  m_framebuffer = FramebufferHandle{};
  m_units[0].texture2D = TextureHandle{};
  return handle;
}

bool
GpuDevice::destroyFramebuffer(FramebufferHandle handle)
{
  GpuFramebuffer* framebuffer = resolveFramebuffer(handle);
  if (framebuffer == nullptr) {
    Logger::LogWarning("DestroyFramebuffer: stale framebuffer handle ignored");
    return false;
  }
  releaseFramebufferTarget(handle);
  if (m_framebuffer == handle) {
    m_framebuffer = FramebufferHandle{};
  }
  const std::vector<TextureHandle> colors = framebuffer->colorTextures;
  const TextureHandle depth = framebuffer->depthTexture;
  for (TextureHandle texture : colors) {
    if (texture.isValid()) {
      destroyTexture(texture);
    }
  }
  if (depth.isValid()) {
    destroyTexture(depth);
  }
  m_framebuffers[handle.slot] = Entry<GpuFramebuffer>{};
  return m_framebufferHandles.release(handle);
}

bool
GpuDevice::isFramebufferValid(FramebufferHandle handle) const
{
  return m_framebufferHandles.isCurrent(handle) &&
         resolveFramebuffer(handle) != nullptr;
}

BufferHandle
GpuDevice::createBuffer(BufferUsage usage, size_t capacityBytes)
{
  if (capacityBytes == 0) {
    return {};
  }
  std::unique_ptr<GpuBuffer> buffer = buildBuffer(capacityBytes);
  if (!buffer) {
    return {};
  }
  buffer->usage = usage;
  buffer->capacity = capacityBytes;
  const BufferHandle handle = m_bufferHandles.allocate();
  Entry<GpuBuffer>& entry = slotEntry(m_buffers, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(buffer);
  return handle;
}

bool
GpuDevice::destroyBuffer(BufferHandle handle)
{
  GpuBuffer* buffer = findBuffer(handle);
  if (buffer == nullptr) {
    Logger::LogWarning("DestroyBuffer: stale buffer handle ignored");
    return false;
  }
  releaseBuffer(*buffer);
  m_buffers[handle.slot] = Entry<GpuBuffer>{};
  return m_bufferHandles.release(handle);
}

bool
GpuDevice::isBufferValid(BufferHandle handle) const
{
  return m_bufferHandles.isCurrent(handle) && findBuffer(handle) != nullptr;
}

GpuMesh*
GpuDevice::findMesh(MeshHandle handle) const
{
  return slotResource(m_meshes, handle.slot, handle.generation);
}

GpuProgram*
GpuDevice::findProgram(ShaderHandle handle) const
{
  return slotResource(m_programs, handle.slot, handle.generation);
}

GpuTexture*
GpuDevice::findTexture(TextureHandle handle) const
{
  return slotResource(m_textures, handle.slot, handle.generation);
}

GpuFramebuffer*
GpuDevice::resolveFramebuffer(FramebufferHandle handle) const
{
  return slotResource(m_framebuffers, handle.slot, handle.generation);
}

GpuBuffer*
GpuDevice::findBuffer(BufferHandle handle) const
{
  return slotResource(m_buffers, handle.slot, handle.generation);
}
