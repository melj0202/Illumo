#include "D3D12Device.h"
#include "Rendering/Gpu/GpuTexels.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <cstring>

void
D3D12Device::reportFrameError(const char* message)
{
  if (m_frameError.empty()) {
    m_frameError = message;
  }
  Logger::LogWarning(message);
}

void
D3D12Device::executeQueue(CommandQueue& queue)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.ExecuteCommandQueue");
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
D3D12Device::executeList(const RecordedCommandList* list)
{
  if (list == nullptr || list->failed()) {
    reportFrameError("ExecuteList: missing or failed recorded list");
    return;
  }
  ILLUMO_PROFILE_ZONE("D3D12Device.ExecuteList");
  m_stats.recordedLists += 1;
  m_stats.recordedCommands += list->size();
  for (size_t index = 0; index < list->size(); ++index) {
    executeCommand(list->at(index));
  }
}

void
D3D12Device::executeCommand(const RenderCommand& command)
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
      const D3D12Texture* texture = resolveTexture(command.bindTexture.handle);
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
      const D3D12Buffer* buffer =
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
      const D3D12Buffer* buffer = resolveBuffer(stream.handle);
      const unsigned int stride = instanceLayoutStride(stream.layout);
      D3D12Mesh* mesh = resolveMesh(m_mesh);
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
D3D12Device::setUniform(const char* name,
                        const UniformKey& key,
                        GlslValueType given,
                        const void* value,
                        size_t bytes)
{
  // Uniform calls reach only the program bound in this submission, as
  // glUniform* reaches only the program in use.
  D3D12Program* program = resolveProgram(m_program);
  if (program != nullptr) {
    program->uniforms.set(name, key, given, value, bytes);
  }
}

void
D3D12Device::updateTexture(const CmdUpdateTexture& update)
{
  D3D12Texture* texture = resolveTexture(update.handle);
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
  ILLUMO_PROFILE_ZONE("D3D12Device.UpdateTexture");
  const size_t rowBytes = static_cast<size_t>(update.width) *
                          static_cast<size_t>(texture->storageBytes);
  std::vector<unsigned char> stored(rowBytes *
                                    static_cast<size_t>(update.height));
  convertTexelsForStorage(static_cast<const unsigned char*>(update.data),
                          update.width,
                          update.height,
                          channels,
                          update.srcRowStride,
                          texture->storageBytes,
                          stored.data());
  uploadToImage(texture->image,
                0,
                update.x,
                update.y,
                update.width,
                update.height,
                stored.data(),
                static_cast<size_t>(texture->storageBytes),
                rowBytes);
  m_stats.uploadBytes += static_cast<size_t>(update.width) *
                         static_cast<size_t>(update.height) *
                         static_cast<size_t>(channels);
  // OpenGL's upload leaves the texture bound to unit 0.
  m_units[0].texture2D = update.handle;
}

