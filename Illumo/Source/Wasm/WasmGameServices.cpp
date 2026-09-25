#include <Illumo/Platform/Clipboard.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/UiScale.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <Illumo/Wasm/WasmPanelWindows.h>
#include <IllumoGuest/Audio.h>
#include <IllumoGuest/Clipboard.h>
#include <IllumoGuest/Console.h>
#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/Display.h>
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/Protocol.h>
#include <IllumoGuest/Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

namespace {
class OsClipboard final : public WasmHostClipboard
{
public:
  std::string getText() override { return Clipboard::GetText(); }
  bool setText(const std::string& text) override
  {
    return Clipboard::SetText(text);
  }
};

class OsDialogs final : public WasmHostDialogs
{
public:
  std::string pick(bool save,
                   const std::string& description,
                   const std::string& defaultName,
                   const std::string& pattern) override
  {
    const SaveLoadDialogSpec specification{ description, defaultName, pattern };
    return save ? SaveLoad::GetSaveLocation(specification)
                : SaveLoad::GetLoadLocation(specification);
  }
};

bool
hasGrant(std::uint32_t grants, GuestCapability capability)
{
  return (grants & static_cast<std::uint32_t>(capability)) != 0;
}

// A chosen file's base name for the guest's UI, bounded at a UTF-8 boundary.
// Never a directory; empty if it cannot be expressed as UTF-8.
std::string
dialogLabel(const std::filesystem::path& chosen)
{
  const std::u8string base = chosen.filename().u8string();
  std::string label(reinterpret_cast<const char*>(base.data()), base.size());
  if (label.size() > 128) {
    std::size_t end = 128;
    while (end > 0 &&
           (static_cast<unsigned char>(label[end]) & 0xC0u) == 0x80u) {
      --end;
    }
    label.resize(end);
  }
  if (!guestUtf8(label) || label.find_first_of("\\/:") != std::string::npos) {
    label.clear();
  }
  return label;
}
} // namespace

WasmGameServices::WasmGameServices(WasmFrameRenderer& frames,
                                   std::uint32_t grants,
                                   std::filesystem::path engineAssets,
                                   std::vector<std::byte> workerModule,
                                   std::unique_ptr<WasmFileServices> files,
                                   IRenderWindow* window,
                                   IEnvVars* environment,
                                   CommandRegistry* commands,
                                   CommandLine* console,
                                   WasmHostClipboard* clipboard,
                                   WasmHostDialogs* dialogs)
  : m_render(frames, grants, std::move(engineAssets))
  , m_files(std::move(files))
  , m_window(window)
  , m_environment(environment)
  , m_commands(commands)
  , m_console(console)
  , m_clipboard(clipboard)
  , m_dialogs(dialogs)
  , m_grants(grants)
  , m_module(std::move(workerModule))
{
  if (m_clipboard == nullptr) {
    m_ownedClipboard = std::make_unique<OsClipboard>();
    m_clipboard = m_ownedClipboard.get();
  }
  if (m_dialogs == nullptr) {
    m_ownedDialogs = std::make_unique<OsDialogs>();
    m_dialogs = m_ownedDialogs.get();
  }
}
WasmGameServices::~WasmGameServices()
{
  cancel();
}
WasmLimits
WasmGameServices::defaultWorkerLimits()
{
  WasmLimits limits;
  limits.memoryBytes = 512ull * 1024ull * 1024ull;
  limits.fuelPerCall = 1000000000u;
  limits.deadlineMilliseconds = 10000u;
  return limits;
}

