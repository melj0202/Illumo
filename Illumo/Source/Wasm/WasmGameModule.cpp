#include "WasmInputMapping.h"
#include <Illumo/Content/VfsConsole.h>
#include <Illumo/Content/VfsTreeSource.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmGameModule.h>
#include <IllumoGuest/Input.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <tracy/Tracy.hpp>

// Reaches the log file and, once, the console: directly when the logger does
// not feed this console (tests, early startup), otherwise through the logger.
static void
reportError(CommandLine* console, const std::string& text)
{
  if (console != nullptr && Logger::getCommandLine() != console) {
    console->logError(text);
  }
  Logger::LogError(text);
}

static std::string
describeCapabilities(std::uint32_t granted)
{
  const std::pair<GuestCapability, const char*> names[] = {
    { GuestCapability::Render, "render" },
    { GuestCapability::Assets, "assets" },
    { GuestCapability::Storage, "storage" },
    { GuestCapability::SelectedFiles, "selected files" },
    { GuestCapability::Clipboard, "clipboard" },
    { GuestCapability::Console, "console" },
    { GuestCapability::Display, "display" },
    { GuestCapability::Jobs, "compute lanes" },
    { GuestCapability::Messages, "messages" },
    { GuestCapability::ProjectFiles, "project files" },
    { GuestCapability::Windows, "panel windows" },
    { GuestCapability::Audio, "audio" }
  };
  std::string text;
  for (const std::pair<GuestCapability, const char*>& name : names) {
    if ((granted & static_cast<std::uint32_t>(name.first)) != 0) {
      text += text.empty() ? name.second : std::string(", ") + name.second;
    }
  }
  return text.empty() ? std::string("none") : text;
}

static double
millisecondsSince(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(
           std::chrono::steady_clock::now() - start)
    .count();
}

static GuestInput
snapshot(IllumoContext& context, double elapsed)
{
  GuestInput input;
  input.elapsed = elapsed;
  const std::array<int, 2> dimensions = context.window->getWindowDimensions();
  input.width = static_cast<std::uint32_t>(std::clamp(dimensions[0], 1, 65536));
  input.height =
    static_cast<std::uint32_t>(std::clamp(dimensions[1], 1, 65536));
  const std::array<double, 2> mouse = context.window->getMouseCoords();
  input.mouseX = mouse[0];
  input.mouseY = mouse[1];
  input.consoleOpen =
    context.commandLine != nullptr && context.commandLine->isOpen;
  InputManager& manager = *context.inputManager;
  input.scroll = *manager.getMouseScrollOffset();
  *manager.getMouseScrollOffset() = 0;
  input.modifiers = (manager.isShiftPressed() ? 1u : 0u) |
                    (manager.isControlPressed() ? 2u : 0u) |
                    (manager.isAltPressed() ? 4u : 0u);
#define ILLUMO_GUEST_KEY(name, number)                                         \
  input.keys[number] = wasmGuestAction(manager.frameAction(KeyCode::name));
#include <IllumoGuest/Keys.inc>
#undef ILLUMO_GUEST_KEY
  std::queue<InputManager::KeyPressEvent>& keys = manager.getKeyQueue();
  while (!keys.empty() && input.events.size() < GuestInput::MaximumEvents) {
    const InputManager::KeyPressEvent event = keys.front();
    keys.pop();
    const GuestKey key = wasmGuestKey(event.key);
    if (key != GuestKey::Count) {
      input.events.push_back(
        { key,
          wasmGuestAction(event.action),
          static_cast<std::uint32_t>(event.modifiers) & 15u });
    }
  }
  std::queue<unsigned int>& characters = manager.getCharQueue();
  while (!characters.empty() &&
         input.characters.size() < GuestInput::MaximumEvents) {
    const std::uint32_t character = characters.front();
    characters.pop();
    if (character <= 0x10ffff &&
        !(character >= 0xd800 && character <= 0xdfff)) {
      input.characters.push_back(character);
    }
  }
  return input;
}

WasmGameModule::WasmGameModule(std::vector<std::byte> module,
                               std::vector<std::byte> startup,
                               WasmLimits limits,
                               std::vector<std::byte> mod,
                               std::vector<std::byte> worker,
                               WasmFileRoots files)
  : m_module(std::move(module))
  , m_startup(std::move(startup))
  , m_modModule(std::move(mod))
  , m_workerModule(std::move(worker))
  , m_fileRoots(std::move(files))
  , m_guest(limits)
{
}
WasmGameModule::~WasmGameModule() = default;