void
D3D12Device::updateMeshBuffer(const CmdUpdateBuffer& update, bool indices)
{
  D3D12Mesh* mesh = resolveMesh(update.handle);
  if (mesh == nullptr || update.data == nullptr) {
    reportFrameError(indices ? "UpdateIndexBuffer: invalid handle or null data"
                             : "UpdateBuffer: invalid handle or null data");
    return;
  }
  const size_t capacity = indices ? mesh->indexCapacity : mesh->vertexCapacity;
  D3D12Memory& memory = indices ? mesh->indices : mesh->vertices;
  if (!memory.resource || update.sizeBytes == 0 ||
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
D3D12Device::writeBuffer(const CmdWriteBuffer& write)
{
  D3D12Buffer* buffer = resolveBuffer(write.handle);
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
D3D12Device::resolveTarget(RenderTarget* target)
{
  *target = RenderTarget{};
  if (m_framebuffer.isValid()) {
    const D3D12Framebuffer* framebuffer = resolveFramebuffer(m_framebuffer);
    if (framebuffer != nullptr) {
      target->backbuffer = false;
      target->handle = m_framebuffer;
      target->width = framebuffer->width;
      target->height = framebuffer->height;
      for (TextureHandle handle : framebuffer->colorTextures) {
        D3D12Texture* texture = resolveTexture(handle);
        if (texture == nullptr ||
            target->colorCount >= kD3D12MaxColorAttachments) {
          return false;
        }
        target->colors[target->colorCount] = &texture->image;
        target->colorCount += 1;
      }
      if (framebuffer->depthTexture.isValid()) {
        D3D12Texture* depth = resolveTexture(framebuffer->depthTexture);
        if (depth == nullptr) {
          return false;
        }
        target->depth = &depth->image;
      }
      return true;
    }
    // A deleted framebuffer leaves the default one bound, as in OpenGL.
    m_framebuffer = FramebufferHandle{};
  }
  if (!m_backbuffer.color.memory.resource) {
    return false;
  }
  target->backbuffer = true;
  target->width = m_backbuffer.width;
  target->height = m_backbuffer.height;
  target->samples = m_samples;
  target->colors[0] = &m_backbuffer.color;
  target->colorCount = 1;
  target->depth = &m_backbuffer.depth;
  return true;
}

// The standard sample pattern mirrored vertically, in sixteenths of a pixel
// from its centre. Images are stored bottom row first while the OpenGL driver
// keeps its default framebuffer top row first, so its samples sit mirrored
// relative to the stored image (as in the Vulkan backend).
static bool
mirroredSamplePositions(uint32_t samples, D3D12_SAMPLE_POSITION* positions)
{
  static const float kTwo[][2] = { { 0.75f, 0.75f }, { 0.25f, 0.25f } };
  static const float kFour[][2] = { { 0.375f, 0.125f },
                                    { 0.875f, 0.375f },
                                    { 0.125f, 0.625f },
                                    { 0.625f, 0.875f } };
  static const float kEight[][2] = {
    { 0.5625f, 0.3125f }, { 0.4375f, 0.6875f }, { 0.8125f, 0.5625f },
    { 0.3125f, 0.1875f }, { 0.1875f, 0.8125f }, { 0.0625f, 0.4375f },
    { 0.6875f, 0.9375f }, { 0.9375f, 0.0625f }
  };
  static const float kSixteen[][2] = {
    { 0.5625f, 0.5625f }, { 0.4375f, 0.3125f }, { 0.3125f, 0.625f },
    { 0.75f, 0.4375f },   { 0.1875f, 0.375f },  { 0.625f, 0.8125f },
    { 0.8125f, 0.6875f }, { 0.6875f, 0.1875f }, { 0.375f, 0.875f },
    { 0.5f, 0.0625f },    { 0.25f, 0.125f },    { 0.125f, 0.75f },
    { 0.0f, 0.5f },       { 0.9375f, 0.25f },   { 0.875f, 0.9375f },
    { 0.0625f, 0.0f }
  };
  const float (*standard)[2] = nullptr;
  switch (samples) {
    case 2:
      standard = kTwo;
      break;
    case 4:
      standard = kFour;
      break;
    case 8:
      standard = kEight;
      break;
    case 16:
      standard = kSixteen;
      break;
    default:
      return false;
  }
  for (uint32_t index = 0; index < samples; ++index) {
    const long x = std::lround(standard[index][0] * 16.0f) - 8;
    const long y = std::lround((1.0f - standard[index][1]) * 16.0f) - 8;
    positions[index].X = static_cast<INT8>(std::clamp(x, -8L, 7L));
    positions[index].Y = static_cast<INT8>(std::clamp(y, -8L, 7L));
  }
  return true;
}

void
D3D12Device::bindTarget(const RenderTarget& target)
{
  ensureRecording();
  for (unsigned index = 0; index < target.colorCount; ++index) {
    transition(target.colors[index]->memory,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
  }
  if (target.depth != nullptr) {
    transition(target.depth->memory, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  }
  if (m_recorded.targetKnown &&
      m_recorded.targetBackbuffer == target.backbuffer &&
      m_recorded.target == target.handle) {
    return;
  }
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, kD3D12MaxColorAttachments> colors{};
  for (unsigned index = 0; index < target.colorCount; ++index) {
    colors[index] = m_targetViews.cpu(target.colors[index]->rtv);
  }
  D3D12_CPU_DESCRIPTOR_HANDLE depth{};
  if (target.depth != nullptr) {
    depth = m_depthViews.cpu(target.depth->dsv);
  }
  m_commands->OMSetRenderTargets(target.colorCount,
                                 target.colorCount > 0 ? colors.data()
                                                       : nullptr,
                                 FALSE,
                                 target.depth != nullptr ? &depth : nullptr);
  m_recorded.targetKnown = true;
  m_recorded.targetBackbuffer = target.backbuffer;
  m_recorded.target = target.handle;
  // Multisampled images take OpenGL's sample pattern; others the default.
  const bool positions =
    target.samples > 1 && m_context.programmableSamplePositions();
  if (positions != m_recorded.samplePositions) {
    std::array<D3D12_SAMPLE_POSITION, 16> pattern{};
    if (positions && mirroredSamplePositions(target.samples, pattern.data())) {
      m_commands->SetSamplePositions(target.samples, 1, pattern.data());
      m_recorded.samplePositions = true;
    } else {
      m_commands->SetSamplePositions(0, 0, nullptr);
      m_recorded.samplePositions = false;
    }
  }
}

D3D12_RECT
D3D12Device::scissorRect(const RenderTarget& target) const
{
  D3D12_RECT rect{ 0, 0, 0, 0 };
  if (!m_scissor.enabled) {
    rect.right = target.width;
    rect.bottom = target.height;
    return rect;
  }
  // Rows are stored bottom first, so OpenGL's y is the stored row.
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
  rect.left = static_cast<LONG>(left);
  rect.top = static_cast<LONG>(bottom);
  rect.right = static_cast<LONG>(right);
  rect.bottom = static_cast<LONG>(top);
  return rect;
}

void
D3D12Device::clear(bool color,
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
  const D3D12_RECT rect = scissorRect(target);
  if (rect.right <= rect.left || rect.bottom <= rect.top) {
    return;
  }
  const bool clearsDepth = (depth || stencil) && target.depth != nullptr;
  if (!color && !clearsDepth) {
    return;
  }
  bindTarget(target);
  if (color) {
    const bool multipleTargets = mrtAware && m_framebufferKnown &&
                                 !target.backbuffer && target.colorCount > 1;
    for (unsigned index = 0; index < target.colorCount; ++index) {
      const bool zero = multipleTargets && index > 0;
      const float values[4] = { zero ? 0.0f : value.r,
                                zero ? 0.0f : value.g,
                                zero ? 0.0f : value.b,
                                zero ? 0.0f : value.a };
      m_commands->ClearRenderTargetView(
        m_targetViews.cpu(target.colors[index]->rtv), values, 1, &rect);
    }
  }
  if (clearsDepth) {
    D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
    if (depth) {
      flags |= D3D12_CLEAR_FLAG_DEPTH;
    }
    if (stencil) {
      flags |= D3D12_CLEAR_FLAG_STENCIL;
    }
    m_commands->ClearDepthStencilView(m_depthViews.cpu(target.depth->dsv),
                                      flags,
                                      std::clamp(depthValue, 0.0f, 1.0f),
                                      0,
                                      1,
                                      &rect);
  }
}

static DXGI_FORMAT
vertexFormat(GpuAttributeFormat format)
{
  switch (format) {
    case GpuAttributeFormat::Float2:
      return DXGI_FORMAT_R32G32_FLOAT;
    case GpuAttributeFormat::Unorm8x4:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
      return DXGI_FORMAT_R32G32B32_FLOAT;
  }
}

static D3D12_BLEND
blendFactor(BlendFactor factor, bool alpha)
{
  // Alpha factors cannot name colour channels in Direct3D; OpenGL reads the
  // colour factor's alpha component there, which is the alpha factor.
  switch (factor) {
    case BlendFactor::Zero:
      return D3D12_BLEND_ZERO;
    case BlendFactor::One:
      return D3D12_BLEND_ONE;
    case BlendFactor::SrcAlpha:
      return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
      return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::SrcColor:
      return alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor:
      return alpha ? D3D12_BLEND_INV_SRC_ALPHA : D3D12_BLEND_INV_SRC_COLOR;
    default:
      return D3D12_BLEND_ONE;
  }
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE
topologyType(Primitives primitives)
{
  switch (primitives) {
    case Primitives::Points:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case Primitives::Lines:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    default:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
}

static D3D12_PRIMITIVE_TOPOLOGY
topologyOf(Primitives primitives)
{
  switch (primitives) {
    case Primitives::Points:
      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case Primitives::Lines:
      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    default:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }
}

// A pipeline-state stream: each subobject is its type followed by its
// description, both aligned like the d3dx12 stream subobjects.
class D3D12PipelineStream
{
public:
  template<typename T>
  void add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type, const T& value)
  {
    const size_t pointer = alignof(void*);
    const size_t start = (m_size + pointer - 1) / pointer * pointer;
    const size_t inner =
      (start + sizeof(type) + alignof(T) - 1) / alignof(T) * alignof(T);
    const size_t end = (inner + sizeof(T) + pointer - 1) / pointer * pointer;
    m_words.resize((end + sizeof(uint64_t) - 1) / sizeof(uint64_t), 0);
    unsigned char* bytes = reinterpret_cast<unsigned char*>(m_words.data());
    std::memcpy(bytes + start, &type, sizeof(type));
    std::memcpy(bytes + inner, &value, sizeof(T));
    m_size = end;
  }
  D3D12_PIPELINE_STATE_STREAM_DESC description()
  {
    D3D12_PIPELINE_STATE_STREAM_DESC stream{};
    stream.SizeInBytes = m_size;
    stream.pPipelineStateSubobjectStream = m_words.data();
    return stream;
  }

private:
  std::vector<uint64_t> m_words;
  size_t m_size = 0;
};

ID3D12PipelineState*
D3D12Device::pipelineFor(D3D12Program& program,
                         const RenderTarget& target,
                         const D3D12Mesh& mesh,
                         InstanceLayout instanceLayout)
{
  D3D12PipelineKey key;
  std::memset(&key, 0, sizeof(key));
  for (unsigned index = 0; index < target.colorCount; ++index) {
    key.colorFormats[index] =
      static_cast<uint32_t>(target.colors[index]->viewFormat);
  }
  if (target.depth != nullptr) {
    key.depthFormat = static_cast<uint32_t>(target.depth->viewFormat);
  }
  key.colorCount = static_cast<uint8_t>(target.colorCount);
  key.samples = static_cast<uint8_t>(target.samples);
  key.topology = static_cast<uint8_t>(m_state.primitives);
  key.meshLayout = static_cast<uint8_t>(mesh.layout);
  key.instanceLayout = static_cast<uint8_t>(instanceLayout);
  key.wireframe = m_state.wireframe ? 1u : 0u;
  key.blend = m_state.blendEnabled ? 1u : 0u;
  if (m_state.blendEnabled) {
    key.blendSrc = static_cast<uint8_t>(m_state.blendSrc);
    key.blendDst = static_cast<uint8_t>(m_state.blendDst);
  }
  key.cull = static_cast<uint8_t>(D3D12_CULL_MODE_NONE);
  if (m_state.faceCullingEnabled) {
    key.cull = static_cast<uint8_t>(m_cullFace == CullMode::Front
                                      ? D3D12_CULL_MODE_FRONT
                                      : D3D12_CULL_MODE_BACK);
  }
  // Rows are stored as OpenGL stores them, which mirrors Direct3D's winding:
  // OpenGL's counter-clockwise front faces are Direct3D's clockwise ones.
  key.frontClockwise = m_frontFace == WindingOrder::CounterClockwise ? 1u : 0u;
  key.depthTest = m_state.depthTestEnabled ? 1u : 0u;
  if (m_lastPipelineProgram == &program &&
      std::memcmp(&key, &m_lastPipelineKey, sizeof(key)) == 0) {
    return m_lastPipeline;
  }
  std::unordered_map<D3D12PipelineKey,
                     D3D12Ref<ID3D12PipelineState>,
                     D3D12PipelineKeyHash,
                     D3D12PipelineKeyEqual>::const_iterator found =
    program.pipelines.find(key);
  if (found != program.pipelines.end()) {
    m_lastPipelineProgram = &program;
    m_lastPipelineKey = key;
    m_lastPipeline = found->second.get();
    return m_lastPipeline;
  }
  ILLUMO_PROFILE_ZONE("D3D12Device.createPipeline");

  std::array<GpuMeshAttribute, 4> meshLayout{};
  uint32_t meshStride = 0;
  const unsigned meshCount =
    gpuMeshAttributes(mesh.layout, meshLayout, &meshStride);
  std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
  for (const GlslVertexInput& input : program.vertexInputs) {
    for (unsigned column = 0; column < input.locationCount; ++column) {
      const uint32_t location = input.location + column;
      D3D12_INPUT_ELEMENT_DESC element{};
      element.SemanticName = "TEXCOORD";
      element.SemanticIndex = location;
      element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      bool placed = false;
      for (unsigned index = 0; index < meshCount && !placed; ++index) {
        if (meshLayout[index].location == location) {
          element.InputSlot = 0;
          element.Format = vertexFormat(meshLayout[index].format);
          element.AlignedByteOffset = meshLayout[index].offset;
          placed = true;
        }
      }
      if (!placed && instanceLayout == InstanceLayout::LitModelTint &&
          location >= 4 && location <= 12) {
        element.InputSlot = 1;
        element.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        element.AlignedByteOffset = (location - 4) * 16u;
        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        element.InstanceDataStepRate = 1;
        placed = true;
      }
      if (!placed) {
        // OpenGL's disabled attribute arrays read (0, 0, 0, 1).
        element.InputSlot = 2;
        element.Format = input.integer ? DXGI_FORMAT_R32G32B32A32_SINT
                                       : DXGI_FORMAT_R32G32B32A32_FLOAT;
        element.AlignedByteOffset = 0;
      }
      elements.push_back(element);
    }
  }

  D3D12_BLEND_DESC blend{};
  blend.IndependentBlendEnable = TRUE;
  for (unsigned index = 0; index < target.colorCount; ++index) {
    D3D12_RENDER_TARGET_BLEND_DESC& attachment = blend.RenderTarget[index];
    attachment.BlendEnable = m_state.blendEnabled ? TRUE : FALSE;
    attachment.SrcBlend = blendFactor(m_state.blendSrc, false);
    attachment.DestBlend = blendFactor(m_state.blendDst, false);
    attachment.BlendOp = D3D12_BLEND_OP_ADD;
    attachment.SrcBlendAlpha = blendFactor(m_state.blendSrc, true);
    attachment.DestBlendAlpha = blendFactor(m_state.blendDst, true);
    attachment.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    attachment.LogicOp = D3D12_LOGIC_OP_NOOP;
    // Attachments the shader does not write keep their contents.
    attachment.RenderTargetWriteMask =
      (program.reflection.fragmentOutputs & (1u << index)) != 0
        ? static_cast<UINT8>(D3D12_COLOR_WRITE_ENABLE_ALL)
        : 0u;
  }

  D3D12_DEPTH_STENCIL_DESC depthStencil{};
  depthStencil.DepthEnable = key.depthTest != 0 ? TRUE : FALSE;
  depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  D3D12_RT_FORMAT_ARRAY formats{};
  formats.NumRenderTargets = target.colorCount;
  for (unsigned index = 0; index < target.colorCount; ++index) {
    formats.RTFormats[index] = target.colors[index]->viewFormat;
  }
  DXGI_SAMPLE_DESC sample{};
  sample.Count = target.samples;
  const D3D12_FILL_MODE fill =
    key.wireframe != 0 ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  const D3D12_CULL_MODE cull = static_cast<D3D12_CULL_MODE>(key.cull);
  const BOOL counterClockwise = key.frontClockwise != 0 ? FALSE : TRUE;
  const bool lines =
    m_state.primitives == Primitives::Lines ||
    (key.wireframe != 0 && m_state.primitives == Primitives::Triangles);
  // OpenGL draws aliased single-sample lines by the diamond-exit rule and
  // multisampled lines as one-pixel rectangles: narrow quadrilaterals here,
  // where the device has them.
  const bool narrow =
    lines && target.samples > 1 && m_context.narrowQuadrilateralLines();

  D3D12PipelineStream stream;
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE,
             program.rootSignature.get());
  D3D12_SHADER_BYTECODE vertex{ program.vertexCode.data(),
                                program.vertexCode.size() };
  D3D12_SHADER_BYTECODE pixel{ program.pixelCode.data(),
                               program.pixelCode.size() };
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, vertex);
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, pixel);
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, blend);
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, UINT_MAX);
  if (narrow) {
    D3D12_RASTERIZER_DESC2 raster{};
    raster.FillMode = fill;
    raster.CullMode = cull;
    raster.FrontCounterClockwise = counterClockwise;
    raster.DepthClipEnable = TRUE;
    raster.LineRasterizationMode =
      D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW;
    stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2, raster);
  } else {
    D3D12_RASTERIZER_DESC raster{};
    raster.FillMode = fill;
    raster.CullMode = cull;
    raster.FrontCounterClockwise = counterClockwise;
    raster.DepthClipEnable = TRUE;
    raster.MultisampleEnable = target.samples > 1 ? TRUE : FALSE;
    stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, raster);
  }
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, depthStencil);
  D3D12_INPUT_LAYOUT_DESC layout{ elements.empty() ? nullptr : elements.data(),
                                  static_cast<UINT>(elements.size()) };
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, layout);
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY,
             topologyType(m_state.primitives));
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS,
             formats);
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT,
             static_cast<DXGI_FORMAT>(key.depthFormat));
  stream.add(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, sample);
  D3D12_PIPELINE_STATE_STREAM_DESC description = stream.description();
  D3D12Ref<ID3D12PipelineState> pipeline;
  const HRESULT result = m_context.device()->CreatePipelineState(
    &description, __uuidof(ID3D12PipelineState), pipeline.putVoid());
  if (FAILED(result)) {
    Logger::LogError("Direct3D 12 pipeline creation failed: " +
                     d3d12ResultText(result));
    return nullptr;
  }
  ID3D12PipelineState* created = pipeline.get();
  program.pipelines[key] = std::move(pipeline);
  m_stats.pipelinesCreated += 1;
  return created;
}

