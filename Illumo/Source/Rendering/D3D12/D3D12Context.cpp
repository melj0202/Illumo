#include "D3D12Context.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

std::string
d3d12ResultText(HRESULT result)
{
  std::array<char, 16> code{};
  std::snprintf(
    code.data(), code.size(), "0x%08lX", static_cast<unsigned long>(result));
  std::string text(code.data());
  switch (result) {
    case DXGI_ERROR_DEVICE_REMOVED:
      return text + " (device removed)";
    case DXGI_ERROR_DEVICE_HUNG:
      return text + " (device hung)";
    case DXGI_ERROR_DEVICE_RESET:
      return text + " (device reset)";
    case E_OUTOFMEMORY:
      return text + " (out of memory)";
    case E_INVALIDARG:
      return text + " (invalid argument)";
    case DXGI_ERROR_UNSUPPORTED:
      return text + " (unsupported)";
    default:
      return text;
  }
}

static std::string
narrow(const wchar_t* wide)
{
  const int bytes =
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) {
    return std::string();
  }
  std::string text(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(
    CP_UTF8, 0, wide, -1, text.data(), bytes, nullptr, nullptr);
  text.resize(static_cast<size_t>(bytes) - 1);
  return text;
}

// ILLUMO_D3D12_DEBUG=1 enables the debug layer when the Graphics Tools are
// installed (development only).
static bool
debugRequested()
{
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, "ILLUMO_D3D12_DEBUG") != 0 ||
      value == nullptr) {
    return false;
  }
  const bool requested = value[0] != '\0' && std::strcmp(value, "0") != 0;
  std::free(value);
  return requested;
}

D3D12Context::~D3D12Context()
{
  shutdown();
}

bool
D3D12Context::selectAdapter(std::string* error)
{
  D3D12Ref<IDXGIFactory6> factory6;
  const bool byPreference = m_factory.query(factory6);
  for (UINT index = 0;; ++index) {
    D3D12Ref<IDXGIAdapter1> adapter;
    const HRESULT found = byPreference
                            ? factory6->EnumAdapterByGpuPreference(
                                index,
                                DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                __uuidof(IDXGIAdapter1),
                                adapter.putVoid())
                            : m_factory->EnumAdapters1(index, adapter.put());
    if (found == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    if (FAILED(found)) {
      continue;
    }
    DXGI_ADAPTER_DESC1 description{};
    adapter->GetDesc1(&description);
    // The software rasterizer is no Direct3D 12 GPU; OpenGL is the fallback.
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
      continue;
    }
    if (SUCCEEDED(D3D12CreateDevice(adapter.get(),
                                    D3D_FEATURE_LEVEL_11_0,
                                    __uuidof(ID3D12Device2),
                                    m_device.putVoid()))) {
      m_adapter = adapter;
      m_adapterName = narrow(description.Description);
      return true;
    }
  }
  *error = "No GPU supports Direct3D 12 at feature level 11.0";
  return false;
}

