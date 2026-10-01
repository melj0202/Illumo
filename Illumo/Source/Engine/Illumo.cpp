#include <Illumo/Engine/Illumo.h>

#include "Rendering/BackendConfig.h"
#include "Rendering/D3D12/CreateD3D12Backend.h"
#include "Rendering/OpenGL/CreateOpenGLBackend.h"
#include "Rendering/RenderWindow.h"
#include "Rendering/Vulkan/CreateVulkanBackend.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Platform/PathText.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/RenderPass.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <array>
#include <filesystem>
#include <glm/fwd.hpp>
#include <utility>

// The window size applyHostDefaults seeds as "WinX"/"WinY" (keep them in
// step), and the fallback for a saved size that cannot open a window.
static constexpr int kDefaultWindowWidth = 1280;
static constexpr int kDefaultWindowHeight = 720;

static void
cleanupBackendAfterInitializationFailure(
  std::unique_ptr<IBackend>* backend) noexcept
{
  if (backend == nullptr || !*backend) {
    return;
  }
  (*backend)->Shutdown();
  backend->reset();
}

static std::unique_ptr<IBackend>
createBackend(IRenderWindow* window, BackendDef api)
{
  if (api == BackendDef::VULKAN) {
    return CreateVulkanBackend(window);
  }
#ifdef _WIN32
  if (api == BackendDef::DIRECTX12) {
    return CreateD3D12Backend(window);
  }
#endif
  return CreateOpenGLBackend(window);
}

static const char*
backendDisplayName(BackendDef api)
{
  if (api == BackendDef::DIRECTX12) {
    return "Direct3D 12";
  }
  return api == BackendDef::VULKAN ? "Vulkan" : "OpenGL";
}

Illumo::Illumo(IllumoConfig config)
  : m_applicationName(config.applicationName.empty() ? "Illumo"
                                                     : config.applicationName)
  , m_windowFactory(CreateRenderWindowFor)
  , m_backendFactory(createBackend)
{
  std::filesystem::path environmentPath = EnvVars::ApplicationConfigPath();
  if (!config.environmentPath.empty() &&
      !pathFromUtf8(config.environmentPath, &environmentPath)) {
    Logger::LogWarning("The settings path is not valid UTF-8; using " +
                       pathToUtf8(environmentPath));
  }
  m_environment = std::make_unique<EnvVars>(environmentPath);
  applyHostDefaults();
  // The configured log level applies from here, before the console exists,
  // so window and GPU startup messages are not filtered by the fallback.
  Logger::setContext(m_environment.get(), nullptr);
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
    { "fps", "60" },     { "vsync", "true" },       { "WinX", "1280" },
    { "WinY", "720" },   { "showFPS", "0" },        { "showMemory", "0" },
    { "logLevel", "2" }, { "fullscreen", "false" },
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
  ILLUMO_PROFILE_ZONE("Illumo.Initialize");

  int initialWindowWidth =
    static_cast<int>(m_environment->getVar("WinX").valueAsLong);
  int initialWindowHeight =
    static_cast<int>(m_environment->getVar("WinY").valueAsLong);
  // A settings file saved with a minimized window (0x0) or edited by hand
  // must not stop the window from opening.
  if (initialWindowWidth <= 0 || initialWindowHeight <= 0) {
    Logger::LogWarning("Ignoring the saved window size " +
                       std::to_string(initialWindowWidth) + "x" +
                       std::to_string(initialWindowHeight) + "; using " +
                       std::to_string(kDefaultWindowWidth) + "x" +
                       std::to_string(kDefaultWindowHeight));
    initialWindowWidth = kDefaultWindowWidth;
    initialWindowHeight = kDefaultWindowHeight;
    m_environment->setVar("WinX", initialWindowWidth);
    m_environment->setVar("WinY", initialWindowHeight);
  }
  BackendDef requestedApi = BackendDef::OPENGL;
  const std::string& requestedName = m_environment->getVar("GraphicsAPI").value;
  if (!requestedName.empty() &&
      !parseBackendDef(requestedName, &requestedApi)) {
    Logger::LogWarning("GraphicsAPI '" + requestedName +
                       "' names no rendering backend; using OpenGL");
  } else if (!isBackendImplemented(requestedApi)) {
    Logger::LogWarning("GraphicsAPI " + TokenToString(requestedApi) +
                       " is not available in this build; using OpenGL");
    requestedApi = BackendDef::OPENGL;
  }
  m_camera =
    std::make_unique<Camera>(glm::vec2(0.0f, 0.0f), 1.0f, m_environment.get());
  std::unique_ptr<IBackend> backend;
  bool started = startGraphics(
    requestedApi, initialWindowWidth, initialWindowHeight, &backend);
  if (!started && requestedApi != BackendDef::OPENGL) {
    // The preference stays saved, so a later launch tries it again.
    Logger::LogWarning(std::string(backendDisplayName(requestedApi)) +
                       " could not start; falling back to OpenGL");
    started = startGraphics(
      BackendDef::OPENGL, initialWindowWidth, initialWindowHeight, &backend);
  }
  if (!started) {
    releaseServices();
    return false;
  }
  {
    ILLUMO_PROFILE_ZONE("Illumo.CreateRenderer");
    m_renderer = std::make_unique<Renderer>(
      m_window.get(), m_environment.get(), m_camera.get(), std::move(backend));
    m_renderer->ensureBuiltinStyles();
  }
  Logger::LogTrace("Renderer ready with built-in styles");
  m_assetManager = std::make_unique<AssetManager>(m_renderer.get());
  Logger::LogTrace("Asset manager ready");
  m_commandRegistry = std::make_unique<CommandRegistry>();
  m_commandLine = std::make_unique<CommandLine>(m_environment.get(),
                                                m_commandRegistry.get(),
                                                m_window.get(),
                                                m_renderer.get(),
                                                m_applicationName);
  // Attaching the console replays everything logged so far into it.
  Logger::setContext(m_environment.get(), m_commandLine.get());
  Logger::LogTrace("Developer console attached");
  m_inputManager =
    std::make_unique<InputManager>(m_window->getWindowInstance());
  Logger::LogTrace("Input manager ready");
  m_scene = std::make_unique<DrawList>(m_window.get(), m_camera.get());
  m_motionBlurPipelineConfigured = false;
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
  m_context.frameProfiler = &m_frameProfiler;
  m_initialized = true;
  Logger::LogInfo("Engine services initialized (renderer, assets, console, "
                  "input, scene)");
  return true;
}

