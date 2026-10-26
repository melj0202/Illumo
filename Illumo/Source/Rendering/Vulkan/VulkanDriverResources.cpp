#include "Rendering/Gpu/GlslProgramCache.h"
#include "Rendering/Gpu/GpuTexels.h"
#include "VulkanDriver.h"

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

std::unique_ptr<GpuMesh>
VulkanDriver::buildMesh(size_t vertexSize, size_t indexSize)
{
  std::unique_ptr<VulkanMesh> mesh = std::make_unique<VulkanMesh>();
  if (!createBufferMemory(
        mesh->vertices, vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
    return nullptr;
  }
  if (indexSize > 0 && !createBufferMemory(mesh->indices,
                                           indexSize,
                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
    retireBuffer(mesh->vertices);
    return nullptr;
  }
  return mesh;
}

void
VulkanDriver::releaseMesh(GpuMesh& record)
{
  VulkanMesh& mesh = static_cast<VulkanMesh&>(record);
  retireBuffer(mesh.vertices);
  retireBuffer(mesh.indices);
}

std::unique_ptr<GpuProgram>
VulkanDriver::buildProgram(const ShaderSources& sources, std::string* error)
{
  std::unique_ptr<VulkanProgram> program = std::make_unique<VulkanProgram>();
  if (!compileGlslProgramCached(m_shaderCache,
                                sources.vertexSource,
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
  program->uniforms.configure(reflection);
  return program;
}

void
VulkanDriver::releaseProgram(GpuProgram& record)
{
  VulkanProgram& program = static_cast<VulkanProgram&>(record);
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

std::unique_ptr<GpuTexture>
VulkanDriver::buildTexture(const unsigned char* data,
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

std::unique_ptr<GpuTexture>
VulkanDriver::buildCubemap(const std::array<const unsigned char*, 6>& faces,
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

std::unique_ptr<GpuTexture>
VulkanDriver::buildRenderTarget(int width,
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
VulkanDriver::releaseTexture(GpuTexture& record)
{
  VulkanTexture& texture = static_cast<VulkanTexture&>(record);
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

int
VulkanDriver::maxTextureSize() const
{
  return static_cast<int>(m_context.limits().maxImageDimension2D);
}

unsigned
VulkanDriver::maxColorAttachments() const
{
  return std::min(kVulkanMaxColorAttachments,
                  m_context.limits().maxColorAttachments);
}

void
VulkanDriver::releaseFramebufferTarget(FramebufferHandle handle)
{
  if (m_renderingActive && !m_target.backbuffer && m_target.handle == handle) {
    endRendering();
  }
}

std::unique_ptr<GpuBuffer>
VulkanDriver::buildBuffer(size_t capacityBytes)
{
  std::unique_ptr<VulkanBuffer> buffer = std::make_unique<VulkanBuffer>();
  if (!createBufferMemory(buffer->memory,
                          capacityBytes,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
    return nullptr;
  }
  return buffer;
}

void
VulkanDriver::releaseBuffer(GpuBuffer& buffer)
{
  retireBuffer(static_cast<VulkanBuffer&>(buffer).memory);
}