void
WasmGameModule::setWorkerLimits(const WasmLimits& limits, std::uint32_t lanes)
{
  m_workerLimits = limits;
  m_workerLanes = lanes == 0u ? 1u : lanes;
}

bool
WasmGameModule::Start(IllumoContext* context)
try {
  if (context == nullptr || context->renderer == nullptr ||
      context->window == nullptr || context->inputManager == nullptr) {
    m_error = "WASM host services are incomplete";
    return false;
  }
  ic = context;
  if (context->commandRegistry != nullptr) {
    // Registered before the guest starts, so it stays host-owned.
    context->commandRegistry->RegisterCommand(
      "wasm_stats",
      [this](const std::vector<std::string>&) {
        if (ic != nullptr && ic->commandLine != nullptr) {
          ic->commandLine->logNormal(describeStats());
        }
      },
      "wasm_stats",
      "Show WASM host exchange timings and frame payload counters");
    if (m_fileRoots.packages) {
      // The virtual file tree belongs to the host; guests never see it here.
      context->commandRegistry->RegisterCommand(
        "vfs",
        [this](const std::vector<std::string>& arguments) {
          if (ic == nullptr || ic->commandLine == nullptr) {
            return;
          }
          for (const std::string& line :
               VfsConsole::run(*m_fileRoots.packages, arguments)) {
            ic->commandLine->logNormal(line);
          }
        },
        "vfs mounts | ls <path> | tree <path> [depth] | stat <path> | cat "
        "<path> [bytes]",
        "Explore the mounted packages' virtual file tree");
    }
  }
  std::vector<std::byte> response;
  if (!m_guest.start(
        m_module,
        GuestRole::Game,
        static_cast<std::uint32_t>(GuestCapability::Render) |
          static_cast<std::uint32_t>(GuestCapability::Assets) |
          static_cast<std::uint32_t>(GuestCapability::Clipboard) |
          (context->envVars
             ? static_cast<std::uint32_t>(GuestCapability::Display)
             : 0u) |
          (context->commandRegistry
             ? static_cast<std::uint32_t>(GuestCapability::Console)
             : 0u) |
          (m_fileRoots.storage.empty()
             ? 0u
             : static_cast<std::uint32_t>(GuestCapability::Storage)) |
          (m_fileRoots.package.empty() && m_fileRoots.storage.empty() &&
               !m_fileRoots.packages
             ? 0u
             : static_cast<std::uint32_t>(GuestCapability::SelectedFiles)) |
          (m_workerModule.empty()
             ? 0u
             : static_cast<std::uint32_t>(GuestCapability::Jobs)) |
          (m_fileRoots.packages &&
               m_fileRoots.packages->table()->find("/project") != nullptr
             ? static_cast<std::uint32_t>(GuestCapability::ProjectFiles)
             : 0u) |
          (m_surfaceWindows != nullptr && m_surfaceWindows->available()
             ? static_cast<std::uint32_t>(GuestCapability::Windows)
             : 0u) |
          (m_audio != nullptr && m_audio->available()
             ? static_cast<std::uint32_t>(GuestCapability::Audio)
             : 0u) |
          static_cast<std::uint32_t>(GuestCapability::Messages),
        m_startup,
        response)) {
    fail(m_guest.error());
    return false;
  }
  GuestWireReader started(response);
  if (started.u32() != 1 || !started.finished()) {
    fail("Guest startup rejected");
    return false;
  }
  Logger::LogInfo("WASM guest instantiated; capabilities granted: " +
                  describeCapabilities(m_guest.capabilities()));
  m_frames =
    std::make_unique<WasmFrameRenderer>(*ic->renderer, m_guest.session());
  std::unique_ptr<WasmFileServices> files;
  if (m_fileRoots.packages) {
    files = std::make_unique<WasmFileServices>(m_guest.session(),
                                               m_guest.capabilities(),
                                               m_fileRoots.packages,
                                               m_fileRoots.storage);
  } else if (!m_fileRoots.package.empty() || !m_fileRoots.storage.empty()) {
    files = std::make_unique<WasmFileServices>(m_guest.session(),
                                               m_guest.capabilities(),
                                               m_fileRoots.package,
                                               m_fileRoots.storage);
    std::uint64_t size = 0;
    if (!m_fileRoots.launch.empty() &&
        !files->grantLaunch(
          m_fileRoots.launch, m_fileRoots.launchEditable, size)) {
      fail("The launch document is missing, unreadable or too large");
      return false;
    }
  }
  m_services = std::make_unique<WasmGameServices>(
    *m_frames,
    m_guest.capabilities(),
    EnvVars::ApplicationConfigPath().parent_path() / "Assets",
    std::move(m_workerModule),
    std::move(files),
    ic->window,
    ic->envVars,
    ic->commandRegistry,
    ic->commandLine);
  m_services->setWorkerLimits(m_workerLimits, m_workerLanes);
  if ((m_guest.capabilities() &
       static_cast<std::uint32_t>(GuestCapability::Windows)) != 0) {
    m_windows = std::make_unique<WasmPanelWindows>(
      *ic->renderer, *ic->window, *m_surfaceWindows);
    m_services->setWindows(m_windows.get());
  }
  if ((m_guest.capabilities() &
       static_cast<std::uint32_t>(GuestCapability::Audio)) != 0) {
    m_services->setAudio(m_audio);
  }
  GuestWireWriter emptyServices;
  GuestServices{}.write(emptyServices);
  m_completions = emptyServices.take();
  if (!m_modModule.empty()) {
    try {
      // Mods stay fuel-metered whatever the package requests.
      WasmLimits modLimits;
      modLimits.meterFuel = true;
      modLimits.fuelPerCall = 1000000;
      modLimits.deadlineMilliseconds = 25;
      m_mod = std::make_unique<WasmGuest>(modLimits, 65536 + 256);
      if (!m_mod->start(m_modModule,
                        GuestRole::Mod,
                        static_cast<std::uint32_t>(GuestCapability::Messages),
                        {},
                        response)) {
        m_modError = m_mod->error();
        m_mod.reset();
      } else {
        GuestWireReader modStarted(response);
        if (modStarted.u32() != 1 || !modStarted.finished() ||
            m_mod->descriptor().extensionApi !=
              m_guest.descriptor().extensionApi) {
          m_modError = "Mod startup or game API compatibility rejected";
          m_mod->retire(m_modError);
          m_mod.reset();
        }
      }
    } catch (const std::exception& exception) {
      m_modError = exception.what();
      m_mod.reset();
    }
    if (!m_modError.empty()) {
      reportError(ic->commandLine, "Mod disabled: " + m_modError);
    } else {
      Logger::LogInfo("Mod reactor started");
    }
    m_modModule.clear();
    m_modModule.shrink_to_fit();
  }
  m_module.clear();
  m_module.shrink_to_fit();
  m_startup.clear();
  Update(0);
  if (m_guest.isAlive() && m_fileRoots.packages) {
    // Engine tools (the debug `files` browser) see the same tree as `vfs`.
    m_treeSource = std::make_unique<VfsTreeSource>(m_fileRoots.packages);
    context->fileTree = m_treeSource.get();
  }
  return m_guest.isAlive();
} catch (const std::exception& exception) {
  fail(exception.what());
  return false;
}

