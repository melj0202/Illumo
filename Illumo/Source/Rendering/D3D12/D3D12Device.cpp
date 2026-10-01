#include "D3D12Device.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cstring>

static constexpr size_t kStagingChunkBytes = 8u * 1024u * 1024u;
static constexpr unsigned kShaderViewCapacity = 65536;
static constexpr unsigned kSamplerCount = 32;
static constexpr unsigned kTargetViewCapacity = 4096;
static constexpr unsigned kDepthViewCapacity = 1024;

size_t
D3D12PipelineKeyHash::operator()(const D3D12PipelineKey& key) const
{
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&key);
  uint64_t hash = 1469598103934665603ull;
  for (size_t index = 0; index < sizeof(D3D12PipelineKey); ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ull;
  }
  return static_cast<size_t>(hash);
}

bool
D3D12PipelineKeyEqual::operator()(const D3D12PipelineKey& left,
                                  const D3D12PipelineKey& right) const
{
  return std::memcmp(&left, &right, sizeof(D3D12PipelineKey)) == 0;
}

unsigned
d3d12SamplerIndex(TextureFilter filter,
                  TextureWrap wrapX,
                  TextureWrap wrapY,
                  bool mipmaps,
                  bool depthBorder)
{
  return (filter == TextureFilter::Linear ? 1u : 0u) |
         (wrapX == TextureWrap::Repeat ? 2u : 0u) |
         (wrapY == TextureWrap::Repeat ? 4u : 0u) | (mipmaps ? 8u : 0u) |
         (depthBorder ? 16u : 0u);
}

bool
D3D12DescriptorHeap::create(ID3D12Device* device,
                            D3D12_DESCRIPTOR_HEAP_TYPE type,
                            unsigned capacity,
                            bool shaderVisible)
{
  D3D12_DESCRIPTOR_HEAP_DESC description{};
  description.Type = type;
  description.NumDescriptors = capacity;
  description.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                    : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if (FAILED(device->CreateDescriptorHeap(
        &description, __uuidof(ID3D12DescriptorHeap), m_heap.putVoid()))) {
    return false;
  }
  m_increment = device->GetDescriptorHandleIncrementSize(type);
  m_capacity = capacity;
  m_next = 0;
  m_free.clear();
  return true;
}

void
D3D12DescriptorHeap::destroy()
{
  m_heap.reset();
  m_capacity = 0;
  m_next = 0;
  m_free.clear();
}

int
D3D12DescriptorHeap::allocate()
{
  if (!m_free.empty()) {
    const int index = m_free.back();
    m_free.pop_back();
    return index;
  }
  if (m_next >= m_capacity) {
    return -1;
  }
  return static_cast<int>(m_next++);
}

void
D3D12DescriptorHeap::release(int index)
{
  if (index >= 0) {
    m_free.push_back(index);
  }
}

D3D12_CPU_DESCRIPTOR_HANDLE
D3D12DescriptorHeap::cpu(int index) const
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle =
    m_heap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(index) * m_increment;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE
D3D12DescriptorHeap::gpu(int index) const
{
  D3D12_GPU_DESCRIPTOR_HANDLE handle =
    m_heap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<UINT64>(index) * m_increment;
  return handle;
}

D3D12Device::~D3D12Device()
{
  shutdown();
}

