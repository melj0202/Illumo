#include "VulkanDevice.h"
#include "VulkanTexels.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cstring>

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

static VkComponentMapping
componentsForChannels(int channels)
{
  VkComponentMapping mapping{ VK_COMPONENT_SWIZZLE_IDENTITY,
                              VK_COMPONENT_SWIZZLE_IDENTITY,
                              VK_COMPONENT_SWIZZLE_IDENTITY,
                              VK_COMPONENT_SWIZZLE_IDENTITY };
  if (channels == 1) {
    // OpenGL swizzles its R8 textures to (r, r, r, 1).
    mapping.r = VK_COMPONENT_SWIZZLE_R;
    mapping.g = VK_COMPONENT_SWIZZLE_R;
    mapping.b = VK_COMPONENT_SWIZZLE_R;
    mapping.a = VK_COMPONENT_SWIZZLE_ONE;
  } else if (channels == 3) {
    mapping.a = VK_COMPONENT_SWIZZLE_ONE;
  }
  return mapping;
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

static const VkComponentMapping kIdentity{ VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY };

std::unique_ptr<VulkanMesh>
VulkanDevice::buildMesh(const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize,
                        MeshVertexLayout layout,
                        bool dynamic)
{
  if (vertexSize == 0 || (vertices == nullptr && !dynamic)) {
    return nullptr;
  }
  std::unique_ptr<VulkanMesh> mesh = std::make_unique<VulkanMesh>();
  mesh->layout = layout;
  mesh->dynamic = dynamic;
  mesh->vertexCapacity = vertexSize;
  mesh->indexCapacity = indexSize;
  if (!createBufferMemory(
        mesh->vertices, vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
    return nullptr;
  }
  if (indexSize > 0) {
    if (!createBufferMemory(
          mesh->indices, indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
      retireBuffer(mesh->vertices);
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
VulkanDevice::releaseMesh(VulkanMesh& mesh)
{
  retireBuffer(mesh.vertices);
  retireBuffer(mesh.indices);
}

MeshHandle
VulkanDevice::createMesh(const void* vertices,
                         size_t vertexSize,
                         const void* indices,
                         size_t indexSize,
                         MeshVertexLayout layout,
                         bool dynamic)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.createMesh");
  std::unique_ptr<VulkanMesh> mesh =
    buildMesh(vertices, vertexSize, indices, indexSize, layout, dynamic);
  if (!mesh) {
    Logger::LogWarning("CreateMesh: the GPU mesh could not be created (" +
                       std::to_string(vertexSize) + " vertex bytes, " +
                       std::to_string(indexSize) + " index bytes)");
    return {};
  }
  const MeshHandle handle = m_meshHandles.allocate();
  Entry<VulkanMesh>& entry = slotEntry(m_meshes, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(mesh);
  return handle;
}

bool
VulkanDevice::replaceMesh(MeshHandle handle,
                          const void* vertices,
                          size_t vertexSize,
                          const void* indices,
                          size_t indexSize,
                          MeshVertexLayout layout,
                          bool dynamic)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.replaceMesh");
  VulkanMesh* current = resolveMesh(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceMesh: stale mesh handle ignored");
    return false;
  }
  std::unique_ptr<VulkanMesh> replacement =
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
VulkanDevice::destroyMesh(MeshHandle handle)
{
  VulkanMesh* mesh = resolveMesh(handle);
  if (mesh == nullptr) {
    Logger::LogWarning("DestroyMesh: stale mesh handle ignored");
    return false;
  }
  releaseMesh(*mesh);
  m_meshes[handle.slot] = Entry<VulkanMesh>{};
  return m_meshHandles.release(handle);
}

bool
VulkanDevice::isMeshValid(MeshHandle handle) const
{
  return m_meshHandles.isCurrent(handle) && resolveMesh(handle) != nullptr;
}

std::unique_ptr<VulkanProgram>
VulkanDevice::buildProgram(const ShaderSources& sources, std::string* error)
{
  std::unique_ptr<VulkanProgram> program = std::make_unique<VulkanProgram>();
  if (!compileGlslProgram(sources.vertexSource,
                          sources.fragmentSource,
                          &program->reflection,
                          error)) {
    return nullptr;
  }
  const GlslProgram& reflection = program->reflection;
  const VkDevice device = m_context.device();
  VkShaderModuleCreateInfo module{};
  module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  module.codeSize = reflection.vertexSpirv.size() * sizeof(uint32_t);
  module.pCode = reflection.vertexSpirv.data();
  if (vkCreateShaderModule(device, &module, nullptr, &program->vertexModule) !=
      VK_SUCCESS) {
    *error = "The vertex shader module could not be created";
    return nullptr;
  }
  module.codeSize = reflection.fragmentSpirv.size() * sizeof(uint32_t);
  module.pCode = reflection.fragmentSpirv.data();
  if (vkCreateShaderModule(
        device, &module, nullptr, &program->fragmentModule) != VK_SUCCESS) {
    releaseProgram(*program);
    *error = "The fragment shader module could not be created";
    return nullptr;
  }
  if (reflection.bindings.size() > 32) {
    releaseProgram(*program);
    *error = "The shader program uses more than 32 uniform blocks and samplers";
    return nullptr;
  }
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  for (const GlslBinding& binding : reflection.bindings) {
    VkDescriptorSetLayoutBinding layout{};
    layout.binding = binding.binding;
    layout.descriptorType = binding.kind == GlslBindingKind::DefaultBlock ||
                                binding.kind == GlslBindingKind::NamedBlock
                              ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                              : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layout.descriptorCount = 1;
    layout.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(layout);
  }
  VkDescriptorSetLayoutCreateInfo setLayout{};
  setLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  setLayout.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  setLayout.bindingCount = static_cast<uint32_t>(bindings.size());
  setLayout.pBindings = bindings.data();
  if (vkCreateDescriptorSetLayout(
        device, &setLayout, nullptr, &program->setLayout) != VK_SUCCESS) {
    releaseProgram(*program);
    *error = "The shader program's descriptor layout could not be created";
    return nullptr;
  }
  VkPipelineLayoutCreateInfo pipelineLayout{};
  pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayout.setLayoutCount = 1;
  pipelineLayout.pSetLayouts = &program->setLayout;
  if (vkCreatePipelineLayout(
        device, &pipelineLayout, nullptr, &program->pipelineLayout) !=
      VK_SUCCESS) {
    releaseProgram(*program);
    *error = "The shader program's pipeline layout could not be created";
    return nullptr;
  }
  program->blockData.assign(reflection.defaultBlockSize, 0);
  program->samplerUnits.assign(reflection.samplerNames.size(), 0);
  for (const GlslUniform& uniform : reflection.uniforms) {
    VulkanUniformSlot slot;
    slot.type = uniform.type;
    slot.offset = uniform.offset;
    slot.arraySize = uniform.arraySize;
    slot.arrayStride = uniform.arrayStride;
    program->uniforms[uniform.name] = slot;
  }
  for (size_t index = 0; index < reflection.samplerNames.size(); ++index) {
    VulkanUniformSlot slot;
    slot.type = GlslValueType::Int;
    slot.sampler = static_cast<int>(index);
    program->uniforms[reflection.samplerNames[index]] = slot;
  }
  return program;
}

void
VulkanDevice::releaseProgram(VulkanProgram& program)
{
  Retired retired;
  retired.modules[0] = program.vertexModule;
  retired.modules[1] = program.fragmentModule;
  retired.setLayout = program.setLayout;
  retired.pipelineLayout = program.pipelineLayout;
  for (std::pair<const VulkanPipelineKey, VkPipeline>& pipeline :
       program.pipelines) {
    retired.pipelines.push_back(pipeline.second);
  }
  if (m_recorded.pushedLayout == program.pipelineLayout) {
    m_recorded.pushedLayout = VK_NULL_HANDLE;
  }
  if (!retired.pipelines.empty()) {
    m_recorded.pipeline = VK_NULL_HANDLE;
  }
  if (m_lastPipelineProgram == &program) {
    m_lastPipelineProgram = nullptr;
  }
  program.vertexModule = VK_NULL_HANDLE;
  program.fragmentModule = VK_NULL_HANDLE;
  program.setLayout = VK_NULL_HANDLE;
  program.pipelineLayout = VK_NULL_HANDLE;
  program.pipelines.clear();
  retire(std::move(retired));
}

ShaderHandle
VulkanDevice::createShader(const ShaderSources& sources, std::string* error)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.createShader");
  std::unique_ptr<VulkanProgram> program = buildProgram(sources, error);
  if (!program) {
    return {};
  }
  const ShaderHandle handle = m_shaderHandles.allocate();
  Entry<VulkanProgram>& entry = slotEntry(m_programs, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(program);
  return handle;
}

bool
VulkanDevice::replaceShader(ShaderHandle handle,
                            const ShaderSources& sources,
                            std::string* error)
{
  VulkanProgram* current = resolveProgram(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceShaderProgram: stale shader handle ignored");
    return false;
  }
  std::unique_ptr<VulkanProgram> replacement = buildProgram(sources, error);
  if (!replacement) {
    return false;
  }
  releaseProgram(*current);
  m_programs[handle.slot].resource = std::move(replacement);
  return true;
}

bool
VulkanDevice::destroyShader(ShaderHandle handle)
{
  VulkanProgram* program = resolveProgram(handle);
  if (program == nullptr) {
    Logger::LogWarning("DestroyShaderProgram: stale shader handle ignored");
    return false;
  }
  releaseProgram(*program);
  m_programs[handle.slot] = Entry<VulkanProgram>{};
  return m_shaderHandles.release(handle);
}

bool
VulkanDevice::isShaderValid(ShaderHandle handle) const
{
  return m_shaderHandles.isCurrent(handle) && resolveProgram(handle) != nullptr;
}

std::unique_ptr<VulkanTexture>
VulkanDevice::buildTexture(const unsigned char* data,
                           int width,
                           int height,
                           int channels,
                           const TextureOptions& options)
{
  const int maximum = maxTextureSize();
  if (data == nullptr || width <= 0 || height <= 0 || width > maximum ||
      height > maximum || (channels != 1 && channels != 3 && channels != 4)) {
    return nullptr;
  }
  std::unique_ptr<VulkanTexture> texture = std::make_unique<VulkanTexture>();
  texture->channels = channels;
  texture->storageBytes = channels == 1 ? 1 : 4;
  texture->format = channels == 1 ? TextureFormat::R8 : TextureFormat::RGBA8;
  const uint32_t levels =
    options.generateMipmaps ? mipLevelsFor(width, height) : 1u;
  VkImageUsageFlags usage =
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (levels > 1) {
    usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  }
  const VkFormat format =
    channels == 1 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
  if (!createImage(texture->image,
                   format,
                   static_cast<uint32_t>(width),
                   static_cast<uint32_t>(height),
                   levels,
                   1,
                   VK_SAMPLE_COUNT_1_BIT,
                   usage,
                   0) ||
      !createImageView(texture->image,
                       VK_IMAGE_VIEW_TYPE_2D,
                       componentsForChannels(channels),
                       VK_IMAGE_ASPECT_COLOR_BIT,
                       &texture->image.sampledView)) {
    retireImage(texture->image);
    return nullptr;
  }
  std::vector<unsigned char> stored(static_cast<size_t>(width) *
                                    static_cast<size_t>(height) *
                                    static_cast<size_t>(texture->storageBytes));
  convertTexelsForStorage(
    data, width, height, channels, 0, texture->storageBytes, stored.data());
  uploadToImage(texture->image,
                0,
                0,
                0,
                width,
                height,
                stored.data(),
                static_cast<size_t>(texture->storageBytes));
  generateMipmaps(texture->image);
  texture->sampler =
    samplerFor(options.filter, options.wrapX, options.wrapY, levels > 1, false);
  return texture;
}

std::unique_ptr<VulkanTexture>
VulkanDevice::buildCubemap(const std::array<const unsigned char*, 6>& faces,
                           int width,
                           int height,
                           int channels)
{
  const int size = width;
  if (size <= 0 || width != height ||
      size > static_cast<int>(m_context.limits().maxImageDimensionCube) ||
      (channels != 1 && channels != 3 && channels != 4)) {
    return nullptr;
  }
  for (const unsigned char* face : faces) {
    if (face == nullptr) {
      return nullptr;
    }
  }
  std::unique_ptr<VulkanTexture> texture = std::make_unique<VulkanTexture>();
  texture->channels = normalizeChannels(channels);
  texture->cubemap = true;
  texture->storageBytes = 0;
  const int storageBytes = channels == 1 ? 1 : 4;
  const VkFormat format =
    channels == 1 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
  if (!createImage(texture->image,
                   format,
                   static_cast<uint32_t>(size),
                   static_cast<uint32_t>(size),
                   1,
                   6,
                   VK_SAMPLE_COUNT_1_BIT,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                   VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) ||
      !createImageView(texture->image,
                       VK_IMAGE_VIEW_TYPE_CUBE,
                       componentsForChannels(channels),
                       VK_IMAGE_ASPECT_COLOR_BIT,
                       &texture->image.sampledView)) {
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
                  static_cast<size_t>(storageBytes));
  }
  texture->sampler = samplerFor(TextureFilter::Linear,
                                TextureWrap::ClampToEdge,
                                TextureWrap::ClampToEdge,
                                false,
                                false);
  return texture;
}

std::unique_ptr<VulkanTexture>
VulkanDevice::buildRenderTarget(int width,
                                int height,
                                TextureFormat format,
                                TextureFilter filter,
                                TextureWrap wrap)
{
  const VkFormat vkFormat = m_context.formatFor(format);
  const VkPhysicalDeviceLimits& limits = m_context.limits();
  if (vkFormat == VK_FORMAT_UNDEFINED || width <= 0 || height <= 0 ||
      static_cast<uint32_t>(width) > limits.maxFramebufferWidth ||
      static_cast<uint32_t>(height) > limits.maxFramebufferHeight) {
    return nullptr;
  }
  const bool depth = format == TextureFormat::Depth24 ||
                     format == TextureFormat::Depth24Stencil8;
  std::unique_ptr<VulkanTexture> texture = std::make_unique<VulkanTexture>();
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
  const VkImageUsageFlags usage =
    depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
          : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (!createImage(texture->image,
                   vkFormat,
                   static_cast<uint32_t>(width),
                   static_cast<uint32_t>(height),
                   1,
                   1,
                   VK_SAMPLE_COUNT_1_BIT,
                   usage,
                   0)) {
    return nullptr;
  }
  VkComponentMapping sampled = kIdentity;
  if (format == TextureFormat::RGB8) {
    sampled.a = VK_COMPONENT_SWIZZLE_ONE;
  }
  if (!createImageView(texture->image,
                       VK_IMAGE_VIEW_TYPE_2D,
                       kIdentity,
                       texture->image.aspect,
                       &texture->image.attachmentView) ||
      !createImageView(texture->image,
                       VK_IMAGE_VIEW_TYPE_2D,
                       sampled,
                       depth ? VK_IMAGE_ASPECT_DEPTH_BIT
                             : VK_IMAGE_ASPECT_COLOR_BIT,
                       &texture->image.sampledView)) {
    retireImage(texture->image);
    return nullptr;
  }
  texture->sampler = depth ? samplerFor(filter,
                                        TextureWrap::ClampToEdge,
                                        TextureWrap::ClampToEdge,
                                        false,
                                        true)
                           : samplerFor(filter, wrap, wrap, false, false);
  return texture;
}

void
VulkanDevice::releaseTexture(VulkanTexture& texture)
{
  if (m_renderingActive) {
    for (unsigned index = 0; index < m_target.colorCount; ++index) {
      if (m_target.colors[index].image == &texture.image) {
        endRendering();
      }
    }
    if (m_target.depth.image == &texture.image) {
      endRendering();
    }
  }
  retireImage(texture.image);
}

TextureHandle
VulkanDevice::registerTexture(std::unique_ptr<VulkanTexture> texture)
{
  const TextureHandle handle = m_textureHandles.allocate();
  Entry<VulkanTexture>& entry = slotEntry(m_textures, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(texture);
  return handle;
}

int
VulkanDevice::maxTextureSize() const
{
  return static_cast<int>(m_context.limits().maxImageDimension2D);
}

TextureHandle
VulkanDevice::createTexture(const unsigned char* data,
                            int width,
                            int height,
                            int channels,
                            const TextureOptions& options)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.createTexture");
  if (data == nullptr || width <= 0 || height <= 0) {
    Logger::LogWarning("CreateTexture: missing pixels or invalid size " +
                       std::to_string(width) + "x" + std::to_string(height));
    return {};
  }
  std::unique_ptr<VulkanTexture> texture =
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
VulkanDevice::createCubemap(const std::array<const unsigned char*, 6>& faces,
                            int width,
                            int height,
                            int channels)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.createCubemap");
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
  std::unique_ptr<VulkanTexture> texture =
    buildCubemap(faces, width, height, channels);
  if (!texture) {
    Logger::LogWarning("CreateCubemap: the GPU cubemap could not be created");
    return {};
  }
  return registerTexture(std::move(texture));
}

bool
VulkanDevice::replaceTexture(TextureHandle handle,
                             const unsigned char* data,
                             int width,
                             int height,
                             int channels,
                             const TextureOptions& options)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.replaceTexture");
  VulkanTexture* current = resolveTexture(handle);
  if (current == nullptr) {
    Logger::LogWarning("ReplaceTexture: stale texture handle ignored");
    return false;
  }
  if (current->cubemap || data == nullptr || width <= 0 || height <= 0) {
    Logger::LogWarning("ReplaceTexture: invalid texture data ignored");
    return false;
  }
  std::unique_ptr<VulkanTexture> replacement =
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
VulkanDevice::replaceCubemap(TextureHandle handle,
                             const std::array<const unsigned char*, 6>& faces,
                             int width,
                             int height,
                             int channels)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.replaceCubemap");
  VulkanTexture* current = resolveTexture(handle);
  if (current == nullptr || !current->cubemap || width <= 0 ||
      width != height || (channels != 1 && channels != 3 && channels != 4)) {
    return false;
  }
  std::unique_ptr<VulkanTexture> replacement =
    buildCubemap(faces, width, height, channels);
  if (!replacement) {
    return false;
  }
  releaseTexture(*current);
  m_textures[handle.slot].resource = std::move(replacement);
  return true;
}

bool
VulkanDevice::destroyTexture(TextureHandle handle)
{
  VulkanTexture* texture = resolveTexture(handle);
  if (texture == nullptr) {
    Logger::LogWarning("DestroyTexture: stale texture handle ignored");
    return false;
  }
  releaseTexture(*texture);
  m_textures[handle.slot] = Entry<VulkanTexture>{};
  return m_textureHandles.release(handle);
}

bool
VulkanDevice::isTextureValid(TextureHandle handle) const
{
  return m_textureHandles.isCurrent(handle) &&
         resolveTexture(handle) != nullptr;
}

TextureInfo
VulkanDevice::textureInfo(TextureHandle handle) const
{
  TextureInfo info;
  const VulkanTexture* texture = resolveTexture(handle);
  if (texture == nullptr) {
    return info;
  }
  info.width = static_cast<int>(texture->image.width);
  info.height = static_cast<int>(texture->image.height);
  info.channels = texture->channels;
  return info;
}

FramebufferHandle
VulkanDevice::createFramebuffer(const FramebufferDesc& desc,
                                FramebufferAttachments* outAttachments)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.createFramebuffer");
  if (desc.width <= 0 || desc.height <= 0) {
    Logger::LogError("CreateFramebuffer: invalid dimensions");
    return {};
  }
  const uint32_t maximumColors = std::min(
    kVulkanMaxColorAttachments, m_context.limits().maxColorAttachments);
  if (desc.colorAttachments.size() > maximumColors ||
      (desc.colorAttachments.empty() &&
       desc.depthStencilFormat == TextureFormat::None)) {
    Logger::LogError("CreateFramebuffer: framebuffer is incomplete");
    return {};
  }
  std::unique_ptr<VulkanFramebuffer> framebuffer =
    std::make_unique<VulkanFramebuffer>();
  framebuffer->width = desc.width;
  framebuffer->height = desc.height;
  bool failed = false;
  for (size_t index = 0; index < desc.colorAttachments.size(); ++index) {
    const FramebufferAttachmentDesc& attachment = desc.colorAttachments[index];
    const bool colorFormat =
      attachment.format != TextureFormat::Depth24 &&
      attachment.format != TextureFormat::Depth24Stencil8 &&
      attachment.format != TextureFormat::None;
    std::unique_ptr<VulkanTexture> texture =
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
    std::unique_ptr<VulkanTexture> texture =
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
  Entry<VulkanFramebuffer>& entry = slotEntry(m_framebuffers, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(framebuffer);
  // OpenGL creation leaves the default framebuffer bound and texture unit
  // 0's 2D target empty.
  m_framebuffer = FramebufferHandle{};
  m_units[0].texture2D = TextureHandle{};
  return handle;
}

bool
VulkanDevice::destroyFramebuffer(FramebufferHandle handle)
{
  VulkanFramebuffer* framebuffer = resolveFramebuffer(handle);
  if (framebuffer == nullptr) {
    Logger::LogWarning("DestroyFramebuffer: stale framebuffer handle ignored");
    return false;
  }
  if (m_renderingActive && !m_target.backbuffer && m_target.handle == handle) {
    endRendering();
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
  m_framebuffers[handle.slot] = Entry<VulkanFramebuffer>{};
  return m_framebufferHandles.release(handle);
}

bool
VulkanDevice::isFramebufferValid(FramebufferHandle handle) const
{
  return m_framebufferHandles.isCurrent(handle) &&
         resolveFramebuffer(handle) != nullptr;
}

BufferHandle
VulkanDevice::createBuffer(BufferUsage usage, size_t capacityBytes)
{
  if (capacityBytes == 0) {
    return {};
  }
  std::unique_ptr<VulkanBuffer> buffer = std::make_unique<VulkanBuffer>();
  buffer->usage = usage;
  buffer->capacity = capacityBytes;
  if (!createBufferMemory(buffer->memory,
                          capacityBytes,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
    return {};
  }
  const BufferHandle handle = m_bufferHandles.allocate();
  Entry<VulkanBuffer>& entry = slotEntry(m_buffers, handle.slot);
  entry.generation = handle.generation;
  entry.resource = std::move(buffer);
  return handle;
}

bool
VulkanDevice::destroyBuffer(BufferHandle handle)
{
  VulkanBuffer* buffer = resolveBuffer(handle);
  if (buffer == nullptr) {
    Logger::LogWarning("DestroyBuffer: stale buffer handle ignored");
    return false;
  }
  retireBuffer(buffer->memory);
  m_buffers[handle.slot] = Entry<VulkanBuffer>{};
  return m_bufferHandles.release(handle);
}

bool
VulkanDevice::isBufferValid(BufferHandle handle) const
{
  return m_bufferHandles.isCurrent(handle) && resolveBuffer(handle) != nullptr;
}

VulkanMesh*
VulkanDevice::resolveMesh(MeshHandle handle) const
{
  return slotResource(m_meshes, handle.slot, handle.generation);
}

VulkanProgram*
VulkanDevice::resolveProgram(ShaderHandle handle) const
{
  return slotResource(m_programs, handle.slot, handle.generation);
}

VulkanTexture*
VulkanDevice::resolveTexture(TextureHandle handle) const
{
  return slotResource(m_textures, handle.slot, handle.generation);
}

VulkanFramebuffer*
VulkanDevice::resolveFramebuffer(FramebufferHandle handle) const
{
  return slotResource(m_framebuffers, handle.slot, handle.generation);
}

VulkanBuffer*
VulkanDevice::resolveBuffer(BufferHandle handle) const
{
  return slotResource(m_buffers, handle.slot, handle.generation);
}