bool
Illumo::startGraphics(BackendDef api,
                      int width,
                      int height,
                      std::unique_ptr<IBackend>* backend)
{
  {
    ILLUMO_PROFILE_ZONE("Illumo.CreateWindow");
    m_window = m_windowFactory(
      width, height, m_applicationName, m_environment.get(), api);
  }
  if (!m_window) {
    Logger::LogError(std::string("Illumo failed to create its render window "
                                 "for ") +
                     backendDisplayName(api));
    return false;
  }
  const std::array<int, 2> windowSize = m_window->getWindowDimensions();
  Logger::LogInfo("Render window ready: " + std::to_string(windowSize[0]) +
                  "x" + std::to_string(windowSize[1]) + ", monitor " +
                  std::to_string(m_window->getRefreshRate()) + " Hz");
  *backend = m_backendFactory(m_window.get(), api);
  if (!*backend) {
    Logger::LogError(std::string("Illumo failed to create its ") +
                     backendDisplayName(api) + " rendering backend");
    m_window.reset();
    return false;
  }
  bool backendInitialized = false;
  {
    ILLUMO_PROFILE_ZONE("Illumo.InitializeBackend");
    backendInitialized = (*backend)->Initialize();
  }
  if (!backendInitialized) {
    Logger::LogError(std::string("Illumo failed to initialize its ") +
                     backendDisplayName(api) + " rendering backend");
    cleanupBackendAfterInitializationFailure(backend);
    m_window.reset();
    return false;
  }
  Logger::LogInfo(std::string("Rendering backend: ") + backendDisplayName(api));
  return true;
}

