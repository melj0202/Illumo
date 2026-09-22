#include <IllumoGuest/RecordingBackend.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

GuestRecordingBackend::GuestRecordingBackend(GuestServiceQueue& services,
                                             std::size_t commandCeiling)
  : m_services(services)
  , m_commands(commandCeiling)
{
  m_retirements.reserve(4096);
  m_releases.reserve(GuestServices::MaximumRecords);
}
GuestRecordingBackend::~GuestRecordingBackend() = default;
void
GuestRecordingBackend::setRenderer(Renderer& renderer)
{
  m_renderer = &renderer;
}
void
GuestRecordingBackend::setFrame(float width, float height)
{
  m_frame.width = width;
  m_frame.height = height;
}
void
GuestRecordingBackend::setLayer(GuestLayer layer)
{
  m_layer = layer;
}
void
GuestRecordingBackend::BeginLayer(RenderLayerId layer)
{
  m_layer = layer == RenderLayerId::World ? GuestLayer::World : GuestLayer::Ui;
  if (layer != RenderLayerId::World || m_renderer == nullptr) {
    return;
  }
  // The world camera and this frame's casters are final once RenderScene
  // reaches the world layer; the host fits its shared shadow pass to them.
  const Renderer::FrameContext& context = m_renderer->getFrameContext();
  m_frame.hasCamera = context.active && context.hasWorldMvp;
  m_frame.camera = context.worldMvp;
  m_frame.shadowCasters.clear();
  for (const Renderer::ShadowCasterDesc& source :
       m_renderer->getShadowCasters()) {
    if (m_frame.shadowCasters.size() == GuestFrameLimits{}.shadowCasters) {
      break;
    }
    GuestShadowCaster caster;
    caster.boundsMin = source.boundsMin;
    caster.boundsMax = source.boundsMax;
    caster.lightDirection = source.lightDirection;
    caster.mapSize =
      static_cast<std::uint32_t>(std::clamp(source.mapSize, 64, 8192));
    caster.minimumRadius = source.minimumRadius;
    caster.lightDistance = source.lightDistance;
    caster.casterDistance = source.casterDistance;
    m_frame.shadowCasters.push_back(caster);
  }
}
bool
GuestRecordingBackend::hasPendingTextures() const
{
  for (const std::pair<const std::uint32_t, Texture>& entry : m_textures) {
    if (entry.second.pending != 0) {
      return true;
    }
  }
  return false;
}
bool
GuestRecordingBackend::Initialize()
{
  return true;
}
void
GuestRecordingBackend::Shutdown()
{
  // The host revokes the complete owner table on retirement. Shutdown must
  // never enqueue work or throw through Renderer destruction.
  m_textures.clear();
  m_meshes.clear();
  m_commands.Reset();
  m_commandLayers.clear();
}
void
GuestRecordingBackend::BeginFrame()
{
  m_frame.batches.clear();
  m_frame.textureWrites.clear();
  m_frame.shadowCasters.clear();
  m_frame.hasCamera = false;
  m_shadowPass = false;
  m_shadowMeshes.clear();
  m_lighting = GuestLighting{};
  m_error.clear();
  m_frameRejections = m_commands.GetTotalRejected();
  m_mesh = {};
  m_shader = {};
  m_texture = {};
  m_clip = {};
  m_pipeline = {};
  m_mvp = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}