bool
D3D12Device::initialize(IRenderWindow* window, bool present, std::string* error)
{
  ILLUMO_PROFILE_ZONE("D3D12Device.initialize");
  m_window = window;
  m_present = present;
  m_glfwWindow = window != nullptr ? window->getWindowInstance() : nullptr;
  if (present) {
    m_hwnd =
      m_glfwWindow != nullptr ? glfwGetWin32Window(m_glfwWindow) : nullptr;
    if (m_hwnd == nullptr) {
      *error = "Presenting with Direct3D 12 needs a GLFW window";
      return false;
    }
  }
  if (!m_context.initialize(error)) {
    return false;
  }
  if (!initializeGlslCompiler()) {
    *error = "The GLSL compiler could not start";
    return false;
  }
  m_compilerStarted = true;
  const int requested = window != nullptr ? window->getMsaaSamples() : 0;
  m_samples = m_context.sampleCountFor(requested > 0 ? requested : 0);
  ID3D12Device* device = m_context.device();
  if (!m_shaderViews.create(device,
                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                            kShaderViewCapacity,
                            true) ||
      !m_samplerHeap.create(
        device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerCount, true) ||
      !m_targetViews.create(
        device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kTargetViewCapacity, false) ||
      !m_depthViews.create(
        device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDepthViewCapacity, false)) {
    *error = "Direct3D 12 descriptor heaps could not be created";
    return false;
  }
  if (!createSlots(error) || !createSamplers(error) || !createDefaults(error)) {
    return false;
  }
  if (!ensureBackbuffer()) {
    *error = "The Direct3D 12 backbuffer could not be created";
    return false;
  }
  if (m_present) {
    if (!createPresentPipeline(error)) {
      return false;
    }
    int width = 0;
    int height = 0;
    framebufferSize(&width, &height);
    if (width > 0 && height > 0 && !createSwapchain(width, height)) {
      *error = "The Direct3D 12 swapchain could not be created";
      return false;
    }
  }
  // A fresh OpenGL context: depth testing off, the viewport the window's.
  m_state = PipelineState{};
  m_state.depthTestEnabled = false;
  m_viewport = { 0, 0, m_backbuffer.width, m_backbuffer.height };
  m_context.logDescription();
  Logger::LogTrace(
    "Direct3D 12 backbuffer: " + std::to_string(m_backbuffer.width) + "x" +
    std::to_string(m_backbuffer.height) + ", " + std::to_string(m_samples) +
    "x MSAA" + (m_present ? "" : ", offscreen"));
  m_initialized = true;
  return true;
}

bool
D3D12Device::createSlots(std::string* error)
{
  ID3D12Device* device = m_context.device();
  for (Slot& slot : m_slots) {
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              __uuidof(ID3D12CommandAllocator),
                                              slot.allocator.putVoid()))) {
      *error = "Direct3D 12 command allocators could not be created";
      return false;
    }
  }
  if (FAILED(device->CreateCommandList(0,
                                       D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       m_slots[0].allocator.get(),
                                       nullptr,
                                       __uuidof(ID3D12GraphicsCommandList1),
                                       m_commands.putVoid()))) {
    *error = "The Direct3D 12 command list could not be created";
    return false;
  }
  m_commands->Close();
  if (FAILED(device->CreateFence(
        0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), m_fence.putVoid()))) {
    *error = "The Direct3D 12 fence could not be created";
    return false;
  }
  m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (m_fenceEvent == nullptr) {
    *error = "The Direct3D 12 fence event could not be created";
    return false;
  }
  return true;
}

void
D3D12Device::forgetRecordedState()
{
  m_recorded = RecordedState{};
  m_lastPipelineProgram = nullptr;
  m_lastPipeline = nullptr;
}

void
D3D12Device::ensureRecording()
{
  if (m_recording) {
    return;
  }
  ILLUMO_PROFILE_ZONE("D3D12Device.beginRecording");
  m_slotIndex = (m_slotIndex + 1) % kSlotCount;
  Slot& slot = m_slots[m_slotIndex];
  if (slot.serial != 0) {
    ILLUMO_PROFILE_ZONE("D3D12Device.waitSlot");
    waitForSerial(slot.serial, INFINITE);
  }
  collectRetired();
  slot.allocator->Reset();
  m_commands->Reset(slot.allocator.get(), nullptr);
  for (StagingChunk& chunk : slot.staging) {
    chunk.used = 0;
  }
  m_recordingSerial = m_lastSubmittedSerial + 1;
  slot.serial = m_recordingSerial;
  ID3D12DescriptorHeap* heaps[2] = { m_shaderViews.heap(),
                                     m_samplerHeap.heap() };
  m_commands->SetDescriptorHeaps(2, heaps);
  m_recording = true;
  forgetRecordedState();
}

