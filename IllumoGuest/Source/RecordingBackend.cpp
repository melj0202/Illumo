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
}
void
GuestRecordingBackend::BeginFrame()
{
  m_frame.batches.clear();
  m_frame.textureWrites.clear();
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
  m_commands.Submit(command);
}
void
GuestRecordingBackend::ClearCommandQueue()
{
  m_commands.Reset();
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
  try {
    for (std::size_t index = 0; index < m_commands.GetCommandCount(); ++index) {
      consume(m_commands.GetCommand(index));
    }
  } catch (const std::exception& exception) {
    m_error = exception.what();
  }
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
bool
GuestRecordingBackend::ReplaceMesh(MeshHandle handle,
                                   const void* vertices,
                                   std::size_t vertexBytes,
                                   const void* indices,
                                   std::size_t indexBytes,
                                   MeshVertexLayout layout,
                                   bool)
{
  if (!IsMeshValid(handle) || vertexBytes > 64u * 1024u * 1024u ||
      indexBytes > 16u * 1024u * 1024u) {
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
  m_meshes.at(handle.slot) = std::move(candidate);
  return true;
}
bool
GuestRecordingBackend::DestroyMesh(MeshHandle handle)
{
  if (!IsMeshValid(handle)) {
    return false;
  }
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
GuestRecordingBackend::CreateCubemap(const std::array<const unsigned char*, 6>&,
                                     int,
                                     int,
                                     int)
{
  return {};
}
bool
GuestRecordingBackend::ReplaceTexture(TextureHandle handle,
                                      const unsigned char* pixels,
                                      int width,
                                      int height,
                                      int channels,
                                      const TextureOptions& options)
{
  if (!IsTextureValid(handle) || m_textures.at(handle.slot).pending != 0 ||
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
}
FramebufferHandle
GuestRecordingBackend::CreateFramebuffer(const FramebufferDesc&,
                                         FramebufferAttachments*)
{
  return {};
}
FramebufferHandle
GuestRecordingBackend::CreateDepthFramebuffer(int, int, TextureHandle*)
{
  return {};
}
bool
GuestRecordingBackend::DestroyFramebuffer(FramebufferHandle)
{
  return false;
}
bool
GuestRecordingBackend::IsFramebufferValid(FramebufferHandle) const
{
  return false;
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
  if (!m_renderer || !IsMeshValid(m_mesh) ||
      m_pipeline.primitives != Primitives::Triangles || count % 3 != 0) {
    throw std::runtime_error("Unsupported guest draw");
  }
  if (count == 0) {
    return;
  }
  GuestBatch batch;
  bool styleFound = false;
  for (RenderStyleId style :
       { RenderStyleId::Shape, RenderStyleId::Sprite, RenderStyleId::Canvas }) {
    const RenderStyle* registered =
      static_cast<const Renderer*>(m_renderer)->getStyle(style);
    if (registered && registered->shaderHandle == m_shader) {
      const PipelineState& expected = registered->pipeline;
      if (m_pipeline.depthTestEnabled != expected.depthTestEnabled ||
          m_pipeline.blendEnabled != expected.blendEnabled ||
          m_pipeline.blendSrc != expected.blendSrc ||
          m_pipeline.blendDst != expected.blendDst ||
          m_pipeline.faceCullingEnabled != expected.faceCullingEnabled ||
          m_pipeline.cullFace != expected.cullFace ||
          m_pipeline.frontFace != expected.frontFace || m_pipeline.wireframe) {
        throw std::runtime_error("Pipeline has no guest recording contract");
      }
      batch.style = style == RenderStyleId::Shape    ? GuestBatchStyle::Shape
                    : style == RenderStyleId::Sprite ? GuestBatchStyle::Sprite
                                                     : GuestBatchStyle::Canvas;
      styleFound = true;
      break;
    }
  }
  if (!styleFound) {
    throw std::runtime_error("Shader has no guest recording contract");
  }
  if (batch.style != GuestBatchStyle::Shape) {
    if (!IsTextureValid(m_texture)) {
      throw std::runtime_error("Guest draw references a stale texture");
    }
    batch.texture = m_textures.at(m_texture.slot).id;
    if (batch.texture.owner == 0) {
      return;
    } // acquisition is still pending
  }
  const Mesh& mesh = m_meshes.at(m_mesh.slot);
  const std::size_t stride = batch.style == GuestBatchStyle::Shape    ? 16
                             : batch.style == GuestBatchStyle::Sprite ? 24
                                                                      : 32;
  const MeshVertexLayout expected =
    batch.style == GuestBatchStyle::Shape    ? MeshVertexLayout::Pos3Color4U8
    : batch.style == GuestBatchStyle::Sprite ? MeshVertexLayout::Pos3Color4U8Uv2
                                             : MeshVertexLayout::Pos3Color3Uv2;
  if (mesh.layout != expected || first > mesh.indices.size() / 4 ||
      count > mesh.indices.size() / 4 - first) {
    throw std::runtime_error("Guest draw geometry mismatch");
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
    } else {
      std::memcpy(&vertex.rgba, source + 12, 4);
    }
    if (batch.style != GuestBatchStyle::Shape) {
      vertex.uv = { readFloat(source + stride - 8),
                    readFloat(source + stride - 4) };
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
    case CommandType::SetUniformMat4:
      if (std::strcmp(command.uniformMat4.name, "uMVP") == 0 &&
          command.uniformMat4.value != nullptr) {
        std::copy_n(command.uniformMat4.value, 16, m_mvp.begin());
      } else {
        throw std::runtime_error("Unsupported guest matrix uniform");
      }
      break;
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
      if (!IsTextureValid(write.handle)) {
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
      draw(command.drawIndexed.firstIndex, command.drawIndexed.elementCount);
      break;
    case CommandType::SetFramebuffer:
      if (command.bindFramebuffer.handle.isValid()) {
        throw std::runtime_error(
          "Offscreen guest rendering requires an instance contract");
      }
      break;
    case CommandType::SetViewport:
      if (command.viewport.x != 0 || command.viewport.y != 0 ||
          command.viewport.width != m_frame.width ||
          command.viewport.height != m_frame.height) {
        throw std::runtime_error("Unsupported guest viewport");
      }
      break;
    case CommandType::SetUniformInt:
      if (std::strcmp(command.uniformInt.name, "uTexture") != 0 ||
          command.uniformInt.value != 0) {
        throw std::runtime_error("Unsupported guest integer uniform");
      }
      break;
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
      if (!m_frame.batches.empty()) {
        throw std::runtime_error(
          "Mid-frame clears require a render-pass contract");
      }
      break;
    default:
      throw std::runtime_error("Unsupported guest recording token");
  }
}