void
GuestRecordingBackend::EndFrame()
{
}
void
GuestRecordingBackend::PushToCommandQueue(RenderCommand command)
{
  const std::size_t before = m_commands.GetCommandCount();
  m_commands.Submit(command);
  if (m_commands.GetCommandCount() != before) {
    m_commandLayers.push_back(m_layer);
  }
}
void
GuestRecordingBackend::ClearCommandQueue()
{
  m_commands.Reset();
  m_commandLayers.clear();
}
std::size_t
GuestRecordingBackend::rejectedCommandCount() const
{
  return m_commands.GetTotalRejected();
}
std::size_t
GuestRecordingBackend::commandHighWaterMark() const
{
  return m_commands.GetHighWaterMark();
}
std::string
GuestRecordingBackend::submissionError() const
{
  return m_error;
}
int
GuestRecordingBackend::getFPS() const
{
  return 0;
}
void
GuestRecordingBackend::SubmitCommandQueue()
{
  const GuestLayer current = m_layer;
  try {
    if (m_commandLayers.size() != m_commands.GetCommandCount()) {
      throw std::runtime_error("Guest command layer journal mismatch");
    }
    for (std::size_t index = 0; index < m_commands.GetCommandCount(); ++index) {
      m_layer = m_commandLayers[index];
      consume(m_commands.GetCommand(index));
    }
  } catch (const std::exception& exception) {
    m_error = exception.what();
  }
  m_layer = current;
}
GuestFrame
GuestRecordingBackend::takeFrame()
{
  if (m_commands.GetTotalRejected() != m_frameRejections ||
      (m_renderer && !m_renderer->frameError().empty())) {
    throw std::runtime_error("Guest frame submission failed");
  }
  if (!m_error.empty()) {
    throw std::runtime_error(m_error);
  }
  GuestFrame result = std::move(m_frame);
  m_frame = GuestFrame{};
  return result;
}
MeshHandle
GuestRecordingBackend::CreateMesh(const void* vertices,
                                  std::size_t vertexBytes,
                                  const void* indices,
                                  std::size_t indexBytes)
{
  return CreateMesh(vertices,
                    vertexBytes,
                    indices,
                    indexBytes,
                    MeshVertexLayout::Pos3Color3Uv2,
                    false);
}
MeshHandle
GuestRecordingBackend::CreateMesh(const void* vertices,
                                  std::size_t vertexBytes,
                                  const void* indices,
                                  std::size_t indexBytes,
                                  MeshVertexLayout layout,
                                  bool dynamic)
{
  if (m_meshes.size() >= 4096) {
    return {};
  }
  const MeshHandle handle = m_meshHandles.allocate();
  m_meshes.emplace(handle.slot, Mesh{});
  if (!ReplaceMesh(
        handle, vertices, vertexBytes, indices, indexBytes, layout, dynamic)) {
    DestroyMesh(handle);
    return {};
  }
  return handle;
}
// The retained-mesh style for a vertex layout; 0 when a layout has no
// retained form (skybox cubes and other small built-in geometry).
static std::uint32_t
retainedStyle(MeshVertexLayout layout)
{
  switch (layout) {
    case MeshVertexLayout::Pos3Color4U8:
      return static_cast<std::uint32_t>(GuestBatchStyle::Shape);
    case MeshVertexLayout::Pos3Color4U8Uv2:
      return static_cast<std::uint32_t>(GuestBatchStyle::Sprite);
    case MeshVertexLayout::Pos3Color3Uv2:
      return static_cast<std::uint32_t>(GuestBatchStyle::Canvas);
    case MeshVertexLayout::Pos3Norm3Color4U8Uv2:
      return static_cast<std::uint32_t>(GuestBatchStyle::LitMesh);
    default:
      return 0;
  }
}
bool
GuestRecordingBackend::ReplaceMesh(MeshHandle handle,
                                   const void* vertices,
                                   std::size_t vertexBytes,
                                   const void* indices,
                                   std::size_t indexBytes,
                                   MeshVertexLayout layout,
                                   bool dynamic)
{
  if (!IsMeshValid(handle) ||
      vertexBytes > GuestMeshRequest::MaximumVertexBytes ||
      indexBytes > GuestMeshRequest::MaximumIndexBytes) {
    return false;
  }
  Mesh candidate;
  candidate.layout = layout;
  candidate.vertices.resize(vertexBytes);
  candidate.indices.resize(indexBytes);
  if (vertices != nullptr) {
    std::memcpy(candidate.vertices.data(), vertices, vertexBytes);
  }
  if (indices != nullptr) {
    std::memcpy(candidate.indices.data(), indices, indexBytes);
  }
  const std::uint32_t style = retainedStyle(layout);
  // Large immutable geometry (loaded models) is uploaded to the host once.
  candidate.retain =
    !dynamic && style != 0 && vertices != nullptr && indices != nullptr &&
    vertexBytes >= RetainedMeshBytes && indexBytes > 0 &&
    vertexBytes % GuestMeshRequest::stride(style) == 0 && indexBytes % 4 == 0;
  Mesh& target = m_meshes.at(handle.slot);
  forgetRetained(target);
  target = std::move(candidate);
  return true;
}
void
GuestRecordingBackend::forgetRetained(Mesh& mesh)
{
  if (mesh.id.owner != 0) {
    m_meshRetirements.push_back(mesh.id);
  }
  if (mesh.create != 0) {
    m_abandonedMeshes.push_back(mesh.create);
  }
  m_drained.insert(m_drained.end(), mesh.writes.begin(), mesh.writes.end());
  mesh.retain = false;
  mesh.id = {};
  mesh.create = 0;
  mesh.writes.clear();
  mesh.vertexSent = 0;
  mesh.indexSent = 0;
  mesh.ready = false;
  mesh.failed = false;
}
bool
GuestRecordingBackend::DestroyMesh(MeshHandle handle)
{
  if (!IsMeshValid(handle)) {
    return false;
  }
  forgetRetained(m_meshes.at(handle.slot));
  m_meshes.erase(handle.slot);
  return m_meshHandles.release(handle);
}
bool
GuestRecordingBackend::IsMeshValid(MeshHandle handle) const
{
  return m_meshHandles.isCurrent(handle) && m_meshes.contains(handle.slot);
}
ShaderHandle
GuestRecordingBackend::CreateShaderProgram(const ShaderPaths&)
{
  return m_shaderHandles.allocate();
}
ShaderHandle
GuestRecordingBackend::CreateShaderProgram(const ShaderSources&)
{
  return m_shaderHandles.allocate();
}
bool
GuestRecordingBackend::ReplaceShaderProgram(ShaderHandle, const ShaderSources&)
{
  return false;
}
bool
GuestRecordingBackend::DestroyShaderProgram(ShaderHandle handle)
{
  return m_shaderHandles.release(handle);
}
bool
GuestRecordingBackend::IsShaderValid(ShaderHandle handle) const
{
  return m_shaderHandles.isCurrent(handle);
}
TextureHandle
GuestRecordingBackend::CreateTexture(const unsigned char* pixels,
                                     int width,
                                     int height)
{
  return CreateTexture(pixels, width, height, 3, {});
}
TextureHandle
GuestRecordingBackend::CreateTexture(const unsigned char* pixels,
                                     int width,
                                     int height,
                                     int channels,
                                     const TextureOptions& options)
{
  if (m_textures.size() >= 1024) {
    return {};
  }
  const TextureHandle handle = m_textureHandles.allocate();
  m_textures.emplace(handle.slot, Texture{});
  if (!ReplaceTexture(handle, pixels, width, height, channels, options)) {
    DestroyTexture(handle);
    return {};
  }
  return handle;
}
TextureHandle
GuestRecordingBackend::CreateCubemap(
  const std::array<const unsigned char*, 6>& faces,
  int width,
  int height,
  int channels)
{
  if (m_textures.size() >= 1024 || width <= 0 || width != height ||
      width > 2048 || (channels != 3 && channels != 4)) {
    return {};
  }
  GuestCubemapRequest request;
  request.size = static_cast<std::uint32_t>(width);
  const std::uint64_t bytes = GuestCubemapRequest::bytesFor(request.size);
  if (bytes > GuestServices::MaximumBytes - 64) {
    return {};
  }
  // The host samples RGBA faces; RGB sources gain an opaque alpha.
  request.faces.resize(static_cast<std::size_t>(bytes));
  const std::size_t pixels = static_cast<std::size_t>(width) * height;
  for (std::size_t face = 0; face < faces.size(); ++face) {
    if (faces[face] == nullptr) {
      return {};
    }
    std::byte* output = request.faces.data() + face * pixels * 4;
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
      for (std::size_t component = 0; component < 4; ++component) {
        output[pixel * 4 + component] =
          component < static_cast<std::size_t>(channels)
            ? static_cast<std::byte>(faces[face][pixel * channels + component])
            : std::byte{ 255 };
      }
    }
  }
  GuestWireWriter payload;
  request.write(payload);
  const std::uint64_t pending =
    m_services.enqueue(GuestService::CreateCubemap, payload.take());
  if (pending == 0) {
    return {};
  }
  const TextureHandle handle = m_textureHandles.allocate();
  Texture texture;
  texture.cubemap = true;
  texture.pending = pending;
  texture.pendingInfo = { width, height, 4 };
  m_textures.emplace(handle.slot, std::move(texture));
  return handle;
}
bool
GuestRecordingBackend::ReplaceTexture(TextureHandle handle,
                                      const unsigned char* pixels,
                                      int width,
                                      int height,
                                      int channels,
                                      const TextureOptions& options)
{
  if (!IsTextureValid(handle) || m_textures.at(handle.slot).cubemap ||
      width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
      (channels != 1 && channels != 3 && channels != 4) ||
      options.wrapX != TextureWrap::ClampToEdge ||
      options.wrapY != TextureWrap::ClampToEdge || options.generateMipmaps) {
    return false;
  }
  const std::size_t bytes = static_cast<std::size_t>(width) * height * channels;
  if (bytes > GuestServices::MaximumBytes - 64) {
    return false;
  }
  GuestTextureRequest request;
  request.width = width;
  request.height = height;
  request.channels = channels;
  request.linear = options.filter == TextureFilter::Linear;
  request.pixels.resize(bytes);
  if (pixels != nullptr) {
    std::memcpy(request.pixels.data(), pixels, bytes);
  }
  GuestWireWriter payload;
  request.write(payload);
  const std::uint64_t pending =
    m_services.enqueue(GuestService::CreateTexture, payload.take());
  if (pending == 0) {
    return false;
  }
  Texture& texture = m_textures.at(handle.slot);
  if (texture.pending != 0) {
    // A newer replacement supersedes an unfinished one; the superseded
    // acquisition is released when its completion arrives.
    m_abandoned.push_back(texture.pending);
  }
  texture.pending = pending;
  texture.pendingInfo = { width, height, channels };
  texture.pendingPixels = std::move(request.pixels);
  texture.changed = false;
  return true;
}
void
GuestRecordingBackend::release(GuestResourceId id)
{
  if (id.owner == 0) {
    return;
  }
  if (m_retirements.size() == 4096) {
    throw std::runtime_error("Guest retirement quota exceeded");
  }
  m_retirements.push_back(id);
}
bool
GuestRecordingBackend::DestroyTexture(TextureHandle handle)
{
  if (!IsTextureValid(handle)) {
    return false;
  }
  const Texture& texture = m_textures.at(handle.slot);
  if (texture.pending != 0) {
    m_abandoned.push_back(texture.pending);
  }
  release(texture.id);
  m_textures.erase(handle.slot);
  return m_textureHandles.release(handle);
}
bool
GuestRecordingBackend::IsTextureValid(TextureHandle handle) const
{
  return m_textureHandles.isCurrent(handle) && m_textures.contains(handle.slot);
}
TextureInfo
GuestRecordingBackend::GetTextureInfo(TextureHandle handle) const
{
  if (!IsTextureValid(handle)) {
    return {};
  }
  const Texture& texture = m_textures.at(handle.slot);
  return texture.pending != 0 ? texture.pendingInfo : texture.info;
}
TextureHandle
GuestRecordingBackend::importTexture(GuestResourceId id)
{
  if (id.owner == 0 || id.kind != GuestResourceKind::Texture ||
      m_textures.size() >= 1024) {
    return {};
  }
  const TextureHandle handle = m_textureHandles.allocate();
  Texture texture;
  texture.id = id;
  m_textures.emplace(handle.slot, std::move(texture));
  return handle;
}
void
GuestRecordingBackend::pump()
{
  GuestServiceRecord result;
  for (std::vector<std::uint64_t>::iterator it = m_releases.begin();
       it != m_releases.end();) {
    if (m_services.take(*it, result)) {
      it = m_releases.erase(it);
    } else {
      ++it;
    }
  }
  for (std::vector<std::uint64_t>::iterator it = m_abandoned.begin();
       it != m_abandoned.end();) {
    if (m_services.take(*it, result)) {
      if (result.status == GuestServiceStatus::Complete) {
        GuestWireReader reader(result.payload);
        release(GuestResourceId::read(reader));
      }
      it = m_abandoned.erase(it);
    } else {
      ++it;
    }
  }
  for (std::pair<const std::uint32_t, Texture>& entry : m_textures) {
    Texture& texture = entry.second;
    if (texture.pending != 0 && m_services.take(texture.pending, result)) {
      texture.pending = 0;
      if (result.status != GuestServiceStatus::Complete) {
        texture.pendingPixels.clear();
        texture.changed = false;
        if (texture.id.owner == 0) {
          throw std::runtime_error("Guest texture acquisition failed");
        }
        continue;
      }
      GuestWireReader reader(result.payload);
      const GuestResourceId replacement = GuestResourceId::read(reader);
      if (!reader.finished() || replacement.owner == 0 ||
          replacement.kind != GuestResourceKind::Texture) {
        throw std::runtime_error("Invalid texture completion");
      }
      release(texture.id);
      texture.id = replacement;
      texture.info = texture.pendingInfo;
      texture.pixels = std::move(texture.pendingPixels);
    }
    if (texture.id.owner != 0 && texture.changed) {
      m_frame.textureWrites.push_back(
        { texture.id,
          0,
          0,
          static_cast<std::uint32_t>(texture.info.width),
          static_cast<std::uint32_t>(texture.info.height),
          static_cast<std::uint32_t>(texture.info.channels),
          texture.pixels,
          static_cast<std::uint32_t>(m_frame.batches.size()) });
      texture.changed = false;
    }
  }
  while (!m_retirements.empty()) {
    GuestWireWriter payload;
    m_retirements.back().write(payload);
    const std::uint64_t request =
      m_services.enqueue(GuestService::ReleaseTexture, payload.take());
    if (request == 0) {
      break;
    }
    m_releases.push_back(request);
    m_retirements.pop_back();
  }
  pumpMeshes();
}

