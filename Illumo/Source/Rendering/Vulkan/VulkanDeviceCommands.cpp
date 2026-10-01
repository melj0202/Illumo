#include "Rendering/Gpu/GpuTexels.h"
#include "VulkanDevice.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

void
VulkanDevice::reportFrameError(const char* message)
{
  if (m_frameError.empty()) {
    m_frameError = message;
  }
  Logger::LogWarning(message);
}

void
VulkanDevice::executeQueue(CommandQueue& queue)
{
  ILLUMO_PROFILE_ZONE("VulkanDevice.ExecuteCommandQueue");
  m_stats.submits += 1;
  m_stats.commands += queue.GetCommandCount();
  // As GLDevice does, each submission starts without a program or mesh and
  // without knowing which framebuffer is bound.
  m_program = ShaderHandle{};
  m_mesh = MeshHandle{};
  m_framebufferKnown = false;
  m_instanceMesh = MeshHandle{};
  m_instanceCapacity = 0;
  for (size_t index = 0; index < queue.GetCommandCount(); ++index) {
    const RenderCommand& command = queue.GetCommand(index);
    if (command.commandType == CommandType::ExecuteList) {
      executeList(command.executeList.list);
    } else {
      executeCommand(command);
    }
  }
}

void
VulkanDevice::executeList(const RecordedCommandList* list)
{
  if (list == nullptr || list->failed()) {
    reportFrameError("ExecuteList: missing or failed recorded list");
    return;
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.ExecuteList");
  m_stats.recordedLists += 1;
  m_stats.recordedCommands += list->size();
  for (size_t index = 0; index < list->size(); ++index) {
    executeCommand(list->at(index));
  }
}

void
VulkanDevice::executeCommand(const RenderCommand& command)
{
  switch (command.commandType) {
    case CommandType::SetPipelineState: {
      const PipelineState& state = command.pipelineState;
      m_state.depthTestEnabled = state.depthTestEnabled;
      m_state.blendEnabled = state.blendEnabled;
      if (state.blendEnabled) {
        m_state.blendSrc = state.blendSrc;
        m_state.blendDst = state.blendDst;
      }
      m_state.faceCullingEnabled = state.faceCullingEnabled;
      // OpenGL keeps the cull and front-face settings of the last state
      // that enabled culling.
      if (state.faceCullingEnabled) {
        m_cullFace = state.cullFace;
        m_frontFace = state.frontFace;
      }
      m_state.wireframe = state.wireframe;
      m_state.primitives = state.primitives;
      break;
    }

    case CommandType::SetViewport:
      m_viewport = command.viewport;
      break;

    case CommandType::SetScissorState:
      if (command.scissor.enabled) {
        m_scissor = command.scissor;
      } else {
        m_scissor.enabled = false;
      }
      break;

    case CommandType::SetFramebuffer: {
      m_framebufferKnown = true;
      if (!command.bindFramebuffer.handle.isValid()) {
        m_framebuffer = FramebufferHandle{};
        break;
      }
      if (resolveFramebuffer(command.bindFramebuffer.handle) == nullptr) {
        reportFrameError("SetFramebuffer: unknown framebuffer handle");
        m_framebuffer = FramebufferHandle{};
        break;
      }
      m_framebuffer = command.bindFramebuffer.handle;
      break;
    }

    case CommandType::SetShader:
      if (resolveProgram(command.bindShader.handle) == nullptr) {
        reportFrameError("SetShader: unknown shader handle");
        m_program = ShaderHandle{};
        break;
      }
      m_program = command.bindShader.handle;
      break;

    case CommandType::SetMesh:
      if (resolveMesh(command.bindMesh.handle) == nullptr) {
        reportFrameError("SetMesh: unknown mesh handle");
        m_mesh = MeshHandle{};
        break;
      }
      m_mesh = command.bindMesh.handle;
      break;

    case CommandType::SetTexture: {
      const VulkanTexture* texture = resolveTexture(command.bindTexture.handle);
      if (texture == nullptr) {
        reportFrameError("SetTexture: unknown texture handle");
        break;
      }
      const unsigned slot = command.bindTexture.slot;
      if (slot < kTextureUnits) {
        if (texture->cubemap) {
          m_units[slot].cube = command.bindTexture.handle;
        } else {
          m_units[slot].texture2D = command.bindTexture.handle;
        }
      }
      break;
    }

    case CommandType::SetUniformInt:
      setUniform(command.uniformInt.name,
                 GlslValueType::Int,
                 &command.uniformInt.value,
                 sizeof(int));
      break;

    case CommandType::SetUniformFloat:
      setUniform(command.uniformFloat.name,
                 GlslValueType::Float,
                 &command.uniformFloat.value,
                 sizeof(float));
      break;

    case CommandType::SetUniformVec2: {
      const float values[2] = { command.uniformVec2.x, command.uniformVec2.y };
      setUniform(
        command.uniformVec2.name, GlslValueType::Vec2, values, sizeof(values));
      break;
    }

    case CommandType::SetUniformVec3: {
      const float values[3] = { command.uniformVec3.x,
                                command.uniformVec3.y,
                                command.uniformVec3.z };
      setUniform(
        command.uniformVec3.name, GlslValueType::Vec3, values, sizeof(values));
      break;
    }

    case CommandType::SetUniformVec4: {
      const float values[4] = { command.uniformVec4.x,
                                command.uniformVec4.y,
                                command.uniformVec4.z,
                                command.uniformVec4.w };
      setUniform(
        command.uniformVec4.name, GlslValueType::Vec4, values, sizeof(values));
      break;
    }

    case CommandType::SetUniformMat4:
      if (command.uniformMat4.value == nullptr) {
        reportFrameError("SetUniformMat4: null matrix value");
        break;
      }
      setUniform(command.uniformMat4.name,
                 GlslValueType::Mat4,
                 command.uniformMat4.value,
                 16 * sizeof(float));
      break;

    case CommandType::UpdateTexture:
      updateTexture(command.updateTexture);
      break;

    case CommandType::ClearScreen:
      clear(true, true, false, true, command.clear, command.clearDepthValue);
      break;

    case CommandType::ClearDepthBuffer:
      clear(false, true, false, false, command.clear, command.clearDepthValue);
      break;

    case CommandType::ClearColorBuffer:
      clear(true, false, false, true, command.clear, command.clearDepthValue);
      break;

    case CommandType::ClearStencilBuffer:
      clear(false, false, true, false, command.clear, command.clearDepthValue);
      break;

    case CommandType::ClearAll:
      clear(true, true, true, true, command.clear, command.clearDepthValue);
      break;

    case CommandType::Draw:
      draw(DrawKind::Arrays,
           command.draw.elementCount,
           command.draw.first,
           1,
           "Draw");
      break;

    case CommandType::DrawIndexed:
      draw(DrawKind::Indexed,
           command.drawIndexed.elementCount,
           command.drawIndexed.firstIndex,
           1,
           "DrawIndexed");
      break;

    case CommandType::DrawInstanced:
      draw(DrawKind::ArraysInstanced,
           command.drawInstanced.elementCount,
           0,
           command.drawInstanced.instanceCount,
           "DrawInstanced");
      break;

    case CommandType::UpdateBuffer:
      updateMeshBuffer(command.updateBuffer, false);
      break;

    case CommandType::UpdateIndexBuffer:
      updateMeshBuffer(command.updateIndexBuffer, true);
      break;

    case CommandType::WriteBuffer:
      writeBuffer(command.writeBuffer);
      break;

    case CommandType::BindUniformBuffer: {
      const VulkanBuffer* buffer =
        resolveBuffer(command.bindUniformBuffer.handle);
      if (buffer == nullptr || buffer->usage != BufferUsage::Uniform ||
          command.bindUniformBuffer.binding >= kUniformBindings) {
        reportFrameError(
          "BindUniformBuffer: unknown or non-uniform buffer, or bad binding");
        break;
      }
      m_uniformBindings[command.bindUniformBuffer.binding] =
        command.bindUniformBuffer.handle;
      break;
    }

    case CommandType::SetInstanceStream: {
      const CmdInstanceStream& stream = command.instanceStream;
      const VulkanBuffer* buffer = resolveBuffer(stream.handle);
      const unsigned int stride = instanceLayoutStride(stream.layout);
      VulkanMesh* mesh = resolveMesh(m_mesh);
      if (buffer == nullptr || buffer->usage != BufferUsage::Instance ||
          stride == 0 || mesh == nullptr ||
          stream.offsetBytes >= buffer->capacity) {
        reportFrameError("SetInstanceStream: unknown or non-instance buffer, "
                         "bad layout, or no mesh bound");
        m_instanceMesh = MeshHandle{};
        m_instanceCapacity = 0;
        break;
      }
      mesh->instanceBuffer = stream.handle;
      mesh->instanceOffset = stream.offsetBytes;
      mesh->instanceLayout = stream.layout;
      m_instanceMesh = m_mesh;
      m_instanceCapacity = (buffer->capacity - stream.offsetBytes) / stride;
      break;
    }

    case CommandType::DrawIndexedInstanced: {
      const CmdDrawIndexedInstanced& drawCommand = command.drawIndexedInstanced;
      if (resolveProgram(m_program) == nullptr ||
          resolveMesh(m_mesh) == nullptr) {
        reportFrameError(
          "DrawIndexedInstanced: missing valid shader or mesh; ignored");
        break;
      }
      if (m_instanceMesh != m_mesh ||
          drawCommand.instanceCount > m_instanceCapacity) {
        reportFrameError("DrawIndexedInstanced: no instance stream on this "
                         "mesh, or more instances than it holds; ignored");
        break;
      }
      if (drawCommand.instanceCount == 0) {
        break;
      }
      draw(DrawKind::IndexedInstanced,
           drawCommand.elementCount,
           drawCommand.firstIndex,
           drawCommand.instanceCount,
           "DrawIndexedInstanced");
      break;
    }

    default:
      break;
  }
}

void
VulkanDevice::setUniform(const char* name,
                         GlslValueType given,
                         const void* value,
                         size_t bytes)
{
  // Uniform calls reach only the program bound in this submission, as
  // glUniform* reaches only the program in use.
  VulkanProgram* program = resolveProgram(m_program);
  if (program != nullptr) {
    program->uniforms.set(name, given, value, bytes);
  }
}

void
VulkanDevice::updateTexture(const CmdUpdateTexture& update)
{
  VulkanTexture* texture = resolveTexture(update.handle);
  if (texture == nullptr || update.data == nullptr) {
    reportFrameError("UpdateTexture: invalid handle or null data");
    return;
  }
  const int channels =
    update.channels == 0 ? texture->channels : update.channels;
  if (texture->cubemap || texture->storageBytes == 0 ||
      !validTextureUpdate(static_cast<int>(texture->image.width),
                          static_cast<int>(texture->image.height),
                          update.x,
                          update.y,
                          update.width,
                          update.height,
                          channels,
                          update.srcRowStride)) {
    reportFrameError("UpdateTexture: invalid rectangle, channels, or stride");
    return;
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.UpdateTexture");
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(update.width) *
                             static_cast<VkDeviceSize>(update.height) *
                             static_cast<VkDeviceSize>(texture->storageBytes);
  StagingSpan span;
  if (!allocateStaging(bytes, 16, &span)) {
    reportFrameError("Vulkan staging memory is exhausted");
    return;
  }
  convertTexelsForStorage(static_cast<const unsigned char*>(update.data),
                          update.width,
                          update.height,
                          channels,
                          update.srcRowStride,
                          texture->storageBytes,
                          span.mapped);
  VulkanImage& image = texture->image;
  VkCommandBuffer commands = transferCommands(image.useSerial);
  transition(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  VkBufferImageCopy region{};
  region.bufferOffset = span.offset;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = { update.x, update.y, 0 };
  region.imageExtent = { static_cast<uint32_t>(update.width),
                         static_cast<uint32_t>(update.height),
                         1 };
  vkCmdCopyBufferToImage(commands,
                         span.buffer,
                         image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1,
                         &region);
  transition(commands, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  m_stats.uploadBytes += static_cast<size_t>(update.width) *
                         static_cast<size_t>(update.height) *
                         static_cast<size_t>(channels);
  // OpenGL's upload leaves the texture bound to unit 0.
  m_units[0].texture2D = update.handle;
}

void
VulkanDevice::updateMeshBuffer(const CmdUpdateBuffer& update, bool indices)
{
  VulkanMesh* mesh = resolveMesh(update.handle);
  if (mesh == nullptr || update.data == nullptr) {
    reportFrameError(indices ? "UpdateIndexBuffer: invalid handle or null data"
                             : "UpdateBuffer: invalid handle or null data");
    return;
  }
  const size_t capacity = indices ? mesh->indexCapacity : mesh->vertexCapacity;
  VulkanBufferMemory& memory = indices ? mesh->indices : mesh->vertices;
  if (memory.buffer == VK_NULL_HANDLE || update.sizeBytes == 0 ||
      update.offsetBytes > capacity ||
      update.sizeBytes > capacity - update.offsetBytes) {
    reportFrameError(
      indices ? "UpdateIndexBuffer: range exceeds index capacity"
              : "UpdateBuffer: range exceeds enrolled vertex capacity");
    return;
  }
  uploadToBuffer(memory, update.offsetBytes, update.data, update.sizeBytes);
}

void
VulkanDevice::writeBuffer(const CmdWriteBuffer& write)
{
  VulkanBuffer* buffer = resolveBuffer(write.handle);
  if (buffer == nullptr || write.data == nullptr || write.sizeBytes == 0 ||
      write.offsetBytes > buffer->capacity ||
      write.sizeBytes > buffer->capacity - write.offsetBytes) {
    reportFrameError(
      "WriteBuffer: invalid handle, null data, or range beyond capacity");
    return;
  }
  uploadToBuffer(
    buffer->memory, write.offsetBytes, write.data, write.sizeBytes);
}

bool
VulkanDevice::resolveTarget(RenderTarget* target)
{
  *target = RenderTarget{};
  if (m_framebuffer.isValid()) {
    const VulkanFramebuffer* framebuffer = resolveFramebuffer(m_framebuffer);
    if (framebuffer != nullptr) {
      target->backbuffer = false;
      target->handle = m_framebuffer;
      target->width = framebuffer->width;
      target->height = framebuffer->height;
      for (TextureHandle handle : framebuffer->colorTextures) {
        VulkanTexture* texture = resolveTexture(handle);
        if (texture == nullptr ||
            target->colorCount >= kVulkanMaxColorAttachments) {
          return false;
        }
        target->colors[target->colorCount].image = &texture->image;
        target->colors[target->colorCount].restsReadable = true;
        target->colorFormats[target->colorCount] = texture->format;
        target->colorCount += 1;
      }
      if (framebuffer->depthTexture.isValid()) {
        VulkanTexture* depth = resolveTexture(framebuffer->depthTexture);
        if (depth == nullptr) {
          return false;
        }
        target->depth.image = &depth->image;
        target->depth.restsReadable = true;
      }
      return true;
    }
    // A deleted framebuffer leaves the default one bound, as in OpenGL.
    m_framebuffer = FramebufferHandle{};
  }
  if (m_backbuffer.color.image == VK_NULL_HANDLE) {
    return false;
  }
  target->backbuffer = true;
  target->width = m_backbuffer.width;
  target->height = m_backbuffer.height;
  target->samples = m_samples;
  target->colors[0].image = &m_backbuffer.color;
  target->colorFormats[0] = TextureFormat::RGBA8;
  target->colorCount = 1;
  target->depth.image = &m_backbuffer.depth;
  return true;
}

void
VulkanDevice::ensureRendering(const RenderTarget* resolved)
{
  ensureRecording();
  endMainTransfers();
  RenderTarget local;
  if (resolved == nullptr) {
    if (!resolveTarget(&local)) {
      return;
    }
    resolved = &local;
  }
  const RenderTarget& target = *resolved;
  if (m_renderingActive) {
    if (m_target.backbuffer == target.backbuffer &&
        m_target.handle == target.handle) {
      return;
    }
    endRendering();
  }
  VkCommandBuffer commands = m_slots[m_slotIndex].main;
  std::array<VkRenderingAttachmentInfo, kVulkanMaxColorAttachments> colors{};
  for (unsigned index = 0; index < target.colorCount; ++index) {
    VulkanImage& image = *target.colors[index].image;
    transition(commands, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    image.useSerial = m_recordingSerial;
    VkRenderingAttachmentInfo& attachment = colors[index];
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = image.attachmentView;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  }
  VkRenderingAttachmentInfo depth{};
  VkRenderingAttachmentInfo stencil{};
  const bool hasDepth = target.depth.image != nullptr;
  bool hasStencilAspect = false;
  if (hasDepth) {
    VulkanImage& image = *target.depth.image;
    transition(
      commands, image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    image.useSerial = m_recordingSerial;
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = image.attachmentView;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    hasStencilAspect = (image.aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
    stencil = depth;
  }
  VkRenderingInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  rendering.renderArea.extent = { static_cast<uint32_t>(target.width),
                                  static_cast<uint32_t>(target.height) };
  rendering.layerCount = 1;
  rendering.colorAttachmentCount = target.colorCount;
  rendering.pColorAttachments = colors.data();
  rendering.pDepthAttachment = hasDepth ? &depth : nullptr;
  rendering.pStencilAttachment = hasStencilAspect ? &stencil : nullptr;
  vkCmdBeginRendering(commands, &rendering);
  m_target = target;
  m_renderingActive = true;
}

void
VulkanDevice::endRendering()
{
  if (!m_renderingActive) {
    return;
  }
  VkCommandBuffer commands = m_slots[m_slotIndex].main;
  vkCmdEndRendering(commands);
  m_renderingActive = false;
  // Render-target textures rest shader-readable, the likely next use.
  for (unsigned index = 0; index < m_target.colorCount; ++index) {
    if (m_target.colors[index].restsReadable) {
      transition(commands,
                 *m_target.colors[index].image,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  }
  if (m_target.depth.image != nullptr && m_target.depth.restsReadable) {
    transition(commands,
               *m_target.depth.image,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
}

VkRect2D
VulkanDevice::scissorRect(const RenderTarget& target) const
{
  VkRect2D rect{};
  if (!m_scissor.enabled) {
    rect.extent = { static_cast<uint32_t>(target.width),
                    static_cast<uint32_t>(target.height) };
    return rect;
  }
  const long long left = std::max(0LL, static_cast<long long>(m_scissor.x));
  const long long bottom = std::max(0LL, static_cast<long long>(m_scissor.y));
  const long long right =
    std::min(static_cast<long long>(target.width),
             static_cast<long long>(m_scissor.x) + m_scissor.width);
  const long long top =
    std::min(static_cast<long long>(target.height),
             static_cast<long long>(m_scissor.y) + m_scissor.height);
  if (right <= left || top <= bottom) {
    return rect;
  }
  rect.offset = { static_cast<int32_t>(left), static_cast<int32_t>(bottom) };
  rect.extent = { static_cast<uint32_t>(right - left),
                  static_cast<uint32_t>(top - bottom) };
  return rect;
}

void
VulkanDevice::clear(bool color,
                    bool depth,
                    bool stencil,
                    bool mrtAware,
                    const CmdClearColor& value,
                    float depthValue)
{
  RenderTarget target;
  if (!resolveTarget(&target)) {
    return;
  }
  // glClear honours the scissor box.
  const VkRect2D rect = scissorRect(target);
  if (rect.extent.width == 0 || rect.extent.height == 0) {
    return;
  }
  const bool multipleTargets = mrtAware && m_framebufferKnown &&
                               !target.backbuffer && target.colorCount > 1;
  std::array<VkClearAttachment, kVulkanMaxColorAttachments + 1> attachments{};
  uint32_t count = 0;
  if (color) {
    for (unsigned index = 0; index < target.colorCount; ++index) {
      VkClearAttachment& attachment = attachments[count++];
      attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      attachment.colorAttachment = index;
      const bool zero = multipleTargets && index > 0;
      attachment.clearValue.color.float32[0] = zero ? 0.0f : value.r;
      attachment.clearValue.color.float32[1] = zero ? 0.0f : value.g;
      attachment.clearValue.color.float32[2] = zero ? 0.0f : value.b;
      attachment.clearValue.color.float32[3] = zero ? 0.0f : value.a;
    }
  }
  if ((depth || stencil) && target.depth.image != nullptr) {
    VkImageAspectFlags aspect = 0;
    if (depth) {
      aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (stencil &&
        (target.depth.image->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0) {
      aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    if (aspect != 0) {
      VkClearAttachment& attachment = attachments[count++];
      attachment.aspectMask = aspect;
      attachment.clearValue.depthStencil.depth =
        std::clamp(depthValue, 0.0f, 1.0f);
      attachment.clearValue.depthStencil.stencil = 0;
    }
  }
  if (count == 0) {
    return;
  }
  ensureRendering();
  if (!m_renderingActive) {
    return;
  }
  VkClearRect clearRect{};
  clearRect.rect = rect;
  clearRect.layerCount = 1;
  vkCmdClearAttachments(
    m_slots[m_slotIndex].main, count, attachments.data(), 1, &clearRect);
}

static VkFormat
vertexFormat(GpuAttributeFormat format)
{
  switch (format) {
    case GpuAttributeFormat::Float2:
      return VK_FORMAT_R32G32_SFLOAT;
    case GpuAttributeFormat::Unorm8x4:
      return VK_FORMAT_R8G8B8A8_UNORM;
    default:
      return VK_FORMAT_R32G32B32_SFLOAT;
  }
}

static VkBlendFactor
blendFactor(BlendFactor factor)
{
  switch (factor) {
    case BlendFactor::Zero:
      return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:
      return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcAlpha:
      return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::SrcColor:
      return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor:
      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    default:
      return VK_BLEND_FACTOR_ONE;
  }
}

static VkPrimitiveTopology
topologyOf(Primitives primitives)
{
  switch (primitives) {
    case Primitives::Points:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case Primitives::Lines:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    default:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  }
}

VkPipeline
VulkanDevice::pipelineFor(VulkanProgram& program,
                          const RenderTarget& target,
                          const VulkanMesh& mesh,
                          InstanceLayout instanceLayout)
{
  VulkanPipelineKey key;
  std::memset(&key, 0, sizeof(key));
  for (unsigned index = 0; index < target.colorCount; ++index) {
    key.colorFormats[index] =
      static_cast<uint32_t>(target.colors[index].image->format);
  }
  if (target.depth.image != nullptr) {
    key.depthFormat = static_cast<uint32_t>(target.depth.image->format);
    if ((target.depth.image->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0) {
      key.stencilFormat = key.depthFormat;
    }
  }
  key.colorCount = static_cast<uint8_t>(target.colorCount);
  key.samples = static_cast<uint8_t>(target.samples);
  key.topology = static_cast<uint8_t>(m_state.primitives);
  key.meshLayout = static_cast<uint8_t>(mesh.layout);
  key.instanceLayout = static_cast<uint8_t>(instanceLayout);
  key.polygonLine = m_state.wireframe && m_context.fillModeNonSolid() ? 1u : 0u;
  key.blend = m_state.blendEnabled ? 1u : 0u;
  if (m_state.blendEnabled) {
    key.blendSrc = static_cast<uint8_t>(m_state.blendSrc);
    key.blendDst = static_cast<uint8_t>(m_state.blendDst);
  }
  if (m_lastPipelineProgram == &program &&
      std::memcmp(&key, &m_lastPipelineKey, sizeof(key)) == 0) {
    return m_lastPipeline;
  }
  std::unordered_map<VulkanPipelineKey,
                     VkPipeline,
                     VulkanPipelineKeyHash,
                     VulkanPipelineKeyEqual>::const_iterator found =
    program.pipelines.find(key);
  if (found != program.pipelines.end()) {
    m_lastPipelineProgram = &program;
    m_lastPipelineKey = key;
    m_lastPipeline = found->second;
    return found->second;
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.createPipeline");

  std::array<GpuMeshAttribute, 4> meshLayout{};
  uint32_t meshStride = 0;
  const unsigned meshCount =
    gpuMeshAttributes(mesh.layout, meshLayout, &meshStride);
  std::vector<VkVertexInputAttributeDescription> attributes;
  bool usesMesh = false;
  bool usesInstances = false;
  bool usesDefaults = false;
  for (const GlslVertexInput& input : program.reflection.inputs) {
    for (unsigned column = 0; column < input.locationCount; ++column) {
      const uint32_t location = input.location + column;
      VkVertexInputAttributeDescription attribute{};
      attribute.location = location;
      bool placed = false;
      for (unsigned index = 0; index < meshCount && !placed; ++index) {
        if (meshLayout[index].location == location) {
          attribute.binding = 0;
          attribute.format = vertexFormat(meshLayout[index].format);
          attribute.offset = meshLayout[index].offset;
          usesMesh = true;
          placed = true;
        }
      }
      if (!placed && instanceLayout == InstanceLayout::LitModelTint &&
          location >= 4 && location <= 12) {
        attribute.binding = 1;
        attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attribute.offset = (location - 4) * 16u;
        usesInstances = true;
        placed = true;
      }
      if (!placed) {
        // OpenGL's disabled attribute arrays read (0, 0, 0, 1).
        attribute.binding = 2;
        attribute.format = input.integer ? VK_FORMAT_R32G32B32A32_SINT
                                         : VK_FORMAT_R32G32B32A32_SFLOAT;
        attribute.offset = 0;
        usesDefaults = true;
      }
      attributes.push_back(attribute);
    }
  }
  std::vector<VkVertexInputBindingDescription> bindings;
  if (usesMesh) {
    bindings.push_back({ 0, meshStride, VK_VERTEX_INPUT_RATE_VERTEX });
  }
  if (usesInstances) {
    bindings.push_back({ 1,
                         instanceLayoutStride(instanceLayout),
                         VK_VERTEX_INPUT_RATE_INSTANCE });
  }
  if (usesDefaults) {
    bindings.push_back({ 2, 0, VK_VERTEX_INPUT_RATE_VERTEX });
  }
  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount =
    static_cast<uint32_t>(bindings.size());
  vertexInput.pVertexBindingDescriptions = bindings.data();
  vertexInput.vertexAttributeDescriptionCount =
    static_cast<uint32_t>(attributes.size());
  vertexInput.pVertexAttributeDescriptions = attributes.data();

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = program.vertexModule;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = program.fragmentModule;
  stages[1].pName = "main";

  VkPipelineInputAssemblyStateCreateInfo assembly{};
  assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = topologyOf(m_state.primitives);

  VkPipelineViewportStateCreateInfo viewport{};
  viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode =
    key.polygonLine != 0 ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  raster.lineWidth = 1.0f;
  // Lines (and wireframe) rasterize as OpenGL rasterizes them.
  VkPipelineRasterizationLineStateCreateInfo lineState{};
  lineState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO;
  lineState.lineRasterizationMode =
    m_context.lineRasterization(target.samples != VK_SAMPLE_COUNT_1_BIT);
  if (lineState.lineRasterizationMode != VK_LINE_RASTERIZATION_MODE_DEFAULT &&
      (m_state.primitives == Primitives::Lines || key.polygonLine != 0)) {
    raster.pNext = &lineState;
  }

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = target.samples;
  std::array<VkSampleLocationEXT, 16> locations{};
  VkPipelineSampleLocationsStateCreateInfoEXT sampleLocations{};
  sampleLocations.sType =
    VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT;
  if (target.samples != VK_SAMPLE_COUNT_1_BIT &&
      m_context.mirroredSampleLocations(
        target.samples,
        locations.data(),
        &sampleLocations.sampleLocationsInfo.sampleLocationsCount)) {
    sampleLocations.sampleLocationsEnable = VK_TRUE;
    sampleLocations.sampleLocationsInfo.sType =
      VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT;
    sampleLocations.sampleLocationsInfo.sampleLocationsPerPixel =
      target.samples;
    sampleLocations.sampleLocationsInfo.sampleLocationGridSize = { 1, 1 };
    sampleLocations.sampleLocationsInfo.pSampleLocations = locations.data();
    multisample.pNext = &sampleLocations;
  }

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depthStencil.depthWriteEnable = VK_TRUE;

  std::array<VkPipelineColorBlendAttachmentState, kVulkanMaxColorAttachments>
    blends{};
  std::array<VkFormat, kVulkanMaxColorAttachments> colorFormats{};
  for (unsigned index = 0; index < target.colorCount; ++index) {
    VkPipelineColorBlendAttachmentState& blend = blends[index];
    blend.blendEnable = m_state.blendEnabled ? VK_TRUE : VK_FALSE;
    blend.srcColorBlendFactor = blendFactor(m_state.blendSrc);
    blend.dstColorBlendFactor = blendFactor(m_state.blendDst);
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = blendFactor(m_state.blendSrc);
    blend.dstAlphaBlendFactor = blendFactor(m_state.blendDst);
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    // Attachments the shader does not write keep their contents.
    blend.colorWriteMask =
      (program.reflection.fragmentOutputs & (1u << index)) != 0
        ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        : 0u;
    colorFormats[index] = target.colors[index].image->format;
  }
  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = target.colorCount;
  colorBlend.pAttachments = blends.data();

  const VkDynamicState dynamicStates[] = {
    VK_DYNAMIC_STATE_VIEWPORT,          VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_CULL_MODE,         VK_DYNAMIC_STATE_FRONT_FACE,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
  };
  VkPipelineDynamicStateCreateInfo dynamic{};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount =
    static_cast<uint32_t>(sizeof(dynamicStates) / sizeof(dynamicStates[0]));
  dynamic.pDynamicStates = dynamicStates;

  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = target.colorCount;
  rendering.pColorAttachmentFormats = colorFormats.data();
  rendering.depthAttachmentFormat = static_cast<VkFormat>(key.depthFormat);
  rendering.stencilAttachmentFormat = static_cast<VkFormat>(key.stencilFormat);

  VkGraphicsPipelineCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  create.pNext = &rendering;
  create.stageCount = 2;
  create.pStages = stages;
  create.pVertexInputState = &vertexInput;
  create.pInputAssemblyState = &assembly;
  create.pViewportState = &viewport;
  create.pRasterizationState = &raster;
  create.pMultisampleState = &multisample;
  create.pDepthStencilState = &depthStencil;
  create.pColorBlendState = &colorBlend;
  create.pDynamicState = &dynamic;
  create.layout = program.pipelineLayout;
  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult result = vkCreateGraphicsPipelines(
    m_context.device(), m_pipelineCache, 1, &create, nullptr, &pipeline);
  if (result != VK_SUCCESS) {
    Logger::LogError("Vulkan pipeline creation failed: " +
                     vulkanResultText(result));
    return VK_NULL_HANDLE;
  }
  program.pipelines[key] = pipeline;
  m_stats.pipelinesCreated += 1;
  return pipeline;
}

void
VulkanDevice::draw(DrawKind kind,
                   unsigned count,
                   unsigned first,
                   unsigned instances,
                   const char* label)
{
  VulkanProgram* program = resolveProgram(m_program);
  VulkanMesh* mesh = resolveMesh(m_mesh);
  if (program == nullptr || mesh == nullptr) {
    const std::string message =
      std::string(label) + ": missing valid shader or mesh; ignored";
    reportFrameError(message.c_str());
    return;
  }
  if (count == 0 || instances == 0 || m_viewport.width <= 0 ||
      m_viewport.height <= 0) {
    return;
  }
  const bool indexed =
    kind == DrawKind::Indexed || kind == DrawKind::IndexedInstanced;
  std::array<GpuMeshAttribute, 4> meshLayout{};
  uint32_t stride = 0;
  gpuMeshAttributes(mesh->layout, meshLayout, &stride);
  // Out-of-range reads are undefined in both APIs; Vulkan may fault, so
  // those draws are skipped.
  if (indexed) {
    if (mesh->indices.buffer == VK_NULL_HANDLE ||
        static_cast<uint64_t>(first) + count > mesh->indexCount) {
      return;
    }
  } else if ((static_cast<uint64_t>(first) + count) * stride >
             mesh->vertexCapacity) {
    return;
  }
  RenderTarget target;
  if (!resolveTarget(&target)) {
    return;
  }
  const VkRect2D scissor = scissorRect(target);
  if (scissor.extent.width == 0 || scissor.extent.height == 0) {
    return;
  }
  ILLUMO_PROFILE_ZONE("VulkanDevice.draw");
  ensureRecording();
  InstanceLayout instanceLayout = InstanceLayout::None;
  VulkanBuffer* instanceBuffer = nullptr;
  if (mesh->instanceLayout != InstanceLayout::None) {
    instanceBuffer = resolveBuffer(mesh->instanceBuffer);
    if (instanceBuffer != nullptr) {
      instanceLayout = mesh->instanceLayout;
    }
  }
  const VkPipeline pipeline =
    pipelineFor(*program, target, *mesh, instanceLayout);
  if (pipeline == VK_NULL_HANDLE) {
    reportFrameError("Vulkan pipeline creation failed");
    return;
  }

  // Descriptors: samplers must be shader-readable before rendering begins.
  VkCommandBuffer commands = m_slots[m_slotIndex].main;
  const std::vector<GlslBinding>& bindings = program->reflection.bindings;
  std::array<VkDescriptorBufferInfo, 32> buffers{};
  std::array<VkDescriptorImageInfo, 32> images{};
  std::array<VkWriteDescriptorSet, 32> writes{};
  // What the pushed descriptors name; an unchanged set is not pushed again.
  std::array<uint64_t, kSignatureWords> signature;
  size_t signatureLength = 0;
  signature[signatureLength++] =
    reinterpret_cast<uint64_t>(program->pipelineLayout);
  const VkDeviceSize uniformRange = m_context.limits().maxUniformBufferRange;
  for (size_t index = 0; index < bindings.size(); ++index) {
    const GlslBinding& binding = bindings[index];
    VkWriteDescriptorSet& write = writes[index];
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstBinding = binding.binding;
    write.descriptorCount = 1;
    if (binding.kind == GlslBindingKind::DefaultBlock) {
      if (program->uniforms.dirty() ||
          program->blockSerial != m_recordingSerial) {
        const std::vector<unsigned char>& block = program->uniforms.block();
        StagingSpan span;
        if (!allocateStaging(block.size(),
                             m_context.limits().minUniformBufferOffsetAlignment,
                             &span)) {
          reportFrameError("Vulkan staging memory is exhausted");
          return;
        }
        std::memcpy(span.mapped, block.data(), block.size());
        program->blockBuffer = span.buffer;
        program->blockOffset = span.offset;
        program->blockSerial = m_recordingSerial;
        program->uniforms.markUploaded();
      }
      buffers[index] = { program->blockBuffer,
                         program->blockOffset,
                         std::max<VkDeviceSize>(
                           program->uniforms.block().size(), 16) };
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.pBufferInfo = &buffers[index];
    } else if (binding.kind == GlslBindingKind::NamedBlock) {
      // OpenGL gives every block binding point 0 unless it names one; the
      // backends bind FrameUniforms there.
      VulkanBuffer* buffer = resolveBuffer(m_uniformBindings[0]);
      if (buffer != nullptr && buffer->usage == BufferUsage::Uniform) {
        buffer->memory.useSerial = m_recordingSerial;
        buffers[index] = { buffer->memory.buffer,
                           0,
                           std::min<VkDeviceSize>(buffer->capacity,
                                                  uniformRange) };
      } else {
        buffers[index] = { m_zeroUniforms.buffer,
                           0,
                           std::min<VkDeviceSize>(m_zeroUniforms.size,
                                                  uniformRange) };
      }
      write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      write.pBufferInfo = &buffers[index];
    } else {
      const bool cube = binding.kind == GlslBindingKind::SamplerCube;
      const int unit = program->uniforms.samplerUnit(binding.sampler);
      TextureHandle handle{};
      if (unit >= 0 && static_cast<unsigned>(unit) < kTextureUnits) {
        handle = cube ? m_units[static_cast<size_t>(unit)].cube
                      : m_units[static_cast<size_t>(unit)].texture2D;
      }
      VulkanTexture* texture = resolveTexture(handle);
      bool feedback = false;
      if (texture != nullptr) {
        for (unsigned color = 0; color < target.colorCount; ++color) {
          feedback = feedback || target.colors[color].image == &texture->image;
        }
        feedback = feedback || target.depth.image == &texture->image;
      }
      if (texture == nullptr || texture->cubemap != cube || feedback) {
        texture = cube ? &m_blackCube : &m_blackTexture;
      }
      VulkanImage& image = texture->image;
      if (image.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        if (m_renderingActive) {
          m_stats.renderPassBreaks += 1;
        }
        endRendering();
        endMainTransfers();
        transition(commands, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      image.useSerial = m_recordingSerial;
      images[index] = { texture->sampler,
                        image.sampledView,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.pImageInfo = &images[index];
      signature[signatureLength++] =
        reinterpret_cast<uint64_t>(image.sampledView);
      signature[signatureLength++] =
        reinterpret_cast<uint64_t>(texture->sampler);
      signature[signatureLength++] = binding.binding;
      continue;
    }
    signature[signatureLength++] =
      reinterpret_cast<uint64_t>(buffers[index].buffer);
    signature[signatureLength++] = buffers[index].offset;
    signature[signatureLength++] = binding.binding;
  }
  mesh->vertices.useSerial = m_recordingSerial;
  mesh->indices.useSerial = m_recordingSerial;
  if (instanceBuffer != nullptr) {
    instanceBuffer->memory.useSerial = m_recordingSerial;
  }

  ensureRendering(&target);
  if (!m_renderingActive) {
    return;
  }
  if (pipeline != m_recorded.pipeline) {
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    m_recorded.pipeline = pipeline;
  }
  if (!bindings.empty() &&
      (m_recorded.pushedLayout != program->pipelineLayout ||
       m_recorded.signatureLength != signatureLength ||
       std::memcmp(m_recorded.signature.data(),
                   signature.data(),
                   signatureLength * sizeof(uint64_t)) != 0)) {
    vkCmdPushDescriptorSetKHR(commands,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              program->pipelineLayout,
                              0,
                              static_cast<uint32_t>(bindings.size()),
                              writes.data());
    m_recorded.pushedLayout = program->pipelineLayout;
    m_recorded.signature = signature;
    m_recorded.signatureLength = signatureLength;
  }

  // Dynamic state lives in the command buffer across rendering instances
  // and pipeline binds; only changes are recorded.
  VkViewport viewport{};
  viewport.x = static_cast<float>(m_viewport.x);
  viewport.y = static_cast<float>(m_viewport.y);
  viewport.width = static_cast<float>(m_viewport.width);
  viewport.height = static_cast<float>(m_viewport.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkCullModeFlags cull = VK_CULL_MODE_NONE;
  if (m_state.faceCullingEnabled) {
    cull = m_cullFace == CullMode::Front ? VK_CULL_MODE_FRONT_BIT
                                         : (m_cullFace == CullMode::FrontAndBack
                                              ? VK_CULL_MODE_FRONT_AND_BACK
                                              : VK_CULL_MODE_BACK_BIT);
  }
  // Framebuffer rows are stored as OpenGL stores them, which mirrors
  // Vulkan's winding: OpenGL's counter-clockwise is Vulkan's clockwise.
  const VkFrontFace frontFace = m_frontFace == WindingOrder::CounterClockwise
                                  ? VK_FRONT_FACE_CLOCKWISE
                                  : VK_FRONT_FACE_COUNTER_CLOCKWISE;
  const VkBool32 depthTest = m_state.depthTestEnabled ? VK_TRUE : VK_FALSE;
  const bool known = m_recorded.dynamicKnown;
  if (!known) {
    vkCmdSetDepthWriteEnable(commands, VK_TRUE);
    vkCmdSetDepthCompareOp(commands, VK_COMPARE_OP_LESS_OR_EQUAL);
  }
  if (!known ||
      std::memcmp(&viewport, &m_recorded.viewport, sizeof(viewport)) != 0) {
    vkCmdSetViewport(commands, 0, 1, &viewport);
    m_recorded.viewport = viewport;
  }
  if (!known ||
      std::memcmp(&scissor, &m_recorded.scissor, sizeof(scissor)) != 0) {
    vkCmdSetScissor(commands, 0, 1, &scissor);
    m_recorded.scissor = scissor;
  }
  if (!known || cull != m_recorded.cullMode) {
    vkCmdSetCullMode(commands, cull);
    m_recorded.cullMode = cull;
  }
  if (!known || frontFace != m_recorded.frontFace) {
    vkCmdSetFrontFace(commands, frontFace);
    m_recorded.frontFace = frontFace;
  }
  if (!known || depthTest != m_recorded.depthTest) {
    vkCmdSetDepthTestEnable(commands, depthTest);
    m_recorded.depthTest = depthTest;
  }
  m_recorded.dynamicKnown = true;

  const std::array<VkBuffer, 3> vertexBuffers = {
    mesh->vertices.buffer,
    instanceBuffer != nullptr ? instanceBuffer->memory.buffer
                              : m_defaultAttributes.buffer,
    m_defaultAttributes.buffer
  };
  const std::array<VkDeviceSize, 3> offsets = {
    0, instanceBuffer != nullptr ? mesh->instanceOffset : 0u, 0
  };
  if (!m_recorded.vertexKnown || vertexBuffers != m_recorded.vertexBuffers ||
      offsets != m_recorded.vertexOffsets) {
    vkCmdBindVertexBuffers(
      commands, 0, 3, vertexBuffers.data(), offsets.data());
    m_recorded.vertexBuffers = vertexBuffers;
    m_recorded.vertexOffsets = offsets;
    m_recorded.vertexKnown = true;
  }
  if (indexed) {
    if (mesh->indices.buffer != m_recorded.indexBuffer) {
      vkCmdBindIndexBuffer(
        commands, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
      m_recorded.indexBuffer = mesh->indices.buffer;
    }
    vkCmdDrawIndexed(commands, count, instances, first, 0, 0);
  } else {
    vkCmdDraw(commands, count, instances, first, 0);
  }
  m_stats.drawCalls += 1;
}
