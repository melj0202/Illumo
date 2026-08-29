#include <Illumo/Engine/Illumo.h>

#include "Rendering/OpenGL/CreateOpenGLBackend.h"
#include "Rendering/RenderWindow.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <exception>
#include <filesystem>
#include <glm/fwd.hpp>
#include <tracy/Tracy.hpp>
#include <utility>

static void
cleanupBackendAfterInitializationFailure(
  std::unique_ptr<IBackend>* backend) noexcept
{
  if (backend == nullptr || !*backend) {
    return;
  }
  try {
    (*backend)->Shutdown();
  } catch (const std::exception& exception) {
    try {
      Logger::LogError(
        std::string("Illumo backend cleanup failed after initialization: ") +
        exception.what());
    } catch (...) {
    }
  } catch (...) {
    try {
      Logger::LogError(
        "Illumo backend cleanup failed with an unknown error after "
        "initialization");
    } catch (...) {
    }
  }
  backend->reset();
}

Illumo::Illumo(IllumoConfig config)
  : m_applicationName(config.applicationName.empty() ? "Illumo"
                                                     : config.applicationName)
  , m_windowFactory(CreateRenderWindow)
  , m_backendFactory(CreateOpenGLBackend)
{
  const std::filesystem::path environmentPath =
    config.environmentPath.empty()
      ? EnvVars::ApplicationConfigPath()
      : std::filesystem::path(config.environmentPath);
  m_environment = std::make_unique<EnvVars>(environmentPath);
  applyHostDefaults();
}

Illumo::~Illumo()
{
  shutdown();
  Logger::setContext(nullptr, nullptr);
}

EnvVars&
Illumo::environment()
{
  return *m_environment;
}

const EnvVars&
Illumo::environment() const
{
  return *m_environment;
}

const std::string&
Illumo::applicationName() const
{
  return m_applicationName;
}