void
GuestRecordingBackend::pumpMeshes()
{
  GuestServiceRecord result;
  for (std::vector<std::uint64_t>::iterator it = m_drained.begin();
       it != m_drained.end();) {
    it = m_services.take(*it, result) ? m_drained.erase(it) : it + 1;
  }
  for (std::vector<std::uint64_t>::iterator it = m_abandonedMeshes.begin();
       it != m_abandonedMeshes.end();) {
    if (!m_services.take(*it, result)) {
      ++it;
      continue;
    }
    if (result.status == GuestServiceStatus::Complete) {
      GuestWireReader reader(result.payload);
      m_meshRetirements.push_back(GuestResourceId::read(reader));
    }
    it = m_abandonedMeshes.erase(it);
  }
  while (!m_meshRetirements.empty()) {
    GuestWireWriter payload;
    m_meshRetirements.back().write(payload);
    const std::uint64_t request =
      m_services.enqueue(GuestService::ReleaseMesh, payload.take());
    if (request == 0) {
      break;
    }
    m_drained.push_back(request);
    m_meshRetirements.pop_back();
  }
  // Each pump keeps a bounded number of chunks in flight so retained uploads
  // never starve fonts, textures or files sharing the service queue.
  constexpr std::size_t maximumWrites = 8;
  for (std::pair<const std::uint32_t, Mesh>& entry : m_meshes) {
    Mesh& mesh = entry.second;
    if (!mesh.retain || mesh.ready || mesh.failed) {
      continue;
    }
    if (mesh.create != 0) {
      if (!m_services.take(mesh.create, result)) {
        continue;
      }
      mesh.create = 0;
      GuestWireReader reader(result.payload);
      const GuestResourceId id = GuestResourceId::read(reader);
      if (result.status != GuestServiceStatus::Complete || !reader.finished() ||
          id.owner == 0 || id.kind != GuestResourceKind::Mesh) {
        mesh.failed = true;
        continue;
      }
      mesh.id = id;
    } else if (mesh.id.owner == 0) {
      GuestMeshRequest request;
      request.style = retainedStyle(mesh.layout);
      request.vertexBytes = static_cast<std::uint32_t>(mesh.vertices.size());
      request.indexBytes = static_cast<std::uint32_t>(mesh.indices.size());
      GuestWireWriter payload;
      request.write(payload);
      mesh.create =
        m_services.enqueue(GuestService::CreateMesh, payload.take());
      continue;
    }
    for (std::vector<std::uint64_t>::iterator it = mesh.writes.begin();
         it != mesh.writes.end();) {
      if (!m_services.take(*it, result)) {
        ++it;
        continue;
      }
      mesh.failed =
        mesh.failed || result.status != GuestServiceStatus::Complete;
      it = mesh.writes.erase(it);
    }
    while (!mesh.failed && mesh.writes.size() < maximumWrites &&
           (mesh.vertexSent < mesh.vertices.size() ||
            mesh.indexSent < mesh.indices.size())) {
      const bool indices = mesh.vertexSent == mesh.vertices.size();
      const std::vector<std::byte>& source =
        indices ? mesh.indices : mesh.vertices;
      std::size_t& sent = indices ? mesh.indexSent : mesh.vertexSent;
      GuestMeshWrite write;
      write.mesh = mesh.id;
      write.indices = indices;
      write.offset = static_cast<std::uint32_t>(sent);
      const std::size_t count = std::min<std::size_t>(
        GuestMeshWrite::MaximumChunk, source.size() - sent);
      write.bytes.assign(source.begin() + static_cast<std::ptrdiff_t>(sent),
                         source.begin() +
                           static_cast<std::ptrdiff_t>(sent + count));
      GuestWireWriter payload;
      write.write(payload);
      const std::uint64_t request =
        m_services.enqueue(GuestService::WriteMesh, payload.take());
      if (request == 0) {
        break;
      }
      mesh.writes.push_back(request);
      sent += count;
    }
    if (mesh.failed) {
      // Draw inline from now on; the host copy is released.
      m_drained.insert(m_drained.end(), mesh.writes.begin(), mesh.writes.end());
      mesh.writes.clear();
      if (mesh.id.owner != 0) {
        m_meshRetirements.push_back(mesh.id);
        mesh.id = {};
      }
    } else if (mesh.writes.empty() && mesh.vertexSent == mesh.vertices.size() &&
               mesh.indexSent == mesh.indices.size()) {
      mesh.ready = true;
    }
  }
}
FramebufferHandle
GuestRecordingBackend::CreateFramebuffer(const FramebufferDesc&,
                                         FramebufferAttachments*)
{
  return {};
}
FramebufferHandle
GuestRecordingBackend::CreateDepthFramebuffer(int width,
                                              int height,
                                              TextureHandle* depth)
{
  // One virtual shadow target: it records which meshes cast shadows and is
  // never rendered. The host owns the real depth map.
  if (m_shadowFramebuffer.isValid() || depth == nullptr || width <= 0 ||
      height <= 0 || m_textures.size() >= 1024) {
    return {};
  }
  const TextureHandle texture = m_textureHandles.allocate();
  Texture target;
  target.info = { width, height, 1 };
  target.depthOnly = true;
  m_textures.emplace(texture.slot, std::move(target));
  m_shadowFramebuffer = m_framebufferHandles.allocate();
  m_shadowDepth = texture;
  *depth = texture;
  return m_shadowFramebuffer;
}
bool
GuestRecordingBackend::DestroyFramebuffer(FramebufferHandle handle)
{
  if (!IsFramebufferValid(handle)) {
    return false;
  }
  m_textures.erase(m_shadowDepth.slot);
  m_textureHandles.release(m_shadowDepth);
  m_shadowDepth = {};
  m_shadowFramebuffer = {};
  return m_framebufferHandles.release(handle);
}
bool
GuestRecordingBackend::IsFramebufferValid(FramebufferHandle handle) const
{
  return m_framebufferHandles.isCurrent(handle) &&
         handle == m_shadowFramebuffer;
}

