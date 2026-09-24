#pragma once

#include <Illumo/Engine/IModule.h>
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Services/FileTreeSource.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <Illumo/Wasm/WasmGuest.h>
#include <Illumo/Wasm/WasmPanelWindows.h>
#include <Illumo/Wasm/WasmRenderServices.h>

// Rolling host-side timings of one Update: each guest exchange plus frame
// acceptance. Diagnostics only; read through stats() or `wasm_stats`.
struct WasmFrameStats
{
  RollingMetric totalMilliseconds;
  RollingMetric servicesMilliseconds;
  RollingMetric updateMilliseconds;
  RollingMetric receiveMilliseconds;
  RollingMetric frameMilliseconds;
  RollingMetric acceptMilliseconds;
  RollingMetric frameBytes;
  std::uint64_t updates = 0;
};

// Generic native shell. Product update, UI and geometry are supplied by a
// GuestApplication reactor. This adapter grants rendering and bounded messages.
class WasmGameModule : public IModule
{
public:
  explicit WasmGameModule(std::vector<std::byte> module,
                          std::vector<std::byte> startup = {},
                          WasmLimits limits = {},
                          std::vector<std::byte> mod = {},
                          std::vector<std::byte> worker = {},
                          WasmFileRoots files = {});
  ~WasmGameModule() override;
  WasmGameModule(const WasmGameModule&) = delete;
  WasmGameModule& operator=(const WasmGameModule&) = delete;
  WasmGameModule(WasmGameModule&&) = delete;
  WasmGameModule& operator=(WasmGameModule&&) = delete;

  // Budgets for compute worker instances and the number of lanes granted.
  // Applies to workers created after Start; call before Start.
  void setWorkerLimits(const WasmLimits& limits, std::uint32_t lanes);
  // Where surface windows (Windows capability) come from; the platform's by
  // default. Null keeps every surface docked (capture, benchmarks). Call
  // before Start.
  void setSurfaceWindows(ISurfaceWindowFactory* factory)
  {
    m_surfaceWindows = factory;
  }
  // Null unless the guest was granted surface windows.
  const WasmPanelWindows* panelWindows() const { return m_windows.get(); }
  bool Start(IllumoContext* context) override;
  void Update(double elapsed) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;
  bool OnCloseRequested() override;
  const std::string& error() const;
  const std::string& modError() const;
  bool hasActiveMod() const;
  const WasmFrameStats& stats() const;
  // Null before Start and after Exit.
  const WasmFrameCounters* frameCounters() const;
  // One human-readable summary of stats() and frameCounters().
  std::string describeStats() const;

private:
  void fail(std::string error);
  WasmFrameStats m_stats;
  WasmLimits m_workerLimits = WasmGameServices::defaultWorkerLimits();
  std::uint32_t m_workerLanes = 1u;
  std::vector<std::byte> m_module;
  std::vector<std::byte> m_startup;
  std::vector<std::byte> m_modModule;
  std::vector<std::byte> m_workerModule;
  WasmFileRoots m_fileRoots;
  // Published as IllumoContext::fileTree while the guest runs.
  std::unique_ptr<IFileTreeSource> m_treeSource;
  WasmGuest m_guest;
  std::unique_ptr<WasmGuest> m_mod;
  std::unique_ptr<WasmFrameRenderer> m_frames;
  std::unique_ptr<WasmGameServices> m_services;
  ISurfaceWindowFactory* m_surfaceWindows = &PlatformSurfaceWindows();
  std::unique_ptr<WasmPanelWindows> m_windows;
  std::vector<std::byte> m_completions;
  std::string m_error;
  std::string m_modError;
};