void
D3D12Device::flush(bool presentFrame)
{
  if (!m_recording) {
    if (!presentFrame) {
      return;
    }
    ensureRecording();
  }
  ILLUMO_PROFILE_ZONE("D3D12Device.flush");
  const bool presenting = presentFrame && recordPresentation();
  const HRESULT closed = m_commands->Close();
  m_recording = false;
  m_lastSubmittedSerial = m_recordingSerial;
  if (SUCCEEDED(closed)) {
    ID3D12CommandList* lists[1] = { m_commands.get() };
    ILLUMO_PROFILE_ZONE("D3D12Device.executeCommandLists");
    m_context.queue()->ExecuteCommandLists(1, lists);
  } else {
    Logger::LogError("Direct3D 12 command recording failed: " +
                     d3d12ResultText(closed));
    reportFrameError("Direct3D 12 command recording failed");
  }
  if (presenting) {
    const bool vsync = m_window == nullptr || m_window->isFramePaced();
    HRESULT shown = S_OK;
    {
      ILLUMO_PROFILE_ZONE("D3D12Device.present");
      shown = m_swapchain.swapchain->Present(vsync ? 1u : 0u, 0u);
    }
    if (shown == DXGI_STATUS_OCCLUDED && vsync) {
      // Nothing is visible; pace like a refresh instead of spinning.
      const int rate = m_window != nullptr ? m_window->getRefreshRate() : 0;
      Sleep(static_cast<DWORD>(1000 / (rate > 0 ? rate : 60)));
    } else if (FAILED(shown)) {
      Logger::LogWarning("Direct3D 12 present failed: " +
                         d3d12ResultText(shown));
      if (shown == DXGI_ERROR_DEVICE_REMOVED ||
          shown == DXGI_ERROR_DEVICE_RESET) {
        reportFrameError("The Direct3D 12 device was lost");
      }
    }
  }
  // Signalled after the present, which the queue orders after the command
  // list, so a wait on this serial also covers the swapchain buffer's use
  // (a resize releases the buffers). The fence advances even when recording
  // failed, so later waits on this serial end.
  m_context.queue()->Signal(m_fence.get(), m_lastSubmittedSerial);
  m_context.drainDebugMessages();
}

bool
D3D12Device::waitForSerial(uint64_t serial, DWORD timeoutMilliseconds)
{
  if (serial <= m_completedSerial) {
    return true;
  }
  if (serial > m_lastSubmittedSerial) {
    return false;
  }
  const uint64_t completed = m_fence->GetCompletedValue();
  if (completed == UINT64_MAX) {
    // The device was removed; nothing will complete, so nothing waits.
    m_completedSerial = m_lastSubmittedSerial;
    return true;
  }
  if (completed >= serial) {
    m_completedSerial = std::max(m_completedSerial, completed);
    return true;
  }
  if (timeoutMilliseconds == 0) {
    return false;
  }
  if (FAILED(m_fence->SetEventOnCompletion(serial, m_fenceEvent)) ||
      WaitForSingleObject(m_fenceEvent, timeoutMilliseconds) != WAIT_OBJECT_0) {
    return false;
  }
  m_completedSerial = std::max(m_completedSerial, serial);
  return true;
}

bool
D3D12Device::isSerialComplete(uint64_t serial)
{
  return waitForSerial(serial, 0);
}

void
D3D12Device::retire(Retired retired)
{
  retired.serial = m_recording ? m_recordingSerial : m_lastSubmittedSerial;
  m_retired.push_back(std::move(retired));
}

void
D3D12Device::retireImage(D3D12Image& image)
{
  if (!image.memory.resource) {
    return;
  }
  Retired retired;
  retired.resource = std::move(image.memory.resource);
  retired.srv = image.srv;
  retired.rtv = image.rtv;
  retired.dsv = image.dsv;
  retire(std::move(retired));
  image = D3D12Image{};
}

void
D3D12Device::retireMemory(D3D12Memory& memory)
{
  if (!memory.resource) {
    return;
  }
  Retired retired;
  retired.resource = std::move(memory.resource);
  retire(std::move(retired));
  memory = D3D12Memory{};
}

void
D3D12Device::collectRetired()
{
  if (m_fence) {
    const uint64_t completed = m_fence->GetCompletedValue();
    if (completed != UINT64_MAX) {
      m_completedSerial = std::max(m_completedSerial, completed);
    }
  }
  size_t kept = 0;
  for (size_t index = 0; index < m_retired.size(); ++index) {
    Retired& retired = m_retired[index];
    if (retired.serial > m_completedSerial) {
      if (kept != index) {
        m_retired[kept] = std::move(retired);
      }
      ++kept;
      continue;
    }
    m_shaderViews.release(retired.srv);
    m_targetViews.release(retired.rtv);
    m_depthViews.release(retired.dsv);
  }
  m_retired.resize(kept);
}

