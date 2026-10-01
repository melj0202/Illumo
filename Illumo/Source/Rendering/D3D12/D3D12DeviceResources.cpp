#include "D3D12Device.h"
#include "Rendering/Gpu/GpuTexels.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cstring>

static constexpr int kMaxTextureSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;

static uint32_t
mipLevelsFor(int width, int height)
{
  uint32_t levels = 1;
  int size = std::max(width, height);
  while (size > 1) {
    size /= 2;
    ++levels;
  }
  return levels;
}

// How a shader reads a texture's channels: OpenGL swizzles R8 textures to
// (r, r, r, 1), and RGB data reads an opaque alpha.
static UINT
componentsForChannels(int channels)
{
  if (channels == 1) {
    return D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
      D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
      D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
      D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
      D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
  }
  if (channels == 3) {
    return D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
      D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0,
      D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
      D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2,
      D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
  }
  return D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
}

static int
normalizeChannels(int channels)
{
  if (channels == 1) {
    return 1;
  }
  if (channels == 3) {
    return 3;
  }
  return 4;
}

// One mip level from the previous: each texel averages its 2x2 source block
// (edges repeat on odd sizes), rounding to nearest, as a linear blit of
// exact halves does.
static std::vector<unsigned char>
halveTexels(const std::vector<unsigned char>& source,
            int width,
            int height,
            int texelBytes,
            int* nextWidth,
            int* nextHeight)
{
  *nextWidth = width > 1 ? width / 2 : 1;
  *nextHeight = height > 1 ? height / 2 : 1;
  std::vector<unsigned char> result(static_cast<size_t>(*nextWidth) *
                                    static_cast<size_t>(*nextHeight) *
                                    static_cast<size_t>(texelBytes));
  for (int y = 0; y < *nextHeight; ++y) {
    const int y0 = std::min(y * 2, height - 1);
    const int y1 = std::min(y * 2 + 1, height - 1);
    for (int x = 0; x < *nextWidth; ++x) {
      const int x0 = std::min(x * 2, width - 1);
      const int x1 = std::min(x * 2 + 1, width - 1);
      for (int channel = 0; channel < texelBytes; ++channel) {
        const unsigned sum =
          source[(static_cast<size_t>(y0) * width + x0) * texelBytes +
                 channel] +
          source[(static_cast<size_t>(y0) * width + x1) * texelBytes +
                 channel] +
          source[(static_cast<size_t>(y1) * width + x0) * texelBytes +
                 channel] +
          source[(static_cast<size_t>(y1) * width + x1) * texelBytes + channel];
        result[(static_cast<size_t>(y) * *nextWidth + x) * texelBytes +
               channel] = static_cast<unsigned char>((sum + 2u) / 4u);
      }
    }
  }
  return result;
}

std::unique_ptr<D3D12Mesh>
D3D12Device::buildMesh(const void* vertices,
                       size_t vertexSize,
                       const void* indices,
                       size_t indexSize,
                       MeshVertexLayout layout,
                       bool dynamic)
{
  if (vertexSize == 0 || (vertices == nullptr && !dynamic)) {
    return nullptr;
  }
  std::unique_ptr<D3D12Mesh> mesh = std::make_unique<D3D12Mesh>();
  mesh->layout = layout;
  mesh->dynamic = dynamic;
  mesh->vertexCapacity = vertexSize;
  mesh->indexCapacity = indexSize;
  if (!createBufferMemory(mesh->vertices, vertexSize)) {
    return nullptr;
  }
  if (indexSize > 0) {
    if (!createBufferMemory(mesh->indices, indexSize)) {
      retireMemory(mesh->vertices);
      return nullptr;
    }
    mesh->indexCount = static_cast<unsigned>(indexSize / sizeof(unsigned int));
  }
  if (vertices != nullptr) {
    uploadToBuffer(mesh->vertices, 0, vertices, vertexSize);
  }
  if (indices != nullptr && indexSize > 0) {
    if (indexSize % sizeof(unsigned int) == 0) {
      uploadToBuffer(mesh->indices, 0, indices, indexSize);
    } else {
      // GLMesh widens byte indices, then uploads the enrolled byte count.
      std::vector<unsigned int> widened(indexSize);
      const unsigned char* bytes = static_cast<const unsigned char*>(indices);
      for (size_t index = 0; index < indexSize; ++index) {
        widened[index] = bytes[index];
      }
      uploadToBuffer(mesh->indices, 0, widened.data(), indexSize);
    }
  }
  return mesh;
}

