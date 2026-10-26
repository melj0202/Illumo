#include "D3D12Driver.h"
#include "Rendering/Gpu/GlslProgramCache.h"
#include "Rendering/Gpu/GpuTexels.h"

#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cstring>
#include <span>
#include <string_view>

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

std::unique_ptr<GpuMesh>
D3D12Driver::buildMesh(size_t vertexSize, size_t indexSize)
{
  std::unique_ptr<D3D12Mesh> mesh = std::make_unique<D3D12Mesh>();
  if (!createBufferMemory(mesh->vertices, vertexSize)) {
    return nullptr;
  }
  if (indexSize > 0 && !createBufferMemory(mesh->indices, indexSize)) {
    retireMemory(mesh->vertices);
    return nullptr;
  }
  return mesh;
}

void
D3D12Driver::releaseMesh(GpuMesh& record)
{
  D3D12Mesh& mesh = static_cast<D3D12Mesh&>(record);
  retireMemory(mesh.vertices);
  retireMemory(mesh.indices);
  m_recorded.vertexKnown = false;
  m_recorded.indexBuffer = 0;
}

// A program's vertex and pixel DXBC: from the cache when an earlier run
// translated and compiled the same SPIR-V and bindings with the same build
// (D-R38), otherwise through SPIRV-Cross and FXC, then stored.
static bool
compileProgramCode(GpuShaderCache& cache,
                   const GlslProgram& reflection,
                   std::vector<unsigned char>* vertexCode,
                   std::vector<unsigned char>* pixelCode,
                   std::string* error)
{
  std::vector<unsigned char> bindings;
  for (const GlslBinding& binding : reflection.bindings) {
    bindings.push_back(static_cast<unsigned char>(binding.kind));
    for (unsigned int index = 0u; index < 4u; ++index) {
      bindings.push_back(
        static_cast<unsigned char>(binding.binding >> (index * 8u)));
    }
  }
  const std::string_view version(BuildInfo::FullVersion);
  const std::span<const unsigned char> parts[] = {
    { reinterpret_cast<const unsigned char*>(version.data()), version.size() },
    { reinterpret_cast<const unsigned char*>(reflection.vertexSpirv.data()),
      reflection.vertexSpirv.size() * sizeof(uint32_t) },
    { reinterpret_cast<const unsigned char*>(reflection.fragmentSpirv.data()),
      reflection.fragmentSpirv.size() * sizeof(uint32_t) },
    { bindings.data(), bindings.size() },
  };
  const Sha256::Digest key = GpuShaderCache::key("d3d12-dxbc-1", parts);
  // Entry: vertex size (u32), vertex DXBC, pixel DXBC.
  std::vector<unsigned char> blob;
  if (cache.load(key, &blob) && blob.size() >= 4u) {
    const std::uint32_t vertexSize =
      static_cast<std::uint32_t>(blob[0]) |
      (static_cast<std::uint32_t>(blob[1]) << 8u) |
      (static_cast<std::uint32_t>(blob[2]) << 16u) |
      (static_cast<std::uint32_t>(blob[3]) << 24u);
    if (vertexSize != 0u && vertexSize < blob.size() - 4u) {
      vertexCode->assign(blob.begin() + 4, blob.begin() + 4 + vertexSize);
      pixelCode->assign(blob.begin() + 4 + vertexSize, blob.end());
      return true;
    }
  }
  std::string vertexHlsl;
  std::string pixelHlsl;
  translateProgramToHlsl(reflection, &vertexHlsl, &pixelHlsl);
  if (!compileHlsl(vertexHlsl, true, vertexCode, error) ||
      !compileHlsl(pixelHlsl, false, pixelCode, error)) {
    return false;
  }
  blob.clear();
  const std::uint32_t vertexSize =
    static_cast<std::uint32_t>(vertexCode->size());
  for (unsigned int index = 0u; index < 4u; ++index) {
    blob.push_back(static_cast<unsigned char>(vertexSize >> (index * 8u)));
  }
  blob.insert(blob.end(), vertexCode->begin(), vertexCode->end());
  blob.insert(blob.end(), pixelCode->begin(), pixelCode->end());
  cache.store(key, blob);
  return true;
}

std::unique_ptr<GpuProgram>
D3D12Driver::buildProgram(const ShaderSources& sources, std::string* error)
{
  std::unique_ptr<D3D12Program> program = std::make_unique<D3D12Program>();
  if (!compileGlslProgramCached(m_shaderCache,
                                sources.vertexSource,
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
  if (!compileProgramCode(m_shaderCache,
                          reflection,
                          &program->vertexCode,
                          &program->pixelCode,
                          error) ||
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
D3D12Driver::releaseProgram(GpuProgram& record)
{
  D3D12Program& program = static_cast<D3D12Program&>(record);
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

std::unique_ptr<GpuTexture>
D3D12Driver::buildTexture(const unsigned char* data,
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

std::unique_ptr<GpuTexture>
D3D12Driver::buildCubemap(const std::array<const unsigned char*, 6>& faces,
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

std::unique_ptr<GpuTexture>
D3D12Driver::buildRenderTarget(int width,
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
D3D12Driver::releaseTexture(GpuTexture& record)
{
  D3D12Texture& texture = static_cast<D3D12Texture&>(record);
  // A bound attachment's view stays valid until the GPU is done; later
  // draws bind their target again.
  m_recorded.targetKnown = false;
  retireImage(texture.image);
}

unsigned
D3D12Driver::maxColorAttachments() const
{
  return kD3D12MaxColorAttachments;
}

void
D3D12Driver::releaseFramebufferTarget(FramebufferHandle /*handle*/)
{
  // Whichever target is bound, the next draw binds its target again.
  m_recorded.targetKnown = false;
}

std::unique_ptr<GpuBuffer>
D3D12Driver::buildBuffer(size_t capacityBytes)
{
  std::unique_ptr<D3D12Buffer> buffer = std::make_unique<D3D12Buffer>();
  if (!createBufferMemory(buffer->memory, capacityBytes)) {
    return nullptr;
  }
  return buffer;
}

void
D3D12Driver::releaseBuffer(GpuBuffer& buffer)
{
  retireMemory(static_cast<D3D12Buffer&>(buffer).memory);
  m_recorded.vertexKnown = false;
}
