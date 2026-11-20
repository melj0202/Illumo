#include <Illumo/Engine/Illumo.h>

#include "Rendering/BackendConfig.h"
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
#include <Illumo/Rendering/RenderPipelineConfig.h>
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
  return CreateOpenGLBackend(window);
}

static const char*
backendDisplayName(BackendDef api)
{
  return api == BackendDef::VULKAN ? "Vulkan" : "OpenGL";
}

Illumo::Illumo(IllumoConfig config)
  : m_applicationName(config.applicationName.empty() ? "Illumo"
                                                     : config.applicationName)
  , m_gpuCacheDirectory(config.gpuCacheDirectory)
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
    { "fps", "60" },
    { "vsync", "true" },
    { "WinX", "1280" },
    { "WinY", "720" },
    { "showFPS", "0" },
    { "showMemory", "0" },
    { "showGamepadVisuals", "0" },
    { "logLevel", "2" },
    { "fullscreen", "false" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (m_environment->getVar(defaultValue.name).value.empty()) {
      m_environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
  RenderPipelineConfig::seedDefaults(m_environment.get());
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
  m_pipelineConfigured = false;
  m_hdrFallbackWarned = false;
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
  if (!m_gpuCacheDirectory.empty()) {
    std::filesystem::path gpuCache;
    if (pathFromUtf8(m_gpuCacheDirectory, &gpuCache)) {
      (*backend)->setGpuCacheDirectory(gpuCache);
    }
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
          if (m_commandLine != nullptr) {
            m_commandLine->logSuccess(
              std::string("Display mode: ") +
              displayModeLabel(m_window->displayMode()));
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
  if (m_renderer != nullptr) {
    m_renderer->setFrameDelta(dt);
  }
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

static PooledRenderTargetDesc
makeBloomMipTargetDesc(int mipIndex)
{
  static constexpr const char* kBloomMipNames[5] = {
    "BloomMip0", "BloomMip1", "BloomMip2", "BloomMip3", "BloomMip4"
  };
  PooledRenderTargetDesc desc;
  desc.name = kBloomMipNames[std::clamp(mipIndex, 0, 4)];
  desc.windowRelative = true;
  desc.mipLevel = std::clamp(mipIndex, 0, 4) + 1;
  FramebufferAttachmentDesc color;
  color.format = TextureFormat::RGBA16F;
  color.filter = TextureFilter::Linear;
  color.wrap = TextureWrap::ClampToEdge;
  desc.colorAttachments.push_back(color);
  return desc;
}

static void
appendPhosphorFeedbackPasses(std::vector<RenderPassDesc>* worldPasses,
                             RenderStyleHandle feedbackStyle,
                             const std::string& inputColorTarget,
                             const std::string& velocityTarget,
                             const char* displayTargetName,
                             bool hasDepth,
                             bool storeLumaInAlpha,
                             bool feedbackPrimed,
                             int variant,
                             float strength,
                             float spread)
{
  FramebufferAttachmentDesc stateColor;
  // Keep temporal colour + invDepth in 16-bit float so exponential decay never
  // stalls in 8-bit quantization rounding residue.
  stateColor.format = TextureFormat::RGBA16F;
  stateColor.filter = TextureFilter::Linear;
  stateColor.wrap = TextureWrap::ClampToEdge;

  FramebufferAttachmentDesc metaColor;
  // Attachment 1 stores exact per-pixel travel direction (.xy), scalar trail
  // energy (.z), and entity foreground state (.w = 0/1/2) without filtering.
  metaColor.format = TextureFormat::RGBA16F;
  metaColor.filter = TextureFilter::Nearest;
  metaColor.wrap = TextureWrap::ClampToEdge;

  PassInputTargetBinding depthIn;
  depthIn.targetName = velocityTarget;
  depthIn.slot = 3;
  depthIn.samplerUniformName = "uDepthTexture";
  depthIn.bindDepth = true;

  RenderPassDesc accumPass;
  accumPass.name = "FeedbackAccum";
  accumPass.type = PassType::PostProcess;
  accumPass.styleHandle = feedbackStyle;
  accumPass.bindTime = true;
  accumPass.useScreenTarget = false;
  accumPass.pooledTargetName = "WorldFeedbackState";
  accumPass.targetDesc.name = "WorldFeedbackState";
  accumPass.targetDesc.windowRelative = true;
  accumPass.targetDesc.colorAttachments.push_back(stateColor);
  accumPass.targetDesc.colorAttachments.push_back(metaColor);

  PassInputTargetBinding currIn;
  currIn.targetName = inputColorTarget;
  currIn.attachmentIndex = 0;
  currIn.slot = 0;
  currIn.samplerUniformName = "uColorTexture";
  accumPass.inputTargetTextures.push_back(currIn);

  PassInputTargetBinding velIn;
  velIn.targetName = velocityTarget;
  velIn.attachmentIndex = 1;
  velIn.slot = 1;
  velIn.samplerUniformName = "uVelocityTexture";
  accumPass.inputTargetTextures.push_back(velIn);

  PassInputTargetBinding histIn;
  histIn.targetName =
    feedbackPrimed ? std::string("WorldFeedbackHistory") : inputColorTarget;
  histIn.attachmentIndex = 0;
  histIn.slot = 2;
  histIn.samplerUniformName = "uHistoryTexture";
  accumPass.inputTargetTextures.push_back(histIn);
  accumPass.inputTargetTextures.push_back(depthIn);

  PassInputTargetBinding metaHistIn;
  metaHistIn.targetName =
    feedbackPrimed ? std::string("WorldFeedbackHistory") : inputColorTarget;
  metaHistIn.attachmentIndex = feedbackPrimed ? 1 : 0;
  metaHistIn.slot = 4;
  metaHistIn.samplerUniformName = "uMetaTexture";
  accumPass.inputTargetTextures.push_back(metaHistIn);

  accumPass.uniformInts.push_back(PassUniformInt{ "uStage", 0 });
  accumPass.uniformInts.push_back(PassUniformInt{ "uVariant", variant });
  accumPass.uniformInts.push_back(
    PassUniformInt{ "uHistoryValid", feedbackPrimed ? 1 : 0 });
  accumPass.uniformInts.push_back(
    PassUniformInt{ "uHasDepth", hasDepth ? 1 : 0 });
  accumPass.uniformFloats.push_back(
    PassUniformFloat{ "uFeedbackStrength", strength });
  accumPass.uniformFloats.push_back(
    PassUniformFloat{ "uFeedbackSpread", spread });
  accumPass.uniformInts.push_back(PassUniformInt{ "uStoreLumaInAlpha", 0 });
  worldPasses->push_back(std::move(accumPass));

  RenderPassDesc storePass;
  storePass.name = "FeedbackStore";
  storePass.type = PassType::PostProcess;
  storePass.styleHandle = feedbackStyle;
  storePass.skipWithoutFeedbackTick = feedbackPrimed;
  storePass.useScreenTarget = false;
  storePass.pooledTargetName = "WorldFeedbackHistory";
  storePass.targetDesc.name = "WorldFeedbackHistory";
  storePass.targetDesc.windowRelative = true;
  storePass.targetDesc.colorAttachments.push_back(stateColor);
  storePass.targetDesc.colorAttachments.push_back(metaColor);

  PassInputTargetBinding storeColorIn;
  storeColorIn.targetName = "WorldFeedbackState";
  storeColorIn.attachmentIndex = 0;
  storeColorIn.slot = 0;
  storeColorIn.samplerUniformName = "uColorTexture";
  storePass.inputTargetTextures.push_back(storeColorIn);

  PassInputTargetBinding storeVelDummy;
  storeVelDummy.targetName = velocityTarget;
  storeVelDummy.attachmentIndex = 1;
  storeVelDummy.slot = 1;
  storeVelDummy.samplerUniformName = "uVelocityTexture";
  storePass.inputTargetTextures.push_back(storeVelDummy);

  PassInputTargetBinding storeHistDummy;
  storeHistDummy.targetName = "WorldFeedbackState";
  storeHistDummy.attachmentIndex = 0;
  storeHistDummy.slot = 2;
  storeHistDummy.samplerUniformName = "uHistoryTexture";
  storePass.inputTargetTextures.push_back(storeHistDummy);
  storePass.inputTargetTextures.push_back(depthIn);

  PassInputTargetBinding storeMetaIn;
  storeMetaIn.targetName = "WorldFeedbackState";
  storeMetaIn.attachmentIndex = 1;
  storeMetaIn.slot = 4;
  storeMetaIn.samplerUniformName = "uMetaTexture";
  storePass.inputTargetTextures.push_back(storeMetaIn);

  storePass.uniformInts.push_back(PassUniformInt{ "uStage", 1 });
  storePass.uniformInts.push_back(PassUniformInt{ "uVariant", variant });
  storePass.uniformInts.push_back(PassUniformInt{ "uHistoryValid", 0 });
  storePass.uniformInts.push_back(
    PassUniformInt{ "uHasDepth", hasDepth ? 1 : 0 });
  storePass.uniformInts.push_back(PassUniformInt{ "uStoreLumaInAlpha", 0 });
  worldPasses->push_back(std::move(storePass));

  RenderPassDesc displayPass;
  displayPass.name = "FeedbackDisplay";
  displayPass.type = PassType::PostProcess;
  displayPass.styleHandle = feedbackStyle;
  displayPass.bindTime = true;
  if (displayTargetName != nullptr) {
    displayPass.useScreenTarget = false;
    displayPass.pooledTargetName = displayTargetName;
    displayPass.targetDesc.name = displayTargetName;
    displayPass.targetDesc.windowRelative = true;
    FramebufferAttachmentDesc ldrOut;
    ldrOut.format = TextureFormat::RGBA8;
    ldrOut.filter = TextureFilter::Linear;
    ldrOut.wrap = TextureWrap::ClampToEdge;
    displayPass.targetDesc.colorAttachments.push_back(ldrOut);
  } else {
    displayPass.useScreenTarget = true;
  }

  PassInputTargetBinding dispCurrIn;
  dispCurrIn.targetName = inputColorTarget;
  dispCurrIn.attachmentIndex = 0;
  dispCurrIn.slot = 0;
  dispCurrIn.samplerUniformName = "uColorTexture";
  displayPass.inputTargetTextures.push_back(dispCurrIn);

  PassInputTargetBinding dispVelIn;
  dispVelIn.targetName = velocityTarget;
  dispVelIn.attachmentIndex = 1;
  dispVelIn.slot = 1;
  dispVelIn.samplerUniformName = "uVelocityTexture";
  displayPass.inputTargetTextures.push_back(dispVelIn);

  PassInputTargetBinding dispStateIn;
  dispStateIn.targetName = "WorldFeedbackState";
  dispStateIn.attachmentIndex = 0;
  dispStateIn.slot = 2;
  dispStateIn.samplerUniformName = "uHistoryTexture";
  displayPass.inputTargetTextures.push_back(dispStateIn);
  displayPass.inputTargetTextures.push_back(depthIn);

  PassInputTargetBinding dispMetaIn;
  dispMetaIn.targetName = "WorldFeedbackState";
  dispMetaIn.attachmentIndex = 1;
  dispMetaIn.slot = 4;
  dispMetaIn.samplerUniformName = "uMetaTexture";
  displayPass.inputTargetTextures.push_back(dispMetaIn);

  displayPass.uniformInts.push_back(PassUniformInt{ "uStage", 2 });
  displayPass.uniformInts.push_back(PassUniformInt{ "uVariant", variant });
  displayPass.uniformInts.push_back(PassUniformInt{ "uHistoryValid", 1 });
  displayPass.uniformInts.push_back(
    PassUniformInt{ "uHasDepth", hasDepth ? 1 : 0 });
  displayPass.uniformFloats.push_back(
    PassUniformFloat{ "uFeedbackStrength", strength });
  displayPass.uniformFloats.push_back(
    PassUniformFloat{ "uFeedbackSpread", spread });
  displayPass.uniformInts.push_back(
    PassUniformInt{ "uStoreLumaInAlpha", storeLumaInAlpha ? 1 : 0 });
  worldPasses->push_back(std::move(displayPass));
}

static void
appendPosterizePass(std::vector<RenderPassDesc>* worldPasses,
                    RenderStyleHandle posterizeStyle,
                    const std::string& inputColorTarget,
                    const RenderPipelineConfig& pipeline)
{
  RenderPassDesc posterizePass;
  posterizePass.name = "Posterize";
  posterizePass.type = PassType::PostProcess;
  posterizePass.useScreenTarget = true;
  posterizePass.styleHandle = posterizeStyle;

  PassInputTargetBinding colorIn;
  colorIn.targetName = inputColorTarget;
  colorIn.attachmentIndex = 0;
  colorIn.slot = 0;
  colorIn.samplerUniformName = "uColorTexture";
  posterizePass.inputTargetTextures.push_back(colorIn);

  posterizePass.uniformInts.push_back(PassUniformInt{
    "uPosterizeMode",
    std::clamp(
      pipeline.posterizeMode, 0, RenderPipelineConfig::kMaxPosterizeMode) });
  posterizePass.uniformFloats.push_back(PassUniformFloat{
    "uPosterizeLevels",
    static_cast<float>(std::clamp(pipeline.posterizeLevels, 2, 32)) });
  posterizePass.uniformFloats.push_back(PassUniformFloat{
    "uPosterizeGamma", std::clamp(pipeline.posterizeGamma, 0.4f, 2.5f) });
  posterizePass.uniformInts.push_back(PassUniformInt{
    "uPosterizeDitherMode",
    std::clamp(pipeline.posterizeDitherMode,
               0,
               RenderPipelineConfig::kMaxPosterizeDitherMode) });
  posterizePass.uniformFloats.push_back(
    PassUniformFloat{ "uPosterizeDitherAmount",
                      std::clamp(pipeline.posterizeDitherAmount, 0.0f, 1.5f) });
  posterizePass.uniformFloats.push_back(PassUniformFloat{
    "uPosterizeDitherScale",
    static_cast<float>(std::clamp(pipeline.posterizeDitherScale, 1, 4)) });
  posterizePass.uniformFloats.push_back(PassUniformFloat{
    "uPosterizeStrength", std::clamp(pipeline.posterizeStrength, 0.0f, 1.0f) });
  worldPasses->push_back(std::move(posterizePass));
}

void
Illumo::configureScenePipeline()
{
  if (!m_scene || !m_renderer || !m_environment) {
    return;
  }

  const RenderPipelineConfig pipeline =
    RenderPipelineConfig::fromEnv(m_environment.get());

  bool has3DWorld = false;
  const std::vector<DrawableBase*>& worldDrawables =
    m_scene->drawablesIn(RenderLayerId::World);
  for (size_t i = 0; i < worldDrawables.size(); ++i) {
    const DrawableBase* d = worldDrawables[i];
    if (d != nullptr && d->isVisible() && d->has3DWorldContent()) {
      has3DWorld = true;
      break;
    }
  }

  const int motionFxMode = std::clamp(
    pipeline.motionFxMode, 0, RenderPipelineConfig::kMaxMotionFxMode);

  ConfiguredPipelineMode mode = ConfiguredPipelineMode::None;
  if (pipeline.hdrEnabled && (has3DWorld || pipeline.canvasBloomEnabled)) {
    mode = ConfiguredPipelineMode::HdrForward;
  } else if (pipeline.motionBlurEnabled || pipeline.sobelEnabled ||
             motionFxMode > 0 || pipeline.posterizeEnabled) {
    mode = ConfiguredPipelineMode::LdrMotionBlur;
  }

  const std::array<int, 2> winDims =
    m_window ? m_window->getWindowDimensions()
             : std::array<int, 2>{ kDefaultWindowWidth, kDefaultWindowHeight };
  const int safeWinW = std::max(1, winDims[0]);
  const int safeWinH = std::max(1, winDims[1]);
  const bool wantsFeedback = (mode == ConfiguredPipelineMode::HdrForward ||
                              mode == ConfiguredPipelineMode::LdrMotionBlur) &&
                             motionFxMode > 0;
  if (!wantsFeedback || safeWinW != m_feedbackWidth ||
      safeWinH != m_feedbackHeight) {
    m_feedbackPrimed = false;
  }

  if (m_pipelineConfigured && m_configuredPipelineMode == mode &&
      m_configuredPipeline == pipeline &&
      (!wantsFeedback ||
       (m_configuredFeedbackPrimed == m_feedbackPrimed &&
        safeWinW == m_feedbackWidth && safeWinH == m_feedbackHeight))) {
    return;
  }

  if (mode == ConfiguredPipelineMode::HdrForward) {
    m_renderer->ensureBuiltinStyles();
    const RenderStyleHandle compositeStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::HdrComposite);
    const RenderStyleHandle downStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::BloomDownsample);
    const RenderStyleHandle upStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::BloomUpsample);
    const RenderStyleHandle fxaaStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::FxaaResolve);
    const RenderStyleHandle sobelStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::SobelEdge);
    const RenderStyleHandle feedbackStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::MotionFeedback);
    const RenderStyleHandle posterizeStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::Posterize);

    const bool runPhosphor = (motionFxMode > 0) && feedbackStyle.isValid();
    const bool runSobel = pipeline.sobelEnabled && sobelStyle.isValid();
    const bool runFxaa = pipeline.fxaaEnabled && fxaaStyle.isValid();
    const bool runPosterize =
      pipeline.posterizeEnabled && posterizeStyle.isValid();

    const bool stylesOk =
      compositeStyle.isValid() &&
      (!pipeline.bloomEnabled || (downStyle.isValid() && upStyle.isValid())) &&
      (!pipeline.fxaaEnabled || fxaaStyle.isValid()) &&
      (!pipeline.sobelEnabled || sobelStyle.isValid()) &&
      (motionFxMode == 0 || feedbackStyle.isValid()) &&
      (!pipeline.posterizeEnabled || posterizeStyle.isValid());

    PooledRenderTargetDesc hdrGeomDesc;
    hdrGeomDesc.name = "WorldHdrVelocity";
    hdrGeomDesc.windowRelative = true;
    FramebufferAttachmentDesc hdrColor;
    hdrColor.format = TextureFormat::RGBA16F;
    hdrColor.filter = TextureFilter::Linear;
    hdrColor.wrap = TextureWrap::ClampToEdge;
    hdrGeomDesc.colorAttachments.push_back(hdrColor);
    FramebufferAttachmentDesc hdrVelocity;
    hdrVelocity.format = TextureFormat::RG16F;
    hdrVelocity.filter = TextureFilter::Nearest;
    hdrVelocity.wrap = TextureWrap::ClampToEdge;
    hdrGeomDesc.colorAttachments.push_back(hdrVelocity);
    hdrGeomDesc.depthStencilFormat = TextureFormat::Depth24;

    const PooledRenderTarget probe = m_renderer->getRenderTargetPool().acquire(
      hdrGeomDesc, safeWinW, safeWinH);

    if (!stylesOk || !probe.isValid()) {
      if (!m_hdrFallbackWarned) {
        m_hdrFallbackWarned = true;
        Logger::LogWarning("Illumo: HDR render targets or styles unavailable; "
                           "falling back to LDR backbuffer");
      }
      m_pipelineConfigured = true;
      m_configuredPipelineMode = mode;
      m_configuredPipeline = pipeline;
      m_configuredFeedbackPrimed = false;
      m_scene->SetDefaultLayerPasses(RenderLayerId::World, {});
      return;
    }

    std::vector<RenderPassDesc> worldPasses;

    RenderPassDesc geomPass;
    geomPass.name = "WorldHdrGeomPass";
    geomPass.type = PassType::Draw;
    geomPass.useScreenTarget = false;
    geomPass.pooledTargetName = hdrGeomDesc.name;
    geomPass.targetDesc = hdrGeomDesc;
    geomPass.clear.clearColor = true;
    geomPass.clear.clearColorValue = { 0.01f, 0.01f, 0.01f, 1.0f };
    geomPass.clear.clearDepth = true;
    worldPasses.push_back(std::move(geomPass));

    const bool runBloom =
      pipeline.bloomEnabled && pipeline.bloomIntensity > 0.0f;
    const int mipCount = std::clamp(pipeline.bloomMipCount, 1, 5);
    if (runBloom) {
      for (int m = 0; m < mipCount; ++m) {
        const PooledRenderTargetDesc mipDesc = makeBloomMipTargetDesc(m);
        RenderPassDesc downPass;
        downPass.name = std::string("BloomDown") + std::to_string(m);
        downPass.type = PassType::PostProcess;
        downPass.useScreenTarget = false;
        downPass.pooledTargetName = mipDesc.name;
        downPass.targetDesc = mipDesc;
        downPass.styleHandle = downStyle;

        PassInputTargetBinding srcBinding;
        srcBinding.targetName = (m == 0) ? std::string("WorldHdrVelocity")
                                         : makeBloomMipTargetDesc(m - 1).name;
        srcBinding.attachmentIndex = 0;
        srcBinding.slot = 0;
        srcBinding.samplerUniformName = "uSourceTexture";
        downPass.inputTargetTextures.push_back(srcBinding);
        downPass.uniformInts.push_back(
          PassUniformInt{ "uKarisAverage", (m == 0) ? 1 : 0 });
        worldPasses.push_back(std::move(downPass));
      }

      for (int m = mipCount - 2; m >= 0; --m) {
        const PooledRenderTargetDesc dstMipDesc = makeBloomMipTargetDesc(m);
        RenderPassDesc upPass;
        upPass.name = std::string("BloomUp") + std::to_string(m);
        upPass.type = PassType::PostProcess;
        upPass.useScreenTarget = false;
        upPass.pooledTargetName = dstMipDesc.name;
        upPass.targetDesc = dstMipDesc;
        upPass.styleHandle = upStyle;

        PassInputTargetBinding srcBinding;
        srcBinding.targetName = makeBloomMipTargetDesc(m + 1).name;
        srcBinding.attachmentIndex = 0;
        srcBinding.slot = 0;
        srcBinding.samplerUniformName = "uSourceTexture";
        upPass.inputTargetTextures.push_back(srcBinding);
        upPass.uniformFloats.push_back(
          PassUniformFloat{ "uFilterRadius", 1.0f });
        worldPasses.push_back(std::move(upPass));
      }
    }

    RenderPassDesc compositePass;
    compositePass.name = "HdrComposite";
    compositePass.type = PassType::PostProcess;
    compositePass.styleHandle = compositeStyle;
    if (runSobel || runPhosphor || runFxaa || runPosterize) {
      const char* compositeTargetName =
        runSobel ? "WorldLdrPreSobel"
                 : (runPhosphor
                      ? "WorldLdrPreFx"
                      : (runFxaa ? "WorldLdrPreFxaa" : "WorldLdrPrePosterize"));
      compositePass.useScreenTarget = false;
      compositePass.pooledTargetName = compositeTargetName;
      compositePass.targetDesc.name = compositeTargetName;
      compositePass.targetDesc.windowRelative = true;
      FramebufferAttachmentDesc ldrColor;
      ldrColor.format = TextureFormat::RGBA8;
      ldrColor.filter = TextureFilter::Linear;
      ldrColor.wrap = TextureWrap::ClampToEdge;
      compositePass.targetDesc.colorAttachments.push_back(ldrColor);
    } else {
      compositePass.useScreenTarget = true;
    }

    PassInputTargetBinding hdrBinding;
    hdrBinding.targetName = "WorldHdrVelocity";
    hdrBinding.attachmentIndex = 0;
    hdrBinding.slot = 0;
    hdrBinding.samplerUniformName = "uColorTexture";
    compositePass.inputTargetTextures.push_back(hdrBinding);

    PassInputTargetBinding velBinding;
    velBinding.targetName = "WorldHdrVelocity";
    velBinding.attachmentIndex = 1;
    velBinding.slot = 1;
    velBinding.samplerUniformName = "uVelocityTexture";
    compositePass.inputTargetTextures.push_back(velBinding);

    if (runBloom) {
      PassInputTargetBinding bloomBinding;
      bloomBinding.targetName = "BloomMip0";
      bloomBinding.attachmentIndex = 0;
      bloomBinding.slot = 2;
      bloomBinding.samplerUniformName = "uBloomTexture";
      compositePass.inputTargetTextures.push_back(bloomBinding);
    }

    compositePass.uniformInts.push_back(PassUniformInt{
      "uMotionBlurEnabled", pipeline.motionBlurEnabled ? 1 : 0 });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uMotionBlurAmount", pipeline.motionBlurAmount });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uMotionBlurMax", pipeline.motionBlurMax });
    compositePass.uniformInts.push_back(
      PassUniformInt{ "uMotionBlurSamples", pipeline.motionBlurSamples });
    compositePass.uniformInts.push_back(
      PassUniformInt{ "uBloomEnabled", runBloom ? 1 : 0 });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uBloomIntensity", pipeline.bloomIntensity });
    compositePass.uniformFloats.push_back(
      PassUniformFloat{ "uExposure", pipeline.exposure });
    compositePass.uniformInts.push_back(PassUniformInt{
      "uTonemapper",
      std::clamp(
        pipeline.tonemapper, 0, RenderPipelineConfig::kMaxTonemapper) });
    compositePass.uniformInts.push_back(PassUniformInt{
      "uStoreLumaInAlpha", (!runSobel && !runPhosphor && runFxaa) ? 1 : 0 });
    worldPasses.push_back(std::move(compositePass));

    // Run Sobel before phosphor feedback so outlines stay crisp on entities and
    // world geometry without outlining individual chromatic trail cubes.
    if (runSobel) {
      RenderPassDesc sobelPass;
      sobelPass.name = "SobelEdge";
      sobelPass.type = PassType::PostProcess;
      sobelPass.styleHandle = sobelStyle;
      if (runPhosphor || runFxaa || runPosterize) {
        const char* sobelTargetName =
          runPhosphor ? "WorldLdrPreFx"
                      : (runFxaa ? "WorldLdrPreFxaa" : "WorldLdrPrePosterize");
        sobelPass.useScreenTarget = false;
        sobelPass.pooledTargetName = sobelTargetName;
        sobelPass.targetDesc.name = sobelTargetName;
        sobelPass.targetDesc.windowRelative = true;
        FramebufferAttachmentDesc sobelColor;
        sobelColor.format = TextureFormat::RGBA8;
        sobelColor.filter = TextureFilter::Linear;
        sobelColor.wrap = TextureWrap::ClampToEdge;
        sobelPass.targetDesc.colorAttachments.push_back(sobelColor);
      } else {
        sobelPass.useScreenTarget = true;
      }

      PassInputTargetBinding colorIn;
      colorIn.targetName = "WorldLdrPreSobel";
      colorIn.attachmentIndex = 0;
      colorIn.slot = 0;
      colorIn.samplerUniformName = "uColorTexture";
      sobelPass.inputTargetTextures.push_back(colorIn);

      PassInputTargetBinding depthIn;
      depthIn.targetName = "WorldHdrVelocity";
      depthIn.slot = 1;
      depthIn.samplerUniformName = "uDepthTexture";
      depthIn.bindDepth = true;
      sobelPass.inputTargetTextures.push_back(depthIn);

      sobelPass.uniformInts.push_back(
        PassUniformInt{ "uSobelMode", std::clamp(pipeline.sobelMode, 0, 1) });
      sobelPass.uniformInts.push_back(PassUniformInt{
        "uSobelSource", std::clamp(pipeline.sobelSource, 0, 2) });
      sobelPass.uniformInts.push_back(
        PassUniformInt{ "uHasDepth", has3DWorld ? 1 : 0 });
      sobelPass.uniformFloats.push_back(
        PassUniformFloat{ "uSobelStrength", pipeline.sobelStrength });
      sobelPass.uniformFloats.push_back(
        PassUniformFloat{ "uSobelThreshold", pipeline.sobelThreshold });
      sobelPass.uniformFloats.push_back(
        PassUniformFloat{ "uSobelThickness", pipeline.sobelThickness });
      sobelPass.uniformInts.push_back(PassUniformInt{
        "uStoreLumaInAlpha", (!runPhosphor && runFxaa) ? 1 : 0 });
      worldPasses.push_back(std::move(sobelPass));
    }

    if (runPhosphor) {
      const char* displayTarget =
        runFxaa ? "WorldLdrPreFxaa"
                : (runPosterize ? "WorldLdrPrePosterize" : nullptr);
      appendPhosphorFeedbackPasses(&worldPasses,
                                   feedbackStyle,
                                   "WorldLdrPreFx",
                                   "WorldHdrVelocity",
                                   displayTarget,
                                   has3DWorld,
                                   runFxaa,
                                   m_feedbackPrimed,
                                   motionFxMode,
                                   pipeline.motionFxStrength,
                                   pipeline.motionFxSpread);
    }

    if (runFxaa) {
      RenderPassDesc fxaaPass;
      fxaaPass.name = "FxaaResolve";
      fxaaPass.type = PassType::PostProcess;
      fxaaPass.styleHandle = fxaaStyle;
      if (runPosterize) {
        fxaaPass.useScreenTarget = false;
        fxaaPass.pooledTargetName = "WorldLdrPrePosterize";
        fxaaPass.targetDesc.name = "WorldLdrPrePosterize";
        fxaaPass.targetDesc.windowRelative = true;
        FramebufferAttachmentDesc fxaaOut;
        fxaaOut.format = TextureFormat::RGBA8;
        fxaaOut.filter = TextureFilter::Linear;
        fxaaOut.wrap = TextureWrap::ClampToEdge;
        fxaaPass.targetDesc.colorAttachments.push_back(fxaaOut);
      } else {
        fxaaPass.useScreenTarget = true;
      }

      PassInputTargetBinding ldrInput;
      ldrInput.targetName = "WorldLdrPreFxaa";
      ldrInput.attachmentIndex = 0;
      ldrInput.slot = 0;
      ldrInput.samplerUniformName = "uColorTexture";
      fxaaPass.inputTargetTextures.push_back(ldrInput);
      worldPasses.push_back(std::move(fxaaPass));
    }

    if (runPosterize) {
      appendPosterizePass(
        &worldPasses, posterizeStyle, "WorldLdrPrePosterize", pipeline);
    }

    m_pipelineConfigured = true;
    m_configuredPipelineMode = mode;
    m_configuredPipeline = pipeline;
    m_configuredFeedbackPrimed = m_feedbackPrimed;
    m_feedbackWidth = safeWinW;
    m_feedbackHeight = safeWinH;
    m_scene->SetDefaultLayerPasses(RenderLayerId::World,
                                   std::move(worldPasses));
    return;
  }

  if (mode == ConfiguredPipelineMode::LdrMotionBlur) {
    m_renderer->ensureBuiltinStyles();
    const RenderStyleHandle sobelStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::SobelEdge);
    const RenderStyleHandle feedbackStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::MotionFeedback);
    const RenderStyleHandle posterizeStyle =
      m_renderer->getBuiltinStyleHandle(RenderStyleId::Posterize);
    const bool runPhosphor = (motionFxMode > 0) && feedbackStyle.isValid();
    const bool runSobel = pipeline.sobelEnabled && sobelStyle.isValid();
    const bool runPosterize =
      pipeline.posterizeEnabled && posterizeStyle.isValid();
    if (!pipeline.motionBlurEnabled && !runSobel && !runPhosphor &&
        !runPosterize) {
      m_pipelineConfigured = true;
      m_configuredPipelineMode = mode;
      m_configuredPipeline = pipeline;
      m_configuredFeedbackPrimed = false;
      m_scene->SetDefaultLayerPasses(RenderLayerId::World, {});
      return;
    }

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

    std::vector<RenderPassDesc> worldPasses;
    worldPasses.push_back(std::move(geomPass));

    std::string currentLdrTarget = "WorldColorVelocity";
    if (pipeline.motionBlurEnabled) {
      RenderPassDesc postPass;
      postPass.name = "MotionBlurResolve";
      postPass.type = PassType::PostProcess;
      if (runSobel || runPhosphor || runPosterize) {
        const char* mbTargetName =
          runSobel ? "WorldLdrPreSobel"
                   : (runPhosphor ? "WorldLdrPreFx" : "WorldLdrPrePosterize");
        postPass.useScreenTarget = false;
        postPass.pooledTargetName = mbTargetName;
        postPass.targetDesc.name = mbTargetName;
        postPass.targetDesc.windowRelative = true;
        FramebufferAttachmentDesc mbColor;
        mbColor.format = TextureFormat::RGBA8;
        mbColor.filter = TextureFilter::Linear;
        mbColor.wrap = TextureWrap::ClampToEdge;
        postPass.targetDesc.colorAttachments.push_back(mbColor);
        currentLdrTarget = mbTargetName;
      } else {
        postPass.useScreenTarget = true;
      }
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

      postPass.uniformFloats.push_back(
        PassUniformFloat{ "uMotionBlurAmount", pipeline.motionBlurAmount });
      postPass.uniformFloats.push_back(
        PassUniformFloat{ "uMotionBlurMax", pipeline.motionBlurMax });
      postPass.uniformInts.push_back(
        PassUniformInt{ "uMotionBlurSamples", pipeline.motionBlurSamples });
      worldPasses.push_back(std::move(postPass));
    }

    if (runSobel) {
      RenderPassDesc sobelPass;
      sobelPass.name = "SobelEdge";
      sobelPass.type = PassType::PostProcess;
      sobelPass.styleHandle = sobelStyle;
      if (runPhosphor || runPosterize) {
        const char* sobelTargetName =
          runPhosphor ? "WorldLdrPreFx" : "WorldLdrPrePosterize";
        sobelPass.useScreenTarget = false;
        sobelPass.pooledTargetName = sobelTargetName;
        sobelPass.targetDesc.name = sobelTargetName;
        sobelPass.targetDesc.windowRelative = true;
        FramebufferAttachmentDesc sobelColor;
        sobelColor.format = TextureFormat::RGBA8;
        sobelColor.filter = TextureFilter::Linear;
        sobelColor.wrap = TextureWrap::ClampToEdge;
        sobelPass.targetDesc.colorAttachments.push_back(sobelColor);
      } else {
        sobelPass.useScreenTarget = true;
      }

      PassInputTargetBinding colorIn;
      colorIn.targetName = currentLdrTarget;
      colorIn.attachmentIndex = 0;
      colorIn.slot = 0;
      colorIn.samplerUniformName = "uColorTexture";
      sobelPass.inputTargetTextures.push_back(colorIn);

      PassInputTargetBinding depthIn;
      depthIn.targetName = "WorldColorVelocity";
      depthIn.slot = 1;
      depthIn.samplerUniformName = "uDepthTexture";
      depthIn.bindDepth = true;
      sobelPass.inputTargetTextures.push_back(depthIn);

      sobelPass.uniformInts.push_back(
        PassUniformInt{ "uSobelMode", std::clamp(pipeline.sobelMode, 0, 1) });
      sobelPass.uniformInts.push_back(PassUniformInt{
        "uSobelSource", std::clamp(pipeline.sobelSource, 0, 2) });
      sobelPass.uniformInts.push_back(
        PassUniformInt{ "uHasDepth", has3DWorld ? 1 : 0 });
      sobelPass.uniformFloats.push_back(
        PassUniformFloat{ "uSobelStrength", pipeline.sobelStrength });
      sobelPass.uniformFloats.push_back(
        PassUniformFloat{ "uSobelThreshold", pipeline.sobelThreshold });
      sobelPass.uniformFloats.push_back(
        PassUniformFloat{ "uSobelThickness", pipeline.sobelThickness });
      sobelPass.uniformInts.push_back(PassUniformInt{ "uStoreLumaInAlpha", 0 });
      worldPasses.push_back(std::move(sobelPass));
      if (runPhosphor) {
        currentLdrTarget = "WorldLdrPreFx";
      } else if (runPosterize) {
        currentLdrTarget = "WorldLdrPrePosterize";
      }
    }

    if (runPhosphor) {
      const char* displayTarget =
        runPosterize ? "WorldLdrPrePosterize" : nullptr;
      appendPhosphorFeedbackPasses(&worldPasses,
                                   feedbackStyle,
                                   currentLdrTarget,
                                   "WorldColorVelocity",
                                   displayTarget,
                                   has3DWorld,
                                   false,
                                   m_feedbackPrimed,
                                   motionFxMode,
                                   pipeline.motionFxStrength,
                                   pipeline.motionFxSpread);
      if (runPosterize) {
        currentLdrTarget = "WorldLdrPrePosterize";
      }
    }

    if (runPosterize) {
      appendPosterizePass(
        &worldPasses, posterizeStyle, currentLdrTarget, pipeline);
    }

    m_pipelineConfigured = true;
    m_configuredPipelineMode = mode;
    m_configuredPipeline = pipeline;
    m_configuredFeedbackPrimed = m_feedbackPrimed;
    m_feedbackWidth = safeWinW;
    m_feedbackHeight = safeWinH;
    m_scene->SetDefaultLayerPasses(RenderLayerId::World,
                                   std::move(worldPasses));
    return;
  }

  m_pipelineConfigured = true;
  m_configuredPipelineMode = mode;
  m_configuredPipeline = pipeline;
  m_configuredFeedbackPrimed = false;
  m_scene->SetDefaultLayerPasses(RenderLayerId::World, {});
}

