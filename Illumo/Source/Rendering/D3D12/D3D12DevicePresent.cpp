#include "D3D12Device.h"
#include "Rendering/Gpu/GpuTexels.h"

#include "Platform/SessionState.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <chrono>
#include <thread>

static constexpr UINT kSwapchainBuffers = 3;

// Copies the backbuffer, stored bottom row first, to the screen top row
// first: each screen pixel loads its mirrored texel (nearest, scaled when
// the sizes differ during a resize).
static const char* const kPresentVertexShader = R"(
float4 main(uint id : SV_VertexID) : SV_Position
{
  float2 corner = float2((id << 1) & 2, id & 2);
  return float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
)";

static const char* const kPresentPixelShader = R"(
cbuffer Flip : register(b0)
{
  float2 scale;
  float sourceHeight;
  float unused;
};
Texture2D<float4> source : register(t0);
float4 main(float4 position : SV_Position) : SV_Target
{
  int2 texel = int2(floor(position.xy * scale));
  return float4(source.Load(int3(texel.x, int(sourceHeight) - 1 - texel.y, 0)).rgb, 1.0);
}
)";

void
D3D12Device::framebufferSize(int* width, int* height) const
{
  *width = 0;
  *height = 0;
  if (m_glfwWindow != nullptr) {
    glfwGetFramebufferSize(m_glfwWindow, width, height);
  } else if (m_window != nullptr) {
    const std::array<int, 2> size = m_window->getWindowDimensions();
    *width = size[0];
    *height = size[1];
  }
}