void
WasmGameModule::fail(std::string error)
{
  if (m_services) {
    m_services->cancel();
    m_services->setWindows(nullptr);
  }
  if (m_windows) {
    m_windows->closeAll();
  }
  m_error = std::move(error);
  m_guest.retire(m_error);
  if (m_mod) {
    m_mod->retire("Parent game retired");
    m_mod.reset();
  }
  if (m_frames) {
    m_frames->retire();
  }
  if (ic != nullptr) {
    reportError(ic->commandLine, "WASM: " + m_error);
    if (ic->window != nullptr) {
      ic->window->requestClose();
    }
  }
}

void
WasmGameModule::Update(double elapsed)
try {
  if (!m_guest.isAlive()) {
    return;
  }
  ZoneScopedN("Wasm.Update");
  const std::chrono::steady_clock::time_point updateStart =
    std::chrono::steady_clock::now();
  // Surfaces of the frame drawn last time replay into their windows now,
  // after its texture writes and before the next frame replaces them.
  if (m_windows) {
    ZoneScopedN("Wasm.SurfaceWindows");
    m_windows->present(*m_frames);
  }
  GuestInput snapshotInput = snapshot(*ic, elapsed);
  if (m_windows) {
    m_windows->collectInput(snapshotInput);
  }
  GuestWireWriter input;
  snapshotInput.write(input);
  std::vector<std::byte> response;
  std::chrono::steady_clock::time_point stageStart = updateStart;
  {
    ZoneScopedN("Wasm.Services");
    if (!m_guest.invoke(GuestCall::Services, m_completions, response)) {
      fail(m_guest.error());
      return;
    }
    if (!m_services->process(response, m_completions)) {
      fail(m_services->error());
      return;
    }
  }
  m_stats.servicesMilliseconds.add(millisecondsSince(stageStart));
  stageStart = std::chrono::steady_clock::now();
  {
    ZoneScopedN("Wasm.GuestUpdate");
    if (!m_guest.invoke(GuestCall::Update, input.data(), response)) {
      fail(m_guest.error());
      return;
    }
  }
  m_stats.updateMilliseconds.add(millisecondsSince(stageStart));
  GuestWireReader updated(response);
  const std::uint32_t flags = updated.u32();
  const std::uint32_t messageBytes = updated.u32();
  const std::span<const std::byte> message = updated.bytes(messageBytes);
  if ((flags & ~GuestUpdateFlags::Known) != 0 || messageBytes > 65536 ||
      !updated.finished()) {
    fail("Unsupported guest update response");
    return;
  }
  if ((flags & GuestUpdateFlags::RequestClose) != 0) {
    // The engine still asks the guest through OnCloseRequested/Close.
    ic->window->requestClose();
  }
  if ((flags & GuestUpdateFlags::ServicesPending) != 0) {
    // Requests queued by this update (compute lane jobs especially) start
    // now, while this frame renders, instead of at the next frame.
    ZoneScopedN("Wasm.ServicesPending");
    const std::chrono::steady_clock::time_point pendingStart =
      std::chrono::steady_clock::now();
    std::vector<std::byte> queued;
    if (!m_guest.invoke(GuestCall::Services, m_completions, queued)) {
      fail(m_guest.error());
      return;
    }
    if (!m_services->process(queued, m_completions)) {
      fail(m_services->error());
      return;
    }
    m_stats.servicesMilliseconds.add(millisecondsSince(pendingStart));
  }
  // Separate completed calls, never a recursive cross-store import. The game
  // validates the opaque reply before changing its own state or geometry.
  stageStart = std::chrono::steady_clock::now();
  if (m_mod && !message.empty()) {
    ZoneScopedN("Wasm.Receive");
    std::vector<std::byte> modReply;
    if (!m_mod->invoke(GuestCall::Receive, message, modReply) ||
        modReply.size() > 65536) {
      m_modError =
        m_mod->error().empty() ? "Mod response exceeds quota" : m_mod->error();
      m_mod->retire(m_modError);
      m_mod.reset();
      reportError(ic->commandLine, "Mod disabled: " + m_modError);
    } else if (!m_guest.invoke(GuestCall::Receive, modReply, response)) {
      fail(m_guest.error());
      return;
    }
  }
  m_stats.receiveMilliseconds.add(millisecondsSince(stageStart));
  stageStart = std::chrono::steady_clock::now();
  {
    ZoneScopedN("Wasm.GuestFrame");
    if (!m_guest.invoke(GuestCall::Frame, {}, response)) {
      fail(m_guest.error());
      return;
    }
  }
  m_stats.frameMilliseconds.add(millisecondsSince(stageStart));
  m_stats.frameBytes.add(static_cast<double>(response.size()));
  stageStart = std::chrono::steady_clock::now();
  {
    ZoneScopedN("Wasm.FrameAccept");
    if (!m_frames->accept(response)) {
      fail(m_frames->error());
      return;
    }
  }
  m_stats.acceptMilliseconds.add(millisecondsSince(stageStart));
  m_stats.totalMilliseconds.add(millisecondsSince(updateStart));
  ++m_stats.updates;
} catch (const std::exception& exception) {
  fail(exception.what());
}