void
Illumo::applyHostDefaults()
{
  struct DefaultValue
  {
    const char* name;
    const char* value;
  };
  const DefaultValue defaults[] = {
    { "fps", "60" },           { "vsync", "true" }, { "WinX", "1280" },
    { "WinY", "720" },         { "showFPS", "0" },  { "logLevel", "2" },
    { "fullscreen", "false" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (m_environment->getVar(defaultValue.name).value.empty()) {
      m_environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}

bool
Illumo::initialize()
{
  if (m_initialized) {
    Logger::LogWarning("Illumo::initialize called more than once; ignoring");
    return true;
  }

  try {
    const int initialWindowWidth =
      static_cast<int>(m_environment->getVar("WinX").valueAsLong);
    const int initialWindowHeight =
      static_cast<int>(m_environment->getVar("WinY").valueAsLong);
    m_window = m_windowFactory(initialWindowWidth,
                               initialWindowHeight,
                               m_applicationName,
                               m_environment.get());
    if (!m_window) {
      Logger::LogError("Illumo failed to create its render window");
      releaseServices();
      return false;
    }

    m_camera = std::make_unique<Camera>(
      glm::vec2(0.0f, 0.0f), 1.0f, m_environment.get());
    std::unique_ptr<IBackend> backend = m_backendFactory(m_window.get());
    if (!backend) {
      Logger::LogError("Illumo failed to create its rendering backend");
      releaseServices();
      return false;
    }
    bool backendInitialized = false;
    try {
      backendInitialized = backend->Initialize();
    } catch (const std::exception& exception) {
      Logger::LogError(
        std::string("Illumo rendering backend threw during initialization: ") +
        exception.what());
      cleanupBackendAfterInitializationFailure(&backend);
      releaseServices();
      return false;
    } catch (...) {
      Logger::LogError(
        "Illumo rendering backend threw an unknown initialization error");
      cleanupBackendAfterInitializationFailure(&backend);
      releaseServices();
      return false;
    }
    if (!backendInitialized) {
      Logger::LogError("Illumo failed to initialize its rendering backend");
      cleanupBackendAfterInitializationFailure(&backend);
      releaseServices();
      return false;
    }
    m_renderer = std::make_unique<Renderer>(
      m_window.get(), m_environment.get(), m_camera.get(), std::move(backend));
    m_renderer->ensureBuiltinStyles();
    m_assetManager = std::make_unique<AssetManager>(m_renderer.get());
    m_commandRegistry = std::make_unique<CommandRegistry>();
    m_commandLine = std::make_unique<CommandLine>(m_environment.get(),
                                                  m_commandRegistry.get(),
                                                  m_window.get(),
                                                  m_renderer.get(),
                                                  m_applicationName);
    Logger::setContext(m_environment.get(), m_commandLine.get());
    m_inputManager =
      std::make_unique<InputManager>(m_window->getWindowInstance());
    m_scene = std::make_unique<Scene>(m_window.get(), m_camera.get());
    GLString::setRenderWindow(m_window.get());

    m_context.envVars = m_environment.get();
    m_context.window = m_window.get();
    m_context.commandLine = m_commandLine.get();
    m_context.inputManager = m_inputManager.get();
    m_context.renderer = m_renderer.get();
    m_context.assetManager = m_assetManager.get();
    m_context.camera = m_camera.get();
    m_context.commandRegistry = m_commandRegistry.get();
    m_context.scene = m_scene.get();
    m_context.moduleHost = this;
    m_initialized = true;
    return true;
  } catch (const std::exception& exception) {
    Logger::LogError(std::string("Illumo initialization failed: ") +
                     exception.what());
  } catch (...) {
    Logger::LogError("Illumo initialization failed with an unknown error");
  }
  releaseServices();
  return false;
}

void
Illumo::addModule(std::unique_ptr<IModule> module,
                  ModuleRequirement requirement)
{
  RegisteredModule registration;
  registration.module = std::move(module);
  registration.requirement = requirement;
  m_modules.push_back(std::move(registration));
}

bool
Illumo::startModules()
{
  if (!m_initialized) {
    Logger::LogError("Illumo modules cannot start before initialization");
    return false;
  }
  if (m_modulesStarted) {
    Logger::LogWarning("Illumo::startModules called more than once; ignoring");
    return true;
  }

  std::vector<RegisteredModule>::iterator registration = m_modules.begin();
  while (registration != m_modules.end()) {
    bool accepted = false;
    bool startThrew = false;
    try {
      accepted =
        registration->module && registration->module->Start(&m_context);
    } catch (const std::exception& exception) {
      Logger::LogError(std::string("An Illumo module threw during startup: ") +
                       exception.what());
      startThrew = true;
    } catch (...) {
      Logger::LogError("An Illumo module threw an unknown startup error");
      startThrew = true;
    }
    if (accepted) {
      registration->started = true;
      ++registration;
      continue;
    }
    if (startThrew) {
      stopModule(*registration, true);
    }
    const ModuleRequirement requirement = registration->requirement;
    registration = m_modules.erase(registration);
    if (requirement == ModuleRequirement::Optional) {
      Logger::LogWarning("An optional Illumo module did not start");
      continue;
    }

    Logger::LogError("A required Illumo module did not start; rolling back");
    rollbackStartedModules();
    return false;
  }

  m_modulesStarted = true;
  return true;
}

void
Illumo::RequestTransition(std::unique_ptr<IModule> nextModule)
{
  m_pendingModuleTransition = std::move(nextModule);
}

bool
Illumo::HasPendingTransition() const
{
  return m_pendingModuleTransition != nullptr;
}

void
Illumo::applyPendingModuleTransition()
{
  if (!m_pendingModuleTransition) {
    return;
  }

  std::unique_ptr<IModule> nextModule = std::move(m_pendingModuleTransition);

  for (std::vector<RegisteredModule>::iterator it = m_modules.begin();
       it != m_modules.end();) {
    if (it->requirement == ModuleRequirement::Required) {
      stopModule(*it, false);
      it = m_modules.erase(it);
    } else {
      ++it;
    }
  }

  if (m_inputManager != nullptr) {
    m_inputManager->clearKeyQueue();
    m_inputManager->clearCharQueue();
  }

  if (m_scene != nullptr) {
    m_scene->ClearDrawables();
  }

  bool accepted = false;
  try {
    accepted = nextModule && nextModule->Start(&m_context);
  } catch (const std::exception& exception) {
    Logger::LogError(
      std::string("A transitioned module threw during startup: ") +
      exception.what());
    accepted = false;
  } catch (...) {
    Logger::LogError("A transitioned module threw an unknown startup error");
    accepted = false;
  }

  if (!accepted) {
    Logger::LogError(
      "A transitioned required module failed to start; closing application");
    if (nextModule) {
      try {
        nextModule->Exit();
      } catch (...) {
      }
    }
    if (m_window != nullptr) {
      m_window->requestClose();
    }
    return;
  }

  RegisteredModule registration;
  registration.module = std::move(nextModule);
  registration.requirement = ModuleRequirement::Required;
  registration.started = true;
  m_modules.insert(m_modules.begin(), std::move(registration));
}

void
Illumo::updateStartedModules(ModuleRequirement requirement, double dt)
{
  for (RegisteredModule& registration : m_modules) {
    if (registration.started && registration.module &&
        registration.requirement == requirement) {
      registration.module->Update(dt);
    }
  }
}

void
Illumo::dispatchStartedModules(ModuleRequirement requirement)
{
  for (RegisteredModule& registration : m_modules) {
    if (registration.started && registration.module &&
        registration.requirement == requirement) {
      registration.module->DispatchDrawables(m_scene.get());
    }
  }
}

void
Illumo::update(double dt)
{
  if (!m_initialized || !m_modulesStarted) {
    return;
  }
  if (m_pendingModuleTransition != nullptr) {
    applyPendingModuleTransition();
  }
  ZoneScoped;
  m_inputManager->update();
  m_camera->Update(static_cast<float>(dt));
  // Optional overlays (DebugModule) consume global console input first.
  updateStartedModules(ModuleRequirement::Optional, dt);
  updateStartedModules(ModuleRequirement::Required, dt);
  // Key/char queues are per-frame events. Unconsumed leftovers must not
  // retrigger on the next update.
  if (m_inputManager != nullptr) {
    m_inputManager->clearKeyQueue();
    m_inputManager->clearCharQueue();
  }
}

void
Illumo::render()
{
  if (!m_initialized || !m_modulesStarted) {
    return;
  }
  ZoneScopedN("Illumo.Render");
  m_scene->ClearDrawables();
  // Product content first; optional overlays (console, FPS, demo) on top.
  dispatchStartedModules(ModuleRequirement::Required);
  dispatchStartedModules(ModuleRequirement::Optional);

  m_assetManager->pump();
  m_renderer->BeginFrame();
  {
    ZoneScopedN("Illumo.RenderScene");
    m_renderer->RenderScene(m_scene.get(), m_camera.get());
  }
  {
    ZoneScopedN("Illumo.EndFrame");
    m_renderer->EndFrame();
  }
}

void
Illumo::stopModule(RegisteredModule& registration, bool force) noexcept
{
  if ((!registration.started && !force) || !registration.module) {
    return;
  }
  registration.started = false;
  try {
    registration.module->Exit();
  } catch (const std::exception& exception) {
    try {
      Logger::LogError(std::string("An Illumo module threw during exit: ") +
                       exception.what());
    } catch (...) {
    }
  } catch (...) {
    try {
      Logger::LogError("An Illumo module threw an unknown exit error");
    } catch (...) {
    }
  }
}

void
Illumo::rollbackStartedModules() noexcept
{
  for (std::vector<RegisteredModule>::reverse_iterator it = m_modules.rbegin();
       it != m_modules.rend();
       ++it) {
    stopModule(*it, false);
  }
}

void
Illumo::shutdown() noexcept
{
  m_pendingModuleTransition.reset();
  rollbackStartedModules();
  m_modules.clear();
  m_modulesStarted = false;
  releaseServices();
}

void
Illumo::releaseServices()
{
  GLString::setRenderWindow(nullptr);
  m_scene.reset();
  m_inputManager.reset();
  Logger::setContext(m_environment.get(), nullptr);
  m_commandLine.reset();
  m_commandRegistry.reset();
  m_assetManager.reset();
  m_renderer.reset();
  m_camera.reset();
  m_window.reset();
  clearContext();
  m_initialized = false;
}

void
Illumo::clearContext()
{
  m_context = IllumoContext{};
}

IllumoContext&
Illumo::context()
{
  return m_context;
}

const IllumoContext&
Illumo::context() const
{
  return m_context;
}

bool
Illumo::shouldClose() const
{
  return !m_window || m_window->shouldWindowClose();
}