bool
D3D12Device::ensureBackbuffer()
{
  int width = 0;
  int height = 0;
  framebufferSize(&width, &height);
  if (width <= 0 || height <= 0) {
    // Minimized: keep the old backbuffer, or start with a pixel.
    if (m_backbuffer.color.memory.resource) {
      return true;
    }
    width = 1;
    height = 1;
  }
  if (width == m_backbuffer.width && height == m_backbuffer.height &&
      m_backbuffer.color.memory.resource) {
    return true;
  }
  m_recorded.targetKnown = false;
  retireImage(m_backbuffer.color);
  retireImage(m_backbuffer.depth);
  retireImage(m_backbuffer.resolve);
  const uint32_t w = static_cast<uint32_t>(width);
  const uint32_t h = static_cast<uint32_t>(height);
  const bool multisampled = m_samples > 1;
  // Stored as OpenGL stores its framebuffer (UNORM, no sRGB encoding); the
  // typeless resource lets the multisample resolve read it as sRGB (see
  // resolvedBackbuffer).
  if (!createImage(m_backbuffer.color,
                   DXGI_FORMAT_R8G8B8A8_TYPELESS,
                   DXGI_FORMAT_R8G8B8A8_UNORM,
                   w,
                   h,
                   1,
                   1,
                   m_samples,
                   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ||
      !createTargetView(m_backbuffer.color) ||
      (!multisampled &&
       !createShaderView(m_backbuffer.color,
                         DXGI_FORMAT_R8G8B8A8_UNORM,
                         false,
                         D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING)) ||
      !createImage(m_backbuffer.depth,
                   DXGI_FORMAT_D24_UNORM_S8_UINT,
                   DXGI_FORMAT_D24_UNORM_S8_UINT,
                   w,
                   h,
                   1,
                   1,
                   m_samples,
                   D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                     D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
      !createTargetView(m_backbuffer.depth)) {
    return false;
  }
  if (multisampled &&
      (!createImage(m_backbuffer.resolve,
                    DXGI_FORMAT_R8G8B8A8_TYPELESS,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    w,
                    h,
                    1,
                    1,
                    1,
                    D3D12_RESOURCE_FLAG_NONE) ||
       !createShaderView(m_backbuffer.resolve,
                         DXGI_FORMAT_R8G8B8A8_UNORM,
                         false,
                         D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING))) {
    return false;
  }
  m_backbuffer.width = width;
  m_backbuffer.height = height;
  return true;
}

void
D3D12Device::beginFrame()
{
  int width = 0;
  int height = 0;
  framebufferSize(&width, &height);
  if (width > 0 && height > 0 &&
      (width != m_backbuffer.width || height != m_backbuffer.height)) {
    ensureBackbuffer();
    // The OpenGL window resets the viewport when it resizes.
    m_viewport = { 0, 0, width, height };
  }
}

void
D3D12Device::endFrame()
{
  flush(m_present);
}

bool
D3D12Device::createPresentPipeline(std::string* error)
{
  std::vector<unsigned char> vertex;
  std::vector<unsigned char> pixel;
  if (!compileHlsl(kPresentVertexShader, true, &vertex, error) ||
      !compileHlsl(kPresentPixelShader, false, &pixel, error)) {
    return false;
  }
  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  D3D12_ROOT_PARAMETER parameters[2]{};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.Num32BitValues = 4;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC description{};
  description.NumParameters = 2;
  description.pParameters = parameters;
  D3D12Ref<ID3DBlob> serialized;
  D3D12Ref<ID3DBlob> messages;
  if (FAILED(D3D12SerializeRootSignature(&description,
                                         D3D_ROOT_SIGNATURE_VERSION_1,
                                         serialized.put(),
                                         messages.put())) ||
      FAILED(
        m_context.device()->CreateRootSignature(0,
                                                serialized->GetBufferPointer(),
                                                serialized->GetBufferSize(),
                                                __uuidof(ID3D12RootSignature),
                                                m_presentRoot.putVoid()))) {
    *error = "The Direct3D 12 presentation root signature failed";
    return false;
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
  pipeline.pRootSignature = m_presentRoot.get();
  pipeline.VS = { vertex.data(), vertex.size() };
  pipeline.PS = { pixel.data(), pixel.size() };
  pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask =
    D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline.SampleMask = UINT_MAX;
  pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.RasterizerState.DepthClipEnable = TRUE;
  pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline.SampleDesc.Count = 1;
  if (FAILED(m_context.device()->CreateGraphicsPipelineState(
        &pipeline,
        __uuidof(ID3D12PipelineState),
        m_presentPipeline.putVoid()))) {
    *error = "The Direct3D 12 presentation pipeline failed";
    return false;
  }
  return true;
}

void
D3D12Device::releaseSwapchainBuffers()
{
  for (D3D12Image& buffer : m_swapchain.buffers) {
    m_targetViews.release(buffer.rtv);
  }
  m_swapchain.buffers.clear();
}

bool
D3D12Device::createSwapchain(int width, int height)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.createSwapchain");
  DXGI_SWAP_CHAIN_DESC1 description{};
  description.Width = static_cast<UINT>(width);
  description.Height = static_cast<UINT>(height);
  // OpenGL's default framebuffer is not sRGB-encoded; neither is this one.
  description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.SampleDesc.Count = 1;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.BufferCount = kSwapchainBuffers;
  description.Scaling = DXGI_SCALING_STRETCH;
  description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  D3D12Ref<IDXGISwapChain1> swapchain;
  const HRESULT created = m_context.factory()->CreateSwapChainForHwnd(
    m_context.queue(), m_hwnd, &description, nullptr, nullptr, swapchain.put());
  if (FAILED(created) || !swapchain.query(m_swapchain.swapchain)) {
    Logger::LogWarning("Direct3D 12 swapchain creation failed: " +
                       d3d12ResultText(created));
    m_swapchain = Swapchain{};
    return false;
  }
  // GLFW owns fullscreen; DXGI must not toggle it on Alt+Enter.
  m_context.factory()->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
  m_swapchain.width = static_cast<uint32_t>(width);
  m_swapchain.height = static_cast<uint32_t>(height);
  return resizeSwapchain(width, height);
}

bool
D3D12Device::resizeSwapchain(int width, int height)
{
  if (!m_swapchain.swapchain) {
    return false;
  }
  // Earlier frames may still draw into the buffers being replaced.
  if (m_lastSubmittedSerial > 0) {
    waitForSerial(m_lastSubmittedSerial, INFINITE);
  }
  releaseSwapchainBuffers();
  if (m_swapchain.width != static_cast<uint32_t>(width) ||
      m_swapchain.height != static_cast<uint32_t>(height)) {
    const HRESULT resized =
      m_swapchain.swapchain->ResizeBuffers(0,
                                           static_cast<UINT>(width),
                                           static_cast<UINT>(height),
                                           DXGI_FORMAT_UNKNOWN,
                                           0);
    if (FAILED(resized)) {
      Logger::LogWarning("Direct3D 12 swapchain resize failed: " +
                         d3d12ResultText(resized));
      return false;
    }
    m_swapchain.width = static_cast<uint32_t>(width);
    m_swapchain.height = static_cast<uint32_t>(height);
  }
  for (UINT index = 0; index < kSwapchainBuffers; ++index) {
    D3D12Image buffer;
    if (FAILED(m_swapchain.swapchain->GetBuffer(
          index, __uuidof(ID3D12Resource), buffer.memory.resource.putVoid()))) {
      releaseSwapchainBuffers();
      return false;
    }
    buffer.memory.state = D3D12_RESOURCE_STATE_PRESENT;
    buffer.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    buffer.viewFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    buffer.width = m_swapchain.width;
    buffer.height = m_swapchain.height;
    if (!createTargetView(buffer)) {
      releaseSwapchainBuffers();
      return false;
    }
    m_swapchain.buffers.push_back(std::move(buffer));
  }
  Logger::LogTrace("Direct3D 12 swapchain: " + std::to_string(width) + "x" +
                   std::to_string(height) + ", " +
                   std::to_string(kSwapchainBuffers) + " buffers");
  return true;
}

D3D12Image&
D3D12Device::resolvedBackbuffer()
{
  if (m_samples <= 1) {
    return m_backbuffer.color;
  }
  // The resolve averages samples in linear light, through the sRGB format,
  // as the NVIDIA OpenGL driver resolves its default framebuffer ("gamma
  // correct antialiasing", on by default). The stored bytes stay OpenGL's.
  transition(m_backbuffer.color.memory, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
  transition(m_backbuffer.resolve.memory, D3D12_RESOURCE_STATE_RESOLVE_DEST);
  m_commands->ResolveSubresource(m_backbuffer.resolve.memory.resource.get(),
                                 0,
                                 m_backbuffer.color.memory.resource.get(),
                                 0,
                                 DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
  return m_backbuffer.resolve;
}

bool
D3D12Device::recordPresentation()
{
  if (!m_present || m_hwnd == nullptr) {
    return false;
  }
  int width = 0;
  int height = 0;
  framebufferSize(&width, &height);
  if (width <= 0 || height <= 0) {
    return false;
  }
  ILLUMO_PROFILE_ZONE("D3D12Device.recordPresentation");
  const bool vsync = m_window == nullptr || m_window->isFramePaced();
  // As on Vulkan: presents pause while the session is locked, and a vsynced
  // loop keeps pacing by a refresh per frame.
  const std::chrono::steady_clock::time_point now =
    std::chrono::steady_clock::now();
  if (now >= m_nextSessionCheck) {
    ILLUMO_PROFILE_ZONE("D3D12Device.sessionCheck");
    m_sessionLocked = PlatformSessionLocked();
    m_nextSessionCheck = now + std::chrono::milliseconds(250);
  }
  if (m_sessionLocked) {
    if (!m_presentationPauseReported) {
      Logger::LogWarning("Direct3D 12 presentation is paused while the "
                         "session is locked");
      m_presentationPauseReported = true;
    }
    m_presentationPaused = true;
    if (vsync) {
      const int rate = m_window != nullptr ? m_window->getRefreshRate() : 0;
      std::this_thread::sleep_for(
        std::chrono::microseconds(1000000 / (rate > 0 ? rate : 60)));
    }
    return false;
  }
  m_presentationPaused = false;
  if (!m_swapchain.swapchain) {
    if (!createSwapchain(width, height)) {
      return false;
    }
  } else if (m_swapchain.width != static_cast<uint32_t>(width) ||
             m_swapchain.height != static_cast<uint32_t>(height) ||
             m_swapchain.buffers.empty()) {
    if (!resizeSwapchain(width, height)) {
      return false;
    }
  }
  D3D12Image& source = resolvedBackbuffer();
  transition(source.memory, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  const UINT index = m_swapchain.swapchain->GetCurrentBackBufferIndex();
  if (index >= m_swapchain.buffers.size()) {
    return false;
  }
  D3D12Image& buffer = m_swapchain.buffers[index];
  transition(buffer.memory, D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (m_recorded.samplePositions) {
    m_commands->SetSamplePositions(0, 0, nullptr);
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE target = m_targetViews.cpu(buffer.rtv);
  m_commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
  m_commands->SetGraphicsRootSignature(m_presentRoot.get());
  m_commands->SetPipelineState(m_presentPipeline.get());
  m_commands->SetGraphicsRootDescriptorTable(0, m_shaderViews.gpu(source.srv));
  const float constants[4] = {
    static_cast<float>(source.width) / static_cast<float>(m_swapchain.width),
    static_cast<float>(source.height) / static_cast<float>(m_swapchain.height),
    static_cast<float>(source.height),
    0.0f
  };
  m_commands->SetGraphicsRoot32BitConstants(1, 4, constants, 0);
  D3D12_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(m_swapchain.width);
  viewport.Height = static_cast<float>(m_swapchain.height);
  viewport.MaxDepth = 1.0f;
  const D3D12_RECT scissor{ 0,
                            0,
                            static_cast<LONG>(m_swapchain.width),
                            static_cast<LONG>(m_swapchain.height) };
  m_commands->RSSetViewports(1, &viewport);
  m_commands->RSSetScissorRects(1, &scissor);
  m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commands->DrawInstanced(3, 1, 0, 0);
  transition(buffer.memory, D3D12_RESOURCE_STATE_PRESENT);
  forgetRecordedState();
  return true;
}

bool
D3D12Device::createReadbackBuffer(ReadbackSlot& slot, size_t size)
{
  if (slot.buffer && slot.size >= size) {
    return true;
  }
  if (slot.buffer) {
    Retired retired;
    retired.resource = std::move(slot.buffer);
    retire(std::move(retired));
    slot.size = 0;
  }
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC description{};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = size;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(m_context.device()->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        __uuidof(ID3D12Resource),
        slot.buffer.putVoid()))) {
    slot.size = 0;
    return false;
  }
  slot.size = size;
  return true;
}

static size_t
alignedRowPitch(size_t rowBytes)
{
  return (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) /
         D3D12_TEXTURE_DATA_PITCH_ALIGNMENT *
         D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
}

// Records the copy of the bottom-left width x height texels of `image`.
static void
copyToReadback(ID3D12GraphicsCommandList* commands,
               const D3D12Image& image,
               ID3D12Resource* buffer,
               int width,
               int height,
               size_t rowPitch)
{
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = buffer;
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint.Footprint.Format = image.format;
  destination.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
  destination.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
  destination.PlacedFootprint.Footprint.Depth = 1;
  destination.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = image.memory.resource.get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0;
  const D3D12_BOX box{
    0, 0, 0, static_cast<UINT>(width), static_cast<UINT>(height), 1
  };
  commands->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
}

FrameReadback
D3D12Device::readBackbuffer(int width, int height)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.readBackbuffer");
  FrameReadback result;
  if (width > m_backbuffer.width || height > m_backbuffer.height) {
    result.error = "Readback dimensions exceed the backbuffer";
    return result;
  }
  const size_t rowPitch = alignedRowPitch(static_cast<size_t>(width) * 4u);
  ReadbackSlot staging;
  if (!createReadbackBuffer(staging, rowPitch * static_cast<size_t>(height))) {
    result.error = "Direct3D 12 readback memory could not be allocated";
    return result;
  }
  ensureRecording();
  D3D12Image& source = resolvedBackbuffer();
  transition(source.memory, D3D12_RESOURCE_STATE_COPY_SOURCE);
  copyToReadback(
    m_commands.get(), source, staging.buffer.get(), width, height, rowPitch);
  const uint64_t serial = m_recordingSerial;
  flush(false);
  if (!m_frameError.empty() || !waitForSerial(serial, INFINITE)) {
    result.error = m_frameError.empty()
                     ? "Direct3D 12 readback did not complete"
                     : m_frameError;
    return result;
  }
  void* mapped = nullptr;
  const D3D12_RANGE read{ 0, rowPitch * static_cast<size_t>(height) };
  if (FAILED(staging.buffer->Map(0, &read, &mapped))) {
    result.error = "Direct3D 12 readback memory could not be mapped";
    return result;
  }
  result.pixels.resize(static_cast<size_t>(width) *
                       static_cast<size_t>(height) * 4u);
  convertReadbackTexels(static_cast<const unsigned char*>(mapped),
                        rowPitch,
                        width,
                        height,
                        TextureFormat::RGBA8,
                        result.pixels.data());
  const D3D12_RANGE written{ 0, 0 };
  staging.buffer->Unmap(0, &written);
  result.width = width;
  result.height = height;
  return result;
}

bool
D3D12Device::requestFramebufferReadback(std::uint32_t stream,
                                        FramebufferHandle framebuffer,
                                        int width,
                                        int height)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.requestFramebufferReadback");
  const D3D12Framebuffer* target = resolveFramebuffer(framebuffer);
  if (!isFramebufferValid(framebuffer) || target == nullptr ||
      target->colorTextures.empty() || width < 1 || height < 1 ||
      width > target->width || height > target->height) {
    return false;
  }
  D3D12Texture* texture = resolveTexture(target->colorTextures[0]);
  if (texture == nullptr) {
    return false;
  }
  const unsigned texelBytes = storedTexelBytes(texture->format);
  if (texelBytes == 0) {
    return false;
  }
  ReadbackStream& readback = m_readbacks[stream];
  ReadbackSlot* available = nullptr;
  for (ReadbackSlot& slot : readback.slots) {
    if (!slot.pending) {
      available = &slot;
      break;
    }
  }
  if (available == nullptr) {
    return false;
  }
  const size_t rowPitch =
    alignedRowPitch(static_cast<size_t>(width) * texelBytes);
  if (!createReadbackBuffer(*available,
                            rowPitch * static_cast<size_t>(height))) {
    return false;
  }
  ensureRecording();
  transition(texture->image.memory, D3D12_RESOURCE_STATE_COPY_SOURCE);
  copyToReadback(m_commands.get(),
                 texture->image,
                 available->buffer.get(),
                 width,
                 height,
                 rowPitch);
  available->pending = true;
  available->serial = m_recordingSerial;
  available->width = width;
  available->height = height;
  available->rowBytes = rowPitch;
  available->format = texture->format;
  available->order = ++m_readbackOrder;
  return true;
}

bool
D3D12Device::takeFramebufferReadback(std::uint32_t stream,
                                     bool wait,
                                     FrameReadback& out)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.takeFramebufferReadback");
  out = FrameReadback{};
  std::unordered_map<std::uint32_t, ReadbackStream>::iterator found =
    m_readbacks.find(stream);
  if (found == m_readbacks.end()) {
    out.error = "No readback is pending";
    return false;
  }
  ReadbackSlot* oldest = nullptr;
  for (ReadbackSlot& slot : found->second.slots) {
    if (slot.pending && (oldest == nullptr || slot.order < oldest->order)) {
      oldest = &slot;
    }
  }
  if (oldest == nullptr) {
    out.error = "No readback is pending";
    return false;
  }
  // A copy still being recorded is submitted now, as OpenGL's poll flushes
  // its fence (GL_SYNC_FLUSH_COMMANDS_BIT), so a poll is never a frame late.
  if (m_recording && oldest->serial == m_recordingSerial) {
    flush(false);
  }
  // One second bounds a blocking wait; a lost GPU must not hang the frame.
  if (!waitForSerial(oldest->serial, wait ? 1000u : 0u)) {
    out.error = "Readback is not complete yet";
    return false;
  }
  void* mapped = nullptr;
  const D3D12_RANGE read{
    0, oldest->rowBytes * static_cast<size_t>(oldest->height)
  };
  if (FAILED(oldest->buffer->Map(0, &read, &mapped))) {
    out.error = "Direct3D 12 readback memory could not be mapped";
    oldest->pending = false;
    return false;
  }
  out.pixels.resize(static_cast<size_t>(oldest->width) *
                    static_cast<size_t>(oldest->height) * 4u);
  convertReadbackTexels(static_cast<const unsigned char*>(mapped),
                        oldest->rowBytes,
                        oldest->width,
                        oldest->height,
                        oldest->format,
                        out.pixels.data());
  const D3D12_RANGE written{ 0, 0 };
  oldest->buffer->Unmap(0, &written);
  oldest->pending = false;
  out.width = oldest->width;
  out.height = oldest->height;
  return true;
}

void
D3D12Device::releaseReadbackStream(std::uint32_t stream)
{
  std::unordered_map<std::uint32_t, ReadbackStream>::iterator found =
    m_readbacks.find(stream);
  if (found == m_readbacks.end()) {
    return;
  }
  for (ReadbackSlot& slot : found->second.slots) {
    if (!slot.buffer) {
      continue;
    }
    Retired retired;
    retired.resource = std::move(slot.buffer);
    retire(std::move(retired));
  }
  m_readbacks.erase(found);
}