bool
D3D12Device::allocateStaging(size_t size, size_t alignment, StagingSpan* span)
{
  ensureRecording();
  Slot& slot = m_slots[m_slotIndex];
  const size_t align = std::max<size_t>(alignment, 16);
  for (StagingChunk& chunk : slot.staging) {
    const size_t offset = (chunk.used + align - 1) / align * align;
    if (offset + size <= chunk.size) {
      chunk.used = offset + size;
      span->buffer = chunk.buffer.get();
      span->offset = offset;
      span->mapped = chunk.mapped + offset;
      span->address = chunk.buffer->GetGPUVirtualAddress() + offset;
      return true;
    }
  }
  StagingChunk chunk;
  chunk.size = std::max(kStagingChunkBytes, size + align);
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC description{};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  description.Width = chunk.size;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(m_context.device()->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        __uuidof(ID3D12Resource),
        chunk.buffer.putVoid()))) {
    return false;
  }
  void* mapped = nullptr;
  D3D12_RANGE nothingRead{ 0, 0 };
  if (FAILED(chunk.buffer->Map(0, &nothingRead, &mapped))) {
    return false;
  }
  chunk.mapped = static_cast<unsigned char*>(mapped);
  chunk.used = size;
  span->buffer = chunk.buffer.get();
  span->offset = 0;
  span->mapped = chunk.mapped;
  span->address = chunk.buffer->GetGPUVirtualAddress();
  slot.staging.push_back(std::move(chunk));
  return true;
}

void
D3D12Device::transition(D3D12Memory& memory, D3D12_RESOURCE_STATES state)
{
  if (memory.state == state || !memory.resource) {
    return;
  }
  ensureRecording();
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = memory.resource.get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = memory.state;
  barrier.Transition.StateAfter = state;
  m_commands->ResourceBarrier(1, &barrier);
  memory.state = state;
  m_stats.barriers += 1;
}

bool
D3D12Device::createBufferMemory(D3D12Memory& memory, size_t size)
{
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC description{};
  description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  // Root CBVs read whole 256-byte units.
  description.Width = (std::max<size_t>(size, 1) + 255u) / 256u * 256u;
  description.Height = 1;
  description.DepthOrArraySize = 1;
  description.MipLevels = 1;
  description.SampleDesc.Count = 1;
  description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  if (FAILED(m_context.device()->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource),
        memory.resource.putVoid()))) {
    memory = D3D12Memory{};
    return false;
  }
  memory.state = D3D12_RESOURCE_STATE_COMMON;
  memory.size = size;
  return true;
}

bool
D3D12Device::createImage(D3D12Image& image,
                         DXGI_FORMAT format,
                         DXGI_FORMAT viewFormat,
                         uint32_t width,
                         uint32_t height,
                         uint32_t mipLevels,
                         uint32_t layers,
                         uint32_t samples,
                         D3D12_RESOURCE_FLAGS flags)
{
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC description{};
  description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  description.Width = width;
  description.Height = height;
  description.DepthOrArraySize = static_cast<UINT16>(layers);
  description.MipLevels = static_cast<UINT16>(mipLevels);
  description.Format = format;
  description.SampleDesc.Count = samples;
  description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  description.Flags = flags;
  if (FAILED(m_context.device()->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &description,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        __uuidof(ID3D12Resource),
        image.memory.resource.putVoid()))) {
    image = D3D12Image{};
    return false;
  }
  image.memory.state = D3D12_RESOURCE_STATE_COMMON;
  image.format = format;
  image.viewFormat = viewFormat;
  image.width = width;
  image.height = height;
  image.mipLevels = mipLevels;
  image.layers = layers;
  image.samples = samples;
  image.depth = (flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0;
  return true;
}

bool
D3D12Device::createShaderView(D3D12Image& image,
                              DXGI_FORMAT format,
                              bool cube,
                              UINT componentMapping)
{
  const int index = m_shaderViews.allocate();
  if (index < 0) {
    return false;
  }
  D3D12_SHADER_RESOURCE_VIEW_DESC description{};
  description.Format = format;
  description.Shader4ComponentMapping = componentMapping;
  if (cube) {
    description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    description.TextureCube.MipLevels = image.mipLevels;
  } else if (image.samples > 1) {
    description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
  } else {
    description.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    description.Texture2D.MipLevels = image.mipLevels;
  }
  m_context.device()->CreateShaderResourceView(
    image.memory.resource.get(), &description, m_shaderViews.cpu(index));
  image.srv = index;
  return true;
}