void
D3D12Device::draw(DrawKind kind,
                  unsigned count,
                  unsigned first,
                  unsigned instances,
                  const char* label)
{
  D3D12Program* program = resolveProgram(m_program);
  D3D12Mesh* mesh = resolveMesh(m_mesh);
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
  // Out-of-range reads are undefined in both APIs; Direct3D 12 may fault, so
  // those draws are skipped.
  if (indexed) {
    if (!mesh->indices.resource ||
        static_cast<uint64_t>(first) + count > mesh->indexCount) {
      return;
    }
  } else if ((static_cast<uint64_t>(first) + count) * stride >
             mesh->vertexCapacity) {
    return;
  }
  // GL_FRONT_AND_BACK culls every polygon; points and lines still draw.
  if (m_state.faceCullingEnabled && m_cullFace == CullMode::FrontAndBack &&
      m_state.primitives == Primitives::Triangles) {
    return;
  }
  RenderTarget target;
  if (!resolveTarget(&target)) {
    return;
  }
  const D3D12_RECT scissor = scissorRect(target);
  if (scissor.right <= scissor.left || scissor.bottom <= scissor.top) {
    return;
  }
  ILLUMO_PROFILE_ZONE("D3D12Device.draw");
  ensureRecording();
  InstanceLayout instanceLayout = InstanceLayout::None;
  D3D12Buffer* instanceBuffer = nullptr;
  if (mesh->instanceLayout != InstanceLayout::None) {
    instanceBuffer = resolveBuffer(mesh->instanceBuffer);
    if (instanceBuffer != nullptr) {
      instanceLayout = mesh->instanceLayout;
    }
  }
  ID3D12PipelineState* pipeline =
    pipelineFor(*program, target, *mesh, instanceLayout);
  if (pipeline == nullptr) {
    reportFrameError("Direct3D 12 pipeline creation failed");
    return;
  }

  // Root values, with every resource moved to the state it is read in.
  const std::vector<GlslBinding>& bindings = program->reflection.bindings;
  std::array<uint64_t, kRootParameters> values{};
  std::array<bool, kRootParameters> tables{};
  for (size_t index = 0; index < bindings.size(); ++index) {
    const GlslBinding& binding = bindings[index];
    const D3D12RootBinding& placed = program->rootLayout[index];
    if (binding.kind == GlslBindingKind::DefaultBlock) {
      const std::vector<unsigned char>& block = program->uniforms.block();
      if (program->uniforms.dirty() ||
          program->blockSerial != m_recordingSerial) {
        const size_t size =
          (std::max<size_t>(block.size(), 16) + 255u) / 256u * 256u;
        StagingSpan span;
        if (!allocateStaging(
              size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &span)) {
          reportFrameError("Direct3D 12 staging memory is exhausted");
          return;
        }
        std::memset(span.mapped, 0, size);
        std::memcpy(span.mapped, block.data(), block.size());
        program->blockAddress = span.address;
        program->blockSerial = m_recordingSerial;
        program->uniforms.markUploaded();
      }
      values[placed.parameter] = program->blockAddress;
    } else if (binding.kind == GlslBindingKind::NamedBlock) {
      // OpenGL gives every block binding point 0 unless it names one; the
      // backends bind FrameUniforms there.
      D3D12Buffer* buffer = resolveBuffer(m_uniformBindings[0]);
      D3D12Memory& memory =
        buffer != nullptr && buffer->usage == BufferUsage::Uniform
          ? buffer->memory
          : m_zeroUniforms;
      transition(memory, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
      values[placed.parameter] = memory.resource->GetGPUVirtualAddress();
    } else {
      const bool cube = binding.kind == GlslBindingKind::SamplerCube;
      const int unit = program->uniforms.samplerUnit(binding.sampler);
      TextureHandle handle{};
      if (unit >= 0 && static_cast<unsigned>(unit) < kTextureUnits) {
        handle = cube ? m_units[static_cast<size_t>(unit)].cube
                      : m_units[static_cast<size_t>(unit)].texture2D;
      }
      D3D12Texture* texture = resolveTexture(handle);
      bool feedback = false;
      if (texture != nullptr) {
        for (unsigned color = 0; color < target.colorCount; ++color) {
          feedback = feedback || target.colors[color] == &texture->image;
        }
        feedback = feedback || target.depth == &texture->image;
      }
      if (texture == nullptr || texture->cubemap != cube || feedback) {
        texture = cube ? &m_blackCube : &m_blackTexture;
      }
      transition(texture->image.memory,
                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      values[placed.parameter] = m_shaderViews.gpu(texture->image.srv).ptr;
      values[placed.samplerParameter] =
        m_samplerHeap.gpu(static_cast<int>(texture->sampler)).ptr;
      tables[placed.parameter] = true;
      tables[placed.samplerParameter] = true;
    }
  }
  transition(mesh->vertices, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
  if (indexed) {
    transition(mesh->indices, D3D12_RESOURCE_STATE_INDEX_BUFFER);
  }
  if (instanceBuffer != nullptr) {
    transition(instanceBuffer->memory,
               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
  }
  transition(m_defaultAttributes,
             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
  bindTarget(target);

  if (program->rootSignature.get() != m_recorded.rootSignature) {
    m_commands->SetGraphicsRootSignature(program->rootSignature.get());
    m_recorded.rootSignature = program->rootSignature.get();
    m_recorded.rootValues.fill(0);
  }
  if (pipeline != m_recorded.pipeline) {
    m_commands->SetPipelineState(pipeline);
    m_recorded.pipeline = pipeline;
  }
  const size_t parameters = bindings.size() * 2u;
  for (size_t parameter = 0;
       parameter < parameters && parameter < values.size();
       ++parameter) {
    const uint64_t value = values[parameter];
    if (value == 0 || value == m_recorded.rootValues[parameter]) {
      continue;
    }
    if (tables[parameter]) {
      D3D12_GPU_DESCRIPTOR_HANDLE table{};
      table.ptr = value;
      m_commands->SetGraphicsRootDescriptorTable(static_cast<UINT>(parameter),
                                                 table);
    } else {
      m_commands->SetGraphicsRootConstantBufferView(
        static_cast<UINT>(parameter), value);
    }
    m_recorded.rootValues[parameter] = value;
  }

  // Dynamic state lives in the command list across pipeline binds; only
  // changes are recorded. Rows are stored bottom first, so OpenGL's viewport
  // numbers are the stored rows.
  D3D12_VIEWPORT viewport{};
  viewport.TopLeftX = static_cast<float>(m_viewport.x);
  viewport.TopLeftY = static_cast<float>(m_viewport.y);
  viewport.Width = static_cast<float>(m_viewport.width);
  viewport.Height = static_cast<float>(m_viewport.height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  const D3D12_PRIMITIVE_TOPOLOGY topology = topologyOf(m_state.primitives);
  const bool known = m_recorded.dynamicKnown;
  if (!known ||
      std::memcmp(&viewport, &m_recorded.viewport, sizeof(viewport)) != 0) {
    m_commands->RSSetViewports(1, &viewport);
    m_recorded.viewport = viewport;
  }
  if (!known ||
      std::memcmp(&scissor, &m_recorded.scissor, sizeof(scissor)) != 0) {
    m_commands->RSSetScissorRects(1, &scissor);
    m_recorded.scissor = scissor;
  }
  if (!known || topology != m_recorded.topology) {
    m_commands->IASetPrimitiveTopology(topology);
    m_recorded.topology = topology;
  }
  m_recorded.dynamicKnown = true;

  std::array<D3D12_VERTEX_BUFFER_VIEW, 3> views{};
  views[0].BufferLocation = mesh->vertices.resource->GetGPUVirtualAddress();
  views[0].SizeInBytes = static_cast<UINT>(mesh->vertexCapacity);
  views[0].StrideInBytes = stride;
  if (instanceBuffer != nullptr) {
    views[1].BufferLocation =
      instanceBuffer->memory.resource->GetGPUVirtualAddress() +
      mesh->instanceOffset;
    views[1].SizeInBytes =
      static_cast<UINT>(instanceBuffer->capacity - mesh->instanceOffset);
    views[1].StrideInBytes = instanceLayoutStride(instanceLayout);
  } else {
    views[1].BufferLocation =
      m_defaultAttributes.resource->GetGPUVirtualAddress();
    views[1].SizeInBytes = 16;
    views[1].StrideInBytes = 0;
  }
  views[2].BufferLocation =
    m_defaultAttributes.resource->GetGPUVirtualAddress();
  views[2].SizeInBytes = 16;
  views[2].StrideInBytes = 0;
  if (!m_recorded.vertexKnown ||
      std::memcmp(views.data(),
                  m_recorded.vertexBuffers.data(),
                  sizeof(D3D12_VERTEX_BUFFER_VIEW) * views.size()) != 0) {
    m_commands->IASetVertexBuffers(0, 3, views.data());
    m_recorded.vertexBuffers = views;
    m_recorded.vertexKnown = true;
  }
  if (indexed) {
    const D3D12_GPU_VIRTUAL_ADDRESS indexAddress =
      mesh->indices.resource->GetGPUVirtualAddress();
    if (indexAddress != m_recorded.indexBuffer) {
      D3D12_INDEX_BUFFER_VIEW view{};
      view.BufferLocation = indexAddress;
      view.SizeInBytes = static_cast<UINT>(mesh->indexCapacity);
      view.Format = DXGI_FORMAT_R32_UINT;
      m_commands->IASetIndexBuffer(&view);
      m_recorded.indexBuffer = indexAddress;
    }
    m_commands->DrawIndexedInstanced(count, instances, first, 0, 0);
  } else {
    m_commands->DrawInstanced(count, instances, first, 0);
  }
  m_stats.drawCalls += 1;
}
