#pragma once
#include <Illumo/Audio/Audio.h>
#include <Illumo/Wasm/WasmFileServices.h>
#include <Illumo/Wasm/WasmRenderServices.h>
#include <Illumo/Wasm/WasmWorker.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

struct GuestAudioRequest;

class IAppLauncher;
class IRenderWindow;
class WasmPanelWindows;
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
  // Adds work that finished after the last process() (the Job worker's and
  // the lanes' replies) to `completions`, the exchange about to be delivered,
  // so an update sees what finished while the previous frame rendered rather
  // than a frame later. Unchanged when nothing finished. False with error()
  // only on an internal failure.
  bool harvest(std::vector<std::byte>& completions);
  void cancel();
  const std::string& error() const { return m_error; }
  // Surface windows for the Window service (Windows capability); null
  // rejects every window request.
  void setWindows(WasmPanelWindows* windows) { m_windows = windows; }
  // Sound output for the Audio service (Audio capability); null rejects
  // every audio request. Replacing it releases the guest's sounds first.
  // The output must outlive these services or be withdrawn with null.
  void setAudio(IAudio* audio);
  // Application launches for the LaunchApp service (Launch capability),
  // borrowed; null rejects every launch request. cancel() stops the launched
  // application.
  void setLauncher(IAppLauncher* launcher) { m_launcher = launcher; }
  // Most samples one guest may keep registered or arriving (128 MiB of float
  // samples: two maximal clips).
  static constexpr std::size_t kMaximumGuestSamples = 32u * 1024u * 1024u;
  std::size_t audioSounds() const { return m_sounds.size(); }
  // Sounds whose chunks are still arriving.
  std::size_t audioUploads() const { return m_uploads.size(); }
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
  void completeWindows(GuestServices& results);
  void completeLaunches(GuestServices& results);
  // Audio requests run at once; each completes Complete or Rejected.
  void completeAudio(std::uint64_t request,
                     GuestAudioRequest& audio,
                     GuestServices& results);
  // Registers a whole clip whose samples the budget already counts; a refusal
  // returns them to the budget.
  bool registerAudioClip(std::uint32_t sound, AudioClip& clip);
  // Destroys every sound this guest registered and silences its voices.
  void releaseAudio();
  WasmRenderServices m_render;
  std::unique_ptr<WasmFileServices> m_files;
  IRenderWindow* m_window;
  IEnvVars* m_environment;
  // Whether the guest asked the host to hide the system cursor (Display).
  bool m_systemCursorHidden = false;
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
  std::deque<GuestServiceRecord> m_windowRequests;
  WasmPanelWindows* m_windows = nullptr;
  std::deque<GuestServiceRecord> m_launchRequests;
  IAppLauncher* m_launcher = nullptr;
  IAudio* m_audio = nullptr;
  // Guest sound id to the output's sound, plus that sound's sample count.
  struct AudioSound
  {
    SoundHandle handle;
    std::size_t samples = 0;
  };
  std::map<std::uint32_t, AudioSound> m_sounds;
  // A sound sent in chunks, registered with the output when the last lands.
  // Its whole size is charged to the budget from its Create.
  struct AudioUpload
  {
    std::uint32_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::size_t total = 0;
    std::vector<float> samples;
  };
  std::map<std::uint32_t, AudioUpload> m_uploads;
  // Registered and arriving samples, bounded by kMaximumGuestSamples.
  std::size_t m_soundSamples = 0;
  bool m_masterVolumeChanged = false;
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
  // Moves finished Job and lane replies into `results`: lane replies up to
  // `laneReplyBudget` bytes (a lone larger one too when
  // `firstReplyAlwaysFits`) and no record past `recordLimit`; a reply that
  // does not fit waits in its lane. True when any record was added.
  bool collectFinished(GuestServices& results,
                       std::size_t laneReplyBudget,
                       std::size_t recordLimit,
                       bool firstReplyAlwaysFits);
  // Whether a Job or lane reply is ready to collect (no polling side effect).
  bool finishedWorkWaiting() const;
  // Answers a deferred JobLanes query once every lane store left Loading.
  void completeLaneQuery(GuestServices& results);
  std::uint32_t m_laneCount = 1u;
  std::vector<Lane> m_lanes;
  GuestServiceRecord m_laneQuery;
  std::uint64_t m_lastRequest = 0;
  // Per-exchange buffers, retained so steady frames reuse their capacity.
  GuestWireWriter m_renderRequests;
  std::vector<std::byte> m_renderResponses;
  GuestWireWriter m_response;
  GuestServices m_harvested;
  bool m_cancelled = false;
  std::string m_error;
};
