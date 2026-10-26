#include "GpuDevice.h"
#include "Rendering/Gpu/GpuTexels.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <string>

void
GpuDevice::reportFrameError(const char* message)
{
  if (m_frameError.empty()) {
    m_frameError = message;
  }
  Logger::LogWarning(message);
}

void
GpuDevice::executeQueue(CommandQueue& queue)
{
  ILLUMO_PROFILE_ZONE("GpuDevice.ExecuteCommandQueue");
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
GpuDevice::executeList(const RecordedCommandList* list)
{
  if (list == nullptr || list->failed()) {
    reportFrameError("ExecuteList: missing or failed recorded list");
    return;
  }
  ILLUMO_PROFILE_ZONE("GpuDevice.ExecuteList");
  m_stats.recordedLists += 1;
  m_stats.recordedCommands += list->size();
  for (size_t index = 0; index < list->size(); ++index) {
    executeCommand(list->at(index));
  }
}

void
GpuDevice::executeCommand(const RenderCommand& command)
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
      if (findProgram(command.bindShader.handle) == nullptr) {
        reportFrameError("SetShader: unknown shader handle");
        m_program = ShaderHandle{};
        break;
      }
      m_program = command.bindShader.handle;
      break;

    case CommandType::SetMesh:
      if (findMesh(command.bindMesh.handle) == nullptr) {
        reportFrameError("SetMesh: unknown mesh handle");
        m_mesh = MeshHandle{};
        break;
      }
      m_mesh = command.bindMesh.handle;
      break;

    case CommandType::SetTexture: {
      const GpuTexture* texture = findTexture(command.bindTexture.handle);
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
                 command.uniformKey,
                 GlslValueType::Int,
                 &command.uniformInt.value,
                 sizeof(int));
      break;

    case CommandType::SetUniformFloat:
      setUniform(command.uniformFloat.name,
                 command.uniformKey,
                 GlslValueType::Float,
                 &command.uniformFloat.value,
                 sizeof(float));
      break;

    case CommandType::SetUniformVec2: {
      const float values[2] = { command.uniformVec2.x, command.uniformVec2.y };
      setUniform(command.uniformVec2.name,
                 command.uniformKey,
                 GlslValueType::Vec2,
                 values,
                 sizeof(values));
      break;
    }

    case CommandType::SetUniformVec3: {
      const float values[3] = { command.uniformVec3.x,
                                command.uniformVec3.y,
                                command.uniformVec3.z };
      setUniform(command.uniformVec3.name,
                 command.uniformKey,
                 GlslValueType::Vec3,
                 values,
                 sizeof(values));
      break;
    }

    case CommandType::SetUniformVec4: {
      const float values[4] = { command.uniformVec4.x,
                                command.uniformVec4.y,
                                command.uniformVec4.z,
                                command.uniformVec4.w };
      setUniform(command.uniformVec4.name,
                 command.uniformKey,
                 GlslValueType::Vec4,
                 values,
                 sizeof(values));
      break;
    }

    case CommandType::SetUniformMat4:
      if (command.uniformMat4.value == nullptr) {
        reportFrameError("SetUniformMat4: null matrix value");
        break;
      }
      setUniform(command.uniformMat4.name,
                 command.uniformKey,
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
      const GpuBuffer* buffer = findBuffer(command.bindUniformBuffer.handle);
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
      const GpuBuffer* buffer = findBuffer(stream.handle);
      const unsigned int stride = instanceLayoutStride(stream.layout);
      GpuMesh* mesh = findMesh(m_mesh);
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
      if (findProgram(m_program) == nullptr || findMesh(m_mesh) == nullptr) {
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
GpuDevice::setUniform(const char* name,
                      const UniformKey& key,
                      GlslValueType given,
                      const void* value,
                      size_t bytes)
{
  // Uniform calls reach only the program bound in this submission, as
  // glUniform* reaches only the program in use.
  GpuProgram* program = findProgram(m_program);
  if (program != nullptr) {
    program->uniforms.set(name, key, given, value, bytes);
  }
}

void
GpuDevice::updateTexture(const CmdUpdateTexture& update)
{
  GpuTexture* texture = findTexture(update.handle);
  if (texture == nullptr || update.data == nullptr) {
    reportFrameError("UpdateTexture: invalid handle or null data");
    return;
  }
  const int channels =
    update.channels == 0 ? texture->channels : update.channels;
  if (texture->cubemap || texture->storageBytes == 0 ||
      !validTextureUpdate(texture->width,
                          texture->height,
                          update.x,
                          update.y,
                          update.width,
                          update.height,
                          channels,
                          update.srcRowStride)) {
    reportFrameError("UpdateTexture: invalid rectangle, channels, or stride");
    return;
  }
  ILLUMO_PROFILE_ZONE("GpuDevice.UpdateTexture");
  if (!uploadTexture(*texture, update, channels)) {
    return;
  }
  m_stats.uploadBytes += static_cast<size_t>(update.width) *
                         static_cast<size_t>(update.height) *
                         static_cast<size_t>(channels);
  // OpenGL's upload leaves the texture bound to unit 0.
  m_units[0].texture2D = update.handle;
}

void
GpuDevice::updateMeshBuffer(const CmdUpdateBuffer& update, bool indices)
{
  GpuMesh* mesh = findMesh(update.handle);
  if (mesh == nullptr || update.data == nullptr) {
    reportFrameError(indices ? "UpdateIndexBuffer: invalid handle or null data"
                             : "UpdateBuffer: invalid handle or null data");
    return;
  }
  // A mesh has an index buffer exactly when its index capacity is not zero.
  const size_t capacity = indices ? mesh->indexCapacity : mesh->vertexCapacity;
  if (capacity == 0 || update.sizeBytes == 0 || update.offsetBytes > capacity ||
      update.sizeBytes > capacity - update.offsetBytes) {
    reportFrameError(
      indices ? "UpdateIndexBuffer: range exceeds index capacity"
              : "UpdateBuffer: range exceeds enrolled vertex capacity");
    return;
  }
  uploadMeshData(
    *mesh, indices, update.offsetBytes, update.data, update.sizeBytes);
}

void
GpuDevice::writeBuffer(const CmdWriteBuffer& write)
{
  GpuBuffer* buffer = findBuffer(write.handle);
  if (buffer == nullptr || write.data == nullptr || write.sizeBytes == 0 ||
      write.offsetBytes > buffer->capacity ||
      write.sizeBytes > buffer->capacity - write.offsetBytes) {
    reportFrameError(
      "WriteBuffer: invalid handle, null data, or range beyond capacity");
    return;
  }
  uploadBufferData(*buffer, write.offsetBytes, write.data, write.sizeBytes);
}

void
GpuDevice::draw(DrawKind kind,
                unsigned count,
                unsigned first,
                unsigned instances,
                const char* label)
{
  GpuProgram* program = findProgram(m_program);
  GpuMesh* mesh = findMesh(m_mesh);
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
  // Out-of-range reads are undefined in OpenGL and may fault on the explicit
  // APIs, so those draws are skipped. A mesh without indices has an index
  // count of zero.
  if (indexed) {
    if (static_cast<uint64_t>(first) + count > mesh->indexCount) {
      return;
    }
  } else if ((static_cast<uint64_t>(first) + count) * stride >
             mesh->vertexCapacity) {
    return;
  }
  recordDraw(kind, count, first, instances, *program, *mesh);
}