void
WasmGameServices::unregisterCommands()
{
  if (m_commands == nullptr) {
    return;
  }
  for (const std::string& name : m_registered) {
    m_commands->UnregisterCommand(name);
  }
  m_registered.clear();
}
void
WasmGameServices::cancel()
{
  m_cancelled = true;
  // A stopped or failed guest no longer draws its pointer.
  if (m_systemCursorHidden && m_window != nullptr) {
    m_window->setSystemCursorHidden(false);
  }
  m_systemCursorHidden = false;
  m_displayRequests.clear();
  m_clipboardRequests.clear();
  m_windowRequests.clear();
  m_dialogRequests.clear();
  m_consoleRequests.clear();
  m_listenRequests.clear();
  m_invocations.clear();
  unregisterCommands();
  releaseAudio();
  if (m_files) {
    m_files->cancel();
  }
  if (m_worker) {
    m_worker->requestStop();
  }
  m_job = {};
  m_hostJob = 0;
  for (Lane& lane : m_lanes) {
    if (lane.worker) {
      lane.worker->requestStop();
    }
    lane.job = {};
    lane.hostJob = 0;
  }
}

bool
WasmGameServices::lanesGranted() const
{
  return (m_grants & static_cast<std::uint32_t>(GuestCapability::Jobs)) != 0 &&
         !m_module.empty() && m_laneCount != 0u;
}

void
WasmGameServices::completeLaneQuery(GuestServices& results)
{
  if (m_laneQuery.request == 0) {
    return;
  }
  std::uint32_t ready = 0;
  for (const Lane& lane : m_lanes) {
    const WasmWorkerStatus status = lane.worker->status();
    if (status == WasmWorkerStatus::Loading) {
      return; // still compiling: the guest keeps its serial generations
    }
    ready += status == WasmWorkerStatus::Failed ? 0u : 1u;
  }
  GuestServiceRecord answer{ m_laneQuery.request,
                             GuestService::JobLanes,
                             GuestServiceStatus::Rejected,
                             {} };
  if (ready == m_lanes.size()) {
    GuestWireWriter count;
    count.u32(m_laneCount);
    answer.status = GuestServiceStatus::Complete;
    answer.payload = count.take();
  }
  results.records.push_back(std::move(answer));
  m_laneQuery = {};
}