void
WasmGameModule::DispatchDrawables(Scene* scene)
{
  if (scene != nullptr && m_frames) {
    m_frames->dispatch(*scene);
  }
}

bool
WasmGameModule::OnCloseRequested()
{
  if (!m_guest.isAlive()) {
    return true;
  }
  std::vector<std::byte> response;
  if (!m_guest.invoke(GuestCall::Close, {}, response)) {
    fail(m_guest.error());
    return true;
  }
  GuestWireReader answer(response);
  const std::uint32_t close = answer.u32();
  if (!answer.finished() || close > 1) {
    fail("Invalid guest close response");
    return true;
  }
  return close == 1;
}

void
WasmGameModule::Exit()
{
  if (m_guest.isAlive()) {
    Logger::LogTrace("Shutting down the WASM guest after " +
                     std::to_string(m_stats.updates) + " updates");
  }
  m_guest.shutdown();
  if (m_mod) {
    m_mod->shutdown();
    m_mod.reset();
  }
  if (m_frames) {
    m_services.reset();
    m_windows.reset();
    m_completions.clear();
    m_frames->retire();
    m_frames.reset();
  }
  if (ic != nullptr && ic->commandRegistry != nullptr) {
    ic->commandRegistry->UnregisterCommand("wasm_stats");
    ic->commandRegistry->UnregisterCommand("vfs");
  }
  if (ic != nullptr && m_treeSource && ic->fileTree == m_treeSource.get()) {
    ic->fileTree = nullptr;
  }
  m_treeSource.reset();
  ic = nullptr;
}