void
D3D12Device::releaseMesh(D3D12Mesh& mesh)
{
  retireMemory(mesh.vertices);
  retireMemory(mesh.indices);
  m_recorded.vertexKnown = false;
  m_recorded.indexBuffer = 0;
}

MeshHandle
D3D12Device::createMesh(const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize,
                        MeshVertexLayout layout,
                        bool dynamic)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.createMesh");
  std::unique_ptr<D3D12Mesh> mesh =
    buildMesh(vertices, vertexSize, indices, indexSize, layout, dynamic);
  if (!mesh) {
    Logger::LogWarning("CreateMesh: the GPU mesh could not be created (" +
                       std::to_string(vertexSize) + " vertex bytes, " +
                       std::to_string(indexSize) + " index bytes)");
    return {};
  }
  const MeshHandle handle = m_meshHandles.allocate();
  Entry<D3D12Mesh>& entry = slotEntry(m_meshes, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(mesh);
  return handle;
}

bool
D3D12Device::replaceMesh(MeshHandle handle,
                         const void* vertices,
                         size_t vertexSize,
                         const void* indices,
                         size_t indexSize,
                         MeshVertexLayout layout,
                         bool dynamic)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.replaceMesh");
  D3D12Mesh* current = resolveMesh(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceMesh: stale mesh handle ignored");
    return false;
  }
  std::unique_ptr<D3D12Mesh> replacement =
    buildMesh(vertices, vertexSize, indices, indexSize, layout, dynamic);
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
D3D12Device::destroyMesh(MeshHandle handle)
{
  D3D12Mesh* mesh = resolveMesh(handle);
  if (mesh == nullptr) {
    Logger::LogWarning("DestroyMesh: stale mesh handle ignored");
    return false;
  }
  releaseMesh(*mesh);
  m_meshes[handle.slot] = Entry<D3D12Mesh>{};
  return m_meshHandles.release(handle);
}

bool
D3D12Device::isMeshValid(MeshHandle handle) const
{
  return m_meshHandles.isCurrent(handle) && resolveMesh(handle) != nullptr;
}

std::unique_ptr<D3D12Program>
D3D12Device::buildProgram(const ShaderSources& sources, std::string* error)
{
  std::unique_ptr<D3D12Program> program = std::make_unique<D3D12Program>();
  if (!compileGlslProgram(sources.vertexSource,
                          sources.fragmentSource,
                          &program->reflection,
                          error)) {
    return nullptr;
  }
  const GlslProgram& reflection = program->reflection;
  if (reflection.bindings.size() > 32) {
    *error = "The shader program uses more than 32 uniform blocks and samplers";
    return nullptr;
  }
  std::string vertexHlsl;
  std::string pixelHlsl;
  translateProgramToHlsl(reflection, &vertexHlsl, &pixelHlsl);
  if (!compileHlsl(vertexHlsl, true, &program->vertexCode, error) ||
      !compileHlsl(pixelHlsl, false, &program->pixelCode, error) ||
      !reflectVertexInputs(
        program->vertexCode, &program->vertexInputs, error)) {
    return nullptr;
  }
  std::vector<unsigned char> root;
  if (!serializeRootSignature(reflection, &root, &program->rootLayout, error)) {
    return nullptr;
  }
  if (FAILED(m_context.device()->CreateRootSignature(
        0,
        root.data(),
        root.size(),
        __uuidof(ID3D12RootSignature),
        program->rootSignature.putVoid()))) {
    *error = "The shader program's root signature could not be created";
    return nullptr;
  }
  program->uniforms.configure(reflection);
  return program;
}

void
D3D12Device::releaseProgram(D3D12Program& program)
{
  Retired retired;
  retired.rootSignature = std::move(program.rootSignature);
  for (std::pair<const D3D12PipelineKey, D3D12Ref<ID3D12PipelineState>>&
         pipeline : program.pipelines) {
    retired.pipelines.push_back(std::move(pipeline.second));
  }
  program.pipelines.clear();
  // The recording may hold these objects; it binds again from scratch.
  m_recorded.pipeline = nullptr;
  m_recorded.rootSignature = nullptr;
  if (m_lastPipelineProgram == &program) {
    m_lastPipelineProgram = nullptr;
  }
  retire(std::move(retired));
}

ShaderHandle
D3D12Device::createShader(const ShaderSources& sources, std::string* error)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.createShader");
  std::unique_ptr<D3D12Program> program = buildProgram(sources, error);
  if (!program) {
    return {};
  }
  const ShaderHandle handle = m_shaderHandles.allocate();
  Entry<D3D12Program>& entry = slotEntry(m_programs, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(program);
  return handle;
}