void
WasmGameServices::ensureLaneWorkers()
{
  // Every lane compiles and instantiates on its own thread, in parallel.
  m_lanes.resize(m_laneCount);
  for (Lane& lane : m_lanes) {
    if (!lane.worker) {
      lane.worker = std::make_unique<WasmWorker>(
        m_module,
        m_workerLimits,
        static_cast<std::uint32_t>(GuestServices::MaximumJobBytes));
    }
  }
}
bool
WasmGameServices::completeDisplay(GuestServices& results)
{
  if (m_displayRequests.empty()) {
    return true;
  }
  const GuestServiceRecord& record = m_displayRequests.front();
  GuestServiceRecord response{
    record.request, GuestService::Display, GuestServiceStatus::Rejected, {}
  };
  if (m_window != nullptr && m_environment != nullptr &&
      hasGrant(m_grants, GuestCapability::Display)) {
    GuestDisplayRequest request;
    GuestDisplayRequest::read(record.payload, request);
    if (request.apply) {
      if (m_environment->getVar("fullscreen").valueAsBool !=
          request.state.fullscreen) {
        m_window->toggleFullscreen();
      }
      m_environment->setVar("vsync", request.state.vsync);
      m_environment->setVar("fps", request.state.fps);
      m_environment->setVar("uiScale", UiScale::text(request.state.uiScale));
      // A product drawing its own pointer hides the system cursor over the
      // main window; cancel() always gives it back. Version 1 requests do
      // not carry the field, so they leave the cursor as it is.
      if (request.version >= 2u &&
          request.state.hideSystemCursor != m_systemCursorHidden) {
        m_systemCursorHidden = request.state.hideSystemCursor;
        m_window->setSystemCursorHidden(m_systemCursorHidden);
      }
      // Saved for the next window; the running one keeps its samples.
      if (request.version >= 4u) {
        m_environment->setVar("msaa", static_cast<long>(request.state.msaa));
      }
    }
    const EnvVar& fps = m_environment->getVar("fps");
    const EnvVar& scale = m_environment->getVar("uiScale");
    const EnvVar& vsync = m_environment->getVar("vsync");
    GuestDisplayState actual;
    actual.fullscreen = m_environment->getVar("fullscreen").valueAsBool;
    actual.vsync = vsync.value.empty() || vsync.valueAsBool;
    actual.fps = static_cast<std::uint32_t>(
      std::clamp(fps.value.empty() ? 60L : fps.valueAsLong, 0L, 1000L));
    // Version 3 reports the preference (0 is automatic); earlier versions
    // only know whole factors, so they get the scale in effect right now.
    const std::array<int, 2> window = m_window->getWindowDimensions();
    const float preference = UiScale::stored(scale);
    actual.uiScale =
      request.version >= 3u && preference == 0.0f
        ? 0.0f
        : std::clamp(request.version >= 3u
                       ? preference
                       : UiScale::resolve(scale, window[0], window[1]),
                     1.0f,
                     4.0f);
    actual.hideSystemCursor = m_systemCursorHidden;
    const EnvVar& msaa = m_environment->getVar("msaa");
    const long savedMsaa = msaa.value.empty() ? 4L : msaa.valueAsLong;
    actual.msaa =
      savedMsaa >= 0L && savedMsaa <= 16L &&
          GuestDisplayState::validMsaa(static_cast<std::uint32_t>(savedMsaa))
        ? static_cast<std::uint32_t>(savedMsaa)
        : 4u;
    const int running = m_window->getMsaaSamples();
    actual.activeMsaa = running >= 0 && GuestDisplayState::validMsaa(
                                          static_cast<std::uint32_t>(running))
                          ? static_cast<std::uint32_t>(running)
                          : GuestDisplayState::kUnknownMsaa;
    // Each completion is written in its request's version, so a version 1
    // guest never sees the trailing field.
    GuestWireWriter payload;
    actual.write(payload, request.version);
    response.status = GuestServiceStatus::Complete;
    response.payload = payload.take();
  }
  results.records.push_back(std::move(response));
  m_displayRequests.pop_front();
  return true;
}
void
WasmGameServices::completeWindows(GuestServices& results)
{
  // Window requests are synchronous on the host, so each completes now.
  while (!m_windowRequests.empty()) {
    const GuestServiceRecord& record = m_windowRequests.front();
    GuestServiceRecord response{
      record.request, GuestService::Window, GuestServiceStatus::Rejected, {}
    };
    GuestWindowRequest request;
    if (m_windows != nullptr && hasGrant(m_grants, GuestCapability::Windows) &&
        GuestWindowRequest::read(record.payload, request)) {
      GuestWindowOpened opened;
      std::string reason;
      if (m_windows->handle(request, &opened, &reason)) {
        response.status = GuestServiceStatus::Complete;
        if (request.action == GuestWindowAction::Open) {
          GuestWireWriter payload;
          opened.write(payload);
          response.payload = payload.take();
        }
      }
    }
    results.records.push_back(std::move(response));
    m_windowRequests.pop_front();
  }
}
void
WasmGameServices::setAudio(IAudio* audio)
{
  if (audio != m_audio) {
    releaseAudio();
    m_audio = audio;
  }
}

void
WasmGameServices::releaseAudio()
{
  if (m_audio == nullptr) {
    m_sounds.clear();
    m_soundSamples = 0;
    return;
  }
  for (const std::pair<const std::uint32_t, AudioSound>& sound : m_sounds) {
    m_audio->destroySound(sound.second.handle);
  }
  if (!m_sounds.empty()) {
    m_audio->stopAll();
  }
  if (m_masterVolumeChanged) {
    m_audio->setMasterVolume(1.0f);
    m_masterVolumeChanged = false;
  }
  m_sounds.clear();
  m_soundSamples = 0;
}

