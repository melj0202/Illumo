#pragma once

#include <Illumo/Engine/IllumoContext.h>
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

// The one WASM program the runtime runs (D-E31): the guest store, its
// services, frame renderer and render worlds. Product update, UI and geometry
// come from a GuestProgram reactor; the host grants rendering and bounded
// messages. The runtime starts it once, updates it after the debug overlay,
// dispatches it before the overlay and asks it before the window closes.
class WasmProgram
{
public:
  explicit WasmProgram(std::vector<std::byte> module,
                       std::vector<std::byte> startup = {},
                       WasmLimits limits = {},
                       std::vector<std::byte> mod = {},
                       std::vector<std::byte> worker = {},
                       WasmFileRoots files = {});
  ~WasmProgram();
  WasmProgram(const WasmProgram&) = delete;
  WasmProgram& operator=(const WasmProgram&) = delete;
  WasmProgram(WasmProgram&&) = delete;
  WasmProgram& operator=(WasmProgram&&) = delete;

  // Budgets for compute worker instances and the number of lanes granted.
  // Applies to workers created after start; call before start.
  void setWorkerLimits(const WasmLimits& limits, std::uint32_t lanes);
  // Where surface windows (Windows capability) come from; the platform's by
  // default. Null keeps every surface docked (capture, benchmarks). Call
  // before start.
  void setSurfaceWindows(ISurfaceWindowFactory* factory)
  {
    m_surfaceWindows = factory;
  }
  // Null unless the guest was granted surface windows.
  const WasmPanelWindows* panelWindows() const { return m_windows.get(); }
  // Sound output for the guest (Audio capability), borrowed; it must outlive
  // this program. Null, the default, withholds the capability (tests,
  // capture, benchmarks). Call before start.
  void setAudio(IAudio* audio) { m_audio = audio; }
  // Whether a guest's restart request (GuestUpdateFlags::RequestRestart,
  // Display grant required) relaunches the application after it closes.
  // Off by default (tests, capture, benchmarks): the request then only
  // closes.
  void setRestartAllowed(bool allowed) { m_restartAllowed = allowed; }
  // False when the services are incomplete or the guest fails to start;
  // error() says why.
  bool start(IllumoContext& context);
  void update(double elapsed);
  void dispatch(DrawList& scene);
  void stop();
  // Whether the guest agrees to close; a failed guest always does.
  bool closeRequested();
  const std::string& error() const;
  const std::string& modError() const;
  bool hasActiveMod() const;
  const WasmFrameStats& stats() const;
  // Null before start and after stop.
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
  IllumoContext* ic = nullptr;
  WasmFileRoots m_fileRoots;
  // Published as IllumoContext::fileTree while the guest runs.
  std::unique_ptr<IFileTreeSource> m_treeSource;
  WasmGuest m_guest;
  std::unique_ptr<WasmGuest> m_mod;
  std::unique_ptr<WasmFrameRenderer> m_frames;
  std::unique_ptr<WasmGameServices> m_services;
  ISurfaceWindowFactory* m_surfaceWindows = &PlatformSurfaceWindows();
  std::unique_ptr<WasmPanelWindows> m_windows;
  IAudio* m_audio = nullptr;
  bool m_restartAllowed = false;
  std::vector<std::byte> m_completions;
  // Per-frame exchange buffers, retained so steady frames reuse capacity.
  GuestWireWriter m_input;
  std::vector<std::byte> m_response;
  std::vector<std::byte> m_queued;
  std::vector<std::byte> m_modReply;
  std::string m_error;
  std::string m_modError;
};