bool
D3D12Device::replaceShader(ShaderHandle handle,
                           const ShaderSources& sources,
                           std::string* error)
{
  D3D12Program* current = resolveProgram(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceShaderProgram: stale shader handle ignored");
    return false;
  }
  std::unique_ptr<D3D12Program> replacement = buildProgram(sources, error);
  if (!replacement) {
    return false;
  }
  releaseProgram(*current);
  m_programs[handle.slot].resource = std::move(replacement);
  return true;
}

bool
D3D12Device::destroyShader(ShaderHandle handle)
{
  D3D12Program* program = resolveProgram(handle);
  if (program == nullptr) {
    Logger::LogWarning("DestroyShaderProgram: stale shader handle ignored");
    return false;
  }
  releaseProgram(*program);
  m_programs[handle.slot] = Entry<D3D12Program>{};
  return m_shaderHandles.release(handle);
}

bool
D3D12Device::isShaderValid(ShaderHandle handle) const
{
  return m_shaderHandles.isCurrent(handle) && resolveProgram(handle) != nullptr;
}

std::unique_ptr<D3D12Texture>
D3D12Device::buildTexture(const unsigned char* data,
                          int width,
                          int height,
                          int channels,
                          const TextureOptions& options)
{
  if (data == nullptr || width <= 0 || height <= 0 || width > kMaxTextureSize ||
      height > kMaxTextureSize ||
      (channels != 1 && channels != 3 && channels != 4)) {
    return nullptr;
  }
  std::unique_ptr<D3D12Texture> texture = std::make_unique<D3D12Texture>();
  texture->channels = channels;
  texture->storageBytes = channels == 1 ? 1 : 4;
  texture->format = channels == 1 ? TextureFormat::R8 : TextureFormat::RGBA8;
  const uint32_t levels =
    options.generateMipmaps ? mipLevelsFor(width, height) : 1u;
  const DXGI_FORMAT format =
    channels == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
  if (!createImage(texture->image,
                   format,
                   format,
                   static_cast<uint32_t>(width),
                   static_cast<uint32_t>(height),
                   levels,
                   1,
                   1,
                   D3D12_RESOURCE_FLAG_NONE) ||
      !createShaderView(
        texture->image, format, false, componentsForChannels(channels))) {
    retireImage(texture->image);
    return nullptr;
  }
  const int bytes = texture->storageBytes;
  std::vector<unsigned char> stored(static_cast<size_t>(width) *
                                    static_cast<size_t>(height) *
                                    static_cast<size_t>(bytes));
  convertTexelsForStorage(
    data, width, height, channels, 0, bytes, stored.data());
  int levelWidth = width;
  int levelHeight = height;
  for (uint32_t level = 0; level < levels; ++level) {
    if (level > 0) {
      int nextWidth = 0;
      int nextHeight = 0;
      stored = halveTexels(
        stored, levelWidth, levelHeight, bytes, &nextWidth, &nextHeight);
      levelWidth = nextWidth;
      levelHeight = nextHeight;
    }
    uploadToImage(texture->image,
                  level,
                  0,
                  0,
                  levelWidth,
                  levelHeight,
                  stored.data(),
                  static_cast<size_t>(bytes),
                  static_cast<size_t>(levelWidth) * static_cast<size_t>(bytes));
  }
  texture->sampler = d3d12SamplerIndex(
    options.filter, options.wrapX, options.wrapY, levels > 1, false);
  return texture;
}