const WasmFrameStats&
WasmGameModule::stats() const
{
  return m_stats;
}

const WasmFrameCounters*
WasmGameModule::frameCounters() const
{
  return m_frames ? &m_frames->counters() : nullptr;
}

std::string
WasmGameModule::describeStats() const
{
  const std::pair<const char*, const RollingMetric*> rows[] = {
    { "total", &m_stats.totalMilliseconds },
    { "services", &m_stats.servicesMilliseconds },
    { "update", &m_stats.updateMilliseconds },
    { "receive", &m_stats.receiveMilliseconds },
    { "frame", &m_stats.frameMilliseconds },
    { "accept", &m_stats.acceptMilliseconds }
  };
  std::string text = "WASM host ms (p50/p95/max over " +
                     std::to_string(m_stats.totalMilliseconds.size()) +
                     " updates):";
  char line[128] = {};
  for (const std::pair<const char*, const RollingMetric*>& row : rows) {
    std::snprintf(line,
                  sizeof(line),
                  "\n  %-9s %8.3f %8.3f %8.3f",
                  row.first,
                  row.second->median(),
                  row.second->p95(),
                  row.second->maximum());
    text += line;
  }
  std::snprintf(line,
                sizeof(line),
                "\n  frame bytes p50 %.0f p95 %.0f",
                m_stats.frameBytes.median(),
                m_stats.frameBytes.p95());
  text += line;
  const WasmFrameCounters* counters = frameCounters();
  if (counters != nullptr) {
    std::snprintf(
      line,
      sizeof(line),
      "\n  batches %llu (retained %llu) inline vtx %llu idx %llu bytes",
      static_cast<unsigned long long>(counters->batches),
      static_cast<unsigned long long>(counters->retainedBatches),
      static_cast<unsigned long long>(counters->inlineVertexBytes),
      static_cast<unsigned long long>(counters->inlineIndexBytes));
    text += line;
    std::snprintf(line,
                  sizeof(line),
                  "\n  texture writes %llu (%llu bytes) mesh writes %llu bytes"
                  "\n  mesh enroll %llu replace %llu",
                  static_cast<unsigned long long>(counters->textureWrites),
                  static_cast<unsigned long long>(counters->textureWriteBytes),
                  static_cast<unsigned long long>(counters->meshWriteBytes),
                  static_cast<unsigned long long>(counters->meshEnrollments),
                  static_cast<unsigned long long>(counters->meshReplacements));
    text += line;
  }
  return text;
}
const std::string&
WasmGameModule::error() const
{
  return m_error;
}
const std::string&
WasmGameModule::modError() const
{
  return m_modError;
}
bool
WasmGameModule::hasActiveMod() const
{
  return m_mod && m_mod->isAlive();
}