bool
D3D12Device::createTargetView(D3D12Image& image)
{
  ID3D12Device* device = m_context.device();
  if (image.depth) {
    const int index = m_depthViews.allocate();
    if (index < 0) {
      return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC description{};
    description.Format = image.viewFormat;
    description.ViewDimension = image.samples > 1
                                  ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                                  : D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(
      image.memory.resource.get(), &description, m_depthViews.cpu(index));
    image.dsv = index;
    return true;
  }
  const int index = m_targetViews.allocate();
  if (index < 0) {
    return false;
  }
  D3D12_RENDER_TARGET_VIEW_DESC description{};
  description.Format = image.viewFormat;
  description.ViewDimension = image.samples > 1
                                ? D3D12_RTV_DIMENSION_TEXTURE2DMS
                                : D3D12_RTV_DIMENSION_TEXTURE2D;
  device->CreateRenderTargetView(
    image.memory.resource.get(), &description, m_targetViews.cpu(index));
  image.rtv = index;
  return true;
}

void
D3D12Device::uploadToBuffer(D3D12Memory& memory,
                            size_t offset,
                            const void* data,
                            size_t size)
{
  if (size == 0 || data == nullptr || !memory.resource) {
    return;
  }
  StagingSpan span;
  if (!allocateStaging(size, 4, &span)) {
    reportFrameError("Direct3D 12 staging memory is exhausted");
    return;
  }
  std::memcpy(span.mapped, data, size);
  transition(memory, D3D12_RESOURCE_STATE_COPY_DEST);
  m_commands->CopyBufferRegion(
    memory.resource.get(), offset, span.buffer, span.offset, size);
  m_stats.uploadBytes += size;
}

void
D3D12Device::uploadToImage(D3D12Image& image,
                           uint32_t subresource,
                           int x,
                           int y,
                           int width,
                           int height,
                           const unsigned char* texels,
                           size_t texelBytes,
                           size_t sourceRowBytes)
{
  const size_t rowBytes = static_cast<size_t>(width) * texelBytes;
  const size_t rowPitch = (rowBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) /
                          D3D12_TEXTURE_DATA_PITCH_ALIGNMENT *
                          D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
  StagingSpan span;
  if (!allocateStaging(rowPitch * static_cast<size_t>(height),
                       D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
                       &span)) {
    reportFrameError("Direct3D 12 staging memory is exhausted");
    return;
  }
  for (int row = 0; row < height; ++row) {
    std::memcpy(span.mapped + static_cast<size_t>(row) * rowPitch,
                texels + static_cast<size_t>(row) * sourceRowBytes,
                rowBytes);
  }
  transition(image.memory, D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = image.memory.resource.get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = subresource;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = span.buffer;
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint.Offset = span.offset;
  source.PlacedFootprint.Footprint.Format = image.format;
  source.PlacedFootprint.Footprint.Width = static_cast<UINT>(width);
  source.PlacedFootprint.Footprint.Height = static_cast<UINT>(height);
  source.PlacedFootprint.Footprint.Depth = 1;
  source.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
  m_commands->CopyTextureRegion(&destination,
                                static_cast<UINT>(x),
                                static_cast<UINT>(y),
                                0,
                                &source,
                                nullptr);
}

bool
D3D12Device::createSamplers(std::string* error)
{
  for (unsigned index = 0; index < kSamplerCount; ++index) {
    const int slot = m_samplerHeap.allocate();
    if (slot != static_cast<int>(index)) {
      *error = "The Direct3D 12 sampler heap is too small";
      return false;
    }
    const bool linear = (index & 1u) != 0;
    const bool repeatX = (index & 2u) != 0;
    const bool repeatY = (index & 4u) != 0;
    const bool mipmaps = (index & 8u) != 0;
    const bool depthBorder = (index & 16u) != 0;
    D3D12_SAMPLER_DESC description{};
    if (!linear) {
      description.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    } else {
      description.Filter = mipmaps ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                                   : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    }
    description.AddressU = repeatX ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                   : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    description.AddressV = repeatY ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                   : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    description.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    if (depthBorder) {
      description.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
      description.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
      for (float& component : description.BorderColor) {
        component = 1.0f;
      }
    }
    description.MaxAnisotropy = 1;
    description.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    description.MinLOD = 0.0f;
    description.MaxLOD = mipmaps ? D3D12_FLOAT32_MAX : 0.0f;
    m_context.device()->CreateSampler(&description, m_samplerHeap.cpu(slot));
  }
  return true;
}

bool
D3D12Device::createDefaults(std::string* error)
{
  // OpenGL samples an incomplete or missing texture as (0, 0, 0, 1).
  const unsigned char black[4] = { 0, 0, 0, 255 };
  std::unique_ptr<D3D12Texture> texture =
    buildTexture(black, 1, 1, 4, TextureOptions{});
  const std::array<const unsigned char*, 6> faces = { black, black, black,
                                                      black, black, black };
  std::unique_ptr<D3D12Texture> cube = buildCubemap(faces, 1, 1, 4);
  if (!texture || !cube) {
    *error = "Direct3D 12 default textures could not be created";
    return false;
  }
  m_blackTexture = std::move(*texture);
  m_blackCube = std::move(*cube);
  const std::vector<unsigned char> zeros(16384, 0);
  const float attribute[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  if (!createBufferMemory(m_zeroUniforms, zeros.size()) ||
      !createBufferMemory(m_defaultAttributes, sizeof(attribute))) {
    *error = "Direct3D 12 default buffers could not be created";
    return false;
  }
  uploadToBuffer(m_zeroUniforms, 0, zeros.data(), zeros.size());
  uploadToBuffer(m_defaultAttributes, 0, attribute, sizeof(attribute));
  return true;
}

void
D3D12Device::shutdown()
{
  if (m_context.device() == nullptr) {
    if (m_compilerStarted) {
      finalizeGlslCompiler();
      m_compilerStarted = false;
    }
    if (m_fenceEvent != nullptr) {
      CloseHandle(m_fenceEvent);
      m_fenceEvent = nullptr;
    }
    m_initialized = false;
    return;
  }
  ILLUMO_PROFILE_ZONE("D3D12Device.shutdown");
  if (m_recording) {
    flush(false);
  }
  if (m_fence && m_lastSubmittedSerial > 0) {
    waitForSerial(m_lastSubmittedSerial, INFINITE);
  }
  for (Entry<D3D12Mesh>& entry : m_meshes) {
    if (entry.resource) {
      releaseMesh(*entry.resource);
    }
  }
  for (Entry<D3D12Program>& entry : m_programs) {
    if (entry.resource) {
      releaseProgram(*entry.resource);
    }
  }
  for (Entry<D3D12Texture>& entry : m_textures) {
    if (entry.resource) {
      releaseTexture(*entry.resource);
    }
  }
  for (Entry<D3D12Buffer>& entry : m_buffers) {
    if (entry.resource) {
      retireMemory(entry.resource->memory);
    }
  }
  m_meshes.clear();
  m_programs.clear();
  m_textures.clear();
  m_framebuffers.clear();
  m_buffers.clear();
  m_meshHandles.clear();
  m_shaderHandles.clear();
  m_textureHandles.clear();
  m_framebufferHandles.clear();
  m_bufferHandles.clear();
  releaseTexture(m_blackTexture);
  releaseTexture(m_blackCube);
  retireMemory(m_zeroUniforms);
  retireMemory(m_defaultAttributes);
  retireImage(m_backbuffer.color);
  retireImage(m_backbuffer.depth);
  retireImage(m_backbuffer.resolve);
  m_backbuffer = Backbuffer{};
  releaseSwapchainBuffers();
  m_swapchain = Swapchain{};
  m_readbacks.clear();
  m_completedSerial = m_lastSubmittedSerial;
  collectRetired();
  m_retired.clear();
  m_presentPipeline.reset();
  m_presentRoot.reset();
  for (Slot& slot : m_slots) {
    slot = Slot{};
  }
  m_commands.reset();
  m_fence.reset();
  m_shaderViews.destroy();
  m_samplerHeap.destroy();
  m_targetViews.destroy();
  m_depthViews.destroy();
  m_context.drainDebugMessages();
  m_context.shutdown();
  if (m_fenceEvent != nullptr) {
    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
  }
  if (m_compilerStarted) {
    finalizeGlslCompiler();
    m_compilerStarted = false;
  }
  m_recording = false;
  m_initialized = false;
}