std::unique_ptr<D3D12Texture>
D3D12Device::buildCubemap(const std::array<const unsigned char*, 6>& faces,
                          int width,
                          int height,
                          int channels)
{
  const int size = width;
  if (size <= 0 || width != height || size > kMaxTextureSize ||
      (channels != 1 && channels != 3 && channels != 4)) {
    return nullptr;
  }
  for (const unsigned char* face : faces) {
    if (face == nullptr) {
      return nullptr;
    }
  }
  std::unique_ptr<D3D12Texture> texture = std::make_unique<D3D12Texture>();
  texture->channels = normalizeChannels(channels);
  texture->cubemap = true;
  texture->storageBytes = 0;
  const int storageBytes = channels == 1 ? 1 : 4;
  const DXGI_FORMAT format =
    channels == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
  if (!createImage(texture->image,
                   format,
                   format,
                   static_cast<uint32_t>(size),
                   static_cast<uint32_t>(size),
                   1,
                   6,
                   1,
                   D3D12_RESOURCE_FLAG_NONE) ||
      !createShaderView(
        texture->image, format, true, componentsForChannels(channels))) {
    retireImage(texture->image);
    return nullptr;
  }
  std::vector<unsigned char> stored(static_cast<size_t>(size) *
                                    static_cast<size_t>(size) *
                                    static_cast<size_t>(storageBytes));
  for (uint32_t face = 0; face < 6; ++face) {
    convertTexelsForStorage(
      faces[face], size, size, channels, 0, storageBytes, stored.data());
    uploadToImage(texture->image,
                  face,
                  0,
                  0,
                  size,
                  size,
                  stored.data(),
                  static_cast<size_t>(storageBytes),
                  static_cast<size_t>(size) *
                    static_cast<size_t>(storageBytes));
  }
  texture->sampler = d3d12SamplerIndex(TextureFilter::Linear,
                                       TextureWrap::ClampToEdge,
                                       TextureWrap::ClampToEdge,
                                       false,
                                       false);
  return texture;
}

static DXGI_FORMAT
targetFormat(TextureFormat format)
{
  switch (format) {
    case TextureFormat::RGBA8:
    case TextureFormat::RGB8:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::R8:
      return DXGI_FORMAT_R8_UNORM;
    case TextureFormat::RGBA16F:
      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::RG16F:
      return DXGI_FORMAT_R16G16_FLOAT;
    case TextureFormat::R16F:
      return DXGI_FORMAT_R16_FLOAT;
    case TextureFormat::Depth24:
    case TextureFormat::Depth24Stencil8:
      return DXGI_FORMAT_R24G8_TYPELESS;
    default:
      return DXGI_FORMAT_UNKNOWN;
  }
}

std::unique_ptr<D3D12Texture>
D3D12Device::buildRenderTarget(int width,
                               int height,
                               TextureFormat format,
                               TextureFilter filter,
                               TextureWrap wrap)
{
  const DXGI_FORMAT resourceFormat = targetFormat(format);
  if (resourceFormat == DXGI_FORMAT_UNKNOWN || width <= 0 || height <= 0 ||
      width > kMaxTextureSize || height > kMaxTextureSize) {
    return nullptr;
  }
  const bool depth = format == TextureFormat::Depth24 ||
                     format == TextureFormat::Depth24Stencil8;
  std::unique_ptr<D3D12Texture> texture = std::make_unique<D3D12Texture>();
  texture->renderTarget = true;
  texture->format = format;
  switch (format) {
    case TextureFormat::RGB8:
      texture->channels = 3;
      break;
    case TextureFormat::R8:
    case TextureFormat::R16F:
    case TextureFormat::Depth24:
      texture->channels = 1;
      break;
    case TextureFormat::RG16F:
    case TextureFormat::Depth24Stencil8:
      texture->channels = 2;
      break;
    default:
      texture->channels = 4;
      break;
  }
  if (format == TextureFormat::RGBA8 || format == TextureFormat::RGB8) {
    texture->storageBytes = 4;
  } else if (format == TextureFormat::R8) {
    texture->storageBytes = 1;
  } else {
    texture->storageBytes = 0;
  }
  const DXGI_FORMAT viewFormat =
    depth ? DXGI_FORMAT_D24_UNORM_S8_UINT : resourceFormat;
  const DXGI_FORMAT sampledFormat =
    depth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : resourceFormat;
  const UINT mapping = format == TextureFormat::RGB8
                         ? componentsForChannels(3)
                         : D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (!createImage(texture->image,
                   resourceFormat,
                   viewFormat,
                   static_cast<uint32_t>(width),
                   static_cast<uint32_t>(height),
                   1,
                   1,
                   1,
                   depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                         : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !createTargetView(texture->image) ||
      !createShaderView(texture->image, sampledFormat, false, mapping)) {
    retireImage(texture->image);
    return nullptr;
  }
  texture->sampler = depth
                       ? d3d12SamplerIndex(filter,
                                           TextureWrap::ClampToEdge,
                                           TextureWrap::ClampToEdge,
                                           false,
                                           true)
                       : d3d12SamplerIndex(filter, wrap, wrap, false, false);
  return texture;
}

void
D3D12Device::releaseTexture(D3D12Texture& texture)
{
  // A bound attachment's view stays valid until the GPU is done; later
  // draws bind their target again.
  m_recorded.targetKnown = false;
  retireImage(texture.image);
}

TextureHandle
D3D12Device::registerTexture(std::unique_ptr<D3D12Texture> texture)
{
  const TextureHandle handle = m_textureHandles.allocate();
  Entry<D3D12Texture>& entry = slotEntry(m_textures, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(texture);
  return handle;
}

TextureHandle
D3D12Device::createTexture(const unsigned char* data,
                           int width,
                           int height,
                           int channels,
                           const TextureOptions& options)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.createTexture");
  if (data == nullptr || width <= 0 || height <= 0) {
    Logger::LogWarning("CreateTexture: missing pixels or invalid size " +
                       std::to_string(width) + "x" + std::to_string(height));
    return {};
  }
  std::unique_ptr<D3D12Texture> texture =
    buildTexture(data, width, height, channels, options);
  if (!texture) {
    Logger::LogWarning("CreateTexture: the GPU texture could not be created (" +
                       std::to_string(width) + "x" + std::to_string(height) +
                       ", " + std::to_string(channels) + " channels)");
    return {};
  }
  const TextureHandle handle = registerTexture(std::move(texture));
  // OpenGL leaves a new 2D texture bound to unit 0.
  m_units[0].texture2D = handle;
  return handle;
}

TextureHandle
D3D12Device::createCubemap(const std::array<const unsigned char*, 6>& faces,
                           int width,
                           int height,
                           int channels)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.createCubemap");
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
  std::unique_ptr<D3D12Texture> texture =
    buildCubemap(faces, width, height, channels);
  if (!texture) {
    Logger::LogWarning("CreateCubemap: the GPU cubemap could not be created");
    return {};
  }
  return registerTexture(std::move(texture));
}