void
WasmGameServices::completeAudio(std::uint64_t request,
                                GuestAudioRequest& audio,
                                GuestServices& results)
{
  // Unknown sound ids, a full table or budget and a missing output are all
  // rejections: the guest keeps running and nothing else changes.
  bool accepted = false;
  if (m_audio != nullptr && hasGrant(m_grants, GuestCapability::Audio)) {
    const std::map<std::uint32_t, AudioSound>::iterator found =
      m_sounds.find(audio.sound);
    switch (audio.action) {
      case GuestAudioAction::Create:
        if (found == m_sounds.end() &&
            audio.samples.size() <= kMaximumGuestSamples - m_soundSamples) {
          AudioClip clip;
          clip.channels = audio.channels;
          clip.sampleRate = audio.sampleRate;
          clip.samples = std::move(audio.samples);
          const SoundHandle handle = m_audio->createSound(clip);
          if (handle.isValid()) {
            m_sounds.emplace(audio.sound,
                             AudioSound{ handle, clip.samples.size() });
            m_soundSamples += clip.samples.size();
            accepted = true;
          }
        }
        break;
      case GuestAudioAction::Destroy:
        if (found != m_sounds.end()) {
          m_audio->destroySound(found->second.handle);
          m_soundSamples -= found->second.samples;
          m_sounds.erase(found);
          accepted = true;
        }
        break;
      case GuestAudioAction::Play:
        if (found != m_sounds.end()) {
          accepted = m_audio->play(found->second.handle,
                                   { audio.volume, audio.pan, audio.pitch });
        }
        break;
      case GuestAudioAction::StopAll:
        m_audio->stopAll();
        accepted = true;
        break;
      case GuestAudioAction::SetVolume:
        m_audio->setMasterVolume(audio.volume);
        m_masterVolumeChanged = true;
        accepted = true;
        break;
    }
  }
  results.records.push_back(
    { request,
      GuestService::Audio,
      accepted ? GuestServiceStatus::Complete : GuestServiceStatus::Rejected,
      {} });
}

