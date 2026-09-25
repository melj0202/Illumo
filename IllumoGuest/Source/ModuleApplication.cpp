#include <Illumo/Rendering/GLString.h>
#include <Illumo/Services/Logger.h>
#include <IllumoGuest/InputProvider.h>
#include <IllumoGuest/ModuleApplication.h>
#include <nlohmann/json.hpp>
#include <stdexcept>

static std::string
stripPrefix(const std::string& text, const char* prefix)
{
  const std::string marker(prefix);
  return text.compare(0, marker.size(), marker) == 0
           ? text.substr(marker.size())
           : text;
}

// Truncates at a UTF-8 boundary so the host's validation never traps us.
static std::string
boundedText(const std::string& text, std::size_t maximum)
{
  if (text.size() <= maximum) {
    return text;
  }
  std::size_t end = maximum;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u) {
    --end;
  }
  return text.substr(0, end);
}

GuestCommandLine::GuestCommandLine(IEnvVars* settings,
                                   CommandRegistry* builtins,
                                   IRenderWindow* window,
                                   GuestConsole& console,
                                   const std::string& applicationName)
  : CommandLine(settings, builtins, window, nullptr, applicationName)
  , m_console(console)
{
}

void
GuestCommandLine::onHistoryAppended(const historyBuffer& item, bool erasedFront)
{
  (void)erasedFront;
  forward(item);
}

void
GuestCommandLine::onHistoryBackUpdated()
{
  if (!getHistory().empty()) {
    forward(getHistory().back());
  }
}

void
GuestCommandLine::forward(const historyBuffer& item)
{
  // The host re-applies its own prefixes for errors, warnings and traces;
  // success lines keep theirs, which the host maps back to its success level.
  std::uint32_t level = 3;
  std::string text = item.content;
  if (item.level == ConsoleLevel::Error) {
    level = 1;
    text = stripPrefix(text, "ERROR: ");
  } else if (item.level == ConsoleLevel::Warning) {
    level = 2;
    text = stripPrefix(text, "WARNING: ");
  } else if (item.level == ConsoleLevel::Trace) {
    level = 4;
    text = stripPrefix(text, "TRACE: ");
  }
  try {
    m_console.log(level, boundedText(text, 4096));
  } catch (const std::invalid_argument&) {
    // Malformed UTF-8 is dropped rather than retiring the whole store.
  }
}

GuestModuleApplication::GuestModuleApplication(std::string applicationName,
                                               std::string settingsPath)
  : m_applicationName(std::move(applicationName))
  , m_diagnostics(services())
  , m_files(services())
  , m_settings(m_files, std::move(settingsPath))
  , m_display(services())
  , m_console(services())
  , m_backend(services())
  , m_camera(glm::vec2(0.0f, 0.0f), 1.0f, &m_settings)
  , m_renderer(&m_window, &m_settings, &m_camera, &m_backend, false)
  , m_assetCache(m_files)
  , m_assets(&m_renderer, false, &m_assetCache)
  , m_fonts(services(), m_backend)
  , m_input(nullptr)
  , m_scene(&m_window, &m_camera)
  , m_panels(services(), m_window, m_camera)
  , m_audio(services())
  , m_commandLine(&m_settings,
                  &m_consoleBuiltins,
                  &m_window,
                  m_console,
                  m_applicationName)
{
  m_backend.setRenderer(m_renderer);
  m_context.scene = &m_scene;
  m_context.window = &m_window;
  m_context.commandLine = &m_commandLine;
  m_context.inputManager = &m_input;
  m_context.renderer = &m_renderer;
  m_context.envVars = &m_settings;
  m_context.camera = &m_camera;
  m_context.commandRegistry = &m_commands;
  m_context.moduleHost = this;
  m_context.assetManager = &m_assets;
}

GuestModuleApplication::~GuestModuleApplication() = default;

bool
GuestModuleApplication::acceptStartup(std::span<const std::byte> startup)
{
  if (startup.empty()) {
    return true;
  }
  GuestLaunch launch;
  if (!GuestLaunch::read(startup, launch)) {
    return false;
  }
  m_launch = std::move(launch);
  return true;
}

void
GuestModuleApplication::applyDefaults(IEnvVars& settings)
{
  (void)settings;
}

std::vector<std::string>
GuestModuleApplication::packageAssets() const
{
  return {};
}