bool
D3D12Device::replaceTexture(TextureHandle handle,
                            const unsigned char* data,
                            int width,
                            int height,
                            int channels,
                            const TextureOptions& options)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.replaceTexture");
  D3D12Texture* current = resolveTexture(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceTexture: stale texture handle ignored");
    return false;
  }
  if (current->cubemap || data == nullptr || width <= 0 || height <= 0) {
    Logger::LogWarning("ReplaceTexture: invalid texture data ignored");
    return false;
  }
  std::unique_ptr<D3D12Texture> replacement =
    buildTexture(data, width, height, channels, options);
  if (!replacement) {
    return false;
  }
  releaseTexture(*current);
  m_textures[handle.slot].resource = std::move(replacement);
  m_units[0].texture2D = handle;
  return true;
}

bool
D3D12Device::replaceCubemap(TextureHandle handle,
                            const std::array<const unsigned char*, 6>& faces,
                            int width,
                            int height,
                            int channels)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.replaceCubemap");
  D3D12Texture* current = resolveTexture(handle);
  if (current == nullptr || !current->cubemap || width <= 0 ||
      width != height || (channels != 1 && channels != 3 && channels != 4)) {
    return false;
  }
  std::unique_ptr<D3D12Texture> replacement =
    buildCubemap(faces, width, height, channels);
  if (!replacement) {
    return false;
  }
  releaseTexture(*current);
  m_textures[handle.slot].resource = std::move(replacement);
  return true;
}

bool
D3D12Device::destroyTexture(TextureHandle handle)
{
  D3D12Texture* texture = resolveTexture(handle);
  if (texture == nullptr) {
    Logger::LogWarning("DestroyTexture: stale texture handle ignored");
    return false;
  }
  releaseTexture(*texture);
  m_textures[handle.slot] = Entry<D3D12Texture>{};
  return m_textureHandles.release(handle);
}

bool
D3D12Device::isTextureValid(TextureHandle handle) const
{
  return m_textureHandles.isCurrent(handle) &&
         resolveTexture(handle) != nullptr;
}

TextureInfo
D3D12Device::textureInfo(TextureHandle handle) const
{
  TextureInfo info;
  const D3D12Texture* texture = resolveTexture(handle);
  if (texture == nullptr) {
    return info;
  }
  info.width = static_cast<int>(texture->image.width);
  info.height = static_cast<int>(texture->image.height);
  info.channels = texture->channels;
  return info;
}