void
Illumo::processGlobalHotkeys()
{
  if (m_inputManager == nullptr) {
    return;
  }

  // When console is open, do not intercept hotkeys so text editing is
  // unimpeded.
  if (m_commandLine != nullptr && m_commandLine->isOpen) {
    return;
  }

  std::queue<InputManager::KeyPressEvent>& keyQueue =
    m_inputManager->getKeyQueue();
  std::queue<InputManager::KeyPressEvent> remainingKeys;

  while (!keyQueue.empty()) {
    InputManager::KeyPressEvent event = keyQueue.front();
    keyQueue.pop();

    if (event.action == InputAction::Press) {
      if (event.key == KeyCode::F11) {
        if (m_window != nullptr) {
          m_window->toggleFullscreen();
          if (m_environment != nullptr) {
            const bool current =
              m_environment->getVar("fullscreen").valueAsBool;
            if (m_commandLine != nullptr) {
              m_commandLine->logSuccess(std::string("Fullscreen: ") +
                                        (current ? "on" : "off"));
            }
          }
        }
        continue;
      }
      if (event.key == KeyCode::F3) {
        if (m_environment != nullptr) {
          const bool current = m_environment->getVar("showFPS").valueAsBool;
          m_environment->setVar("showFPS", !current);
          if (m_commandLine != nullptr) {
            m_commandLine->logSuccess(std::string("FPS overlay: ") +
                                      (!current ? "on" : "off"));
          }
        }
        continue;
      }
      if (event.key == KeyCode::F5) {
        if (m_assetManager != nullptr) {
          const size_t queued = m_assetManager->reloadAll();
          if (m_commandLine != nullptr) {
            m_commandLine->logNormal("Asset reloads queued: " +
                                     std::to_string(queued));
          }
        }
        continue;
      }
    }

    remainingKeys.push(event);
  }

  keyQueue.swap(remainingKeys);
}

void
Illumo::beginUpdate(double dt)
{
  if (!m_initialized) {
    return;
  }
  ILLUMO_PROFILE_ZONE("Illumo.BeginUpdate");
  m_frameProfiler.mark(FramePhase::Input);
  m_inputManager->update();
  processGlobalHotkeys();
  m_frameProfiler.mark(FramePhase::Camera);
  m_camera->Update(static_cast<float>(dt));
}

void
Illumo::endUpdate()
{
  if (!m_initialized) {
    return;
  }
  m_frameProfiler.mark(FramePhase::Other);
  // Key/char queues are per-frame events. Unconsumed leftovers must not
  // retrigger on the next update.
  m_inputManager->clearKeyQueue();
  m_inputManager->clearCharQueue();
}