static float
readFloat(const std::byte* bytes)
{
  float value = 0;
  std::memcpy(&value, bytes, sizeof(value));
  return value;
}

void
GuestRecordingBackend::draw(std::uint32_t first, std::uint32_t count)
{
  const bool lines = m_pipeline.primitives == Primitives::Lines;
  if (!m_renderer || !IsMeshValid(m_mesh) ||
      (!lines && m_pipeline.primitives != Primitives::Triangles) ||
      count % (lines ? 2u : 3u) != 0 || m_pipeline.wireframe) {
    throw std::runtime_error("Unsupported guest draw");
  }
  if (count == 0) {
    return;
  }
  GuestBatch batch;
  bool styleFound = false;
  // Derived styles (MeshVisual's depth-tested lines, triangles and world
  // sprites) share a built-in shader; the host re-derives the pipeline from
  // the recorded primitive, depth test and blend.
  for (RenderStyleId style : { RenderStyleId::Shape,
                               RenderStyleId::Sprite,
                               RenderStyleId::Canvas,
                               RenderStyleId::LitMesh,
                               RenderStyleId::Skybox }) {
    const RenderStyle* registered =
      static_cast<const Renderer*>(m_renderer)->getStyle(style);
    if (registered && registered->shaderHandle == m_shader) {
      batch.style = style == RenderStyleId::Shape    ? GuestBatchStyle::Shape
                    : style == RenderStyleId::Sprite ? GuestBatchStyle::Sprite
                    : style == RenderStyleId::Canvas ? GuestBatchStyle::Canvas
                    : style == RenderStyleId::Skybox ? GuestBatchStyle::Skybox
                                                     : GuestBatchStyle::LitMesh;
      styleFound = true;
      break;
    }
  }
  if (!styleFound) {
    throw std::runtime_error("Shader has no guest recording contract");
  }
  if (lines && batch.style != GuestBatchStyle::Shape) {
    throw std::runtime_error("Only shape batches may draw lines");
  }
  const bool lit = batch.style == GuestBatchStyle::LitMesh;
  const bool sky = batch.style == GuestBatchStyle::Skybox;
  if ((lit || sky) && m_layer != GuestLayer::World) {
    throw std::runtime_error("Lit meshes and skyboxes draw only in the world");
  }
  batch.primitive = lines ? GuestPrimitive::Lines : GuestPrimitive::Triangles;
  batch.depthTest = m_pipeline.depthTestEnabled;
  batch.blend = m_pipeline.blendEnabled;
  if (lit) {
    batch.lighting = m_lighting;
    batch.lighting.castsShadow = m_shadowMeshes.contains(m_mesh.slot);
  } else if (batch.style != GuestBatchStyle::Shape) {
    if (!IsTextureValid(m_texture) ||
        m_textures.at(m_texture.slot).cubemap != sky) {
      throw std::runtime_error("Guest draw references a stale texture");
    }
    batch.texture = m_textures.at(m_texture.slot).id;
    if (batch.texture.owner == 0) {
      return;
    } // acquisition is still pending
    if (sky) {
      batch.lighting.tint = m_lighting.tint;
    }
  }
  const Mesh& mesh = m_meshes.at(m_mesh.slot);
  // Lit meshes: position, normal, RGBA8 color, UV (36 bytes).
  const std::size_t stride = batch.style == GuestBatchStyle::Shape    ? 16
                             : batch.style == GuestBatchStyle::Sprite ? 24
                             : lit                                    ? 36
                             : sky                                    ? 12
                                                                      : 32;
  const MeshVertexLayout expected =
    batch.style == GuestBatchStyle::Shape    ? MeshVertexLayout::Pos3Color4U8
    : batch.style == GuestBatchStyle::Sprite ? MeshVertexLayout::Pos3Color4U8Uv2
    : lit ? MeshVertexLayout::Pos3Norm3Color4U8Uv2
    : sky ? MeshVertexLayout::Pos3
          : MeshVertexLayout::Pos3Color3Uv2;
  if (mesh.layout != expected || first > mesh.indices.size() / 4 ||
      count > mesh.indices.size() / 4 - first) {
    throw std::runtime_error("Guest draw geometry mismatch");
  }
  if (mesh.retain && !mesh.failed) {
    if (!mesh.ready) {
      return; // the host copy is still uploading
    }
    batch.mesh = mesh.id;
    batch.firstIndex = first;
    batch.indexCount = count;
    batch.mvp = m_mvp;
    batch.layer = m_layer;
    batch.clipped = m_clip.enabled;
    batch.clip = { static_cast<float>(m_clip.x),
                   m_frame.height - m_clip.y - m_clip.height,
                   static_cast<float>(m_clip.width),
                   static_cast<float>(m_clip.height) };
    m_frame.batches.push_back(std::move(batch));
    return;
  }
  batch.indices.resize(count);
  std::memcpy(batch.indices.data(),
              mesh.indices.data() + static_cast<std::size_t>(first) * 4,
              static_cast<std::size_t>(count) * 4);
  const std::uint32_t minimum =
    *std::min_element(batch.indices.begin(), batch.indices.end());
  const std::uint32_t maximum =
    *std::max_element(batch.indices.begin(), batch.indices.end());
  if (maximum >= mesh.vertices.size() / stride) {
    throw std::runtime_error("Guest draw index outside mesh");
  }
  for (std::uint32_t& index : batch.indices) {
    index -= minimum;
  }
  batch.vertices.reserve(static_cast<std::size_t>(maximum) - minimum + 1);
  for (std::size_t index = minimum; index <= maximum; ++index) {
    const std::byte* source = mesh.vertices.data() + index * stride;
    GuestVertex vertex;
    for (std::size_t component = 0; component < 3; ++component) {
      vertex.position[component] = readFloat(source + component * 4);
    }
    if (batch.style == GuestBatchStyle::Canvas) {
      vertex.rgba = 0xff000000;
      for (unsigned int component = 0; component < 3; ++component) {
        const float color = readFloat(source + 12 + component * 4);
        if (!std::isfinite(color)) {
          throw std::runtime_error("Non-finite guest color");
        }
        vertex.rgba |= static_cast<std::uint32_t>(
                         std::round(std::clamp(color, 0.0f, 1.0f) * 255))
                       << (component * 8);
      }
    } else if (lit) {
      for (std::size_t component = 0; component < 3; ++component) {
        vertex.normal[component] = readFloat(source + 12 + component * 4);
      }
      std::memcpy(&vertex.rgba, source + 24, 4);
    } else if (!sky) {
      std::memcpy(&vertex.rgba, source + 12, 4);
    }
    if (batch.style != GuestBatchStyle::Shape && !sky) {
      vertex.uv = { readFloat(source + stride - 8),
                    readFloat(source + stride - 4) };
    }
    for (float value : vertex.position) {
      if (!std::isfinite(value)) {
        throw std::runtime_error("Non-finite guest vertex");
      }
    }
    batch.vertices.push_back(vertex);
  }
  batch.mvp = m_mvp;
  batch.layer = m_layer;
  batch.clipped = m_clip.enabled;
  batch.clip = { static_cast<float>(m_clip.x),
                 m_frame.height - m_clip.y - m_clip.height,
                 static_cast<float>(m_clip.width),
                 static_cast<float>(m_clip.height) };
  m_frame.batches.push_back(std::move(batch));
}