bool
D3D12Context::initialize(std::string* error)
{
  ILLUMO_PROFILE_ZONE("D3D12Context.initialize");
  UINT factoryFlags = 0;
  if (debugRequested()) {
    D3D12Ref<ID3D12Debug> debug;
    if (SUCCEEDED(
          D3D12GetDebugInterface(__uuidof(ID3D12Debug), debug.putVoid()))) {
      debug->EnableDebugLayer();
      m_debugLayer = true;
      factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    } else {
      Logger::LogWarning(
        "ILLUMO_D3D12_DEBUG is set but the debug layer is missing");
    }
  }
  if (FAILED(CreateDXGIFactory2(
        factoryFlags, __uuidof(IDXGIFactory4), m_factory.putVoid()))) {
    *error = "DXGI is unavailable";
    return false;
  }
  if (!selectAdapter(error)) {
    return false;
  }
  if (m_debugLayer && m_device.query(m_infoQueue)) {
    // Keep the messages for drainDebugMessages; never break into a debugger.
    m_infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
    // OpenGL semantics the layer flags on purpose: clears to any colour, and
    // fragment outputs with no attachment are discarded.
    D3D12_MESSAGE_ID expected[] = {
      D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
      D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
      D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_RENDERTARGETVIEW_NOT_SET,
    };
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumIDs = static_cast<UINT>(std::size(expected));
    filter.DenyList.pIDList = expected;
    m_infoQueue->PushStorageFilter(&filter);
  }
  D3D12_COMMAND_QUEUE_DESC queue{};
  queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(m_device->CreateCommandQueue(
        &queue, __uuidof(ID3D12CommandQueue), m_queue.putVoid()))) {
    *error = "The Direct3D 12 command queue could not be created";
    return false;
  }
  D3D12_FEATURE_DATA_D3D12_OPTIONS2 options2{};
  if (SUCCEEDED(m_device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS2, &options2, sizeof(options2)))) {
    m_samplePositions = options2.ProgrammableSamplePositionsTier !=
                        D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED;
  }
  D3D12_FEATURE_DATA_D3D12_OPTIONS19 options19{};
  if (SUCCEEDED(m_device->CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS19, &options19, sizeof(options19)))) {
    m_narrowLines = options19.NarrowQuadrilateralLinesSupported != FALSE;
  }
  D3D12Ref<IDXGIFactory5> factory5;
  if (m_factory.query(factory5)) {
    BOOL allowed = FALSE;
    m_tearing =
      SUCCEEDED(factory5->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed))) &&
      allowed != FALSE;
  }
  return true;
}

void
D3D12Context::shutdown()
{
  m_infoQueue.reset();
  m_queue.reset();
  m_device.reset();
  m_adapter.reset();
  m_factory.reset();
  m_adapterName.clear();
  m_samplePositions = false;
  m_narrowLines = false;
  m_tearing = false;
  m_debugLayer = false;
}

unsigned
D3D12Context::sampleCountFor(int requested) const
{
  const unsigned counts[] = { 16u, 8u, 4u, 2u };
  for (unsigned count : counts) {
    if (requested < static_cast<int>(count)) {
      continue;
    }
    bool supported = true;
    const DXGI_FORMAT formats[] = { DXGI_FORMAT_R8G8B8A8_UNORM,
                                    DXGI_FORMAT_D24_UNORM_S8_UINT };
    for (DXGI_FORMAT format : formats) {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
      levels.Format = format;
      levels.SampleCount = count;
      supported =
        supported &&
        SUCCEEDED(m_device->CheckFeatureSupport(
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))) &&
        levels.NumQualityLevels > 0;
    }
    if (supported) {
      return count;
    }
  }
  return 1u;
}

void
D3D12Context::logDescription() const
{
  Logger::LogInfo("GPU: " + m_adapterName);
  Logger::LogInfo(std::string("Direct3D 12 device: feature level 11.0+") +
                  (m_samplePositions ? ", programmable sample positions" : "") +
                  (m_narrowLines ? ", narrow quadrilateral lines" : "") +
                  (m_debugLayer ? ", debug layer on" : ""));
  Logger::LogTrace(
    "GPU limits: " + std::to_string(D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) +
    " px textures, " + std::to_string(sampleCountFor(64)) + "x MSAA");
}

void
D3D12Context::drainDebugMessages() const
{
  if (!m_infoQueue) {
    return;
  }
  const UINT64 count = m_infoQueue->GetNumStoredMessages();
  for (UINT64 index = 0; index < count; ++index) {
    SIZE_T bytes = 0;
    m_infoQueue->GetMessage(index, nullptr, &bytes);
    // Word storage keeps the message's pointers aligned.
    std::vector<unsigned long long> storage(
      (bytes + sizeof(unsigned long long) - 1) / sizeof(unsigned long long));
    D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
    if (FAILED(m_infoQueue->GetMessage(index, message, &bytes))) {
      continue;
    }
    const std::string text = std::string("D3D12: ") + message->pDescription;
    if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
      Logger::LogError(text);
    } else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING) {
      Logger::LogWarning(text);
    }
  }
  m_infoQueue->ClearStoredMessages();
}