bool
GuestModuleApplication::applyPackagedDefaults()
{
  if (m_defaultsApplied) {
    return true;
  }
  if (m_defaultsTask == 0) {
    m_defaultsTask =
      m_files.read(GuestFileArea::Package, "envvars.json", 1024u * 1024u);
    return false;
  }
  GuestFileResult result;
  if (!m_files.take(m_defaultsTask, result)) {
    return false;
  }
  m_defaultsTask = 0;
  m_defaultsApplied = true;
  if (result.outcome != GuestFileOutcome::Success) {
    if (result.outcome != GuestFileOutcome::NotFound) {
      Logger::LogWarning("Packaged envvars.json was not readable (outcome " +
                         std::to_string(static_cast<int>(result.outcome)) +
                         "); first-run defaults skipped");
    }
    return true; // packages without first-run defaults are valid
  }
  const nlohmann::json defaults = nlohmann::json::parse(
    reinterpret_cast<const char*>(result.bytes.data()),
    reinterpret_cast<const char*>(result.bytes.data()) + result.bytes.size(),
    nullptr,
    false);
  if (!defaults.is_object()) {
    Logger::LogWarning("Packaged envvars.json is not a JSON object");
    return true;
  }
  std::size_t applied = 0;
  for (nlohmann::json::const_iterator it = defaults.begin();
       it != defaults.end();
       ++it) {
    if (it.value().is_string() && m_settings.getVar(it.key()).value.empty()) {
      m_settings.setVar(it.key(), it.value().get<std::string>());
      ++applied;
    }
  }
  Logger::LogTrace("Applied " + std::to_string(applied) +
                   " first-run defaults from packaged envvars.json");
  return true;
}

bool
GuestModuleApplication::bootstrap()
{
  return true;
}

void
GuestModuleApplication::pumpProduct()
{
}

void
GuestModuleApplication::updateOverlay(double elapsed)
{
  (void)elapsed;
}

void
GuestModuleApplication::dispatchOverlay(Scene& scene)
{
  (void)scene;
}

bool
GuestModuleApplication::start(std::span<const std::byte> startup)
{
  if (!acceptStartup(startup)) {
    return false;
  }
  m_renderer.ensureBuiltinStyles();
  GLString::setRenderWindow(&m_window);
  m_panels.setGranted(granted(GuestCapability::Windows));
  m_context.panelSurfaces =
    granted(GuestCapability::Windows) ? &m_panels : nullptr;
  m_audio.setGranted(granted(GuestCapability::Audio));
  m_context.audio = granted(GuestCapability::Audio) ? &m_audio : nullptr;
  Font::getDefaultFont();
  m_settings.load();
  return true;
}

void
GuestModuleApplication::update(const GuestInput& input)
{
  m_window.accept(input);
  m_panels.accept(input);
  // Camera and settings-driven layout read the window size from settings.
  m_settings.setVar("WinX", static_cast<int>(input.width));
  m_settings.setVar("WinY", static_cast<int>(input.height));
  GuestInputProvider::accept(m_input, input);
  m_commandLine.isOpen = input.consoleOpen;
  m_files.pump();
  m_settings.pump();
  m_fonts.pump();
  m_console.pump();
  m_panels.pump();
  pumpProduct();
  if (m_phase == Phase::Settings) {
    if (!m_settings.loaded() || !applyPackagedDefaults()) {
      return;
    }
    if (!m_settings.error().empty()) {
      Logger::LogWarning("Settings unavailable: " + m_settings.error());
    }
    applyDefaults(m_settings);
    m_window.bindDisplay(m_display, m_settings);
    m_phase = Phase::Bootstrap;
  }
  if (m_phase == Phase::Bootstrap) {
    if (!m_assetsRequested) {
      const std::vector<std::string> assets = packageAssets();
      if (!assets.empty()) {
        Logger::LogTrace("Preloading " + std::to_string(assets.size()) +
                         " package asset(s)");
      }
      m_assetCache.preload(assets);
      m_assetsRequested = true;
    }
    if (!m_assetCache.ready() || !bootstrap()) {
      return;
    }
    m_phase = Phase::Running;
    startModule(createFirstModule());
  }
  if (m_phase != Phase::Running) {
    return;
  }
  applyPendingTransition();
  runConsoleInvocations();
  m_camera.Update(static_cast<float>(input.elapsed));
  if (m_module) {
    m_module->Update(input.elapsed);
  }
  updateOverlay(input.elapsed);
  // Key and character queues are per-frame events, as in the native loop.
  m_input.clearKeyQueue();
  m_input.clearCharQueue();
  synchronizeCommands();
  m_window.synchronizeDisplay();
}

