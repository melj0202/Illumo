#pragma once
#include <Illumo/Wasm/WasmFileServices.h>
#include <Illumo/Wasm/WasmRenderServices.h>
#include <Illumo/Wasm/WasmWorker.h>
#include <deque>
#include <string>
#include <vector>

class IRenderWindow;
class IEnvVars;
class CommandRegistry;
class CommandLine;

class WasmHostClipboard
{
public:
  virtual ~WasmHostClipboard() = default;
  virtual std::string getText() = 0;
  virtual bool setText(const std::string& text) = 0;
};

class WasmHostDialogs
{
public:
  virtual ~WasmHostDialogs() = default;
  virtual std::string pick(bool save,
                           const std::string& description,
                           const std::string& defaultName,
                           const std::string& pattern) = 0;
};

// One control principal, one optional compute child. The host schedules copied
// opaque jobs; it never decodes or executes game-domain operations.
class WasmGameServices
{
public:
  WasmGameServices(WasmFrameRenderer& frames,
                   std::uint32_t grants,
                   std::filesystem::path engineAssets,
                   std::vector<std::byte> workerModule = {},
                   std::unique_ptr<WasmFileServices> files = {},
                   IRenderWindow* window = nullptr,
                   IEnvVars* environment = nullptr,
                   CommandRegistry* commands = nullptr,
                   CommandLine* console = nullptr,
                   WasmHostClipboard* clipboard = nullptr,
                   WasmHostDialogs* dialogs = nullptr);
  ~WasmGameServices();
  WasmGameServices(const WasmGameServices&) = delete;
  WasmGameServices& operator=(const WasmGameServices&) = delete;
  WasmGameServices(WasmGameServices&&) = delete;
  WasmGameServices& operator=(WasmGameServices&&) = delete;
  bool process(std::span<const std::byte> requests,
               std::vector<std::byte>& completions);
  void cancel();
  const std::string& error() const { return m_error; }
  // Budgets for compute children: the Job worker, created on its first job,
  // and `lanes` LaneJob workers, created when the guest asks for its lanes.
  static WasmLimits defaultWorkerLimits();
  void setWorkerLimits(const WasmLimits& limits, std::uint32_t lanes = 1u)
  {
    m_workerLimits = limits;
    m_laneCount = lanes;
  }

private:
  void unregisterCommands();
  bool completeDisplay(GuestServices& results);
  bool completeClipboard(GuestServices& results);
  bool completeDialog(GuestServices& results);
  bool completeConsole(GuestServices& results);
  WasmRenderServices m_render;
  std::unique_ptr<WasmFileServices> m_files;
  IRenderWindow* m_window;
  IEnvVars* m_environment;
  CommandRegistry* m_commands;
  CommandLine* m_console;
  std::unique_ptr<WasmHostClipboard> m_ownedClipboard;
  WasmHostClipboard* m_clipboard;
  std::unique_ptr<WasmHostDialogs> m_ownedDialogs;
  WasmHostDialogs* m_dialogs;
  std::deque<GuestServiceRecord> m_displayRequests;
  std::deque<GuestServiceRecord> m_clipboardRequests;
  std::deque<GuestServiceRecord> m_dialogRequests;
  std::deque<GuestServiceRecord> m_consoleRequests;
  std::deque<GuestServiceRecord> m_listenRequests;
  std::vector<std::string> m_registered;
  struct Invocation
  {
    std::string name;
    std::vector<std::string> arguments;
  };
  std::deque<Invocation> m_invocations;
  std::uint32_t m_grants;
  std::vector<std::byte> m_module;
  WasmLimits m_workerLimits = defaultWorkerLimits();
  std::unique_ptr<WasmWorker> m_worker;
  GuestServiceRecord m_job;
  std::uint64_t m_hostJob = 0;
  // Compute lanes: one isolated worker store per lane, one job in flight.
  struct Lane
  {
    std::unique_ptr<WasmWorker> worker;
    GuestServiceRecord job;
    std::uint64_t hostJob = 0;
    // A finished job whose reply waits for room in a completion exchange.
    bool finished = false;
    bool accepted = false;
    std::vector<std::byte> reply;
  };
  bool lanesGranted() const;
  void ensureLaneWorkers();
  // Answers a deferred JobLanes query once every lane store left Loading.
  void completeLaneQuery(GuestServices& results);
  std::uint32_t m_laneCount = 1u;
  std::vector<Lane> m_lanes;
  GuestServiceRecord m_laneQuery;
  std::uint64_t m_lastRequest = 0;
  bool m_cancelled = false;
  std::string m_error;
};
