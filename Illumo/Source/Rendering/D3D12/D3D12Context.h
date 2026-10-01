#pragma once

#include "D3D12Common.h"
#include <string>

// Factory, adapter, device, the one direct queue and what the device
// supports. Needs feature level 11.0 and ID3D12Device2 (pipeline-state
// streams). ILLUMO_D3D12_DEBUG=1 enables the debug layer where installed.
class D3D12Context
{
public:
  D3D12Context() = default;
  ~D3D12Context();
  D3D12Context(const D3D12Context&) = delete;
  D3D12Context& operator=(const D3D12Context&) = delete;
  D3D12Context(D3D12Context&&) = delete;
  D3D12Context& operator=(D3D12Context&&) = delete;

  bool initialize(std::string* error);
  void shutdown();

  IDXGIFactory4* factory() const { return m_factory.get(); }
  ID3D12Device2* device() const { return m_device.get(); }
  ID3D12CommandQueue* queue() const { return m_queue.get(); }

  // SetSamplePositions works (tier 1 or better), so multisampled images can
  // use OpenGL's sample pattern.
  bool programmableSamplePositions() const { return m_samplePositions; }
  // Narrow (one pixel wide) quadrilateral lines through a RASTERIZER2
  // pipeline subobject, as OpenGL draws multisampled lines.
  bool narrowQuadrilateralLines() const { return m_narrowLines; }
  // Uncapped presents may tear (DXGI_FEATURE_PRESENT_ALLOW_TEARING).
  bool tearing() const { return m_tearing; }
  bool debugLayer() const { return m_debugLayer; }

  // The largest sample count colour (RGBA8) and depth (D24S8) attachments
  // both support that does not exceed the request (OpenGL's GLFW_SAMPLES).
  unsigned sampleCountFor(int requested) const;
  // Logs the adapter the way the OpenGL backend logs its context.
  void logDescription() const;
  // Debug layer messages since the last call go to the log.
  void drainDebugMessages() const;

private:
  bool selectAdapter(std::string* error);

  D3D12Ref<IDXGIFactory4> m_factory;
  D3D12Ref<IDXGIAdapter1> m_adapter;
  D3D12Ref<ID3D12Device2> m_device;
  D3D12Ref<ID3D12CommandQueue> m_queue;
  D3D12Ref<ID3D12InfoQueue> m_infoQueue;
  std::string m_adapterName;
  bool m_samplePositions = false;
  bool m_narrowLines = false;
  bool m_tearing = false;
  bool m_debugLayer = false;
};