bool
WasmGameServices::completeClipboard(GuestServices& results)
{
  if (m_clipboardRequests.empty()) {
    return true;
  }
  const GuestServiceRecord& record = m_clipboardRequests.front();
  GuestServiceRecord response{
    record.request, GuestService::Clipboard, GuestServiceStatus::Rejected, {}
  };
  if (m_clipboard != nullptr &&
      hasGrant(m_grants, GuestCapability::Clipboard)) {
    GuestClipboardRequest request;
    GuestClipboardRequest::read(record.payload, request);
    bool accepted = true;
    if (request.set) {
      accepted = m_clipboard->setText(request.text);
    }
    std::string text;
    if (accepted) {
      text = m_clipboard->getText();
      accepted =
        text.size() <= GuestClipboardRequest::MaximumBytes && guestUtf8(text);
    }
    if (accepted) {
      GuestWireWriter payload;
      GuestClipboardRequest{ false, std::move(text) }.write(payload);
      response.status = GuestServiceStatus::Complete;
      response.payload = payload.take();
    }
  }
  results.records.push_back(std::move(response));
  m_clipboardRequests.pop_front();
  return true;
}
bool
WasmGameServices::completeDialog(GuestServices& results)
{
  if (m_dialogRequests.empty()) {
    return true;
  }
  const GuestServiceRecord& record = m_dialogRequests.front();
  GuestServiceRecord response{
    record.request, GuestService::Dialog, GuestServiceStatus::Rejected, {}
  };
  if (m_files && m_dialogs != nullptr &&
      hasGrant(m_grants, GuestCapability::SelectedFiles)) {
    GuestDialogRequest request;
    GuestDialogRequest::read(record.payload, request);
    const std::string path = m_dialogs->pick(
      request.save, request.description, request.defaultName, request.pattern);
    GuestDialogResult result;
    if (path.empty()) {
      result.outcome = GuestFileOutcome::Cancelled;
    } else {
      // The same path conversion the grant uses; only its base name leaves
      // the host.
      const std::filesystem::path chosen(path);
      std::uint64_t size = 0;
      const bool granted =
        request.edit
          ? m_files->grantEditable(chosen, result.name, size)
          : m_files->grantSelected(chosen, request.save, result.name, size);
      if (granted) {
        result.outcome = GuestFileOutcome::Success;
        result.writing = request.save || request.edit;
        result.size = size;
        result.label = dialogLabel(chosen);
      } else {
        result.outcome = GuestFileOutcome::IoError;
      }
    }
    GuestWireWriter payload;
    result.write(payload);
    response.status = GuestServiceStatus::Complete;
    response.payload = payload.take();
  }
  results.records.push_back(std::move(response));
  m_dialogRequests.pop_front();
  return true;
}
bool
WasmGameServices::completeConsole(GuestServices& results)
{
  while (!m_consoleRequests.empty()) {
    GuestServiceRecord record = m_consoleRequests.front();
    m_consoleRequests.pop_front();
    GuestServiceRecord response{
      record.request, GuestService::Console, GuestServiceStatus::Rejected, {}
    };
    GuestConsoleRequest request;
    GuestConsoleRequest::read(record.payload, request);
    if (!hasGrant(m_grants, GuestCapability::Console)) {
      results.records.push_back(std::move(response));
      continue;
    }
    if (request.action == GuestConsoleAction::Listen) {
      m_listenRequests.push_back(std::move(record));
      continue;
    }
    if (request.action == GuestConsoleAction::Log) {
      const std::string text = request.text;
      if (m_console != nullptr) {
        if (request.level == 1) {
          m_console->logError(text);
        } else if (request.level == 2) {
          m_console->logWarning(text);
        } else if (request.level == 4) {
          m_console->logTrace(text);
        } else if (text.rfind("SUCCESS: ", 0) == 0) {
          // Guest success lines keep CommandLineCore's prefix; restore the
          // level so the host console colors and filters them as successes.
          m_console->logSuccess(text.substr(9));
        } else {
          m_console->logNormal(text);
        }
      } else if (request.level == 1) {
        Logger::LogError(text.c_str());
      } else if (request.level == 2) {
        Logger::LogWarning(text.c_str());
      } else if (request.level == 4) {
        Logger::LogTrace(text.c_str());
      } else {
        Logger::LogInfo(text.c_str());
      }
      response.status = GuestServiceStatus::Complete;
    } else if (m_commands == nullptr) {
      results.records.push_back(std::move(response));
      continue;
    } else if (request.action == GuestConsoleAction::Unregister) {
      m_commands->UnregisterCommand(request.name);
      std::vector<std::string>::iterator found =
        std::find(m_registered.begin(), m_registered.end(), request.name);
      if (found != m_registered.end()) {
        m_registered.erase(found);
      }
      response.status = GuestServiceStatus::Complete;
    } else if (m_commands->HasCommand(request.name) &&
               std::find(m_registered.begin(),
                         m_registered.end(),
                         request.name) == m_registered.end()) {
      // Host-owned commands are never replaced by a guest trampoline.
      response.status = GuestServiceStatus::Rejected;
    } else {
      const std::string commandName = request.name;
      m_commands->RegisterCommand(
        commandName,
        [this, commandName](const std::vector<std::string>& arguments) {
          if (!m_cancelled && m_invocations.size() < 32) {
            m_invocations.push_back({ commandName, arguments });
          }
        },
        request.usage,
        request.description,
        request.completions);
      if (std::find(m_registered.begin(), m_registered.end(), commandName) ==
          m_registered.end()) {
        m_registered.push_back(commandName);
      }
      response.status = GuestServiceStatus::Complete;
    }
    results.records.push_back(std::move(response));
  }
  if (!m_listenRequests.empty() && !m_invocations.empty()) {
    const GuestServiceRecord& listen = m_listenRequests.front();
    GuestConsoleRequest invocation;
    invocation.action = GuestConsoleAction::Listen;
    invocation.name = m_invocations.front().name;
    invocation.arguments = m_invocations.front().arguments;
    GuestWireWriter payload;
    invocation.write(payload);
    results.records.push_back({ listen.request,
                                GuestService::Console,
                                GuestServiceStatus::Complete,
                                payload.take() });
    m_listenRequests.pop_front();
    m_invocations.pop_front();
  }
  return true;
}
bool
WasmGameServices::process(std::span<const std::byte> requests,
                          std::vector<std::byte>& completions)