void
Illumo::configureScenePipeline()
{
  if (!m_scene || !m_renderer || !m_environment) {
    return;
  }

  const EnvVar& mbVar = m_environment->getVar("motionBlurEnabled");
  const bool motionBlur = !mbVar.value.empty() && mbVar.valueAsBool;

  if (motionBlur) {
    float amount = 0.5f;
    float maxVel = 0.2f;
    int samples = 8;

    const EnvVar& amountVar = m_environment->getVar("motionBlurAmount");
    if (!amountVar.value.empty()) {
      amount = static_cast<float>(amountVar.valueAsDouble);
    }
    const EnvVar& maxVar = m_environment->getVar("motionBlurMax");
    if (!maxVar.value.empty()) {
      maxVel = static_cast<float>(maxVar.valueAsDouble);
    }
    const EnvVar& samplesVar = m_environment->getVar("motionBlurSamples");
    if (!samplesVar.value.empty()) {
      samples = static_cast<int>(samplesVar.valueAsLong);
    }

    if (m_motionBlurPipelineConfigured && m_configuredBlurAmount == amount &&
        m_configuredBlurMax == maxVel && m_configuredBlurSamples == samples) {
      return;
    }

    m_motionBlurPipelineConfigured = true;
    m_configuredBlurAmount = amount;
    m_configuredBlurMax = maxVel;
    m_configuredBlurSamples = samples;

    RenderPassDesc geomPass;
    geomPass.name = "WorldGeomPass";
    geomPass.type = PassType::Draw;
    geomPass.useScreenTarget = false;
    geomPass.pooledTargetName = "WorldColorVelocity";
    geomPass.targetDesc.name = "WorldColorVelocity";
    geomPass.targetDesc.windowRelative = true;

    FramebufferAttachmentDesc color0;
    color0.format = TextureFormat::RGBA8;
    geomPass.targetDesc.colorAttachments.push_back(color0);

    FramebufferAttachmentDesc color1;
    color1.format = TextureFormat::RG16F;
    geomPass.targetDesc.colorAttachments.push_back(color1);

    geomPass.targetDesc.depthStencilFormat = TextureFormat::Depth24;
    geomPass.clear.clearColor = true;
    geomPass.clear.clearColorValue = { 0.1f, 0.1f, 0.1f, 1.0f };
    geomPass.clear.clearDepth = true;

    RenderPassDesc postPass;
    postPass.name = "MotionBlurResolve";
    postPass.type = PassType::PostProcess;
    postPass.useScreenTarget = true;
    postPass.styleHandle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::MotionBlur);

    PassInputTargetBinding colorBinding;
    colorBinding.targetName = "WorldColorVelocity";
    colorBinding.attachmentIndex = 0;
    colorBinding.slot = 0;
    colorBinding.samplerUniformName = "uColorTexture";
    postPass.inputTargetTextures.push_back(colorBinding);

    PassInputTargetBinding velBinding;
    velBinding.targetName = "WorldColorVelocity";
    velBinding.attachmentIndex = 1;
    velBinding.slot = 1;
    velBinding.samplerUniformName = "uVelocityTexture";
    postPass.inputTargetTextures.push_back(velBinding);

    PassUniformFloat blurAmount;
    blurAmount.name = "uMotionBlurAmount";
    blurAmount.value = amount;
    postPass.uniformFloats.push_back(blurAmount);

    PassUniformFloat blurMax;
    blurMax.name = "uMotionBlurMax";
    blurMax.value = maxVel;
    postPass.uniformFloats.push_back(blurMax);

    PassUniformInt blurSamples;
    blurSamples.name = "uMotionBlurSamples";
    blurSamples.value = samples;
    postPass.uniformInts.push_back(blurSamples);

    std::vector<RenderPassDesc> worldPasses;
    worldPasses.push_back(std::move(geomPass));
    worldPasses.push_back(std::move(postPass));
    m_scene->SetDefaultLayerPasses(RenderLayerId::World,
                                   std::move(worldPasses));
  } else {
    m_motionBlurPipelineConfigured = false;
    m_scene->SetDefaultLayerPasses(RenderLayerId::World, {});
  }
}

DrawList*
Illumo::beginRender()
{
  if (!m_initialized) {
    return nullptr;
  }
  ILLUMO_PROFILE_ZONE("Illumo.BeginRender");
  m_frameProfiler.mark(FramePhase::ScenePreparation);
  configureScenePipeline();
  m_scene->ClearDrawables();
  return m_scene.get();
}

void
Illumo::endRender()
{
  if (!m_initialized) {
    return;
  }
  ILLUMO_PROFILE_ZONE("Illumo.Render");
  m_frameProfiler.mark(FramePhase::Assets);
  {
    ILLUMO_PROFILE_ZONE("Illumo.AssetPump");
    m_assetManager->pump();
  }
  m_frameProfiler.mark(FramePhase::Commands);
  {
    ILLUMO_PROFILE_ZONE("Illumo.BeginFrame");
    m_renderer->BeginFrame();
  }
  {
    ILLUMO_PROFILE_ZONE("Illumo.RenderScene");
    m_renderer->RenderScene(m_scene.get(), m_camera.get());
  }
  {
    ILLUMO_PROFILE_ZONE("Illumo.EndFrame");
    if (m_frameProfiler.recording()) {
      FrameProfiler::TimePoint presentationStart;
      m_renderer->EndFrame(&presentationStart);
      m_frameProfiler.mark(FramePhase::Presentation, presentationStart);
    } else {
      m_renderer->EndFrame();
    }
  }
  m_frameProfiler.mark(FramePhase::Other);
}

void
Illumo::shutdown() noexcept
{
  const bool wasRunning = m_initialized;
  releaseServices();
  if (wasRunning) {
    Logger::LogTrace("Engine services released");
  }
}

void
Illumo::releaseServices()
{
  ILLUMO_PROFILE_ZONE("Illumo.ReleaseServices");
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

void
Illumo::deferClose()
{
  if (m_window) {
    m_window->cancelCloseRequest();
    m_window->clearRestartRequest();
  }
}