GuestFrame
GuestModuleApplication::frame()
{
  const std::array<int, 2> dimensions = m_window.getWindowDimensions();
  try {
    m_renderer.BeginFrame();
    m_backend.setFrame(static_cast<float>(dimensions[0]),
                       static_cast<float>(dimensions[1]));
    m_backend.pump();
    m_scene.ClearDrawables();
    m_panels.clearScenes();
    if (m_module) {
      m_module->DispatchDrawables(&m_scene);
    }
    if (m_phase == Phase::Running) {
      dispatchOverlay(m_scene);
    }
    // As the native host: queued asset loads complete before submission.
    m_assets.pump();
    m_renderer.RenderScene(&m_scene, &m_camera);
    // Detached panels record after the main scene, into the frame's
    // surfaces section.
    m_panels.record(m_renderer, m_backend, m_window);
    m_renderer.EndFrame();
    if (!m_renderer.frameError().empty()) {
      throw std::runtime_error(m_renderer.frameError());
    }
    GuestFrame recorded = m_backend.takeFrame();
    m_panels.finish(recorded);
    m_lastFrameError.clear();
    return recorded;
  } catch (const std::runtime_error& error) {
    // A rejected recording drops this frame only; product state is intact.
    // Report each distinct failure once so a persistent fault stays visible.
    if (m_lastFrameError != error.what()) {
      m_lastFrameError = error.what();
      Logger::LogError("Frame dropped: " + m_lastFrameError);
    }
    GuestFrame empty;
    empty.width = static_cast<float>(dimensions[0]);
    empty.height = static_cast<float>(dimensions[1]);
    return empty;
  }
}

bool
GuestModuleApplication::close()
{
  // A product-initiated close already ran its own confirmation.
  if (!m_module || m_window.shouldWindowClose()) {
    return true;
  }
  return m_module->OnCloseRequested();
}

bool
GuestModuleApplication::closeRequested()
{
  return m_window.shouldWindowClose();
}

void
GuestModuleApplication::shutdown()
{
  m_pending.reset();
  if (m_module) {
    m_module->Exit();
    m_module.reset();
  }
  m_scene.ClearDrawables();
  m_phase = Phase::Stopped;
}

void
GuestModuleApplication::RequestTransition(std::unique_ptr<IModule> nextModule)
{
  if (nextModule == nullptr || m_pending != nullptr) {
    Logger::LogWarning("Module transition rejected: empty request or "
                       "transition already pending");
    return;
  }
  m_pending = std::move(nextModule);
}

bool
GuestModuleApplication::HasPendingTransition() const
{
  return m_pending != nullptr;
}

void
GuestModuleApplication::startModule(std::unique_ptr<IModule> module)
{
  m_input.clearKeyQueue();
  m_input.clearCharQueue();
  m_scene.ResetDefaultPasses();
  m_scene.ClearDrawables();
  bool accepted = false;
  try {
    accepted = module && module->Start(&m_context);
  } catch (const std::exception& exception) {
    Logger::LogError(std::string("A module threw during startup: ") +
                     exception.what());
    if (module) {
      module->Exit();
    }
  }
  if (!accepted) {
    // Mirror the native host: a failed required module closes the product.
    Logger::LogError("The product module failed to start; closing");
    m_module.reset();
    m_window.requestClose();
    return;
  }
  m_module = std::move(module);
}

void
GuestModuleApplication::applyPendingTransition()
{
  if (!m_pending) {
    return;
  }
  std::unique_ptr<IModule> next = std::move(m_pending);
  Logger::LogTrace("Switching to the next module");
  if (m_module) {
    m_module->Exit();
    m_module.reset();
  }
  startModule(std::move(next));
}

void
GuestModuleApplication::runConsoleInvocations()
{
  GuestConsoleRequest invocation;
  while (m_console.take(invocation)) {
    m_commands.QueueCommand(invocation.name, invocation.arguments);
  }
  m_commands.ExecuteQueue();
}

void
GuestModuleApplication::synchronizeCommands()
{
  const std::vector<std::string> names = m_commands.GetCommandNames();
  const std::set<std::string> current(names.begin(), names.end());
  for (std::set<std::string>::iterator it = m_forwarded.begin();
       it != m_forwarded.end();) {
    if (current.contains(*it)) {
      ++it;
      continue;
    }
    m_console.remove(*it);
    it = m_forwarded.erase(it);
  }
  for (const std::string& name : names) {
    if (m_forwarded.contains(name) || !GuestConsoleRequest::validName(name)) {
      continue;
    }
    std::vector<std::string> completions;
    for (const std::string& completion :
         m_commands.GetCommandCompletions(name)) {
      if (completions.size() == 64) {
        break;
      }
      completions.push_back(boundedText(completion, 64));
    }
    try {
      m_console.add(name,
                    boundedText(m_commands.GetCommandUsage(name), 256),
                    boundedText(m_commands.GetCommandDescription(name), 512),
                    std::move(completions));
      m_forwarded.insert(name);
    } catch (const std::invalid_argument&) {
      Logger::LogWarning("Console command not forwarded: " + name);
      m_forwarded.insert(name);
    }
  }
}