try {
  completions.clear();
  m_error.clear();
  GuestServices incoming;
  if (m_cancelled || !GuestServices::read(requests, incoming, true)) {
    m_error = "Invalid or retired service session";
    return false;
  }
  std::uint64_t last = m_lastRequest;
  std::size_t laneJobs = 0;
  for (const Lane& lane : m_lanes) {
    laneJobs += lane.job.request != 0 ? 1u : 0u;
  }
  if (incoming.records.size() + m_render.pendingRequests() +
        m_displayRequests.size() + m_clipboardRequests.size() +
        m_windowRequests.size() + m_dialogRequests.size() +
        m_consoleRequests.size() + m_listenRequests.size() +
        (m_files ? m_files->pendingRequests() : 0u) +
        (m_job.request != 0 ? 1u : 0u) + laneJobs +
        (m_laneQuery.request != 0 ? 1u : 0u) >
      GuestServices::MaximumRecords) {
    m_error = "Too many outstanding service requests";
    return false;
  }
  GuestServices rendering;
  GuestServices files;
  std::vector<std::pair<std::uint64_t, GuestAudioRequest>> audio;
  unsigned int listens = 0;
  for (const GuestServiceRecord& record : incoming.records) {
    if (record.request <= last) {
      m_error = "Replayed service request";
      return false;
    }
    last = record.request;
    if (record.operation == GuestService::Display) {
      GuestDisplayRequest request;
      if (!GuestDisplayRequest::read(record.payload, request)) {
        m_error = "Invalid display request";
        return false;
      }
    } else if (record.operation == GuestService::Clipboard) {
      GuestClipboardRequest request;
      if (!GuestClipboardRequest::read(record.payload, request)) {
        m_error = "Invalid clipboard request";
        return false;
      }
    } else if (record.operation == GuestService::Window) {
      GuestWindowRequest request;
      if (!GuestWindowRequest::read(record.payload, request)) {
        m_error = "Invalid window request";
        return false;
      }
    } else if (record.operation == GuestService::Audio) {
      GuestAudioRequest request;
      if (!GuestAudioRequest::read(record.payload, request)) {
        m_error = "Invalid audio request";
        return false;
      }
      audio.emplace_back(record.request, std::move(request));
    } else if (record.operation == GuestService::Dialog) {
      GuestDialogRequest request;
      if (!GuestDialogRequest::read(record.payload, request)) {
        m_error = "Invalid dialog request";
        return false;
      }
    } else if (record.operation == GuestService::Console) {
      GuestConsoleRequest request;
      if (!GuestConsoleRequest::read(record.payload, request)) {
        m_error = "Invalid console request";
        return false;
      }
      if (request.action == GuestConsoleAction::Listen) {
        ++listens;
      }
    } else if (record.operation == GuestService::File) {
      GuestFileRequest request;
      if (!GuestFileRequest::read(record.payload, request)) {
        m_error = "Invalid file request";
        return false;
      }
      files.records.push_back(record);
    } else if (record.operation == GuestService::JobLanes) {
      if (!record.payload.empty()) {
        m_error = "Invalid compute lane query";
        return false;
      }
    } else if (record.operation == GuestService::LaneJob) {
      // A lane index, then a non-empty opaque job.
      if (record.payload.size() <= 4u ||
          record.payload.size() > GuestServices::MaximumJobBytes) {
        m_error = "Invalid compute lane request size";
        return false;
      }
    } else if (record.operation != GuestService::Job) {
      rendering.records.push_back(record);
    } else if (record.payload.empty() ||
               record.payload.size() > GuestServices::MaximumJobBytes) {
      m_error = "Invalid compute request size";
      return false;
    }
  }
  if (listens + m_listenRequests.size() > 1) {
    m_error = "At most one console listen may be outstanding";
    return false;
  }
  GuestWireWriter renderRequests;
  rendering.write(renderRequests);
  std::vector<std::byte> renderResponses;
  if (!m_render.process(renderRequests.data(), renderResponses)) {
    m_error = m_render.error();
    return false;
  }
  GuestServices results;
  if (!GuestServices::read(renderResponses, results, false)) {
    m_error = "Invalid host resource completion";
    return false;
  }
  for (const GuestServiceRecord& record : incoming.records) {
    if (record.operation == GuestService::Display) {
      m_displayRequests.push_back(record);
    } else if (record.operation == GuestService::Clipboard) {
      m_clipboardRequests.push_back(record);
    } else if (record.operation == GuestService::Window) {
      m_windowRequests.push_back(record);
    } else if (record.operation == GuestService::Dialog) {
      m_dialogRequests.push_back(record);
    } else if (record.operation == GuestService::Console) {
      m_consoleRequests.push_back(record);
    }
  }
  completeDisplay(results);
  completeClipboard(results);
  completeWindows(results);
  completeDialog(results);
  completeConsole(results);
  // In request order, so a sound registered and played in one update plays.
  for (std::pair<std::uint64_t, GuestAudioRequest>& request : audio) {
    completeAudio(request.first, request.second, results);
  }
  // Poll before accepting another operation. Never block the control frame on
  // worker compilation, execution or a game-defined synchronization barrier.
  if (m_worker && m_job.request != 0) {
    WasmJobResult result;
    if (m_worker->poll(result)) {
      const bool accepted =
        result.error.empty() && m_hostJob != 0 &&
        result.requestId == m_hostJob &&
        result.bytes.size() <= GuestServices::MaximumJobBytes;
      results.records.push_back(
        { m_job.request,
          GuestService::Job,
          accepted ? GuestServiceStatus::Complete
                   : GuestServiceStatus::Rejected,
          accepted ? std::move(result.bytes) : std::vector<std::byte>{} });
      m_job = {};
      m_hostJob = 0;
    }
  }
  // Lane replies share one completion exchange; a reply that does not fit
  // waits in its lane for the next one.
  std::size_t laneReplyBytes = 0;
  constexpr std::size_t kLaneReplyBudget = 12u * 1024u * 1024u;
  for (Lane& lane : m_lanes) {
    WasmJobResult result;
    if (lane.worker && lane.job.request != 0 && !lane.finished &&
        lane.worker->poll(result)) {
      lane.accepted = result.error.empty() && lane.hostJob != 0 &&
                      result.requestId == lane.hostJob &&
                      result.bytes.size() <= GuestServices::MaximumJobBytes;
      lane.reply =
        lane.accepted ? std::move(result.bytes) : std::vector<std::byte>{};
      lane.finished = true;
    }
    if (!lane.finished ||
        (laneReplyBytes != 0 &&
         lane.reply.size() > kLaneReplyBudget - laneReplyBytes)) {
      continue;
    }
    laneReplyBytes += std::min(lane.reply.size(), kLaneReplyBudget);
    results.records.push_back({ lane.job.request,
                                GuestService::LaneJob,
                                lane.accepted ? GuestServiceStatus::Complete
                                              : GuestServiceStatus::Rejected,
                                std::move(lane.reply) });
    lane.job = {};
    lane.hostJob = 0;
    lane.finished = false;
    lane.accepted = false;
    lane.reply.clear();
  }
  completeLaneQuery(results);
  for (GuestServiceRecord& record : incoming.records) {
    if (record.operation == GuestService::JobLanes) {
      if (!lanesGranted() || m_laneQuery.request != 0) {
        results.records.push_back({ record.request,
                                    record.operation,
                                    GuestServiceStatus::Rejected,
                                    {} });
        continue;
      }
      // Lane stores compile in parallel; the answer waits until they can run.
      ensureLaneWorkers();
      m_laneQuery = std::move(record);
      continue;
    }
    if (record.operation == GuestService::LaneJob) {
      std::uint32_t lane = 0;
      std::memcpy(&lane, record.payload.data(), sizeof(lane)); // LE wire
      if (!lanesGranted() || lane >= m_laneCount) {
        results.records.push_back({ record.request,
                                    record.operation,
                                    GuestServiceStatus::Rejected,
                                    {} });
        continue;
      }
      ensureLaneWorkers();
      Lane& target = m_lanes[lane];
      if (target.job.request != 0 ||
          target.worker->status() == WasmWorkerStatus::Failed) {
        results.records.push_back({ record.request,
                                    record.operation,
                                    GuestServiceStatus::Rejected,
                                    {} });
        continue;
      }
      target.job = std::move(record);
      continue;
    }
    if (record.operation != GuestService::Job) {
      continue;
    }
    if ((m_grants & static_cast<std::uint32_t>(GuestCapability::Jobs)) == 0 ||
        m_module.empty() || m_job.request != 0 ||
        (m_worker && m_worker->status() == WasmWorkerStatus::Failed)) {
      results.records.push_back(
        { record.request, record.operation, GuestServiceStatus::Rejected, {} });
      continue;
    }
    if (!m_worker) {
      m_worker = std::make_unique<WasmWorker>(
        m_module,
        m_workerLimits,
        static_cast<std::uint32_t>(GuestServices::MaximumJobBytes));
    }
    m_job = std::move(record);
  }
  if (m_worker && m_job.request != 0 && m_hostJob == 0 &&
      m_worker->status() == WasmWorkerStatus::Idle) {
    if (!m_worker->submit(m_job.payload, m_hostJob)) {
      m_error = "Worker rejected its accepted request";
      return false;
    }
    m_job.payload.clear();
  }
  // Accepted lane jobs start as soon as their store is idle (it may still be
  // compiling); submission copies the job bytes after the lane prefix.
  for (Lane& lane : m_lanes) {
    if (!lane.worker || lane.job.request == 0 || lane.hostJob != 0 ||
        lane.worker->status() != WasmWorkerStatus::Idle) {
      continue;
    }
    if (!lane.worker->submit(std::span(lane.job.payload).subspan(4),
                             lane.hostJob)) {
      m_error = "A compute lane rejected its accepted request";
      return false;
    }
    lane.job.payload.clear();
  }
  if (!files.records.empty()) {
    if (m_files) {
      if (!m_files->submit(files)) {
        m_error = m_files->error();
        return false;
      }
    } else {
      for (const GuestServiceRecord& request : files.records) {
        results.records.push_back({ request.request,
                                    GuestService::File,
                                    GuestServiceStatus::Rejected,
                                    {} });
      }
    }
  }
  if (m_files) {
    GuestWireWriter measured;
    results.write(measured);
    if (measured.data().size() > GuestServices::MaximumBytes) {
      m_error = "Completion budget exceeded";
      return false;
    }
    GuestServices ready =
      m_files->poll(GuestServices::MaximumBytes - measured.data().size());
    for (GuestServiceRecord& result : ready.records) {
      results.records.push_back(std::move(result));
    }
  }
  GuestWireWriter response;
  results.write(response);
  if (results.records.size() > GuestServices::MaximumRecords ||
      response.data().size() > GuestServices::MaximumBytes) {
    m_error = "Service completion budget exceeded";
    return false;
  }
  completions = response.take();
  m_lastRequest = last;
  return true;
} catch (const std::exception& exception) {
  m_error = exception.what();
  completions.clear();
  return false;
}