DrawList*
Illumo::beginRender()
{
  if (!m_initialized) {
    return nullptr;
  }
  ILLUMO_PROFILE_ZONE("Illumo.BeginRender");
  m_frameProfiler.mark(FramePhase::ScenePreparation);
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
  configureScenePipeline();
  m_frameProfiler.mark(FramePhase::Commands);
  {
    ILLUMO_PROFILE_ZONE("Illumo.BeginFrame");
    m_renderer->BeginFrame();
  }
  {
    ILLUMO_PROFILE_ZONE("Illumo.RenderScene");
    m_renderer->RenderScene(m_scene.get(), m_camera.get());
  }
  if ((m_configuredPipelineMode == ConfiguredPipelineMode::HdrForward ||
       m_configuredPipelineMode == ConfiguredPipelineMode::LdrMotionBlur) &&
      m_configuredPipeline.motionFxMode > 0 && !m_feedbackPrimed) {
    m_feedbackPrimed = true;
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
  m_pipelineConfigured = false;
  m_hdrFallbackWarned = false;
  m_feedbackPrimed = false;
  m_configuredFeedbackPrimed = false;
  m_feedbackWidth = 0;
  m_feedbackHeight = 0;
  m_configuredPipelineMode = ConfiguredPipelineMode::None;
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