FramebufferHandle
D3D12Device::createFramebuffer(const FramebufferDesc& desc,
                               FramebufferAttachments* outAttachments)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.createFramebuffer");
  if (desc.width <= 0 || desc.height <= 0) {
    Logger::LogError("CreateFramebuffer: invalid dimensions");
    return {};
  }
  if (desc.colorAttachments.size() > kD3D12MaxColorAttachments ||
      (desc.colorAttachments.empty() &&
       desc.depthStencilFormat == TextureFormat::None)) {
    Logger::LogError("CreateFramebuffer: framebuffer is incomplete");
    return {};
  }
  std::unique_ptr<D3D12Framebuffer> framebuffer =
    std::make_unique<D3D12Framebuffer>();
  framebuffer->width = desc.width;
  framebuffer->height = desc.height;
  bool failed = false;
  for (size_t index = 0; index < desc.colorAttachments.size(); ++index) {
    const FramebufferAttachmentDesc& attachment = desc.colorAttachments[index];
    const bool colorFormat =
      attachment.format != TextureFormat::Depth24 &&
      attachment.format != TextureFormat::Depth24Stencil8 &&
      attachment.format != TextureFormat::None;
    std::unique_ptr<D3D12Texture> texture =
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
    framebuffer->colorTextures.push_back(registerTexture(std::move(texture)));
  }
  if (!failed && desc.depthStencilFormat != TextureFormat::None) {
    const bool depthFormat =
      desc.depthStencilFormat == TextureFormat::Depth24 ||
      desc.depthStencilFormat == TextureFormat::Depth24Stencil8;
    std::unique_ptr<D3D12Texture> texture =
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
      framebuffer->depthTexture = registerTexture(std::move(texture));
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
  Entry<D3D12Framebuffer>& entry = slotEntry(m_framebuffers, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(framebuffer);
  // OpenGL creation leaves the default framebuffer bound and texture unit
  // 0's 2D target empty.
  m_framebuffer = FramebufferHandle{};
  m_units[0].texture2D = TextureHandle{};
  return handle;
}

bool
D3D12Device::destroyFramebuffer(FramebufferHandle handle)
{
  D3D12Framebuffer* framebuffer = resolveFramebuffer(handle);
  if (framebuffer == nullptr) {
    Logger::LogWarning("DestroyFramebuffer: stale framebuffer handle ignored");
    return false;
  }
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
  m_framebuffers[handle.slot] = Entry<D3D12Framebuffer>{};
  m_recorded.targetKnown = false;
  return m_framebufferHandles.release(handle);
}

bool
D3D12Device::isFramebufferValid(FramebufferHandle handle) const
{
  return m_framebufferHandles.isCurrent(handle) &&
         resolveFramebuffer(handle) != nullptr;
}

BufferHandle
D3D12Device::createBuffer(BufferUsage usage, size_t capacityBytes)
{
  if (capacityBytes == 0) {
    return {};
  }
  std::unique_ptr<D3D12Buffer> buffer = std::make_unique<D3D12Buffer>();
  buffer->usage = usage;
  buffer->capacity = capacityBytes;
  if (!createBufferMemory(buffer->memory, capacityBytes)) {
    return {};
  }
  const BufferHandle handle = m_bufferHandles.allocate();
  Entry<D3D12Buffer>& entry = slotEntry(m_buffers, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(buffer);
  return handle;
}

bool
D3D12Device::destroyBuffer(BufferHandle handle)
{
  D3D12Buffer* buffer = resolveBuffer(handle);
  if (buffer == nullptr) {
    Logger::LogWarning("DestroyBuffer: stale buffer handle ignored");
    return false;
  }
  retireMemory(buffer->memory);
  m_buffers[handle.slot] = Entry<D3D12Buffer>{};
  m_recorded.vertexKnown = false;
  return m_bufferHandles.release(handle);
}

bool
D3D12Device::isBufferValid(BufferHandle handle) const
{
  return m_bufferHandles.isCurrent(handle) && resolveBuffer(handle) != nullptr;
}

D3D12Mesh*
D3D12Device::resolveMesh(MeshHandle handle) const
{
  return slotResource(m_meshes, handle.slot, handle.generation);
}

D3D12Program*
D3D12Device::resolveProgram(ShaderHandle handle) const
{
  return slotResource(m_programs, handle.slot, handle.generation);
}

D3D12Texture*
D3D12Device::resolveTexture(TextureHandle handle) const
{
  return slotResource(m_textures, handle.slot, handle.generation);
}

D3D12Framebuffer*
D3D12Device::resolveFramebuffer(FramebufferHandle handle) const
{
  return slotResource(m_framebuffers, handle.slot, handle.generation);
}

D3D12Buffer*
D3D12Device::resolveBuffer(BufferHandle handle) const
{
  return slotResource(m_buffers, handle.slot, handle.generation);
}