void
GuestRecordingBackend::consume(const RenderCommand& command)
{
  switch (command.commandType) {
    case CommandType::SetMesh:
      m_mesh = command.bindMesh.handle;
      break;
    case CommandType::SetShader:
      m_shader = command.bindShader.handle;
      break;
    case CommandType::SetTexture:
      // Unit 1 is the shared shadow map; the host binds its own.
      if (command.bindTexture.slot == 1 &&
          command.bindTexture.handle == m_shadowDepth) {
        break;
      }
      if (command.bindTexture.slot != 0) {
        throw std::runtime_error("Unsupported guest texture unit");
      }
      m_texture = command.bindTexture.handle;
      break;
    case CommandType::SetPipelineState:
      m_pipeline = command.pipelineState;
      break;
    case CommandType::SetScissorState:
      m_clip = command.scissor;
      break;
    case CommandType::SetUniformMat4: {
      const char* name = command.uniformMat4.name;
      if (command.uniformMat4.value == nullptr) {
        throw std::runtime_error("Missing guest matrix uniform");
      }
      if (std::strcmp(name, "uMVP") == 0 ||
          std::strcmp(name, "uViewProjection") == 0) {
        // uViewProjection is the skybox's rotation-only view projection.
        std::copy_n(command.uniformMat4.value, 16, m_mvp.begin());
      } else if (std::strcmp(name, "uModel") == 0) {
        std::copy_n(command.uniformMat4.value, 16, m_lighting.model.begin());
      } else if (std::strcmp(name, "uLightSpaceMatrix") != 0 &&
                 std::strcmp(name, "uPrevMVP") != 0) {
        // Light space comes from the host pass; motion blur is host policy.
        throw std::runtime_error("Unsupported guest matrix uniform");
      }
      break;
    }
    case CommandType::SetUniformVec3: {
      const CmdUniformVec3& value = command.uniformVec3;
      const std::array<float, 3> vector{ value.x, value.y, value.z };
      if (std::strcmp(value.name, "uLightDir") == 0) {
        m_lighting.lightDirection = vector;
      } else if (std::strcmp(value.name, "uLightColor") == 0) {
        m_lighting.lightColor = vector;
      } else if (std::strcmp(value.name, "uAmbientColor") == 0) {
        m_lighting.ambientColor = vector;
      } else {
        throw std::runtime_error("Unsupported guest vector uniform");
      }
      break;
    }
    case CommandType::SetUniformVec4:
      if (std::strcmp(command.uniformVec4.name, "uTint") != 0) {
        throw std::runtime_error("Unsupported guest vector uniform");
      }
      m_lighting.tint = { command.uniformVec4.x,
                          command.uniformVec4.y,
                          command.uniformVec4.z,
                          command.uniformVec4.w };
      break;
    case CommandType::SetUniformFloat: {
      const CmdUniformFloat& value = command.uniformFloat;
      if (std::strcmp(value.name, "uShadowBias") == 0) {
        m_lighting.shadowBias = value.value;
      } else if (std::strcmp(value.name, "uShadowSlopeScale") == 0) {
        m_lighting.shadowSlopeScale = value.value;
      } else if (std::strcmp(value.name, "uShadowNormalOffset") == 0) {
        m_lighting.shadowNormalOffset = value.value;
      } else if (std::strcmp(value.name, "uMotionBlurAmount") != 0 &&
                 std::strcmp(value.name, "uMotionBlurMax") != 0) {
        throw std::runtime_error("Unsupported guest float uniform");
      }
      break;
    }
    case CommandType::UpdateBuffer:
    case CommandType::UpdateIndexBuffer: {
      const bool indices =
        command.commandType == CommandType::UpdateIndexBuffer;
      const MeshHandle handle = indices ? command.updateIndexBuffer.handle
                                        : command.updateBuffer.handle;
      const std::size_t offset = indices ? command.updateIndexBuffer.offsetBytes
                                         : command.updateBuffer.offsetBytes;
      const std::size_t size = indices ? command.updateIndexBuffer.sizeBytes
                                       : command.updateBuffer.sizeBytes;
      const void* data =
        indices ? command.updateIndexBuffer.data : command.updateBuffer.data;
      if (!IsMeshValid(handle)) {
        throw std::runtime_error("Stale guest mesh update");
      }
      // A mesh that changes is not static after all: draw it inline.
      forgetRetained(m_meshes.at(handle.slot));
      std::vector<std::byte>& buffer = indices
                                         ? m_meshes.at(handle.slot).indices
                                         : m_meshes.at(handle.slot).vertices;
      if (offset > buffer.size() || size > buffer.size() - offset ||
          (size != 0 && data == nullptr)) {
        throw std::runtime_error("Invalid guest buffer update");
      }
      if (size != 0) {
        std::memcpy(buffer.data() + offset, data, size);
      }
      break;
    }
    case CommandType::UpdateTexture: {
      const CmdUpdateTexture& write = command.updateTexture;
      if (!IsTextureValid(write.handle) ||
          m_textures.at(write.handle.slot).cubemap) {
        throw std::runtime_error("Stale guest texture update");
      }
      Texture& texture = m_textures.at(write.handle.slot);
      const TextureInfo& info =
        texture.pending != 0 ? texture.pendingInfo : texture.info;
      std::vector<std::byte>& pixels =
        texture.pending != 0 ? texture.pendingPixels : texture.pixels;
      const int channels = write.channels == 0 ? info.channels : write.channels;
      const int stride =
        write.srcRowStride == 0 ? write.width : write.srcRowStride;
      if (write.x < 0 || write.y < 0 || write.width <= 0 || write.height <= 0 ||
          write.x > info.width || write.y > info.height ||
          write.width > info.width - write.x ||
          write.height > info.height - write.y || channels != info.channels ||
          stride < write.width || write.data == nullptr) {
        throw std::runtime_error("Invalid guest texture write");
      }
      GuestTextureWrite copied{ texture.id,
                                static_cast<std::uint32_t>(write.x),
                                static_cast<std::uint32_t>(write.y),
                                static_cast<std::uint32_t>(write.width),
                                static_cast<std::uint32_t>(write.height),
                                static_cast<std::uint32_t>(channels),
                                {} };
      const std::size_t rowBytes =
        static_cast<std::size_t>(write.width) * channels;
      copied.pixels.resize(rowBytes * write.height);
      for (int row = 0; row < write.height; ++row) {
        const std::byte* source =
          static_cast<const std::byte*>(write.data) +
          static_cast<std::size_t>(row) * stride * channels;
        std::memcpy(copied.pixels.data() +
                      static_cast<std::size_t>(row) * rowBytes,
                    source,
                    rowBytes);
        std::memcpy(
          pixels.data() +
            (static_cast<std::size_t>(write.y + row) * info.width + write.x) *
              channels,
          source,
          rowBytes);
      }
      if (texture.id.owner != 0 && texture.pending == 0) {
        copied.beforeBatch = static_cast<std::uint32_t>(m_frame.batches.size());
        m_frame.textureWrites.push_back(std::move(copied));
      } else {
        texture.changed = true;
      }
      break;
    }
    case CommandType::DrawIndexed:
      if (m_shadowPass) {
        // Shadow depth draws only identify casters; nothing is recorded.
        if (IsMeshValid(m_mesh)) {
          m_shadowMeshes.insert(m_mesh.slot);
        }
        break;
      }
      draw(command.drawIndexed.firstIndex, command.drawIndexed.elementCount);
      break;
    case CommandType::SetFramebuffer:
      if (command.bindFramebuffer.handle.isValid()) {
        if (command.bindFramebuffer.handle != m_shadowFramebuffer) {
          throw std::runtime_error(
            "Offscreen guest rendering requires an instance contract");
        }
        m_shadowPass = true;
      } else {
        m_shadowPass = false;
      }
      break;
    case CommandType::SetViewport:
      if (!m_shadowPass &&
          (command.viewport.x != 0 || command.viewport.y != 0 ||
           command.viewport.width != m_frame.width ||
           command.viewport.height != m_frame.height)) {
        throw std::runtime_error("Unsupported guest viewport");
      }
      break;
    case CommandType::SetUniformInt: {
      const CmdUniformInt& value = command.uniformInt;
      if (std::strcmp(value.name, "uShadowsEnabled") == 0) {
        m_lighting.receivesShadow = value.value != 0;
      } else if (std::strcmp(value.name, "uShadowPcf") == 0) {
        m_lighting.shadowPcf = value.value != 0;
      } else if (std::strcmp(value.name, "uShadowMap") == 0) {
        if (value.value != 1) {
          throw std::runtime_error("Unsupported guest shadow map unit");
        }
      } else if (std::strcmp(value.name, "uMotionBlurEnabled") != 0 &&
                 ((std::strcmp(value.name, "uTexture") != 0 &&
                   std::strcmp(value.name, "uSkybox") != 0) ||
                  value.value != 0)) {
        throw std::runtime_error("Unsupported guest integer uniform");
      }
      break;
    }
    case CommandType::SetUniformVec2:
      if (std::strcmp(command.uniformVec2.name, "u_resolution") != 0) {
        throw std::runtime_error("Unsupported guest vector uniform");
      }
      break;
    case CommandType::ClearScreen:
    case CommandType::ClearDepthBuffer:
    case CommandType::ClearColorBuffer:
    case CommandType::ClearStencilBuffer:
    case CommandType::ClearAll:
      if (!m_shadowPass && !m_frame.batches.empty()) {
        throw std::runtime_error(
          "Mid-frame clears require a render-pass contract");
      }
      break;
    default:
      throw std::runtime_error("Unsupported guest recording token");
  }
}
